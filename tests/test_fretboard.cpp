#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <zub/core/fretboard.hpp>
#include <zub/core/score.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/textfile.hpp>

namespace fb = zub::fretboard;
namespace fs = std::filesystem;
using Positions = std::vector<fb::Position>;

namespace {

// The cursor's place in `s`: the offset of the first '|'.
std::size_t at(const std::string& s) {
    return s.find('|');
}
std::string without(std::string s) {
    s.erase(s.find('|'), 1);
    return s;
}
// The edit's result with '|' at the cursor.
std::string shown(const fb::Change& c) {
    std::string s = c.text;
    s.insert(c.cursor, "|");
    return s;
}

} // namespace

TEST_CASE("fretboard pitch names across the guitar range", "[fretboard]") {
    CHECK(fb::pitchName(60, true, false) == "c'");
    CHECK(fb::pitchName(48, true, false) == "c");
    CHECK(fb::pitchName(40, true, false) == "e,");
    CHECK(fb::pitchName(40, false, false) == "e,");
    CHECK(fb::pitchName(28, true, false) == "e,,");
    CHECK(fb::pitchName(88, true, false) == "e'''");
    CHECK(fb::pitchName(66, true, false) == "fis'");
    CHECK(fb::pitchName(66, false, false) == "fs'");
    CHECK(fb::pitchName(66, true, true) == "ges'");
    CHECK(fb::pitchName(66, false, true) == "gf'");
    CHECK(fb::pitchName(59, true, false) == "h");
    CHECK(fb::pitchName(59, false, false) == "b");
    CHECK(fb::pitchName(58, true, false) == "ais");
    CHECK(fb::pitchName(58, true, true) == "b");
    CHECK(fb::pitchName(58, false, false) == "as");
    CHECK(fb::pitchName(58, false, true) == "bf");
    CHECK(fb::pitchName(56, true, true) == "as");
    CHECK(fb::pitchName(56, false, true) == "af");
    CHECK(fb::pitchName(51, true, true) == "es");
    CHECK(fb::pitchName(51, false, true) == "ef");
    CHECK(fb::pitchName(49, true, true) == "des");
    CHECK(fb::pitchName(49, false, true) == "df");

    // Every pitch E2..E6 reads back as itself, in all four spellings.
    for (int midi = 40; midi <= 88; ++midi) {
        for (const bool german : {true, false}) {
            for (const bool flats : {true, false}) {
                const std::string text = fb::noteToken(midi, 1, german, flats);
                const auto positions = fb::positionsAt(text, 0, fb::Tuning{{0}}, german);
                REQUIRE(positions.size() == 1);
                CHECK(positions[0].fret == midi);
            }
        }
    }
}

TEST_CASE("fretboard noteToken and flats", "[fretboard]") {
    CHECK(fb::noteToken(66, 1, true, false) == "fis'\\1");
    CHECK(fb::noteToken(66, 1, false, false) == "fs'\\1");
    CHECK(fb::noteToken(40, 6, true, false) == "e,\\6");
    CHECK(fb::prefersFlats("E\\flt"));
    CHECK(fb::prefersFlats("{B\\flt m\\rightarrow G\\flt}"));
    CHECK(fb::prefersFlats("A\\flt{}"));
    CHECK(fb::prefersFlats("F"));
    CHECK(fb::prefersFlats("Dm"));
    CHECK(fb::prefersFlats("Gm"));
    CHECK(fb::prefersFlats("Cm"));
    CHECK(fb::prefersFlats("Fm"));
    CHECK_FALSE(fb::prefersFlats("Am"));
    CHECK_FALSE(fb::prefersFlats("C"));
    CHECK_FALSE(fb::prefersFlats("G"));
    CHECK_FALSE(fb::prefersFlats("F\\shrp m"));
    CHECK(fb::prefersFlats("Cm\\rightarrow C"));
    CHECK_FALSE(fb::prefersFlats("Am\\rightarrow C\\flt")); // only the first key counts
    CHECK_FALSE(fb::prefersFlats(""));
}

TEST_CASE("fretboard tunings", "[fretboard]") {
    CHECK(fb::guitar().open == std::vector<int>{64, 59, 55, 50, 45, 40});
    CHECK(fb::banjoOpenG().open == std::vector<int>{62, 59, 55, 50, 67});
    CHECK(fb::banjoOpenG().shortString == 5);
    CHECK(fb::guitar().shortString == 0);
    CHECK(fb::tuningOf("music = { e\\1 }\n").open.size() == 6);
    CHECK(fb::tuningOf("\\new TabStaff \\with {\n  stringTunings = #banjo-open-g-tuning\n}\n")
              .open.size() == 5);
    CHECK(fb::tuningOf("% stringTunings = #banjo-open-g-tuning\n{ e\\1 }").open.size() == 6);
}

