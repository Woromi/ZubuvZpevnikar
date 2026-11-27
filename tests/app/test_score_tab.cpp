// The score tabs of a song: caret stops, the fretboard input and its tuning, New Score.
// (Nothing here needs lilypond: the preview is never built, PATH is empty in default runs.)
#include "AppTestAccess.hpp"
#include "FretboardWidget.hpp"
#include "ScoreInputBar.hpp"
#include "ScorePreviewPane.hpp"
#include "ScoreRunner.hpp"
#include "SongEditor.hpp"
#include "Support.hpp"

#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QTextBlock>
#include <QToolButton>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <zub/core/scoreregion.hpp>

using A = AppTestAccess;
namespace fs = std::filesystem;

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
        REQUIRE(A::openExistingSong(window,
                                    fixture / "songs/chords/Test" / (std::string(song) + ".tex")));
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

TEST_CASE("Fretboard: staging, Insert, Clear, double-click and keypad Enter",
          "[app][score][fretboard]") {
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

TEST_CASE("Instruments: a hidden melody gets a row of its own, voice by default",
          "[app][score][melody]") {
    ScoreWindow w("Lyr");
    QTest::qWait(300); // the staves are read on a timer
    ScorePreviewPane* pane = A::scorePreview(w.window);
    REQUIRE(apptest::waitUntil([&] {
        return !pane->findChildren<QComboBox*>(QStringLiteral("scoreInstrument")).isEmpty();
    }));
    const auto combos = pane->findChildren<QComboBox*>(QStringLiteral("scoreInstrument"));
    REQUIRE(combos.size() == 2); // the staff, the melody
    CHECK(pane->melodyChoice() == QStringLiteral("voice"));
    QComboBox* melody = combos.back();
    CHECK(melody->itemData(0).toString() == QStringLiteral("silent")); // still selectable
    melody->setCurrentIndex(0);
    CHECK(pane->melodyChoice() == QStringLiteral("silent"));
}

TEST_CASE("Score tab: Play starts at the cursor's note and the cursor follows the music",
          "[app][score][playback]") {
    ScoreWindow s;
    auto* player = new apptest::SilentPlayer;
    A::replacePlayer(s.window, player);
    SongEditor* ed = s.editor();
    const auto events = zub::scoreregion::musicEvents(ed->editorText());
    REQUIRE(events.size() > 4);

    // An injected timeline (the build would give it): the first events at 0, 1, 2 s; the second
    // event again at 3 s (a repeat); the last one at 4 s.
    auto& timeline = A::timeline(s.window);
    timeline.firstBegin = events[0].begin;
    timeline.cues = {
        {0.0, events[0]}, {1.0, events[1]}, {2.0, events[2]}, {3.0, events[1]}, {4.0, events[4]}};
    A::setScoreLinksFresh(s.window, true);

    // The cursor's note, its first pass; before the music or no cursor: the start.
    ed->setCursorByteOffset(events[2].begin, false);
    CHECK(A::startSeconds(s.window) == 2.0);
    ed->setCursorByteOffset(events[1].begin + 1, false); // inside the repeated note
    CHECK(A::startSeconds(s.window) == 1.0);
    ed->setCursorByteOffset(events[1].end,
                            false); // right behind it: that note (where Stop leaves it)
    CHECK(A::startSeconds(s.window) == 1.0);
    ed->setCursorByteOffset(events[1].end + 1, false); // in the gap before the next: the next one
    CHECK(A::startSeconds(s.window) == 2.0);
    ed->setCursorByteOffset(0, false);
    CHECK(A::startSeconds(s.window) == 0.0);
    // A selection starts where it starts.
    ed->selectByteRange(events[2].begin, events[4].end, false);
    CHECK(A::startSeconds(s.window) == 2.0);
    // A text changed since the build: from the start.
    A::setScoreLinksFresh(s.window, false);
    CHECK(!A::startSeconds(s.window).has_value());
    A::setScoreLinksFresh(s.window, true);

    // Play from the third event: the player is told the position, the cursor follows.
    ed->setCursorByteOffset(events[2].begin, false);
    A::playFromCursor(s.window);
    PlaybackResult rendered;
    rendered.success = true;
    rendered.wav = "/nonexistent/x.wav";
    A::showPlaybackResult(s.window, rendered);
    REQUIRE(player->played.size() == 1);
    CHECK(player->played[0].first == rendered.wav);
    CHECK(player->played[0].second == 2000);
    CHECK(A::following(s.window));
    CHECK(A::followTimer(s.window)->isActive());

    A::followAt(s.window, 0.2);
    CHECK(ed->cursorByteOffset() == events[0].end);
    CHECK(!ed->selectedByteRange());
    A::followAt(s.window, 1.5);
    CHECK(ed->cursorByteOffset() == events[1].end);
    CHECK(!ed->selectedByteRange());
    A::followAt(s.window, 2.9);
    CHECK(ed->cursorByteOffset() == events[2].end);
    CHECK(!ed->selectedByteRange());
    A::followAt(s.window, 3.0); // the repeat: the second note again
    CHECK(ed->cursorByteOffset() == events[1].end);
    CHECK(!ed->selectedByteRange());
    // The cursor is right behind the note that began last: exactly at its time, and up to just
    // before the next one.
    A::followAt(s.window, 0.999);
    CHECK(ed->cursorByteOffset() == events[0].end);
    CHECK(!ed->selectedByteRange());
    A::followAt(s.window, 1.0);
    CHECK(ed->cursorByteOffset() == events[1].end);
    CHECK(!ed->selectedByteRange());
    A::followAt(s.window, 99);
    CHECK(ed->cursorByteOffset() == events[4].end);
    CHECK(!ed->selectedByteRange());
    CHECK(A::following(s.window));

    // Stop, then Play: resumes at the note that was sounding (the one just before the caret).
    CHECK(A::startSeconds(s.window) == 4.0);

    // The user clicks into the text (or the preview): the player restarts at that note (the
    // cursor before event 2: 2 s) and the following goes on from there.
    player->played.clear();
    ed->setCursorByteOffset(events[2].begin, false);
    A::followAt(s.window, 4.5);
    REQUIRE(player->played.size() == 1);
    CHECK(player->played[0].first == rendered.wav);
    CHECK(player->played[0].second == 2000);
    CHECK(A::following(s.window));
    CHECK(A::followTimer(s.window)->isActive());
    CHECK(ed->cursorByteOffset() == events[2].end); // the caret is behind the note that sounds
    // A selection (a drag over notes): from its first note.
    ed->selectByteRange(events[1].begin, events[2].end, false);
    A::followAt(s.window, 2.5);
    REQUIRE(player->played.size() == 2);
    CHECK(player->played[1].second == 1000);
    CHECK(A::following(s.window));
    CHECK(!ed->selectedByteRange());
    // An edit stops the following and the playback.
    QTextCursor typed = ed->textEdit()->textCursor();
    typed.movePosition(QTextCursor::End);
    typed.insertText(" ");
    CHECK(!A::following(s.window));
    CHECK(!player->playing);
    CHECK(!A::followTimer(s.window)->isActive());
    // A caret the user put right behind a note starts at that note, in front of it too.
    A::setScoreLinksFresh(s.window, true);
    ed->setCursorByteOffset(events[1].end, false);
    CHECK(A::startSeconds(s.window) == 1.0);
}

TEST_CASE("Score tab: Play passes the start to the player; Stop and the end leave the cursor",
          "[app][score][playback]") {
    ScoreWindow s;
    auto* player = new apptest::SilentPlayer;
    A::replacePlayer(s.window, player);
    SongEditor* ed = s.editor();
    const auto events = zub::scoreregion::musicEvents(ed->editorText());
    REQUIRE(events.size() > 3);
    auto& timeline = A::timeline(s.window);
    timeline.firstBegin = events[0].begin;
    timeline.cues = {{0.0, events[0]}, {1.0, events[1]}, {2.5, events[2]}};
    A::setScoreLinksFresh(s.window, true);

    // MainWindow-free stand-in of playScore: the start comes from the cursor.
    ed->setCursorByteOffset(events[2].begin, false);
    A::playFromCursor(s.window);
    PlaybackResult rendered;
    rendered.success = true;
    rendered.wav = "/nonexistent/x.wav";
    A::showPlaybackResult(s.window, rendered);
    REQUIRE(player->played.size() == 1);
    CHECK(player->played[0].second == 2500);

    // The next position: the cursor follows; Stop leaves it at the last played note.
    player->position = 2600;
    A::followAt(s.window, 2.6);
    CHECK(ed->cursorByteOffset() == events[2].end);
    A::followAt(s.window, 0.4);
    CHECK(ed->cursorByteOffset() == events[0].end);
    A::stopPlayback(s.window);
    CHECK(!A::following(s.window));
    CHECK(A::startSeconds(s.window) == 0.0); // Play again: at the note that was sounding
    A::followAt(s.window, 2.6);              // no longer following
    CHECK(ed->cursorByteOffset() == events[0].end);

    // An edit while playing ends the following too.
    A::playFromCursor(s.window);
    A::showPlaybackResult(s.window, rendered);
    CHECK(A::following(s.window));
    QTextCursor c = ed->textEdit()->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(" ");
    CHECK(!A::scoreLinksFresh(s.window));
    A::followAt(s.window, 1.0);
    CHECK(!A::following(s.window));

    // Played to the end: the cursor stays at the last note.
    A::setScoreLinksFresh(s.window, true);
    A::playFromCursor(s.window);
    A::showPlaybackResult(s.window, rendered);
    A::followAt(s.window, 1.2);
    const auto at = ed->cursorByteOffset();
    emit player->finished();
    CHECK(!A::following(s.window));
    CHECK(ed->cursorByteOffset() == at);

    // Play again with the caret where the end left it: the whole score from 0, not the last note.
    player->played.clear();
    A::playFromCursor(s.window);
    A::showPlaybackResult(s.window, rendered);
    REQUIRE(player->played.size() == 1);
    CHECK(player->played[0].second == 0);
    // The caret moved after the end: Play starts at that note as usual.
    A::followAt(s.window, 1.2);
    emit player->finished();
    ed->setCursorByteOffset(events[2].begin, false);
    player->played.clear();
    A::playFromCursor(s.window);
    A::showPlaybackResult(s.window, rendered);
    REQUIRE(player->played.size() == 1);
    CHECK(player->played[0].second == 2500);
}

namespace {

// 1-based line and 0-based column (code points) of byte `offset` of `text`: a link's position.
QPoint linkPosition(const std::string& text, std::size_t offset) {
    const int line = 1 + static_cast<int>(std::ranges::count(
                             text.begin(), text.begin() + static_cast<long>(offset), '\n'));
    const auto lineStart = text.rfind('\n', offset == 0 ? 0 : offset - 1);
    const std::size_t from = lineStart == std::string::npos || offset == 0 ? 0 : lineStart + 1;
    const int column =
        static_cast<int>(QString::fromStdString(text.substr(from, offset - from)).size());
    return {column, line};
}

} // namespace

// The orange box of a sounding lyric or chord name is as wide as the text as the renderer draws it,
// for long words and for wide letters alike (not an estimate from the character count).
TEST_CASE("Score tab: the sounding box covers the rendered text", "[app][score][playback]") {
    ScoreWindow s("Sing");
    const fs::path copy = "/nonexistent/copy/Test/Sing.ly";
    const std::vector<std::string> words{"la", "nejkrásnější", "WWWWMMMM", "Cmaj7/G#",
                                         "iiiiiiiiii"};
    QByteArray svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
        "width=\"400\" height=\"200\" viewBox=\"0 0 400 200\">";
    for (std::size_t i = 0; i < words.size(); ++i)
        svg += QStringLiteral(
                   "<a xlink:href=\"textedit://%1:%2:0:2\"><g transform=\"translate(10, %3)\">"
                   "<text font-family=\"%4\" font-size=\"10\" text-anchor=\"start\" "
                   "fill=\"currentColor\"><tspan>%5</tspan></text></g></a>")
                   .arg(toQString(copy))
                   .arg(i + 1)
                   .arg(20 + 35 * i)
                   .arg(i == 3 ? "sans-serif" : "serif")
                   .arg(QString::fromStdString(words[i]))
                   .toUtf8();
    svg += "</svg>";
    QTest::qWait(300);
    ScoreResult result;
    result.success = true;
    result.svg = svg;
    result.inputName = s.tab().name;
    result.scoreCopy = copy;
    A::showScoreResult(s.window, result);
    ScorePreviewPane* pane = A::scorePreview(s.window);
    QWidget* view = pane->scoreView();
    view->resize(800, view->heightForWidth(800));
    QList<ScorePreviewPane::SoundingText> texts;
    for (std::size_t i = 0; i < words.size(); ++i)
        texts.append({QPoint(0, static_cast<int>(i) + 1), static_cast<int>(words[i].size())});
    pane->setSoundingNotes({}, texts);
    const QImage plain = [&] {
        pane->setSoundingNotes({});
        const QImage image = view->grab().toImage();
        pane->setSoundingNotes({}, texts);
        return image;
    }();
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto box = pane->soundingBox(QPoint(0, static_cast<int>(i) + 1));
        REQUIRE(box.has_value());
        // The dark pixels of the text inside the box's rows.
        int left = plain.width(), right = -1;
        for (int y = static_cast<int>(box->top()); y < static_cast<int>(box->bottom()); ++y)
            for (int x = 0; x < plain.width(); ++x)
                if (qGray(plain.pixel(x, y)) < 100) {
                    left = std::min(left, x);
                    right = std::max(right, x);
                }
        INFO(words[i]);
        REQUIRE(right >= 0);
        CHECK(box->left() <= left);
        CHECK(box->right() >= right);
        CHECK(box->width() < static_cast<double>(right - left) + 30); // and not far too wide
    }
}

