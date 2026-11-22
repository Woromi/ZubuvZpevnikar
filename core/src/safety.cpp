#include "zub/core/safety.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <zub/core/latex.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>

namespace zub::safety {

namespace {

// Plain byte order (uppercase names first): looked up with binary_search. The commands real songs
// use, plus every user command of the songs package (songs.sty: no @, no SB prefix), except the
// ones that open a file by a name given in the song (\newindex, \newauthorindex,
// \newscripindex, \showindex: preamble commands) and the TeX primitives songs.sty itself uses
// (\csname would build any command name, e.g. \directlua, from text).
constexpr std::array kKnown = std::to_array<std::string_view>({
    "Acolon",
    "BarreDelims",
    "Bcolon",
    "DeclareFlatSize",
    "DeclareLyricChar",
    "DeclareNoHyphen",
    "DeclareNonLyric",
    "H",
    "MultiwordChords",
    "O",
    "X",
    "addtocounter",
    "afterpreludeskip",
    "alphascale",
    "authbyword",
    "authignoreword",
    "authsepword",
    "baselineadj",
    "beforepostludeskip",
    "beginbridge",
    "beginbridgeb",
    "beginchorus",
    "begininstrumental",
    "begininterlude",
    "beginintro",
    "beginoutro",
    "beginprechorus",
    "beginrecprechorus",
    "beginscripture",
    "beginsong",
    "beginverse",
    "beginverseA",
    "beginverseB",
    "beginverseC",
    "beginwidebridge",
    "bfseries",
    "brk",
    "capo",
    "cbarwidth",
    "ch",
    "chordedfalse",
    "chordedtrue",
    "chordlocals",
    "chordsoff",
    "chordsoffonsmallpage",
    "chordson",
    "chorus",
    "chorusfont",
    "chorusjustify",
    "chorusmark",
    "clineparams",
    "colbotglue",
    "commitsongs",
    "copychords",
    "copyright",
    "echo",
    "echofont",
    "else",
    "endbridge",
    "endbridgeb",
    "endchorus",
    "endidxblock",
    "endinstrumental",
    "endinterlude",
    "endintersong",
    "endintro",
    "endoutro",
    "endprechorus",
    "endrecprechorus",
    "endscripture",
    "endsong",
    "endsonggroup",
    "endsongs",
    "endverse",
    "endverseA",
    "endverseB",
    "endverseC",
    "endwidebridge",
    "enquote",
    "everychorus",
    "everyverse",
    "extendpostlude",
    "extendprelude",
    "fi",
    "flatsymbol",
    "flt",
    "foreachtitle",
    "foreignlanguage",
    "gtab",
    "gtabtrans",
    "idxaltentry",
    "idxauthfont",
    "idxbgcolor",
    "idxblock",
    "idxbook",
    "idxcont",
    "idxentry",
    "idxheadfont",
    "idxheadwidth",
    "idxlyricfont",
    "idxrefsfont",
    "idxscripfont",
    "idxtitlefont",
    "ifchorded", // the songs package: chords shown or not (\ifchorded … \else … \fi)
    "iflargepaper",
    "iflyric",
    "ifmeasures",
    "ifnolyrics",
    "ifpagepreludes",
    "ifpartiallist",
    "ifrawtext",
    "ifrepchorus",
    "ifsepindexes",
    "ifslides",
    "ifsongindexes",
    "iftranscapos",
    "ifvnumbered",
    "includeonlysongs",
    "indexentry",
    "indexesoff",
    "indexeson",
    "indexsongsas",
    "indextitleentry",
    "insertchorus",
    "insertprechorus", // a repeatable part (\newSongPartType*{prechorus}), like \insertchorus
    "intersong",
    "itshape",
    "justifycenter",
    "justifyleft",
    "lastcolglue",
    "lilypondfile",
    "lrep",
    "lyricfalse",
    "lyricfont",
    "lyrictrue",
    "makepostlude",
    "makeprelude",
    "mbar",
    "mch",
    "measurebar",
    "measuresfalse",
    "measuresoff",
    "measureson",
    "measurestrue",
    "memorize",
    "meter",
    "meterfont",
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
    "newsongkey",
    "nextcol",
    "nexttitle",
    "nochord",
    "nolyrics",
    "nolyricsfalse",
    "nolyricstrue",
    "norepchoruses",
    "normalfont",
    "nosongnumbers",
    "notebgcolor",
    "notefont",
    "notejustify",
    "notenameA",
    "notenameB",
    "notenameC",
    "notenameD",
    "notenameE",
    "notenameF",
    "notenameG",
    "notenames",
    "notenamesin",
    "notenamesout",
    "notrans",
    "noversenumbers",
    "onesongcolumn",
    "pagepreludes",
    "pagepreludesfalse",
    "pagepreludestrue",
    "partiallistfalse",
    "partiallisttrue",
    "placenote",
    "placeversenum",
    "preferflats", // the songs package: one song spelled with flats / sharps
    "prefersharps",
    "printchord",
    "printnoteA",
    "printnoteB",
    "printnoteC",
    "printnoteD",
    "printnoteE",
    "printnoteF",
    "printnoteG",
    "printscrcite",
    "printsongnum",
    "printversenum",
    "rawtextfalse",
    "rawtexttrue",
    "relax",
    "rep",
    "repchoruses",
    "repchorusfalse",
    "repchorustrue",
    "replay",
    "resettitles",
    "rightarrow",
    "role",
    "rolesoff", // two-voice role labels off (feature/hlasy.cfg); tidy writes it
    "roleson",
    "rrep",
    "sbarheight",
    "scitehere",
    "scleardpage",
    "sclearpage",
    "scripindent",
    "scripoutdent",
    "scripture",
    "scripturefont",
    "scriptureoff",
    "scriptureon",
    "sepindexesfalse",
    "sepindexestrue",
    "sepverses",
    "setchordwidth",
    "setlicense",
    "setmainfont",
    "sharpsymbol",
    "shiftdblquotes",
    "showauthors",
    "showrefs",
    "shrp",
    "slides",
    "slidesfalse",
    "slidestrue",
    "smaller",
    "smalleronsmallpage",
    "snumbgcolor",
    "solfedge",
    "song",
    "songauthors",
    "songchapter",
    "songcolumns",
    "songcopyright",
    "songgroup",
    "songindexesfalse",
    "songindexestrue",
    "songlicense",
    "songlink",
    "songlist",
    "songmark",
    "songnumstyle",
    "songnumwidth",
    "songpos",
    "songrefs",
    "songs",
    "songsection",
    "songtarget",
    "songtitle",
    "stitlefont",
    "strophe",
    "t",
    "texorpdfstring",
    "textnote",
    "textquotedbl", // the converter's quotation mark that the source never closes
    "textsuperscript",
    "thesongnum",
    "theversenum",
    "titleprefixword",
    "transcaposfalse",
    "transcapostrue",
    "transpose",
    "transposehere",
    "trchordformat",
    "twosongcolumns",
    "u",
    "usechords",
    "v",
    "versefont",
    "versejustify",
    "versemark",
    "versenumstyle",
    "versenumwidth",
    "versesep",
    "vnumberedfalse",
    "vnumberedtrue",
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

namespace {

// Commands a hand-written book of the songbook repo (songbooks/**.tex) uses besides the song
// commands: the class's title pages and indexes, page layout, the keep-order block, and what a
// Tabor book's akordy.tex uses. Sorted. \input and \newindex open files by name and \begin
// starts any environment (luacode runs Lua): unknownBookConstructs checks their arguments.
constexpr std::array kBookKnown = std::to_array<std::string_view>({
    "author",          "begin",         "begingroup",    "beginkeeporder",
    "bindingleaf",     "campyear",      "centering",     "clearpage",
    "documentclass",   "end",           "endgroup",      "endkeeporder",
    "hfill",           "indexsongsas",  "input",         "insertallchords",
    "insertindex",     "large",         "makebackcover", "makefrontcover",
    "makeimprint",     "maketitlepage", "newindex",      "newpage",
    "padbookletpages", "padbookpages",  "pagebreak",     "pagestyle",
    "renewcommand",    "section",       "setcounter",    "showindex",
    "songsection",     "subsection",    "texttt",        "thepage",
    "title",           "transposesong", "url",
});

std::vector<std::string> scanConstructs(std::string_view content,
                                        const std::function<bool(std::string_view)>& known) {
    const auto text = latex::stripComments(content);
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
            // `\^^64irectlua` is `\directlua`: TeX decodes `^^xx` before it reads a name, so the
            // backslash must not take the first caret as a control symbol (`\^`).
            if (text[i + 1] == '^' && i + 2 < text.size() && text[i + 2] == '^') {
                report("\\^^");
                ++i; // the ^^ branch reports what the escape stands for
                continue;
            }
            if (!isLetter(text[i + 1])) {
                i += 2; // control symbol
                continue;
            }
            std::size_t end = i + 1;
            while (end < text.size() && isLetter(text[end]))
                ++end;
            const std::string_view name(text.data() + i + 1, end - i - 1);
            if (!known(name)) {
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
        } else if (c == '\r' && (i + 1 >= text.size() || text[i + 1] != '\n')) {
            // A bare CR ends a TeX line, so a comment stops there and code follows. Songs are
            // LF or CRLF files; report the rest, whatever the comment stripper made of it.
            report("^^M");
            ++i;
        } else {
            ++i;
        }
    }
    return found;
}

// The argument of the brace group starting at `open` (nullopt: none, or unbalanced).
std::optional<std::string_view> braceArgument(std::string_view text, std::size_t open) {
    while (open < text.size() && (text[open] == ' ' || text[open] == '\t'))
        ++open;
    if (open >= text.size() || text[open] != '{')
        return std::nullopt;
    const auto close = latex::findMatching(text, open);
    if (!close)
        return std::nullopt;
    return text.substr(open + 1, *close - open - 1);
}

// The argument of the bracket group starting at `open` (nullopt: none, or unbalanced).
std::optional<std::pair<std::string_view, std::size_t>> bracketArgument(std::string_view text,
                                                                        std::size_t open) {
    while (open < text.size() && (text[open] == ' ' || text[open] == '\t'))
        ++open;
    if (open >= text.size() || text[open] != '[')
        return std::nullopt;
    const auto close = text.find(']', open);
    if (close == std::string_view::npos)
        return std::nullopt;
    return std::pair{text.substr(open + 1, close - open - 1), close + 1};
}

// An image path under the build's folders: relative, plain characters, no "..".
bool plainFilePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.find("..") != std::string_view::npos)
        return false;
    return std::ranges::all_of(path, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '/' || c == '.';
    });
}

bool plainName(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '_' ||
               c == '-';
    });
}

