// Playback of scores: the MIDI document LilyPond builds (see zub::score::midiDocument).
#include "scorescan.hpp"
#include "zub/core/fretboard.hpp"
#include "zub/core/midi.hpp"
#include "zub/core/score.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <tuple>

namespace zub::score {

namespace {

using zub::fretboard::scan_detail::isDigit;
using zub::fretboard::scan_detail::isLetter;
using zub::fretboard::scan_detail::isSpace;
using zub::fretboard::scan_detail::Names;
using zub::fretboard::scan_detail::Scanner;

constexpr std::size_t npos = std::string_view::npos;
constexpr std::size_t proceed = npos - 1; // onCommand: go on right after the word
constexpr std::size_t stop = npos;        // onCommand: end the walk

// Walks `text` code point by code point of LilyPond source, skipping comments, strings and
// Scheme: `onCommand(begin, wordEnd, word)` is called for every `\word` and returns where to go
// on (`proceed`: right after the word, `stop`: end the walk); `onBrace(i, c)` for every `{` and
// `}`.
template <class OnCommand, class OnBrace>
void walk(std::string_view text, std::size_t from, OnCommand&& onCommand, OnBrace&& onBrace) {
    const Scanner scanner{text, Names::Any, {}};
    std::size_t i = from;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '%') {
            i = scanner.skipComment(i);
        } else if (c == '"') {
            i = scanner.skipString(i);
        } else if (c == '#' || c == '$') {
            i = scanner.skipScheme(i);
        } else if (c == '\\') {
            std::size_t end = i + 1;
            while (end < text.size() && isLetter(text[end]))
                ++end;
            if (end == i + 1) { // `\\`, `\(` ...
                i = std::min(i + 2, text.size());
                continue;
            }
            const std::size_t next = onCommand(i, end, text.substr(i + 1, end - i - 1));
            if (next == stop)
                return;
            i = next == proceed ? end : next;
        } else {
            if (c == '{' || c == '}') {
                if (!onBrace(i, c))
                    return;
            }
            ++i;
        }
    }
}

std::size_t skipSpace(std::string_view text, std::size_t i) {
    while (i < text.size() && isSpace(text[i]))
        ++i;
    return i;
}

// The index after the `}` that closes the `{` at `open`; npos if there is none.
std::size_t closingBrace(std::string_view text, std::size_t open) {
    int depth = 0;
    std::size_t end = npos;
    walk(
        text, open, [](std::size_t, std::size_t, std::string_view) { return proceed; },
        [&](std::size_t i, char c) {
            depth += c == '{' ? 1 : -1;
            if (depth == 0) {
                end = i + 1;
                return false;
            }
            return true;
        });
    return end;
}

struct ScoreBlock {
    std::size_t begin; // the backslash of \score
    std::size_t open;  // its `{`
    std::size_t end;   // after its `}`
};

std::optional<ScoreBlock> firstScore(std::string_view text) {
    std::optional<ScoreBlock> found;
    walk(
        text, 0,
        [&](std::size_t begin, std::size_t wordEnd, std::string_view word) {
            if (word != "score")
                return proceed;
            const std::size_t open = skipSpace(text, wordEnd);
            if (open >= text.size() || text[open] != '{')
                return proceed;
            const std::size_t end = closingBrace(text, open);
            if (end != npos)
                found = ScoreBlock{begin, open, end};
            return stop;
        },
        [](std::size_t, char) { return true; });
    return found;
}

// Reads digits at `i`; returns the value and moves `i` past them.
std::optional<long> number(std::string_view text, std::size_t& i) {
    const std::size_t start = i;
    while (i < text.size() && isDigit(text[i]))
        ++i;
    if (i == start)
        return std::nullopt;
    long value = 0;
    const auto [end, ec] = std::from_chars(text.data() + start, text.data() + i, value);
    if (ec != std::errc{} || end != text.data() + i)
        return std::nullopt;
    return value;
}

std::string scaled(long value, double factor) {
    return std::to_string(std::max(1L, std::lround(static_cast<double>(value) * factor)));
}

// ---- Staves ----

