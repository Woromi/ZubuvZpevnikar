#include "zub/core/songimport.hpp"

#include "zub/core/song.hpp"
#include "zub/core/textfile.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace zub::songimport {

namespace {

// UTF-8 text of a path (see utf8() in library.cpp).
std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return {u8.begin(), u8.end()};
}

// A path from UTF-8 text.
fs::path fromUtf8(std::string_view text) {
    return fs::path(std::u8string(text.begin(), text.end()));
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

std::string_view trim(std::string_view text) {
    constexpr std::string_view space = " \t\r\n";
    const auto first = text.find_first_not_of(space);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(space) - first + 1);
}

// Lines without their "\n" / "\r\n".
std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    }
    return lines;
}

std::string asciiLower(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return result;
}

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    for (;;) {
        const auto at = text.find(separator);
        parts.push_back(text.substr(0, at));
        if (at == std::string_view::npos) {
            return parts;
        }
        text = text.substr(at + 1);
    }
}

// "Artist/Title" from "out/latex/<Artist>/<Title>.tex"; nullopt if the path has another shape.
std::optional<std::string> inputNameFromOutputPath(std::string_view path) {
    std::string normalized(path);
    std::ranges::replace(normalized, '\\', '/');
    const auto parts = split(normalized, '/');
    if (parts.size() != 4 || parts[0] != "out" || parts[1] != "latex") {
        return std::nullopt;
    }
    constexpr std::string_view extension = ".tex";
    const auto artist = parts[2];
    auto title = parts[3];
    if (artist.empty() || artist == "." || artist == ".." || title.size() <= extension.size() ||
        !title.ends_with(extension)) {
        return std::nullopt;
    }
    title.remove_suffix(extension.size());
    if (title == "." || title == "..") {
        return std::nullopt;
    }
    return std::string(artist) + "/" + std::string(title);
}

bool isSiteAnsweredWithStatus(std::string_view standardError) {
    constexpr std::string_view marker = "the site answered ";
    for (auto at = standardError.find(marker); at != std::string_view::npos;
         at = standardError.find(marker, at + 1)) {
        const auto next = at + marker.size();
        if (next < standardError.size() &&
            std::isdigit(static_cast<unsigned char>(standardError[next])) != 0) {
            return true;
        }
    }
    return false;
}

FailureKind classify(std::string_view standardError) {
    if (contains(standardError, "bot check")) {
        return FailureKind::BotCheck;
    }
    if (contains(standardError, "Parser Error: No parser available")) {
        return FailureKind::UnsupportedSite;
    }
    if (contains(standardError, "the request failed")) {
        return FailureKind::Network;
    }
    if (isSiteAnsweredWithStatus(standardError)) {
        return FailureKind::Http;
    }
    if (contains(standardError, "Input Error:") || contains(standardError, "Failed to read file")) {
        return FailureKind::InvalidInput;
    }
    return FailureKind::Other;
}

// stderr without the "Retrying (n/m) ..." lines, trimmed.
std::string failureMessage(std::string_view standardError) {
    std::string message;
    for (const auto line : splitLines(standardError)) {
        if (startsWith(line, "Retrying (")) {
            continue;
        }
        message += line;
        message += '\n';
    }
    return std::string(trim(message));
}

Failure failure(FailureKind kind, std::string_view standardError, std::string fallback) {
    auto message = failureMessage(standardError);
    if (message.empty()) {
        message = std::move(fallback);
    }
    return Failure{kind, std::move(message)};
}

// The converter prints this (parsers/*.rs) when a page parses but holds no song.
constexpr std::string_view kNoSongWarning = "Warning: no song found on ";
// Ultimate Guitar says it its own way (parsers/ultimate_guitar_com.rs).
constexpr std::string_view kNoSongDataWarning = "Warning: no song data found on ";

// Appends the "Warning: ..." lines to an Other failure's message unless it already has them.
Failure withWarnings(Failure failure, const std::vector<std::string>& warnings) {
    if (failure.kind != FailureKind::Other) {
        return failure;
    }
    for (const auto& warning : warnings) {
        if (!contains(failure.message, warning)) {
            failure.message += '\n';
            failure.message += warning;
        }
    }
    return failure;
}

} // namespace

