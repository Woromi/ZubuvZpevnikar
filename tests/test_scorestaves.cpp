#include "midi_events.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <zub/core/midi.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/score.hpp>
#include <zub/core/textfile.hpp>

namespace fs = std::filesystem;
namespace sc = zub::score;

namespace {

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

std::size_t count(const std::string& text, const std::string& part) {
    std::size_t n = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
        ++n;
    return n;
}

// Shaped like Jolene: chord names and one tab staff.
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
  \layout { indent = 0 }
}
)";

// Shaped like Rozarka: a banjo tab and a guitar tab.
const char* rozarka = R"(\include "zubuvzpevnik.ily"

\score {
<<
  \new ChordNames { \chordmode { a1:m | } }
  \new TabStaff \with {
    tablatureFormat = #fret-number-tablature-format-banjo
    stringTunings = #banjo-open-g-tuning
    instrumentName = "Banjo "
    % midiInstrument = "flute"
  }
  {
    \tabFullNotation
    a4\3 e'8\1 a\3 h\2 |
  }
  \new TabStaff \with {
    instrumentName = "Kytara "
  }
  {
    \tabFullNotation
    <a,\5 e\4 a\3 c'\2 e'\1>2. |
  }
>>
  \layout { indent = 0 }
}
)";

// Shaped like the tinwhistle scores: a variable played by a Staff with a voice, and a Dynamics.
const char* whistle = R"(\include "tinwhistle.ily"

melody = { r2. d''16 cs'' | b'2 }

\score {
  <<
    \new Staff {
      \clef treble
      \time 4/4
      \new Voice = "lead" { \melody }
    }
    \new Dynamics \with { \override VerticalAxisGroup.nonstaff-nonstaff-spacing.padding = #3 } {
      \twTabs \melody
    }
  >>
  \layout { }
}
)";

std::string withMidiFor(const std::string& score, const sc::MidiOptions& options) {
    const auto played = sc::midiScore(score, options);
    REQUIRE(played);
    return *played;
}

struct TempDir {
    fs::path path;
    TempDir() {
        std::random_device device;
        path = fs::temp_directory_path() /
               ("zub-scorestaves-test-" + std::to_string(device()) + "-" + std::to_string(device()));
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

TEST_CASE("staves lists the staves of the first score and skips the other contexts",
          "[score][staves]") {
    auto found = sc::staves(jolene);
    REQUIRE(found.size() == 1);
    CHECK(found[0].type == "TabStaff");
    CHECK(found[0].name.empty());
    CHECK(found[0].instrument.empty());
    CHECK(found[0].tuning.empty());
    const std::string text = jolene;
    CHECK(text.substr(found[0].begin, 15) == "\\new TabStaff \\");
    REQUIRE(found[0].insertAt != sc::noPosition);
    CHECK(text.substr(found[0].insertAt - 1, 2) == "{\n");
    CHECK(text.substr(found[0].withOpen, 1) == "{");
    CHECK(text[found[0].withClose] == '}');

    found = sc::staves(rozarka);
    REQUIRE(found.size() == 2);
    CHECK(found[0].tuning == "banjo-open-g-tuning");
    CHECK(found[0].instrument.empty()); // the commented line does not count
    CHECK(found[1].tuning.empty());
    CHECK(found[1].begin > found[0].end);

    CHECK(sc::staves("{ c4 }").empty());
    CHECK(sc::staves("\\score { { c4 } }").empty());
}

TEST_CASE("staves of a score whose staff plays a variable", "[score][staves]") {
    const auto found = sc::staves(whistle);
    REQUIRE(found.size() == 1); // the Dynamics is no staff
    CHECK(found[0].type == "Staff");
    const std::string text = whistle;
    REQUIRE(found[0].insertAt != sc::noPosition);
    // Inside the braces of `\new Staff { ... }`, before `\clef`.
    CHECK(text.substr(found[0].insertAt).starts_with("\n      \\clef treble"));
    CHECK(found[0].end < text.find("\\new Dynamics"));
}

TEST_CASE("staves reads names, file instruments and ignores comments, strings and Scheme",
          "[score][staves]") {
    const std::string text = R"(\score {
  <<
    % \new Staff { c4 }
    \markup { "\new Staff" }
    \new Staff = "lead" \with { midiInstrument = "flute" } { c4 }
    \new TabStaff = "tab" {
      \set Staff.midiInstrument = "banjo"
      \once \set Staff.midiInstrument = "gunshot"
      c4 { \set Staff.midiInstrument = "violin" } d
    }
    \new Staff { \once \set Staff.midiInstrument = "tuba" c4 }
    \new RhythmicStaff { c4 }
    \new Staff \relative c' <<
      { c4 }
    >>
    \new Staff \melody
    \new Staff \relative c' \melody
    #(display "\\new Staff")
  >>
}
)";
    const auto found = sc::staves(text);
    REQUIRE(found.size() == 6);
    CHECK(found[0].name == "lead");
    CHECK(found[0].instrument == "flute"); // from the \with
    CHECK(found[0].setBegin == sc::noPosition);
    CHECK(found[1].name == "tab");
    CHECK(found[1].instrument == "banjo"); // the first \set, not \once, not nested
    CHECK(text.substr(found[1].setBegin, found[1].setEnd - found[1].setBegin) == "banjo");
    CHECK(found[2].instrument.empty()); // \once does not set the staff's instrument
    CHECK(found[3].insertAt != sc::noPosition);
    CHECK(text.substr(found[3].insertAt - 2, 2) == "<<");
    CHECK(found[4].insertAt == sc::noPosition); // a bare variable
    CHECK(found[5].insertAt == sc::noPosition);
    CHECK(text.substr(found[5].begin, found[5].end - found[5].begin) ==
          "\\new Staff \\relative c' \\melody");
}

