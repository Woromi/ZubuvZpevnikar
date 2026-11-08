#include <catch2/catch_test_macros.hpp>
#include <string>
#include <zub/core/songtodo.hpp>

namespace todo = zub::todo;

TEST_CASE("TODO notes are read from comments", "[todo]") {
    const std::string song = "\\beginsong{T}[by=X]\n"
                             "% TODO: Zkontrolovat akordy\n"
                             "  %TODO Solo\n"
                             "\\beginverse\n"
                             "la la % TODO: second voice\n"
                             "50 \\% TODO: not a comment\n"
                             "% todo: lower case is not one\n"
                             "% TODO\n"
                             "%% TODO:   spaces   \r\n"
                             "\\endverse\n\\endsong\n";
    CHECK(todo::notes(song) == std::vector<std::string>{"Zkontrolovat akordy", "Solo",
                                                        "second voice", "TODO", "spaces"});
    CHECK(todo::notes("\\beginsong{T}[by=X]\n\\endsong\n").empty());
}

TEST_CASE("the not-checked mark goes under the header and comes off again", "[todo]") {
    const std::string song = "\\beginsong{Letter to Dana}[\n"
                             "    by={Sonata Arctica},\n"
                             "    key=Dm]\n"
                             "\n"
                             "\\beginverse\nla\n\\endverse\n\\endsong\n";
    CHECK_FALSE(todo::hasNotCheckedMark(song));
    const std::string marked = todo::addNotCheckedMark(song);
    CHECK(marked == "\\beginsong{Letter to Dana}[\n"
                    "    by={Sonata Arctica},\n"
                    "    key=Dm]\n"
                    "% TODO: not checked yet (imported by the app)\n"
                    "\n"
                    "\\beginverse\nla\n\\endverse\n\\endsong\n");
    CHECK(todo::hasNotCheckedMark(marked));
    CHECK(todo::notes(marked) == std::vector<std::string>{"not checked yet (imported by the app)"});
    CHECK(todo::addNotCheckedMark(marked) == marked); // once
    CHECK(todo::removeNotCheckedMark(marked) == song);
    CHECK(todo::removeNotCheckedMark(song) == song);
}

TEST_CASE("the not-checked mark keeps CRLF and skips a commented header", "[todo]") {
    const std::string crlf = "\\beginsong{T}[by=X]\r\n\\beginverse\r\n\\endverse\r\n\\endsong\r\n";
    const std::string marked = todo::addNotCheckedMark(crlf);
    CHECK(marked == "\\beginsong{T}[by=X]\r\n% TODO: not checked yet (imported by the app)\r\n"
                    "\\beginverse\r\n\\endverse\r\n\\endsong\r\n");
    CHECK(todo::removeNotCheckedMark(marked) == crlf);

    const std::string commented = "% \\beginsong{Old}[by=Y]\n\\beginsong{T}[by=X]\n\\endsong\n";
    CHECK(todo::addNotCheckedMark(commented) ==
          "% \\beginsong{Old}[by=Y]\n\\beginsong{T}[by=X]\n"
          "% TODO: not checked yet (imported by the app)\n\\endsong\n");

    // the header on the last line, no newline after it
    CHECK(todo::addNotCheckedMark("\\beginsong{T}[by=X]") ==
          "\\beginsong{T}[by=X]\n% TODO: not checked yet (imported by the app)\n");
    // no header: first
    CHECK(todo::addNotCheckedMark("text\n") ==
          "% TODO: not checked yet (imported by the app)\ntext\n");
}
