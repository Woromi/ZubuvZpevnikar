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

} // namespace zub::booktranspose
