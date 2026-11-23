#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <unistd.h>
#include <zub/core/bookfile.hpp>
#include <zub/core/safety.hpp>
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

// ---- Transposing ----

namespace {

SongTranspose tr(int n, const std::string& key = "G", bool capo = false, bool fixed = false) {
    return SongTranspose{"", n, capo, key, fixed};
}

std::string transposed(const std::string& text, const std::string& name,
                       const std::optional<SongTranspose>& t) {
    auto r = setTranspose(text, name, t);
    INFO((r ? "" : r.error()));
    REQUIRE(r);
    return *r;
}

} // namespace

TEST_CASE("transpose settings: read, add, change, remove", "[bookfile][transpose]") {
    const auto plain = wrap("    \\input{A/One}\n");
    CHECK_FALSE(readTransposeSettings(plain).key);
    TransposeSettings s;
    s.key = zub::booktranspose::Key{7, false};
    const auto added = setTransposeSettings(plain, s);
    CHECK(added == "% zpevnikar: key=G transpose=chords\n" + plain);
    CHECK(readTransposeSettings(added).key == s.key);
    CHECK(readTransposeSettings(added).mode == TransposeSettings::Mode::Chords);
    CHECK(setTransposeSettings(added, s) == added);

    s.key = zub::booktranspose::Key{4, true};
    s.mode = TransposeSettings::Mode::Capo;
    const auto changed = setTransposeSettings(added, s);
    CHECK(changed == "% zpevnikar: key=Em transpose=capo\n" + plain);
    CHECK(readTransposeSettings(changed).mode == TransposeSettings::Mode::Capo);
    CHECK(readTransposeSettings(changed).key == s.key);

    CHECK(setTransposeSettings(changed, TransposeSettings{}) == plain);
    CHECK(setTransposeSettings(plain, TransposeSettings{}) == plain);
}

TEST_CASE("transpose settings sit after the lock mark and keep CRLF", "[bookfile][transpose]") {
    const std::string lock = std::string(lockMark());
    const auto text = lock + "\n" + wrap("    \\input{A/One}\n");
    TransposeSettings s;
    s.key = zub::booktranspose::Key{10, false};
    const auto out = setTransposeSettings(text, s);
    CHECK(out == lock + "\n% zpevnikar: key=Bb transpose=chords\n" + wrap("    \\input{A/One}\n"));
    CHECK(setTransposeSettings(out, TransposeSettings{}) == text);
    CHECK(setLocked(out, false) ==
          setLocked(text, false).insert(0, "% zpevnikar: key=Bb transpose=chords\n"));

    std::string crlf;
    for (const char c : wrap("    \\input{A/One}\n"))
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    const auto withKey = setTransposeSettings(crlf, s);
    CHECK(withKey.starts_with("% zpevnikar: key=Bb transpose=chords\r\n\\documentclass"));
    CHECK(setTransposeSettings(withKey, TransposeSettings{}) == crlf);
    // the lock mark and the book key do not disturb each other
    CHECK(
        setLocked(withKey, true).starts_with(std::string(lockMark()) + "\r\n% zpevnikar: key=Bb"));
    CHECK(readSettings(out).locked);
}

TEST_CASE("setTranspose adds, replaces and removes the line above the input",
          "[bookfile][transpose]") {
    const auto text = wrap("    \\input{A/One}\n    \\input{B/Two}\n");
    const auto added = transposed(text, "B/Two", tr(-5));
    CHECK(added ==
          wrap("    \\input{A/One}\n    \\transposesong[key=G]{-5}\n    \\input{B/Two}\n"));
    const auto replaced = transposed(added, "B/Two", tr(3, "F\\shrp m", false, true));
    CHECK(replaced ==
          wrap("    \\input{A/One}\n    \\transposesong[key=F\\shrp m]{3} % fixed\n"
               "    \\input{B/Two}\n"));
    CHECK(transposed(replaced, "B/Two", tr(3, "F\\shrp m", false, true)) == replaced);
    // the package has no capo option: never written
    CHECK_FALSE(setTranspose(text, "B/Two", tr(3, "G", true)));
    CHECK(transposed(replaced, "B/Two", std::nullopt) == text);
    CHECK(transposed(replaced, "B/Two", tr(0, "G")) == transposed(replaced, "B/Two", std::nullopt));
    CHECK(transposed(text, "B/Two", std::nullopt) == text);
    CHECK(transposed(text, "B/Two", tr(0)) == text);
    // "do not transpose this song" is a fixed 0
    CHECK(transposed(text, "B/Two", tr(0, "", false, true)) ==
          wrap("    \\input{A/One}\n    \\transposesong{0} % fixed\n    \\input{B/Two}\n"));
    CHECK(setTranspose(text, "X/Missing", tr(1)).error().find("X/Missing") != std::string::npos);
}

TEST_CASE("readTransposes", "[bookfile][transpose]") {
    const auto text =
        wrap("    \\input{A/One}\n    \\transposesong[capo,key=E\\flt]{-3} % fixed\n"
             "    \\input{B/Two}\n    % \\transposesong[key=G]{2}\n    \\input{C/Three}\n"
             "    \\transposesong[key=G]{2}\n\n    \\input{D/Four}\n"
             "    \\transposesong[key=G]{+2}\n    \\input{E/Five}\n"
             "    \\transposesong[bogus]{2}\n    \\input{F/Six}\n");
    const auto all = readTransposes(text);
    REQUIRE(all.size() == 2);
    CHECK(all[0].inputName == "B/Two");
    CHECK(all[0].semitones == -3);
    CHECK(all[0].capo);
    CHECK(all[0].key == "E\\flt");
    CHECK(all[0].fixed);
    CHECK(all[1].inputName == "E/Five");
    CHECK(all[1].semitones == 2);
    CHECK_FALSE(all[1].capo);
    CHECK_FALSE(all[1].fixed);
}