constexpr std::size_t none = zub::score::noPosition;

// The index after the `>>` that closes the `<<` at `open` (comments, strings, Scheme and `\>`
// skipped); npos if there is none.
std::size_t closingDouble(std::string_view text, std::size_t open) {
    const Scanner scanner{text, Names::Any, {}};
    int depth = 0;
    std::size_t i = open;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '%') {
            i = scanner.skipComment(i);
        } else if (c == '"') {
            i = scanner.skipString(i);
        } else if (c == '#' || c == '$') {
            i = scanner.skipScheme(i);
        } else if (c == '\\') {
            i += 2;
        } else if (c == '<' && i + 1 < text.size() && text[i + 1] == '<') {
            ++depth;
            i += 2;
        } else if (c == '>' && i + 1 < text.size() && text[i + 1] == '>') {
            i += 2;
            if (--depth == 0)
                return i;
        } else {
            ++i;
        }
    }
    return npos;
}

// The music a staff holds, starting at `at`: `{ }`, `<< >>`, `\relative c' { }` and the like, or a
// bare `\variable`. `open`: where the braces open (npos for a variable), `inner`: the text between
// the braces (empty view without), `end`: after the music.
struct MusicSpan {
    std::size_t open = npos;
    std::size_t openLength = 0;
    std::size_t end = 0;
    bool closed = false;
};

MusicSpan musicAt(std::string_view text, std::size_t at) {
    MusicSpan span;
    span.end = at;
    if (at >= text.size())
        return span;
    if (text[at] == '{') {
        const std::size_t close = closingBrace(text, at);
        span.open = at;
        span.openLength = 1;
        span.closed = close != npos;
        span.end = span.closed ? close : text.size();
    } else if (text.substr(at).starts_with("<<")) {
        const std::size_t close = closingDouble(text, at);
        span.open = at;
        span.openLength = 2;
        span.closed = close != npos;
        span.end = span.closed ? close : text.size();
    } else if (text[at] == '\\') {
        std::size_t end = at + 1;
        while (end < text.size() && isLetter(text[end]))
            ++end;
        const std::string_view word = text.substr(at + 1, end - at - 1);
        span.end = end;
        if (word == "relative" || word == "transpose" || word == "fixed" ||
            word == "transposition" || word == "absolute") {
            // The arguments are pitches (`c'`, `d ees`): up to the music itself.
            std::size_t i = end;
            while (i < text.size() && text[i] != '{' && text[i] != '<' && text[i] != '\\' &&
                   text[i] != '}' && text[i] != '%' && text[i] != '"' && text[i] != '#' &&
                   text[i] != '$')
                ++i;
            if (i < text.size() && (text[i] == '{' || text[i] == '<' || text[i] == '\\'))
                return musicAt(text, i);
        }
    }
    return span;
}

// `midiInstrument = "..."` and `stringTunings = #...` in the text of a \with block.
void scanWith(std::string_view text, std::size_t from, std::size_t to, zub::score::Staff& staff) {
    const Scanner scanner{text, Names::Any, {}};
    std::size_t i = from;
    while (i < to) {
        const char c = text[i];
        if (c == '%') {
            i = scanner.skipComment(i);
        } else if (c == '"') {
            i = scanner.skipString(i);
        } else if (c == '#' || c == '$') {
            i = scanner.skipScheme(i);
        } else if (c == '\\') {
            ++i;
            while (i < to && isLetter(text[i]))
                ++i;
        } else if (isLetter(c)) {
            std::size_t end = i;
            while (end < to && (isLetter(text[end]) || isDigit(text[end]) || text[end] == '-'))
                ++end;
            const std::string_view word = text.substr(i, end - i);
            std::size_t j = skipSpace(text, end);
            if ((word == "midiInstrument" || word == "stringTunings") && j < to && text[j] == '=') {
                j = skipSpace(text, j + 1);
                if (word == "midiInstrument" && j < to && text[j] == '"') {
                    const std::size_t close = scanner.skipString(j);
                    if (staff.withBegin == none && close - 1 <= to && close > j + 1 &&
                        text[close - 1] == '"') {
                        staff.withBegin = j + 1;
                        staff.withEnd = close - 1;
                    }
                } else if (word == "stringTunings" && j < to && text[j] == '#') {
                    const std::size_t close = std::min(scanner.skipScheme(j), to);
                    staff.tuning = std::string(text.substr(j + 1, close - j - 1));
                }
            }
            i = std::max(end, i + 1);
        } else {
            ++i;
        }
    }
}

