#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <zub/core/booktranspose.hpp>
#include <zub/core/songbook.hpp>

// Reading and editing the song lists of the hand-written LaTeX books of the songbook repo
// (`songbooks/**.tex`). Editing is byte-exact: only the lines of a song list that change are
// touched; wish lines (`% TODO: A/B`), commented inputs, trailing comments, blank lines and
// everything outside the `songs` environments stay as they are.
//
// The tokenisation of \input / \beginkeeporder / \endkeeporder is a port of the package's
// scripts/checkorder.py (`TOKEN`, over comment-stripped text): keep them in agreement.
namespace zub::bookfile {

// One entry of a song list: a single `\input{Name}` line, or a `\beginkeeporder` ...
// `\endkeeporder` block (its songs keep their order; the block sorts by its first song).
struct Unit {
    std::vector<std::string>
        songs; // input names, in order (1 for a single input, >= 0 for a block)
    bool block = false;
    // Byte range in the file: the unit's own lines, whole lines including their breaks. That is the
    // plain lines (blank, comments, `% TODO:` wishes, `% \input{X}`) directly above it (since the
    // previous unit or the start of the body), then its input line, or for a block everything from
    // `\beginkeeporder` to `\endkeeporder`.
    std::size_t begin = 0, end = 0;
};

struct Environment {
    std::string
        section; // last \songsection{..} between the previous \end{songs} and this \begin{songs}
    std::vector<std::string> indexes; // the \begin{songs}{a,b} names, trimmed
    // The lines strictly between the \begin{songs} line and the \end{songs} line (whole lines).
    std::size_t bodyBegin = 0, bodyEnd = 0;
    std::vector<Unit> units; // plain lines after the last unit (the tail) are in no unit
    std::string readOnly;    // not empty: why setSongs refuses this environment

    // The songs of all units, in order.
    std::vector<std::string> songs() const;
};

struct Book {
    std::string documentClass; // \documentclass[...]{name} -> name
    std::string title, author; // raw arguments of \title and \author ("" if none)
    std::vector<std::pair<std::string, std::string>> indexes; // \newindex{id}{file}, in order
    std::vector<Environment> environments;

