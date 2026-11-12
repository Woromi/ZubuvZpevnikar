#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <zub/core/converter.hpp>
#include <zub/core/preview.hpp>
#include <zub/core/songsuggest.hpp>

namespace fs = std::filesystem;
namespace ss = zub::songsuggest;
namespace cv = zub::converter;

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

TEST_CASE("shrink, check and apply build the command lines", "[songsuggest]") {
    const auto full = ss::shrinkCommand("/opt/cc", "/tmp/x/S.tex", ss::Paper::A5, true, "/sb");
    CHECK(full.program == "/opt/cc");
    CHECK(full.arguments == std::vector<std::string>{"shrink", "/tmp/x/S.tex", "--fit", "a5",
                                                     "--measure", "--songbook", "/sb", "--json"});
    const auto plain = ss::shrinkCommand("/opt/cc", "/tmp/x/S.tex", std::nullopt, false, "/sb");
    CHECK(plain.arguments ==
          std::vector<std::string>{"shrink", "/tmp/x/S.tex", "--songbook", "/sb", "--json"});
    const auto a4 = ss::shrinkCommand("/opt/cc", "S.tex", ss::Paper::A4, false, "/sb");
    CHECK(a4.arguments[2] == "--fit");
    CHECK(a4.arguments[3] == "a4");
    const std::vector<std::string> ids{"a@1", "b@2"};
    CHECK(ss::applyCommand("/opt/cc", "/tmp/x/S.tex", ids).arguments ==
          std::vector<std::string>{"apply", "/tmp/x/S.tex", "--id", "a@1", "--id", "b@2", "-o", "-"});
    const auto apply = ss::applyCommand("/opt/cc", "/tmp/x/S.tex", "chordsoff@81");
    CHECK(apply.arguments == std::vector<std::string>{"apply", "/tmp/x/S.tex", "--id",
                                                      "chordsoff@81", "-o", "-"});
}

TEST_CASE("shrink interprets a real run with both papers measured", "[songsuggest]") {
    const auto result = ss::interpretProposals(exitCode("shrink"), slurp(converterData / "shrink.json"),
                                             slurp(converterData / "shrink.err"));
    REQUIRE(result);
    CHECK(result->fits == false);
    CHECK(result->note == "It fits A4: the changes for small paper only.");
    REQUIRE(result->a4);
    CHECK(result->a4->pages == 1);
    CHECK(result->a4->fits);
    CHECK(result->a4->wrapLines.empty());
    REQUIRE(result->a5);
    CHECK(result->a5->pages == 2);
    CHECK_FALSE(result->a5->fits);
    CHECK(result->a5->wrapLines == std::vector<int>{45, 60, 61, 66});
    CHECK(result->a5->chorusWraps == 0);
    REQUIRE(result->proposals.size() == 2);
    const auto& first = result->proposals[0];
    CHECK(first.id == "chordsoffonsmallpage@48");
    CHECK(first.kind == "chordsoffonsmallpage");
    CHECK(first.category == "fit");
    CHECK(first.stage == 2);
    CHECK(first.line == 48);
    CHECK(first.gainLines == 1);
    CHECK(first.score == 48);
    CHECK(first.grade == "poor");
    CHECK(first.sources == std::vector<ss::Source>{{24, 657, 786}});
    CHECK(first.edits == std::vector<ss::Edit>{{1615, 1615, "\\chordsoffonsmallpage"}});
    CHECK(first.description == "the 2nd pre-chorus repeats the chords of the 1st pre-chorus (11 lines "
                               "away): \\chordsoffonsmallpage");
    CHECK(result->proposals[1].id == "smalleronsmallpage@16");
    CHECK(result->proposals[1].gainLines == 6);
    CHECK(result->proposals[1].score == 20);
    CHECK(result->proposals[1].grade == "last resort");
    CHECK(result->proposals[1].sources.empty());
}

TEST_CASE("shrink interprets a run where only A4 was measured", "[songsuggest]") {
    const auto result = ss::interpretProposals(exitCode("shrink-a4only"),
                                             slurp(converterData / "shrink-a4only.json"), "");
    REQUIRE(result);
    CHECK(result->fits == false);
    REQUIRE(result->a4);
    CHECK(result->a4->pages == 2);
    CHECK_FALSE(result->a5); // not measured while A4 does not fit
    // 0.9.0: both stages are listed when the song does not fit A4
    CHECK(result->note.contains("small paper only"));
    REQUIRE(result->proposals.size() == 5);
    CHECK(result->proposals[0].id == "chordsoff@86");
    CHECK(result->proposals[1].id == "chordsoff@81");
    CHECK(result->proposals[1].stage == 1);
    CHECK(result->proposals[1].gainLines == 2);
    CHECK(result->proposals[1].sources == std::vector<ss::Source>{{76, 2717, 2932}});
    CHECK(result->proposals[2].id == "chordsoffonsmallpage@86");
    CHECK(result->proposals[2].stage == 2);
    CHECK(result->proposals[3].stage == 2);
    const auto& smaller = result->proposals[4];
    CHECK(smaller.id == "smalleronsmallpage@9");
    CHECK(smaller.stage == 2);
    CHECK(smaller.score == 20);
    CHECK(smaller.grade == "last resort");
    for (const auto& proposal : result->proposals)
        CHECK_FALSE(proposal.dismiss); // not corrections
}

