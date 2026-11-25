#include "ProcessTree.hpp"

#include <QObject>
#include <QtLogging>
#include <QProcess>
#include <QTimer>
#include <QVariant>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace processtree {

namespace {

#ifdef Q_OS_UNIX
// The group's id: the leader's pid, kept after the leader has exited (processId() is 0 then).
const char* const groupProperty = "zubProcessGroup";

// The group, also before started() was handled (the child made it before exec).
qint64 groupOf(QProcess* process) {
    const QVariant group = process->property(groupProperty);
    if (group.isValid())
        return group.toLongLong();
    return process->state() != QProcess::NotRunning ? process->processId() : 0;
}

// False when there is no such group (yet): the child may not have called setpgid.
bool signalGroup(QProcess* process, int signal) {
    const qint64 group = groupOf(process);
    return group > 0 && ::kill(-static_cast<pid_t>(group), signal) == 0;
}
#endif

#ifdef Q_OS_WIN
// Owns the job of the latest start; closing it kills what is still in it.
class Job : public QObject {
public:
    explicit Job(QProcess* process) : QObject(process) {
        handle_ = CreateJobObjectW(nullptr, nullptr);
        if (!handle_)
            return;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(handle_, JobObjectExtendedLimitInformation, &limits,
                                sizeof(limits));
    }
    ~Job() override { close(); }
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    void adopt(qint64 pid) {
        if (!handle_ || pid <= 0)
            return;
        if (HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                       static_cast<DWORD>(pid))) {
            if (!AssignProcessToJobObject(handle_, child))
                qWarning("processtree: the process could not be put into a job (error %lu): "
                         "what it starts may outlive a cancel",
                         GetLastError());
            CloseHandle(child);
        }
    }
    void terminate() {
        if (handle_)
            TerminateJobObject(handle_, 1);
    }
    void close() {
        if (handle_) {
            CloseHandle(handle_); // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            handle_ = nullptr;
        }
    }

private:
    HANDLE handle_ = nullptr;
};

// (Not findChild: Job has no Q_OBJECT, and QProcess has children of Qt's own.)
Job* jobOf(QProcess* process) {
    for (QObject* child : process->children())
        if (auto* job = dynamic_cast<Job*>(child))
            return job;
    return nullptr;
}
#endif

const char* const preparedProperty = "zubProcessTree";

} // namespace

void prepare(QProcess* process) {
#ifdef Q_OS_UNIX
    process->setChildProcessModifier([] { ::setpgid(0, 0); });
    // A new run: the last one's group is not this one's (until started, groupOf uses the pid).
    process->setProperty(groupProperty, QVariant());
#endif
    if (process->property(preparedProperty).toBool())
        return;
    process->setProperty(preparedProperty, true);
    // On every start: the new run's group or job (a reused QProcess drops the old one).
    QObject::connect(process, &QProcess::started, process, [process] {
#ifdef Q_OS_UNIX
        process->setProperty(groupProperty, process->processId());
#endif
#ifdef Q_OS_WIN
        delete jobOf(process); // what an earlier run left
        auto* job = new Job(process);
        // The program itself is put in right after it started, before it starts others.
        job->adopt(process->processId());
#endif
    });
#ifdef Q_OS_UNIX
    // The group's children may outlive the leader for a moment (the callers kill them then), but
    // its id must not be signalled once it could be another process's: forgotten after a while.
    QObject::connect(process, &QProcess::finished, process, [process] {
        const QVariant group = process->property(groupProperty);
        QTimer::singleShot(5000, process, [process, group] {
            if (process->property(groupProperty) == group)
                process->setProperty(groupProperty, QVariant());
        });
    });
#endif
}

void terminate(QProcess* process) {
#ifdef Q_OS_UNIX
    if (!signalGroup(process, SIGTERM) && process->state() != QProcess::NotRunning)
        process->terminate();
#else
    kill(process);
#endif
}

void kill(QProcess* process) {
#ifdef Q_OS_UNIX
    signalGroup(process, SIGKILL);
#endif
#ifdef Q_OS_WIN
    if (Job* job = jobOf(process))
        job->terminate();
#endif
    if (process->state() != QProcess::NotRunning)
        process->kill();
}

} // namespace processtree