std::expected<Command, std::string> prepare(const Request& request) {
    const auto& source = request.source;
    const auto urlRest = startsWith(source, "http://")    ? source.substr(7)
                         : startsWith(source, "https://") ? source.substr(8)
                                                          : std::string{};
    if (urlRest.empty()) {
        std::error_code ec;
        if (source.empty() || !fs::is_regular_file(fromUtf8(source), ec)) {
            return std::unexpected("not a web address and not an existing file: " + source);
        }
    }
    if (request.converter.empty()) {
        return std::unexpected("no chords_converter executable given");
    }
    if (!request.workDir.is_absolute()) {
        return std::unexpected("work directory is not absolute: " + utf8(request.workDir));
    }
    std::error_code ec;
    fs::create_directories(request.workDir, ec);
    if (ec) {
        return std::unexpected("cannot create work directory " + utf8(request.workDir) + ": " +
                               ec.message());
    }
    std::string argument = source;
    if (urlRest.empty()) {
        // The converter runs in workDir, so a relative path would point somewhere else.
        std::error_code absoluteEc;
        const auto absolute = fs::absolute(fromUtf8(source), absoluteEc);
        if (absoluteEc) {
            return std::unexpected("cannot resolve " + source + ": " + absoluteEc.message());
        }
        argument = utf8(absolute);
    }
    return Command{utf8(request.converter), {argument, "latex"}, request.workDir};
}

std::expected<Imported, Failure> interpret(int exitCode, std::string_view standardOutput,
                                           std::string_view standardError,
                                           const fs::path& workDir) {
    std::vector<std::string> warnings;
    for (const auto line : splitLines(standardError)) {
        if (startsWith(line, "Warning: ")) {
            warnings.emplace_back(line);
        }
    }

    if (exitCode == 0) {
        constexpr std::string_view marker = "Saving output to: ";
        std::optional<std::string_view> savedPath;
        for (const auto line : splitLines(standardOutput)) {
            if (startsWith(line, marker)) {
                savedPath = line.substr(marker.size());
                break;
            }
        }
        if (savedPath) {
            const auto inputName = inputNameFromOutputPath(*savedPath);
            if (!inputName) {
                return std::unexpected(
                    withWarnings(Failure{FailureKind::Other,
                                         "unexpected output path: " + std::string(*savedPath)},
                                 warnings));
            }
            if (const auto reason = invalidInputName(*inputName)) {
                return std::unexpected(withWarnings(
                    Failure{FailureKind::Other,
                            "the converter produced a name the songbook cannot use: " + *inputName +
                                " (" + *reason + ")"},
                    warnings));
            }
            if (contains(standardError, kNoSongWarning) ||
                contains(standardError, kNoSongDataWarning) ||
                *inputName == "UnknownArtist/UnknownTitle") {
                return std::unexpected(withWarnings(
                    Failure{FailureKind::Other, "No song was found on that page."}, warnings));
            }
            const bool writeFailed = contains(standardError, "Failed to create file") ||
                                     contains(standardError, "Failed to write to file");
            if (!writeFailed) {
                auto content = readTextFile(workDir / fromUtf8(*savedPath));
                if (content) {
                    return Imported{*inputName, std::move(*content), std::move(warnings)};
                }
            }
        }
        return std::unexpected(withWarnings(failure(classify(standardError), standardError,
                                                    "chords_converter did not write the song"),
                                            warnings));
    }
    return std::unexpected(
        withWarnings(failure(classify(standardError), standardError,
                             "chords_converter exited with code " + std::to_string(exitCode)),
                     warnings));
}

Destination destination(const fs::path& songbookRoot, std::string_view inputName) {
    const auto chords = songbookRoot / "songs" / "chords";
    const auto parts = split(inputName, '/');
    std::string name(inputName);
    if (parts.size() == 2 && !parts[0].empty()) {
        const auto artist = asciiLower(parts[0]);
        std::error_code ec;
        for (fs::directory_iterator artists(chords, ec), end; !ec && artists != end;
             artists.increment(ec)) {
            std::error_code entryEc;
            const auto folder = utf8(artists->path().filename());
            if (artists->is_directory(entryEc) && asciiLower(folder) == artist) {
                name = folder + "/" + std::string(parts[1]);
                break;
            }
        }
    }
    return {chords / fromUtf8(name + ".tex"), name};
}

std::optional<fs::path> existingSong(const fs::path& songbookRoot, std::string_view inputName) {
    const auto parts = split(inputName, '/');
    if (parts.size() != 2 || parts[0].empty() || parts[1].empty()) {
        return std::nullopt;
    }
    const auto artist = asciiLower(parts[0]);
    const auto title = asciiLower(parts[1]);
    const auto chords = songbookRoot / "songs" / "chords";

    std::error_code ec;
    for (fs::directory_iterator artists(chords, ec), end; !ec && artists != end;
         artists.increment(ec)) {
        std::error_code entryEc;
        if (!artists->is_directory(entryEc) ||
            asciiLower(utf8(artists->path().filename())) != artist) {
            continue;
        }
        std::error_code songsEc;
        for (fs::directory_iterator songs(artists->path(), songsEc), songsEnd;
             !songsEc && songs != songsEnd; songs.increment(songsEc)) {
            std::error_code fileEc;
            const auto& path = songs->path();
            if (songs->is_regular_file(fileEc) && path.extension() == ".tex" &&
                asciiLower(utf8(path.stem())) == title) {
                return path;
            }
        }
    }
    return std::nullopt;
}

} // namespace zub::songimport
