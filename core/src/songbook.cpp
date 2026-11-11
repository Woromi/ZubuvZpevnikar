#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <set>
#include <system_error>
#include <zub/core/json.hpp>
#include <zub/core/latex.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/textfile.hpp>

namespace zub::songbook {

namespace {

std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return {u8.begin(), u8.end()};
}

std::filesystem::path fromUtf8(std::string_view s) {
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string oneLine(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c == '\n' || c == '\r')
            c = ' ';
    return out;
}

const char* boolText(bool b) {
    return b ? "true" : "false";
}

std::optional<bool> boolMember(const json::Value& object, std::string_view key, bool fallback,
                               std::string& error) {
    const auto* v = object.find(key);
    if (!v)
        return fallback;
    const auto b = v->asBool();
    if (!b)
        error = "\"" + std::string(key) + "\" must be true or false";
    return b;
}

std::optional<std::string> stringMember(const json::Value& object, std::string_view key,
                                        std::string& error) {
    const auto* v = object.find(key);
    if (!v)
        return std::string();
    const auto* s = v->asString();
    if (!s) {
        error = "\"" + std::string(key) + "\" must be a string";
        return std::nullopt;
    }
    return *s;
}

} // namespace

std::string_view className(BookClass bookClass) {
    switch (bookClass) {
    case BookClass::Velky:
        return "velkyzpevnik";
    case BookClass::Maly:
        return "malyzpevnik";
    case BookClass::Tabor:
        return "taborovyzpevnik";
    }
    return "velkyzpevnik";
}

std::optional<BookClass> bookClassFromName(std::string_view name) {
    for (const auto c : {BookClass::Velky, BookClass::Maly, BookClass::Tabor})
        if (className(c) == name)
            return c;
    return std::nullopt;
}

std::string toJson(const Songbook& book) {
    std::string out = "{\n  \"version\": 1,\n";
    out += "  \"title\": " + json::quote(book.title) + ",\n";
    out += "  \"author\": " + json::quote(book.author) + ",\n";
    out += "  \"class\": " + json::quote(className(book.bookClass)) + ",\n";
    out += std::string("  \"chorded\": ") + boolText(book.chorded) + ",\n";
    out += std::string("  \"twoColumns\": ") + boolText(book.twoColumns) + ",\n";
    out += std::string("  \"shading\": ") + boolText(book.shading) + ",\n";
    out += std::string("  \"chordChart\": ") + boolText(book.chordChart) + ",\n";
    out += std::string("  \"printReady\": ") + boolText(book.printReady) + ",\n";
    out += "  \"campYear\": " + json::quote(book.campYear) + ",\n";
    out += "  \"coverImage\": " + json::quote(book.coverImage) + ",\n";
    out += std::string("  \"authorIndex\": ") + boolText(book.authorIndex) + ",\n";
    if (book.sections.empty()) {
        out += "  \"sections\": []\n";
    } else {
        out += "  \"sections\": [\n";
        for (std::size_t i = 0; i < book.sections.size(); ++i) {
            const Section& section = book.sections[i];
            out += "    {\n      \"name\": " + json::quote(section.name) + ",\n";
            if (section.songs.empty()) {
                out += "      \"songs\": []\n";
            } else {
                out += "      \"songs\": [\n";
                for (std::size_t j = 0; j < section.songs.size(); ++j)
                    out += "        " + json::quote(section.songs[j]) +
                           (j + 1 < section.songs.size() ? ",\n" : "\n");
                out += "      ]\n";
            }
            out += i + 1 < book.sections.size() ? "    },\n" : "    }\n";
        }
        out += "  ]\n";
    }
    out += "}\n";
    return out;
}

