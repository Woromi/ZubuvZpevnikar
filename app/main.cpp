#include "MainWindow.hpp"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QByteArrayView>
#include <QCommandLineParser>
#include <QString>
#include <QTimer>
#include <zub/core/version.hpp>

namespace {

// Until 2026-11 the app was called ZubuvZpevnikUi, and its settings, songbooks and caches live
// under that name. Moved once to the current name (the old settings file is copied and kept).
void migrateFromOldName() {
    const QString oldName = QStringLiteral("ZubuvZpevnikUi");
    QSettings current;
    if (current.allKeys().isEmpty()) {
        const QSettings old(QSettings::NativeFormat, QSettings::UserScope,
                            QApplication::organizationName(), oldName);
        for (const QString& key : old.allKeys())
            current.setValue(key, old.value(key));
        current.sync();
    }
    for (const auto location : {QStandardPaths::AppDataLocation, QStandardPaths::CacheLocation}) {
        const QString now = QStandardPaths::writableLocation(location);
        if (now.isEmpty() || QDir(now).exists())
            continue;
        // Cleaned first: "<new>/../<old>" does not resolve while <new> does not exist.
        const QString before =
            QDir::cleanPath(QDir(now).absoluteFilePath(QStringLiteral("../") + oldName));
        if (QDir(before).exists())
            QDir().rename(before, now);
    }
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
    migrateFromOldName();

    MainWindow window;
    window.show();
    // After show(), so a folder dialog opens on top of the main window.
    QTimer::singleShot(0, &window, [&window, path = parser.value(songbookOption)] {
        window.openInitialSongbook(path);
    });
    return QApplication::exec();
}
