#pragma once

// The tokeniser of LilyPond score text that zub::fretboard and zub::scoreregion share: notes,
// chords and rests with their durations, strings and post-events. Private to core (not installed).

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/fretboard.hpp>

namespace zub::fretboard::scan_detail {

inline bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
inline bool isDigit(char c) {
    return c >= '0' && c <= '9';
}
inline bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
inline bool isWordChar(char c) {
    return isLetter(c) || isDigit(c) || c == '_';
}

// The pitch of a note name in the octave of `c` (= 48), nullopt if `word` is no pitch.
inline std::optional<int> parseNamed(std::string_view word, bool german) {
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

inline std::optional<int> parsePitchWord(std::string_view word, Names names) {
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
    // After `end` and the articulations written right against it (`^\strokeDown`, `-.`, `_"x"`,
    // more ( ) ~): the end of the whole token for a selection. Equals `end` for rests.
    std::size_t postEnd = 0;
    bool rest = false; // r, R or s
    // The whole value of a variable (`emChord = <e\\6 g\\3>4`): music, but of the definition, not
    // of a place in the score. Region actions leave it alone.
    bool definition = false;
    // Where the music functions written right before the event start (`\deadNote`, `\grace`,
    // `\tweak x #y`, `\once \override ...`): they belong to it. Equals `begin` without any.
    std::size_t prefixBegin = 0;
    bool chord = false;
    std::size_t closeAt = 0; // chord: the `>`
    // Single note only:
    std::size_t pitchEnd = 0;                   // after the pitch and its octave marks
    std::size_t stringBegin = 0, stringEnd = 0; // `\N`; empty when none
    // A written duration (notes, chords, rests); empty when none.
    std::size_t durBegin = 0, durEnd = 0;
    std::vector<std::size_t> pitches; // where each note's pitch starts (all notes of a chord)
    std::vector<Position> raw;        // {string, midi} (fret filled in later)
    std::vector<int> midi;
};

struct Scanner {
    std::string_view s;
    Names names;
    std::vector<Event> events;

    static constexpr std::size_t npos = std::string_view::npos;
    std::size_t pending_ = npos; // start of the music functions seen since the last token
    std::size_t carried_ = npos; // ... as the next event takes them

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

    // Post-events and articulations written directly after a note or chord from `i` on.
    std::size_t trail(std::size_t i) const {
        while (i < s.size()) {
            const char c = s[i];
            if (c == '(' || c == ')' || c == '~' || c == '[' || c == ']') {
                ++i;
                continue;
            }
            if (c != '^' && c != '_' && c != '-')
                break;
            std::size_t j = i + 1;
            if (j >= s.size())
                break;
            if (s[j] == '\\') {
                ++j;
                if (j >= s.size() || !isLetter(s[j]))
                    break;
                while (j < s.size() && isLetter(s[j]))
                    ++j;
            } else if (s[j] == '"') {
                j = skipString(j);
            } else if (isDigit(s[j])) {
                while (j < s.size() && isDigit(s[j]))
                    ++j;
            } else if (s[j] == '.' || s[j] == '>' || s[j] == '!' || s[j] == '+' || s[j] == '-' ||
                       s[j] == '^' || s[j] == '_') {
                ++j;
            } else {
                break;
            }
            i = j;
        }
        return i;
    }

    // The pitch arguments of `\key`, `\relative`, `\transpose`, `\transposition` are no notes.
    std::size_t skipPitchArguments(std::size_t i, const std::string_view command) const {
        int count = 0;
        if (command == "key" || command == "relative" || command == "transposition")
            count = 1;
        else if (command == "transpose")
            count = 2;
        for (; count > 0; --count) {
            std::size_t j = i;
            while (j < s.size() && isSpace(s[j]))
                ++j;
            std::size_t wordEnd;
            if (j >= s.size() || !isLetter(s[j]) || !pitchAt(j, wordEnd))
                break;
            int marks;
            i = octave(wordEnd, marks);
        }
        return i;
    }

    // Scheme after `#` or `$` at `i` (LilyPond's embedded Scheme): `##t`, `#'sym`, `#'(a b)`,
    // `#(...)` (balanced, strings and `;` comments aware), `#"text"`, an identifier such as
    // `#banjo-open-g-tuning`, a number `#-6`, `#{ ... #}`. Returns the end; none of it is music.
    std::size_t skipScheme(std::size_t i) const {
        ++i;
        if (i < s.size() && s[i] == '{') {
            const auto close = s.find("#}", i + 1);
            return close == std::string_view::npos ? s.size() : close + 2;
        }
        return skipDatum(i);
    }

    std::size_t skipDatum(std::size_t i) const {
        if (i >= s.size())
            return i;
        const char c = s[i];
        if (c == '\'' || c == '`' || c == ',')
            return skipDatum(i + 1);
        if (c == '"')
            return skipString(i);
        if (c == '(') {
            int depth = 0;
            while (i < s.size()) {
                const char d = s[i];
                if (d == '"') {
                    i = skipString(i);
                    continue;
                }
                if (d == ';') {
                    const auto nl = s.find('\n', i);
                    i = nl == std::string_view::npos ? s.size() : nl;
                    continue;
                }
                if (d == '#' && i + 1 < s.size() && s[i + 1] == '\\') { // a character: #\(
                    i += 3;
                    continue;
                }
                ++i;
                if (d == '(')
                    ++depth;
                else if (d == ')' && --depth == 0)
                    break;
            }
            return std::min(i, s.size());
        }
        if (c == '#') { // #t #f #\a #(vector)
            if (i + 1 < s.size() && s[i + 1] == '(')
                return skipDatum(i + 1);
            i += (i + 1 < s.size() && s[i + 1] == '\\') ? std::size_t{3} : std::size_t{1};
            i = std::min(i, s.size());
        }
        while (i < s.size() && !isSpace(s[i]) && s[i] != '(' && s[i] != ')' && s[i] != '"' &&
               s[i] != ';' && s[i] != '{' && s[i] != '}')
            ++i;
        return i;
    }

    // Is the text right before `i` `name =`?
    bool assignmentBefore(std::size_t i) const {
        while (i > 0 && isSpace(s[i - 1]))
            --i;
        if (i == 0 || s[i - 1] != '=')
            return false;
        --i;
        while (i > 0 && isSpace(s[i - 1]))
            --i;
        return i > 0 && isWordChar(s[i - 1]);
    }

    void add(Event e) {
        e.definition = assignmentBefore(e.begin);
        e.prefixBegin = carried_ != npos ? carried_ : e.begin;
        carried_ = npos;
        events.push_back(std::move(e));
    }

    std::size_t skipSpaces(std::size_t i) const {
        while (i < s.size() && isSpace(s[i]))
            ++i;
        return i;
    }

    // The `{ ... }` at `i`: its end (after the matching `}`). Strings, comments and Scheme inside
    // do not count.
    std::size_t skipBraced(std::size_t i) const {
        int depth = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == '%') {
                i = skipComment(i);
            } else if (c == '"') {
                i = skipString(i);
            } else if (c == '#' || c == '$') {
                i = skipScheme(i);
            } else if (c == '\\') {
                i = std::min(i + 2, s.size());
            } else {
                ++i;
                if (c == '{')
                    ++depth;
                else if (c == '}' && --depth <= 0)
                    break;
            }
        }
        return std::min(i, s.size());
    }

