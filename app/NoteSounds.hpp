#pragma once

#include "SoundTools.hpp"

#include <QObject>
#include <QString>
#include <filesystem>
#include <map>
#include <optional>
#include <utility>
#include <vector>

class QProcess;
class QSoundEffect;

// The short sound of one note (a fret that was clicked): a one-note MIDI file
// (zub::midi::noteFile) is rendered once by fluidsynth to <cacheDir>/<soundfont key>/<program>-
// <pitch>.wav, then played with QSoundEffect, which starts at once. A note that is not rendered
// yet plays when it is ready, unless another note was asked for meanwhile.
class NoteSounds : public QObject {
    Q_OBJECT

public:
    explicit NoteSounds(std::filesystem::path cacheDir, QObject* parent = nullptr);
    ~NoteSounds() override;

    // `program`: General MIDI program (0-based), `pitch`: MIDI note number.
    void play(const SoundTools& tools, int program, int pitch);
    // The sound file of a note, for tests; empty before the first play() with these tools.
    std::filesystem::path fileOf(int program, int pitch) const;

signals:
    void failed(const QString& message);

private:
    using Note = std::pair<int, int>; // program, pitch
    void renderNext();
    void playFile(const std::filesystem::path& wav);

    std::filesystem::path baseDir_;
    std::filesystem::path dir_; // for the current soundfont
    SoundTools tools_;
    QProcess* process_ = nullptr;
    std::vector<Note> queue_;      // waiting to be rendered
    std::optional<Note> rendering_; // being rendered
    std::optional<Note> wanted_;    // the newest request: plays when it is ready
    std::map<QString, QSoundEffect*> effects_;
};