TEST_CASE("Score tab: everything that sounds is highlighted in the preview, in all parts",
          "[app][score][playback]") {
    ScoreWindow s("Sing");
    auto* player = new apptest::SilentPlayer;
    A::replacePlayer(s.window, player);
    SongEditor* ed = s.editor();
    const std::string text = ed->editorText();
    const auto range = [&](std::string_view needle, std::size_t length = 0) {
        const auto at = text.find(needle);
        REQUIRE(at != std::string::npos);
        return zub::scoreregion::Range{at, at + (length ? length : needle.size())};
    };
    const auto chord = range("<e, e>2");
    const auto chordName = range("c1");
    const auto syllable = range("la");
    const auto nextNote = range("g'2");
    const auto nextName = range("g1:m7");
    const auto nextSyllable = range("lo");

    // A preview built from these texts: a link at the start of each (and one in the chord's
    // second note), and one in a place no sound is logged for.
    const fs::path copy = "/nonexistent/copy/Test/Sing.ly";
    QByteArray svg =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
        "width=\"300\" height=\"100\" viewBox=\"0 0 300 100\">";
    int x = 10;
    const auto addLink = [&](std::size_t offset) {
        const QPoint p = linkPosition(text, offset);
        svg +=
            QStringLiteral("<a xlink:href=\"textedit://%1:%2:%3:%4\"><g transform=\"translate(%5, "
                           "%6)\"><path d=\"M0,0 L4,0\"/></g></a>")
                .arg(toQString(copy))
                .arg(p.y())
                .arg(p.x())
                .arg(p.x() + 1)
                .arg(x)
                .arg(40)
                .toUtf8();
        x += 20;
        return p;
    };
    const QPoint chordLink = addLink(chord.begin);
    addLink(chord.begin + 4); // the chord's second note
    const QPoint nameLink = addLink(chordName.begin);
    const QPoint syllableLink = addLink(syllable.begin);
    const QPoint nextNoteLink = addLink(nextNote.begin);
    const QPoint nextNameLink = addLink(nextName.begin);
    const QPoint nextSyllableLink = addLink(nextSyllable.begin);
    const QPoint elsewhere = addLink(text.find("\\tabFullNotation"));
    svg += "</svg>";
    Q_UNUSED(chordLink);
    Q_UNUSED(nextNoteLink);
    Q_UNUSED(nextNameLink);
    Q_UNUSED(nextSyllableLink);
    Q_UNUSED(nameLink);
    Q_UNUSED(syllableLink);
    Q_UNUSED(elsewhere);

    QTest::qWait(300); // the preview build timer; no lilypond here, nothing comes of it
    ScoreResult result;
    result.success = true;
    result.svg = svg;
    result.inputName = s.tab().name;
    result.scoreCopy = copy;
    A::showScoreResult(s.window, result);
    REQUIRE(A::scoreLinksFresh(s.window));
    ScorePreviewPane* pane = A::scorePreview(s.window);
    CHECK(pane->soundingNoteCount() == 0);

    // An injected timeline: two bars of parts that begin together.
    auto& timeline = A::timeline(s.window);
    using zub::scoretiming::Kind;
    timeline.firstBegin = chord.begin;
    timeline.cues = {{0.0, chord}, {1.0, nextNote}};
    timeline.sounds = {
        {0.0, 1.0, Kind::Note, chord},         {0.0, 1.0, Kind::ChordName, chordName},
        {0.0, 0.5, Kind::Lyric, syllable},     {1.0, 2.0, Kind::Note, nextNote},
        {1.0, 2.0, Kind::ChordName, nextName}, {1.0, 1.5, Kind::Lyric, nextSyllable}};
    // The hidden melody's note (no link in the preview): marked in the text only.
    const std::size_t melodyAt = text.find("\\score");
    REQUIRE(melodyAt != std::string::npos);
    timeline.sounds.push_back({0.0, 0.5, Kind::Melody, {melodyAt, melodyAt + 6}});
    const auto marked = [&] {
        QStringList texts;
        for (const auto& mark : ed->textEdit()->extraSelections())
            texts.append(mark.cursor.selectedText());
        texts.sort();
        return texts;
    };
    CHECK(marked().isEmpty());

    ed->setCursorByteOffset(0, false);
    A::playFromCursor(s.window);
    PlaybackResult rendered;
    rendered.success = true;
    rendered.wav = "/nonexistent/x.wav";
    A::showPlaybackResult(s.window, rendered);
    REQUIRE(A::following(s.window));

    // 0.2 s: the chord (two links: both notes), its name, the syllable. The editor's cursor is a
    // plain cursor right behind the chord (no selection); the text has marks on everything that
    // sounds (the chord, its name, the syllable, the melody note); the preview has the same cue's
    // caret line at once (no highlight timer), and the boxes.
    A::followAt(s.window, 0.2);
    CHECK(pane->soundingNoteCount() == 4);
    CHECK(!ed->selectedByteRange());
    CHECK(ed->cursorByteOffset() == chord.end);
    CHECK(pane->hasCaret()); // no qWait: the same tick
    CHECK(pane->highlightedNoteCount() == 0);
    const auto word = [&](const zub::scoreregion::Range& r) {
        return QString::fromStdString(text.substr(r.begin, r.end - r.begin));
    };
    QStringList expected{word(chord), word(chordName), word(syllable), QStringLiteral("\\score")};
    expected.sort();
    CHECK(marked() == expected);
    // 0.7 s: the syllable and the melody note are over, the rest goes on.
    A::followAt(s.window, 0.7);
    CHECK(pane->soundingNoteCount() == 3);
    CHECK(marked().size() == 2);
    // 1.2 s: the next note, chord name and syllable (three parts); the cursor behind the note.
    A::followAt(s.window, 1.2);
    CHECK(pane->soundingNoteCount() == 3);
    CHECK(ed->cursorByteOffset() == nextNote.end);
    CHECK(marked().size() == 3);
    // 1.8 s: no syllable.
    A::followAt(s.window, 1.8);
    CHECK(pane->soundingNoteCount() == 2);
    CHECK(marked().size() == 2);

    // Stop clears the boxes and the marks (the cursor stays behind the last note).
    A::stopPlayback(s.window);
    CHECK(pane->soundingNoteCount() == 0);
    CHECK(marked().isEmpty());
    CHECK(ed->cursorByteOffset() == nextNote.end);

    // The end of the audio clears them too, and so does an edit while playing.
    A::playFromCursor(s.window);
    A::showPlaybackResult(s.window, rendered);
    A::followAt(s.window, 0.2);
    CHECK(pane->soundingNoteCount() == 4);
    emit player->finished();
    CHECK(pane->soundingNoteCount() == 0);
    CHECK(marked().isEmpty());
    A::setScoreLinksFresh(s.window, true);
    A::playFromCursor(s.window);
    A::showPlaybackResult(s.window, rendered);
    A::followAt(s.window, 0.2);
    CHECK(pane->soundingNoteCount() == 4);
    QTextCursor c = ed->textEdit()->textCursor();
    c.movePosition(QTextCursor::End);
    c.insertText(" ");
    A::followAt(s.window, 0.3);
    CHECK(pane->soundingNoteCount() == 0);
    CHECK(marked().isEmpty());
}

