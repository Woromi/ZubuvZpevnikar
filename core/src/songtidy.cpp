#include <zub/core/songtidy.hpp>

#include <cctype>
#include <charconv>
#include <cstddef>

namespace zub::songtidy {

namespace {

std::string_view trim(std::string_view text) {
    constexpr std::string_view space = " \t\r\n";
    const std::size_t first = text.find_first_not_of(space);
    if (first == std::string_view::npos)
        return {};
    return text.substr(first, text.find_last_not_of(space) + 1 - first);
}

// "<anything>:<digits>: <text>" with the first ":<digits>: ".
bool parseChange(std::string_view line, Change& change) {
    for (std::size_t colon = line.find(':'); colon != std::string_view::npos;
         colon = line.find(':', colon + 1)) {
        std::size_t end = colon + 1;
        while (end < line.size() && std::isdigit(static_cast<unsigned char>(line[end])))
            ++end;
        if (end == colon + 1 || !line.substr(end).starts_with(": "))
            continue;
        int number = 0;
        const auto digits = line.substr(colon + 1, end - colon - 1);
        const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
        if (ec != std::errc{})
            return false;
        change.line = number;
        change.description = std::string(trim(line.substr(end + 2)));
        return true;
    }
    return false;
}

std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return {u8.begin(), u8.end()};
}

} // namespace

Command prepare(const std::filesystem::path& converter, const std::filesystem::path& songFile) {
    return {utf8(converter), {"tidy", utf8(songFile), "-o", "-"}};
}

std::expected<Tidied, std::string> interpret(int exitCode, std::string_view stdOut,
                                             std::string_view stdErr) {
    if (exitCode != 0) {
        const auto message = trim(stdErr);
        if (message.empty())
            return std::unexpected("chords_converter tidy exited with code " +
                                   std::to_string(exitCode));
        return std::unexpected(std::string(message));
    }
    if (!stdOut.contains("\\endsong"))
        return std::unexpected("chords_converter tidy printed no song");
    Tidied result;
    result.content = std::string(stdOut);
    while (!stdErr.empty()) {
        const std::size_t end = stdErr.find('\n');
        const std::string_view line = stdErr.substr(0, end);
        Change change;
        if (parseChange(line, change))
            result.changes.push_back(std::move(change));
        if (end == std::string_view::npos)
            break;
        stdErr.remove_prefix(end + 1);
    }
    return result;
}

} // namespace zub::songtidy
