#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/converter.hpp>

// Tidying a song with the chords_converter CLI ("chords_converter tidy SONG.tex -o -"): an
// identical repeated chorus becomes \insertchorus, a hand-typed chorus reminder becomes
// \insertchorus*, a chord inside its syllable moves to the syllable's start. core prepares the
// command and interprets the result; the app runs the process.
//
// The converter prints the tidied song on stdout (identical when nothing applies) and one line
// "<file>:<line>: <description>" per change on stderr. The line numbers refer to the INPUT text,
// not to the tidied one. It exits 1 when the file is unreadable.
namespace zub::songtidy {

using Command = converter::Command; // arguments: {"tidy", file, "-o", "-"}

Command prepare(const std::filesystem::path& converter, const std::filesystem::path& songFile);

struct Change {
    int line = 0; // in the INPUT text
    std::string description;
    bool operator==(const Change&) const = default;
};

struct Tidied {
    std::string content;
    std::vector<Change> changes;
    bool operator==(const Tidied&) const = default; // Qt's metatype of std::expected needs it
};

// Exit code 0: stdout is the song; stderr lines "<anything>:<digits>: <text>" become changes
// (split at the FIRST ":<digits>: " in the line). Other stderr lines are ignored. A non-zero
// exit gives an error message (stderr trimmed, or "chords_converter tidy exited with code N").
std::expected<Tidied, std::string> interpret(int exitCode, std::string_view stdOut,
                                             std::string_view stdErr);

} // namespace zub::songtidy
