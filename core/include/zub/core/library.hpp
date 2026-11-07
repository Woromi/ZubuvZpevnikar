#pragma once

#include "zub/core/song.hpp"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace zub {

// One song of the songbook repository.
struct SongEntry {
    std::filesystem::path path; // the .tex file
    std::string inputName;      // "Artist/Title": what a project writes into \input{}
    SongMetadata meta;
};

// A file that could not be read or parsed, or that looks suspicious.
struct ScanIssue {
    std::filesystem::path path;
    std::string message;
};

struct Library {
    std::vector<SongEntry> songs; // sorted by inputName
    std::vector<ScanIssue> issues;
};

// Checks that `root` looks like a ZubuvZpevnik checkout (has songs/chords/ and zubuvzpevnik/).
std::expected<void, std::string> validateSongbookRepo(const std::filesystem::path& root);

// Reads every songs/chords/<Artist>/<Title>.tex under `root` (songs/tinwhistle/ is not
// scanned yet). Files that fail to parse are
// reported in Library::issues and skipped; they do not stop the scan. Fails only if `root`
// is not a songbook repository.
std::expected<Library, std::string> scanLibrary(const std::filesystem::path& root);

} // namespace zub
