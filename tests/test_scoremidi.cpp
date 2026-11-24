#include "midi_events.hpp"

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <zub/core/midi.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/score.hpp>
#include <zub/core/scoretiming.hpp>
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
    const std::string text = "% \\score { c }\n%{ \\score { d } %}\n#(define x \"\\\\score {\")\n"
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
    CHECK(sc::scaleTempo("\\tempo \"Allegro\" 4 = 120 c4", 0.75) ==
          "\\tempo \"Allegro\" 4 = 90 c4");
    CHECK(sc::scaleTempo("\\tempo 4 = 100-120 c4", 0.5) == "\\tempo 4 = 50-60 c4");
    CHECK(sc::scaleTempo("\\tempo 2=80 c4 \\tempo 8 = 200", 0.5) ==
          "\\tempo 2=40 c4 \\tempo 8 = 100");
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
    CHECK(
        contains(*score, "\\context { \\TabStaff midiInstrument = \"acoustic guitar (steel)\" }"));
    CHECK(contains(*score, "\\repeat volta 2 { a,16\\5"));
    // The score's own \layout is not copied; the only one lists the notes (zub::scoretiming).
    CHECK(std::ranges::count(*score, '#') == 2); // the `##t` of skipTypesetting, no Scheme
    CHECK(contains(*score, "\\layout {\n    \\context { \\Score skipTypesetting = ##t }\n"
                           "    \\context { \\Voice \\consists \\zubNoteLogger }\n"
                           "    \\context { \\TabVoice \\consists \\zubNoteLogger }\n"
                           "    \\context { \\ChordNames \\consists \\zubNoteLogger }\n"
                           "    \\context { \\Devnull \\consists \\zubNoteLogger }\n"
                           "    \\context { \\Lyrics \\consists \\zubNoteLogger }\n  }"));
    CHECK_FALSE(contains(score->substr(score->find("\\layout") + 1), "\\layout"));
    CHECK_FALSE(contains(*score, "\\header"));
    CHECK_FALSE(contains(*score, "= 50")); // the score's own \midi is not copied
    CHECK(score->ends_with("\n  }\n}"));
}

