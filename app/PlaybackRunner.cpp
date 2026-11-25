#include "PlaybackRunner.hpp"

#include "ProcessTree.hpp"

#include "SoundTools.hpp"
#include "WavFile.hpp"
#include "convert.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <zub/core/audio.hpp>
#include <zub/core/midi.hpp>


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
    if (worker_) {
        worker_->disconnect(this);
        worker_->wait();
        delete worker_; // no parent: the thread object is ours
    }
}

bool PlaybackRunner::isRunning() const {
    return running_;
}

void PlaybackRunner::request(PlaybackRequest request) {
    if (worker_) {
        // The temp file is still being processed: start when the worker is done.
        pending_ = std::move(request);
        emit started();
        return;
    }
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
    if (worker_) {
        cancelled_ = true;
    } else if (process_->state() != QProcess::NotRunning) {
        cancelled_ = true;
        killTree();
    } else {
        running_ = false;
    }
}

void PlaybackRunner::killTree() {
    // lualatex, lilypond, gs, fluidsynth: what it started dies with it (processtree).
    processtree::kill(process_);
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
    const std::string soundfont = soundfontIdentity(current_.soundfont);
    // The effect and its version are part of the key: the same MIDI bytes sound different with it.
    const std::string effect =
        current_.amp ? std::string(zub::audio::ampId) + std::to_string(zub::audio::ampVersion)
                     : std::string();
    const std::string rawMidi(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    std::string rawAmpMidi;
    if (!current_.ampMidiFile.empty()) {
        QFile ampMidi(toQString(current_.ampMidiFile));
        if (!ampMidi.open(QIODevice::ReadOnly)) {
            failure.message = tr("Cannot read the MIDI file: %1").arg(ampMidi.errorString());
            emit finished(failure);
            return;
        }
        const QByteArray ampBytes = ampMidi.readAll();
        rawAmpMidi.assign(ampBytes.constData(), static_cast<std::size_t>(ampBytes.size()));
    }
    // The amp staves are mixed in: another sound for the same main MIDI file.
    const bool mixAmp = !current_.ampMidiFile.empty();
    // The bends and slides are part of what is played: their version is part of the key too.
    const std::string bends = "bend" + std::to_string(zub::midi::bendVersion);
    std::vector<std::string_view> parts{rawMidi, soundfont, bends};
    if (!effect.empty())
        parts.push_back(effect);
    const std::string ampEffect =
        std::string(zub::audio::ampId) + std::to_string(zub::audio::ampVersion) + "-mixed";
    if (mixAmp) {
        parts.push_back(rawAmpMidi);
        parts.push_back(ampEffect);
    }
    const std::string key = zub::midi::cacheKey(parts);
    fs::create_directories(current_.cacheDir, ec);
    wav_ = current_.cacheDir / (key + ".wav");
    const std::string pid = std::to_string(QCoreApplication::applicationPid());
    wavTemp_ = current_.cacheDir / (key + "." + pid + ".tmp");
    ampTemp_ = current_.cacheDir / (key + "." + pid + ".amp.tmp");
    renderingAmp_ = false;
    if (fs::is_regular_file(wav_, ec) && fs::file_size(wav_, ec) > 0) {
        fs::last_write_time(wav_, fs::file_time_type::clock::now(), ec); // newest again
        PlaybackResult result;
        result.success = true;
        result.cached = true;
        result.wav = wav_;
        emit finished(result);
        return;
    }

    // fluidsynth plays the file with the bends and slides as pitch bend events; the file is small,
    // so this is no work for a worker thread. Without it (not a file the rewrite reads) the
    // original is played.
    const auto smoothed = [this](const std::filesystem::path& file, const std::string& raw,
                                 const std::string& name) {
        if (const auto smooth = zub::midi::smoothBends(raw); smooth && *smooth != raw) {
            const fs::path smoothFile = current_.cacheDir / (name + ".mid");
            QFile out(toQString(smoothFile));
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                out.write(smooth->data(), static_cast<qint64>(smooth->size())) ==
                    static_cast<qint64>(smooth->size()))
                return smoothFile;
        }
        return file;
    };
    playedMidi_ = smoothed(current_.midiFile, rawMidi, key);
    playedAmpMidi_.clear();
    if (mixAmp)
        playedAmpMidi_ = smoothed(current_.ampMidiFile, rawAmpMidi, key + ".amp");

    running_ = true;
    emit started();
    startRender(playedMidi_, wavTemp_);
}

