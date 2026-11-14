#include "ScorePlayer.hpp"

#include "convert.hpp"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>

ScorePlayer::ScorePlayer(QObject* parent)
    : QObject(parent), player_(new QMediaPlayer(this)), output_(new QAudioOutput(this)) {
    player_->setAudioOutput(output_);
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia)
            emit finished();
    });
    connect(player_, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString& message) { emit failed(message); });
}

void ScorePlayer::play(const std::filesystem::path& file) {
    player_->stop();
    player_->setSource(QUrl::fromLocalFile(toQString(file)));
    player_->play();
}

void ScorePlayer::stop() {
    player_->stop();
}

bool ScorePlayer::isPlaying() const {
    return player_->playbackState() == QMediaPlayer::PlayingState;
}
