// The score tabs of a song: caret stops, the fretboard input and its tuning, New Score.
// (Nothing here needs lilypond: the preview is never built, PATH is empty in default runs.)
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QTextBlock>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>
#include <zub/core/scoreregion.hpp>

#include "AppTestAccess.hpp"
#include "FretboardWidget.hpp"
#include "ScoreInputBar.hpp"
#include "ScorePreviewPane.hpp"
#include "SongEditor.hpp"
#include "Support.hpp"

using A = AppTestAccess;

namespace {

// A window with the fixture's Test/Cz open and its score on the second tab.
struct ScoreWindow {
    // `song`: Test/Cz (one tab staff and chord names), Test/Tun (a banjo staff and a guitar staff).
    explicit ScoreWindow(const char* song = "Cz") : fixture(apptest::Fixture::Extras::Yes) {
        window.resize(1500, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        REQUIRE(A::openSongbook(window, fixture.root()));
        REQUIRE(A::openExistingSong(window, fixture / "songs/chords/Test" / (std::string(song) + ".tex")));
        A::syncScoreTabs(window);
        A::syncScoreTabs(window);
        REQUIRE(A::scoreTabs(window).size() == 1);
        A::tabs(window)->setCurrentIndex(1);
    }
    auto& tab() { return A::scoreTabs(window)[0]; }
    SongEditor* editor() { return tab().editor; }
    // Puts the cursor before `needle` (+ offset) in the score text.
    void go(const QString& needle, int offset = 0) {
        auto* edit = editor()->textEdit();
        QTextCursor c(edit->document());
        const auto at = edit->document()->toPlainText().indexOf(needle);
        REQUIRE(at >= 0);
        c.setPosition(static_cast<int>(at) + offset);
        edit->setTextCursor(c);
        edit->setFocus();
        QTest::qWait(300); // the fretboard follows the cursor on a timer
    }
    QString text() { return editor()->textEdit()->toPlainText(); }
    QToolButton* button(const char* name) { return tab().inputBar->findChild<QToolButton*>(name); }

    apptest::Fixture fixture;
    MainWindow window;
};

} // namespace

TEST_CASE("Score tab: caret stops of an older text are never used", "[app][score]") {
    ScoreWindow s;
    SongEditor* ed = s.editor();
    REQUIRE(ed != nullptr);
    QPlainTextEdit* edit = ed->textEdit();
    CHECK(zub::scoreregion::musicEvents(ed->editorText()).size() > 2);
    using Key = ScorePreviewPane::KeyCommand;

    // Stops of an older, longer text: Shift+Left must not read past the events (no crash).
    A::scoreStops(s.window) = std::make_pair(std::size_t{0}, std::size_t{999});
    A::scoreKey(s.window, Key::Left, true, false);

    // The stops are dropped when the text changes ...
    A::scoreStops(s.window) = std::make_pair(std::size_t{0}, std::size_t{1});
    A::scoreAnchor(s.window) = 0;
    QTextCursor c = edit->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(" ");
    CHECK(apptest::waitUntil([&] { return !A::scoreStops(s.window).has_value(); }, 2000));
    CHECK(!A::scoreAnchor(s.window).has_value());
    // ... on Undo from the preview ...
    A::scoreStops(s.window) = std::make_pair(std::size_t{0}, std::size_t{1});
    A::scoreKey(s.window, Key::Undo, false, false);
    CHECK(apptest::waitUntil([&] { return !A::scoreStops(s.window).has_value(); }, 2000));
    // ... and on a tab switch.
    A::scoreStops(s.window) = std::make_pair(std::size_t{0}, std::size_t{1});
    A::tabs(s.window)->setCurrentIndex(0);
    CHECK(apptest::waitUntil([&] { return !A::scoreStops(s.window).has_value(); }, 2000));
    edit->document()->setModified(false);
}

TEST_CASE("Fretboard: the tuning follows the staff at the cursor, the combo forces one",
          "[app][score][fretboard]") {
    ScoreWindow s("Tun"); // a banjo staff, then a guitar staff
    FretboardWidget* fb = s.tab().fretboard;
    s.go("g'\\5");
    CHECK(fb->tuning().open.size() == 5);
    s.go("e'\\1");
    CHECK(fb->tuning().open.size() == 6);

    // Forced Guitar in the banjo staff, forced Banjo in the guitar staff.
    auto* combo = s.tab().inputBar->findChild<QComboBox*>("scoreTuning");
    REQUIRE(combo != nullptr);
    s.go("g'\\5");
    combo->setCurrentIndex(1);
    CHECK(fb->tuning().open.size() == 6);
    // Staged notes are cleared when the tuning changes.
    QTest::mouseClick(fb, Qt::LeftButton, Qt::NoModifier, fb->cellCenter(1, 2));
    CHECK(fb->staged().size() == 1);
    combo->setCurrentIndex(2);
    CHECK(fb->tuning().open.size() == 5);
    CHECK(fb->staged().empty());
    combo->setCurrentIndex(0); // from the score again
    CHECK(fb->tuning().open.size() == 5);
    s.go("e'\\1");
    CHECK(fb->tuning().open.size() == 6);
    combo->setCurrentIndex(2);
    CHECK(fb->tuning().open.size() == 5);
    s.editor()->textEdit()->document()->setModified(false);
}

TEST_CASE("Fretboard: a note inserted with Ctrl+Return is named by the staff's tuning",
          "[app][score][fretboard]") {
    ScoreWindow s("Tun");
    FretboardWidget* fb = s.tab().fretboard;
    auto* edit = s.editor()->textEdit();
    s.go("e'\\1");
    const QString before = s.text();
    QTest::mouseClick(fb, Qt::LeftButton, Qt::NoModifier, fb->cellCenter(1, 2));
    REQUIRE(fb->staged().size() == 1);
    QTest::keyClick(edit, Qt::Key_Return, Qt::ControlModifier);
    // Guitar string 1, fret 2 is f#' (a banjo's string 1 would give e').
    CHECK(s.text().contains("fis'\\1"));
    edit->undo();
    CHECK(s.text() == before);

    // In the banjo staff the same click is another pitch.
    s.go("g'\\5");
    QTest::mouseClick(fb, Qt::LeftButton, Qt::NoModifier, fb->cellCenter(1, 2));
    QTest::keyClick(edit, Qt::Key_Return, Qt::ControlModifier);
    CHECK(!s.text().contains("fis'\\1"));
    edit->undo();
    CHECK(s.text() == before);
    edit->document()->setModified(false);
}

TEST_CASE("Fretboard: staging, Insert, Clear, double-click and keypad Enter", "[app][score][fretboard]") {
    ScoreWindow s;
    FretboardWidget* fb = s.tab().fretboard;
    auto* edit = s.editor()->textEdit();
    // Cursor at the end of the last music line.
    s.go("e16\\4 g\\3 |\n    }", 11);
    const int pos = edit->textCursor().position();
    const auto click = [&](int string, int fret) {
        QTest::mouseClick(fb, Qt::LeftButton, Qt::NoModifier, fb->cellCenter(string, fret));
    };
    const QString t0 = s.text();
    CHECK(!s.button("scoreInsert")->isEnabled());

    // Escape with nothing staged: nothing happens.
    QTest::keyClick(edit, Qt::Key_Escape);
    CHECK(fb->staged().empty());

    // Clicks only stage; a second click on the same fret unstages; another fret moves it.
    click(1, 3);
    CHECK(fb->staged().size() == 1);
    CHECK(s.text() == t0);
    CHECK(s.button("scoreInsert")->isEnabled());
    click(1, 3);
    CHECK(fb->staged().empty());
    CHECK(!s.button("scoreInsert")->isEnabled());
    click(1, 3);
    click(1, 5);
    REQUIRE(fb->staged().size() == 1);
    CHECK(fb->staged()[0].fret == 5);
    click(3, 2);
    CHECK(fb->staged().size() == 2);
    CHECK(s.text() == t0);
    QTest::keyClick(edit, Qt::Key_Escape);
    CHECK(fb->staged().empty());

    // Two strings staged: Ctrl+Return writes one chord, one undo step.
    click(1, 5);
    click(3, 2);
    QTest::keyClick(edit, Qt::Key_Return, Qt::ControlModifier);
    CHECK(s.text() != t0);
    CHECK(s.text().mid(pos).contains(QRegularExpression(R"(<[^>]*\\3 [^>]*\\1>)")));
    CHECK(fb->staged().empty());
    edit->undo();
    CHECK(s.text() == t0);

    // Keypad Enter with the focus on the fretboard.
    click(2, 1);
    click(4, 0);
    fb->setFocus();
    QTest::keyClick(fb, Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
    CHECK(fb->staged().empty());
    CHECK(s.text() != t0);
    edit->undo();
    CHECK(s.text() == t0);

    // A double-click stages and inserts at once: one note, not a chord.
    click(2, 4);
    QTest::mouseDClick(fb, Qt::LeftButton, Qt::NoModifier, fb->cellCenter(2, 4));
    CHECK(s.text() != t0);
    CHECK(fb->staged().empty());
    CHECK(!s.text().mid(pos).contains("<a"));
    edit->undo();
    CHECK(s.text() == t0);

    // The Clear button.
    click(1, 2);
    s.button("scoreClear")->click();
    CHECK(fb->staged().empty());
    edit->document()->setModified(false);
}

TEST_CASE("New Score: asks for the part name and references the file", "[app][score]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    MainWindow w;
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    REQUIRE(A::openExistingSong(w, fixture / "songs/chords/Test/En.tex"));
    A::tabs(w)->setCurrentIndex(0);

    std::vector<QString> labels;
    bool okEmpty = false, okBad = true;
    auto answer = apptest::answerDialog<QDialog>([&](QDialog* d) {
        auto* edit = d->findChild<QLineEdit*>();
        auto* ok = d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        okEmpty = ok->isEnabled(); // empty: the song's only score, named like the song
        edit->setText("x/y");
        okBad = ok->isEnabled();
        edit->setText(QString::fromUtf8("kytarové sólo 2"));
        ok->click();
    });
    A::newScore(w);
    REQUIRE(answer.fired());
    CHECK(okEmpty);
    CHECK(!okBad);
    CHECK(A::editor(w)->textEdit()->toPlainText().contains("KytaroveSolo2.ly}"));
    A::editor(w)->textEdit()->document()->setModified(false);
    for (auto& tab : A::scoreTabs(w))
        if (tab.editor)
            tab.editor->textEdit()->document()->setModified(false);
}
