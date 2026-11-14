#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <zub/core/midi.hpp>
#include <zub/core/score.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;
namespace sc = zub::score;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

const char* jolene = R"(\include "zubuvzpevnik.ily"

\score {
  <<
  \new ChordNames {
    \chordmode { \repeat volta 2 { a1:m | a:m | } }
  }
  \new TabStaff \with { instrumentName = "Kytara " } {
    \tabFullNotation
    \repeat volta 2 { a,16\5 a\3 e\4 b\2 }
  }
  >>
  \layout { \context { \Score \override SpacingSpanner.common-shortest-duration = #(ly:make-moment 1/32) } }
  \header { piece = "x" }
  \midi { \tempo 4 = 50 }
}
)";

sc::MidiOptions options() {
    sc::MidiOptions o;
    o.tempo = 120;
    o.factor = 1.0;
    o.instrument = "acoustic guitar (steel)";
    return o;
}

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device device;
        path = fs::temp_directory_path() /
               ("zub-scoremidi-test-" + std::to_string(device()) + "-" + std::to_string(device()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

} // namespace

TEST_CASE("scoreMusic is the first score without its layout, header and midi", "[score][midi]") {
    const auto music = sc::scoreMusic(jolene);
    REQUIRE(music);
    CHECK(contains(*music, "\\new ChordNames"));
    CHECK(contains(*music, "\\with { instrumentName = \"Kytara \" }"));
    CHECK(contains(*music, "\\repeat volta 2 { a,16\\5"));
    CHECK_FALSE(contains(*music, "layout"));
    CHECK_FALSE(contains(*music, "header"));
    CHECK_FALSE(contains(*music, "midi"));
    CHECK_FALSE(contains(*music, "SpacingSpanner"));
}

TEST_CASE("scoreMusic skips comments, strings and Scheme before the score", "[score][midi]") {
    const std::string text =
        "% \\score { c }\n%{ \\score { d } %}\n#(define x \"\\\\score {\")\n"
        "text = \"\\\\score { e }\"\n\\score { { f4 } }\n\\score { { g4 } }\n";
    const auto music = sc::scoreMusic(text);
    REQUIRE(music);
    CHECK(*music == " { f4 } ");
}

TEST_CASE("scoreMusic needs a balanced score block", "[score][midi]") {
    CHECK_FALSE(sc::scoreMusic("{ c4 d }"));
    CHECK_FALSE(sc::scoreMusic("\\score { { c4 d }"));
    CHECK_FALSE(sc::scoreMusic("\\score c4"));
    // A brace in a string or comment does not count.
    const auto music = sc::scoreMusic("\\score { { c4^\"}\" % }\n d4 } }");
    REQUIRE(music);
    CHECK(contains(*music, "d4"));
}

TEST_CASE("scoreMusic keeps layout blocks that are inside the music", "[score][midi]") {
    const auto music = sc::scoreMusic("\\score { \\new Staff \\with { \\override X = 1 } { c4 } "
                                      "\\layout { a } }");
    REQUIRE(music);
    CHECK(contains(*music, "\\with { \\override X = 1 }"));
    CHECK_FALSE(contains(*music, "layout"));
}

TEST_CASE("scaleTempo scales every form of \\tempo", "[score][midi]") {
    CHECK(sc::scaleTempo("\\tempo 4 = 100 c4", 0.5) == "\\tempo 4 = 50 c4");
    CHECK(sc::scaleTempo("\\tempo 4. = 60 c4", 1.25) == "\\tempo 4. = 75 c4");
    CHECK(sc::scaleTempo("\\tempo \"Allegro\" 4 = 120 c4", 0.75) == "\\tempo \"Allegro\" 4 = 90 c4");
    CHECK(sc::scaleTempo("\\tempo 4 = 100-120 c4", 0.5) == "\\tempo 4 = 50-60 c4");
    CHECK(sc::scaleTempo("\\tempo 2=80 c4 \\tempo 8 = 200", 0.5) == "\\tempo 2=40 c4 \\tempo 8 = 100");
    CHECK(sc::scaleTempo("\\tempo 4 = 1", 0.1) == "\\tempo 4 = 1"); // never below 1
    // Nothing to scale.
    CHECK(sc::scaleTempo("\\tempo \"Allegro\" c4", 0.5) == "\\tempo \"Allegro\" c4");
    CHECK(sc::scaleTempo("c4 d4", 0.5) == "c4 d4");
    // Comments, strings and Scheme.
    CHECK(sc::scaleTempo("% \\tempo 4 = 100\nc4", 0.5) == "% \\tempo 4 = 100\nc4");
    CHECK(sc::scaleTempo("c4^\"\\\\tempo 4 = 100\"", 0.5) == "c4^\"\\\\tempo 4 = 100\"");
    CHECK(sc::scaleTempo("#(display \"\\\\tempo 4 = 100\") c4", 0.5) ==
          "#(display \"\\\\tempo 4 = 100\") c4");
    CHECK(sc::scaleTempo("\\tempo 4 =", 2.0) == "\\tempo 4 =");
}

TEST_CASE("songTempo reads the tem header", "[score][midi]") {
    CHECK(sc::songTempo("{120}") == 120);
    CHECK(sc::songTempo(" { 97 } ") == 97);
    CHECK(sc::songTempo("88") == 88);
    CHECK(sc::songTempo("") == 100);
    CHECK(sc::songTempo("{}") == 100);
    CHECK(sc::songTempo("{Tempo di valse}") == 100);
    CHECK(sc::songTempo("{120 or so}") == 100);
    CHECK(sc::songTempo("{3}") == 100);
    CHECK(sc::songTempo("{9000}") == 100);
}

TEST_CASE("midiInstrument follows the score", "[score][midi]") {
    CHECK(sc::midiInstrument(jolene) == "acoustic guitar (steel)");
    CHECK(sc::midiInstrument("\\include \"zubuvzpevnik.ily\"\n\\score { \\new TabStaff \\with { "
                             "stringTunings = #banjo-open-g-tuning } { c4 } }") == "banjo");
    CHECK(sc::midiInstrument("\\include \"tinwhistle.ily\"\n\\score { { c4 } }") == "whistle");
    CHECK(sc::midiInstrument("% \\include \"tinwhistle.ily\"\n\\score { { c4 } }") ==
          "acoustic guitar (steel)");
}

TEST_CASE("midiScore is a score that unfolds repeats and plays", "[score][midi]") {
    auto o = options();
    o.tempo = 100;
    o.factor = 0.75;
    const auto score = sc::midiScore(jolene, o);
    REQUIRE(score);
    CHECK(score->starts_with("\\score {\n  \\unfoldRepeats {"));
    CHECK(contains(*score, "\\tempo 4 = 75\n")); // 100 * 0.75
    CHECK(contains(*score, "\\context { \\ChordNames \\remove \"Staff_performer\" }"));
    CHECK(contains(*score, "\\context { \\Staff midiInstrument = \"acoustic guitar (steel)\" }"));
    CHECK(contains(*score, "\\context { \\TabStaff midiInstrument = \"acoustic guitar (steel)\" }"));
    CHECK(contains(*score, "\\repeat volta 2 { a,16\\5"));
    CHECK_FALSE(contains(*score, "\\layout"));
    CHECK_FALSE(contains(*score, "\\header"));
    CHECK_FALSE(contains(*score, "= 50")); // the score's own \midi is not copied
    CHECK(score->ends_with("\n  }\n}"));
}

TEST_CASE("midiScore sounds as with the capo on", "[score][midi]") {
    auto o = options();
    o.transpose = 4;
    const auto english = sc::midiScore(jolene, o);
    REQUIRE(english);
    CHECK(english->starts_with("\\score {\n  \\transpose c' e' {\n  \\unfoldRepeats {"));
    o.transpose = 11;
    o.germanNames = true;
    CHECK(contains(*sc::midiScore(jolene, o), "\\transpose c' h' {"));
    o.transpose = 1;
    o.germanNames = false;
    CHECK(contains(*sc::midiScore(jolene, o), "\\transpose c' cs' {"));
    o.transpose = 0;
    CHECK(sc::midiScore(jolene, o)->starts_with("\\score {\n  \\unfoldRepeats {"));
}

TEST_CASE("songCapo reads the cap header", "[score][midi]") {
    CHECK(sc::songCapo("{4}") == 4);
    CHECK(sc::songCapo(" 2 ") == 2);
    CHECK(sc::songCapo("{12}") == 12);
    CHECK(sc::songCapo("") == 0);
    CHECK(sc::songCapo("{13}") == 0);
    CHECK(sc::songCapo("{II}") == 0);
    CHECK(sc::songCapo("{2.5}") == 0);
}

TEST_CASE("midiScore scales the score's tempi and the factor sets the midi tempo", "[score][midi]") {
    auto o = options();
    o.factor = 0.5;
    const auto score = sc::midiScore("melody = { c4 }\n\\score { \\tempo 4 = 80 \\melody }\n", o);
    REQUIRE(score);
    CHECK(contains(*score, "\\tempo 4 = 40 \\melody"));
    CHECK(contains(*score, "\\midi {\n    \\tempo 4 = 60\n")); // 120 * 0.5
}

TEST_CASE("midiScore needs a score and keeps odd names out", "[score][midi]") {
    CHECK_FALSE(sc::midiScore("{ c4 }", options()));
    auto o = options();
    o.instrument = "ban\"jo\\";
    const auto score = sc::midiScore(jolene, o);
    REQUIRE(score);
    CHECK(contains(*score, "midiInstrument = \"banjo\""));
}

TEST_CASE("prepare appends the midi score and reports the MIDI file", "[score][midi]") {
    TempDir dir;
    sc::Request request;
    request.workDir = dir.path / "work";
    request.inputName = "Artist/Title.ly";
    request.content = jolene;
    request.includeDir = "/songbook/zubuvzpevnik/lilypond";

    auto cmd = sc::prepare(request); // without midi: as before
    REQUIRE(cmd);
    CHECK(cmd->midi.empty());
    CHECK(cmd->svg == dir.path / "work" / "out" / "score.cropped.svg");

    request.midi = options();
    cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(cmd->midi == dir.path / "work" / "out" / "score.midi");
    CHECK(cmd->svg == dir.path / "work" / "out" / "score.cropped.svg");
    std::ifstream copy(cmd->scoreCopy);
    std::stringstream copied;
    copied << copy.rdbuf();
    const std::string text = copied.str();
    CHECK(text.starts_with(jolene)); // the original is untouched: line numbers stay valid
    CHECK(contains(text.substr(std::string(jolene).size()), "\\unfoldRepeats"));

    request.content = "{ c4 }"; // nothing to play: the preview is still built
    cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(cmd->midi.empty());
}

// Hidden: needs lilypond and the real ../ZubuvZpevnik checkout.
// Run with: build/debug/tests/zub_core_tests "[integration]"
TEST_CASE("every real score builds an SVG and a MIDI file", "[.integration][score][midi]") {
#ifdef _WIN32
    SKIP("POSIX shell only");
#else
    const fs::path realRepo{ZUB_REAL_SONGBOOK_DIR};
    if (!fs::is_directory(realRepo / "songs"))
        SKIP("no songbook checkout at " << realRepo);
    if (std::system("command -v lilypond >/dev/null 2>&1") != 0)
        SKIP("lilypond is not installed");

    std::map<std::string, bool> germanOf;
    std::map<std::string, int> tempoOf;
    for (const auto& entry : fs::recursive_directory_iterator(realRepo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tex")
            continue;
        const auto text = zub::readTextFile(entry.path());
        REQUIRE(text);
        const auto meta = zub::parseSong(*text);
        if (!meta)
            continue;
        for (const auto& ref : zub::songbook::referencedFiles(*text)) {
            germanOf[ref] = sc::germanNames(meta->value("lang"));
            tempoOf[ref] = sc::songTempo(meta->value("tem"));
        }
    }
    int count = 0;
    TempDir dir;
    for (const auto& entry : fs::recursive_directory_iterator(realRepo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ly")
            continue;
        const auto content = zub::readTextFile(entry.path());
        REQUIRE(content);
        const auto name =
            fs::relative(entry.path(), entry.path().parent_path().parent_path()).generic_string();
        sc::Request request;
        request.songbookRoot = realRepo;
        request.workDir = dir.path / "work";
        request.inputName = name;
        request.content = *content;
        request.germanNames = germanOf.contains(name) ? germanOf[name] : true;
        request.includeDir = realRepo / "zubuvzpevnik/lilypond";
        sc::MidiOptions midi;
        midi.tempo = tempoOf.contains(name) ? tempoOf[name] : 100;
        midi.instrument = sc::midiInstrument(*content);
        request.midi = midi;
        const auto cmd = sc::prepare(request);
        REQUIRE(cmd);
        std::string line = "cd '" + cmd->workingDirectory.string() + "' && LC_ALL=C " + cmd->program;
        for (const auto& a : cmd->arguments)
            line += " '" + a + "'";
        line += " >'" + (dir.path / "log").string() + "' 2>&1";
        const bool ok = std::system(line.c_str()) == 0 && fs::exists(cmd->svg) &&
                        !cmd->midi.empty() && fs::exists(cmd->midi) && fs::file_size(cmd->midi) > 0;
        if (!ok) {
            std::ifstream log(dir.path / "log");
            std::stringstream text;
            text << log.rdbuf();
            UNSCOPED_INFO(name << ": " << text.str());
        }
        CHECK(ok);
        ++count;
    }
    CHECK(count > 0);
#endif
}
