#pragma once

#include <zub/core/songsuggest.hpp>

#include <optional>
#include <string>
#include <string_view>

// Options of a song's \beginsong header (`key={value}`), edited byte-exact. `zub::tags::setTags`
// and the `pap` helpers are built on it.
namespace zub::songheader {

// `songText` with the `key` option of its \beginsong header set to `value`, which is written
// verbatim (braces included: "{a4,a5}"). An existing option is replaced in place; an empty or
// missing value removes it (with its line, or its share of the commas in a one-line header; with
// `dropEmptyHeader` the `[...]` goes too when the option was all it held, like checkpaper.py);
// a missing option is added after the `by=` line, or before the closing `]`, or inside a
// one-line header, in the file's own layout. Everything else stays byte-identical, line endings
// included. Text without \beginsong is returned unchanged.
std::string setOption(std::string_view songText, std::string_view key,
                      std::optional<std::string_view> value, bool dropEmptyHeader = false);

// The value of the last `key` option of the header with its outer braces taken off, raw
// (not decoded); nullopt if there is none.
std::optional<std::string> option(std::string_view songText, std::string_view key);

// The `pap` list after measuring: for each MEASURED paper, one page (fits, one page) ensures
// `paper` and drops `paperx2`; not fitting drops `paper` but keeps `paperx2` (a human's mark);
// fitting on more than one page changes nothing. Unmeasured papers and unknown entries stay.
// Result: a4 entries, then a5 entries, then the others in their old order, joined by "," with no
// blanks ("a4,a5"); empty when nothing is left.
std::string measuredPapers(std::string_view currentPap, const std::optional<songsuggest::Measured>& a4,
                           const std::optional<songsuggest::Measured>& a5);

// `songText` with its `pap` set to `pap` (the bare list, "a4,a5"; empty writes `pap={}`: "fits
// nothing", as chords_converter spells it; checkpaper.py's set_pap removes the key instead).
std::string setPap(std::string_view songText, std::string_view pap);

} // namespace zub::songheader
