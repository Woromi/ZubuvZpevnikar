// Find & replace: the matching helper, the find bar of an editor, and where Ctrl+F / Ctrl+H open
// it.
#include "AppTestAccess.hpp"
#include "FindBar.hpp"
#include "FindMatches.hpp"
#include "SongFilterProxy.hpp"
#include "SongListPage.hpp"
#include "Support.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QTabWidget>
#include <QTest>
#include <QVBoxLayout>
#include <catch2/catch_test_macros.hpp>

using A = AppTestAccess;
using findmatches::findAll;
using findmatches::Options;

namespace {

QString utf8(const char* text) {
    return QString::fromUtf8(text);
}

QList<int> starts(const findmatches::Result& result) {
    QList<int> out;
    for (const auto& m : result.matches)
        out.append(m.start);
    return out;
}

// The text with its non-breaking spaces (toPlainText makes them plain) and newlines.
QString raw(const QPlainTextEdit& edit) {
    QString text = edit.document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}

// An editor with a find bar, shown and active.
struct Bar {
    explicit Bar(const QString& text) : edit(&host), bar(&edit, &host) {
        auto* layout = new QVBoxLayout(&host);
        layout->addWidget(&edit);
        layout->addWidget(&bar);
        edit.setPlainText(text);
        host.resize(700, 400);
        host.show();
        host.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&host));
    }
    void search(const QString& needle) { A::findField(bar)->setText(needle); }
    QString selected() { return edit.textCursor().selectedText(); }
    QWidget host;
    CodeTextEdit edit;
    FindBar bar;
};

} // namespace

TEST_CASE("findmatches: case and whole word, Czech letters count", "[app][find]") {
    const QString text = utf8("Píseň písně PÍSEŇ píseň1 _píseň xpíseň píseň.");
    CHECK(findAll(text, utf8("píseň"), {}).matches.size() == 6); // case-insensitive, any context
    CHECK(findAll(text, utf8("píseň"), {.matchCase = true}).matches.size() == 4);
    // Whole word: "píseň1", "_píseň" and "xpíseň" are other words.
    CHECK(starts(findAll(text, utf8("píseň"), {.wholeWord = true})) == QList<int>{0, 12, 39});
    CHECK(findAll(text, utf8("písně"), {.wholeWord = true}).matches.size() == 1);
    CHECK(findAll(text, QString(), {}).matches.isEmpty());
    // Non-overlapping.
    CHECK(findAll(QStringLiteral("aaaa"), QStringLiteral("aa"), {}).matches.size() == 2);
}

TEST_CASE("findmatches: regular expressions and their replacement", "[app][find]") {
    const QString text = QStringLiteral("\\chord{C} and \\chord{Am}");
    const Options regex{.matchCase = true, .regex = true};
    const auto result = findAll(text, QStringLiteral("\\\\chord\\{(\\w+)\\}"), regex);
    REQUIRE(result.matches.size() == 2);
    CHECK(findmatches::replacementFor(result.matches[1], QStringLiteral("[$1] $$ $0"), regex) ==
          QStringLiteral("[Am] $ \\chord{Am}"));
    // A plain search takes the text as it is.
    CHECK(findmatches::replacementFor(result.matches[1], QStringLiteral("$1"), {}) ==
          QStringLiteral("$1"));
    CHECK(!findAll(text, QStringLiteral("("), regex).error.isEmpty());
    CHECK(
        findAll(text, QStringLiteral("chord"), {.wholeWord = true, .regex = true}).matches.size() ==
        2);
    CHECK(findAll(QStringLiteral("a\nb"), QStringLiteral("^b$"), {.regex = true}).matches.size() ==
          1);
}

TEST_CASE("FindBar: next and previous wrap around, the count follows", "[app][find]") {
    Bar b(QStringLiteral("one two one\nthree one"));
    b.bar.open(false);
    CHECK(b.bar.isVisible());
    CHECK(!b.bar.replaceMode());
    b.search(QStringLiteral("one"));
    // Typing selects the first match at or after the cursor.
    CHECK(b.selected() == "one");
    CHECK(b.edit.textCursor().selectionStart() == 0);
    CHECK(b.bar.countText().contains("1 of 3"));

    b.bar.findNext();
    CHECK(b.edit.textCursor().selectionStart() == 8);
    CHECK(b.bar.countText().contains("2 of 3"));
    b.bar.findNext();
    b.bar.findNext(); // wraps
    CHECK(b.edit.textCursor().selectionStart() == 0);
    b.bar.findPrevious(); // wraps backwards
    CHECK(b.edit.textCursor().selectionStart() == 18);
    CHECK(b.bar.countText().contains("3 of 3"));

    b.search(QStringLiteral("nothing"));
    CHECK(b.bar.countText() == "No results");
    b.search(QString());
    CHECK(b.bar.countText().isEmpty());
}