TEST_CASE("midiScore: the amp staves' own book does not list notes", "[score][midi]") {
    auto o = options();
    o.sounding = sc::MidiOptions::Sounding::AmpOnly;
    const auto score = sc::midiScore(jolene, o);
    REQUIRE(score);
    CHECK_FALSE(contains(*score, "\\layout"));
    CHECK_FALSE(contains(*score, "zubNoteLogger"));
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

TEST_CASE("midiScore scales the score's tempi and the factor sets the midi tempo",
          "[score][midi]") {
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
    request.includeDir = fs::absolute("/songbook/zubuvzpevnik/lilypond");

    auto cmd = sc::prepare(request); // without midi: as before
    REQUIRE(cmd);
    CHECK(cmd->midi.empty());
    CHECK(cmd->svg == dir.path / "work" / "out" / "score.cropped.svg");

    request.midi = options();
    cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(cmd->midi == dir.path / "work" / "out" / "zub-playback.midi");
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

TEST_CASE("prepare appends no playback score after text that would change how it is read",
          "[score][midi][safety]") {
    // Each text is harmless as it is (no risks), but ends inside a string, a %{ comment or a
    // quoted Scheme list: a copy of its music appended after it would turn the payload into code.
    TempDir dir;
    sc::Request request;
    request.workDir = dir.path / "work";
    request.inputName = "Artist/Title.ly";
    request.includeDir = fs::absolute("/songbook/zubuvzpevnik/lilypond");
    request.midi = options();
    for (const std::string text :
         {"\\score { { c'4^\"#(ly:warning (symbol->string 'PWNED))\" } }\n\"\n",
          "\\score { { c'4 %} #(ly:warning \"PWNEDB\")\n } }\n%{\n",
          "\\score { { c'4 % ) #(ly:warning \"PWNEDQ\")\n } }\n#'(\n"}) {
        CAPTURE(text);
        REQUIRE(zub::safety::lilypondRisks(text).empty());
        CHECK(sc::midiScore(text, options())); // there is music to play: the guard refuses it
        request.content = text;
        const auto cmd = sc::prepare(request);
        REQUIRE(cmd);
        CHECK(cmd->midi.empty()); // no playback, and the copy is the text alone
        std::ifstream copy(cmd->scoreCopy);
        std::stringstream copied;
        copied << copy.rdbuf();
        CHECK(copied.str() == text);
        CHECK(zub::safety::lilypondRisks(copied.str()).empty());
    }
}

TEST_CASE("markBends marks the targets of bends, pre-bends, releases and slides", "[score][midi]") {
    const auto set = [](const char* name) {
        return std::string("\\once \\set Staff.midiInstrument = \"") + name + "\" ";
    };
    // A bend; a pre-bend (only \^ between \preBend and \grace); both with a release (\^ after the
    // grace note); a slide with a bar check before the target.
    const std::string music =
        "a'\\2\\^ \\grace h'8\\2 a'\\preBend \\^ \\grace h'\\2 a'16\\^ \\grace h'16\\2\\^ a'\\2 "
        "a'\\preBend \\^ \\grace h'\\2\\^ a' d'\\3 \\glissando | g'\\2 g'\\glissando <a e'>";
    const auto marked = sc::markBends(music);
    CHECK(contains(marked, "\\^ " + set("gunshot") + "\\grace h'8"));
    CHECK(contains(marked, "\\^ " + set("helicopter") + "\\grace h'\\2 a'16"));
    CHECK(contains(marked, "\\^ " + set("applause") + "\\grace h'16\\2\\^"));
    CHECK(contains(marked, "\\^ " + set("telephone ring") + "\\grace h'\\2\\^ a'"));
    CHECK(contains(marked, "\\glissando | " + set("seashore") + "g'\\2"));
    CHECK(contains(marked, "\\glissando " + set("seashore") + "<a e'>"));
    // The music itself is unchanged.
    std::string without = marked;
    for (const char* name : {"gunshot", "helicopter", "applause", "telephone ring", "seashore"})
        for (auto at = without.find(set(name)); at != std::string::npos;
             at = without.find(set(name)))
            without.erase(at, set(name).size());
    CHECK(without == music);
}

TEST_CASE("markBends finds a slide's target past \\unHideNotes and a grace's brace",
          "[score][midi]") {
    // A slide from nowhere: a hidden grace note slides into the note.
    const std::string slide = "\\grace { \\hideNotes g\\3 \\glissando \\unHideNotes } e'4\\3";
    const std::string marked = sc::markBends(slide);
    CHECK(marked.find("\"bird tweet\" e'4") != std::string::npos); // slides in
    // A grace note that slides on is no bend target (no bend marker before it).
    for (const std::string text : {"f4\\4 \\hideNotes\\grace g4\\3\\glissando\\unHideNotes e'4\\3",
                                    "g'4\\2 \\grace g'\\2 \\glissando a'8\\2"}) {
        const std::string out = sc::markBends(text);
        INFO(out);
        CHECK(out.find("gunshot") == std::string::npos);
        CHECK(out.find("bird tweet") != std::string::npos);
    }
    // A slide between two notes of one grace stays a slide from one to the other.
    const std::string within = sc::markBends("\\grace { g16\\glissando a16 } b4");
    CHECK(within.find("seashore") != std::string::npos);
    CHECK(within.find("bird tweet") == std::string::npos);
    // Between two sounding notes it is still a slide from one to the other.
    CHECK(sc::markBends("c4\\3 \\glissando e4\\3").find("seashore") != std::string::npos);
    // Still nothing before a rest after the brace.
    CHECK(sc::markBends("\\grace { g\\3 \\glissando } r4").find("seashore") == std::string::npos);
}

TEST_CASE("markBends leaves comments, strings, Scheme, rests and a plain \\grace shape alone",
          "[score][midi]") {
    const std::string music =
        "% \\grace c' \\glissando c''\n\\markup \"\\grace\" #(display \"\\glissando\") "
        "c'\\glissando r c' \\glissando s c' \\grace { d'8 } \\grace\n";
    CHECK(sc::markBends(music) == music);
    CHECK(sc::markBends("c'4 d'") == "c'4 d'");
}

TEST_CASE("the playback score marks bends and is still appended", "[score][midi]") {
    TempDir dir;
    sc::Request request;
    request.workDir = dir.path / "work";
    request.inputName = "Artist/Title.ly";
    request.content = "\\score { \\new TabStaff { a'4\\2\\^ \\grace h'8\\2 a'4\\2 } }\n";
    request.includeDir = fs::absolute("/songbook/zubuvzpevnik/lilypond");
    request.midi = options();
    const auto cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK_FALSE(cmd->midi.empty()); // appendIsNeutral: the markers add no Scheme
    std::ifstream copy(cmd->scoreCopy);
    std::stringstream copied;
    copied << copy.rdbuf();
    CHECK(contains(copied.str(), "\\once \\set Staff.midiInstrument = \"gunshot\" \\grace"));
    CHECK(zub::safety::lilypondRisks(copied.str()).empty());
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
    int timed = 0;
    int withChordNames = 0;
    int withLyrics = 0;
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
        midi.melodyInstrument = "voice"; // a hidden melody must build as a sounding staff too
        midi.transpose = 2;              // ... and the music inside \\transpose
        request.midi = midi;
        const auto cmd = sc::prepare(request);
        REQUIRE(cmd);
        std::string line =
            "cd '" + cmd->workingDirectory.string() + "' && LC_ALL=C " + cmd->program;
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
        if (ok) {
            std::ifstream midiIn(cmd->midi, std::ios::binary);
            std::stringstream midiBytes;
            midiBytes << midiIn.rdbuf();
            const auto smoothed = zub::midi::smoothBends(midiBytes.str());
            if (!smoothed) {
                UNSCOPED_INFO(name << ": " << smoothed.error());
            }
            REQUIRE(smoothed);
            const auto raw = testmidi::parse(midiBytes.str());
            const auto after = testmidi::parse(*smoothed);
            REQUIRE(raw.size() == after.size());
            for (std::size_t t = 0; t < raw.size(); ++t) {
                int before = 0;
                int now = 0;
                int markers = 0;
                for (const auto& e : raw[t]) {
                    before += e.noteOn() ? 1 : 0;
                    markers +=
                        e.status == 0xc0 && e.d1 >= 122 && e.d1 <= 127 ? 1 : 0;
                }
                for (const auto& e : after[t]) {
                    now += e.noteOn() ? 1 : 0;
                    CHECK_FALSE((e.status == 0xc0 && e.d1 >= 122 && e.d1 <= 127));
                }
                // A marker merges at most two notes (the target and a release) into its note.
                CHECK(now <= before);
                CHECK(now >= before - 2 * markers);
            }
        }
        // The listener's note list gives a timeline: every note of the score's own file sits in a
        // music event of the text (the score cursor can follow it), the playback score's copy of
        // the music pairs with the text's events.
        if (ok) {
            const auto notes = zub::readTextFile(cmd->notes);
            const auto copy = zub::readTextFile(cmd->scoreCopy);
            const auto midiBytes = zub::readTextFile(cmd->midi);
            REQUIRE(notes);
            REQUIRE(copy);
            REQUIRE(midiBytes);
            std::string editorText = *content; // the editor's text has LF
            for (std::size_t at = 0; (at = editorText.find("\r\n", at)) != std::string::npos;)
                editorText.erase(at, 1);
            const zub::scoretiming::Source source{
                *notes, *midiBytes, *copy, editorText, cmd->scoreCopy, cmd->generatedFromLine};
            const auto timeline = zub::scoretiming::buildTimeline(source);
            if (!timeline) {
                UNSCOPED_INFO(name << ": " << timeline.error());
            }
            REQUIRE(timeline);
            // Not events (zub::scoreregion::musicEvents): a definition of a single note or chord
            // (`emChord = <e,\\6 e\\4>4`), which the cursor cannot stand in either.
            static const std::regex singleDefinition(R"((^|\n)[A-Za-z]+ *= *(<|[a-g]))");
            const bool hasSingle = std::regex_search(editorText, singleDefinition);
            if (timeline->unmapped > 0 && !hasSingle) {
                UNSCOPED_INFO(name << ": " << timeline->unmapped << " notes are in no music event");
            }
            CHECK((timeline->unmapped == 0 || hasSingle));
            CHECK_FALSE(timeline->empty());
            // Sounds (zub::scoretiming::soundingAt): the chord names and lyrics of the scores that
            // have them are mapped too.
            for (const auto& sound : timeline->sounds) {
                withChordNames += sound.kind == zub::scoretiming::Kind::ChordName ? 1 : 0;
                withLyrics += sound.kind == zub::scoretiming::Kind::Lyric ? 1 : 0;
                CHECK(sound.end > sound.start);
                CHECK(sound.range.end <= editorText.size());
            }
            ++timed;
        }
        ++count;
    }
    CHECK(count > 0);
    CHECK(timed > 0);
    CHECK(withChordNames > 0);
    CHECK(withLyrics > 0);
#endif
}

// Hidden: needs lilypond. The timeline of the synthetic score of tests/data/timing/ built for every
// way the playback score is assembled: all staves in one book, some staves through the amp (a
// second book with the same music appended), all of them through the amp, chord names and capo.
TEST_CASE("the timeline is the same for every way the playback score is built",
          "[.integration][score][midi][scoretiming]") {
#ifdef _WIN32
    SKIP("POSIX shell only");
#else
    if (std::system("command -v lilypond >/dev/null 2>&1") != 0)
        SKIP("lilypond is not installed");
    const fs::path data = fs::path(ZUB_TEST_DATA_DIR) / "timing";
    const auto content = zub::readTextFile(data / "Timing.ly");
    REQUIRE(content);
    struct Variant {
        const char* name;
        std::vector<std::string> staves;
        int capo;
        std::string chords;
    };
    const std::vector<Variant> variants{{"default", {}, 0, ""},
                                        {"mixed amp", {"amp", "steel"}, 0, ""},
                                        {"all amp", {"amp", "amp"}, 2, ""},
                                        {"amp chords", {"steel", "steel"}, 0, "amp"}};
    std::size_t cueCount = 0;
    for (const auto& variant : variants) {
        TempDir dir;
        sc::Request request;
        request.workDir = dir.path / "work";
        request.songbookRoot = dir.path;
        request.inputName = "Test/Timing.ly";
        request.content = *content;
        request.includeDir = dir.path;
        sc::MidiOptions midi;
        midi.instrument = "acoustic guitar (steel)";
        midi.staffInstruments = variant.staves;
        midi.transpose = variant.capo;
        midi.chordInstrument = variant.chords;
        request.midi = midi;
        const auto cmd = sc::prepare(request);
        REQUIRE(cmd);
        std::string line =
            "cd '" + cmd->workingDirectory.string() + "' && LC_ALL=C " + cmd->program;
        for (const auto& a : cmd->arguments)
            line += " '" + a + "'";
        line += " >'" + (dir.path / "log").string() + "' 2>&1";
        INFO(variant.name);
        REQUIRE(std::system(line.c_str()) == 0);
        const auto notes = zub::readTextFile(cmd->notes);
        const auto copy = zub::readTextFile(cmd->scoreCopy);
        const auto midiBytes = zub::readTextFile(cmd->midi);
        REQUIRE(notes);
        REQUIRE(copy);
        REQUIRE(midiBytes);
        const auto timeline = zub::scoretiming::buildTimeline(
            {*notes, *midiBytes, *copy, *content, cmd->scoreCopy, cmd->generatedFromLine});
        REQUIRE(timeline);
        CHECK(timeline->unmapped == 0);
        CHECK(timeline->cues.size() > 10);
        if (cueCount == 0)
            cueCount = timeline->cues.size();
        CHECK(timeline->cues.size() == cueCount);
        // Raising the music by the capo does not move it in time.
        CHECK(timeline->cues.back().seconds == Catch::Approx(5.75));
    }
#endif
}

TEST_CASE("a hidden melody sounds only with an instrument of its own", "[score][midi]") {
    const std::string text = "\\score {\n  <<\n    \\new Staff { c'4 }\n"
                             "    % \\new Devnull in a comment\n"
                             "    \\new Devnull = \"vocal\" { \\nullMelody }\n"
                             "    \\new Lyrics \\lyricsto \"vocal\" { \\text }\n  >>\n}\n";
    CHECK(sc::hasHiddenMelody(text));
    CHECK_FALSE(sc::hasHiddenMelody("\\score { \\new Staff { c'4 } % \\new Devnull\n}"));
    sc::MidiOptions options;
    options.tempo = 100;
    options.instrument = "acoustic grand";
    const auto silent = sc::midiScore(text, options);
    REQUIRE(silent);
    CHECK(silent->find("\\new Devnull = \"vocal\"") != std::string::npos);
    options.melodyInstrument = "voice";
    const auto sung = sc::midiScore(text, options);
    REQUIRE(sung);
    CHECK(sung->find("\\new Staff \\with { midiInstrument = \"voice oohs\" } \\new Voice = "
                     "\"vocal\" \\with { zubMelody = ##t } { \\nullMelody }") != std::string::npos);
    CHECK(sung->find("% \\new Devnull in a comment") != std::string::npos);
    // The printed score is the file itself: only the MIDI copy changes.
    CHECK(zub::midi::findInstrument("voice")->program == 53);
}

TEST_CASE("errors in the appended playback score are dropped", "[score][midi]") {
    TempDir dir;
    sc::Request request;
    request.workDir = dir.path / "work";
    request.inputName = "Artist/Title.ly";
    request.content = "\\score { { c'4 } }\n";
    request.includeDir = fs::absolute("/songbook/zubuvzpevnik/lilypond");

    auto cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(cmd->generatedFromLine == 0); // nothing appended

    request.midi = options();
    cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(cmd->generatedFromLine == 2);

    const sc::Error own{"bad", cmd->scoreCopy, 1, 3};
    const sc::Error generated{"bad", cmd->scoreCopy, 5, 3};
    const sc::Error other{"bad", "/songbook/zubuvzpevnik/lilypond/x.ily", 5, 3};
    const auto kept = sc::withoutGeneratedErrors({own, generated, other}, *cmd);
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].line == 1);
    CHECK(kept[1].file == other.file);

    sc::Command none;
    CHECK(sc::withoutGeneratedErrors({generated}, none).size() == 1);
}
