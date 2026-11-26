#pragma once

#include <filesystem>

// The LaTeX package that builds the songs (Woromi/zubuvzpevnik-package): zubuvzpevnik.sty, the
// classes, lilypond/ and scripts/ (build.sh, songidx.lua, checkpaper.py). The app builds only with
// its own copy, never with a song library's zubuvzpevnik/ submodule (that would run unchecked).
namespace zub::package {

// Whether `dir` is a package checkout: it has zubuvzpevnik.sty and scripts/build.sh. (A songbook
// from before the package became a repository of its own has zubuvzpevnik/ without scripts/.)
bool isPackage(const std::filesystem::path& dir);

// Whether the package defines `\transposesong` (what a book with a key needs): a non-comment line
// of zubuvzpevnik.sty or of feature/*.cfg that defines the command (\newcommand, \def, ...).
bool definesTransposesong(const std::filesystem::path& dir);

} // namespace zub::package
