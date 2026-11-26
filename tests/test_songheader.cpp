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

TEST_CASE("setPap adds to a multi-line header in the converter's order", "[songheader]") {
    const auto in = song("\\beginsong{T}[\n  by={X},\n  lang={czech},\n]\n");
    CHECK(setPap(in, "a5") ==
          song("\\beginsong{T}[\n  by={X},\n  lang={czech},\n  pap={a5},\n]\n"));
    const auto noBy = song("\\beginsong{T}[\n    lang={czech}\n]\n");
    CHECK(setPap(noBy, "a5") == song("\\beginsong{T}[\n    lang={czech},\n    pap={a5},\n]\n"));
}

TEST_CASE("setPap in one-line headers", "[songheader]") {
    CHECK(setPap(song("\\beginsong{T}[by={X}]\n"), "a4") ==
          song("\\beginsong{T}[by={X}, pap={a4}]\n"));
    CHECK(setPap(song("\\beginsong{T}\n"), "a4") == song("\\beginsong{T}[pap={a4}]\n"));
    CHECK(setPap(song("\\beginsong{T}[]\n"), "a4") == song("\\beginsong{T}[pap={a4}]\n"));
}

TEST_CASE("setPap with an empty list keeps the key as pap={}", "[songheader]") {
    const auto in = song("\\beginsong{T}[\n    by={X},\n    pap={a4,a5},\n    lang={czech},\n]\n");
    CHECK(setPap(in, "") == song("\\beginsong{T}[\n    by={X},\n    pap={},\n    lang={czech},\n]\n"));
    CHECK(setPap(song("\\beginsong{T}[by={X}, pap={a4}, lang={c}]\n"), "") ==
          song("\\beginsong{T}[by={X}, pap={}, lang={c}]\n"));
    // The only option: the header keeps its brackets.
    CHECK(setPap(song("\\beginsong{T}[pap={a4}]\n"), "") == song("\\beginsong{T}[pap={}]\n"));
    CHECK(setPap(song("\\beginsong{T}[\n    pap={a4},\n]\n"), "") ==
          song("\\beginsong{T}[\n    pap={},\n]\n"));
    // Already empty: unchanged. Absent: written.
    CHECK(setPap(song("\\beginsong{T}[by={X}, pap={}]\n"), "") ==
          song("\\beginsong{T}[by={X}, pap={}]\n"));
    CHECK(option(song("\\beginsong{T}[pap={}]\n"), "pap") == "");
    CHECK(setPap(song("\\beginsong{T}[by={X}]\n"), "") == song("\\beginsong{T}[by={X}, pap={}]\n"));
    // ... and a list can be added to it again.
    CHECK(setPap(song("\\beginsong{T}[pap={}]\n"), "a4") == song("\\beginsong{T}[pap={a4}]\n"));
    // setOption alone still removes the key and keeps the brackets.
    CHECK(setOption(song("\\beginsong{T}[pap={a4}]\n"), "pap", std::nullopt) ==
          song("\\beginsong{T}[]\n"));
}

TEST_CASE("setPap keeps CRLF and other keys", "[songheader]") {
    const std::string in =
        "\\beginsong{T}[\r\n    by={X},\r\n    tags={fav},\r\n]\r\n\\beginverse\r\nLa\r\n"
        "\\endverse\r\n\\endsong\r\n";
    const auto out = setPap(in, "a4,a5");
    CHECK(out ==
          "\\beginsong{T}[\r\n    by={X},\r\n    tags={fav},\r\n    pap={a4,a5},\r\n]\r\n"
          "\\beginverse\r\nLa\r\n\\endverse\r\n\\endsong\r\n");
    CHECK(setPap(out, "") ==
          "\\beginsong{T}[\r\n    by={X},\r\n    tags={fav},\r\n    pap={},\r\n]\r\n"
          "\\beginverse\r\nLa\r\n\\endverse\r\n\\endsong\r\n");
}

