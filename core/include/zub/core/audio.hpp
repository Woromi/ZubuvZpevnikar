#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <span>
#include <string_view>

// Audio effects on rendered WAV data (what FluidSynth writes: 16-bit PCM). std only; the app maps
// the file and calls these in place.
namespace zub::audio {

// The id and version of the amp effect: part of the cache key of an audio file that went through
// it. Raise ampVersion whenever applyAmp's sound changes, so older cached files are not played.
inline constexpr std::string_view ampId = "amp";
inline constexpr int ampVersion = 2; // 2: the effect runs before the silence is trimmed

// Guitar amp distortion, applied in place to the 16-bit PCM WAV file `wav` (the whole file: RIFF
// header and data). It works on the mixed signal, so notes played together intermodulate: a chord
// through it sounds rougher than its notes alone. Per channel and sample: pre-gain 10, tanh soft
// clip, one-pole low-pass at 5.5 kHz (the speaker cabinet takes the fizz away), one-pole high-pass
// at 90 Hz (tightness), then x 0.5 so the peaks stay below full scale. Silence stays silence.
// Returns false and leaves `wav` untouched when it is not a 16-bit PCM WAV file with 1 to 8
// channels. Needs no memory beyond the file.
bool applyAmp(std::span<char> wav);

// Mixes two 16-bit PCM WAV files of the same channel count and sample rate (what FluidSynth
// writes for two MIDI files of one score): a new WAV file as long as the longer one, each sample
// the sum of both clamped to 16 bits (the shorter file counts as silence behind its end). Nullopt
// when either is no such file or they differ in channels or rate.
std::optional<std::string> mixWav(std::span<const char> first, std::span<const char> second);

} // namespace zub::audio
