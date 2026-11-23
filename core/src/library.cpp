#include "zub/core/library.hpp"

#include "zub/core/latex.hpp"
#include "zub/core/songbook.hpp"
#include "zub/core/songtodo.hpp"
#include "zub/core/textfile.hpp"

#include <algorithm>
#include <map>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace zub {

namespace {

constexpr auto songsSubdir = "songs";
constexpr auto chordsCollection = "chords";

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
    if (!isDirectory(root / songsSubdir / chordsCollection))
        return std::unexpected("no songs/chords/ in " + utf8(root));
    // The package that builds the songs may be the app's own copy (zub::package).
    return {};
}

std::expected<void, std::string> createSongbookRepo(const fs::path& root) {
    std::error_code ec;
    if (fs::exists(root, ec)) {
        if (!fs::is_directory(root, ec))
            return std::unexpected("not a directory: " + utf8(root));
        if (!fs::is_empty(root, ec) || ec)
            return std::unexpected("not empty: " + utf8(root));
    }
    for (const fs::path& dir : {root / songsSubdir / chordsCollection, root / "songbooks"}) {
        fs::create_directories(dir, ec);
        if (ec)
            return std::unexpected("cannot create " + utf8(dir) + ": " + ec.message());
    }
    const std::pair<fs::path, std::string_view> files[] = {
        {root / songsSubdir / chordsCollection / ".gitkeep", ""},
        {root / "songbooks" / ".gitkeep", ""},
        {root / "README.md",
         "# Songbook\n\nSongs in `songs/chords/<Artist>/<Title>.tex`, songbooks in "
         "`songbooks/`. Made with Zpěvníkář (ZubuvZpevnikar); the LaTeX package that builds them "
         "is zubuvzpevnik.\n"},
        {root / ".gitignore", "build/\n*.aux\n*.log\n*.out\n*.toc\n*.sbx\n*.sxd\n*.sxc\n"
                              "tmp-ly/\n"},
    };
    for (const auto& [path, text] : files)
        if (auto written = writeTextFileAtomic(path, text); !written)
            return std::unexpected(written.error());
    return validateSongbookRepo(root);
}

bool validCollectionName(std::string_view name) {
    return !name.empty() && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-';
    });
}

std::vector<std::string> collections(const fs::path& root) {
    std::vector<std::string> names;
    std::error_code ec;
    fs::directory_iterator it(root / songsSubdir, ec);
    for (const fs::directory_iterator end; !ec && it != end; it.increment(ec)) {
        std::error_code typeEc;
        // A real folder with a plain name: the names go onto TEXINPUTS and into build.sh (a ':'
        // or '$' there would add paths or run code), and a link could point anywhere.
        if (it->is_symlink(typeEc) || !it->is_directory(typeEc))
            continue;
        auto name = utf8(it->path().filename());
        if (validCollectionName(name))
            names.push_back(std::move(name));
    }
    std::ranges::sort(names);
    if (const auto chords = std::ranges::find(names, chordsCollection); chords != names.end())
        std::rotate(names.begin(), chords, chords + 1);
    return names;
}

std::optional<std::string> collectionOf(const fs::path& root, const fs::path& path) {
    const std::string prefix = (root / songsSubdir / "").generic_string();
    const std::string full = path.generic_string();
    if (!full.starts_with(prefix))
        return std::nullopt;
    const auto rest = std::string_view(full).substr(prefix.size());
    const auto slash = rest.find('/');
    if (slash == 0 || slash == std::string_view::npos)
        return std::nullopt;
    return std::string(rest.substr(0, slash));
}

std::expected<Library, std::string> scanLibrary(const fs::path& root) {
    if (auto valid = validateSongbookRepo(root); !valid)
        return std::unexpected(valid.error());

    Library library;
    // Collection order of the first song of each input name, for the duplicate note.
    std::map<std::string, std::string> firstCollection;
    const auto collectionNames = collections(root);
    for (const auto& collection : collectionNames) {
        const fs::path songsRoot = root / songsSubdir / collection;
        auto artistDirs = listSorted(songsRoot, true);
        if (!artistDirs) {
            if (collection == chordsCollection)
                return std::unexpected(utf8(songsRoot) + ": " + artistDirs.error());
            library.issues.push_back({songsRoot, artistDirs.error()});
            continue;
        }
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
                // Counted without comments, like songfile.py: a commented-out header is not a
                // song.
                const auto begins = countOccurrences(latex::stripComments(*content), "\\beginsong");
                if (begins > 1) {
                    library.issues.push_back(
                        {file, "contains " + std::to_string(begins) +
                                   " \\beginsong; only the first one is listed"});
                }
                auto inputName = utf8(artistDir.filename() / file.stem());
                const auto [first, isNew] = firstCollection.try_emplace(inputName, collection);
                if (!isNew) {
                    library.issues.push_back(
                        {file, "the same name as songs/" + first->second + "/" + inputName +
                                   ".tex: a songbook's \\input{" + inputName + "} gets that one"});
                }
                library.songs.push_back({file, std::move(inputName), std::move(*meta),
                                         todo::notes(*content), songbook::referencedFiles(*content),
                                         collection});
            }
        }
    }
    // Stable: equal names stay in collection order.
    std::ranges::stable_sort(library.songs, {}, &SongEntry::inputName);
    return library;
}

} // namespace zub
