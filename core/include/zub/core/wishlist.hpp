#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/bookfile.hpp>

// The song library's wish list: `wishes.txt` at its top level, UTF-8, one wish per line: an input
// name (`Artist/Title`, optionally a note after it) or any text (`LodJohnB`). Blank lines and lines
// starting with '#' are no wishes and stay. Edits are byte-exact, in the file's line breaks.
namespace zub::wishlist {

// The file name, relative to the song library.
inline constexpr std::string_view kFileName = "wishes.txt";

// The wishes, in file order: `text` is the line trimmed, `name` its first word when that is an
// input name with a '/' (as for a book's wishes, zub::bookfile::Wish), `line` 1-based.
std::vector<bookfile::Wish> parse(std::string_view text);

// `wish` (trimmed) appended as a line in the file's line break (LF for an empty file); a missing
// final line break is added first. Errors: empty, a line break in it, starting with '#'.
std::expected<std::string, std::string> add(std::string_view text, std::string_view wish);

// The wish on `line` removed with its line break, only when it still says `expectedText` (the
// file may have changed since it was read). Errors: no such wish there.
std::expected<std::string, std::string> remove(std::string_view text, std::size_t line,
                                               std::string_view expectedText);

} // namespace zub::wishlist
