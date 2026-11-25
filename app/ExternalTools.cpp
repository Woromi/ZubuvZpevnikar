#include "ExternalTools.hpp"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace externaltools {

#ifdef Q_OS_WIN
namespace {

// A Git for Windows folder: bin\bash.exe with the rest of Git next to it (not any bin\bash.exe
// above some git.exe shim).
QString gitBash(const QString& gitRoot) {
    const QDir root(gitRoot);
    const QString bash = root.filePath(QStringLiteral("bin/bash.exe"));
    if (!QFileInfo(bash).isFile())
        return {};
    if (!QFileInfo(root.filePath(QStringLiteral("cmd/git.exe"))).isFile() &&
        !QFileInfo(root.filePath(QStringLiteral("usr/bin/bash.exe"))).isFile())
        return {};
    return QDir::cleanPath(bash);
}

// Runs as Python 3 (the Microsoft Store stubs in WindowsApps do not: they offer the Store).
bool isPython3(const QString& program, const QStringList& leading) {
    QProcess probe;
    probe.start(program, leading + QStringList{QStringLiteral("-c"),
                                               QStringLiteral("import sys; sys.exit(sys.version_info[0] != 3)")});
    if (!probe.waitForFinished(5000)) {
        probe.kill();
        probe.waitForFinished(1000);
        return false;
    }
    return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

} // namespace
#endif

QString bash() {
#ifdef Q_OS_WIN
    QStringList roots;
    // Where the Git for Windows installer says it is, then the usual folders, then above the
    // git on PATH.
    for (const char* key : {"HKEY_LOCAL_MACHINE\\SOFTWARE\\GitForWindows",
                            "HKEY_CURRENT_USER\\SOFTWARE\\GitForWindows"}) {
        const QString path =
            QSettings(QString::fromLatin1(key), QSettings::NativeFormat).value(QStringLiteral("InstallPath")).toString();
        if (!path.isEmpty())
            roots << path;
    }
    for (const char* variable : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"})
        if (const QString root = qEnvironmentVariable(variable); !root.isEmpty())
            roots << root + QStringLiteral("/Git");
    if (const QString local = qEnvironmentVariable("LOCALAPPDATA"); !local.isEmpty())
        roots << local + QStringLiteral("/Programs/Git");
    if (const QString git = QStandardPaths::findExecutable(QStringLiteral("git")); !git.isEmpty()) {
        QDir dir = QFileInfo(git).absoluteDir();
        for (int up = 0; up < 3 && dir.cdUp(); ++up)
            roots << dir.absolutePath();
    }
    for (const QString& root : roots)
        if (QString found = gitBash(root); !found.isEmpty())
            return found;
    return {};
#else
    return QStringLiteral("bash");
#endif
}

QString python() {
#ifdef Q_OS_WIN
    // Asked once per run (each probe starts Python).
    static const QString found = [] {
        for (const char* name : {"python3", "python", "py"}) {
            const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
            if (!path.isEmpty() && isPython3(path, {}))
                return path;
        }
        return QString();
    }();
    return found;
#else
    return QStringLiteral("python3");
#endif
}

} // namespace externaltools
