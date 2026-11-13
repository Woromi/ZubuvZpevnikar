#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <zub/core/fretboard.hpp>

namespace zub::fretboard {

namespace {

bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool isDigit(char c) {
    return c >= '0' && c <= '9';
}
bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
bool isWordChar(char c) {
    return isLetter(c) || isDigit(c) || c == '_';
}

// The pitch of a note name in the octave of `c` (= 48), nullopt if `word` is no pitch.
std::optional<int> parseNamed(std::string_view word, bool german) {
    if (word.empty())
        return std::nullopt;
    if (german && word == "b")
        return 58;
    const char base = word.front();
    int pc;
    if (german) {
        switch (base) {
        case 'c':
            pc = 0;
            break;
        case 'd':
            pc = 2;
            break;
        case 'e':
            pc = 4;
            break;
        case 'f':
            pc = 5;
            break;
        case 'g':
            pc = 7;
            break;
        case 'a':
            pc = 9;
            break;
        case 'h':
            pc = 11;
            break;
        default:
            return std::nullopt;
        }
    } else {
        switch (base) {
        case 'c':
            pc = 0;
            break;
        case 'd':
            pc = 2;
            break;
        case 'e':
            pc = 4;
            break;
        case 'f':
            pc = 5;
            break;
        case 'g':
            pc = 7;
            break;
        case 'a':
            pc = 9;
            break;
        case 'b':
            pc = 11;
            break;
        default:
            return std::nullopt;
        }
    }
    const std::string_view rest = word.substr(1);
    int alt;
    if (rest.empty())
        alt = 0;
    else if (german) {
        if (rest == "is")
            alt = 1;
        else if (rest == "isis")
            alt = 2;
        else if (rest == "es" || (rest == "s" && (base == 'e' || base == 'a')))
            alt = -1;
        else if (rest == "eses" || (rest == "ses" && (base == 'e' || base == 'a')))
            alt = -2;
        else
            return std::nullopt;
        if (base == 'h' && alt < 0)
            return std::nullopt;
    } else {
        if (rest == "s")
            alt = 1;
        else if (rest == "ss")
            alt = 2;
        else if (rest == "f")
            alt = -1;
        else if (rest == "ff")
            alt = -2;
        else
            return std::nullopt;
    }
    return 48 + pc + alt;
}

// Which note names to read: Any accepts both languages (for durations and spacing, where the
// pitch does not matter).
enum class Names { English, German, Any };

std::optional<int> parsePitchWord(std::string_view word, Names names) {
    if (names == Names::Any) {
        if (const auto g = parseNamed(word, true))
            return g;
        return parseNamed(word, false);
    }
    return parseNamed(word, names == Names::German);
}

// One note or chord found in the text; positions are byte offsets into it.
struct Event {
    std::size_t begin = 0; // `<`, or the start of the pitch
    std::size_t end = 0;   // after the duration, string and the simple post-events ( ) ~ [ ]
    bool chord = false;
    std::size_t closeAt = 0; // chord: the `>`
    // Single note only:
    std::size_t pitchEnd = 0;                   // after the pitch and its octave marks
    std::size_t stringBegin = 0, stringEnd = 0; // `\N`; empty when none
    // A written duration (notes, chords, rests); empty when none.
    std::size_t durBegin = 0, durEnd = 0;
    std::vector<Position> raw; // {string, midi} (fret filled in later)
    std::vector<int> midi;
};

struct Scanner {
    std::string_view s;
    Names names;
    std::vector<Event> events;

    std::size_t skipDuration(std::size_t i, std::size_t& db, std::size_t& de) const {
        db = de = i;
        std::size_t j = i;
        while (j < s.size() && isDigit(s[j]))
            ++j;
        if (j == i || j - i > 3)
            return i;
        while (j < s.size() && s[j] == '.')
            ++j;
        db = i;
        de = j;
        return j;
    }

    // Octave marks after a pitch word at `i`.
    std::size_t octave(std::size_t i, int& marks) const {
        marks = 0;
        while (i < s.size() && (s[i] == '\'' || s[i] == ',')) {
            marks += s[i] == '\'' ? 1 : -1;
            ++i;
        }
        return i;
    }

    // `\N` at `i`; returns the end (or i), `n` the string.
    std::size_t stringAt(std::size_t i, int& n) const {
        n = 0;
        if (i + 1 >= s.size() || s[i] != '\\' || !isDigit(s[i + 1]))
            return i;
        std::size_t j = i + 1;
        while (j < s.size() && isDigit(s[j])) {
            if (n < 1000)
                n = n * 10 + (s[j] - '0');
            ++j;
        }
        return j;
    }

    std::size_t skipPost(std::size_t i) const {
        while (i < s.size() &&
               (s[i] == '(' || s[i] == ')' || s[i] == '~' || s[i] == '[' || s[i] == ']'))
            ++i;
        return i;
    }

    std::size_t skipComment(std::size_t i) const {
        if (i + 1 < s.size() && s[i + 1] == '{') {
            const auto close = s.find("%}", i + 2);
            return close == std::string_view::npos ? s.size() : close + 2;
        }
        const auto nl = s.find('\n', i);
        return nl == std::string_view::npos ? s.size() : nl;
    }

    std::size_t skipString(std::size_t i) const {
        ++i;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\')
                ++i;
            ++i;
        }
        return std::min(i + 1, s.size());
    }

