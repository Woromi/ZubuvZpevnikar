#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <system_error>
#include <unordered_map>
#include <zub/core/bookfile.hpp>
#include <zub/core/latex.hpp>
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
}

std::expected<std::vector<EnvDetail>, std::string> scan(string_view text) {
    const auto raw = splitLines(text);
    std::vector<EnvDetail> out;
    string section;
    bool inBody = false;
    EnvDetail cur;
    size_t beginLine = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        const auto content = text.substr(raw[i].begin, raw[i].contentEnd - raw[i].begin);
        const auto code = latex::stripComments(content);
        if (!inBody) {
            if (const auto p = findCommand(code, "songsection"); p != npos) {
                if (const auto arg = braceArg(code, p + 12))
                    section = arg->value;
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
            cur.env.section = section;
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
            string notClean;
            if (trim(code) != "\\end{songs}")
                notClean =
                    "line " + std::to_string(i + 1) + ": \\end{songs} is not alone on its line";
            buildUnits(cur, notClean);
            out.push_back(std::move(cur));
            cur = EnvDetail{};
            inBody = false;
            section.clear();
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
        for (const auto& l : d.lines) {
            if (l.kind == Kind::Begin)
                inBlock = true;
            else if (l.kind == Kind::End)
                inBlock = false;
            else if (l.kind == Kind::Plain && !inBlock) {
                const auto c =
                    commentedInput(string_view(work).substr(l.begin, l.contentEnd - l.begin));
                if (c && nameSet.contains(c->name) && !existing.contains(c->name) &&
                    done.insert(c->name).second)
                    erases.emplace_back(l.begin + c->eraseBegin, l.begin + c->eraseEnd);
            }
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

std::vector<std::filesystem::path> findBooks(const std::filesystem::path& repoRoot) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = repoRoot / "songbooks";
    if (!fs::is_directory(dir, ec)) {
        dir = repoRoot / "projects";
        if (!fs::is_directory(dir, ec))
            return {};
    }
    std::vector<fs::path> out;
    fs::recursive_directory_iterator it(dir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (it->path().extension() != ".tex" || !it->is_regular_file(e2))
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
    command.environment.emplace_back("TEXINPUTS", ".:" + utf8(workDir) + ":" + utf8(bookDir) +
                                                      ":" + root + "/songs/chords:" + root +
                                                      "/songs/tinwhistle:" + root + ":" +
                                                      utf8(packageDir) + ":");
    command.pdf = workDir / (std::string(bookName) + ".pdf");
    command.log = workDir / (std::string(bookName) + ".log");
    return command;
}

} // namespace zub::bookfile
