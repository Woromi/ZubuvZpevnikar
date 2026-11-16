#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <zub/core/bookfile.hpp>
#include <zub/core/latex.hpp>
#include <zub/core/library.hpp>
#include <zub/core/song.hpp>
#include <zub/core/textfile.hpp>

namespace zub::bookfile {

namespace {

using std::size_t;
using std::string;
using std::string_view;
constexpr auto npos = string_view::npos;

constexpr string_view whitespace = " \t\r\n\f\v";

string_view trim(string_view s) {
    const auto b = s.find_first_not_of(whitespace);
    if (b == npos)
        return {};
    return s.substr(b, s.find_last_not_of(whitespace) - b + 1);
}

bool isLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// Position of the first `\<name>` in `code` at or after `from` that is not the prefix of a longer
// command (the (?![A-Za-z]) of checkorder.py's TOKEN); npos if none.
size_t findCommand(string_view code, string_view name, size_t from = 0) {
    const string needle = "\\" + string(name);
    while (true) {
        const auto p = code.find(needle, from);
        if (p == npos)
            return npos;
        const auto after = p + needle.size();
        if (after >= code.size() || !isLetter(code[after]))
            return p;
        from = after;
    }
}

struct BraceArg {
    string value;
    size_t end = 0; // one past the closing brace
};

// The `{...}` after `pos` (whitespace skipped), if any.
std::optional<BraceArg> braceArg(string_view code, size_t pos) {
    pos = code.find_first_not_of(whitespace, pos);
    if (pos == npos || code[pos] != '{')
        return std::nullopt;
    const auto close = latex::findMatching(code, pos);
    if (!close)
        return std::nullopt;
    return BraceArg{string(code.substr(pos + 1, *close - pos - 1)), *close + 1};
}

std::vector<string> splitNames(string_view list) {
    std::vector<string> out;
    size_t start = 0;
    while (start <= list.size()) {
        auto end = list.find(',', start);
        if (end == npos)
            end = list.size();
        const auto name = trim(list.substr(start, end - start));
        if (!name.empty())
            out.emplace_back(name);
        start = end + 1;
    }
    return out;
}

enum class Kind { Plain, Input, Begin, End, Odd };

struct Line {
    size_t begin = 0, contentEnd = 0, end = 0; // end includes the break, contentEnd does not
    size_t number = 0;                         // 1-based line in the file
    Kind kind = Kind::Plain;
    std::vector<string> names; // Input: the name; Odd: the inputs found
};

struct EnvDetail {
    Environment env;
    std::vector<Line> lines;                          // the body's lines
    std::vector<std::pair<size_t, size_t>> unitLines; // [first, last) body line of each unit
    // For the section edits (byte offsets in the text).
    string indexText;      // the raw argument of \begin{songs}
    size_t blockBegin = 0; // start of the line of the last \songsection, else of \begin{songs}
    size_t blockEnd = 0;   // end (with the break) of the \end{songs} line
    size_t sectionArgBegin = npos, sectionArgEnd = npos; // inside the braces of that \songsection
};

struct RawLine {
    size_t begin, contentEnd, end;
};

std::vector<RawLine> splitLines(string_view text) {
    std::vector<RawLine> lines;
    size_t pos = 0;
    while (pos < text.size()) {
        const auto nl = text.find('\n', pos);
        RawLine l{pos, text.size(), text.size()};
        if (nl != npos) {
            l.end = nl + 1;
            l.contentEnd = nl;
            if (l.contentEnd > pos && text[l.contentEnd - 1] == '\r')
                --l.contentEnd;
        }
        lines.push_back(l);
        pos = l.end;
    }
    return lines;
}

bool hasBookTokens(string_view code) {
    return findCommand(code, "input") != npos || findCommand(code, "beginkeeporder") != npos ||
           findCommand(code, "endkeeporder") != npos;
}

// The inputs of a line, as checkorder.py's TOKEN finds them.
std::vector<string> inputsIn(string_view code) {
    std::vector<string> out;
    size_t from = 0;
    while (true) {
        const auto p = findCommand(code, "input", from);
        if (p == npos)
            break;
        from = p + 6;
        if (from <= code.size() && code.substr(p + 6, 1) == "{") {
            const auto close = code.find('}', p + 6);
            if (close == npos)
                break;
            const auto name = trim(code.substr(p + 7, close - p - 7));
            if (!name.empty())
                out.emplace_back(name);
            from = close;
        }
    }
    return out;
}

void classify(Line& line, string_view code) {
    const auto t = trim(code);
    if (t == "\\beginkeeporder") {
        line.kind = Kind::Begin;
    } else if (t == "\\endkeeporder") {
        line.kind = Kind::End;
    } else if (t.starts_with("\\input{")) {
        const auto arg = braceArg(t, 6);
        if (arg && arg->end == t.size() && !trim(arg->value).empty()) {
            line.kind = Kind::Input;
            line.names = {string(trim(arg->value))};
            return;
        }
        line.kind = Kind::Odd;
        line.names = inputsIn(code);
    } else if (hasBookTokens(code)) {
        line.kind = Kind::Odd;
        line.names = inputsIn(code);
    }
}

void buildUnits(EnvDetail& d, const string& notClean) {
    auto& env = d.env;
    auto readOnly = [&](size_t lineNumber, const string& why) {
        if (env.readOnly.empty())
            env.readOnly = "line " + std::to_string(lineNumber) + ": " + why;
    };
    size_t unitStart = 0;
    bool inBlock = false;
    Unit block;
    auto push = [&](Unit u, size_t last) {
        u.begin = d.lines[unitStart].begin;
        u.end = d.lines[last - 1].end;
        d.unitLines.emplace_back(unitStart, last);
        env.units.push_back(std::move(u));
        unitStart = last;
    };
    for (size_t i = 0; i < d.lines.size(); ++i) {
        const auto& l = d.lines[i];
        switch (l.kind) {
        case Kind::Plain:
            break;
        case Kind::Input:
        case Kind::Odd:
            if (l.kind == Kind::Odd)
                readOnly(l.number, "an \\input that is not alone on its line");
            if (inBlock) {
                block.songs.insert(block.songs.end(), l.names.begin(), l.names.end());
            } else {
                Unit u;
                u.songs = l.names;
                push(std::move(u), i + 1);
            }
            break;
        case Kind::Begin:
            if (inBlock) {
                readOnly(l.number, "\\beginkeeporder inside a block");
            } else {
                inBlock = true;
                block = Unit{};
                block.block = true;
            }
            break;
        case Kind::End:
            if (!inBlock) {
                readOnly(l.number, "\\endkeeporder without \\beginkeeporder");
            } else {
                inBlock = false;
                push(std::move(block), i + 1);
            }
            break;
        }
    }
    if (inBlock) {
        readOnly(d.lines.back().number, "\\beginkeeporder without \\endkeeporder");
        if (!d.lines.empty())
            push(std::move(block), d.lines.size());
    }
    if (!notClean.empty() && env.readOnly.empty())
        env.readOnly = notClean;
    // A song listed twice: which of its lines would an edit move or remove?
    std::set<string> seen;
    for (const auto& u : env.units)
        for (const auto& name : u.songs)
            if (!seen.insert(name).second && env.readOnly.empty())
                env.readOnly = "\\input{" + name + "} is in this list twice";
}

std::expected<std::vector<EnvDetail>, std::string> scan(string_view text) {
    const auto raw = splitLines(text);
    std::vector<EnvDetail> out;
    string section;
    bool inBody = false;
    EnvDetail cur;
    size_t beginLine = 0;
    string beginProblem;
    size_t sectionLineBegin = npos, argBegin = npos, argEnd = npos;
    for (size_t i = 0; i < raw.size(); ++i) {
        const auto content = text.substr(raw[i].begin, raw[i].contentEnd - raw[i].begin);
        const auto code = latex::stripComments(content);
        if (!inBody) {
            if (const auto p = findCommand(code, "songsection"); p != npos) {
                if (const auto arg = braceArg(code, p + 12)) {
                    section = arg->value;
                    sectionLineBegin = raw[i].begin;
                    argEnd = raw[i].begin + arg->end - 1;
                    argBegin = argEnd - arg->value.size();
                }
            }
            const auto b = code.find("\\begin{songs}");
            if (b == npos)
                continue;
            const auto arg = braceArg(code, b + 13);
            if (!arg)
                return std::unexpected("line " + std::to_string(i + 1) +
                                       ": \\begin{songs} without its index list");
            if (code.find("\\begin{songs}", arg->end) != npos)
                return std::unexpected("line " + std::to_string(i + 1) + ": nested \\begin{songs}");
            cur = EnvDetail{};
            // Code after the index list on the same line (\newpage) stays there; a song there
            // would be in no unit.
            const string after(code.substr(arg->end));
            if (after.find("\\input") != npos || after.find("keeporder") != npos)
                beginProblem =
                    "line " + std::to_string(i + 1) + ": an \\input on the \\begin{songs} line";
            cur.env.section = section;
            cur.indexText = arg->value;
            cur.blockBegin = sectionLineBegin != npos ? sectionLineBegin : raw[i].begin;
            cur.sectionArgBegin = argBegin;
            cur.sectionArgEnd = argEnd;
            cur.env.indexes = splitNames(arg->value);
            cur.env.bodyBegin = raw[i].end;
            beginLine = i + 1;
            inBody = true;
            continue;
        }
        if (code.find("\\begin{songs}") != npos)
            return std::unexpected("line " + std::to_string(i + 1) + ": nested \\begin{songs}");
        if (code.find("\\end{songs}") != npos) {
            cur.env.bodyEnd = raw[i].begin;
            cur.blockEnd = raw[i].end;
            string notClean = std::exchange(beginProblem, string());
            if (notClean.empty() && trim(code) != "\\end{songs}")
                notClean =
                    "line " + std::to_string(i + 1) + ": \\end{songs} is not alone on its line";
            buildUnits(cur, notClean);
            out.push_back(std::move(cur));
            cur = EnvDetail{};
            inBody = false;
            section.clear();
            sectionLineBegin = argBegin = argEnd = npos;
            continue;
        }
        Line l;
        l.begin = raw[i].begin;
        l.contentEnd = raw[i].contentEnd;
        l.end = raw[i].end;
        l.number = i + 1;
        classify(l, code);
        cur.lines.push_back(std::move(l));
    }
    if (inBody)
        return std::unexpected("line " + std::to_string(beginLine) +
                               ": \\begin{songs} without \\end{songs}");
    return out;
}

// The first occurrence of `\<command>{..}` (optionally `[..]` before the brace) in code.
std::optional<BraceArg> commandArg(string_view code, string_view command, bool optional = false) {
    const auto p = findCommand(code, command);
    if (p == npos)
        return std::nullopt;
    auto pos = p + command.size() + 1;
    if (optional) {
        const auto q = code.find_first_not_of(whitespace, pos);
        if (q != npos && code[q] == '[') {
            const auto close = latex::findMatching(code, q);
            if (!close)
                return std::nullopt;
            pos = *close + 1;
        }
    }
    return braceArg(code, pos);
}

// A plain line that is only the commented-out input of a song: `<indent>%<spaces>\input{X}`,
// then optionally spaces and a `%` comment. Gives the name, and the range to erase.
struct CommentedInput {
    string name;
    size_t eraseBegin, eraseEnd; // offsets in the line's content
};

std::optional<CommentedInput> commentedInput(string_view content) {
    const auto pct = content.find_first_not_of(" \t");
    if (pct == npos || content[pct] != '%')
        return std::nullopt;
    const auto in = content.find_first_not_of(" \t", pct + 1);
    if (in == npos || content.substr(in, 7) != "\\input{")
        return std::nullopt;
    const auto arg = braceArg(content, in + 6);
    if (!arg)
        return std::nullopt;
    const auto rest = content.find_first_not_of(whitespace, arg->end);
    if (rest != npos && content[rest] != '%')
        return std::nullopt;
    const auto name = trim(arg->value);
    if (name.empty())
        return std::nullopt;
    return CommentedInput{string(name), pct, in};
}

} // namespace

std::vector<string> Environment::songs() const {
    std::vector<string> out;
    for (const auto& u : units)
        out.insert(out.end(), u.songs.begin(), u.songs.end());
    return out;
}

std::vector<string> Book::songs() const {
    std::vector<string> out;
    for (const auto& e : environments) {
        const auto s = e.songs();
        out.insert(out.end(), s.begin(), s.end());
    }
    return out;
}

std::expected<Book, string> parse(string_view text) {
    auto scanned = scan(text);
    if (!scanned)
        return std::unexpected(scanned.error());
    Book book;
    for (auto& d : *scanned)
        book.environments.push_back(std::move(d.env));

    const auto code = latex::stripComments(text);
    if (const auto a = commandArg(code, "documentclass", true))
        book.documentClass = string(trim(a->value));
    if (const auto a = commandArg(code, "title"))
        book.title = a->value;
    if (const auto a = commandArg(code, "author"))
        book.author = a->value;
    size_t from = 0;
    while (true) {
        const auto p = findCommand(code, "newindex", from);
        if (p == npos)
            break;
        from = p + 9;
        const auto id = braceArg(code, from);
        if (!id)
            continue;
        const auto file = braceArg(code, id->end);
        if (!file)
            continue;
        book.indexes.emplace_back(trim(id->value), trim(file->value));
        from = file->end;
    }
    return book;
}

std::expected<string, string> setSongs(string_view text, size_t environment,
                                       const std::vector<string>& names) {
    auto scanned = scan(text);
    if (!scanned)
        return std::unexpected(scanned.error());
    if (environment >= scanned->size())
        return std::unexpected("no such environment: " + std::to_string(environment));
    if (!(*scanned)[environment].env.readOnly.empty())
        return std::unexpected("environment cannot be edited: " +
                               (*scanned)[environment].env.readOnly);
    std::set<string> nameSet;
    for (const auto& n : names) {
        if (const auto why = invalidInputName(n))
            return std::unexpected("invalid song name \"" + n + "\": " + *why);
        if (!nameSet.insert(n).second)
            return std::unexpected("duplicate song name \"" + n + "\"");
    }

    // Uncomment `% \input{X}` lines for the new names X (only outside keep-order blocks).
    string work(text);
    {
        const auto& d = (*scanned)[environment];
        std::set<string> existing;
        for (const auto& u : d.env.units)
            existing.insert(u.songs.begin(), u.songs.end());
        std::vector<std::pair<size_t, size_t>> erases; // ascending
        std::set<string> done;
        bool inBlock = false;
        // Uncommented, a line takes the plain lines above it into its unit: only where those
        // are no wish or other note (which belongs to the song below it), i.e. right after a
        // unit, the body's start or another commented input. Elsewhere a new line is added.
        bool afterNote = false;
        for (const auto& l : d.lines) {
            const auto content = string_view(work).substr(l.begin, l.contentEnd - l.begin);
            if (l.kind == Kind::Begin) {
                inBlock = true;
            } else if (l.kind == Kind::End) {
                inBlock = false;
            } else if (l.kind == Kind::Plain) {
                const auto c = commentedInput(content);
                if (c && !inBlock && !afterNote && nameSet.contains(c->name) &&
                    !existing.contains(c->name) && done.insert(c->name).second)
                    erases.emplace_back(l.begin + c->eraseBegin, l.begin + c->eraseEnd);
                afterNote = !c;
                continue;
            }
            afterNote = false;
        }
        for (const auto& [b, e] : erases | std::views::reverse)
            work.erase(b, e - b);
        if (!erases.empty()) {
            scanned = scan(work);
            if (!scanned)
                return std::unexpected(scanned.error());
        }
    }
    const auto& all = *scanned;
    const auto& d = all[environment];
    const auto& units = d.env.units;

    std::unordered_map<string, size_t> index; // name -> position in names
    for (size_t i = 0; i < names.size(); ++i)
        index[names[i]] = i;
    std::unordered_map<string, size_t> unitOf;
    std::vector<size_t> minIndex(units.size(), npos);
    for (size_t u = 0; u < units.size(); ++u)
        for (const auto& s : units[u].songs) {
            unitOf.emplace(s, u);
            if (const auto it = index.find(s); it != index.end())
                minIndex[u] = std::min(minIndex[u], it->second);
        }
    auto movable = [&](size_t u) { return minIndex[u] != npos; };

    // Indent and line break of new lines.
    string indent;
    bool haveIndent = false;
    auto indentOf = [&](const EnvDetail& e) {
        for (const auto& l : e.lines)
            if (l.kind == Kind::Input) {
                const auto content = string_view(work).substr(l.begin, l.contentEnd - l.begin);
                indent = string(content.substr(0, content.find_first_not_of(" \t")));
                return true;
            }
        return false;
    };
    haveIndent = indentOf(d);
    for (size_t i = 0; !haveIndent && i < all.size(); ++i)
        haveIndent = indentOf(all[i]);
    if (!haveIndent)
        indent = "    ";
    const auto firstNl = work.find('\n');
    const string eol = firstNl != npos && firstNl > 0 && work[firstNl - 1] == '\r' ? "\r\n" : "\n";

    // Where each new line goes: after a unit, or at the front.
    std::vector<size_t> anchor(names.size(), npos);
    std::vector<std::vector<string>> after(units.size());
    std::vector<string> front;
    constexpr auto frontAnchor = npos;
    for (size_t i = 0; i < names.size(); ++i) {
        if (const auto it = unitOf.find(names[i]); it != unitOf.end()) {
            anchor[i] = it->second;
            continue;
        }
        const auto a = i == 0 ? frontAnchor : anchor[i - 1];
        const string line = indent + "\\input{" + names[i] + "}" + eol;
        (a == frontAnchor ? front : after[a]).push_back(line);
        anchor[i] = a;
    }

    // The text of a unit: leftover form when nothing of it is kept.
    auto render = [&](size_t u, string& out) {
        const bool keepMarkers = movable(u) || units[u].songs.empty();
        const auto [lb, le] = d.unitLines[u];
        for (size_t i = lb; i < le; ++i) {
            const auto& l = d.lines[i];
            const auto line = string_view(work).substr(l.begin, l.end - l.begin);
            switch (l.kind) {
            case Kind::Plain:
            case Kind::Odd:
                out += line;
                break;
            case Kind::Begin:
            case Kind::End:
                if (keepMarkers)
                    out += line;
                break;
            case Kind::Input: {
                if (nameSet.contains(l.names[0])) {
                    out += line;
                    break;
                }
                const auto content = line.substr(0, l.contentEnd - l.begin);
                if (latex::stripComments(content).size() < content.size()) {
                    const auto ind = content.find_first_not_of(" \t");
                    out += content.substr(0, ind);
                    out += "% ";
                    out += line.substr(ind);
                }
                break;
            }
            }
        }
    };

    std::vector<size_t> order;
    for (size_t u = 0; u < units.size(); ++u)
        if (movable(u))
            order.push_back(u);
    std::ranges::stable_sort(order, [&](size_t a, size_t b) { return minIndex[a] < minIndex[b]; });

    string body;
    size_t next = 0;
    bool frontDone = false;
    for (size_t u = 0; u < units.size(); ++u) {
        if (!movable(u)) {
            render(u, body);
            continue;
        }
        if (!frontDone) {
            for (const auto& l : front)
                body += l;
            frontDone = true;
        }
        const auto v = order[next++];
        render(v, body);
        for (const auto& l : after[v])
            body += l;
    }
    if (!frontDone)
        for (const auto& l : front)
            body += l;
    const auto tailBegin = d.unitLines.empty() ? d.env.bodyBegin : units.back().end;
    body += string_view(work).substr(tailBegin, d.env.bodyEnd - tailBegin);

    return work.substr(0, d.env.bodyBegin) + body + work.substr(d.env.bodyEnd);
}

std::vector<string> leaders(const Environment& env) {
    std::vector<string> out;
    for (const auto& u : env.units)
        if (!u.songs.empty())
            out.push_back(u.songs.front());
    return out;
}

std::vector<string> expandLeaders(const Environment& env, const std::vector<string>& ordered) {
    std::map<string, const Unit*> byLeader;
    for (const auto& u : env.units)
        if (!u.songs.empty())
            byLeader.emplace(u.songs.front(), &u);
    std::vector<string> out;
    for (const auto& name : ordered) {
        if (const auto it = byLeader.find(name); it != byLeader.end())
            out.insert(out.end(), it->second->songs.begin(), it->second->songs.end());
        else
            out.push_back(name);
    }
    return out;
}

std::vector<string> indexFiles(const Book& book) {
    std::vector<string> out;
    auto add = [&](const string& file) {
        if (std::ranges::find(out, file) == out.end())
            out.push_back(file);
    };
    // A declared index is built even when no environment shows it (ayreon's indexTTE).
    for (const auto& [id, file] : book.indexes)
        add(file);
    for (const auto& env : book.environments)
        for (const auto& name : env.indexes) {
            const auto it =
                std::ranges::find(book.indexes, name, &std::pair<string, string>::first);
            add(it == book.indexes.end() ? name : it->second);
        }
    return out;
}

std::filesystem::path booksFolder(const std::filesystem::path& repoRoot) {
    std::error_code ec;
    if (!std::filesystem::is_directory(repoRoot / "songbooks", ec) &&
        std::filesystem::is_directory(repoRoot / "projects", ec))
        return "projects";
    return "songbooks";
}

std::vector<std::filesystem::path> findBooks(const std::filesystem::path& repoRoot) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = repoRoot / booksFolder(repoRoot);
    if (!fs::is_directory(dir, ec))
        return {};
    std::vector<fs::path> out;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec),
        end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        // A link could point anywhere: only real files of the tree are books.
        if (it->path().extension() != ".tex" || it->is_symlink(e2) || !it->is_regular_file(e2))
            continue;
        const auto text = readTextFile(it->path());
        if (!text)
            continue;
        if (latex::stripComments(*text).find("\\documentclass") != npos)
            out.push_back(fs::relative(it->path(), repoRoot, e2));
    }
    std::ranges::sort(out);
    return out;
}