// The first `\set [Context.]midiInstrument = "..."` directly in the music (not in a nested
// block, not after `\once`): the text inside its quotes, as offsets into `text`.
void scanLeadingSet(std::string_view text, const MusicSpan& music, zub::score::Staff& staff) {
    if (music.open == npos || !music.closed)
        return;
    const std::size_t from = music.open + music.openLength;
    const std::size_t to = music.end - music.openLength;
    const std::string_view inner = text.substr(from, to - from);
    int depth = 0;
    std::string_view previous;
    walk(
        inner, 0,
        [&](std::size_t, std::size_t wordEnd, std::string_view word) {
            const std::string_view before = previous;
            previous = word;
            if (depth != 0 || word != "set" || before == "once" || before == "temporary")
                return proceed;
            std::size_t k = skipSpace(inner, wordEnd);
            const std::size_t nameBegin = k;
            while (k < inner.size() && (isLetter(inner[k]) || inner[k] == '.'))
                ++k;
            const std::string_view property = inner.substr(nameBegin, k - nameBegin);
            if (property != "midiInstrument" && !property.ends_with(".midiInstrument"))
                return proceed;
            k = skipSpace(inner, k);
            if (k >= inner.size() || inner[k] != '=')
                return proceed;
            k = skipSpace(inner, k + 1);
            if (k >= inner.size() || inner[k] != '"')
                return proceed;
            const std::size_t close = Scanner{inner, Names::Any, {}}.skipString(k);
            if (close <= k + 1 || inner[close - 1] != '"')
                return proceed;
            staff.setBegin = from + k + 1;
            staff.setEnd = from + close - 1;
            return stop;
        },
        [&](std::size_t, char c) {
            depth += c == '{' ? 1 : -1;
            return true;
        });
}

std::vector<zub::score::Staff> findStaves(std::string_view text) {
    std::vector<zub::score::Staff> found;
    walk(
        text, 0,
        [&](std::size_t begin, std::size_t wordEnd, std::string_view word) {
            if (word != "new")
                return proceed;
            std::size_t typeBegin = skipSpace(text, wordEnd);
            std::size_t typeEnd = typeBegin;
            while (typeEnd < text.size() && isLetter(text[typeEnd]))
                ++typeEnd;
            const std::string_view type = text.substr(typeBegin, typeEnd - typeBegin);
            if (type != "Staff" && type != "TabStaff")
                return proceed;
            zub::score::Staff staff;
            staff.type = std::string(type);
            staff.begin = begin;
            staff.headEnd = typeEnd;
            std::size_t i = skipSpace(text, typeEnd);
            if (i < text.size() && text[i] == '=') {
                const std::size_t quote = skipSpace(text, i + 1);
                if (quote < text.size() && text[quote] == '"') {
                    const std::size_t close = Scanner{text, Names::Any, {}}.skipString(quote);
                    if (close > quote + 1 && text[close - 1] == '"') {
                        staff.name = std::string(text.substr(quote + 1, close - quote - 2));
                        staff.headEnd = close;
                        i = skipSpace(text, close);
                    }
                }
            }
            if (text.substr(i).starts_with("\\with") &&
                (i + 5 >= text.size() || !isLetter(text[i + 5]))) {
                const std::size_t open = skipSpace(text, i + 5);
                if (open < text.size() && text[open] == '{') {
                    const std::size_t close = closingBrace(text, open);
                    if (close != npos) {
                        staff.withOpen = open;
                        staff.withClose = close - 1;
                        scanWith(text, open + 1, close - 1, staff);
                        i = skipSpace(text, close);
                    }
                }
            }
            const MusicSpan music = musicAt(text, i);
            staff.end = std::max(music.end, wordEnd);
            if (music.open != npos)
                staff.insertAt = music.open + music.openLength;
            scanLeadingSet(text, music, staff);
            if (staff.setBegin != none)
                staff.instrument =
                    std::string(text.substr(staff.setBegin, staff.setEnd - staff.setBegin));
            else if (staff.withBegin != none)
                staff.instrument =
                    std::string(text.substr(staff.withBegin, staff.withEnd - staff.withBegin));
            const std::size_t next = staff.end;
            found.push_back(std::move(staff));
            return next;
        },
        [](std::size_t, char) { return true; });
    return found;
}

