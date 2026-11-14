#include "scorescan.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <zub/core/fretboard.hpp>

namespace zub::fretboard {

using namespace scan_detail;

std::size_t insertionPoint(std::string_view line, std::size_t cursor) {
    cursor = std::min(cursor, line.size());
    // Positions in the middle of a UTF-8 sequence move left first.
    while (cursor > 0 && cursor < line.size() &&
           (static_cast<unsigned char>(line[cursor]) & 0xC0) == 0x80)
        --cursor;
    for (const Event& e : scan(line, Names::Any)) {
        if (e.begin < cursor && cursor < e.postEnd)
            return std::min(e.postEnd, line.size());
    }
    return cursor;
}

bool inCommentOrString(std::string_view line, std::size_t cursor) {
    const Scanner scanner{line, Names::Any, {}};
    std::size_t i = 0;
    while (i < line.size() && i < cursor) {
        const char c = line[i];
        if (c == '%') {
            const std::size_t end = scanner.skipComment(i);
            const bool block = i + 1 < line.size() && line[i + 1] == '{';
            if (cursor > i && (cursor < end || (cursor == end && (!block || end == line.size()))))
                return true;
            i = end;
        } else if (c == '"') {
            const std::size_t end = scanner.skipString(i);
            if (cursor < end)
                return true;
            i = end;
        } else if (c == '#' || c == '$') {
            i = scanner.skipScheme(i);
        } else if (c == '\\') {
            i += 2;
        } else {
            ++i;
        }
    }
    return false;
}

namespace {

std::string durationText(std::string_view text, const Event& e) {
    return std::string(text.substr(e.durBegin, e.durEnd - e.durBegin));
}

// Spaces so that a token inserted at `at` touches nothing; returns the new line and the cursor
// after the token.
Change insertToken(std::string_view line, std::size_t cursor, std::string_view token) {
    cursor = insertionPoint(line, cursor);
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
    // The text without `%` comments and `%{ ... %}` blocks.
    const Scanner scanner{scoreText, Names::Any, {}};
    std::string code;
    std::size_t i = 0;
    while (i < scoreText.size()) {
        if (scoreText[i] == '%') {
            i = scanner.skipComment(i);
            code += ' ';
        } else {
            code += scoreText[i++];
        }
    }
    if (code.find("banjo-open-g-tuning") != std::string::npos)
        return banjoOpenG();
    return guitar();
}

namespace {

// `text` with the `%` comments blanked (same length).
std::string blankComments(std::string_view text) {
    const Scanner scanner{text, Names::Any, {}};
    std::string code(text);
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '%') {
            const std::size_t end = scanner.skipComment(i);
            for (std::size_t k = i; k < end && k < code.size(); ++k)
                if (code[k] != '\n')
                    code[k] = ' ';
            i = end;
        } else if (text[i] == '"') {
            i = scanner.skipString(i);
        } else {
            ++i;
        }
    }
    return code;
}

constexpr std::string_view banjoName = "banjo-open-g-tuning";

// [begin, end) of the music a variable is defined as: `name = { ... }` at the top level (before
// the \score), including its braces; nullopt when there is none.
std::optional<std::pair<std::size_t, std::size_t>>
variableDefinition(std::string_view code, std::string_view name, std::size_t limit) {
    std::size_t from = 0;
    while ((from = code.find(name, from)) != std::string_view::npos && from < limit) {
        const std::size_t after = from + name.size();
        const bool wordStart =
            from == 0 ||
            !(std::isalpha(static_cast<unsigned char>(code[from - 1])) || code[from - 1] == '\\');
        std::size_t i = after;
        while (i < code.size() && std::isspace(static_cast<unsigned char>(code[i])))
            ++i;
        if (wordStart && i < code.size() && code[i] == '=' &&
            (i + 1 >= code.size() || code[i + 1] != '=')) {
            ++i;
            while (i < code.size() && std::isspace(static_cast<unsigned char>(code[i])))
                ++i;
            // Braces and << >> (strings and Scheme skipped), up to the matching close.
            const Scanner scanner{code, Names::Any, {}};
            const std::size_t begin = i;
            int depth = 0;
            while (i < code.size()) {
                const char c = code[i];
                if (c == '"') {
                    i = scanner.skipString(i);
                    continue;
                }
                if (c == '#' || c == '$') {
                    i = scanner.skipScheme(i);
                    continue;
                }
                if (c == '\\') {
                    i += 2;
                    continue;
                }
                if (c == '{') {
                    ++depth;
                } else if (c == '<' && i + 1 < code.size() && code[i + 1] == '<') {
                    ++depth;
                    ++i;
                } else if (c == '}') {
                    --depth;
                } else if (c == '>' && i + 1 < code.size() && code[i + 1] == '>') {
                    --depth;
                    ++i;
                } else if (depth == 0) {
                    break; // not a braced definition
                }
                ++i;
                if (depth == 0 && i > begin)
                    return std::pair{begin, std::min(i, code.size())};
            }
            return std::nullopt;
        }
        from = after;
    }
    return std::nullopt;
}

} // namespace

