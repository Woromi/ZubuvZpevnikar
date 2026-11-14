#include "zub/core/songtags.hpp"

#include "zub/core/latex.hpp"
#include "zub/core/songheader.hpp"

#include <algorithm>
#include <cstddef>

namespace zub::tags {

namespace {

constexpr std::size_t npos = std::string_view::npos;

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && isSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isSpace(s.back()))
        s.remove_suffix(1);
    return s;
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
    return songheader::setOption(songText, "tags", tags.empty() ? "" : joined(tags));
}

} // namespace zub::tags
