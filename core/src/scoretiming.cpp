#include "zub/core/scoretiming.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <system_error>
#include <zub/core/midi.hpp>
#include <zub/core/score.hpp>

namespace fs = std::filesystem;

namespace zub::scoretiming {

namespace {

// Reads a decimal integer at `i` (after blanks); false when there is none.
bool readInt(std::string_view line, std::size_t& i, long long& value) {
    while (i < line.size() && line[i] == ' ')
        ++i;
    const auto [end, ec] = std::from_chars(line.data() + i, line.data() + line.size(), value);
    if (ec != std::errc{} || end == line.data() + i)
        return false;
    i = static_cast<std::size_t>(end - line.data());
    return true;
}

// `text` with CR LF as LF, like the editor's text.
std::string withoutCr(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] != '\r' || i + 1 >= text.size() || text[i + 1] != '\n')
            out += text[i];
    return out;
}

// Byte offsets of the line starts, to find offsets without scanning from the top each time.
struct Lines {
    std::string_view text;
    std::vector<std::size_t> starts{0};
    explicit Lines(std::string_view t) : text(t) {
        for (std::size_t i = 0; i < t.size(); ++i)
            if (t[i] == '\n')
                starts.push_back(i + 1);
    }
    std::optional<std::size_t> offsetOf(int line, int column) const {
        if (line < 1 || static_cast<std::size_t>(line) > starts.size())
            return std::nullopt;
        const std::size_t from = starts[static_cast<std::size_t>(line) - 1];
        const auto inLine = scoreregion::offsetOf(text.substr(from), 1, column);
        return inLine ? std::optional<std::size_t>(from + *inLine) : std::nullopt;
    }
};

using Events = std::vector<scoreregion::Range>;

// The events of `all` that lie within [begin, end).
Events within(const Events& all, std::size_t begin, std::size_t end) {
    Events out;
    for (const auto& e : all)
        if (e.begin >= begin && e.end <= end)
            out.push_back(e);
    return out;
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || (c & 0x80) != 0;
}

// The text of a chord name or a lyric syllable that begins at `at`: up to the next blank, but a
// quoted string whole and not past a closing brace.
std::size_t tokenEnd(std::string_view text, std::size_t at) {
    std::size_t i = at;
    if (i < text.size() && text[i] == '"') {
        for (++i; i < text.size() && text[i] != '"'; ++i)
            if (text[i] == '\\')
                ++i;
        return std::min(i + 1, text.size());
    }
    while (i < text.size() && !isSpace(text[i]) && text[i] != '}')
        ++i;
    return std::max(i, std::min(at + 1, text.size()));
}

// The text from `at` to the next blank, the word-like places where it occurs in `text`.
std::vector<std::size_t> occurrences(std::string_view text, std::string_view word) {
    std::vector<std::size_t> found;
    for (std::size_t at = text.find(word); at != std::string_view::npos;
         at = text.find(word, at + 1)) {
        const bool startsWord = at == 0 || !isWordChar(text[at - 1]);
        const bool endsWord = at + word.size() >= text.size() || isSpace(text[at + word.size()]);
        if (startsWord && endsWord)
            found.push_back(at);
    }
    return found;
}

} // namespace

std::vector<Note> parseNotes(std::string_view text) {
    std::vector<Note> notes;
    std::size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        // "<kind> <moment: 4 numbers> <line> <column> <length: 2 numbers> <file>", or, in the
        // first lists, "<moment: 4 numbers> <line> <column> <file>".
        Kind kind = Kind::Note;
        std::size_t i = 0;
        bool withLength = false;
        if (!line.empty() && line[0] >= 'a' && line[0] <= 'z') {
            switch (line[0]) {
            case 'n':
                kind = Kind::Note;
                break;
            case 'c':
                kind = Kind::ChordName;
                break;
            case 'm':
                kind = Kind::Melody;
                break;
            case 'l':
                kind = Kind::Lyric;
                break;
            default:
                continue;
            }
            withLength = true;
            i = 1;
        }
        long long v[8];
        const std::size_t count = withLength ? 8 : 6;
        bool ok = true;
        for (std::size_t k = 0; k < count; ++k)
            ok = ok && readInt(line, i, v[k]);
        if (!ok || v[1] <= 0 || v[3] <= 0 || (withLength && v[7] <= 0) || i >= line.size() ||
            line[i] != ' ')
            continue;
        Note note;
        note.kind = kind;
        note.whole = static_cast<double>(v[0]) / static_cast<double>(v[1]) +
                     static_cast<double>(v[2]) / static_cast<double>(v[3]);
        note.line = static_cast<int>(v[4]);
        note.column = static_cast<int>(v[5]);
        if (withLength)
            note.length = static_cast<double>(v[6]) / static_cast<double>(v[7]);
        note.file = std::string(line.substr(i + 1));
        notes.push_back(std::move(note));
    }
    return notes;
}

