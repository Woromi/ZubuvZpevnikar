#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Score-only previews: a song's LilyPond file (.ly) engraved on its own to SVG, much faster than
// building the song's PDF. core prepares the build and reads its log; the app runs the command
// (QProcess), so it can run in the background and be cancelled.
namespace zub::score {

struct Request {
    std::filesystem::path songbookRoot; // the ZubuvZpevnik checkout (not read by prepare)
    std::filesystem::path workDir;      // reused between builds
    std::string inputName;              // "Artist/X.ly", as in \lilypondfile{}
    std::string content;                // the score text to engrave (e.g. unsaved editor content)
    bool germanNames = false;           // note names deutsch, see germanNames()
    std::filesystem::path includeDir;   // <root>/zubuvzpevnik/lilypond: zubuvzpevnik.ily etc.
};

struct Command {
    std::string program; // "lilypond"
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    // Set on top of the inherited environment: LC_ALL=C, so the log is in English and
    // errors() can read it (LilyPond translates "error" with the user's locale).
    std::vector<std::pair<std::string, std::string>> environment;
    std::filesystem::path svg;       // the cropped SVG, written on success
    std::filesystem::path scoreCopy; // the copy of content the build reads
};

// Writes the score copy (workDir/score/<inputName>) and the wrapper document (workDir/score.ly:
// \language english or deutsch, then \include) and creates workDir/out, and returns the command to
// run. Stale copies and outputs of earlier builds are removed. Fails on an invalid request
// (inputName not a relative "dir/Name.ly" path without special characters, relative workDir) or I/O
// errors. LilyPond's point-and-click links stay in the SVG.
std::expected<Command, std::string> prepare(const Request& request);

// The songbook's rule (zubuvzpevnik.sty): Czech and Slovak songs use German note names.
// `lang` is the song's lang value as zub::parseSong gives it; braces and spaces are trimmed,
// case is ignored.
bool germanNames(std::string_view lang);

struct Error {
    std::string message;        // e.g. "syntax error, unexpected '}'"
    std::filesystem::path file; // as LilyPond printed it
    int line = 0;               // 1-based
    int column = 0;             // 1-based
};

// The "file:line:col: error: message" lines of a LilyPond log, in order.
std::vector<Error> errors(std::string_view log);
std::optional<Error> firstError(std::string_view log);

struct NewScore {
    std::string text;
    std::size_t cursor; // byte offset inside the TabStaff braces, where the notes go
};

// A skeleton like the songbook's guitar tablature files.
NewScore newScore();

// The songbook's naming of scores: <SongFileName><PartName>.ly next to the song, e.g.
// HeartOfGold.tex -> HeartOfGoldGuitarIntro.ly, or <SongFileName>.ly for a song's only score.
// partName turns what the user typed into the part name: words (split at spaces, '-' and '_') each
// start with a capital letter and are joined ("guitar intro" -> "GuitarIntro", "solo 2" ->
// "Solo2"). Empty input gives "" (the song's only score); nullopt when a word has anything but
// ASCII letters and digits.
std::optional<std::string> partName(std::string_view typed);
// songStem + part + ".ly".
std::string scoreFileName(std::string_view songStem, std::string_view part);

} // namespace zub::score