std::vector<std::string> inputs(std::string_view text) {
    const string code = latex::stripComments(text);
    std::vector<string> names;
    for (auto p = findCommand(code, "input"); p != npos; p = findCommand(code, "input", p + 6)) {
        const auto arg = braceArg(code, p + 6);
        const string name = arg ? string(trim(arg->value)) : string();
        if (std::ranges::find(names, name) == names.end())
            names.push_back(name);
    }
    return names;
}

songbook::BuildCommand buildCommand(const std::filesystem::path& repoRoot,
                                    const std::filesystem::path& packageDir,
                                    const std::filesystem::path& workDir,
                                    const std::filesystem::path& bookDir, std::string_view bookName,
                                    const std::vector<std::string>& indexes) {
    const auto utf8 = [](const std::filesystem::path& p) {
        const auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    };
    songbook::BuildCommand command;
    const std::string root = utf8(repoRoot);
    command.program = "bash";
    command.arguments = {utf8(packageDir / "scripts/build.sh")};
    for (const auto& index : indexes) {
        command.arguments.emplace_back("-i");
        command.arguments.push_back(index);
    }
    command.arguments.push_back(utf8(workDir / (std::string(bookName) + ".tex")));
    command.workingDirectory = workDir;
    // The songs and the package first: nothing in the book's folder or the repo root can stand
    // in for a checked song or a package file (class, .sty, .cfg, Lua).
    std::string texinputs = ".:" + utf8(workDir) + ":";
    for (const auto& collection : collections(repoRoot))
        texinputs += root + "/songs/" + collection + ":";
    texinputs += utf8(packageDir) + ":" + utf8(bookDir) + ":" + root + ":";
    command.environment.emplace_back("TEXINPUTS", std::move(texinputs));
    command.pdf = workDir / (std::string(bookName) + ".pdf");
    command.log = workDir / (std::string(bookName) + ".log");
    return command;
}