TEST_CASE("setTranspose: indent, CRLF, keep-order blocks", "[bookfile][transpose]") {
    const auto tabs = wrap("\t\\input{A/One}\n");
    CHECK(transposed(tabs, "A/One", tr(2)) ==
          wrap("\t\\transposesong[key=G]{2}\n\t\\input{A/One}\n"));
    const auto blocked = wrap("    \\beginkeeporder\n      \\input{A/One}\n      \\input{A/Two}\n"
                              "    \\endkeeporder\n    \\input{B/Three}\n");
    const auto out = transposed(blocked, "A/Two", tr(-2));
    CHECK(out ==
          wrap("    \\beginkeeporder\n      \\input{A/One}\n      \\transposesong[key=G]{-2}\n"
               "      \\input{A/Two}\n    \\endkeeporder\n    \\input{B/Three}\n"));
    CHECK(readTransposes(out).size() == 1);
    CHECK(parsed(out).songs() == parsed(blocked).songs());
    CHECK(transposed(out, "A/Two", std::nullopt) == blocked);

    std::string crlf;
    for (const char c : wrap("    \\input{A/One}\n"))
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    const auto withLine = transposed(crlf, "A/One", tr(1));
    CHECK(withLine.find("    \\transposesong[key=G]{1}\r\n    \\input{A/One}\r\n") !=
          std::string::npos);
    CHECK(withLine.find("\n") == withLine.find("\r\n") + 1);
    CHECK(readTransposes(withLine).size() == 1);
    CHECK(transposed(withLine, "A/One", tr(2)).find("{2}\r\n") != std::string::npos);
    CHECK(transposed(withLine, "A/One", std::nullopt) == crlf);
}

TEST_CASE("setSongs keeps the song line with its input", "[bookfile][transpose]") {
    const auto text = wrap("    \\input{A/One}\n    % TODO: X/Wish\n    \\transposesong[key=G]{2}\n"
                           "    \\input{B/Two}\n    \\transposesong[capo,key=G]{-1} % fixed\n"
                           "    \\input{C/Three}\n");
    CHECK(set(text, {"A/One", "B/Two", "C/Three"}) == text);
    const auto moved = set(text, {"C/Three", "B/Two", "A/One"});
    const auto lines = readTransposes(moved);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].inputName == "C/Three");
    CHECK(lines[0].semitones == -1);
    CHECK(lines[0].fixed);
    CHECK(lines[1].inputName == "B/Two");
    CHECK(lines[1].semitones == 2);
    CHECK(moved.find("% TODO: X/Wish\n    \\transposesong[key=G]{2}\n    \\input{B/Two}") !=
          std::string::npos);
    // a removed input takes its line with it, a kept one keeps its own
    const auto removed = set(text, {"A/One", "C/Three"});
    CHECK(
        removed ==
        wrap("    \\input{A/One}\n    % TODO: X/Wish\n    \\transposesong[capo,key=G]{-1} % fixed\n"
             "    \\input{C/Three}\n"));
    const auto removedLast = set(text, {"A/One", "B/Two"});
    CHECK(removedLast ==
          wrap("    \\input{A/One}\n    % TODO: X/Wish\n    \\transposesong[key=G]{2}\n"
               "    \\input{B/Two}\n"));
    CHECK(readTransposes(set(text, {})).empty());
    // a commented-out removed input keeps its note but not the line
    const auto commented =
        set(wrap("    \\transposesong[key=G]{2}\n    \\input{A/One} % hi\n"), {});
    CHECK(commented == wrap("    % \\input{A/One} % hi\n"));
}

TEST_CASE("setSongs and blocks with song lines", "[bookfile][transpose]") {
    const auto text =
        wrap("    \\beginkeeporder\n    \\transposesong[key=G]{1}\n    \\input{A/One}\n"
             "    \\transposesong[key=G]{2}\n    \\input{A/Two}\n    \\endkeeporder\n"
             "    \\input{B/Three}\n");
    CHECK(set(text, {"B/Three", "A/One", "A/Two"}) ==
          wrap("    \\input{B/Three}\n    \\beginkeeporder\n    \\transposesong[key=G]{1}\n    "
               "\\input{A/One}\n"
               "    \\transposesong[key=G]{2}\n    \\input{A/Two}\n    \\endkeeporder\n"));
    // one block song removed: its line goes, the other's stays
    CHECK(set(text, {"A/Two", "B/Three"}) ==
          wrap("    \\beginkeeporder\n    \\transposesong[key=G]{2}\n    \\input{A/Two}\n    "
               "\\endkeeporder\n"
               "    \\input{B/Three}\n"));
    // the whole block removed
    CHECK(set(text, {"B/Three"}) == wrap("    \\input{B/Three}\n"));
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
        for (const auto& wish : wishes(*text)) {
            INFO(wish.text << " line " << wish.line);
            CHECK(!wish.text.empty());
            CHECK((wish.name.empty() || wish.text.starts_with(wish.name)));
        }
        // the book's own text has no transposing yet, and the edits give it back
        for (const auto& t : readTransposes(*text))
            CHECK_FALSE(t.inputName.empty());
        CHECK(setTransposeSettings(*text, readTransposeSettings(*text)) == *text);
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
          ".:/work:/pkg:/sb/songbooks/t/2025:/sb:"); // /sb has no songs/ folders here
    CHECK(command.pdf == "/work/book.pdf");
    CHECK(command.log == "/work/book.log");
}

TEST_CASE("bookfile build TEXINPUTS lists every collection", "[bookfile]") {
    const auto repo = fs::path{ZUB_TEST_DATA_DIR} / "songbook";
    const auto command = zub::bookfile::buildCommand(repo, "/pkg", "/work", "/bd", "book", {});
    const auto root = repo.generic_string();
    CHECK(command.environment[0].second ==
          ".:/work:/pkg:" + root + "/songs/chords:" + root + "/songs/whistle2:/bd:" + root + ":");

    const auto script = zub::bookfile::buildScriptFor("b", zub::songbook::BookClass::Velky, {},
                                                      {"chords", "tinwhistle", "whistle2"});
    CHECK(script.contains("$REPO_ROOT/songs/chords:$REPO_ROOT/songs/tinwhistle:"
                          "$REPO_ROOT/songs/whistle2:${TEXINPUTS:-}"));
    CHECK(zub::bookfile::buildScriptFor("b", zub::songbook::BookClass::Velky, {})
              .contains("$REPO_ROOT/songs/chords:$REPO_ROOT/songs/tinwhistle:${TEXINPUTS:-}"));
}

