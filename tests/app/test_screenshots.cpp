// Screenshots of the app's screens for a design review (hidden: run by hand with
// ZUB_SCREENSHOT_DIR=<folder> zub_app_tests "[.screens]"; add a spec naming "tools", e.g.
// "[.screens],[tools]", for the previews to build). Writes PNGs, checks nothing.
#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QStyleFactory>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>
#include <zub/core/songsuggest.hpp>
#include <zub/core/textfile.hpp>

#include "AddToBooksDialog.hpp"
#include "AppTestAccess.hpp"
#include "MainWindow.hpp"
#include "PreviewRunner.hpp"
#include "ShortcutsDialog.hpp"
#include "ShrinkDialog.hpp"
#include "SongFilterProxy.hpp"
#include "SongbookTree.hpp"
#include "SongbooksPage.hpp"
#include "Support.hpp"

using A = AppTestAccess;
using V = ActivityBar::View;

namespace {

QString shotDir() {
    return qEnvironmentVariable("ZUB_SCREENSHOT_DIR");
}

void save(QWidget* widget, const QString& name) {
    QTest::qWait(150);
    widget->grab().save(QDir(shotDir()).filePath(name + QStringLiteral(".png")));
}

// A popup menu as it shows.
void saveMenu(QMenu* menu, const QString& name) {
    menu->popup(QPoint(100, 100));
    QTest::qWait(150);
    save(menu, name);
    menu->hide();
}

// The context menu that `open` shows (a nested event loop): grabbed, then closed.
void saveContextMenu(const std::function<void()>& open, const QString& name) {
    auto* timer = new QTimer;
    QObject::connect(timer, &QTimer::timeout, timer, [timer, name] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            save(menu, name);
            menu->close();
            timer->deleteLater();
        }
    });
    timer->start(50);
    open();
}

void waitForPreview(MainWindow& w) {
    apptest::waitUntil([&] { return !A::previewRunner(w)->isRunning(); }, 90000);
    QTest::qWait(300);
}

} // namespace

TEST_CASE("Screenshots of the screens", "[.screens]") {
    if (shotDir().isEmpty())
        SKIP("ZUB_SCREENSHOT_DIR is not set");
    QDir().mkpath(shotDir());
    // As on the user's desktop (KDE), when Breeze is installed.
    QIcon::setThemeName(QStringLiteral("breeze"));
    if (QStyleFactory::keys().contains(QStringLiteral("Breeze")))
        QApplication::setStyle(QStringLiteral("Breeze"));
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    MainWindow w;
    w.resize(1400, 860);
    w.show();
    w.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&w));
    REQUIRE(A::openSongbook(w, fixture.root()));

    A::showView(w, V::Home);
    A::refreshHome(w);
    if (QTreeWidget* wishes = A::homeWishes(*A::homePage(w)); wishes->topLevelItemCount() > 0)
        wishes->setCurrentItem(wishes->topLevelItem(0));
    save(&w, QStringLiteral("01-home"));

    A::showView(w, V::Songs);
    save(&w, QStringLiteral("02-songs"));
    QTableView* table = A::listPage(w)->table();
    table->selectRow(0);
    const QPoint at = table->visualRect(A::proxy(w)->index(0, 0)).center();
    saveContextMenu([&] { A::showTableMenu(w, at); }, QStringLiteral("03-songs-right-click"));

    // Menus of the menu bar.
    int n = 0;
    for (QAction* top : w.menuBar()->actions())
        if (QMenu* menu = top->menu())
            saveMenu(menu, QStringLiteral("04-menu-%1-%2")
                               .arg(++n)
                               .arg(top->text().remove(QLatin1Char('&'))));

    // A song.
    REQUIRE(A::openExistingSong(w, fixture / "songs/chords/Test/Cz.tex"));
    A::tabs(w)->setCurrentIndex(0);
    waitForPreview(w);
    save(&w, QStringLiteral("05-song"));
    for (QAction* top : w.menuBar()->actions())
        if (top->menu() && top->text().remove(QLatin1Char('&')) == QStringLiteral("Song"))
            saveMenu(top->menu(), QStringLiteral("06-song-menu-with-a-song"));
    A::editor(w)->textEdit()->setFocus();
    QTest::keyClick(&w, Qt::Key_P, Qt::ControlModifier);
    QTest::qWait(300);
    if (auto* menu = A::editor(w)->textEdit()->findChild<QMenu*>(); menu && menu->isVisible()) {
        save(menu, QStringLiteral("07-insert-part"));
        menu->close();
    }

    // The score tab, with and without the fretboard.
    A::syncScoreTabs(w);
    A::syncScoreTabs(w);
    if (!A::scoreTabs(w).empty()) {
        A::tabs(w)->setCurrentIndex(1);
        QTest::qWait(500);
        QTest::qWait(15000); // the score preview builds
        save(&w, QStringLiteral("08-score"));
        for (QAction* action : w.findChildren<QAction*>())
            if (action->text() == QStringLiteral("Fretboard") && action->isChecked()) {
                action->trigger();
                save(&w, QStringLiteral("09-score-no-fretboard"));
                action->trigger();
                break;
            }
        A::tabs(w)->setCurrentIndex(0);
    }

    // Make Smaller's list (a real shrink run's answer).
    {
        const auto data = std::filesystem::path(ZUB_TEST_DATA_DIR) / "converter";
        const auto json = zub::readTextFile(data / "shrink.json");
        const auto input = zub::readTextFile(data / "shrink-input.tex");
        REQUIRE(json);
        REQUIRE(input);
        const auto suggestions = zub::songsuggest::interpretProposals(0, *json, "");
        REQUIRE(suggestions);
        ShrinkDialog dialog(*suggestions, *input, QStringLiteral("Make Smaller"),
                            QStringLiteral("Pick a change to make the song smaller:"), &w, false,
                            QStringLiteral("A5"));
        dialog.show();
        save(&dialog, QStringLiteral("10-make-smaller"));
    }
    {
        ShortcutsDialog dialog(w.menuBar(), &w);
        dialog.show();
        save(&dialog, QStringLiteral("11-keyboard-shortcuts"));
    }
    {
        AddToBooksDialog dialog(fixture.root(), "Test/Cz", QStringLiteral("Cz"), {}, &w);
        dialog.show();
        save(&dialog, QStringLiteral("12-add-to-songbooks"));
    }

    // Songbooks.
    A::showView(w, V::Songbooks);
    SongbooksPage* page = A::songbooksPage(w);
    QTest::qWait(300);
    save(&w, QStringLiteral("13-songbooks"));
    QTabWidget* bookTabs = A::bookTabs(*page);
    for (int i = 1; i < bookTabs->count(); ++i) {
        bookTabs->setCurrentIndex(i);
        save(&w, QStringLiteral("14-songbooks-tab-%1").arg(bookTabs->tabText(i)));
    }
    bookTabs->setCurrentIndex(0);
    for (auto* tabs : page->findChildren<QTabWidget*>())
        if (tabs != bookTabs)
            for (int i = 1; i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                save(&w, QStringLiteral("15-songbooks-right-%1").arg(tabs->tabText(i)));
                tabs->setCurrentIndex(0);
            }

    // Scan Songs (a modal dialog: grabbed by the answerer, then rejected).
    {
        auto dialog = apptest::answerTitledDialog("Scan Songs", [](QDialog* d) {
            save(d, QStringLiteral("16-scan-songs"));
            d->reject();
        });
        A::scanSongbook(w);
    }
}
