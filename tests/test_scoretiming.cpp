// The score cursor as the play position: the note list of the playback score, the tempo map and
// the timeline. tests/data/timing/ is a real LilyPond run (see tests/data/README.md).
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <zub/core/midi.hpp>
#include <zub/core/score.hpp>
#include <zub/core/scoreregion.hpp>
#include <zub/core/scoretiming.hpp>

namespace fs = std::filesystem;
using Catch::Approx;
namespace timing = zub::scoretiming;

namespace {

const fs::path dataDir = fs::path(ZUB_TEST_DATA_DIR) / "timing";

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string replaceAll(std::string text, const std::string& from, const std::string& to) {
    for (std::size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
        text.replace(at, from.size(), to);
    return text;
}

// A Standard MIDI file (format 1, 96 ticks per quarter) with the given tempo changes in track 0.
std::string tempoFile(const std::vector<std::pair<long, unsigned>>& changes) {
    std::string track;
    long last = 0;
    for (const auto& [tick, micros] : changes) {
        track += static_cast<char>(tick - last); // < 128 here
        track += "\xff\x51\x03";
        track += static_cast<char>((micros >> 16) & 0xff);
        track += static_cast<char>((micros >> 8) & 0xff);
        track += static_cast<char>(micros & 0xff);
        last = tick;
    }
    track += std::string("\x00\xff\x2f\x00", 4);
    std::string file("MThd\0\0\0\6\0\1\0\1\0\x60", 14);
    file += "MTrk";
    for (int shift = 24; shift >= 0; shift -= 8)
        file += static_cast<char>((track.size() >> shift) & 0xff);
    return file + track;
}

bool sameTimes(const std::vector<double>& got, const std::vector<double>& expected) {
    if (got.size() != expected.size())
        return false;
    for (std::size_t i = 0; i < got.size(); ++i)
        if (got[i] != Approx(expected[i]))
            return false;
    return true;
}

struct Fixture {
    std::string original = readFile(dataDir / "Timing.ly");
    std::string copy = readFile(dataDir / "Timing.copy.ly");
    std::string notes;
    std::string midi = readFile(dataDir / "timing.mid");
    fs::path copyFile = "/work/score/Test/Timing.ly";
    Fixture() {
        notes = replaceAll(readFile(dataDir / "timing.notes"), "@COPY@", copyFile.string());
    }
    timing::Source source() const { return {notes, midi, copy, original, copyFile, 17}; }
    // The first cue's event text of the event beginning with `needle` (searched from `from`).
    std::size_t at(std::string_view needle, std::size_t from = 0) const {
        const auto found = original.find(needle, from);
        REQUIRE(found != std::string::npos);
        return found;
    }
    std::string textOf(const zub::scoreregion::Range& r) const {
        return original.substr(r.begin, r.end - r.begin);
    }
};

} // namespace

TEST_CASE("Note list: lines parse, damaged ones are skipped", "[scoretiming]") {
    const auto notes = timing::parseNotes("0 1 0 1 2 10 /a b/X.ly\n"
                                          "19 8 -1 8 27 85 /a b/X.ly\r\n"
                                          "garbage\n"
                                          "1 0 0 1 3 4 /bad/denominator.ly\n"
                                          "1 4 0 1 3 4\n" // no file
                                          "3 4 0 1 5 6 /x.ly");
    REQUIRE(notes.size() == 3);
    CHECK(notes[0].whole == Approx(0));
    CHECK(notes[0].line == 2);
    CHECK(notes[0].column == 10);
    CHECK(notes[0].file == "/a b/X.ly");                 // the file is the rest of the line
    CHECK(notes[1].whole == Approx(19.0 / 8 - 1.0 / 8)); // a grace note sounds before its moment
    CHECK(notes[1].file == "/a b/X.ly");
    CHECK(notes[2].whole == Approx(0.75));
    CHECK(timing::parseNotes("").empty());
}

TEST_CASE("Note list: kind and length", "[scoretiming]") {
    const auto notes = timing::parseNotes("n 1 4 0 1 7 19 1 4 /a b/X.ly\n"
                                          "l 19 8 -1 8 27 85 1 2 /x.ly\n"
                                          "c 0 1 0 1 6 33 3 8 /x.ly\n"
                                          "m 0 1 0 1 9 27 1 8 /x.ly\n"
                                          "x 0 1 0 1 9 27 1 8 /x.ly\n" // unknown kind
                                          "n 0 1 0 1 9 27 1 0 /x.ly\n" // a zero denominator
                                          "n 0 1 0 1 9 27 /x.ly\n"     // no length
                                          "0 1 0 1 2 10 /old.ly\n");   // the old format
    REQUIRE(notes.size() == 5);
    CHECK(notes[0].kind == timing::Kind::Note);
    CHECK(notes[0].whole == Approx(0.25));
    CHECK(notes[0].length == Approx(0.25));
    CHECK(notes[0].file == "/a b/X.ly");
    CHECK(notes[1].kind == timing::Kind::Lyric);
    CHECK(notes[1].whole == Approx(19.0 / 8 - 1.0 / 8));
    CHECK(notes[1].length == Approx(0.5));
    CHECK(notes[2].kind == timing::Kind::ChordName);
    CHECK(notes[2].length == Approx(0.375));
    CHECK(notes[3].kind == timing::Kind::Melody);
    CHECK(notes[4].kind == timing::Kind::Note); // the old format: no length
    CHECK(notes[4].length == 0);
    CHECK(notes[4].file == "/old.ly");
}

TEST_CASE("Tempo map: whole notes to seconds across tempo changes", "[scoretiming][midi]") {
    // 96 ticks per quarter: 500000 us (120) from the start, 1000000 us (60) from tick 96
    // (the second quarter note), 250000 us (240) from tick 192.
    const auto map = zub::midi::tempoMap(tempoFile({{0, 500000}, {96, 1000000}, {120, 250000}}));
    REQUIRE(map);
    CHECK(map->division == 96);
    CHECK(map->seconds(0) == Approx(0));
    CHECK(map->seconds(-1) == Approx(0));
    CHECK(map->seconds(0.25) == Approx(0.5));     // the first quarter at 120: 96 ticks
    CHECK(map->seconds(0.3125) == Approx(0.75));  // + 24 ticks at 60: a quarter takes 1 s
    CHECK(map->seconds(0.375) == Approx(0.8125)); // 144 ticks: + 24 ticks at 240 (0.25 s a quarter)
    CHECK(map->seconds(0.5) == Approx(0.9375));   // 192 ticks: + 72 more at 240
    CHECK(map->seconds(1.0) == Approx(0.9375 + 192.0 / 96 * 0.25));
}

TEST_CASE("Tempo map: no tempo event means 120, bad files are refused", "[scoretiming][midi]") {
    const auto map = zub::midi::tempoMap(tempoFile({}));
    REQUIRE(map);
    CHECK(map->seconds(1.0) == Approx(2.0)); // a whole note is four beats of 0.5 s
    CHECK(!zub::midi::tempoMap("not midi"));
    std::string smpte = tempoFile({});
    smpte[12] = static_cast<char>(0xe7); // SMPTE time division
    CHECK(!zub::midi::tempoMap(smpte));
}

TEST_CASE("Tempo map of the real playback MIDI file", "[scoretiming][midi]") {
    Fixture f;
    const auto map = zub::midi::tempoMap(f.midi);
    REQUIRE(map);
    CHECK(map->division == 384);
    // \tempo 4 = 120 from the start, 4 = 60 from the moment 15/8 (after the b'8 of the second
    // pass): 15/8 whole notes are 3.75 s, the eighth note d' after the repeat starts at 4.25 s.
    CHECK(map->seconds(15.0 / 8) == Approx(3.75));
    CHECK(map->seconds(2.0) == Approx(3.75 + 0.5));
}

TEST_CASE("Timeline: events of the text with the seconds they sound", "[scoretiming]") {
    Fixture f;
    const auto result = timing::buildTimeline(f.source());
    REQUIRE(result);
    const auto& t = *result;
    const auto times = [&](std::size_t begin) {
        std::vector<double> found;
        for (const auto& cue : t.cues)
            if (cue.event.begin == begin)
                found.push_back(cue.seconds);
        return found;
    };
    // The music of the score begins at the staff's chord; the notes of the variable \intro are
    // events of their definition (the use of the variable is none) and sound at 0 and 0.5 s.
    CHECK(t.firstBegin == f.at("<e e'>4"));
    CHECK(sameTimes(times(f.at("c'4")), {0.0}));
    CHECK(sameTimes(times(f.at("d' }")), {0.5}));

    // A note is written once but sounds in both passes of the repeat (the first pass first).
    // The staff's chord is at half a whole note, 120 bpm: 1.0 s; its second pass at 5/4: 2.5 s.
    CHECK(sameTimes(times(f.at("<e e'>4")), {1.0, 2.5}));
    CHECK(sameTimes(times(f.at("<e,\\6 e\\4>4")), {0.0, 1.5}));
    // An alternative sounds once; after the tempo change (4 = 60 from 15/8) a whole note is 4 s.
    CHECK(sameTimes(times(f.at("b'8 \\tempo")), {3.5}));
    CHECK(sameTimes(times(f.at("d'8 ")), {4.25}));
    // A tuplet's note, a grace note (before its moment) and the note after it.
    CHECK(sameTimes(times(f.at("e'8 f'")), {3.75 + 0.25 * 4}));
    CHECK(sameTimes(times(f.at("\\grace a'8")), {3.75 + 0.375 * 4}));
    CHECK(sameTimes(times(f.at("b'4 r4")), {3.75 + 0.5 * 4}));
    // A rest has no cue.
    CHECK(times(f.at("r4")).empty());
    // Sorted by time.
    for (std::size_t i = 1; i < t.cues.size(); ++i)
        CHECK(t.cues[i - 1].seconds <= t.cues[i].seconds);
}

TEST_CASE("Timeline: where playback starts for a caret", "[scoretiming]") {
    Fixture f;
    const auto result = timing::buildTimeline(f.source());
    REQUIRE(result);
    const auto& t = *result;
    // Before the first event, or no caret at all: from the start.
    CHECK(timing::startSeconds(t, 0) == Approx(0.0));
    CHECK(timing::startSeconds(t, f.at("\\intro")) == Approx(0.0));
    CHECK(timing::startSeconds(t, f.at("<e e'>4")) == Approx(0.0));
    // In the definition of the variable: the note's first pass.
    CHECK(timing::startSeconds(t, f.at("d' }")) == Approx(0.5));
    // In the chord, and between its notes: that chord, its first pass.
    const auto chord = f.at("<e e'>4");
    CHECK(timing::startSeconds(t, chord + 1) == Approx(1.0));
    CHECK(timing::startSeconds(t, chord + 4) == Approx(1.0));
    // Right behind the chord: the next event; g' sounds at 1.5 s in the first pass, 3.0 s in the
    // second.
    CHECK(timing::startSeconds(t, chord + 7) == Approx(1.5));
    // In the second ending only.
    CHECK(timing::startSeconds(t, f.at("b'8 \\tempo")) == Approx(3.5));
    // A rest does not sound: the next event that does (here the tab staff's, which is next in the
    // text).
    CHECK(timing::startSeconds(t, f.at("r4")) == Approx(0.0));
    CHECK(!timing::startSeconds(t, f.original.size()));
    CHECK(!timing::startSeconds(timing::Timeline{}, 5));
}

TEST_CASE("Timeline: the cue playing at a time", "[scoretiming]") {
    Fixture f;
    const auto result = timing::buildTimeline(f.source());
    REQUIRE(result);
    const auto& t = *result;
    CHECK(timing::cueAt(t, -0.5) == nullptr);
    REQUIRE(timing::cueAt(t, 0.0));
    CHECK(timing::cueAt(t, 0.0)->seconds == Approx(0.0));
    // 1.5 s: g' of the staff and the tab staff's chord (second pass) begin together: the later one
    // in the text.
    REQUIRE(timing::cueAt(t, 1.6));
    CHECK(timing::cueAt(t, 1.6)->seconds == Approx(1.5));
    CHECK(f.textOf(timing::cueAt(t, 1.6)->event) == "<e,\\6 e\\4>4");
    CHECK(f.textOf(timing::cueAt(t, 3.6)->event) == "b'8");
    CHECK(f.textOf(timing::cueAt(t, 5.3)->event) == "\\grace a'8");
    CHECK(f.textOf(timing::cueAt(t, 99)->event) == "b'4");
}

namespace {

// tests/data/timing/Sing.ly: a chord name, a staff, a tab staff, a silent melody (Devnull) timing
// the lyrics, at 120 bpm (a whole note is 2 s). The real LilyPond run of it, the first score is
// shorter than the playback score that is appended from line 13.
struct Sing {
    std::string original = readFile(dataDir / "Sing.ly");
    std::string copy = readFile(dataDir / "Sing.copy.ly");
    std::string notes;
    std::string midi = readFile(dataDir / "sing.mid");
    fs::path copyFile = "/work/score/Test/Sing.ly";
    Sing() { notes = replaceAll(readFile(dataDir / "sing.notes"), "@COPY@", copyFile.string()); }
    timing::Source source() const { return {notes, midi, copy, original, copyFile, 13}; }
    std::size_t at(std::string_view needle, std::size_t from = 0) const {
        const auto found = original.find(needle, from);
        REQUIRE(found != std::string::npos);
        return found;
    }
    // The texts of what sounds at `seconds`, in text order.
    std::vector<std::string> soundingTexts(const timing::Timeline& t, double seconds) const {
        std::vector<std::string> texts;
        for (const auto& sound : timing::soundingAt(t, seconds))
            texts.push_back(
                original.substr(sound.range.begin, sound.range.end - sound.range.begin));
        std::ranges::sort(texts);
        return texts;
    }
};

} // namespace

TEST_CASE("Timeline: everything that sounds at a time, in all parts", "[scoretiming]") {
    Sing f;
    const auto result = timing::buildTimeline(f.source());
    REQUIRE(result);
    const auto& t = *result;
    using V = std::vector<std::string>;
    // The first syllable has the length of the first melody note (a quarter, 0.5 s): the staff's
    // c', the tab staff's chord (two notes: one event), the chord name c2 (a half note, 1 s) and
    // the syllable "la".
    CHECK(f.soundingTexts(t, 0.0) == V{"<e, e>2", "c'4", "c2", "la"});
    CHECK(f.soundingTexts(t, 0.49) == V{"<e, e>2", "c'4", "c2", "la"});
    // 0.5 s: d' and the second "la" (which the hyphen follows: only the word is the syllable).
    CHECK(f.soundingTexts(t, 0.5) == V{"<e, e>2", "c2", "d'", "la"});
    CHECK(f.soundingTexts(t, 0.8) == V{"<e, e>2", "c2", "d'", "la"});
    // 1.0 s: the second half: g2:m7 (the whole of the chord's text), g', e'2, "lo".
    CHECK(f.soundingTexts(t, 1.0) == V{"e'2", "g'2", "g2:m7", "lo"});
    CHECK(f.soundingTexts(t, 1.99) == V{"e'2", "g'2", "g2:m7", "lo"});
    // Before and after the music: nothing.
    CHECK(timing::soundingAt(t, -0.1).empty());
    CHECK(timing::soundingAt(t, 2.0).empty());
    // The silent melody is no sound and no cue; the cues are the staves' notes only.
    for (const auto& cue : t.cues)
        CHECK(f.original.substr(cue.event.begin, 3) != "c''");
    for (const auto& sound : t.sounds)
        CHECK(sound.kind != timing::Kind::Melody);
    CHECK(t.unmapped == 0);
    // Each sound belongs to its kind.
    for (const auto& sound : timing::soundingAt(t, 1.0)) {
        const auto text = f.original.substr(sound.range.begin, sound.range.end - sound.range.begin);
        if (text == "lo")
            CHECK(sound.kind == timing::Kind::Lyric);
        if (text == "g2:m7")
            CHECK(sound.kind == timing::Kind::ChordName);
        if (text == "e'2")
            CHECK(sound.kind == timing::Kind::Note);
    }
    // The selected-note cue is unchanged: the later note of two that begin together (text order).
    CHECK(f.original.substr(timing::cueAt(t, 0.2)->event.begin, 7) == "<e, e>2");
}

TEST_CASE("Timeline: sounds of the old note list, grace notes and words that do not map",
          "[scoretiming]") {
    // An old list (no kind, no length): a note sounds for the minimum.
    Fixture old;
    const auto base = timing::buildTimeline(old.source());
    REQUIRE(base);
    const auto at = timing::soundingAt(*base, 0.0);
    REQUIRE(!at.empty());
    for (const auto& s : at)
        CHECK(s.end - s.start == Approx(timing::minimumSounding));
    CHECK(timing::soundingAt(*base, 0.2).empty());
    CHECK(base->sounds.size() == base->cues.size());

    // A lyric with a word the generated copy has more often than the block (a changed copy) is
    // skipped, not mapped to the wrong word.
    Sing changed;
    const auto lyrics = changed.copy.find("la la -- la lo", changed.copy.find("\\book"));
    REQUIRE(lyrics != std::string::npos);
    changed.copy.insert(lyrics + 14, " la");
    const auto result = timing::buildTimeline(changed.source());
    REQUIRE(result);
    using V = std::vector<std::string>;
    CHECK(changed.soundingTexts(*result, 0.0) == V{"<e, e>2", "c'4", "c2"}); // no "la"
    CHECK(changed.soundingTexts(*result, 1.0) == V{"e'2", "g'2", "g2:m7", "lo"});
}

TEST_CASE("Timeline: other files, CR LF copies and files that do not belong together",
          "[scoretiming]") {
    Fixture f;
    const auto base = timing::buildTimeline(f.source());
    REQUIRE(base);

    // The editor's text has LF; the copy of a CR LF file has CR LF.
    Fixture crlf;
    crlf.copy = replaceAll(crlf.copy, "\n", "\r\n");
    const auto same = timing::buildTimeline(crlf.source());
    REQUIRE(same);
    CHECK(same->cues.size() == base->cues.size());
    CHECK(same->cues.front().event == base->cues.front().event);

    // Notes of another file (an \include) are not ours.
    Fixture other;
    other.notes += "0 1 0 1 7 4 /other/Included.ly\n";
    CHECK(timing::buildTimeline(other.source())->cues.size() == base->cues.size());

    // Without an appended playback score only the notes written in the file count.
    Fixture plain;
    plain.copy = plain.original;
    auto source = plain.source();
    source.generatedFromLine = 0;
    const auto without = timing::buildTimeline(source);
    REQUIRE(without);
    CHECK(without->cues.size() == 2); // the notes of the variable's definition, once: 0 and 0.5 s

    // The copy was made from another text: no timeline.
    Fixture edited;
    edited.original += "% edited\n";
    CHECK(!timing::buildTimeline(edited.source()));
    // The playback score's notes do not pair with the score's.
    Fixture unpaired;
    unpaired.copy = replaceAll(unpaired.copy, "\\grace a'8 b'4", "b'4");
    CHECK(!timing::buildTimeline(unpaired.source()));
    // A MIDI file that is none.
    Fixture bad;
    bad.midi = "x";
    CHECK(!timing::buildTimeline(bad.source()));
}

TEST_CASE("prepare: the playback score lists its notes", "[scoretiming][score]") {
    zub::score::Request request;
    const auto work = fs::temp_directory_path() / "zub-test-scoretiming";
    fs::remove_all(work);
    request.workDir = work;
    request.songbookRoot = work;
    request.includeDir = work;
    request.inputName = "Test/X.ly";
    request.content = "\\score { \\new Staff { c'4 } }\n";
    request.midi = zub::score::MidiOptions{};
    const auto command = zub::score::prepare(request);
    REQUIRE(command);
    CHECK(command->notes == work / "out" / "zub-playback.notes");
    std::ifstream copy(command->scoreCopy);
    std::stringstream ss;
    ss << copy.rdbuf();
    // The playback score's \layout only runs the logger; no Scheme is written into the copy.
    CHECK(ss.str().find("\\consists \\zubNoteLogger") != std::string::npos);
    CHECK(ss.str().find("#(") == std::string::npos);
    std::ifstream wrapper(work / "score.ly");
    std::stringstream ws;
    ws << wrapper.rdbuf();
    CHECK(ws.str().find("\\include \"zub-playback.ily\"") != std::string::npos);
    std::ifstream logger(work / "zub-playback.ily");
    std::stringstream ls;
    ls << logger.rdbuf();
    CHECK(ls.str().find("zubNoteLogger = #(lambda") == 0);
    CHECK(ls.str().find((work / "out" / "zub-playback.notes").string()) != std::string::npos);

    // Without MIDI there is neither a logger nor a list.
    request.midi.reset();
    const auto plain = zub::score::prepare(request);
    REQUIRE(plain);
    CHECK(plain->notes.empty());
    std::ifstream plainWrapper(work / "score.ly");
    std::stringstream pw;
    pw << plainWrapper.rdbuf();
    CHECK(pw.str().find("zub-playback") == std::string::npos);
    fs::remove_all(work);
}
