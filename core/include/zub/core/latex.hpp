#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

// Low-level reading of TeX source. Ported from ZubuvZpevnik/scripts/songfile.py so the
// app reads the song database exactly like the linters do.
namespace zub::latex {

// Removes TeX line comments: from the first unescaped '%' to the end of the line.
// "\%" is kept. Newlines are kept, so line numbers stay valid.
std::string stripComments(std::string_view tex);

// Index of the closer matching the opener at `open` ('{' -> '}', '[' -> ']'), respecting
// nesting of the same pair and skipping backslash-escaped characters. nullopt if unbalanced
// or if `open` is not an opener.
std::optional<std::size_t> findMatching(std::string_view tex, std::size_t open);

// Converts a short TeX value (title, artist, key, ...) to plain UTF-8 for display:
// "Green\ Day" -> "Green Day", "E\flt" -> "E♭", "C\shrp m" -> "C♯m",
// "\texorpdfstring{a}{b}" -> "b", "\&" -> "&", "\," -> " ", "\-" -> "". Unknown macros are
// dropped but their arguments are kept; braces are dropped; runs of whitespace collapse to
// one space. Accent macros (\v{c}, \'e) lose their accent: songs are written in UTF-8.
std::string toDisplayText(std::string_view tex);

} // namespace zub::latex