// ---- Settings ----

namespace {

constexpr string_view kLockMark = "% zpevnikar: locked";

// The text with every comment replaced by spaces: same length, so offsets are the text's.
string maskComments(string_view t) {
    string out(t);
    size_t i = 0;
    while (i < t.size()) {
        if (t[i] == '\\') {
            i += 2;
        } else if (t[i] == '%') {
            const auto nl = t.find_first_of("\r\n", i);
            const auto e = nl == npos ? t.size() : nl;
            for (size_t k = i; k < e; ++k)
                out[k] = ' ';
            i = e;
        } else {
            ++i;
        }
    }
    return out;
}

string eolOf(string_view t) {
    const auto nl = t.find('\n');
    return nl != npos && nl > 0 && t[nl - 1] == '\r' ? "\r\n" : "\n";
}

// A value that can stand in a one-line `{...}`: no line break, no comment, balanced braces.
bool validText(string_view v) {
    int depth = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        const char c = v[i];
        if (c == '\n' || c == '\r' || c == '%')
            return false;
        if (c == '\\') {
            if (++i >= v.size() || v[i] == '\n' || v[i] == '\r')
                return false;
        } else if (c == '{') {
            ++depth;
        } else if (c == '}' && --depth < 0) {
            return false;
        }
    }
    return depth == 0;
}

