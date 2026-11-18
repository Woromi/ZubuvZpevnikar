// The welcome page: shown instead of the song list while no songbook is open.
#include "AppTestAccess.hpp"
#include "Support.hpp"
#include "WelcomePage.hpp"

#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <catch2/catch_test_macros.hpp>

namespace {
QString message(MainWindow& w) {
    return w.findChild<QLabel*>(QStringLiteral("welcomeMessage"))->text();
}
QPushButton* button(MainWindow& w, const char* name) {
    return w.findChild<QPushButton*>(QString::fromLatin1(name));
}
bool welcomeShown(MainWindow& w) {
    return w.findChild<WelcomePage*>()->isVisible();
}
} // namespace

TEST_CASE("Welcome: the first start shows it, no folder dialog", "[app][welcome]") {
    QSettings{}.remove(QStringLiteral("songbook/path"));
    MainWindow w;
    w.show();
    w.openInitialSongbook({});
    CHECK(welcomeShown(w));
    CHECK(message(w).contains("songs/chords/"));
    CHECK(button(w, "welcomeChoose")->text() == "Open Songbook Folder…");
    CHECK_FALSE(button(w, "welcomeRetry")->isVisible());
}

TEST_CASE("Welcome: a missing remembered folder says where it was; Try Again opens it",
          "[app][welcome]") {
    apptest::Fixture fixture;
    const auto missing = fixture.root().parent_path() / "unplugged";
    QSettings{}.setValue(QStringLiteral("songbook/path"), toQString(missing));
    MainWindow w;
    w.show();
    w.openInitialSongbook({});
    REQUIRE(welcomeShown(w));
    CHECK(message(w).contains(toQString(missing)));
    CHECK(button(w, "welcomeChoose")->text() == "Choose Another…");
    REQUIRE(button(w, "welcomeRetry")->isVisible());
    // Still missing: the page stays, no message box.
    button(w, "welcomeRetry")->click();
    CHECK(welcomeShown(w));
    // The disk is back: Try Again opens it and shows the song list.
    std::filesystem::rename(fixture.root(), missing);
    button(w, "welcomeRetry")->click();
    CHECK_FALSE(welcomeShown(w));
    CHECK(QSettings{}.value(QStringLiteral("songbook/path")).toString() == toQString(missing));
    std::filesystem::rename(missing, fixture.root()); // the fixture removes its own folder
    QSettings{}.remove(QStringLiteral("songbook/path"));
}
