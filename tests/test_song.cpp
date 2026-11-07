#include <catch2/catch_test_macros.hpp>
#include <zub/core/song.hpp>

using zub::parseOptions;
using zub::parseSong;

TEST_CASE("parseSong reads title and options", "[song]") {
    const auto song = parseSong("\\beginsong{Franky Dlouhán}[\n"
                                "    by={Brontosauři},\n"
                                "    lang={czech},\n"
                                "    key={E\\flt},\n"
                                "]\n\\beginverse\n\\endverse\n\\endsong\n");
    REQUIRE(song);
    CHECK(song->title == "Franky Dlouhán");
    CHECK(song->value("by") == "Brontosauři");
    CHECK(song->value("lang") == "czech");
    CHECK(song->value("key") == "E\\flt");
    CHECK(song->value("tem").empty());
}

TEST_CASE("parseSong handles nested braces in the title", "[song]") {
    const auto song =
        parseSong("\\beginsong{E=mc\\texorpdfstring{\\textsuperscript{2}}{2}}[by={Ayreon}]");
    REQUIRE(song);
    CHECK(song->title == "E=mc\\texorpdfstring{\\textsuperscript{2}}{2}");
    CHECK(song->value("by") == "Ayreon");
}

TEST_CASE("parseSong ignores commented-out keys and trailing comments", "[song]") {
    const auto song = parseSong("\\beginsong{T}[\n"
                                "    lang={czech}, % TODO: And quenya\n"
                                "    % note={a, b = c},\n"
                                "    by={X},\n"
                                "]");
    REQUIRE(song);
    CHECK(song->keys.size() == 2);
    CHECK(song->value("lang") == "czech");
    CHECK_FALSE(song->keys.contains("note"));
}

TEST_CASE("parseSong without options or with whitespace before them", "[song]") {
    const auto bare = parseSong("\\beginsong{Only Title}\n\\beginverse");
    REQUIRE(bare);
    CHECK(bare->title == "Only Title");
    CHECK(bare->keys.empty());

    const auto spaced = parseSong("\\beginsong{T}  \n  [by={X}]");
    REQUIRE(spaced);
    CHECK(spaced->value("by") == "X");
}

TEST_CASE("parseSong reports broken headers", "[song]") {
    CHECK_FALSE(parseSong("\\beginverse la \\endverse"));
    CHECK_FALSE(parseSong("% \\beginsong{Commented}[by={X}]\n"));
    CHECK_FALSE(parseSong("\\beginsong"));
    CHECK_FALSE(parseSong("\\beginsong{Unclosed title[by={X}]"));
    CHECK_FALSE(parseSong("\\beginsong{T}[by={X}"));
    CHECK(parseSong("").error() == "no \\beginsong");
}

TEST_CASE("parseOptions edge cases", "[song]") {
    SECTION("commas and brackets inside braces stay in the value") {
        const auto keys = parseOptions("by={Howard Shore, The Lord\\ of\\ the\\ Rings}, "
                                       "chr={ \\gtab{C}{X32010:032010} }");
        CHECK(keys.at("by") == "Howard Shore, The Lord\\ of\\ the\\ Rings");
        CHECK(keys.at("chr") == "\\gtab{C}{X32010:032010}");
    }
    SECTION("unbraced values run to the next comma") {
        const auto keys = parseOptions("tem=120, met = 4/4 ,");
        CHECK(keys.at("tem") == "120");
        CHECK(keys.at("met") == "4/4");
    }
    SECTION("last duplicate wins, trailing commas are fine") {
        const auto keys = parseOptions("key={C}, key={D},,,");
        CHECK(keys.size() == 1);
        CHECK(keys.at("key") == "D");
    }
    SECTION("entries without '=' are skipped") {
        const auto keys = parseOptions("garbage, by={X}");
        CHECK(keys.size() == 1);
        CHECK(keys.at("by") == "X");
    }
    SECTION("unclosed brace takes the rest") {
        const auto keys = parseOptions("by={X");
        CHECK(keys.at("by") == "X");
    }
}
