#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

// Songs are built with lualatex -shell-escape (LilyPond needs it), so a song file is code: an
// imported song containing e.g. \directlua{os.execute(...)} would run it on preview. Before
// building a song automatically, the app checks it against the commands songs actually use.
// This is an allowlist speed bump, not a sandbox.
namespace zub::safety {

// Control words (without the backslash) used by ZubuvZpevnik songs: the songs package and the
// zubuvzpevnik macros, collected from the song database. Sorted.
std::span<const std::string_view> knownSongCommands();

// Distinct suspicious constructs in `songContent`, in order of first appearance, comments
// ignored: control words not in knownSongCommands() (reported as "\name"), and TeX "^^"
// character escapes (reported as written, e.g. "^^5c"). Empty means nothing suspicious.
std::vector<std::string> unknownConstructs(std::string_view songContent);

// LilyPond scores (.ly) are code too: lyluatex and the score preview run lilypond, whose Scheme
// can run anything (#(system "...")). Distinct constructs in `ly` that can run code, in order of
// first appearance, % comments and strings ignored:
//   - "#(head" / "$(head": Scheme whose head is not in the allowlist ({ly:make-moment} with
//     plain number arguments only),
//   - "$name": embedded Scheme variable reference,
//   - "#." "#," "#`": Scheme reader tricks,
//   - "\\include \"name\"": anything except zubuvzpevnik.ily and tinwhistle.ily (the shared
//     files of the songbook, trusted).
// Data forms are fine: #'sym, #'(...), ##t, #-6, #"...", #UP, #name, numbers; the inside of
// #{ ... #} is checked like the rest. This is an allowlist speed bump, not a sandbox.
std::vector<std::string> lilypondRisks(std::string_view ly);

} // namespace zub::safety
