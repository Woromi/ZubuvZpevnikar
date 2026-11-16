#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

} // namespace zub::bookfile
