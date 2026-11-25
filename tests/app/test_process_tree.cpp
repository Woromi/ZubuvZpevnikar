// processtree: kill() ends a program together with what it started.
#include "ProcessTree.hpp"
#include "Support.hpp"

#include <QFile>
#include <QProcess>
#include <QStringList>
#include <catch2/catch_test_macros.hpp>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#endif

namespace {

// Whether the process `pid` still runs.
bool alive(qint64 pid) {
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!handle)
        return false;
    DWORD code = 0;
    const bool running = GetExitCodeProcess(handle, &code) && code == STILL_ACTIVE;
    CloseHandle(handle);
    return running;
#else
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM)
        return false;
    // A killed orphan stays a zombie where nothing reaps it (a container's PID 1, as in CI): it
    // has ended all the same.
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (stat.open(QIODevice::ReadOnly)) {
        const QByteArray line = stat.readAll();
        const qsizetype close = line.lastIndexOf(')'); // after the name, which may hold spaces
        if (close >= 0 && close + 2 < line.size() && line.at(close + 2) == 'Z')
            return false;
    }
    return true;
#endif
}

} // namespace

TEST_CASE("processtree: kill ends the program and what it started", "[app][processtree]") {
    QProcess process;
    processtree::prepare(&process);
    // A program that starts a long-running child, prints the child's id and waits.
#ifdef Q_OS_WIN
    const QString shell = qEnvironmentVariable("SystemRoot") +
                          QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
    process.start(shell, {QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
                          QStringLiteral("$p = Start-Process -PassThru -WindowStyle Hidden -FilePath "
                                         "$env:SystemRoot\\System32\\PING.EXE -ArgumentList "
                                         "'-n','60','127.0.0.1'; "
                                         "Write-Output $p.Id; Start-Sleep 60")});
#else
    process.start(QStringLiteral("/bin/sh"),
                  {QStringLiteral("-c"), QStringLiteral("/bin/sleep 60 & echo $!; wait")}); // PATH is empty here
#endif
    REQUIRE(process.waitForStarted(10000));
    REQUIRE(apptest::waitUntil([&] { return process.canReadLine(); }, 20000));
    const qint64 child = QString::fromLatin1(process.readLine()).trimmed().toLongLong();
    REQUIRE(child > 0);
    CHECK(alive(child));

    processtree::kill(&process);
    CHECK(process.waitForFinished(5000));
    // The child is gone too (not just its parent).
    CHECK(apptest::waitUntil([&] { return !alive(child); }, 5000));
}
