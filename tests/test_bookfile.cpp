#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <unistd.h>
#include <zub/core/bookfile.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;
using namespace zub::bookfile;
using Strings = std::vector<std::string>;

namespace {

const fs::path root = fs::path(ZUB_TEST_DATA_DIR) / "songbook";

std::string fixture(const std::string& rel) {
    const auto text = zub::readTextFile(root / rel);
    REQUIRE(text);
    return *text;
}

// A book around a song list body.
std::string wrap(const std::string& body, const std::string& before = "") {
    return "\\documentclass{velkyzpevnik}\n\\begin{document}\n" + before +
           "\\begin{songs}{obsah}\n" + body + "\\end{songs}\n\\end{document}\n";
}

Book parsed(const std::string& text) {
    auto b = parse(text);
    REQUIRE(b);
    return *b;
}

std::string set(const std::string& text, const Strings& names, std::size_t env = 0) {
    auto r = setSongs(text, env, names);
    INFO((r ? "" : r.error()));
    REQUIRE(r);
    return *r;
}

} // namespace

TEST_CASE("parse reads the header of a book", "[bookfile]") {
    const auto b = parsed(fixture("songbooks/big/big.tex"));
    CHECK(b.documentClass == "velkyzpevnik");
    CHECK(b.title == "Big Test Book");
    CHECK(b.author == "Tester");
    CHECK(b.indexes.empty());
    REQUIRE(b.environments.size() == 1);
    const auto& e = b.environments[0];
    CHECK(e.section.empty());
    CHECK(e.indexes == Strings{"obsah", "obsahAutor"});
    CHECK(e.readOnly.empty());
    REQUIRE(e.units.size() == 6);
    CHECK(e.units[0].songs == Strings{"Abba/Alpha"});
    CHECK(e.units[2].songs == Strings{"Cee/Charlie"});
    CHECK(e.units[3].block);
    CHECK(e.units[3].songs == Strings{"Delta/Six", "Delta/Seven"});
    CHECK(e.units[5].songs == Strings{"Hotel/Last"});
    CHECK(b.songs() == Strings{"Abba/Alpha", "Beta/Bravo", "Cee/Charlie", "Delta/Six",
                               "Delta/Seven", "Foxtrot/Fox", "Hotel/Last"});
}

TEST_CASE("unit ranges are the leading plain lines plus the unit's own lines", "[bookfile]") {
    const auto text = fixture("songbooks/big/big.tex");
    const auto b = parsed(text);
    const auto& e = b.environments[0];
    const auto slice = [&](const Unit& u) { return text.substr(u.begin, u.end - u.begin); };
    CHECK(slice(e.units[0]) == "\n    \\input{Abba/Alpha}\n");
    CHECK(slice(e.units[1]) == "    % TODO: Beta/Wished\n    \\input{Beta/Bravo}\n");
    CHECK(slice(e.units[3]) ==
          "    \\beginkeeporder % six before seven, not the alphabet\n"
          "    \\input{Delta/Six}\n    \\input{Delta/Seven}\n    \\endkeeporder\n");
    CHECK(slice(e.units[4]).starts_with("    %\\input{Echo/Parked}\n"));
    CHECK(text.substr(e.bodyEnd).starts_with("\\end{songs}"));
    CHECK(text.substr(e.bodyBegin, 1) == "\n");
    CHECK(text.substr(0, e.bodyBegin).ends_with("\\begin{songs}{obsah, obsahAutor}\n"));
}

TEST_CASE("parse reads sections, \\newindex and several environments", "[bookfile]") {
    const auto b = parsed(fixture("songbooks/bands/one/one.tex"));
    CHECK(b.indexes ==
          std::vector<std::pair<std::string, std::string>>{{"indexAA", "AA"}, {"indexBB", "BB"}});
    REQUIRE(b.environments.size() == 3);
    CHECK(b.environments[0].section == "First Album");
    CHECK(b.environments[0].indexes == Strings{"indexAA"});
    CHECK(b.environments[0].units.size() == 1);
    CHECK(b.environments[1].section == "Second Album");
    CHECK(b.environments[1].units.empty());
    CHECK(b.environments[1].bodyBegin == b.environments[1].bodyEnd);
    CHECK(b.environments[2].section == "Third: Album");
    CHECK(b.environments[2].indexes == Strings{"indexCC", "indexAA"});
    CHECK(b.songs() == Strings{"Band/One", "Band/Two", "Band/Three"});
}