bool validFile(string_view v) {
    return validText(v) && v.find_first_of("{}[]") == npos;
}

struct Group {
    size_t open = 0, close = 0; // the delimiters
};
enum class Found { Absent, Ok, Broken };

Found groupAt(const string& m, size_t pos, char open, bool sameLine, Group& g) {
    pos = m.find_first_not_of(sameLine ? " \t" : whitespace, pos);
    if (pos == npos || m[pos] != open)
        return Found::Absent;
    const auto close = latex::findMatching(m, pos);
    if (!close)
        return Found::Broken;
    g = Group{pos, *close};
    return Found::Ok;
}

bool multiline(const string& m, const Group& g) {
    return m.find_first_of("\r\n", g.open) < g.close;
}

std::vector<size_t> findAll(const string& m, string_view name) {
    std::vector<size_t> out;
    for (auto p = findCommand(m, name); p != npos; p = findCommand(m, name, p + 1))
        out.push_back(p);
    return out;
}

struct LineSpan {
    size_t begin, contentEnd, end;
};

LineSpan lineAt(string_view t, size_t off) {
    LineSpan l{0, 0, t.size()};
    if (off > 0)
        if (const auto b = t.rfind('\n', off - 1); b != npos)
            l.begin = b + 1;
    if (const auto e = t.find('\n', off); e != npos)
        l.end = e + 1;
    l.contentEnd = l.end;
    if (l.contentEnd > l.begin && t[l.contentEnd - 1] == '\n')
        --l.contentEnd;
    if (l.contentEnd > l.begin && t[l.contentEnd - 1] == '\r')
        --l.contentEnd;
    return l;
}