TEST_CASE("check lists corrections in song order", "[songsuggest]") {
    CHECK(ss::checkCommand("/opt/cc", "/tmp/x/S.tex").arguments ==
          std::vector<std::string>{"check", "/tmp/x/S.tex", "--json"});
    const auto result =
        ss::interpretProposals(exitCode("check"), slurp(converterData / "check.json"), "");
    REQUIRE(result);
    CHECK_FALSE(result->a4);
    CHECK_FALSE(result->a5);
    REQUIRE(result->proposals.size() == 5);
    std::vector<std::string> ids;
    for (const auto& proposal : result->proposals) {
        ids.push_back(proposal.id);
        CHECK(proposal.score == 0);
        CHECK(proposal.grade == "correction");
    }
    CHECK(ids == std::vector<std::string>{"typo@14", "apostrophes@15", "tie@15", "restructure@18",
                                          "typo@29"});
    CHECK(result->proposals[0].category == "typo");
    CHECK(result->proposals[0].edits.size() == 1);
    // the song says lang=english without braces: read since converter 41651cd, so "~" is a finding
    const auto& tie = result->proposals[2];
    CHECK(tie.kind == "tie");
    CHECK(tie.category == "typo");
    CHECK(tie.edits == std::vector<ss::Edit>{{340, 341, " "}});
    const auto& restructure = result->proposals[3];
    CHECK(restructure.category == "restructure");
    CHECK(restructure.gainLines == 0);
    CHECK(restructure.sources == std::vector<ss::Source>{{8, 155, 258}});
    // every finding can be marked right with a "% nocheck" line, also the one to fix by hand
    for (const auto& proposal : result->proposals) {
        REQUIRE(proposal.dismiss);
        CHECK(proposal.dismiss->start == proposal.dismiss->end);
        CHECK(proposal.dismiss->replacement.contains("% nocheck"));
    }
    // a typo with a chord inside the word: to fix by hand, nothing to apply
    CHECK(result->proposals[4].edits.empty());
    CHECK(result->proposals[4].description.ends_with("so fix it by hand"));

    const auto applied = ss::interpretApply(exitCode("check-apply"),
                                            slurp(converterData / "check-apply.out"),
                                            slurp(converterData / "check-apply.err"));
    REQUIRE(applied);
    CHECK(applied->find("\\beginverse*") == std::string::npos);
    CHECK(applied->find("Ill be") == std::string::npos);
    CHECK(applied->find("\xE2\x80\x99") == std::string::npos); // no ’ left
}

TEST_CASE("check finds a missing comma in a Czech song", "[songsuggest]") {
    const auto result =
        ss::interpretProposals(exitCode("check-cs"), slurp(converterData / "check-cs.json"), "");
    REQUIRE(result);
    REQUIRE(result->proposals.size() == 1);
    const auto& comma = result->proposals[0];
    CHECK(comma.id == "comma@5");
    CHECK(comma.kind == "comma");
    CHECK(comma.category == "typo");
    CHECK(comma.grade == "correction");
    CHECK(comma.line == 5);
    CHECK(comma.edits == std::vector<ss::Edit>{{112, 112, ","}});
    CHECK(comma.dismiss == ss::Edit{103, 103, "% nocheck\n"});
    const std::string text = slurp(converterData / "check-cs-input.tex");
    CHECK(text.substr(103, 5) == "\\[Am]");
}

TEST_CASE("a malformed dismiss edit is an error, null is none", "[songsuggest]") {
    const auto make = [](std::string_view dismiss) {
        return std::string(R"({"proposals":[{"id":"a@1","description":"d","stage":1,"gain_lines":0,)"
                           R"("score":0,"grade":"correction","dismiss":)") +
               std::string(dismiss) + "}]}";
    };
    const auto none = ss::interpretProposals(0, make("null"), "");
    REQUIRE(none);
    CHECK_FALSE(none->proposals[0].dismiss);
    const auto good =
        ss::interpretProposals(0, make(R"({"start":3,"end":3,"replacement":"% nocheck\n"})"), "");
    REQUIRE(good);
    CHECK(good->proposals[0].dismiss == ss::Edit{3, 3, "% nocheck\n"});
    CHECK_FALSE(ss::interpretProposals(0, make(R"({"start":3,"end":3})"), ""));
    CHECK_FALSE(ss::interpretProposals(0, make(R"({"start":5,"end":3,"replacement":""})"), ""));
}

