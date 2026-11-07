#include <catch2/catch_test_macros.hpp>
#include <zub/core/latex.hpp>

using namespace zub::latex;

TEST_CASE("stripComments removes comments but keeps escaped percent and newlines", "[latex]") {
    CHECK(stripComments("a % comment\nb") == "a \nb");
    CHECK(stripComments("100\\% sure % really") == "100\\% sure ");
    CHECK(stripComments("% whole line\n% another\nx") == "\n\nx");
    CHECK(stripComments("no comment") == "no comment");
    CHECK(stripComments("trailing %") == "trailing ");
    CHECK(stripComments("ends with backslash\\") == "ends with backslash\\");
}

TEST_CASE("findMatching handles nesting and escapes", "[latex]") {
    CHECK(findMatching("{a{b}c}", 0) == 6u);
    CHECK(findMatching("{a\\}b}", 0) == 5u);
    CHECK(findMatching("[x={[y]}]", 0) == 8u);
    CHECK_FALSE(findMatching("{unbalanced", 0));
    CHECK_FALSE(findMatching("abc", 0));
    CHECK_FALSE(findMatching("", 0));
    CHECK_FALSE(findMatching("{}", 5));
}

TEST_CASE("toDisplayText turns metadata TeX into plain text", "[latex]") {
    CHECK(toDisplayText("Green\\ Day") == "Green Day");
    CHECK(toDisplayText("The Mamas\\ \\&\\ the\\ Papas") == "The Mamas & the Papas");
    CHECK(toDisplayText("E\\flt") == "E♭");
    CHECK(toDisplayText("C\\shrp m") == "C♯m");
    CHECK(toDisplayText("B\\flt m\\rightarrow G\\flt") == "B♭m→G♭");
    CHECK(toDisplayText("E=mc\\texorpdfstring{\\textsuperscript{2}}{2}") == "E=mc2");
    CHECK(toDisplayText("  Bodláky  ve\n vlasech ") == "Bodláky ve vlasech");
    CHECK(toDisplayText("\\textbf{bold} text") == "bold text");
    CHECK(toDisplayText("a~b") == "a b");
    CHECK(toDisplayText("") == "");
}

TEST_CASE("toDisplayText control symbols", "[latex]") {
    CHECK(toDisplayText("a\\,b") == "a b");
    CHECK(toDisplayText("a\\\\b") == "a b");
    CHECK(toDisplayText("hy\\-phen") == "hyphen");
    CHECK(toDisplayText("100\\%") == "100%");
    CHECK(toDisplayText("\\#1 \\$5 a\\_b \\{x\\}") == "#1 $5 a_b {x}");
    CHECK(toDisplayText("caf\\'e") == "cafe");
}