    // `{ ... }` when it follows (after spaces) at `i`, else `i`.
    std::size_t skipBracedArgument(std::size_t i) const {
        const std::size_t j = skipSpaces(i);
        return j < s.size() && s[j] == '{' ? skipBraced(j) : i;
    }

    // The argument(s) of `\markup` at `i`: `{ ... }`, a string, a word, Scheme, or commands with
    // such arguments (`\bold "x"`, `\override #'(a . b) { x }`).
    std::size_t skipMarkupArgument(std::size_t i, int depth = 0) const {
        const std::size_t j = skipSpaces(i);
        if (j >= s.size())
            return i;
        const char c = s[j];
        if (c == '{')
            return skipBraced(j);
        if (c == '"')
            return skipString(j);
        if (c == '#' || c == '$')
            return skipScheme(j);
        if (c == '\\' && j + 1 < s.size() && isLetter(s[j + 1]) && depth < 3) {
            std::size_t k = j + 1;
            while (k < s.size() && (isLetter(s[k]) || s[k] == '-'))
                ++k;
            // A command takes arguments; a few at most.
            for (int n = 0; n < 3; ++n) {
                const std::size_t next = skipSpaces(k);
                if (next >= s.size() || !(s[next] == '{' || s[next] == '"' || s[next] == '#' ||
                                          s[next] == '$' || s[next] == '\\'))
                    break;
                // Only the first argument may be another command (`\bold \italic "x"`); later
                // ones are music commands that follow the markup.
                if (s[next] == '\\' &&
                    (n > 0 || next + 1 >= s.size() || !isLetter(s[next + 1])))
                    break;
                k = skipMarkupArgument(next, depth + 1);
            }
            return k;
        }
        if (isLetter(c) || isDigit(c)) { // a bare word
            std::size_t k = j;
            while (k < s.size() && !isSpace(s[k]) && s[k] != '{' && s[k] != '}' && s[k] != '<' &&
                   s[k] != '>')
                ++k;
            return k;
        }
        return i;
    }

