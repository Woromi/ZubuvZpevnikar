#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <vector>
#include <zub/core/latex.hpp>
#include <zub/core/songedit.hpp>

namespace zub::songedit {

namespace {

// The "\[...]" chords of a line, in order. A backslash escapes the next character ("\\[" is no
// chord); a "\[" without "]" is no chord.
std::vector<ChordSpan> chordsIn(std::string_view line) {
    std::vector<ChordSpan> chords;
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] != '\\') {
            ++i;
        } else if (i + 1 < line.size() && line[i + 1] == '[') {
            const auto close = line.find(']', i + 2);
            if (close == std::string_view::npos)
                break;
            chords.push_back({i, close + 1});
            i = close + 1;
        } else {
            i += 2;
        }
    }
    return chords;
}

bool isContinuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
}

// The start of the code point before `pos` (> 0).
std::size_t previousCodePoint(std::string_view text, std::size_t pos) {
    --pos;
    while (pos > 0 && isContinuation(text[pos]))
        --pos;
    return pos;
}

// The end of the code point starting at `pos` (< size).
std::size_t nextCodePoint(std::string_view text, std::size_t pos) {
    ++pos;
    while (pos < text.size() && isContinuation(text[pos]))
        ++pos;
    return pos;
}

bool isWordByte(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') ||
           (u >= 'a' && u <= 'z') || c == '\'';
}

const ChordSpan* startingAt(const std::vector<ChordSpan>& chords, std::size_t pos) {
    for (const auto& chord : chords)
        if (chord.begin == pos)
            return &chord;
    return nullptr;
}

const ChordSpan* endingAt(const std::vector<ChordSpan>& chords, std::size_t pos) {
    for (const auto& chord : chords)
        if (chord.end == pos)
            return &chord;
    return nullptr;
}

// Where the chord, taken out of `line`, goes when moved right from its end `from`; npos if it
// cannot move. Positions are in `line`.
std::size_t targetRight(std::string_view line, const std::vector<ChordSpan>& chords,
                        std::size_t from, Step step) {
    const std::size_t size = line.size();
    if (from >= size)
        return std::string_view::npos;
    if (step == Step::Letter) {
        if (const auto* next = startingAt(chords, from))
            return next->end;
        return nextCodePoint(line, from);
    }
    std::size_t i = from;
    while (i < size) { // the rest of the current word
        if (const auto* chord = startingAt(chords, i))
            i = chord->end;
        else if (isWordByte(line[i]))
            ++i;
        else
            break;
    }
    std::size_t landing = i;
    while (i < size) { // what separates it from the next word
        if (const auto* chord = startingAt(chords, i)) {
            i = chord->end;
        } else if (!isWordByte(line[i])) {
            ++i;
            landing = i;
        } else {
            break;
        }
    }
    return i >= size ? size : landing;
}

std::size_t targetLeft(std::string_view line, const std::vector<ChordSpan>& chords,
                       std::size_t from, Step step) {
    if (from == 0)
        return std::string_view::npos;
    if (step == Step::Letter) {
        if (const auto* prev = endingAt(chords, from))
            return prev->begin;
        return previousCodePoint(line, from);
    }
    std::size_t i = from;
    while (i > 0) { // what separates the previous word from here
        if (const auto* chord = endingAt(chords, i))
            i = chord->begin;
        else if (!isWordByte(line[i - 1]))
            --i;
        else
            break;
    }
    std::size_t landing = i;
    while (i > 0) { // the word itself
        if (const auto* chord = endingAt(chords, i)) {
            i = chord->begin;
        } else if (isWordByte(line[i - 1])) {
            i = previousCodePoint(line, i);
            landing = i;
        } else {
            break;
        }
    }
    return landing;
}

std::string_view environmentName(Part part) {
    switch (part) {
    case Part::Verse:
    case Part::VerseUnnumbered:
        return "verse";
    case Part::Chorus:
        return "chorus";
    case Part::PreChorus:
        return "prechorus";
    case Part::Bridge:
        return "bridge";
    case Part::Intro:
        return "intro";
    case Part::Outro:
        return "outro";
    case Part::Instrumental:
        return "instrumental";
    case Part::ChorusAgain:
    case Part::ChorusReminder:
    case Part::SongHeader:
        break;
    }
    return {};
}

} // namespace

namespace {

// The chord that `pos` is strictly inside ("\\[" < pos < "]" end), if any.
std::optional<ChordSpan> chordContaining(std::string_view line, std::size_t pos) {
    for (const auto& chord : chordsIn(line))
        if (pos > chord.begin && pos < chord.end)
            return chord;
    return std::nullopt;
}

// Inserts `piece` at `pos`; the cursor goes `inside` bytes into it.
Change insertAt(std::string_view line, std::size_t pos, std::string_view piece,
                std::size_t inside) {
    std::string text(line.substr(0, pos));
    text += piece;
    text += line.substr(pos);
    return {std::move(text), pos + inside};
}

} // namespace

