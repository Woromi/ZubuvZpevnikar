#include <catch2/catch_test_macros.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songtags.hpp>

using namespace zub::tags;
using Tags = std::vector<std::string>;

namespace {
const char* const kBody = "\\beginverse\nLa\n\\endverse\n\\endsong\n";
std::string song(const std::string& header) {
    return header + kBody;
}
} // namespace

TEST_CASE("tags::parse splits, trims, decodes and deduplicates", "[tags]") {
    CHECK(parse("fav, a\\&b ,, fav,  ") == Tags{"fav", "a&b"});
    CHECK(parse("").empty());
    CHECK(parse("táborák, Ohníček") == Tags{"táborák", "Ohníček"});
    CHECK(isFavourite(parse("a, fav")));
    CHECK_FALSE(isFavourite(parse("a, favourite")));
}

TEST_CASE("tags::invalidTag", "[tags]") {
    CHECK_FALSE(invalidTag("táborák"));
    CHECK_FALSE(invalidTag("do auta"));
    CHECK(invalidTag(""));
    CHECK(invalidTag("   "));
    for (const char* bad : {"a,b", "a{", "a}", "a\\b", "a%", "a#", "a\nb"})
        CHECK(invalidTag(bad));
}

TEST_CASE("setTags replaces an existing option in place", "[tags]") {
    const auto in =
        song("\\beginsong{Africa}[\n    by={Toto},\n    tags={fav},\n    lang={english},\n]\n");
    const auto out = setTags(in, {"fav", "táborák"});
    CHECK(out == song("\\beginsong{Africa}[\n    by={Toto},\n    tags={fav, táborák},\n    "
                      "lang={english},\n]\n"));
    CHECK(zub::parseSong(out)->value("tags") == "fav, táborák");
    CHECK(setTags(out, {"fav", "táborák"}) == out);
}

TEST_CASE("setTags inserts after the by line", "[tags]") {
    const auto in = song("\\beginsong{Africa}[\n  by={Toto},\n  lang={english},\n]\n");
    const auto out = setTags(in, {"a", "b"});
    CHECK(out ==
          song("\\beginsong{Africa}[\n  by={Toto},\n  tags={a, b},\n  lang={english},\n]\n"));
    CHECK(setTags(out, {"a", "b"}) == out);
}

TEST_CASE("setTags adds a comma to a by line without one", "[tags]") {
    const auto in = song("\\beginsong{T}[\n    by={X}\n]\n");
    CHECK(setTags(in, {"a"}) == song("\\beginsong{T}[\n    by={X},\n    tags={a},\n]\n"));
}

TEST_CASE("setTags inserts before the closing bracket without a by line", "[tags]") {
    const auto in = song("\\beginsong{T}[\n    lang={czech},\n]\n");
    CHECK(setTags(in, {"a"}) == song("\\beginsong{T}[\n    lang={czech},\n    tags={a},\n]\n"));
    const auto noComma = song("\\beginsong{T}[\n    lang={czech}\n]\n");
    CHECK(setTags(noComma, {"a"}) ==
          song("\\beginsong{T}[\n    lang={czech},\n    tags={a},\n]\n"));
    CHECK(setTags(song("\\beginsong{T}[\n]\n"), {"a"}) ==
          song("\\beginsong{T}[\n    tags={a},\n]\n"));
}

TEST_CASE("setTags removes the option with its line", "[tags]") {
    const auto in =
        song("\\beginsong{T}[\n    by={X},\n    tags={fav, a},\n    lang={czech},\n]\n");
    CHECK(setTags(in, {}) == song("\\beginsong{T}[\n    by={X},\n    lang={czech},\n]\n"));
    const auto last = song("\\beginsong{T}[\n    by={X},\n    tags={a}\n]\n");
    CHECK(setTags(last, {}) == song("\\beginsong{T}[\n    by={X},\n]\n"));
}

TEST_CASE("setTags keeps CRLF", "[tags]") {
    const auto in = song("\\beginsong{T}[\r\n    by={X},\r\n    lang={czech},\r\n]\r\n");
    const auto out = setTags(in, {"fav"});
    CHECK(out ==
          song("\\beginsong{T}[\r\n    by={X},\r\n    tags={fav},\r\n    lang={czech},\r\n]\r\n"));
    CHECK(setTags(out, {}) == in);
}

TEST_CASE("setTags in a one-line header", "[tags]") {
    const auto in = song("\\beginsong{T}[by=X]\n");
    const auto out = setTags(in, {"a", "b"});
    CHECK(out == song("\\beginsong{T}[by=X, tags={a, b}]\n"));
    CHECK(setTags(out, {"c"}) == song("\\beginsong{T}[by=X, tags={c}]\n"));
    CHECK(setTags(out, {}) == in);
    CHECK(setTags(song("\\beginsong{T}[tags={a}, by=X]\n"), {}) == song("\\beginsong{T}[by=X]\n"));
    CHECK(setTags(song("\\beginsong{T}[]\n"), {"a"}) == song("\\beginsong{T}[tags={a}]\n"));
}

TEST_CASE("setTags ignores comments and keeps them", "[tags]") {
    const auto in = song("\\beginsong{T}[\n    % tags={old},\n    by={X}, % the artist, really\n"
                         "    lang={czech},\n]\n");
    const auto out = setTags(in, {"a"});
    CHECK(out == song("\\beginsong{T}[\n    % tags={old},\n    by={X}, % the artist, really\n"
                      "    tags={a},\n    lang={czech},\n]\n"));
    CHECK(zub::parseSong(out)->value("tags") == "a");
    // A tags option that has a comment after it keeps the comment when removed.
    const auto withComment = song("\\beginsong{T}[\n    tags={a}, % mine\n    by={X},\n]\n");
    CHECK(setTags(withComment, {"b"}) ==
          song("\\beginsong{T}[\n    tags={b}, % mine\n    by={X},\n]\n"));
}

TEST_CASE("setTags on a song without options or without a header", "[tags]") {
    CHECK(setTags(song("\\beginsong{T}\n"), {"a"}) == song("\\beginsong{T}[tags={a}]\n"));
    CHECK(setTags(song("\\beginsong{T}\n"), {}) == song("\\beginsong{T}\n"));
    CHECK(setTags("no header\n", {"a"}) == "no header\n");
}

TEST_CASE("setTags replaces an unbraced value", "[tags]") {
    CHECK(setTags(song("\\beginsong{T}[tags=fav, by=X]\n"), {"a"}) ==
          song("\\beginsong{T}[tags={a}, by=X]\n"));
}