TEST_CASE("bookfile: review findings", "[bookfile]") {
    namespace bf = zub::bookfile;
    const std::string head = "\\documentclass{velkyzpevnik}\n\\begin{document}\n";
    const std::string tail = "\\end{songs}\n\\end{document}\n";
    // A song listed twice: no edit can tell its lines apart.
    const auto twice = bf::parse(head +
                                 "\\begin{songs}{obsah}\n\\input{A/a}\n\\input{B/b}\n"
                                 "\\input{A/a}\n" +
                                 tail);
    REQUIRE(twice);
    CHECK(twice->environments[0].readOnly.find("twice") != std::string::npos);
    // A song on the \begin{songs} line is in no unit: read-only; \newpage there is fine.
    const auto onBegin =
        bf::parse(head + "\\begin{songs}{obsah}\\input{Z/z}\n\\input{A/a}\n" + tail);
    REQUIRE(onBegin);
    CHECK_FALSE(onBegin->environments[0].readOnly.empty());
    const auto newpage = bf::parse(head + "\\begin{songs}{obsah}\\newpage\n\\input{A/a}\n" + tail);
    REQUIRE(newpage);
    CHECK(newpage->environments[0].readOnly.empty());
    // A commented input right under a wish stays: the wish belongs to the song below it.
    const std::string wished = head +
                               "\\begin{songs}{obsah}\n    \\input{A/a}\n"
                               "    % TODO: wish for C\n    % \\input{X/x}\n"
                               "    \\input{C/c}\n" +
                               tail;
    const auto added = bf::setSongs(wished, 0, {"A/a", "C/c", "X/x"});
    REQUIRE(added);
    CHECK(*added == head +
                        "\\begin{songs}{obsah}\n    \\input{A/a}\n    % TODO: wish for C\n"
                        "    % \\input{X/x}\n    \\input{C/c}\n    \\input{X/x}\n" +
                        tail);
    // ... but one in a parked list, or right after a song, is uncommented.
    const std::string parked = head +
                               "\\begin{songs}{obsah}\n    \\input{A/a}\n"
                               "    % \\input{W/w}\n    % \\input{X/x}\n"
                               "    \\input{C/c}\n" +
                               tail;
    const auto uncommented = bf::setSongs(parked, 0, {"A/a", "C/c", "X/x"});
    REQUIRE(uncommented);
    CHECK(uncommented->find("    % \\input{W/w}\n    \\input{X/x}\n") != std::string::npos);
    // Every \input, wherever it is.
    CHECK(bf::inputs("\\renewcommand{\\campyear}{\\input{A/x}}\n% \\input{No/no}\n"
                     "\\begin{songs}{o}\\input{ B/b }\n\\input{A/x}\\input foo\n") ==
          std::vector<std::string>{"A/x", "B/b", ""});
}

// ---- Settings, lock, sections, new books ----

namespace {

namespace bf = zub::bookfile;

std::string setting(const std::string& text, bf::Field f, const std::string& value) {
    auto r = bf::setSetting(text, f, value);
    INFO((r ? "" : r.error()));
    REQUIRE(r);
    return *r;
}

std::string crlf(std::string s) {
    std::string out;
    for (const char c : s) {
        if (c == '\n')
            out += '\r';
        out += c;
    }
    return out;
}

const std::string velky = "\\documentclass[chorded, onesongcolumn, noshading]{velkyzpevnik}\n"
                          "\n\\title{T}\n\\author{A}\n\n\\begin{document}\n\n"
                          "\\makefrontcover{img/x}\n\\insertindex\n\n"
                          "\\begin{songs}{obsah,obsahAutor}\n\n\\end{songs}\n\n\\end{document}\n";

} // namespace

TEST_CASE("readSettings reads the fixtures", "[bookfile]") {
    const auto big = bf::readSettings(fixture("songbooks/big/big.tex"));
    CHECK(big.bookClass == "velkyzpevnik");
    CHECK(big.chorded);
    CHECK_FALSE(big.twoColumns);
    CHECK(big.shading);
    CHECK(big.title == "Big Test Book");
    CHECK(big.author == "Tester");
    CHECK(big.campYear.empty());
    CHECK(big.cover.empty());
    CHECK_FALSE(big.chordChart);
    CHECK_FALSE(big.locked);
    CHECK(big.notEditable.empty());

    const auto camp = bf::readSettings(fixture("songbooks/camps/2025/camp2025.tex"));
    CHECK(camp.bookClass == "taborovyzpevnik");
    CHECK(camp.campYear == "2025");
    CHECK(camp.cover.empty());
    CHECK(camp.notEditable.empty());

    const auto one = bf::readSettings(fixture("songbooks/bands/one/one.tex"));
    CHECK(one.title == "One Band");
    CHECK(one.notEditable.empty());
}

TEST_CASE("readSettings on inline books", "[bookfile]") {
    const auto s = bf::readSettings(
        "\\documentclass[lyric, twosongcolumns, noshading, foo]{malyzpevnik}\n"
        "% \\title{commented}\n\\title{T}\n\\begin{document}\n\\bindingleaf\n"
        "\\maketitlepage[img/m]\n\\insertallchords\n% zpevnikar: locked\n\\end{document}\n");
    CHECK(s.bookClass == "malyzpevnik");
    CHECK_FALSE(s.chorded);
    CHECK(s.twoColumns);
    CHECK_FALSE(s.shading);
    CHECK(s.title == "T");
    CHECK(s.cover == "img/m");
    CHECK(s.chordChart);
    CHECK(s.locked);
    CHECK(s.notEditable.empty());
    // a bare \makefrontcover (taborovyzpevnik) has no cover
    const auto t = bf::readSettings("\\documentclass{taborovyzpevnik}\n\\begin{document}\n"
                                    "\\makefrontcover\n\n\\end{document}\n");
    CHECK(t.cover.empty());
    CHECK(t.notEditable.empty());
}

