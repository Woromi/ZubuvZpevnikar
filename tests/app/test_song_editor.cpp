// The song editor's text editing: several cursors, the chord keys, linked \begin/\end renaming and
// the marks of a selected word.
#include <QApplication>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QTest>
#include <QTextBlock>
#include <catch2/catch_test_macros.hpp>

#include "AppTestAccess.hpp"
#include "CodeTextEdit.hpp"
#include "Support.hpp"

using A = AppTestAccess;

namespace {

// A window with the fixture's Test/Cz open on its song tab.
struct OpenSong {
    OpenSong() : fixture(apptest::Fixture::Extras::Yes) {
        window.resize(1500, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        REQUIRE(A::openSongbook(window, fixture.root()));
        REQUIRE(A::openExistingSong(window, fixture / "songs/chords/Test/Cz.tex"));
        editor = A::editor(window);
        REQUIRE(editor != nullptr);
        A::tabs(window)->setCurrentIndex(0);
        edit = static_cast<CodeTextEdit*>(editor->textEdit());
        edit->setFocus();
    }
    QString qline(int n) const { return edit->document()->findBlockByNumber(n).text(); }
    // std::string: a failing comparison prints it readably.
    std::string line(int n) const { return qline(n).toStdString(); }
    void cursorAt(int block, int column) {
        QTextCursor c(edit->document());
        c.setPosition(edit->document()->findBlockByNumber(block).position() + column);
        edit->setTextCursor(c);
    }
    // Three known lines at the start of the song.
    void writeThreeLines() {
        cursorAt(0, 0);
        QTest::keyClicks(edit, "alpha one");
        QTest::keyClick(edit, Qt::Key_Return);
        QTest::keyClicks(edit, "beta two");
        QTest::keyClick(edit, Qt::Key_Return);
        QTest::keyClicks(edit, "gamma three");
        QTest::keyClick(edit, Qt::Key_Return);
    }
    void addCursorDown() { QTest::keyClick(edit, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier); }
    void addCursorUp() { QTest::keyClick(edit, Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier); }

    apptest::Fixture fixture;
    MainWindow window;
    SongEditor* editor = nullptr;
    CodeTextEdit* edit = nullptr;
};

} // namespace

TEST_CASE("Multi-cursor: typing, Backspace, End and paste act at every cursor", "[app][editor][multicursor]") {
    OpenSong s;
    s.writeThreeLines();
    s.cursorAt(0, 5);
    s.addCursorDown();
    s.addCursorDown();
    REQUIRE(s.edit->extraCursorCount() == 2);
    QTest::keyClicks(s.edit, "X");
    CHECK(s.line(0) == "alphaX one");
    CHECK(s.line(1) == "beta Xtwo");
    CHECK(s.line(2) == "gammaX three");
    QTest::keyClick(s.edit, Qt::Key_Backspace);
    CHECK(s.line(1) == "beta two");
    QTest::keyClick(s.edit, Qt::Key_End);
    QTest::keyClicks(s.edit, ";");
    CHECK(s.line(0) == "alpha one;");
    CHECK(s.line(1) == "beta two;");
    CHECK(s.line(2) == "gamma three;");

    // Copy the first word at every cursor, delete it, paste it back.
    QTest::keyClick(s.edit, Qt::Key_Home);
    QTest::keyClick(s.edit, Qt::Key_Right, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::keyClick(s.edit, Qt::Key_C, Qt::ControlModifier);
    QTest::keyClick(s.edit, Qt::Key_Delete);
    QTest::keyClick(s.edit, Qt::Key_V, Qt::ControlModifier);
    CHECK(s.line(0) == "alpha one;");
    CHECK(s.line(1) == "beta two;");
    CHECK(s.line(2) == "gamma three;");
}

TEST_CASE("Multi-cursor: Escape, direction of growth and comment toggling", "[app][editor][multicursor]") {
    OpenSong s;
    s.writeThreeLines();

    // Escape drops the extra cursors.
    s.cursorAt(0, 0);
    s.addCursorDown();
    CHECK(s.edit->extraCursorCount() == 1);
    QTest::keyClick(s.edit, Qt::Key_Escape);
    CHECK(s.edit->extraCursorCount() == 0);

    // Toggle comment on lines 0..1 (a cursor added below), one undo step.
    s.addCursorDown();
    QTest::keyClick(s.edit, Qt::Key_Slash, Qt::ControlModifier);
    CHECK(s.line(0) == "% alpha one");
    CHECK(s.line(1) == "% beta two");
    CHECK(s.line(2) == "gamma three");
    CHECK(s.edit->extraCursorCount() == 1);
    QTest::keyClick(s.edit, Qt::Key_Slash, Qt::ControlModifier);
    CHECK(s.line(0) == "alpha one");
    CHECK(s.line(1) == "beta two");
    s.edit->undo();
    CHECK(s.line(0) == "% alpha one");
    CHECK(s.line(1) == "% beta two");

    // Upwards.
    QTest::keyClick(s.edit, Qt::Key_Escape);
    s.cursorAt(2, 2);
    s.addCursorUp();
    CHECK(s.edit->extraCursorCount() == 1);
    QTest::keyClicks(s.edit, "Z");
    CHECK(s.line(1) == "% Zbeta two");
    CHECK(s.line(2) == "gaZmma three");

    // The other direction takes back the last cursor added.
    QTest::keyClick(s.edit, Qt::Key_Escape);
    s.cursorAt(0, 0);
    s.addCursorDown();
    s.addCursorDown();
    CHECK(s.edit->extraCursorCount() == 2);
    s.addCursorUp();
    CHECK(s.edit->extraCursorCount() == 1);
    CHECK(s.edit->cursors().last().blockNumber() == 1);
    s.addCursorUp();
    CHECK(s.edit->extraCursorCount() == 0);
    s.addCursorDown();
    CHECK(s.edit->extraCursorCount() == 1);
    CHECK(s.edit->cursors().last().blockNumber() == 1);

    // From the middle upwards, then down takes them back.
    QTest::keyClick(s.edit, Qt::Key_Escape);
    s.cursorAt(2, 0);
    s.addCursorUp();
    s.addCursorUp();
    CHECK(s.edit->extraCursorCount() == 2);
    s.addCursorDown();
    CHECK(s.edit->extraCursorCount() == 1);
    CHECK(s.edit->cursors().last().blockNumber() == 1);

    // Qt's own cursor is hidden while there are several, and the blink is driven by one timer.
    CHECK(s.edit->cursorWidth() == 0);
    CHECK(A::blinkTimer(*s.edit)->isActive());
    QTest::keyClick(s.edit, Qt::Key_Escape);
    CHECK(s.edit->cursorWidth() >= 1);
}

TEST_CASE("Multi-cursor: Insert Chord puts \\[] at every cursor, one undo step", "[app][editor][multicursor]") {
    OpenSong s;
    s.writeThreeLines();
    s.cursorAt(0, 2);
    s.addCursorDown();
    s.addCursorDown();
    REQUIRE(s.edit->extraCursorCount() == 2);
    const QString l0 = s.qline(0), l1 = s.qline(1), l2 = s.qline(2);
    s.editor->insertChord();
    CHECK(s.edit->extraCursorCount() == 2);
    CHECK(s.qline(0) == l0.left(2) + "\\[]" + l0.mid(2));
    CHECK(s.qline(1) == l1.left(2) + "\\[]" + l1.mid(2));
    CHECK(s.qline(2) == l2.left(2) + "\\[]" + l2.mid(2));
    QTest::keyClicks(s.edit, "C"); // every cursor sits between the brackets
    CHECK(s.qline(0) == l0.left(2) + "\\[C]" + l0.mid(2));
    CHECK(s.qline(2) == l2.left(2) + "\\[C]" + l2.mid(2));
    s.edit->undo(); // the typing
    s.edit->undo(); // the three chords: one step
    CHECK(s.qline(0) == l0);
    CHECK(s.qline(1) == l1);
    CHECK(s.qline(2) == l2);
}

TEST_CASE("Song editor: a space inside a chord leaves it", "[app][editor][chords]") {
    OpenSong s;
    QTextCursor c = s.edit->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText("\nJá zpívám");
    s.edit->setTextCursor(c);
    const int lineStart = c.block().position();
    c.setPosition(lineStart + 2); // after "Já"
    s.edit->setTextCursor(c);
    s.editor->insertChord();
    QTest::keyClicks(s.edit, "Am");
    QTest::keyClick(s.edit, Qt::Key_Space); // out of the brackets, then a space
    QTest::keyClicks(s.edit, "x");
    CHECK(s.edit->textCursor().block().text() == QString::fromUtf8("Já\\[Am] x zpívám"));
    // Outside a chord a space is a space.
    QTest::keyClick(s.edit, Qt::Key_Space);
    CHECK(s.edit->textCursor().block().text() == QString::fromUtf8("Já\\[Am] x  zpívám"));
}

TEST_CASE("Song editor: \\begin and \\end are renamed together, undo never splits them",
          "[app][editor][linked]") {
    OpenSong s;
    const QString before = s.edit->toPlainText();
    const int at = static_cast<int>(before.indexOf("\\beginverse")) + 6;
    QTextCursor r = s.edit->textCursor();
    r.setPosition(at);
    r.setPosition(at + 5, QTextCursor::KeepAnchor);
    s.edit->setTextCursor(r);
    QTest::qWait(50);
    QTest::keyClicks(s.edit, "chorus");
    QTest::qWait(100);
    QString after = s.edit->toPlainText();
    CHECK(after.contains("\\beginchorus"));
    CHECK(after.contains("\\endchorus"));
    CHECK(!after.contains("\\endverse"));

    // Backspace inside the end's name renames the begin.
    QTextCursor r2 = s.edit->textCursor();
    r2.setPosition(static_cast<int>(after.indexOf("\\endchorus")) + 10);
    s.edit->setTextCursor(r2);
    QTest::qWait(50);
    QTest::keyClick(s.edit, Qt::Key_Backspace);
    QTest::keyClick(s.edit, Qt::Key_Backspace);
    QTest::qWait(100);
    CHECK(s.edit->toPlainText().contains("\\beginchor\n"));
    CHECK(s.edit->toPlainText().contains("\\endchor\n"));

    // Undoing restores the text, and the names agree after every undo step.
    const auto names = [&] {
        const QString t = s.edit->toPlainText();
        const auto b = t.indexOf("\\begin", t.indexOf("\\lilypondfile"));
        const auto e = t.indexOf("\\end", b);
        const auto word = [&](qsizetype from) {
            qsizetype i = from;
            while (i < t.size() && t[i].isLetter())
                ++i;
            return t.mid(from, i - from);
        };
        return std::pair{word(b + 6), word(e + 4)};
    };
    int undos = 0;
    while (s.edit->toPlainText() != before && undos < 20) {
        s.edit->undo();
        ++undos;
        const auto [begin, end] = names();
        CHECK(begin == end);
    }
    CHECK(s.edit->toPlainText() == before);
}

TEST_CASE("Song editor: a selected word marks its other occurrences", "[app][editor]") {
    OpenSong s;
    QTextCursor all(s.edit->document());
    all.movePosition(QTextCursor::End);
    all.insertText("\nJá zpívám\nzpívám a zpívám, Zpívám");
    QTextCursor sel = all;
    sel.movePosition(QTextCursor::StartOfBlock);
    // A selection the app makes (the playing note, a click in the preview) marks nothing.
    QTextCursor appSelection = sel;
    appSelection.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 6);
    s.edit->setTextCursor(appSelection);
    QTest::qWait(200);
    CHECK(s.edit->extraSelections().isEmpty());
    // The user's selection does: the other zpívám on this line and the one of the earlier text;
    // not the capitalised one.
    s.edit->setTextCursor(sel);
    s.edit->setFocus();
    for (int i = 0; i < 6; ++i)
        QTest::keyClick(s.edit, Qt::Key_Right, Qt::ShiftModifier);
    CHECK(apptest::waitUntil([&] { return s.edit->extraSelections().size() == 2; }));
    QTest::keyClick(s.edit, Qt::Key_Left); // the selection goes, its marks too
    CHECK(apptest::waitUntil([&] { return s.edit->extraSelections().isEmpty(); }));
}