Change insertChord(std::string_view line, std::size_t cursor, std::size_t selStart,
                   std::size_t selEnd) {
    cursor = std::min(cursor, line.size());
    selEnd = std::min(selEnd, line.size());
    const bool selected = selStart < selEnd;
    // Chords do not nest: inside one, the new chord goes right after it.
    if (const auto chord = chordContaining(line, selected ? selStart : cursor))
        return insertAt(line, chord->end, "\\[]", 2);
    if (selected) {
        std::string text(line.substr(0, selStart));
        text += "\\[";
        text += line.substr(selStart, selEnd - selStart);
        text += ']';
        const std::size_t after = text.size();
        text += line.substr(selEnd);
        return {std::move(text), after};
    }
    return insertAt(line, cursor, "\\[]", 2);
}

Change insertMultivoice(std::string_view line, std::size_t cursor, std::size_t selStart,
                        std::size_t selEnd) {
    cursor = std::min(cursor, line.size());
    selEnd = std::min(selEnd, line.size());
    constexpr std::string_view head = "{\\multivoice{";
    if (selStart < selEnd) {
        const std::string wrapped = "{" + std::string(line.substr(selStart, selEnd - selStart)) + "}";
        if (latex::findMatching(wrapped, 0) != wrapped.size() - 1)
            selStart = selEnd = cursor; // unbalanced braces would break the command: no selection
    }
    const bool selected = selStart < selEnd && !chordContaining(line, selStart) &&
                          !chordContaining(line, selEnd);
    if (selected) {
        std::string text(line.substr(0, selStart));
        text += head;
        text += line.substr(selStart, selEnd - selStart);
        text += "}{";
        const std::size_t inside = text.size();
        text += "}}";
        text += line.substr(selEnd);
        return {std::move(text), inside};
    }
    std::size_t pos = cursor;
    if (selStart < selEnd) {
        if (const auto chord = chordContaining(line, selStart))
            pos = chord->end;
    } else if (const auto chord = chordContaining(line, cursor)) {
        pos = chord->end;
    }
    return insertAt(line, pos, "{\\multivoice{}{}}", head.size());
}

std::optional<ChordSpan> chordAt(std::string_view line, std::size_t cursor) {
    const auto chords = chordsIn(line);
    for (const auto& chord : chords)
        if (cursor > chord.begin && cursor < chord.end)
            return chord;
    if (const auto* chord = startingAt(chords, cursor))
        return *chord;
    if (const auto* chord = endingAt(chords, cursor))
        return *chord;
    return std::nullopt;
}

std::optional<std::size_t> spaceTarget(std::string_view line, std::size_t cursor) {
    for (const auto& chord : chordsIn(line))
        if (cursor >= chord.begin + 2 && cursor < chord.end)
            return chord.end;
    return std::nullopt;
}

std::optional<Change> moveChord(std::string_view line, std::size_t cursor, int direction,
                                Step step) {
    const auto span = chordAt(line, cursor);
    if (!span || direction == 0)
        return std::nullopt;
    const auto chords = chordsIn(line);
    const std::size_t inside = cursor - span->begin;
    const std::size_t length = span->end - span->begin;
    const std::string_view chord = line.substr(span->begin, length);
    std::string text;
    std::size_t begin = 0;
    if (direction > 0) {
        const std::size_t target = targetRight(line, chords, span->end, step);
        if (target == std::string_view::npos || target <= span->end)
            return std::nullopt;
        text = std::string(line.substr(0, span->begin));
        text += line.substr(span->end, target - span->end);
        begin = text.size();
        text += chord;
        text += line.substr(target);
    } else {
        const std::size_t target = targetLeft(line, chords, span->begin, step);
        if (target == std::string_view::npos || target >= span->begin)
            return std::nullopt;
        text = std::string(line.substr(0, target));
        begin = text.size();
        text += chord;
        text += line.substr(target, span->begin - target);
        text += line.substr(span->end);
    }
    return Change{std::move(text), begin + inside};
}