// A new line `line` after the line holding `off`.
void insertLineAfter(string& text, size_t off, string_view line) {
    const auto l = lineAt(text, off);
    const auto eol = eolOf(text);
    if (l.end == text.size() && (text.empty() || text.back() != '\n'))
        text.insert(l.end, eol + string(line));
    else
        text.insert(l.end, string(line) + eol);
}

struct Token {
    size_t begin, end;
};

struct DocScan {
    string masked;
    Settings s;
    bool hasClass = false;
    size_t classEnd = 0; // just after "\documentclass"
    std::optional<Group> options;
    Group classGroup;
    std::vector<Token> tokens;
    size_t chordedTok = npos, columnsTok = npos, shadingTok = npos; // indexes into tokens
    std::optional<Group> titleGroup, authorGroup, campGroup;
    size_t titleAt = npos, authorAt = npos, campAt = npos;             // the commands
    size_t titleAnchor = npos, authorAnchor = npos, campAnchor = npos; // offsets on their lines
    size_t coverAt = npos, coverEnd = npos;
    std::optional<Group> coverGroup;
    std::vector<size_t> chartAts;
};

string_view slice(const string& t, size_t b, size_t e) {
    return string_view(t).substr(b, e - b);
}

DocScan scanDoc(string_view text) {
    DocScan d;
    d.masked = maskComments(text);
    const auto& m = d.masked;
    auto& s = d.s;
    auto& bad = s.notEditable;

    if (const auto cmd = findCommand(m, "documentclass"); cmd != npos) {
        d.hasClass = true;
        d.classEnd = cmd + 14;
        size_t pos = d.classEnd;
        Group g;
        bool optionsBroken = false;
        switch (groupAt(m, pos, '[', false, g)) {
        case Found::Ok:
            d.options = g;
            pos = g.close + 1;
            break;
        case Found::Broken:
            optionsBroken = true;
            break;
        case Found::Absent:
            break;
        }
        if (!optionsBroken && groupAt(m, pos, '{', false, d.classGroup) == Found::Ok) {
            s.bookClass = string(trim(slice(m, d.classGroup.open + 1, d.classGroup.close)));
        } else {
            optionsBroken = true;
            bad.insert(Field::Class);
        }
        if (optionsBroken) {
            bad.insert(Field::Chorded);
            bad.insert(Field::TwoColumns);
            bad.insert(Field::Shading);
        }
        if (d.options && !optionsBroken) {
            size_t start = d.options->open + 1;
            while (start <= d.options->close) {
                auto end = m.find(',', start);
                if (end == npos || end > d.options->close)
                    end = d.options->close;
                auto b = start, e = end;
                while (b < e && whitespace.find(m[b]) != npos)
                    ++b;
                while (e > b && whitespace.find(m[e - 1]) != npos)
                    --e;
                if (b < e)
                    d.tokens.push_back({b, e});
                start = end + 1;
            }
            auto find = [&](Field f, std::initializer_list<string_view> names, size_t& index) {
                size_t count = 0;
                for (size_t i = 0; i < d.tokens.size(); ++i) {
                    const auto t = slice(m, d.tokens[i].begin, d.tokens[i].end);
                    if (std::ranges::find(names, t) != names.end()) {
                        index = i;
                        ++count;
                    }
                }
                if (count > 1)
                    bad.insert(f);
            };
            find(Field::Chorded, {"chorded", "lyric"}, d.chordedTok);
            find(Field::TwoColumns, {"twosongcolumns", "onesongcolumn"}, d.columnsTok);
            find(Field::Shading, {"noshading"}, d.shadingTok);
            if (d.chordedTok != npos)
                s.chorded =
                    slice(m, d.tokens[d.chordedTok].begin, d.tokens[d.chordedTok].end) == "chorded";
            if (d.columnsTok != npos)
                s.twoColumns = slice(m, d.tokens[d.columnsTok].begin, d.tokens[d.columnsTok].end) ==
                               "twosongcolumns";
            if (d.shadingTok != npos)
                s.shading = false;
        }
    }

    auto simple = [&](string_view name, Field f, std::optional<Group>& group, size_t& at,
                      size_t& anchor, string& value) {
        const auto all = findAll(m, name);
        if (all.empty())
            return;
        at = anchor = all[0];
        Group g;
        const bool ok =
            groupAt(m, at + name.size() + 1, '{', false, g) == Found::Ok && !multiline(m, g);
        if (ok) {
            group = g;
            anchor = g.close;
            value = string(slice(string(text), g.open + 1, g.close));
        }
        if (all.size() > 1 || !ok)
            bad.insert(f);
    };
    simple("title", Field::Title, d.titleGroup, d.titleAt, d.titleAnchor, s.title);
    simple("author", Field::Author, d.authorGroup, d.authorAt, d.authorAnchor, s.author);

    {
        size_t count = 0;
        bool ok = true;
        for (const auto at : findAll(m, "renewcommand")) {
            Group name, value;
            if (groupAt(m, at + 13, '{', false, name) != Found::Ok ||
                trim(slice(m, name.open + 1, name.close)) != "\\campyear")
                continue;
            ++count;
            d.campAt = d.campAnchor = at;
            ok = groupAt(m, name.close + 1, '{', false, value) == Found::Ok && !multiline(m, value);
            if (ok) {
                d.campGroup = value;
                d.campAnchor = value.close;
                s.campYear = string(text.substr(value.open + 1, value.close - value.open - 1));
            }
        }
        if (count > 1 || (count == 1 && !ok))
            bad.insert(Field::CampYear);
    }

    {
        const auto cls = songbook::bookClassFromName(s.bookClass);
        if (!cls) {
            bad.insert(Field::Cover);
        } else {
            const bool velky = *cls == songbook::BookClass::Velky;
            const string_view name =
                *cls == songbook::BookClass::Maly ? "maketitlepage" : "makefrontcover";
            const auto all = findAll(m, name);
            if (all.size() > 1)
                bad.insert(Field::Cover);
            if (!all.empty()) {
                d.coverAt = all[0];
                d.coverEnd = all[0] + name.size() + 1;
                Group g;
                const auto f = groupAt(m, d.coverEnd, velky ? '{' : '[', true, g);
                if (f == Found::Ok && !multiline(m, g)) {
                    d.coverGroup = g;
                    s.cover = string(text.substr(g.open + 1, g.close - g.open - 1));
                } else if (f != Found::Absent || velky) {
                    bad.insert(Field::Cover);
                } else if (const auto next = m.find_first_not_of(" \t", d.coverEnd);
                           next != npos && m[next] == '{') {
                    bad.insert(Field::Cover); // `{x}` is a velkyzpevnik's argument
                }
            }
        }
    }

    for (const auto at : findAll(m, "insertallchords")) {
        d.chartAts.push_back(at);
        s.chordChart = true;
        const auto l = lineAt(m, at);
        if (trim(slice(m, l.begin, l.contentEnd)) != "\\insertallchords")
            bad.insert(Field::ChordChart);
    }

    size_t pos = 0;
    while (pos < text.size()) {
        const auto l = lineAt(text, pos);
        if (trim(text.substr(l.begin, l.contentEnd - l.begin)) == kLockMark)
            s.locked = true;
        pos = l.end;
    }
    return d;
}

