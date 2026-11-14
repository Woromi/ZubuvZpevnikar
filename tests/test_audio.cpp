#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <zub/core/audio.hpp>
#include <zub/core/midi.hpp>

namespace {

constexpr double pi = 3.14159265358979323846;

void putLe(std::string& s, unsigned value, int bytes) {
    for (int i = 0; i < bytes; ++i)
        s.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
}

// A 16-bit PCM WAV file of interleaved samples.
std::string wavFile(const std::vector<std::int16_t>& samples, unsigned channels = 1,
                    unsigned rate = 44100, unsigned bits = 16) {
    std::string out = "RIFF";
    putLe(out, static_cast<unsigned>(36 + samples.size() * 2), 4);
    out += "WAVEfmt ";
    putLe(out, 16, 4);
    putLe(out, 1, 2);
    putLe(out, channels, 2);
    putLe(out, rate, 4);
    putLe(out, rate * channels * 2, 4);
    putLe(out, channels * 2, 2);
    putLe(out, bits, 2);
    out += "data";
    putLe(out, static_cast<unsigned>(samples.size() * 2), 4);
    for (const std::int16_t s : samples)
        putLe(out, static_cast<std::uint16_t>(s), 2);
    return out;
}

std::vector<std::int16_t> samplesOf(const std::string& wav) {
    std::vector<std::int16_t> out;
    for (std::size_t at = 44; at + 1 < wav.size(); at += 2)
        out.push_back(static_cast<std::int16_t>(
            static_cast<unsigned char>(wav[at]) |
            (static_cast<unsigned>(static_cast<unsigned char>(wav[at + 1])) << 8)));
    return out;
}

// Sum of sines at `hz` with `amplitude` each (of 32767), one second at 44.1 kHz.
std::vector<std::int16_t> sines(std::initializer_list<double> hz, double amplitude) {
    std::vector<std::int16_t> out(44100);
    for (std::size_t i = 0; i < out.size(); ++i) {
        double v = 0;
        for (const double f : hz)
            v += std::sin(2 * pi * f * static_cast<double>(i) / 44100.0);
        out[i] = static_cast<std::int16_t>(std::lround(amplitude * 32767.0 * v));
    }
    return out;
}

std::string amped(std::string wav) {
    REQUIRE(zub::audio::applyAmp(std::span<char>(wav.data(), wav.size())));
    return wav;
}

// Goertzel: the magnitude of the component at `hz` (second half only, past the filter's start).
double magnitude(const std::vector<std::int16_t>& samples, double hz) {
    double re = 0, im = 0;
    const std::size_t from = samples.size() / 2;
    for (std::size_t i = from; i < samples.size(); ++i) {
        const double angle = 2 * pi * hz * static_cast<double>(i) / 44100.0;
        re += samples[i] * std::cos(angle);
        im += samples[i] * std::sin(angle);
    }
    return std::hypot(re, im) / static_cast<double>(samples.size() - from);
}

int peak(const std::vector<std::int16_t>& samples) {
    int p = 0;
    for (const std::int16_t s : samples)
        p = std::max(p, std::abs(static_cast<int>(s)));
    return p;
}

} // namespace

TEST_CASE("amp keeps silence silent and the header intact", "[audio]") {
    const std::string wav = wavFile(std::vector<std::int16_t>(1000, 0));
    const std::string out = amped(wav);
    CHECK(out == wav);
}

TEST_CASE("amp flattens the peaks of a sine and adds odd harmonics", "[audio]") {
    const std::string wav = wavFile(sines({440}, 0.2));
    const auto in = samplesOf(wav);
    const auto out = samplesOf(amped(wav));
    REQUIRE(out.size() == in.size());
    // The clean tone has no third harmonic; the clipped one does, none at the even ones.
    CHECK(magnitude(in, 1320) < 20);
    CHECK(magnitude(out, 1320) > 500);
    CHECK(magnitude(out, 1320) > 10 * magnitude(out, 880));
    CHECK(peak(out) < 32767 / 2 + 2000); // below full scale with room
}

TEST_CASE("amp lets two notes intermodulate", "[audio]") {
    // 440 and 660 Hz: their difference tone at 220 Hz exists only after the clipping.
    const double a = 440, b = 660, difference = 220;
    const auto aloneA = samplesOf(amped(wavFile(sines({a}, 0.1))));
    const auto aloneB = samplesOf(amped(wavFile(sines({b}, 0.1))));
    const auto together = samplesOf(amped(wavFile(sines({a, b}, 0.1))));
    const auto clean = samplesOf(wavFile(sines({a, b}, 0.1)));
    CHECK(magnitude(clean, difference) < 20);
    CHECK(magnitude(aloneA, difference) < 100); // a lone note has harmonics at 880, ... only
    CHECK(magnitude(aloneB, difference) < 100);
    CHECK(magnitude(together, difference) > 300);
    CHECK(magnitude(together, difference) > 5 * magnitude(aloneA, difference) + 100);
}