// `key=<key>` (the package has no `capo` option: it is a LaTeX error); the key is letters, digits, `#`, spaces and the macros \shrp and \flt.
bool plainTransposeOption(std::string_view option) {
    while (!option.empty() && (option.front() == ' ' || option.front() == '\t'))
        option.remove_prefix(1);
    while (!option.empty() && (option.back() == ' ' || option.back() == '\t'))
        option.remove_suffix(1);
    if (!option.starts_with("key="))
        return false;
    option.remove_prefix(4);
    if (option.empty())
        return false;
    for (std::size_t i = 0; i < option.size();) {
        const char c = option[i];
        if (c == '\\') {
            const auto rest = option.substr(i);
            if (rest.starts_with("\\shrp"))
                i += 5;
            else if (rest.starts_with("\\flt"))
                i += 4;
            else
                return false;
            if (i < option.size() && isLetter(option[i]))
                return false;
        } else if (isLetter(c) || isDigit(c) || c == '#' || c == ' ') {
            ++i;
        } else {
            return false;
        }
    }
    return true;
}

// An integer -11..11, optionally signed.
bool transposeCount(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    if (!value.empty() && (value.front() == '-' || value.front() == '+'))
        value.remove_prefix(1);
    if (value.empty() || value.size() > 2 || !std::ranges::all_of(value, isDigit))
        return false;
    return std::stoi(std::string(value)) <= 11;
}

} // namespace