std::expected<Timeline, std::string> buildTimeline(const Source& source) {
    const auto tempo = midi::tempoMap(source.midi);
    if (!tempo)
        return std::unexpected(tempo.error());
    const std::string copy = withoutCr(source.copy);
    if (!copy.starts_with(source.original))
        return std::unexpected("the score changed since it was built");
    const auto body = score::firstScoreBody(source.original);
    if (!body)
        return std::unexpected("no \\score block");
    const Events originalEvents = scoreregion::musicEvents(source.original);
    const Events blockEvents = within(originalEvents, body->first, body->second);

    const Lines copyLines(copy);
    // The playback score's own copy of the first block's music: its events are the block's events
    // again, in the same order (the generated text adds none).
    Events generated;
    std::string_view generatedText; // the playback score's copy of the block's music
    const std::size_t originalEnd = source.original.size();
    const auto generatedAt = source.generatedFromLine > 0
                                 ? copyLines.offsetOf(source.generatedFromLine, 0)
                                 : std::nullopt;
    if (generatedAt) {
        constexpr std::string_view music = "\\unfoldRepeats {\n";
        constexpr std::string_view ampBook = "\\bookOutputName \"zub-playback-amp\"";
        const auto from = copy.find(music, *generatedAt);
        if (from == std::string::npos)
            return std::unexpected("the playback score is missing");
        const auto to = std::min(copy.find(ampBook, from), copy.size());
        generated = within(scoreregion::musicEvents(copy), from + music.size(), to);
        if (generated.size() != blockEvents.size())
            return std::unexpected("the playback score's notes do not match the score's");
        generatedText =
            std::string_view(copy).substr(from + music.size(), to - from - music.size());
    }

    const fs::path copyFile = source.copyFile.lexically_normal();
    Timeline timeline;
    timeline.firstBegin = !blockEvents.empty()      ? blockEvents.front().begin
                          : !originalEvents.empty() ? originalEvents.front().begin
                                                    : 0;
    const std::string_view blockText =
        source.original.substr(body->first, body->second - body->first);
    const std::size_t generatedBegin =
        generatedText.empty() ? copy.size()
                              : static_cast<std::size_t>(generatedText.data() - copy.data());
    // A chord name or a lyric syllable of the playback score's copy, as the same word of the
    // block: the n-th occurrence of its text there is the n-th here (the generated text changes
    // notes and tempi, not these); none when the two texts do not have as many of them.
    const auto blockWord = [&](std::size_t at) -> std::optional<std::size_t> {
        const std::size_t end = tokenEnd(copy, at);
        std::string_view word = std::string_view(copy).substr(at, end - at);
        // Up to the next blank (a quoted string may hold some): the occurrences are of that.
        for (std::size_t i = 0; i < word.size(); ++i)
            if (isSpace(word[i])) {
                word = word.substr(0, i);
                break;
            }
        const auto inGenerated = occurrences(generatedText, word);
        const auto inBlock = occurrences(blockText, word);
        const auto index = std::ranges::find(inGenerated, at - generatedBegin);
        if (index == inGenerated.end() || inGenerated.size() != inBlock.size())
            return std::nullopt;
        return body->first + inBlock[static_cast<std::size_t>(index - inGenerated.begin())];
    };

    struct Raw {
        double start;
        double length;
        Kind kind;
        scoreregion::Range range;
    };
    std::vector<Raw> raw;
    std::vector<std::pair<double, double>> melody; // {moment, length} of the silent melody
    for (const Note& note : parseNotes(source.notes)) {
        if (fs::path(note.file).lexically_normal() != copyFile)
            continue;
        if (note.kind == Kind::Melody) {
            melody.emplace_back(note.whole, note.length);
            continue;
        }
        const auto at = copyLines.offsetOf(note.line, note.column);
        std::optional<scoreregion::Range> event;
        if (note.kind == Kind::Note) {
            if (at && *at >= originalEnd) {
                if (const auto g = scoreregion::eventAt(generated, *at))
                    event = blockEvents[*g];
            } else if (at) {
                if (const auto e = scoreregion::eventAt(originalEvents, *at))
                    event = originalEvents[*e];
            }
            if (event)
                timeline.cues.push_back({tempo->seconds(note.whole), *event});
            else
                ++timeline.unmapped;
        } else if (at) {
            std::optional<std::size_t> begin;
            if (*at < originalEnd)
                begin = *at;
            else if (*at >= generatedBegin && *at < generatedBegin + generatedText.size())
                begin = blockWord(*at);
            if (begin)
                event = scoreregion::Range{*begin, tokenEnd(source.original, *begin)};
        }
        if (event)
            raw.push_back({note.whole, note.length, note.kind, *event});
    }
    // A syllable is sung as long as the melody note that begins with it; else (no silent melody:
    // the melody sounds in a staff) until the next syllable, the last one as logged.
    std::vector<double> lyricMoments;
    for (const Raw& r : raw)
        if (r.kind == Kind::Lyric)
            lyricMoments.push_back(r.start);
    std::ranges::sort(lyricMoments);
    for (Raw& r : raw) {
        if (r.kind != Kind::Lyric)
            continue;
        const auto note = std::ranges::find_if(
            melody, [&](const auto& m) { return std::abs(m.first - r.start) < 1e-9; });
        if (note != melody.end() && note->second > 0) {
            r.length = note->second;
            continue;
        }
        const auto next = std::ranges::upper_bound(lyricMoments, r.start + 1e-9);
        if (next != lyricMoments.end())
            r.length = *next - r.start;
    }
    for (const Raw& r : raw) {
        const double start = tempo->seconds(r.start);
        double end = r.length > 0 ? tempo->seconds(r.start + r.length) : start;
        end = std::max(end, start + (r.length > 0 ? 0.0 : minimumSounding));
        timeline.sounds.push_back({start, end, r.kind, r.range});
    }
    std::ranges::sort(timeline.cues, [](const Cue& a, const Cue& b) {
        return a.seconds != b.seconds ? a.seconds < b.seconds : a.event.begin < b.event.begin;
    });
    std::ranges::sort(timeline.sounds, [](const Sound& a, const Sound& b) {
        return a.start != b.start ? a.start < b.start : a.range.begin < b.range.begin;
    });
    // A chord, or the same music on two staves, lists one event several times.
    timeline.cues.erase(std::ranges::unique(timeline.cues,
                                            [](const Cue& a, const Cue& b) {
                                                return a.event == b.event &&
                                                       std::abs(a.seconds - b.seconds) < 1e-6;
                                            })
                            .begin(),
                        timeline.cues.end());
    timeline.sounds.erase(std::ranges::unique(timeline.sounds,
                                              [](const Sound& a, const Sound& b) {
                                                  return a.range == b.range &&
                                                         std::abs(a.start - b.start) < 1e-6;
                                              })
                              .begin(),
                          timeline.sounds.end());
    return timeline;
}

