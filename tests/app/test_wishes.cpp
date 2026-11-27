// Wishes: the wish list (wishes.txt), wishes done and removed from Home, a wish fulfilled by the
// first save of its imported song; Add to Songbooks after the first save of a new song.
#include "AppTestAccess.hpp"
#include "AddToBooksDialog.hpp"
#include "HomeData.hpp"
#include "HomePage.hpp"
#include "NewSongDialog.hpp"
#include "Support.hpp"

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>

using A = AppTestAccess;
using apptest::has;
using apptest::slurp;

namespace {

const char* const wishBook = R"(\documentclass[chorded]{velkyzpevnik}
\title{Wish Book}
\begin{document}
\begin{songs}{obsah}
    % TODO: Abba/OurLastSummer
    \input{WabiDanek/Montgomery}
    % TODO: Nobody/Wanted
\end{songs}
\end{document}
)";

struct WishWindow {
    explicit WishWindow(apptest::Fixture& fixture) {
        window.resize(1300, 900);
        window.show();
        REQUIRE(A::openSongbook(window, fixture.root()));
        A::activityBar(window)->button(ActivityBar::View::Home)->click();
        page = A::homePage(window);
    }
    MainWindow window;
    HomePage* page = nullptr;
};

QTreeWidgetItem* wishItem(HomePage& page, const QString& start) {
    for (QTreeWidgetItemIterator it(A::homeWishes(page)); *it; ++it)
        if ((*it)->text(0).startsWith(start))
            return *it;
    return nullptr;
}

HomeWish wishOf(const std::vector<HomeWish>& wishes, const QString& text) {
    for (const HomeWish& w : wishes)
        if (w.text == text)
            return w;
    FAIL("no wish " << text.toStdString());
    return {};
}

} // namespace

TEST_CASE("Wishes: the wish list comes first; Add Wish and Remove edit wishes.txt",
          "[app][wishes]") {
    apptest::Fixture fixture;
    apptest::writeFile(fixture / "wishes.txt", "# my wishes\nPavelDobes/NecoOLasce\n");
    WishWindow w(fixture);
    QTreeWidget* tree = A::homeWishes(*w.page);
    // One flat list, no group headers: the wish list's wishes first (the fixture's big book has
    // one wish of its own).
    REQUIRE(tree->topLevelItemCount() == 2);
    CHECK(tree->topLevelItem(0)->text(0) == "Neco O Lasce — Pavel Dobes");
    CHECK(tree->topLevelItem(1)->text(0).contains("in Big Test Book"));
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        CHECK(tree->topLevelItem(i)->childCount() == 0);

    {
        auto typed = apptest::answerInputDialog("LodJohnB");
        emit w.page->wishAddRequested();
        REQUIRE(typed.fired());
    }
    CHECK(slurp(fixture / "wishes.txt") == "# my wishes\nPavelDobes/NecoOLasce\nLodJohnB\n");
    REQUIRE(wishItem(*w.page, "LodJohnB") != nullptr);

    const auto data = buildHomeData(*A::model(w.window), fixture.root(), {});
    {
        auto yes = apptest::answerMessageBox(QMessageBox::Yes);
        emit w.page->wishRemoveRequested(wishOf(data.wishes, "PavelDobes/NecoOLasce"));
        REQUIRE(yes.fired());
    }
    CHECK(slurp(fixture / "wishes.txt") == "# my wishes\nLodJohnB\n");
}

