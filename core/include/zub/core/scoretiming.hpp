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
// moment, length and origin in the score copy (zub::score::playbackNotesName), for the staves'
// notes and also the chord names, the silent melody and the lyrics; the unfolded repeats give each
// written note one entry per pass. The moments become seconds with the tempo map of the
// MIDI file (zub::midi::tempoMap: FluidSynth's audio starts with the MIDI file, and only its tail
// is trimmed), the origins become the music events of the editor's text.
namespace zub::scoretiming {

// What a line of the list was logged in: a staff's voice (`n`: Voice, TabVoice), the chord names
// (`c`), a melody that makes no sound (`m`: Devnull, which times the lyrics) or a lyric (`l`).
// The first lists (before the sounding highlight) had no kind and no length: notes of length 0.
enum class Kind { Note, ChordName, Melody, Lyric };

// One line of the note list.
struct Note {
    double whole = 0;  // moment in whole notes from the start (a grace note's offset included)
    double length = 0; // the event's duration in whole notes (0: not listed)
    Kind kind = Kind::Note;
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

// Something that sounds (or is sung) from `start` to `end` seconds: a music event of a staff, a
// chord name or a lyric syllable of the editor's text, with its bytes.
struct Sound {
    double start = 0;
    double end = 0;
    Kind kind = Kind::Note; // never Melody
    scoreregion::Range range;
};

struct Timeline {
    std::vector<Cue> cues;      // by time, then by position; one per event and pass
    std::vector<Sound> sounds;  // by start, then by position; every part, one per item and pass
    std::size_t firstBegin = 0; // where the first music event of the first \score begins
    std::size_t unmapped = 0;   // notes of the list (of the score's file) in no music event (staff
                                // notes only: chord names and lyrics without a place are skipped)
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

// Everything that sounds at `seconds`, in all parts: the items with start <= seconds < end (every
// event of every staff, the chord name, the lyric syllable). A note logged with no length (an old
// list) and an event of length 0 sound for `minimumSounding` seconds.
inline constexpr double minimumSounding = 0.12;
std::vector<Sound> soundingAt(const Timeline& timeline, double seconds);

// The cue playing at `seconds`: the latest one that has begun (the latest of equal times in text
// order); nullptr before the first.
const Cue* cueAt(const Timeline& timeline, double seconds);

} // namespace zub::scoretiming
