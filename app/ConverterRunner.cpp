#include "ConverterRunner.hpp"

#include "ProcessTree.hpp"

#include "convert.hpp"

#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>
#include <QPointer>
#include <memory>

namespace {
// Measuring (shrink --measure, join) builds the song with lualatex: slow on a cold cache.
constexpr int killGraceMs = 500; // between SIGTERM and SIGKILL of a cancelled run

// Stops the converter and what it started (a measuring shrink or join runs python3 checkpaper.py,
// lualatex and lilypond). On Unix the converter leads its own process group (see start()):
// SIGTERM to the group, a short grace period, then SIGKILL. Waits for the converter to exit.
void killTree(QProcess* process) {
    if (process->state() == QProcess::NotRunning)
        return;
    processtree::terminate(process);
    const bool exited = process->waitForFinished(500);
    processtree::kill(process); // what is left of the tree
    if (!exited)
        process->waitForFinished(2000);
}
} // namespace

ConverterRunner::ConverterRunner(QObject* parent) : QObject(parent), watchdog_(new QTimer(this)) {
    watchdog_->setSingleShot(true);
    connect(watchdog_, &QTimer::timeout, this, [this] {
        if (process_) {
            timedOut_ = true;
            killTree(process_);
        }
    });
}

ConverterRunner::~ConverterRunner() {
    release();
    // Cancelled runs that are still dying: no process may outlive the app.
    for (const auto& dying : dying_) {
        if (dying)
            killTree(dying); // the QProcess itself is deleted with its parent
    }
}

void ConverterRunner::release() {
    busy_ = false;
    ++generation_;
    watchdog_->stop();
    if (process_) {
        process_->disconnect(this);
        killTree(process_);
        delete process_;
        process_ = nullptr;
    }
    dir_.reset();
}

// Cancels without waiting: the result is dropped (generation_), the converter and its process
// group get SIGTERM now and SIGKILL after a grace period, and the temp dir (its working
// directory) stays until the process is gone.
void ConverterRunner::cancel() {
    busy_ = false;
    ++generation_;
    watchdog_->stop();
    QProcess* process = process_;
    process_ = nullptr;
    std::shared_ptr<QTemporaryDir> dir(std::move(dir_));
    if (!process)
        return;
    process->disconnect(this);
    if (process->state() == QProcess::NotRunning) {
        delete process;
        return;
    }
    processtree::terminate(process);
    dying_.append(process);
    connect(process, &QProcess::finished, this, [this, process, dir] {
        dying_.removeOne(process);
        process->deleteLater();
    });
    // A QProcess that fails to start or dies badly: the same cleanup.
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            dying_.removeOne(process);
            process->deleteLater();
        }
    });
    QTimer::singleShot(killGraceMs, process, [process] {
        if (process->state() == QProcess::NotRunning)
            return;
        processtree::kill(process);
    });
}

bool ConverterRunner::start(const CommandFactory& makeCommand, const std::string& content,
                            const QString& stem) {
    return start(makeCommand, content, stem, Options{});
}

bool ConverterRunner::start(const CommandFactory& makeCommand, const std::string& content,
                            const QString& stem, const Options& options) {
    if (busy_)
        return false;
    options_ = options;
    watchdog_->setInterval(options.watchdogSeconds * 1000);
    busy_ = true;
    out_.clear();
    err_.clear();
    timedOut_ = false;

    dir_ = std::make_unique<QTemporaryDir>();
    if (!dir_->isValid()) {
        failLater(tr("Cannot create a temporary directory: %1").arg(dir_->errorString()));
        return true;
    }
    // ":<digits>: " in the file name would confuse the parsing of the converter's messages.
    QString safeStem = stem.isEmpty() ? QStringLiteral("song") : stem;
    safeStem.replace(QLatin1Char(':'), QLatin1Char('_'));
    const std::filesystem::path file = toPath(dir_->filePath(safeStem + QStringLiteral(".tex")));
    {
        QFile out(toQString(file));
        if (!out.open(QIODevice::WriteOnly) ||
            out.write(content.data(), static_cast<qint64>(content.size())) !=
                static_cast<qint64>(content.size())) {
            failLater(tr("Cannot write %1: %2").arg(toQString(file), out.errorString()));
            return true;
        }
    }

    const zub::converter::Command command = makeCommand(file);
    QStringList arguments;
    for (const std::string& argument : command.arguments)
        arguments << toQString(argument);

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setWorkingDirectory(dir_->path());
    // The converter's measure root (with symlinks into the songbook) goes under the temp dir and
    // is removed with it; QTemporaryDir does not follow symlinks.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("TMPDIR"), dir_->path());
    process_->setProcessEnvironment(environment);
    processtree::prepare(process_);
    connect(process_, &QProcess::readyReadStandardOutput, this,
            [this] { out_ += process_->readAllStandardOutput(); });
    connect(process_, &QProcess::readyReadStandardError, this,
            [this] { err_ += process_->readAllStandardError(); });
    connect(process_, &QProcess::finished, this, [this] { onFinished(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Other errors (crash, kill) are followed by finished(); FailedToStart is not.
        if (error != QProcess::FailedToStart)
            return;
        // Through the event loop: on Windows QProcess reports it inside start(), and a slot of
        // finished() may start the next run (Check Songs would recurse once per song).
        failLater(tr("Cannot run chords_converter: %1").arg(process_->errorString()));
    });
    watchdog_->start();
    process_->start(toQString(command.program), arguments);
    return true;
}

void ConverterRunner::onFinished() {
    out_ += process_->readAllStandardOutput();
    err_ += process_->readAllStandardError();
    if (timedOut_) {
        finish(std::unexpected(
            tr("chords_converter did not finish within %1 s and was stopped.")
                .arg(options_.watchdogSeconds)));
        return;
    }
    if (process_->exitStatus() == QProcess::CrashExit) {
        finish(std::unexpected(tr("chords_converter crashed.")));
        return;
    }
    ConverterOutput output{process_->exitCode(), out_.toStdString(), err_.toStdString(), {}};
    if (!options_.readBack.isEmpty() && dir_) {
        QFile file(dir_->filePath(options_.readBack));
        if (file.open(QIODevice::ReadOnly))
            output.file = file.readAll().toStdString();
    }
    finish(std::move(output));
}

void ConverterRunner::failLater(const QString& message) {
    const quint64 generation = generation_;
    QTimer::singleShot(0, this, [this, generation, message] {
        if (generation == generation_)
            finish(std::unexpected(message));
    });
}

void ConverterRunner::finish(std::expected<ConverterOutput, QString> result) {
    busy_ = false;
    // Clean up before emitting: a slot may start the next run.
    if (process_) {
        process_->disconnect(this);
        process_->deleteLater();
        process_ = nullptr;
    }
    watchdog_->stop();
    dir_.reset();
    emit finished(result);
}
