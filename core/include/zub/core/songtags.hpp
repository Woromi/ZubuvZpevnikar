#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Labels of a song: the `tags={a, b}` option of its \beginsong header. The label "fav" marks a
// favourite. The songbook declares the key (\newsongkey{tags}{}{}); the app writes it.
namespace zub::tags {

inline constexpr std::string_view kFavourite = "fav";

// Labels in a raw `tags` value ("fav, t\'abor{\'a}k"): split on commas, trimmed, decoded with
// latex::toDisplayText, empties dropped, duplicates removed (first one kept).
std::vector<std::string> parse(std::string_view value);

bool isFavourite(const std::vector<std::string>& tags);

// Why `tag` cannot be a label (empty after trimming, or a character that needs TeX escaping or
// would split the list: { } \ % , # or a line break); nullopt if it is fine.
std::optional<std::string> invalidTag(std::string_view tag);

// `songText` with the `tags` option of its \beginsong header set to `tags` (each must pass
// invalidTag and be trimmed). An existing option is replaced in place; an empty list removes it
// (with its line); a missing one is added after the `by=` line, or before the closing `]`, or
// inside a one-line header. Everything else stays byte-identical, line endings included. Text
// without \beginsong is returned unchanged.
std::string setTags(std::string_view songText, const std::vector<std::string>& tags);

} // namespace zub::tags