void PlaybackRunner::startRender(const fs::path& midi, const fs::path& wav) {
    const auto render = zub::midi::renderCommand(toStdString(current_.fluidsynth),
                                                 current_.soundfont, midi, wav);
    QStringList list;
    for (const std::string& argument : render.arguments)
        list << toQString(argument);
    timeout_->start();
    processtree::prepare(process_);
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
        fs::remove(ampTemp_, ec);
        start(std::move(next));
        return;
    }
    if (cancelled_) {
        cancelled_ = false;
        timedOut_ = false;
        running_ = false;
        fs::remove(wavTemp_, ec);
        fs::remove(ampTemp_, ec);
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
    } else {
        const fs::path& written = renderingAmp_ ? ampTemp_ : wavTemp_;
        if (!fs::is_regular_file(written, ec) || fs::file_size(written, ec) <= 44)
            failure.message = tr("fluidsynth wrote no audio.");
    }
    if (!failure.message.isEmpty()) {
        fs::remove(wavTemp_, ec);
        fs::remove(ampTemp_, ec);
        finishWith(failure);
        return;
    }
    if (!playedAmpMidi_.empty() && !renderingAmp_) {
        // The other staves are done: the amp staves are rendered on their own.
        renderingAmp_ = true;
        startRender(playedAmpMidi_, ampTemp_);
        return;
    }
    postProcess();
}

// Applies the effect and cuts the silence the soundfont's long release leaves behind the music (in
// that order: the effect lifts the quiet tail, a cut before it would end in a click), on
// a worker thread: both map the whole file, which can be tens of megabytes.
void PlaybackRunner::postProcess() {
    const fs::path temp = wavTemp_;
    const fs::path ampTemp = renderingAmp_ ? ampTemp_ : fs::path();
    const bool amp = current_.amp;
    auto ok = std::make_shared<std::atomic<bool>>(false);
    worker_ = QThread::create([temp, ampTemp, amp, ok] {
        bool done = !amp || ampWavFile(temp);
        if (done && !ampTemp.empty()) {
            // The amp staves go through the effect on their own, then join the others.
            done = ampWavFile(ampTemp) && mixWavFiles(temp, ampTemp);
            std::error_code ec;
            fs::remove(ampTemp, ec);
        }
        ok->store(done && trimWavFile(temp));
    });
    connect(worker_, &QThread::finished, this, [this, ok] {
        worker_->deleteLater();
        worker_ = nullptr;
        onPostProcessed(ok->load());
    });
    worker_->start();
}

void PlaybackRunner::onPostProcessed(bool ok) {
    std::error_code ec;
    PlaybackResult failure;
    if (pending_) {
        auto next = std::move(*pending_);
        pending_.reset();
        // The work is done: keep it in the cache (a request for the same MIDI is a hit then).
        if (ok)
            fs::rename(wavTemp_, wav_, ec);
        if (!ok || ec)
            fs::remove(wavTemp_, ec);
        else
            trimCache();
        fs::remove(ampTemp_, ec);
        start(std::move(next));
        return;
    }
    if (cancelled_) {
        cancelled_ = false;
        running_ = false;
        fs::remove(wavTemp_, ec);
        fs::remove(ampTemp_, ec);
        return;
    }
    // A failed trim leaves the temp file in an unknown state: it never reaches the cache.
    if (!ok) {
        fs::remove(wavTemp_, ec);
        fs::remove(ampTemp_, ec);
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
    // The rewritten MIDI files (<key>.mid) of the same keys.
    const QFileInfoList midis =
        dir.entryInfoList({QStringLiteral("*.mid")}, QDir::Files, QDir::Time);
    for (int i = keptWavFiles; i < midis.size(); ++i)
        QFile::remove(midis[i].absoluteFilePath());
}
