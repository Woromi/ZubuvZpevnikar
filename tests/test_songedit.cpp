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

TEST_CASE("removeScoreReference removes a command alone on its line", "[songedit]") {
    const std::string text = "A\n  \\lilypondfile{Artist/X.ly}  \nB\n";
    CHECK(removeScoreReference(text, "Artist/X.ly") == "A\nB\n");
}

TEST_CASE("removeScoreReference removes only the command inline", "[songedit]") {
    CHECK(removeScoreReference("Text \\lilypondfile{Artist/X.ly} more\n", "Artist/X.ly") ==
          "Text  more\n");
}

TEST_CASE("removeScoreReference handles options and narrow", "[songedit]") {
    CHECK(removeScoreReference("A\n\\lilypondfile[width=5cm]{Artist/X.ly}\nB\n", "Artist/X.ly") ==
          "A\nB\n");
    CHECK(removeScoreReference("A\n\\narrowlilypondfile{Artist/X.ly}\nB\n", "Artist/X.ly") ==
          "A\nB\n");
}

TEST_CASE("removeScoreReference removes several and keeps others", "[songedit]") {
    const std::string text = "\\lilypondfile{Artist/X.ly}\n\\lilypondfile{Artist/"
                             "Y.ly}\n\\narrowlilypondfile{Artist/X.ly}";
    CHECK(removeScoreReference(text, "Artist/X.ly") == "\\lilypondfile{Artist/Y.ly}");
    CHECK(removeScoreReference(text, "Artist/Z.ly") == text);
    CHECK(removeScoreReference("\\lilypondfile{Artist/X2.ly}\n", "Artist/X.ly") ==
          "\\lilypondfile{Artist/X2.ly}\n");
}

TEST_CASE("removeScoreReference at the end without a line break", "[songedit]") {
    CHECK(removeScoreReference("A\n\\lilypondfile{Artist/X.ly}", "Artist/X.ly") == "A");
}

TEST_CASE("removeScoreReference leaves comments alone", "[songedit]") {
    const std::string text = "% \\lilypondfile{Artist/X.ly}\n";
    CHECK(removeScoreReference(text, "Artist/X.ly") == text);
}

TEST_CASE("setScoreWidth switches both ways", "[songedit]") {
    CHECK(setScoreWidth("A\n\\lilypondfile{Artist/X.ly}\nB\n", "Artist/X.ly", true) ==
          "A\n\\narrowlilypondfile{Artist/X.ly}\nB\n");
    CHECK(setScoreWidth("\\narrowlilypondfile{Artist/X.ly}", "Artist/X.ly", false) ==
          "\\lilypondfile{Artist/X.ly}");
}

TEST_CASE("setScoreWidth keeps options, other files and no-ops", "[songedit]") {
    const std::string text =
        "\\lilypondfile[width=5cm]{Artist/X.ly}\n\\lilypondfile{Artist/Y.ly}\n";
    CHECK(setScoreWidth(text, "Artist/X.ly", true) ==
          "\\narrowlilypondfile[width=5cm]{Artist/X.ly}\n\\lilypondfile{Artist/Y.ly}\n");
    CHECK(setScoreWidth(text, "Artist/X.ly", false) == text);
    CHECK(setScoreWidth("% \\lilypondfile{Artist/X.ly}\n", "Artist/X.ly", true) ==
          "% \\lilypondfile{Artist/X.ly}\n");
}

TEST_CASE("scoreIsNarrow reports the first reference", "[songedit]") {
    const std::string text = "\\narrowlilypondfile{Artist/X.ly}\n\\lilypondfile{Artist/X.ly}\n";
    CHECK(scoreIsNarrow(text, "Artist/X.ly") == true);
    CHECK(scoreIsNarrow("\\lilypondfile{Artist/X.ly}", "Artist/X.ly") == false);
    CHECK_FALSE(scoreIsNarrow(text, "Artist/Z.ly").has_value());
}

TEST_CASE("insertChord inside a chord goes after it", "[songedit]") {
    const std::string line = "ab\\[Am]cd"; // chord = bytes 2..7
    for (const std::size_t cursor : {3U, 4U, 6U}) {
        const auto change = insertChord(line, cursor, cursor, cursor);
        CHECK(change.text == "ab\\[Am]\\[]cd");
        CHECK(change.cursor == 9);
    }
    const auto selected = insertChord(line, 6, 4, 6); // a selection inside the chord is ignored
    CHECK(selected.text == "ab\\[Am]\\[]cd");
    CHECK(selected.cursor == 9);
    CHECK(insertChord(line, 2, 2, 2).text == "ab\\[]\\[Am]cd"); // at its start: before it
    CHECK(insertChord(line, 7, 7, 7).text == "ab\\[Am]\\[]cd"); // at its end
}

