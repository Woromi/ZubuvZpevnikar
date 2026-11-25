#include <catch2/catch_test_macros.hpp>
#include <zub/core/scorecache.hpp>

using namespace zub::scorecache;

TEST_CASE("scorecache: the scores a build used, from its log", "[scorecache]") {
    // As lyluatex writes it, the lines broken by TeX at max_print_line.
    const std::string log =
        "Module lyluatex Info: Compiling score tmp-ly/6a98dcfd07cdb2219f2e34fffa662cb6 wi\n"
        "th LilyPond executable 'lilypond'. on input line 16\n"
        "Package epstopdf Info: Source file: <tmp-ly/6a98dcfd07cdb2219f2e34fffa662cb6-1.\n"
        "eps>\n"
        "Module lyluatex Info: Compiling score tmp-ly/1a3d62675ec021fe5f95964696f9\r\n"
        "e4bd with LilyPond executable 'lilypond'.\n"
        "tmp-ly/notahash and tmp-ly/0123\n";
    CHECK(usedScores(log) == std::set<std::string>{"1a3d62675ec021fe5f95964696f9e4bd",
                                                   "6a98dcfd07cdb2219f2e34fffa662cb6"});
    CHECK(usedScores("").empty());
    CHECK(usedScores("tmp-ly/").empty());
}

TEST_CASE("scorecache: the score a cache file belongs to", "[scorecache]") {
    const std::string hash = "aa303ae2cec5df21c92f8a80e2d99da6";
    CHECK(scoreOf(hash + ".pdf") == hash);
    CHECK(scoreOf(hash + "-1.eps") == hash);
    CHECK(scoreOf(hash + "-1-eps-converted-to.pdf") == hash);
    CHECK(scoreOf(hash) == hash);
    CHECK_FALSE(scoreOf(hash + "x.pdf"));
    CHECK_FALSE(scoreOf("readme.txt"));
    CHECK_FALSE(scoreOf("AA303AE2CEC5DF21C92F8A80E2D99DA6.pdf")); // md5 in lower case only
}
