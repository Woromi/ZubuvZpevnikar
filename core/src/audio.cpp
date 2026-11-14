#include "zub/core/audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace zub::audio {

namespace {

constexpr double preGain = 10.0;
constexpr double lowPassHz = 5500.0;
constexpr double highPassHz = 90.0;
constexpr double outputLevel = 0.5;
constexpr unsigned maxChannels = 8;
constexpr double pi = 3.14159265358979323846;

unsigned le16(std::span<const char> bytes, std::size_t at) {
    return static_cast<unsigned char>(bytes[at]) |
           (static_cast<unsigned>(static_cast<unsigned char>(bytes[at + 1])) << 8);
}

std::size_t le32(std::span<const char> bytes, std::size_t at) {
    return le16(bytes, at) | (static_cast<std::size_t>(le16(bytes, at + 2)) << 16);
}

std::string_view idAt(std::span<const char> bytes, std::size_t at) {
    return {bytes.data() + at, 4};
}

struct Layout {
    unsigned channels = 0;
    unsigned rate = 0;
    std::size_t dataAt = 0;
    std::size_t dataSize = 0;
};

bool parse(std::span<const char> wav, Layout& layout) {
    if (wav.size() < 12 || idAt(wav, 0) != "RIFF" || idAt(wav, 8) != "WAVE")
        return false;
    unsigned bits = 0;
    for (std::size_t pos = 12; pos + 8 <= wav.size();) {
        const std::size_t size = le32(wav, pos + 4);
        if (idAt(wav, pos) == "fmt " && pos + 8 + 16 <= wav.size()) {
            if (le16(wav, pos + 8) != 1)
                return false; // not PCM
            layout.channels = le16(wav, pos + 10);
            layout.rate = static_cast<unsigned>(le32(wav, pos + 12));
            bits = le16(wav, pos + 22);
        } else if (idAt(wav, pos) == "data") {
            layout.dataAt = pos + 8;
            layout.dataSize = std::min(size, wav.size() - layout.dataAt);
            break;
        }
        pos += 8 + size + (size & 1);
    }
    return layout.dataAt != 0 && layout.channels >= 1 && layout.channels <= maxChannels &&
           bits == 16 && layout.rate > 0;
}

// The coefficient of a one-pole filter with the cutoff `hz`.
double onePole(double hz, unsigned rate) {
    return 1.0 - std::exp(-2.0 * pi * hz / static_cast<double>(rate));
}

} // namespace

bool applyAmp(std::span<char> wav) {
    Layout layout;
    if (!parse(wav, layout))
        return false;
    const double lowCoefficient = onePole(lowPassHz, layout.rate);
    const double highCoefficient = onePole(highPassHz, layout.rate);
    std::array<double, maxChannels> low{};  // state of the low-pass
    std::array<double, maxChannels> rumble{}; // state of the low-pass the high-pass subtracts

    const std::size_t samples = layout.dataSize / 2;
    char* data = wav.data() + layout.dataAt;
    for (std::size_t i = 0; i < samples; ++i) {
        const std::size_t channel = i % layout.channels;
        char* at = data + i * 2;
        const auto raw = static_cast<std::int16_t>(
            static_cast<unsigned>(static_cast<unsigned char>(at[0])) |
            (static_cast<unsigned>(static_cast<unsigned char>(at[1])) << 8));
        const double clipped = std::tanh(preGain * raw / 32768.0);
        low[channel] += lowCoefficient * (clipped - low[channel]);
        rumble[channel] += highCoefficient * (low[channel] - rumble[channel]);
        const double out = (low[channel] - rumble[channel]) * outputLevel;
        const long scaled = std::clamp(std::lround(out * 32768.0), -32768L, 32767L);
        const auto value = static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled));
        at[0] = static_cast<char>(value & 0xff);
        at[1] = static_cast<char>(value >> 8);
    }
    return true;
}

} // namespace zub::audio
