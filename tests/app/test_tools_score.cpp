// Hidden [.tools] cases: the score preview's mouse and keyboard editing needs lilypond (the SVG
// links of the preview). Run: build/debug/tests/app/zub_app_tests "[.tools]"
#include <QAction>
#include <QPlainTextEdit>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTest>
#include <QTextBlock>
#include <QToolButton>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <zub/core/fretboard.hpp>
#include <zub/core/scoreregion.hpp>

#include "AppTestAccess.hpp"
#include "FretboardWidget.hpp"
#include "ScoreInputBar.hpp"
#include "ScorePreviewPane.hpp"
#include "SongEditor.hpp"
#include "Support.hpp"

using A = AppTestAccess;
namespace sr = zub::scoreregion;

namespace {

// Test/Cz open, its score tab shown, the lilypond preview built (so the note links are fresh).
struct LinkedScore {
    LinkedScore() : fixture(apptest::Fixture::Extras::Yes) {
        if (QStandardPaths::findExecutable("lilypond").isEmpty())
            SKIP("lilypond not found");
        window.resize(1500, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        REQUIRE(A::openSongbook(window, fixture.root()));
        REQUIRE(A::openExistingSong(window, fixture / "songs/chords/Test/Cz.tex"));
        A::syncScoreTabs(window);
        A::tabs(window)->setCurrentIndex(1);
        REQUIRE(A::scoreTabs(window).size() == 1);
        ed = A::scoreTabs(window)[0].editor;
        REQUIRE(ed != nullptr);
        REQUIRE(waitFresh());
        pane = A::scorePreview(window);
        view = pane->scoreView();
        canvas = pane->canvasWidget();
        orig = ed->editorText();
        events0 = sr::musicEvents(orig);
    }
    bool waitFresh() {
        return apptest::waitUntil(
            [&] { return A::scoreLinksFresh(window) && !A::scorePreview(window)->notePositions().isEmpty(); },
            40000);
    }
    void refresh() {
        QTest::qWait(1500);
        REQUIRE(waitFresh());
    }
    std::string text() { return ed->editorText(); }
    // The widget position of an event's link: left of the note (before it), or right (behind).
    QPoint widgetOf(std::size_t event) {
        const QPoint p = A::eventLink(window).at(event);
        return pane->noteViewPosition(p.y(), p.x()) + QPoint(-5, 0);
    }
    QPoint behind(std::size_t event) { return widgetOf(event) + QPoint(10, 0); }
    std::size_t eventOfText(const std::string& needle) {
        const auto off = text().find(needle);
        REQUIRE(off != std::string::npos);
        const auto idx = sr::eventAt(A::scoreEvents(window), off);
        REQUIRE(idx.has_value());
        return *idx;
    }
    void key(Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) {
        QTest::keyClick(canvas, k, m);
        QTest::qWait(50);
    }
    QString selected() { return ed->textEdit()->textCursor().selectedText().replace(QChar(0x2029), '\n'); }

    apptest::Fixture fixture;
    MainWindow window;
    SongEditor* ed = nullptr;
    ScorePreviewPane* pane = nullptr;
    QWidget* view = nullptr;
    QWidget* canvas = nullptr;
    std::string orig;
    std::vector<sr::Range> events0;
};

using Range = std::pair<std::size_t, std::size_t>;

} // namespace

TEST_CASE("Score preview keyboard: Backspace, Delete, undo, redo", "[.tools][app][score]") {
    LinkedScore s;
    const std::size_t ch = s.eventOfText("<d\\4 g\\3 d'\\2 g'\\1>8");

    // Click behind a chord: the focus goes to the preview, the cursor after the chord.
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.behind(ch));
    QTest::qWait(300);
    CHECK(s.canvas->hasFocus());
    CHECK(!s.ed->textEdit()->hasFocus());
    CHECK(s.ed->cursorByteOffset() == s.events0[ch].end);
    CHECK(s.pane->hasCaret());

