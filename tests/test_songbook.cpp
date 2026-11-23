#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <map>
#include <unistd.h>
#include <zub/core/bookfile.hpp>
#include <zub/core/json.hpp>
#include <zub/core/library.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/textfile.hpp>

using namespace zub::songbook;

namespace {

const std::filesystem::path dataDir = ZUB_TEST_DATA_DIR;

} // namespace

TEST_CASE("quote escapes JSON strings", "[songbook]") {
    CHECK(zub::json::quote("a\"b\\c\n\t\x01 š") == "\"a\\\"b\\\\c\\n\\t\\u0001 š\"");
}

TEST_CASE("class names", "[songbook]") {
    CHECK(className(BookClass::Tabor) == "taborovyzpevnik");
    CHECK(bookClassFromName("malyzpevnik") == BookClass::Maly);
    CHECK_FALSE(bookClassFromName("nope"));
}

TEST_CASE("escapeText", "[songbook]") {
    CHECK(escapeText("a\\b{c}&%$#_~^ č\nx") ==
          "a\\textbackslash{}b\\{c\\}\\&\\%\\$\\#\\_\\textasciitilde{}\\textasciicircum{} č x");
}

namespace {

using V = std::vector<std::string>;

} // namespace

TEST_CASE("resolveSongs", "[songbook]") {
    Songbook b;
    b.sections = {{"", {"TheMamasAndThePapas/CaliforniaDreamin", "Abba/Nope", "../x"}},
                  {"S", {"TheMamasAndThePapas/CaliforniaDreamin", "/abs", "a}b"}}};
    const auto root = dataDir / "songbook";
    const auto r = resolveSongs(b, root);
    REQUIRE(r.songs.size() == 2);
    CHECK(r.songs[0].inputName == "TheMamasAndThePapas/CaliforniaDreamin");
    CHECK(r.songs[0].path == root / "songs/chords/TheMamasAndThePapas/CaliforniaDreamin.tex");
    CHECK(r.songs[1].inputName == "Abba/Nope");
    CHECK(r.songs[1].path.empty());
    CHECK(r.invalid == std::vector<std::string>{"../x", "/abs", "a}b"});
}

TEST_CASE("resolveSongs and findScoreFile use every collection", "[songbook]") {
    namespace fs = std::filesystem;
    const auto root = dataDir / "songbook";
    Songbook b;
    b.sections = {{"", {"Imaginary/Hum"}}};
    const auto r = resolveSongs(b, root);
    REQUIRE(r.songs.size() == 1);
    CHECK(r.songs[0].path == root / "songs/whistle2/Imaginary/Hum.tex");
    CHECK(zub::songbook::findScoreFile(root, r.songs[0].path, "Imaginary/HumMelody.ly") ==
          root / "songs/whistle2/Imaginary/HumMelody.ly");

    // the song's own collection first, then the others in collections() order
    const auto tmp = fs::temp_directory_path() / "zub-findscore-test";
    fs::remove_all(tmp);
    for (const char* c : {"chords", "tinwhistle", "zither"}) {
        fs::create_directories(tmp / "songs" / c / "A");
        std::ofstream(tmp / "songs" / c / "A" / "S.ly") << "{ c4 }\n";
        std::ofstream(tmp / "songs" / c / "A" / "S.tex") << "x\n";
    }
    CHECK(zub::songbook::findScoreFile(tmp, tmp / "songs/zither/A/S.tex", "A/S.ly") ==
          tmp / "songs/zither/A/S.ly");
    CHECK(zub::songbook::findScoreFile(tmp, tmp / "songs/chords/A/S.tex", "A/S.ly") ==
          tmp / "songs/chords/A/S.ly");
    fs::remove(tmp / "songs/chords/A/S.ly");
    CHECK(zub::songbook::findScoreFile(tmp, tmp / "songs/zither/A/S.tex", "A/S.ly") ==
          tmp / "songs/zither/A/S.ly");
    CHECK(zub::songbook::findScoreFile(tmp, tmp / "songs/chords/A/S.tex", "A/S.ly") ==
          tmp / "songs/tinwhistle/A/S.ly");
    Songbook b2;
    b2.sections = {{"", {"A/S"}}};
    CHECK(resolveSongs(b2, tmp).songs[0].path == tmp / "songs/chords/A/S.tex");
    fs::remove(tmp / "songs/chords/A/S.tex");
    CHECK(resolveSongs(b2, tmp).songs[0].path == tmp / "songs/tinwhistle/A/S.tex");
    fs::remove_all(tmp);
}

