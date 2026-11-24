// The Songbooks page (milestone 7): the books of the songbook repo, their form, sections, songs,
// lock and LaTeX tab, New Songbook, Duplicate and Check.
#include "AppTestAccess.hpp"
#include "BuildFiles.hpp"
#include "HomePage.hpp"
#include "ActivityBar.hpp"
#include "HomeData.hpp"
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
#include <ranges>
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
    A::bookTabs(*w.page)->setCurrentWidget(A::latexPane(*w.page));
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

namespace {

// The overview's row whose text starts with `title`, and the texts of its songs.
QTreeWidgetItem* overviewRow(SongbooksPage& page, const QString& title) {
    QTreeWidget* tree = A::overviewTree(*A::overview(page));
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->text(0).mid(3).startsWith(title))
            return tree->topLevelItem(i);
    return nullptr;
}
QStringList childTexts(QTreeWidgetItem* row) {
    QStringList out;
    for (int i = 0; row && i < row->childCount(); ++i)
        out << row->child(i)->text(0);
    return out;
}

} // namespace

TEST_CASE("Songbooks page: the Overview says what keeps a book from printing", "[app][songbooks]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    // Test/Cz, Test/En: no pap. Hum: imported, not checked, no key. Montgomery: a TODO, an
    // unknown command. Gone/Missing: no such song.
    const auto hum = fixture / "songs/whistle2/Imaginary/Hum.tex";
    const auto montgomery = fixture / "songs/chords/WabiDanek/Montgomery.tex";
    REQUIRE(std::filesystem::exists(hum));
    apptest::writeFile(hum, slurp(hum) + "% TODO: not checked yet (imported by the app)\n");
    apptest::writeFile(montgomery, slurp(montgomery) + "% TODO: the bridge\n\\directlua{}\n");
    apptest::writeFile(fixture / "songbooks/check/check.tex",
                       "\\documentclass[chorded]{malyzpevnik}\n\n\\title{Check test}\n\n"
                       "\\begin{document}\n\n\\begin{songs}{obsah}\n"
                       "    \\input{Test/Cz}\n    \\input{Test/En}\n    \\input{Imaginary/Hum}\n"
                       "    \\input{WabiDanek/Montgomery}\n    \\input{Gone/Missing}\n"
                       "\\end{songs}\n\n\\end{document}\n");
    BooksWindow w(fixture, "songbooks/check/check.tex");
    REQUIRE(A::check(*w.page)->isEnabled());
    A::checkBook(*w.page); // Check… shows the Overview tab
    BookOverview* overview = A::overview(*w.page);
    REQUIRE(A::bookTabs(*w.page)->currentWidget() == static_cast<QWidget*>(overview));
    CHECK(A::bookTabs(*w.page)->tabText(1) == "Overview");
    CHECK(A::bookTabs(*w.page)->tabText(2) == "LaTeX");

    CHECK(childTexts(overviewRow(*w.page, "Not in the song library")) ==
          QStringList{"Gone/Missing"});
    const QStringList risky = childTexts(overviewRow(*w.page, "The build would ask about these"));
    REQUIRE(risky.size() == 1);
    CHECK(risky.front().contains("\\directlua"));
    const QStringList unfit = childTexts(overviewRow(*w.page, "Not marked as fitting A5"));
    CHECK(unfit.size() == 3); // Cz, En, Hum (Montgomery has a5)
    CHECK(childTexts(overviewRow(*w.page, "Imported, not checked yet")).size() == 1);
    const QStringList todos = childTexts(overviewRow(*w.page, "TODO notes"));
    REQUIRE(todos.size() == 1);
    CHECK(todos.front().endsWith("the bridge"));
    CHECK(childTexts(overviewRow(*w.page, "No key")).size() >= 1);
    // Songs, each once: Gone, Montgomery (unsafe), Cz, En, Hum (unfit and not checked).
    CHECK(A::overviewHeading(*overview)->text().startsWith("5 to fix"));

    // Remove from the Songbook: the missing song goes; the overview follows.
    QTreeWidgetItem* missingRow = overviewRow(*w.page, "Not in the song library");
    A::overviewTree(*overview)->setCurrentItem(missingRow->child(0));
    REQUIRE(A::overviewRemove(*overview)->isEnabled());
    A::overviewRemove(*overview)->click();
    CHECK(!has(slurp(fixture / "songbooks/check/check.tex"), "Gone/Missing"));
    CHECK(childTexts(overviewRow(*w.page, "Not in the song library")).isEmpty());
    CHECK(A::overviewHeading(*overview)->text().startsWith("4 to fix"));

    // A row the user collapsed stays so on a refresh of the same book.
    QTreeWidgetItem* unfitRow = overviewRow(*w.page, "Not marked as fitting A5");
    REQUIRE(unfitRow->isExpanded());
    unfitRow->setExpanded(false);
    A::overviewTree(*overview)->setCurrentItem(nullptr);
    emit overview->refreshRequested();
    CHECK(!overviewRow(*w.page, "Not marked as fitting A5")->isExpanded());
    CHECK(overviewRow(*w.page, "TODO notes")->isExpanded());

    // Open Song: the song page with that song.
    QTreeWidgetItem* todoRow = overviewRow(*w.page, "TODO notes");
    A::overviewTree(*overview)->setCurrentItem(todoRow->child(0));
    A::overviewOpen(*overview)->click();
    REQUIRE(A::editor(w.window) != nullptr);
    CHECK(A::editor(w.window)->path() == montgomery);
    CHECK(A::pages(w.window)->currentWidget() == A::songPage(w.window));
}

