#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/library.hpp>
#include <zub/core/songdates.hpp>

using namespace std::literals;
namespace sd = zub::songdates;
using std::chrono::days;
using std::chrono::seconds;
using std::chrono::sys_seconds;

namespace {
sys_seconds at(long long unix) {
    return sys_seconds{seconds{unix}};
}
} // namespace

TEST_CASE("the git commands", "[songdates]") {
    const auto log = sd::logCommand("/x/book");
    CHECK(log.program == "git");
    CHECK(log.arguments == std::vector<std::string>{"-C", "/x/book", "-c", "core.quotePath=false",
                                                    "log", "--no-renames", "--name-status",
                                                    "--format=%x01%at", "--", "songs"});
    const auto status = sd::statusCommand("/x/book");
    CHECK(status.program == "git");
    CHECK(status.arguments.at(0) == "-C");
    CHECK(std::ranges::find(status.arguments, "-z") != status.arguments.end());
    CHECK(std::ranges::find(status.arguments, "--no-optional-locks") != status.arguments.end());
}

TEST_CASE("parseLog: added is the oldest creation, edited the newest commit", "[songdates]") {
    const auto dates = sd::parseLog("\x01"
                                    "300\n\nM\tsongs/chords/A/One.tex\nA\tsongs/chords/B/New.tex\n"
                                    "\x01"
                                    "200\n\nM\tsongs/chords/A/One.tex\n"
                                    "\x01"
                                    "100\n\nA\tsongs/chords/A/One.tex\n"
                                    "A\tsongs/chords/A/Two.tex\n"sv);
    REQUIRE(dates.size() == 3);
    const auto& one = dates.at("songs/chords/A/One.tex");
    CHECK(one.added == at(100));
    CHECK(one.edited == at(300));
    CHECK_FALSE(one.uncommitted);
    const auto& two = dates.at("songs/chords/A/Two.tex");
    CHECK(two.added == at(100));
    CHECK(two.edited == at(100));
    CHECK(dates.at("songs/chords/B/New.tex").added == at(300));
}

TEST_CASE("parseLog: a rename is a delete and a create; deleted paths are dropped", "[songdates]") {
    const auto dates = sd::parseLog("\x01"
                                    "500\n\nD\tsongs/chords/A/Old.tex\n"
                                    "A\tsongs/chords/A/Renamed.tex\n"
                                    "\x01"
                                    "400\n\nD\tsongs/chords/A/Gone.tex\n"
                                    "\x01"
                                    "300\n\nM\tsongs/chords/A/Old.tex\n"
                                    "\x01"
                                    "100\n\nA\tsongs/chords/A/Old.tex\n"
                                    "A\tsongs/chords/A/Gone.tex\n"sv);
    CHECK_FALSE(dates.contains("songs/chords/A/Old.tex"));
    CHECK_FALSE(dates.contains("songs/chords/A/Gone.tex"));
    REQUIRE(dates.contains("songs/chords/A/Renamed.tex"));
    CHECK(dates.at("songs/chords/A/Renamed.tex").added == at(500));
}

TEST_CASE("parseLog: a path deleted and created again counts from the new creation",
          "[songdates]") {
    const auto dates = sd::parseLog("\x01"
                                    "300\n\nA\tsongs/chords/A/X.tex\n"
                                    "\x01"
                                    "200\n\nD\tsongs/chords/A/X.tex\n"
                                    "\x01"
                                    "100\n\nA\tsongs/chords/A/X.tex\n"sv);
    REQUIRE(dates.contains("songs/chords/A/X.tex"));
    CHECK(dates.at("songs/chords/A/X.tex").added == at(300));
}

TEST_CASE("parseLog: spaces, UTF-8, CRLF, empty and broken input", "[songdates]") {
    const auto dates =
        sd::parseLog("\x01"
                     "10\r\n\r\nA\tsongs/chords/Jaromír Nohavica/Dokud se zpívá.tex\r\n"
                     "\x01"
                     "20\n\n" // a merge: no files
                     "A\tsongs/tinwhistle/Ž/Šíleně.ly\n"sv);
    REQUIRE(dates.size() == 2);
    CHECK(dates.at("songs/chords/Jaromír Nohavica/Dokud se zpívá.tex").added == at(10));
    CHECK(dates.at("songs/tinwhistle/Ž/Šíleně.ly").added == at(20));

    CHECK(sd::parseLog("").empty());
    CHECK(sd::parseLog("\n\n").empty());
    CHECK(sd::parseLog("A\tsongs/x.tex\n").empty()); // no commit time before it
    CHECK(sd::parseLog("\x01"
                       "abc\n\nA\tsongs/x.tex\n"sv)
              .empty()); // unreadable time
    CHECK(sd::parseLog("\x01"
                       "5\n\nnonsense\nA\t\n"sv)
              .empty());
}

