// Home: the start page with the songbook's cards (opened, recently added, TODOs, books, wishes),
// and the welcome panel without a songbook.
#include "ActivityBar.hpp"
#include "AppTestAccess.hpp"
#include "HomeData.hpp"
#include "HomePage.hpp"
#include "SongListPage.hpp"
#include "SongFilterProxy.hpp"
#include "SongTableModel.hpp"
#include "Support.hpp"
#include "WelcomePage.hpp"

#include <QComboBox>
#include <QListWidget>
#include <QProcess>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>

using A = AppTestAccess;
using apptest::slurp;

namespace {

const char* const homeBook = R"(\documentclass[chorded]{velkyzpevnik}
\title{Home Book}
\begin{document}
\begin{songs}{obsah}
    % TODO: Abba/OurLastSummer
    \input{WabiDanek/Montgomery}
    % TODO: Some Title Only
    % TODO: Wabi/Montgomery
\end{songs}
\end{document}
)";

// A window started as the app starts: on the remembered songbook (the fixture), never
// remembering anything itself.
struct HomeWindow {
    explicit HomeWindow(apptest::Fixture& fixture) {
        QSettings{}.setValue(QStringLiteral("songbook/path"), toQString(fixture.root()));
        window.resize(1400, 900);
        window.show();
        window.openInitialSongbook({});
        page = A::homePage(window);
    }
    ~HomeWindow() { QSettings{}.remove(QStringLiteral("songbook/path")); }
    MainWindow window;
    HomePage* page = nullptr;
};

QListWidgetItem* itemContaining(QListWidget* list, const QString& part) {
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text().contains(part))
            return list->item(i);
    return nullptr;
}

void click(QListWidget* list, QListWidgetItem* item) {
    REQUIRE(item != nullptr);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {},
                      list->visualItemRect(item).center());
}

} // namespace

TEST_CASE("Home: the app starts on Home with the songbook's cards", "[app][home]") {
    apptest::Fixture fixture;
    apptest::writeFile(fixture / "songbooks/home/home.tex", homeBook);
    const auto montgomery = fixture / "songs/chords/WabiDanek/Montgomery.tex";
    apptest::writeFile(montgomery,
                       slurp(montgomery) + "% TODO: check the second verse\n");
    HomeWindow w(fixture);
    REQUIRE(A::pages(w.window)->currentWidget() == static_cast<QWidget*>(w.page));
    CHECK(!w.page->showsWelcome());
    CHECK(A::activityBar(w.window)->button(ActivityBar::View::Home)->isChecked());
    CHECK(A::homeSummary(*w.page)->text().contains(toQString(fixture.root())));

    // To do: the song with its first note.
    CHECK(itemContaining(A::homeTodo(*w.page), "check the second verse") != nullptr);
    // No git on PATH in the tests: no dates, no Recently added card.
    CHECK(A::homeAddedCard(*w.page)->isHidden());
    // Nothing opened yet.
    CHECK(A::homeOpened(*w.page)->item(0)->flags() == Qt::NoItemFlags);

    // The books, with their song and wish counts.
    QListWidgetItem* book = itemContaining(A::homeBooks(*w.page), "Home Book");
    REQUIRE(book != nullptr);
    CHECK(book->text().contains("1 song"));
    CHECK(book->text().contains("3 wish")); // the tests run untranslated: "wish(es)"
    CHECK(itemContaining(A::homeBooks(*w.page), "Big Test Book") != nullptr);

    // The wishes, by book: one is a library song (Open Song works), one only a title.
    QTreeWidget* wishes = A::homeWishes(*w.page);
    QTreeWidgetItem* inLibrary = nullptr;
    QTreeWidgetItem* titleOnly = nullptr;
    for (QTreeWidgetItemIterator it(wishes); *it; ++it) {
        if ((*it)->text(0).startsWith("Our Last Summer — Abba"))
            inLibrary = *it;
        if ((*it)->text(0) == "Some Title Only")
            titleOnly = *it;
    }
    REQUIRE(inLibrary != nullptr);
    // Another artist folder, the same file name (the only song of that name): in the library.
    QTreeWidgetItem* otherFolder = nullptr;
    for (QTreeWidgetItemIterator it(wishes); *it; ++it)
        if ((*it)->text(0).startsWith("Montgomery — Wabi"))
            otherFolder = *it;
    REQUIRE(otherFolder != nullptr);
    CHECK(otherFolder->text(0).contains("in the library"));
    REQUIRE(titleOnly != nullptr);
    CHECK(inLibrary->text(0).contains("in the library"));
    wishes->setCurrentItem(titleOnly);
    CHECK(!A::homeWishOpen(*w.page)->isEnabled());
    wishes->setCurrentItem(inLibrary);
    REQUIRE(A::homeWishOpen(*w.page)->isEnabled());
    A::homeWishOpen(*w.page)->click();
    REQUIRE(A::editor(w.window) != nullptr);
    CHECK(A::editor(w.window)->path() == fixture / "songs/chords/Abba/OurLastSummer.tex");
    CHECK(A::pages(w.window)->currentWidget() == A::songPage(w.window));
}