TEST_CASE("Instruments: the distorted guitar can be saved to the file", "[app][score]") {
    ScoreWindow w("Tun"); // two staves: a row and a Save to file button each
    ScorePreviewPane* pane = A::scorePreview(w.window);
    REQUIRE(apptest::waitUntil([&] {
        return pane->findChildren<QComboBox*>(QStringLiteral("scoreInstrument")).size() >= 2;
    }));
    QComboBox* combo = pane->findChildren<QComboBox*>(QStringLiteral("scoreInstrument")).front();
    QPushButton* save =
        pane->findChildren<QPushButton*>(QStringLiteral("scoreInstrumentSave")).front();
    combo->setCurrentIndex(combo->findData(QStringLiteral("amp")));
    REQUIRE(save->isEnabled());
    save->click();
    CHECK(apptest::waitUntil([&] {
        return w.text().contains(QStringLiteral("midiInstrument = \"distorted guitar\""));
    }));
}

TEST_CASE("Score tab: note input keys work on the preview and the fretboard, not in the text",
          "[app][score]") {
    ScoreWindow s;
    ScoreInputBar* bar = s.tab().inputBar;
    REQUIRE(bar != nullptr);
    // The selection buttons are shown even with the fretboard off.
    auto* toggle = s.tab().page->findChild<QToolButton*>("scoreFretboardToggle");
    REQUIRE(toggle != nullptr);
    if (toggle->isChecked())
        toggle->click();
    CHECK(s.tab().inputPanel->isHidden());
    auto* tie = s.tab().page->findChild<QToolButton*>("scoreRegionTie");
    REQUIRE(tie != nullptr);
    CHECK(!tie->isHidden());
    toggle->click();
    CHECK(!s.tab().inputPanel->isHidden());

    QSignalSpy rests(bar, &ScoreInputBar::restRequested);
    QSignalSpy bars(bar, &ScoreInputBar::barCheckRequested);

    QWidget* canvas = A::scorePreview(s.window)->canvasWidget();
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_8);
    CHECK(bar->duration() == "8");
    QTest::keyClick(canvas, Qt::Key_Period);
    CHECK(bar->duration() == "8.");
    QTest::keyClick(canvas, Qt::Key_6);
    CHECK(bar->duration() == "16.");
    QTest::keyClick(canvas, Qt::Key_Period);
    QTest::keyClick(canvas, Qt::Key_2);
    CHECK(bar->duration() == "2");
    QTest::keyClick(canvas, Qt::Key_R);
    CHECK(rests.size() == 1);
    QTest::keyClick(canvas, Qt::Key_Bar, Qt::ShiftModifier);
    CHECK(bars.size() == 1);

    QTest::keyClick(s.tab().fretboard, Qt::Key_4);
    CHECK(bar->duration() == "4");

    // In the text editor the digit is text.
    s.editor()->textEdit()->setFocus();
    const auto before = s.editor()->textEdit()->toPlainText().size();
    QTest::keyClick(s.editor()->textEdit(), Qt::Key_8);
    CHECK(s.editor()->textEdit()->toPlainText().size() == before + 1);
    CHECK(bar->duration() == "4");
}
