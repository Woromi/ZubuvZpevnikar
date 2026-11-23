#pragma once

#include <optional>
#include <string>
#include <string_view>

// Keys for transposing a songbook: reading a song's `key=` value, the smallest move to a book's
// key, and writing a key the way songs do. Std-only.
namespace zub::booktranspose {

struct Key {
    int pitch = 0; // pitch class, 0 = C ... 11 = B (B natural)
    bool minor = false;
    bool operator==(const Key&) const = default;
};

// The first key of a song's `key=` value (with or without its braces): `G`, `Am`, `F\shrp m`,
// `B\flt`, `E\flt\ (B\flt\ whistle)`, `Cm\rightarrow C`, `Dm, F`, `G (D whistle)`, `C#`, `Bb`.
// Whatever follows the first key must start with a space, a comma, `\`, `(`, `-`, `/` or `~`.
// Accidentals: `\shrp`, `\flt`, `#`, `b`. With `german` (lang czech/slovak,
// zub::score::germanNames) `H` is B natural and `B` is B flat. Nullopt for an empty or unreadable
// value (`Fis`, `Dmaj`).
std::optional<Key> parseKey(std::string_view value, bool german);

// `value` (a `key=` value, with or without braces) with EVERY key it holds moved by `semitones`,
// each written by keyName in its own minor/major (`Cm\rightarrow C`, `B\flt m\rightarrow G\flt`,
// `Dm, F`, `E\flt\ (B\flt\ whistle)`). Everything else stays byte-identical: separators,
// macros, parentheses and the macro terminators after a key (`{}`, `\ `). A key is rewritten in
// keyName's spelling even by 0 (`C#` -> `C\shrp`, `A\shrp` -> `B\flt`), so +N then -N gives back
// the text only where the original already is that spelling.
std::string shiftKeys(std::string_view value, int semitones, bool german);

// Semitones, in -5..+6, that move a song in `from` to the book's `target`. Keys are compared by
// key signature: a minor song goes to the relative minor of a major target, a major song to the
// relative major of a minor target (target G: Am songs go to Em, C songs to G; target Em: C songs
// go to G, Am songs to Em). A tie (6) goes up.
int shift(Key from, Key target);

// The key as songs write it: the spelling of zub::songcheck::keyValue (A#, D#, G# majors are
// flats, the other black keys sharps; `\shrp`/`\flt` macros with " m" after a macro; German: H,
// and B for B flat).
std::string keyName(Key key, bool german);

// The key of a book's setting line: `G`, `Em`, `Bb`, `F#`, `F#m`, `C`. Plain ASCII, English names
// (B = B natural, `b` = flat, `#` = sharp), minor with a trailing `m`. bookKeyName writes flats
// for Db Eb Gb Ab Bb (majors) and sharps for the rest (`C#m`, `F#`, `G#m`, `D#m`, `A#m`); bookKey
// also reads the other spelling (`C#`, `Dbm`).
std::optional<Key> bookKey(std::string_view text);
std::string bookKeyName(Key key);

// ---- Transposing a book for one build ----
//
// Nothing of this is saved: the app applies it to its build copy of the book (and of the songs) in
// the cache, the book file and the songs stay as they are.
//
// Chords: a song is re-shaped by a `\transposesong[key=K]{N}` line in the copy. Capo: a song keeps
// its shapes and its capo rises by N (raiseCapo, a copy of the song shadows it on TEXINPUTS); a
// song whose capo would leave 0..9 is re-shaped instead. NoCapo: the copy gets the class option
// `transposecapos` (every cap= and \capo transposes the chords to sounding pitch instead of
// printing "Capo n"), then the songs are re-shaped from their sounding key.
enum class Mode { Chords, Capo, NoCapo };

// The song text (a song file's) sounding `semitones` higher with the same shapes: `cap=` raised
// (removed at 0: songs without a capo have none), every code `\capo{n}` raised, every key of
// `key=` moved (shiftKeys). Nullopt when the header cannot be read or a capo would leave 0..9.
std::optional<std::string> raiseCapo(std::string_view songText, int semitones, bool german);

} // namespace zub::booktranspose