TEST_CASE("parse reads tab-indented books with commented inputs", "[bookfile]") {
    const auto b = parsed(fixture("songbooks/camps/2025/camp2025.tex"));
    CHECK(b.documentClass == "taborovyzpevnik");
    CHECK(b.songs() == Strings{"Camp/Arwen", "Camp/Bravo", "Camp/Charlie", "Camp/Delta"});
}

TEST_CASE("a \\songsection inside a body does not name the next environment", "[bookfile]") {
    const auto text = "\\begin{songs}{a}\n\\songsection{Inner}\n\\input{A/B}\n\\end{songs}\n"
                      "\\begin{songs}{b}\n\\end{songs}\n";
    const auto b = parsed(text);
    CHECK(b.environments[0].section.empty());
    CHECK(b.environments[1].section.empty());
}

TEST_CASE("parse handles code after \\begin{songs} and text before it", "[bookfile]") {
    const auto b = parsed("x \\begin{songs}{a}\\newpage\n\\input{A/B}\n\\end{songs}\n");
    REQUIRE(b.environments.size() == 1);
    CHECK(b.environments[0].readOnly.empty());
    CHECK(b.songs() == Strings{"A/B"});
}

TEST_CASE("parse reports malformed environments", "[bookfile]") {
    CHECK_FALSE(parse("\\begin{songs}{a}\n\\input{A/B}\n"));
    CHECK_FALSE(parse("\\begin{songs}\n\\end{songs}\n"));
    CHECK_FALSE(parse("\\begin{songs}{a}\n\\begin{songs}{b}\n\\end{songs}\n"));
}

TEST_CASE("lines that cannot be edited make the environment read-only", "[bookfile]") {
    SECTION("two inputs on a line") {
        const auto b = parsed(wrap("\\input{A/B}\n\\input{A/C} \\input{A/D}\n"));
        CHECK(b.environments[0].readOnly.starts_with("line 5: "));
        CHECK(b.songs() == Strings{"A/B", "A/C", "A/D"});
    }
    SECTION("input with other code") {
        CHECK(!parsed(wrap("\\input{A/B}\\newpage\n")).environments[0].readOnly.empty());
        CHECK(!parsed(wrap("\\newpage\\input{A/B}\n")).environments[0].readOnly.empty());
        CHECK(!parsed(wrap("\\input{}\n")).environments[0].readOnly.empty());
    }
    SECTION("\\end{songs} with other code") {
        const auto b = parsed("\\begin{songs}{a}\n\\input{A/B}\nx\\end{songs}\n");
        CHECK(!b.environments[0].readOnly.empty());
        CHECK(b.songs() == Strings{"A/B"});
    }
    SECTION("unbalanced keep-order markers") {
        CHECK(!parsed(wrap("\\beginkeeporder\n\\input{A/B}\n")).environments[0].readOnly.empty());
        CHECK(!parsed(wrap("\\endkeeporder\n")).environments[0].readOnly.empty());
        CHECK(!parsed(wrap("\\beginkeeporder\n\\beginkeeporder\n\\endkeeporder\n"))
                   .environments[0]
                   .readOnly.empty());
        CHECK(!parsed(wrap("\\beginkeeporder x\n")).environments[0].readOnly.empty());
    }
    SECTION("a longer command name is not a marker") {
        CHECK(parsed(wrap("\\beginkeeporderx\n")).environments[0].readOnly.empty());
    }
    SECTION("commented tokens are plain lines") {
        const auto b = parsed(wrap("% \\input{A/B}\n\\input{A/C} % \\input{A/D}\n"));
        CHECK(b.environments[0].readOnly.empty());
        CHECK(b.songs() == Strings{"A/C"});
    }
}

