#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/songedit.hpp>

// Writing tablature from a fretboard: LilyPond pitches and `\string` numbers as the songbook's
// scores write them (TabStaff, absolute octaves: `<e,\6 h,\5 e\4 g\3 h\2 e'\1>4`, `fis'\1`).
// Positions are UTF-8 byte offsets; the edit functions work on ONE line (without its line
// break), like zub::songedit.
namespace zub::fretboard {

// The open strings as MIDI pitches, string 1 (the highest) first. The Nth string of a score
// (`\N`) is `open[N - 1]`.
struct Tuning {
    std::vector<int> open;
    // A short string (the banjo's 5th) that only exists from fret shortStringStartFret on, 1-based;
    // 0 for none. Frets stay relative to its own nut: `g'\5` is fret 0, drawn at fret 5.
    int shortString = 0;
};

inline constexpr int shortStringStartFret = 5;

Tuning guitar();     // 64 59 55 50 45 40
Tuning banjoOpenG(); // 62 59 55 50 67, shortString 5

// The tuning the score's TabStaff uses: banjoOpenG() when the text sets `banjo-open-g-tuning`
// (outside comments), else guitar() (also for the E-flat songs: their tab tuning is standard).
Tuning tuningOf(std::string_view scoreText);

// An absolute LilyPond pitch with octave marks: c' is 60, e, is 40. German names (c cis d dis e f
// fis g gis a ais h; with flats des es ges as b) or English (c cs d ds e f fs g gs a as b; with
// flats df ef gf af bf).
std::string pitchName(int midi, bool german, bool flats);

// Flats for a song key (`key` value, e.g. "E\flt", "Dm"): keys written with \flt, and F, Dm, Gm,
// Cm and Fm. Only the first key counts in "Cm\rightarrow C".
bool prefersFlats(std::string_view keyValue);

// pitchName + the string: `fis'\1`.
std::string noteToken(int midi, int string, bool german, bool flats);

struct Position {
    int string; // 1-based, as in `\N`
    int fret;   // midi - open[string - 1]; not limited (the caller shows 0...24)
    bool operator==(const Position&) const = default;
};

// The note or chord the cursor is in or right after (only a note with an explicit `\N` has a
// position). Never fails: anything it does not understand gives nothing.
std::vector<Position> positionsAt(std::string_view text, std::size_t cursor, const Tuning& tuning,
                                  bool german);

// The last duration written before the cursor ("4", "8."), skipping those of commands
// (\time 3/4, \tuplet 3/2, \partial 8 ...); "4" when there is none.
std::string effectiveDuration(std::string_view text, std::size_t cursor);

// Where insertNote / insertRest / insertBarCheck really insert on `line` for a cursor there: a
// cursor inside a note or chord (or its articulations) moves behind the whole token. The duration
// "in force" for an insertion is the one at this point (effectiveDuration of the text up to it).
std::size_t insertionPoint(std::string_view line, std::size_t cursor);

using Change = songedit::Change;

struct Insertion {
    std::size_t offset;
    std::string text;
};

// The first note, chord or rest at or after `from` that writes no duration of its own gets
// `effective` explicitly: where to insert it, nothing when that event has a duration or there is
// none (or `effective` is empty). For the caller that inserted a note with another duration across
// lines: insertNote only sees its own line.
std::optional<Insertion> carriedDurationFix(std::string_view text, std::size_t from,
                                            std::string_view effective);

// Inserts `token` (a note `fis'\1` or a chord `<...>`) at the cursor with spaces so that tokens
// never touch, the cursor after it. A cursor inside a note or chord moves to its end first.
// `duration` is written after the pitch (after `>` for a chord) when it is not empty: the caller
// passes it only when it differs from effectiveDuration(). `effective` is that duration (the one in
// force before the insertion, from the whole text): when a new `duration` is written and the next
// note, chord or rest on the line writes none of its own, it gets `effective` so that its length
// does not change. (A following event on a later line is out of reach: see carriedDurationFix.)
Change insertNote(std::string_view line, std::size_t cursor, std::string_view token,
                  std::string_view duration, std::string_view effective = {});

// A chord just before the cursor (or the cursor inside it) gets `token` (`pitch\N`, no duration)
// before its `>`. A single note with a string becomes `<note token>` keeping its duration and what
// follows it (`g4\3(` + `e\2` -> `<g\3 e\2>4(`). Without either: insertNote(line, cursor, token,
// "").
Change addToChord(std::string_view line, std::size_t cursor, std::string_view token);

// `r` and `duration` (see insertNote), and a bar check ` |`.
Change insertRest(std::string_view line, std::size_t cursor, std::string_view duration,
                  std::string_view effective = {});
Change insertBarCheck(std::string_view line, std::size_t cursor);

} // namespace zub::fretboard
