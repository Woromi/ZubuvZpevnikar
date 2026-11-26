#include "SongDatesRunner.hpp"

#include "ProcessTree.hpp"

#include "convert.hpp"

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStringList>

SongDatesRunner::SongDatesRunner(QObject* parent) : QObject(parent) {}

SongDatesRunner::~SongDatesRunner() {
    release();
    // Killed processes still exiting: a QProcess must not be destroyed while it runs.
    for (QProcess* process : findChildren<QProcess*>(Qt::FindDirectChildrenOnly)) {
        if (process->state() != QProcess::NotRunning) {
            processtree::kill(process);
            process->waitForFinished(1000);
        }
    }
}

void SongDatesRunner::release() {
    if (!process_)
        return;
    QProcess* process = process_;
    process_ = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning)
        processtree::kill(process);
    // Deleted once it is gone (a process still starting or exiting must not be destroyed).
    if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
    } else {
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        connect(process, &QProcess::errorOccurred, process, &QObject::deleteLater);
    }
}

void SongDatesRunner::cancel() {
    release();
    dates_.clear();
}

void SongDatesRunner::start(const std::filesystem::path& root) {
    cancel();
    root_ = root;
    run(Stage::Log);
}

void SongDatesRunner::run(Stage stage) {
    const auto command = stage == Stage::Log ? zub::songdates::logCommand(root_)
                                             : zub::songdates::statusCommand(root_);
    auto* process = new QProcess(this);
    process_ = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    // Started from a git hook or a shell with these set, git would read another repository
    // than -C names.
    for (const char* name : {"GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR",
                             "GIT_OBJECT_DIRECTORY"})
        environment.remove(QString::fromLatin1(name));
    process->setProcessEnvironment(environment);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process == process_ && error == QProcess::FailedToStart)
            fail();
    });
    connect(process, &QProcess::finished, this,
            [this, process, stage] { onFinished(process, stage); });
    QStringList arguments;
    for (const auto& argument : command.arguments)
        arguments << toQString(argument);
    processtree::prepare(process);
    process->start(toQString(command.program), arguments);
}

void SongDatesRunner::onFinished(QProcess* process, Stage stage) {
    if (process != process_)
        return;
    const bool ok = process->exitStatus() == QProcess::NormalExit && process->exitCode() == 0;
    const QByteArray out = process->readAllStandardOutput();
    if (!ok) {
        fail();
        return;
    }
    const std::string_view text(out.constData(), static_cast<std::size_t>(out.size()));
    if (stage == Stage::Log) {
        dates_ = zub::songdates::parseLog(text);
        // A shallow clone's oldest commit lists every file as added: those dates are the
        // clone's, not the songs'. Dropped (the edited dates stay).
        if (QFileInfo::exists(toQString(root_ / ".git" / "shallow"))) {
            std::optional<std::chrono::sys_seconds> oldest;
            for (const auto& [path, dates] : dates_)
                if (dates.added && (!oldest || *dates.added < *oldest))
                    oldest = dates.added;
            for (auto& [path, dates] : dates_)
                if (dates.added == oldest)
                    dates.added.reset();
        }
        release();
        run(Stage::Status);
        return;
    }
    zub::songdates::applyStatus(dates_, text);
    release();
    auto dates = std::move(dates_);
    dates_.clear();
    emit datesReady(root_, dates);
}

void SongDatesRunner::fail() {
    release();
    dates_.clear();
    emit failed(root_);
}
