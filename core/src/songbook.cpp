#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <map>
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

// For labels people read: '/' between folders on every system.
std::string utf8Generic(const std::filesystem::path& p) {
    const auto u8 = p.generic_u8string();
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

namespace {

// One \lilypondfile or \narrowlilypondfile of a song (comments stripped).
struct ScoreCall {
    std::optional<std::string> target; // the braced argument, trimmed; nullopt when unreadable
    bool bracketed = false;            // "[...]" before it: the package takes no options
    bool oddBlanks = false;            // blanks around the name, a run of them, tabs or breaks
    bool unreadable() const { return !target || bracketed || oddBlanks; }
    std::string text;                  // the call as written, for a risk message
};

std::vector<ScoreCall> scoreCalls(const std::string& text) {
    constexpr std::string_view name = "lilypondfile";
    std::vector<ScoreCall> calls;
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
        const std::size_t callStart = narrow ? start - 7 : start - 1;
        ScoreCall call;
        const auto skipSpaces = [&] {
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
                ++i;
        };
        skipSpaces();
        if (i < text.size() && text[i] == '[') {
            call.bracketed = true;
            const auto closeBracket = text.find(']', i);
            if (closeBracket != std::string::npos) {
                i = closeBracket + 1;
                skipSpaces();
            }
        }
        std::size_t end = std::min(text.size(), i + 1);
        if (i < text.size() && text[i] == '{') {
            if (const auto closeBrace = text.find('}', i); closeBrace != std::string::npos) {
                const std::string raw = text.substr(i + 1, closeBrace - i - 1);
                call.target = trimScoreTarget(raw);
                // TeX keeps a leading blank and squeezes a run of them, and lyluatex trims
                // nothing: such a name is not the file the scan would read.
                call.oddBlanks = raw != *call.target || raw.find("  ") != std::string::npos ||
                                 raw.find_first_of("\t\r\n") != std::string::npos;
                end = closeBrace + 1;
            }
        }
        std::size_t length = std::min<std::size_t>(end - callStart, 60);
        while (length > 0 && callStart + length < text.size() &&
               (static_cast<unsigned char>(text[callStart + length]) & 0xC0) == 0x80)
            --length; // not in the middle of a UTF-8 character
        call.text = text.substr(callStart, length);
        std::ranges::replace_if(call.text, [](char c) { return c == '\n' || c == '\r'; }, ' ');
        calls.push_back(std::move(call));
    }
    return calls;
}

std::string lowerAscii(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace

std::vector<std::string> includeShadows(const std::filesystem::path& scoreDir) {
    std::vector<std::string> found;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(scoreDir, ec), end; !ec && it != end;
         it.increment(ec)) {
        // Case-insensitive: so are the file systems of Windows and macOS.
        const std::string fileName = utf8(it->path().filename());
        if (safety::isTrustedInclude(lowerAscii(fileName)))
            found.push_back(fileName + " beside the score: it stands in for the package's");
    }
    std::ranges::sort(found);
    return found;
}

namespace {

// Extensions of files TeX, LuaTeX or LilyPond load by a bare name a package probes for
// (hyperref.cfg, a missing .fd, a Lua module, a score's include), lower case.
bool loadableByName(const std::string& lowerName, bool texToo) {
    constexpr std::array extensions = std::to_array<std::string_view>(
        {".cfg", ".sty", ".cls", ".def", ".fd", ".clo", ".ldf", ".lua", ".luc", ".ltx", ".dtx",
         ".ins", ".bbx", ".cbx", ".lbx", ".sbx", ".sxd", ".ly", ".ily"});
    if (lowerName == "akordy")
        return true; // \input{akordy} tries "akordy" after "akordy.tex"
    if (texToo && lowerName.ends_with(".tex"))
        return true;
    return std::ranges::any_of(extensions,
                               [&](std::string_view e) { return lowerName.ends_with(e); });
}

std::string relativeLabel(const std::filesystem::path& file, const std::filesystem::path& root) {
    const auto relative = file.lexically_relative(root);
    return utf8Generic(relative.empty() ? file : relative);
}

} // namespace

std::vector<std::string> texinputsShadows(const std::filesystem::path& repoRoot,
                                          const std::filesystem::path& bookDir) {
    namespace fs = std::filesystem;
    std::vector<std::string> found;
    const auto report = [&](const fs::path& file, std::string_view why) {
        found.push_back(relativeLabel(file, repoRoot) + ": " + std::string(why));
    };
    std::error_code ec;
    // A collection's top folder holds only artist folders: any file there is found by a bare
    // name before the book's folder and the repo root (\input{akordy}, hyperref.cfg, ...).
    for (const std::string& collection : collections(repoRoot)) {
        const fs::path dir = repoRoot / "songs" / fromUtf8(collection);
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code statusError;
            if (!it->is_directory(statusError) || it->is_symlink(statusError))
                report(it->path(), "a file in a song folder that a build could load by its name");
        }
    }
    // The repo root and the book's folder have other files too (README.md, the book, its
    // akordy.tex and cover, which are checked by themselves): only what TeX could load by name.
    const auto checkTop = [&](const fs::path& dir, bool texToo) {
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code statusError;
            if (it->is_directory(statusError) && !it->is_symlink(statusError))
                continue;
            if (loadableByName(lowerAscii(utf8(it->path().filename())), texToo))
                report(it->path(), "a file a build could load by its name");
        }
    };
    checkTop(repoRoot, true);
    if (!bookDir.empty()) {
        checkTop(bookDir, false); // books and akordy.tex: checked as books
        // A score under the book's folder stands in for a missing library score.
        for (fs::recursive_directory_iterator it(bookDir, ec), end; !ec && it != end;
             it.increment(ec)) {
            const std::string name = lowerAscii(utf8(it->path().filename()));
            if (it.depth() > 0 && (name.ends_with(".ly") || name.ends_with(".ily") ||
                                   name.ends_with(".ly.tex")))
                report(it->path(), "a score in the book's folder that a build could load");
        }
    }
    std::ranges::sort(found);
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

