#include "scorescan.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <zub/core/fretboard.hpp>

namespace zub::fretboard {

using namespace scan_detail;

namespace {

std::string durationText(std::string_view text, const Event& e) {
    return std::string(text.substr(e.durBegin, e.durEnd - e.durBegin));
}

// Spaces so that a token inserted at `at` touches nothing; returns the new line and the cursor
// after the token.
Change insertToken(std::string_view line, std::size_t cursor, std::string_view token) {
    cursor = std::min(cursor, line.size());
    // A cursor inside a note or chord goes behind it. Positions in the middle of a UTF-8
    // sequence move left first.
    while (cursor > 0 && cursor < line.size() &&
           (static_cast<unsigned char>(line[cursor]) & 0xC0) == 0x80)
        --cursor;
    for (const Event& e : scan(line, Names::Any)) {
        if (e.begin < cursor && cursor < e.postEnd) {
            cursor = e.postEnd;
            break;
        }
    }
    cursor = std::min(cursor, line.size());
    std::string text(line.substr(0, cursor));
    const bool spaceBefore = cursor > 0 && !isSpace(line[cursor - 1]);
    if (spaceBefore)
        text += ' ';
    text += token;
    const std::size_t after = text.size();
    if (cursor < line.size() && !isSpace(line[cursor]))
        text += ' ';
    text += line.substr(cursor);
    return {std::move(text), after};
}

// `token` with `duration` after its pitch (or after `>`).
std::string withDuration(std::string_view token, std::string_view duration) {
    std::string out(token);
    if (duration.empty())
        return out;
    if (!token.empty() && token.front() == '<') {
        out += duration;
        return out;
    }
    const auto slash = out.find('\\');
    out.insert(slash == std::string::npos ? out.size() : slash, duration);
    return out;
}

} // namespace

Tuning guitar() {
    return {{64, 59, 55, 50, 45, 40}};
}
Tuning banjoOpenG() {
    return {{62, 59, 55, 50, 67}, 5};
}

Tuning tuningOf(std::string_view scoreText) {
    std::size_t pos = 0;
    while (pos <= scoreText.size()) {
        auto end = scoreText.find('\n', pos);
        if (end == std::string_view::npos)
            end = scoreText.size();
        std::string_view line = scoreText.substr(pos, end - pos);
        if (const auto comment = line.find('%'); comment != std::string_view::npos)
            line = line.substr(0, comment);
        if (line.find("banjo-open-g-tuning") != std::string_view::npos)
            return banjoOpenG();
        pos = end + 1;
    }
    return guitar();
}

std::string pitchName(int midi, bool german, bool flats) {
    static constexpr const char* germanSharp[] = {"c",   "cis", "d",   "dis", "e",   "f",
                                                  "fis", "g",   "gis", "a",   "ais", "h"};
    static constexpr const char* germanFlat[] = {"c",   "des", "d",  "es", "e", "f",
                                                 "ges", "g",   "as", "a",  "b", "h"};
    static constexpr const char* englishSharp[] = {"c",  "cs", "d",  "ds", "e",  "f",
                                                   "fs", "g",  "gs", "a",  "as", "b"};
    static constexpr const char* englishFlat[] = {"c",  "df", "d",  "ef", "e",  "f",
                                                  "gf", "g",  "af", "a",  "bf", "b"};
    const int pc = ((midi % 12) + 12) % 12;
    const int octave = (midi - pc) / 12 - 4; // c = 48 has none
    const char* const* names =
        german ? (flats ? germanFlat : germanSharp) : (flats ? englishFlat : englishSharp);
    std::string out = names[pc];
    if (octave > 0)
        out.append(static_cast<std::size_t>(octave), '\'');
    else if (octave < 0)
        out.append(static_cast<std::size_t>(-octave), ',');
    return out;
}

bool prefersFlats(std::string_view keyValue) {
    std::string key;
    for (const char c : keyValue) {
        if (c != '{' && c != '}' && !isSpace(c))
            key += c;
    }
    if (const auto arrow = key.find("\\rightarrow"); arrow != std::string::npos)
        key.resize(arrow);
    if (key.find("\\flt") != std::string::npos)
        return true;
    return key == "F" || key == "Dm" || key == "Gm" || key == "Cm" || key == "Fm";
}

std::string noteToken(int midi, int string, bool german, bool flats) {
    return pitchName(midi, german, flats) + "\\" + std::to_string(string);
}

std::vector<Position> positionsAt(std::string_view text, std::size_t cursor, const Tuning& tuning,
                                  bool german) {
    const auto events = scan(text, german ? Names::German : Names::English);
    const Event* found = nullptr;
    for (const Event& e : events) {
        if (e.begin <= cursor && cursor <= e.postEnd && !e.midi.empty())
            found = &e;
    }
    std::vector<Position> out;
    if (!found)
        return out;
    for (std::size_t i = 0; i < found->midi.size(); ++i) {
        const int string = found->raw[i].string;
        if (string < 1 || static_cast<std::size_t>(string) > tuning.open.size())
            continue;
        out.push_back({string, found->midi[i] - tuning.open[static_cast<std::size_t>(string - 1)]});
    }
    return out;
}

std::string effectiveDuration(std::string_view text, std::size_t cursor) {
    std::string last = "4";
    for (const Event& e : scan(text, Names::Any)) {
        if (e.durEnd > e.durBegin && e.durEnd <= cursor)
            last = durationText(text, e);
    }
    return last;
}

std::optional<Insertion> carriedDurationFix(std::string_view text, std::size_t from,
                                            std::string_view effective) {
    if (effective.empty())
        return std::nullopt;
    for (const Event& e : scan(text, Names::Any)) {
        if (e.begin < from)
            continue;
        const bool unclosed = e.chord && e.closeAt >= text.size();
        if (e.durBegin != e.durEnd || unclosed)
            return std::nullopt;
        return Insertion{e.durBegin, std::string(effective)};
    }
    return std::nullopt;
}

namespace {

// A new `duration` changes what the next note, chord or rest inherits when it writes none of its
// own: it gets `effective` (the duration in force before the insertion) so it keeps its length.
Change keepFollowingDuration(Change change, std::string_view duration, std::string_view effective) {
    if (duration.empty() || duration == effective)
        return change;
    if (const auto fix = carriedDurationFix(change.text, change.cursor, effective))
        change.text.insert(fix->offset, fix->text);
    return change;
}

} // namespace

Change insertNote(std::string_view line, std::size_t cursor, std::string_view token,
                  std::string_view duration, std::string_view effective) {
    return keepFollowingDuration(insertToken(line, cursor, withDuration(token, duration)), duration,
                                 effective);
}

Change addToChord(std::string_view line, std::size_t cursor, std::string_view token) {
    cursor = std::min(cursor, line.size());
    const auto events = scan(line, Names::Any);
    const Event* target = nullptr;
    for (const Event& e : events) {
        if (e.begin >= cursor)
            break;
        const bool inside = cursor <= e.postEnd;
        const bool justAfter =
            e.postEnd < cursor && std::ranges::all_of(line.substr(e.postEnd, cursor - e.postEnd),
                                                  [](char c) { return isSpace(c); });
        if ((e.chord || e.stringBegin != e.stringEnd) && (inside || justAfter))
            target = &e;
    }
    if (!target)
        return insertNote(line, cursor, token, {});

    std::string text;
    if (target->chord) {
        if (target->closeAt >= line.size()) // no `>` yet
            return insertNote(line, cursor, token, {});
        text.assign(line.substr(0, target->closeAt));
        if (!text.empty() && !isSpace(text.back()))
            text += ' ';
        text += token;
        const std::size_t after = text.size();
        text += line.substr(target->closeAt);
        return {std::move(text), after};
    }
    // A single note: <pitch\N token>duration post-events
    text.assign(line.substr(0, target->begin));
    text += '<';
    text += line.substr(target->begin, target->pitchEnd - target->begin);
    text += line.substr(target->stringBegin, target->stringEnd - target->stringBegin);
    text += ' ';
    text += token;
    const std::size_t after = text.size();
    text += '>';
    text += line.substr(target->durBegin, target->durEnd - target->durBegin);
    text += line.substr(target->stringEnd, target->end - target->stringEnd);
    text += line.substr(target->end);
    return {std::move(text), after};
}

Change insertRest(std::string_view line, std::size_t cursor, std::string_view duration,
                  std::string_view effective) {
    return keepFollowingDuration(
        insertToken(line, cursor, std::string("r") + std::string(duration)), duration, effective);
}

Change insertBarCheck(std::string_view line, std::size_t cursor) {
    return insertToken(line, cursor, "|");
}

} // namespace zub::fretboard
