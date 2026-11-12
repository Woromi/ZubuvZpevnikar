#include "zub/core/safety.hpp"

#include <algorithm>
#include <array>
#include <zub/core/latex.hpp>

namespace zub::safety {

namespace {

// Plain byte order (uppercase names first): looked up with binary_search.
constexpr std::array kKnown = std::to_array<std::string_view>({
    "MultiwordChords",
    "addtocounter",
    "baselineadj",
    "beginbridge",
    "beginbridgeb",
    "beginchorus",
    "begininstrumental",
    "begininterlude",
    "beginintro",
    "beginoutro",
    "beginprechorus",
    "beginrecprechorus",
    "beginsong",
    "beginverse",
    "beginverseA",
    "beginverseB",
    "beginverseC",
    "beginwidebridge",
    "bfseries",
    "capo",
    "chordsoff",
    "chordsoffonsmallpage",
    "chordson",
    "copychords",
    "else",
    "endbridge",
    "endbridgeb",
    "endchorus",
    "endinstrumental",
    "endinterlude",
    "endintro",
    "endoutro",
    "endprechorus",
    "endrecprechorus",
    "endsong",
    "endverse",
    "endverseA",
    "endverseB",
    "endverseC",
    "endwidebridge",
    "enquote",
    "fi",
    "flt",
    "foreignlanguage",
    "gtab",
    "iflargepaper",
    "insertchorus",
    "itshape",
    "lilypondfile",
    "lrep",
    "mbar",
    "memorize",
    "meter",
    "multivoice",
    "multivoiceinline",
    "multivoiceinlineonsmallpage",
    "multivoiceoff",
    "multivoiceon",
    "musicnote",
    "narrowlilypondfile",
    "narrowtextnote",
    "newSongPartType",
    "newchords",
    "nochord",
    "normalfont",
    "preferflats",
    "relax",
    "rep",
    "replay",
    "rightarrow",
    "role",
    "rolesoff", // two-voice role labels off (feature/hlasy.cfg); tidy writes it
    "roleson",
    "rrep",
    "setchordwidth",
    "setmainfont",
    "shrp",
    "smaller",
    "smalleronsmallpage",
    "texorpdfstring",
    "textnote",
    "textquotedbl", // the converter's quotation mark that the source never closes
    "textsuperscript",
    "transpose",
    "usechords",
    "versesep",
});

bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '@';
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isLowerHex(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f');
}

} // namespace

std::span<const std::string_view> knownSongCommands() {
    return kKnown;
}

std::vector<std::string> unknownConstructs(std::string_view songContent) {
    const auto text = latex::stripComments(songContent);
    std::vector<std::string> found;
    const auto report = [&found](std::string construct) {
        if (std::ranges::find(found, construct) == found.end())
            found.push_back(std::move(construct));
    };

    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '\\') {
            if (i + 1 >= text.size())
                break;
            if (!isLetter(text[i + 1])) {
                i += 2; // control symbol
                continue;
            }
            std::size_t end = i + 1;
            while (end < text.size() && isLetter(text[end]))
                ++end;
            const std::string_view name(text.data() + i + 1, end - i - 1);
            if (!std::ranges::binary_search(kKnown, name)) {
                // Digits are shown too (\write18), they are not part of the name for TeX.
                auto shown = end;
                while (shown < text.size() && isDigit(text[shown]))
                    ++shown;
                report("\\" + text.substr(i + 1, shown - i - 1));
            }
            i = end;
        } else if (c == '^' && i + 1 < text.size() && text[i + 1] == '^') {
            if (i + 3 < text.size() + 0 && isLowerHex(text[i + 2]) && isLowerHex(text[i + 3])) {
                report(text.substr(i, 4));
            } else if (i + 2 < text.size()) {
                const char n = text[i + 2];
                if (std::string_view(" \t^|\n\r").find(n) == std::string_view::npos) {
                    auto end = i + 3;
                    // keep a whole UTF-8 sequence together
                    while (end < text.size() &&
                           (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                        ++end;
                    report(text.substr(i, end - i));
                }
            }
            i += 2;
        } else {
            ++i;
        }
    }
    return found;
}

namespace {

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// After the "(" of "#(": the head symbol, i.e. up to whitespace or a parenthesis.
std::string_view schemeHead(std::string_view text, std::size_t open) {
    auto i = open + 1;
    while (i < text.size() && isSpace(text[i]))
        ++i;
    const auto start = i;
    while (i < text.size() && !isSpace(text[i]) && text[i] != '(' && text[i] != ')')
        ++i;
    return text.substr(start, i - start);
}

// True when everything from `from` to the closing ")" is a number, a fraction or space.
bool plainNumbersToClose(std::string_view text, std::size_t from) {
    for (auto i = from; i < text.size(); ++i) {
        const char c = text[i];
        if (c == ')')
            return true;
        if (!isSpace(c) && !isDigit(c) && c != '/' && c != '-' && c != '.')
            return false;
    }
    return false;
}

constexpr std::array kTrustedIncludes =
    std::to_array<std::string_view>({"zubuvzpevnik.ily", "tinwhistle.ily"});

} // namespace

std::vector<std::string> lilypondRisks(std::string_view ly) {
    std::vector<std::string> found;
    const auto report = [&found](std::string construct) {
        if (std::ranges::find(found, construct) == found.end())
            found.push_back(std::move(construct));
    };
    const auto scheme = [&](std::size_t sigil, std::size_t open) {
        const auto head = schemeHead(ly, open);
        const auto afterHead = static_cast<std::size_t>(head.data() - ly.data()) + head.size();
        if (head == "ly:make-moment" && plainNumbersToClose(ly, afterHead))
            return;
        report(std::string(ly.substr(sigil, 2)) + std::string(head));
    };

    for (std::size_t i = 0; i < ly.size();) {
        const char c = ly[i];
        if (c == '%') {
            if (i + 1 < ly.size() && ly[i + 1] == '{') {
                const auto close = ly.find("%}", i + 2);
                i = close == std::string_view::npos ? ly.size() : close + 2;
            } else {
                const auto eol = ly.find('\n', i);
                i = eol == std::string_view::npos ? ly.size() : eol + 1;
            }
        } else if (c == '"') {
            ++i;
            while (i < ly.size() && ly[i] != '"')
                i += ly[i] == '\\' ? 2U : 1U;
            ++i;
        } else if (c == '\\') {
            constexpr std::string_view include = "\\include";
            if (ly.substr(i, include.size()) == include &&
                (i + include.size() >= ly.size() || !isLetter(ly[i + include.size()]))) {
                auto j = i + include.size();
                while (j < ly.size() && isSpace(ly[j]))
                    ++j;
                std::string_view name;
                bool quoted = false;
                if (j < ly.size() && ly[j] == '"') {
                    const auto close = ly.find('"', j + 1);
                    if (close != std::string_view::npos) {
                        name = ly.substr(j + 1, close - j - 1);
                        quoted = true;
                    }
                }
                if (!quoted || !std::ranges::contains(kTrustedIncludes, name))
                    report("\\include \"" + std::string(name) + "\"");
                i = quoted ? j + name.size() + 2 : j;
            } else {
                i += 2; // a command or an escaped character; letters are harmless on their own
            }
        } else if (c == '#' || c == '$') {
            const char next = i + 1 < ly.size() ? ly[i + 1] : '\0';
            if (next == '(') {
                scheme(i, i + 1);
            } else if (c == '$' && isIdentStart(next)) {
                std::size_t end = i + 1;
                while (end < ly.size() && (isIdentStart(ly[end]) || isDigit(ly[end]) ||
                                           ly[end] == '-' || ly[end] == '_' || ly[end] == ':'))
                    ++end;
                report(std::string(ly.substr(i, end - i)));
            } else if (c == '#' && (next == '.' || next == ',' || next == '`')) {
                report(std::string("#") + next);
            }
            i += c == '#' && next == '#' ? 2 : 1; // "##t": the second '#' is not a sigil
        } else {
            ++i;
        }
    }
    return found;
}

} // namespace zub::safety