bool parseBool(string_view v, bool& out) {
    if (v != "0" && v != "1")
        return false;
    out = v == "1";
    return true;
}

// The line of \begin{document} (anywhere in code); npos without one.
size_t beginDocument(const string& masked) {
    return masked.find("\\begin{document}");
}

void addOption(string& text, const DocScan& d, string_view token) {
    if (!d.options) {
        text.insert(d.classEnd, "[" + string(token) + "]");
    } else if (d.tokens.empty()) {
        text.insert(d.options->open + 1, token);
    } else {
        text.insert(d.tokens.back().end, ", " + string(token));
    }
}

void removeToken(string& text, const DocScan& d, size_t i) {
    if (d.tokens.size() == 1) {
        text.erase(d.options->open, d.options->close + 1 - d.options->open);
    } else if (i > 0) {
        text.erase(d.tokens[i - 1].end, d.tokens[i].end - d.tokens[i - 1].end);
    } else {
        text.erase(d.tokens[0].begin, d.tokens[1].begin - d.tokens[0].begin);
    }
}

void replaceToken(string& text, const DocScan& d, size_t i, string_view token) {
    text.replace(d.tokens[i].begin, d.tokens[i].end - d.tokens[i].begin, token);
}

void replaceGroup(string& text, const Group& g, string_view value) {
    text.replace(g.open + 1, g.close - g.open - 1, value);
}

} // namespace

Settings readSettings(string_view text) {
    return scanDoc(text).s;
}

std::expected<string, string> setSetting(string_view textView, Field field, string_view value) {
    const auto d = scanDoc(textView);
    const auto& s = d.s;
    if (!d.hasClass)
        return std::unexpected("the file has no \\documentclass");
    if (s.notEditable.contains(field))
        return std::unexpected("this setting is in a form the app cannot edit safely");
    string text(textView);
    bool flag = false;
    const auto needFlag = [&]() -> std::optional<string> {
        if (!parseBool(value, flag))
            return "the value must be 1 or 0";
        return std::nullopt;
    };
    switch (field) {
    case Field::Class: {
        if (!songbook::bookClassFromName(value))
            return std::unexpected("not a songbook class: " + string(value));
        if (s.bookClass == value)
            return text;
        replaceGroup(text, d.classGroup, value);
        return text;
    }
    case Field::Chorded:
    case Field::TwoColumns:
    case Field::Shading: {
        if (const auto why = needFlag())
            return std::unexpected(*why);
        const bool current = field == Field::Chorded      ? s.chorded
                             : field == Field::TwoColumns ? s.twoColumns
                                                          : s.shading;
        if (current == flag)
            return text;
        if (field == Field::Shading) {
            if (flag)
                removeToken(text, d, d.shadingTok);
            else
                addOption(text, d, "noshading");
            return text;
        }
        const size_t tok = field == Field::Chorded ? d.chordedTok : d.columnsTok;
        const string_view token = field == Field::Chorded
                                      ? (flag ? "chorded" : "lyric")
                                      : (flag ? "twosongcolumns" : "onesongcolumn");
        if (tok != npos)
            replaceToken(text, d, tok, token);
        else
            addOption(text, d, token);
        return text;
    }
    case Field::Title:
    case Field::Author: {
        if (!validText(value))
            return std::unexpected("the text must be one line with balanced braces and no "
                                   "unescaped %");
        const bool title = field == Field::Title;
        if ((title ? s.title : s.author) == value && (title ? d.titleAt : d.authorAt) != npos)
            return text;
        const auto& group = title ? d.titleGroup : d.authorGroup;
        if (group) {
            replaceGroup(text, *group, value);
            return text;
        }
        const string line = string(title ? "\\title{" : "\\author{") + string(value) + "}";
        size_t anchor = d.classGroup.close;
        if (!title && d.titleAnchor != npos)
            anchor = d.titleAnchor;
        insertLineAfter(text, anchor, line);
        return text;
    }
    case Field::CampYear: {
        if (!validText(value))
            return std::unexpected("the text must be one line with balanced braces and no "
                                   "unescaped %");
        if (s.campYear == value)
            return text;
        if (d.campGroup) {
            if (value.empty()) {
                const auto l = lineAt(text, d.campAt);
                if (trim(slice(d.masked, l.begin, l.contentEnd)) !=
                    trim(slice(d.masked, d.campAt, d.campGroup->close + 1)))
                    return std::unexpected("\\campyear shares its line with other code");
                text.erase(l.begin, l.end - l.begin);
            } else {
                replaceGroup(text, *d.campGroup, value);
            }
            return text;
        }
        size_t anchor = d.classGroup.close;
        if (d.authorAnchor != npos)
            anchor = d.authorAnchor;
        else if (d.titleAnchor != npos)
            anchor = d.titleAnchor;
        insertLineAfter(text, anchor, "\\renewcommand{\\campyear}{" + string(value) + "}");
        return text;
    }
    case Field::Cover: {
        if (!validFile(value))
            return std::unexpected("not a file name: " + string(value));
        if (s.cover == value)
            return text;
        const auto cls = *songbook::bookClassFromName(s.bookClass);
        const bool velky = cls == songbook::BookClass::Velky;
        if (d.coverAt != npos) {
            if (velky && value.empty())
                return std::unexpected("velkyzpevnik needs a cover image");
            if (d.coverGroup)
                if (value.empty())
                    text.erase(d.coverEnd, d.coverGroup->close + 1 - d.coverEnd);
                else
                    replaceGroup(text, *d.coverGroup, value);
            else
                text.insert(d.coverEnd, "[" + string(value) + "]");
            return text;
        }
        const auto doc = beginDocument(d.masked);
        if (doc == npos)
            return std::unexpected("the file has no \\begin{document}");
        const string line = velky ? "\\makefrontcover{" + string(value) + "}"
                            : cls == songbook::BookClass::Maly
                                ? "\\maketitlepage[" + string(value) + "]"
                                : "\\makefrontcover[" + string(value) + "]";
        insertLineAfter(text, doc, line);
        return text;
    }
    case Field::ChordChart: {
        if (const auto why = needFlag())
            return std::unexpected(*why);
        if (s.chordChart == flag)
            return text;
        if (!flag) {
            for (const auto at : d.chartAts | std::views::reverse) {
                const auto l = lineAt(text, at);
                text.erase(l.begin, l.end - l.begin);
            }
            return text;
        }
        const auto endSongs = d.masked.rfind("\\end{songs}");
        const auto endDoc = d.masked.rfind("\\end{document}");
        if (endSongs == npos && endDoc == npos)
            return std::unexpected("the file has no \\end{songs} or \\end{document}");
        const auto at = endSongs != npos ? endSongs : endDoc;
        const auto l = lineAt(text, at);
        const string line =
            string(text.substr(l.begin, text.find_first_not_of(" \t", l.begin) - l.begin)) +
            "\\insertallchords";
        if (endSongs != npos)
            insertLineAfter(text, at, line);
        else
            text.insert(l.begin, line + eolOf(text));
        return text;
    }
    }
    return text;
}