TEST_CASE("saveStaffInstrument adds a \\set line at the start of the staff's music",
          "[score][staves]") {
    const std::string text = rozarka;
    const auto change = sc::saveStaffInstrument(text, 0, "banjo");
    REQUIRE(change);
    const std::string expected = std::string(text).insert(
        text.find("    \\tabFullNotation\n    a4"),
        "    \\set Staff.midiInstrument = \"banjo\"\n");
    CHECK(change->text == expected);
    CHECK(change->text.substr(change->cursor, 1) == "\"");
    CHECK(change->text.substr(change->cursor - 5, 5) == "banjo");
    // It reads back as the staff's file instrument, and the second staff stays alone.
    const auto again = sc::staves(change->text);
    REQUIRE(again.size() == 2);
    CHECK(again[0].instrument == "banjo");
    CHECK(again[1].instrument.empty());

    // The second staff.
    const auto second = sc::saveStaffInstrument(text, 1, "acoustic guitar (nylon)");
    REQUIRE(second);
    CHECK(sc::staves(second->text)[1].instrument == "acoustic guitar (nylon)");
    CHECK(sc::staves(second->text)[0].instrument.empty());
}

TEST_CASE("saveStaffInstrument replaces an existing setting instead of adding a second",
          "[score][staves]") {
    const std::string text = "\\score { \\new Staff {\n  \\clef treble\n"
                             "  \\set Staff.midiInstrument = \"flute\"\n  c4 }\n}\n";
    const auto change = sc::saveStaffInstrument(text, 0, "whistle");
    REQUIRE(change);
    CHECK(change->text == "\\score { \\new Staff {\n  \\clef treble\n"
                          "  \\set Staff.midiInstrument = \"whistle\"\n  c4 }\n}\n");
    CHECK(change->text.substr(change->cursor - 7, 7) == "whistle");

    const std::string with = "\\score { \\new TabStaff \\with { midiInstrument = \"flute\" } { c4 } }";
    const auto fromWith = sc::saveStaffInstrument(with, 0, "banjo");
    REQUIRE(fromWith);
    CHECK(fromWith->text == "\\score { \\new TabStaff \\with { midiInstrument = \"banjo\" } { c4 } }");
}

TEST_CASE("saveStaffInstrument keeps line endings and puts a line inline when code follows",
          "[score][staves]") {
    std::string crlf;
    for (const char c : std::string(jolene))
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);
    const auto change = sc::saveStaffInstrument(crlf, 0, "banjo");
    REQUIRE(change);
    CHECK(contains(change->text, "{\r\n    \\set Staff.midiInstrument = \"banjo\"\r\n    \\tabFullNotation\r\n"));
    CHECK(count(change->text, "\r\n") == count(crlf, "\r\n") + 1);
    CHECK(count(change->text, "\n") == count(crlf, "\n") + 1); // no bare LF added

    // One-line braces of a tinwhistle-like staff.
    const auto inlineChange =
        sc::saveStaffInstrument("\\score { \\new Staff { \\melody } }", 0, "flute");
    REQUIRE(inlineChange);
    CHECK(inlineChange->text ==
          "\\score { \\new Staff { \\set Staff.midiInstrument = \"flute\" \\melody } }");

    // The brace's line indent plus two when the next line is blank.
    const auto blank = sc::saveStaffInstrument("\\score {\n    \\new Staff {\n\n c4 } }", 0, "flute");
    REQUIRE(blank);
    CHECK(contains(blank->text, "{\n      \\set Staff.midiInstrument = \"flute\"\n\n c4"));
}

