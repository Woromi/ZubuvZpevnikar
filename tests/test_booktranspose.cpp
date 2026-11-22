#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <iostream>
#include <set>
#include <zub/core/booktranspose.hpp>
#include <zub/core/score.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songcheck.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;
using namespace zub::booktranspose;

namespace {

Key K(int pitch, bool minor = false) {
    return Key{pitch, minor};
}

} // namespace

TEST_CASE("parseKey reads the forms songs use", "[booktranspose]") {
    CHECK(parseKey("G", false) == K(7));
    CHECK(parseKey("{G}", false) == K(7));
    CHECK(parseKey("Am", false) == K(9, true));
    CHECK(parseKey("F\\shrp m", false) == K(6, true));
    CHECK(parseKey("F\\shrp", false) == K(6));
    CHECK(parseKey("B\\flt", false) == K(10));
    CHECK(parseKey("E\\flt m", false) == K(3, true));
    CHECK(parseKey("B", false) == K(11));
    CHECK(parseKey("C#", false) == K(1));
    CHECK(parseKey("Bb", false) == K(10));
    CHECK(parseKey("Ebm", false) == K(3, true));
    CHECK(parseKey("A\\flt{}", false) == K(8));
    CHECK(parseKey("B\\flt{}", false) == K(10));
    CHECK(parseKey("  Dm ", false) == K(2, true));
}

TEST_CASE("parseKey reads Czech and German naming", "[booktranspose]") {
    CHECK(parseKey("H", true) == K(11));
    CHECK(parseKey("Hm", true) == K(11, true));
    CHECK(parseKey("B", true) == K(10));
    CHECK(parseKey("Bm", true) == K(10, true));
    CHECK(parseKey("H", false) == std::nullopt);
    CHECK(parseKey("A\\shrp", true) == K(10));
}

TEST_CASE("parseKey takes the first key of a list and ignores notes", "[booktranspose]") {
    CHECK(parseKey("Cm\\rightarrow C", false) == K(0, true));
    CHECK(parseKey("Am\\rightarrow Em", false) == K(9, true));
    CHECK(parseKey("A\\rightarrow Hm", true) == K(9));
    CHECK(parseKey("B\\flt m\\rightarrow G\\flt", false) == K(10, true));
    CHECK(parseKey("Dm, F", false) == K(2, true));
    CHECK(parseKey("G (D whistle)", false) == K(7));
    CHECK(parseKey("Dm (C whistle)", false) == K(2, true));
    CHECK(parseKey("E\\flt\\ (B\\flt\\ whistle)", false) == K(3));
    CHECK(parseKey("{Am -> C}", false) == K(9, true));
}

TEST_CASE("parseKey rejects what it cannot read", "[booktranspose]") {
    CHECK(parseKey("", false) == std::nullopt);
    CHECK(parseKey("{}", false) == std::nullopt);
    CHECK(parseKey("Fis", false) == std::nullopt);
    CHECK(parseKey("Dmaj", false) == std::nullopt);
    CHECK(parseKey("X", false) == std::nullopt);
    CHECK(parseKey("Gx", false) == std::nullopt);
    CHECK(parseKey("\\shrp", false) == std::nullopt);
}

TEST_CASE("parseKey reads back keyValue for all 24 keys in both namings", "[booktranspose]") {
    static const char* names[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                    "F#", "G",  "G#", "A",  "A#", "B"};
    for (const bool german : {false, true})
        for (int p = 0; p < 12; ++p)
            for (const bool minor : {false, true}) {
                const std::string detected = std::string(names[p]) + (minor ? "m" : "");
                const auto value = zub::songcheck::keyValue(detected, german);
                INFO(detected << " -> " << value << (german ? " (german)" : ""));
                CHECK(parseKey(value, german) == K(p, minor));
                CHECK(keyName(K(p, minor), german) == value);
            }
}

