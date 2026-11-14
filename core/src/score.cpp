#include "zub/core/score.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <system_error>
#include <zub/core/song.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;

namespace zub::score {

namespace {

// UTF-8 text of a path (see utf8() in library.cpp).
std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return {u8.begin(), u8.end()};
}

std::optional<std::string> invalidScoreName(const std::string& name) {
    if (const auto reason = invalidInputName(name))
        return reason;
    if (!name.ends_with(".ly") || name.size() == 3 || name[name.size() - 4] == '/')
        return std::string("must be a .ly file");
    return std::nullopt;
}

std::optional<int> toInt(std::string_view digits) {
    int value = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc{} || end != digits.data() + digits.size())
        return std::nullopt;
    return value;
}

} // namespace

std::expected<Command, std::string> prepare(const Request& request) {
    if (const auto reason = invalidScoreName(request.inputName))
        return std::unexpected("invalid score name: " + *reason);
    const auto& work = request.workDir;
    if (!work.is_absolute())
        return std::unexpected("work directory must be an absolute path");
    std::error_code ec;
    fs::create_directories(work, ec);
    if (ec)
        return std::unexpected(utf8(work) + ": " + ec.message());

    const auto scoreDir = work / "score";
    fs::remove_all(scoreDir, ec);
    if (ec)
        return std::unexpected(utf8(scoreDir) + ": " + ec.message());
    // The score that makes LilyPond play the music too goes last: line numbers stay valid.
    std::string content = request.content;
    bool withMidi = false;
    if (request.midi) {
        if (const auto played = midiScore(content, *request.midi)) {
            if (!content.empty() && content.back() != '\n')
                content += '\n';
            content += *played;
            content += '\n';
            withMidi = true;
        }
    }
    const fs::path copy = scoreDir / fs::path(request.inputName);
    fs::create_directories(copy.parent_path(), ec);
    if (ec)
        return std::unexpected(utf8(copy.parent_path()) + ": " + ec.message());
    if (auto w = writeTextFileAtomic(copy, content); !w)
        return std::unexpected(w.error());

    // LilyPond fails with "cannot change directory" if the output directory is missing.
    const auto outDir = work / "out";
    fs::remove_all(outDir, ec);
    fs::create_directories(outDir, ec);
    if (ec)
        return std::unexpected(utf8(outDir) + ": " + ec.message());

    // zubuvzpevnik.sty loads lyluatex with language=english, and deutsch for Czech and Slovak
    // songs; LilyPond itself would default to Dutch note names.
    std::string wrapper =
        request.germanNames ? "\\language \"deutsch\"\n" : "\\language \"english\"\n";
    wrapper += "\\include \"" + request.inputName + "\"\n";
    if (auto w = writeTextFileAtomic(work / "score.ly", wrapper); !w)
        return std::unexpected(w.error());

    Command cmd;
    cmd.program = "lilypond";
    cmd.arguments = {"-dbackend=svg",
                     "-dcrop",
                     "-I",
                     utf8(request.includeDir),
                     "-I",
                     utf8(scoreDir),
                     "-o",
                     utf8(outDir / "score"),
                     "score.ly"};
    cmd.workingDirectory = work;
    cmd.environment = {{"LC_ALL", "C"}};
    cmd.svg = outDir / "score.cropped.svg";
    if (withMidi)
        cmd.midi = outDir / "score.midi";
    cmd.scoreCopy = copy;
    return cmd;
}

bool germanNames(std::string_view lang) {
    while (!lang.empty() && (lang.front() == '{' || lang.front() == ' ' || lang.front() == '\t'))
        lang.remove_prefix(1);
    while (!lang.empty() && (lang.back() == '}' || lang.back() == ' ' || lang.back() == '\t'))
        lang.remove_suffix(1);
    std::string lower(lang);
    std::ranges::transform(lower, lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "czech" || lower == "slovak";
}

std::vector<Error> errors(std::string_view log) {
    constexpr std::string_view marker = ": error: ";
    std::vector<Error> found;
    std::size_t start = 0;
    while (start <= log.size()) {
        auto end = log.find('\n', start);
        if (end == std::string_view::npos)
            end = log.size();
        auto line = log.substr(start, end - start);
        start = end + 1;
        if (line.ends_with('\r'))
            line.remove_suffix(1);
        const auto at = line.find(marker);
        if (at == std::string_view::npos)
            continue;
        // "<file>:<line>:<col>" before the marker, parsed from the right: files may contain ':'.
        const auto head = line.substr(0, at);
        const auto colonCol = head.rfind(':');
        if (colonCol == std::string_view::npos || colonCol == 0)
            continue;
        const auto colonLine = head.rfind(':', colonCol - 1);
        if (colonLine == std::string_view::npos || colonLine == 0)
            continue;
        const auto col = toInt(head.substr(colonCol + 1));
        const auto lineNo = toInt(head.substr(colonLine + 1, colonCol - colonLine - 1));
        if (!col || !lineNo)
            continue;
        Error e;
        e.message = std::string(line.substr(at + marker.size()));
        e.file = fs::path(std::u8string(head.begin(), head.begin() + static_cast<long>(colonLine)));
        e.line = *lineNo;
        e.column = *col;
        found.push_back(std::move(e));
    }
    return found;
}

std::optional<Error> firstError(std::string_view log) {
    auto all = errors(log);
    if (all.empty())
        return std::nullopt;
    return std::move(all.front());
}

NewScore newScore() {
    const std::string before = "\\include \"zubuvzpevnik.ily\"\n"
                               "\n"
                               "\\score {\n"
                               "  <<\n"
                               "  \\new ChordNames {\n"
                               "    \\chordmode {\n"
                               "      c1 |\n"
                               "    }\n"
                               "  }\n"
                               "  \\new TabStaff {\n"
                               "    {\n"
                               "      \\tabFullNotation\n"
                               "      ";
    const std::string after = "\n"
                              "    }\n"
                              "  }\n"
                              ">>\n"
                              "}\n";
    return {before + "<e,\\6 e\\4>1" + after, before.size()};
}

std::optional<std::string> partName(std::string_view typed) {
    std::string part;
    bool wordStart = true;
    for (const char c : typed) {
        if (c == ' ' || c == '\t' || c == '-' || c == '_') {
            wordStart = true;
            continue;
        }
        const bool lower = c >= 'a' && c <= 'z';
        const bool upper = c >= 'A' && c <= 'Z';
        const bool digit = c >= '0' && c <= '9';
        if (!lower && !upper && !digit)
            return std::nullopt;
        part += wordStart && lower ? static_cast<char>(c - 'a' + 'A') : c;
        wordStart = false;
    }
    return part;
}

std::string scoreFileName(std::string_view songStem, std::string_view part) {
    std::string name(songStem);
    name += part;
    name += ".ly";
    return name;
}

} // namespace zub::score
