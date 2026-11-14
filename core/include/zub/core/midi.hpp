#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Sound: what core knows about turning MIDI into audio with FluidSynth (the app runs the command
// with QProcess and plays the WAV with Qt Multimedia), and the small MIDI files of single notes.
namespace zub::midi {

// General MIDI programs (0-based, as in a MIDI program change).
inline constexpr int programSteelGuitar = 25; // "acoustic guitar (steel)"
inline constexpr int programBanjo = 105;      // "banjo"
inline constexpr int programWhistle = 78;     // "whistle"

struct Command {
    std::string program; // the fluidsynth executable as given
    std::vector<std::string> arguments;
    std::filesystem::path wav; // written on success
};

// `fluidsynth -ni -q -g 0.7 -r 44100 -T wav -F <wav> <soundfont> <midi>`: renders the MIDI file to
// a WAV file without an audio driver and exits by itself.
Command renderCommand(const std::string& fluidsynth, const std::filesystem::path& soundfont,
                      const std::filesystem::path& midiFile, const std::filesystem::path& wavFile);

// The General MIDI soundfonts distributions install, in order of preference.
std::vector<std::filesystem::path> defaultSoundfonts();
// The first of `candidates` that is an existing regular file.
std::optional<std::filesystem::path>
findSoundfont(const std::vector<std::filesystem::path>& candidates);

// FluidSynth renders until the soundfont's samples have died away, which can be ten seconds of
// silence behind the music. Returns the 16-bit PCM WAV file `wav` cut `keepMs` after its last
// sample louder than `threshold` (of 32767); unchanged when it is not such a file or has no sound.
std::string trimSilence(std::string_view wav, int threshold = 64, int keepMs = 300);

// A Standard MIDI File (format 0, one track, 1000 ticks per quarter at 60 bpm: one tick is one
// millisecond) that plays `pitch` (MIDI note number) on channel 0 with the General MIDI `program`
// (0-based) for `durationMs`, followed by silence so FluidSynth renders the release. Arguments
// outside the MIDI ranges are clamped (program, pitch 0-127; duration at least 1).
std::string noteFile(int program, int pitch, int durationMs);

// FNV-1a, 64 bit: a hash that is the same in every build (std::hash is not).
std::uint64_t fnv1a64(std::string_view data);
// fnv1a64 of the parts joined with NUL bytes, as 16 lower-case hex digits: a cache file name.
std::string cacheKey(const std::vector<std::string_view>& parts);

} // namespace zub::midi
