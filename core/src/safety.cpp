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
    "rrep",
    "setchordwidth",
    "setmainfont",
    "shrp",
    "smaller",
    "smalleronsmallpage",
    "texorpdfstring",
    "textnote",
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

} // namespace zub::safety
