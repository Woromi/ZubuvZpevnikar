#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// Small text edits for the song editor (insert a chord, move it along the text, insert a part).
// All positions are UTF-8 byte offsets within the given text; the line functions work on ONE
// line (without its line break), the caller passes the cursor's line.
namespace zub::songedit {

// A line after an edit: its new text and the cursor in it.
struct Change {
    std::string text;
    std::size_t cursor;
};

// Inserts a chord at `cursor`: "\[]" with the cursor between the brackets. With a selection
// [selStart, selEnd) inside the line: "\[<selected>]" with the cursor after the "]". Chords do not
// nest: with the cursor (or the selection's start) inside a chord, the new "\[]" goes right after
// that chord.
Change insertChord(std::string_view line, std::size_t cursor, std::size_t selStart,
                   std::size_t selEnd);

// Inserts "{\multivoice{}{}}" at `cursor`, the cursor in the first braces. With a selection
// inside the line: "{\multivoice{<selected>}{}}", the cursor in the second braces. Inside a
// chord it goes after the chord (the selection is then ignored).
Change insertMultivoice(std::string_view line, std::size_t cursor, std::size_t selStart,
                        std::size_t selEnd);

// Appends " \role{}" to the end of the line (trailing whitespace dropped, no space on an empty
// line) with the cursor in the braces. A selection inside the line is copied as the name (the
// text stays). A line that already ends with a \role command only gets the cursor in its first
// argument.
Change insertRole(std::string_view line, std::size_t cursor, std::size_t selStart,
                  std::size_t selEnd);

// Removes the innermost element whose text (start to end, both inclusive) holds `cursor`: a
// chord, a role (with one space before it), or a multivoice (replaced by its first voice, with or
// without its outer braces). The cursor ends at the element's former start. nullopt when there is
// none.
std::optional<Change> removeElementAt(std::string_view line, std::size_t cursor);

struct ChordSpan {
    std::size_t begin;
    std::size_t end; // after "]"
};

// The chord `cursor` is in or touches: inside "\[...]" or exactly at its start or end. Where two
// chords meet, the one that starts at the cursor wins over the one that ends there. nullopt
// without one.
std::optional<ChordSpan> chordAt(std::string_view line, std::size_t cursor);

// Where typing a space at `cursor` should go: a space is no chord character, so with the cursor
// inside a chord's name (after "\[", up to before "]") the space goes after the chord. The offset
// after the chord's "]", or nullopt when the cursor is not inside a chord's name.
std::optional<std::size_t> spaceTarget(std::string_view line, std::size_t cursor);

enum class Step { Letter, Word };

// Moves the chord at the cursor one step left (direction < 0) or right (> 0). Letter: past one
// code point of the text next to it, or past a whole neighbouring chord. Word: to the start of
// the previous/next word (letters, digits, apostrophes and anything non-ASCII), skipping other
// chords. The cursor keeps its place inside the moved chord. nullopt when there is no chord at
// the cursor or the chord cannot move (line start/end).
std::optional<Change> moveChord(std::string_view line, std::size_t cursor, int direction,
                                Step step);

enum class Part {
    Verse,
    VerseUnnumbered,
    Chorus,
    PreChorus,
    Bridge,
    Intro,
    Outro,
    Instrumental,
    ChorusAgain,    // \insertchorus
    ChorusReminder, // \insertchorus*
    SongHeader,
};

struct Snippet {
    std::string text; // whole lines, ends with "\n"
    std::size_t cursor;
};

// The text to insert at the start of a line, with the cursor in it. `selected` (whole lines, may
// be empty) goes inside the environment; without it the cursor sits on an empty line inside.
Snippet partSnippet(Part part, std::string_view selected);

// Removes the \lilypondfile / \narrowlilypondfile commands (with an optional [..] argument) that
// name exactly `inputName` ("Artist/X.ly"); commands in comments are left alone. A command alone
// on its line (plus whitespace) takes the whole line with it, otherwise only the command goes.
std::string removeScoreReference(std::string_view text, std::string_view inputName);

// The width of the first reference to `inputName` outside comments: true for
// \narrowlilypondfile, false for \lilypondfile; nullopt without a reference.
std::optional<bool> scoreIsNarrow(std::string_view text, std::string_view inputName);

// Turns every reference to `inputName` into \narrowlilypondfile (narrow) or \lilypondfile,
// keeping its [..] argument and file name. Unchanged text when already so.
std::string setScoreWidth(std::string_view text, std::string_view inputName, bool narrow);

} // namespace zub::songedit
