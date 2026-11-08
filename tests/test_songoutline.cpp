#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <zub/core/songoutline.hpp>
#include <zub/core/songsuggest.hpp>

namespace fs = std::filesystem;
namespace so = zub::songoutline;
using so::Chords;
using so::Repeat;

namespace {

const fs::path converterData = fs::path(ZUB_TEST_DATA_DIR) / "converter";

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// "Verse 1|Chorus 1 full|Verse 2 off|..." for compact checks
std::string summary(const so::Outline& outline) {
    std::string result;
    for (const auto& part : outline.parts) {
        result += part.name;
        if (part.repeat == Repeat::Full)
            result += " full";
        if (part.repeat == Repeat::Reminder)
            result += " reminder";
        if (part.chords == Chords::Off)
            result += " off";
        if (part.chords == Chords::OffOnSmallPage)
            result += " offsmall";
        if (part.chordsOffPartWay)
            result += " someoff";
        result += '|';
    }
    return result;
}

} // namespace

TEST_CASE("outline names and numbers the parts", "[songoutline]") {
    const std::string song = "\\beginsong{T}[by=X]\n"
                             "\\beginverse\n\\[C]la\n\\endverse\n"
                             "\\beginchorus\n\\[G]re\n\\endchorus\n"
                             "\\beginverse\\chordsoff\nla\n\\endverse\n"
                             "\\insertchorus\n"
                             "\\beginbridge\nb\n\\endbridge\n"
                             "% \\beginverse in a comment\n"
                             "\\beginverse\n\\chordsoffonsmallpage\nla\n\\endverse\n"
                             "\\insertchorus*\n"
                             "\\insertchorus[][\\chordsoff]\n"
                             "\\begininstrumental\\usechords{verse}\n\\endinstrumental\n"
                             "\\endsong\n";
    const auto outline = so::outline(song);
    CHECK(summary(outline) ==
          "Verse 1|Chorus 1|Verse 2 off|Chorus 2 full|Bridge 1|Verse 3 offsmall|"
          "Chorus 3 reminder off|Chorus 4 full off|Instrumental 1|");
    REQUIRE(outline.parts.size() == 9);
    CHECK(outline.parts[0].line == 2);
    CHECK(outline.parts[3].line == 11);
    CHECK(song.substr(outline.parts[0].begin, outline.parts[0].end - outline.parts[0].begin) ==
          "\\beginverse\n\\[C]la\n\\endverse");
    CHECK(song.substr(outline.parts[3].begin, outline.parts[3].end - outline.parts[3].begin) ==
          "\\insertchorus");
    CHECK_FALSE(outline.smallerOnSmallPage);
}

TEST_CASE("outline: chords off part way, song-wide settings, numbered choruses", "[songoutline]") {
    const std::string song = "\\beginsong{T}[by=X]\n\\smalleronsmallpage\n"
                             "\\beginverse\n\\[C]la\n\\chordsoff\nla\n\\endverse\n"
                             "\\beginverseA\nx\n\\endverseA\n"
                             "\\beginchorus[1]\na\n\\endchorus\n"
                             "\\beginchorus[2]\nb\n\\endchorus\n"
                             "\\insertchorus*[1]\n\\insertchorus\n"
                             "\\chordsoff\n"
                             "\\beginverse\nla\n\\endverse\n"
                             "\\endsong\n";
    const auto outline = so::outline(song);
    CHECK(outline.smallerOnSmallPage);
    CHECK(summary(outline) == "Verse 1 someoff|verseA 1|Chorus R1 (1)|Chorus R2 (1)|"
                              "Chorus R1 (2) reminder off|Chorus R2 (2) full|Verse 2 off|");
}

TEST_CASE("applyEdits", "[songoutline]") {
    using E = zub::songsuggest::Edit;
    const std::vector<E> edits{{4, 5, "X"}, {0, 0, "<"}, {9, 9, ">"}};
    CHECK(so::applyEdits("abcdefghi", edits) == "<abcdXfghi>");
    CHECK_FALSE(so::applyEdits("abc", std::vector<E>{{2, 4, ""}}));
    CHECK_FALSE(so::applyEdits("abcdef", std::vector<E>{{1, 3, ""}, {2, 4, ""}}));
}

TEST_CASE("preview marks the parts a real proposal changes", "[songoutline]") {
    const std::string song = slurp(converterData / "suggest-input.tex");
    const auto suggestions =
        zub::songsuggest::interpretSuggest(0, slurp(converterData / "suggest.json"), "");
    REQUIRE(suggestions);
    REQUIRE(suggestions->proposals.size() == 2);

    const auto before = so::outline(song);
    CHECK(summary(before) == "Verse 1 someoff|Pre-chorus 1|Chorus 1|Instrumental 1|"
                             "Verse 2 someoff|Pre-chorus 2|Chorus 2|Instrumental 2|"
                             "Pre-chorus 3|Chorus 3|");

    // chordsoffonsmallpage@50: the 2nd pre-chorus (line 50) loses its chords on small paper
    const auto chordsOff = so::preview(song, suggestions->proposals[0].edits);
    REQUIRE(chordsOff);
    CHECK_FALSE(chordsOff->songChanged);
    REQUIRE(chordsOff->changed.size() == 10);
    for (std::size_t i = 0; i < chordsOff->changed.size(); ++i)
        CHECK(chordsOff->changed[i] == (i == 5));
    CHECK(chordsOff->after.parts[5].chords == Chords::OffOnSmallPage);
    CHECK(chordsOff->after.parts[5].line == 50);

    // smalleronsmallpage@18: song-wide, no part touched
    const auto smaller = so::preview(song, suggestions->proposals[1].edits);
    REQUIRE(smaller);
    CHECK(smaller->songChanged);
    CHECK(smaller->after.smallerOnSmallPage);
    // \smalleronsmallpage and a blank line go in before line 18: lines stay the editor's
    CHECK(smaller->after.parts[0].line == 18);
    CHECK(smaller->after.parts[9].line == 71);
    CHECK(std::ranges::none_of(smaller->changed, [](bool b) { return b; }));
}

