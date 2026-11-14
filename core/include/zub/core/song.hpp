#pragma once

#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace zub {

// Metadata of one song: the \beginsong{title}[key={value}, ...] header.
// Everything is kept as raw TeX; use latex::toDisplayText() to show it.
struct SongMetadata {
    std::string title;
    // by, lang, key, tun, tem, met, pap, tags, ...
    std::map<std::string, std::string, std::less<>> keys;

    // Raw value of `key`, or "" if the song does not set it.
    std::string value(std::string_view key) const;
};

// Parses the \beginsong header of a song file's content. Comments are ignored. On failure
// returns a short message (e.g. "no \beginsong").
std::expected<SongMetadata, std::string> parseSong(std::string_view content);

// Parses the body of a [key={value}, ...] option list. Brace-aware, so values may contain
// commas, brackets and macros. The last value wins on duplicate keys.
std::map<std::string, std::string, std::less<>> parseOptions(std::string_view options);

// Why `inputName` ("Artist/Title", relative, '/'-separated, no .tex) cannot name a song the
// songbook can \input; nullopt if it is fine.
std::optional<std::string> invalidInputName(std::string_view inputName);

// The languages of a `lang` value ("czech, quenya", with or without its braces): split on commas,
// trimmed, empties dropped. The first is the song's language (grammar, chord and note naming,
// index order, book filtering); the rest only tag it (lokalizace.cfg). Port of songfile.py's
// song_langs / main_lang.
std::vector<std::string> songLanguages(std::string_view lang);
std::string mainLanguage(std::string_view lang); // "" without one

} // namespace zub