std::expected<Songbook, std::string> fromJson(std::string_view text) {
    const auto parsed = json::parse(text);
    if (!parsed)
        return std::unexpected(parsed.error());
    const json::Value& root = *parsed;
    if (!root.asObject())
        return std::unexpected("the songbook must be a JSON object");
    const auto* version = root.find("version");
    const auto versionNumber = version ? version->asInt() : std::nullopt;
    if (!versionNumber)
        return std::unexpected("missing or invalid \"version\"");
    if (*versionNumber != 1)
        return std::unexpected("unsupported songbook version " + std::to_string(*versionNumber));

    Songbook book;
    std::string error;
    const auto title = stringMember(root, "title", error);
    const auto author = error.empty() ? stringMember(root, "author", error) : std::nullopt;
    const auto campYear = error.empty() ? stringMember(root, "campYear", error) : std::nullopt;
    const auto coverImage = error.empty() ? stringMember(root, "coverImage", error) : std::nullopt;
    if (!error.empty())
        return std::unexpected(error);
    book.title = *title;
    book.author = *author;
    book.campYear = *campYear;
    book.coverImage = *coverImage;

    if (const auto* c = root.find("class")) {
        const auto* name = c->asString();
        if (!name)
            return std::unexpected("\"class\" must be a string");
        const auto bookClass = bookClassFromName(*name);
        if (!bookClass)
            return std::unexpected("unknown class \"" + *name + "\"");
        book.bookClass = *bookClass;
    }

    struct Flag {
        const char* key;
        bool* target;
    };
    for (const Flag flag : {Flag{"chorded", &book.chorded}, Flag{"twoColumns", &book.twoColumns},
                            Flag{"shading", &book.shading}, Flag{"chordChart", &book.chordChart},
                            Flag{"printReady", &book.printReady},
                            Flag{"authorIndex", &book.authorIndex}}) {
        const auto value = boolMember(root, flag.key, *flag.target, error);
        if (!value)
            return std::unexpected(error);
        *flag.target = *value;
    }

    if (const auto* sections = root.find("sections")) {
        const auto* array = sections->asArray();
        if (!array)
            return std::unexpected("\"sections\" must be an array");
        for (const auto& item : *array) {
            if (!item.asObject())
                return std::unexpected("every section must be an object");
            Section section;
            const auto name = stringMember(item, "name", error);
            if (!name)
                return std::unexpected("section " + error);
            section.name = *name;
            if (const auto* songs = item.find("songs")) {
                const auto* songArray = songs->asArray();
                if (!songArray)
                    return std::unexpected("\"songs\" must be an array");
                for (const auto& song : *songArray) {
                    const auto* s = song.asString();
                    if (!s)
                        return std::unexpected("every song must be a string");
                    section.songs.push_back(*s);
                }
            }
            book.sections.push_back(std::move(section));
        }
    }
    normalize(book);
    return book;
}

void normalize(Songbook& book) {
    Section root;
    std::vector<Section> named;
    for (Section& section : book.sections) {
        if (!section.name.empty()) {
            named.push_back(std::move(section));
            continue;
        }
        for (auto& song : section.songs)
            if (std::ranges::find(root.songs, song) == root.songs.end())
                root.songs.push_back(std::move(song));
    }
    book.sections.clear();
    book.sections.push_back(std::move(root));
    for (Section& section : named)
        book.sections.push_back(std::move(section));
}

std::vector<std::string> mergeIntoOrder(const std::vector<std::string>& current,
                                        const std::vector<std::string>& sortedAll,
                                        const std::vector<std::string>& added) {
    std::vector<std::string> result = current;
    std::set<std::string> pending;
    for (const auto& name : added)
        if (std::ranges::find(current, name) == current.end())
            pending.insert(name);
    std::set<std::string> placed(current.begin(), current.end());
    for (std::size_t i = 0; i < sortedAll.size(); ++i) {
        const std::string& name = sortedAll[i];
        if (!pending.erase(name))
            continue;
        auto position = result.begin();
        for (std::size_t j = i; j > 0; --j) {
            if (!placed.contains(sortedAll[j - 1]))
                continue;
            position = std::ranges::find(result, sortedAll[j - 1]) + 1;
            break;
        }
        result.insert(position, name);
        placed.insert(name);
    }
    // Names the index order does not list go to the end.
    for (const auto& name : added)
        if (pending.erase(name))
            result.push_back(name);
    return result;
}