// A General MIDI name as the songbook's instruments spell it, without anything that could end the
// string: "" for an id that sets nothing.
std::string chosenName(std::string_view id) {
    const auto* instrument = id == "auto" ? nullptr : zub::midi::findInstrument(id);
    return instrument ? std::string(instrument->name) : std::string();
}

bool chosenAmp(std::string_view id) {
    const auto* instrument = zub::midi::findInstrument(id);
    return instrument && instrument->amp;
}

// `music` with every staff set to its choice of `options` (see MidiOptions::staffInstruments):
// the instrument goes into the staff's \with (a file's `\set` of the staff is changed to it, since
// that one would win), and staves that must not sound get `\remove "Staff_performer"`.
std::string applyStaffChoices(std::string_view music, const zub::score::MidiOptions& options) {
    using Sounding = zub::score::MidiOptions::Sounding;
    const auto staves = findStaves(music);
    if (staves.empty() || (staves.size() == 1 && options.staffInstruments.empty() &&
                           options.sounding == Sounding::All))
        return std::string(music);
    struct Edit {
        std::size_t begin;
        std::size_t end;
        std::string text;
    };
    std::vector<Edit> edits;
    for (std::size_t i = 0; i < staves.size(); ++i) {
        const auto& staff = staves[i];
        const std::string_view id = i < options.staffInstruments.size()
                                        ? std::string_view(options.staffInstruments[i])
                                        : "";
        const bool amp = chosenAmp(id);
        const bool muted = (options.sounding == Sounding::AmpOnly && !amp) ||
                           (options.sounding == Sounding::WithoutAmp && amp);
        std::string name = muted ? std::string() : chosenName(id);
        // "Automatic" in a score with several staves: a tab staff that sets nothing plays the
        // banjo when its own tuning is the banjo's, else the guitar (the whole-score guess says
        // banjo for all of them as soon as one staff has the banjo tuning).
        if (name.empty() && !muted && staves.size() >= 2 && staff.type == "TabStaff" &&
            staff.instrument.empty() && chosenName(id).empty())
            name = staff.tuning.find("banjo") != std::string::npos
                       ? std::string(zub::midi::findInstrument("banjo")->name)
                       : std::string(zub::midi::findInstrument("steel")->name);
        std::string with;
        if (!name.empty())
            with += " midiInstrument = \"" + name + "\"";
        if (muted)
            with += " \\remove \"Staff_performer\"";
        if (with.empty())
            continue;
        if (!name.empty() && staff.setBegin != none)
            edits.push_back({staff.setBegin, staff.setEnd, name});
        if (staff.withClose != none)
            edits.push_back({staff.withClose, staff.withClose, with + " "});
        else
            edits.push_back({staff.headEnd, staff.headEnd, " \\with {" + with + " }"});
    }
    std::ranges::stable_sort(edits, {}, &Edit::begin);
    std::string out;
    std::size_t copied = 0;
    for (const auto& edit : edits) {
        out.append(music.substr(copied, edit.begin - copied));
        out += edit.text;
        copied = edit.end;
    }
    out.append(music.substr(copied));
    return out;
}

} // namespace

int songCapo(std::string_view cap) {
    while (!cap.empty() && (isSpace(cap.front()) || cap.front() == '{'))
        cap.remove_prefix(1);
    while (!cap.empty() && (isSpace(cap.back()) || cap.back() == '}'))
        cap.remove_suffix(1);
    std::size_t i = 0;
    const auto value = number(cap, i);
    if (!value || i != cap.size() || *value < 0 || *value > 12)
        return 0;
    return static_cast<int>(*value);
}

