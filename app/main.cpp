#include "MainWindow.hpp"

#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <QByteArrayView>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>
#include <QString>
#include <QTimer>
#include <zub/core/version.hpp>

namespace {

// ui/language: "" or missing = the system language, else "cs" or "en". The translators are
// installed on the application, so they must outlive it: the caller owns them.
void installTranslators(QApplication& app, QTranslator& appTranslator, QTranslator& qtTranslator) {
    const QString language = QSettings().value(QStringLiteral("ui/language")).toString();
    const QLocale locale = language.isEmpty() ? QLocale::system() : QLocale(language);
    // English too: the .ts for it holds the plural forms ("1 song", "2 songs").
    if (appTranslator.load(locale, QStringLiteral("zpevnikar"), QStringLiteral("_"),
                           QStringLiteral(":/i18n")))
        app.installTranslator(&appTranslator);
    if (qtTranslator.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);
}

/// The app icon in all embedded sizes (an installed theme icon of the same name would be the same).
QIcon appIcon() {
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256, 512})
        icon.addFile(QStringLiteral(":/icons/zpevnikar-%1.png").arg(size), QSize(size, size));
    return icon;
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    const auto version = zub::version();
    // Organization + application name decide the QSettings and QStandardPaths locations.
    QApplication::setOrganizationName(QStringLiteral("Modrozoubek"));
    QApplication::setApplicationName(QStringLiteral("ZubuvZpevnikar"));
    QApplication::setApplicationDisplayName(QStringLiteral("Zpěvníkář"));
    QApplication::setDesktopFileName(QStringLiteral("zpevnikar"));
    QApplication::setApplicationVersion(QString::fromUtf8(QByteArrayView{version}));
    QApplication::setWindowIcon(appIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Editor and assembler for the ZubuvZpevnik songbook"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption songbookOption(QStringLiteral("songbook"),
                                            QStringLiteral("Path to the ZubuvZpevnik checkout."),
                                            QStringLiteral("dir"));
    parser.addOption(songbookOption);
    parser.process(app);
    QTranslator appTranslator;
    QTranslator qtTranslator;
    installTranslators(app, appTranslator, qtTranslator);

    MainWindow window;
    window.show();
    // After show(), so a folder dialog opens on top of the main window.
    QTimer::singleShot(0, &window, [&window, path = parser.value(songbookOption)] {
        window.openInitialSongbook(path);
    });
    return QApplication::exec();
}