std::string escapeText(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '\\':
            out += "\\textbackslash{}";
            break;
        case '{':
            out += "\\{";
            break;
        case '}':
            out += "\\}";
            break;
        case '&':
            out += "\\&";
            break;
        case '%':
            out += "\\%";
            break;
        case '$':
            out += "\\$";
            break;
        case '#':
            out += "\\#";
            break;
        case '_':
            out += "\\_";
            break;
        case '~':
            out += "\\textasciitilde{}";
            break;
        case '^':
            out += "\\textasciicircum{}";
            break;
        case '\n':
        case '\r':
            out += ' ';
            break;
        default:
            out += c;
        }
    }
    return out;
}

namespace {

// The first character of a title (one UTF-8 code point, ASCII letters upper-cased): what the title
// index groups by.
std::string initialOf(std::string_view title) {
    std::size_t length = 1;
    if (!title.empty()) {
        const auto lead = static_cast<unsigned char>(title[0]);
        length = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
    }
    std::string initial(title.substr(0, length));
    for (char& c : initial)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return initial;
}

struct GenSection {
    std::string name;
    std::vector<std::string> songs;
    std::string index; // the title index it belongs to
};

std::string lowerAscii(std::string text) {
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

constexpr std::string_view sectionPageMacro = R"tex(\makeatletter
\newcommand{\zubsectionpage}[2]{%
  \clearpage
  @ODD@\thispagestyle{empty}\null\vfill
  \songsection[#1]{\fontsize{30}{36}\selectfont #2}%
  \vfill\null\clearpage}
\makeatother
)tex";

constexpr std::string_view velkyCoverMacro = R"tex(\makeatletter
\newcommand{\zubcover}{%
  \pagenumbering{Alph}\pagestyle{empty}
  \begin{titlepage}\centering\null\vfill
    {\fontsize{62}{65}\bfseries \@title\par}\vfill
    {\Large \@author\par}{\Large \today\par}\vfill
  \end{titlepage}}
\makeatother
)tex";

constexpr std::string_view taborPatchHead = R"tex(\makeatletter
\newcommand{\zubtitleindexes}{%
  \begingroup\sepindexesfalse
)tex";

constexpr std::string_view taborPatchTail = R"tex(
  \endgroup}
% the class hard-codes its one-page title index inside \makefrontcover
\expandafter\xpatchcmd\csname\string\makefrontcover\endcsname
  {\large \showindex[1]{Obsah}{obsah}}{\large\zubtitleindexes}
  {}{\PackageError{zubuvzpevnik}{cannot patch makefrontcover}{}}
\makeatother
)tex";

} // namespace

