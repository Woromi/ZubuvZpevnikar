#include "zub/core/preview.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>
#include <zub/core/song.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;

namespace zub::preview {

namespace {

#ifdef _WIN32
constexpr char kSep = ';';
#else
constexpr char kSep = ':';
#endif

// UTF-8 text of a path (see utf8() in library.cpp).
std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return {u8.begin(), u8.end()};
}

std::optional<std::string> validateClass(const std::string& cls) {
    if (cls.empty())
        return std::string("invalid document class: empty");
    for (const char c : cls) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) &&
              static_cast<unsigned char>(c) < 0x80) &&
            c != '_' && c != '-')
            return std::string("invalid document class: only letters, digits, '_' and '-' allowed");
    }
    return std::nullopt;
}

// Parses a line number; nullopt if it does not fit in an int (logs are untrusted input).
std::optional<int> toInt(std::string_view digits) {
    int value = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc{} || end != digits.data() + digits.size())
        return std::nullopt;
    return value;
}

std::string rtrim(std::string s) {
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::vector<std::string> splitLines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        auto line = text.substr(start, end - start);
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        lines.emplace_back(line);
        start = end + 1;
    }
    return lines;
}

struct Located {
    std::string file;
    int line = 0;
    std::string message;
};

bool texishExtension(const std::string& file) {
    const auto name =
        file.substr(file.find_last_of('/') == std::string::npos ? 0 : file.find_last_of('/') + 1);
    const auto dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0)
        return true; // no extension
    std::string ext = name.substr(dot + 1);
    std::ranges::transform(ext, ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "tex" || ext == "sty" || ext == "cls" || ext == "cfg" || ext == "ly" ||
           ext == "clo" || ext == "def";
}

// "<file>:<line>: <message>" at column 0. The file must look like a path: a TeX-ish extension
// (or none), and no spaces unless it is clearly absolute/relative with an extension.
std::optional<Located> parseFileLine(const std::string& text) {
    if (text.empty() || text.front() == ' ' || text.front() == '\t')
        return std::nullopt;
    std::size_t pos = 0;
    while ((pos = text.find(':', pos)) != std::string::npos) {
        std::size_t digitsEnd = pos + 1;
        while (digitsEnd < text.size() && text[digitsEnd] >= '0' && text[digitsEnd] <= '9')
            ++digitsEnd;
        if (digitsEnd > pos + 1 && digitsEnd < text.size() && text[digitsEnd] == ':' &&
            (digitsEnd + 1 == text.size() || text[digitsEnd + 1] == ' ')) {
            const auto file = text.substr(0, pos);
            if (file.empty() || file.find_first_of("|\"<>*?") != std::string::npos ||
                !texishExtension(file))
                return std::nullopt;
            if (file.find(' ') != std::string::npos) {
                const bool pathLike = file.front() == '/' || file.starts_with("./") ||
                                      file.starts_with("../") ||
                                      (file.size() > 2 && file[1] == ':' &&
                                       std::isalpha(static_cast<unsigned char>(file[0])));
                const bool hasExt = file.find_last_of('.') != std::string::npos;
                if (!pathLike || !hasExt)
                    return std::nullopt;
            }
            const auto line = toInt(std::string_view(text).substr(pos + 1, digitsEnd - pos - 1));
            if (!line)
                return std::nullopt;
            Located l;
            l.file = file;
            l.line = *line;
            l.message = text.size() > digitsEnd + 2 ? text.substr(digitsEnd + 2) : "";
            return l;
        }
        ++pos;
    }
    return std::nullopt;
}

constexpr std::string_view kFatalSummary = " ==> Fatal error occurred";

bool isFatalSummary(const Located& l) {
    return std::string_view(l.message).starts_with(kFatalSummary);
}

// "l.<N> ..." context line.
std::optional<int> parseContextLine(const std::string& text) {
    if (!text.starts_with("l.") || text.size() < 3)
        return std::nullopt;
    std::size_t i = 2;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9')
        ++i;
    if (i == 2 || (i < text.size() && text[i] != ' '))
        return std::nullopt;
    return toInt(std::string_view(text).substr(2, i - 2));
}

std::vector<std::string> contextAfter(const std::vector<std::string>& lines, std::size_t from) {
    std::vector<std::string> context;
    for (auto i = from; i < lines.size() && context.size() < 6; ++i) {
        if (lines[i].empty())
            break;
        context.push_back(rtrim(lines[i]));
    }
    return context;
}

bool refersTo(const fs::path& reported, const fs::path& songCopy) {
    const auto a = reported.lexically_normal();
    const auto b = songCopy.lexically_normal();
    if (a == b)
        return true;
    if (!a.is_relative())
        return false;
    // Relative: the song copy must end with all of its components.
    auto ia = a.end();
    auto ib = b.end();
    while (ia != a.begin()) {
        if (ib == b.begin())
            return false;
        --ia;
        --ib;
        if (*ia != *ib)
            return false;
    }
    return a.has_filename();
}

} // namespace

