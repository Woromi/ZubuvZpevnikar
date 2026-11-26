#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <zub/core/songcheck.hpp>

namespace sc = zub::songcheck;

namespace {
std::string slurp(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}
const std::filesystem::path checks = std::filesystem::path(ZUB_TEST_DATA_DIR) / "checks";
} // namespace

TEST_CASE("songcheck runs the package's scripts with --json", "[songcheck]") {
    const auto command = sc::scriptCommand("/p", "checkkeys.py", "/sb", {"--all"});
    CHECK(command.program == "python3");
    CHECK(command.arguments == std::vector<std::string>{"/p/scripts/checkkeys.py", "--songbook",
                                                        "/sb", "--json", "--all"});
}

TEST_CASE("songcheck reads checkkeys --json", "[songcheck]") {
    const auto findings = sc::parseCheckKeys(slurp(checks / "checkkeys.json"));
    REQUIRE(findings);
    REQUIRE(findings->size() == 9);
    const auto& shy = (*findings)[0];
    CHECK(shy.file == "songs/chords/SonataArctica/Shy.tex");
    CHECK(shy.kind == "mismatch");
    CHECK(shy.stated == "Dm");
    CHECK(shy.detected == "D#m");
    CHECK(shy.margin > 100);
    const auto& blueBird = (*findings)[2];
    CHECK(blueBird.kind == "propose");
    CHECK(blueBird.stated.empty()); // null in the JSON
    CHECK(blueBird.detected == "F#m");
    CHECK((*findings)[8].suggest == "Am\\rightarrow Em");
    CHECK_FALSE(sc::parseCheckKeys(R"({"tool": "checksongs", "songs": []})"));
    CHECK_FALSE(sc::parseCheckKeys("not json"));
}

TEST_CASE("songcheck reads checksongs --json", "[songcheck]") {
    const auto issues = sc::parseCheckSongs(slurp(checks / "checksongs.json"));
    REQUIRE(issues);
    const auto dots = issues->find("songs/chords/Ayreon/ConnectTheDots.tex");
    REQUIRE(dots != issues->end());
    REQUIRE(dots->second.size() == 1);
    CHECK(dots->second[0].code == "bars-without-meter");
    CHECK(dots->second[0].severity == "warn");
    CHECK_FALSE(dots->second[0].line);
    CHECK(dots->second[0].message.starts_with("measure bars but no met"));
    CHECK(issues->contains("songs/chords/FalesneSpolecenstvo/DraciJezero.tex"));
    const auto lined = sc::parseCheckSongs(
        R"({"tool": "checksongs", "songs": [{"file": "f.tex", "issues": [{"severity": "error",
            "code": "note-naming", "line": 4, "message": "m"}]}, {"file": "clean.tex", "issues": []}]})");
    REQUIRE(lined);
    CHECK(lined->at("f.tex")[0].line == 4);
    CHECK_FALSE(lined->contains("clean.tex"));
}

TEST_CASE("songcheck spells a detected key as songs write it", "[songcheck]") {
    CHECK(sc::keyValue("Am", false) == "Am");
    CHECK(sc::keyValue("F#m", false) == "F\\shrp m");
    CHECK(sc::keyValue("C#", false) == "C\\shrp");
    CHECK(sc::keyValue("A#", false) == "B\\flt");
    CHECK(sc::keyValue("D#", true) == "E\\flt");
    CHECK(sc::keyValue("G#", false) == "A\\flt");
    CHECK(sc::keyValue("D#m", false) == "D\\shrp m");
    // German naming (Czech, Slovak): H is B natural, B is B flat.
    CHECK(sc::keyValue("Bm", true) == "Hm");
    CHECK(sc::keyValue("B", true) == "H");
    CHECK(sc::keyValue("Bm", false) == "Bm");
    CHECK(sc::keyValue("A#", true) == "B");
    CHECK(sc::keyValue("A#m", true) == "Bm"); // B flat minor
    CHECK(sc::keyValue("A#m", false) == "A\\shrp m");
    CHECK(sc::keyValue("", false).empty());
    CHECK(sc::keyValue("X", false).empty());
    CHECK(sc::keyValue("Cmaj7", false).empty());
}

TEST_CASE("songcheck confirms a stated key with % nocheck key above it", "[songcheck]") {
    const std::string song = "\\beginsong{T}[\n    by={A},\n    key={Dm},\n    tem={90},\n]\n"
                             "\\beginverse\nla\n\\endverse\n\\endsong\n";
    CHECK_FALSE(sc::keyConfirmed(song));
    const auto confirmed = sc::confirmKey(song);
    REQUIRE(confirmed);
    CHECK(*confirmed == "\\beginsong{T}[\n    by={A},\n    % nocheck key\n    key={Dm},\n"
                        "    tem={90},\n]\n\\beginverse\nla\n\\endverse\n\\endsong\n");
    CHECK(sc::keyConfirmed(*confirmed));
    CHECK_FALSE(sc::confirmKey(*confirmed)); // once is enough
    // Another kind above does not confirm the key; several kinds and stacked marks do.
    const auto with = [&](const std::string& mark) {
        std::string text = song;
        text.insert(text.find("    key="), mark + "\n");
        return text;
    };
    CHECK_FALSE(sc::keyConfirmed(with("    % nocheck tem")));
    CHECK(sc::keyConfirmed(with("    % nocheck tem key")));
    CHECK(sc::keyConfirmed(with("    % nocheck key\n    % nocheck tem")));
    CHECK(sc::keyConfirmed(with("    % nocheck"))); // a bare mark, older songs
    // A CRLF song gets a CRLF mark line.
    std::string crlf = song;
    for (std::size_t at = 0; (at = crlf.find('\n', at)) != std::string::npos; at += 2)
        crlf.insert(at, "\r");
    const auto confirmedCrlf = sc::confirmKey(crlf);
    REQUIRE(confirmedCrlf);
    CHECK(confirmedCrlf->find("    % nocheck key\r\n    key={Dm},\r\n") != std::string::npos);
    // No key line: nothing to confirm (a one-line header, or none).
    CHECK_FALSE(sc::confirmKey("\\beginsong{T}[by={A}, key={C}]\n\\endsong\n"));
    CHECK_FALSE(sc::confirmKey("\\beginsong{T}[\n    by={A},\n]\n\\endsong\n"));
    CHECK_FALSE(sc::keyConfirmed("no song"));
}

TEST_CASE("songcheck reads the missing header fields from checksongs' issues", "[songcheck]") {
    using Fields = std::vector<std::string>;
    // checksongs.py's own messages (scripts/checksongs.py).
    CHECK(sc::missingFields({"warn", "missing-recommended", std::nullopt,
                             "missing recommended: tem, tun"}) == Fields{"tem", "tun"});
    CHECK(sc::missingFields({"error", "missing-required", std::nullopt,
                             "missing required key 'by'"}) == Fields{"by"});
    CHECK(sc::missingFields({"warn", "bars-without-meter", std::nullopt,
                             "measure bars but no met: the first bar shows no meter"}) ==
          Fields{"met"});
    CHECK(sc::missingFields({"error", "note-naming", 4, "m"}).empty());
    // A reworded message gives nothing rather than wrong fields.
    CHECK(sc::missingFields({"warn", "missing-recommended", std::nullopt, "no tem"}).empty());
    // The real listing: Connect The Dots misses met.
    const auto issues = sc::parseCheckSongs(slurp(checks / "checksongs.json"));
    REQUIRE(issues);
    CHECK(sc::missingFields(issues->at("songs/chords/Ayreon/ConnectTheDots.tex")[0]) ==
          Fields{"met"});
}
