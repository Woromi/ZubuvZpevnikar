#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <set>
#include <system_error>
#include <zub/core/json.hpp>
#include <zub/core/latex.hpp>
#include <zub/core/library.hpp>
#include <zub/core/safety.hpp>
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

// For the index data file: one line.
std::string oneLine(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        if (c == '\n' || c == '\r')
            c = ' ';
    return out;
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

namespace {

std::string trimScoreTarget(std::string_view target) {
    constexpr std::string_view blanks = " \t\r\n";
    const auto first = target.find_first_not_of(blanks);
    if (first == std::string_view::npos)
        return {};
    return std::string(target.substr(first, target.find_last_not_of(blanks) - first + 1));
}

} // namespace

std::optional<std::string> resolveScoreReference(std::string_view target) {
    std::string name = trimScoreTarget(target);
    // lyluatex adds ".ly" to a name without an extension
    const auto slash = name.rfind('/');
    const auto lastPart = slash == std::string::npos ? name : name.substr(slash + 1);
    if (!lastPart.empty() && lastPart.find('.') == std::string::npos)
        name += ".ly";
    if (invalidInputName(name) || !name.ends_with(".ly") || name.size() == 3 ||
        name[name.size() - 4] == '/')
        return std::nullopt;
    return name;
}

bool inTinwhistleTree(const std::filesystem::path& repoRoot,
                      const std::filesystem::path& songPath) {
    return collectionOf(repoRoot, songPath) == "tinwhistle";
}

std::filesystem::path findScoreFile(const std::filesystem::path& repoRoot,
                                    const std::filesystem::path& songPath, std::string_view name) {
    auto names = collections(repoRoot);
    if (const auto own = collectionOf(repoRoot, songPath)) {
        if (const auto it = std::ranges::find(names, *own); it != names.end())
            std::rotate(names.begin(), it, it + 1);
    }
    std::error_code ec;
    for (const std::string& collection : names) {
        std::filesystem::path candidate =
            repoRoot / "songs" / fromUtf8(collection) / fromUtf8(name);
        if (std::filesystem::is_regular_file(candidate, ec))
            return candidate;
    }
    return {};
}

std::vector<std::string> scoreRisks(const std::filesystem::path& repoRoot,
                                    const std::filesystem::path& songPath,
                                    std::string_view songText) {
    std::vector<std::string> risks;
    for (const std::string& reference : referencedFiles(songText)) {
        const auto name = resolveScoreReference(reference);
        if (!name) {
            risks.push_back("\\lilypondfile{" + reference + "}");
            continue;
        }
        const auto file = findScoreFile(repoRoot, songPath, *name);
        if (file.empty())
            continue;
        const auto text = readTextFile(file);
        if (!text) {
            risks.push_back(*name + ": cannot read the file");
            continue;
        }
        for (const std::string& risk : safety::lilypondRisks(*text))
            risks.push_back(*name + ": " + risk);
    }
    return risks;
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
        std::string target = trimScoreTarget(text.substr(i + 1, closeBrace - i - 1));
        if (!target.empty() && std::ranges::find(files, target) == files.end())
            files.push_back(std::move(target));
    }
    return files;
}

Resolution resolveSongs(const Songbook& book, const std::filesystem::path& repoRoot) {
    namespace fs = std::filesystem;
    Resolution result;
    std::set<std::string> seen;
    const auto collectionNames = collections(repoRoot);
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
            for (const std::string& collection : collectionNames) {
                const fs::path candidate =
                    repoRoot / "songs" / fromUtf8(collection) / (fromUtf8(name) += ".tex");
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

std::string_view paperName(BookClass bookClass) {
    return bookClass == BookClass::Velky ? "a4" : "a5";
}

std::vector<std::string>
notMarkedFitting(const Songbook& book,
                 const std::function<std::optional<std::string>(std::string_view)>& papOf) {
    const std::string paper(paperName(book.bookClass));
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const Section& section : book.sections) {
        for (const std::string& name : section.songs) {
            if (!seen.insert(name).second)
                continue;
            const auto pap = papOf(name);
            if (!pap)
                continue;
            bool marked = false;
            std::string_view rest = *pap;
            while (!rest.empty() && !marked) {
                const std::size_t comma = rest.find(',');
                std::string_view entry = rest.substr(0, comma);
                rest =
                    comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                while (!entry.empty() && (entry.front() == ' ' || entry.front() == '\t'))
                    entry.remove_prefix(1);
                while (!entry.empty() && (entry.back() == ' ' || entry.back() == '\t'))
                    entry.remove_suffix(1);
                std::string lower;
                for (const char c : entry)
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                marked = lower == paper || lower == paper + "x2";
            }
            if (!marked)
                out.push_back(name);
        }
    }
    return out;
}

bool removeSong(Songbook& book, std::string_view inputName) {
    bool removed = false;
    for (Section& section : book.sections)
        removed |= std::erase(section.songs, std::string(inputName)) > 0;
    return removed;
}

} // namespace zub::songbook