Generated generate(const Songbook& book, const std::function<bool(std::string_view inputName)>& exists,
                   const std::function<std::string(std::string_view inputName)>& titleOf) {
    namespace fs = std::filesystem;
    Generated result;
    const bool tabor = book.bookClass == BookClass::Tabor;

    std::string options = book.chorded ? "chorded" : "lyric";
    options += book.twoColumns ? ", twosongcolumns" : ", onesongcolumn";
    if (!book.shading)
        options += ", noshading";

    // The songs that are built, split into the root part and the named sections.
    std::vector<std::string> rootSongs;
    std::vector<GenSection> named;
    for (const Section& section : book.sections) {
        std::vector<std::string> kept;
        for (const auto& song : section.songs) {
            if (!invalidInputName(song) && exists(song))
                kept.push_back(song);
            else
                result.skipped.push_back(song);
        }
        if (kept.empty())
            continue;
        if (section.name.empty()) {
            rootSongs.insert(rootSongs.end(), kept.begin(), kept.end());
        } else {
            named.push_back({section.name, std::move(kept), {}});
        }
    }
    const bool emptyBook = rootSongs.empty() && named.empty();
    const bool hasRoot = !rootSongs.empty() || emptyBook; // an empty book still has one songs block

    // Tabor's title index is one booklet page: estimate whether the split one still fits.
    bool split = true;
    if (tabor && !named.empty()) {
        const auto cost = [&](const std::vector<std::string>& songs, double heading) {
            std::set<std::string> initials;
            if (titleOf)
                for (const auto& song : songs)
                    initials.insert(initialOf(titleOf(song)));
            const double letters = titleOf ? static_cast<double>(initials.size())
                                           : static_cast<double>(songs.size());
            return static_cast<double>(songs.size()) + 1.5 * letters + heading;
        };
        double lines = rootSongs.empty() ? 0.0 : cost(rootSongs, 0.0);
        for (const auto& section : named)
            lines += cost(section.songs, 3.0);
        if (lines > 28.0) {
            split = false;
            result.warnings.emplace_back("The index is not split into sections: it would not fit "
                                         "the booklet's single index page.");
        }
    }
    // Tabor without a split has the one flat index.
    const bool sectionIndexes = !named.empty() && split;
    if (sectionIndexes) {
        int n = 0;
        for (auto& section : named)
            section.index = "idxs" + std::to_string(++n);
    } else {
        for (auto& section : named)
            section.index = "obsah";
    }
    const bool authorIndex = book.authorIndex && !tabor;
    const bool rootIndex = hasRoot || (!sectionIndexes && !named.empty());
    if (rootIndex)
        result.indexes.emplace_back("obsah");
    if (sectionIndexes)
        for (const auto& section : named)
            result.indexes.push_back(section.index);
    if (authorIndex)
        result.indexes.emplace_back("obsahAutor");

    // The cover.
    if (!book.coverImage.empty()) {
        const fs::path source = fromUtf8(book.coverImage);
        std::string extension = lowerAscii(utf8(source.extension()));
        std::error_code ec;
        if (!fs::is_regular_file(source, ec)) {
            result.warnings.push_back("The cover image " + book.coverImage + " does not exist.");
        } else if (extension != ".png" && extension != ".jpg" && extension != ".jpeg" &&
                   extension != ".pdf") {
            result.warnings.push_back("The cover image " + book.coverImage +
                                      " is not a png, jpg or pdf file.");
        } else {
            result.coverSource = source;
            result.coverName = "cover" + extension;
        }
    }
    const std::string& cover = result.coverName;
    const bool coverPdf = cover.ends_with(".pdf");

    const std::string authorSuffix = authorIndex ? ",obsahAutor" : "";
    const auto songsBlock = [&](const std::string& index, const std::vector<std::string>& songs) {
        std::string text = "\\begin{songs}{" + index + authorSuffix + "}\n";
        for (const auto& song : songs)
            text += "    \\input{" + song + "}\n";
        return text + "\\end{songs}\n\n";
    };
    std::string body;
    if (hasRoot)
        body += songsBlock("obsah", rootSongs);
    for (const auto& section : named) {
        const std::string name = escapeText(section.name);
        body += "\\zubsectionpage{" + name + "}{" + name + "}\n" +
                songsBlock(section.index, section.songs);
    }

    // The title index (and the author index) of Velky and Maly.
    const auto indexBlock = [&](std::string_view gap) {
        std::string text = "  \\begingroup\\sepindexesfalse\n";
        bool first = true;
        if (rootIndex) {
            text += "  {\\large \\showindex[2]{}{obsah}}\n";
            first = false;
        }
        if (sectionIndexes)
            for (const auto& section : named) {
                if (!first)
                    text += "  \\vskip " + std::string(gap) + "\\baselineskip\n";
                first = false;
                text += "  {\\large \\showindex[2]{" + escapeText(section.name) + "}{" +
                        section.index + "}}\n";
            }
        text += "  \\endgroup\n";
        if (authorIndex)
            text += "  \\clearpage\n  {\\large \\showindex[2]{Obsah podle interpreta}{obsahAutor}}\n";
        return text;
    };

    std::string& tex = result.tex;
    tex = "% Generated by Zpěvníkář (ZubuvZpevnikar) from the songbook \"" + oneLine(book.title) +
          "\"; edit the songbook in the app.\n"
          "% Build inside a ZubuvZpevnik checkout: see the app's export notes.\n";
    tex += "\\documentclass[" + options + "]{" + std::string(className(book.bookClass)) + "}\n\n";
    tex += "\\title{" + escapeText(book.title) + "}\n";
    tex += "\\author{" + escapeText(book.author) + "}\n";
    if (tabor && !book.campYear.empty())
        tex += "\\renewcommand{\\campyear}{" + escapeText(book.campYear) + "}\n";
    if (sectionIndexes)
        for (const auto& section : named)
            tex += "\\newindex{" + section.index + "}{" + section.index + "}\n\\indexsongsas{" +
                   section.index + "}{\\thepage}\n";
    if (!named.empty()) {
        std::string macro(sectionPageMacro);
        const std::string odd = book.bookClass == BookClass::Maly
                                    ? "\\ifodd\\c@page\\else\\thispagestyle{empty}\\null\\newpage\\fi\n  "
                                    : "";
        macro.replace(macro.find("@ODD@"), 5, odd);
        tex += macro;
    }
    if (book.bookClass == BookClass::Velky && cover.empty())
        tex += velkyCoverMacro;
    if (tabor) {
        if (sectionIndexes) {
            tex += taborPatchHead;
            bool first = true;
            if (rootIndex) {
                tex += "  \\showindex[1]{}{obsah}";
                first = false;
            }
            for (const auto& section : named) {
                tex += std::string(first ? "" : "\n") + "  \\vskip 1\\baselineskip\n  \\showindex[1]{" +
                       escapeText(section.name) + "}{" + section.index + "}";
                first = false;
            }
            tex += taborPatchTail;
        }
        if (!coverPdf)
            tex += "\\newcommand{\\zubCoverInc}[2][]{" +
                   (cover.empty() ? std::string() : "\\zubOrigInc[width=350pt]{" + cover + "}") +
                   "}\n";
    }
    tex += "\n\\begin{document}\n\n";

    switch (book.bookClass) {
    case BookClass::Velky:
        tex += cover.empty() ? "\\zubcover\n" : "\\makefrontcover{" + cover + "}\n";
        tex += "\\clearpage\\pagestyle{empty}\\pagenumbering{roman}\n{\n" + indexBlock("2") +
               "}\n\\clearpage\\pagenumbering{arabic}\\pagestyle{fancy}\n\n";
        tex += body;
        if (book.chordChart)
            tex += "\\insertallchords\n\n";
        break;
    case BookClass::Maly:
        if (book.printReady)
            tex += "\\bindingleaf\n";
        tex += cover.empty() ? "\\maketitlepage\n\n" : "\\maketitlepage[" + cover + "]\n\n";
        tex += body;
        if (book.chordChart)
            tex += "\\insertallchords\n";
        tex += "\\clearpage\n\\begingroup\\addtolength{\\hsize}{-8pt}%\n" + indexBlock("1") +
               "\\endgroup\n";
        if (book.printReady)
            tex += "\\bindingleaf[\\makeimprint]\n\\padbookpages\n";
        tex += "\n";
        break;
    case BookClass::Tabor:
        if (coverPdf)
            tex += "\\makefrontcover[" + cover + "]\n\n";
        else
            tex += "\\let\\zubOrigInc\\includegraphics\\let\\includegraphics\\zubCoverInc\n"
                   "\\makefrontcover\n\\let\\includegraphics\\zubOrigInc\n\n";
        tex += body;
        tex += "\\padbookletpages\n\\makebackcover\n\n";
        result.akordy = "% Generated by Zpěvníkář (ZubuvZpevnikar): the chord chart \\makebackcover inputs.\n"
                        "\\begingroup\\centering\\insertallchords\\endgroup\n";
        break;
    }
    tex += "\\end{document}\n";
    return result;
}

