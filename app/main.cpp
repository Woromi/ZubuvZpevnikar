#include "MainWindow.hpp"

#include <QApplication>
#include <QByteArrayView>
#include <QCommandLineParser>
#include <QString>
#include <QTimer>
#include <zub/core/version.hpp>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    const auto version = zub::version();
    // Organization + application name decide the QSettings and QStandardPaths locations.
    QApplication::setOrganizationName(QStringLiteral("Modrozoubek"));
    QApplication::setApplicationName(QStringLiteral("ZubuvZpevnikUi"));
    QApplication::setApplicationDisplayName(QStringLiteral("Zubův Zpěvník"));
    QApplication::setDesktopFileName(QStringLiteral("zubuvzpevnik-ui"));
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

    MainWindow window;
    window.show();
    // After show(), so a folder dialog opens on top of the main window.
    QTimer::singleShot(0, &window, [&window, path = parser.value(songbookOption)] {
        window.openInitialSongbook(path);
    });
    return QApplication::exec();
}