TEST_CASE("setSongs with the same songs returns the text unchanged", "[bookfile]") {
    for (const auto* rel : {"songbooks/big/big.tex", "songbooks/bands/one/one.tex",
                            "songbooks/camps/2025/camp2025.tex"}) {
        const auto text = fixture(rel);
        const auto b = parsed(text);
        for (std::size_t e = 0; e < b.environments.size(); ++e) {
            INFO(rel << " environment " << e);
            Strings names;
            for (const auto& u : b.environments[e].units)
                names.insert(names.end(), u.songs.begin(), u.songs.end());
            CHECK(set(text, names, e) == text);
        }
    }
}

TEST_CASE("setSongs reorders units with their wish lines", "[bookfile]") {
    const auto text = wrap(
        "\n    \\input{A/One}\n    % TODO: X/Wish\n    \\input{B/Two}\n    \\input{C/Three}\n\n");
    const auto out = set(text, {"C/Three", "B/Two", "A/One"});
    // the leading lines (the blank line, the wish) moved with their inputs
    CHECK(out == wrap("    \\input{C/Three}\n    % TODO: X/Wish\n    \\input{B/Two}\n\n    "
                      "\\input{A/One}\n\n"));
}

TEST_CASE("setSongs moves a block whole and keeps its inner order", "[bookfile]") {
    const auto text = fixture("songbooks/big/big.tex");
    const auto out = set(text, {"Hotel/Last", "Delta/Seven", "Delta/Six", "Abba/Alpha",
                                "Beta/Bravo", "Cee/Charlie", "Foxtrot/Fox"});
    const auto b = parsed(out);
    CHECK(b.songs() == Strings{"Hotel/Last", "Delta/Six", "Delta/Seven", "Abba/Alpha", "Beta/Bravo",
                               "Cee/Charlie", "Foxtrot/Fox"});
    CHECK(out.find("\\beginkeeporder % six before seven, not the alphabet\n    \\input{Delta/Six}\n"
                   "    \\input{Delta/Seven}\n    \\endkeeporder\n") != std::string::npos);
    CHECK(b.environments[0].units[1].block);
    // the wish line moved with Bravo, the unit order of the others is the slot order
    CHECK(out.find("    % TODO: Beta/Wished\n    \\input{Beta/Bravo}\n") != std::string::npos);
}

TEST_CASE("setSongs removes a bare input and keeps its wish lines", "[bookfile]") {
    const auto text =
        wrap("    \\input{A/One}\n    % TODO: X/Wish\n    \\input{B/Two}\n    \\input{C/Three}\n");
    CHECK(set(text, {"A/One", "C/Three"}) ==
          wrap("    \\input{A/One}\n    % TODO: X/Wish\n    \\input{C/Three}\n"));
    CHECK(set(text, {}) == wrap("    % TODO: X/Wish\n"));
}

TEST_CASE("setSongs comments out a removed input with a trailing comment", "[bookfile]") {
    const auto text =
        wrap("    \\input{A/One} % TODO: note\n\t\\input{B/Two}\t%x\n    \\input{C/Three}\n");
    CHECK(
        set(text, {"C/Three"}) ==
        wrap("    % \\input{A/One} % TODO: note\n\t% \\input{B/Two}\t%x\n    \\input{C/Three}\n"));
}

TEST_CASE("setSongs and blocks", "[bookfile]") {
    const auto text = wrap(
        "    \\input{A/One}\n    \\beginkeeporder % why\n    \\input{B/Two}\n"
        "    % inside\n    \\input{C/Three} % note\n    \\endkeeporder\n    \\input{D/Four}\n");
    SECTION("no song of the block is kept") {
        CHECK(set(text, {"A/One", "D/Four"}) ==
              wrap("    \\input{A/One}\n    % inside\n    % \\input{C/Three} % note\n    "
                   "\\input{D/Four}\n"));
    }
    SECTION("some songs are kept") {
        CHECK(set(text, {"A/One", "C/Three", "D/Four"}) ==
              wrap("    \\input{A/One}\n    \\beginkeeporder % why\n    % inside\n"
                   "    \\input{C/Three} % note\n    \\endkeeporder\n    \\input{D/Four}\n"));
        CHECK(set(text, {"A/One", "B/Two", "D/Four"}) ==
              wrap("    \\input{A/One}\n    \\beginkeeporder % why\n    \\input{B/Two}\n"
                   "    % inside\n    % \\input{C/Three} % note\n    \\endkeeporder\n    "
                   "\\input{D/Four}\n"));
    }
    SECTION("the block's own order is not changed by names") {
        CHECK(set(text, {"A/One", "C/Three", "B/Two", "D/Four"}) == text);
    }
}