TEST_CASE("Songbooks page: a book with nothing to fix is ready to print", "[app][songbooks]") {
    apptest::Fixture fixture;
    apptest::writeFile(fixture / "songbooks/ready/ready.tex",
                       "\\documentclass[chorded]{velkyzpevnik}\n\\title{Ready}\n"
                       "\\begin{document}\n\\begin{songs}{obsah}\n"
                       "    \\input{WabiDanek/Montgomery}\n\\end{songs}\n\\end{document}\n");
    BooksWindow w(fixture, "songbooks/ready/ready.tex");
    A::checkBook(*w.page);
    CHECK(A::overviewHeading(*A::overview(*w.page))->text().startsWith("Ready to print"));
    // Home says so too.
    const auto data = buildHomeData(*A::model(w.window), fixture.root(), {});
    const auto ready = std::ranges::find(data.books, QStringLiteral("songbooks/ready/ready.tex"),
                                         &HomeBook::id);
    REQUIRE(ready != data.books.end());
    CHECK(ready->toFix == 0);
    A::activityBar(w.window)->button(ActivityBar::View::Home)->click();
    bool shown = false;
    QListWidget* books = A::homeBooks(*A::homePage(w.window));
    for (int i = 0; i < books->count(); ++i)
        if (books->item(i)->text().startsWith("Ready"))
            shown = books->item(i)->text().contains("ready ✓");
    CHECK(shown);
}

