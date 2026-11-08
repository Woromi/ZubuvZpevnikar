#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <zub/core/songtidy.hpp>

namespace fs = std::filesystem;
namespace st = zub::songtidy;

namespace {

const fs::path converterData = fs::path(ZUB_TEST_DATA_DIR) / "converter";

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

int exitCode(const char* name) {
    return std::stoi(slurp(converterData / (std::string(name) + ".code")));
}

} // namespace

TEST_CASE("tidy prepare builds the command line", "[songtidy]") {
    const auto command = st::prepare("/opt/chords_converter", "/tmp/x/Song.tex");
    CHECK(command.program == "/opt/chords_converter");
    CHECK(command.arguments == std::vector<std::string>{"tidy", "/tmp/x/Song.tex", "-o", "-"});
}

TEST_CASE("tidy interprets a real run", "[songtidy]") {
    const auto result =
        st::interpret(exitCode("tidy"), slurp(converterData / "tidy.out"),
                      slurp(converterData / "tidy.err"));
    REQUIRE(result);
    CHECK(result->content == slurp(converterData / "tidy.out"));
    REQUIRE(result->changes.size() == 3);
    CHECK(result->changes[0].line == 9);
    CHECK(result->changes[0].description.starts_with("chord moved to the start of its syllable"));
    CHECK(result->changes[1].line == 23);
    CHECK(result->changes[1].description.find("\\insertchorus") != std::string::npos);
    // the description keeps its own ":digits:"-free text, including "on line 13"
    CHECK(result->changes[1].description.find("line 13") != std::string::npos);
    CHECK(result->changes[2].line == 32);
    CHECK(result->changes[2].description.find("\\insertchorus*") != std::string::npos);
}

TEST_CASE("tidy reports an unreadable file as an error", "[songtidy]") {
    const auto result = st::interpret(exitCode("tidy-unreadable"),
                                      slurp(converterData / "tidy-unreadable.out"),
                                      slurp(converterData / "tidy-unreadable.err"));
    REQUIRE_FALSE(result);
    CHECK(result.error().find("Failed to read file") != std::string::npos);
}

TEST_CASE("tidy interpret edge cases", "[songtidy]") {
    SECTION("non-zero exit without a message") {
        const auto result = st::interpret(2, "", "  \n");
        REQUIRE_FALSE(result);
        CHECK(result.error() == "chords_converter tidy exited with code 2");
    }
    SECTION("stderr is trimmed") {
        const auto result = st::interpret(1, "", "\nboom\n");
        REQUIRE_FALSE(result);
        CHECK(result.error() == "boom");
    }
    SECTION("no song on stdout is an error") {
        for (const char* out : {"", "\n", "no song here"}) {
            const auto result = st::interpret(0, out, "");
            REQUIRE_FALSE(result);
            CHECK(result.error() == "chords_converter tidy printed no song");
        }
    }
    SECTION("nothing to change") {
        const auto result = st::interpret(0, "song\n\\endsong\n", "");
        REQUIRE(result);
        CHECK(result->content == "song\n\\endsong\n");
        CHECK(result->changes.empty());
    }
    SECTION("other stderr lines are ignored, the first :digits: splits") {
        const auto result = st::interpret(
            0, "\\endsong", "Warning: something\n/a/b.tex:7: note: see 12: here\nno number: here\n"
                    "/p/q.tex:x: not a number\n/p/q.tex:15: last without newline");
        REQUIRE(result);
        REQUIRE(result->changes.size() == 2);
        CHECK(result->changes[0].line == 7);
        CHECK(result->changes[0].description == "note: see 12: here");
        CHECK(result->changes[1].line == 15);
        CHECK(result->changes[1].description == "last without newline");
    }
    SECTION("CRLF in stderr") {
        const auto result = st::interpret(0, "\\endsong", "f.tex:3: moved\r\n");
        REQUIRE(result);
        REQUIRE(result->changes.size() == 1);
        CHECK(result->changes[0].description == "moved");
    }
}
