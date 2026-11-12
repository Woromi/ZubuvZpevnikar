#include <algorithm>
#include <vector>
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

Change insertChord(std::string_view line, std::size_t cursor, std::size_t selStart,
                   std::size_t selEnd) {
    cursor = std::min(cursor, line.size());
    selEnd = std::min(selEnd, line.size());
    if (selStart < selEnd) {
        std::string text(line.substr(0, selStart));
        text += "\\[";
        text += line.substr(selStart, selEnd - selStart);
        text += ']';
        const std::size_t after = text.size();
        text += line.substr(selEnd);
        return {std::move(text), after};
    }
    std::string text(line.substr(0, cursor));
    text += "\\[]";
    text += line.substr(cursor);
    return {std::move(text), cursor + 2};
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

} // namespace zub::songedit
