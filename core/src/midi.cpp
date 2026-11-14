#include "zub/core/midi.hpp"

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace fs = std::filesystem;

namespace zub::midi {

namespace {

constexpr int tailMs = 400; // silence after the note: the release is rendered too

std::string utf8(const fs::path& p) {
    const auto u8 = p.generic_u8string();
    return {u8.begin(), u8.end()};
}

void appendVlq(std::string& out, unsigned value) {
    std::string bytes;
    bytes.push_back(static_cast<char>(value & 0x7f));
    while (value >>= 7)
        bytes.push_back(static_cast<char>((value & 0x7f) | 0x80));
    out.append(bytes.rbegin(), bytes.rend());
}

void appendBytes(std::string& out, std::initializer_list<int> bytes) {
    for (const int b : bytes)
        out.push_back(static_cast<char>(b));
}

} // namespace

Command renderCommand(const std::string& fluidsynth, const fs::path& soundfont,
                      const fs::path& midiFile, const fs::path& wavFile) {
    Command cmd;
    cmd.program = fluidsynth;
    cmd.arguments = {"-ni", "-q", "-g", "0.7", "-r", "44100", "-T", "wav",
                     "-F",  utf8(wavFile), utf8(soundfont), utf8(midiFile)};
    cmd.wav = wavFile;
    return cmd;
}

std::vector<fs::path> defaultSoundfonts() {
    return {"/usr/share/sounds/sf2/default-GM.sf2", "/usr/share/sounds/sf2/FluidR3_GM.sf2",
            "/usr/share/soundfonts/default.sf2", "/usr/share/soundfonts/FluidR3_GM.sf2",
            "/usr/share/sounds/sf2/TimGM6mb.sf2"};
}

std::optional<fs::path> findSoundfont(const std::vector<fs::path>& candidates) {
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec))
            return candidate;
    }
    return std::nullopt;
}

std::string noteFile(int program, int pitch, int durationMs) {
    program = std::clamp(program, 0, 127);
    pitch = std::clamp(pitch, 0, 127);
    durationMs = std::max(durationMs, 1);

    std::string track;
    appendBytes(track, {0x00, 0xff, 0x51, 0x03, 0x0f, 0x42, 0x40}); // 1,000,000 us per quarter
    appendBytes(track, {0x00, 0xc0, program});                      // program change
    appendBytes(track, {0x00, 0x90, pitch, 100});                   // note on
    appendVlq(track, static_cast<unsigned>(durationMs));
    appendBytes(track, {0x80, pitch, 0}); // note off
    appendVlq(track, tailMs);
    appendBytes(track, {0xff, 0x2f, 0x00}); // end of track

    std::string file = "MThd";
    appendBytes(file, {0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xe8});
    file += "MTrk";
    const auto size = static_cast<unsigned>(track.size());
    appendBytes(file, {static_cast<int>(size >> 24), static_cast<int>((size >> 16) & 0xff),
                       static_cast<int>((size >> 8) & 0xff), static_cast<int>(size & 0xff)});
    file += track;
    return file;
}

namespace {
unsigned le32(std::string_view s, std::size_t at) {
    unsigned v = 0;
    for (std::size_t i = 4; i-- > 0;)
        v = (v << 8) | static_cast<unsigned char>(s[at + i]);
    return v;
}
unsigned le16(std::string_view s, std::size_t at) {
    return static_cast<unsigned char>(s[at]) | (static_cast<unsigned>(static_cast<unsigned char>(s[at + 1])) << 8);
}
void putLe32(std::string& s, std::size_t at, unsigned v) {
    for (std::size_t i = 0; i < 4; ++i)
        s[at + i] = static_cast<char>((v >> (8 * i)) & 0xff);
}
} // namespace

std::string trimSilence(std::string_view wav, int threshold, int keepMs) {
    const std::string unchanged(wav);
    if (wav.size() < 12 || wav.substr(0, 4) != "RIFF" || wav.substr(8, 4) != "WAVE")
        return unchanged;
    unsigned channels = 0, rate = 0, bits = 0;
    std::size_t dataAt = 0, dataSize = 0;
    for (std::size_t pos = 12; pos + 8 <= wav.size();) {
        const std::string_view id = wav.substr(pos, 4);
        const std::size_t size = le32(wav, pos + 4);
        if (id == "fmt " && pos + 8 + 16 <= wav.size()) {
            if (le16(wav, pos + 8) != 1)
                return unchanged; // not PCM
            channels = le16(wav, pos + 10);
            rate = le32(wav, pos + 12);
            bits = le16(wav, pos + 22);
        } else if (id == "data") {
            dataAt = pos + 8;
            dataSize = std::min(size, wav.size() - dataAt);
            break;
        }
        pos += 8 + size + (size & 1);
    }
    if (dataAt == 0 || channels == 0 || bits != 16 || rate == 0)
        return unchanged;
    const std::size_t frameBytes = channels * 2;
    const std::size_t frames = dataSize / frameBytes;
    std::size_t last = frames; // one past the last loud frame
    while (last > 0) {
        bool loud = false;
        for (unsigned c = 0; c < channels && !loud; ++c) {
            const auto sample = static_cast<std::int16_t>(le16(wav, dataAt + (last - 1) * frameBytes + c * 2));
            loud = std::abs(static_cast<int>(sample)) > threshold;
        }
        if (loud)
            break;
        --last;
    }
    if (last == 0)
        return unchanged;
    const std::size_t keep = static_cast<std::size_t>(keepMs) * rate / 1000;
    const std::size_t newFrames = std::min(frames, last + keep);
    if (newFrames == frames && dataSize == frames * frameBytes && dataAt + dataSize == wav.size())
        return unchanged;
    std::string out(wav.substr(0, dataAt + newFrames * frameBytes));
    putLe32(out, dataAt - 4, static_cast<unsigned>(newFrames * frameBytes));
    putLe32(out, 4, static_cast<unsigned>(out.size() - 8));
    return out;
}

std::uint64_t fnv1a64(std::string_view data) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : data) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string cacheKey(const std::vector<std::string_view>& parts) {
    std::string joined;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            joined.push_back('\0');
        joined += parts[i];
    }
    constexpr char digits[] = "0123456789abcdef";
    const std::uint64_t hash = fnv1a64(joined);
    std::string key(16, '0');
    for (int i = 0; i < 16; ++i)
        key[static_cast<std::size_t>(i)] = digits[(hash >> (60 - 4 * i)) & 0xf];
    return key;
}

} // namespace zub::midi