TEST_CASE("shift is the smallest move, compared by key signature", "[booktranspose]") {
    CHECK(shift(K(0), K(0)) == 0);
    CHECK(shift(K(0), K(7)) == -5); // C -> G
    CHECK(shift(K(0), K(6)) == 6);  // tie goes up
    CHECK(shift(K(7), K(0)) == 5);
    CHECK(shift(K(0), K(2)) == 2);
    CHECK(shift(K(2), K(0)) == -2);
    CHECK(shift(K(11), K(0)) == 1);
    CHECK(shift(K(0), K(11)) == -1);
    // A book in G: Am songs go to Em, C songs to G.
    CHECK(shift(K(9, true), K(7)) == -5);
    CHECK(shift(K(0), K(7)) == -5);
    // A minor target means its relative major: a book in Em, C songs go to G, Am songs to Em.
    CHECK(shift(K(0), K(4, true)) == -5);
    CHECK(shift(K(9, true), K(4, true)) == -5);
    // Same signature: no move.
    CHECK(shift(K(9, true), K(0)) == 0);
    CHECK(shift(K(0), K(9, true)) == 0);
    for (int a = 0; a < 12; ++a)
        for (int b = 0; b < 12; ++b)
            for (const bool m1 : {false, true})
                for (const bool m2 : {false, true}) {
                    const int n = shift(K(a, m1), K(b, m2));
                    CHECK(n >= -5);
                    CHECK(n <= 6);
                    const int moved = ((a + (m1 ? 3 : 0) + n) % 12 + 12) % 12;
                    CHECK(moved == (b + (m2 ? 3 : 0)) % 12);
                }
}

TEST_CASE("keyName spells keys like songs", "[booktranspose]") {
    CHECK(keyName(K(7), false) == "G");
    CHECK(keyName(K(3), false) == "E\\flt");
    CHECK(keyName(K(10), false) == "B\\flt");
    CHECK(keyName(K(10), true) == "B");
    CHECK(keyName(K(6, true), false) == "F\\shrp m");
    CHECK(keyName(K(11, true), true) == "Hm");
    CHECK(keyName(K(11), true) == "H");
    CHECK(keyName(K(10, true), true) == "Bm");
    CHECK(keyName(K(10, true), false) == "A\\shrp m");
}

TEST_CASE("bookKey and bookKeyName", "[booktranspose]") {
    CHECK(bookKey("G") == K(7));
    CHECK(bookKey("Em") == K(4, true));
    CHECK(bookKey("Bb") == K(10));
    CHECK(bookKey("B") == K(11));
    CHECK(bookKey("F#m") == K(6, true));
    CHECK(bookKey("C#") == K(1));
    CHECK(bookKey("Dbm") == K(1, true));
    CHECK(bookKey("H") == std::nullopt);
    CHECK(bookKey("") == std::nullopt);
    CHECK(bookKey("G ") == std::nullopt);
    CHECK(bookKey("Gmm") == std::nullopt);
    CHECK(bookKeyName(K(10)) == "Bb");
    CHECK(bookKeyName(K(1)) == "Db");
    CHECK(bookKeyName(K(6)) == "F#");
    CHECK(bookKeyName(K(10, true)) == "A#m");
    CHECK(bookKeyName(K(11, true)) == "Bm");
    for (int p = 0; p < 12; ++p)
        for (const bool m : {false, true})
            CHECK(bookKey(bookKeyName(K(p, m))) == K(p, m));
}