TEST_CASE("fretboard positionsAt reads chords and notes", "[fretboard]") {
    const auto guitar = fb::guitar();
    const std::string chord = "  <e,\\6 h,\\5 e\\4 g\\3 h\\2 e'\\1>4 |\n";
    const Positions open{{6, 0}, {5, 2}, {4, 2}, {3, 0}, {2, 0}, {1, 0}};     // E minor
    CHECK(fb::positionsAt(chord, 2, guitar, true) == open);                   // at `<`
    CHECK(fb::positionsAt(chord, 10, guitar, true) == open);                  // inside
    CHECK(fb::positionsAt(chord, chord.find("|") - 1, guitar, true) == open); // after `>4`
    CHECK(fb::positionsAt(chord, chord.find("|") + 1, guitar, true)
              .empty()); // after the space, at `|`: bar
    CHECK(fb::positionsAt(chord, 0, guitar, true).empty());

    const std::string line = "fis'\\1 g4\\3( a16\\3 )";
    CHECK(fb::positionsAt(line, 0, guitar, true) == Positions{{1, 2}});
    CHECK(fb::positionsAt(line, 6, guitar, true) == Positions{{1, 2}}); // right after
    CHECK(fb::positionsAt(line, 8, guitar, true) == Positions{{3, 0}});
    CHECK(fb::positionsAt(line, 14, guitar, true) == Positions{{3, 2}});
    // The same text in English: fis is no pitch there, h neither.
    CHECK(fb::positionsAt("fs'\\1", 2, guitar, false) == Positions{{1, 2}});
    CHECK(fb::positionsAt("fis'\\1", 2, guitar, false).empty());
    CHECK(fb::positionsAt("h\\2 bf\\3", 1, guitar, false).empty());
    CHECK(fb::positionsAt("bf\\3", 1, guitar, false) == Positions{{3, 3}});
    CHECK(fb::positionsAt("es\\3 as\\4 b\\2", 1, guitar, true) == Positions{{3, -4}});
    CHECK(fb::positionsAt("es\\3 as\\4 b\\2", 7, guitar, true) == Positions{{4, 6}});
    CHECK(fb::positionsAt("es\\3 as\\4 b\\2", 12, guitar, true) == Positions{{2, -1}});
}

TEST_CASE("fretboard positionsAt in odd places", "[fretboard]") {
    const auto guitar = fb::guitar();
    // Dead notes and glissandi: the commands are skipped, the notes still count.
    CHECK(fb::positionsAt("\\deadNote e\\4", 12, guitar, true) == Positions{{4, 2}});
    CHECK(fb::positionsAt("a16\\3( \\glissando h\\3)", 22, guitar, true) == Positions{{3, 4}});
    CHECK(fb::positionsAt("a16\\3( \\glissando h\\3)", 3, guitar, true) == Positions{{3, 2}});
    // No string, no position.
    CHECK(fb::positionsAt("\\key g \\major c'4 d'", 4, guitar, true).empty());
    // Comments and strings are not music.
    CHECK(fb::positionsAt("% e\\1\n", 3, guitar, true).empty());
    CHECK(fb::positionsAt("%{ e\\1 %} e\\2", 3, guitar, true).empty());
    CHECK(fb::positionsAt("%{ e\\1 %} e'\\2", 12, guitar, true) == Positions{{2, 5}});
    CHECK(fb::positionsAt("instrumentName = \"e\\1\"", 19, guitar, true).empty());
    // A string beyond the tuning is dropped (a banjo score read as a guitar is still safe).
    CHECK(fb::positionsAt("e\\7", 1, guitar, true).empty());
    CHECK(fb::positionsAt("g'\\5", 1, fb::banjoOpenG(), true) == Positions{{5, 0}});
    CHECK(fb::positionsAt("a\\3 a'\\5", 7, fb::banjoOpenG(), true) == Positions{{5, 2}});
    CHECK(fb::positionsAt("<h\\2 d'\\1>4", 3, fb::banjoOpenG(), true) == Positions{{2, 0}, {1, 0}});
    // Anything at all: no crash, no hit.
    for (const std::string text :
         {"", "<", "<<", ">", "<e\\", "e\\", "\\", "%", "%{", "\"", "<e,\\6 h",
          "e'''''''\\99999999999", "\\\x80\xff", "<\\", "e\\1\xc3", "\xc3\xa9\\1 e\\1"}) {
        for (std::size_t cursor = 0; cursor <= text.size() + 2; ++cursor)
            (void)fb::positionsAt(text, cursor, guitar, true);
    }
}

