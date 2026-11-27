// CodeTextEdit: Enter, Shift+Enter, Ctrl+Enter and Ctrl+Shift+Enter, also with several cursors.
#include <QApplication>
#include <QClipboard>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

#include "AppTestAccess.hpp"
#include "CodeTextEdit.hpp"
#include "Support.hpp"

namespace {

void reset(CodeTextEdit& e) {
    e.setPlainText("a\n    bc\nd");
    QTextCursor c = e.textCursor();
    c.setPosition(7); // between b and c
    e.setTextCursor(c);
}

} // namespace

TEST_CASE("CodeTextEdit: Enter keeps the indent", "[app][editor]") {
    CodeTextEdit e;
    e.show();
    reset(e);
    QTest::keyClick(&e, Qt::Key_Return);
    CHECK(e.toPlainText() == "a\n    b\n    c\nd");
}

TEST_CASE("CodeTextEdit: Ctrl+Enter and Shift+Enter open a line below, Ctrl+Shift+Enter above",
          "[app][editor]") {
    CodeTextEdit e;
    e.show();

    reset(e);
    QTest::keyClick(&e, Qt::Key_Return, Qt::ShiftModifier);
    CHECK(e.toPlainText() == "a\n    bc\n    \nd");
    CHECK(e.textCursor().blockNumber() == 2);

    reset(e);
    QTest::keyClick(&e, Qt::Key_Return, Qt::ControlModifier);
    CHECK(e.toPlainText() == "a\n    bc\n    \nd");
    CHECK(e.textCursor().blockNumber() == 2);

    reset(e);
    QTest::keyClick(&e, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(e.toPlainText() == "a\n    \n    bc\nd");
    CHECK(e.textCursor().blockNumber() == 1);
    CHECK(e.textCursor().positionInBlock() == 4);
    QTest::keyClick(&e, Qt::Key_X);
    CHECK(e.toPlainText() == "a\n    x\n    bc\nd");

    // The line and the typing are separate undo steps; the line is one.
    e.undo();
    CHECK(e.toPlainText() == "a\n    \n    bc\nd");
    e.undo();
    CHECK(e.toPlainText() == "a\n    bc\nd");
}

TEST_CASE("CodeTextEdit: Shift+Enter with two cursors", "[app][editor]") {
    CodeTextEdit e;
    e.show();
    reset(e);
    e.addCursorVertically(-1);
    QTest::keyClick(&e, Qt::Key_Return, Qt::ShiftModifier);
    // Pinned from the behaviour at the time: one new line below each cursor's line, indented
    // like it (the cursor above sits in the first line, which has no indent).
    CHECK(e.toPlainText() == "a\n\n    bc\n    \nd");
    CHECK(e.extraCursorCount() == 1);
    e.undo();
    CHECK(e.toPlainText() == "a\n    bc\nd");
}

TEST_CASE("CodeTextEdit: the cursors blink together", "[app][editor]") {
    CodeTextEdit e;
    e.show();
    reset(e);
    CHECK(e.cursorWidth() >= 1);
    e.addCursorVertically(-1);
    CHECK(e.extraCursorCount() == 1);
    CHECK(e.cursorWidth() == 0); // Qt's own cursor is hidden, every cursor is drawn together
    CHECK(AppTestAccess::blinkTimer(e)->isActive());
    QTest::keyClick(&e, Qt::Key_Escape);
    CHECK(e.extraCursorCount() == 0);
    CHECK(e.cursorWidth() >= 1);
}

TEST_CASE("CodeTextEdit: Alt+Shift+Up/Down add cursors, Ctrl+Shift does not", "[app][editor]") {
    CodeTextEdit e;
    e.show();
    reset(e);
    QTest::keyClick(&e, Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(e.extraCursorCount() == 0);
    QTest::keyClick(&e, Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier);
    CHECK(e.extraCursorCount() == 1);
    QTest::keyClick(&e, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier); // takes it back
    CHECK(e.extraCursorCount() == 0);
}

TEST_CASE("CodeTextEdit: Up/Down keep the column past a shorter line's end", "[app][editor]") {
    CodeTextEdit e;
    e.show();
    e.setPlainText("abcdef\nab\nabcdef");
    e.document()->setModified(false);
    QTextCursor c = e.textCursor();
    c.setPosition(5); // before f
    e.setTextCursor(c);

    // Through the short line and on: the column stays, nothing is written.
    QTest::keyClick(&e, Qt::Key_Down);
    CHECK(e.textCursor().blockNumber() == 1);
    CHECK(e.textCursor().positionInBlock() == 2);
    CHECK(AppTestAccess::mainVirtualSpaces(e) == 3);
    CHECK(e.cursorWidth() == 0); // drawn past the end by the editor
    QTest::keyClick(&e, Qt::Key_Down);
    CHECK(e.textCursor().blockNumber() == 2);
    CHECK(e.textCursor().positionInBlock() == 5);
    CHECK(AppTestAccess::mainVirtualSpaces(e) == 0);
    CHECK(e.cursorWidth() >= 1);
    CHECK(e.toPlainText() == "abcdef\nab\nabcdef");
    CHECK(!e.document()->isModified());

    // Typing there writes the spaces before the text, one undo step.
    QTest::keyClick(&e, Qt::Key_Up);
    QTest::keyClick(&e, Qt::Key_X);
    CHECK(e.toPlainText() == "abcdef\nab   x\nabcdef");
    e.undo();
    CHECK(e.toPlainText() == "abcdef\nab\nabcdef");

    // Left and Backspace step back through the virtual space; End leaves it.
    c.setPosition(5);
    e.setTextCursor(c);
    QTest::keyClick(&e, Qt::Key_Down);
    QTest::keyClick(&e, Qt::Key_Backspace);
    QTest::keyClick(&e, Qt::Key_Left);
    CHECK(AppTestAccess::mainVirtualSpaces(e) == 1);
    CHECK(e.toPlainText() == "abcdef\nab\nabcdef");
    QTest::keyClick(&e, Qt::Key_X);
    CHECK(e.toPlainText() == "abcdef\nab x\nabcdef");
    e.undo();
    QTest::keyClick(&e, Qt::Key_End);
    CHECK(AppTestAccess::mainVirtualSpaces(e) == 0);
}

TEST_CASE("CodeTextEdit: added cursors keep the column on shorter lines", "[app][editor]") {
    CodeTextEdit e;
    e.show();
    e.setPlainText("abcd\na\nabcd");
    QTextCursor c = e.textCursor();
    c.setPosition(3);
    e.setTextCursor(c);
    QTest::keyClick(&e, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    QTest::keyClick(&e, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier);
    REQUIRE(e.extraCursorCount() == 2);
    CHECK(e.toPlainText() == "abcd\na\nabcd");
    QTest::keyClick(&e, Qt::Key_X);
    CHECK(e.toPlainText() == "abcxd\na  x\nabcxd");
    e.undo();
    CHECK(e.toPlainText() == "abcd\na\nabcd");
}

namespace {
// "ab" between two longer lines, the cursor moved down into it at column 5.
void intoVirtualSpace(CodeTextEdit& e) {
    e.setPlainText("abcdef\nab\nabcdef");
    e.document()->setModified(false);
    QTextCursor c = e.textCursor();
    c.setPosition(5);
    e.setTextCursor(c);
    QTest::keyClick(&e, Qt::Key_Down);
    REQUIRE(AppTestAccess::mainVirtualSpaces(e) == 3);
}
} // namespace

TEST_CASE("CodeTextEdit: virtual space and input methods, AltGr, read-only, empty paste",
          "[app][editor]") {
    CodeTextEdit e;
    e.show();

    // A dead key or an input method commits text without a key press.
    intoVirtualSpace(e);
    QInputMethodEvent commit;
    commit.setCommitString(QStringLiteral("ó"));
    QApplication::sendEvent(&e, &commit);
    CHECK(e.toPlainText() == QStringLiteral("abcdef\nab   ó\nabcdef"));
    e.undo();
    CHECK(e.toPlainText() == "abcdef\nab\nabcdef");

    // AltGr on Windows arrives as Ctrl+Alt: Qt inserts it, so it is padded too.
    intoVirtualSpace(e);
    QKeyEvent altGr(QEvent::KeyPress, Qt::Key_BraceLeft, Qt::ControlModifier | Qt::AltModifier,
                    QStringLiteral("{"));
    QApplication::sendEvent(&e, &altGr);
    CHECK(e.toPlainText() == "abcdef\nab   {\nabcdef");

    // Ctrl+V with nothing to paste writes nothing.
    intoVirtualSpace(e);
    QApplication::clipboard()->clear();
    QTest::keyClick(&e, Qt::Key_V, Qt::ControlModifier);
    CHECK(e.toPlainText() == "abcdef\nab\nabcdef");
    CHECK(!e.document()->isModified());
    // ... and something to paste comes after the spaces.
    intoVirtualSpace(e);
    QApplication::clipboard()->setText(QStringLiteral("zz"));
    QTest::keyClick(&e, Qt::Key_V, Qt::ControlModifier);
    CHECK(e.toPlainText() == "abcdef\nab   zz\nabcdef");

    // Turned read-only while in the virtual space: no key writes the spaces.
    intoVirtualSpace(e);
    e.setReadOnly(true);
    QTest::keyClick(&e, Qt::Key_X);
    CHECK(e.toPlainText() == "abcdef\nab\nabcdef");
    CHECK(AppTestAccess::mainVirtualSpaces(e) == 0);
}

TEST_CASE("CodeTextEdit: Up/Down count columns as shown (tabs, surrogate pairs, marks)",
          "[app][editor]") {
    CodeTextEdit e;
    e.show();
    e.setTabStopDistance(4 * QFontMetricsF(e.document()->defaultFont()).horizontalAdvance(' '));
    // A tab reaches column 4: "\tab" ends at column 6.
    e.setPlainText(QStringLiteral("\tab\n\nabcdefgh"));
    QTextCursor c = e.textCursor();
    c.setPosition(3);
    e.setTextCursor(c);
    QTest::keyClick(&e, Qt::Key_Down);
    CHECK(AppTestAccess::mainVirtualSpaces(e) == 6);
    QTest::keyClick(&e, Qt::Key_Down);
    CHECK(e.textCursor().positionInBlock() == 6);

    // An emoji is one column of two UTF-16 units: the cursor never lands between them.
    e.setPlainText(QStringLiteral("abc\n😀x"));
    c = e.textCursor();
    c.setPosition(2); // column 2
    e.setTextCursor(c);
    QTest::keyClick(&e, Qt::Key_Down);
    CHECK(e.textCursor().positionInBlock() == 3); // after "😀x"
    CHECK(AppTestAccess::mainVirtualSpaces(e) == 0);
    c.setPosition(1); // column 1
    e.setTextCursor(c);
    QTest::keyClick(&e, Qt::Key_Down);
    CHECK(e.textCursor().positionInBlock() == 2); // after the emoji, not inside it

    // A letter with its combining mark is one column.
    e.setPlainText(QStringLiteral("ab\na\u0301b"));
    c = e.textCursor();
    c.setPosition(1);
    e.setTextCursor(c);
    QTest::keyClick(&e, Qt::Key_Down);
    CHECK(e.textCursor().positionInBlock() == 2); // after "á", not between a and its mark
}