std::expected<Command, std::string> prepare(const Request& request) {
    if (const auto reason = invalidInputName(request.inputName))
        return std::unexpected("invalid input name: " + *reason);
    if (auto err = validateClass(request.documentClass))
        return std::unexpected(*err);
    for (const auto& [name, content] : request.fileOverrides) {
        if (const auto reason = invalidInputName(name))
            return std::unexpected("invalid override name: " + *reason);
        if (!name.ends_with(".ly"))
            return std::unexpected("invalid override name: only .ly files can be overridden");
    }

    const auto& work = request.workDir;
    if (!work.is_absolute())
        return std::unexpected("work directory must be an absolute path");
    std::error_code ec;
    fs::create_directories(work, ec);
    if (ec)
        return std::unexpected(utf8(work) + ": " + ec.message());

    const auto overrideDir = work / "override";
    fs::remove_all(overrideDir, ec); // a missing directory is not an error
    if (ec)
        return std::unexpected(utf8(overrideDir) + ": " + ec.message());

    const fs::path songCopy = overrideDir / fs::path(request.inputName + ".tex");
    fs::create_directories(songCopy.parent_path(), ec);
    if (ec)
        return std::unexpected(utf8(songCopy.parent_path()) + ": " + ec.message());
    if (auto w = writeTextFileAtomic(songCopy, request.songContent); !w)
        return std::unexpected(w.error());
    for (const auto& [name, content] : request.fileOverrides) {
        const fs::path target = overrideDir / fs::path(name);
        fs::create_directories(target.parent_path(), ec);
        if (ec)
            return std::unexpected(utf8(target.parent_path()) + ": " + ec.message());
        if (auto w = writeTextFileAtomic(target, content); !w)
            return std::unexpected(w.error());
    }

    const std::string wrapper = "\\documentclass[chorded, onesongcolumn, noshading]{" +
                                request.documentClass +
                                "}\n"
                                "\\begin{document}\n"
                                "\\begin{songs}{}\n"
                                "\\input{" +
                                request.inputName +
                                "}\n"
                                "\\end{songs}\n"
                                "\\end{document}\n";
    if (auto w = writeTextFileAtomic(work / "preview.tex", wrapper); !w)
        return std::unexpected(w.error());

    Command cmd;
    cmd.program = "lualatex";
    cmd.arguments = {"-shell-escape",    "-interaction=nonstopmode", "-halt-on-error",
                     "-file-line-error", "-jobname=preview",         "preview.tex"};
    cmd.workingDirectory = work;
    cmd.pdf = work / "preview.pdf";
    cmd.log = work / "preview.log";
    cmd.songCopy = songCopy;

    const std::string root = utf8(request.songbookRoot);
    const std::string texinputs = std::string(".") + kSep + utf8(overrideDir) + kSep + root + kSep +
                                  root + "/zubuvzpevnik" + kSep + root + "/songs/chords" + kSep;
    cmd.environment = {{"TEXINPUTS", texinputs},
                       {"max_print_line", "1000"},
                       {"error_line", "254"},
                       {"half_error_line", "238"}};

    fs::remove(cmd.pdf, ec); // a stale PDF must not look like the result of this build
    fs::remove(cmd.log, ec); // likewise a stale log
    return cmd;
}

std::vector<std::string> documentClasses(const fs::path& songbookRoot) {
    std::vector<std::string> names;
    std::error_code ec;
    fs::directory_iterator it(songbookRoot / "zubuvzpevnik", ec);
    if (ec)
        return names;
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec)
            break;
        std::error_code fe;
        if (it->is_regular_file(fe) && it->path().extension() == ".cls")
            names.push_back(utf8(it->path().stem()));
    }
    std::ranges::sort(names);
    return names;
}

std::optional<songsuggest::Paper> paperOf(const fs::path& songbookRoot,
                                          std::string_view className) {
    if (className.empty() || className.find_first_of("/\\") != std::string_view::npos ||
        className.find("..") != std::string_view::npos)
        return std::nullopt;
    const auto content =
        readTextFile(songbookRoot / "zubuvzpevnik" / (std::string(className) + ".cls"));
    if (!content)
        return std::nullopt;
    for (const std::string_view line : splitLines(*content)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] == '%')
            continue;
        const auto load = line.find("\\LoadClass", first);
        if (load == std::string_view::npos)
            continue;
        const auto close = line.find(']', load);
        const auto options = line.substr(load, close == std::string_view::npos
                                                   ? std::string_view::npos
                                                   : close - load);
        if (options.find("a5paper") != std::string_view::npos)
            return songsuggest::Paper::A5;
        if (options.find("a4paper") != std::string_view::npos)
            return songsuggest::Paper::A4;
    }
    return std::nullopt;
}

std::optional<Error> firstError(std::string_view log, const fs::path& songCopy) {
    const auto lines = splitLines(log);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        Error err;
        if (lines[i].starts_with("! ")) {
            err.message = rtrim(lines[i].substr(2));
            err.context = contextAfter(lines, i + 1);
            const auto limit = std::min(lines.size(), i + 31);
            for (auto j = i + 1; j < limit; ++j) {
                if (const auto loc = parseFileLine(lines[j]); loc && !isFatalSummary(*loc)) {
                    err.file = loc->file;
                    err.line = loc->line;
                    break;
                }
                if (const auto n = parseContextLine(lines[j])) {
                    err.line = *n;
                    break;
                }
            }
        } else if (const auto loc = parseFileLine(lines[i]); loc && !isFatalSummary(*loc)) {
            err.message = rtrim(loc->message);
            err.file = loc->file;
            err.line = loc->line;
            err.context = contextAfter(lines, i + 1);
        } else {
            continue;
        }
        err.inSong = !err.file.empty() && refersTo(err.file, songCopy);
        return err;
    }
    return std::nullopt;
}

} // namespace zub::preview
