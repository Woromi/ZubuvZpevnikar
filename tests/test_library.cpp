#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <zub/core/latex.hpp>
#include <zub/core/library.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;

namespace {

const fs::path fixtureRepo = fs::path{ZUB_TEST_DATA_DIR} / "songbook";

const zub::SongEntry* find(const zub::Library& library, std::string_view inputName) {
    const auto it = std::ranges::find(library.songs, inputName, &zub::SongEntry::inputName);
    return it == library.songs.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("validateSongbookRepo", "[library]") {
    CHECK(zub::validateSongbookRepo(fixtureRepo));
    CHECK_FALSE(zub::validateSongbookRepo(fixtureRepo / "songs"));
    // The package may be the app's own copy: songs/chords/ is enough.
    const auto bare = std::filesystem::temp_directory_path() / "zub-bare-songbook";
    std::filesystem::remove_all(bare);
    std::filesystem::create_directories(bare / "songs" / "chords");
    CHECK(zub::validateSongbookRepo(bare));
    std::filesystem::remove_all(bare);
    CHECK_FALSE(zub::validateSongbookRepo(fixtureRepo / "does-not-exist"));
}

TEST_CASE("collections lists the folders under songs/", "[library]") {
    CHECK(zub::collections(fixtureRepo) == std::vector<std::string>{"chords", "whistle2"});
    CHECK(zub::collections(fixtureRepo / "does-not-exist").empty());

    const auto root = fs::temp_directory_path() / "zub-collections-test";
    fs::remove_all(root);
    for (const char* d : {"tinwhistle", "chords", "banjo", ".hidden", "alto"})
        fs::create_directories(root / "songs" / d);
    std::ofstream(root / "songs" / "file.txt") << "x";
    CHECK(zub::collections(root) ==
          std::vector<std::string>{"chords", "alto", "banjo", "tinwhistle"});
    // Names that would add TEXINPUTS entries or run code in a build.sh, and links: skipped.
    for (const char* d : {"a:b", "x$(id)", "sp ace", "q\"q"})
        fs::create_directories(root / "songs" / d);
    fs::create_directory_symlink(root / "songs" / "alto", root / "songs" / "linked");
    CHECK(zub::collections(root) ==
          std::vector<std::string>{"chords", "alto", "banjo", "tinwhistle"});
    CHECK(zub::validCollectionName("tin_whistle-2"));
    CHECK_FALSE(zub::validCollectionName("a:b"));
    CHECK_FALSE(zub::validCollectionName(""));
    // chords need not exist for the list
    fs::remove_all(root / "songs" / "chords");
    CHECK(zub::collections(root) == std::vector<std::string>{"alto", "banjo", "tinwhistle"});
    fs::remove_all(root);
}

TEST_CASE("collectionOf", "[library]") {
    const fs::path root = "/sb";
    CHECK(zub::collectionOf(root, "/sb/songs/chords/A/B.tex") == "chords");
    CHECK(zub::collectionOf(root, "/sb/songs/whistle2/A/B.tex") == "whistle2");
    CHECK_FALSE(zub::collectionOf(root, "/sb/songs/B.tex"));
    CHECK_FALSE(zub::collectionOf(root, "/sb/songs/"));
    CHECK_FALSE(zub::collectionOf(root, "/sb/songbooks/x/B.tex"));
    CHECK_FALSE(zub::collectionOf(root, "/other/songs/chords/A/B.tex"));
}

TEST_CASE("scanLibrary reads every collection", "[library]") {
    const auto library = zub::scanLibrary(fixtureRepo);
    REQUIRE(library);
    const auto* hum = find(*library, "Imaginary/Hum");
    REQUIRE(hum);
    CHECK(hum->collection == "whistle2");
    CHECK(hum->path == fixtureRepo / "songs/whistle2/Imaginary/Hum.tex");
    CHECK(hum->scores == std::vector<std::string>{"Imaginary/HumMelody.ly"});
    const auto* emc2 = find(*library, "Ayreon/EMc2");
    REQUIRE(emc2);
    CHECK(emc2->collection == "chords");
}

TEST_CASE("scanLibrary reports a name in two collections", "[library]") {
    const auto root = fs::temp_directory_path() / "zub-dupcollections-test";
    fs::remove_all(root);
    const char* song = "\\beginsong{Same}[by={X}]\n\\endsong\n";
    for (const char* c : {"chords", "tinwhistle", "zither"}) {
        fs::create_directories(root / "songs" / c / "Art");
        std::ofstream(root / "songs" / c / "Art" / "Same.tex") << song;
    }
    const auto library = zub::scanLibrary(root);
    REQUIRE(library);
    REQUIRE(library->songs.size() == 3);
    CHECK(library->songs[0].collection == "chords");
    CHECK(library->songs[1].collection == "tinwhistle");
    CHECK(library->songs[2].collection == "zither");
    REQUIRE(library->issues.size() == 2);
    CHECK(library->issues[0].path == root / "songs/tinwhistle/Art/Same.tex");
    CHECK(library->issues[0].message ==
          "the same name as songs/chords/Art/Same.tex: a songbook's \\input{Art/Same} gets that "
          "one");
    CHECK(library->issues[1].path == root / "songs/zither/Art/Same.tex");
    fs::remove_all(root);
}

TEST_CASE("scanLibrary reads the fixture songbook", "[library]") {
    const auto library = zub::scanLibrary(fixtureRepo);
    REQUIRE(library);

    CHECK(library->songs.size() == 8); // 7 chords songs and one in songs/whistle2
    CHECK(std::ranges::is_sorted(library->songs, {}, &zub::SongEntry::inputName));

    const auto* emc2 = find(*library, "Ayreon/EMc2");
    REQUIRE(emc2);
    CHECK(zub::latex::toDisplayText(emc2->meta.title) == "E=mc2");
    CHECK(emc2->path == fixtureRepo / "songs/chords/Ayreon/EMc2.tex");
    CHECK(emc2->todos.empty());

    const auto* abba = find(*library, "Abba/OurLastSummer");
    REQUIRE(abba);
    CHECK_FALSE(abba->meta.keys.contains("note"));

    const auto* mamas = find(*library, "TheMamasAndThePapas/CaliforniaDreamin");
    REQUIRE(mamas);
    CHECK(zub::latex::toDisplayText(mamas->meta.value("by")) == "The Mamas & the Papas");

    const auto* franky = find(*library, "Brontosauri/FrankyDlouhan");
    REQUIRE(franky);
    CHECK(franky->meta.value("lang") == "czech");
    CHECK(zub::latex::toDisplayText(franky->meta.value("key")) == "E♭");

    // A commented-out older header is neither the title nor a "second \beginsong" issue.
    const auto* commented = find(*library, "Synthetic/CommentedHeader");
    REQUIRE(commented);
    CHECK(commented->meta.title == "Commented Header");
    CHECK(std::ranges::none_of(library->issues, [&](const zub::ScanIssue& issue) {
        return issue.path == commented->path;
    }));
}

TEST_CASE("scanLibrary reports broken files and ignores non-.tex files", "[library]") {
    const auto library = zub::scanLibrary(fixtureRepo);
    REQUIRE(library);

    REQUIRE(library->issues.size() == 2);
    for (const auto& issue : library->issues) {
        CHECK(issue.path.parent_path().filename() == "Broken");
        CHECK_FALSE(issue.message.empty());
    }
    CHECK_FALSE(find(*library, "Ayreon/NotASong"));
    CHECK_FALSE(find(*library, "Ayreon/indent"));
}

TEST_CASE("scanLibrary fails on a non-songbook directory", "[library]") {
    CHECK_FALSE(zub::scanLibrary(fixtureRepo / "songs"));
}

// Hidden: runs against the real ../ZubuvZpevnik checkout, which changes on its own.
// Run with: build/debug/tests/zub_core_tests "[integration]"
TEST_CASE("scanLibrary reads the real songbook without issues", "[.integration]") {
    const fs::path realRepo{ZUB_REAL_SONGBOOK_DIR};
    if (!zub::validateSongbookRepo(realRepo))
        SKIP("no songbook checkout at " << realRepo);

    const auto library = zub::scanLibrary(realRepo);
    REQUIRE(library);
    for (const auto& issue : library->issues)
        UNSCOPED_INFO(issue.path << ": " << issue.message);
    CHECK(library->issues.empty());
    CHECK(library->songs.size() >= 200);
    // every folder under songs/ is read, each song knows its collection
    for (const auto& collection : zub::collections(realRepo)) {
        INFO(collection);
        CHECK(std::ranges::any_of(library->songs,
                                  [&](const auto& s) { return s.collection == collection; }));
    }
    for (const auto& song : library->songs)
        CHECK(zub::collectionOf(realRepo, song.path) == song.collection);
    // "% TODO: Solo" and the like (8 songs on 2026-11-08)
    CHECK(std::ranges::count_if(library->songs, [](const auto& s) { return !s.todos.empty(); }) >=
          8);
    for (const auto& song : library->songs) {
        INFO(song.inputName);
        CHECK_FALSE(song.meta.title.empty());
        CHECK_FALSE(song.meta.value("by").empty());
        const auto content = zub::readTextFile(song.path);
        REQUIRE(content);
        const auto unknown = zub::safety::unknownConstructs(*content);
        INFO(song.path.string());
        for (const auto& construct : unknown)
            UNSCOPED_INFO(construct);
        CHECK(unknown.empty());
        for (const auto& raw : {song.meta.title, song.meta.value("by"), song.meta.value("key")}) {
            const auto shown = zub::latex::toDisplayText(raw);
            INFO(raw << " -> " << shown);
            CHECK(shown.find_first_of("\\{}") == std::string::npos);
        }
    }
}

TEST_CASE("scanLibrary records the scores a song includes", "[library]") {
    const auto library = zub::scanLibrary(fixtureRepo);
    REQUIRE(library);
    const auto* california = find(*library, "TheMamasAndThePapas/CaliforniaDreamin");
    REQUIRE(california);
    CHECK(california->scores ==
          std::vector<std::string>{"TheMamasAndThePapas/CaliforniaDreamin.ly"});
    const auto* abba = find(*library, "Abba/OurLastSummer");
    REQUIRE(abba);
    CHECK(abba->scores.empty());
}
