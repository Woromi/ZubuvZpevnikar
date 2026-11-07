#include "zub/core/library.hpp"

#include "zub/core/latex.hpp"
#include "zub/core/textfile.hpp"

#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

namespace zub {

namespace {

constexpr auto songsSubdir = "songs/chords";

std::size_t countOccurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    for (auto pos = text.find(needle); pos != std::string_view::npos;
         pos = text.find(needle, pos + needle.size()))
        ++count;
    return count;
}

bool isDirectory(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// UTF-8 text of a path. path::string() uses the ANSI code page on Windows, which mangles
// Czech names; u8string() is UTF-8 everywhere.
std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return {u8.begin(), u8.end()};
}

// Sorted subdirectories (or .tex files) of `dir`; directory_iterator order is unspecified.
// A listing error is returned rather than silently truncating the result.
std::expected<std::vector<fs::path>, std::string> listSorted(const fs::path& dir,
                                                             bool wantDirectories) {
    std::vector<fs::path> result;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    for (const fs::directory_iterator end; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        std::error_code typeEc;
        if (wantDirectories) {
            if (entry.is_directory(typeEc))
                result.push_back(entry.path());
        } else if (entry.is_regular_file(typeEc) && entry.path().extension() == ".tex") {
            result.push_back(entry.path());
        }
    }
    if (ec)
        return std::unexpected("cannot list directory: " + ec.message());
    std::ranges::sort(result);
    return result;
}

} // namespace

std::expected<void, std::string> validateSongbookRepo(const fs::path& root) {
    if (!isDirectory(root))
        return std::unexpected("not a directory: " + utf8(root));
    if (!isDirectory(root / songsSubdir))
        return std::unexpected("no songs/chords/ in " + utf8(root));
    if (!isDirectory(root / "zubuvzpevnik"))
        return std::unexpected("no zubuvzpevnik/ in " + utf8(root));
    return {};
}

std::expected<Library, std::string> scanLibrary(const fs::path& root) {
    if (auto valid = validateSongbookRepo(root); !valid)
        return std::unexpected(valid.error());

    const fs::path songsRoot = root / songsSubdir;
    auto artistDirs = listSorted(songsRoot, true);
    if (!artistDirs)
        return std::unexpected(utf8(songsRoot) + ": " + artistDirs.error());

    Library library;
    for (const auto& artistDir : *artistDirs) {
        auto files = listSorted(artistDir, false);
        if (!files) {
            library.issues.push_back({artistDir, files.error()});
            continue;
        }
        for (const auto& file : *files) {
            const auto content = readTextFile(file);
            if (!content) {
                library.issues.push_back({file, "cannot read the file"});
                continue;
            }
            auto meta = parseSong(*content);
            if (!meta) {
                library.issues.push_back({file, meta.error()});
                continue;
            }
            // Counted without comments, like songfile.py: a commented-out header is not a song.
            const auto begins = countOccurrences(latex::stripComments(*content), "\\beginsong");
            if (begins > 1) {
                library.issues.push_back({file, "contains " + std::to_string(begins) +
                                                    " \\beginsong; only the first one is listed"});
            }
            auto inputName = utf8(artistDir.filename() / file.stem());
            library.songs.push_back({file, std::move(inputName), std::move(*meta)});
        }
    }
    std::ranges::sort(library.songs, {}, &SongEntry::inputName);
    return library;
}

} // namespace zub