TEST_CASE("FindBar: Enter and Shift+Enter go next and previous", "[app][find]") {
    Bar b(QStringLiteral("a b a b a"));
    b.bar.open(false);
    b.search(QStringLiteral("a"));
    QTest::keyClick(A::findField(b.bar), Qt::Key_Return);
    CHECK(b.edit.textCursor().selectionStart() == 4);
    QTest::keyClick(A::findField(b.bar), Qt::Key_Return, Qt::ShiftModifier);
    CHECK(b.edit.textCursor().selectionStart() == 0);
    QTest::keyClick(A::findField(b.bar), Qt::Key_F3);
    CHECK(b.edit.textCursor().selectionStart() == 4);
    QTest::keyClick(A::findField(b.bar), Qt::Key_F3, Qt::ShiftModifier);
    CHECK(b.edit.textCursor().selectionStart() == 0);
    // The field keeps the focus.
    CHECK(A::findField(b.bar)->hasFocus());
}

TEST_CASE("FindBar: match case and whole word, Czech", "[app][find]") {
    Bar b(utf8("píseň písně Píseň"));
    b.bar.open(false);
    b.search(utf8("píseň"));
    CHECK(b.bar.countText().contains("2 match"));
    A::matchCaseButton(b.bar)->click();
    CHECK(b.bar.countText().contains("1 of 1"));
    A::matchCaseButton(b.bar)->click();
    b.search(utf8("písz"));
    CHECK(b.bar.countText() == "No results");
    b.search(utf8("pís"));
    CHECK(b.bar.countText().contains("3 match"));
    A::wholeWordButton(b.bar)->click();
    CHECK(b.bar.countText() == "No results");
    b.search(utf8("písně"));
    CHECK(b.bar.countText().contains("1 of 1"));
    // An invalid expression is said so, nothing is selected.
    A::regexButton(b.bar)->click();
    b.search(QStringLiteral("("));
    CHECK(b.bar.countText() == "Invalid expression");
}

TEST_CASE("FindBar: Replace replaces the current match and moves on", "[app][find]") {
    Bar b(QStringLiteral("x one x one x"));
    b.bar.open(true);
    CHECK(b.bar.replaceMode());
    b.search(QStringLiteral("x"));
    A::replaceField(b.bar)->setText(QStringLiteral("yy"));
    A::replaceButton(b.bar)->click();
    CHECK(b.edit.toPlainText() == "yy one x one x");
    CHECK(b.selected() == "x"); // moved to the next match
    CHECK(b.edit.textCursor().selectionStart() == 7);
    A::replaceButton(b.bar)->click();
    CHECK(b.edit.toPlainText() == "yy one yy one x");
    CHECK(b.bar.countText().contains("1 of 1"));
    // The replacement is one normal edit: the document is modified, undo brings the text back.
    CHECK(b.edit.document()->isModified());
    b.edit.undo();
    CHECK(b.edit.toPlainText() == "yy one x one x");
}

TEST_CASE("FindBar: Replace All is one undo step and keeps the rest", "[app][find]") {
    Bar b(utf8("a\tb  a\u00a0a\n a \n"));
    b.bar.open(true);
    b.search(QStringLiteral("a"));
    A::replaceField(b.bar)->setText(QStringLiteral("zz"));
    A::replaceAllButton(b.bar)->click();
    CHECK(raw(b.edit) == utf8("zz\tb  zz\u00a0zz\n zz \n"));
    CHECK(b.bar.countText().contains("4 replaced"));
    b.search(utf8("\u00a0")); // a non-breaking space is not a plain one
    CHECK(b.bar.countText().contains("1 of 1"));
    b.search(QStringLiteral("a"));
    b.edit.undo();
    CHECK(raw(b.edit) == utf8("a\tb  a\u00a0a\n a \n"));
    b.edit.redo();
    CHECK(raw(b.edit) == utf8("zz\tb  zz\u00a0zz\n zz \n"));

    // A regular expression with groups.
    b.edit.setPlainText(QStringLiteral("\\chord{C}\\chord{Am}"));
    A::regexButton(b.bar)->click();
    b.search(QStringLiteral("\\\\chord\\{(\\w+)\\}"));
    A::replaceField(b.bar)->setText(QStringLiteral("\\[$1]"));
    A::replaceAllButton(b.bar)->click();
    CHECK(raw(b.edit) == "\\[C]\\[Am]");
}