TEST_CASE("saveStaffInstrument refuses what it cannot write", "[score][staves]") {
    CHECK_FALSE(sc::saveStaffInstrument(jolene, 1, "banjo")); // no such staff
    CHECK_FALSE(sc::saveStaffInstrument(jolene, 0, "kazoo")); // not a General MIDI name we know
    CHECK_FALSE(sc::saveStaffInstrument(jolene, 0, "ban\"jo"));
    CHECK_FALSE(sc::saveStaffInstrument("\\score { \\new Staff \\melody }", 0, "banjo")); // variable
    CHECK_FALSE(sc::saveStaffInstrument("{ c4 }", 0, "banjo"));
}

TEST_CASE("the playback score gives each staff its own instrument", "[score][staves][midi]") {
    sc::MidiOptions options;
    options.instrument = "acoustic guitar (steel)";
    options.staffInstruments = {"banjo", "piano"};
    const std::string played = withMidiFor(rozarka, options);
    CHECK(contains(played, "\\with {"));
    CHECK(contains(played, "midiInstrument = \"banjo\""));
    CHECK(contains(played, "midiInstrument = \"acoustic grand\""));
    CHECK(contains(played, "instrumentName = \"Banjo \"")); // the staff's own \with is kept
    CHECK(count(played, "\\remove \"Staff_performer\"") == 1); // only the ChordNames in \midi

    // Without a choice, a tab staff plays by its own tuning: banjo for the banjo staff, the
    // guitar for the other (not both banjo, as the whole score's tuning would say).
    options.staffInstruments = {"", "auto"};
    std::string plain = withMidiFor(rozarka, options);
    CHECK_FALSE(contains(plain, "acoustic grand"));
    CHECK(count(plain, "midiInstrument = \"banjo\"") == 1);
    CHECK(count(plain, "midiInstrument = \"acoustic guitar (steel)\"") == 3); // staff 2, \midi x2
    options.staffInstruments = {};
    plain = withMidiFor(rozarka, options);
    CHECK(count(plain, "midiInstrument = \"banjo\"") == 1);
    // A file's own instrument stays, and one staff alone is not changed.
    CHECK(count(withMidiFor(jolene, options), "midiInstrument") == 2);

    // A staff without a \with gets one; a file's \set of that staff is changed to the choice.
    options.staffInstruments = {"flute"};
    const std::string played2 = withMidiFor(whistle, options);
    CHECK(contains(played2, "\\new Staff \\with { midiInstrument = \"flute\" } {"));
    const std::string withSet =
        "\\score { \\new Staff {\n  \\set Staff.midiInstrument = \"tuba\"\n  c4 \\once \\set Staff.midiInstrument = \"x\" d }}";
    const std::string played3 = withMidiFor(withSet, options);
    CHECK(contains(played3, "\\set Staff.midiInstrument = \"flute\"\n  c4"));
    CHECK_FALSE(contains(played3, "tuba"));
    CHECK(contains(played3, "\\once \\set Staff.midiInstrument = \"x\"")); // later changes stay
    // The user's text is never changed.
    CHECK(withSet.find("tuba") != std::string::npos);
}

TEST_CASE("the playback score marks bends and keeps the choices", "[score][staves][midi]") {
    sc::MidiOptions options;
    options.instrument = "acoustic guitar (steel)";
    options.staffInstruments = {"banjo"};
    const std::string score =
        "\\score { \\new TabStaff { a'\\2\\^ \\grace h'\\2 c'4 } }";
    const std::string played = withMidiFor(score, options);
    CHECK(contains(played, "midiInstrument = \"banjo\""));
    CHECK(contains(played, "\\once \\set Staff.midiInstrument = \"gunshot\""));
}

