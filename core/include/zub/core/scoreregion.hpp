#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Edits of a REGION of a LilyPond score: the notes the user marked (by a rectangle in the
// rendered score or by a selection in the editor). The text is the whole score (multi-line), the
// positions are UTF-8 byte offsets, the region is a half-open range [begin, end). Tokens are read
// like zub::fretboard reads them: notes (`g'16\2`), chords (`<d\4 g\3>8`), rests, and what is
// written against them (`~ ( ) [ ]`, `^\strokeDown`, `-.`). Comments, strings and other commands
// are skipped. Not supported: duration multipliers (`c4*3/2`), which no score of the songbook
// uses.
namespace zub::scoreregion {

struct Range {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool operator==(const Range&) const = default;
};

// The byte offset of the 1-based `line` and 0-based `column` (code points, as LilyPond counts
// them; past the end of the line: its end). Nothing when the text has no such line.
std::optional<std::size_t> offsetOf(std::string_view text, int line, int column);

// Whole tokens: from the start of the first note, chord or rest that overlaps `range` (a chord
// from its `<`, with the music functions written right before it: `\deadNote`, `\grace`,
// `\tweak x #y`, `\once \override ...`) to the end of the last one, including its duration, string, `>`, and the
// articulations written right against it. A range whose begin equals its end is a point. Nothing
// when the range touches no note, chord or rest.
std::optional<Range> expandToEvents(std::string_view text, Range range);

// The music events of a score in text order: notes, chords, `q` and rests, each from its first
// prefix function (see expandToEvents) to the end of its articulations. Chord names, lyrics, marks,
// markup, Scheme and the value of a variable definition (`emChord = <e\6 g\3>4`) are none. These are
// the units of a selection in the rendered score: it runs over whole events, in this order.
std::vector<Range> musicEvents(std::string_view text);

// The index of the event with `begin <= offset < end`; nothing between events.
std::optional<std::size_t> eventAt(const std::vector<Range>& events, std::size_t offset);

// The places a caret between events can stand, like between characters of a text: before each
// event (0 .. size-1) and behind the last one (size). An event's end and the next event's begin
// are the same stop. `caretStop`: the stop of a text offset (the number of events that end at or
// before it; inside an event it is that event's stop); `stopOffset`: where the caret of a stop is
// written (the event's begin, the last event's end for the last stop; 0 without events).
std::size_t caretStop(const std::vector<Range>& events, std::size_t offset);
std::size_t stopOffset(const std::vector<Range>& events, std::size_t stop);

enum class Action {
    Tie,        // `~` after each note or chord that a note or chord follows (not when already tied)
    Slur,       // `(` after the first note or chord, `)` after the last (needs two or more)
    Glissando,  // ` \glissando` after the first note or chord
    Repeat,     // `\repeat volta 2 { ... }` around the region (twice: nested)
    StrokeDown, // `^\strokeDown` after every note and chord, replacing `^\strokeUp`
    StrokeUp,   // `^\strokeUp` after every note and chord, replacing `^\strokeDown`
    DeadNotes,  // `\deadNote ` before every note (also in chords); removed when all have it
};

struct Result {
    std::string text;
    Range selection; // the transformed region
};

// Applies `action` to the notes of the region (expandToEvents of `range` first). Rests never get
// ties, slurs, glissandos or strokes. Nothing when there is nothing to apply it to (no token in
// the range; no note; a slur over one note); when the text is already as the action wants it
// (already tied, slurred ...), the result has the same text. Applying the same action twice never
// doubles anything, except Repeat, which nests.
//
// Why `apply` gave nothing, when it is more than "nothing to apply to".
enum class Refusal {
    NothingToApply,
    CrossesBlock, // Repeat: the `{ }` of the region are not balanced
    SlurPresent,  // Slur: a slur is open across the region's start, or the region has slur marks
};
// `why` (when given) tells the reason of an empty result.
std::optional<Result> apply(Action action, std::string_view text, Range range,
                            Refusal* why = nullptr);

enum class RemoveRefusal {
    NothingToRemove, // the range touches no event
    UnclosedChord,   // an event of the range is a chord without its `>`: the text is not what the
                     // events say
};

// Deletes the events of the range (expandToEvents of `range` first) like text: each event from its
// first prefix function to the end of its articulations, with the blanks around it so that no
// double or trailing space remains. Line breaks, comments, bar checks, braces and every other
// non-event stay (a line left empty stays, without its indentation). What would change meaning
// is repaired: the next event that wrote no duration, if the removed ones changed the duration in
// force for it, gets that duration written; a slur `( )`, phrasing slur `\( \)` or beam `[ ]`
// that loses one end loses the other too; a `~` on the event right before the range goes (it
// would tie to nothing). `selection` of the result is the empty range where the removed text
// began.
std::optional<Result> removeEvents(std::string_view text, Range range,
                                   RemoveRefusal* why = nullptr);

} // namespace zub::scoreregion
