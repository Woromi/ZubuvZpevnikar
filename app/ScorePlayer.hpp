#pragma once

#include <QObject>
#include <QString>
#include <filesystem>

class QAudioOutput;
class QMediaPlayer;

// Plays a WAV file with Qt Multimedia, one at a time.
class ScorePlayer : public QObject {
    Q_OBJECT

public:
    explicit ScorePlayer(QObject* parent = nullptr);

    void play(const std::filesystem::path& file);
    void stop();
    bool isPlaying() const;

signals:
    void finished();             // played to the end
    void failed(const QString&); // no audio device, unreadable file ...

private:
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* output_ = nullptr;
};
