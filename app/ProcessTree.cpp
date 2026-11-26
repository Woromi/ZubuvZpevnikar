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
#include <vector>
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
    ~Job() override {
        close();
        if (attributesReady_)
            DeleteProcThreadAttributeList(attributeList());
    }
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    // Makes CreateProcess start the program inside the job (Windows 10: a job list in the
    // extended startup info), so nothing it starts at once can escape it. False when that could
    // not be set up: adopt() after the start is the fallback.
    bool attachAtCreation(QProcess::CreateProcessArguments* args) {
        // PROC_THREAD_ATTRIBUTE_JOB_LIST needs _WIN32_WINNT >= 0x0A00 (Qt 6 needs Windows 10):
        // no #ifdef, so a lower target fails to compile instead of quietly falling back.
        if (!handle_ || !args->startupInfo)
            return false;
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        if (size == 0)
            return false;
        attributes_.assign(size, 0);
        if (!InitializeProcThreadAttributeList(attributeList(), 1, 0, &size))
            return false;
        attributesReady_ = true;
        // handle_ and the list live in this object until after CreateProcess has returned.
        if (!UpdateProcThreadAttribute(attributeList(), 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                       &handle_, sizeof(handle_), nullptr, nullptr))
            return false;
        startup_ = {};
        startup_.StartupInfo = *args->startupInfo; // Qt's pipes and window settings
        startup_.StartupInfo.cb = sizeof(startup_);
        startup_.lpAttributeList = attributeList();
        args->startupInfo = &startup_.StartupInfo;
        args->flags |= EXTENDED_STARTUPINFO_PRESENT;
        attached_ = true;
        return true;
    }
    bool attached() const { return attached_; }
    HANDLE handle() const { return handle_; }

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
    LPPROC_THREAD_ATTRIBUTE_LIST attributeList() {
        return reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes_.data());
    }

    HANDLE handle_ = nullptr;
    std::vector<char> attributes_;
    bool attributesReady_ = false;
    STARTUPINFOEXW startup_{};
    bool attached_ = false;
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
#ifdef Q_OS_WIN
const char* const noJobListProperty = "zubNoJobList";
#endif

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
#ifdef Q_OS_WIN
    // Each start gets a new job, which the program is created in (what an earlier run left in
    // the old one is killed as that job is closed).
    // When CreateProcess refused the job list (an app running in a job that allows no nested
    // ones), the next start goes without it: adopt() after the start, as before.
    process->setCreateProcessArgumentsModifier([process](QProcess::CreateProcessArguments* args) {
        delete jobOf(process);
        auto* job = new Job(process);
        if (!process->property(noJobListProperty).toBool())
            job->attachAtCreation(args);
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        if (Job* job = jobOf(process); job && job->attached()) {
            qWarning("processtree: the program could not be started inside a job (error %lu); "
                     "the next start goes without one",
                     GetLastError());
            process->setProperty(noJobListProperty, true);
        }
    });
#endif
    // On every start: the new run's group or job (a reused QProcess drops the old one).
    QObject::connect(process, &QProcess::started, process, [process] {
#ifdef Q_OS_UNIX
        process->setProperty(groupProperty, process->processId());
#endif
#ifdef Q_OS_WIN
        // Created in its job; else (no job list) put in now, which a child it starts at once
        // may escape.
        Job* job = jobOf(process);
        if (!job)
            job = new Job(process);
        if (!job->attached())
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

bool createdInJob(QProcess* process) {
#ifdef Q_OS_WIN
    const Job* job = jobOf(process);
    if (!job || !job->attached() || !job->handle())
        return false;
    HANDLE child = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(process->processId()));
    if (!child)
        return false;
    BOOL inJob = FALSE;
    const bool ok = IsProcessInJob(child, job->handle(), &inJob) && inJob;
    CloseHandle(child);
    return ok;
#else
    (void)process;
    return false;
#endif
}

} // namespace processtree
