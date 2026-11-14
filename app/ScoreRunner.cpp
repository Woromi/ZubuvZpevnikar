#include "ScoreRunner.hpp"

#include "convert.hpp"

#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <system_error>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace {
constexpr int timeoutSeconds = 60;
}

ScoreRunner::ScoreRunner(QObject* parent)
    : QObject(parent), process_(new QProcess(this)), timeout_(new QTimer(this)) {
    timeout_->setSingleShot(true);
    timeout_->setInterval(timeoutSeconds * 1000);
    process_->setProcessChannelMode(QProcess::MergedChannels);

    connect(process_, &QProcess::finished, this, [this] { onProcessFinished(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Other errors (crash, timeout) are followed by finished(); FailedToStart is not.
        if (error != QProcess::FailedToStart)
            return;
        timeout_->stop();
        if (pending_) {
            command_.reset();
            startPending();
            return;
        }
        if (cancelled_) {
            cancelled_ = false;
            timedOut_ = false;
            command_.reset();
            return;
        }
        ScoreResult result;
        result.inputName = inputName_;
        result.elapsedMs = clock_.isValid() ? clock_.elapsed() : 0;
        result.message =
            tr("Cannot start lilypond: %1. Is LilyPond installed?").arg(process_->errorString());
        timedOut_ = false;
        command_.reset();
        emit finished(result);
    });
    connect(timeout_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        killTree();
    });
}

ScoreRunner::~ScoreRunner() {
    if (process_->state() != QProcess::NotRunning) {
        process_->blockSignals(true);
        killTree();
        process_->waitForFinished(2000);
    }
}

bool ScoreRunner::isRunning() const {
    return process_->state() != QProcess::NotRunning;
}

void ScoreRunner::request(zub::score::Request request) {
    if (isRunning()) {
        // The work dir files must not be rewritten while the old build runs; the newest
        // request starts once that process has exited.
        pending_ = std::move(request);
        emit started();
        killTree();
        return;
    }
    start(std::move(request), true);
}

void ScoreRunner::cancel() {
    pending_.reset();
    if (isRunning()) {
        cancelled_ = true;
        killTree();
    }
}

void ScoreRunner::killTree() {
#ifdef Q_OS_UNIX
    // The build runs in its own process group, so anything lilypond started dies with it.
    if (const qint64 pid = process_->processId(); pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
    process_->kill();
}

void ScoreRunner::startPending() {
    auto next = std::move(*pending_);
    pending_.reset();
    cancelled_ = false;
    timedOut_ = false;
    start(std::move(next), false);
}

void ScoreRunner::start(zub::score::Request request, bool announce) {
    inputName_ = request.inputName;
    auto prepared = zub::score::prepare(request);
    if (!prepared) {
        ScoreResult result;
        result.inputName = inputName_;
        result.message = toQString(prepared.error());
        emit finished(result);
        return;
    }
    command_ = std::move(*prepared);
    const zub::score::Command& cmd = *command_;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const auto& [name, value] : cmd.environment)
        env.insert(toQString(name), toQString(value));
    process_->setProcessEnvironment(env);
    process_->setWorkingDirectory(toQString(cmd.workingDirectory));

    QStringList arguments;
    for (const std::string& argument : cmd.arguments)
        arguments << toQString(argument);

    timedOut_ = false;
    clock_.start();
    timeout_->start();
    if (announce)
        emit started();
#ifdef Q_OS_UNIX
    process_->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
    process_->start(toQString(cmd.program), arguments);
}

void ScoreRunner::onProcessFinished() {
    timeout_->stop();
    if (pending_) {
        command_.reset();
        startPending();
        return;
    }
    if (cancelled_) {
        cancelled_ = false;
        timedOut_ = false;
        command_.reset();
        return;
    }

    ScoreResult result;
    result.inputName = inputName_;
    result.elapsedMs = clock_.elapsed();
    const zub::score::Command cmd = command_.value_or(zub::score::Command{});
    result.scoreCopy = cmd.scoreCopy;

    const QByteArray output = process_->readAll();
    result.log = QString::fromUtf8(output);

    if (timedOut_) {
        result.message = tr("The build took longer than %1 s and was stopped.").arg(timeoutSeconds);
    } else if (process_->exitStatus() == QProcess::CrashExit) {
        result.message = tr("lilypond crashed.");
    }

    const bool cleanExit = !timedOut_ && process_->exitStatus() == QProcess::NormalExit;
    result.success = cleanExit && process_->exitCode() == 0 && !cmd.svg.empty();
    std::vector<zub::score::Error> found;
    if (!result.success) {
        found = zub::score::errors(output.toStdString());
        const std::size_t all = found.size();
        found = zub::score::withoutGeneratedErrors(std::move(found), cmd);
        // Every error is in the appended playback score: the engraved SVG was still written
        // (prepare removed the old one), so the score is shown; there is just no MIDI to play.
        if (cleanExit && all > 0 && found.empty() && !cmd.svg.empty())
            result.success = true;
        else
            result.errors = std::move(found);
    }
    if (result.success) {
        QFile file(toQString(cmd.svg));
        if (file.open(QIODevice::ReadOnly))
            result.svg = file.readAll();
        if (result.svg.isEmpty())
            result.success = false;
        std::error_code ec;
        if (result.success && process_->exitCode() == 0 && !cmd.midi.empty() &&
            std::filesystem::is_regular_file(cmd.midi, ec))
            result.midi = cmd.midi;
    }
    if (!result.success) {
        result.svg.clear();
        if (result.errors.empty() && result.message.isEmpty())
            result.message = tr("The build failed; see the log.");
    }

    timedOut_ = false;
    command_.reset();
    emit finished(result);
}
