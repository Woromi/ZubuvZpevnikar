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

} // namespace zub::scoreregion
