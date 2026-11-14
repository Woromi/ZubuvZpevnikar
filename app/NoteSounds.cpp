#include "NoteSounds.hpp"

#include "WavFile.hpp"
#include "convert.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QProcess>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <fstream>
#include <system_error>
#include <zub/core/midi.hpp>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
constexpr int timeoutSeconds = 30;
}

NoteSounds::NoteSounds(fs::path cacheDir, QObject* parent)
    : QObject(parent), baseDir_(std::move(cacheDir)), process_(new QProcess(this)),
      timeout_(new QTimer(this)) {
    process_->setProcessChannelMode(QProcess::MergedChannels);
    timeout_->setSingleShot(true);
    timeout_->setInterval(timeoutSeconds * 1000);
    // A hung render must not block every later fret sound.
    connect(timeout_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        killTree();
    });
    const auto done = [this] {
        if (!rendering_)
            return;
        timeout_->stop();
        // The paths belong to the note that was rendered, not to the current soundfont.
        const Rendering job = *rendering_;
        rendering_.reset();
        std::error_code ec;
        const bool ok = !timedOut_ && process_->exitStatus() == QProcess::NormalExit &&
                        process_->exitCode() == 0 && fs::is_regular_file(job.temp, ec) &&
                        fs::file_size(job.temp, ec) > 44;
        QString problem;
        if (timedOut_) {
            problem = tr("The sound was not rendered within %1 s.").arg(timeoutSeconds);
        } else if (!ok) {
            problem = tr("Cannot render the note sound: %1")
                          .arg(QString::fromUtf8(process_->readAll()).trimmed());
        } else if (!trimWavFile(job.temp) || (job.note.amp && !ampWavFile(job.temp))) {
            // A note is short: the effect is quick enough for the UI thread.
            problem = tr("Cannot trim the rendered note sound.");
        } else {
            fs::rename(job.temp, job.wav, ec);
            if (ec)
                problem = toQString(job.wav.string() + ": " + ec.message());
        }
        timedOut_ = false;
        if (problem.isEmpty()) {
            if (wanted_ == job.note && wantedFile_ == job.wav)
                playFile(job.wav);
        } else {
            fs::remove(job.temp, ec);
            emit failed(problem);
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
        killTree();
        process_->waitForFinished(2000);
    }
}

void NoteSounds::killTree() {
#ifdef Q_OS_UNIX
    // The render runs in its own process group, like PlaybackRunner's.
    if (const qint64 pid = process_->processId(); pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
    process_->kill();
}

fs::path NoteSounds::fileOf(int program, int pitch, bool amp) const {
    return dir_ / (std::to_string(program) + "-" + std::to_string(pitch) + (amp ? "-amp" : "") +
                   ".wav");
}

void NoteSounds::play(const SoundTools& tools, int program, int pitch, bool amp) {
    if (!tools.available())
        return;
    tools_ = tools;
    const std::string sfKey = zub::midi::cacheKey({soundfontIdentity(tools.soundfont)});
    dir_ = baseDir_ / sfKey;
    const Note note{program, pitch, amp};
    wanted_ = note;
    const fs::path wav = fileOf(program, pitch, amp);
    wantedFile_ = wav;
    std::error_code ec;
    if (fs::is_regular_file(wav, ec)) {
        playFile(wav);
        return;
    }
    if ((rendering_ && rendering_->note == note && rendering_->wav == wav) || std::ranges::find(queue_, note) != queue_.end())
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
    const fs::path wav = fileOf(note.program, note.pitch, note.amp);
    fs::path temp = wav;
    temp += ".tmp";
    rendering_ = Rendering{note, temp, wav};
    timedOut_ = false;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    fs::path midi = wav;
    midi.replace_extension(".mid");
    {
        std::ofstream out(midi, std::ios::binary);
        out << zub::midi::noteFile(note.program, note.pitch, 1000);
    }
    const auto command = zub::midi::renderCommand(toStdString(tools_.fluidsynth),
                                                  tools_.soundfont, midi, temp);
    QStringList arguments;
    for (const std::string& argument : command.arguments)
        arguments << toQString(argument);
    timeout_->start();
#ifdef Q_OS_UNIX
    process_->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
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
    // Other notes stop: only the newest click sounds, so an older one that is still loading must
    // not start after it.
    for (auto& [other, sound] : effects_) {
        if (sound && sound != effect) {
            sound->disconnect(sound, &QSoundEffect::statusChanged, nullptr, nullptr);
            if (sound->isPlaying())
                sound->stop();
        }
    }
    if (!effect->isLoaded()) {
        // Status Null or Loading right after setSource: play once it is loaded (one pending play).
        disconnect(effect, &QSoundEffect::statusChanged, this, nullptr);
        connect(effect, &QSoundEffect::statusChanged, this, [this, effect] {
            if (effect->status() == QSoundEffect::Ready) {
                effect->disconnect(effect, &QSoundEffect::statusChanged, nullptr, nullptr);
                effect->play();
            } else if (effect->status() == QSoundEffect::Error) {
                effect->disconnect(effect, &QSoundEffect::statusChanged, nullptr, nullptr);
                emit failed(tr("Cannot load the note sound %1.").arg(effect->source().toLocalFile()));
            }
        });
        return;
    }
    // play() on a sound that is still playing does not restart it.
    effect->stop();
    effect->play();
}