TEST_CASE("setSetting: class and options", "[bookfile]") {
    using bf::Field;
    CHECK(setting(velky, Field::Class, "malyzpevnik")
              .starts_with("\\documentclass[chorded, onesongcolumn, noshading]{malyzpevnik}\n"));
    CHECK_FALSE(bf::setSetting(velky, Field::Class, "article"));
    CHECK(setting(velky, Field::Chorded, "0")
              .starts_with("\\documentclass[lyric, onesongcolumn, noshading]{velkyzpevnik}\n"));
    CHECK(setting(velky, Field::TwoColumns, "1")
              .starts_with("\\documentclass[chorded, twosongcolumns, noshading]{velkyzpevnik}\n"));
    CHECK(setting(velky, Field::Shading, "1")
              .starts_with("\\documentclass[chorded, onesongcolumn]{velkyzpevnik}\n"));
    // the first or the only option goes with its comma
    CHECK(setting("\\documentclass[noshading, foo]{velkyzpevnik}\n", Field::Shading, "1") ==
          "\\documentclass[foo]{velkyzpevnik}\n");
    CHECK(setting("\\documentclass[ noshading ]{velkyzpevnik}\n", Field::Shading, "1") ==
          "\\documentclass{velkyzpevnik}\n");
    // added: at the end, in new brackets, in empty ones; unknown options stay
    CHECK(setting("\\documentclass[foo,bar ]{velkyzpevnik}\n", Field::Shading, "0") ==
          "\\documentclass[foo,bar, noshading ]{velkyzpevnik}\n");
    CHECK(setting("\\documentclass{velkyzpevnik}\n", Field::Chorded, "0") ==
          "\\documentclass[lyric]{velkyzpevnik}\n");
    CHECK(setting("\\documentclass[]{velkyzpevnik}\n", Field::TwoColumns, "1") ==
          "\\documentclass[twosongcolumns]{velkyzpevnik}\n");
    CHECK(setting("\\documentclass[lyric]{velkyzpevnik}\n", Field::Chorded, "1") ==
          "\\documentclass[chorded]{velkyzpevnik}\n");
    // defaults: nothing to add
    const std::string plain = "\\documentclass[foo]{velkyzpevnik}\n";
    CHECK(setting(plain, Field::Chorded, "1") == plain);
    CHECK(setting(plain, Field::TwoColumns, "0") == plain);
    CHECK(setting(plain, Field::Shading, "1") == plain);
    CHECK_FALSE(bf::setSetting(plain, Field::Shading, "yes"));
    // a comment is not code
    CHECK(setting("\\documentclass[foo]{velkyzpevnik} % [noshading]\n", Field::Shading, "0") ==
          "\\documentclass[foo, noshading]{velkyzpevnik} % [noshading]\n");
}

TEST_CASE("setSetting: title, author, campyear", "[bookfile]") {
    using bf::Field;
    CHECK(setting(velky, Field::Title, "New \\& {x}").find("\\title{New \\& {x}}\n\\author{A}") !=
          std::string::npos);
    CHECK(setting(velky, Field::Author, "").find("\\author{}\n") != std::string::npos);
    CHECK_FALSE(bf::setSetting(velky, Field::Title, "a\nb"));
    CHECK_FALSE(bf::setSetting(velky, Field::Title, "a}"));
    CHECK_FALSE(bf::setSetting(velky, Field::Title, "{a"));
    CHECK_FALSE(bf::setSetting(velky, Field::Author, "50% off"));
    const std::string bare =
        "\\documentclass{velkyzpevnik}\n\n\\begin{document}\n\\end{document}\n";
    // missing: the title after \documentclass, the author after the title
    const auto t = setting(bare, Field::Title, "T");
    CHECK(t == "\\documentclass{velkyzpevnik}\n\\title{T}\n\n\\begin{document}\n\\end{document}\n");
    CHECK(setting(t, Field::Author, "A") ==
          "\\documentclass{velkyzpevnik}\n\\title{T}\n\\author{A}\n\n\\begin{document}\n"
          "\\end{document}\n");
    CHECK(setting(bare, Field::Author, "A") ==
          "\\documentclass{velkyzpevnik}\n\\author{A}\n\n\\begin{document}\n\\end{document}\n");
    // CRLF
    CHECK(setting(crlf(bare), Field::Title, "T") ==
          crlf("\\documentclass{velkyzpevnik}\n\\title{T}\n\n\\begin{document}\n"
               "\\end{document}\n"));

    const std::string camp = "\\documentclass{taborovyzpevnik}\n\n\\title{T}\n\\author{A}\n\n"
                             "\\renewcommand{\\campyear}{2025}\n\n\\begin{document}\n";
    CHECK(setting(camp, Field::CampYear, "2026").find("{\\campyear}{2026}\n") != std::string::npos);
    CHECK(setting(camp, Field::CampYear, "") ==
          "\\documentclass{taborovyzpevnik}\n\n\\title{T}\n\\author{A}\n\n\n\\begin{document}\n");
    const auto added = setting("\\documentclass{taborovyzpevnik}\n\n\\title{T}\n\\author{A}\n\n"
                               "\\begin{document}\n",
                               Field::CampYear, "2027");
    CHECK(added == "\\documentclass{taborovyzpevnik}\n\n\\title{T}\n\\author{A}\n"
                   "\\renewcommand{\\campyear}{2027}\n\n\\begin{document}\n");
    CHECK(setting(added, Field::CampYear, "") ==
          "\\documentclass{taborovyzpevnik}\n\n\\title{T}\n\\author{A}\n\n\\begin{document}\n");
}

TEST_CASE("setSetting: cover by class", "[bookfile]") {
    using bf::Field;
    // velkyzpevnik: \makefrontcover{F}, required
    CHECK(setting(velky, Field::Cover, "img/y").find("\\makefrontcover{img/y}\n") !=
          std::string::npos);
    const auto empty = bf::setSetting(velky, Field::Cover, "");
    REQUIRE_FALSE(empty);
    CHECK(empty.error() == "velkyzpevnik needs a cover image");
    const std::string none = "\\documentclass{velkyzpevnik}\n\\begin{document}\n\\insertindex\n"
                             "\\end{document}\n";
    CHECK(setting(none, Field::Cover, "img/a") ==
          "\\documentclass{velkyzpevnik}\n\\begin{document}\n\\makefrontcover{img/a}\n"
          "\\insertindex\n\\end{document}\n");
    CHECK(setting(none, Field::Cover, "") == none);
    CHECK_FALSE(bf::setSetting(velky, Field::Cover, "a]b"));
    CHECK_FALSE(bf::setSetting(velky, Field::Cover, "a\nb"));

    // malyzpevnik: \maketitlepage[F]
    const std::string maly = "\\documentclass{malyzpevnik}\n\\begin{document}\n"
                             "\\maketitlepage[img/m]\n\\end{document}\n";
    CHECK(setting(maly, Field::Cover, "img/n") ==
          "\\documentclass{malyzpevnik}\n\\begin{document}\n\\maketitlepage[img/n]\n"
          "\\end{document}\n");
    const auto bareMaly = setting(maly, Field::Cover, "");
    CHECK(bareMaly == "\\documentclass{malyzpevnik}\n\\begin{document}\n\\maketitlepage\n"
                      "\\end{document}\n");
    CHECK(setting(bareMaly, Field::Cover, "img/m") == maly);
    CHECK(setting(bareMaly, Field::Cover, "") == bareMaly);

    // taborovyzpevnik: \makefrontcover[F]
    const std::string tabor = "\\documentclass{taborovyzpevnik}\n\\begin{document}\n"
                              "\\makefrontcover[]\n\\end{document}\n";
    CHECK(setting(tabor, Field::Cover, "") == tabor); // current value: untouched
    CHECK(setting(tabor, Field::Cover, "c.pdf") ==
          "\\documentclass{taborovyzpevnik}\n\\begin{document}\n\\makefrontcover[c.pdf]\n"
          "\\end{document}\n");
    CHECK(setting("\\documentclass{taborovyzpevnik}\n\\begin{document}\n\\end{document}\n",
                  Field::Cover, "c.pdf") ==
          "\\documentclass{taborovyzpevnik}\n\\begin{document}\n\\makefrontcover[c.pdf]\n"
          "\\end{document}\n");

    // unknown forms
    CHECK(bf::readSettings("\\documentclass{velkyzpevnik}\n\\begin{document}\n\\makefrontcover\n")
              .notEditable.contains(Field::Cover));
    CHECK(bf::readSettings("\\documentclass{taborovyzpevnik}\n\\makefrontcover{x}\n")
              .notEditable.contains(Field::Cover));
    CHECK(bf::readSettings("\\documentclass{taborovyzpevnik}\n\\makefrontcover[a]\n"
                           "\\makefrontcover[b]\n")
              .notEditable.contains(Field::Cover));
    CHECK(bf::readSettings("\\documentclass{taborovyzpevnik}\n\\makefrontcover[a\nb]\n")
              .notEditable.contains(Field::Cover));
    CHECK_FALSE(bf::setSetting("\\documentclass{taborovyzpevnik}\n\\makefrontcover{x}\n",
                               Field::Cover, "y"));
}

