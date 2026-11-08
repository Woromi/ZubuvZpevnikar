#include <catch2/catch_test_macros.hpp>
#include <string>
#include <zub/core/linediff.hpp>

namespace diff = zub::diff;
using diff::Op;

namespace {

// "=a", "-b", "+c" per line, old/new numbers checked separately.
std::string summary(const std::vector<diff::Line>& lines) {
    std::string result;
    for (const auto& line : lines) {
        result += line.op == Op::Same ? '=' : line.op == Op::Removed ? '-' : '+';
        result += line.text;
        result += '|';
    }
    return result;
}

} // namespace

TEST_CASE("diff of identical texts is all Same", "[diff]") {
    const auto lines = diff::lines("a\nb\n", "a\nb\n");
    CHECK(summary(lines) == "=a|=b|");
    CHECK(lines[1].oldLine == 2);
    CHECK(lines[1].newLine == 2);
}

TEST_CASE("diff with empty sides", "[diff]") {
    CHECK(diff::lines("", "").empty());
    CHECK(summary(diff::lines("", "a\nb\n")) == "+a|+b|");
    CHECK(summary(diff::lines("a\nb\n", "")) == "-a|-b|");
    const auto added = diff::lines("", "a\n");
    CHECK(added[0].oldLine == 0);
    CHECK(added[0].newLine == 1);
}

TEST_CASE("diff of a pure insert and a pure delete", "[diff]") {
    const auto inserted = diff::lines("a\nc\n", "a\nb\nc\n");
    CHECK(summary(inserted) == "=a|+b|=c|");
    CHECK(inserted[2].oldLine == 2);
    CHECK(inserted[2].newLine == 3);

    const auto deleted = diff::lines("a\nb\nc\n", "a\nc\n");
    CHECK(summary(deleted) == "=a|-b|=c|");
    CHECK(deleted[1].oldLine == 2);
    CHECK(deleted[1].newLine == 0);
}

TEST_CASE("diff of a replaced block", "[diff]") {
    const auto lines = diff::lines("a\nx\ny\nz\nb\n", "a\nR\nb\n");
    CHECK(summary(lines) == "=a|-x|-y|-z|+R|=b|");
    CHECK(lines[4].newLine == 2);
    CHECK(lines[5].oldLine == 5);
    CHECK(lines[5].newLine == 3);
}

TEST_CASE("diff keeps a trailing CR", "[diff]") {
    const auto lines = diff::lines("a\r\nb\r\n", "a\r\nc\r\n");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0].op == Op::Same);
    CHECK(lines[0].text == "a\r");
    CHECK(lines[1].text == "b\r");
    CHECK(lines[2].text == "c\r");
    // CRLF vs LF is a difference
    CHECK(summary(diff::lines("a\r\n", "a\n")) == "-a\r|+a|");
}

TEST_CASE("diff without a final newline", "[diff]") {
    CHECK(summary(diff::lines("a\nb", "a\nb")) == "=a|=b|");
    // the last line differs only by the missing newline: same text, so Same
    CHECK(summary(diff::lines("a\nb", "a\nb\n")) == "=a|=b|");
    CHECK(summary(diff::lines("a\nb", "a\nc")) == "=a|-b|+c|");
}

TEST_CASE("mapOldLine", "[diff]") {
    const auto lines = diff::lines("a\nx\ny\nb\nc\n", "a\nR\nb\nc\n");
    CHECK(diff::mapOldLine(lines, 1) == 1); // kept
    CHECK(diff::mapOldLine(lines, 2) == 2); // removed: the added line that replaced it
    CHECK(diff::mapOldLine(lines, 3) == 2);
    CHECK(diff::mapOldLine(lines, 4) == 3); // kept, moved up
    CHECK(diff::mapOldLine(lines, 5) == 4);
    CHECK(diff::mapOldLine(lines, 99) == 4); // past the end: the last line
    CHECK(diff::mapOldLine(lines, 0) == 1);  // below the start: line 1, never an added line
    // Added lines at the start carry oldLine 0 and must not match line 0.
    CHECK(diff::mapOldLine(diff::lines("a\n", "N\na\n"), 0) == 2);
}

TEST_CASE("mapOldLine of a removed line without a replacement", "[diff]") {
    const auto middle = diff::lines("a\nx\nb\n", "a\nb\n");
    CHECK(diff::mapOldLine(middle, 2) == 2); // the next kept line
    const auto end = diff::lines("a\nb\nx\n", "a\nb\n");
    CHECK(diff::mapOldLine(end, 3) == 2); // nothing after: the last line
    CHECK(diff::mapOldLine(diff::lines("a\n", ""), 1) == 0);
}
