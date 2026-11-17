// File > New Song: the dialog, the unsaved editor, the collection and what Save writes.
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTest>
#include <QTextBlock>
#include <catch2/catch_test_macros.hpp>

#include "AppTestAccess.hpp"
#include "NewSongDialog.hpp"
#include "SongTableModel.hpp"
#include "Support.hpp"

using apptest::slurp;
using A = AppTestAccess;

namespace {

struct Shown {
    QString inputName;
    QString collection;
};

// Fills the next New Song dialog and presses OK, reporting what it showed.
apptest::Responder fillNewSongDialog(const QString& title, const QString& artist, Shown* shown,
                                     const QString& collection = {}) {
    return apptest::answerDialog<NewSongDialog>([=](NewSongDialog* d) {
        d->findChild<QLineEdit*>("newSongTitle")->setText(title);
        d->findChild<QComboBox*>("newSongArtist")->setCurrentText(artist);
        if (auto* combo = d->findChild<QComboBox*>("newSongCollection"); combo && !collection.isEmpty())
            combo->setCurrentIndex(combo->findData(collection));
        shown->inputName = d->inputName();
        shown->collection = d->collection();
        d->accept();
    });
}

} // namespace

TEST_CASE("New Song: a new artist gets a folder, the song is written on save", "[app][newsong]") {
    apptest::Fixture fixture;
    MainWindow w;
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    const int rowsBefore = A::model(w)->rowCount();

    Shown shown;
    auto answer = fillNewSongDialog("Žluťoučký kůň", "Nová Kapela & Spol", &shown);
    A::newSong(w);
    REQUIRE(answer.fired());
    CHECK(shown.inputName == "NovaKapelaAndSpol/ZlutouckyKun");
    CHECK(shown.collection == "chords");

    SongEditor* editor = A::editor(w);
    REQUIRE(editor != nullptr);
    const auto path = fixture / "songs/chords/NovaKapelaAndSpol/ZlutouckyKun.tex";
    CHECK(editor->path() == path);
    CHECK(editor->textEdit()->toPlainText().startsWith(
        "\\beginsong{Žluťoučký kůň}[\n    by={Nová Kapela \\& Spol},\n    % lang={},"));
    // The cursor waits in the verse, on its indented line; nothing is written yet.
    CHECK(editor->textEdit()->textCursor().block().text() == "    ");
    CHECK(!std::filesystem::exists(path));

    QTest::keyClicks(editor->textEdit(), "Prvni radek");
    A::saveCurrent(w);
    REQUIRE(std::filesystem::exists(path));
    CHECK(apptest::has(slurp(path), "\\beginverse\n    Prvni radek\n\\endverse\n"));
    CHECK(apptest::waitUntil([&] { return A::model(w)->rowCount() == rowsBefore + 1; }));
    CHECK(A::closeSong(w));
}

TEST_CASE("New Song: the collection chooses the folder under songs/", "[app][newsong]") {
    apptest::Fixture fixture;
    MainWindow w;
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));

    Shown shown;
    auto answer = fillNewSongDialog("Pisnicka", "Nikdo", &shown, "whistle2");
    A::newSong(w);
    REQUIRE(answer.fired());
    CHECK(shown.collection == "whistle2");
    REQUIRE(A::editor(w) != nullptr);
    CHECK(A::editor(w)->path() == fixture / "songs/whistle2/Nikdo/Pisnicka.tex");

    auto discard = apptest::answerMessageBox(QMessageBox::Discard);
    CHECK(A::closeSong(w));
    CHECK(discard.fired());
    CHECK(!std::filesystem::exists(fixture / "songs/whistle2/Nikdo/Pisnicka.tex"));
}

TEST_CASE("New Song: an existing artist keeps their folder; an existing song can be opened",
          "[app][newsong]") {
    apptest::Fixture fixture;
    // An artist whose folder is named by hand, as in the real songbook.
    apptest::writeFile(fixture / "songs/chords/Hand/Old.tex",
                       "\\beginsong{Old}[by={Jiří Suchý, Jiří Šlitr}]\n\\endsong\n");
    MainWindow w;
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));

    Shown shown;
    {
        auto answer = fillNewSongDialog("Nová", "Jiří Suchý, Jiří Šlitr", &shown);
        A::newSong(w);
        REQUIRE(answer.fired());
    }
    CHECK(shown.inputName == "Hand/Nova");
    REQUIRE(A::editor(w) != nullptr);
    CHECK(A::editor(w)->path() == fixture / "songs/chords/Hand/Nova.tex");
    CHECK(A::editor(w)->textEdit()->toPlainText().contains("% lang={},"));
    {
        // A new song is unsaved work: closing asks, and Discard leaves nothing on disk.
        auto asked = apptest::answerMessageBox(QMessageBox::Discard);
        CHECK(A::closeSong(w));
        CHECK(asked.fired());
    }
    CHECK(!std::filesystem::exists(fixture / "songs/chords/Hand/Nova.tex"));

    // The same title again for an existing song: the box offers to open it.
    auto open = apptest::answerMessageBoxText("Open Existing");
    auto answer = fillNewSongDialog("Old", "Jiří Suchý, Jiří Šlitr", &shown);
    A::newSong(w);
    REQUIRE(answer.fired());
    CHECK(open.fired());
    REQUIRE(A::editor(w) != nullptr);
    CHECK(A::editor(w)->path() == fixture / "songs/chords/Hand/Old.tex");
}