TEST_CASE("setSetting: chord chart", "[bookfile]") {
    using bf::Field;
    const auto on = setting(velky, Field::ChordChart, "1");
    CHECK(on == "\\documentclass[chorded, onesongcolumn, noshading]{velkyzpevnik}\n"
                "\n\\title{T}\n\\author{A}\n\n\\begin{document}\n\n"
                "\\makefrontcover{img/x}\n\\insertindex\n\n"
                "\\begin{songs}{obsah,obsahAutor}\n\n\\end{songs}\n\\insertallchords\n\n"
                "\\end{document}\n");
    CHECK(setting(on, Field::ChordChart, "1") == on);
    CHECK(setting(on, Field::ChordChart, "0") == velky);
    CHECK(setting(velky, Field::ChordChart, "0") == velky);
    // the indentation of the \end{songs} line; CRLF
    CHECK(setting("\\documentclass{velkyzpevnik}\n  \\end{songs}\n", Field::ChordChart, "1") ==
          "\\documentclass{velkyzpevnik}\n  \\end{songs}\n  \\insertallchords\n");
    CHECK(setting(crlf(velky), Field::ChordChart, "1") == crlf(on));
    // after the last one; commented ones do not count; lines with a comment go whole
    const auto twice = setting("\\documentclass{x}\n\\end{songs}\n\\end{songs}\n\\end{document}\n",
                               Field::ChordChart, "1");
    CHECK(twice == "\\documentclass{x}\n\\end{songs}\n\\end{songs}\n\\insertallchords\n"
                   "\\end{document}\n");
    CHECK_FALSE(bf::readSettings("\\documentclass{x}\n%\\insertallchords\n").chordChart);
    CHECK(setting("\\documentclass{x}\n\\insertallchords % why\n\\insertallchords\nz\n",
                  Field::ChordChart, "0") == "\\documentclass{x}\nz\n");
    // without \end{songs}: before \end{document}
    CHECK(setting("\\documentclass{x}\n\\end{document}\n", Field::ChordChart, "1") ==
          "\\documentclass{x}\n\\insertallchords\n\\end{document}\n");
    // sharing a line with code
    const std::string shared = "\\documentclass{x}\n\\insertallchords \\newpage\n";
    CHECK(bf::readSettings(shared).notEditable.contains(Field::ChordChart));
    CHECK_FALSE(bf::setSetting(shared, Field::ChordChart, "0"));
}

TEST_CASE("setSetting: refusals and identity", "[bookfile]") {
    using bf::Field;
    CHECK_FALSE(bf::setSetting("\\title{T}\n", Field::Title, "x"));
    const std::string two = "\\documentclass{velkyzpevnik}\n\\title{A}\n\\title{B}\n"
                            "\\author{a\nb}\n";
    const auto s = bf::readSettings(two);
    CHECK(s.notEditable.contains(Field::Title));
    CHECK(s.notEditable.contains(Field::Author));
    CHECK_FALSE(s.notEditable.contains(Field::Class));
    CHECK_FALSE(bf::setSetting(two, Field::Title, "x"));
    CHECK(setting(two, Field::Class, "malyzpevnik").find("{malyzpevnik}") != std::string::npos);
    // every field set to its current value: unchanged, in LF and CRLF
    for (const auto& text : {velky, crlf(velky)}) {
        const auto cur = bf::readSettings(text);
        CHECK(setting(text, Field::Class, cur.bookClass) == text);
        CHECK(setting(text, Field::Chorded, cur.chorded ? "1" : "0") == text);
        CHECK(setting(text, Field::TwoColumns, cur.twoColumns ? "1" : "0") == text);
        CHECK(setting(text, Field::Shading, cur.shading ? "1" : "0") == text);
        CHECK(setting(text, Field::Title, cur.title) == text);
        CHECK(setting(text, Field::Author, cur.author) == text);
        CHECK(setting(text, Field::CampYear, cur.campYear) == text);
        CHECK(setting(text, Field::Cover, cur.cover) == text);
        CHECK(setting(text, Field::ChordChart, cur.chordChart ? "1" : "0") == text);
    }
    // toggling and back
    CHECK(setting(setting(velky, Field::Shading, "1"), Field::Shading, "0") == velky);
    CHECK(setting(setting(velky, Field::Chorded, "0"), Field::Chorded, "1") == velky);
}

TEST_CASE("the lock mark", "[bookfile]") {
    CHECK(bf::lockMark() == "% zpevnikar: locked");
    const auto locked = bf::setLocked(velky, true);
    CHECK(locked == "% zpevnikar: locked\n" + velky);
    CHECK(bf::readSettings(locked).locked);
    CHECK(bf::setLocked(locked, true) == locked);
    CHECK(bf::setLocked(locked, false) == velky);
    CHECK(bf::setLocked(velky, false) == velky);
    CHECK(bf::setLocked(crlf(velky), true) == "% zpevnikar: locked\r\n" + crlf(velky));
    CHECK(bf::setLocked(bf::setLocked(crlf(velky), true), false) == crlf(velky));
    // anywhere, every one; the other comments stay
    CHECK(bf::setLocked("a\n  % zpevnikar: locked  \n%% zpevnikar: locked\n% zpevnikar: locked\nb",
                        false) == "a\n%% zpevnikar: locked\nb");
    CHECK(bf::setLocked("", true) == "% zpevnikar: locked\n");
}

