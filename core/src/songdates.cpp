#include <charconv>
#include <iterator>
#include <set>
#include <vector>
#include <zub/core/songdates.hpp>

namespace zub::songdates {

namespace {
converter::Command gitCommand(const std::filesystem::path& root, std::vector<std::string> rest) {
    converter::Command command{"git", {"-C", root.string(), "-c", "core.quotePath=false"}};
    command.arguments.insert(command.arguments.end(), std::make_move_iterator(rest.begin()),
                             std::make_move_iterator(rest.end()));
    return command;
}
} // namespace

converter::Command logCommand(const std::filesystem::path& root) {
    return gitCommand(root,
                      {"log", "--no-renames", "--name-status", "--format=%x01%at", "--", "songs"});
}

converter::Command statusCommand(const std::filesystem::path& root) {
    return gitCommand(root, {"--no-optional-locks", "status", "--porcelain=v1", "-z",
                             "--untracked-files=all", "--", "songs"});
}

std::map<std::string, Dates> parseLog(std::string_view log) {
    std::map<std::string, Dates> result;
    std::set<std::string> closed; // deleted, or created: older commits are an earlier life
    std::optional<std::chrono::sys_seconds> time;
    while (!log.empty()) {
        const auto end = log.find('\n');
        std::string_view line = log.substr(0, end);
        log = end == std::string_view::npos ? std::string_view{} : log.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.empty())
            continue;
        if (line.front() == '\x01') {
            long long seconds = 0;
            const auto digits = line.substr(1);
            const auto [ptr, ec] =
                std::from_chars(digits.data(), digits.data() + digits.size(), seconds);
            time = ec == std::errc{} && ptr == digits.data() + digits.size()
                       ? std::optional(std::chrono::sys_seconds{std::chrono::seconds{seconds}})
                       : std::nullopt;
            continue;
        }
        const auto tab = line.find('\t');
        if (!time || tab == std::string_view::npos || tab == 0 || tab + 1 >= line.size())
            continue;
        const char status = line.front();
        const std::string path(line.substr(tab + 1));
        if (closed.contains(path))
            continue;
        if (status == 'D') { // deleted since (newest), or an earlier life before a creation
            closed.insert(path);
            continue;
        }
        Dates& dates = result[path];
        if (!dates.edited)
            dates.edited = time;
        if (status == 'A')
            dates.added = time;
    }
    return result;
}

void applyStatus(std::map<std::string, Dates>& dates, std::string_view porcelainZ) {
    while (!porcelainZ.empty()) {
        const auto end = porcelainZ.find('\0');
        const std::string_view record = porcelainZ.substr(0, end);
        porcelainZ =
            end == std::string_view::npos ? std::string_view{} : porcelainZ.substr(end + 1);
        if (record.size() < 4 || record[2] != ' ')
            continue;
        const char x = record[0];
        const char y = record[1];
        if (x == 'R' || x == 'C' || y == 'R' || y == 'C') { // "new" then the old path
            const auto oldEnd = porcelainZ.find('\0');
            porcelainZ = oldEnd == std::string_view::npos ? std::string_view{}
                                                          : porcelainZ.substr(oldEnd + 1);
        }
        if (x == 'D' || y == 'D') // gone from the working tree: not a song to date
            continue;
        dates[std::string(record.substr(3))].uncommitted = true;
    }
}

bool isRecent(const Dates& dates, std::chrono::sys_seconds now, std::chrono::days within) {
    if (!dates.added)
        return dates.uncommitted;
    return now - *dates.added <= within;
}

} // namespace zub::songdates