TEST_CASE("setSongs adds new songs after their predecessor", "[bookfile]") {
    const auto text = wrap("    \\input{A/One}\n    \\beginkeeporder\n    \\input{B/Two}\n"
                           "    \\input{C/Three}\n    \\endkeeporder\n    \\input{D/Four}\n");
    SECTION("after a block") {
        CHECK(set(text, {"A/One", "B/Two", "C/Three", "N/New1", "N/New2", "D/Four"}) ==
              wrap("    \\input{A/One}\n    \\beginkeeporder\n    \\input{B/Two}\n"
                   "    \\input{C/Three}\n    \\endkeeporder\n    \\input{N/New1}\n    "
                   "\\input{N/New2}\n"
                   "    \\input{D/Four}\n"));
    }
    SECTION("without a predecessor") {
        CHECK(
            set(text, {"N/New", "A/One", "B/Two", "C/Three", "D/Four"}) ==
            wrap(
                "    \\input{N/New}\n    \\input{A/One}\n    \\beginkeeporder\n    \\input{B/Two}\n"
                "    \\input{C/Three}\n    \\endkeeporder\n    \\input{D/Four}\n"));
    }
    SECTION("at the end") {
        CHECK(set(text, {"A/One", "B/Two", "C/Three", "D/Four", "N/New"}) ==
              text.substr(0, text.find("\\end{songs}")) + "    \\input{N/New}\n" +
                  text.substr(text.find("\\end{songs}")));
    }
}

TEST_CASE("setSongs adds to an empty or blank environment", "[bookfile]") {
    CHECK(set(wrap(""), {"A/One", "B/Two"}) == wrap("    \\input{A/One}\n    \\input{B/Two}\n"));
    CHECK(set(wrap("\n"), {"A/One"}) == wrap("    \\input{A/One}\n\n"));
    const auto b = fixture("songbooks/bands/one/one.tex");
    const auto out = set(b, {"Band/Four"}, 1);
    CHECK(out.find("\\begin{songs}{indexBB}\n\t\\input{Band/Four}\n\\end{songs}") !=
          std::string::npos);
}

TEST_CASE("setSongs uncomments a commented-out input of that name", "[bookfile]") {
    const auto text =
        wrap("    \\input{A/One}\n    % \\input{B/Two}  % parked\n    %\\input{C/Three}\n"
             "    \\input{D/Four}\n");
    CHECK(set(text, {"A/One", "B/Two", "C/Three", "D/Four"}) ==
          wrap("    \\input{A/One}\n    \\input{B/Two}  % parked\n    \\input{C/Three}\n    "
               "\\input{D/Four}\n"));
    // it is ordered like any unit
    CHECK(set(text, {"C/Three", "A/One", "D/Four"}) ==
          wrap("    % \\input{B/Two}  % parked\n    \\input{C/Three}\n    \\input{A/One}\n"
               "    \\input{D/Four}\n"));
    // a different name does not match, a wish line is not a commented input
    CHECK(set(text, {"A/One", "D/Four", "B/Other"}) ==
          wrap("    \\input{A/One}\n    % \\input{B/Two}  % parked\n    %\\input{C/Three}\n"
               "    \\input{D/Four}\n    \\input{B/Other}\n"));
}

