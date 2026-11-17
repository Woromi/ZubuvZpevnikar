// The Songbooks page (milestone 7): the books of the songbook repo, their form, sections, songs,
// lock and LaTeX tab, New Songbook, Duplicate and Check.
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>

#include "AppTestAccess.hpp"
#include "SongEditor.hpp"
#include "SongTableModel.hpp"
#include "Support.hpp"

using apptest::has;
using apptest::slurp;
using A = AppTestAccess;

namespace {

// A main window on a songbook copy, showing the Songbooks page with `bookId` selected.
struct BooksWindow {
    explicit BooksWindow(apptest::Fixture& fixture, const char* bookId = "songbooks/big/big.tex") {
        window.resize(1400, 800);
        window.show();
        REQUIRE(A::openSongbook(window, fixture.root()));
        A::showView(window, ActivityBar::View::Songbooks);
        page = A::songbooksPage(window);
        REQUIRE(page != nullptr);
        if (bookId)
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

std::string listing(QTreeWidget* tree) {
    std::string out;
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        int depth = 0;
        for (auto* p = (*it)->parent(); p; p = p->parent())
            ++depth;
        out += std::string(static_cast<std::size_t>(depth) * 2, ' ') + (*it)->text(0).toStdString() +
               "  [" + (*it)->data(0, Qt::UserRole).toString().toStdString() + "]\n";
    }
    return out;
}

} // namespace

TEST_CASE("Songbooks page: the list shows the books in their folders", "[app][songbooks]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    BooksWindow w(fixture, nullptr);
    const std::string shown = listing(A::list(*w.page));
    INFO(shown);
    for (const char* id : {"songbooks/big/big.tex", "songbooks/bands/one/one.tex",
                           "songbooks/camps/2025/camp2025.tex", "songbooks/whistle/whistle.tex"})
        CHECK(A::listItemOf(*w.page, QString::fromUtf8(id)) != nullptr);
    CHECK(has(shown, "big  [songbooks/big/big.tex]"));
}

TEST_CASE("Songbooks page: a book shows its settings, sections and songs", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    CHECK(A::canEdit(*w.page));
    CHECK(A::title(*w.page)->text() == "Big Test Book");
    // big.tex has no \songsection: one root part holding its seven \input songs.
    REQUIRE(A::book(*w.page).sections.size() == 1);
    CHECK(A::book(*w.page).sections[0].songs.size() == 7);

    w.select("songbooks/bands/one/one.tex");
    CHECK(A::canEdit(*w.page));
    // Three named sections; the tree lists them (and their songs below).
    CHECK(A::book(*w.page).sections.size() == 3);
    for (int i = 0; i < A::tree(*w.page)->topLevelItemCount(); ++i)
        CHECK(A::tree(*w.page)->isSection(A::tree(*w.page)->topLevelItem(i)));
}

TEST_CASE("Songbooks page: the form edits the file at once, exactly", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    const std::string before = slurp(big);

    A::title(*w.page)->setText("Velký test");
    A::applyTitle(*w.page);
    std::string expected = before;
    expected.replace(expected.find("Big Test Book"), 13, "Velký test");
    CHECK(slurp(big) == expected);

    A::chorded(*w.page)->setChecked(false);
    const std::string unchorded = slurp(big);
    CHECK(unchorded != expected);
    INFO(unchorded);
    CHECK(has(unchorded, "\\documentclass[lyric, onesongcolumn]{velkyzpevnik}"));
    A::chorded(*w.page)->setChecked(true);
    CHECK(slurp(big) == expected);

    const bool chart = A::chordChart(*w.page)->isChecked();
    A::chordChart(*w.page)->setChecked(!chart);
    CHECK(slurp(big) != expected);
    CHECK(A::settings(*w.page).chordChart == !chart);
}

TEST_CASE("Songbooks page: sections are added, renamed, moved and removed", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";

    {
        auto answer = apptest::answerInputDialog("Extra");
        A::addSection(*w.page);
        REQUIRE(answer.fired());
    }
    CHECK(has(slurp(big), "\\songsection{Extra}"));
    CHECK(A::book(*w.page).sections.size() == 2);
    CHECK(A::currentSection(*w.page) == 1);

    // A song into the new (empty) section: nothing to sort, so no sorter involved.
    A::addSongs(*w.page, 1, {"Abba/OurLastSummer"});
    CHECK(has(slurp(big), "\\songsection{Extra}\n\\begin{songs}{obsah, obsahAutor}\n"
                          "    \\input{Abba/OurLastSummer}"));

    {
        auto answer = apptest::answerInputDialog("Renamed");
        A::renameSection(*w.page, 1);
        REQUIRE(answer.fired());
    }
    CHECK(has(slurp(big), "\\songsection{Renamed}"));
    CHECK(!has(slurp(big), "\\songsection{Extra}"));

    A::section(*w.page) = 1;
    A::moveSection(*w.page, -1);
    {
        const std::string text = slurp(big);
        CHECK(text.find("\\songsection{Renamed}") < text.find("\\input{Abba/Alpha}"));
    }

    {
        auto answer = apptest::answerMessageBox(QMessageBox::Yes);
        A::removeSection(*w.page, 0);
        REQUIRE(answer.fired());
    }
    CHECK(!has(slurp(big), "Renamed"));
    CHECK(has(slurp(big), "\\input{Abba/Alpha}"));
}

TEST_CASE("Songbooks page: removing a song comments nothing else out", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    zub::songbook::removeSong(A::book(*w.page), "Beta/Bravo");
    zub::songbook::removeSong(A::book(*w.page), "Cee/Charlie");
    REQUIRE(A::commit(*w.page));
    const std::string now = slurp(big);
    CHECK(!has(now, "\\input{Beta/Bravo}"));
    CHECK(has(now, "% TODO: Beta/Wished"));
    // A song with a note after it is commented out, keeping the note; the rest is as it was.
    CHECK(has(now, "    % \\input{Cee/Charlie} % TODO: fix the bridge"));
    CHECK(has(now, "\\beginkeeporder"));
}

TEST_CASE("Songbooks page: a locked book is not changed", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";

    A::toggleLock(*w.page);
    CHECK(slurp(big).rfind("% zpevnikar: locked\n", 0) == 0);
    CHECK(!A::canEdit(*w.page));
    CHECK(A::listItemOf(*w.page, "songbooks/big/big.tex")->text(0).contains("🔒"));
    const std::string locked = slurp(big);
    A::title(*w.page)->setText("Nope");
    A::applyTitle(*w.page);
    CHECK(slurp(big) == locked);
    CHECK(A::latex(*w.page)->isReadOnly());

    auto answer = apptest::answerMessageBox(QMessageBox::Yes);
    A::toggleLock(*w.page);
    REQUIRE(answer.fired());
    CHECK(!has(slurp(big), "zpevnikar: locked"));
    CHECK(A::canEdit(*w.page));
}

TEST_CASE("Songbooks page: the LaTeX tab saves, reverts and keeps the unsaved text", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    A::bookTabs(*w.page)->setCurrentIndex(1);
    auto* latex = A::latex(*w.page);

    latex->moveCursor(QTextCursor::End);
    latex->insertPlainText("% raw\n");
    CHECK(A::latexModified(*w.page));
    CHECK(!A::canEdit(*w.page)); // the form waits for the LaTeX to be saved or reverted
    CHECK(!A::editPane(*w.page)->isEnabled());
    A::saveLatex(*w.page);
    CHECK(has(slurp(big), "% raw\n"));
    CHECK(!A::latexModified(*w.page));
    CHECK(A::canEdit(*w.page));

    // Changed on disk meanwhile: the save never overwrites it, the text stays for the user.
    latex->insertPlainText("% mine\n");
    apptest::writeFile(big, slurp(big) + "% theirs\n");
    {
        auto answer = apptest::answerMessageBox(QMessageBox::Ok);
        A::saveLatex(*w.page);
        REQUIRE(answer.fired());
    }
    CHECK(has(slurp(big), "% theirs"));
    CHECK(!has(slurp(big), "% mine"));
    CHECK(A::latexModified(*w.page));
    A::revertLatex(*w.page);
    CHECK(!A::latexModified(*w.page));

    // The page listing the books again (shown again) keeps the unsaved text; quitting asks.
    latex->moveCursor(QTextCursor::End);
    latex->insertPlainText("% unsaved\n");
    A::reloadList(*w.page);
    CHECK(A::latexModified(*w.page));
    CHECK(latex->toPlainText().contains("% unsaved"));
    {
        auto answer = apptest::answerMessageBox(QMessageBox::Discard);
        CHECK(w.page->confirmClose());
        REQUIRE(answer.fired());
    }
    A::revertLatex(*w.page);
    CHECK(!A::latexModified(*w.page));
}

TEST_CASE("Songbooks page: a book changed on disk is not overwritten", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    const auto big = fixture / "songbooks/big/big.tex";
    apptest::writeFile(big, slurp(big) + "% external\n");
    const std::string external = slurp(big);
    zub::songbook::removeSong(A::book(*w.page), "Hotel/Last");
    CHECK(!A::commit(*w.page));
    CHECK(slurp(big) == external);
    CHECK(A::warning(*w.page)->isVisible());
    // The file was read again: Hotel/Last is still in it.
    CHECK(A::book(*w.page).sections[0].songs.size() == 7);
}

TEST_CASE("Songbooks page: New Songbook writes the book, build.sh and a camp's akordy.tex",
          "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture);
    auto answer = apptest::answerTitledDialog("New Songbook", [](QDialog* d) {
        const auto combos = d->findChildren<QComboBox*>();
        combos[0]->setCurrentText("tabory/2027"); // the folder; the file name follows it
        combos[1]->setCurrentIndex(2);            // Táborový zpěvník
        // The line edits: the folder combo's own, the file name, then the title.
        const auto edits = d->findChildren<QLineEdit*>();
        edits[2]->setText("Tábor & spol");
        d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    A::newSongbook(*w.page);
    REQUIRE(answer.fired());

    const auto made = fixture / "songbooks/tabory/2027/2027.tex";
    REQUIRE(std::filesystem::exists(made));
    CHECK(std::filesystem::exists(fixture / "songbooks/tabory/2027/build.sh"));
    const std::string tex = slurp(made);
    INFO(tex);
    CHECK(has(tex, "taborovyzpevnik"));
    CHECK(has(tex, "Tábor \\& spol"));
    // The chord chart of the newest other camp is copied beside it.
    CHECK(slurp(fixture / "songbooks/tabory/2027/akordy.tex") ==
          slurp(fixture / "songbooks/camps/2025/akordy.tex"));
    CHECK(A::id(*w.page) == "songbooks/tabory/2027/2027.tex");
}

TEST_CASE("Songbooks page: Duplicate copies the book into a new folder", "[app][songbooks]") {
    apptest::Fixture fixture;
    BooksWindow w(fixture, "songbooks/camps/2025/camp2025.tex");
    auto answer = apptest::answerInputDialog("camps/2026/camp2026");
    A::duplicateSongbook(*w.page);
    REQUIRE(answer.fired());
    const auto copy = fixture / "songbooks/camps/2026/camp2026.tex";
    REQUIRE(std::filesystem::exists(copy));
    CHECK(slurp(copy) == slurp(fixture / "songbooks/camps/2025/camp2025.tex"));
    CHECK(std::filesystem::exists(fixture / "songbooks/camps/2026/build.sh"));
    CHECK(std::filesystem::exists(fixture / "songbooks/camps/2026/akordy.tex"));
    CHECK(A::id(*w.page) == "songbooks/camps/2026/camp2026.tex");
}

TEST_CASE("Songbooks page: Check lists the songs not marked as fitting, one can go", "[app][songbooks]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    // A small book (A5) of three songs none of which has pap marked (the fixture's other songs do).
    apptest::writeFile(fixture / "songbooks/check/check.tex",
                       "\\documentclass[chorded]{malyzpevnik}\n\n\\title{Check test}\n\n"
                       "\\begin{document}\n\n\\begin{songs}{obsah}\n"
                       "    \\input{Test/Cz}\n    \\input{Test/En}\n    \\input{Imaginary/Hum}\n"
                       "\\end{songs}\n\n\\end{document}\n");
    BooksWindow w(fixture, "songbooks/check/check.tex");
    REQUIRE(A::check(*w.page)->isEnabled());

    std::size_t listed = 0, left = 0;
    auto answer = apptest::answerDialog<QDialog>([&](QDialog* d) {
        auto* list = d->findChild<QListWidget*>("songbookCheckList");
        if (!list) {
            d->reject(); // not the Check dialog; the checks below fail
            return;
        }
        listed = static_cast<std::size_t>(list->count());
        list->setCurrentRow(0);
        for (auto* b : d->findChildren<QPushButton*>())
            if (b->text() == "Remove from the Songbook")
                b->click();
        left = static_cast<std::size_t>(list->count());
        list->setCurrentRow(0);
        for (auto* b : d->findChildren<QPushButton*>())
            if (b->text() == "Open Song")
                b->click();
    });
    A::checkBook(*w.page);
    REQUIRE(answer.fired());
    CHECK(listed == 3);
    CHECK(left == 2);
    const std::string text = slurp(fixture / "songbooks/check/check.tex");
    CHECK(!has(text, "\\input{Test/Cz}"));
    CHECK(has(text, "\\input{Test/En}"));
    CHECK(has(text, "\\input{Imaginary/Hum}"));
    // Open Song went to the song page with the next song.
    REQUIRE(A::editor(w.window) != nullptr);
    CHECK(A::editor(w.window)->path() == fixture / "songs/chords/Test/En.tex");
    CHECK(A::pages(w.window)->currentWidget() == A::songPage(w.window));
}
