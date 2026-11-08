#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
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
    CHECK_FALSE(zub::validateSongbookRepo(fixtureRepo / "does-not-exist"));
}

TEST_CASE("scanLibrary reads the fixture songbook", "[library]") {
    const auto library = zub::scanLibrary(fixtureRepo);
    REQUIRE(library);

    CHECK(library->songs.size() == 7);
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
    // "% TODO: Solo" and the like (8 songs on 2026-11-08)
    CHECK(std::ranges::count_if(library->songs, [](const auto& s) { return !s.todos.empty(); }) >= 8);
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
