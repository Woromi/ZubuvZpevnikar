#include "PreviewRunner.hpp"

#include "convert.hpp"

#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <zub/core/textfile.hpp>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace {
constexpr int timeoutSeconds = 120;
}

PreviewRunner::PreviewRunner(QObject* parent)
    : QObject(parent), process_(new QProcess(this)), timeout_(new QTimer(this)) {
    timeout_->setSingleShot(true);
    timeout_->setInterval(timeoutSeconds * 1000);
    // The log file is the source of truth; discard the output so no pipe buffer fills up.
    process_->setStandardOutputFile(QProcess::nullDevice());
    process_->setStandardErrorFile(QProcess::nullDevice());

    connect(process_, &QProcess::finished, this, [this] { onProcessFinished(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Other errors (crash, timeout) are followed by finished(); FailedToStart is not.
        if (error != QProcess::FailedToStart)
            return;
        timeout_->stop();
        if (pending_) {
            auto next = std::move(*pending_);
            pending_.reset();
            cancelled_ = false;
            timedOut_ = false;
            command_.reset();
            start(std::move(next), false);
            return;
        }
        if (cancelled_) {
            cancelled_ = false;
            timedOut_ = false;
            command_.reset();
            return;
        }
        PreviewResult result;
        result.inputName = inputName_;
        result.elapsedMs = clock_.isValid() ? clock_.elapsed() : 0;
        result.message = QStringLiteral("Cannot start lualatex: %1. Is TeX Live installed?")
                             .arg(process_->errorString());
        timedOut_ = false;
        command_.reset();
        emit finished(result);
    });
    connect(timeout_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        killTree();
    });
}

PreviewRunner::~PreviewRunner() {
    if (process_->state() != QProcess::NotRunning) {
        process_->blockSignals(true);
        killTree();
        process_->waitForFinished(2000);
    }
}

bool PreviewRunner::isRunning() const {
    return process_->state() != QProcess::NotRunning;
}

void PreviewRunner::request(zub::preview::Request request) {
    if (isRunning()) {
        // The work dir files must not be rewritten while the old build runs; the newest
        // request starts once that process has exited.
        pending_ = std::move(request);
        emit started(); // the pane must leave the old song's result right away
        killTree();
        return;
    }
    start(std::move(request), true);
}

void PreviewRunner::cancel() {
    pending_.reset();
    if (isRunning()) {
        cancelled_ = true;
        killTree();
    }
}

void PreviewRunner::killTree() {
#ifdef Q_OS_UNIX
    // lualatex -shell-escape starts sh/lilypond/gs children; the build runs in its own
    // process group so they die with it.
    if (const qint64 pid = process_->processId(); pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
    process_->kill();
}

void PreviewRunner::start(zub::preview::Request request, bool announce) {
    inputName_ = request.inputName;
    auto prepared = zub::preview::prepare(request);
    if (!prepared) {
        PreviewResult result;
        result.inputName = inputName_;
        result.message = toQString(prepared.error());
        emit finished(result);
        return;
    }
    command_ = std::move(*prepared);
    const zub::preview::Command& cmd = *command_;

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

void PreviewRunner::onProcessFinished() {
    timeout_->stop();
    if (pending_) {
        auto next = std::move(*pending_);
        pending_.reset();
        cancelled_ = false;
        timedOut_ = false;
        command_.reset();
        start(std::move(next), false);
        return;
    }
    if (cancelled_) {
        cancelled_ = false;
        timedOut_ = false;
        command_.reset();
        return;
    }

    PreviewResult result;
    result.inputName = inputName_;
    result.elapsedMs = clock_.elapsed();
    const zub::preview::Command cmd = command_.value_or(zub::preview::Command{});

    std::string logText; // raw bytes: the log may hold invalid UTF-8
    if (!cmd.log.empty()) {
        if (auto text = zub::readTextFile(cmd.log))
            logText = std::move(*text);
    }
    result.log = QString::fromUtf8(logText.data(), static_cast<qsizetype>(logText.size()));

    if (timedOut_) {
        result.message =
            QStringLiteral("The build took longer than %1 s and was stopped.").arg(timeoutSeconds);
    } else if (process_->exitStatus() == QProcess::CrashExit) {
        result.message = QStringLiteral("lualatex crashed.");
    }

    result.success = !timedOut_ && process_->exitStatus() == QProcess::NormalExit &&
                     process_->exitCode() == 0 && !cmd.pdf.empty();
    if (result.success) {
        QFile file(toQString(cmd.pdf));
        if (file.open(QIODevice::ReadOnly))
            result.pdf = file.readAll();
        else
            result.success = false;
        if (result.pdf.isEmpty())
            result.success = false;
    }

    if (!result.success) {
        result.pdf.clear();
        result.error = zub::preview::firstError(logText, cmd.songCopy);
        if (!result.error && result.message.isEmpty())
            result.message = QStringLiteral("The build failed; see the log.");
    }

    timedOut_ = false;
    command_.reset();
    emit finished(result);
}