    s.key(Qt::Key_Backspace); // removes the chord
    const std::string now = s.text();
    CHECK(sr::musicEvents(now).size() == s.events0.size() - 1);
    CHECK(now.size() < s.orig.size());
    CHECK(s.canvas->hasFocus());
    CHECK(s.ed->cursorByteOffset() <= s.events0[ch].begin);
    s.key(Qt::Key_Z, Qt::ControlModifier); // one undo restores it
    CHECK(s.text() == s.orig);
    s.key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier); // redo
    CHECK(s.text() == now);
    s.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(s.text() == s.orig);
    s.key(Qt::Key_Y, Qt::ControlModifier);
    CHECK(s.text() == now);
    s.key(Qt::Key_Z, Qt::ControlModifier);
    s.refresh();

    // Delete removes the event after the caret.
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(ch));
    QTest::qWait(300);
    CHECK(s.ed->cursorByteOffset() == s.events0[ch].begin);
    s.key(Qt::Key_Delete);
    const std::string deleted = s.text();
    CHECK(sr::musicEvents(deleted).size() == s.events0.size() - 1);
    CHECK(deleted.substr(0, s.events0[ch].begin) == s.orig.substr(0, s.events0[ch].begin));
    s.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(s.text() == s.orig);
    s.ed->textEdit()->document()->setModified(false);
}

TEST_CASE("Score preview keyboard: Left/Right and Shift select events, Home/End", "[.tools][app][score]") {
    LinkedScore s;
    const std::size_t ch = s.eventOfText("<d\\4 g\\3 d'\\2 g'\\1>8");
    const auto& ev = s.events0;
    const auto sel = [&] { return s.ed->selectedByteRange(); };

    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(ch));
    QTest::qWait(300);
    s.key(Qt::Key_Right);
    CHECK(s.ed->cursorByteOffset() == ev[ch + 1].begin);
    s.key(Qt::Key_Left);
    CHECK(s.ed->cursorByteOffset() == ev[ch].begin);
    s.key(Qt::Key_Left);
    CHECK(s.ed->cursorByteOffset() == ev[ch - 1].begin);
    QTest::qWait(300);
    CHECK(s.pane->hasCaret());

    // Shift+Right extends one event at a time, Shift+Left shrinks.
    s.key(Qt::Key_Right, Qt::ShiftModifier);
    CHECK(sel() == std::make_optional(Range(ev[ch - 1].begin, ev[ch - 1].end)));
    s.key(Qt::Key_Right, Qt::ShiftModifier);
    s.key(Qt::Key_Right, Qt::ShiftModifier);
    CHECK(sel() == std::make_optional(Range(ev[ch - 1].begin, ev[ch + 1].end)));
    QTest::qWait(300);
    CHECK(s.pane->highlightedNoteCount() >= 3);
    s.key(Qt::Key_Left, Qt::ShiftModifier);
    CHECK(sel() == std::make_optional(Range(ev[ch - 1].begin, ev[ch].end)));
    s.key(Qt::Key_Left, Qt::ShiftModifier);
    s.key(Qt::Key_Left, Qt::ShiftModifier);
    CHECK(!sel().has_value());
    CHECK(s.ed->cursorByteOffset() == ev[ch - 1].begin);
    s.key(Qt::Key_Left, Qt::ShiftModifier); // past the anchor: selects backwards
    CHECK(sel() == std::make_optional(Range(ev[ch - 2].begin, ev[ch - 2].end)));
    s.key(Qt::Key_Left, Qt::ShiftModifier);
    CHECK(sel() == std::make_optional(Range(ev[ch - 3].begin, ev[ch - 2].end)));
    s.key(Qt::Key_Right, Qt::ShiftModifier);
    CHECK(sel() == std::make_optional(Range(ev[ch - 2].begin, ev[ch - 2].end)));
    s.key(Qt::Key_Right, Qt::ShiftModifier);
    CHECK(!sel().has_value());

    // A selection of three events + Backspace removes exactly them.
    for (int i = 0; i < 3; ++i)
        s.key(Qt::Key_Right, Qt::ShiftModifier);
    CHECK(sel().has_value());
    const std::size_t before = sr::musicEvents(s.text()).size();
    s.key(Qt::Key_Backspace);
    CHECK(sr::musicEvents(s.text()).size() == before - 3);
    CHECK(!sel().has_value());
    s.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(s.text() == s.orig);
    s.refresh();

    // Home / End: the staff line, with Ctrl the whole score.
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(ch));
    QTest::qWait(300);
    s.key(Qt::Key_Home);
    CHECK(s.ed->cursorByteOffset() <= ev[ch].begin);
    s.key(Qt::Key_End);
    CHECK(s.ed->cursorByteOffset() >= ev[ch].end);
    s.key(Qt::Key_Home, Qt::ControlModifier);
    CHECK(s.ed->cursorByteOffset() == ev.front().begin);
    s.key(Qt::Key_End, Qt::ControlModifier);
    CHECK(s.ed->cursorByteOffset() == ev.back().end);
    s.key(Qt::Key_Right); // nothing after the last stop
    CHECK(s.ed->cursorByteOffset() == ev.back().end);
    s.key(Qt::Key_Left);
    CHECK(s.ed->cursorByteOffset() == ev.back().begin);
    s.key(Qt::Key_Right);
    CHECK(s.ed->cursorByteOffset() == ev.back().end);
    // Backspace at the last stop removes the last event.
    s.key(Qt::Key_Backspace);
    CHECK(sr::musicEvents(s.text()).size() == ev.size() - 1);
    s.key(Qt::Key_Z, Qt::ControlModifier);
    CHECK(s.text() == s.orig);
    // Backspace at the very start: nothing.
    s.key(Qt::Key_Home, Qt::ControlModifier);
    s.key(Qt::Key_Backspace);
    CHECK(s.text() == s.orig);
    s.ed->textEdit()->document()->setModified(false);
}