    // A pitch word at `i` (maximal run of letters): its midi without octave marks, and the end.
    std::optional<int> pitchAt(std::size_t i, std::size_t& wordEnd) const {
        wordEnd = i;
        while (wordEnd < s.size() && isLetter(s[wordEnd]))
            ++wordEnd;
        return parsePitchWord(s.substr(i, wordEnd - i), names);
    }

    void chordAt(std::size_t i) {
        Event e;
        e.chord = true;
        e.begin = i;
        std::size_t j = i + 1;
        bool closed = false;
        while (j < s.size()) {
            const char c = s[j];
            if (c == '>') {
                closed = true;
                break;
            }
            if (c == '%') {
                j = skipComment(j);
                continue;
            }
            if (c == '"') {
                j = skipString(j);
                continue;
            }
            if (c == '\\') {
                ++j;
                if (j < s.size() && isLetter(s[j])) {
                    while (j < s.size() && isLetter(s[j]))
                        ++j;
                } else if (j < s.size()) {
                    ++j;
                }
                continue;
            }
            if (isLetter(c) && (j == i + 1 || !isWordChar(s[j - 1]))) {
                std::size_t wordEnd;
                const auto base = pitchAt(j, wordEnd);
                if (base) {
                    int marks;
                    std::size_t k = octave(wordEnd, marks);
                    while (k < s.size() && (s[k] == '!' || s[k] == '?'))
                        ++k;
                    int n;
                    const std::size_t strEnd = stringAt(k, n);
                    if (strEnd != k && n >= 1)
                        e.raw.push_back({n, 0}), e.midi.push_back(*base + 12 * marks);
                    j = strEnd;
                } else {
                    j = wordEnd;
                }
                continue;
            }
            ++j;
        }
        if (!closed) {
            // An unclosed chord: it runs to the end of the text.
            e.closeAt = s.size();
            e.end = s.size();
            events.push_back(std::move(e));
            return;
        }
        e.closeAt = j;
        std::size_t k = j + 1;
        k = skipDuration(k, e.durBegin, e.durEnd);
        e.end = skipPost(k);
        events.push_back(std::move(e));
    }

    void run() {
        std::size_t i = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '%') {
                i = skipComment(i);
            } else if (c == '"') {
                i = skipString(i);
            } else if (c == '\\') {
                ++i;
                if (i < s.size() && isLetter(s[i])) {
                    while (i < s.size() && isLetter(s[i]))
                        ++i;
                } else if (i < s.size()) {
                    ++i;
                }
            } else if (c == '<') {
                if (i + 1 < s.size() && s[i + 1] == '<') {
                    i += 2;
                } else {
                    chordAt(i);
                    i = std::max(i + 1, events.back().end);
                }
            } else if (isLetter(c) && (i == 0 || !(isWordChar(s[i - 1]) || s[i - 1] == '\\'))) {
                std::size_t wordEnd;
                const auto base = pitchAt(i, wordEnd);
                const std::string_view word = s.substr(i, wordEnd - i);
                if (base) {
                    Event e;
                    e.begin = i;
                    int marks;
                    std::size_t k = octave(wordEnd, marks);
                    e.pitchEnd = k;
                    while (k < s.size() && (s[k] == '!' || s[k] == '?'))
                        ++k;
                    k = skipDuration(k, e.durBegin, e.durEnd);
                    int n;
                    const std::size_t strEnd = stringAt(k, n);
                    if (strEnd != k) {
                        e.stringBegin = k;
                        e.stringEnd = strEnd;
                        if (n >= 1) {
                            e.raw.push_back({n, 0});
                            e.midi.push_back(*base + 12 * marks);
                        }
                        k = strEnd;
                    }
                    e.end = skipPost(k);
                    events.push_back(std::move(e));
                    i = std::max(i + 1, events.back().end);
                } else if (word == "r" || word == "s" || word == "R") {
                    Event e;
                    e.begin = i;
                    std::size_t k = skipDuration(wordEnd, e.durBegin, e.durEnd);
                    e.end = k;
                    events.push_back(std::move(e));
                    i = std::max(i + 1, k);
                } else {
                    i = wordEnd;
                }
            } else {
                ++i;
            }
        }
    }
};

std::vector<Event> scan(std::string_view text, Names names) {
    Scanner scanner{text, names, {}};
    scanner.run();
    return std::move(scanner.events);
}

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
        if (e.begin < cursor && cursor < e.end) {
            cursor = e.end;
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
        if (e.begin <= cursor && cursor <= e.end && !e.midi.empty())
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

Change insertNote(std::string_view line, std::size_t cursor, std::string_view token,
                  std::string_view duration) {
    return insertToken(line, cursor, withDuration(token, duration));
}

Change addToChord(std::string_view line, std::size_t cursor, std::string_view token) {
    cursor = std::min(cursor, line.size());
    const auto events = scan(line, Names::Any);
    const Event* target = nullptr;
    for (const Event& e : events) {
        if (e.begin >= cursor)
            break;
        const bool inside = cursor <= e.end;
        const bool justAfter =
            e.end < cursor && std::ranges::all_of(line.substr(e.end, cursor - e.end),
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

Change insertRest(std::string_view line, std::size_t cursor, std::string_view duration) {
    return insertToken(line, cursor, std::string("r") + std::string(duration));
}

Change insertBarCheck(std::string_view line, std::size_t cursor) {
    return insertToken(line, cursor, "|");
}

} // namespace zub::fretboard