int songTempo(std::string_view tem) {
    const auto trim = [](std::string_view s) {
        while (!s.empty() && (isSpace(s.front()) || s.front() == '{'))
            s.remove_prefix(1);
        while (!s.empty() && (isSpace(s.back()) || s.back() == '}'))
            s.remove_suffix(1);
        return s;
    };
    tem = trim(tem);
    std::size_t i = 0;
    const auto value = number(tem, i);
    if (!value || i != tem.size() || *value < 20 || *value > 400)
        return 100;
    return static_cast<int>(*value);
}

std::string midiInstrument(std::string_view scoreText) {
    bool whistle = false;
    walk(
        scoreText, 0,
        [&](std::size_t, std::size_t wordEnd, std::string_view word) {
            if (word != "include")
                return proceed;
            const std::size_t quote = skipSpace(scoreText, wordEnd);
            if (quote < scoreText.size() && scoreText[quote] == '"' &&
                scoreText.substr(quote + 1).starts_with("tinwhistle.ily\""))
                whistle = true;
            return proceed;
        },
        [](std::size_t, char) { return true; });
    if (whistle)
        return "whistle";
    return fretboard::tuningOf(scoreText).shortString != 0 ? "banjo" : "acoustic guitar (steel)";
}

std::optional<std::pair<std::size_t, std::size_t>> firstScoreBody(std::string_view text) {
    const auto block = firstScore(text);
    if (!block)
        return std::nullopt;
    return std::make_pair(block->open + 1, block->end - 1);
}

std::optional<std::string> scoreMusic(std::string_view text) {
    const auto block = firstScore(text);
    if (!block)
        return std::nullopt;
    const std::string_view body = text.substr(block->open + 1, block->end - 1 - block->open - 1);

    // Remove the blocks of the score that are no music, at the top level of its braces.
    std::string music;
    std::size_t copied = 0;
    int depth = 0;
    walk(
        body, 0,
        [&](std::size_t begin, std::size_t wordEnd, std::string_view word) {
            if (depth != 0 || (word != "layout" && word != "header" && word != "midi"))
                return proceed;
            const std::size_t open = skipSpace(body, wordEnd);
            if (open >= body.size() || body[open] != '{')
                return proceed;
            const std::size_t end = closingBrace(body, open);
            if (end == npos)
                return proceed;
            music.append(body.substr(copied, begin - copied));
            copied = end;
            return end;
        },
        [&](std::size_t, char c) {
            depth += c == '{' ? 1 : -1;
            return true;
        });
    music.append(body.substr(copied));
    return music;
}

std::string scaleTempo(std::string_view music, double factor) {
    std::string out;
    std::size_t copied = 0;
    walk(
        music, 0,
        [&](std::size_t, std::size_t wordEnd, std::string_view word) {
            if (word != "tempo")
                return proceed;
            std::size_t i = skipSpace(music, wordEnd);
            if (i < music.size() && music[i] == '"') { // a text before the beat
                i = skipSpace(music, Scanner{music, Names::Any, {}}.skipString(i));
            }
            if (!number(music, i)) // the duration
                return proceed;
            while (i < music.size() && music[i] == '.')
                ++i;
            i = skipSpace(music, i);
            if (i >= music.size() || music[i] != '=')
                return proceed;
            i = skipSpace(music, i + 1);
            std::size_t at = i;
            const auto first = number(music, i);
            if (!first)
                return proceed;
            out.append(music.substr(copied, at - copied));
            out += scaled(*first, factor);
            copied = i;
            std::size_t j = skipSpace(music, i);
            if (j < music.size() && music[j] == '-') { // a range, "100-120"
                j = skipSpace(music, j + 1);
                at = j;
                if (const auto second = number(music, j)) {
                    out.append(music.substr(copied, at - copied));
                    out += scaled(*second, factor);
                    copied = j;
                    i = j;
                }
            }
            return i;
        },
        [](std::size_t, char) { return true; });
    out.append(music.substr(copied));
    return out;
}

