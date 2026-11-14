#include "zub/core/songheader.hpp"

#include "zub/core/latex.hpp"
#include "zub/core/songsuggest.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace zub::songheader {

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


} // namespace

std::string setOption(std::string_view songText, std::string_view key,
                      std::optional<std::string_view> value, bool dropEmptyHeader) {
    const bool remove = !value || value->empty();
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
        if (remove)
            return std::string{songText};
        return splice(*titleClose + 1, *titleClose + 1,
                      "[" + std::string{key} + "=" + std::string{*value} + "]");
    }

    const auto entries = entriesOf(m, open + 1, *close);
    const auto last = [&](std::string_view name) -> const Entry* {
        const Entry* found = nullptr;
        for (const Entry& e : entries)
            if (e.name == name)
                found = &e;
        return found;
    };

    if (const Entry* e = last(key)) {
        if (!remove)
            return splice(e->valBegin, e->valEnd, *value);
        if (dropEmptyHeader && entries.size() == 1) {
            const bool restBlank = [&] {
                for (std::size_t i = open + 1; i < *close; ++i) {
                    if (i >= e->nameBegin && i < e->valEnd)
                        continue;
                    if (!isSpace(m[i]) && m[i] != ',')
                        return false;
                }
                return true;
            }();
            if (restBlank)
                return splice(open, *close + 1, "");
        }
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
    if (remove)
        return std::string{songText};

    const std::string newOption = std::string{key} + "=" + std::string{*value};
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

std::optional<std::string> option(std::string_view songText, std::string_view key) {
    const std::string masked = maskComments(songText);
    const std::string_view m = masked;
    const auto begin = m.find("\\beginsong");
    if (begin == npos)
        return std::nullopt;
    const auto titleOpen = m.find('{', begin);
    if (titleOpen == npos)
        return std::nullopt;
    const auto titleClose = latex::findMatching(m, titleOpen);
    if (!titleClose)
        return std::nullopt;
    const auto open = skipSpaces(m, *titleClose + 1);
    if (open >= m.size() || m[open] != '[')
        return std::nullopt;
    const auto close = latex::findMatching(m, open);
    if (!close)
        return std::nullopt;
    std::optional<std::string> found;
    for (const Entry& e : entriesOf(m, open + 1, *close))
        if (e.name == key)
            found = e.braced && e.valEnd >= e.valBegin + 2
                        ? std::string{songText.substr(e.valBegin + 1, e.valEnd - e.valBegin - 2)}
                        : std::string{songText.substr(e.valBegin, e.valEnd - e.valBegin)};
    return found;
}


std::string measuredPapers(std::string_view currentPap, const std::optional<songsuggest::Measured>& a4,
                           const std::optional<songsuggest::Measured>& a5) {
    std::vector<std::string> a4Entries, a5Entries, others;
    std::size_t start = 0;
    while (start <= currentPap.size()) {
        const auto comma = currentPap.find(',', start);
        const auto end = comma == npos ? currentPap.size() : comma;
        auto part = currentPap.substr(start, end - start);
        while (!part.empty() && isSpace(part.front()))
            part.remove_prefix(1);
        while (!part.empty() && isSpace(part.back()))
            part.remove_suffix(1);
        if (!part.empty()) {
            const std::string entry{part};
            auto& list = entry == "a4" || entry == "a4x2"   ? a4Entries
                         : entry == "a5" || entry == "a5x2" ? a5Entries
                                                            : others;
            if (std::ranges::find(list, entry) == list.end())
                list.push_back(entry);
        }
        if (comma == npos)
            break;
        start = comma + 1;
    }
    const auto apply = [](std::vector<std::string>& list, const std::string& paper,
                          const std::optional<songsuggest::Measured>& m) {
        if (!m)
            return;
        const std::string spread = paper + "x2";
        const bool fitsOne = m->fits && m->pages == 1;
        if (fitsOne) {
            std::erase(list, spread);
            if (std::ranges::find(list, paper) == list.end())
                list.insert(list.begin(), paper);
        } else if (!m->fits) {
            std::erase(list, paper);
        }
    };
    apply(a4Entries, "a4", a4);
    apply(a5Entries, "a5", a5);
    std::string out;
    for (const auto* list : {&a4Entries, &a5Entries, &others})
        for (const std::string& e : *list) {
            if (!out.empty())
                out += ',';
            out += e;
        }
    return out;
}

std::string setPap(std::string_view songText, std::string_view pap) {
    const std::string value = "{" + std::string{pap} + "}";
    return setOption(songText, "pap", pap.empty() ? std::string_view{} : std::string_view{value},
                     true);
}

} // namespace zub::songheader