TEST_CASE("index order against the songidx capture", "[songbook]") {
    const std::vector<SortEntry> entries = {{"The Bard's Song", "english"},
                                            {"Čarodějnice z Amesbury", "czech"},
                                            {"Chlast", "czech"},
                                            {"Counting Stars", "english"},
                                            {"Akordy", "czech"},
                                            {"A Day in the Life", "english"},
                                            {"Hřbitov", "czech"},
                                            {"Africa", ""},
                                            {"Cudzinka v tvojej zemi", "slovak"}};
    const auto sxd = zub::readTextFile(dataDir / "songidx/order.sxd");
    REQUIRE(sxd);
    CHECK(indexOrderInput(entries) == *sxd);

    const auto sbx = zub::readTextFile(dataDir / "songidx/order.sbx");
    REQUIRE(sbx);
    const auto order = parseIndexOrder(*sbx, 9);
    REQUIRE(order);
    CHECK(*order == std::vector<int>{8, 5, 1, 4, 9, 2, 6, 7, 3});

    CHECK_FALSE(parseIndexOrder(*sbx, 8));
    CHECK_FALSE(parseIndexOrder(*sbx, 10));
    CHECK_FALSE(parseIndexOrder("\\songlink{a}{1}\\songlink{b}{1}", 2));
    CHECK_FALSE(parseIndexOrder("\\songlink{a}{1}\\songlink{b}{3}", 2));
    CHECK_FALSE(parseIndexOrder("\\songlink{a}{x}", 1));
    CHECK(parseIndexOrder("\\songlink{a}{2}\\songlink{b}{1}", 2));
}

TEST_CASE("indexOrderInput keeps titles on one line", "[songbook]") {
    const std::vector<SortEntry> entries = {{"A\nB", ""}};
    CHECK(indexOrderInput(entries) == "TITLE INDEX DATA FILE\n%lang english\nA B\n1\nsong1-1.1\n");
}

TEST_CASE("indexOrderInput protects a leading percent", "[songbook]") {
    const std::vector<SortEntry> entries = {{"%x", ""}, {"a%", "cs"}};
    CHECK(indexOrderInput(entries) == "TITLE INDEX DATA FILE\n%lang english\n %x\n1\nsong1-1.1\n"
                                      "%lang cs\na%\n2\nsong1-2.1\n");
}

TEST_CASE("sortCommand", "[songbook]") {
    const auto c = sortCommand("/t/texlua", "/r/songidx.lua", "cs_CZ.utf8", "/w/a.sxd", "/w/a.sbx");
    CHECK(c.program == "/t/texlua");
    CHECK(c.arguments ==
          std::vector<std::string>{"/r/songidx.lua", "-l", "cs_CZ.utf8", "/w/a.sxd", "/w/a.sbx"});
    const auto n = sortCommand("texlua", "s.lua", "", "a.sxd", "a.sbx");
    CHECK(n.arguments == std::vector<std::string>{"s.lua", "a.sxd", "a.sbx"});
}

TEST_CASE("referencedFiles", "[songbook]") {
    const auto text = zub::readTextFile(
        dataDir / "songbook/songs/chords/TheMamasAndThePapas/CaliforniaDreamin.tex");
    REQUIRE(text);
    CHECK(referencedFiles(*text) ==
          std::vector<std::string>{"TheMamasAndThePapas/CaliforniaDreamin.ly"});

    CHECK(referencedFiles("\\lilypondfile[a=b]{X/a.ly}\n% \\lilypondfile{no.ly}\n"
                          "\\narrowlilypondfile{X/b.ly} \\lilypondfile{X/a.ly}\n"
                          "\\lilypondfilex{no} \\lilypondfile no") ==
          std::vector<std::string>{"X/a.ly", "X/b.ly"});
    CHECK(referencedFiles("no files").empty());
    CHECK(referencedFiles("\\lilypondfile{ A/X.ly }") == std::vector<std::string>{"A/X.ly"});
}

