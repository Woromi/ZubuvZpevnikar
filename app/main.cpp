#include "DesktopIntegration.hpp"
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
#include <QStatusBar>
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
        QStringLiteral("Song editor and songbook assembler for a ZubuvZpevnik song library"));
    parser.addHelpOption();
    parser.addVersionOption();
    // --songbook: the option's older name, still read.
    const QCommandLineOption songbookOption(
        QStringList{QStringLiteral("library"), QStringLiteral("songbook")},
        QStringLiteral("Path to the song library (a ZubuvZpevnik checkout)."), QStringLiteral("dir"));
    parser.addOption(songbookOption);
    parser.process(app);
    QTranslator appTranslator;
    QTranslator qtTranslator;
    installTranslators(app, appTranslator, qtTranslator);

    MainWindow window;
    window.show();
    // Run as an AppImage: into the application menu, as an installed app is.
    if (const QString image = desktopintegration::appImage();
        !image.isEmpty() && desktopintegration::enabled() &&
        desktopintegration::install(image, desktopintegration::bundled()) ==
            desktopintegration::Result::Added)
        window.statusBar()->showMessage(
            QCoreApplication::translate("main",
                                        "Zpěvníkář is in the application menu now (Settings can "
                                        "remove it)."),
            15000);
    // After show(), so a folder dialog opens on top of the main window.
    QTimer::singleShot(0, &window, [&window, path = parser.value(songbookOption)] {
        window.openInitialSongbook(path);
    });
    return QApplication::exec();
}
