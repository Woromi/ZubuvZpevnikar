// The Songbooks page: undo/redo of the book's edits, its keys, opening songs from the tree.
#include "ActivityBar.hpp"
#include "AppTestAccess.hpp"
#include "SongTableModel.hpp"
#include "Support.hpp"

#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTabWidget>
#include <QTest>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>

using apptest::has;
using apptest::slurp;
using A = AppTestAccess;

namespace {

struct BooksWindow {
    explicit BooksWindow(apptest::Fixture& fixture, const char* bookId = "songbooks/big/big.tex") {
        window.resize(1400, 800);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        REQUIRE(A::openSongbook(window, fixture.root()));
        A::showView(window, ActivityBar::View::Songbooks);
        page = A::songbooksPage(window);
        REQUIRE(page != nullptr);
        select(bookId);
    }
    void select(const char* id) {
        auto* item = A::listItemOf(*page, QString::fromUtf8(id));
        REQUIRE(item != nullptr);
        A::list(*page)->setCurrentItem(item);
        REQUIRE(A::id(*page) == QString::fromUtf8(id));
    }
    MainWindow window;
    SongbooksPage* page = nullptr;
};

// Selects the song `name` in the shown book's tree.
QTreeWidgetItem* selectSong(SongbooksPage& page, const char* name) {
    for (QTreeWidgetItemIterator it(A::tree(page)); *it; ++it) {
        if (!A::tree(page)->isSection(*it) &&
            (*it)->data(0, Qt::UserRole + 1).toString() == QString::fromUtf8(name)) {
            A::tree(page)->setCurrentItem(*it);
            (*it)->setSelected(true);
            return *it;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Songbooks page: undo and redo of a removed song", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    const std::string original = slurp(big);
    CHECK(!w.page->canUndo());
    int changes = 0;
    QObject::connect(w.page, &SongbooksPage::historyChanged, w.page, [&] { ++changes; });

    REQUIRE(selectSong(*w.page, "Beta/Bravo"));
    A::removeSelectedSongs(*w.page);
    const std::string removed = slurp(big);
    REQUIRE(removed != original);
    CHECK(w.page->canUndo());
    CHECK(w.page->undoText() == "remove 1 song");
    CHECK(changes > 0);
    CHECK(!A::warning(*w.page)->isHidden());

    w.page->undo();
    CHECK(slurp(big) == original);
    CHECK(has(slurp(big), "\\input{Beta/Bravo}"));
    CHECK(!w.page->canUndo());
    CHECK(w.page->canRedo());
    CHECK(w.page->redoText() == "remove 1 song");

    w.page->redo();
    CHECK(slurp(big) == removed);
    CHECK(!w.page->canRedo());

    // The page has no Ctrl+Z of its own any more (the Edit menu's key does it).
    A::tree(*w.page)->setFocus();
    QTest::keyClick(A::tree(*w.page), Qt::Key_Z, Qt::ControlModifier);
    CHECK(slurp(big) == removed);
}

TEST_CASE("Songbooks page: edits are named in the history", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    A::applyTitle(*w.page); // nothing changed: no step
    CHECK(!w.page->canUndo());
    A::title(*w.page)->setText("A new title");
    A::applyTitle(*w.page);
    CHECK(w.page->undoText() == "change the title");
    w.page->undo();
    CHECK(w.page->redoText() == "change the title");
    A::toggleLock(*w.page);
    CHECK(w.page->undoText() == "lock");
}

TEST_CASE("Songbooks page: undo is refused after the file changed on disk", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    REQUIRE(selectSong(*w.page, "Beta/Bravo"));
    A::removeSelectedSongs(*w.page);
    {
        std::ofstream out(big, std::ios::binary | std::ios::app);
        out << "% elsewhere\n";
    }
    const std::string changed = slurp(big);
    A::undoEdit(*w.page);
    CHECK(slurp(big) == changed);
    CHECK(!w.page->canUndo());
    CHECK(!A::warning(*w.page)->isHidden());
}

TEST_CASE("Songbooks page: undo goes back to the book it changed", "[app][songbooks]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    const std::string original = slurp(big);
    REQUIRE(selectSong(*w.page, "Beta/Bravo"));
    A::removeSelectedSongs(*w.page);
    REQUIRE(slurp(big) != original);
    w.select("songbooks/whistle/whistle.tex");
    A::undoEdit(*w.page);
    CHECK(slurp(big) == original);
    CHECK(A::id(*w.page) == "songbooks/big/big.tex");
}

TEST_CASE("Songbooks page: undo never unlocks a released book", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    A::toggleLock(*w.page);
    const std::string locked = slurp(big);
    A::undoEdit(*w.page);
    CHECK(slurp(big) == locked); // Unlock asks first; undo does not go around it
    CHECK(!A::warning(*w.page)->isHidden());
}

TEST_CASE("Songbooks page: Ctrl+S saves the LaTeX tab, Ctrl+Z there is the editor's",
          "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    A::bookTabs(*w.page)->setCurrentWidget(A::latexPane(*w.page));
    auto* latex = A::latex(*w.page);
    latex->moveCursor(QTextCursor::End);
    latex->insertPlainText("% raw\n");
    REQUIRE(A::latexModified(*w.page));
    latex->setFocus();
    QTest::keyClick(latex, Qt::Key_S, Qt::ControlModifier);
    CHECK(!A::latexModified(*w.page));
    CHECK(has(slurp(big), "% raw\n"));
    QTest::keyClick(latex, Qt::Key_Z, Qt::ControlModifier);
    CHECK(has(slurp(big), "% raw\n"));
}

TEST_CASE("Songbooks page: double-click and Enter open a song", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    std::vector<std::filesystem::path> opened;
    QObject::connect(w.page, &SongbooksPage::openSongRequested, w.page,
                     [&](const std::filesystem::path& p) { opened.push_back(p); });
    A::addSongs(*w.page, 0, {"Abba/OurLastSummer"});
    auto* item = selectSong(*w.page, "Abba/OurLastSummer");
    REQUIRE(item);
    emit A::tree(*w.page)->itemDoubleClicked(item, 0);
    REQUIRE(opened.size() == 1);
    CHECK(opened[0].filename() == "OurLastSummer.tex");
    A::tree(*w.page)->setFocus();
    QTest::keyClick(A::tree(*w.page), Qt::Key_Return);
    CHECK(opened.size() == 2);
}

TEST_CASE("Songbooks page: Ctrl+R shows and builds the Preview tab", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    A::rightTabs(*w.page)->setCurrentIndex(0);
    A::tree(*w.page)->setFocus();
    // Without tools the build says so in a box.
    auto answer = apptest::answerDialog<QMessageBox>([](QMessageBox* d) { d->reject(); });
    QTest::keyClick(A::tree(*w.page), Qt::Key_R, Qt::ControlModifier);
    CHECK(A::rightTabs(*w.page)->currentIndex() == 1);
}