string_view lockMark() {
    return kLockMark;
}

string setLocked(string_view text, bool locked) {
    if (scanDoc(text).s.locked == locked)
        return string(text);
    if (locked)
        return string(kLockMark) + eolOf(text) + string(text);
    string out;
    size_t pos = 0;
    while (pos < text.size()) {
        const auto l = lineAt(text, pos);
        if (trim(text.substr(l.begin, l.contentEnd - l.begin)) != kLockMark)
            out += text.substr(l.begin, l.end - l.begin);
        pos = l.end;
    }
    return out;
}

// ---- Sections ----

namespace {

std::expected<std::vector<EnvDetail>, string> scanFor(string_view text, size_t environment) {
    auto scanned = scan(text);
    if (!scanned)
        return std::unexpected(scanned.error());
    if (environment >= scanned->size())
        return std::unexpected("no such environment: " + std::to_string(environment));
    return scanned;
}

bool validName(string_view name) {
    return !trim(name).empty() && validText(name);
}

constexpr string_view kBadName =
    "the name must be one line with balanced braces and no unescaped %";

} // namespace

std::expected<string, string> addSection(string_view text, string_view name,
                                         size_t afterEnvironment) {
    if (!validName(name))
        return std::unexpected(string(kBadName));
    auto scanned = scan(text);
    if (!scanned)
        return std::unexpected(scanned.error());
    const auto eol = eolOf(text);
    // The book-wide indexes of the first section: an index of its own (\newindex, an album's
    // in ayreon.tex) is not the new section's; "obsah" when nothing else is left.
    string indexes = "obsah";
    if (!scanned->empty()) {
        const auto book = parse(text);
        std::vector<string> kept;
        for (const auto& index : scanned->front().env.indexes)
            if (!book || std::ranges::none_of(book->indexes, [&](const auto& declared) {
                    return declared.first == index;
                }))
                kept.push_back(index);
        if (kept.size() == scanned->front().env.indexes.size())
            indexes = scanned->front().indexText; // as written
        else if (!kept.empty()) {
            indexes.clear();
            for (const auto& index : kept)
                indexes += (indexes.empty() ? "" : ",") + index;
        }
    }
    const string block = eol + "\\songsection{" + string(name) + "}" + eol + "\\begin{songs}{" +
                         indexes + "}" + eol + "\\end{songs}" + eol;
    string out(text);
    if (scanned->empty()) {
        const auto masked = maskComments(text);
        const auto at = masked.rfind("\\end{document}");
        if (at == npos)
            return std::unexpected("the file has no \\end{document}");
        out.insert(lineAt(text, at).begin, block);
        return out;
    }
    if (afterEnvironment == npos)
        afterEnvironment = scanned->size() - 1;
    if (afterEnvironment >= scanned->size())
        return std::unexpected("no such environment: " + std::to_string(afterEnvironment));
    const auto end = (*scanned)[afterEnvironment].blockEnd;
    if (end == text.size() && (text.empty() || text.back() != '\n'))
        out.insert(end, eol + block.substr(eol.size()) + string());
    else
        out.insert(end, block);
    return out;
}

std::expected<string, string> renameSection(string_view text, size_t environment,
                                            string_view name) {
    if (!validName(name))
        return std::unexpected(string(kBadName));
    const auto scanned = scanFor(text, environment);
    if (!scanned)
        return std::unexpected(scanned.error());
    const auto& d = (*scanned)[environment];
    if (d.sectionArgBegin == npos)
        return std::unexpected("environment " + std::to_string(environment) +
                               " has no \\songsection");
    string out(text);
    out.replace(d.sectionArgBegin, d.sectionArgEnd - d.sectionArgBegin, name);
    return out;
}

std::expected<string, string> removeSection(string_view text, size_t environment) {
    const auto scanned = scanFor(text, environment);
    if (!scanned)
        return std::unexpected(scanned.error());
    const auto& d = (*scanned)[environment];
    auto begin = d.blockBegin;
    if (begin > 0) {
        const auto prev = lineAt(text, begin - 1);
        if (trim(text.substr(prev.begin, prev.contentEnd - prev.begin)).empty())
            begin = prev.begin;
    }
    string out(text);
    out.erase(begin, d.blockEnd - begin);
    return out;
}

std::expected<string, string> moveSection(string_view text, size_t environment, int delta) {
    if (delta != -1 && delta != 1)
        return std::unexpected("a section moves one place at a time");
    const auto scanned = scanFor(text, environment);
    if (!scanned)
        return std::unexpected(scanned.error());
    if ((delta < 0 && environment == 0) || (delta > 0 && environment + 1 >= scanned->size()))
        return std::unexpected("the section cannot move there");
    const auto& lo = (*scanned)[delta < 0 ? environment - 1 : environment];
    const auto& hi = (*scanned)[delta < 0 ? environment : environment + 1];
    if (hi.blockBegin < lo.blockEnd || hi.blockEnd == text.size())
        return std::unexpected("these sections cannot be swapped safely");
    string out(text.substr(0, lo.blockBegin));
    out += text.substr(hi.blockBegin, hi.blockEnd - hi.blockBegin);
    out += text.substr(lo.blockEnd, hi.blockBegin - lo.blockEnd);
    out += text.substr(lo.blockBegin, lo.blockEnd - lo.blockBegin);
    out += text.substr(hi.blockEnd);
    return out;
}

// ---- New books ----

