// The library carries what the executable used to: translations and the song highlighting.
#include <QFile>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("the app library embeds its translations and the song syntax", "[app][resources]") {
    CHECK(QFile::exists(QStringLiteral(":/org.kde.syntax-highlighting/syntax-addons/zubuvzpevnik-song.xml")));
    CHECK(QFile::exists(QStringLiteral(":/i18n/zpevnikar_cs.qm")));
    CHECK(QFile::exists(QStringLiteral(":/i18n/zpevnikar_en.qm")));
}

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

TEST_CASE("the app tests never use the real user's settings, cache or data", "[app][isolation]") {
    const QString temp = QDir::tempPath();
    for (const auto location :
         {QStandardPaths::AppDataLocation, QStandardPaths::CacheLocation,
          QStandardPaths::AppConfigLocation, QStandardPaths::GenericConfigLocation}) {
        INFO(QStandardPaths::writableLocation(location).toStdString());
#ifdef Q_OS_WIN
        // Windows ignores HOME/XDG_*: Qt's test mode keeps them in its own qttest folder.
        CHECK(QStandardPaths::writableLocation(location).contains(QStringLiteral("/qttest")));
#else
        CHECK(QStandardPaths::writableLocation(location).startsWith(temp));
#endif
    }
    INFO(QSettings().fileName().toStdString());
    CHECK(QSettings().fileName().startsWith(temp));
    CHECK(QCoreApplication::applicationName() == "ZubuvZpevnikarTests");
}
