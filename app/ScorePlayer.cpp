#include "ScorePlayer.hpp"

#include "convert.hpp"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#include <utility>

ScorePlayer::ScorePlayer(QObject* parent) : QObject(parent) {}

// The player and its audio output are made on the first play, so starting the app does not
// connect to the sound server (PipeWire): a crash in its client thread was seen at start-up.
void ScorePlayer::ensurePlayer() {
    if (player_)
        return;
    player_ = new QMediaPlayer(this);
    output_ = new QAudioOutput(this);
    player_->setAudioOutput(output_);
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia)
            emit finished();
        else if ((s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia) &&
                 pendingStartMs_ >= 0) {
            // Seek before the first sound, not after it.
            const qint64 start = std::exchange(pendingStartMs_, -1);
            player_->setPosition(start);
            player_->play();
        }
    });
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) { emit failed(message); });
}

void ScorePlayer::play(const std::filesystem::path& file, qint64 startMs) {
    ensurePlayer();
    player_->stop();
    const QUrl url = QUrl::fromLocalFile(toQString(file));
    // The same file again: QMediaPlayer keeps it loaded and reports no new status.
    const auto status = player_->mediaStatus();
    const bool loaded = player_->source() == url && (status == QMediaPlayer::LoadedMedia ||
                                                     status == QMediaPlayer::BufferedMedia ||
                                                     status == QMediaPlayer::EndOfMedia);
    pendingStartMs_ = startMs > 0 && !loaded ? startMs : -1;
    player_->setSource(url);
    if (startMs > 0 && loaded)
        player_->setPosition(startMs);
    if (startMs <= 0 || loaded)
        player_->play();
}

void ScorePlayer::stop() {
    pendingStartMs_ = -1;
    if (player_)
        player_->stop();
}

qint64 ScorePlayer::positionMs() const {
    return player_ && pendingStartMs_ < 0 ? player_->position() : -1;
}

bool ScorePlayer::isPlaying() const {
    return player_ && player_->playbackState() == QMediaPlayer::PlayingState;
}
