#include <catch2/catch_test_macros.hpp>
#include <zub/core/wishlist.hpp>

using namespace zub::wishlist;

TEST_CASE("wishlist reads wishes, skipping comments and blank lines", "[wishlist]") {
    const std::string text = "# Songs to add\r\n\r\nPavelDobes/NecoOLasce na tábor\r\n"
                             "LodJohnB\r\n  Simon&Garfunkel/SoundOfSilence  \r\nPíseň o zemi";
    const auto list = parse(text);
    REQUIRE(list.size() == 4);
    CHECK(list[0].text == "PavelDobes/NecoOLasce na tábor");
    CHECK(list[0].name == "PavelDobes/NecoOLasce");
    CHECK(list[0].line == 3);
    CHECK(list[1].name.empty());
    CHECK(list[2].text == "Simon&Garfunkel/SoundOfSilence");
    CHECK(list[3].text == "Píseň o zemi");
    CHECK(list[3].line == 6);
    CHECK(parse("").empty());
}

TEST_CASE("wishlist adds a line in the file's line breaks", "[wishlist]") {
    CHECK(add("", "A/B").value() == "A/B\n");
    CHECK(add("# x\r\nA/B\r\n", " C/D ").value() == "# x\r\nA/B\r\nC/D\r\n");
    CHECK(add("A/B", "C/D").value() == "A/B\nC/D\n"); // the missing final line break first
    CHECK_FALSE(add("", "  "));
    CHECK_FALSE(add("", "a\nb"));
    CHECK_FALSE(add("", "# not a wish"));
}

TEST_CASE("wishlist removes a wish only where it still is", "[wishlist]") {
    const std::string text = "# x\r\nA/B\r\nC/D note\r\nE";
    CHECK(remove(text, 3, "C/D note").value() == "# x\r\nA/B\r\nE");
    CHECK(remove(text, 4, "E").value() == "# x\r\nA/B\r\nC/D note\r\n");
    CHECK_FALSE(remove(text, 2, "C/D note")); // the file changed
    CHECK_FALSE(remove(text, 1, "# x"));      // a comment
    CHECK_FALSE(remove(text, 9, "A/B"));
}

TEST_CASE("wishlist keeps a byte order mark and skips blank lines", "[wishlist]") {
    const std::string bom = "\xEF\xBB\xBF";
    const std::string text = bom + "A/B\n   \nC/D";
    const auto list = parse(text);
    REQUIRE(list.size() == 2);
    CHECK(list[0].text == "A/B");
    CHECK(list[0].name == "A/B");
    CHECK(remove(text, 1, "A/B").value() == bom + "   \nC/D");
    CHECK(remove(text, 3, "C/D").value() == bom + "A/B\n   \n");
    CHECK_FALSE(remove(text, 2, ""));
}