TEST_CASE("Wishes: Done puts a library song in its book; a changed file is left alone",
          "[app][wishes]") {
    apptest::Fixture fixture;
    const auto book = fixture / "songbooks/wish/wish.tex";
    apptest::writeFile(book, wishBook);
    WishWindow w(fixture);
    const auto data = buildHomeData(*A::model(w.window), fixture.root(), {});
    const HomeWish summer = wishOf(data.wishes, "Abba/OurLastSummer");
    REQUIRE(!summer.song.empty());
    emit w.page->wishDoneRequested(summer);
    CHECK(has(slurp(book), "\\begin{songs}{obsah}\n    \\input{Abba/OurLastSummer}\n"
                           "    \\input{WabiDanek/Montgomery}\n    % TODO: Nobody/Wanted\n"));

    // Remove the other wish (Done kept the line count; a moved line is tested with the save).
    const HomeWish moved = wishOf(data.wishes, "Nobody/Wanted");
    {
        auto yes = apptest::answerMessageBox(QMessageBox::Yes);
        emit w.page->wishRemoveRequested(moved);
        REQUIRE(yes.fired());
    }
    CHECK(!has(slurp(book), "Nobody/Wanted"));

    // Gone from the file meanwhile: refused with a message, nothing written.
    const std::string before = slurp(book);
    {
        auto yes = apptest::answerMessageBox(QMessageBox::Yes);
        auto warning = apptest::answerMessageBox(QMessageBox::Ok);
        emit w.page->wishRemoveRequested(moved);
        REQUIRE(yes.fired());
        CHECK(apptest::waitUntil([&] { return warning.fired(); }));
    }
    CHECK(slurp(book) == before);
}

TEST_CASE("Wishes: the first save of an imported wish fulfils it, then Add to Songbooks asks",
          "[app][wishes]") {
    apptest::Fixture fixture;
    const auto book = fixture / "songbooks/wish/wish.tex";
    apptest::writeFile(book, wishBook);
    apptest::writeFile(fixture / "wishes.txt", "Someone/ListWish\n");
    WishWindow w(fixture);

    // A new song standing in for an import of the book's wish "Nobody/Wanted".
    const auto data = buildHomeData(*A::model(w.window), fixture.root(), {});
    {
        auto answer = apptest::answerDialog<NewSongDialog>([](NewSongDialog* d) {
            d->findChild<QLineEdit*>("newSongTitle")->setText("Wanted");
            d->findChild<QComboBox*>("newSongArtist")->setCurrentText("Nobody");
            d->accept();
        });
        A::newSong(w.window);
        REQUIRE(answer.fired());
    }
    SongEditor* editor = A::editor(w.window);
    REQUIRE(editor != nullptr);
    const auto path = editor->path();
    A::setPendingWish(w.window, path, wishOf(data.wishes, "Nobody/Wanted"));
    // A wish above it goes before the save: its line moves up one, it is still found.
    REQUIRE(removeWish(fixture.root(), wishOf(data.wishes, "Abba/OurLastSummer")));

    // The first save: the wish line becomes the song, then the dialog offers the books; the
    // wish book has it already (checked, and can be unchecked). Not Now.
    bool offered = false, wishBookHasIt = false;
    auto dialog = apptest::answerDialog<AddToBooksDialog>([&](AddToBooksDialog* d) {
        offered = true;
        for (QTreeWidgetItemIterator it(d->findChild<QTreeWidget*>("addToBooksTree")); *it; ++it)
            if ((*it)->text(0).startsWith("Wish Book"))
                wishBookHasIt = (*it)->checkState(0) == Qt::Checked &&
                                (*it)->flags() & Qt::ItemIsEnabled;
        d->reject();
    });
    A::saveCurrent(w.window);
    REQUIRE(apptest::waitUntil([&] { return dialog.fired(); }));
    CHECK(offered);
    CHECK(wishBookHasIt);
    CHECK(has(slurp(book), "\\begin{songs}{obsah}\n    \\input{WabiDanek/Montgomery}\n"
                           "    \\input{Nobody/Wanted}\n\\end{songs}"));
    CHECK(!A::hasPendingWish(w.window));
    // A second save asks nothing more.
    editor->textEdit()->appendPlainText("% more");
    A::saveCurrent(w.window);
    QTest::qWait(50);
}

