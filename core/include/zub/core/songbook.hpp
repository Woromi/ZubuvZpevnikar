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

// Songbooks (the LaTeX books of the songbook repo, read and edited by zub::bookfile): the book
// classes, finding a book's songs and the scores they build, the title index order (the
// package's scripts/songidx.lua does the sorting), the build's report and the checks of a book.
namespace zub::songbook {

enum class BookClass {
    Velky,
    Maly,
    Tabor
}; // velkyzpevnik (A4), malyzpevnik (A5), taborovyzpevnik (A5 camp)

std::string_view className(BookClass); // "velkyzpevnik", "malyzpevnik", "taborovyzpevnik"
std::optional<BookClass> bookClassFromName(std::string_view);
std::string_view paperName(BookClass); // the paper it typesets: "a4" (velkyzpevnik) or "a5"

struct Section {
    std::string name;               // "" = unnamed (no \songsection)
    std::vector<std::string> songs; // input names "Artist/Title", in book order
    bool operator==(const Section&) const = default;
};

// What the Songbooks page shows of a book (zub::bookfile reads it from the .tex): its title,
// class and the song lists of its sections (one per songs environment, in file order).
struct Songbook {
    std::string title, author;
    BookClass bookClass = BookClass::Velky;
    std::vector<Section> sections;
    bool operator==(const Songbook&) const = default;
};

// Where the songs `added` go in the manual order `current` (which does not hold them): each one
// right after the nearest name before it in `sortedAll` (the index order of current + added) that
// is already placed, at the front if there is none. Added names are processed in `sortedAll` order;
// those already in `current` are ignored.
std::vector<std::string> mergeIntoOrder(const std::vector<std::string>& current,
                                        const std::vector<std::string>& sortedAll,
                                        const std::vector<std::string>& added);

// For \title, \author, section names and the camp year: LaTeX special characters escaped,
// newlines turned into spaces, UTF-8 untouched.
std::string escapeText(std::string_view);

// ---- Finding the songs ----

struct ResolvedSong {
    std::string inputName;
    std::filesystem::path path; // empty: not found
};
struct Resolution {
    std::vector<ResolvedSong> songs;  // deduplicated, in book order
    std::vector<std::string> invalid; // names failing zub::invalidInputName (never given a path)
};
// <repoRoot>/songs/<collection>/<name>.tex in the first collection (zub::collections: chords
// first) that has it (regular files).
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

// ---- Scores ----

// The targets of \lilypondfile / \narrowlilypondfile in a song (comments ignored), in order,
// without duplicates.
// Names are trimmed, as lyluatex does.
std::vector<std::string> referencedFiles(std::string_view songText);

// The in-tree score a \lilypondfile target builds: trimmed, ".ly" appended when the last path
// component has no extension (as lyluatex does). nullopt when that is not a relative path to a
// .ly file without "..", backslashes or characters LaTeX treats specially: such a reference is
// built from wherever TEXINPUTS finds it and cannot be checked.
std::optional<std::string> resolveScoreReference(std::string_view target);

// What building the song would run through LilyPond that the scan flags: lilypondRisks of every
// copy of every score it references that a build could find (each collection's, `bookDir`'s
// when given, the repo root's), as "Artist/X.ly: #(system"; a file beside such a copy that stands
// in for a trusted include (includeShadows); "\lilypondfile{name}" for a reference that is not an
// in-tree score (resolveScoreReference fails), and the call as written for one without a braced
// argument or with [options] (the package's \lilypondfile takes one token): neither can be
// checked. A score that does not exist adds nothing.
std::vector<std::string> scoreRisks(const std::filesystem::path& repoRoot,
                                    const std::filesystem::path& songPath,
                                    std::string_view songText,
                                    const std::filesystem::path& bookDir = {});

// The \lilypondfile / \narrowlilypondfile calls of a song (comments ignored) without a braced
// argument or with [options], as written: the package's \lilypondfile takes a single token as
// the file name, so what such a call builds is not what a scan reads. Each needs confirmation.
std::vector<std::string> unreadableScoreCalls(std::string_view songText);

// Files in the song library's folders on a build's TEXINPUTS that a build could load by a bare
// name instead of what it means: a song collection's top folder holds only artist folders (any
// file or link there is reported: \input{akordy}, a probed hyperref.cfg, a .lua module, ...); in
// the repo root and `bookDir` (when given: a book build) files TeX, LuaTeX or LilyPond load by
// name (.cfg, .sty, .lua, .ly, a bare "akordy", ...; .tex too in the root, not in a book's folder,
// which has its books and akordy.tex, checked by themselves); a score anywhere under `bookDir`.
// TeX's own tree comes before these folders, but files it does not have (an optional .cfg a
// package probes for) are still looked for in them. "songs/chords/hyperref.cfg: ...".
std::vector<std::string> texinputsShadows(const std::filesystem::path& repoRoot,
                                          const std::filesystem::path& bookDir = {});

// Files in a score's folder that LilyPond would include instead of the package's trusted
// zubuvzpevnik.ily / tinwhistle.ily (it looks beside the score first); compared ignoring case.
std::vector<std::string> includeShadows(const std::filesystem::path& scoreDir);

// True when `songPath` is inside <repoRoot>/songs/tinwhistle (collectionOf == "tinwhistle").
bool inTinwhistleTree(const std::filesystem::path& repoRoot, const std::filesystem::path& songPath);

// The score file `name` (a resolved reference) as the build finds it: in the song's own
// collection (zub::collectionOf) first, then in the others of <repoRoot>/songs in
// zub::collections order (the order of the preview's TEXINPUTS). Empty when there is no such
// regular file.
std::filesystem::path findScoreFile(const std::filesystem::path& repoRoot,
                                    const std::filesystem::path& songPath, std::string_view name);

// ---- PDF build ----

struct BuildCommand {
    std::string program;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    std::vector<std::pair<std::string, std::string>> environment;
    std::filesystem::path pdf, log;
};
// Number of "Package zubuvzpevnik Warning"s about "not marked as fitting" (a warning wrapped at
// max_print_line counts once).
int paperWarnings(std::string_view log);

// ---- Checks of a book ----

// The songs of `book` (book order, once each) not marked as fitting its paper, as checkpaper.py
// checks a project: their `pap` (comma-separated, as `papOf` gives it) names neither the paper
// nor <paper>x2 (two facing pages). Songs `papOf` does not know (nullopt) are left out.
std::vector<std::string>
notMarkedFitting(const Songbook& book,
                 const std::function<std::optional<std::string>(std::string_view)>& papOf);

// What readiness needs to know of one song of a book (the app gathers it: the library, the file).
struct SongFacts {
    bool exists = false;            // a song of the library has this input name
    std::string pap;                // its `pap` value ("" none)
    bool notChecked = false;        // the import's "not checked yet" line (zub::todo)
    std::vector<std::string> todos; // its other TODO notes
    bool hasKey = false;            // a non-empty `key=`
    std::vector<std::string> risks; // what a build would ask about (unknown commands, scores)
    std::string changes;            // what the last Check Songs found ("" nothing)
};

// Is a book ready to print? Per category the songs (input names, book order, once each).
struct Readiness {
    // Blockers: the build fails or stops. `files`: problems of the book's own files (its .tex, a
    // camp book's chord chart), as text; readiness leaves it empty, the caller adds them.
    std::vector<std::string> missing, unsafe, files;
    // To fix.
    std::vector<std::string> notFitting, notChecked;
    // Notes: worth a look, they do not block.
    std::vector<std::string> todo, changes, noKey;

    // Songs (each once, however many problems it has) plus the file problems.
    std::size_t toFix() const;
    std::size_t notes() const; // songs with notes, each once
    bool ready() const { return toFix() == 0; }
};

// A missing song is only `missing`; the rest go into every category they meet (notFitting as
// notMarkedFitting: the book's paper or <paper>x2 in `pap`). `facts` is asked once per song.
Readiness readiness(const Songbook& book,
                    const std::function<SongFacts(std::string_view inputName)>& facts);

// Removes `inputName` from every section of `book`; false if it was in none.
bool removeSong(Songbook& book, std::string_view inputName);

} // namespace zub::songbook