TEST_CASE("Home: a click opens a song, which then is under Continue editing", "[app][home]") {
    apptest::Fixture fixture;
    const auto montgomery = fixture / "songs/chords/WabiDanek/Montgomery.tex";
    apptest::writeFile(montgomery, slurp(montgomery) + "% TODO: chords of the bridge\n");
    HomeWindow w(fixture);
    click(A::homeTodo(*w.page), itemContaining(A::homeTodo(*w.page), "chords of the bridge"));
    REQUIRE(A::editor(w.window) != nullptr);
    CHECK(A::editor(w.window)->path() == montgomery);
    CHECK(A::pages(w.window)->currentWidget() == A::songPage(w.window));

    A::activityBar(w.window)->button(ActivityBar::View::Home)->click();
    REQUIRE(A::pages(w.window)->currentWidget() == static_cast<QWidget*>(w.page));
    QListWidgetItem* opened = A::homeOpened(*w.page)->item(0);
    REQUIRE(opened != nullptr);
    CHECK(opened->data(Qt::UserRole).toString() == toQString(montgomery));
}

TEST_CASE("Home: Show all sets the song list's filter, a book opens on the Songbooks page",
          "[app][home]") {
    apptest::Fixture fixture;
    apptest::writeFile(fixture / "songbooks/home/home.tex", homeBook);
    HomeWindow w(fixture);
    emit w.page->showAllRequested(HomePage::ShowAll::Todo);
    CHECK(A::pages(w.window)->currentWidget() == A::listPage(w.window));
    CHECK(A::show(*A::listPage(w.window))->currentData().toInt() == SongListPage::Todo);

    A::activityBar(w.window)->button(ActivityBar::View::Home)->click();
    click(A::homeBooks(*w.page), itemContaining(A::homeBooks(*w.page), "Home Book"));
    CHECK(A::pages(w.window)->currentWidget() ==
          static_cast<QWidget*>(A::songbooksPage(w.window)));
    CHECK(A::id(*A::songbooksPage(w.window)) == QStringLiteral("songbooks/home/home.tex"));
}

TEST_CASE("Home: opened songs are remembered per songbook, newest first, at most 10",
          "[app][home]") {
    const std::filesystem::path root = "/somewhere/songbook";
    for (int i = 0; i < 12; ++i)
        rememberSong(root, root / "songs/chords/A" / ("S" + std::to_string(i) + ".tex"));
    rememberSong(root, root / "songs/chords/A/S3.tex"); // again: to the front, once
    rememberSong(root, "/elsewhere/X.tex");             // not of the songbook: ignored
    const auto songs = rememberedSongs(root);
    REQUIRE(songs.size() == 10);
    CHECK(songs[0] == "songs/chords/A/S3.tex");
    CHECK(songs[1] == "songs/chords/A/S11.tex");
    CHECK(std::ranges::count(songs, std::filesystem::path("songs/chords/A/S3.tex")) == 1);
    CHECK(rememberedSongs("/another/songbook").empty());
}