TEST_CASE("join lists joined lines in shrink's shape", "[songsuggest]") {
    const auto command = ss::joinCommand("/opt/cc", "/tmp/x/S.tex", "/sb");
    CHECK(command.arguments ==
          std::vector<std::string>{"join", "/tmp/x/S.tex", "--songbook", "/sb", "--json"});
    const auto result =
        ss::interpretProposals(exitCode("join"), slurp(converterData / "join.json"), "");
    REQUIRE(result);
    REQUIRE(result->a4);
    REQUIRE(result->a5);
    CHECK(result->a5->pages == 4);
    REQUIRE(result->proposals.size() == 2);
    CHECK(result->proposals[0].kind == "joinlines");
    CHECK(result->proposals[1].id == "joinlines@3");
    CHECK(result->proposals[1].gainLines == 48);
    CHECK(result->proposals[1].edits.size() == 24);
}

TEST_CASE("suggest copes with null fits, null wrap lines and nothing measured", "[songsuggest]") {
    const auto result = ss::interpretProposals(
        0,
        R"({"file":"x.tex","fit":null,"fits":null,"note":"","proposals":[],
            "measured":{"a5":{"fits":false,"pages":3,
              "wraps":[{"in_chorus":true,"line":null},{"in_chorus":false,"line":9},
                       {"in_chorus":true,"line":null}]}}})",
        "");
    REQUIRE(result);
    CHECK_FALSE(result->fits);
    CHECK_FALSE(result->a4);
    REQUIRE(result->a5);
    CHECK(result->a5->pages == 3);
    CHECK(result->a5->chorusWraps == 2);
    CHECK(result->a5->wrapLines == std::vector<int>{9});
    CHECK(result->proposals.empty());

    const auto bare = ss::interpretProposals(0, R"({"proposals":[]})", "");
    REQUIRE(bare);
    CHECK_FALSE(bare->fits);
    CHECK(bare->note.empty());
}

TEST_CASE("suggest decodes escapes in descriptions", "[songsuggest]") {
    const auto result = ss::interpretProposals(
        0,
        R"({"proposals":[{"id":"a@1","kind":"k","stage":1,"line":1,"gain_lines":2,
            "score":80,"grade":"good","description":"quote \" backslash \\ caron \u010d"}]})",
        "");
    REQUIRE(result);
    CHECK(result->proposals[0].description == "quote \" backslash \\ caron \xC4\x8D");
}

TEST_CASE("suggest reports failures", "[songsuggest]") {
    SECTION("non-zero exit uses stderr") {
        const auto result = ss::interpretProposals(1, "", "  Failed to read file\n");
        REQUIRE_FALSE(result);
        CHECK(result.error() == "Failed to read file");
    }
    SECTION("non-zero exit without a message") {
        const auto result = ss::interpretProposals(2, "", "");
        REQUIRE_FALSE(result);
        CHECK(result.error() == "chords_converter exited with code 2");
    }
    SECTION("truncated json") {
        const std::string json = slurp(converterData / "shrink.json");
        CHECK_FALSE(ss::interpretProposals(0, json.substr(0, json.size() / 2), ""));
        CHECK_FALSE(ss::interpretProposals(0, "", ""));
    }
    SECTION("trailing garbage") {
        CHECK_FALSE(ss::interpretProposals(0, slurp(converterData / "shrink.json") + "x", ""));
    }
    SECTION("wrong shape") {
        CHECK_FALSE(ss::interpretProposals(0, "[]", ""));
        CHECK_FALSE(ss::interpretProposals(0, "{}", ""));
        CHECK_FALSE(ss::interpretProposals(0, R"({"proposals":{}})", ""));
        CHECK_FALSE(ss::interpretProposals(0, R"({"proposals":[{"id":"a"}]})", ""));
        CHECK_FALSE(ss::interpretProposals(0, R"({"proposals":[{"id":"a@1","stage":1,"gain_lines":2,
            "score":1,"grade":"g","description":"d","edits":[{"start":5,"end":4,"replacement":""}]}]})", ""));
        CHECK_FALSE(ss::interpretProposals(0, R"({"proposals":[{"id":"a@1","stage":1,"gain_lines":2,
            "score":1,"grade":"g","description":"d","sources":[{"line":1,"start":2}]}]})", ""));
        // a 0.2.0 proposal: no score and grade
        CHECK_FALSE(ss::interpretProposals(
            0, R"({"proposals":[{"id":"a@1","stage":1,"gain_lines":2,"description":"d"}]})", ""));
        CHECK_FALSE(ss::interpretProposals(0, R"({"proposals":[],"fits":"yes"})", ""));
        CHECK_FALSE(ss::interpretProposals(0, R"({"proposals":[],"measured":{"a4":{"pages":1}}})", ""));
    }
}