namespace {

// The "\\role{..}" or "\\role{..}{..}" command starting at `pos`: its end and the position
// just inside its first braces. nullopt when `pos` is no such command.
struct RoleSpan {
    std::size_t end;
    std::size_t firstArgument;
};

std::optional<RoleSpan> roleAt(std::string_view line, std::size_t pos) {
    constexpr std::string_view name = "\\role{";
    if (line.substr(pos, name.size()) != name)
        return std::nullopt;
    const std::size_t open = pos + name.size() - 1;
    const auto close = latex::findMatching(line, open);
    if (!close)
        return std::nullopt;
    std::size_t end = *close + 1;
    if (end < line.size() && line[end] == '{') {
        if (const auto second = latex::findMatching(line, end))
            end = *second + 1;
    }
    return RoleSpan{end, open + 1};
}

std::string_view trimRight(std::string_view s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.remove_suffix(1);
    return s;
}

} // namespace

Change insertRole(std::string_view line, std::size_t cursor, std::size_t selStart,
                  std::size_t selEnd) {
    (void)cursor;
    const std::string_view body = trimRight(line);
    // A line that already ends with a role: the cursor goes into its first argument.
    for (std::size_t from = body.size(); from > 0;) {
        const std::size_t pos = body.rfind("\\role{", from - 1);
        if (pos == std::string_view::npos)
            break;
        if (const auto role = roleAt(body, pos); role && role->end == body.size())
            return {std::string(line), role->firstArgument};
        if (pos == 0)
            break;
        from = pos;
    }
    selEnd = std::min(selEnd, line.size());
    std::string_view name;
    if (selStart < selEnd) {
        name = line.substr(selStart, selEnd - selStart);
        const std::string wrapped = "{" + std::string(name) + "}";
        if (latex::findMatching(wrapped, 0) != wrapped.size() - 1)
            name = {}; // unbalanced braces would break the command
    }
    std::string text(body);
    if (!body.empty())
        text += ' ';
    text += "\\role{";
    text += name;
    const std::size_t inside = text.size();
    text += '}';
    return {std::move(text), inside};
}

namespace {

// Something removeElementAt can take out: [begin, end) and what stands in its place.
struct Element {
    std::size_t begin;
    std::size_t end;
    std::string replacement;
};

std::vector<Element> elementsIn(std::string_view line) {
    constexpr std::string_view multivoice = "\\multivoice{";
    std::vector<Element> elements;
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] != '\\') {
            ++i;
            continue;
        }
        if (i + 1 < line.size() && line[i + 1] == '[') {
            const auto close = line.find(']', i + 2);
            if (close == std::string_view::npos)
                break;
            elements.push_back({i, close + 1, {}});
            i = close + 1;
        } else if (const auto role = roleAt(line, i)) {
            const std::size_t begin = i > 0 && line[i - 1] == ' ' ? i - 1 : i;
            elements.push_back({begin, role->end, {}});
            i += 5; // the content may hold chords
        } else if (line.substr(i, multivoice.size()) == multivoice) {
            const std::size_t open = i + multivoice.size() - 1;
            const auto first = latex::findMatching(line, open);
            if (first && *first + 1 < line.size() && line[*first + 1] == '{') {
                const auto second = latex::findMatching(line, *first + 1);
                if (second) {
                    Element element{i, *second + 1,
                                    std::string(line.substr(open + 1, *first - open - 1))};
                    if (i > 0 && line[i - 1] == '{' && latex::findMatching(line, i - 1) ==
                                                           std::optional<std::size_t>(*second + 1)) {
                        element.begin = i - 1;
                        element.end = *second + 2;
                    }
                    elements.push_back(std::move(element));
                }
            }
            i += multivoice.size() - 1;
        } else {
            i += 2;
        }
    }
    return elements;
}

} // namespace

std::optional<Change> removeElementAt(std::string_view line, std::size_t cursor) {
    const Element* best = nullptr;
    const auto elements = elementsIn(line);
    for (const auto& element : elements) {
        if (cursor < element.begin || cursor > element.end)
            continue;
        if (!best) {
            best = &element;
            continue;
        }
        const std::size_t size = element.end - element.begin;
        const std::size_t bestSize = best->end - best->begin;
        // Smallest wins; where two meet, the one that starts at the cursor.
        if (size < bestSize || (size == bestSize && element.begin == cursor && best->begin != cursor))
            best = &element;
    }
    if (!best)
        return std::nullopt;
    std::string text(line.substr(0, best->begin));
    text += best->replacement;
    text += line.substr(best->end);
    return Change{std::move(text), best->begin};
}

