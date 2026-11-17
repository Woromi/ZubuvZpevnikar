// CodeTextEdit: Enter, Shift+Enter and Ctrl+Enter, also with several cursors.
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

TEST_CASE("CodeTextEdit: Shift+Enter opens a line below, Ctrl+Enter above", "[app][editor]") {
    CodeTextEdit e;
    e.show();

    reset(e);
    QTest::keyClick(&e, Qt::Key_Return, Qt::ShiftModifier);
    CHECK(e.toPlainText() == "a\n    bc\n    \nd");
    CHECK(e.textCursor().blockNumber() == 2);

    reset(e);
    QTest::keyClick(&e, Qt::Key_Return, Qt::ControlModifier);
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