TEST_CASE("fretboard effectiveDuration", "[fretboard]") {
    CHECK(fb::effectiveDuration("", 0) == "4");
    CHECK(fb::effectiveDuration("e\\1 g\\2", 7) == "4");
    const std::string text =
        "\\time 3/4 \\tempo 4 = 120\ng8\\3 a\\3 <e\\4 g\\3>2. r8 \\tuplet 3/2 { a16\\3 b\\3 }\n";
    CHECK(fb::effectiveDuration(text, 0) == "4");
    CHECK(fb::effectiveDuration(text, 22) == "4"); // before the first note's duration ends
    CHECK(fb::effectiveDuration(text, text.find("g8") + 2) == "8"); // after g8
    CHECK(fb::effectiveDuration(text, text.find("<e")) == "8");
    CHECK(fb::effectiveDuration(text, text.find(" r8")) == "2.");
    CHECK(fb::effectiveDuration(text, text.find(" \\tuplet")) == "8");
    CHECK(fb::effectiveDuration(text, text.size()) == "16");
    // Commands' numbers do not count, however late.
    CHECK(fb::effectiveDuration("e8\\1 \\partial 16 \\repeat volta 2 \\time 6/8 ", 40) == "8");
    // Either language for the notes.
    CHECK(fb::effectiveDuration("h2\\2 ", 5) == "2");
    CHECK(fb::effectiveDuration("bf2\\3 ", 6) == "2");
}

TEST_CASE("fretboard insertNote spacing and duration", "[fretboard]") {
    const auto insert = [](const std::string& marked, const std::string& token,
                           const std::string& duration) {
        return shown(fb::insertNote(without(marked), at(marked), token, duration));
    };
    CHECK(insert("|", "fis'\\1", "") == "fis'\\1|");
    CHECK(insert("e\\2 |", "fis'\\1", "") == "e\\2 fis'\\1|");
    CHECK(insert("e\\2|", "fis'\\1", "") == "e\\2 fis'\\1|");
    CHECK(insert("e\\2| g\\3", "fis'\\1", "") == "e\\2 fis'\\1| g\\3");
    CHECK(insert("e\\2 |g\\3", "fis'\\1", "") == "e\\2 fis'\\1| g\\3");
    CHECK(insert("e\\2|g\\3", "fis'\\1", "") == "e\\2 fis'\\1| g\\3");
    CHECK(insert("    |", "e\\4", "8") == "    e8\\4|");
    CHECK(insert("e\\2 |", "fis'\\1", "8.") == "e\\2 fis'8.\\1|");
    CHECK(insert("|", "<e\\4 g\\3>", "2") == "<e\\4 g\\3>2|");
    // A cursor inside a note or chord goes behind it.
    CHECK(insert("e4\\|2 g\\3", "a\\3", "") == "e4\\2 a\\3| g\\3");
    CHECK(insert("<e\\4 |g\\3>4 g\\3", "a\\3", "") == "<e\\4 g\\3>4 a\\3| g\\3");
    CHECK(insert("<e\\4 g\\3>4| g\\3", "a\\3", "") == "<e\\4 g\\3>4 a\\3| g\\3");
    // UTF-8 around: the cursor is a byte offset.
    CHECK(insert("% \xc4\x9b |", "e\\4", "") == "% \xc4\x9b e\\4|");
}