TEST_CASE("setSongs uncomments in the fixtures", "[bookfile]") {
    const auto text = fixture("songbooks/camps/2025/camp2025.tex");
    const auto out =
        set(text, {"Camp/Arwen", "Camp/Bravo", "Camp/Parked", "Camp/Charlie", "Camp/Delta"});
    const auto expected = [&] {
        auto t = text;
        t.replace(t.find("%\\input{Camp/Parked}"), 20, "\\input{Camp/Parked}");
        return t;
    }();
    CHECK(out == expected);
}

TEST_CASE("setSongs copies the indent of the environment, else the book", "[bookfile]") {
    const auto camp = fixture("songbooks/camps/2025/camp2025.tex");
    const auto out = set(camp, {"Camp/Arwen", "Camp/New"});
    CHECK(out.find("\t\\input{Camp/New}\n") != std::string::npos);

    const auto one = fixture("songbooks/bands/one/one.tex");
    // environment 1 is empty: the first input line of the book is "\t\input{Band/One}"
    CHECK(set(one, {"Band/New"}, 1).find("\t\\input{Band/New}") != std::string::npos);
    // no input line anywhere: four spaces
    CHECK(set(wrap(""), {"A/B"}).find("\n    \\input{A/B}\n") != std::string::npos);
}

TEST_CASE("setSongs keeps CRLF line breaks", "[bookfile]") {
    std::string text = wrap("    \\input{A/One}\n    % TODO: x\n    \\input{B/Two} % n\n");
    std::string crlf;
    for (const char c : text) {
        if (c == '\n')
            crlf += '\r';
        crlf += c;
    }
    const auto b = parsed(crlf);
    REQUIRE(b.environments[0].units.size() == 2);
    CHECK(b.songs() == Strings{"A/One", "B/Two"});
    CHECK(set(crlf, {"A/One", "B/Two"}) == crlf);
    const auto out = set(crlf, {"B/Two", "N/New"});
    CHECK(out == "\\documentclass{velkyzpevnik}\r\n\\begin{document}\r\n\\begin{songs}{obsah}\r\n"
                 "    % TODO: x\r\n    \\input{B/Two} % n\r\n    \\input{N/New}\r\n"
                 "\\end{songs}\r\n\\end{document}\r\n");
}

TEST_CASE("setSongs refuses what it cannot do", "[bookfile]") {
    const auto text = wrap("    \\input{A/One}\n");
    CHECK_FALSE(setSongs(text, 1, {"A/One"}));
    CHECK_FALSE(setSongs(text, 0, {"A/One", "A/One"}));
    CHECK_FALSE(setSongs(text, 0, {"../x"}));
    CHECK_FALSE(setSongs(text, 0, {"A\\B"}));
    CHECK_FALSE(setSongs(text, 0, {""}));
    CHECK_FALSE(setSongs(wrap("\\input{A/B} \\input{A/C}\n"), 0, {"A/B"}));
    CHECK_FALSE(setSongs("\\begin{songs}{a}\n", 0, {}));
}

TEST_CASE("leaders and expandLeaders", "[bookfile]") {
    const auto b = parsed(fixture("songbooks/big/big.tex"));
    const auto& e = b.environments[0];
    CHECK(leaders(e) == Strings{"Abba/Alpha", "Beta/Bravo", "Cee/Charlie", "Delta/Six",
                                "Foxtrot/Fox", "Hotel/Last"});
    CHECK(expandLeaders(e, {"Hotel/Last", "Delta/Six", "Abba/Alpha", "Unknown/Single"}) ==
          Strings{"Hotel/Last", "Delta/Six", "Delta/Seven", "Abba/Alpha", "Unknown/Single"});
    CHECK(expandLeaders(e, leaders(e)) == b.songs());

    const auto empty = parsed(wrap("\\beginkeeporder\n\\endkeeporder\n\\input{A/B}\n"));
    CHECK(leaders(empty.environments[0]) == Strings{"A/B"});
}

TEST_CASE("indexFiles maps index names to their files", "[bookfile]") {
    CHECK(indexFiles(parsed(fixture("songbooks/big/big.tex"))) == Strings{"obsah", "obsahAutor"});
    // indexBB's environment is empty but still names it; indexCC has no \newindex; indexAA twice
    CHECK(indexFiles(parsed(fixture("songbooks/bands/one/one.tex"))) ==
          Strings{"AA", "BB", "indexCC"});
}