TEST_CASE("Wishes: Add to Songbooks puts the song into the chosen section", "[app][wishes]") {
    apptest::Fixture fixture;
    const auto book = fixture / "songbooks/wish/wish.tex";
    apptest::writeFile(book, wishBook);
    apptest::writeFile(fixture / "songbooks/locked/locked.tex",
                       "% zpevnikar: locked\n" + std::string(wishBook));
    WishWindow w(fixture);
    REQUIRE(A::openExistingSong(w.window, fixture / "songs/chords/Ayreon/EMc2.tex"));

    bool lockedDisabled = false;
    auto dialog = apptest::answerDialog<AddToBooksDialog>([&](AddToBooksDialog* d) {
        for (QTreeWidgetItemIterator it(d->findChild<QTreeWidget*>("addToBooksTree")); *it; ++it) {
            const QString text = (*it)->text(0);
            if (text.contains("locked/locked.tex") && text.contains("locked)"))
                lockedDisabled = !((*it)->flags() & Qt::ItemIsEnabled);
            if (text.startsWith("Wish Book") && text.contains("wish/wish.tex"))
                (*it)->setCheckState(0, Qt::Checked);
        }
        for (auto* b : d->findChildren<QPushButton*>())
            if (b->text() == "Apply")
                b->click();
    });
    A::addToBooksAction(w.window)->trigger();
    REQUIRE(dialog.fired());
    CHECK(lockedDisabled);
    // No texlua on PATH in the tests: at the end of the section.
    REQUIRE(apptest::waitUntil([&] { return !A::bookAdder(w.window)->busy(); }));
    CHECK(has(slurp(book), "    \\input{WabiDanek/Montgomery}\n    \\input{Ayreon/EMc2}\n"
                           "    % TODO: Nobody/Wanted\n\\end{songs}"));
    CHECK(slurp(fixture / "songbooks/locked/locked.tex") ==
          "% zpevnikar: locked\n" + std::string(wishBook));
    // The Songbooks page does not take Add to Songbooks' sorting (failed: no texlua) as its own.
    CHECK(A::sorterWarning(*A::songbooksPage(w.window))->isHidden());
}

TEST_CASE("Wishes: Add to Songbooks writes no book locked while its dialog was open",
          "[app][wishes]") {
    apptest::Fixture fixture;
    const auto book = fixture / "songbooks/wish/wish.tex";
    apptest::writeFile(book, wishBook);
    WishWindow w(fixture);
    REQUIRE(A::openExistingSong(w.window, fixture / "songs/chords/Ayreon/EMc2.tex"));

    const std::string locked = "% zpevnikar: locked\n" + std::string(wishBook);
    QString warning;
    apptest::Responder box([](QWidget* top) { return qobject_cast<QMessageBox*>(top) != nullptr; },
                           [&](QWidget* top) {
                               auto* m = static_cast<QMessageBox*>(top);
                               warning = m->text();
                               m->button(QMessageBox::Ok)->click();
                           });
    auto dialog = apptest::answerDialog<AddToBooksDialog>([&](AddToBooksDialog* d) {
        for (QTreeWidgetItemIterator it(d->findChild<QTreeWidget*>("addToBooksTree")); *it; ++it)
            if ((*it)->text(0).startsWith("Wish Book") &&
                (*it)->text(0).contains("wish/wish.tex"))
                (*it)->setCheckState(0, Qt::Checked);
        apptest::writeFile(book, locked); // released meanwhile (a git pull)
        for (auto* b : d->findChildren<QPushButton*>())
            if (b->text() == "Apply")
                b->click();
    });
    A::addToBooksAction(w.window)->trigger();
    REQUIRE(dialog.fired());
    REQUIRE(apptest::waitUntil([&] { return box.fired(); }));
    CHECK(warning.contains("locked meanwhile"));
    CHECK(slurp(book) == locked);
}

