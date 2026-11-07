#include "zub/core/song.hpp"

#include "zub/core/latex.hpp"

#include <cstddef>
#include <filesystem>

namespace zub {

namespace {

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
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

std::size_t skipSpaces(std::string_view s, std::size_t pos) {
    while (pos < s.size() && isSpace(s[pos]))
        ++pos;
    return pos;
}

// First `target` at or after `start` that is outside any {...} group and not escaped.
std::size_t findOutsideBraces(std::string_view s, std::size_t start, char target) {
    int depth = 0;
    for (std::size_t i = start; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\') {
            ++i;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}') {
            depth = depth > 0 ? depth - 1 : 0;
        } else if (c == target && depth == 0) {
            return i;
        }
    }
    return std::string_view::npos;
}

} // namespace

std::string SongMetadata::value(std::string_view key) const {
    const auto it = keys.find(key);
    return it == keys.end() ? std::string{} : it->second;
}

std::map<std::string, std::string, std::less<>> parseOptions(std::string_view options) {
    std::map<std::string, std::string, std::less<>> keys;
    const std::size_t n = options.size();
    std::size_t i = 0;
    while (i < n) {
        if (isSpace(options[i]) || options[i] == ',') {
            ++i;
            continue;
        }

        std::size_t nameEnd = i;
        while (nameEnd < n && isAsciiAlnum(options[nameEnd]))
            ++nameEnd;
        const auto name = options.substr(i, nameEnd - i);

        std::size_t k = skipSpaces(options, nameEnd);
        if (k >= n || options[k] != '=') {
            // Not a key=value entry: resynchronise at the next top-level comma.
            const auto next = findOutsideBraces(options, i, ',');
            if (next == std::string_view::npos)
                break;
            i = next + 1;
            continue;
        }
        k = skipSpaces(options, k + 1);

        std::string_view value;
        if (k < n && options[k] == '{') {
            const auto close = latex::findMatching(options, k);
            if (close) {
                value = options.substr(k + 1, *close - k - 1);
                i = *close + 1;
            } else {
                value = options.substr(k + 1);
                i = n;
            }
        } else {
            const auto next = findOutsideBraces(options, k, ',');
            if (next == std::string_view::npos) {
                value = options.substr(k);
                i = n;
            } else {
                value = options.substr(k, next - k);
                i = next;
            }
        }
        if (!name.empty())
            keys.insert_or_assign(std::string{name}, std::string{trim(value)});
    }
    return keys;
}

std::expected<SongMetadata, std::string> parseSong(std::string_view content) {
    const std::string text = latex::stripComments(content);
    const std::string_view tex = text;

    const auto begin = tex.find("\\beginsong");
    if (begin == std::string_view::npos)
        return std::unexpected("no \\beginsong");

    const auto titleOpen = tex.find('{', begin);
    if (titleOpen == std::string_view::npos)
        return std::unexpected("\\beginsong without a title");
    const auto titleClose = latex::findMatching(tex, titleOpen);
    if (!titleClose)
        return std::unexpected("unbalanced braces in the title");

    SongMetadata meta;
    meta.title = std::string{trim(tex.substr(titleOpen + 1, *titleClose - titleOpen - 1))};

    const auto optionsOpen = skipSpaces(tex, *titleClose + 1);
    if (optionsOpen < tex.size() && tex[optionsOpen] == '[') {
        const auto optionsClose = latex::findMatching(tex, optionsOpen);
        if (!optionsClose)
            return std::unexpected("unbalanced [...] options after the title");
        meta.keys = parseOptions(tex.substr(optionsOpen + 1, *optionsClose - optionsOpen - 1));
    }
    return meta;
}

std::optional<std::string> invalidInputName(std::string_view name) {
    if (name.empty())
        return "empty";
    if (name.front() == '/')
        return "must be relative";
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F ||
            std::string_view("\\{}%#$&~^:\"*?<>|").find(c) != std::string_view::npos)
            return "contains a character that is not allowed";
    }
    std::size_t start = 0;
    while (true) {
        const auto end = name.find('/', start);
        const auto part = name.substr(start, end == std::string_view::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..")
            return "empty, '.' or '..' path component";
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    if (!std::filesystem::path(std::u8string(name.begin(), name.end())).is_relative())
        return "must be relative";
    return std::nullopt;
}

} // namespace zub