    // What a command skips after its name (at `i`): content that looks like notes but is chord
    // names, lyrics, text.
    std::size_t skipCommandArguments(std::string_view command, std::size_t i) const {
        if (command == "chordmode" || command == "chords" || command == "lyricmode" ||
            command == "lyrics" || command == "addlyrics" || command == "with")
            return skipBracedArgument(i);
        if (command == "lyricsto") {
            std::size_t j = skipSpaces(i);
            if (j < s.size() && s[j] == '"') {
                j = skipString(j);
            } else if (j < s.size() && (isLetter(s[j]) || s[j] == '\\')) {
                ++j;
                while (j < s.size() && (isWordChar(s[j]) || s[j] == '-'))
                    ++j;
            } else {
                return i;
            }
            return skipBracedArgument(j);
        }
        if (command == "markup" || command == "markuplist")
            return skipMarkupArgument(i);
        if (command == "mark") {
            const std::size_t j = skipSpaces(i);
            if (j < s.size() && s[j] == '"')
                return skipString(j);
            if (j < s.size() && (s[j] == '#' || s[j] == '$'))
                return skipScheme(j);
            if (j < s.size() && isDigit(s[j])) {
                std::size_t k = j;
                while (k < s.size() && isDigit(s[k]))
                    ++k;
                return k;
            }
            if (s.substr(j, 8) == "\\default")
                return j + 8;
        }
        return i;
    }

    // A word of an argument (`Slur.color`, `font-size`) at `i` after spaces; the end.
    std::size_t skipPropertyWord(std::size_t i) const {
        i = skipSpaces(i);
        while (i < s.size() && (isWordChar(s[i]) || s[i] == '.' || s[i] == '-'))
            ++i;
        return i;
    }

    // A value after spaces: Scheme or one word.
    std::size_t skipValue(std::size_t i) const {
        i = skipSpaces(i);
        if (i < s.size() && (s[i] == '#' || s[i] == '$'))
            return skipScheme(i);
        while (i < s.size() && !isSpace(s[i]))
            ++i;
        return i;
    }

