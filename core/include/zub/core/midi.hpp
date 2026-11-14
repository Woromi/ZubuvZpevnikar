#pragma once

#include <cstdint>
#include <expected>
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

// An instrument the playback can use: a stable id (what the app remembers), the General MIDI
// program (0-based) and the name LilyPond's midiInstrument knows for it. `amp` is the clean
// electric guitar sound that goes through zub::audio::applyAmp afterwards.
struct Instrument {
    std::string_view id;
    int program;
    std::string_view name;
    bool amp;
};
// All of them in the order of the instrument menu (the "automatic" choice is the app's).
const std::vector<Instrument>& instruments();
// By id, or by LilyPond name (the first one with it); nullptr when unknown.
const Instrument* findInstrument(std::string_view id);
const Instrument* findInstrumentByName(std::string_view name);

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
// A cut that trimPlan found: shorten the file to `size` bytes, write `dataSize` (little endian,
// 4 bytes) at `dataSizeAt` and `size - 8` at offset 4 (the RIFF size). Nothing else changes, so a
// big file needs no copy.
struct WavTrim {
    std::size_t size = 0;
    std::size_t dataSizeAt = 0;
    unsigned dataSize = 0;
};
// nullopt when trimSilence would leave the input unchanged. Reads only the tail of the data.
std::optional<WavTrim> trimPlan(std::string_view wav, int threshold = 64, int keepMs = 300);
std::string trimSilence(std::string_view wav, int threshold = 64, int keepMs = 300);

// A Standard MIDI File (format 0, one track, 1000 ticks per quarter at 60 bpm: one tick is one
// millisecond) that plays `pitch` (MIDI note number) on channel 0 with the General MIDI `program`
// (0-based) for `durationMs`, followed by silence so FluidSynth renders the release. Arguments
// outside the MIDI ranges are clamped (program, pitch 0-127; duration at least 1).
std::string noteFile(int program, int pitch, int durationMs);

// ---- Bends and slides ----
// LilyPond's MIDI has no pitch bends: a bend `a'\^ \grace h'`, a pre-bend `a'\preBend \^ \grace h'`
// and a slide `d' \glissando g'` are separate notes. The playback score marks the notes that
// belong together (zub::score::markBends): the bend's grace note and the slide's target get a
// one-moment program change to one of these programs (General MIDI sound effects no instrument
// of the playback uses). smoothBends reads them from the MIDI file and turns the notes into one
// sounding note with pitch bend events.
inline constexpr int markerBend = 127;           // grace note: the note before it bends up to it
inline constexpr int markerBendRelease = 126;    // ... and the note after it is the release
inline constexpr int markerPreBend = 125;        // grace note: the note before it starts bent to it
inline constexpr int markerPreBendRelease = 124; // ... and the note after it is the release
inline constexpr int markerSlide = 122;          // the note the previous one slides to
// Raise when smoothBends changes the sound (part of the WAV cache key).
inline constexpr int bendVersion = 1;

// Rewrites the Standard MIDI File `midi` (any format, with ticks per quarter): every marked note
// (see above) is merged into the note before it (the note after a release into that one too),
// which gets pitch bend events (range +-12 semitones, set with RPN 0 on its channel) that glide
// to the marked note's pitch; the markers are removed and the bend is reset before the next
// note of the channel. Pitch bend is per channel, so notes that sound together with a bent one
// bend too. Unmarked or unpairable notes are left alone; a file without markers comes back
// unchanged. Error for anything that is no readable SMF (or has SMPTE time).
std::expected<std::string, std::string> smoothBends(std::string_view midi);

// FNV-1a, 64 bit: a hash that is the same in every build (std::hash is not).
std::uint64_t fnv1a64(std::string_view data);
// fnv1a64 of the parts joined with NUL bytes, as 16 lower-case hex digits: a cache file name.
std::string cacheKey(const std::vector<std::string_view>& parts);

} // namespace zub::midi