TEST_CASE("Home: a wish's web search uses the words of its name", "[app][home]") {
    CHECK(wishSearchText("JohnLennon/Imagine", "JohnLennon/Imagine", "chords") ==
          "John Lennon Imagine chords");
    CHECK(wishSearchText("PavelDobes/NecoOLasce", "PavelDobes/NecoOLasce", "chords") ==
          "Pavel Dobes Neco O Lasce chords");
    CHECK(wishSearchText("Simon&Garfunkel/SoundOfSilence", "", "akordy") ==
          "Simon&Garfunkel/SoundOfSilence akordy");
    CHECK(wishSearchText("Lod John B", "", "chords") == "Lod John B chords");
    CHECK(splitWords("ILoveRockNRoll") == "I Love Rock N Roll");
    CHECK(splitWords("TheKidsArentAlright") == "The Kids Arent Alright");
    CHECK(splitWords("21Guns") == "21 Guns");
    CHECK(splitWords("Song2") == "Song 2");
    CHECK(splitWords("ACDC") == "ACDC");
    CHECK(splitWords("Jiri_Suchy") == "Jiri Suchy");
    CHECK(wishDisplayText("PavelDobes/NecoOLasce", "PavelDobes/NecoOLasce") ==
          "Neco O Lasce — Pavel Dobes");
    CHECK(wishDisplayText("A/BeeGees maybe later", "A/BeeGees") == "Bee Gees — A  (maybe later)");
    CHECK(wishDisplayText("LodJohnB", "") == "LodJohnB");
}

TEST_CASE("Home: without a songbook the welcome panel shows; a new song library opens",
          "[app][home]") {
    QSettings{}.remove(QStringLiteral("songbook/path"));
    MainWindow w;
    w.show();
    w.openInitialSongbook({});
    HomePage* page = A::homePage(w);
    CHECK(A::pages(w)->currentWidget() == static_cast<QWidget*>(page));
    CHECK(page->showsWelcome());
    auto* library = w.findChild<QPushButton*>(QStringLiteral("welcomeLibrary"));
    REQUIRE(library != nullptr);
    CHECK(library->isEnabled()); // creating needs no git
    // The Songs view without a songbook is Home too.
    A::activityBar(w)->button(ActivityBar::View::Songs)->click();
    CHECK(A::pages(w)->currentWidget() == static_cast<QWidget*>(page));

    // New Song Library…: Create (the default) in a new folder; no git on PATH: no git init, and
    // Download cannot be chosen. The library opens on Home.
    QTemporaryDir parent;
    const QString folder = parent.filePath(QStringLiteral("Mine"));
    auto dialog = apptest::answerTitledDialog("New Song Library", [&](QDialog* d) {
        auto* download = d->findChild<QRadioButton*>(QStringLiteral("libraryDownload"));
        CHECK(!download->isEnabled());
        CHECK(d->findChild<QRadioButton*>(QStringLiteral("libraryCreate"))->isChecked());
        d->findChild<QLineEdit*>(QStringLiteral("cloneFolder"))->setText(folder);
        for (auto* b : d->findChildren<QPushButton*>())
            if (b->text() == "Create")
                b->click();
    });
    library->click();
    REQUIRE(dialog.fired());
    CHECK(std::filesystem::is_directory(toPath(folder) / "songs/chords"));
    CHECK(std::filesystem::exists(toPath(folder) / ".gitignore"));
    CHECK(!std::filesystem::exists(toPath(folder) / ".git"));
    CHECK(A::songbookRoot(w) == toPath(folder));
    CHECK(!page->showsWelcome());
    QSettings{}.remove(QStringLiteral("songbook/path")); // opening remembered it
}

TEST_CASE("Home: New Song Library downloads or creates a repository", "[.tools][home]") {
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (git.isEmpty())
        SKIP("git not found");
    apptest::Fixture fixture;
    const auto run = [&](const QStringList& args) {
        QProcess p;
        p.setWorkingDirectory(toQString(fixture.root()));
        p.start(git, args);
        REQUIRE(p.waitForFinished(30000));
        REQUIRE(p.exitCode() == 0);
    };
    run({"init", "-q"});
    run({"-c", "user.name=T", "-c", "user.email=t@t", "add", "."});
    run({"-c", "user.name=T", "-c", "user.email=t@t", "commit", "-qm", "fixture"});
    QTemporaryDir target;
    const QString folder = target.filePath(QStringLiteral("Cloned"));

    LibraryDialog dialog;
    dialog.findChild<QRadioButton*>(QStringLiteral("libraryDownload"))->setChecked(true);
    A::cloneUrl(dialog)->setText(toQString(fixture.root()));
    A::cloneFolder(dialog)->setText(folder);
    A::cloneStart(dialog);
    REQUIRE(apptest::waitUntil([&] { return dialog.result() == QDialog::Accepted; }, 30000));
    CHECK(std::filesystem::exists(toPath(folder) / "songs/chords/Abba/OurLastSummer.tex"));

    // A folder that is not empty is refused: git does not run.
    LibraryDialog again;
    again.findChild<QRadioButton*>(QStringLiteral("libraryDownload"))->setChecked(true);
    A::cloneUrl(again)->setText(toQString(fixture.root()));
    A::cloneFolder(again)->setText(folder);
    A::cloneStart(again);
    QTest::qWait(200);
    CHECK(again.result() != QDialog::Accepted);

    // Create with git there: a repository.
    LibraryDialog create;
    const QString fresh = target.filePath(QStringLiteral("Fresh"));
    A::cloneFolder(create)->setText(fresh);
    A::cloneStart(create);
    REQUIRE(apptest::waitUntil([&] { return create.result() == QDialog::Accepted; }, 30000));
    CHECK(std::filesystem::is_directory(toPath(fresh) / ".git"));
    CHECK(std::filesystem::exists(toPath(fresh) / "songbooks/.gitkeep"));
}