TEST_CASE("resolveScoreReference", "[songbook]") {
    using zub::songbook::resolveScoreReference;
    CHECK(resolveScoreReference("Artist/X.ly") == "Artist/X.ly");
    CHECK(resolveScoreReference("  Artist/X.ly \t") == "Artist/X.ly");
    CHECK(resolveScoreReference("Artist/Evil") == "Artist/Evil.ly");
    CHECK(resolveScoreReference("Evil") == "Evil.ly");
    CHECK_FALSE(resolveScoreReference(""));
    CHECK_FALSE(resolveScoreReference("   "));
    CHECK_FALSE(resolveScoreReference("/etc/evil.ly"));
    CHECK_FALSE(resolveScoreReference("../Evil.ly"));
    CHECK_FALSE(resolveScoreReference("A/../../Evil"));
    CHECK_FALSE(resolveScoreReference("A\\Evil.ly"));
    CHECK_FALSE(resolveScoreReference("A/Evil.txt"));
    CHECK_FALSE(resolveScoreReference("A/.ly"));
    CHECK_FALSE(resolveScoreReference("A/"));
    CHECK_FALSE(resolveScoreReference("A/E~vil.ly"));
}

TEST_CASE("paperWarnings", "[songbook]") {
    const std::string log =
        "Package zubuvzpevnik Warning: Song 'A' is not marked as fitting a5 paper on input line "
        "3.\n"
        "Package zubuvzpevnik Warning: something else.\n"
        "Package foo Warning: not marked as fitting\n"
        "Package zubuvzpevnik Warning: Song 'B' is not marked as fitting a5 paper\n"
        // wrapped at max_print_line in the middle of the phrase
        "Package zubuvzpevnik Warning: Song 'A Very Long Title, Long Enough To Wrap The Log Line "
        "Right Here' "
        "is not marked as fi\n"
        "tting a5 paper; its pap key says 'a4'.";
    CHECK(paperWarnings(log) == 3);
    CHECK(paperWarnings("") == 0);

    // a long warning without the phrase, then one with it: counted once
    CHECK(paperWarnings("Package zubuvzpevnik Warning: " + std::string(150, 'x') +
                        " and so on.\n"
                        "Package zubuvzpevnik Warning: Song 'C' is not marked as fitting a5.\n") ==
          1);
    // wrapped before 119 bytes
    CHECK(paperWarnings("Package zubuvzpevnik Warning: Song 'D' is not marked as fi\n"
                        "tting a5 paper.\n\nPackage foo Warning: not marked as fitting\n") == 1);
    // a blank line ends the block
    CHECK(paperWarnings("Package zubuvzpevnik Warning: Song 'E' is not\n\nmarked as fitting\n") ==
          0);
}

TEST_CASE("mergeIntoOrder", "[songbook]") {
    using V = std::vector<std::string>;
    CHECK(mergeIntoOrder({}, {"a", "b"}, {"a", "b"}) == V{"a", "b"});
    // front, middle, end
    CHECK(mergeIntoOrder({"b", "c"}, {"a", "b", "c"}, {"a"}) == V{"a", "b", "c"});
    CHECK(mergeIntoOrder({"a", "c"}, {"a", "b", "c"}, {"b"}) == V{"a", "b", "c"});
    CHECK(mergeIntoOrder({"a", "b"}, {"a", "b", "c"}, {"c"}) == V{"a", "b", "c"});
    // a manual order: the new name follows its alphabetical predecessor wherever that is now
    CHECK(mergeIntoOrder({"c", "a", "e"}, {"a", "b", "c", "d", "e"}, {"b"}) ==
          V{"c", "a", "b", "e"});
    CHECK(mergeIntoOrder({"c", "a", "e"}, {"a", "c", "d", "e", "f"}, {"d"}) ==
          V{"c", "d", "a", "e"});
    // several at once, in index order, one may follow another
    CHECK(mergeIntoOrder({"e", "a"}, {"a", "b", "c", "d", "e"}, {"d", "b", "c"}) ==
          V{"e", "a", "b", "c", "d"});
    CHECK(mergeIntoOrder({"z"}, {"a", "b", "z"}, {"b", "a"}) == V{"a", "b", "z"});
    // duplicates and names already present are ignored
    CHECK(mergeIntoOrder({"a", "c"}, {"a", "b", "c"}, {"b", "b", "a"}) == V{"a", "b", "c"});
    CHECK(mergeIntoOrder({"a"}, {"a"}, {"a"}) == V{"a"});
    // a name missing from the index order goes last
    CHECK(mergeIntoOrder({"a"}, {"a"}, {"q"}) == V{"a", "q"});
}