TEST_CASE("Score preview mouse: click, drag, Shift+click and double-click select events",
          "[.tools][app][score]") {
    LinkedScore s;
    const auto& links = A::linkEvent(s.window);
    // Chord names, lyrics and the variable's definition are links but not selectable.
    const auto all = static_cast<std::size_t>(s.pane->notePositions().size());
    CHECK(links.size() < all);
    for (const auto& [key, event] : links) {
        const QString lineText = s.ed->textEdit()->document()->findBlockByNumber(key.first - 1).text();
        CHECK(!lineText.contains("chordmode"));
        CHECK(!lineText.contains("emChord ="));
    }
    CHECK(A::regionActions(s.window).size() == 7);
    for (auto* a : A::regionActions(s.window))
        CHECK(!a->isEnabled());

    // A plain click: the text cursor goes there, the caret shows.
    const std::size_t evA = s.eventOfText("a,16\\5");
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(evA));
    QTest::qWait(300);
    CHECK(!s.ed->textEdit()->textCursor().hasSelection());
    CHECK(s.pane->hasCaret());
    // A click beside the note, anywhere on its staff, puts the cursor at the same place.
    const int cursorAtNote = s.ed->textEdit()->textCursor().position();
    s.ed->selectByteRange(0, 0);
    QTest::qWait(300);
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(evA) + QPoint(-3, -9));
    QTest::qWait(300);
    CHECK(s.ed->textEdit()->textCursor().position() == cursorAtNote);
    CHECK(s.pane->hasCaret());

    // Drag from a note in the first system onto the second system.
    const std::size_t evFrom = s.eventOfText("h,\\5 <d");
    // A note of the next system (where the lines wrap depends on the preview's width).
    const QPoint from = s.widgetOf(evFrom);
    std::size_t evTo = evFrom + 4;
    const auto& linked = A::eventLink(s.window);
    while (evTo + 1 < A::scoreEvents(s.window).size() &&
           (!linked.contains(evTo) || s.behind(evTo).y() <= from.y() + 40))
        ++evTo;
    const QPoint to = s.behind(evTo);
    REQUIRE(to.y() > from.y() + 40); // another system
    QTest::mousePress(s.view, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(s.view, s.widgetOf(evFrom + 2));
    QTest::mouseMove(s.view, s.widgetOf(evFrom + 3) + QPoint(0, 25));
    QTest::mouseMove(s.view, to + QPoint(0, 12));
    QTest::mouseRelease(s.view, Qt::LeftButton, Qt::NoModifier, to + QPoint(0, 12));
    QTest::qWait(300);
    const std::string t0 = s.text();
    const auto& events = A::scoreEvents(s.window);
    const std::string want =
        t0.substr(events[evFrom].begin, events[evTo].end - events[evFrom].begin);
    CHECK(s.selected().toStdString() == want);
    CHECK(want.find("h,\\5 <d") == 0);
    CHECK(want.find("chord") == std::string::npos);
    int expectedNotes = 0;
    for (const auto& [key, event] : links)
        if (event >= evFrom && event <= evTo)
            ++expectedNotes;
    CHECK(s.pane->highlightedNoteCount() == expectedNotes);
    CHECK(!s.pane->hasCaret());
    for (auto* a : A::regionActions(s.window))
        CHECK(a->isEnabled());

    // Dragging backwards selects the same way round.
    const std::size_t evBack = s.eventOfText("a,16\\5");
    QTest::mousePress(s.view, Qt::LeftButton, Qt::NoModifier, to);
    QTest::mouseMove(s.view, s.widgetOf(evFrom));
    QTest::mouseMove(s.view, s.widgetOf(evBack));
    QTest::mouseRelease(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(evBack));
    QTest::qWait(300);
    const std::string t1 = s.text();
    CHECK(s.selected().toStdString() ==
          t1.substr(events[evBack].begin, events[evTo].end - events[evBack].begin));

    // Click, then Shift+click extends; another Shift+click keeps the first anchor.
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(evFrom));
    QTest::qWait(100);
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::ShiftModifier, s.behind(evFrom + 2));
    QTest::qWait(300);
    CHECK(s.selected().toStdString() ==
          t1.substr(events[evFrom].begin, events[evFrom + 2].end - events[evFrom].begin));
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::ShiftModifier, s.behind(evTo));
    QTest::qWait(300);
    CHECK(s.selected().toStdString() ==
          t1.substr(events[evFrom].begin, events[evTo].end - events[evFrom].begin));

    // A stroke on every selected note and chord; chord names untouched. One undo step.
    QTest::mousePress(s.view, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(s.view, s.widgetOf(evFrom + 1));
    QTest::mouseMove(s.view, to);
    QTest::mouseRelease(s.view, Qt::LeftButton, Qt::NoModifier, to);
    QTest::qWait(300);
    QAction* strokeDown = nullptr;
    for (auto* a : A::regionActions(s.window))
        if (a->objectName() == "StrokeDown")
            strokeDown = a;
    REQUIRE(strokeDown != nullptr);
    strokeDown->trigger();
    const QString after = s.ed->textEdit()->toPlainText();
    CHECK(after.count("^\\strokeDown") == static_cast<qsizetype>(evTo - evFrom + 1));
    CHECK(after.contains("chordNames = \\chordmode { e1:m a2:m d2 }"));
    CHECK(after.contains("words = \\lyricmode { la4 la8 be2 }"));
    s.ed->textEdit()->undo();
    CHECK(s.text() == s.orig);
    s.ed->textEdit()->document()->setModified(false);
}

