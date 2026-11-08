#include <zub/core/converter.hpp>

#include <charconv>
#include <cstddef>

namespace zub::converter {

namespace {

bool readNumber(std::string_view& text, int& number) {
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), number);
    if (ec != std::errc{} || ptr == text.data() || number < 0)
        return false;
    text.remove_prefix(static_cast<std::size_t>(ptr - text.data()));
    return true;
}

} // namespace

std::optional<Version> parseVersion(std::string_view stdOut) {
    const std::size_t eol = stdOut.find_first_of("\r\n");
    std::string_view line = stdOut.substr(0, eol);
    constexpr std::string_view prefix = "chords_converter ";
    if (!line.starts_with(prefix))
        return std::nullopt;
    line.remove_prefix(prefix.size());
    Version version;
    if (!readNumber(line, version.major) || !line.starts_with('.'))
        return std::nullopt;
    line.remove_prefix(1);
    if (!readNumber(line, version.minor) || !line.starts_with('.'))
        return std::nullopt;
    line.remove_prefix(1);
    if (!readNumber(line, version.patch))
        return std::nullopt;
    if (!line.empty() && line.front() != '-' && line.front() != '+' && line.front() != ' ')
        return std::nullopt;
    return version;
}

std::string toString(const Version& version) {
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

} // namespace zub::converter
