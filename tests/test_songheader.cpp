#include <catch2/catch_test_macros.hpp>
#include <zub/core/songheader.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace zub::songheader;
using zub::songsuggest::Measured;

namespace {
const char* const kBody = "\\beginverse\nLa\n\\endverse\n\\endsong\n";
std::string song(const std::string& header) {
    return header + kBody;
}
Measured fitsOne() {
    return Measured{1, true, {}, 0};
}
Measured tooBig() {
    return Measured{2, false, {}, 0};
}
} // namespace

TEST_CASE("setPap replaces in place", "[songheader]") {
    const auto in = song("\\beginsong{T}[\n    by={X},\n    pap={a4},\n    lang={czech},\n]\n");
    CHECK(setPap(in, "a4,a5") ==
          song("\\beginsong{T}[\n    by={X},\n    pap={a4,a5},\n    lang={czech},\n]\n"));
    CHECK(setPap(in, "a4") == in);
    CHECK(option(in, "pap") == "a4");
}

TEST_CASE("setPap adds to a multi-line header after by", "[songheader]") {
    const auto in = song("\\beginsong{T}[\n  by={X},\n  lang={czech},\n]\n");
    CHECK(setPap(in, "a5") ==
          song("\\beginsong{T}[\n  by={X},\n  pap={a5},\n  lang={czech},\n]\n"));
    const auto noBy = song("\\beginsong{T}[\n    lang={czech}\n]\n");
    CHECK(setPap(noBy, "a5") == song("\\beginsong{T}[\n    lang={czech},\n    pap={a5},\n]\n"));
}

TEST_CASE("setPap in one-line headers", "[songheader]") {
    CHECK(setPap(song("\\beginsong{T}[by={X}]\n"), "a4") ==
          song("\\beginsong{T}[by={X}, pap={a4}]\n"));
    CHECK(setPap(song("\\beginsong{T}\n"), "a4") == song("\\beginsong{T}[pap={a4}]\n"));
    CHECK(setPap(song("\\beginsong{T}[]\n"), "a4") == song("\\beginsong{T}[pap={a4}]\n"));
}

TEST_CASE("setPap removes the option", "[songheader]") {
    const auto in = song("\\beginsong{T}[\n    by={X},\n    pap={a4,a5},\n    lang={czech},\n]\n");
    CHECK(setPap(in, "") == song("\\beginsong{T}[\n    by={X},\n    lang={czech},\n]\n"));
    CHECK(setPap(song("\\beginsong{T}[by={X}, pap={a4}, lang={c}]\n"), "") ==
          song("\\beginsong{T}[by={X}, lang={c}]\n"));
    CHECK(setPap(song("\\beginsong{T}[by={X}, pap={a4}]\n"), "") == song("\\beginsong{T}[by={X}]\n"));
    // The only option: the brackets go too.
    CHECK(setPap(song("\\beginsong{T}[pap={a4}]\n"), "") == song("\\beginsong{T}\n"));
    CHECK(setPap(song("\\beginsong{T}[\n    pap={a4},\n]\n"), "") == song("\\beginsong{T}\n"));
    // Nothing to remove.
    CHECK(setPap(song("\\beginsong{T}[by={X}]\n"), "") == song("\\beginsong{T}[by={X}]\n"));
    // setOption alone keeps the brackets.
    CHECK(setOption(song("\\beginsong{T}[pap={a4}]\n"), "pap", std::nullopt) ==
          song("\\beginsong{T}[]\n"));
}

TEST_CASE("setPap keeps CRLF and other keys", "[songheader]") {
    const std::string in =
        "\\beginsong{T}[\r\n    by={X},\r\n    tags={fav},\r\n]\r\n\\beginverse\r\nLa\r\n"
        "\\endverse\r\n\\endsong\r\n";
    const auto out = setPap(in, "a4,a5");
    CHECK(out ==
          "\\beginsong{T}[\r\n    by={X},\r\n    pap={a4,a5},\r\n    tags={fav},\r\n]\r\n"
          "\\beginverse\r\nLa\r\n\\endverse\r\n\\endsong\r\n");
    CHECK(setPap(out, "") == in);
}

TEST_CASE("setPap ignores commented-out options", "[songheader]") {
    const auto in = song("\\beginsong{T}[\n    by={X},\n    % pap={a4},\n]\n");
    CHECK(option(in, "pap") == std::nullopt);
    CHECK(setPap(in, "a5") ==
          song("\\beginsong{T}[\n    by={X},\n    pap={a5},\n    % pap={a4},\n]\n"));
}

TEST_CASE("measuredPapers", "[songheader]") {
    CHECK(measuredPapers("", fitsOne(), fitsOne()) == "a4,a5");
    CHECK(measuredPapers("a4", std::nullopt, fitsOne()) == "a4,a5");
    CHECK(measuredPapers("a5", fitsOne(), std::nullopt) == "a4,a5");
    CHECK(measuredPapers("a4,a5x2", std::nullopt, fitsOne()) == "a4,a5");
    CHECK(measuredPapers("a4,a5", std::nullopt, tooBig()) == "a4");
    CHECK(measuredPapers("a4,a5x2", std::nullopt, tooBig()) == "a4,a5x2");
    CHECK(measuredPapers("a4,a5", tooBig(), tooBig()) == "");
    // Unmeasured papers and unknown entries stay; a4 first.
    CHECK(measuredPapers("a5,tin,a4", std::nullopt, std::nullopt) == "a4,a5,tin");
    CHECK(measuredPapers("a4, a5", std::nullopt, std::nullopt) == "a4,a5");
    // Fits two pages (a5x2 as the song says): left alone.
    CHECK(measuredPapers("a5x2", std::nullopt, Measured{2, true, {}, 0}) == "a5x2");
    CHECK(measuredPapers("", std::nullopt, Measured{2, true, {}, 0}) == "");
    // One page but a line is too wide: does not fit.
    CHECK(measuredPapers("a5", std::nullopt, Measured{1, false, {3}, 0}) == "");
}

// Run with: build/debug/tests/zub_core_tests "[integration]"
TEST_CASE("setOption pap round-trips every real song", "[.integration][songheader]") {
    const std::filesystem::path root = std::filesystem::path{ZUB_REAL_SONGBOOK_DIR} / "songs" / "chords";
    if (!std::filesystem::is_directory(root))
        SKIP("no songbook checkout at " << ZUB_REAL_SONGBOOK_DIR);
    int n = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (e.path().extension() != ".tex")
            continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        const auto pap = option(text, "pap");
        if (!pap || pap->empty())
            continue;
        ++n;
        INFO(e.path().string());
        CHECK(setPap(text, *pap) == text);
        CHECK(measuredPapers(*pap, std::nullopt, std::nullopt) == *pap);
    }
    CHECK(n > 150);
}