TEST_CASE("Score preview: a click right of the last note, double-click, inserting there",
          "[.tools][app][score]") {
    LinkedScore s;
    const auto& evs = A::scoreEvents(s.window);
    // The rightmost event of the first system.
    std::size_t last = 0;
    int bestX = -1, minY = 100000;
    for (std::size_t i = 0; i < evs.size(); ++i)
        if (A::eventLink(s.window).count(i))
            minY = std::min(minY, s.widgetOf(i).y());
    for (std::size_t i = 0; i < evs.size(); ++i) {
        if (!A::eventLink(s.window).count(i))
            continue;
        const QPoint pi = s.widgetOf(i);
        if (pi.y() > minY + 110)
            continue;
        if (pi.x() > bestX) {
            bestX = pi.x();
            last = i;
        }
    }
    REQUIRE(last > 0);
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(last) + QPoint(25, 0));
    QTest::qWait(300);
    CHECK(!s.ed->textEdit()->textCursor().hasSelection());
    CHECK(s.ed->cursorByteOffset() == evs[last].end);
    CHECK(s.pane->hasCaret());

    // Inserting a fretboard note there appends after it.
    auto& tab = A::scoreTabs(s.window)[0];
    const std::string before = s.text();
    tab.fretboard->clearStaged();
    tab.fretboard->stage(1, 3);
    A::insertStagedNotes(s.window, tab.fretboard);
    QTest::qWait(100);
    const std::string now = s.text();
    CHECK(now.compare(0, evs[last].end, before, 0, evs[last].end) == 0);
    CHECK(now.size() > before.size());
    s.ed->textEdit()->undo();
    CHECK(s.text() == before);
    s.refresh();

    // A click left of a note: its begin. A double-click selects exactly that event.
    QTest::mouseClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(last));
    QTest::qWait(300);
    CHECK(s.ed->cursorByteOffset() == evs[last].begin);
    QTest::mouseDClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(last) + QPoint(5, 0));
    QTest::qWait(300);
    REQUIRE(s.ed->selectedByteRange().has_value());
    CHECK(s.ed->selectedByteRange()->first == evs[last].begin);
    CHECK(s.ed->selectedByteRange()->second == evs[last].end);
    CHECK(s.pane->highlightedNoteCount() >= 1);

    // The text cursor at the very end of the score: the caret behind the last note.
    s.ed->setCursorByteOffset(s.text().size());
    QTest::qWait(300);
    CHECK(s.pane->hasCaret());

    // An editor selection lights its notes: not the chord names, all of them for everything.
    const std::string base = s.text();
    s.ed->selectByteRange(0, 0);
    s.ed->selectByteRange(base.find("\\chordmode"), base.find("\\score"));
    QTest::qWait(300);
    CHECK(s.pane->highlightedNoteCount() == 0);
    s.ed->selectByteRange(0, base.size());
    QTest::qWait(300);
    CHECK(s.pane->highlightedNoteCount() == static_cast<int>(A::linkEvent(s.window).size()));
    s.ed->textEdit()->document()->setModified(false);
}

