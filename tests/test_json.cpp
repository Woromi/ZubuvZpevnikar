#include <catch2/catch_test_macros.hpp>
#include <string>
#include <zub/core/json.hpp>

namespace js = zub::json;

TEST_CASE("json parses scalars", "[json]") {
    CHECK(js::parse("null")->isNull());
    CHECK(js::parse("true")->asBool() == true);
    CHECK(js::parse(" false \n")->asBool() == false);
    CHECK(js::parse("0")->asInt() == 0);
    CHECK(js::parse("42")->asInt() == 42);
    CHECK(js::parse("-17")->asInt() == -17);
    CHECK(js::parse("3.0")->asInt() == 3);
    CHECK_FALSE(js::parse("3.5")->asInt());
    CHECK(js::parse("1e2")->asInt() == 100);
    CHECK(*js::parse("\"abc\"")->asString() == "abc");
    CHECK_FALSE(js::parse("\"abc\"")->asInt());
    CHECK_FALSE(js::parse("1")->asString());
}

TEST_CASE("json decodes string escapes", "[json]") {
    CHECK(*js::parse(R"("a\\b\"c\/d\n\t\r\b\f")")->asString() == "a\\b\"c/d\n\t\r\b\f");
    CHECK(*js::parse(R"("\u0041\u00e1\u010D\u20AC")")->asString() ==
          "A\xC3\xA1\xC4\x8D\xE2\x82\xAC");
    // U+1F3B8 as a surrogate pair
    CHECK(*js::parse(R"("\ud83c\udfb8")")->asString() == "\xF0\x9F\x8E\xB8");
    // raw UTF-8 passes through
    CHECK(*js::parse("\"\xC4\x8D\"")->asString() == "\xC4\x8D");
    CHECK(*js::parse(R"("\\chordsoff")")->asString() == "\\chordsoff");
}

TEST_CASE("json parses nested structures", "[json]") {
    const auto value = js::parse(R"({"a": {"b": [1, -2, null, true, {"c": "d"}]}, "e": [], "f": {}})");
    REQUIRE(value);
    const js::Value* b = value->find("a")->find("b");
    REQUIRE(b);
    REQUIRE(b->asArray());
    REQUIRE(b->asArray()->size() == 5);
    CHECK((*b->asArray())[1].asInt() == -2);
    CHECK((*b->asArray())[2].isNull());
    CHECK((*b->asArray())[3].asBool() == true);
    CHECK(*(*b->asArray())[4].find("c")->asString() == "d");
    CHECK(value->find("e")->asArray()->empty());
    CHECK(value->find("f")->asObject()->empty());
    CHECK(value->find("missing") == nullptr);
    CHECK(b->find("x") == nullptr); // not an object
}

TEST_CASE("json rejects malformed input", "[json]") {
    for (const char* bad : {"", "   ", "{", "[1,2", "{\"a\":", "{\"a\" 1}", "{\"a\":1,}", "[1,]",
                            "\"abc", "\"a\\", "\"\\u12\"", "\"\\uZZZZ\"", "\"\\ud83c\"",
                            "\"\\udfb8\"", "\"\\x\"", "tru", "nul", "-", "1.", "1e", "01x",
                            "{\"a\":1} x", "[] []", "{1:2}", "\"a\nb\"", "@"}) {
        INFO(bad);
        const auto result = js::parse(bad);
        CHECK_FALSE(result);
        if (!result)
            CHECK_FALSE(result.error().empty());
    }
}

TEST_CASE("json limits the nesting depth", "[json]") {
    CHECK(js::parse(std::string(100, '[') + std::string(100, ']')));
    CHECK_FALSE(js::parse(std::string(100000, '[')));
}
