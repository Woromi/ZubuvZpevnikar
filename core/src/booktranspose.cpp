#include <array>
#include <zub/core/booktranspose.hpp>
#include <zub/core/songcheck.hpp>

namespace zub::booktranspose {

namespace {

using std::string_view;

constexpr std::array<const char*, 12> kSharpNames = {"C",  "C#", "D",  "D#", "E",  "F",
                                                     "F#", "G",  "G#", "A",  "A#", "B"};

bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int wrap12(int v) {
    return ((v % 12) + 12) % 12;
}

// Pitch class of a note letter; nullopt for a non-note.
std::optional<int> letterPitch(char c, bool german) {
    switch (c) {
    case 'C':
        return 0;
    case 'D':
        return 2;
    case 'E':
        return 4;
    case 'F':
        return 5;
    case 'G':
        return 7;
    case 'A':
        return 9;
    case 'B':
        return german ? 10 : 11;
    case 'H':
        return german ? std::optional<int>(11) : std::nullopt;
    default:
        return std::nullopt;
    }
}

// `\name` at the start of `s` (not the prefix of a longer command).
bool startsWithMacro(string_view s, string_view name) {
    return s.starts_with(name) && (s.size() == name.size() || !isLetter(s[name.size()]));
}

} // namespace

std::optional<Key> parseKey(string_view value, bool german) {
    auto skipSpaces = [](string_view& s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            s.remove_prefix(1);
    };
    skipSpaces(value);
    if (value.starts_with('{')) {
        value.remove_prefix(1);
        skipSpaces(value);
    }
    if (value.empty())
        return std::nullopt;
    const auto base = letterPitch(value.front(), german);
    if (!base)
        return std::nullopt;
    value.remove_prefix(1);
    int pitch = *base;
    bool macro = false;
    if (startsWithMacro(value, "\\shrp")) {
        ++pitch;
        value.remove_prefix(5);
        macro = true;
    } else if (startsWithMacro(value, "\\flt")) {
        --pitch;
        value.remove_prefix(4);
        macro = true;
    } else if (value.starts_with('#')) {
        ++pitch;
        value.remove_prefix(1);
    } else if (value.starts_with('b') &&
               (value.size() == 1 || !isLetter(value[1]) ||
                (value[1] == 'm' && (value.size() == 2 || !isLetter(value[2]))))) {
        --pitch;
        value.remove_prefix(1);
    }
    if (macro) {
        // "E\flt{}", "E\flt\ (B\flt\ whistle)", "F\shrp m": the macro's own terminators.
        for (bool again = true; again;) {
            again = false;
            if (value.starts_with("{}")) {
                value.remove_prefix(2);
                again = true;
            } else if (value.starts_with("\\ ") || value.starts_with("\\,")) {
                value.remove_prefix(2);
                again = true;
            } else if (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
                again = true;
            }
        }
    }
    bool minor = false;
    if (value.starts_with('m') && (value.size() == 1 || !isLetter(value[1]))) {
        minor = true;
        value.remove_prefix(1);
    }
    if (!value.empty() && std::string_view(" \t,\\(-/~}").find(value.front()) == string_view::npos)
        return std::nullopt;
    return Key{wrap12(pitch), minor};
}

int shift(Key from, Key target) {
    const int signatureOf = wrap12(from.pitch + (from.minor ? 3 : 0));
    const int signatureTo = wrap12(target.pitch + (target.minor ? 3 : 0));
    int d = wrap12(signatureTo - signatureOf);
    if (d > 6)
        d -= 12;
    return d;
}

std::string keyName(Key key, bool german) {
    const std::string detected =
        std::string(kSharpNames[static_cast<std::size_t>(wrap12(key.pitch))]) +
        (key.minor ? "m" : "");
    return songcheck::keyValue(detected, german);
}

std::optional<Key> bookKey(string_view text) {
    if (text.empty())
        return std::nullopt;
    const auto base = letterPitch(text.front(), false);
    if (!base || text.front() == 'H')
        return std::nullopt;
    int pitch = *base;
    text.remove_prefix(1);
    if (text.starts_with('#')) {
        ++pitch;
        text.remove_prefix(1);
    } else if (text.starts_with('b')) {
        --pitch;
        text.remove_prefix(1);
    }
    bool minor = false;
    if (text == "m") {
        minor = true;
        text = {};
    }
    if (!text.empty())
        return std::nullopt;
    return Key{wrap12(pitch), minor};
}

std::string bookKeyName(Key key) {
    static constexpr std::array<const char*, 12> majors = {"C",  "Db", "D",  "Eb", "E",  "F",
                                                           "F#", "G",  "Ab", "A",  "Bb", "B"};
    const auto i = static_cast<std::size_t>(wrap12(key.pitch));
    return key.minor ? std::string(kSharpNames[i]) + "m" : std::string(majors[i]);
}

} // namespace zub::booktranspose