TEST_CASE("a written option takes the place of its commented-out placeholder", "[songheader]") {
    const auto in = song("\\beginsong{T}[\n    by={X},\n    % pap={a4},\n]\n");
    CHECK(option(in, "pap") == std::nullopt); // a comment is no value
    CHECK(setPap(in, "a5") == song("\\beginsong{T}[\n    by={X},\n    pap={a5},\n]\n"));
    // The converter's header: the key goes where its "% key={}," was.
    const auto imported = song("\\beginsong{T}[\n    by={X},\n    % lang={czech|english},\n"
                               "    % tem={},\n    tun={Standard},\n    % key={},\n"
                               "    % cap={},\n    src={u},\n    pap={},\n]\n");
    CHECK(setOption(imported, "key", "{Am}") ==
          song("\\beginsong{T}[\n    by={X},\n    % lang={czech|english},\n    % tem={},\n"
               "    tun={Standard},\n    key={Am},\n    % cap={},\n    src={u},\n    pap={},\n]\n"));
    // A value already there: a leftover placeholder goes.
    const auto both = song("\\beginsong{T}[\n    by={X},\n    key={C},\n    % key={},\n]\n");
    CHECK(setOption(both, "key", "{D}") == song("\\beginsong{T}[\n    by={X},\n    key={D},\n]\n"));
    // Without a placeholder, in the converter's order: key after cap, before src.
    const auto plain = song("\\beginsong{T}[\n    by={X},\n    cap={2},\n    src={u},\n]\n");
    CHECK(setOption(plain, "key", "{G}") ==
          song("\\beginsong{T}[\n    by={X},\n    cap={2},\n    key={G},\n    src={u},\n]\n"));
    const auto first = song("\\beginsong{T}[\n    src={u},\n]\n");
    CHECK(setOption(first, "key", "{G}") ==
          song("\\beginsong{T}[\n    key={G},\n    src={u},\n]\n"));
    // A key the order does not name ranks just before pap; chr comes after pap.
    const auto other = song("\\beginsong{T}[\n    by={X},\n    foo={1},\n    chr={x},\n]\n");
    CHECK(setPap(other, "a5") ==
          song("\\beginsong{T}[\n    by={X},\n    foo={1},\n    pap={a5},\n    chr={x},\n]\n"));
    CHECK(setOption(other, "src", "{u}") ==
          song("\\beginsong{T}[\n    by={X},\n    src={u},\n    foo={1},\n    chr={x},\n]\n"));
    // A last option without its comma gets one.
    const auto noComma = song("\\beginsong{T}[\n    by={X}\n    % key={}\n]\n");
    CHECK(setOption(noComma, "key", "{G}") ==
          song("\\beginsong{T}[\n    by={X},\n    key={G},\n]\n"));
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
        if (!pap)
            continue;
        ++n;
        INFO(e.path().string());
        CHECK(setPap(text, *pap) == text);
        CHECK(measuredPapers(*pap, std::nullopt, std::nullopt) == *pap);
    }
    CHECK(n > 150);
}