    // Every song of every environment, in order.
    std::vector<std::string> songs() const;
};

// Works on LF or CRLF text. Errors: a \begin{songs} without its argument or without \end{songs},
// and a nested \begin{songs}. Lines that cannot be edited safely (a line with two inputs, an input
// with other code, a misplaced \end{songs}, unbalanced keep-order markers) make their
// environment readOnly; its songs are still listed as well as they can be read.
std::expected<Book, std::string> parse(std::string_view text);

// Replaces the song list of environment `environment` by `names` and returns the new text.
// Errors: environment out of range or readOnly, a name failing zub::invalidInputName, duplicates.
//
// Rules (slots model):
//  - Every unit is a slot. A unit is "kept" when at least one of its songs is in `names`. Kept
//    units are permuted among the kept slots, ordered by the smallest index in `names` of any of
//    their songs (a block moves whole, its own order never changes). Each moves together with its
//    leading plain lines (wish lines).
//  - An unkept unit stays in its slot as a leftover: its leading and inner plain lines stay; its
//    input lines are removed, or, when they carry a trailing comment, commented out
//    (`    \input{X} % note` -> `    % \input{X} % note`); the block markers of a block with no
//    kept song go. A block with some songs kept keeps its markers; its other input lines are
//    removed or commented out the same way. An empty block is left verbatim.
//  - A name in `names` that is in no unit: if a plain line outside any block is the commented-out
//    input of that very name (`% \input{X}`, `%\input{X}` and an optional trailing `% ...`), it is
//    uncommented in place (the `%` and the spaces after it go) and becomes a unit like any other.
//    Otherwise a new line `<indent>\input{X}<eol>` is placed directly after the whole unit holding
//    the name before it in `names` (after the new line of that name when it was new itself), or,
//    without a predecessor, before the first kept unit (before the tail if there is none).
//    Indent: that of the first input line of the environment, else of the book, else 4 spaces.
//    eol: CRLF if the first line break of the text is one, else LF.
//  - Everything outside [bodyBegin, bodyEnd) is copied unchanged, and setSongs(text, e, songs of
//    environment e) returns `text` unchanged.
std::expected<std::string, std::string> setSongs(std::string_view text, std::size_t environment,
                                                 const std::vector<std::string>& names);

// The first song of each unit (empty blocks skipped).
std::vector<std::string> leaders(const Environment&);

// Each leader of `orderedLeaders` followed by the rest of its block; names that are not a leader
// of this environment are kept as they are (singles).
std::vector<std::string> expandLeaders(const Environment&,
                                       const std::vector<std::string>& orderedLeaders);

// The files of the book's indexes, as build.sh gets them with -i: the file of every \newindex in
// declaration order (an index no environment shows is still built), then, for each environment's
// index names, the name itself when it has no \newindex (`obsah`); duplicates dropped.
std::vector<std::string> indexFiles(const Book&);

// The books of a songbook repo: every .tex file below `<repoRoot>/songbooks` (else `projects`)
// with a \documentclass in code, relative to repoRoot, sorted. Symlinked directories are not
// followed; unreadable files are skipped.
std::vector<std::filesystem::path> findBooks(const std::filesystem::path& repoRoot);
// The folder of the books, relative to the repo root: "songbooks", or "projects" in an older
// checkout that has only that (where new books go too).
std::filesystem::path booksFolder(const std::filesystem::path& repoRoot);

// Every \input{name} of `text` (comments ignored, anywhere, trimmed, once each, in order); "" for
// an \input without a brace argument.
std::vector<std::string> inputs(std::string_view text);

// The build of a book of the repo from a copy <workDir>/<bookName>.tex, with the package's
// scripts/build.sh -i <indexes> (indexFiles), writing only to workDir. TEXINPUTS: the work dir,
// the song folders, the package, then the book's own folder `bookDir` (a Tabor book's cover.pdf
// and akordy.tex), the repo root (img/), and the defaults (the trailing ':').
songbook::BuildCommand buildCommand(const std::filesystem::path& repoRoot,
                                    const std::filesystem::path& packageDir,
                                    const std::filesystem::path& workDir,
                                    const std::filesystem::path& bookDir, std::string_view bookName,
                                    const std::vector<std::string>& indexes);

// ---- The settings of a book (the header and the cover/chart commands) ----
//
// All edits are byte-exact: only the construct being changed is touched, new lines get the file's
// line break, setting the current value returns the text unchanged. Constructs are found in code
// (comments ignored), except the lock mark, which is a comment.

enum class Field {
    Class,
    Chorded,
    TwoColumns,
    Shading,
    Title,
    Author,
    CampYear,
    Cover,
    ChordChart
};

struct Settings {
    std::string
        bookClass;       // raw name from \documentclass: velkyzpevnik, malyzpevnik, taborovyzpevnik
    bool chorded = true; // option `lyric` -> false
    bool twoColumns = false;   // `twosongcolumns` -> true (`onesongcolumn` or absent -> false)
    bool shading = true;       // `noshading` -> false
    std::string title, author; // raw LaTeX arguments
    std::string campYear;      // \renewcommand{\campyear}{X} -> X, "" none
    // The file of the cover command of the class: \makefrontcover{F} (velkyzpevnik),
    // \maketitlepage[F] (malyzpevnik), \makefrontcover[F] (taborovyzpevnik); "" none/bare.
    std::string cover;
    bool chordChart = false; // an \insertallchords in code
    bool locked = false;     // a line that is the lock mark (lockMark()) anywhere in the file
    // Fields present in a form the app cannot edit safely (two \title, an argument over several
    // lines, a cover command in an unknown form, \insertallchords sharing its line with code).
    // Unknown \documentclass options are fine: they stay untouched.
    std::set<Field> notEditable;
};

Settings readSettings(std::string_view text);

// Sets one field; `value` of the bool fields is "1" or "0". Errors (English): the file has no
// \documentclass, the field is in notEditable, an invalid value (a class that is not one of the
// three, a text with a newline, an unescaped % or unbalanced braces, a cover file with braces or
// brackets, an empty cover of velkyzpevnik whose \makefrontcover exists: it has no coverless form).
// Where a setting is missing the command is added: \title / \author after \documentclass (the
// author after the title), \campyear after the author line, a cover command after \begin{document},
// \insertallchords after the last \end{songs} line (before \end{document} without one).
// Turning shading on removes `noshading` with its comma; an empty campYear removes its line; an
// empty cover of the optional-argument forms removes the bracket group. Turning the chart off
// removes every line that is \insertallchords.
std::expected<std::string, std::string> setSetting(std::string_view text, Field field,
                                                   std::string_view value);

// "% zpevnikar: locked"
std::string_view lockMark();
// Adds the mark as the first line (with the file's line break), or removes every line that is the
// mark; unchanged when already so.
std::string setLocked(std::string_view text, bool locked);

// ---- Transposing a book ----
//
// The book's key is a comment line `% zpevnikar: key=G transpose=chords` (or `transpose=capo`; the
// key as zub::booktranspose::bookKeyName writes it), like the lock mark. A song is transposed by a
// line directly above its `\input` (the previous line of the file, not a blank or a wish):
// `\transposesong[key=<key>]{N}` (re-shaping the chords; the package has no `capo` option: Capo mode
// edits the song files instead), optionally followed by a `% fixed` comment (a per-song choice that a change of the book's key leaves alone). It is a plain
// line of its input's unit: setSongs moves it with the input and drops it with a removed input.

struct TransposeSettings {
    enum class Mode { Chords, Capo };
    std::optional<booktranspose::Key> key; // nullopt: the book is not transposed
    Mode mode = Mode::Chords;
};

// The first settings line of the text; a missing line, or one without a readable key, gives no key
// (a missing or unknown transpose= means Chords).
TransposeSettings readTransposeSettings(std::string_view text);
// Writes the line: replaced in place when there is one (further ones go), else added after the lock
// mark line when that is the first line, else as the first line. No key removes every such line.
// Byte-exact otherwise, new lines get the file's line break.
std::string setTransposeSettings(std::string_view text, const TransposeSettings& settings);

struct SongTranspose {
    std::string inputName;
    int semitones = 0;  // -11..11
    bool capo = false;  // an old `capo` option: read only, setTranspose refuses it
    std::string key;    // the `key=` option as written ("" none)
    bool fixed = false; // `% fixed`
};

// Every song line in the song lists (code `\transposesong[...]{N}` alone on the line directly
// above an `\input{name}` line, options only `capo` (old books) and `key=`, N an integer), in file
// order.
std::vector<SongTranspose> readTransposes(std::string_view text);

// Writes, replaces or removes the line above the song's `\input` (the first environment that has
// it): `<indent of the \input>\transposesong[key=K]{N}` plus ` % fixed`, with the file's line
// break; a replaced line keeps its own indent. nullopt, or semitones 0 without `fixed`, removes
// it (a `fixed` 0 stays: "do not transpose this song"). Errors: no such input, read-only
// environment, `capo` set. `inputName` of the argument is ignored.
std::expected<std::string, std::string> setTranspose(std::string_view text,
                                                     std::string_view inputName,
                                                     const std::optional<SongTranspose>& transpose);

// ---- Sections (a \songsection, the \begin{songs} ... \end{songs} that follows) ----
// `environment` is the index in parse(). The block of a section runs from the line of its
// \songsection (else of its \begin{songs}) through its \end{songs} line, everything between
// included (a \pagebreak between them is the section's).

// Inserts, after the \end{songs} line of `afterEnvironment` (npos: the last), a blank line and
// \songsection{name}, \begin{songs}{<indexes>}, \end{songs} lines. <indexes> is the argument of the
// first environment as written ("obsah" without one). Without any environment it goes before the
// \end{document} line. `name` is raw LaTeX (nonempty, no newline, no unescaped %, balanced braces).
std::expected<std::string, std::string> addSection(std::string_view text, std::string_view name,
                                                   std::size_t afterEnvironment);
// Replaces the argument of the environment's \songsection (error without one).
std::expected<std::string, std::string>
renameSection(std::string_view text, std::size_t environment, std::string_view name);
// Deletes the block and one blank line directly above it (so add + remove is the identity).
std::expected<std::string, std::string> removeSection(std::string_view text,
                                                      std::size_t environment);
// Swaps the block with that of the neighbour (delta -1 or +1); the text between them stays.
std::expected<std::string, std::string> moveSection(std::string_view text, std::size_t environment,
                                                    int delta);

struct NewBook {
    std::string tex, buildSh;
};
// A new book in the style of the repo's own: `name` is the file stem ([A-Za-z0-9_-]+), used in
// build.sh. Settings::locked and notEditable are ignored. A velkyzpevnik without a cover has no
// \makefrontcover line; a malyzpevnik always has \maketitlepage (its only cover).
// build.sh's TEXINPUTS lists $REPO_ROOT/songs/<c> for each of `collections` (zub::collections
// of the repo), in order.
std::expected<NewBook, std::string> newBook(const Settings&, std::string_view name,
                                            const std::vector<std::string>& collections = {
                                                "chords", "tinwhistle"});

// The build.sh newBook writes, for a book <name>.tex of that class with these build.sh -i
// indexes (indexFiles): for a copy of a book under a new name.
std::string buildScriptFor(std::string_view name, songbook::BookClass bookClass,
                           const std::vector<std::string>& indexes,
                           const std::vector<std::string>& collections = {"chords", "tinwhistle"});

} // namespace zub::bookfile