TEST_CASE("Fretboard shows and replaces the selected note or chord", "[.tools][app][score][fretboard]") {
    LinkedScore s;
    auto& tab = A::scoreTabs(s.window)[0];
    FretboardWidget* fb = tab.fretboard;
    const auto insertButton = [&] { return tab.inputBar->findChild<QToolButton*>("scoreInsert"); };

    // A collapsed cursor between notes: no rings, nothing staged, "Insert".
    s.ed->setCursorByteOffset(s.text().find("e16\\4"));
    QTest::qWait(300);
    CHECK(fb->marked().empty());
    CHECK(fb->staged().empty());
    CHECK(insertButton()->text() == "Insert");

    // Double-click a chord: exactly it is selected; the staged dots are its positions.
    const std::size_t ch = s.eventOfText("<d\\4 g\\3 d'\\2 g'\\1>8");
    QTest::mouseDClick(s.view, Qt::LeftButton, Qt::NoModifier, s.widgetOf(ch) + QPoint(5, 0));
    QTest::qWait(400);
    REQUIRE(fb->staged().size() == 4);
    CHECK(fb->marked().size() == 4);
    {
        auto marked = fb->marked();
        std::ranges::sort(marked, {}, &zub::fretboard::Position::string);
        CHECK(marked == fb->staged());
    }
    CHECK(insertButton()->text() == "Replace");

    // Toggle string 1's fret (g'\1 = fret 3 off), Alt+N: <d g d'>8 only.
    const std::string before = s.text();
    fb->toggleStaged(1, 3);
    QAction* act = A::scores(s.window)->insertStagedAction();
    REQUIRE(act != nullptr);
    act->trigger();
    QTest::qWait(300);
    const std::string now = s.text();
    const auto at = before.find("<d\\4 g\\3 d'\\2 g'\\1>8");
    CHECK(now.compare(0, at, before, 0, at) == 0);
    CHECK(now.compare(at, 15, "<d\\4 g\\3 d'\\2>8 <d\\4", 0, 15) == 0);
    CHECK(now.size() == before.size() - 5);
    REQUIRE(s.ed->selectedByteRange().has_value());
    CHECK(s.ed->selectedByteRange()->first == at);
    CHECK(fb->staged().size() == 3);
    CHECK(fb->marked().size() == 3);

    // Further edit: a single note, keeping the duration. One undo per replacement.
    fb->toggleStaged(2, 3);
    fb->toggleStaged(3, 0);
    act->trigger();
    QTest::qWait(300);
    CHECK(s.text().compare(at, 7, "d8\\4 <d\\", 0, 7) == 0);
    s.ed->textEdit()->undo();
    CHECK(s.text() == now);
    s.ed->textEdit()->undo();
    CHECK(s.text() == before);

    // Moving off the event clears the loaded staging.
    s.ed->setCursorByteOffset(0);
    QTest::qWait(400);
    CHECK(fb->staged().empty());
    CHECK(fb->marked().empty());
    CHECK(insertButton()->text() == "Insert");
    s.ed->textEdit()->document()->setModified(false);
}
