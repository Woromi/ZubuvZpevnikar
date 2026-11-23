#pragma once

#include "zub/core/song.hpp"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zub {

// One song of the songbook repository.
struct SongEntry {
    std::filesystem::path path; // the .tex file
    std::string inputName;      // "Artist/Title": what a project writes into \input{}
    SongMetadata meta;
    std::vector<std::string> todos;  // its TODO comments (zub::todo::notes), file order
    std::vector<std::string> scores; // its \lilypondfile targets (songbook::referencedFiles)
    std::string collection;          // "chords", "tinwhistle", ...: the folder under songs/
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

// Checks that `root` looks like a ZubuvZpevnik checkout: it has songs/chords/ (the package that
// builds the songs may be the app's own copy, see zub::package).
std::expected<void, std::string> validateSongbookRepo(const std::filesystem::path& root);

// A new, empty songbook at `root` (which must not exist or be an empty directory): songs/chords/
// and songbooks/ (each with a .gitkeep, so git keeps them), a README.md and a .gitignore (build/
// and LaTeX's auxiliary files). It validates with validateSongbookRepo. Errors (English): `root`
// is a file or a directory with something in it, a folder or file that cannot be created.
std::expected<void, std::string> createSongbookRepo(const std::filesystem::path& root);

// The song collections of a songbook: the directories directly under <root>/songs/ ("chords",
// "tinwhistle", a future "<instrument>"), "chords" first, then the rest sorted by name (byte
// order). Hidden ones (a leading '.') are skipped; empty when <root>/songs is missing.
// A collection's folder name as it may be used: [A-Za-z0-9_-]+ (it goes onto TEXINPUTS and into
// a build.sh).
bool validCollectionName(std::string_view name);
std::vector<std::string> collections(const std::filesystem::path& root);

// The collection of a file under <root>/songs/<collection>/... (lexically, nothing is read from
// disk); nullopt for a path elsewhere.
std::optional<std::string> collectionOf(const std::filesystem::path& root,
                                        const std::filesystem::path& path);

// Reads every songs/<collection>/<Artist>/<Title>.tex under `root` (every collection(),
// SongEntry::collection says which). Library::songs is sorted by inputName, then by collection
// order. An input name in two collections is listed twice, with a ScanIssue on the later one
// (a songbook's \input gets the first). Files that fail to parse are reported in
// Library::issues and skipped; they do not stop the scan. Fails only if `root` is not a
// songbook repository.
std::expected<Library, std::string> scanLibrary(const std::filesystem::path& root);

} // namespace zub
