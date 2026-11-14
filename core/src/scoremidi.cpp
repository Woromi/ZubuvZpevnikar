// Playback of scores: the MIDI document LilyPond builds (see zub::score::midiDocument).
#include "scorescan.hpp"
#include "zub/core/fretboard.hpp"
#include "zub/core/midi.hpp"
#include "zub/core/score.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

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
// on (`proceed`: right after the word, `stop`: end the walk); `onBrace(i, c)` for every `{` and `}`.
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

    std::string played = "\\unfoldRepeats {\n" + scaleTempo(*music, options.factor) + "\n  }";
    if (options.transpose > 0 && options.transpose <= 12)
        // c' up to the capo's pitch, in the note names of the score's language
        played = "\\transpose " + fretboard::pitchName(60, options.germanNames, false) + " " +
                 fretboard::pitchName(60 + options.transpose, options.germanNames, false) +
                 " {\n  " + played + "\n  }";
    return "\\score {\n  " + played + "\n  \\midi {\n    \\tempo 4 = " + std::to_string(beats) +
           "\n    \\context { \\ChordNames \\remove \"Staff_performer\" }\n"
           "    \\context { \\Staff midiInstrument = \"" + instrument +
           "\" }\n"
           "    \\context { \\TabStaff midiInstrument = \"" + instrument + "\" }\n  }\n}";
}

} // namespace zub::score