TEST_CASE("FindBar: a read-only editor cannot be replaced in", "[app][find]") {
    Bar b(QStringLiteral("one one"));
    b.edit.setReadOnly(true);
    b.bar.open(true);
    CHECK(!A::replaceField(b.bar)->isEnabled());
    CHECK(!A::replaceButton(b.bar)->isEnabled());
    CHECK(!A::replaceAllButton(b.bar)->isEnabled());
    b.search(QStringLiteral("one"));
    b.bar.replaceAll(); // even called directly
    b.bar.replaceCurrent();
    CHECK(b.edit.toPlainText() == "one one");
    b.bar.findNext(); // finding still works
    CHECK(b.edit.textCursor().selectionStart() == 4);
    b.edit.setReadOnly(false);
    CHECK(A::replaceAllButton(b.bar)->isEnabled());
}

TEST_CASE("FindBar: Escape closes the bar and focuses the editor", "[app][find]") {
    Bar b(QStringLiteral("abc abc"));
    b.bar.open(false);
    b.search(QStringLiteral("abc"));
    CHECK(A::findField(b.bar)->hasFocus());
    QTest::keyClick(A::findField(b.bar), Qt::Key_Escape);
    CHECK(!b.bar.isVisible());
    CHECK(b.edit.hasFocus());
    // The matches are no longer marked; the selection stays.
    CHECK(b.edit.extraSelections().size() <= 1);

    // Escape in the editor itself still leaves the multi-cursor.
    b.edit.moveCursor(QTextCursor::Start);
    b.edit.addCursorVertically(+1);
    b.edit.setPlainText(QStringLiteral("a\nb\nc"));
    b.edit.moveCursor(QTextCursor::Start);
    b.edit.addCursorVertically(+1);
    CHECK(b.edit.extraCursorCount() == 1);
    QTest::keyClick(&b.edit, Qt::Key_Escape);
    CHECK(b.edit.extraCursorCount() == 0);
}

TEST_CASE("FindBar: the matches are marked apart from the selection's occurrences", "[app][find]") {
    Bar b(QStringLiteral("cat dog cat dog cat"));
    b.bar.open(false);
    b.search(QStringLiteral("dog"));
    // Two matches, plus the current one drawn again.
    CHECK(b.edit.extraSelections().size() == 2);
    QTest::qWait(200); // the occurrence marks of the selection join after a pause
    CHECK(b.edit.extraSelections().size() == 3);
    b.bar.dismiss();
    QTest::qWait(200);
    CHECK(b.edit.extraSelections().size() == 1);
}

TEST_CASE("Ctrl+F: the Songs page keeps its search, the song page opens the find bar",
          "[app][find]") {
    apptest::Fixture fixture;
    MainWindow w;
    w.resize(1300, 700);
    w.show();
    w.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&w));
    REQUIRE(A::openSongbook(w, fixture.root()));
    A::showView(w, ActivityBar::View::Songs);
    A::listPage(w)->setFocus();
    QTest::keyClick(&w, Qt::Key_F, Qt::ControlModifier);
    CHECK(A::pages(w)->currentWidget() == A::listPage(w));
    CHECK(A::listPage(w)->findChild<QLineEdit*>()->hasFocus());
    QTest::keyClick(&w, Qt::Key_H, Qt::ControlModifier); // nothing to replace in a list

    A::openSong(w, A::proxy(w)->index(0, 0));
    SongEditor* editor = A::editor(w);
    REQUIRE(editor != nullptr);
    FindBar* bar = editor->findBar();
    CHECK(!bar->isVisible());
    // The one-line selection becomes the search text.
    const QString text = editor->textEdit()->toPlainText();
    const auto at = static_cast<int>(text.indexOf(QStringLiteral("beginsong")));
    REQUIRE(at >= 0);
    QTextCursor c(editor->textEdit()->document());
    c.setPosition(at);
    c.setPosition(at + 9, QTextCursor::KeepAnchor);
    editor->textEdit()->setTextCursor(c);
    editor->textEdit()->setFocus();
    QTest::keyClick(&w, Qt::Key_F, Qt::ControlModifier);
    CHECK(bar->isVisible());
    CHECK(!bar->replaceMode());
    CHECK(A::findField(*bar)->text() == "beginsong");
    CHECK(A::findField(*bar)->hasFocus());
    CHECK(A::findField(*bar)->selectedText() == "beginsong");

    // Ctrl+H adds the replace row; Replace All is one undo step on the song's own document.
    QTest::keyClick(&w, Qt::Key_H, Qt::ControlModifier);
    CHECK(bar->replaceMode());
    CHECK(!editor->isModified());
    A::replaceField(*bar)->setText(QStringLiteral("beginsong"));
    A::replaceField(*bar)->setText(QStringLiteral("endsong2"));
    A::findField(*bar)->setText(QStringLiteral("endsong"));
    A::replaceAllButton(*bar)->click();
    CHECK(editor->isModified());
    CHECK(editor->content().find("endsong2") != std::string::npos);
    editor->textEdit()->undo();
    CHECK(editor->content().find("endsong2") == std::string::npos);
    CHECK(!editor->isModified());

    // The toolbar's button opens it too.
    bar->dismiss();
    A::findAction(w)->trigger();
    CHECK(bar->isVisible());

    // Escape in the bar returns to the editor.
    QTest::keyClick(A::findField(*bar), Qt::Key_Escape);
    CHECK(!bar->isVisible());
    CHECK(editor->textEdit()->hasFocus());
}