namespace {

const std::string sections =
    "\\documentclass{velkyzpevnik}\n\\begin{document}\n\n"
    "\\songsection{First}\n\\begin{songs}{obsah, obsahAutor}\n    \\input{A/a}\n\\end{songs}\n\n"
    "\\songsection{Second}\n\\pagebreak\n\\begin{songs}{obsah}\n    \\input{B/b}\n\\end{songs}\n\n"
    "\\begin{songs}{obsah}\n    \\input{C/c}\n\\end{songs}\n\n\\end{document}\n";

std::string section(const std::string& text, std::size_t i, int delta) {
    auto r = bf::moveSection(text, i, delta);
    INFO((r ? "" : r.error()));
    REQUIRE(r);
    return *r;
}

} // namespace

constexpr auto npos = std::string::npos;

TEST_CASE("addSection and removeSection", "[bookfile]") {
    const auto at = [&](std::size_t after) {
        auto r = bf::addSection(sections, "Third {x}", after);
        INFO((r ? "" : r.error()));
        REQUIRE(r);
        return *r;
    };
    const std::string block = "\n\\songsection{Third {x}}\n\\begin{songs}{obsah, obsahAutor}\n"
                              "\\end{songs}\n";
    const auto last = at(npos);
    CHECK(last ==
          sections.substr(0, sections.find("\n\\end{document}")) + block + "\n\\end{document}\n");
    CHECK(at(2) == last);
    const auto first = at(0);
    const auto cut = sections.find("\n\\songsection{Second}");
    CHECK(first == sections.substr(0, cut) + block + sections.substr(cut));
    CHECK(bf::parse(first)->environments.size() == 4);
    CHECK(bf::parse(first)->environments[1].section == "Third {x}");
    CHECK(bf::parse(first)->environments[2].section == "Second");
    // round trips
    for (const std::size_t after : {std::size_t{0}, std::size_t{1}, std::size_t{2}, npos}) {
        const auto added = at(after);
        const auto index = after == npos ? std::size_t{3} : after + 1;
        const auto removed = bf::removeSection(added, index);
        REQUIRE(removed);
        CHECK(*removed == sections);
    }
    // CRLF
    const auto crlfAdded = bf::addSection(crlf(sections), "N", npos);
    REQUIRE(crlfAdded);
    CHECK(crlfAdded->find("\r\n\\songsection{N}\r\n\\begin{songs}{obsah, obsahAutor}\r\n"
                          "\\end{songs}\r\n") != std::string::npos);
    CHECK(*bf::removeSection(*crlfAdded, 3) == crlf(sections));
    // refused
    CHECK_FALSE(bf::addSection(sections, "", npos));
    CHECK_FALSE(bf::addSection(sections, "a\nb", npos));
    CHECK_FALSE(bf::addSection(sections, "{a", npos));
    CHECK_FALSE(bf::addSection(sections, "a", 7));
    // no environment: before \end{document}, "obsah"
    const auto empty = bf::addSection("\\documentclass{velkyzpevnik}\n\\begin{document}\n\n"
                                      "\\end{document}\n",
                                      "S", npos);
    REQUIRE(empty);
    CHECK(*empty == "\\documentclass{velkyzpevnik}\n\\begin{document}\n\n\n\\songsection{S}\n"
                    "\\begin{songs}{obsah}\n\\end{songs}\n\\end{document}\n");
    CHECK(*bf::removeSection(*empty, 0) ==
          "\\documentclass{velkyzpevnik}\n\\begin{document}\n\n\\end{document}\n");
    // removal takes the \pagebreak with the section, the section without songsection too
    const auto no2 = bf::removeSection(sections, 1);
    REQUIRE(no2);
    CHECK(no2->find("pagebreak") == std::string::npos);
    CHECK(no2->find("B/b") == std::string::npos);
    CHECK(no2->find("\\songsection{First}") != std::string::npos);
    const auto no3 = bf::removeSection(sections, 2);
    REQUIRE(no3);
    CHECK(*no3 == sections.substr(0, sections.find("\n\\begin{songs}{obsah}\n    \\input{C/c}")) +
                      "\n\\end{document}\n");
    CHECK_FALSE(bf::removeSection(sections, 3));
}

TEST_CASE("renameSection", "[bookfile]") {
    const auto r = bf::renameSection(sections, 1, "Zweite \\& dritte");
    REQUIRE(r);
    CHECK(r->find("\\songsection{Zweite \\& dritte}\n\\pagebreak\n") != std::string::npos);
    CHECK(bf::parse(*r)->environments[1].section == "Zweite \\& dritte");
    CHECK(*bf::renameSection(sections, 0, "First") == sections);
    CHECK_FALSE(bf::renameSection(sections, 2, "x")); // none of its own
    CHECK_FALSE(bf::renameSection(sections, 0, ""));
    CHECK_FALSE(bf::renameSection(sections, 0, "a}"));
    CHECK_FALSE(bf::renameSection(sections, 5, "a"));
    // on the line of \begin{songs}, CRLF
    const auto same =
        bf::renameSection("\\songsection{A}\\begin{songs}{o}\r\n\\end{songs}\r\n", 0, "B");
    REQUIRE(same);
    CHECK(*same == "\\songsection{B}\\begin{songs}{o}\r\n\\end{songs}\r\n");
}

