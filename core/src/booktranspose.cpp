#include <array>
#include <zub/core/booktranspose.hpp>
#include <zub/core/score.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songcheck.hpp>
#include <zub/core/songheader.hpp>

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

namespace {

struct ScannedKey {
    Key key;
    std::size_t end; // the key proper: letter, accidental, and a following `m` (with its space)
};

// The key at the start of `value` (no leading space or brace skipped); what follows must start a
// new word (see parseKey).
std::optional<ScannedKey> scanKey(string_view value, bool german) {
    const string_view whole = value;
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
    std::size_t end = whole.size() - value.size();
    const string_view afterAccidental = value;
    const auto endsWord = [](string_view rest) {
        return rest.empty() || rest.starts_with("{}") || std::string_view(" \t,\\(-/~}").find(rest.front()) != string_view::npos;
    };
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
        end = whole.size() - value.size();
    }
    // "B\flt\ whistle": the macro's own `\ ` also ends the key.
    if (!endsWord(value) && !(!minor && endsWord(afterAccidental)))
        return std::nullopt;
    return ScannedKey{Key{wrap12(pitch), minor}, end};
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
    const auto scanned = scanKey(value, german);
    if (!scanned)
        return std::nullopt;
    return scanned->key;
}

std::string shiftKeys(string_view value, int semitones, bool german) {
    std::string out;
    std::size_t i = 0;
    bool wordStart = true;
    while (i < value.size()) {
        const char c = value[i];
        if (c == '\\') { // a macro: its name (or one character) is no key
            std::size_t j = i + 1;
            if (j < value.size() && isLetter(value[j]))
                while (j < value.size() && isLetter(value[j]))
                    ++j;
            else if (j < value.size())
                ++j;
            out.append(value.substr(i, j - i));
            i = j;
            wordStart = true;
            continue;
        }
        if (wordStart) {
            if (const auto scanned = scanKey(value.substr(i), german)) {
                const Key moved{wrap12(scanned->key.pitch + semitones), scanned->key.minor};
                out += keyName(moved, german);
                i += scanned->end;
                wordStart = false;
                continue;
            }
        }
        out += c;
        wordStart = std::string_view(" \t,{(/-~").find(c) != string_view::npos;
        ++i;
    }
    return out;
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

std::optional<std::string> raiseCapo(std::string_view songText, int semitones, bool german) {
    const auto meta = parseSong(songText);
    if (!meta)
        return std::nullopt;
    const int raised = score::songCapo(meta->value("cap")) + semitones;
    bool fits = raised >= 0 && raised <= 9;
    for (const int body : songheader::bodyCapos(songText))
        fits = fits && body + semitones >= 0 && body + semitones <= 9;
    if (!fits)
        return std::nullopt;
    std::string text = songheader::shiftBodyCapos(songText, semitones);
    text = songheader::setOption(text, "cap",
                                 raised == 0 ? std::nullopt
                                             : std::optional<std::string>(
                                                   "{" + std::to_string(raised) + "}"));
    if (const auto key = songheader::option(text, "key"))
        text = songheader::setOption(text, "key", "{" + shiftKeys(*key, semitones, german) + "}");
    return text;
}

} // namespace zub::booktranspose
