#pragma once

#include <QObject>
#include <QString>
#include <filesystem>

class QAudioOutput;
class QMediaPlayer;

// Plays a WAV file with Qt Multimedia, one at a time. The methods are virtual so that the app tests
// can stand in a silent player that has no audio device.
class ScorePlayer : public QObject {
    Q_OBJECT

public:
    explicit ScorePlayer(QObject* parent = nullptr);

    // Plays `file` from `startMs`.
    virtual void play(const std::filesystem::path& file, qint64 startMs = 0);
    virtual void stop();
    virtual bool isPlaying() const;
    // Where the audio is in milliseconds; -1 while nothing plays yet (a start position is
    // pending until the file is loaded).
    virtual qint64 positionMs() const;

signals:
    void finished();             // played to the end
    void failed(const QString&); // no audio device, unreadable file ...

private:
    void ensurePlayer(); // made on the first play()
    QMediaPlayer* player_ = nullptr;
    qint64 pendingStartMs_ = -1; // wait for the media to load, then seek and play
    QAudioOutput* output_ = nullptr;
};