TEST_CASE("sounding mutes the staves of the other group", "[score][staves][midi]") {
    sc::MidiOptions options;
    options.instrument = "acoustic guitar (steel)";
    options.staffInstruments = {"banjo", "amp"};
    options.sounding = sc::MidiOptions::Sounding::WithoutAmp;
    std::string played = withMidiFor(rozarka, options);
    CHECK(count(played, "\\remove \"Staff_performer\"") == 2); // ChordNames and the amp staff
    CHECK(contains(played, "midiInstrument = \"banjo\""));
    CHECK_FALSE(contains(played, "electric guitar (clean)\" \\remove"));

    options.sounding = sc::MidiOptions::Sounding::AmpOnly;
    played = withMidiFor(rozarka, options);
    CHECK(count(played, "\\remove \"Staff_performer\"") == 2);
    CHECK(contains(played, "midiInstrument = \"electric guitar (clean)\""));
    CHECK_FALSE(contains(played, "midiInstrument = \"banjo\""));
}

TEST_CASE("prepare plays amp staves in a book of their own", "[score][staves][midi]") {
    TempDir dir;
    sc::Request request;
    request.workDir = dir.path / "work";
    request.inputName = "Artist/Title.ly";
    request.content = rozarka;
    request.includeDir = "/songbook/zubuvzpevnik/lilypond";
    sc::MidiOptions midi;
    midi.instrument = "acoustic guitar (steel)";

    midi.staffInstruments = {"banjo", "amp"}; // mixed: two books
    request.midi = midi;
    auto cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(cmd->midi == dir.path / "work" / "out" / "zub-playback.midi");
    CHECK(cmd->ampMidi == dir.path / "work" / "out" / "zub-playback-amp.midi");
    CHECK_FALSE(cmd->midiIsAmp);
    std::stringstream copied;
    copied << std::ifstream(cmd->scoreCopy).rdbuf();
    CHECK(contains(copied.str(), "\\bookOutputName \"zub-playback-amp\""));
    CHECK(zub::safety::lilypondRisks(copied.str()) == zub::safety::lilypondRisks(rozarka));

    midi.staffInstruments = {"amp", "amp"}; // all amp: one book, run through the amp
    request.midi = midi;
    cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(cmd->ampMidi.empty());
    CHECK(cmd->midiIsAmp);

    midi.staffInstruments = {"banjo", "piano"}; // no amp
    request.midi = midi;
    cmd = sc::prepare(request);
    REQUIRE(cmd);
    CHECK(cmd->ampMidi.empty());
    CHECK_FALSE(cmd->midiIsAmp);
}