TEST_CASE("fretboard addToChord", "[fretboard]") {
    const auto add = [](const std::string& marked, const std::string& token) {
        return shown(fb::addToChord(without(marked), at(marked), token));
    };
    // Into a chord: before its `>`.
    CHECK(add("<e\\4 g\\3>4|", "h\\2") == "<e\\4 g\\3 h\\2|>4");
    CHECK(add("<e\\4 g\\3>4 |", "h\\2") == "<e\\4 g\\3 h\\2|>4 ");
    CHECK(add("<e\\4 |g\\3>4", "h\\2") == "<e\\4 g\\3 h\\2|>4");
    CHECK(add("a\\3 <e\\4 g\\3>|2. b\\2", "h\\2") == "a\\3 <e\\4 g\\3 h\\2|>2. b\\2");
    // A single note becomes a chord, keeping duration and what follows it.
    CHECK(add("g4\\3|", "e\\2") == "<g\\3 e\\2|>4");
    CHECK(add("a\\3 g4\\3(| b\\2", "e\\2") == "a\\3 <g\\3 e\\2|>4( b\\2");
    CHECK(add("g8.\\3~ |", "e\\2") == "<g\\3 e\\2|>8.~ ");
    CHECK(add("fis'\\1|", "e\\2") == "<fis'\\1 e\\2|>");
    // Nothing to join: like insertNote.
    CHECK(add("|", "e\\2") == "e\\2|");
    CHECK(add("\\time 3/4 |", "e\\2") == "\\time 3/4 e\\2|");
    CHECK(add("e\\1 | g\\2", "e\\2") == "<e\\1 e\\2|>  g\\2");
    CHECK(add("e\\1 x |", "e\\2") == "e\\1 x e\\2|");
}

TEST_CASE("fretboard insertRest and insertBarCheck", "[fretboard]") {
    CHECK(shown(fb::insertRest("e\\1 ", 4, "")) == "e\\1 r|");
    CHECK(shown(fb::insertRest("e\\1", 3, "8")) == "e\\1 r8|");
    CHECK(shown(fb::insertRest("", 0, "4.")) == "r4.|");
    CHECK(fb::insertBarCheck("e\\1 g\\2", 7).text == "e\\1 g\\2 |");
    CHECK(fb::insertBarCheck("e\\1 ", 4).text == "e\\1 |");
    CHECK(fb::insertBarCheck("e\\1 ", 4).cursor == 5);
    CHECK(fb::insertBarCheck("", 0).text == "|");
    // Cursor beyond the line: clamped.
    CHECK(fb::insertBarCheck("e\\1", 99).text == "e\\1 |");
}

// Hidden: reads the real ../ZubuvZpevnik checkout (read-only).
// Run with: build/debug/tests/zub_core_tests "[integration]"
TEST_CASE("every note of every real score has a valid position", "[.integration][fretboard]") {
    const fs::path realRepo{ZUB_REAL_SONGBOOK_DIR};
    if (!fs::is_directory(realRepo / "songs"))
        SKIP("no songbook checkout at " << realRepo);

    std::map<std::string, bool> germanOf;
    for (const auto& entry : fs::recursive_directory_iterator(realRepo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tex")
            continue;
        const auto text = zub::readTextFile(entry.path());
        REQUIRE(text);
        const auto meta = zub::parseSong(*text);
        if (!meta)
            continue;
        for (const auto& ref : zub::songbook::referencedFiles(*text))
            germanOf[ref] = zub::score::germanNames(meta->value("lang"));
    }

    // A few real scores write a note below its string's open pitch (a leftover guitar chord in a
    // banjo score, a typo): those are tolerated up to 3 % of the positions; the rest must be a
    // playable fret.
    int scores = 0;
    int cursors = 0;
    int unplayable = 0;
    std::set<std::string> reported;
    for (const auto& entry : fs::recursive_directory_iterator(realRepo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ly")
            continue;
        const auto content = zub::readTextFile(entry.path());
        REQUIRE(content);
        const auto name =
            fs::relative(entry.path(), entry.path().parent_path().parent_path()).generic_string();
        const auto known = germanOf.find(name);
        const bool german = known != germanOf.end() && known->second;
        const auto tuning = fb::tuningOf(*content);
        ++scores;
        for (std::size_t cursor = 0; cursor <= content->size(); ++cursor) {
            const auto positions = fb::positionsAt(*content, cursor, tuning, german);
            if (positions.empty())
                continue;
            ++cursors;
            for (const auto& p : positions) {
                CHECK(p.string >= 1);
                CHECK(static_cast<std::size_t>(p.string) <= tuning.open.size());
                if (p.fret >= 0 && p.fret <= 24)
                    continue;
                ++unplayable;
                const std::string what = name + (german ? " (deutsch)" : "") + ": string " +
                                         std::to_string(p.string) + " fret " +
                                         std::to_string(p.fret);
                if (reported.insert(what).second)
                    UNSCOPED_INFO(what << " at offset " << cursor);
                break;
            }
        }
    }
    CAPTURE(cursors, unplayable);
    CHECK(unplayable * 100 < cursors * 3);
    CHECK(scores > 0);
    CHECK(cursors > 0);
}
