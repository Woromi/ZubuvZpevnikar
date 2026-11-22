// The Songbooks page (milestone 7): the books of the songbook repo, their form, sections, songs,
// lock and LaTeX tab, New Songbook, Duplicate and Check.
#include "AppTestAccess.hpp"
#include "SongEditor.hpp"
#include "SongTableModel.hpp"
#include "Support.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>
#include <map>

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
        out += std::string(static_cast<std::size_t>(depth) * 2, ' ') +
               (*it)->text(0).toStdString() + "  [" +
               (*it)->data(0, Qt::UserRole).toString().toStdString() + "]\n";
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

TEST_CASE("Songbooks page: the LaTeX tab saves, reverts and keeps the unsaved text",
          "[app][songbooks]") {
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

TEST_CASE("Songbooks page: Check lists the songs not marked as fitting, one can go",
          "[app][songbooks]") {
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

// ---- Transposing a book ----

namespace {

const char* const keyedBook = R"(\documentclass[chorded]{velkyzpevnik}

\title{Keyed}
\author{Tester}

\begin{document}

\begin{songs}{obsah}

    \input{Abba/OurLastSummer}
    \input{Ayreon/EMc2}
    \input{Brontosauri/FrankyDlouhan}
    \input{Imaginary/Hum}
    \input{Imaginary/Pipe}
    \input{TheMamasAndThePapas/CaliforniaDreamin}
    \input{WabiDanek/Montgomery}

\end{songs}

\end{document}
)";

// A copy of the fixture with the book songbooks/keyed/keyed.tex: songs in D, C (capo 5), E-flat
// (capo 3, Czech), C-sharp minor (capo 4), no key (a whistle2 song) and a song of the tinwhistle
// collection (D).
struct KeyedFixture {
    KeyedFixture() {
        apptest::writeFile(fixture / "songbooks/keyed/keyed.tex", keyedBook);
        apptest::writeFile(fixture / "songs/tinwhistle/Imaginary/Pipe.tex",
                           "\\beginsong{Pipe}[by={Test}, key={D}]\n\\beginverse\n\\[D]La\n"
                           "\\endverse\n\\endsong\n");
    }
    apptest::Fixture fixture;
    std::filesystem::path book() const { return fixture / "songbooks/keyed/keyed.tex"; }
};

void setKey(BooksWindow& w, const char* data) {
    auto* combo = A::bookKey(*w.page);
    const int index = combo->findData(QString::fromUtf8(data));
    REQUIRE(index >= 0);
    combo->setCurrentIndex(index);
}

// The tree item of `name`.
QTreeWidgetItem* songItem(BooksWindow& w, const char* name) {
    for (QTreeWidgetItemIterator it(A::tree(*w.page)); *it; ++it)
        if ((*it)->data(0, Qt::UserRole + 1).toString() == QString::fromUtf8(name))
            return *it;
    return nullptr;
}

} // namespace

TEST_CASE("Songbooks page: a book key writes each song's transpose line", "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    const std::string before = slurp(f.book());
    CHECK(A::transposeWarning(*w.page)->isHidden()); // no key: nothing to build

    setKey(w, "G");
    const std::string text = slurp(f.book());
    INFO(text);
    CHECK(has(text, "% zpevnikar: key=G transpose=chords"));
    CHECK(has(text, "    \\transposesong[key=G]{5}\n    \\input{Abba/OurLastSummer}"));
    CHECK(has(text, "    \\transposesong[key=G]{-5}\n    \\input{Ayreon/EMc2}"));
    CHECK(has(text, "    \\transposesong[key=G]{4}\n    \\input{Brontosauri/FrankyDlouhan}"));
    CHECK(has(text, "    \\transposesong[key=Em]{3}\n    \\input{TheMamasAndThePapas/"));
    CHECK(has(text, "    \\transposesong[key=G]{5}\n    \\input{WabiDanek/Montgomery}"));
    // No key, and the tin whistle collection: no line.
    CHECK(has(text, "    \\input{Imaginary/Hum}\n    \\input{Imaginary/Pipe}\n"));
    CHECK(!has(text, "{0}"));

    // The tree shows each key and where it goes, or why it stays.
    CHECK(songItem(w, "Abba/OurLastSummer")->text(0).endsWith("D → G"));
    CHECK(songItem(w, "TheMamasAndThePapas/CaliforniaDreamin")->text(0).endsWith("C♯m → Em"));
    CHECK(songItem(w, "Brontosauri/FrankyDlouhan")->text(0).endsWith("E♭ → G"));
    CHECK(songItem(w, "Imaginary/Hum")->text(0).endsWith("(no key)"));
    CHECK(songItem(w, "Imaginary/Pipe")->text(0).endsWith("(D, tin whistle: not transposed)"));

    // A second key replaces the lines; None removes the settings and every line.
    setKey(w, "A");
    CHECK(has(slurp(f.book()), "% zpevnikar: key=A transpose=chords"));
    CHECK(has(slurp(f.book()), "    \\transposesong[key=A]{-3}\n    \\input{Ayreon/EMc2}"));
    setKey(w, "");
    CHECK(slurp(f.book()) == before);
}

namespace {

// Capo mode asks first (Yes here); the confirmation is the only dialog.
void setCapoMode(BooksWindow& w) {
    auto yes = apptest::answerMessageBox(QMessageBox::Yes);
    A::transposeMode(*w.page)->setCurrentIndex(1); // Capo
}

void setKeyYes(BooksWindow& w, const char* data) {
    auto yes = apptest::answerMessageBox(QMessageBox::Yes);
    setKey(w, data);
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

const char* const keyedSongs[] = {"Abba/OurLastSummer", "Ayreon/EMc2", "Brontosauri/FrankyDlouhan",
                                  "TheMamasAndThePapas/CaliforniaDreamin",
                                  "WabiDanek/Montgomery"};

} // namespace

TEST_CASE("Songbooks page: capo mode edits the song files, a song out of range is re-shaped",
          "[app][songbooks]") {
    KeyedFixture f;
    const auto emc2 = f.fixture / "songs/chords/Ayreon/EMc2.tex";
    // A song with a \capo in its body: raised too (code only, not the comment).
    const std::string emc2Before =
        replaced(slurp(emc2), "\\beginverse", "% \\capo{5}\n\\capo{5}\n\\beginverse");
    apptest::writeFile(emc2, emc2Before);
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setKey(w, "B");
    setCapoMode(w);
    const std::string text = slurp(f.book());
    INFO(text);
    CHECK(has(text, "% zpevnikar: key=B transpose=capo"));
    CHECK(!has(text, "[capo"));
    // C with capo 5 to B: capo 4, key B, the \capo note too; the chords stay. No book line.
    CHECK(slurp(emc2) == replaced(replaced(replaced(emc2Before, "key={C}", "key={B}"), "cap={5}",
                                           "cap={4}"),
                                  "\n\\capo{5}", "\n\\capo{4}"));
    CHECK(!has(text, "{-1}\n    \\input{Ayreon/EMc2}")); // the chords-mode line is gone
    // D without a capo to B would be capo -3, E-flat with capo 3 capo -1: their chords are
    // re-shaped instead by a plain book line, their files stay.
    CHECK(has(text, "    \\transposesong[key=B]{-3}\n    \\input{Abba/OurLastSummer}"));
    CHECK(!has(slurp(f.fixture / "songs/chords/Abba/OurLastSummer.tex"), "key={B}"));
    CHECK(songItem(w, "Abba/OurLastSummer")
              ->text(0)
              .endsWith("D → B (capo would be -3: chords re-shaped)"));
    CHECK(songItem(w, "Brontosauri/FrankyDlouhan")
              ->text(0)
              .endsWith("E♭ → H (capo would be -1: chords re-shaped)")); // Czech: H
    // The tree now shows the new key of the moved song.
    CHECK(songItem(w, "Ayreon/EMc2")->text(0).endsWith("  B"));
}

TEST_CASE("Songbooks page: capo mode writes cap, key and \\capo exactly, and only relative",
          "[app][songbooks]") {
    KeyedFixture f;
    const auto song = [&](const char* name) {
        return f.fixture / "songs/chords" / (std::string(name) + ".tex");
    };
    const std::string emc2Before =
        replaced(slurp(song("Ayreon/EMc2")), "\\beginverse", "\\capo{5}\n\\beginverse");
    apptest::writeFile(song("Ayreon/EMc2"), emc2Before);
    std::map<std::string, std::string> before;
    for (const char* name : keyedSongs)
        before[name] = slurp(song(name));
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setCapoMode(w); // no key yet: nothing to ask
    setKeyYes(w, "G");

    const std::string bookText = slurp(f.book());
    INFO(bookText);
    CHECK(has(bookText, "% zpevnikar: key=G transpose=capo"));
    CHECK(!has(bookText, "\\transposesong")); // every song moved by capo: no book line

    // C (capo 5) to G: down 5, capo 0: no cap= (as songs without a capo), \capo{0}.
    CHECK(slurp(song("Ayreon/EMc2")) ==
          replaced(replaced(replaced(emc2Before, "key={C}", "key={G}"), "    cap={5},\n", ""),
                   "\\capo{5}", "\\capo{0}"));
    // D (no capo) to G: capo 5 added, key G.
    const std::string abba = slurp(song("Abba/OurLastSummer"));
    CHECK(has(abba, "key={G}"));
    CHECK(has(abba, "cap={5}"));
    CHECK(!has(abba, "key={D}"));
    CHECK(abba.size() == before["Abba/OurLastSummer"].size() + std::string("    cap={5},\n").size());
    // E-flat (capo 3, Czech) to G: up 4, capo 7; C-sharp minor (capo 4) to E minor: up 3, capo 7.
    CHECK(slurp(song("Brontosauri/FrankyDlouhan")) ==
          replaced(replaced(before["Brontosauri/FrankyDlouhan"], "key={E\\flt}", "key={G}"),
                   "cap={3}", "cap={7}"));
    CHECK(has(slurp(song("TheMamasAndThePapas/CaliforniaDreamin")), "cap={7}"));
    // The tin whistle song and the song without a key stay.
    CHECK(has(slurp(f.fixture / "songs/tinwhistle/Imaginary/Pipe.tex"), "key={D}"));

    // The song list shows the new key and capo.
    const auto* entry = A::model(w.window)->song(A::model(w.window)->rowOf(song("Ayreon/EMc2")));
    REQUIRE(entry != nullptr);
    CHECK(entry->meta.value("key") == "G");
    CHECK(entry->meta.value("cap").empty());
    CHECK(songItem(w, "Abba/OurLastSummer")->text(0).endsWith("  G"));

    // Re-applying is relative to the current keys: the same key again changes nothing.
    std::map<std::string, std::string> after;
    for (const char* name : keyedSongs)
        after[name] = slurp(song(name));
    setKey(w, ""); // None does not undo the songs; no dialog
    CHECK(!has(slurp(f.book()), "zpevnikar:"));
    for (const char* name : keyedSongs)
        CHECK(slurp(song(name)) == after[name]);
    setKey(w, "G"); // nothing to move: no confirmation (an unanswered one would block)
    for (const char* name : keyedSongs)
        CHECK(slurp(song(name)) == after[name]);
    CHECK(has(slurp(f.book()), "% zpevnikar: key=G transpose=capo"));
    CHECK(!has(slurp(f.book()), "\\transposesong"));
}

TEST_CASE("Songbooks page: capo mode moves every key of a modulating song", "[app][songbooks]") {
    KeyedFixture f;
    const auto emc2 = f.fixture / "songs/chords/Ayreon/EMc2.tex";
    apptest::writeFile(emc2, replaced(slurp(emc2), "key={C}", "key={Cm\\rightarrow C}"));
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setCapoMode(w);
    setKeyYes(w, "G");
    // Cm to the relative minor of G (Em): up 4, capo 5 + 4 = 9; the second key moves too.
    const std::string text = slurp(emc2);
    CHECK(has(text, "key={Em\\rightarrow E}"));
    CHECK(has(text, "cap={9}"));
}

TEST_CASE("Songbooks page: a body capo out of reach re-shapes the song's chords",
          "[app][songbooks]") {
    KeyedFixture f;
    const auto emc2 = f.fixture / "songs/chords/Ayreon/EMc2.tex";
    const std::string original = replaced(slurp(emc2), "\\beginverse", "\\capo{0}\n\\beginverse");
    apptest::writeFile(emc2, original);
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setCapoMode(w);
    setKeyYes(w, "B"); // C to B: header capo 5 -> 4 fits, but the body's capo 0 would be -1
    CHECK(slurp(emc2) == original);
    CHECK(has(slurp(f.book()), "    \\transposesong[key=B]{-1}\n    \\input{Ayreon/EMc2}"));
    CHECK(songItem(w, "Ayreon/EMc2")
              ->text(0)
              .endsWith("C → B (capo would be -1: chords re-shaped)"));
}

TEST_CASE("Songbooks page: Cancel in capo mode changes nothing", "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setCapoMode(w);
    const std::string bookBefore = slurp(f.book());
    std::map<std::string, std::string> before;
    for (const char* name : keyedSongs)
        before[name] = slurp(f.fixture / "songs/chords" / (std::string(name) + ".tex"));
    {
        auto cancel = apptest::answerMessageBox(QMessageBox::Cancel);
        setKey(w, "G");
        CHECK(cancel.fired());
    }
    CHECK(slurp(f.book()) == bookBefore);
    CHECK(A::bookKey(*w.page)->currentIndex() == 0); // back to what the file says
    for (const char* name : keyedSongs)
        CHECK(slurp(f.fixture / "songs/chords" / (std::string(name) + ".tex")) == before[name]);
}

TEST_CASE("Songbooks page: capo mode skips an open song with unsaved changes and reloads a saved one",
          "[app][songbooks]") {
    KeyedFixture f;
    const auto abba = f.fixture / "songs/chords/Abba/OurLastSummer.tex";
    const auto emc2 = f.fixture / "songs/chords/Ayreon/EMc2.tex";
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setCapoMode(w);
    const std::string abbaBefore = slurp(abba);

    // Abba is open and modified: skipped and reported.
    REQUIRE(A::openExistingSong(w.window, abba));
    SongEditor* editor = A::editor(w.window);
    REQUIRE(editor != nullptr);
    editor->textEdit()->appendPlainText("% unsaved");
    REQUIRE(editor->isModified());
    const auto unsavedText = editor->textEdit()->toPlainText();
    A::showView(w.window, ActivityBar::View::Songbooks);
    setKeyYes(w, "G");
    CHECK(slurp(abba) == abbaBefore);
    CHECK(has(A::warning(*w.page)->text().toStdString(), "OurLastSummer.tex"));
    CHECK(has(A::warning(*w.page)->text().toStdString(), "unsaved"));
    CHECK(has(slurp(emc2), "key={G}")); // the others moved
    CHECK(editor->textEdit()->toPlainText() == unsavedText); // not touched
    editor->textEdit()->document()->setModified(false);      // let the next song replace it

    // EMc2 is open and unmodified: the editor reloads what was written.
    setKey(w, "");
    REQUIRE(A::openExistingSong(w.window, emc2));
    A::showView(w.window, ActivityBar::View::Songbooks);
    SongEditor* open = A::editor(w.window);
    REQUIRE(open != nullptr);
    REQUIRE(open->path() == emc2);
    REQUIRE(!open->isModified());
    setKeyYes(w, "A"); // EMc2 (now G) goes up 2: capo 2
    CHECK(has(slurp(emc2), "key={A}"));
    CHECK(apptest::waitUntil([&] {
        return A::editor(w.window) && A::editor(w.window)->content().find("key={A}") !=
                                          std::string::npos;
    }));
    CHECK(has(A::editor(w.window)->content(), "cap={2}"));
}

TEST_CASE("Songbooks page: a song's own choice survives key changes", "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setKey(w, "G");
    // Montgomery (D) is kept two semitones up.
    REQUIRE(A::setSongTranspose(*w.page, "WabiDanek/Montgomery", 2));
    CHECK(has(slurp(f.book()),
              "    \\transposesong[key=E]{2} % fixed\n    \\input{WabiDanek/Montgomery}"));
    CHECK(songItem(w, "WabiDanek/Montgomery")->text(0).endsWith("kept: E"));
    // Abba is not transposed at all.
    REQUIRE(A::setSongTranspose(*w.page, "Abba/OurLastSummer", 0));
    CHECK(has(slurp(f.book()), "{0} % fixed\n    \\input{Abba/OurLastSummer}"));

    setKey(w, "A");
    std::string text = slurp(f.book());
    CHECK(has(text, "{2} % fixed\n    \\input{WabiDanek/Montgomery}"));
    CHECK(has(text, "{0} % fixed\n    \\input{Abba/OurLastSummer}"));
    CHECK(has(text, "    \\transposesong[key=A]{-3}\n    \\input{Ayreon/EMc2}"));

    // None drops the settings and the lines that follow the book; the song's own choices stay.
    setKey(w, "");
    text = slurp(f.book());
    CHECK(!has(text, "zpevnikar:"));
    CHECK(has(text, "{2} % fixed\n    \\input{WabiDanek/Montgomery}"));
    CHECK(!has(text, "Ayreon/EMc2}\n    \\transposesong"));
    CHECK(!has(text, "transposesong[key=A]"));

    // Follow the songbook: the song's line is its own again.
    setKey(w, "G");
    REQUIRE(A::setSongTranspose(*w.page, "WabiDanek/Montgomery", std::nullopt));
    CHECK(has(slurp(f.book()), "    \\transposesong[key=G]{5}\n    \\input{WabiDanek/Montgomery}"));
    CHECK(!has(slurp(f.book()), "fixed\n    \\input{WabiDanek"));
}

TEST_CASE("Songbooks page: an added song of a keyed book gets its line in the same write",
          "[app][songbooks]") {
    KeyedFixture f;
    std::string without = keyedBook;
    const std::string abba = "    \\input{Abba/OurLastSummer}\n";
    without.erase(without.find(abba), abba.size());
    apptest::writeFile(f.book(), without);
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setKey(w, "G");
    CHECK(!has(slurp(f.book()), "Abba/OurLastSummer"));
    A::addSongs(*w.page, 0, {"Abba/OurLastSummer"});
    const std::string text = slurp(f.book());
    INFO(text);
    CHECK(has(text, "    \\transposesong[key=G]{5}\n    \\input{Abba/OurLastSummer}"));
}

TEST_CASE("Songbooks page: a locked book refuses a key", "[app][songbooks]") {
    KeyedFixture f;
    apptest::writeFile(f.book(), "% zpevnikar: locked\n" + std::string(keyedBook));
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    const std::string before = slurp(f.book());
    CHECK(!A::bookKey(*w.page)->isEnabled());
    setKey(w, "G");
    CHECK(slurp(f.book()) == before);
    CHECK(A::bookKey(*w.page)->currentIndex() == 0); // back to what the file says
    CHECK(!A::setSongTranspose(*w.page, "Ayreon/EMc2", 2));
    CHECK(slurp(f.book()) == before);
}

TEST_CASE("Songbooks page: the package must define \\transposesong", "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    w.page->setPackageDir(f.fixture / "zubuvzpevnik"); // the fixture's package: no \transposesong
    setKey(w, "G");
    CHECK(!A::transposeWarning(*w.page)->isHidden());
    CHECK(has(A::transposeWarning(*w.page)->text().toStdString(), "can't transpose yet"));

    apptest::writeFile(f.fixture / "zubuvzpevnik/feature/transpozice.cfg",
                       "\\newcommand\\transposesong[2][]{}\n");
    w.page->setPackageDir(f.fixture / "zubuvzpevnik");
    CHECK(A::transposeWarning(*w.page)->isHidden());
    w.page->setPackageDir(f.fixture / "zubuvzpevnik/nope");
    CHECK(!A::transposeWarning(*w.page)->isHidden());
    setKey(w, "");
    CHECK(A::transposeWarning(*w.page)->isHidden()); // no key: nothing to build
}

// Not run by default: ZUB_SCREENSHOT=<file.png> saves the page of a book with a key.
TEST_CASE("Songbooks page: screenshot of a keyed book", "[.screenshot]") {
    const char* file = std::getenv("ZUB_SCREENSHOT");
    REQUIRE(file != nullptr);
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    w.page->setPackageDir(f.fixture / "zubuvzpevnik");
    setKey(w, "G");
    QTest::qWait(200);
    REQUIRE(w.window.grab().save(QString::fromUtf8(file)));
}
