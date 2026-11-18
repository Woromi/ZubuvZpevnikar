#include "midi_events.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <zub/core/midi.hpp>

namespace fs = std::filesystem;
using testmidi::Raw;

namespace {

std::vector<testmidi::Ev> trackOf(const std::string& midi) {
    auto tracks = testmidi::parse(midi);
    REQUIRE(tracks.size() == 1);
    return tracks[0];
}

std::string smooth(const std::string& midi) {
    const auto out = zub::midi::smoothBends(midi);
    REQUIRE(out);
    return *out;
}

double semis(const testmidi::Ev& e) {
    return (e.d1 + e.d2 * 128 - 8192) / 8192.0 * 12.0;
}

int count(const std::vector<testmidi::Ev>& t, int status) {
    int n = 0;
    for (const auto& e : t)
        n += e.status == status ? 1 : 0;
    return n;
}

int noteOns(const std::vector<testmidi::Ev>& t) {
    int n = 0;
    for (const auto& e : t)
        n += e.noteOn() ? 1 : 0;
    return n;
}

// a' 0-340, the grace note 340-383 (marked), a' 384-768: "a'\^ \grace h'  a'"
std::vector<Raw> bendScore(int marker) {
    return {{0, {0xc0, 25}},       {0, {0x90, 60, 90}},   {340, {0x90, 60, 0}},
            {340, {0xc0, marker}}, {340, {0x90, 62, 90}}, {383, {0x90, 62, 0}},
            {384, {0xc0, 25}},     {384, {0x90, 60, 90}}, {768, {0x90, 60, 0}}};
}

} // namespace

TEST_CASE("smoothBends leaves a file without markers byte for byte", "[midi][bend]") {
    const std::string midi =
        testmidi::build({{0, {0xc0, 25}}, {0, {0x90, 60, 90}}, {96, {0x90, 60, 0}}});
    CHECK(smooth(midi) == midi);
}

TEST_CASE("smoothBends turns a bend into one note that glides up", "[midi][bend]") {
    const auto t = trackOf(smooth(testmidi::build(bendScore(zub::midi::markerBend))));
    CHECK(noteOns(t) == 2);     // the grace note is gone
    CHECK(count(t, 0xc0) == 2); // and its marker: only the score's own program changes stay

    // The RPN setup of the pitch bend range comes first.
    REQUIRE(t.size() > 6);
    CHECK(t[0].status == 0xb0);
    CHECK(t[0].d1 == 101);
    CHECK(t[2].d1 == 6);
    CHECK(t[2].d2 == 12);

    // The first note sounds from 0 to 383 (the end of the grace note), bent from 0 to +2.
    const testmidi::Ev* on = nullptr;
    const testmidi::Ev* off = nullptr;
    double highest = 0;
    long lastBend = -1;
    for (const auto& e : t) {
        if (e.noteOn() && !on)
            on = &e;
        if (e.noteOff() && !off)
            off = &e;
        if (e.status == 0xe0 && e.tick < 384) {
            CHECK(!off);                       // all of them while the note sounds
            CHECK(semis(e) >= highest - 1e-9); // only upwards
            highest = semis(e);
            lastBend = e.tick;
        }
    }
    REQUIRE(on);
    REQUIRE(off);
    CHECK(off->tick == 383);
    CHECK(off->d1 == 60);
    CHECK(highest == Catch::Approx(2.0).margin(0.01));
    CHECK(lastBend > 100);

    // Before the next note the bend is back at the centre.
    const testmidi::Ev* second = nullptr;
    for (const auto& e : t)
        if (e.noteOn() && e.tick == 384)
            second = &e;
    REQUIRE(second);
    const testmidi::Ev* before = nullptr;
    for (const auto& e : t) {
        if (&e == second)
            break;
        if (e.status == 0xe0)
            before = &e;
    }
    REQUIRE(before);
    CHECK(before->tick == 384);
    CHECK(semis(*before) == Catch::Approx(0.0).margin(0.01));
}

TEST_CASE("smoothBends starts a pre-bend at the target pitch", "[midi][bend]") {
    const auto t = trackOf(smooth(testmidi::build(bendScore(zub::midi::markerPreBend))));
    CHECK(noteOns(t) == 2);
    const testmidi::Ev* firstBend = nullptr;
    for (const auto& e : t) {
        if (e.status == 0xe0 && !firstBend) {
            firstBend = &e;
            continue;
        }
        if (e.noteOn()) {
            REQUIRE(firstBend); // the bend comes before the note
            CHECK(firstBend->tick == 0);
            CHECK(semis(*firstBend) == Catch::Approx(2.0).margin(0.01));
            break;
        }
    }
    int bendsInFirstNote = 0;
    for (const auto& e : t)
        bendsInFirstNote += e.status == 0xe0 && e.tick > 0 && e.tick < 384 ? 1 : 0;
    CHECK(bendsInFirstNote == 0); // it stays there
}

