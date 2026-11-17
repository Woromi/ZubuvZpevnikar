// Test runner of the app tests: Catch2 with a QApplication on the offscreen platform and every
// per-user location (settings, cache, data) in a temporary directory, so a test never touches the
// real app's settings or songbook.
#include <QApplication>
#include <QByteArray>
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
    if (!wantsTools)
        qputenv("PATH", "/nonexistent");

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

    // Qt must not see (and complain about) Catch's command line.
    int qtArgc = 1;
    char* qtArgv[] = {argv[0], nullptr};
    QApplication app(qtArgc, qtArgv);
    QApplication::setOrganizationName(QStringLiteral("ZubuvZpevnikarTests"));
    QApplication::setApplicationName(QStringLiteral("ZubuvZpevnikarTests"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    return Catch::Session().run(argc, argv);
}
