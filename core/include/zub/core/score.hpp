#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <zub/core/songedit.hpp>

// Score-only previews: a song's LilyPond file (.ly) engraved on its own to SVG, much faster than
// building the song's PDF. core prepares the build and reads its log; the app runs the command
// (QProcess), so it can run in the background and be cancelled.
namespace zub::score {

// Asks prepare() to append a second score that LilyPond only plays: the first \\score block's music
// with repeats unfolded, and a \\midi block.
struct MidiOptions {
    int tempo = 100;        // quarter notes per minute (the song's `tem`, see songTempo)
    double factor = 1.0;    // playback speed: the tempo is tempo * factor
    std::string instrument; // General MIDI name for Staff and TabStaff, see midiInstrument
    // Semitones the music sounds higher than written: the song's capo (songCapo), since the
    // songbook's tablature is written in the capo's fret shapes.
    int transpose = 0;
    bool germanNames = false; // set by prepare from Request::germanNames (the \transpose names)
    // The instrument of each staff of the score (see staves(), by index) as a zub::midi::Instrument
    // id; "", "auto" and unknown ids leave the staff as the file has it, else `instrument`; in a
    // score with several staves a tab staff that sets nothing plays the banjo if its own
    // `stringTunings` is a banjo tuning, else the steel guitar. The
    // "amp" id is the clean electric guitar here: prepare plays the amp staves in a score of their
    // own (Command::ampMidi) so the app can run that one through the amp effect.
    std::vector<std::string> staffInstruments;
    // The instrument of the chord names (`\new ChordNames`, `\chords`) as a zub::midi::Instrument
    // id; "", "silent" and unknown ids keep them silent (LilyPond would play them). "amp" plays
    // them in the amp's book like an amp staff.
    std::string chordInstrument;
    // Which staves sound; prepare sets it (a muted staff gets `\remove "Staff_performer"`).
    enum class Sounding { All, AmpOnly, WithoutAmp };
    Sounding sounding = Sounding::All;
};

struct Request {
    std::filesystem::path songbookRoot; // the ZubuvZpevnik checkout (not read by prepare)
    std::filesystem::path workDir;      // reused between builds
    std::string inputName;              // "Artist/X.ly", as in \lilypondfile{}
    std::string content;                // the score text to engrave (e.g. unsaved editor content)
    bool germanNames = false;           // note names deutsch, see germanNames()
    std::filesystem::path includeDir;   // <package>/lilypond (absolute): zubuvzpevnik.ily etc.
    // Also write a MIDI file of the score (appended second score, see midiScore); the engraved
    // first score and its links are not touched.
    std::optional<MidiOptions> midi;
};

// The base name of the playback MIDI file that prepare's appended score writes (in a \book of its
// own, so a score with its own \midi, which writes score.midi, does not clash).
inline constexpr std::string_view playbackOutputName = "zub-playback";
// ... and of the second book prepare appends when some staves are "amp" and others are not.
inline constexpr std::string_view playbackAmpOutputName = "zub-playback-amp";

struct Command {
    std::string program; // "lilypond"
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    // Set on top of the inherited environment: LC_ALL=C, so the log is in English and
    // errors() can read it (LilyPond translates "error" with the user's locale).
    std::vector<std::pair<std::string, std::string>> environment;
    std::filesystem::path svg;       // the cropped SVG, written on success 
    std::filesystem::path midi;      // the MIDI file, written on success; empty when not asked for
                                     // or the score has no \\score block to play
    // The MIDI file of the amp staves when other staves sound without the amp (two books were
    // appended); empty otherwise. Both files have the same length of music: mix their audio.
    std::filesystem::path ampMidi;
    // `midi` holds only amp staves (every staff of the score chose "amp"): run its audio through the amp.
    bool midiIsAmp = false;
    std::filesystem::path scoreCopy; // the copy of content the build reads
    // 1-based first line of the score copy that prepare appended (the playback score); 0 when
    // nothing was appended. LilyPond reports an error of the music again there.
    int generatedFromLine = 0;
};

// Writes the score copy (workDir/score/<inputName>) and the wrapper document (workDir/score.ly:
// \language english or deutsch, then \include) and creates workDir/out, and returns the command to
// run. Stale copies and outputs of earlier builds are removed. Fails on an invalid request
// (inputName not a relative "dir/Name.ly" path without special characters, relative workDir) or I/O
// errors. LilyPond's point-and-click links stay in the SVG.
std::expected<Command, std::string> prepare(const Request& request);

// The songbook's rule (zubuvzpevnik.sty): Czech and Slovak songs use German note names.
// `lang` is the song's lang value as zub::parseSong gives it; braces and spaces are trimmed,
// case is ignored.
bool germanNames(std::string_view lang);

struct Error {
    std::string message;        // e.g. "syntax error, unexpected '}'"
    std::filesystem::path file; // as LilyPond printed it
    int line = 0;               // 1-based
    int column = 0;             // 1-based
};

// The "file:line:col: error: message" lines of a LilyPond log, in order.
std::vector<Error> errors(std::string_view log);
std::optional<Error> firstError(std::string_view log);

// `found` without the errors in the lines prepare appended to the score copy (the playback
// score): LilyPond reports an error of the copied music a second time there, and the user's text
// has no such lines. Errors in other files are kept.
std::vector<Error> withoutGeneratedErrors(std::vector<Error> found, const Command& command);

// ---- Playback: the preview build also writes a MIDI file; zub::midi renders it to audio ----

// The song's `tem` header value ("{120}") as quarter notes per minute; 100 when it is missing,
// not a plain integer or outside 20 to 400.
int songTempo(std::string_view tem);

// The song's `cap` header value ("{4}") as semitones; 0 when it is missing, not a plain integer
// or outside 0 to 12.
int songCapo(std::string_view cap);

// The General MIDI instrument name to play the score with: "whistle" for a score that includes
// tinwhistle.ily, "banjo" for one with the banjo tuning (zub::fretboard::tuningOf), else
// "acoustic guitar (steel)".
std::string midiInstrument(std::string_view scoreText);

// The music of the first \score { } block of `text` (outside comments, strings and Scheme),
// without that block's own \layout, \header and \midi blocks; nullopt when there is no block
// or its braces are not balanced.
std::optional<std::string> scoreMusic(std::string_view text);

// Multiplies the N of every `\tempo 4 = N` (also `4. = N`, `"Text" 4 = N`, ranges `N-M`) by
// `factor`, rounded, at least 1. Comments, strings, Scheme and a `\tempo "Text"` without a number
// are left alone.
std::string scaleTempo(std::string_view music, double factor);

// Marks the notes of bends and slides for zub::midi::smoothBends: before every `\grace` (the
// bend's target; a `\preBend` before it, only `\^` in between, makes it a pre-bend; a `\^` after
// its note makes the next note its release) and before the note after `\glissando` the text
// `\once \set Staff.midiInstrument = "<marker>"` is inserted, which LilyPond writes as a program
// change at that moment. Comments, strings and Scheme are left alone.
std::string markBends(std::string_view music);

// The score that makes LilyPond write the MIDI file, to append to `text`:
//   \\score { \\unfoldRepeats { <scoreMusic with scaled tempi, markBends> } \\midi { \\tempo 4 = N ... } }
// (the chord names make no sound unless `options.chordInstrument` names one; Staff and TabStaff
// play `options.instrument`). Only numbers and
// the instrument name are written into it. Nullopt when `text` has no \\score block.
std::optional<std::string> midiScore(std::string_view text, const MidiOptions& options);

// ---- Staves: one instrument per staff ----

// Does the first \score block have chord names (`\new ChordNames`, `\chords`)? Comments and
// strings don't count.
bool hasChordNames(std::string_view text);

inline constexpr std::size_t noPosition = static_cast<std::size_t>(-1);

// A `\new Staff` or `\new TabStaff` of the first \score block (other contexts, such as ChordNames,
// Lyrics or Dynamics, are no staves; RhythmicStaff and DrumStaff are skipped). All offsets are byte
// offsets into the text given to staves().
struct Staff {
    std::string type;       // "Staff" or "TabStaff"
    std::string name;       // `\new Staff = "name"`; empty without
    std::string instrument; // the General MIDI name the file sets, empty if none: the first
                            // `\set Staff.midiInstrument = "..."` directly in the staff's music,
                            // else `midiInstrument = "..."` in its \with
    std::string tuning;     // `stringTunings = #...` of the \with without the `#`, e.g.
                            // "banjo-open-g-tuning"; empty without
    std::size_t begin = 0;  // the backslash of `\new`
    std::size_t end = 0;    // after the staff's music
    std::size_t headEnd = 0;           // after `\new Staff` and its `= "name"`
    std::size_t withOpen = noPosition; // the `{` and `}` of its \with block
    std::size_t withClose = noPosition;
    std::size_t setBegin = noPosition;  // the text inside the quotes of the `\set`
    std::size_t setEnd = noPosition;
    std::size_t withBegin = noPosition; // ... and of `midiInstrument = "..."` in the \with
    std::size_t withEnd = noPosition;
    std::size_t insertAt = noPosition;  // right after the `{` (or `<<`) that opens the music
};

// The staves of the first \score block of `text` in the order they are written, outside
// comments, strings and Scheme (a staff inside another one is part of that one). A staff whose
// music is a bare variable (`\new Staff \melody`) has no insertAt.
std::vector<Staff> staves(std::string_view text);

// `text` with the staff's instrument set to the General MIDI name `gmName` for the song file: an
// existing `\set Staff.midiInstrument` (or the \with's) gets the new name, else the line
// `\set Staff.midiInstrument = "<name>"` goes right after the `{` that opens the staff's music
// (own line, indented like the next line, in the file's line ending; or inline when the brace has
// code behind it). Everything else is byte-exact. The change's cursor is behind the name. Nullopt
// when there is no such staff, `gmName` is not one of zub::midi::instruments() names, or the
// staff's music is a bare variable.
std::optional<songedit::Change> saveStaffInstrument(std::string_view text, std::size_t staffIndex,
                                                    std::string_view gmName);

struct NewScore {
    std::string text;
    std::size_t cursor; // byte offset inside the TabStaff braces, where the notes go
};

// A skeleton like the songbook's guitar tablature files.
NewScore newScore();

// The songbook's naming of scores: <SongFileName><PartName>.ly next to the song, e.g.
// HeartOfGold.tex -> HeartOfGoldGuitarIntro.ly, or <SongFileName>.ly for a song's only score.
// partName turns what the user typed into the part name: words (split at spaces, '-' and '_') each
// start with a capital letter and are joined ("guitar intro" -> "GuitarIntro", "solo 2" ->
// "Solo2"). Empty input gives "" (the song's only score); nullopt when a word has anything but
// ASCII letters and digits.
std::optional<std::string> partName(std::string_view typed);
// songStem + part + ".ly".
std::string scoreFileName(std::string_view songStem, std::string_view part);

} // namespace zub::score