TEST_CASE("moveSection", "[bookfile]") {
    const auto down = section(sections, 0, 1);
    CHECK(down == "\\documentclass{velkyzpevnik}\n\\begin{document}\n\n"
                  "\\songsection{Second}\n\\pagebreak\n\\begin{songs}{obsah}\n    \\input{B/b}\n"
                  "\\end{songs}\n\n"
                  "\\songsection{First}\n\\begin{songs}{obsah, obsahAutor}\n    \\input{A/a}\n"
                  "\\end{songs}\n\n"
                  "\\begin{songs}{obsah}\n    \\input{C/c}\n\\end{songs}\n\n\\end{document}\n");
    CHECK(section(down, 1, -1) == sections);
    // the section without a songsection moves alone
    const auto last = section(sections, 1, 1);
    const auto book = bf::parse(last);
    REQUIRE(book);
    CHECK(book->environments[1].section.empty());
    CHECK(book->environments[1].songs() == Strings{"C/c"});
    CHECK(book->environments[2].section == "Second");
    CHECK(book->environments[2].songs() == Strings{"B/b"});
    CHECK(book->environments[2].units.size() == 1);
    CHECK(last.find("\\songsection{Second}\n\\pagebreak\n\\begin{songs}") != std::string::npos);
    CHECK(section(last, 2, -1) == sections);
    CHECK(section(crlf(sections), 0, 1) == crlf(section(sections, 0, 1)));
    CHECK_FALSE(bf::moveSection(sections, 0, -1));
    CHECK_FALSE(bf::moveSection(sections, 2, 1));
    CHECK_FALSE(bf::moveSection(sections, 0, 2));
    // the fixture: sections and \pagebreak
    const auto one = fixture("songbooks/bands/one/one.tex");
    for (std::size_t i = 0; i + 1 < 3; ++i)
        CHECK(section(section(one, i, 1), i + 1, -1) == one);
}

TEST_CASE("newBook writes the books of the three classes", "[bookfile]") {
    using bf::Field;
    for (const std::string cls : {"velkyzpevnik", "malyzpevnik", "taborovyzpevnik"}) {
        INFO(cls);
        bf::Settings s;
        s.bookClass = cls;
        s.chorded = false;
        s.twoColumns = true;
        s.shading = false;
        s.title = "Moje \\& {písně}";
        s.author = "Já";
        s.campYear = cls == "taborovyzpevnik" ? "2027" : "";
        s.cover = "img/cover";
        s.chordChart = true;
        const auto book = bf::newBook(s, "my-book_1");
        REQUIRE(book);
        const auto parsedBook = bf::parse(book->tex);
        REQUIRE(parsedBook);
        CHECK(parsedBook->documentClass == cls);
        CHECK(parsedBook->title == s.title);
        CHECK(parsedBook->environments.size() == 1);
        CHECK(parsedBook->environments[0].readOnly.empty());
        const auto back = bf::readSettings(book->tex);
        CHECK(back.bookClass == s.bookClass);
        CHECK(back.chorded == s.chorded);
        CHECK(back.twoColumns == s.twoColumns);
        CHECK(back.shading == s.shading);
        CHECK(back.title == s.title);
        CHECK(back.author == s.author);
        CHECK(back.campYear == s.campYear);
        CHECK(back.cover == s.cover);
        CHECK(back.chordChart);
        CHECK_FALSE(back.locked);
        CHECK(back.notEditable.empty());
        CHECK(zub::safety::unknownBookConstructs(book->tex).empty());

        const auto indexes = bf::indexFiles(*parsedBook);
        CHECK(book->buildSh.starts_with("#!/bin/bash\n"));
        CHECK(book->buildSh.find("my-book_1.tex") != std::string::npos);
        CHECK(book->buildSh.find("songs/chords") != std::string::npos);
        for (const auto& i : indexes)
            CHECK(book->buildSh.find(" -i " + i) != std::string::npos);
        CHECK(indexes ==
              (cls == "taborovyzpevnik" ? Strings{"obsah"} : Strings{"obsah", "obsahAutor"}));
        CHECK(book->buildSh.find(cls == "malyzpevnik" ? "makebook.sh"
                                 : cls == "taborovyzpevnik"
                                     ? "makebooklet.sh"
                                     : "scripts/build.sh") != std::string::npos);

        // defaults, no cover
        bf::Settings plain;
        plain.bookClass = cls;
        const auto bare = bf::newBook(plain, "b");
        REQUIRE(bare);
        const auto bareBack = bf::readSettings(bare->tex);
        CHECK(bareBack.cover.empty());
        CHECK(bareBack.chorded);
        CHECK_FALSE(bareBack.twoColumns);
        CHECK(bareBack.shading);
        CHECK_FALSE(bareBack.chordChart);
        CHECK(bareBack.notEditable.empty());
        CHECK(zub::safety::unknownBookConstructs(bare->tex).empty());
        // the settings stay editable on the result
        CHECK(setting(bare->tex, Field::Shading, "0") != bare->tex);
    }
    // the exact velkyzpevnik
    bf::Settings v;
    v.bookClass = "velkyzpevnik";
    v.shading = false;
    v.twoColumns = false;
    v.title = "T";
    v.author = "A";
    v.cover = "img/x";
    v.chordChart = true;
    CHECK(bf::newBook(v, "x")->tex ==
          "\\documentclass[chorded, onesongcolumn, noshading]{velkyzpevnik}\n\n\\title{T}\n"
          "\\author{A}\n\n\\begin{document}\n\n\\makefrontcover{img/x}\n\\insertindex\n\n"
          "\\begin{songs}{obsah,obsahAutor}\n\n\\end{songs}\n\n\\insertallchords\n\n"
          "\\end{document}\n");
    CHECK_FALSE(bf::newBook(v, "a b"));
    CHECK_FALSE(bf::newBook(v, ""));
    CHECK_FALSE(bf::newBook(v, "../x"));
    v.bookClass = "article";
    CHECK_FALSE(bf::newBook(v, "x"));
    v.bookClass = "velkyzpevnik";
    v.title = "a}";
    CHECK_FALSE(bf::newBook(v, "x"));
}

// Hidden: runs against the real ../ZubuvZpevnik checkout, which changes on its own.
TEST_CASE("every real book keeps its bytes under identity edits", "[.integration][bookfile]") {
    using bf::Field;
    const fs::path repo{ZUB_REAL_SONGBOOK_DIR};
    const auto books = findBooks(repo);
    if (books.empty())
        SKIP("no songbook checkout at " << repo);
    for (const auto& rel : books) {
        INFO(rel.string());
        const auto text = zub::readTextFile(repo / rel);
        REQUIRE(text);
        const auto s = bf::readSettings(*text);
        CHECK_FALSE(s.bookClass.empty());
        const auto same = [&](Field f, const std::string& value) {
            if (s.notEditable.contains(f))
                return;
            const auto r = bf::setSetting(*text, f, value);
            INFO("field " << static_cast<int>(f));
            INFO((r ? "" : r.error()));
            REQUIRE(r);
            CHECK(*r == *text);
        };
        same(Field::Class, s.bookClass);
        same(Field::Chorded, s.chorded ? "1" : "0");
        same(Field::TwoColumns, s.twoColumns ? "1" : "0");
        same(Field::Shading, s.shading ? "1" : "0");
        same(Field::Title, s.title);
        same(Field::Author, s.author);
        same(Field::CampYear, s.campYear);
        same(Field::Cover, s.cover);
        same(Field::ChordChart, s.chordChart ? "1" : "0");
        CHECK(bf::setLocked(*text, s.locked) == *text);
        // Locking and unlocking again gives the file back (some real books are locked).
        CHECK(bf::setLocked(bf::setLocked(*text, !s.locked), s.locked) == *text);

        const auto book = bf::parse(*text);
        REQUIRE(book);
        for (std::size_t i = 0; i + 1 < book->environments.size(); ++i) {
            INFO("sections " << i);
            const auto down = bf::moveSection(*text, i, 1);
            REQUIRE(down);
            const auto up = bf::moveSection(*down, i + 1, -1);
            REQUIRE(up);
            CHECK(*up == *text);
        }
    }
}

