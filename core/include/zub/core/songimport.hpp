#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Importing a song with the chords_converter CLI (../chords_converter). core prepares the run
// and interprets its result; the app runs the process (QProcess) in a fresh work dir.
//
// The converter writes out/latex/<Artist>/<Title>.tex relative to its working directory and
// prints "Saving output to: <path>" on success. It exits 0 even when writing the file failed,
// so success also requires the file to exist.
namespace zub::songimport {

struct Request {
    std::filesystem::path converter; // the chords_converter executable
    std::filesystem::path workDir;   // a fresh, empty directory for this run
    std::string source;              // an http(s) URL, or a saved .html / .txt file
};

struct Command {
    std::string program;
    std::vector<std::string> arguments; // {source, "latex"}; a file source is made absolute
    std::filesystem::path workingDirectory;
};

// Validates the request (source is an http:// or https:// URL, or an existing regular file;
// workDir is absolute; converter is non-empty), creates workDir, and returns the command.
std::expected<Command, std::string> prepare(const Request& request);

enum class FailureKind {
    BotCheck,        // the site answered with a bot check (Cloudflare, Ultimate Guitar)
    UnsupportedSite, // no parser for the URL
    Network,         // the request failed (DNS, timeout, refused, ...)
    Http,            // the site answered with an HTTP error status
    InvalidInput,    // not a URL, not an existing file, file not UTF-8, ...
    Other,           // anything else, including "exited 0 but no output file"
};

struct Failure {
    FailureKind kind = FailureKind::Other;
    std::string message; // the converter's own message (multi-line), or ours
};

struct Imported {
    std::string inputName;             // "Artist/Title", from the output path
    std::string content;               // the .tex the converter wrote
    std::vector<std::string> warnings; // "Warning: ..." lines from stderr
};

// Interprets a finished run: the exit code and what the converter printed. On success reads
// the output file from workDir.
std::expected<Imported, Failure> interpret(int exitCode, std::string_view standardOutput,
                                           std::string_view standardError,
                                           const std::filesystem::path& workDir);

struct Destination {
    std::filesystem::path path;
    std::string inputName;
};

// Where an imported song belongs: <songbookRoot>/songs/chords/<inputName>.tex. Reuses an
// existing artist folder whose name matches case-insensitively (ASCII), so "AcDc/X" goes into
// an existing "ACDC/" folder as "ACDC/X"; the title keeps the converter's spelling.
Destination destination(const std::filesystem::path& songbookRoot, std::string_view inputName);

// An existing song file with the same Artist/Title, compared case-insensitively (ASCII),
// e.g. "AcDc/HighwayToHell" finds songs/chords/ACDC/HighwayToHell.tex.
std::optional<std::filesystem::path> existingSong(const std::filesystem::path& songbookRoot,
                                                  std::string_view inputName);

} // namespace zub::songimport