TEST_CASE("addPlaceholders changes no value of any real song", "[.integration][songheader]") {
    const std::filesystem::path root = std::filesystem::path{ZUB_REAL_SONGBOOK_DIR} / "songs";
    if (!std::filesystem::is_directory(root))
        SKIP("no songbook checkout at " << ZUB_REAL_SONGBOOK_DIR);
    const std::vector<std::string> keys{"by", "lang", "key", "tem", "tun", "met", "src", "cap"};
    int added = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (e.path().extension() != ".tex")
            continue;
        std::ifstream f(e.path(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        INFO(e.path().string());
        const auto r = zub::songheader::addPlaceholders(text, keys);
        added += static_cast<int>(r.added.size());
        // Only comment lines were added: every value reads the same.
        for (const auto& key : keys)
            CHECK(option(r.text, key) == option(text, key));
        CHECK(r.text.size() >= text.size());
        // Adding them again adds nothing.
        CHECK(zub::songheader::addPlaceholders(r.text, keys).text == r.text);
    }
    CHECK(added > 0);
}

TEST_CASE("a comment ends at a bare CR, as in TeX", "[songheader]") {
    // Before, the comment ran to the LF and swallowed the key.
    CHECK(option(song("\\beginsong{T}[by={A}, % x\rkey={C}]\n"), "key") == "C");
    CHECK(option(song("\\beginsong{T}[by={A}, % x\r\nkey={C}]\n"), "key") == "C");
}

TEST_CASE("only the converter's placeholder form is replaced, not a remark", "[songheader]") {
    using zub::songheader::setOption;
    const auto remark = song("\\beginsong{T}[\n    by={X},\n    % key=C was wrong, it is D\n]\n");
    CHECK(setOption(remark, "key", "{D}") ==
          song("\\beginsong{T}[\n    by={X},\n    key={D},\n    % key=C was wrong, it is D\n]\n"));
}

TEST_CASE("bodyCapos and shiftBodyCapos change every \\capo{n} in code", "[songheader]") {
    const std::string text =
        "\\beginsong{A}[by={B}, cap={2}]\n\\capo{2}\n% \\capo{7}\n\\iflargepaper\\else\\capo{5}\\fi\n"
        "\\capo{}\nx 50\\% \\capo{3}\n\\endsong\n";
    CHECK(bodyCapos(text) == std::vector<int>{2, 5, 3});
    const std::string shifted = shiftBodyCapos(text, 4);
    CHECK(shifted ==
          "\\beginsong{A}[by={B}, cap={2}]\n\\capo{6}\n% \\capo{7}\n\\iflargepaper\\else\\capo{9}"
          "\\fi\n\\capo{}\nx 50\\% \\capo{7}\n\\endsong\n");
    CHECK(shiftBodyCapos(shifted, -4) == text);

    const std::string crlf = "\\capo{1}\r\nLa\r\n\\capo{3}\r\n";
    CHECK(shiftBodyCapos(crlf, 2) == "\\capo{3}\r\nLa\r\n\\capo{5}\r\n");
    CHECK(shiftBodyCapos(crlf, -1) == "\\capo{0}\r\nLa\r\n\\capo{2}\r\n");

    const std::string none = "\\beginverse\nLa\n\\endverse\n";
    CHECK(bodyCapos(none).empty());
    CHECK(shiftBodyCapos(none, 3) == none);
}

TEST_CASE("addPlaceholders writes the missing keys as the converter's placeholders",
          "[songheader]") {
    using zub::songheader::addPlaceholders;
    const std::string head = "\\beginsong{T}[\n    by={X},\n    lang={czech},\n    key={G},\n"
                             "    % src={},\n    pap={a5},\n]\nbody\n\\endsong\n";
    const auto r = addPlaceholders(head, {"lang", "key", "tem", "tun", "met", "src"});
    // In the converter's order; present options and an existing placeholder are left alone.
    CHECK(r.added == std::vector<std::string>{"tun", "tem", "met"});
    CHECK(r.text == "\\beginsong{T}[\n    by={X},\n    % tun={},\n    % tem={},\n    % met={},\n"
                    "    lang={czech},\n    key={G},\n    % src={},\n    pap={a5},\n]\nbody\n"
                    "\\endsong\n");
    // Again: nothing more to add.
    CHECK(addPlaceholders(r.text, {"tem", "tun", "met"}).added.empty());
    // An empty value counts as present (it is there to be filled in).
    CHECK(addPlaceholders("\\beginsong{T}[\n    by={X},\n    tem={},\n]\n", {"tem"}).added.empty());
    // CR LF stays.
    CHECK(addPlaceholders("\\beginsong{T}[\r\n    by={X},\r\n]\r\n", {"tem"}).text ==
          "\\beginsong{T}[\r\n    by={X},\r\n    % tem={},\r\n]\r\n");
    // Writing the key later takes the placeholder's place.
    CHECK(zub::songheader::setOption(r.text, "tem", "{120}").find("    tem={120},\n    % met={}") !=
          std::string::npos);
    // A one-line header, or none: a comment would end it, nothing is added.
    const std::string oneLine = "\\beginsong{T}[by={X}]\n";
    CHECK(addPlaceholders(oneLine, {"tem"}).text == oneLine);
    CHECK(addPlaceholders("\\beginsong{T}\n", {"tem"}).added.empty());
}