TEST_CASE("a new section shares the book's indexes, not an album's own", "[bookfile]") {
    namespace bf = zub::bookfile;
    const std::string book = "\\documentclass{velkyzpevnik}\n\\newindex{indexA}{A}\n"
                             "\\begin{document}\n\\songsection{Album}\n\\begin{songs}{indexA}\n"
                             "\\end{songs}\n\\end{document}\n";
    const auto added = bf::addSection(book, "New", std::string::npos);
    REQUIRE(added);
    CHECK(added->find("\\songsection{New}\n\\begin{songs}{obsah}\n") != std::string::npos);
    const std::string mixed = "\\documentclass{velkyzpevnik}\n\\newindex{indexA}{A}\n"
                              "\\begin{document}\n\\begin{songs}{indexA, obsahAutor}\n"
                              "\\end{songs}\n\\end{document}\n";
    const auto kept = bf::addSection(mixed, "New", std::string::npos);
    REQUIRE(kept);
    CHECK(kept->find("\\begin{songs}{obsahAutor}\n") != std::string::npos);
}

TEST_CASE("booksFolder is songbooks, or projects in an older checkout", "[bookfile]") {
    const auto root = std::filesystem::path(ZUB_TEST_DATA_DIR) / "songbook";
    CHECK(zub::bookfile::booksFolder(root) == "songbooks");
}

TEST_CASE("a build.sh never gets an odd collection name", "[bookfile]") {
    const auto script = zub::bookfile::buildScriptFor("b", zub::songbook::BookClass::Velky,
                                                      {"obsah"}, {"chords", "x$(id)", "a:b"});
    CHECK(script.find("songs/chords:") != std::string::npos);
    CHECK(script.find("x$(id)") == std::string::npos);
    CHECK(script.find("a:b") == std::string::npos);
}

TEST_CASE("bookfile build TEXINPUTS puts song copies after the package", "[bookfile]") {
    const auto command =
        zub::bookfile::buildCommand("/sb", "/pkg", "/work", "/bd", "book", {}, "/work/override");
    CHECK(command.environment[0].second == ".:/work:/pkg:/work/override:/bd:/sb:");
}

TEST_CASE("addClassOption adds a class option once, byte-exact", "[bookfile]") {
    CHECK(addClassOption("\\documentclass{velkyzpevnik}\r\nX", "transposecapos").value() ==
          "\\documentclass[transposecapos]{velkyzpevnik}\r\nX");
    CHECK(addClassOption("\\documentclass[]{malyzpevnik}", "transposecapos").value() ==
          "\\documentclass[transposecapos]{malyzpevnik}");
    CHECK(addClassOption("\\documentclass[ lyric ,noshading]{v}", "transposecapos").value() ==
          "\\documentclass[ lyric ,noshading, transposecapos]{v}");
    const std::string already = "% \\documentclass{x}\n\\documentclass[transposecapos]{v}\n";
    CHECK(addClassOption(already, "transposecapos").value() == already);
    CHECK_FALSE(addClassOption("no class", "transposecapos").has_value());
    CHECK_FALSE(addClassOption("\\documentclass[lyric{v}", "transposecapos").has_value());
}

TEST_CASE("wishes lists the TODO lines of the song lists", "[bookfile]") {
    const std::string text = "\\documentclass{velkyzpevnik}\r\n"
                             "% TODO: Outside/Before\r\n"
                             "\\begin{document}\r\n"
                             "\\begin{songs}{obsah}\r\n"
                             "    % TODO: A/One\r\n"
                             "    \\input{A/Two}\r\n"
                             "    %TODO:B/Three maybe later \r\n"
                             "    \\input{A/Seven} % TODO: a note, no wish\r\n"
                             "    % TODO: not a song name\r\n"
                             "    % TODO:\r\n"
                             "    % \\input{C/Four}\r\n"
                             "    \\beginkeeporder\r\n"
                             "    \\input{D/Five}\r\n"
                             "    % TODO: D/Six\r\n"
                             "    \\endkeeporder\r\n"
                             "\\end{songs}\r\n"
                             "% TODO: Outside/After\r\n"
                             "\\end{document}\r\n";
    const auto list = wishes(text);
    REQUIRE(list.size() == 4);
    CHECK(list[0].text == "A/One");
    CHECK(list[0].name == "A/One");
    CHECK(list[0].line == 5);
    CHECK(list[1].text == "B/Three maybe later");
    CHECK(list[1].name == "B/Three");
    CHECK(list[1].line == 7);
    CHECK(list[2].text == "not a song name"); // a title or a note: no input name
    CHECK(list[2].name.empty());
    CHECK(list[3].name == "D/Six");
    CHECK(list[3].line == 14);
    CHECK(wishes("\\begin{songs}{x}\n% TODO: A/B\n").empty()); // parse() refuses it

    // Two song lists, LF: the lines count through the whole file; Czech text stays as it is;
    // "%% TODO:" and "% TODO :" are no wishes.
    const std::string two = "\\begin{songs}{a}\n% TODO: A/B\n\\end{songs}\n"
                            "\\songsection{X}\n\\begin{songs}{b}\n%% TODO: C/D\n"
                            "% TODO : E/F\n% TODO: Píseň o zemi\n\\end{songs}\n";
    const auto both = wishes(two);
    REQUIRE(both.size() == 2);
    CHECK(both[0].line == 2);
    CHECK(both[1].text == "Píseň o zemi");
    CHECK(both[1].name.empty());
    CHECK(both[1].line == 8);
}