Snippet partSnippet(Part part, std::string_view selected) {
    if (part == Part::ChorusAgain)
        return {"\\insertchorus\n", 14};
    if (part == Part::ChorusReminder)
        return {"\\insertchorus*\n", 15};

    std::string body(selected);
    if (!body.empty() && body.back() != '\n')
        body += '\n';

    if (part == Part::SongHeader) {
        const std::string head = "\\beginsong{}[\n    by={},\n    lang={czech},\n]\n\n";
        std::string text = head + (body.empty() ? "\n" : body) + "\\endsong\n";
        return {std::move(text), 11}; // between the braces of the title
    }

    const std::string name(environmentName(part));
    std::string text = "\\begin" + name;
    if (part == Part::VerseUnnumbered)
        text += '*';
    text += '\n';
    const std::size_t cursorInside = text.size();
    text += body.empty() ? "\n" : body;
    const std::size_t cursor = body.empty() ? cursorInside : text.size();
    text += "\\end" + name + "\n";
    return {std::move(text), cursor};
}

namespace {

bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

// True when a "%" (not escaped) comes in text[lineStart, pos).
bool inComment(std::string_view text, std::size_t lineStart, std::size_t pos) {
    for (std::size_t i = lineStart; i < pos; ++i) {
        if (text[i] == '\\')
            ++i;
        else if (text[i] == '%')
            return true;
    }
    return false;
}

} // namespace

namespace {

struct ScoreReference {
    std::size_t start; // at the backslash of \lilypondfile or \narrowlilypondfile
    std::size_t end;   // after the closing brace
    bool narrow;
};

// The first reference to `inputName` that starts at or after `from`, outside comments.
std::optional<ScoreReference> findScoreReference(const std::string& text,
                                                 std::string_view inputName, std::size_t from) {
    constexpr std::string_view keyword = "lilypondfile";
    std::size_t pos = from;
    while ((pos = text.find(keyword, pos)) != std::string::npos) {
        const std::size_t keywordPos = pos;
        pos += keyword.size();
        std::size_t start = keywordPos;
        bool narrow = false;
        if (keywordPos >= 7 && text.compare(keywordPos - 7, 7, "\\narrow") == 0) {
            start = keywordPos - 7;
            narrow = true;
        } else if (keywordPos >= 1 && text[keywordPos - 1] == '\\') {
            start = keywordPos - 1;
        } else {
            continue;
        }
        if (start < from)
            continue;
        std::size_t i = pos;
        if (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i])))
            continue;
        const auto skipSpace = [&] {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
        };
        skipSpace();
        if (i < text.size() && text[i] == '[') {
            const auto closeBracket = text.find(']', i);
            if (closeBracket == std::string::npos)
                continue;
            i = closeBracket + 1;
            skipSpace();
        }
        if (i >= text.size() || text[i] != '{')
            continue;
        const auto closeBrace = text.find('}', i);
        if (closeBrace == std::string::npos)
            continue;
        if (trimmed(std::string_view(text).substr(i + 1, closeBrace - i - 1)) != inputName)
            continue;
        std::size_t lineStart = text.rfind('\n', start);
        lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
        if (inComment(text, lineStart, start))
            continue;
        return ScoreReference{start, closeBrace + 1, narrow};
    }
    return std::nullopt;
}

} // namespace

std::string removeScoreReference(std::string_view text, std::string_view inputName) {
    std::string result(text);
    std::size_t from = 0;
    while (const auto ref = findScoreReference(result, inputName, from)) {
        std::size_t start = ref->start;
        std::size_t end = ref->end;
        std::size_t lineStart = result.rfind('\n', start);
        lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
        std::size_t lineEnd = result.find('\n', end);
        if (lineEnd == std::string::npos)
            lineEnd = result.size();
        const auto blank = [&](std::size_t a, std::size_t b) {
            return std::all_of(result.begin() + static_cast<std::ptrdiff_t>(a),
                               result.begin() + static_cast<std::ptrdiff_t>(b), isBlank);
        };
        if (blank(lineStart, start) && blank(end, lineEnd)) {
            start = lineStart;
            end = lineEnd;
            if (end < result.size())
                ++end; // its line break
            else if (start > 0)
                --start; // last line without a break: take the break before it
        }
        result.erase(start, end - start);
        from = start;
    }
    return result;
}

std::optional<bool> scoreIsNarrow(std::string_view text, std::string_view inputName) {
    const std::string copy(text);
    if (const auto ref = findScoreReference(copy, inputName, 0))
        return ref->narrow;
    return std::nullopt;
}

std::string setScoreWidth(std::string_view text, std::string_view inputName, bool narrow) {
    std::string result(text);
    std::size_t from = 0;
    while (const auto ref = findScoreReference(result, inputName, from)) {
        const std::size_t nameLength = ref->narrow ? std::string_view("\\narrowlilypondfile").size()
                                                   : std::string_view("\\lilypondfile").size();
        if (ref->narrow != narrow)
            result.replace(ref->start, nameLength,
                           narrow ? "\\narrowlilypondfile" : "\\lilypondfile");
        from = ref->start + 1;
    }
    return result;
}

} // namespace zub::songedit
