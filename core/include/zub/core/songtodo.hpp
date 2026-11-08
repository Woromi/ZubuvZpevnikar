#pragma once

#include <string>
#include <string_view>
#include <vector>

// TODO notes in songs: comment lines such as "% TODO: Zkontrolovat akordy", a convention of the
// songbook. An imported song gets one ("% TODO: not checked yet (imported by the app)") under its
// header until the user marks it checked, so unfinished songs can be listed.
namespace zub::todo {

// The note the app writes into an imported song (without "% TODO: ").
inline constexpr std::string_view kNotChecked = "not checked yet (imported by the app)";

// The text of every TODO comment, in file order: a comment (from an unescaped '%') whose text
// starts with "TODO", the "TODO" and a ':' after it dropped ("% TODO: Solo" -> "Solo"; a bare
// "% TODO" -> "TODO"). Lines with a TODO after other text count too.
std::vector<std::string> notes(std::string_view song);

// Whether the song has the app's "not checked yet" line.
bool hasNotCheckedMark(std::string_view song);

// The song with "% TODO: not checked yet (imported by the app)" on its own line after the
// \beginsong header (its title and options), in the file's line ending; unchanged when it is
// there already. Without a \beginsong the line goes first.
std::string addNotCheckedMark(std::string_view song);

// The song without the app's line (every copy of it); unchanged when there is none.
std::string removeNotCheckedMark(std::string_view song);

} // namespace zub::todo