TEST_CASE("smoothBends glides back to the note after a release", "[midi][bend]") {
    auto events = bendScore(zub::midi::markerBendRelease);
    // The note after the grace note is a half tone lower: 60 -> 59 would be the release target.
    events[7] = {384, {0x90, 58, 90}};
    events[8] = {768, {0x90, 58, 0}};
    const auto t = trackOf(smooth(testmidi::build(events)));
    CHECK(noteOns(t) == 1); // one note for the three
    double last = 99;
    long off = -1;
    for (const auto& e : t) {
        if (e.status == 0xe0)
            last = semis(e);
        if (e.noteOff())
            off = e.tick;
    }
    CHECK(off == 768);
    CHECK(last == Catch::Approx(-2.0).margin(0.01)); // 58 is two semitones below 60
}

TEST_CASE("smoothBends makes a slide one gliding note", "[midi][bend]") {
    const auto t = trackOf(smooth(testmidi::build({{0, {0x90, 50, 90}},
                                                   {384, {0x90, 50, 0}},
                                                   {384, {0xc0, zub::midi::markerSlide}},
                                                   {384, {0x90, 55, 90}},
                                                   {768, {0x90, 55, 0}}})));
    CHECK(noteOns(t) == 1);
    double last = 0;
    for (const auto& e : t) {
        if (e.status == 0xe0)
            last = semis(e);
        if (e.noteOff()) {
            CHECK(e.tick == 768);
            CHECK(e.d1 == 50);
        }
    }
    CHECK(last == Catch::Approx(5.0).margin(0.01));
}

TEST_CASE("smoothBends leaves marked chords and wide jumps alone", "[midi][bend]") {
    // A chord at the marked moment: no single target note.
    auto t = trackOf(smooth(testmidi::build({{0, {0x90, 50, 90}},
                                             {384, {0x90, 50, 0}},
                                             {384, {0xc0, zub::midi::markerSlide}},
                                             {384, {0x90, 55, 90}},
                                             {384, {0x90, 59, 90}},
                                             {768, {0x90, 55, 0}},
                                             {768, {0x90, 59, 0}}})));
    CHECK(noteOns(t) == 3);
    CHECK(count(t, 0xe0) == 0);
    CHECK(count(t, 0xc0) == 0); // the marker is gone anyway
    // 13 semitones are more than the bend range.
    t = trackOf(smooth(testmidi::build({{0, {0x90, 40, 90}},
                                        {384, {0x90, 40, 0}},
                                        {384, {0xc0, zub::midi::markerSlide}},
                                        {384, {0x90, 53, 90}},
                                        {768, {0x90, 53, 0}}})));
    CHECK(noteOns(t) == 2);
    CHECK(count(t, 0xe0) == 0);
    // A marked note nothing ends before.
    t = trackOf(smooth(testmidi::build({{0, {0x90, 40, 90}},
                                        {100, {0x90, 40, 0}},
                                        {384, {0xc0, zub::midi::markerSlide}},
                                        {384, {0x90, 41, 90}},
                                        {768, {0x90, 41, 0}}})));
    CHECK(noteOns(t) == 2);
    CHECK(count(t, 0xe0) == 0);
}

TEST_CASE("smoothBends reads running status and keeps meta, sysex and the other channels",
          "[midi][bend]") {
    const std::string midi = testmidi::build({{0, {0xff, 0x51, 3, 0x09, 0x27, 0xc0}},
                                              {0, {0xf0, 2, 0x7d, 0xf7}},
                                              {0, {0x91, 40, 90}},
                                              {0, {0x91, 44, 90}},
                                              {0, {0x90, 50, 90}},
                                              {384, {0x90, 50, 0}},
                                              {384, {0xc0, zub::midi::markerSlide}},
                                              {384, {0x90, 52, 90}},
                                              {768, {0x90, 52, 0}},
                                              {768, {0x91, 40, 0}},
                                              {768, {0x91, 44, 0}}},
                                             true);
    const auto t = trackOf(smooth(midi));
    int meta = 0;
    int sysex = 0;
    for (const auto& e : t) {
        meta += e.status == 0xff && e.d1 == 0x51 ? 1 : 0;
        sysex += e.status == 0xf0 ? 1 : 0;
        if (e.status == 0xe0)
            CHECK(e.channel == 0); // only the bent channel
    }
    CHECK(meta == 1);
    CHECK(sysex == 1);
    CHECK(noteOns(t) == 3); // the slide's target is merged, channel 1 is untouched
    CHECK(count(t, 0xe0) > 2);
}

