#pragma once

#include <QString>
#include <filesystem>
#include <string>

// What playback needs: FluidSynth (renders MIDI to a WAV file) and a General MIDI soundfont.
struct SoundTools {
    QString fluidsynth;                // the executable's path; empty when it is not installed
    std::filesystem::path soundfont;   // empty when none was found

    bool available() const { return !fluidsynth.isEmpty() && !soundfont.empty(); }
    // Says what to install when something is missing (for a tooltip); empty when available.
    QString missingHint() const;
};

// fluidsynth on PATH; the soundfont is `playback/soundfont` of the settings when that file exists,
// else the first of zub::midi::defaultSoundfonts().
SoundTools findSoundTools();

// What identifies a soundfont's content for cache keys: its path, size and last-write time (a
// soundfont replaced in place must not reuse sounds rendered with the old one).
std::string soundfontIdentity(const std::filesystem::path& soundfont);