std::vector<std::string> unreadableScoreCalls(std::string_view songText) {
    std::vector<std::string> found;
    for (ScoreCall& call : scoreCalls(latex::stripComments(songText)))
        if (call.unreadable() && std::ranges::find(found, call.text) == found.end())
            found.push_back(std::move(call.text));
    return found;
}

std::vector<std::string> scoreRisks(const std::filesystem::path& repoRoot,
                                    const std::filesystem::path& /*songPath*/,
                                    std::string_view songText,
                                    const std::filesystem::path& bookDir) {
    std::vector<std::string> risks;
    const auto add = [&risks](std::string risk) {
        if (std::ranges::find(risks, risk) == risks.end())
            risks.push_back(std::move(risk));
    };
    // Every folder a build finds a score in: each collection (the preview and a book build
    // search them in different orders), the book's folder and the repo root (both on a book
    // build's TEXINPUTS), so whichever copy a build takes has been checked.
    std::vector<std::filesystem::path> dirs;
    for (const std::string& collection : collections(repoRoot))
        dirs.push_back(repoRoot / "songs" / fromUtf8(collection));
    if (!bookDir.empty())
        dirs.push_back(bookDir);
    dirs.push_back(repoRoot);

    for (const ScoreCall& call : scoreCalls(latex::stripComments(songText))) {
        if (call.unreadable()) {
            add(call.text); // unreadableScoreCalls
            continue;
        }
        const auto name = resolveScoreReference(*call.target);
        if (!name) {
            add("\\lilypondfile{" + *call.target + "}");
            continue;
        }
        // lyluatex ends with kpse.find_file("Artist/X.ly"), which tries "Artist/X.ly.tex" before
        // "Artist/X.ly" in every folder: both are read.
        std::vector<std::filesystem::path> files;
        for (const auto& dir : dirs) {
            files.push_back(dir / fromUtf8(*name + ".tex"));
            files.push_back(dir / fromUtf8(*name));
        }
        for (const auto& file : files) {
            std::error_code ec;
            if (!std::filesystem::exists(std::filesystem::symlink_status(file, ec)))
                continue;
            if (!std::filesystem::is_regular_file(file, ec)) {
                add(utf8Generic(file.lexically_relative(repoRoot)) + ": not a plain file");
                continue;
            }
            const auto text = readTextFile(file);
            if (!text) {
                add(*name + ": cannot read the file");
                continue;
            }
            const std::string label =
                file.extension() == ".tex" ? *name + ".tex" : *name; // the .tex one is odd anyway
            for (const std::string& risk : safety::lilypondRisks(*text))
                add(label + ": " + risk);
            if (label != *name)
                add(label + ": would be built instead of " + *name);
            for (const std::string& risk : includeShadows(file.parent_path()))
                add(*name + ": " + risk);
        }
    }
    return risks;
}

std::vector<std::string> referencedFiles(std::string_view songText) {
    std::vector<std::string> files;
    for (ScoreCall& call : scoreCalls(latex::stripComments(songText))) {
        if (call.target && !call.target->empty() &&
            std::ranges::find(files, *call.target) == files.end())
            files.push_back(std::move(*call.target));
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

std::size_t Readiness::toFix() const {
    std::set<std::string> songs;
    for (const auto* list : {&missing, &unsafe, &notFitting, &notChecked})
        songs.insert(list->begin(), list->end());
    return songs.size() + files.size();
}

std::size_t Readiness::notes() const {
    std::set<std::string> songs;
    for (const auto* list : {&todo, &changes, &noKey})
        songs.insert(list->begin(), list->end());
    return songs.size();
}

Readiness readiness(const Songbook& book,
                    const std::function<SongFacts(std::string_view inputName)>& facts) {
    Readiness out;
    std::map<std::string, SongFacts> known;
    std::set<std::string> seen;
    for (const Section& section : book.sections)
        for (const std::string& name : section.songs) {
            if (!seen.insert(name).second)
                continue;
            SongFacts f = facts(name);
            if (!f.exists) {
                out.missing.push_back(name);
                continue;
            }
            if (!f.risks.empty())
                out.unsafe.push_back(name);
            if (f.notChecked)
                out.notChecked.push_back(name);
            if (!f.todos.empty())
                out.todo.push_back(name);
            if (!f.changes.empty())
                out.changes.push_back(name);
            if (!f.hasKey)
                out.noKey.push_back(name);
            known.emplace(name, std::move(f));
        }
    out.notFitting = notMarkedFitting(book, [&known](std::string_view name) {
        const auto it = known.find(std::string(name));
        return it == known.end() ? std::nullopt : std::optional<std::string>(it->second.pap);
    });
    return out;
}

bool removeSong(Songbook& book, std::string_view inputName) {
    bool removed = false;
    for (Section& section : book.sections)
        removed |= std::erase(section.songs, std::string(inputName)) > 0;
    return removed;
}

} // namespace zub::songbook