namespace {

string optionList(const Settings& s) {
    string out = s.chorded ? "chorded" : "lyric";
    out += s.twoColumns ? ", twosongcolumns" : ", onesongcolumn";
    if (!s.shading)
        out += ", noshading";
    return out;
}

string buildScript(string_view name, songbook::BookClass cls, const std::vector<string>& indexes,
                   const std::vector<string>& collections) {
    string songTrees;
    for (const auto& c : collections)
        songTrees += "$REPO_ROOT/songs/" + c + ":";
    string indexArgs;
    for (const auto& i : indexes)
        indexArgs += " -i " + i;
    const string n(name);
    string out =
        "#!/bin/bash\n"
        "set -euo pipefail\n"
        "trap 'echo \"ERROR: " +
        n +
        " build.sh failed at line $LINENO\"; exit 1' ERR\n"
        "\n"
        "SCRIPT_DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
        "# The songbook repo: the nearest folder above this one with songs/chords\n"
        "# (normalised: TeX prints the resolved path of every song it opens).\n"
        "REPO_ROOT=\"$SCRIPT_DIR\"\n"
        "while [ ! -d \"$REPO_ROOT/songs/chords\" ]; do\n"
        "    if [ \"$REPO_ROOT\" = \"/\" ]; then\n"
        "        echo \"ERROR: no songs/chords above $SCRIPT_DIR\" >&2\n"
        "        exit 1\n"
        "    fi\n"
        "    REPO_ROOT=\"$(dirname \"$REPO_ROOT\")\"\n"
        "done\n"
        "\n"
        "# $REPO_ROOT is on the path for img/: \\graphicspath is relative to the cwd.\n"
        "export TEXINPUTS=\".:$SCRIPT_DIR:$REPO_ROOT:$REPO_ROOT/zubuvzpevnik:" +
        songTrees +
        "${TEXINPUTS:-}\"\n"
        "\n"
        "# Output goes to build/ at the repo root: zubuvzpevnik/scripts/build.sh writes to\n"
        "# the working directory.\n"
        "mkdir -p \"$REPO_ROOT/build\"\n"
        "cd \"$REPO_ROOT/build\"\n"
        "\n";
    if (cls == songbook::BookClass::Velky)
        return out + "\"$REPO_ROOT/zubuvzpevnik/scripts/build.sh\" \"$@\"" + indexArgs +
               " \"$SCRIPT_DIR/" + n + ".tex\"\n";
    const bool maly = cls == songbook::BookClass::Maly;
    out += string("# ") +
           (maly ? "-s belongs to makebook.sh alone" : "makebooklet.sh only understands -v/-c") +
           ", and -o renames the PDF it has to read;\n# every other option is build.sh's.\n"
           "buildargs=()\n"
           "bookflags=()\n"
           "bookinput=" +
           n +
           "\n"
           "while [ $# -gt 0 ]; do\n"
           "    case \"$1\" in\n";
    if (maly)
        out += "        -s) bookflags+=(-s \"$2\"); shift 2 ;;\n";
    out += "        -o) bookinput=$2; buildargs+=(-o \"$2\"); shift 2 ;;\n"
           "        -v|-c) bookflags+=(\"$1\"); buildargs+=(\"$1\"); shift ;;\n"
           "        *) buildargs+=(\"$1\"); shift ;;\n"
           "    esac\n"
           "done\n"
           "\n"
           "\"$REPO_ROOT/zubuvzpevnik/scripts/build.sh\" \"${buildargs[@]}\"" +
           indexArgs + " \"$SCRIPT_DIR/" + n +
           ".tex\"\n"
           "\"$REPO_ROOT/zubuvzpevnik/scripts/" +
           (maly ? "makebook.sh" : "makebooklet.sh") + "\" \"${bookflags[@]}\" \"$bookinput\"\n";
    return out;
}

} // namespace

std::expected<NewBook, string> newBook(const Settings& s, string_view name,
                                       const std::vector<string>& collections) {
    const auto cls = songbook::bookClassFromName(s.bookClass);
    if (!cls)
        return std::unexpected("not a songbook class: " + s.bookClass);
    if (name.empty() || !std::ranges::all_of(name, [](char c) {
            return isLetter(c) || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }))
        return std::unexpected("the file name may only have letters, digits, _ and -");
    if (!validText(s.title) || !validText(s.author) || !validText(s.campYear))
        return std::unexpected("title, author and year must be one line with balanced braces and "
                               "no unescaped %");
    if (!validFile(s.cover))
        return std::unexpected("not a file name: " + s.cover);

    const string indexes = *cls == songbook::BookClass::Tabor ? "obsah" : "obsah,obsahAutor";
    const bool velky = *cls == songbook::BookClass::Velky;
    const bool maly = *cls == songbook::BookClass::Maly;
    const string chart = s.chordChart ? "\\insertallchords\n\n" : "";
    const string cover = s.cover;

    string tex = "\\documentclass[" + optionList(s) + "]{" + string(songbook::className(*cls)) +
                 "}\n\n\\title{" + s.title + "}\n\\author{" + s.author + "}\n";
    if (*cls == songbook::BookClass::Tabor && !s.campYear.empty())
        tex += "\n\\renewcommand{\\campyear}{" + s.campYear + "}\n";
    tex += "\n\\begin{document}\n\n";
    const string songs = "\\begin{songs}{" + indexes + "}\n\n\\end{songs}\n\n";
    if (velky) {
        if (!cover.empty())
            tex += "\\makefrontcover{" + cover + "}\n";
        tex += "\\insertindex\n\n" + songs + chart;
    } else if (maly) {
        tex += "% The hard cover is glued to this leaf, so it stays blank.\n\\bindingleaf\n\n";
        tex += "\\maketitlepage" + (cover.empty() ? string() : "[" + cover + "]") + "\n\n";
        tex += songs + chart;
        tex += "% At the end: an index at the front would move the song pages it records.\n"
               "\\insertindex\n\n"
               "\\bindingleaf[\\makeimprint]\n\n"
               "% Round the book up to whole sheets of paper for makebook.sh.\n"
               "\\padbookpages\n\n";
    } else {
        tex += "\\makefrontcover" + (cover.empty() ? string() : "[" + cover + "]") + "\n\n";
        tex += songs + chart + "\\padbookletpages\n\n\\makebackcover[]\n\n";
    }
    tex += "\\end{document}\n";

    const auto book = parse(tex);
    if (!book)
        return std::unexpected(book.error());
    return NewBook{tex, buildScript(name, *cls, indexFiles(*book), collections)};
}

std::string buildScriptFor(std::string_view name, songbook::BookClass bookClass,
                           const std::vector<std::string>& indexes,
                           const std::vector<std::string>& collections) {
    return buildScript(name, bookClass, indexes, collections);
}

} // namespace zub::bookfile