TuningAt tuningAt(std::string_view text, std::size_t cursor) {
    TuningAt result{tuningOf(text), score::noPosition, {}};
    const auto all = score::staves(text);
    if (all.empty())
        return result;
    const std::string code = blankComments(text);
    const std::size_t scoreBegin = all.front().begin;

    // The staff's variable: music that is a bare `\name` after the head and the \with.
    const auto variableOf = [&](const score::Staff& staff) -> std::string {
        std::size_t i = staff.withClose != score::noPosition ? staff.withClose + 1 : staff.headEnd;
        while (i < staff.end && std::isspace(static_cast<unsigned char>(code[i])))
            ++i;
        if (i >= staff.end || code[i] != '\\')
            return {};
        std::size_t e = i + 1;
        while (e < staff.end && std::isalpha(static_cast<unsigned char>(code[e])))
            ++e;
        std::size_t rest = e;
        while (rest < staff.end && std::isspace(static_cast<unsigned char>(code[rest])))
            ++rest;
        return rest >= staff.end && e > i + 1 ? code.substr(i + 1, e - i - 1) : std::string();
    };

    // The text no staff owns: staves and the variables they play blanked.
    std::string outside = code;
    const auto blank = [&](std::size_t b, std::size_t e) {
        for (std::size_t k = b; k < e && k < outside.size(); ++k)
            outside[k] = ' ';
    };
    std::vector<std::string> variables(all.size());
    std::vector<std::optional<std::pair<std::size_t, std::size_t>>> definitions(all.size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        blank(all[i].begin, all[i].end);
        variables[i] = variableOf(all[i]);
        if (!variables[i].empty()) {
            definitions[i] = variableDefinition(code, variables[i], scoreBegin);
            if (definitions[i])
                blank(definitions[i]->first, definitions[i]->second);
        }
    }
    const bool global = outside.find(banjoName) != std::string::npos;

    const auto tuningOfStaff = [&](std::size_t i) {
        const score::Staff& staff = all[i];
        TuningAt r{guitar(), i, {}};
        if (!staff.tuning.empty()) {
            if (staff.tuning == banjoName)
                r.tuning = banjoOpenG();
            else if (staff.tuning != "guitar-tuning")
                r.unknownName = staff.tuning;
            return r;
        }
        bool banjo = global || code.substr(staff.begin, staff.end - staff.begin).find(banjoName) !=
                                   std::string::npos;
        if (definitions[i])
            banjo = banjo || code.substr(definitions[i]->first,
                                         definitions[i]->second - definitions[i]->first)
                                     .find(banjoName) != std::string::npos;
        if (banjo)
            r.tuning = banjoOpenG();
        return r;
    };

    for (std::size_t i = 0; i < all.size(); ++i) {
        if (cursor >= all[i].begin && cursor <= all[i].end)
            return tuningOfStaff(i);
    }
    // Inside the definition of a variable: the staves that play it must agree.
    std::optional<TuningAt> agreed;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (!definitions[i] || cursor < definitions[i]->first || cursor > definitions[i]->second)
            continue;
        const TuningAt r = tuningOfStaff(i);
        if (agreed && (agreed->tuning.open != r.tuning.open ||
                       agreed->tuning.shortString != r.tuning.shortString))
            return result;
        if (!agreed)
            agreed = r;
    }
    return agreed ? *agreed : result;
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