// Hidden: needs lilypond.
TEST_CASE("per-staff instruments reach the MIDI file", "[.integration][score][staves][midi]") {
#ifdef _WIN32
    SKIP("POSIX shell only");
#else
    if (std::system("command -v lilypond >/dev/null 2>&1") != 0)
        SKIP("lilypond is not installed");
    const fs::path realRepo{ZUB_REAL_SONGBOOK_DIR};
    if (!fs::is_directory(realRepo / "zubuvzpevnik" / "lilypond"))
        SKIP("no songbook checkout at " << realRepo);
    TempDir dir;
    // The programs of each track of the MIDI file, in order.
    const auto programs = [](const fs::path& midiFile) {
        std::ifstream in(midiFile, std::ios::binary);
        std::stringstream bytes;
        bytes << in.rdbuf();
        std::vector<std::set<int>> result;
        for (const auto& track : testmidi::parse(bytes.str())) {
            std::set<int> used;
            bool notes = false;
            for (const auto& e : track) {
                if (e.status == 0xc0)
                    used.insert(e.d1);
                notes = notes || e.noteOn();
            }
            if (notes)
                result.push_back(used);
        }
        return result;
    };
    const auto build = [&](const std::string& content, const std::vector<std::string>& choice) {
        sc::Request request;
        request.songbookRoot = realRepo;
        request.workDir = dir.path / "work";
        request.inputName = "Artist/Title.ly";
        request.content = content;
        request.germanNames = true;
        request.includeDir = realRepo / "zubuvzpevnik/lilypond";
        sc::MidiOptions midi;
        midi.instrument = "acoustic guitar (steel)";
        midi.staffInstruments = choice;
        request.midi = midi;
        const auto cmd = sc::prepare(request);
        REQUIRE(cmd);
        std::string line = "cd '" + cmd->workingDirectory.string() + "' && LC_ALL=C " + cmd->program;
        for (const auto& a : cmd->arguments)
            line += " '" + a + "'";
        line += " >'" + (dir.path / "log").string() + "' 2>&1";
        const int status = std::system(line.c_str());
        if (status != 0) {
            std::ifstream log(dir.path / "log");
            std::stringstream text;
            text << log.rdbuf();
            UNSCOPED_INFO(text.str());
        }
        REQUIRE(status == 0);
        return *cmd;
    };

    const std::string content = std::string("\\include \"zubuvzpevnik.ily\"\n") +
                                "\\score {\n<<\n"
                                "  \\new TabStaff \\with { instrumentName = \"Banjo \" } {\n"
                                "    \\set Staff.midiInstrument = \"flute\"\n"
                                "    a'4 b' \\once \\set Staff.midiInstrument = \"gunshot\" c'' d'' }\n"
                                "  \\new TabStaff { c4 d e f }\n"
                                ">>\n}\n";
    auto cmd = build(content, {"banjo", "piano"});
    auto found = programs(cmd.midi);
    REQUIRE(found.size() == 2);
    CHECK(found[0].contains(105)); // banjo, not the file's flute
    CHECK_FALSE(found[0].contains(73));
    CHECK(found[0].contains(127)); // the marker still sits in the first staff
    CHECK(found[1] == std::set<int>{0});

    // Mixed: banjo without the amp, the amp in its own file.
    cmd = build(content, {"banjo", "amp"});
    REQUIRE_FALSE(cmd.ampMidi.empty());
    found = programs(cmd.midi);
    REQUIRE(found.size() == 1);
    CHECK(found[0].contains(105));
    found = programs(cmd.ampMidi);
    REQUIRE(found.size() == 1);
    CHECK(found[0].contains(27));

    // Without a choice the file's own instrument plays.
    cmd = build(content, {});
    found = programs(cmd.midi);
    REQUIRE(found.size() == 2);
    CHECK(found[0].contains(73));
    CHECK(found[1].contains(25));

    // Every real score: its staves are found, and a choice for each still builds a MIDI file.
    std::map<std::string, bool> germanOf; // the songs' lang decides the note names
    for (const auto& entry : fs::recursive_directory_iterator(realRepo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".tex")
            continue;
        const auto text = zub::readTextFile(entry.path());
        REQUIRE(text);
        if (const auto meta = zub::parseSong(*text))
            for (const auto& ref : zub::songbook::referencedFiles(*text))
                germanOf[ref] = sc::germanNames(meta->value("lang"));
    }
    int scores = 0;
    for (const auto& entry : fs::recursive_directory_iterator(realRepo / "songs")) {
        if (!entry.is_regular_file() || entry.path().extension() != ".ly")
            continue;
        const auto text = zub::readTextFile(entry.path());
        REQUIRE(text);
        const auto all = sc::staves(*text);
        if (all.empty()) {
            UNSCOPED_INFO(entry.path().string() << ": no staves found");
        }
        CHECK(!all.empty());
        std::vector<std::string> choice;
        for (std::size_t i = 0; i < all.size(); ++i)
            choice.push_back(i % 2 == 0 ? "banjo" : "piano");
        sc::Request request;
        request.songbookRoot = realRepo;
        request.workDir = dir.path / "work";
        request.inputName =
            fs::relative(entry.path(), entry.path().parent_path().parent_path()).generic_string();
        request.content = *text;
        request.germanNames =
            germanOf.contains(request.inputName) ? germanOf[request.inputName] : true;
        request.includeDir = realRepo / "zubuvzpevnik/lilypond";
        sc::MidiOptions midi;
        midi.instrument = sc::midiInstrument(*text);
        midi.staffInstruments = choice;
        request.midi = midi;
        const auto prepared = sc::prepare(request);
        REQUIRE(prepared);
        std::string line =
            "cd '" + prepared->workingDirectory.string() + "' && LC_ALL=C " + prepared->program;
        for (const auto& a : prepared->arguments)
            line += " '" + a + "'";
        line += " >'" + (dir.path / "log").string() + "' 2>&1";
        const bool ok = std::system(line.c_str()) == 0 && fs::exists(prepared->midi) &&
                        fs::file_size(prepared->midi) > 0;
        if (!ok) {
            std::ifstream log(dir.path / "log");
            std::stringstream logText;
            logText << log.rdbuf();
            UNSCOPED_INFO(request.inputName << ": " << logText.str());
        }
        CHECK(ok);
        ++scores;
    }
    CHECK(scores > 0);
#endif
}