TEST_CASE("insertMultivoice", "[songedit]") {
    const auto plain = insertMultivoice("Víno na ku", 3, 3, 3); // after "Ví"
    CHECK(plain.text == "Ví{\\multivoice{}{}}no na ku");
    CHECK(plain.cursor == 3 + 13);

    const auto selected = insertMultivoice("Víno na ku", 0, 0, 5); // "Víno"
    CHECK(selected.text == "{\\multivoice{Víno}{}} na ku");
    CHECK(selected.cursor == 13 + 5 + 2);

    const std::string line = "\\[Am]ab"; // chord = 0..5
    const auto inChord = insertMultivoice(line, 3, 3, 3);
    CHECK(inChord.text == "\\[Am]{\\multivoice{}{}}ab");
    CHECK(inChord.cursor == 5 + 13);
    const auto selInChord = insertMultivoice(line, 4, 2, 4);
    CHECK(selInChord.text == "\\[Am]{\\multivoice{}{}}ab");

    // a selection with unbalanced braces would break the command: it is ignored
    const auto unbalanced = insertMultivoice("{\\role{Petr} Ah", 9, 7, 15); // "Petr} Ah"
    CHECK(unbalanced.text == "{\\role{Pe{\\multivoice{}{}}tr} Ah");
    CHECK(unbalanced.cursor == 9 + 13);
    const auto balanced = insertMultivoice("a {b} c", 0, 2, 5); // "{b}"
    CHECK(balanced.text == "a {\\multivoice{{b}}{}} c");
}

TEST_CASE("insertRole", "[songedit]") {
    const auto plain = insertRole("Žluťoučký kůň  ", 0, 0, 0);
    CHECK(plain.text == "Žluťoučký kůň \\role{}");
    CHECK(plain.cursor == plain.text.size() - 1);

    const auto empty = insertRole("  ", 0, 0, 0);
    CHECK(empty.text == "\\role{}");
    CHECK(empty.cursor == 6);

    const auto selected = insertRole("Ayreon text", 0, 0, 6);
    CHECK(selected.text == "Ayreon text \\role{Ayreon}");
    CHECK(selected.cursor == selected.text.size() - 1);

    const auto unbalanced = insertRole("a { b", 0, 2, 3);
    CHECK(unbalanced.text == "a { b \\role{}");

    const std::string has = "dark \\role{Ayreon}";
    const auto existing = insertRole(has, 0, 0, 0);
    CHECK(existing.text == has);
    CHECK(existing.cursor == 11);
    const std::string two = "dark \\role{A}{B}  ";
    const auto existing2 = insertRole(two, 0, 0, 0);
    CHECK(existing2.text == two);
    CHECK(existing2.cursor == 11);
    // a role in the middle is not "at the end"
    CHECK(insertRole("\\role{A} dál", 0, 0, 0).text == "\\role{A} dál \\role{}");
}

TEST_CASE("removeElementAt a chord", "[songedit]") {
    const std::string line = "ab\\[Am]cd"; // 2..7
    for (const std::size_t cursor : {2U, 4U, 7U}) {
        const auto change = removeElementAt(line, cursor);
        REQUIRE(change);
        CHECK(change->text == "abcd");
        CHECK(change->cursor == 2);
    }
    CHECK_FALSE(removeElementAt(line, 1));
    CHECK_FALSE(removeElementAt(line, 8));
    CHECK_FALSE(removeElementAt("plain text", 3));
    // two chords that meet: the one starting at the cursor
    CHECK(removeElementAt("\\[A]\\[B]", 4)->text == "\\[A]");
}

TEST_CASE("removeElementAt a role", "[songedit]") {
    const auto one = removeElementAt("dark \\role{Ayreon}", 12);
    REQUIRE(one);
    CHECK(one->text == "dark");
    CHECK(one->cursor == 4);
    const auto two = removeElementAt("ř \\role{A}{B}", 13);
    REQUIRE(two);
    CHECK(two->text == "ř");
    CHECK(two->cursor == 2);
    CHECK(removeElementAt("\\role{A} x", 0)->text == " x"); // no space before it
    CHECK_FALSE(removeElementAt("ab \\role{A}", 1));
}

TEST_CASE("removeElementAt a multivoice", "[songedit]") {
    const std::string line = "\\[Am]{\\multivoice{Víno na ku}{Víno}}\\[C]x";
    const auto change = removeElementAt(line, 10);
    REQUIRE(change);
    CHECK(change->text == "\\[Am]Víno na ku\\[C]x");
    CHECK(change->cursor == 5);
    // just inside the outer braces
    CHECK(removeElementAt(line, 6)->text == "\\[Am]Víno na ku\\[C]x");
    // without the outer braces
    const auto bare = removeElementAt("a \\multivoice{A}{B} b", 5);
    REQUIRE(bare);
    CHECK(bare->text == "a A b");
    CHECK(bare->cursor == 2);
}

TEST_CASE("removeElementAt takes the innermost element", "[songedit]") {
    const std::string line = "{\\multivoice{a\\[Dm]b}{c}}";
    const auto chord = removeElementAt(line, 17); // inside the chord
    REQUIRE(chord);
    CHECK(chord->text == "{\\multivoice{ab}{c}}");
    CHECK(chord->cursor == 14);
    const auto voice = removeElementAt(line, 15 + 8); // after the chord: the multivoice
    REQUIRE(voice);
    CHECK(voice->text == "a\\[Dm]b");
}