TEST_CASE("findBooks lists the books with a \\documentclass", "[bookfile]") {
    const auto books = findBooks(root);
    CHECK(books == std::vector<fs::path>{"songbooks/bands/one/one.tex", "songbooks/big/big.tex",
                                         "songbooks/camps/2025/camp2025.tex"});
    CHECK(findBooks(root / "songs").empty());

    // `projects` is the fallback, a commented \documentclass does not count
    const fs::path dir = fs::temp_directory_path() / ("zub-bookfile-" + std::to_string(::getpid()));
    fs::create_directories(dir / "projects" / "p");
    std::ofstream(dir / "projects" / "p" / "p.tex") << "\\documentclass{x}\n";
    std::ofstream(dir / "projects" / "p" / "q.tex") << "% \\documentclass{x}\n";
    std::ofstream(dir / "projects" / "p" / "r.txt") << "\\documentclass{x}\n";
    CHECK(findBooks(dir) == std::vector<fs::path>{"projects/p/p.tex"});
    fs::remove_all(dir);
}

// Hidden: runs against the real ../ZubuvZpevnik checkout, which changes on its own.
// Run with: build/debug/tests/zub_core_tests "[integration]"
TEST_CASE("every real book parses and round-trips", "[.integration][bookfile]") {
    const fs::path repo{ZUB_REAL_SONGBOOK_DIR};
    const auto books = findBooks(repo);
    if (books.empty())
        SKIP("no songbook checkout at " << repo);
    for (const auto& rel : books) {
        INFO(rel.string());
        const auto text = zub::readTextFile(repo / rel);
        REQUIRE(text);
        const auto b = parse(*text);
        REQUIRE(b);
        for (std::size_t e = 0; e < b->environments.size(); ++e) {
            INFO("environment " << e);
            CHECK(b->environments[e].readOnly.empty());
            Strings names;
            for (const auto& u : b->environments[e].units)
                names.insert(names.end(), u.songs.begin(), u.songs.end());
            const auto again = setSongs(*text, e, names);
            REQUIRE(again);
            CHECK(*again == *text);
        }
        for (const auto& name : b->songs()) {
            INFO(name);
            CHECK((fs::exists(repo / "songs" / "chords" / (name + ".tex")) ||
                   fs::exists(repo / "songs" / "tinwhistle" / (name + ".tex"))));
        }

        // the -i list of the build.sh line that names this book
        const auto script = zub::readTextFile((repo / rel).parent_path() / "build.sh");
        if (!script)
            continue;
        std::istringstream lines(*script);
        std::string line;
        while (std::getline(lines, line)) {
            if (line.find(rel.stem().string() + ".tex") == std::string::npos ||
                line.find("-i") == std::string::npos || line.starts_with("#"))
                continue;
            Strings expected;
            static const std::regex flag(R"re(\s-i\s+"?([A-Za-z0-9_]+)"?)re");
            for (std::sregex_iterator it(line.begin(), line.end(), flag), end; it != end; ++it)
                expected.push_back((*it)[1]);
            CHECK(indexFiles(*b) == expected);
            break;
        }
    }
}

TEST_CASE("bookfile builds a repo book from a copy in the work dir", "[bookfile]") {
    const auto command = zub::bookfile::buildCommand("/sb", "/pkg", "/work", "/sb/songbooks/t/2025",
                                                     "book", {"obsah", "obsahAutor"});
    CHECK(command.program == "bash");
    CHECK(command.arguments == std::vector<std::string>{"/pkg/scripts/build.sh", "-i", "obsah",
                                                        "-i", "obsahAutor", "/work/book.tex"});
    CHECK(command.workingDirectory == "/work");
    REQUIRE(command.environment.size() == 1);
    CHECK(command.environment[0].second ==
          ".:/work:/sb/songbooks/t/2025:/sb/songs/chords:/sb/songs/tinwhistle:/sb:/pkg:");
    CHECK(command.pdf == "/work/book.pdf");
    CHECK(command.log == "/work/book.log");
}
