#include "zub/core/latex.hpp"

#include <string_view>

namespace zub::latex {

namespace {

bool isAsciiLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// Replacement text for control words that have a plain-text meaning in metadata values.
std::optional<std::string_view> controlWordText(std::string_view name) {
    if (name == "flt")
        return "♭";
    if (name == "shrp")
        return "♯";
    if (name == "rightarrow")
        return "→";
    return std::nullopt;
}

// Parses one {group} at `pos` (after optional spaces). Returns its content and the
// position after it, or nullopt if there is no balanced group there.
struct Group {
    std::string_view content;
    std::size_t end;
};
std::optional<Group> readGroup(std::string_view tex, std::size_t pos) {
    while (pos < tex.size() && isSpace(tex[pos]))
        ++pos;
    if (pos >= tex.size() || tex[pos] != '{')
        return std::nullopt;
    const auto close = findMatching(tex, pos);
    if (!close)
        return std::nullopt;
    return Group{tex.substr(pos + 1, *close - pos - 1), *close + 1};
}

void appendCollapsingSpace(std::string& out, char c) {
    if (isSpace(c)) {
        if (!out.empty() && out.back() != ' ')
            out += ' ';
        return;
    }
    out += c;
}

// A control symbol: backslash + one non-letter. Spacing ones become a space, invisible
// ones are dropped, escaped specials ("\&", "\%", ...) are kept literally. Accent macros
// like \'e are not decoded (the songs are UTF-8); the accent character is just dropped.
void appendControlSymbol(std::string& out, char symbol) {
    switch (symbol) {
    case ' ':
    case ',':
    case ';':
    case ':':
    case '\\':
    case '\n':
    case '\t':
        appendCollapsingSpace(out, ' ');
        break;
    case '-':
    case '/':
    case '!':
    case '\'':
    case '`':
    case '^':
    case '"':
    case '~':
    case '=':
    case '.':
        break;
    default:
        out += symbol; // & % # $ _ { } and anything else
    }
}

} // namespace

std::string stripComments(std::string_view tex) {
    std::string out;
    out.reserve(tex.size());
    std::size_t i = 0;
    while (i < tex.size()) {
        const char c = tex[i];
        if (c == '\\') {
            out += c;
            if (i + 1 < tex.size()) {
                out += tex[i + 1];
            }
            i += 2;
        } else if (c == '%') {
            const auto newline = tex.find('\n', i);
            if (newline == std::string_view::npos)
                break;
            i = newline; // the newline itself is copied by the next iteration
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

std::optional<std::size_t> findMatching(std::string_view tex, std::size_t open) {
    if (open >= tex.size())
        return std::nullopt;
    const char openCh = tex[open];
    char closeCh = 0;
    if (openCh == '{')
        closeCh = '}';
    else if (openCh == '[')
        closeCh = ']';
    else
        return std::nullopt;

    int depth = 0;
    for (std::size_t i = open; i < tex.size(); ++i) {
        const char c = tex[i];
        if (c == '\\') {
            ++i; // skip the escaped character
            continue;
        }
        if (c == openCh) {
            ++depth;
        } else if (c == closeCh) {
            if (--depth == 0)
                return i;
        }
    }
    return std::nullopt;
}

std::string toDisplayText(std::string_view tex) {
    std::string out;
    out.reserve(tex.size());
    std::size_t i = 0;
    while (i < tex.size()) {
        const char c = tex[i];
        if (c == '{' || c == '}') {
            ++i;
        } else if (c == '~') {
            appendCollapsingSpace(out, ' ');
            ++i;
        } else if (c == '\\' && i + 1 < tex.size() && isAsciiLetter(tex[i + 1])) {
            // Control word: letters, then TeX skips the spaces after it.
            std::size_t end = i + 1;
            while (end < tex.size() && isAsciiLetter(tex[end]))
                ++end;
            const auto name = tex.substr(i + 1, end - i - 1);
            while (end < tex.size() && isSpace(tex[end]))
                ++end;
            i = end;

            if (name == "texorpdfstring") {
                // \texorpdfstring{tex}{plain}: the plain-text version is what we want.
                const auto first = readGroup(tex, i);
                const auto second = first ? readGroup(tex, first->end) : std::nullopt;
                if (second) {
                    out += toDisplayText(second->content);
                    i = second->end;
                }
            } else if (const auto text = controlWordText(name)) {
                out += *text;
            }
            // Any other macro is dropped; its {arguments} are kept as plain text.
        } else if (c == '\\' && i + 1 < tex.size()) {
            appendControlSymbol(out, tex[i + 1]);
            i += 2;
        } else if (c == '\\') {
            ++i; // lone trailing backslash
        } else {
            appendCollapsingSpace(out, c);
            ++i;
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    if (!out.empty() && out.front() == ' ')
        out.erase(0, 1);
    return out;
}

} // namespace zub::latex