TEST_CASE("amp never leaves the 16-bit range, whatever comes in", "[audio]") {
    std::vector<std::int16_t> loud(5000);
    for (std::size_t i = 0; i < loud.size(); ++i)
        loud[i] = (i / 20) % 2 ? 32767 : -32768; // a full-scale square wave
    const auto out = samplesOf(amped(wavFile(loud)));
    CHECK(out.size() == loud.size());
    for (const std::int16_t s : out)
        CHECK(std::abs(static_cast<int>(s)) <= 32767);
    CHECK(peak(out) < 32767); // and not flattened against full scale
}

TEST_CASE("amp works on each channel of a stereo file", "[audio]") {
    const auto tone = sines({440}, 0.2);
    std::vector<std::int16_t> stereo;
    for (const std::int16_t s : tone) {
        stereo.push_back(s);
        stereo.push_back(0); // the right channel is silent
    }
    const auto out = samplesOf(amped(wavFile(stereo, 2)));
    REQUIRE(out.size() == stereo.size());
    std::vector<std::int16_t> left, right;
    for (std::size_t i = 0; i < out.size(); i += 2) {
        left.push_back(out[i]);
        right.push_back(out[i + 1]);
    }
    CHECK(peak(left) > 5000);
    CHECK(peak(right) == 0);
    // The left channel is what a mono file of the same tone gives.
    CHECK(left == samplesOf(amped(wavFile(tone))));
}

TEST_CASE("amp leaves what is not 16-bit PCM WAV untouched", "[audio]") {
    const auto untouched = [](std::string wav) {
        const std::string before = wav;
        CHECK_FALSE(zub::audio::applyAmp(std::span<char>(wav.data(), wav.size())));
        CHECK(wav == before);
    };
    untouched("");
    untouched("RIFF");
    untouched("not a wav file at all, but long enough");
    untouched(wavFile(sines({440}, 0.2), 1, 44100, 8));
    untouched(wavFile(sines({440}, 0.2), 0));
    untouched(wavFile(sines({440}, 0.2), 9));
    untouched(wavFile(sines({440}, 0.2), 1, 0));
    std::string float32 = wavFile(sines({440}, 0.2));
    float32[20] = 3; // format tag 3: IEEE float
    untouched(float32);
    std::string noData = wavFile({});
    noData.resize(36); // the data chunk header is gone
    untouched(noData);
}

TEST_CASE("amp copes with a truncated data chunk and chunks before data", "[audio]") {
    std::string wav = wavFile(sines({440}, 0.2));
    wav.resize(wav.size() - 1); // an odd byte is left over
    CHECK(zub::audio::applyAmp(std::span<char>(wav.data(), wav.size())));
    // A LIST chunk between fmt and data.
    std::string withList = wavFile(sines({440}, 0.2));
    std::string list = "LIST";
    putLe(list, 4, 4);
    list += "abcd";
    withList.insert(36, list);
    const std::string plain = amped(wavFile(sines({440}, 0.2)));
    CHECK(amped(withList).substr(44 + list.size()) == plain.substr(44));
}

TEST_CASE("the instrument menu names real General MIDI programs", "[midi]") {
    namespace mi = zub::midi;
    REQUIRE(mi::findInstrument("steel"));
    CHECK(mi::findInstrument("steel")->program == 25);
    CHECK(mi::findInstrument("steel")->name == "acoustic guitar (steel)");
    CHECK(mi::findInstrument("clean")->program == 27);
    CHECK(mi::findInstrument("amp")->program == 27);
    CHECK(mi::findInstrument("amp")->amp);
    CHECK_FALSE(mi::findInstrument("clean")->amp);
    CHECK(mi::findInstrument("banjo")->program == 105);
    CHECK(mi::findInstrument("whistle")->program == 78);
    CHECK(mi::findInstrument("bass")->program == 33);
    CHECK_FALSE(mi::findInstrument("auto"));
    CHECK_FALSE(mi::findInstrument(""));
    // midiInstrument's names resolve (the automatic choice); the clean guitar wins over amp.
    CHECK(mi::findInstrumentByName("acoustic guitar (steel)")->id == "steel");
    CHECK(mi::findInstrumentByName("banjo")->id == "banjo");
    CHECK(mi::findInstrumentByName("whistle")->id == "whistle");
    CHECK(mi::findInstrumentByName("electric guitar (clean)")->id == "clean");
    // ids are unique, programs in range
    for (const auto& a : mi::instruments()) {
        CHECK(a.program >= 0);
        CHECK(a.program <= 127);
        CHECK(mi::findInstrument(a.id) == &a);
    }
}