TEST_CASE("smoothBends refuses what is no readable MIDI file", "[midi][bend]") {
    CHECK(!zub::midi::smoothBends(""));
    CHECK(!zub::midi::smoothBends("MThd\0\0\0\6\0\0\0\1\x01\x80"));
    CHECK(!zub::midi::smoothBends("RIFF0000WAVE0000000000"));
    std::string smpte = testmidi::build({{0, {0x90, 60, 90}}});
    smpte[12] = static_cast<char>(0xe7); // SMPTE time division
    CHECK(!zub::midi::smoothBends(smpte));
    std::string cut = testmidi::build({{0, {0x90, 60, 90}}, {10, {0x90, 60, 0}}});
    cut.resize(cut.size() - 2);
    CHECK(!zub::midi::smoothBends(cut));
    // A data byte with no status in front.
    std::string noStatus = testmidi::build({{0, {0x90, 60, 90}}});
    noStatus[23] = 0x3c; // the first event's status byte
    CHECK(!zub::midi::smoothBends(noStatus));
}

TEST_CASE("smoothBends smooths what LilyPond writes for bends, pre-bends and slides",
          "[midi][bend]") {
    // tests/data/midi/BendExamples.ly through zub::score::prepare (the playback score marks the
    // notes), built by LilyPond 2.24.3.
    std::ifstream in(fs::path(ZUB_TEST_DATA_DIR) / "midi/bend-examples.mid", std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string raw = buffer.str();
    REQUIRE(!raw.empty());

    const auto before = testmidi::parse(raw);
    const auto after = testmidi::parse(smooth(raw));
    REQUIRE(before.size() == 2);
    REQUIRE(after.size() == 2);
    CHECK(after[0].size() == before[0].size()); // the first track has no notes: untouched
    int markers = 0;
    for (const auto& e : before[1])
        markers += e.status == 0xc0 && e.d1 >= 122 && e.d1 != 25 ? 1 : 0;
    CHECK(markers == 5);
    for (const auto& e : after[1]) {
        if (e.status == 0xc0)
            CHECK(e.d1 == 25);
        CHECK(e.text != "gunshot");
    }
    // Bend, pre-bend and release (3 notes), pre-bend... : 4 merged notes less 1 slide merged;
    // the chord slide at the end stays as written.
    CHECK(noteOns(after[1]) < noteOns(before[1]));
    CHECK(count(after[1], 0xe0) > 20);
    // Every bend is undone before the end: the last bend of the channel is not above the
    // centre except at a note that is still sounding.
    CHECK(smooth(smooth(raw)) == smooth(raw)); // no marker left: a second pass changes nothing
}

TEST_CASE("smoothBends folds a note above MIDI's range down by octaves", "[midi][bend]") {
    // LilyPond writes key 139 (b and beyond) as an out-of-range byte.
    std::string midi = testmidi::build({{0, {0x90, 61, 90}}, {96, {0x90, 61, 0}}});
    for (std::size_t i = 0; i + 1 < midi.size(); ++i)
        if (static_cast<unsigned char>(midi[i]) == 0x90 && midi[i + 1] == 61)
            midi[i + 1] = static_cast<char>(139);
    const auto track = trackOf(smooth(midi));
    REQUIRE(noteOns(track) == 1);
    for (const auto& e : track)
        if (e.status == 0x90)
            CHECK(e.d1 == 127); // 139 - 12, the same note name
}

TEST_CASE("smoothBends: a grace note sliding in is a fast slide at the start of the note",
          "[midi][bend]") {
    // f3 0-340 (shortened by the grace note), the grace g3 340-383, the marked e4 384-768.
    const std::string midi = testmidi::build({{0, {0xc0, 25}},
                                              {0, {0x90, 53, 90}},
                                              {340, {0x90, 53, 0}},
                                              {340, {0x90, 55, 90}},
                                              {383, {0x90, 55, 0}},
                                              {384, {0xc0, 123}},
                                              {384, {0x90, 64, 90}},
                                              {768, {0x90, 64, 0}}});
    const auto track = trackOf(smooth(midi));
    // Two notes only: f3, then e4 for its whole length; the grace note is gone.
    REQUIRE(noteOns(track) == 2);
    int fOff = -1;
    int eOn = -1;
    double firstBend = 1;
    double lastBend = 1;
    for (const auto& e : track) {
        if (e.status == 0x90 && e.d1 == 53 && e.d2 == 0)
            fOff = static_cast<int>(e.tick);
        if (e.noteOn() && e.d1 == 64)
            eOn = static_cast<int>(e.tick);
        if (e.status == 0xe0 && e.tick >= 384) {
            if (firstBend == 1)
                firstBend = semis(e);
            lastBend = semis(e);
        }
    }
    CHECK(fOff == 384); // the F keeps the time the grace note took
    CHECK(eOn == 384);
    CHECK(firstBend < -8.5);          // starts at the grace note's pitch (g3, 9 semitones down)
    CHECK(std::abs(lastBend) < 0.01); // then the E itself
}
