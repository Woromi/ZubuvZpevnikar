#pragma once

#include <expected>
#include <map>
#include <string>
#include <string_view>

namespace zub {

// Metadata of one song: the \beginsong{title}[key={value}, ...] header.
// Everything is kept as raw TeX; use latex::toDisplayText() to show it.
struct SongMetadata {
    std::string title;
    std::map<std::string, std::string, std::less<>> keys; // by, lang, key, tun, tem, met, pap, ...

    // Raw value of `key`, or "" if the song does not set it.
    std::string value(std::string_view key) const;
};

// Parses the \beginsong header of a song file's content. Comments are ignored. On failure
// returns a short message (e.g. "no \beginsong").
std::expected<SongMetadata, std::string> parseSong(std::string_view content);

// Parses the body of a [key={value}, ...] option list. Brace-aware, so values may contain
// commas, brackets and macros. The last value wins on duplicate keys.
std::map<std::string, std::string, std::less<>> parseOptions(std::string_view options);

} // namespace zub
