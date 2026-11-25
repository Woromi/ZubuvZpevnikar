#include <algorithm>
#include <iterator>
#include <zub/core/scorecache.hpp>

namespace zub::scorecache {

namespace {

constexpr std::size_t kHashLength = 32;

bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

} // namespace

std::set<std::string> usedScores(std::string_view log) {
    // TeX breaks long log lines anywhere: the names are read from the text without line breaks.
    std::string text;
    text.reserve(log.size());
    std::ranges::copy_if(log, std::back_inserter(text),
                         [](char c) { return c != '\n' && c != '\r'; });
    std::set<std::string> used;
    constexpr std::string_view marker = "tmp-ly/";
    for (std::size_t at = text.find(marker); at != std::string::npos;
         at = text.find(marker, at + marker.size())) {
        const std::size_t start = at + marker.size();
        if (text.size() - start < kHashLength)
            continue;
        const std::string_view hash = std::string_view(text).substr(start, kHashLength);
        if (std::ranges::all_of(hash, isHex))
            used.emplace(hash);
    }
    return used;
}

std::optional<std::string> scoreOf(std::string_view fileName) {
    if (fileName.size() < kHashLength ||
        !std::ranges::all_of(fileName.substr(0, kHashLength), isHex))
        return std::nullopt;
    if (fileName.size() > kHashLength && fileName[kHashLength] != '.' &&
        fileName[kHashLength] != '-')
        return std::nullopt;
    return std::string(fileName.substr(0, kHashLength));
}

} // namespace zub::scorecache
