#pragma once

#include "SoundTools.hpp"

#include <QObject>
#include <QString>
#include <filesystem>
#include <map>
#include <optional>
#include <vector>

class QProcess;
class QTimer;
class QSoundEffect;

// The short sound of one note (a fret that was clicked): a one-note MIDI file
// (zub::midi::noteFile) is rendered once by fluidsynth to <cacheDir>/<soundfont key>/<program>-
// <pitch>.wav (<program>-<pitch>-amp.wav for the amp: the note goes through zub::audio::applyAmp),
// then played with QSoundEffect, which starts at once. A note that is not rendered
// yet plays when it is ready, unless another note was asked for meanwhile.
class NoteSounds : public QObject {
    Q_OBJECT

public:
    explicit NoteSounds(std::filesystem::path cacheDir, QObject* parent = nullptr);
    ~NoteSounds() override;

    // `program`: General MIDI program (0-based), `pitch`: MIDI note number.
    // `amp`: the note is distorted like the score playback's Distortion (amp).
    void play(const SoundTools& tools, int program, int pitch, bool amp = false);
    // The sound file of a note, for tests; empty before the first play() with these tools.
    std::filesystem::path fileOf(int program, int pitch, bool amp = false) const;

signals:
    void failed(const QString& message);

private:
    struct Note {
        int program = 0;
        int pitch = 0;
        bool amp = false;
        bool operator==(const Note&) const = default;
    };
    // The note being rendered with the paths it was started with.
    struct Rendering {
        Note note;
        std::filesystem::path temp; // fluidsynth writes here
        std::filesystem::path wav;  // moved here when it is complete
    };
    void renderNext();
    void killTree();
    void playFile(const std::filesystem::path& wav);

    std::filesystem::path baseDir_;
    std::filesystem::path dir_; // for the current soundfont
    SoundTools tools_;
    QProcess* process_ = nullptr;
    std::vector<Note> queue_;      // waiting to be rendered
    QTimer* timeout_ = nullptr;
    bool timedOut_ = false;
    std::optional<Rendering> rendering_;
    std::optional<Note> wanted_;    // the newest request: plays when it is ready
    std::filesystem::path wantedFile_; // its sound file (a soundfont change makes another one)
    std::map<QString, QSoundEffect*> effects_;
};
