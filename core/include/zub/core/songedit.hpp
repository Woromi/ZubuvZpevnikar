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
// [selStart, selEnd) inside the line: "\[<selected>]" with the cursor after the "]".
Change insertChord(std::string_view line, std::size_t cursor, std::size_t selStart,
                   std::size_t selEnd);

struct ChordSpan {
    std::size_t begin;
    std::size_t end; // after "]"
};

// The chord `cursor` is in or touches: inside "\[...]" or exactly at its start or end. Where two
// chords meet, the one that starts at the cursor wins over the one that ends there. nullopt
// without one.
std::optional<ChordSpan> chordAt(std::string_view line, std::size_t cursor);

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

} // namespace zub::songedit