TEST_CASE("apply interprets real runs", "[songsuggest]") {
    const auto ok = ss::interpretApply(exitCode("apply"), slurp(converterData / "apply.out"),
                                       slurp(converterData / "apply.err"));
    REQUIRE(ok);
    CHECK(*ok == slurp(converterData / "apply.out"));
    CHECK(ok->find("\\beginprechorus\\chordsoffonsmallpage") != std::string::npos);

    const auto stale = ss::interpretApply(exitCode("apply-stale"),
                                          slurp(converterData / "apply-stale.out"),
                                          slurp(converterData / "apply-stale.err"));
    REQUIRE_FALSE(stale);
    CHECK(stale.error().starts_with("No proposal 'nosuch@999'"));

    for (const char* out : {"", "\n", "garbage"}) {
        const auto empty = ss::interpretApply(0, out, "");
        REQUIRE_FALSE(empty);
        CHECK(empty.error() == "chords_converter apply printed no song");
    }

    const auto silent = ss::interpretApply(1, "", "");
    REQUIRE_FALSE(silent);
    CHECK(silent.error() == "chords_converter apply exited with code 1");
}

TEST_CASE("version output is parsed", "[converter]") {
    const auto version = cv::parseVersion(slurp(converterData / "version.out"));
    REQUIRE(version);
    CHECK(*version == cv::Version{0, 10, 0});
    CHECK(cv::toString(*version) == "0.10.0");
    CHECK(*version >= cv::kRequired);

    CHECK(cv::parseVersion("chords_converter 0.10.3\r\nmore")->minor == 10);
    CHECK(cv::parseVersion("chords_converter 1.0.0-beta")->major == 1);
    CHECK(*cv::parseVersion("chords_converter 0.9.0") < cv::kRequired);
    CHECK(*cv::parseVersion("chords_converter 0.10.1") > cv::kRequired);
}

TEST_CASE("unparseable version output gives nothing", "[converter]") {
    CHECK_FALSE(cv::parseVersion(""));
    CHECK_FALSE(cv::parseVersion("error: unexpected argument '--version' found"));
    CHECK_FALSE(cv::parseVersion("chords_converter"));
    CHECK_FALSE(cv::parseVersion("chords_converter 0.2"));
    CHECK_FALSE(cv::parseVersion("chords_converter a.b.c"));
    CHECK_FALSE(cv::parseVersion("chords_converter 0.2.0x"));
    CHECK_FALSE(cv::parseVersion("other 0.2.0"));
    CHECK_FALSE(cv::parseVersion("\nchords_converter 0.2.0"));
}

namespace {
struct TempRoot {
    fs::path path;
    TempRoot() {
        std::random_device device;
        path = fs::temp_directory_path() / ("zub-paper-test-" + std::to_string(device()));
        fs::create_directories(path / "zubuvzpevnik");
    }
    ~TempRoot() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void write(const std::string& cls, const std::string& text) const {
        std::ofstream(path / "zubuvzpevnik" / (cls + ".cls"), std::ios::binary) << text;
    }
};
} // namespace

TEST_CASE("paperOf reads the base class options", "[songsuggest][preview]") {
    TempRoot root;
    root.write("small", "\\ProvidesClass{small}\n\\LoadClass[a5paper,twoside,10pt]{book}\n");
    root.write("big", "\\LoadClass[a4paper]{article}\n");
    root.write("commented", "% \\LoadClass[a4paper]{article}\n\\LoadClass[a5paper]{book}\n");
    root.write("none", "\\LoadClass{article}\n");
    root.write("later", "\\PassOptionsToClass{a4paper}{x}\n\\LoadClass[10pt]{book}\n");
    CHECK(zub::preview::paperOf(root.path, "small") == ss::Paper::A5);
    CHECK(zub::preview::paperOf(root.path, "big") == ss::Paper::A4);
    CHECK(zub::preview::paperOf(root.path, "commented") == ss::Paper::A5);
    CHECK_FALSE(zub::preview::paperOf(root.path, "none"));
    CHECK_FALSE(zub::preview::paperOf(root.path, "later"));
    CHECK_FALSE(zub::preview::paperOf(root.path, "missing"));
    CHECK_FALSE(zub::preview::paperOf(root.path, "../small"));
    CHECK_FALSE(zub::preview::paperOf(root.path, ""));
}