std::string markBends(std::string_view music) {
    const auto marker = [](const char* name) {
        return std::string("\\once \\set Staff.midiInstrument = \"") + name + "\" ";
    };
    std::vector<std::pair<std::size_t, std::string>> inserts;
    std::size_t preBendEnd = npos;
    walk(
        music, 0,
        [&](std::size_t begin, std::size_t wordEnd, std::string_view word) {
            if (word == "preBend") {
                preBendEnd = wordEnd;
            } else if (word == "grace") {
                // Only `\^` between the \preBend and the \grace.
                bool pre = false;
                if (preBendEnd != npos && preBendEnd <= begin) {
                    std::string between;
                    for (const char c : music.substr(preBendEnd, begin - preBendEnd))
                        if (!isSpace(c))
                            between += c;
                    pre = between.empty() || between == "\\^";
                }
                preBendEnd = npos;
                const std::size_t note = skipSpace(music, wordEnd);
                if (note >= music.size() || music[note] == '{' || music[note] == '}')
                    return proceed;
                std::size_t end = note;
                while (end < music.size() && !isSpace(music[end]) && music[end] != '}')
                    ++end;
                const std::string_view chunk = music.substr(note, end - note);
                const std::size_t after = skipSpace(music, end);
                const bool release =
                    chunk.find("\\^") != npos || music.substr(after).starts_with("\\^");
                inserts.emplace_back(begin, marker(pre ? (release ? "telephone ring" : "helicopter")
                                                       : (release ? "applause" : "gunshot")));
            } else if (word == "glissando") {
                std::size_t next = skipSpace(music, wordEnd);
                if (next < music.size() && music[next] == '|')
                    next = skipSpace(music, next + 1);
                // A note or chord follows, not a rest or a skip.
                if (next < music.size() && ((music[next] >= 'a' && music[next] <= 'z' &&
                                             music[next] != 'r' && music[next] != 's') ||
                                            music[next] == '<'))
                    inserts.emplace_back(next, marker("seashore"));
            }
            return proceed;
        },
        [](std::size_t, char) { return true; });
    std::ranges::stable_sort(inserts, {}, &std::pair<std::size_t, std::string>::first);
    std::string out;
    std::size_t copied = 0;
    for (const auto& [at, text] : inserts) {
        out.append(music.substr(copied, at - copied));
        out += text;
        copied = at;
    }
    out.append(music.substr(copied));
    return out;
}

namespace {

// The type of the context a `\new` at `wordEnd` makes: [typeBegin, typeEnd).
std::pair<std::size_t, std::size_t> newType(std::string_view music, std::size_t wordEnd) {
    const std::size_t typeBegin = skipSpace(music, wordEnd);
    std::size_t typeEnd = typeBegin;
    while (typeEnd < music.size() && isLetter(music[typeEnd]))
        ++typeEnd;
    return {typeBegin, typeEnd};
}

// `\new Devnull = "vocal" {...}` (a melody that times the lyrics and makes no sound) as a staff of
// its own sounding `midiName`: `\new Staff \with { midiInstrument = "..." } \new Voice = "vocal"
// {...}` (the Voice keeps the name \lyricsto looks for).
std::string soundHiddenMelody(std::string_view music, std::string_view midiName) {
    std::string out;
    std::size_t copied = 0;
    walk(
        music, 0,
        [&](std::size_t, std::size_t wordEnd, std::string_view word) {
            if (word != "new")
                return proceed;
            const auto [typeBegin, typeEnd] = newType(music, wordEnd);
            if (music.substr(typeBegin, typeEnd - typeBegin) != "Devnull")
                return proceed;
            out.append(music.substr(copied, typeBegin - copied));
            out +=
                "Staff \\with { midiInstrument = \"" + std::string(midiName) + "\" } \\new Voice";
            copied = typeEnd;
            return typeEnd;
        },
        [](std::size_t, char) { return true; });
    out.append(music.substr(copied));
    return out;
}

} // namespace