TEST_CASE("Songbooks page: the Overview of an unreadable, an empty and a risky book",
          "[app][songbooks]") {
    apptest::Fixture fixture;
    apptest::writeFile(fixture / "songbooks/broken/broken.tex",
                       "\\documentclass{velkyzpevnik}\n\\begin{document}\n\\begin{songs}\n"
                       "\\end{document}\n");
    apptest::writeFile(fixture / "songbooks/empty/empty.tex",
                       "\\documentclass{velkyzpevnik}\n\\begin{document}\n"
                       "\\begin{songs}{obsah}\n\\end{songs}\n\\end{document}\n");
    apptest::writeFile(fixture / "songbooks/risky/risky.tex",
                       "\\documentclass{velkyzpevnik}\n\\directlua{}\n\\begin{document}\n"
                       "\\begin{songs}{obsah}\n    \\input{WabiDanek/Montgomery}\n"
                       "\\end{songs}\n\\end{document}\n");
    BooksWindow w(fixture, "songbooks/broken/broken.tex");
    A::checkBook(*w.page);
    BookOverview* overview = A::overview(*w.page);
    CHECK(A::overviewHeading(*overview)->text().startsWith("The songbook cannot be read"));
    w.select("songbooks/empty/empty.tex");
    CHECK(A::overviewHeading(*overview)->text().startsWith("No songs"));
    w.select("songbooks/risky/risky.tex");
    CHECK(A::overviewHeading(*overview)->text().startsWith("1 to fix"));
    const QStringList files = childTexts(overviewRow(*w.page, "The build would ask about the"));
    REQUIRE(files.size() == 1);
    CHECK(files.front().startsWith("risky.tex: \\directlua"));
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

namespace {

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

const char* const keyedSongs[] = {"Abba/OurLastSummer", "Ayreon/EMc2", "Brontosauri/FrankyDlouhan",
                                  "TheMamasAndThePapas/CaliforniaDreamin",
                                  "WabiDanek/Montgomery"};

void setMode(BooksWindow& w, int index) { // 0 re-shape, 1 capo, 2 without capo
    A::transposeMode(*w.page)->setCurrentIndex(index);
}

BookPreview::BuildInput compose(BooksWindow& w) {
    auto input = A::composeBuild(*w.page);
    REQUIRE(input);
    return *input;
}

// Every song file of the fixture's keyed book, to check that nothing was written.
std::map<std::string, std::string> songFiles(const KeyedFixture& f) {
    std::map<std::string, std::string> files;
    for (const char* name : keyedSongs)
        files[name] = slurp(f.fixture / "songs/chords" / (std::string(name) + ".tex"));
    return files;
}

} // namespace


TEST_CASE("Songbooks page: a key transposes the build copy, never the book", "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    const std::string before = slurp(f.book());
    const auto songsBefore = songFiles(f);
    CHECK(A::transposeWarning(*w.page)->isHidden()); // no key: nothing to build
    CHECK(compose(w).text == before);

    setKey(w, "G");
    CHECK(slurp(f.book()) == before);
    CHECK(songFiles(f) == songsBefore);
    auto input = compose(w);
    INFO(input.text);
    CHECK(input.songs.empty());
    CHECK(!has(input.text, "zpevnikar:"));
    CHECK(has(input.text, "    \\transposesong[key=G]{5}\n    \\input{Abba/OurLastSummer}"));
    CHECK(has(input.text, "    \\transposesong[key=G]{-5}\n    \\input{Ayreon/EMc2}"));
    CHECK(has(input.text, "    \\transposesong[key=G]{4}\n    \\input{Brontosauri/FrankyDlouhan}"));
    CHECK(has(input.text, "    \\transposesong[key=Em]{3}\n    \\input{TheMamasAndThePapas/"));
    CHECK(has(input.text, "    \\transposesong[key=G]{5}\n    \\input{WabiDanek/Montgomery}"));
    // No key, and the tin whistle collection: no line.
    CHECK(has(input.text, "    \\input{Imaginary/Hum}\n    \\input{Imaginary/Pipe}\n"));
    CHECK(!has(input.text, "{0}"));

    // The tree shows each key and where it goes, or why it stays.
    CHECK(songItem(w, "Abba/OurLastSummer")->text(0).endsWith("D → G"));
    CHECK(songItem(w, "TheMamasAndThePapas/CaliforniaDreamin")->text(0).endsWith("C♯m → Em"));
    CHECK(songItem(w, "Brontosauri/FrankyDlouhan")->text(0).endsWith("E♭ → G"));
    CHECK(songItem(w, "Imaginary/Hum")->text(0).endsWith("(no key)"));
    CHECK(songItem(w, "Imaginary/Pipe")->text(0).endsWith("(D, tin whistle: not transposed)"));

    // Another key replaces the lines; "as written" builds the book as it is.
    setKey(w, "A");
    CHECK(has(compose(w).text, "    \\transposesong[key=A]{-3}\n    \\input{Ayreon/EMc2}"));
    setKey(w, "");
    CHECK(compose(w).text == before);
    CHECK(slurp(f.book()) == before);
}

TEST_CASE("Songbooks page: the book's own transpose lines give way to a key", "[app][songbooks]") {
    KeyedFixture f;
    apptest::writeFile(f.book(), replaced(keyedBook, "    \\input{Ayreon/EMc2}",
                                          "    \\transposesong{2}\n    \\input{Ayreon/EMc2}"));
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    CHECK(has(compose(w).text, "\\transposesong{2}\n    \\input{Ayreon/EMc2}")); // as written
    setKey(w, "C"); // EMc2 is in C: no line
    const auto text = compose(w).text;
    CHECK(!has(text, "\\transposesong{2}"));
    CHECK(has(text, "    \\transposesong[key=C]{-2}\n    \\input{Abba/OurLastSummer}"));
}

TEST_CASE("Songbooks page: capo mode builds with song copies, the files stay",
          "[app][songbooks]") {
    KeyedFixture f;
    const auto emc2 = f.fixture / "songs/chords/Ayreon/EMc2.tex";
    // A song with a \capo in its body: raised too (code only, not the comment).
    const std::string emc2Before =
        replaced(slurp(emc2), "\\beginverse", "% \\capo{5}\n\\capo{5}\n\\beginverse");
    apptest::writeFile(emc2, emc2Before);
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    const std::string bookBefore = slurp(f.book());
    const auto songsBefore = songFiles(f);
    setKey(w, "B");
    setMode(w, 1);
    const auto input = compose(w);
    INFO(input.text);
    CHECK(slurp(f.book()) == bookBefore);
    CHECK(songFiles(f) == songsBefore);
    // C with capo 5 to B: capo 4, key B, the \capo note too; the chords stay. No book line.
    REQUIRE(input.songs.contains("Ayreon/EMc2"));
    CHECK(input.songs.at("Ayreon/EMc2") ==
          replaced(replaced(replaced(emc2Before, "key={C}", "key={B}"), "cap={5}", "cap={4}"),
                   "\n\\capo{5}", "\n\\capo{4}"));
    CHECK(!has(input.text, "\\transposesong[key=B]{-1}"));
    // D without a capo to B would be capo -3, E-flat with capo 3 capo -1: their chords are
    // re-shaped by a line instead, no copy.
    CHECK(has(input.text, "    \\transposesong[key=B]{-3}\n    \\input{Abba/OurLastSummer}"));
    CHECK(!input.songs.contains("Abba/OurLastSummer"));
    CHECK(songItem(w, "Abba/OurLastSummer")
              ->text(0)
              .endsWith("D → B (capo would be -3: chords re-shaped)"));
    CHECK(songItem(w, "Brontosauri/FrankyDlouhan")
              ->text(0)
              .endsWith("E♭ → H (capo would be -1: chords re-shaped)")); // Czech: H
    CHECK(songItem(w, "Ayreon/EMc2")->text(0).endsWith("C → B (capo -1)"));
    // The song list keeps the songs as they are.
    const auto* entry = A::model(w.window)->song(A::model(w.window)->rowOf(emc2));
    REQUIRE(entry != nullptr);
    CHECK(entry->meta.value("key") == "C");
}

TEST_CASE("Songbooks page: capo mode copies write cap, key and \\capo exactly",
          "[app][songbooks]") {
    KeyedFixture f;
    const auto song = [&](const char* name) {
        return f.fixture / "songs/chords" / (std::string(name) + ".tex");
    };
    const std::string emc2Before =
        replaced(slurp(song("Ayreon/EMc2")), "\\beginverse", "\\capo{5}\n\\beginverse");
    apptest::writeFile(song("Ayreon/EMc2"), emc2Before);
    const auto before = songFiles(f);
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setMode(w, 1);
    setKey(w, "G");
    const auto input = compose(w);
    INFO(input.text);
    CHECK(!has(input.text, "\\transposesong")); // every song moved by capo: no line
    // C (capo 5) to G: down 5, capo 0: no cap= (as songs without a capo), \capo{0}.
    CHECK(input.songs.at("Ayreon/EMc2") ==
          replaced(replaced(replaced(emc2Before, "key={C}", "key={G}"), "    cap={5},\n", ""),
                   "\\capo{5}", "\\capo{0}"));
    // D (no capo) to G: capo 5 added, key G.
    const std::string abba = input.songs.at("Abba/OurLastSummer");
    CHECK(has(abba, "key={G}"));
    CHECK(has(abba, "cap={5}"));
    CHECK(abba.size() == before.at("Abba/OurLastSummer").size() +
                             std::string("    cap={5},\n").size());
    // E-flat (capo 3, Czech) to G: up 4, capo 7; C-sharp minor (capo 4) to E minor: up 3, capo 7.
    CHECK(input.songs.at("Brontosauri/FrankyDlouhan") ==
          replaced(replaced(before.at("Brontosauri/FrankyDlouhan"), "key={E\\flt}", "key={G}"),
                   "cap={3}", "cap={7}"));
    CHECK(has(input.songs.at("TheMamasAndThePapas/CaliforniaDreamin"), "cap={7}"));
    // The tin whistle song and the song without a key get no copy.
    CHECK(!input.songs.contains("Imaginary/Pipe"));
    CHECK(!input.songs.contains("Imaginary/Hum"));
    CHECK(songFiles(f) == before);
}

TEST_CASE("Songbooks page: capo mode moves every key of a modulating song", "[app][songbooks]") {
    KeyedFixture f;
    const auto emc2 = f.fixture / "songs/chords/Ayreon/EMc2.tex";
    apptest::writeFile(emc2, replaced(slurp(emc2), "key={C}", "key={Cm\\rightarrow C}"));
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setMode(w, 1);
    setKey(w, "G");
    // Cm to the relative minor of G (Em): up 4, capo 5 + 4 = 9; the second key moves too.
    const std::string text = compose(w).songs.at("Ayreon/EMc2");
    CHECK(has(text, "key={Em\\rightarrow E}"));
    CHECK(has(text, "cap={9}"));
}

TEST_CASE("Songbooks page: a body capo out of reach re-shapes the song's chords",
          "[app][songbooks]") {
    KeyedFixture f;
    const auto emc2 = f.fixture / "songs/chords/Ayreon/EMc2.tex";
    apptest::writeFile(emc2, replaced(slurp(emc2), "\\beginverse", "\\capo{0}\n\\beginverse"));
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setMode(w, 1);
    setKey(w, "B"); // C to B: header capo 5 -> 4 fits, but the body's capo 0 would be -1
    const auto input = compose(w);
    CHECK(!input.songs.contains("Ayreon/EMc2"));
    CHECK(has(input.text, "    \\transposesong[key=B]{-1}\n    \\input{Ayreon/EMc2}"));
    CHECK(songItem(w, "Ayreon/EMc2")
              ->text(0)
              .endsWith("C → B (capo would be -1: chords re-shaped)"));
}

TEST_CASE("Songbooks page: without capo the copy transposes every capo away",
          "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    const std::string before = slurp(f.book());
    // No key: the songs keep their sounding keys, only the capos go.
    setMode(w, 2);
    auto input = compose(w);
    INFO(input.text);
    CHECK(has(input.text, "\\documentclass[chorded, transposecapos]{velkyzpevnik}"));
    CHECK(!has(input.text, "\\transposesong"));
    CHECK(input.songs.empty());
    CHECK(songItem(w, "Ayreon/EMc2")->text(0).endsWith("(capo 5 dropped)"));
    CHECK(!songItem(w, "Abba/OurLastSummer")->text(0).contains(QStringLiteral("capo")));
    CHECK(A::transposeWarning(*w.page)->isHidden()); // transposecapos needs no \transposesong

    // With a key: re-shaped from the sounding key (EMc2 sounds in C: to G is -5).
    setKey(w, "G");
    input = compose(w);
    CHECK(has(input.text, "transposecapos]"));
    CHECK(has(input.text, "    \\transposesong[key=G]{-5}\n    \\input{Ayreon/EMc2}"));
    CHECK(songItem(w, "Ayreon/EMc2")->text(0).endsWith("C → G (capo 5 dropped)"));
    CHECK(input.songs.empty());
    CHECK(slurp(f.book()) == before);
}

TEST_CASE("Songbooks page: a song's own choice survives key changes", "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    const std::string before = slurp(f.book());
    // Only a song's own choice, no key: that line only.
    A::setSongTranspose(*w.page, "WabiDanek/Montgomery", 2);
    auto text = compose(w).text;
    CHECK(has(text, "    \\transposesong[key=E]{2}\n    \\input{WabiDanek/Montgomery}"));
    CHECK(!has(text, "{5}"));
    CHECK(songItem(w, "WabiDanek/Montgomery")->text(0).endsWith("kept: E"));

    setKey(w, "G");
    A::setSongTranspose(*w.page, "Abba/OurLastSummer", 0); // not transposed at all
    text = compose(w).text;
    CHECK(has(text, "    \\transposesong[key=E]{2}\n    \\input{WabiDanek/Montgomery}"));
    CHECK(has(text, "\n\n    \\input{Abba/OurLastSummer}"));
    CHECK(has(text, "    \\transposesong[key=G]{-5}\n    \\input{Ayreon/EMc2}"));
    setKey(w, "A");
    CHECK(has(compose(w).text, "]{2}\n    \\input{WabiDanek/Montgomery}"));

    // Follow the songbook: the song moves with the key again.
    A::setSongTranspose(*w.page, "WabiDanek/Montgomery", std::nullopt);
    CHECK(has(compose(w).text, "    \\transposesong[key=A]{-5}\n    \\input{WabiDanek/Montgomery}"));
    CHECK(slurp(f.book()) == before);
}

TEST_CASE("Songbooks page: each book keeps its own transposition for the session",
          "[app][songbooks]") {
    KeyedFixture f;
    apptest::writeFile(f.fixture / "songbooks/keyed/other.tex", keyedBook);
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    setKey(w, "G");
    setMode(w, 2);
    w.select("songbooks/keyed/other.tex");
    CHECK(A::bookKey(*w.page)->currentIndex() == 0);
    CHECK(A::transposeMode(*w.page)->currentIndex() == 0);
    CHECK(compose(w).text == keyedBook);
    w.select("songbooks/keyed/keyed.tex");
    CHECK(A::bookKey(*w.page)->currentData().toString() == QStringLiteral("G"));
    CHECK(A::transposeMode(*w.page)->currentIndex() == 2);

    // A PDF built without transposing is out of date once a key is chosen, and fine again after.
    auto* preview = A::bookPreview(*w.page);
    setKey(w, "");
    setMode(w, 0);
    A::fakeBookBuild(*preview);
    CHECK(preview->upToDate());
    setKey(w, "G");
    CHECK(!preview->upToDate());
    CHECK(A::bookPreviewStatus(*preview)->text().startsWith(
        QStringLiteral("Out of date: built without transposing")));
    setKey(w, "");
    CHECK(preview->upToDate());
    CHECK(A::bookPreviewStatus(*preview)->text() == QStringLiteral("1 page"));
}

TEST_CASE("Songbooks page: a locked book can be transposed for an export", "[app][songbooks]") {
    KeyedFixture f;
    apptest::writeFile(f.book(), "% zpevnikar: locked\n" + std::string(keyedBook));
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    const std::string before = slurp(f.book());
    CHECK(A::bookKey(*w.page)->isEnabled());
    setKey(w, "G");
    A::setSongTranspose(*w.page, "Ayreon/EMc2", 2);
    CHECK(has(compose(w).text, "    \\transposesong[key=G]{5}\n    \\input{Abba/OurLastSummer}"));
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

TEST_CASE("Songbooks page: the build writes the song copies where TEXINPUTS finds them first",
          "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    auto* exporter = A::exporter(*w.page);
    const std::string copy = "\\beginsong{E=mc2}[by={Ayreon}, key={B}, cap={4}]\n\\endsong\n";
    const auto command =
        exporter->prepareRepoBuild(f.book(), keyedBook, {{"Ayreon/EMc2", copy}});
    REQUIRE(command);
    const auto override = command->workingDirectory / "override";
    CHECK(slurp(override / "Ayreon/EMc2.tex") == copy);
    REQUIRE(command->environment.size() == 1);
    const std::string texinputs = command->environment[0].second;
    INFO(texinputs);
    // ".", the work dir, the package, the copies, then the song folders.
    std::vector<std::string> entries;
    for (const auto part : std::views::split(texinputs, ':'))
        entries.emplace_back(part.begin(), part.end());
    REQUIRE(entries.size() > 4);
    CHECK(entries[1] == command->workingDirectory.string());
    CHECK(entries[3] == override.string());
    CHECK(entries[4].ends_with("/songs/chords"));

    // The next build without copies: the old one is gone, TEXINPUTS has no override folder.
    const auto plain = exporter->prepareRepoBuild(f.book(), keyedBook);
    REQUIRE(plain);
    CHECK(!std::filesystem::exists(override / "Ayreon/EMc2.tex"));
    CHECK(!plain->environment[0].second.contains("override"));
}

TEST_CASE("Songbooks page: without keeping build files the book's work dir goes after a build",
          "[app][songbooks][buildfiles]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    auto* preview = A::bookPreview(*w.page);
    apptest::KeepBuildFiles restore;
    // Kept (the default): the build (bash does not start here) leaves its work dir.
    REQUIRE(preview->build());
    REQUIRE(apptest::waitUntil([&] { return !preview->building(); }, 5000));
    const auto workDir = A::bookWorkDir(*preview);
    REQUIRE(workDir.filename().string().starts_with("repo-"));
    CHECK(std::filesystem::exists(workDir / "book.tex"));

    buildfiles::setKeep(false);
    REQUIRE(preview->build());
    REQUIRE(apptest::waitUntil([&] { return !preview->building(); }, 5000));
    CHECK(A::bookWorkDir(*preview) == workDir);
    CHECK(!std::filesystem::exists(workDir));
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

TEST_CASE("Songbooks page: a song copy for a song not in the book is refused", "[app][songbooks]") {
    KeyedFixture f;
    BooksWindow w(f.fixture, "songbooks/keyed/keyed.tex");
    auto* exporter = A::exporter(*w.page);
    for (const char* name : {"../book", "../../x", "Nobody/Nothing"}) {
        INFO(name);
        auto ok = apptest::answerMessageBox(QMessageBox::Ok);
        CHECK(!exporter->prepareRepoBuild(f.book(), keyedBook, {{name, "\\beginsong{X}\\endsong"}}));
        CHECK(ok.fired());
    }
}