TEST_CASE("applyStatus reads the -z records", "[songdates]") {
    auto dates = sd::parseLog("\x01"
                              "100\n\nA\tsongs/chords/A/One.tex\nA\tsongs/chords/A/Two.tex\n"sv);
    sd::applyStatus(dates, " M songs/chords/A/One.tex\0"
                           "?? songs/chords/B/Untracked název.tex\0"
                           "A  songs/chords/B/Staged.tex\0"
                           "R  songs/chords/C/Moved.tex\0songs/chords/C/Before.tex\0"
                           " D songs/chords/A/Two.tex\0"
                           "?? songs/chords/Z/Last.tex\0"sv);
    CHECK(dates.at("songs/chords/A/One.tex").uncommitted);
    CHECK(dates.at("songs/chords/A/One.tex").added == at(100));
    CHECK_FALSE(dates.at("songs/chords/A/Two.tex").uncommitted);
    const auto& untracked = dates.at("songs/chords/B/Untracked název.tex");
    CHECK(untracked.uncommitted);
    CHECK_FALSE(untracked.added);
    CHECK(dates.at("songs/chords/B/Staged.tex").uncommitted);
    CHECK(dates.at("songs/chords/C/Moved.tex").uncommitted);
    CHECK_FALSE(dates.contains("songs/chords/C/Before.tex"));
    CHECK(dates.at("songs/chords/Z/Last.tex").uncommitted);
    CHECK(dates.size() == 6);

    auto none = std::map<std::string, sd::Dates>{};
    sd::applyStatus(none, ""sv);
    sd::applyStatus(none, "x\0\0?"sv);
    CHECK(none.empty());
}

TEST_CASE("isRecent", "[songdates]") {
    const auto now = at(100 * 86400);
    sd::Dates dates;
    CHECK_FALSE(sd::isRecent(dates, now, days{30})); // unknown
    dates.uncommitted = true;
    CHECK(sd::isRecent(dates, now, days{30})); // new, not committed
    dates.added = now - days{29};
    dates.uncommitted = false;
    CHECK(sd::isRecent(dates, now, days{30}));
    dates.added = now - days{30};
    CHECK(sd::isRecent(dates, now, days{30}));
    dates.added = now - days{31};
    CHECK_FALSE(sd::isRecent(dates, now, days{30}));
    dates.uncommitted = true; // edited since, but added long ago
    CHECK_FALSE(sd::isRecent(dates, now, days{30}));
}

// Hidden: runs git on the real ../ZubuvZpevnik checkout.
TEST_CASE("every real song has a date", "[.integration]") {
    const std::filesystem::path realRepo{ZUB_REAL_SONGBOOK_DIR};
    if (!zub::validateSongbookRepo(realRepo) || !std::filesystem::exists(realRepo / ".git"))
        SKIP("no songbook git checkout at " << realRepo);
    const auto library = zub::scanLibrary(realRepo);
    REQUIRE(library);

    const auto tmp = std::filesystem::temp_directory_path();
    const auto run = [&](const zub::converter::Command& command, const char* name) {
        const auto out = tmp / name;
        std::string line = "'" + command.program + "'";
        for (const auto& argument : command.arguments)
            line += " '" + argument + "'";
        line += " > '" + out.string() + "'";
        REQUIRE(std::system(line.c_str()) == 0);
        std::ifstream in(out, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        std::filesystem::remove(out);
        return text.str();
    };
    auto dates = sd::parseLog(run(sd::logCommand(realRepo), "zub_songdates_log.txt"));
    sd::applyStatus(dates, run(sd::statusCommand(realRepo), "zub_songdates_status.txt"));

    std::vector<std::pair<sys_seconds, std::string>> newest;
    for (const auto& song : library->songs) {
        INFO(song.path.string());
        const auto key = song.path.lexically_relative(realRepo).generic_string();
        REQUIRE(dates.contains(key));
        const auto& d = dates.at(key);
        CHECK((d.added || d.uncommitted));
        if (d.added)
            newest.emplace_back(*d.added, song.inputName);
    }
    std::ranges::sort(newest, std::greater<>{});
    std::cout << library->songs.size() << " songs, " << newest.size() << " with an added date\n";
    for (std::size_t i = 0; i < std::min<std::size_t>(5, newest.size()); ++i)
        std::cout << std::chrono::floor<std::chrono::seconds>(newest[i].first) << "  "
                  << newest[i].second << '\n';
}
