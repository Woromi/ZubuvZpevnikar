#include "NoteSounds.hpp"

#include "WavFile.hpp"
#include "convert.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QSoundEffect>
#include <QUrl>
#include <algorithm>
#include <fstream>
#include <system_error>
#include <zub/core/midi.hpp>

namespace fs = std::filesystem;

NoteSounds::NoteSounds(fs::path cacheDir, QObject* parent)
    : QObject(parent), baseDir_(std::move(cacheDir)), process_(new QProcess(this)) {
    process_->setProcessChannelMode(QProcess::MergedChannels);
    const auto done = [this] {
        if (!rendering_)
            return;
        const Note note = *rendering_;
        rendering_.reset();
        const fs::path wav = fileOf(note.first, note.second);
        fs::path temp = wav;
        temp += ".tmp";
        std::error_code ec;
        const bool ok = process_->exitStatus() == QProcess::NormalExit &&
                        process_->exitCode() == 0 && fs::is_regular_file(temp, ec) &&
                        fs::file_size(temp, ec) > 44;
        if (ok && !trimWavFile(temp)) {
            fs::remove(temp, ec);
            emit failed(tr("Cannot trim the rendered note sound."));
            renderNext();
            return;
        }
        if (ok) {
            fs::rename(temp, wav, ec);
            if (wanted_ == note)
                playFile(wav);
        } else {
            fs::remove(temp, ec);
            emit failed(tr("Cannot render the note sound: %1")
                            .arg(QString::fromUtf8(process_->readAll()).trimmed()));
        }
        renderNext();
    };
    connect(process_, &QProcess::finished, this, done);
    connect(process_, &QProcess::errorOccurred, this, [this, done](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            done();
    });
}

NoteSounds::~NoteSounds() {
    if (process_->state() != QProcess::NotRunning) {
        process_->blockSignals(true);
        process_->kill();
        process_->waitForFinished(2000);
    }
}

fs::path NoteSounds::fileOf(int program, int pitch) const {
    return dir_ / (std::to_string(program) + "-" + std::to_string(pitch) + ".wav");
}

void NoteSounds::play(const SoundTools& tools, int program, int pitch) {
    if (!tools.available())
        return;
    tools_ = tools;
    const std::string sfKey = zub::midi::cacheKey({toStdString(toQString(tools.soundfont))});
    dir_ = baseDir_ / sfKey;
    const Note note{program, pitch};
    wanted_ = note;
    const fs::path wav = fileOf(program, pitch);
    std::error_code ec;
    if (fs::is_regular_file(wav, ec)) {
        playFile(wav);
        return;
    }
    if (rendering_ == note || std::ranges::find(queue_, note) != queue_.end())
        return;
    queue_.push_back(note);
    renderNext();
}

void NoteSounds::renderNext() {
    if (rendering_ || queue_.empty())
        return;
    // Newest request first: that is the one the user waits for.
    const Note note = queue_.back();
    queue_.pop_back();
    rendering_ = note;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    const fs::path wav = fileOf(note.first, note.second);
    fs::path temp = wav;
    temp += ".tmp";
    fs::path midi = wav;
    midi.replace_extension(".mid");
    {
        std::ofstream out(midi, std::ios::binary);
        out << zub::midi::noteFile(note.first, note.second, 1000);
    }
    const auto command = zub::midi::renderCommand(toStdString(tools_.fluidsynth),
                                                  tools_.soundfont, midi, temp);
    QStringList arguments;
    for (const std::string& argument : command.arguments)
        arguments << toQString(argument);
    process_->start(tools_.fluidsynth, arguments);
}

void NoteSounds::playFile(const fs::path& wav) {
    const QString path = toQString(wav);
    QSoundEffect*& effect = effects_[path];
    if (effect && effect->status() == QSoundEffect::Error) {
        effect->deleteLater(); // a failed load is not retried by QSoundEffect itself
        effect = nullptr;
    }
    if (!effect) {
        effect = new QSoundEffect(this);
        effect->setSource(QUrl::fromLocalFile(path));
    }
    // Other notes stop: only the newest click sounds.
    for (auto& [other, sound] : effects_) {
        if (sound && sound != effect && sound->isPlaying())
            sound->stop();
    }
    if (!effect->isLoaded()) {
        // Status Null or Loading right after setSource: play once it is loaded (one pending play).
        disconnect(effect, &QSoundEffect::statusChanged, this, nullptr);
        connect(effect, &QSoundEffect::statusChanged, this, [effect] {
            if (effect->status() == QSoundEffect::Ready) {
                effect->disconnect(effect, &QSoundEffect::statusChanged, nullptr, nullptr);
                effect->play();
            }
        });
        return;
    }
    // play() on a sound that is still playing does not restart it.
    effect->stop();
    effect->play();
}
