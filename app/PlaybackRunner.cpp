#include "PlaybackRunner.hpp"

#include "WavFile.hpp"
#include "convert.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <algorithm>
#include <string_view>
#include <system_error>
#include <zub/core/midi.hpp>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {
constexpr int timeoutSeconds = 60;
constexpr int keptWavFiles = 6; // the newest ones stay in the cache
} // namespace

PlaybackRunner::PlaybackRunner(QObject* parent)
    : QObject(parent), process_(new QProcess(this)), timeout_(new QTimer(this)) {
    timeout_->setSingleShot(true);
    timeout_->setInterval(timeoutSeconds * 1000);
    process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(process_, &QProcess::finished, this, [this] { onProcessFinished(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Other errors (crash, timeout) are followed by finished(); FailedToStart is not.
        if (error != QProcess::FailedToStart)
            return;
        startFailed_ = true;
        onProcessFinished();
    });
    connect(timeout_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        killTree();
    });
}

PlaybackRunner::~PlaybackRunner() {
    if (process_->state() != QProcess::NotRunning) {
        process_->blockSignals(true);
        killTree();
        process_->waitForFinished(2000);
    }
}

bool PlaybackRunner::isRunning() const {
    return running_;
}

void PlaybackRunner::request(PlaybackRequest request) {
    if (process_->state() != QProcess::NotRunning) {
        // The work dir must not be rewritten while the old build runs.
        pending_ = std::move(request);
        emit started();
        killTree();
        return;
    }
    start(std::move(request));
}

void PlaybackRunner::cancel() {
    pending_.reset();
    if (process_->state() != QProcess::NotRunning) {
        cancelled_ = true;
        killTree();
    } else {
        running_ = false;
    }
}

void PlaybackRunner::killTree() {
#ifdef Q_OS_UNIX
    // Each process runs in its own process group, so whatever lilypond started dies with it.
    if (const qint64 pid = process_->processId(); pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
    process_->kill();
}

void PlaybackRunner::start(PlaybackRequest request) {
    current_ = std::move(request);
    cancelled_ = false;
    timedOut_ = false;
    startFailed_ = false;

    PlaybackResult failure;
    std::error_code ec;
    QFile midi(toQString(current_.midiFile));
    if (!midi.open(QIODevice::ReadOnly)) {
        failure.message = tr("Cannot read the MIDI file: %1").arg(midi.errorString());
        emit finished(failure);
        return;
    }
    const QByteArray bytes = midi.readAll();
    const std::string soundfont = toStdString(toQString(current_.soundfont));
    const std::string key = zub::midi::cacheKey(
        {std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), soundfont});
    fs::create_directories(current_.cacheDir, ec);
    wav_ = current_.cacheDir / (key + ".wav");
    wavTemp_ = current_.cacheDir /
               (key + "." + std::to_string(QCoreApplication::applicationPid()) + ".tmp");
    if (fs::is_regular_file(wav_, ec) && fs::file_size(wav_, ec) > 0) {
        fs::last_write_time(wav_, fs::file_time_type::clock::now(), ec); // newest again
        PlaybackResult result;
        result.success = true;
        result.cached = true;
        result.wav = wav_;
        emit finished(result);
        return;
    }

    running_ = true;
    emit started();
    const auto render = zub::midi::renderCommand(toStdString(current_.fluidsynth),
                                                 current_.soundfont, current_.midiFile, wavTemp_);
    QStringList list;
    for (const std::string& argument : render.arguments)
        list << toQString(argument);
    timeout_->start();
#ifdef Q_OS_UNIX
    process_->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
    process_->start(current_.fluidsynth, list);
}

void PlaybackRunner::finishWith(PlaybackResult result) {
    timeout_->stop();
    running_ = false;
    timedOut_ = false;
    emit finished(result);
}

void PlaybackRunner::onProcessFinished() {
    timeout_->stop();
    std::error_code ec;
    if (pending_) {
        auto next = std::move(*pending_);
        pending_.reset();
        fs::remove(wavTemp_, ec);
        start(std::move(next));
        return;
    }
    if (cancelled_) {
        cancelled_ = false;
        timedOut_ = false;
        running_ = false;
        fs::remove(wavTemp_, ec);
        return;
    }

    PlaybackResult failure;
    const QByteArray output = process_->readAll();
    if (startFailed_) {
        startFailed_ = false;
        failure.message = tr("Cannot start fluidsynth: %1").arg(process_->errorString());
    } else if (timedOut_) {
        failure.message = tr("fluidsynth took longer than %1 s and was stopped.").arg(timeoutSeconds);
    } else if (process_->exitStatus() == QProcess::CrashExit) {
        failure.message = tr("fluidsynth crashed.");
    } else if (process_->exitCode() != 0) {
        failure.message = tr("fluidsynth failed: %1").arg(QString::fromUtf8(output).trimmed());
    } else if (!fs::is_regular_file(wavTemp_, ec) || fs::file_size(wavTemp_, ec) <= 44) {
        failure.message = tr("fluidsynth wrote no audio.");
    }
    if (!failure.message.isEmpty()) {
        fs::remove(wavTemp_, ec);
        finishWith(failure);
        return;
    }
    // Cut the silence the soundfont's long release leaves behind the music. A failed trim leaves
    // the temp file in an unknown state: it never reaches the cache.
    if (!trimWavFile(wavTemp_)) {
        fs::remove(wavTemp_, ec);
        failure.message = tr("Cannot trim the rendered audio.");
        finishWith(failure);
        return;
    }
    fs::rename(wavTemp_, wav_, ec);
    if (ec) {
        failure.message = toQString(wav_.string() + ": " + ec.message());
        finishWith(failure);
        return;
    }
    trimCache();
    PlaybackResult result;
    result.success = true;
    result.wav = wav_;
    finishWith(result);
}

// Keeps the newest few WAV files of the cache directory.
void PlaybackRunner::trimCache() const {
    QDir dir(toQString(current_.cacheDir));
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("*.wav")}, QDir::Files, QDir::Time); // newest first
    for (int i = keptWavFiles; i < files.size(); ++i)
        QFile::remove(files[i].absoluteFilePath());
}
