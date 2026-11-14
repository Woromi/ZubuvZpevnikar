#pragma once

#include <QString>
#include <filesystem>

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