TEST_CASE("Ctrl+F on a score tab opens that score's own bar", "[app][find]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    MainWindow w;
    w.resize(1500, 900);
    w.show();
    w.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&w));
    REQUIRE(A::openSongbook(w, fixture.root()));
    REQUIRE(A::openExistingSong(w, fixture / "songs/chords/Test/Cz.tex"));
    A::syncScoreTabs(w);
    A::syncScoreTabs(w);
    REQUIRE(A::scoreTabs(w).size() == 1);
    A::tabs(w)->setCurrentIndex(1);
    SongEditor* score = A::scoreTabs(w)[0].editor;
    REQUIRE(score != nullptr);
    QTest::keyClick(&w, Qt::Key_F, Qt::ControlModifier);
    CHECK(score->findBar()->isVisible());
    CHECK(!A::editor(w)->findBar()->isVisible());
    QTest::keyClick(&w, Qt::Key_H, Qt::ControlModifier);
    CHECK(score->findBar()->replaceMode());
}

TEST_CASE("Ctrl+F on the Songbooks page opens the LaTeX tab's bar", "[app][find]") {
    apptest::Fixture fixture;
    MainWindow w;
    w.resize(1400, 800);
    w.show();
    w.activateWindow();
    REQUIRE(QTest::qWaitForWindowActive(&w));
    REQUIRE(A::openSongbook(w, fixture.root()));
    A::showView(w, ActivityBar::View::Songbooks);
    SongbooksPage* page = A::songbooksPage(w);
    auto* item = A::listItemOf(*page, QStringLiteral("songbooks/big/big.tex"));
    REQUIRE(item != nullptr);
    A::list(*page)->setCurrentItem(item);
    FindBar* bar = A::latexFind(*page);
    REQUIRE(bar != nullptr);

    A::bookTabs(*page)->setCurrentIndex(0); // the Songbook tab has no text to search
    QTest::keyClick(&w, Qt::Key_F, Qt::ControlModifier);
    CHECK(!bar->isVisible());

    A::bookTabs(*page)->setCurrentIndex(1);
    QTest::keyClick(&w, Qt::Key_F, Qt::ControlModifier);
    CHECK(bar->isVisible());
    A::findField(*bar)->setText(QStringLiteral("Beta/Bravo"));
    CHECK(bar->countText().contains("1 of 1"));
    QTest::keyClick(&w, Qt::Key_H, Qt::ControlModifier);
    CHECK(bar->replaceMode());
    A::replaceField(*bar)->setText(QStringLiteral("Beta/Brave"));
    A::replaceButton(*bar)->click();
    CHECK(A::latex(*page)->toPlainText().contains("\\input{Beta/Brave}"));
    CHECK(A::latexModified(*page));

    // A locked book is read-only: no replacing.
    A::revertLatex(*page);
    A::toggleLock(*page);
    CHECK(!A::replaceButton(*bar)->isEnabled());
    CHECK(!A::replaceAllButton(*bar)->isEnabled());
}
