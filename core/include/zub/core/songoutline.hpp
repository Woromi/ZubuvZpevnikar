#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/songsuggest.hpp>

// A song's parts in order, as a schema: "Verse 1, Chorus 1, Verse 2 (no chords), Chorus 2
// (reminder line)". Make Smaller shows it for a proposal, with the parts the proposal changes
// marked, so the user can tell apart proposals whose descriptions read the same.
//
// Parts are named and numbered like chords_converter's descriptions ("verse 6", "the 3rd
// chorus"): verses by their count, choruses by how often that chorus (or chorus number) has come
// so far, repeats included, anything else by its count of its kind.
namespace zub::songoutline {

enum class Chords {
    On,
    Off,            // \chordsoff
    OffOnSmallPage, // \chordsoffonsmallpage
};

enum class Repeat {
    None,     // a part written out: \beginverse ... \endverse
    Full,     // \insertchorus
    Reminder, // \insertchorus*: the chorus's first line
};

// A line as printed, for a sketch of the part: the text and, above it, the chords at the
// characters (code points) they sit over ("Am      F" over "Our paper lanterns float").
struct ShownLine {
    std::string chords; // spaces between chord names; empty without chords; a chord that would
                        // cross kShownColumns is left out
    std::string text;   // plain UTF-8, TeX markup removed; ends in "…" when cut
    bool operator==(const ShownLine&) const = default;
};

inline constexpr std::size_t kShownLines = 1;    // per part
inline constexpr std::size_t kShownColumns = 36; // code points per shown line

struct Part {
    std::string kind; // "verse", "chorus", "bridge", ... as in \begin<kind>; "verse*" unnumbered
    std::string name; // "Verse 3", "Chorus 2", "Chorus R2 (1)", "Bridge 1"
    std::string label; // as printed before the part: "3." for a verse, "R:", "R2:", "*", "P:", ""
    // Its first lines (kShownLines at most). A repeat shows the chorus it repeats (a reminder its
    // first line, with "..."); chords are shown whatever `chords` says.
    std::vector<ShownLine> shown;
    Chords chords = Chords::On; // as the part starts (setup before its first line)
    bool chordsOffPartWay = false; // a \chordsoff(onsmallpage) after its first line: some lines
                                   // without chords
    Repeat repeat = Repeat::None;
    int lines = 0; // printed source lines (text or chords) of a written-out part; 0 for a repeat
    int line = 0;          // where it starts (1-based)
    std::size_t begin = 0; // byte span in the text, \begin<kind> to after \end<kind>
    std::size_t end = 0;
    bool operator==(const Part&) const = default;
};

struct Outline {
    std::vector<Part> parts;
    bool smallerOnSmallPage = false; // \smalleronsmallpage outside the parts
    bool operator==(const Outline&) const = default;
};

// Reads the parts between \beginsong and \endsong (the whole text when there is none). Comments
// are ignored. Chords switched off outside the parts (\chordsoff before a verse) carry into the
// following parts, as in TeX.
Outline outline(std::string_view songText);

// The text with the edits made. They must lie inside the text and must not overlap; their order
// does not matter.
std::expected<std::string, std::string> applyEdits(std::string_view text,
                                                   std::span<const songsuggest::Edit> edits);

struct Preview {
    // The outline with the edits made. Its lines are those of `before` (the editor's text), its
    // byte spans those of the changed text.
    Outline after;
    std::vector<bool> changed;   // per part of `after`: an edit touches it, or it looks different
    bool songChanged = false;    // an edit outside the parts, or smallerChanged
    bool smallerChanged = false; // \smalleronsmallpage added or removed
    // per part of `after`: its `lines` before the change; -1 when the parts can't be matched
    // (the change added or removed parts)
    std::vector<int> linesBefore;
    // per part of `after`: one of the proposal's sources (the part it relies on)
    std::vector<bool> source;
};

// What a proposal does, for showing: the outline after its edits, with the touched parts and
// the parts it relies on (`sources`, positions in `before`).
std::expected<Preview, std::string> preview(std::string_view before,
                                            std::span<const songsuggest::Edit> edits,
                                            std::span<const songsuggest::Source> sources = {});

} // namespace zub::songoutline