std::string indexOrderInput(std::span<const SortEntry> entries) {
    std::string out = "TITLE INDEX DATA FILE\n";
    std::size_t position = 0;
    for (const SortEntry& entry : entries) {
        ++position;
        const std::string p = std::to_string(position);
        out +=
            "%lang " + (entry.lang.empty() ? std::string("english") : oneLine(entry.lang)) + "\n";
        // songidx strips leading spaces, so a space keeps a leading '%' from being a comment.
        const std::string title = oneLine(entry.title);
        out += (title.starts_with('%') ? " " : "") + title + "\n" + p + "\nsong1-" + p + ".1\n";
    }
    return out;
}

std::expected<std::vector<int>, std::string> parseIndexOrder(std::string_view sbx,
                                                             std::size_t count) {
    constexpr std::string_view marker = "\\songlink{";
    std::vector<int> order;
    std::size_t pos = 0;
    while ((pos = sbx.find(marker, pos)) != std::string_view::npos) {
        pos += marker.size();
        const auto close = sbx.find("}{", pos);
        if (close == std::string_view::npos)
            return std::unexpected("malformed \\songlink in the index");
        const char* first = sbx.data() + close + 2;
        const char* last = sbx.data() + sbx.size();
        int number = 0;
        const auto [ptr, ec] = std::from_chars(first, last, number);
        if (ec != std::errc{} || ptr == last || *ptr != '}')
            return std::unexpected("malformed \\songlink number in the index");
        order.push_back(number);
        pos = static_cast<std::size_t>(ptr - sbx.data());
    }
    if (order.size() != count)
        return std::unexpected("the index lists " + std::to_string(order.size()) +
                               " songs, expected " + std::to_string(count));
    std::vector<bool> seen(count + 1, false);
    for (const int n : order) {
        if (n < 1 || static_cast<std::size_t>(n) > count)
            return std::unexpected("the index has an unexpected number " + std::to_string(n));
        if (seen[static_cast<std::size_t>(n)])
            return std::unexpected("the index lists song " + std::to_string(n) + " twice");
        seen[static_cast<std::size_t>(n)] = true;
    }
    return order;
}

