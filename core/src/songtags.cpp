#include "zub/core/songtags.hpp"

#include "zub/core/latex.hpp"

#include <algorithm>
#include <cstddef>

namespace zub::tags {

namespace {

constexpr std::size_t npos = std::string_view::npos;

bool isBlank(char c) {
    return c == ' ' || c == '\t';
}

bool isSpace(char c) {
    return isBlank(c) || c == '\n' || c == '\r';
}

bool isAsciiAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
}

// Like latex::stripComments, but the comment text becomes spaces so offsets stay valid.
std::string maskComments(std::string_view tex) {
    std::string out{tex};
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\\') {
            ++i;
        } else if (out[i] == '%') {
            while (i < out.size() && out[i] != '\n')
                out[i++] = ' ';
        }
    }
    return out;
}

std::size_t skipSpaces(std::string_view s, std::size_t pos) {
    while (pos < s.size() && isSpace(s[pos]))
        ++pos;
    return pos;
}

std::size_t findComma(std::string_view s, std::size_t start, std::size_t end) {
    int depth = 0;
    for (std::size_t i = start; i < end; ++i) {
        const char c = s[i];
        if (c == '\\')
            ++i;
        else if (c == '{')
            ++depth;
        else if (c == '}')
            depth = depth > 0 ? depth - 1 : 0;
        else if (c == ',' && depth == 0)
            return i;
    }
    return npos;
}

struct Entry {
    std::string name;
    std::size_t nameBegin; // first character of the name
    std::size_t valBegin;  // first character of the value (the '{' of a braced one)
    std::size_t valEnd;    // after the value
    bool braced;
};

// The key=value entries of the options text [begin, end) of `text` (comments masked).
std::vector<Entry> entriesOf(std::string_view text, std::size_t begin, std::size_t end) {
    std::vector<Entry> entries;
    std::size_t i = begin;
    while (i < end) {
        if (isSpace(text[i]) || text[i] == ',') {
            ++i;
            continue;
        }
        std::size_t nameEnd = i;
        while (nameEnd < end && isAsciiAlnum(text[nameEnd]))
            ++nameEnd;
        std::size_t k = skipSpaces(text, nameEnd);
        if (k >= end || text[k] != '=') {
            const auto next = findComma(text, i, end);
            if (next == npos)
                break;
            i = next + 1;
            continue;
        }
        k = skipSpaces(text, k + 1);
        Entry e{std::string{text.substr(i, nameEnd - i)}, i, k, k, false};
        if (k < end && text[k] == '{') {
            const auto close = latex::findMatching(text, k);
            e.braced = true;
            e.valEnd = close && *close < end ? *close + 1 : end;
            i = e.valEnd;
        } else {
            const auto next = findComma(text, k, end);
            std::size_t stop = next == npos ? end : next;
            while (stop > k && isSpace(text[stop - 1]))
                --stop;
            e.valEnd = stop;
            i = next == npos ? end : next;
        }
        if (nameEnd > e.nameBegin)
            entries.push_back(std::move(e));
    }
    return entries;
}

std::size_t lineStart(std::string_view text, std::size_t pos) {
    const auto nl = text.rfind('\n', pos == 0 ? 0 : pos - 1);
    return pos == 0 || nl == npos ? 0 : nl + 1;
}

// Start of the next line after the one holding `pos` (text.size() if none).
std::size_t nextLineStart(std::string_view text, std::size_t pos) {
    const auto nl = text.find('\n', pos);
    return nl == npos ? text.size() : nl + 1;
}

bool onlyBlanks(std::string_view text, std::size_t from, std::size_t to) {
    return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(from),
                       text.begin() + static_cast<std::ptrdiff_t>(to), isBlank);
}

// After `pos` (a value's end): optional blanks, optional ',', optional blanks, then a line
// break (a comment does not count). Returns the position after the comma (or `pos` if none) in
// `commaEnd`.
bool restOfLineIsEmpty(std::string_view orig, std::size_t pos, std::size_t& commaEnd) {
    std::size_t p = pos;
    while (p < orig.size() && isBlank(orig[p]))
        ++p;
    commaEnd = pos;
    if (p < orig.size() && orig[p] == ',') {
        ++p;
        commaEnd = p;
    }
    while (p < orig.size() && isBlank(orig[p]))
        ++p;
    return p >= orig.size() || orig[p] == '\n' || orig[p] == '\r';
}

std::string joined(const std::vector<std::string>& tags) {
    std::string out = "{";
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i > 0)
            out += ", ";
        out += tags[i];
    }
    return out + "}";
}

} // namespace

std::vector<std::string> parse(std::string_view value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto comma = value.find(',', start);
        const auto end = comma == npos ? value.size() : comma;
        const std::string tag = latex::toDisplayText(trim(value.substr(start, end - start)));
        if (!tag.empty() && std::ranges::find(result, tag) == result.end())
            result.push_back(tag);
        if (comma == npos)
            break;
        start = comma + 1;
    }
    return result;
}

bool isFavourite(const std::vector<std::string>& tags) {
    return std::ranges::find(tags, kFavourite) != tags.end();
}

std::optional<std::string> invalidTag(std::string_view tag) {
    if (trim(tag).empty())
        return "empty";
    if (tag.find_first_of("{}\\%,#\n\r") != npos)
        return "contains a character that is not allowed: { } \\ % , # or a line break";
    return std::nullopt;
}

