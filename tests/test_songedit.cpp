#include <catch2/catch_test_macros.hpp>
#include <zub/core/songedit.hpp>

using namespace zub::songedit;

TEST_CASE("insertChord without a selection", "[songedit]") {
    const auto change = insertChord("Hello world", 5, 5, 5);
    CHECK(change.text == "Hello\\[] world");
    CHECK(change.cursor == 7);
}

TEST_CASE("insertChord with a selection", "[songedit]") {
    const auto change = insertChord("Hello world", 5, 0, 5);
    CHECK(change.text == "\\[Hello] world");
    CHECK(change.cursor == 8);
}

TEST_CASE("insertChord after UTF-8 text", "[songedit]") {
    const std::string line = "Žluťoučký kůň";
    const auto change = insertChord(line, 3, 3, 3); // after "Žl"
    CHECK(change.text == "Žl\\[]uťoučký kůň");
    CHECK(change.cursor == 5);
    const auto sel = insertChord("ať žije", 0, 0, 3);
    CHECK(sel.text == "\\[ať] žije");
    CHECK(sel.cursor == 6);
}

TEST_CASE("chordAt", "[songedit]") {
    const std::string line = "ab\\[Am]cd\\[G]\\[C]ef";
    // \[Am] = bytes 2..7, \[G] = 9..13, \[C] = 13..17
    CHECK_FALSE(chordAt(line, 0));
    CHECK_FALSE(chordAt(line, 1));
    CHECK(chordAt(line, 2)->begin == 2);
    CHECK(chordAt(line, 4)->end == 7);
    CHECK(chordAt(line, 7)->begin == 2);
    CHECK_FALSE(chordAt(line, 8));
    CHECK(chordAt(line, 9)->end == 13);
    CHECK(chordAt(line, 13)->begin == 13); // between two chords: the one that starts there
    CHECK(chordAt(line, 17)->begin == 13);
    CHECK_FALSE(chordAt(line, 18));
    CHECK_FALSE(chordAt("no chord \\[ here", 11));
    CHECK_FALSE(chordAt("\\\\[A]", 3));
}

TEST_CASE("moveChord by letter", "[songedit]") {
    auto r = moveChord("a\\[C]bc", 4, +1, Step::Letter);
    REQUIRE(r);
    CHECK(r->text == "ab\\[C]c");
    CHECK(r->cursor == 5); // still between "C" and "]"... one inside after "\["
    r = moveChord("ab\\[C]c", 4, -1, Step::Letter);
    REQUIRE(r);
    CHECK(r->text == "a\\[C]bc");
    CHECK(r->cursor == 3);
}

TEST_CASE("moveChord over Czech letters", "[songedit]") {
    // "ž" and "ě" are two bytes each
    auto r = moveChord("\\[C]žěa", 2, +1, Step::Letter);
    REQUIRE(r);
    CHECK(r->text == "ž\\[C]ěa");
    CHECK(r->cursor == 4);
    r = moveChord(r->text, r->cursor, -1, Step::Letter);
    REQUIRE(r);
    CHECK(r->text == "\\[C]žěa");
    r = moveChord("žě\\[C]a", 4, -1, Step::Letter);
    REQUIRE(r);
    CHECK(r->text == "ž\\[C]ěa");
}

TEST_CASE("moveChord jumps over a neighbour chord", "[songedit]") {
    auto r = moveChord("\\[C]\\[G]ab", 0, +1, Step::Letter);
    REQUIRE(r);
    CHECK(r->text == "\\[G]\\[C]ab");
    r = moveChord("a\\[G]\\[C]b", 7, -1, Step::Letter);
    REQUIRE(r);
    CHECK(r->text == "a\\[C]\\[G]b");
}

TEST_CASE("moveChord at the line's ends", "[songedit]") {
    CHECK_FALSE(moveChord("\\[C]ab", 0, -1, Step::Letter));
    CHECK_FALSE(moveChord("ab\\[C]", 2, +1, Step::Letter));
    CHECK_FALSE(moveChord("\\[C]ab", 0, -1, Step::Word));
    CHECK_FALSE(moveChord("ab\\[C]", 2, +1, Step::Word));
    CHECK_FALSE(moveChord("abc", 1, +1, Step::Letter)); // no chord
}

