#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/scoreregion.hpp>

// The score cursor as the play position: when each note of a score sounds in the playback audio,
// and where it is written. LilyPond's playback score lists every note event it plays with its
// moment and its origin in the score copy (zub::score::playbackNotesName); the unfolded repeats
// give each written note one entry per pass. The moments become seconds with the tempo map of the
// MIDI file (zub::midi::tempoMap: FluidSynth's audio starts with the MIDI file, and only its tail
// is trimmed), the origins become the music events of the editor's text.
namespace zub::scoretiming {

// One line of the note list.
struct Note {
    double whole = 0; // moment in whole notes from the start (a grace note's offset included)
    int line = 0;     // 1-based, as in LilyPond's point-and-click links
    int column = 0;   // 0-based, code points
    std::string file; // the file the note is written in, as LilyPond opened it
};

// The lines of the note list in file order; lines that do not parse are skipped.
std::vector<Note> parseNotes(std::string_view text);

// A music event of the editor's text that sounds at `seconds`.
struct Cue {
    double seconds = 0;
    scoreregion::Range event;
};

struct Timeline {
    std::vector<Cue> cues;      // by time, then by position; one per event and pass
    std::size_t firstBegin = 0; // where the first music event of the first \score begins
    std::size_t unmapped = 0;   // notes of the list (of the score's file) in no music event
    bool empty() const { return cues.empty(); }
};

struct Source {
    std::string_view notes;         // the note list file
    std::string_view midi;          // the playback MIDI file (its tempo map)
    std::string_view copy;          // the score copy LilyPond read, as written
    std::string_view original;      // the editor's text (LF) the copy's start was made from
    std::filesystem::path copyFile; // zub::score::Command::scoreCopy: notes of other files
                                    // are skipped
    int generatedFromLine = 0;      // zub::score::Command::generatedFromLine
};

// The timeline of the score: each note of the list that sits in a music event of `original` (a
// note written in the playback score's own copy of the first \score block is mapped back to the
// event it was copied from) gets a cue; a variable's notes (`mel = { c'4 d' }`) get one for each
// time the variable is used, but a definition of one note or chord (`emChord = <e\6 g\3>4`) is no
// music event (zub::scoreregion::musicEvents) and has none. Error when the files do not belong
// together (the MIDI file is unreadable, the copy does not start with the original, or the playback
// score's events cannot be paired with the original's).
std::expected<Timeline, std::string> buildTimeline(const Source& source);

// Where playback starts for a caret (or the start of a selection) at byte offset `caret` of the
// text: the first pass of the first event at or after it that has a cue; 0 when the caret is
// before the music of the first \score (`firstBegin`), nothing when no event at or after it sounds.
std::optional<double> startSeconds(const Timeline& timeline, std::size_t caret);

// The cue playing at `seconds`: the latest one that has begun (the latest of equal times in text
// order); nullptr before the first.
const Cue* cueAt(const Timeline& timeline, double seconds);

} // namespace zub::scoretiming