converter::Command sortCommand(const std::filesystem::path& texlua,
                               const std::filesystem::path& songidxLua, std::string_view locale,
                               const std::filesystem::path& sxd, const std::filesystem::path& sbx) {
    converter::Command command{utf8(texlua), {utf8(songidxLua)}};
    if (!locale.empty()) {
        command.arguments.emplace_back("-l");
        command.arguments.emplace_back(locale);
    }
    command.arguments.push_back(utf8(sxd));
    command.arguments.push_back(utf8(sbx));
    return command;
}

std::vector<std::string> referencedFiles(std::string_view songText) {
    const std::string text = latex::stripComments(songText);
    constexpr std::string_view name = "lilypondfile";
    std::vector<std::string> files;
    std::size_t pos = 0;
    while ((pos = text.find(name, pos)) != std::string::npos) {
        const std::size_t start = pos;
        pos += name.size();
        const bool plain = start >= 1 && text[start - 1] == '\\';
        const bool narrow = start >= 7 && text.compare(start - 7, 7, "\\narrow") == 0;
        if (!plain && !narrow)
            continue;
        std::size_t i = pos;
        if (i < text.size() && std::isalpha(static_cast<unsigned char>(text[i])))
            continue;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i < text.size() && text[i] == '[') {
            const auto closeBracket = text.find(']', i);
            if (closeBracket == std::string::npos)
                continue;
            i = closeBracket + 1;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
        }
        if (i >= text.size() || text[i] != '{')
            continue;
        const auto closeBrace = text.find('}', i);
        if (closeBrace == std::string::npos)
            continue;
        std::string target = text.substr(i + 1, closeBrace - i - 1);
        if (!target.empty() && std::ranges::find(files, target) == files.end())
            files.push_back(std::move(target));
    }
    return files;
}

