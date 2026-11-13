#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <zub/core/converter.hpp>

// The songbook assembler: a songbook the app manages (a JSON file), the LaTeX document generated
// from it, the title index order (the songbook's own scripts/songidx.lua does the sorting), the
// plan for exporting a self-contained folder and the command that builds the PDF.
namespace zub::songbook {

enum class BookClass {
    Velky,
    Maly,
    Tabor
}; // velkyzpevnik (A4), malyzpevnik (A5), taborovyzpevnik (A5 camp)

std::string_view className(BookClass); // "velkyzpevnik", "malyzpevnik", "taborovyzpevnik"
std::optional<BookClass> bookClassFromName(std::string_view);

struct Section {
    std::string name;               // "" = unnamed (no \songsection)
    std::vector<std::string> songs; // input names "Artist/Title", in book order
    bool operator==(const Section&) const = default;
};

struct Songbook {
    std::string title, author;
    BookClass bookClass = BookClass::Velky;
    bool chorded = true;           // class option chorded (else lyric)
    bool twoColumns = false;       // twosongcolumns (else onesongcolumn)
    bool shading = false;          // false -> noshading
    bool chordChart = true;        // \insertallchords (Velky, Maly)
    bool printReady = false;       // Maly only: binding leaves, imprint, \padbookpages
    std::string campYear;          // Tabor only: \renewcommand{\campyear}{...}; "" = class default
    std::string coverImage;        // absolute path of a png/jpg/jpeg/pdf cover; "" = none
    bool authorIndex = true;       // one author index for the whole book (never in Tabor)
    std::vector<Section> sections; // after fromJson / normalize: sections[0] is the unnamed part
    bool operator==(const Songbook&) const = default;
};

// Makes sections[0] the unnamed part (name "", possibly empty): the songs of any other unnamed
// sections are appended to it (no duplicates within it) and those sections are removed; with none,
// an empty one is inserted at index 0. Named sections keep their order.
void normalize(Songbook&);

// Where the songs `added` go in the manual order `current` (which does not hold them): each one
// right after the nearest name before it in `sortedAll` (the index order of current + added) that
// is already placed, at the front if there is none. Added names are processed in `sortedAll` order;
// those already in `current` are ignored.
std::vector<std::string> mergeIntoOrder(const std::vector<std::string>& current,
                                        const std::vector<std::string>& sortedAll,
                                        const std::vector<std::string>& added);

// Pretty JSON (2-space indent), keys in a fixed order, "version": 1, trailing newline.
std::string toJson(const Songbook&);
// Missing optional keys give the defaults; wrong types, an unknown class, a missing or unknown
// version and a non-object document are errors. The result is normalized.
std::expected<Songbook, std::string> fromJson(std::string_view);

// For \title, \author, section names and the camp year: LaTeX special characters escaped,
// newlines turned into spaces, UTF-8 untouched.
std::string escapeText(std::string_view);

struct Generated {
    std::string tex;                   // the songbook document
    std::optional<std::string> akordy; // Tabor only: content of akordy.tex
    std::vector<std::string> skipped;  // input names left out because `exists` said no or invalid
    // The build.sh -i list, in order: "obsah" (songs outside sections), "idxs1".. (one per
    // non-empty named section), "obsahAutor" (author index).
    std::vector<std::string> indexes;
    // The cover file to copy as `coverName` next to the document (nullopt: no cover).
    std::optional<std::filesystem::path> coverSource;
    std::string coverName; // "cover.<lowercase extension>", "" without a cover
    std::vector<std::string> warnings;
};
// Names failing zub::invalidInputName are always left out (never written into the document).
// `titleOf` gives a song's title for estimating the Tabor index length; without it every song
// is assumed to have its own initial (the worst case).
Generated generate(const Songbook&, const std::function<bool(std::string_view inputName)>& exists,
                   const std::function<std::string(std::string_view inputName)>& titleOf = {});

// ---- Finding the songs ----

struct ResolvedSong {
    std::string inputName;
    std::filesystem::path path; // empty: not found
};
struct Resolution {
    std::vector<ResolvedSong> songs;  // deduplicated, in book order
    std::vector<std::string> invalid; // names failing zub::invalidInputName (never given a path)
};
// <repoRoot>/songs/chords/<name>.tex, else songs/tinwhistle/<name>.tex (regular files).
Resolution resolveSongs(const Songbook&, const std::filesystem::path& repoRoot);

// ---- Index order (scripts/songidx.lua) ----

struct SortEntry {
    std::string title;
    std::string lang; // "" is written as english
};
// The title index data file (what checkorder.py writes), entry p (1-based) at book position p.
std::string indexOrderInput(std::span<const SortEntry>);
// The numbers of every \songlink{..}{N} in songidx's output, in order; an error unless they are
// a permutation of 1..count.
std::expected<std::vector<int>, std::string> parseIndexOrder(std::string_view sbx,
                                                             std::size_t count);
// {texlua, {songidxLua, "-l", locale, sxd, sbx}}; no "-l" pair when locale is empty.
converter::Command sortCommand(const std::filesystem::path& texlua,
                               const std::filesystem::path& songidxLua, std::string_view locale,
                               const std::filesystem::path& sxd, const std::filesystem::path& sbx);

// ---- Folder export ----

// The targets of \lilypondfile / \narrowlilypondfile in a song (comments ignored), in order,
// without duplicates.
// Names are trimmed, as lyluatex does.
std::vector<std::string> referencedFiles(std::string_view songText);

// The in-tree score a \lilypondfile target builds: trimmed, ".ly" appended when the last path
// component has no extension (as lyluatex does). nullopt when that is not a relative path to a
// .ly file without "..", backslashes or characters LaTeX treats specially: such a reference is
// built from wherever TEXINPUTS finds it and cannot be checked.
std::optional<std::string> resolveScoreReference(std::string_view target);

// True when `songPath` is inside <repoRoot>/songs/tinwhistle.
bool inTinwhistleTree(const std::filesystem::path& repoRoot, const std::filesystem::path& songPath);

// The score file `name` (a resolved reference) as the build finds it: <repoRoot>/songs/chords,
// then songs/tinwhistle, except that a tinwhistle song looks in its own tree first (the order of
// the preview's TEXINPUTS). Empty when there is no such regular file.
std::filesystem::path findScoreFile(const std::filesystem::path& repoRoot,
                                    const std::filesystem::path& songPath, std::string_view name);

struct FileCopy {
    std::filesystem::path from;
    std::filesystem::path to; // relative to the export folder
};
struct FolderExport {
    std::vector<FileCopy> copies;
    std::vector<std::pair<std::filesystem::path, std::string>> written; // relative path -> content
    std::vector<std::string> skipped;                                   // songs not found
    std::vector<std::string> invalid;                                   // not valid song names
    std::vector<std::string> warnings;                                  // from generate()
};
// `bookName` is a file stem: non-empty, only [A-Za-z0-9_-]. scripts/* sources are listed even
// when missing (the caller reports that).
std::expected<FolderExport, std::string>
planFolderExport(const Songbook&, const std::filesystem::path& repoRoot, std::string_view bookName,
                 const std::function<std::string(std::string_view inputName)>& titleOf = {});

// ---- PDF build ----

struct BuildCommand {
    std::string program;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    std::vector<std::pair<std::string, std::string>> environment;
    std::filesystem::path pdf, log;
};
// The build of a generated songbook in `workDir` (already holding only <bookName>.tex, the cover
// and akordy.tex for Tabor); `indexes` is Generated::indexes, built with the songbook's scripts/build.sh. TEXINPUTS ends with ':' to keep the
// defaults; the song folders come before the repo root so a name cannot find another file.
BuildCommand buildCommand(const Songbook&, const std::filesystem::path& repoRoot,
                          const std::filesystem::path& workDir, std::string_view bookName,
                          const std::vector<std::string>& indexes);
// Number of "Package zubuvzpevnik Warning"s about "not marked as fitting" (a warning wrapped at
// max_print_line counts once).
int paperWarnings(std::string_view log);

} // namespace zub::songbook