std::string setTags(std::string_view songText, const std::vector<std::string>& tags) {
    const std::string masked = maskComments(songText);
    const std::string_view m = masked;
    const std::string_view orig = songText;
    const std::string eol = [&] {
        const auto nl = orig.find('\n');
        return nl != npos && nl > 0 && orig[nl - 1] == '\r' ? "\r\n" : "\n";
    }();

    const auto begin = m.find("\\beginsong");
    if (begin == npos)
        return std::string{songText};
    const auto titleOpen = m.find('{', begin);
    if (titleOpen == npos)
        return std::string{songText};
    const auto titleClose = latex::findMatching(m, titleOpen);
    if (!titleClose)
        return std::string{songText};

    const auto open = skipSpaces(m, *titleClose + 1);
    const bool hasOptions = open < m.size() && m[open] == '[';
    const auto close = hasOptions ? latex::findMatching(m, open) : std::nullopt;
    if (hasOptions && !close)
        return std::string{songText};

    auto splice = [&](std::size_t from, std::size_t to, std::string_view with) {
        std::string out{orig.substr(0, from)};
        out += with;
        out += orig.substr(to);
        return out;
    };

    if (!hasOptions) {
        if (tags.empty())
            return std::string{songText};
        return splice(*titleClose + 1, *titleClose + 1, "[tags=" + joined(tags) + "]");
    }

    const auto entries = entriesOf(m, open + 1, *close);
    const auto last = [&](std::string_view name) -> const Entry* {
        const Entry* found = nullptr;
        for (const Entry& e : entries)
            if (e.name == name)
                found = &e;
        return found;
    };

    if (const Entry* e = last("tags")) {
        if (!tags.empty())
            return splice(e->valBegin, e->valEnd, joined(tags));
        // Remove the option: its whole line if it is alone on it, else it and one comma.
        const auto ls = lineStart(m, e->nameBegin);
        std::size_t commaEnd = 0;
        const bool restEmpty = restOfLineIsEmpty(orig, e->valEnd, commaEnd);
        if (onlyBlanks(orig, ls, e->nameBegin) && restEmpty)
            return splice(ls, nextLineStart(orig, e->valEnd), "");
        const bool commaAfter = commaEnd > e->valEnd || [&] {
            std::size_t p = e->valEnd;
            while (p < *close && isBlank(m[p]))
                ++p;
            if (p < *close && m[p] == ',') {
                commaEnd = p + 1;
                return true;
            }
            return false;
        }();
        std::size_t afterBlanks = commaEnd;
        while (afterBlanks < *close && isBlank(m[afterBlanks]))
            ++afterBlanks;
        if (commaAfter && !restEmpty)
            return splice(e->nameBegin, afterBlanks, "");
        // Last on its line: take the comma before it along instead, so no blanks are left over.
        std::size_t q = e->nameBegin;
        while (q > open + 1 && isSpace(m[q - 1]))
            --q;
        if (q > open + 1 && m[q - 1] == ',')
            return splice(q - 1, commaAfter ? commaEnd : e->valEnd, "");
        return splice(e->nameBegin, commaAfter ? afterBlanks : e->valEnd, "");
    }
    if (tags.empty())
        return std::string{songText};

    const std::string newOption = "tags=" + joined(tags);
    auto commaAfter = [&](const Entry& e) {
        std::size_t p = e.valEnd;
        while (p < *close && isBlank(m[p]))
            ++p;
        return p < *close && m[p] == ',';
    };
    // Inserts the option as a line before `lineBegin`, adding the comma the previous option
    // may lack. `prev` is that option (nullptr if none).
    auto insertLine = [&](std::size_t lineBegin, const Entry* prev, std::string_view indent) {
        std::string out{orig.substr(0, prev && !commaAfter(*prev) ? prev->valEnd : lineBegin)};
        if (prev && !commaAfter(*prev)) {
            out += ',';
            out += orig.substr(prev->valEnd, lineBegin - prev->valEnd);
        }
        out += indent;
        out += newOption;
        out += ',';
        out += eol;
        out += orig.substr(lineBegin);
        return out;
    };
    auto indentOf = [&](std::size_t pos) {
        const auto ls = lineStart(m, pos);
        std::size_t p = ls;
        while (p < pos && isBlank(orig[p]))
            ++p;
        return std::string{orig.substr(ls, p - ls)};
    };

    if (const Entry* by = last("by")) {
        const auto ls = lineStart(m, by->nameBegin);
        std::size_t commaEnd = 0;
        if (onlyBlanks(orig, ls, by->nameBegin) && restOfLineIsEmpty(m, by->valEnd, commaEnd)) {
            const auto next = nextLineStart(orig, by->valEnd);
            if (next <= *close)
                return insertLine(next, by, indentOf(by->nameBegin));
        }
    }
    // Before the closing "]" when it starts its own line, else at the end of the options.
    const auto closeLine = lineStart(m, *close);
    if (closeLine > open && onlyBlanks(orig, closeLine, *close)) {
        const Entry* prev = entries.empty() ? nullptr : &entries.back();
        return insertLine(closeLine, prev, prev ? indentOf(prev->nameBegin) : "    ");
    }
    if (entries.empty())
        return splice(*close, *close, newOption);
    return splice(entries.back().valEnd, entries.back().valEnd, ", " + newOption);
}

} // namespace zub::tags