Resolution resolveSongs(const Songbook& book, const std::filesystem::path& repoRoot) {
    namespace fs = std::filesystem;
    Resolution result;
    std::set<std::string> seen;
    for (const Section& section : book.sections) {
        for (const auto& name : section.songs) {
            if (!seen.insert(name).second)
                continue;
            if (invalidInputName(name)) {
                result.invalid.push_back(name);
                continue;
            }
            ResolvedSong song{name, {}};
            std::error_code ec;
            for (const char* dir : {"songs/chords", "songs/tinwhistle"}) {
                const fs::path candidate = repoRoot / dir / (fromUtf8(name) += ".tex");
                if (fs::is_regular_file(candidate, ec)) {
                    song.path = candidate;
                    break;
                }
            }
            result.songs.push_back(std::move(song));
        }
    }
    return result;
}

namespace {

bool validBookName(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '-';
    });
}

std::string buildScript(const std::vector<std::string>& indexes, std::string_view bookName) {
    std::string script =
        "#!/bin/bash\n"
        "# Builds " +
        std::string(bookName) +
        ".pdf into out/ (needs lualatex, lilypond, texlua: see README.md).\n"
        "set -euo pipefail\n"
        "HERE=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
        "export TEXINPUTS=\".:$HERE:$HERE/zubuvzpevnik:$HERE/songs:${TEXINPUTS:-}\"\n"
        "mkdir -p \"$HERE/out\"\n"
        "cd \"$HERE/out\"\n"
        "\"$HERE/scripts/build.sh\" ";
    for (const auto& index : indexes)
        script += "-i " + index + " ";
    script += "\"$HERE/" + std::string(bookName) + ".tex\"\n";
    return script;
}

std::string readme(const Songbook& book, std::string_view bookName) {
    return "# " + (book.title.empty() ? std::string(bookName) : oneLine(book.title)) +
           "\n\n"
           "A songbook exported by Zpěvníkář (ZubuvZpevnikar): the songs it uses, the ZubuvZpevnik LaTeX\n"
           "package and the scripts that build it.\n\n"
           "## Build\n\n"
           "    ./build.sh\n\n"
           "The PDF is written to `out/" +
           std::string(bookName) +
           ".pdf`.\n\n"
           "## Requirements\n\n"
           "- TeX Live with LuaLaTeX and the packages listed in the ZubuvZpevnik\n"
           "  `docs/building.md` (songs, lyluatex, babel, fontspec, ...)\n"
           "- LilyPond (chord diagrams and notation are built by lyluatex)\n"
           "- the fonts Open Sans, Roboto Mono and FreeSans\n"
           "- a Czech locale (`cs_CZ.UTF-8`) for the order of the song index\n";
}

} // namespace

