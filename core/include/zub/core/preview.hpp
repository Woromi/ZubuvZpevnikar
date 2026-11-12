#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <zub/core/songsuggest.hpp>

// Single-song preview builds. core prepares the build and reads its log; the app runs the
// command (QProcess), so it can run in the background and be cancelled.
namespace zub::preview {

struct Request {
    std::filesystem::path songbookRoot; // the ZubuvZpevnik checkout
    std::filesystem::path workDir;      // reused between builds: keeps LilyPond's tmp-ly cache
    std::string inputName;              // "Artist/Title", as in \input{}
    std::string songContent;            // the song text to build (e.g. unsaved editor content)
    std::string documentClass;          // "velkyzpevnik", "malyzpevnik", ...
    // (relative path, content) of score files (.ly) to build instead of the real ones, e.g. the
    // unsaved text of score editors. Written next to the song copy in override/, which comes
    // before songs/chords on TEXINPUTS, so \lilypondfile finds them first.
    std::vector<std::pair<std::string, std::string>> fileOverrides;
};

struct Command {
    std::string program; // "lualatex"
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    // Set on top of the inherited environment: TEXINPUTS (song copy first, then the
    // songbook), and max_print_line etc. so log messages are not wrapped.
    std::vector<std::pair<std::string, std::string>> environment;
    std::filesystem::path pdf; // written on success
    std::filesystem::path log;
    std::filesystem::path songCopy; // the copy of songContent the build reads
};

// Writes the wrapper document and the song copy into request.workDir (creating it, and
// removing song copies left by earlier builds), and returns the command to run in it.
// Fails on an invalid request (empty or unsafe inputName, invalid class name) or I/O errors.
std::expected<Command, std::string> prepare(const Request& request);

// Document classes the songbook provides: the stems of zubuvzpevnik/*.cls, sorted.
std::vector<std::string> documentClasses(const std::filesystem::path& songbookRoot);

// The paper size of a document class: zubuvzpevnik/<className>.cls loads its base class with
// \LoadClass[...a5paper...] (A5) or a4paper (A4). Nullopt when the file is unreadable, the
// name is not a plain class name, or no paper option is found.
std::optional<songsuggest::Paper> paperOf(const std::filesystem::path& songbookRoot,
                                          std::string_view className);

struct Error {
    std::string message;              // e.g. "Undefined control sequence."
    std::filesystem::path file;       // where TeX reports it; empty if unknown
    std::optional<int> line;          // 1-based line in `file`
    bool inSong = false;              // `file` is the song copy: `line` is a line of the song
    std::vector<std::string> context; // the lines TeX prints after the message
};

// The first error in a lualatex -file-line-error log, or nullopt if there is none.
std::optional<Error> firstError(std::string_view log, const std::filesystem::path& songCopy);

} // namespace zub::preview