TEST_CASE("preview of a chorus shortened to a reminder", "[songoutline]") {
    const std::string song = "\\beginsong{T}[by=X]\n"
                             "\\beginchorus\na\n\\endchorus\n"
                             "\\beginverse\nv\n\\endverse\n"
                             "\\beginchorus\na\n\\endchorus\n"
                             "\\endsong\n";
    const auto begin = song.rfind("\\beginchorus");
    const auto end = song.rfind("\\endchorus") + 10;
    const std::vector<zub::songsuggest::Edit> edits{{begin, end, "\\insertchorus*"}};
    const auto result = so::preview(song, edits);
    REQUIRE(result);
    CHECK(summary(result->after) == "Chorus 1|Verse 1|Chorus 2 reminder off|");
    CHECK(result->changed == std::vector<bool>{false, false, true});
    CHECK_FALSE(result->songChanged);
}

TEST_CASE("outline: comments, \\chordson, an unterminated part", "[songoutline]") {
    const std::string song = "\\beginsong{T}[by=X]\n"
                             "\\beginverse\\chordsoff 100\\% sure % \\beginchorus\n\\endverse\n"
                             "\\chordsoff\n"
                             "\\beginverse\\chordson\nla\n\\endverse\n"
                             "\\beginbridge\nno end\n"
                             "\\beginverse\nla \\\\% \\beginchorus\n\\endverse\n"
                             "\\endsong\n";
    const auto outline = so::outline(song);
    CHECK(summary(outline) == "Verse 1 off|Verse 2|Bridge 1 off|Verse 3 off|");
    REQUIRE(outline.parts.size() == 4);
    CHECK(song.substr(outline.parts[2].begin, outline.parts[2].end - outline.parts[2].begin) ==
          "\\beginbridge\nno end\n");
}

TEST_CASE("preview: a deletion right before a part, joined lines, bad edits", "[songoutline]") {
    const std::string song = "\\beginsong{T}[by=X]\n"
                             "\\beginverse\na\nb\n\\endverse\n"
                             "\n\n"
                             "\\beginverse\nc\n\\endverse\n"
                             "\\endsong\n";
    using E = zub::songsuggest::Edit;
    const auto blank = song.find("\n\n\n") + 1; // the two blank lines' newlines
    const auto deleted = so::preview(song, std::vector<E>{{blank, blank + 2, ""}});
    REQUIRE(deleted);
    CHECK(deleted->after.parts[1].line == 8); // the editor's line, not 6
    CHECK(deleted->changed == std::vector<bool>{false, false});
    CHECK(deleted->songChanged);
    CHECK_FALSE(deleted->smallerChanged);

    // join "a" and "b" of verse 1: verse 2 keeps its editor line
    const auto newline = song.find("a\nb") + 1;
    const auto joined = so::preview(song, std::vector<E>{{newline, newline + 1, " "}});
    REQUIRE(joined);
    CHECK(joined->changed == std::vector<bool>{true, false});
    CHECK(joined->after.parts[1].line == 8);
    CHECK_FALSE(joined->songChanged);

    CHECK_FALSE(so::preview(song, std::vector<E>{{song.size(), song.size() + 1, ""}}));
}

TEST_CASE("preview: chords off between parts marks the parts after it", "[songoutline]") {
    const std::string song = "\\beginsong{T}[by=X]\n"
                             "\\beginverse\na\n\\endverse\n"
                             "\\beginverse\nb\n\\endverse\n"
                             "\\endsong\n";
    const auto at = song.find("\\beginverse\nb");
    const auto result =
        so::preview(song, std::vector<zub::songsuggest::Edit>{{at, at, "\\chordsoff\n"}});
    REQUIRE(result);
    CHECK(result->changed == std::vector<bool>{false, true});
    CHECK(result->songChanged);
}

TEST_CASE("applyEdits keeps the order of insertions at one place", "[songoutline]") {
    using E = zub::songsuggest::Edit;
    CHECK(so::applyEdits("ab", std::vector<E>{{1, 1, "X"}, {1, 1, "Y"}, {0, 1, "_"}}) == "_XYb");
}

// Hidden: runs against the real ../ZubuvZpevnik checkout.
// Run with: build/debug/tests/zub_core_tests "[integration]"
TEST_CASE("outline reads every real song", "[.integration]") {
    const fs::path songs = fs::path{ZUB_REAL_SONGBOOK_DIR} / "songs" / "chords";
    if (!fs::is_directory(songs))
        SKIP("no songbook checkout at " << ZUB_REAL_SONGBOOK_DIR);
    int count = 0;
    for (const auto& entry : fs::recursive_directory_iterator(songs)) {
        if (entry.path().extension() != ".tex")
            continue;
        ++count;
        const std::string song = slurp(entry.path());
        const auto outline = so::outline(song);
        INFO(entry.path().string() << "\n" << summary(outline));
        CHECK_FALSE(outline.parts.empty());
        std::size_t previous = 0;
        for (const auto& part : outline.parts) {
            CHECK(part.begin >= previous);
            CHECK(part.end > part.begin);
            CHECK(part.end <= song.size());
            CHECK(part.name.find('\\') == std::string::npos);
            previous = part.end;
        }
    }
    CHECK(count >= 200);
}
