#include "MainWindow.hpp"

#include <QApplication>
#include <QByteArrayView>
#include <QString>
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

    MainWindow window;
    window.show();
    return QApplication::exec();
}