// Not run by default: ZUB_SCREENSHOT=<file.png> ZUB_SONGBOOK=<checkout> saves Home of a songbook
// (read only; settings stay in the test's temp dir). With "tools" in the arguments git dates show.
TEST_CASE("Home: screenshot with the tools", "[.screenshot]") {
    const char* file = std::getenv("ZUB_SCREENSHOT");
    const char* songbook = std::getenv("ZUB_SONGBOOK");
    REQUIRE(file != nullptr);
    REQUIRE(songbook != nullptr);
    MainWindow w;
    w.resize(1400, 1000);
    w.show();
    REQUIRE(A::openSongbook(w, songbook));
    A::activityBar(w)->button(ActivityBar::View::Home)->click();
    apptest::waitUntil([&] { return A::model(w)->hasDates(); }, 15000);
    A::refreshHome(w);
    QTest::qWait(300);
    REQUIRE(w.grab().save(QString::fromUtf8(file)));
}

TEST_CASE("Home: a refresh keeps the chosen wish; a song that did not open is not remembered",
          "[app][home]") {
    apptest::Fixture fixture;
    apptest::writeFile(fixture / "songbooks/home/home.tex", homeBook);
    HomeWindow w(fixture);
    QTreeWidget* wishes = A::homeWishes(*w.page);
    QTreeWidgetItem* chosen = nullptr;
    for (QTreeWidgetItemIterator it(wishes); *it; ++it)
        if ((*it)->text(0) == "Some Title Only")
            chosen = *it;
    REQUIRE(chosen != nullptr);
    wishes->setCurrentItem(chosen);
    A::refreshHome(w.window);
    REQUIRE(wishes->currentItem() != nullptr);
    CHECK(wishes->currentItem()->text(0) == "Some Title Only");

    // A missing file: no editor, nothing remembered.
    {
        auto ok = apptest::answerMessageBox(QMessageBox::Ok);
        emit w.page->songRequested(fixture / "songs/chords/Nobody/Missing.tex");
    }
    CHECK(rememberedSongs(fixture.root()).empty());
}

TEST_CASE("Home: Back and Close Song return to where the song was opened from", "[app][home]") {
    apptest::Fixture fixture;
    const auto montgomery = fixture / "songs/chords/WabiDanek/Montgomery.tex";
    apptest::writeFile(montgomery, slurp(montgomery) + "% TODO: the bridge\n");
    HomeWindow w(fixture);
    auto* pages = A::pages(w.window);
    const auto openFromHome = [&] {
        click(A::homeTodo(*w.page), itemContaining(A::homeTodo(*w.page), "the bridge"));
        REQUIRE(pages->currentWidget() == A::songPage(w.window));
    };
    // Back (the mouse's back button): Home, the song stays open.
    openFromHome();
    QTest::mouseClick(pages, Qt::BackButton);
    CHECK(pages->currentWidget() == static_cast<QWidget*>(w.page));
    CHECK(A::editor(w.window) != nullptr);
    // Close Song after opening it from Home again: Home.
    openFromHome();
    REQUIRE(A::closeSong(w.window));
    CHECK(pages->currentWidget() == static_cast<QWidget*>(w.page));

    // Back from Home with nothing before it (the start): the song list.
    QTest::mouseClick(pages, Qt::BackButton);
    CHECK(pages->currentWidget() == A::listPage(w.window));

    // From the list: back to the list.
    A::openSong(w.window, A::proxy(w.window)->index(0, 0));
    REQUIRE(pages->currentWidget() == A::songPage(w.window));
    REQUIRE(A::closeSong(w.window));
    CHECK(pages->currentWidget() == A::listPage(w.window));
}