std::optional<double> startSeconds(const Timeline& timeline, std::size_t caret) {
    if (timeline.cues.empty())
        return std::nullopt;
    // The event the caret is in or the next one that sounds, then its first pass.
    const Cue* first = nullptr;
    for (const Cue& cue : timeline.cues) {
        if (cue.event.end <= caret)
            continue;
        if (!first || cue.event.begin < first->event.begin ||
            (cue.event.begin == first->event.begin && cue.seconds < first->seconds))
            first = &cue;
    }
    if (!first)
        return std::nullopt;
    // Before the music of the first \score (a variable's definition aside): from the start, not
    // from its first note (which may follow rests).
    if (caret <= timeline.firstBegin && first->event.begin >= timeline.firstBegin)
        return 0.0;
    return first->seconds;
}

std::vector<Sound> soundingAt(const Timeline& timeline, double seconds) {
    std::vector<Sound> now;
    for (const Sound& sound : timeline.sounds)
        if (sound.start <= seconds + 1e-6 && seconds + 1e-6 < sound.end)
            now.push_back(sound);
    return now;
}

const Cue* cueAt(const Timeline& timeline, double seconds) {
    const auto after = std::ranges::upper_bound(timeline.cues, seconds + 1e-6, {}, &Cue::seconds);
    return after == timeline.cues.begin() ? nullptr : &*(after - 1);
}

} // namespace zub::scoretiming
