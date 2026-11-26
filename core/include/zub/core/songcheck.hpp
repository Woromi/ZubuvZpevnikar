#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/converter.hpp>

// The package's checking scripts (scripts/checkkeys.py, scripts/checksongs.py) run with --json
// (docs/tools.md#machine-readable-output in zubuvzpevnik-package), and what the app makes of
// their findings: a key to write in the song's own naming, a stated key confirmed with
// `% nocheck key` (docs/check-marks.md).
namespace zub::songcheck {

// {"python3", <package>/scripts/<script>, "--songbook", root, "--json", extra...}
converter::Command scriptCommand(const std::filesystem::path& packageDir, std::string_view script,
                                 const std::filesystem::path& songbookRoot,
                                 const std::vector<std::string>& extra = {});

// One song of checkkeys.py --json --all.
struct KeyFinding {
    std::string file;     // relative to the songbook, "songs/chords/X/Y.tex"
    std::string stated;   // English, sharps ("D#m"); "" for none
    std::string detected; // likewise
    std::string kind;     // checkkeys' class: match, tie, relative, parallel, mismatch, propose,
                          // no-chords
    double margin = 0;    // how sure the detection is (the score gap)
    std::string suggest;  // a per-part key=… to paste (split.suggest), or ""
};
std::expected<std::vector<KeyFinding>, std::string> parseCheckKeys(std::string_view json);

// One issue of checksongs.py --json.
struct SongIssue {
    std::string severity; // "error" or "warn"
    std::string code;     // stable: "missing-recommended", "bars-without-meter", ...
    std::optional<int> line;
    std::string message;
};
// The songs with issues by file (relative to the songbook).
std::expected<std::map<std::string, std::vector<SongIssue>>, std::string>
parseCheckSongs(std::string_view json);

// The header fields an issue says are missing, in its order: "missing recommended: tem, tun"
// (missing-recommended), "missing required key 'by'" (missing-required), and met for
// bars-without-meter. Empty for every other issue. (Reads checksongs.py's message texts: keep in
// step with the script.)
std::vector<std::string> missingFields(const SongIssue& issue);

// `detected` ("F#m", "A#", English sharps) as the song header writes keys: `\shrp` and `\flt`
// macros ("F\shrp m"), the majors on A#, D# and G# as flats (B\flt, E\flt, A\flt), the rest as
// sharps; with German names (Czech, Slovak) B natural is H and B flat is B. Empty for anything
// else.
std::string keyValue(std::string_view detected, bool germanNames);

// Whether the stated key is confirmed: `% nocheck key` (or a bare `% nocheck`) on the line(s)
// right above the header's `key=` line.
bool keyConfirmed(std::string_view songText);

// `songText` with `% nocheck key` on a line of its own right above the `key=` line, indented
// like it; nullopt without a key line, or when it is confirmed already.
std::optional<std::string> confirmKey(std::string_view songText);

} // namespace zub::songcheck