std::vector<std::string> unknownConstructs(std::string_view songContent) {
    return scanConstructs(songContent, [](std::string_view name) {
        return std::ranges::binary_search(kKnown, name);
    });
}

std::span<const std::string_view> knownBookCommands() {
    return kBookKnown;
}

std::vector<std::string> unknownBookConstructs(std::string_view bookContent) {
    auto found = scanConstructs(bookContent, [](std::string_view name) {
        return std::ranges::binary_search(kKnown, name) ||
               std::ranges::binary_search(kBookKnown, name);
    });
    const auto report = [&found](std::string construct) {
        if (std::ranges::find(found, construct) == found.end())
            found.push_back(std::move(construct));
    };
    // The file names: an \input is a song (Artist/Title), an index file a plain name.
    const std::string text = latex::stripComments(bookContent);
    for (std::size_t at = 0; (at = text.find('\\', at)) != std::string::npos; ++at) {
        std::size_t end = at + 1;
        while (end < text.size() && isLetter(text[end]))
            ++end;
        const std::string_view name(text.data() + at + 1, end - at - 1);
        if (name == "input") {
            const auto argument = braceArgument(text, end);
            if (!argument || invalidInputName(*argument))
                report(argument ? "\\input{" + std::string(*argument) + "}" : "\\input");
        } else if (name == "documentclass") {
            // One of the package's classes, found in the package (a class runs anything).
            std::size_t argAt = end;
            if (const auto options = bracketArgument(text, argAt))
                argAt = options->second;
            const auto argument = braceArgument(text, argAt);
            if (!argument || !songbook::bookClassFromName(*argument))
                report("\\documentclass" +
                       (argument ? "{" + std::string(*argument) + "}" : std::string()));
        } else if (name == "transposesong") {
            // \transposesong[key=<key>]{N}: a count of semitones and plain key text.
            std::size_t argAt = end;
            bool ok = true;
            if (const auto options = bracketArgument(text, argAt)) {
                argAt = options->second;
                std::string_view rest = options->first;
                while (ok) {
                    const auto comma = rest.find(',');
                    ok = plainTransposeOption(rest.substr(0, comma));
                    if (comma == std::string_view::npos)
                        break;
                    rest.remove_prefix(comma + 1);
                }
            }
            const auto count = braceArgument(text, argAt);
            if (!ok || !count || !transposeCount(*count)) {
                const auto line = text.find('\n', at);
                report(text.substr(at, std::min(line, at + 80) - at));
            }
        } else if (name == "makefrontcover" || name == "maketitlepage") {
            // An image file name: \includegraphics with -shell-escape may convert it by a
            // command line made from the name.
            std::size_t argAt = end;
            std::optional<std::string_view> file;
            if (const auto optional = bracketArgument(text, argAt)) {
                file = optional->first;
                argAt = optional->second;
            }
            if (const auto required = braceArgument(text, argAt);
                required && name == "makefrontcover" && !file)
                file = *required;
            if (file && !file->empty() && !plainFilePath(*file))
                report("\\" + std::string(name) + "{" + std::string(*file) + "}");
        } else if (name == "begin" || name == "end") {
            const auto argument = braceArgument(text, end);
            if (!argument ||
                (*argument != "document" && *argument != "songs" && *argument != "center")) {
                report("\\" + std::string(name) +
                       (argument ? "{" + std::string(*argument) + "}" : std::string()));
            } else if (name == "begin" && *argument == "songs") {
                // The index names become file names (build.sh -i, songidx.lua).
                const auto close =
                    static_cast<std::size_t>(argument->data() - text.data()) + argument->size();
                const auto indexes = braceArgument(text, close + 1);
                bool plain = indexes.has_value();
                if (indexes) {
                    std::string_view rest = *indexes;
                    while (plain && !rest.empty()) {
                        const auto comma = rest.find(',');
                        std::string_view one = rest.substr(0, comma);
                        while (!one.empty() && (one.front() == ' ' || one.front() == '\t'))
                            one.remove_prefix(1);
                        while (!one.empty() && (one.back() == ' ' || one.back() == '\t'))
                            one.remove_suffix(1);
                        plain = plainName(one) && one.front() != '-';
                        rest = comma == std::string_view::npos ? std::string_view()
                                                               : rest.substr(comma + 1);
                    }
                }
                if (!plain)
                    report("\\begin{songs}{" + std::string(indexes ? *indexes : "") + "}");
            }
        } else if (name == "newindex") {
            const auto id = braceArgument(text, end);
            const auto file =
                id ? braceArgument(text, static_cast<std::size_t>(id->data() - text.data()) +
                                             id->size() + 1)
                   : std::nullopt;
            if (!file || !plainName(*file) || file->front() == '-')
                report(file ? "\\newindex{..}{" + std::string(*file) + "}" : "\\newindex");
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

// Quoted Scheme data "#'(": read with Scheme's lexical rules up to the matching ")". '%' is an
// ordinary symbol there, ';' starts a comment, a string may hide parentheses. Returns the index
// after the closing parenthesis. Block comments and datum comments are reported, not followed.
std::size_t skipQuotedList(std::string_view text, std::size_t open,
                           const std::function<void(std::string)>& report) {
    int depth = 0;
    auto i = open;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '(') {
            ++depth;
            ++i;
        } else if (c == ')') {
            ++i;
            if (--depth == 0)
                return i;
        } else if (c == '"') {
            ++i;
            while (i < text.size() && text[i] != '"')
                i += text[i] == '\\' ? 2U : 1U;
            ++i;
        } else if (c == ';') {
            const auto eol = text.find('\n', i);
            i = eol == std::string_view::npos ? text.size() : eol + 1;
        } else if (c == '#' && i + 1 < text.size()) {
            const char n = text[i + 1];
            if (n == '|' || n == ';' || n == '!') {
                report(std::string("#") + n);
                i += 2;
            } else if (n == '\\') {
                i += 3; // a character such as #\( or #\"
            } else {
                ++i;
            }
        } else {
            ++i;
        }
    }
    return text.size();
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
                i += 1;
            } else if (next == '\'' && i + 2 < ly.size() && ly[i + 2] == '(') {
                i = skipQuotedList(ly, i + 2, report);
            } else if (next == '#') {
                i += 2; // "##t": the second '#' is not a sigil
            } else if (next == '\'' || next == '"' || next == '+' || next == '-' || next == '{' ||
                       next == '}' || next == '\\' || isDigit(next) ||
                       (c == '#' && isIdentStart(next))) {
                i += 1;
            } else if (c == '$' && isIdentStart(next)) {
                std::size_t end = i + 1;
                while (end < ly.size() && (isIdentStart(ly[end]) || isDigit(ly[end]) ||
                                           ly[end] == '-' || ly[end] == '_' || ly[end] == ':'))
                    ++end;
                report(std::string(ly.substr(i, end - i)));
                i = end;
            } else {
                // whitespace, '@', ';', '!', '|', '.', ',', '`', the end: Scheme the scanner
                // cannot follow, or a sigil that LilyPond reads differently from what it looks like
                report(std::string(1, c) + (next == '\0' || isSpace(next) ? ' ' : next));
                i += 1;
            }
        } else {
            ++i;
        }
    }
    return found;
}

} // namespace zub::safety