    // The music functions that belong to the note that follows. Returns where the scan goes on, or
    // npos when `command` is none of them; remembers where the first one started.
    std::size_t prefixCommand(std::string_view command, std::size_t commandBegin,
                              std::size_t i) {
        if (command == "deadNote" || command == "grace" || command == "acciaccatura" ||
            command == "appoggiatura" || command == "slashedGrace" || command == "parenthesize" ||
            command == "once") {
            if (pending_ == npos)
                pending_ = commandBegin;
            return i;
        }
        if (command == "tweak") {
            if (pending_ == npos)
                pending_ = commandBegin;
            return skipValue(skipPropertyWord(i));
        }
        if (command == "override" || command == "set") {
            // Only `\once \override` belongs to the next note (the others last).
            i = skipPropertyWord(i);
            const std::size_t eq = skipSpaces(i);
            if (eq < s.size() && s[eq] == '=')
                i = skipValue(eq + 1);
            return i;
        }
        if (command == "revert" || command == "unset")
            return skipPropertyWord(i);
        return npos;
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
            if (c == '#' || c == '$') {
                j = skipScheme(j);
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
                    e.pitches.push_back(j);
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
            e.postEnd = s.size();
            add(std::move(e));
            return;
        }
        e.closeAt = j;
        std::size_t k = j + 1;
        k = skipDuration(k, e.durBegin, e.durEnd);
        e.end = skipPost(k);
        e.postEnd = trail(e.end);
        add(std::move(e));
    }

    void run() {
        std::size_t i = 0;
        while (i < s.size()) {
            const char c = s[i];
            // Spaces and comments keep the music functions that wait for their note.
            if (isSpace(c)) {
                ++i;
                continue;
            }
            if (c == '%') {
                i = skipComment(i);
                continue;
            }
            if (c == '\\' && i + 1 < s.size() && isLetter(s[i + 1])) {
                std::size_t nameEnd = i + 1;
                while (nameEnd < s.size() && isLetter(s[nameEnd]))
                    ++nameEnd;
                const std::size_t next =
                    prefixCommand(s.substr(i + 1, nameEnd - i - 1), i, nameEnd);
                if (next != npos) {
                    i = next;
                    continue;
                }
            }
            carried_ = pending_;
            pending_ = npos;
            if (c == '"') {
                i = skipString(i);
            } else if (c == '#' || c == '$') {
                i = skipScheme(i);
            } else if (c == '\\') {
                ++i;
                if (i < s.size() && isLetter(s[i])) {
                    const std::size_t nameBegin = i;
                    while (i < s.size() && isLetter(s[i]))
                        ++i;
                    const std::string_view name = s.substr(nameBegin, i - nameBegin);
                    i = skipPitchArguments(i, name);
                    i = skipCommandArguments(name, i);
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
                    e.pitches.push_back(i);
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
                    e.postEnd = trail(e.end);
                    add(std::move(e));
                    i = std::max(i + 1, events.back().end);
                } else if (word == "r" || word == "s" || word == "R") {
                    Event e;
                    e.begin = i;
                    e.rest = true;
                    std::size_t k = skipDuration(wordEnd, e.durBegin, e.durEnd);
                    e.end = k;
                    e.postEnd = k;
                    add(std::move(e));
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

// Are the `{ }` of the text balanced (not counting strings, comments, Scheme)?
inline bool bracesBalanced(std::string_view text) {
    const Scanner scanner{text, Names::Any, {}};
    int depth = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '%') {
            i = scanner.skipComment(i);
        } else if (c == '"') {
            i = scanner.skipString(i);
        } else if (c == '#' || c == '$') {
            i = scanner.skipScheme(i);
        } else if (c == '\\') {
            i = std::min(i + 2, text.size());
        } else {
            ++i;
            if (c == '{')
                ++depth;
            else if (c == '}' && --depth < 0)
                return false;
        }
    }
    return depth == 0;
}

inline std::vector<Event> scan(std::string_view text, Names names) {
    Scanner scanner{text, names, {}};
    scanner.run();
    return std::move(scanner.events);
}

} // namespace zub::fretboard::scan_detail