bool hasHiddenMelody(std::string_view text) {
    const auto block = firstScore(text);
    if (!block)
        return false;
    const std::size_t from = block->open + 1;
    const std::string_view music = text.substr(from, block->end - 1 - from);
    bool found = false;
    walk(
        music, 0,
        [&](std::size_t, std::size_t wordEnd, std::string_view word) {
            if (word == "new") {
                const auto [typeBegin, typeEnd] = newType(music, wordEnd);
                found = music.substr(typeBegin, typeEnd - typeBegin) == "Devnull";
            }
            return found ? stop : proceed;
        },
        [](std::size_t, char) { return true; });
    return found;
}

bool hasChordNames(std::string_view text) {
    const auto block = firstScore(text);
    if (!block)
        return false;
    const std::size_t from = block->open + 1;
    const std::string_view music = text.substr(from, block->end - 1 - from);
    bool found = false;
    walk(
        music, 0,
        [&](std::size_t, std::size_t wordEnd, std::string_view word) {
            if (word == "chords") {
                found = true;
            } else if (word == "new") {
                const std::size_t typeBegin = skipSpace(music, wordEnd);
                std::size_t typeEnd = typeBegin;
                while (typeEnd < music.size() && isLetter(music[typeEnd]))
                    ++typeEnd;
                found = music.substr(typeBegin, typeEnd - typeBegin) == "ChordNames";
            }
            return found ? stop : proceed;
        },
        [](std::size_t, char) { return true; });
    return found;
}

std::vector<Staff> staves(std::string_view text) {
    const auto block = firstScore(text);
    if (!block)
        return {};
    const std::size_t from = block->open + 1;
    auto found = findStaves(text.substr(from, block->end - 1 - from));
    const auto shift = [from](std::size_t& offset) {
        if (offset != none)
            offset += from;
    };
    for (auto& staff : found) {
        for (std::size_t* offset :
             {&staff.begin, &staff.end, &staff.headEnd, &staff.withOpen, &staff.withClose,
              &staff.setBegin, &staff.setEnd, &staff.withBegin, &staff.withEnd, &staff.insertAt})
            shift(*offset);
    }
    return found;
}

std::optional<songedit::Change> saveStaffInstrument(std::string_view text, std::size_t staffIndex,
                                                    std::string_view gmName) {
    if (!midi::findInstrumentByName(gmName))
        return std::nullopt;
    const auto all = staves(text);
    if (staffIndex >= all.size())
        return std::nullopt;
    const Staff& staff = all[staffIndex];
    std::size_t begin = staff.setBegin != none ? staff.setBegin : staff.withBegin;
    std::size_t end = staff.setBegin != none ? staff.setEnd : staff.withEnd;
    std::string replacement(gmName);
    if (begin == none) {
        if (staff.insertAt == none)
            return std::nullopt;
        const std::string line = "\\set Staff.midiInstrument = \"" + std::string(gmName) + "\"";
        // The brace ends its line: the new line goes behind that line break, indented like the
        // next line of the music (or two spaces deeper than the brace's line).
        std::size_t eol = staff.insertAt;
        while (eol < text.size() && (text[eol] == ' ' || text[eol] == '\t'))
            ++eol;
        const bool ownLine = eol < text.size() && (text[eol] == '\n' || text[eol] == '\r');
        if (!ownLine) {
            begin = end = staff.insertAt;
            replacement = " " + line;
        } else {
            const std::string_view lineBreak = text.substr(eol).starts_with("\r\n") ? "\r\n"
                                               : text[eol] == '\r'                  ? "\r"
                                                                                    : "\n";
            const std::size_t next = eol + lineBreak.size();
            std::size_t indentEnd = next;
            while (indentEnd < text.size() && (text[indentEnd] == ' ' || text[indentEnd] == '\t'))
                ++indentEnd;
            std::string indent(text.substr(next, indentEnd - next));
            const bool blank =
                indentEnd >= text.size() || text[indentEnd] == '\n' || text[indentEnd] == '\r';
            if (blank || indent.empty()) {
                std::size_t lineStart = staff.insertAt;
                while (lineStart > 0 && text[lineStart - 1] != '\n')
                    --lineStart;
                std::size_t i = lineStart;
                while (i < text.size() && (text[i] == ' ' || text[i] == '\t'))
                    ++i;
                indent = std::string(text.substr(lineStart, i - lineStart)) + "  ";
            }
            begin = end = next;
            replacement = indent + line + std::string(lineBreak);
            std::string out(text.substr(0, begin));
            out += replacement;
            out.append(text.substr(end));
            return songedit::Change{std::move(out), begin + indent.size() + line.size() - 1};
        }
        std::string out(text.substr(0, begin));
        out += replacement;
        out.append(text.substr(end));
        return songedit::Change{std::move(out), begin + replacement.size() - 1};
    }
    std::string out(text.substr(0, begin));
    out += replacement;
    out.append(text.substr(end));
    return songedit::Change{std::move(out), begin + replacement.size()};
}