TEST_CASE("findScoreFile and inTinwhistleTree", "[songbook]") {
    using namespace zub::songbook;
    const auto root = dataDir / "songbook";
    CHECK(inTinwhistleTree(root, root / "songs/tinwhistle/A/B.tex"));
    CHECK_FALSE(inTinwhistleTree(root, root / "songs/chords/A/B.tex"));
    CHECK_FALSE(inTinwhistleTree(root, root / "songs/tinwhistleX/B.tex"));
    const auto song = root / "songs/chords/Ayreon/NotASong.tex";
    CHECK(findScoreFile(root, song, "Ayreon/NotASong.ly") ==
          root / "songs/chords/Ayreon/NotASong.ly");
    CHECK(findScoreFile(root, song, "Nope/Nope.ly").empty());
}

TEST_CASE("scoreRisks", "[songbook]") {
    using zub::songbook::scoreRisks;
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "zub-scorerisks-test";
    fs::remove_all(root);
    fs::create_directories(root / "songs/chords/A");
    fs::create_directories(root / "songs/tinwhistle/A");
    const auto write = [](const fs::path& file, std::string_view text) {
        std::ofstream(file) << text;
    };
    write(root / "songs/chords/A/Good.ly", "{ c4 }\n");
    write(root / "songs/chords/A/Evil.ly", "x = #(system \"touch P\")\n");
    write(root / "songs/tinwhistle/A/Tin.ly", "x = #(system \"touch Q\")\n");
    const auto chordsSong = root / "songs/chords/A/S.tex";
    const auto tinSong = root / "songs/tinwhistle/A/S.tex";

    CHECK(scoreRisks(root, chordsSong, "\\lilypondfile{A/Good.ly}\n").empty());
    CHECK(scoreRisks(root, chordsSong, "\\lilypondfile{A/Missing.ly}\n").empty());
    CHECK(scoreRisks(root, chordsSong, "\\lilypondfile{A/Evil}\n") ==
          std::vector<std::string>{"A/Evil.ly: #(system"});
    CHECK(scoreRisks(root, chordsSong, "\\narrowlilypondfile{ A/Tin.ly }\n") ==
          std::vector<std::string>{"A/Tin.ly: #(system"});
    CHECK(scoreRisks(root, tinSong, "\\lilypondfile{A/Good.ly}\n").empty());
    CHECK(scoreRisks(root, chordsSong, "\\lilypondfile{../Evil.ly}\n") ==
          std::vector<std::string>{"\\lilypondfile{../Evil.ly}"});
    fs::remove_all(root);
}

TEST_CASE("notMarkedFitting lists a book's songs not marked for its paper", "[songbook]") {
    Songbook b;
    b.sections = {{"", {"A/Fits", "A/Spread", "A/BigOnly", "A/Nothing"}},
                  {"S", {"A/Fits", "A/Unknown", "A/Upper", "A/Spaced"}}};
    const std::map<std::string, std::string> pap{{"A/Fits", "a4,a5"}, {"A/Spread", "a4,a5x2"},
                                                 {"A/BigOnly", "a4"}, {"A/Nothing", ""},
                                                 {"A/Upper", "A5"},   {"A/Spaced", "a4, a5"}};
    const auto papOf = [&](std::string_view name) -> std::optional<std::string> {
        const auto it = pap.find(std::string(name));
        if (it == pap.end())
            return std::nullopt;
        return it->second;
    };
    b.bookClass = BookClass::Maly; // A5
    CHECK(paperName(b.bookClass) == "a5");
    CHECK(notMarkedFitting(b, papOf) == std::vector<std::string>{"A/BigOnly", "A/Nothing"});
    b.bookClass = BookClass::Velky; // A4: only the ones without a4
    CHECK(paperName(b.bookClass) == "a4");
    CHECK(notMarkedFitting(b, papOf) == std::vector<std::string>{"A/Nothing", "A/Upper"});
}

TEST_CASE("removeSong takes a song out of every section", "[songbook]") {
    Songbook b;
    b.sections = {{"", {"A/X", "A/Y"}}, {"S", {"A/X"}}};
    CHECK(removeSong(b, "A/X"));
    CHECK(b.sections[0].songs == std::vector<std::string>{"A/Y"});
    CHECK(b.sections[1].songs.empty());
    CHECK_FALSE(removeSong(b, "A/X"));
}