std::expected<FolderExport, std::string> planFolderExport(const Songbook& book,
                                                          const std::filesystem::path& repoRoot,
                                                          std::string_view bookName,
                                                          const std::function<std::string(std::string_view)>& titleOf) {
    namespace fs = std::filesystem;
    if (!validBookName(bookName))
        return std::unexpected("the book file name must be non-empty and use only letters, "
                               "digits, '_' and '-'");

    FolderExport plan;
    std::set<fs::path> seenTargets;
    const auto addCopy = [&](const fs::path& from, const fs::path& to) {
        if (seenTargets.insert(to).second)
            plan.copies.push_back({from, to});
    };

    const Resolution resolution = resolveSongs(book, repoRoot);
    plan.invalid = resolution.invalid;
    std::set<std::string> found;
    for (const ResolvedSong& song : resolution.songs) {
        if (song.path.empty()) {
            plan.skipped.push_back(song.inputName);
            continue;
        }
        found.insert(song.inputName);
        addCopy(song.path, fs::path("songs") / (fromUtf8(song.inputName) += ".tex"));
        const auto content = readTextFile(song.path);
        if (!content)
            continue;
        std::error_code ec;
        for (const auto& ref : referencedFiles(*content)) {
            if (invalidInputName(ref))
                continue;
            const fs::path source = song.path.parent_path() / fromUtf8(ref);
            if (fs::is_regular_file(source, ec))
                addCopy(source, fs::path("songs") / fromUtf8(ref));
        }
    }

    std::vector<fs::path> classFiles;
    std::error_code ec;
    const fs::path classDir = repoRoot / "zubuvzpevnik";
    if (fs::is_directory(classDir, ec)) {
        for (fs::recursive_directory_iterator it(classDir, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code fileError;
            if (it->is_regular_file(fileError))
                classFiles.push_back(it->path());
        }
    }
    std::ranges::sort(classFiles);
    for (const auto& file : classFiles)
        addCopy(file, fs::path("zubuvzpevnik") / file.lexically_relative(classDir));

    for (const char* script :
         {"scripts/build.sh", "scripts/buildindex.sh", "scripts/songidx.lua", "scripts/cleanup.sh"})
        addCopy(repoRoot / script, script);
    if (book.bookClass == BookClass::Velky)
        addCopy(repoRoot / "img/zubuvzpevnik.png", "img/zubuvzpevnik.png");
    else if (book.bookClass == BookClass::Tabor)
        addCopy(repoRoot / "img/kane.jpg", "img/kane.jpg");

    const auto generated =
        generate(book, [&](std::string_view name) { return found.contains(std::string(name)); },
                 titleOf);
    if (generated.coverSource)
        addCopy(*generated.coverSource, generated.coverName);
    plan.warnings = generated.warnings;
    const std::string stem(bookName);
    plan.written.emplace_back(stem + ".tex", generated.tex);
    if (generated.akordy)
        plan.written.emplace_back("akordy.tex", *generated.akordy);
    plan.written.emplace_back("build.sh", buildScript(generated.indexes, bookName));
    plan.written.emplace_back("README.md", readme(book, bookName));
    return plan;
}

BuildCommand buildCommand(const Songbook&, const std::filesystem::path& repoRoot,
                          const std::filesystem::path& workDir, std::string_view bookName,
                          const std::vector<std::string>& indexes) {
    BuildCommand command;
    const std::string root = utf8(repoRoot);
    command.program = "bash";
    command.arguments = {utf8(repoRoot / "scripts/build.sh")};
    for (const auto& index : indexes) {
        command.arguments.emplace_back("-i");
        command.arguments.push_back(index);
    }
    command.arguments.push_back(utf8(workDir / (std::string(bookName) + ".tex")));
    command.workingDirectory = workDir;
    command.environment.emplace_back(
        "TEXINPUTS", ".:" + utf8(workDir) + ":" + root + "/songs/chords:" + root +
                         "/songs/tinwhistle:" + root + ":" + root + "/zubuvzpevnik:");
    command.pdf = workDir / (std::string(bookName) + ".pdf");
    command.log = workDir / (std::string(bookName) + ".log");
    return command;
}

int paperWarnings(std::string_view log) {
    int count = 0;
    std::string block; // the lines of the current warning, joined without separators
    bool inBlock = false;
    const auto flush = [&] {
        if (inBlock && block.contains("not marked as fitting"))
            ++count;
        block.clear();
        inBlock = false;
    };
    std::size_t pos = 0;
    while (pos <= log.size()) {
        auto end = log.find('\n', pos);
        if (end == std::string_view::npos)
            end = log.size();
        std::string_view line = log.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        if (line.contains("Package zubuvzpevnik Warning")) {
            flush();
            inBlock = true;
            block = line;
        } else if (inBlock) {
            if (line.empty() || line.contains("Package ") || line.contains("LaTeX Warning") ||
                line.starts_with('('))
                flush();
            else
                block += line;
        }
    }
    flush();
    return count;
}

} // namespace zub::songbook