std::optional<std::string> midiScore(std::string_view text, const MidiOptions& options) {
    const auto music = scoreMusic(text);
    if (!music)
        return std::nullopt;
    // Only numbers and the instrument name go into the score; the name is a fixed string of the
    // songbook's, but keep quotes and backslashes out of it anyway.
    std::string instrument;
    for (const char c : options.instrument)
        if (c != '"' && c != '\\' && c != '\n' && c != '\r')
            instrument += c;
    const long beats =
        std::max(1L, std::lround(static_cast<double>(options.tempo) * options.factor));

    using Sounding = MidiOptions::Sounding;
    // The hidden melody sounds only with an instrument of its own, in the book of its group.
    const auto* melody = midi::findInstrument(options.melodyInstrument);
    if (melody && ((options.sounding == Sounding::AmpOnly && !melody->amp) ||
                   (options.sounding == Sounding::WithoutAmp && melody->amp)))
        melody = nullptr;
    std::string staffMusic =
        markBends(applyStaffChoices(scaleTempo(*music, options.factor), options));
    if (melody)
        staffMusic = soundHiddenMelody(staffMusic, melody->name);
    std::string played = "\\unfoldRepeats {\n" + staffMusic + "\n  }";
    if (options.transpose > 0 && options.transpose <= 12)
        // c' up to the capo's pitch, in the note names of the score's language. The names assume
        // the wrapper's \language (zub::score::prepare); no real score sets its own.
        played = "\\transpose " + fretboard::pitchName(60, options.germanNames, false) + " " +
                 fretboard::pitchName(60 + options.transpose, options.germanNames, false) +
                 " {\n  " + played + "\n  }";
    // The chord names sound only with an instrument of their own, in the book of their group.
    const auto* chords = midi::findInstrument(options.chordInstrument);
    if (chords && ((options.sounding == Sounding::AmpOnly && !chords->amp) ||
                   (options.sounding == Sounding::WithoutAmp && chords->amp)))
        chords = nullptr;
    const std::string chordContext = chords
                                         ? "midiInstrument = \"" + std::string(chords->name) + "\""
                                         : std::string("\\remove \"Staff_performer\"");
    // Only lists the notes (see playbackNotesName): in the book of the staves that sound without
    // the amp, which also holds the muted amp staves; the amp's own book has the same music.
    const std::string listing =
        options.sounding == Sounding::AmpOnly
            ? std::string()
            : "\n  \\layout {\n    \\context { \\Score skipTypesetting = ##t }\n"
              "    \\context { \\Voice \\consists \\zubNoteLogger }\n"
              "    \\context { \\TabVoice \\consists \\zubNoteLogger }\n  }";
    return "\\score {\n  " + played + listing +
           "\n  \\midi {\n    \\tempo 4 = " + std::to_string(beats) +
           "\n    \\context { \\ChordNames " + chordContext +
           " }\n"
           "    \\context { \\Staff midiInstrument = \"" +
           instrument +
           "\" }\n"
           "    \\context { \\TabStaff midiInstrument = \"" +
           instrument + "\" }\n  }\n}";
}

} // namespace zub::score