// Hidden: the real ../ZubuvZpevnik checkout. Run with: build/debug/tests/zub_core_tests
// "[integration]"
TEST_CASE("every real song key parses, or is in the documented list",
          "[.integration][booktranspose]") {
    const fs::path repo{ZUB_REAL_SONGBOOK_DIR};
    if (!fs::exists(repo / "songs"))
        SKIP("no songbook checkout at " << repo);
    // Values that are deliberately not keys.
    const std::set<std::string> unparseable = {""};
    std::set<std::string> seen;
    int parsed = 0;
    for (const auto& entry : fs::recursive_directory_iterator(repo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tex")
            continue;
        const auto text = zub::readTextFile(entry.path());
        if (!text)
            continue;
        const auto song = zub::parseSong(*text);
        if (!song)
            continue;
        const auto key = song->value("key");
        const bool german = zub::score::germanNames(zub::mainLanguage(song->value("lang")));
        if (parseKey(key, german)) {
            ++parsed;
        } else if (!unparseable.contains(key)) {
            seen.insert(key + "  (" + entry.path().string() + ")");
        }
    }
    for (const auto& s : seen)
        std::cerr << "unparseable key: " << s << "\n";
    CHECK(seen.empty());
    CHECK(parsed > 100);
}

TEST_CASE("shiftKeys moves every key of a key= value", "[booktranspose]") {
    CHECK(shiftKeys("G", 2, false) == "A");
    CHECK(shiftKeys("{Am}", 3, false) == "{Cm}");
    CHECK(shiftKeys("C\\shrp m", 1, false) == "Dm");
    CHECK(shiftKeys("Cm\\rightarrow C", 2, false) == "Dm\\rightarrow D");
    CHECK(shiftKeys("B\\flt m\\rightarrow G\\flt", -2, false) == "G\\shrp m\\rightarrow E");
    CHECK(shiftKeys("A\\rightarrow Hm", 2, true) == "H\\rightarrow C\\shrp m");
    CHECK(shiftKeys("Dm, F", 2, false) == "Em, G");
    CHECK(shiftKeys("G (D whistle)", 2, false) == "A (E whistle)");
    CHECK(shiftKeys("E\\flt\\ (B\\flt\\ whistle)", 2, false) == "F\\ (C\\ whistle)");
    CHECK(shiftKeys("E\\flt{}", 1, false) == "E{}");
    CHECK(shiftKeys("H", 1, true) == "C");
    CHECK(shiftKeys("E\\flt", 11, true) == "D");
    // Not keys: left alone
    CHECK(shiftKeys("Bridge, Dmaj", 2, false) == "Bridge, Dmaj");
    CHECK(shiftKeys("", 3, false) == "");
    // Round trip for the spelling keyName writes
    for (const char* value : {"G", "Am", "D\\shrp m", "Cm\\rightarrow C", "A\\shrp m\\rightarrow F\\shrp",
                              "Dm, F", "F\\shrp m", "G (D whistle)"})
        for (int n = -11; n <= 11; ++n)
            CHECK(shiftKeys(shiftKeys(value, n, false), -n, false) == value);
}

TEST_CASE("shifting every real song key by N and back keeps its text",
          "[.integration][booktranspose]") {
    const fs::path repo{ZUB_REAL_SONGBOOK_DIR};
    if (!fs::exists(repo / "songs"))
        SKIP("no songbook checkout at " << repo);
    int checked = 0, respelled = 0;
    for (const auto& entry : fs::recursive_directory_iterator(repo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tex")
            continue;
        const auto text = zub::readTextFile(entry.path());
        if (!text)
            continue;
        const auto song = zub::parseSong(*text);
        if (!song)
            continue;
        const auto key = song->value("key");
        const bool german = zub::score::germanNames(zub::mainLanguage(song->value("lang")));
        if (!parseKey(key, german))
            continue;
        const auto normal = shiftKeys(key, 0, german);
        if (normal != key)
            ++respelled; // `A\shrp`, `D\shrp`...: keyName spells them as flats
        for (const int n : {1, 2, 5, 7, -3, -11}) {
            INFO(key << " by " << n << " in " << entry.path().string());
            CHECK(shiftKeys(shiftKeys(key, n, german), -n, german) == normal);
        }
        ++checked;
    }
    CHECK(checked > 100);
    std::cerr << respelled << " of " << checked << " keys are respelled by keyName\n";
}
