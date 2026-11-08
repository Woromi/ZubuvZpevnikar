#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/converter.hpp>

// "Make Smaller" with the chords_converter CLI: "suggest SONG.tex --fit a5 --measure --songbook
// ROOT --json" lists changes that make the song print smaller (rated by readability, best
// first), "apply SONG.tex --id ID -o -" prints the song with one of them made. core prepares the
// commands and interprets the results; the app runs the processes.
//
// A proposal id such as "chordsoff@81" is valid only for the text suggest was run on; apply
// refuses a stale id with exit code 1. --measure builds the song with lualatex -shell-escape
// (the songbook's scripts/checkpaper.py), so callers apply the same safety rule as for a preview.
namespace zub::songsuggest {

enum class Paper { A4, A5 };

struct Measured {
    int pages = 0;
    bool fits = false;
    std::vector<int> wrapLines; // lines of the song that are too wide for the column
    int chorusWraps = 0;        // too-wide lines inside a chorus (reported without a line number)
    bool operator==(const Measured&) const = default;
};

// One text change of a proposal: bytes [start, end) of the text suggest ran on become
// `replacement` (UTF-8 byte offsets).
struct Edit {
    std::size_t start = 0;
    std::size_t end = 0;
    std::string replacement;
    bool operator==(const Edit&) const = default;
};

// A part a proposal relies on (0.4.0): the part whose chords stay shown for a part that loses
// them, or the chorus an \insertchorus* stands for. Bytes from its \begin... to after \end...
struct Source {
    int line = 0;
    std::size_t start = 0;
    std::size_t end = 0;
    bool operator==(const Source&) const = default;
};

struct Proposal {
    std::string id;
    std::string kind;
    std::string category = "fit"; // "fit", or "restructure": changes what a part is (0.5.0)
    std::string description;
    int stage = 0; // 2: for small paper only
    int line = 0;  // in the text suggest ran on
    int gainLines = 0;
    int score = 0;     // 0-100: how readable the song stays (higher is better)
    std::string grade; // "good", "fair", "poor" or "last resort"; shown as is
    std::vector<Edit> edits; // what apply would change; for showing it (apply makes the change)
    std::vector<Source> sources;
    bool operator==(const Proposal&) const = default;
};

struct Suggestions {
    std::optional<bool> fits; // null when nothing was measured
    std::string note;
    std::optional<Measured> a4; // only the papers --measure measured
    std::optional<Measured> a5;
    std::vector<Proposal> proposals; // best first
    bool operator==(const Suggestions&) const = default;
};

// {"suggest", file, ["--fit", "a4"|"a5"], ["--measure"], "--songbook", root, "--json"}
converter::Command suggestCommand(const std::filesystem::path& converter,
                                  const std::filesystem::path& songFile,
                                  std::optional<Paper> fit, bool measure,
                                  const std::filesystem::path& songbookRoot);
// {"join", file, "--songbook", root, "--json"}: joining lines in pairs (0.5.0). Always builds the
// song on A4 and A5 (seconds), so it needs the preview's safety approval; its output is suggest's.
converter::Command joinCommand(const std::filesystem::path& converter,
                               const std::filesystem::path& songFile,
                               const std::filesystem::path& songbookRoot);
// {"apply", file, "--id", id, "-o", "-"}
converter::Command applyCommand(const std::filesystem::path& converter,
                                const std::filesystem::path& songFile, std::string_view id);

// Exit code 0 and stdout = the JSON (of suggest or join). Non-zero exit, malformed JSON or a missing field gives an
// error message (stderr trimmed, or a description). stderr is ignored on success.
std::expected<Suggestions, std::string> interpretSuggest(int exitCode, std::string_view stdOut,
                                                         std::string_view stdErr);
// Exit code 0: stdout is the changed song. Otherwise the error message as above.
std::expected<std::string, std::string> interpretApply(int exitCode, std::string_view stdOut,
                                                       std::string_view stdErr);

} // namespace zub::songsuggest
