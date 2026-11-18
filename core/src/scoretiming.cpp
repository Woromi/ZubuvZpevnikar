#include "zub/core/scoretiming.hpp"

#include <algorithm>
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
        long long v[6];
        std::size_t i = 0;
        bool ok = true;
        for (auto& value : v)
            ok = ok && readInt(line, i, value);
        if (!ok || v[1] <= 0 || v[3] <= 0 || i >= line.size() || line[i] != ' ')
            continue;
        Note note;
        note.whole = static_cast<double>(v[0]) / static_cast<double>(v[1]) +
                     static_cast<double>(v[2]) / static_cast<double>(v[3]);
        note.line = static_cast<int>(v[4]);
        note.column = static_cast<int>(v[5]);
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
    }

    const fs::path copyFile = source.copyFile.lexically_normal();
    Timeline timeline;
    timeline.firstBegin = !blockEvents.empty()      ? blockEvents.front().begin
                          : !originalEvents.empty() ? originalEvents.front().begin
                                                    : 0;
    for (const Note& note : parseNotes(source.notes)) {
        if (fs::path(note.file).lexically_normal() != copyFile)
            continue;
        const auto at = copyLines.offsetOf(note.line, note.column);
        std::optional<scoreregion::Range> event;
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
    }
    std::ranges::sort(timeline.cues, [](const Cue& a, const Cue& b) {
        return a.seconds != b.seconds ? a.seconds < b.seconds : a.event.begin < b.event.begin;
    });
    // A chord, or the same music on two staves, lists one event several times.
    timeline.cues.erase(std::ranges::unique(timeline.cues,
                                            [](const Cue& a, const Cue& b) {
                                                return a.event == b.event &&
                                                       std::abs(a.seconds - b.seconds) < 1e-6;
                                            })
                            .begin(),
                        timeline.cues.end());
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

const Cue* cueAt(const Timeline& timeline, double seconds) {
    const auto after = std::ranges::upper_bound(timeline.cues, seconds + 1e-6, {}, &Cue::seconds);
    return after == timeline.cues.begin() ? nullptr : &*(after - 1);
}

} // namespace zub::scoretiming
