#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zub/core/midi.hpp>

namespace fs = std::filesystem;
namespace mi = zub::midi;

namespace {
std::string bytes(std::initializer_list<int> values) {
    std::string s;
    for (const int v : values)
        s.push_back(static_cast<char>(v));
    return s;
}
} // namespace

TEST_CASE("midi noteFile is a one-track file with one tick per millisecond", "[midi]") {
    const std::string expected =
        std::string("MThd") + bytes({0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xe8}) + "MTrk" +
        bytes({0, 0, 0, 24}) + bytes({0x00, 0xff, 0x51, 0x03, 0x0f, 0x42, 0x40}) +
        bytes({0x00, 0xc0, 25}) + bytes({0x00, 0x90, 64, 100}) +
        bytes({0x87, 0x68}) + // 1000 ms
        bytes({0x80, 64, 0}) + bytes({0x83, 0x10}) +          // 400 ms of silence
        bytes({0xff, 0x2f, 0x00});
    CHECK(mi::noteFile(25, 64, 1000) == expected);
}

TEST_CASE("midi noteFile clamps its arguments", "[midi]") {
    CHECK(mi::noteFile(500, 300, 0) == mi::noteFile(127, 127, 1));
    CHECK(mi::noteFile(-3, -1, 5) == mi::noteFile(0, 0, 5));
    // A short note has a one-byte delta: 5 ms.
    const auto file = mi::noteFile(0, 60, 5);
    CHECK(file.find(bytes({0x05, 0x80, 60, 0})) != std::string::npos);
}

TEST_CASE("midi renderCommand renders to a file and exits", "[midi]") {
    const auto cmd = mi::renderCommand("fluidsynth", "/sf/GM.sf2", "/tmp/a b.mid", "/tmp/a.wav");
    CHECK(cmd.program == "fluidsynth");
    CHECK(cmd.arguments == std::vector<std::string>{"-ni", "-q", "-g", "0.7", "-r", "44100", "-T",
                                                    "wav", "-F", "/tmp/a.wav", "/sf/GM.sf2",
                                                    "/tmp/a b.mid"});
    CHECK(cmd.wav == "/tmp/a.wav");
}

TEST_CASE("midi findSoundfont returns the first existing file", "[midi]") {
    const fs::path dir = fs::temp_directory_path() / "zub-midi-test-sf";
    fs::create_directories(dir);
    std::ofstream(dir / "second.sf2") << "x";
    std::ofstream(dir / "third.sf2") << "x";
    fs::create_directories(dir / "folder.sf2"); // a directory is no soundfont

    CHECK(mi::findSoundfont({dir / "missing.sf2", dir / "folder.sf2", dir / "second.sf2",
                             dir / "third.sf2"}) == dir / "second.sf2");
    CHECK_FALSE(mi::findSoundfont({dir / "missing.sf2"}));
    CHECK_FALSE(mi::findSoundfont({}));
    fs::remove_all(dir);

    const auto defaults = mi::defaultSoundfonts();
    REQUIRE(defaults.size() == 5);
    CHECK(defaults.front() == "/usr/share/sounds/sf2/default-GM.sf2");
    CHECK(defaults.back() == "/usr/share/sounds/sf2/TimGM6mb.sf2");
}

TEST_CASE("midi hashing is stable", "[midi]") {
    CHECK(mi::fnv1a64("") == 0xcbf29ce484222325ull);
    CHECK(mi::fnv1a64("a") == 0xaf63dc4c8601ec8cull);
    CHECK(mi::fnv1a64("foobar") == 0x85944171f73967e8ull);
    CHECK(mi::cacheKey({"a"}) == "af63dc4c8601ec8c");
    CHECK(mi::cacheKey({"ab", "c"}) != mi::cacheKey({"a", "bc"}));
    CHECK(mi::cacheKey({"x", "y"}) == mi::cacheKey({"x", "y"}));
}

namespace {
std::string le(unsigned v, int bytes) {
    std::string s;
    for (int i = 0; i < bytes; ++i)
        s.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    return s;
}
// A mono 16-bit WAV at 1000 Hz with a LIST chunk before the data.
std::string wavOf(const std::vector<int>& samples) {
    std::string data;
    for (const int s : samples)
        data += le(static_cast<unsigned>(s) & 0xffff, 2);
    std::string body = "WAVE";
    body += "fmt " + le(16, 4) + le(1, 2) + le(1, 2) + le(1000, 4) + le(2000, 4) + le(2, 2) + le(16, 2);
    body += "LIST" + le(3, 4) + "abc" + std::string(1, '\0'); // odd size: padded
    body += "data" + le(static_cast<unsigned>(data.size()), 4) + data;
    return "RIFF" + le(static_cast<unsigned>(body.size()), 4) + body;
}
} // namespace

TEST_CASE("midi trimSilence cuts the silent tail", "[midi]") {
    std::vector<int> samples(200, 0);
    samples[40] = 5000;
    samples[41] = -3000; // the last loud sample
    const auto trimmed = mi::trimSilence(wavOf(samples), 64, 100);
    CHECK(trimmed == wavOf(std::vector<int>(samples.begin(), samples.begin() + 42 + 100)));
    // A tail shorter than keepMs stays.
    CHECK(mi::trimSilence(wavOf(samples), 64, 1000) == wavOf(samples));
    // Quiet noise below the threshold counts as silence.
    samples[150] = 30;
    CHECK(mi::trimSilence(wavOf(samples), 64, 100) == trimmed);
}

TEST_CASE("midi trimSilence leaves other input alone", "[midi]") {
    CHECK(mi::trimSilence("") == "");
    CHECK(mi::trimSilence("not a wav file at all") == "not a wav file at all");
    const auto silent = wavOf(std::vector<int>(50, 0));
    CHECK(mi::trimSilence(silent) == silent);
}

TEST_CASE("midi trimPlan describes the cut without a copy", "[midi]") {
    std::vector<int> samples(200, 0);
    samples[40] = 5000;
    const std::string wav = wavOf(samples);
    const auto plan = mi::trimPlan(wav, 64, 100);
    REQUIRE(plan);
    const std::string expected = mi::trimSilence(wav, 64, 100);
    CHECK(plan->size == expected.size());
    CHECK(plan->size < wav.size());
    CHECK(plan->dataSize == 141u * 2u);
    // The patch the app applies to the file gives the same bytes as the copy.
    std::string patched = wav.substr(0, plan->size);
    for (std::size_t i = 0; i < 4; ++i) {
        patched[plan->dataSizeAt + i] = static_cast<char>((plan->dataSize >> (8 * i)) & 0xff);
        patched[4 + i] = static_cast<char>(((plan->size - 8) >> (8 * i)) & 0xff);
    }
    CHECK(patched == expected);

    CHECK_FALSE(mi::trimPlan("", 64, 100));
    CHECK_FALSE(mi::trimPlan("RIFF", 64, 100));
    CHECK_FALSE(mi::trimPlan(wav, 64, 1000));
    CHECK_FALSE(mi::trimPlan(wavOf(std::vector<int>(50, 0))));
    CHECK_FALSE(mi::trimPlan(wav.substr(0, 30), 64, 100)); // cut inside the header
    // A data chunk that claims more than the file holds stays within the file.
    std::string lying = wav;
    lying[wav.find("data") + 4] = '\xff';
    lying[wav.find("data") + 5] = '\xff';
    lying[wav.find("data") + 6] = '\xff';
    const auto lyingPlan = mi::trimPlan(lying, 64, 100);
    REQUIRE(lyingPlan);
    CHECK(lyingPlan->size <= lying.size());
}
