#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <zub/core/converter.hpp>

// When a song was added and last edited, from git (never from the song files: their modification
// times change with every checkout). The app runs the two commands (core starts no processes) and
// hands their output to parseLog and applyStatus. Paths are relative to the repository's top
// level (the songbook folder must be that), with '/' separators.
namespace zub::songdates {

struct Dates {
    std::optional<std::chrono::sys_seconds> added;  // the commit that created the file
    std::optional<std::chrono::sys_seconds> edited; // the newest commit touching it
    bool uncommitted = false; // new, staged or changed in the working tree since the last commit
};

// `git log` over songs/, newest commit first. Output: per commit a line "\x01<unix time>", an
// empty line, then one "<status>\t<path>" line per file ("A", "M", "D", ...; renames are off, so
// a rename is a D and an A). core.quotePath=false keeps UTF-8 names unquoted (git still quotes
// names with quotes, backslashes or control characters: those songs get no dates).
converter::Command logCommand(const std::filesystem::path& root);
// `git status` over songs/: NUL-separated "XY <path>" records (rename and copy records are
// followed by the old path). --no-optional-locks: the status never writes the index.
converter::Command statusCommand(const std::filesystem::path& root);

// Per path: added = the OLDEST commit that created it (a path deleted and created again counts
// from the newest creation), edited = the newest commit touching it. A path whose newest status
// is D (deleted since) is dropped. Empty or malformed input gives an empty (or shorter) map.
std::map<std::string, Dates> parseLog(std::string_view log);

// Marks what the working tree changed: untracked ("??"), staged-new ("A") and changed files
// get `uncommitted`; a path the log does not know gets an entry of its own (no dates).
void applyStatus(std::map<std::string, Dates>& dates, std::string_view porcelainZ);

// Added within `within` before `now`, or new and not committed yet (no added date, uncommitted).
bool isRecent(const Dates& dates, std::chrono::sys_seconds now, std::chrono::days within);

} // namespace zub::songdates