namespace {

const Event* eventSpanning(const std::vector<Event>& events, std::size_t begin, std::size_t end) {
    for (const Event& e : events) {
        if (e.prefixBegin == begin && e.postEnd == end && !e.definition)
            return &e;
    }
    return nullptr;
}

} // namespace

std::vector<Position> positionsOfEvent(std::string_view text, std::size_t begin, std::size_t end,
                                       const Tuning& tuning, bool german) {
    const auto events = scan(text, german ? Names::German : Names::English);
    const Event* e = eventSpanning(events, begin, end);
    std::vector<Position> out;
    if (!e || e->rest || e->pitches.empty() || e->midi.size() != e->pitches.size())
        return out;
    for (std::size_t i = 0; i < e->midi.size(); ++i) {
        const int string = e->raw[i].string;
        if (string < 1 || static_cast<std::size_t>(string) > tuning.open.size())
            return {};
        out.push_back({string, e->midi[i] - tuning.open[static_cast<std::size_t>(string - 1)]});
    }
    return out;
}

std::optional<Replacement> replacePitches(std::string_view text, std::size_t eventBegin,
                                          std::size_t eventEnd, std::string_view token) {
    if (token.empty())
        return std::nullopt;
    const bool chordToken = token.front() == '<';
    std::string_view pitchPart, stringPart;
    if (chordToken) {
        if (token.size() < 3 || token.back() != '>')
            return std::nullopt;
    } else {
        const auto slash = token.rfind('\\');
        if (slash == std::string_view::npos || slash == 0 || slash + 1 >= token.size() ||
            !std::ranges::all_of(token.substr(slash + 1), isDigit))
            return std::nullopt;
        pitchPart = token.substr(0, slash);
        stringPart = token.substr(slash);
    }
    const auto events = scan(text, Names::Any);
    const Event* e = eventSpanning(events, eventBegin, eventEnd);
    if (!e)
        return std::nullopt;
    const bool single = !e->chord && !e->rest;
    if (single && e->pitches.empty()) // q
        return std::nullopt;
    if (e->chord && e->closeAt >= text.size())
        return std::nullopt;
    const std::size_t coreEnd =
        single ? std::max({e->durEnd, e->stringEnd, e->pitchEnd}) : e->durEnd;
    const std::string_view duration = text.substr(e->durBegin, e->durEnd - e->durBegin);
    std::string out;
    if (chordToken) {
        out += token;
        out += duration;
    } else {
        out += pitchPart;
        // `!` and `?` of the old note stay with its pitch.
        if (single && e->durBegin > e->pitchEnd)
            out += text.substr(e->pitchEnd, e->durBegin - e->pitchEnd);
        out += duration;
        out += stringPart;
    }
    return Replacement{e->begin, coreEnd, std::move(out)};
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
    // A single note: <pitch\N token>duration post-events. Its prefix function (`\deadNote`) and
    // `!` / `?` stay with its pitch.
    text.assign(line.substr(0, target->prefixBegin));
    text += '<';
    text += line.substr(target->prefixBegin, target->pitchEnd - target->prefixBegin);
    text += line.substr(target->pitchEnd, target->durBegin - target->pitchEnd);
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
