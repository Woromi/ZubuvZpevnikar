// Test runner of the app tests: Catch2 with a QApplication on the offscreen platform and every
// per-user location (settings, cache, data) in a temporary directory, so a test never touches the
// real app's settings or songbook.
#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QtLogging>
#include <cstdio>
#include <QTemporaryDir>
#include <catch2/catch_session.hpp>

namespace {
// The offscreen platform warns about size hints on every window: not worth the noise.
void quietHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (message.contains(QLatin1String("propagateSizeHints")))
        return;
    std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
}
} // namespace

int main(int argc, char** argv) {
    qInstallMessageHandler(quietHandler);
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // Default runs use no external tool (lualatex, lilypond, fluidsynth, python3, the converter):
    // what the app starts is looked up on PATH, so PATH is emptied. Only the hidden [.tools] cases
    // (selected by a command line argument naming "tools") get the real PATH.
    bool wantsTools = false;
    for (int i = 1; i < argc; ++i)
        wantsTools = wantsTools || QByteArray(argv[i]).contains("tools");
    if (!wantsTools) {
        qputenv("PATH", "/nonexistent");
        qunsetenv("ZUB_CONVERTER"); // a developer's converter build, found without PATH
    }

    // Silent: playback and fret sounds go to a sound server that does not exist (Qt Multimedia
    // talks to PulseAudio/PipeWire), never to the speakers. ZUB_TEST_SOUND=1 lets them be heard.
    if (qEnvironmentVariable("ZUB_TEST_SOUND") != QLatin1String("1")) {
        qputenv("PULSE_SERVER", "unix:/nonexistent/zub-test-no-sound");
        qputenv("PIPEWIRE_REMOTE", "/nonexistent/zub-test-no-sound");
        qputenv("ALSA_CONFIG_PATH", "/nonexistent/zub-test-no-sound");
    }

    // Alive for the whole run; every test process (ctest runs one test case per process) gets its own.
    QTemporaryDir state;
    if (!state.isValid())
        return 2;
    const QByteArray dir = state.path().toUtf8();
    // Qt's test mode keeps its files under $HOME/.qttest, the XDG variables cover the rest.
    qputenv("HOME", dir);
    qputenv("XDG_CONFIG_HOME", dir + "/config");
    qputenv("XDG_CACHE_HOME", dir + "/cache");
    qputenv("XDG_DATA_HOME", dir + "/data");
    QStandardPaths::setTestModeEnabled(true);
#ifdef Q_OS_WIN
    // Windows: QSettings would use the registry and QStandardPaths ignores the variables above
    // (test mode puts the folders under %LOCALAPPDATA%/qttest, shared by the test processes).
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QString::fromUtf8(dir + "/config"));
#endif

    // Qt must not see (and complain about) Catch's command line.
    int qtArgc = 1;
    char* qtArgv[] = {argv[0], nullptr};
    QApplication app(qtArgc, qtArgv);
    QApplication::setOrganizationName(QStringLiteral("ZubuvZpevnikarTests"));
    QApplication::setApplicationName(QStringLiteral("ZubuvZpevnikarTests"));
    QApplication::setStyle(QStringLiteral("Fusion"));
#ifdef Q_OS_WIN
    // Every test starts with an empty cache and data folder there too (only Qt's test-mode
    // folders of this test program).
    for (const auto location : {QStandardPaths::AppDataLocation, QStandardPaths::CacheLocation,
                                QStandardPaths::AppConfigLocation}) {
        const QString path = QStandardPaths::writableLocation(location);
        if (path.contains(QStringLiteral("/qttest/")) &&
            path.contains(QStringLiteral("ZubuvZpevnikarTests")))
            QDir(path).removeRecursively();
    }
#endif

    return Catch::Session().run(argc, argv);
}