TEST_CASE("scoreRisks checks every collection's copy of a score", "[songbook]") {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "zub-scorerisks-test";
    fs::remove_all(root);
    fs::create_directories(root / "songs/chords/A");
    fs::create_directories(root / "songs/tinwhistle/A");
    std::ofstream(root / "songs/tinwhistle/A/X.ly") << "{ c'4 }\n";
    std::ofstream(root / "songs/chords/A/X.ly") << "#(system \"id\")\n";
    // The tin whistle song's own copy is harmless, but a book build may find the chords one.
    const auto risks = zub::songbook::scoreRisks(root, root / "songs/tinwhistle/A/S.tex",
                                                 "\\lilypondfile{A/X.ly}");
    REQUIRE(risks.size() == 1);
    CHECK(risks[0].starts_with("A/X.ly: #(system"));
    fs::remove_all(root);
}

TEST_CASE("readiness sorts a book's songs into blockers, to fix and notes", "[songbook]") {
    zub::songbook::Songbook book;
    book.bookClass = zub::songbook::BookClass::Velky; // a4
    book.sections = {{"", {"A/Ok", "B/Gone", "C/Risky", "D/Small"}},
                     {"Two", {"E/New", "A/Ok", "F/Notes"}}};
    int asked = 0;
    const auto facts = [&](std::string_view name) {
        ++asked;
        zub::songbook::SongFacts f;
        f.exists = name != "B/Gone";
        f.pap = name == "D/Small" ? "a5" : "a4,a5";
        f.hasKey = name != "F/Notes";
        if (name == "C/Risky")
            f.risks = {"\\directlua"};
        if (name == "E/New")
            f.notChecked = true;
        if (name == "F/Notes") {
            f.todos = {"Solo"};
            f.changes = "Tidy 2";
        }
        return f;
    };
    const auto r = zub::songbook::readiness(book, facts);
    CHECK(asked == 6); // A/Ok once
    CHECK(r.missing == std::vector<std::string>{"B/Gone"});
    CHECK(r.unsafe == std::vector<std::string>{"C/Risky"});
    CHECK(r.notFitting == std::vector<std::string>{"D/Small"}); // not the missing one
    CHECK(r.notChecked == std::vector<std::string>{"E/New"});
    CHECK(r.todo == std::vector<std::string>{"F/Notes"});
    CHECK(r.changes == std::vector<std::string>{"F/Notes"});
    CHECK(r.noKey == std::vector<std::string>{"F/Notes"});
    CHECK(r.toFix() == 4);  // four songs
    CHECK(r.notes() == 1);  // F/Notes, three notes
    auto withFile = r;
    withFile.files.push_back("akordy.tex: \\foo");
    CHECK(withFile.toFix() == 5);
    CHECK(!r.ready());

    zub::songbook::Songbook fine;
    fine.bookClass = zub::songbook::BookClass::Maly; // a5
    fine.sections = {{"", {"D/Small", "F/Notes"}}};
    const auto ok = zub::songbook::readiness(fine, facts);
    CHECK(ok.ready());
    CHECK(ok.notes() == 1);
}

// Hidden: runs against the real ../ZubuvZpevnik checkout.
TEST_CASE("readiness of every real book", "[.integration][songbook]") {
    const std::filesystem::path repo{ZUB_REAL_SONGBOOK_DIR};
    const auto library = zub::scanLibrary(repo);
    if (!library)
        SKIP("no songbook checkout at " << repo);
    std::map<std::string, const zub::SongEntry*> byName;
    for (const auto& song : library->songs)
        byName.emplace(song.inputName, &song);
    for (const auto& rel : zub::bookfile::findBooks(repo)) {
        INFO(rel.string());
        const auto text = zub::readTextFile(repo / rel);
        REQUIRE(text);
        const auto parsed = zub::bookfile::parse(*text);
        REQUIRE(parsed);
        Songbook book;
        book.bookClass = bookClassFromName(parsed->documentClass).value_or(BookClass::Velky);
        for (const auto& environment : parsed->environments)
            book.sections.push_back({environment.section, environment.songs()});
        const auto r = readiness(book, [&](std::string_view name) {
            SongFacts f;
            if (const auto it = byName.find(std::string(name)); it != byName.end()) {
                f.exists = true;
                f.pap = it->second->meta.value("pap");
                f.hasKey = !it->second->meta.value("key").empty();
            }
            return f;
        });
        CHECK(r.missing.empty()); // every input of a real book is a song
        CHECK(r.notes() <= parsed->songs().size());
    }
}
