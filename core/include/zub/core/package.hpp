#pragma once

#include <filesystem>
#include <optional>

// The LaTeX package that builds the songs (Woromi/zubuvzpevnik-package): zubuvzpevnik.sty, the
// classes, lilypond/ and scripts/ (build.sh, songidx.lua, checkpaper.py). A songbook has it as
// the git submodule zubuvzpevnik/; the app bundles a copy for songbooks without one.
namespace zub::package {

// Whether `dir` is a package checkout: it has zubuvzpevnik.sty and scripts/build.sh. (A songbook
// from before the package became a repository of its own has zubuvzpevnik/ without scripts/.)
bool isPackage(const std::filesystem::path& dir);

// The songbook's own package, <songbookRoot>/zubuvzpevnik, when it is a package checkout; nullopt
// otherwise (the submodule is not checked out, or an older songbook).
std::optional<std::filesystem::path> ofSongbook(const std::filesystem::path& songbookRoot);

} // namespace zub::package