TEST_CASE("moveChord by word", "[songedit]") {
    auto r = moveChord("\\[C]hello big world", 0, +1, Step::Word);
    REQUIRE(r);
    CHECK(r->text == "hello \\[C]big world");
    r = moveChord("hel\\[C]lo big world", 3, +1, Step::Word);
    REQUIRE(r);
    CHECK(r->text == "hello \\[C]big world");
    r = moveChord("hello \\[C]big world", 6, -1, Step::Word);
    REQUIRE(r);
    CHECK(r->text == "\\[C]hello big world");
    r = moveChord("hello bi\\[C]g world", 8, -1, Step::Word);
    REQUIRE(r);
    CHECK(r->text == "hello \\[C]big world");
    // the last word: to the end
    r = moveChord("a \\[C]bc", 2, +1, Step::Word);
    REQUIRE(r);
    CHECK(r->text == "a bc\\[C]");
}

TEST_CASE("moveChord by word over Czech text, apostrophes and chords", "[songedit]") {
    auto r = moveChord("\\[C]žluťoučký kůň", 0, +1, Step::Word);
    REQUIRE(r);
    CHECK(r->text == "žluťoučký \\[C]kůň");
    r = moveChord("a \\[G]don't \\[C]go", 14, -1, Step::Word);
    REQUIRE(r);
    CHECK(r->text == "a \\[G]\\[C]don't go");
    r = moveChord("\\[C]ab \\[G]cd ef", 0, +1, Step::Word);
    REQUIRE(r);
    CHECK(r->text == "ab \\[C]\\[G]cd ef");
}

TEST_CASE("moveChord keeps the cursor inside the chord", "[songedit]") {
    const auto r = moveChord("a\\[Am]bc", 5, +1, Step::Letter); // between "m" and "]"
    REQUIRE(r);
    CHECK(r->text == "ab\\[Am]c");
    CHECK(r->cursor == 6);
}

TEST_CASE("partSnippet environments", "[songedit]") {
    struct Case {
        Part part;
        const char* begin;
        const char* end;
    };
    const Case cases[] = {
        {Part::Verse, "\\beginverse", "\\endverse"},
        {Part::VerseUnnumbered, "\\beginverse*", "\\endverse"},
        {Part::Chorus, "\\beginchorus", "\\endchorus"},
        {Part::PreChorus, "\\beginprechorus", "\\endprechorus"},
        {Part::Bridge, "\\beginbridge", "\\endbridge"},
        {Part::Intro, "\\beginintro", "\\endintro"},
        {Part::Outro, "\\beginoutro", "\\endoutro"},
        {Part::Instrumental, "\\begininstrumental", "\\endinstrumental"},
    };
    for (const auto& c : cases) {
        const std::string begin = c.begin;
        const std::string end = c.end;
        const auto empty = partSnippet(c.part, "");
        CHECK(empty.text == begin + "\n\n" + end + "\n");
        CHECK(empty.cursor == begin.size() + 1);
        const auto sel = partSnippet(c.part, "line 1\nline 2");
        CHECK(sel.text == begin + "\nline 1\nline 2\n" + end + "\n");
        CHECK(sel.cursor == begin.size() + 1 + 14);
        const auto selNl = partSnippet(c.part, "line\n");
        CHECK(selNl.text == begin + "\nline\n" + end + "\n");
    }
}

TEST_CASE("partSnippet repeats and header", "[songedit]") {
    CHECK(partSnippet(Part::ChorusAgain, "").text == "\\insertchorus\n");
    CHECK(partSnippet(Part::ChorusAgain, "").cursor == 14);
    CHECK(partSnippet(Part::ChorusReminder, "x").text == "\\insertchorus*\n");
    CHECK(partSnippet(Part::ChorusReminder, "").cursor == 15);

    const auto header = partSnippet(Part::SongHeader, "");
    CHECK(header.text.starts_with("\\beginsong{}[\n    by={},\n    lang={czech},\n]\n\n"));
    CHECK(header.text.ends_with("\\endsong\n"));
    CHECK(header.text.substr(header.cursor - 1, 2) == "{}");
    const auto withText = partSnippet(Part::SongHeader, "\\beginverse\n\\endverse");
    CHECK(withText.text.find("]\n\n\\beginverse\n\\endverse\n\\endsong\n") != std::string::npos);
}