TEST_CASE("Wishes: unchecking a list in Add to Songbooks takes the song out", "[app][wishes]") {
    apptest::Fixture fixture;
    const auto book = fixture / "songbooks/wish/wish.tex";
    apptest::writeFile(book, wishBook);
    apptest::writeFile(fixture / "songbooks/locked/locked.tex",
                       "% zpevnikar: locked\n" + std::string(wishBook));
    WishWindow w(fixture);
    REQUIRE(has(slurp(book), "\\input{WabiDanek/Montgomery}"));
    REQUIRE(A::openExistingSong(w.window, fixture / "songs/chords/WabiDanek/Montgomery.tex"));

    bool lockedStays = false;
    auto dialog = apptest::answerDialog<AddToBooksDialog>([&](AddToBooksDialog* d) {
        for (QTreeWidgetItemIterator it(d->findChild<QTreeWidget*>("addToBooksTree")); *it; ++it) {
            const QString text = (*it)->text(0);
            // The locked book has it too: checked, and it cannot be unchecked.
            if (text.contains("locked/locked.tex") && text.contains("locked)"))
                lockedStays = (*it)->checkState(0) == Qt::Checked &&
                              !((*it)->flags() & Qt::ItemIsEnabled);
            if (text.startsWith("Wish Book") && text.contains("wish/wish.tex")) {
                REQUIRE((*it)->checkState(0) == Qt::Checked);
                (*it)->setCheckState(0, Qt::Unchecked);
            }
        }
        for (auto* b : d->findChildren<QPushButton*>())
            if (b->text() == "Apply")
                b->click();
    });
    A::addToBooksAction(w.window)->trigger();
    REQUIRE(dialog.fired());
    CHECK(lockedStays);
    CHECK_FALSE(has(slurp(book), "\\input{WabiDanek/Montgomery}"));
    CHECK(slurp(fixture / "songbooks/locked/locked.tex") ==
          "% zpevnikar: locked\n" + std::string(wishBook));
}

TEST_CASE("Wishes: --library opens a song library for that run only", "[app][wishes]") {
    apptest::Fixture fixture;
    QSettings{}.setValue(QStringLiteral("songbook/path"), QStringLiteral("/remembered/library"));
    MainWindow w;
    w.show();
    w.openInitialSongbook(toQString(fixture.root()));
    CHECK(A::songbookRoot(w) == fixture.root());
    CHECK(QSettings{}.value(QStringLiteral("songbook/path")).toString() ==
          QStringLiteral("/remembered/library"));
    QSettings{}.remove(QStringLiteral("songbook/path"));
}

TEST_CASE("Wishes: New Song writes a wish by hand, the first save fulfils it", "[app][wishes]") {
    apptest::Fixture fixture;
    apptest::writeFile(fixture / "wishes.txt", "PavelDobes/NecoOLasce\nWabi/Montgomery\n");
    WishWindow w(fixture);
    const auto data = buildHomeData(*A::model(w.window), fixture.root(), {});
    QString title, artist;
    {
        auto answer = apptest::answerDialog<NewSongDialog>([&](NewSongDialog* d) {
            title = d->title();
            artist = d->artist();
            d->accept();
        });
        emit w.page->wishNewSongRequested(wishOf(data.wishes, "PavelDobes/NecoOLasce"));
        REQUIRE(answer.fired());
    }
    CHECK(title == "Neco O Lasce");
    CHECK(artist == "Pavel Dobes");
    SongEditor* editor = A::editor(w.window);
    REQUIRE(editor != nullptr);
    CHECK(editor->textEdit()->toPlainText().startsWith("\\beginsong{Neco O Lasce}[\n    by={Pavel Dobes},"));

    auto later = apptest::answerTitledDialog("Add to Songbooks", [](QDialog* d) { d->reject(); });
    A::saveCurrent(w.window);
    CHECK(slurp(fixture / "wishes.txt") == "Wabi/Montgomery\n");
    apptest::waitUntil([&] { return later.fired(); }, 2000);
}

TEST_CASE("Wishes: a book's wish is marked with its book and moves to the wish list",
          "[app][wishes]") {
    apptest::Fixture fixture;
    const auto book = fixture / "songbooks/wish/wish.tex";
    apptest::writeFile(book, wishBook);
    apptest::writeFile(fixture / "wishes.txt", "A/First\n");
    WishWindow w(fixture);
    QTreeWidgetItem* wanted = wishItem(*w.page, "Wanted — Nobody");
    REQUIRE(wanted != nullptr);
    CHECK(wanted->text(0).contains("in Wish Book"));
    CHECK(A::homeWishes(*w.page)->topLevelItem(0)->text(0) == "First — A"); // the list first

    const auto data = buildHomeData(*A::model(w.window), fixture.root(), {});
    emit w.page->wishMoveRequested(wishOf(data.wishes, "Nobody/Wanted"));
    CHECK(slurp(fixture / "wishes.txt") == "A/First\nNobody/Wanted\n");
    CHECK(!has(slurp(book), "Nobody/Wanted"));
    CHECK(has(slurp(book), "% TODO: Abba/OurLastSummer"));
}
