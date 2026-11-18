// Hidden [.tools] cases that run the real tools: lualatex (the song preview), texlua (the
// songbook sorter), python3 (the package's linters), the converter, fluidsynth (playback).
// Run: build/debug/tests/app/zub_app_tests "[.tools]"   (each case skips what it cannot find)
#include "AppTestAccess.hpp"
#include "ConverterRunner.hpp"
#include "PreviewRunner.hpp"
#include "ScorePreviewPane.hpp"
#include "ShrinkDialog.hpp"
#include "SongFilterProxy.hpp"
#include "SongTableModel.hpp"
#include "Support.hpp"
#include "midi_events.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTest>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <set>
#include <sstream>
#include <zub/core/songheader.hpp>

using apptest::has;
using apptest::slurp;
using A = AppTestAccess;

#define REQUIRE_TOOL(name)                                                                         \
    do {                                                                                           \
        if (QStandardPaths::findExecutable(QStringLiteral(name)).isEmpty())                        \
            SKIP(name " not found");                                                               \
    } while (0)

namespace {

std::filesystem::path converterPath() {
    return std::filesystem::path(ZUB_CONVERTER_BUILT);
}

// The converter of this build as "the remembered one" in the isolated settings.
void useBuiltConverter() {
    if (!std::filesystem::exists(converterPath()))
        SKIP("chords_converter is not built (cargo missing or the submodule not checked out)");
    QSettings().setValue("converter/path", toQString(converterPath()));
}

std::vector<std::set<int>> programs(const std::filesystem::path& file) {
    std::vector<std::set<int>> result;
    for (const auto& track : testmidi::parse(slurp(file))) {
        std::set<int> used;
        bool notes = false;
        for (const auto& e : track) {
            if (e.status == 0xc0)
                used.insert(e.d1);
            notes = notes || e.noteOn();
        }
        if (notes)
            result.push_back(used);
    }
    return result;
}

} // namespace

TEST_CASE("Songbooks page: the sorter puts an added song in place and keeps blocks",
          "[.tools][app][songbooks]") {
    REQUIRE_TOOL("texlua");
    apptest::Fixture fixture;
    MainWindow w;
    w.resize(1400, 800);
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    A::showView(w, ActivityBar::View::Songbooks);
    SongbooksPage* page = A::songbooksPage(w);
    A::list(*page)->setCurrentItem(A::listItemOf(*page, "songbooks/big/big.tex"));
    const auto big = fixture / "songbooks/big/big.tex";

    // Added to a section of several songs: appended, then sorted into place.
    A::addSongs(*page, 0, {"Abba/OurLastSummer"});
    REQUIRE(apptest::waitUntil([&] { return !A::sorting(*page); }, 20000));
    std::string now = slurp(big);
    INFO(now);
    CHECK(has(now, "\\input{Abba/OurLastSummer}"));
    CHECK(has(now, "\\input{Delta/Six}\n    \\input{Delta/Seven}")); // the keep-order block

    // Sort everything: the block stays together in its order.
    A::sortAll(*page);
    REQUIRE(apptest::waitUntil([&] { return !A::sorting(*page); }, 20000));
    now = slurp(big);
    INFO(now);
    CHECK(has(now, "\\input{Delta/Six}\n    \\input{Delta/Seven}"));
    CHECK(A::sorterWarning(*page)->text().isEmpty());
}

TEST_CASE("Preview: a song builds with the bundled package, also a whistle song with its score",
          "[.tools][app][preview]") {
    REQUIRE_TOOL("lualatex");
    REQUIRE_TOOL("lilypond");
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    MainWindow w;
    w.resize(1500, 900);
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    if (A::packageDir(w).empty())
        SKIP("the bundled package is not checked out");

    REQUIRE(A::openExistingSong(w, fixture / "songs/chords/Abba/OurLastSummer.tex"));
    const auto pdf = A::previewWorkDir(w) / "preview.pdf";
    CHECK(apptest::waitUntil([&] { return std::filesystem::exists(pdf); }, 120000));

    // The whistle song's preview finds its score (songs/whistle2/Imaginary/HumMelody.ly).
    REQUIRE(A::closeSong(w));
    REQUIRE(A::openExistingSong(w, fixture / "songs/whistle2/Imaginary/Hum.tex"));
    CHECK(apptest::waitUntil([&] { return A::status(*A::preview(w))->text().contains("page"); },
                             120000));
}

TEST_CASE("Converter: Measure offers pap, is cancelled by typing and by Tidy",
          "[.tools][app][converter]") {
    REQUIRE_TOOL("lualatex");
    useBuiltConverter();
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    if (A::packageDir(w).empty())
        SKIP("the bundled package is not checked out");
    REQUIRE(A::openExistingSong(w, fixture / "songs/chords/Synthetic/Bilingual.tex"));
    A::tabs(w)->setCurrentIndex(0);
    QTest::qWait(300); // the opened song settles (a late text change would cancel the measurement)
    SongEditor* ed = A::editor(w);
    REQUIRE(ed != nullptr);
    REQUIRE(!zub::songheader::option(ed->content(), "pap").has_value());

    // No modal box comes up during a measurement; the bar offers to mark the fitting papers.
    bool boxSeen = false;
    auto watcher = apptest::Responder(
        [&](QWidget* top) {
            boxSeen = boxSeen || qobject_cast<QDialog*>(top) != nullptr;
            return false;
        },
        [](QWidget*) {});
    std::string converterOut;
    QObject::connect(A::converterRunner(w), &ConverterRunner::finished, &w,
                     [&](const std::expected<ConverterOutput, QString>& r) {
                         converterOut =
                             r ? "exit " + std::to_string(r->exitCode) + "\n" + r->out + r->err
                               : r.error().toStdString();
                     });
    A::startMeasure(w, ed);
    CHECK(A::converterRunner(w)->isRunning());
    REQUIRE(apptest::waitUntil([&] { return !A::converterRunner(w)->isRunning(); }, 120000));
    apptest::waitUntil([&] { return !converterOut.empty(); }, 5000);
    INFO(converterOut);
    CHECK(apptest::waitUntil([&] { return ed->suggestionShown(); }, 5000));
    CHECK(!boxSeen);
    if (ed->suggestionShown())
        QTest::mouseClick(ed->findChild<QWidget*>("suggestionBar")->findChild<QPushButton*>(),
                          Qt::LeftButton);
    CHECK(zub::songheader::option(ed->content(), "pap").has_value());
    ed->textEdit()->undo();

    // Typing cancels the measurement; the song actions stay available.
    A::startMeasure(w, ed);
    CHECK(A::converterRunner(w)->isRunning());
    CHECK(A::tidyAction(w)->isEnabled());
    CHECK(A::checkAction(w)->isEnabled());
    ed->textEdit()->insertPlainText("z");
    CHECK(!A::converterRunner(w)->isRunning());
    QTest::qWait(1000);
    CHECK(!ed->suggestionShown());

    // Tidy takes over from a running measurement.
    A::startMeasure(w, ed);
    CHECK(A::converterRunner(w)->isRunning());
    A::startTidy(w);
    CHECK(A::tidyRunning(w));
    CHECK(A::converterRunner(w)->isRunning());
    CHECK(apptest::waitUntil([&] { return !A::converterRunner(w)->isRunning(); }, 30000));
    ed->textEdit()->document()->setModified(false);
}

TEST_CASE("Check Songs: the key check offers the key and a nocheck comment",
          "[.tools][app][converter]") {
    REQUIRE_TOOL("python3");
    useBuiltConverter();
    apptest::Fixture fixture;
    {
        // Our Last Summer without a key (the check finds Am), Montgomery with a wrong one.
        const auto summer = fixture / "songs/chords/Abba/OurLastSummer.tex";
        std::string text = slurp(summer);
        const auto at = text.find("    key={D},\n");
        REQUIRE(at != std::string::npos);
        text.erase(at, std::string("    key={D},\n").size());
        apptest::writeFile(summer, text);
        const auto montgomery = fixture / "songs/chords/WabiDanek/Montgomery.tex";
        text = slurp(montgomery);
        text.replace(text.find("key={D}"), 7, "key={Fm}");
        apptest::writeFile(montgomery, text);
    }
    MainWindow w;
    w.resize(1300, 700);
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    if (A::packageDir(w).empty())
        SKIP("the bundled package is not checked out");

    // The dialog: only the Keys and Metadata script checks.
    auto dialog = apptest::answerTitledDialog("Check Songs", [](QDialog* d) {
        for (auto* box : d->findChildren<QCheckBox*>()) {
            const QString text = box->text();
            box->setChecked(text.startsWith("Keys") || text.startsWith("Metadata"));
        }
        for (auto* b : d->findChildren<QPushButton*>())
            if (b->text() == "Check") {
                b->click();
                return;
            }
    });
    A::scanSongbook(w);
    REQUIRE(dialog.fired());
    REQUIRE(apptest::waitUntil([&] { return !A::scanRunning(w); }, 60000));

    const auto row = [&](const char* name) {
        return A::model(w)->rowOf(fixture / "songs/chords" / name);
    };
    const auto changes = [&](const char* name) {
        return A::model(w)->index(row(name), SongTableModel::Changes).data().toString();
    };
    CHECK(changes("Abba/OurLastSummer.tex").contains("Key"));
    CHECK(changes("WabiDanek/Montgomery.tex").contains("Key"));
    CHECK(A::keyFindings(w).size() == 2);

    // Our Last Summer: the bar offers the key; Write writes it, one undo step.
    A::openSong(w,
                A::proxy(w)->mapFromSource(A::model(w)->index(row("Abba/OurLastSummer.tex"), 0)));
    SongEditor* ed = A::editor(w);
    REQUIRE(ed != nullptr);
    REQUIRE(apptest::waitUntil([&] { return ed->suggestionShown(); }, 5000));
    A::suggestionButton(*ed)->click();
    CHECK(has(ed->content(), "key={Am}"));
    ed->textEdit()->undo();
    CHECK(!has(ed->content(), "key={"));
    ed->textEdit()->document()->setModified(false);

    // Montgomery: keep the written key: a "% nocheck key" line above it.
    A::openSong(w,
                A::proxy(w)->mapFromSource(A::model(w)->index(row("WabiDanek/Montgomery.tex"), 0)));
    ed = A::editor(w);
    REQUIRE(ed != nullptr);
    REQUIRE(apptest::waitUntil([&] { return ed->suggestionShown(); }, 5000));
    REQUIRE(A::suggestionOtherButton(*ed)->isVisible());
    A::suggestionOtherButton(*ed)->click();
    CHECK(has(ed->content(), "    % nocheck key\n    key={Fm},"));
    CHECK(!A::keyFindings(w).contains(ed->path()));
    ed->textEdit()->document()->setModified(false);
}

namespace {

// A window with Test/Cz's score tab open and its playback ready (fluidsynth and a soundfont).
struct PlayableScore {
    // `song`: Test/Cz (one staff), Test/Two (a banjo and a guitar tab staff, chord names).
    explicit PlayableScore(const char* song = "Cz") : fixture(apptest::Fixture::Extras::Yes) {
        REQUIRE_TOOL("lilypond");
        REQUIRE_TOOL("fluidsynth");
        window.resize(1500, 900);
        window.show();
        window.activateWindow();
        REQUIRE(QTest::qWaitForWindowActive(&window));
        REQUIRE(A::openSongbook(window, fixture.root()));
        REQUIRE(A::openExistingSong(window,
                                    fixture / "songs/chords/Test" / (std::string(song) + ".tex")));
        A::syncScoreTabs(window);
        A::syncScoreTabs(window);
        A::tabs(window)->setCurrentIndex(1);
        pane = A::scorePreview(window);
        play = qobject_cast<QToolButton*>(pane->playButton());
        REQUIRE(play != nullptr);
        if (!waitPlay())
            SKIP("no soundfont for fluidsynth, or the playback never became ready");
    }
    bool waitPlay() {
        QTest::qWait(300);
        return apptest::waitUntil([&] { return play->isEnabled(); }, 60000);
    }
    QDir cache() const {
        return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/playback");
    }
    void clearCache() {
        for (const auto& f : cache().entryList({"*.wav"}))
            cache().remove(f);
    }
    // Plays and waits for the rendered WAV; stops again.
    QFileInfoList renderOnce() {
        clearCache();
        QTest::mouseClick(play, Qt::LeftButton);
        apptest::waitUntil([&] { return !cache().entryList({"*.wav"}).isEmpty(); }, 60000);
        const auto wavs = cache().entryInfoList({"*.wav"});
        QTest::qWait(300);
        QTest::mouseClick(play, Qt::LeftButton); // stop
        return wavs;
    }

    apptest::Fixture fixture;
    MainWindow window;
    ScorePreviewPane* pane = nullptr;
    QToolButton* play = nullptr;
};

} // namespace

TEST_CASE("Playback: instruments per staff, saved into the file, one-staff toolbar",
          "[.tools][app][playback]") {
    PlayableScore s("Two");
    auto& tab = A::scoreTabs(s.window)[0];
    auto* button = s.pane->findChild<QToolButton*>("scoreInstruments");
    REQUIRE(button != nullptr);
    CHECK(!button->isHidden());
    auto combos = s.pane->findChildren<QComboBox*>("scoreInstrument");
    auto saves = s.pane->findChildren<QPushButton*>("scoreInstrumentSave");
    // One row per staff, then the chord names' (since c1a2ac4: the harness before had two).
    REQUIRE(combos.size() == 3);
    REQUIRE(saves.size() == 3);
    CHECK(!saves[0]->isEnabled());
    CHECK(!saves[1]->isEnabled());

    combos[0]->setCurrentIndex(combos[0]->findData("banjo"));
    combos[1]->setCurrentIndex(combos[1]->findData("amp"));
    CHECK(!saves[1]->isEnabled()); // amp cannot be saved
    CHECK(saves[0]->isEnabled());
    REQUIRE(s.waitPlay());
    CHECK(!A::scoreMidi(s.window).empty());
    CHECK(!A::scoreAmpMidi(s.window).empty());
    CHECK(!A::scoreAmp(s.window));
    const auto main = programs(A::scoreMidi(s.window)), amp = programs(A::scoreAmpMidi(s.window));
    REQUIRE(main.size() == 1);
    REQUIRE(amp.size() == 1);
    CHECK(main[0].count(105)); // banjo
    CHECK(amp[0].count(27));   // the clean electric guitar the amp is made of
    CHECK(s.renderOnce().size() == 1);

    // Save to file for staff 1: one undo step.
    const QString before = tab.editor->textEdit()->toPlainText();
    QTest::mouseClick(saves[0], Qt::LeftButton);
    const QString after = tab.editor->textEdit()->toPlainText();
    CHECK(after.contains("\\set Staff.midiInstrument = \"banjo\""));
    CHECK(after.count("midiInstrument") == 1);
    tab.editor->textEdit()->undo();
    CHECK(tab.editor->textEdit()->toPlainText() == before);
    tab.editor->textEdit()->redo();
    CHECK(tab.editor->textEdit()->toPlainText() == after);
    CHECK(
        apptest::waitUntil([&] { return A::staves(*s.pane)[0].fileInstrument == "banjo"; }, 20000));
    CHECK(s.pane->staffChoices().value(0) == "auto");
    REQUIRE(s.waitPlay());
    const auto p3 = programs(A::scoreMidi(s.window));
    REQUIRE(!p3.empty());
    CHECK(p3[0].count(105));

    // Saved file: closing and reopening shows the file's instrument as the staff's default.
    REQUIRE(tab.editor->save().has_value());
    CHECK(has(slurp(s.fixture / "songs/chords/Test/Two.ly"),
              "\\set Staff.midiInstrument = \"banjo\""));
    A::tabs(s.window)->setCurrentIndex(0);
    A::removeScoreTabs(s.window);
    A::staffChoices(s.window).clear(); // a fresh session
    A::syncScoreTabs(s.window);
    A::syncScoreTabs(s.window);
    A::tabs(s.window)->setCurrentIndex(1);
    QTest::qWait(800);
    REQUIRE(s.waitPlay());
    const auto again = s.pane->findChildren<QComboBox*>("scoreInstrument");
    REQUIRE(again.size() == 3);
    CHECK(again[0]->currentText().contains("banjo"));
    CHECK(again[1]->currentText() == "Automatic");
    // The MIDI file of the fresh session (both staves in one file) comes with the rebuild.
    REQUIRE(
        apptest::waitUntil([&] { return programs(A::scoreMidi(s.window)).size() == 2; }, 60000));
    const auto p4 = programs(A::scoreMidi(s.window));
    REQUIRE(p4.size() == 2);
    CHECK(p4[0].count(105)); // banjo from the file
    CHECK(p4[1].count(25));  // guitar by tuning

    // A score with one staff: a combo in the toolbar, no popup button.
    A::tabs(s.window)->setCurrentIndex(0);
    A::removeScoreTabs(s.window);
    REQUIRE(A::openExistingSong(s.window, s.fixture / "songs/chords/Test/En.tex"));
    A::syncScoreTabs(s.window);
    A::syncScoreTabs(s.window);
    A::tabs(s.window)->setCurrentIndex(1);
    QTest::qWait(800);
    REQUIRE(s.waitPlay());
    CHECK(button->isHidden());
    const auto toolbar = s.pane->findChildren<QComboBox*>("scoreInstrument");
    REQUIRE(toolbar.size() == 1);
    CHECK(toolbar[0]->isVisible());
    toolbar[0]->setCurrentIndex(toolbar[0]->findData("flute"));
    CHECK(!s.play->isEnabled()); // the old MIDI file is dropped until the rebuild
    REQUIRE(s.waitPlay());
    const auto p5 = programs(A::scoreMidi(s.window));
    REQUIRE(p5.size() == 1);
    CHECK(p5[0].count(73));
    for (auto& t : A::scoreTabs(s.window))
        if (t.editor)
            t.editor->textEdit()->document()->setModified(false);
}

TEST_CASE("Playback: every instrument has its own cached WAV, the amp does not clip",
          "[.tools][app][playback]") {
    PlayableScore s;
    auto* combo = s.pane->findChild<QComboBox*>("scoreInstrument");
    REQUIRE(combo != nullptr);
    const auto render = [&](const QString& id) -> QFileInfo {
        combo->setCurrentIndex(combo->findData(id));
        REQUIRE(s.waitPlay());
        const auto wavs = s.renderOnce();
        REQUIRE(wavs.size() == 1);
        return wavs[0];
    };
    const QFileInfo clean = render("clean");
    const QByteArray cleanData = [&] {
        QFile f(clean.absoluteFilePath());
        REQUIRE(f.open(QIODevice::ReadOnly));
        return f.readAll();
    }();
    const QFileInfo amp = render("amp");
    QByteArray ampData;
    {
        QFile f(amp.absoluteFilePath());
        REQUIRE(f.open(QIODevice::ReadOnly));
        ampData = f.readAll();
    }
    const QFileInfo steel = render("steel");
    CHECK(clean.fileName() != amp.fileName());
    CHECK(clean.fileName() != steel.fileName());
    CHECK(amp.fileName() != steel.fileName());
    // Back to amp: the same cache key as before.
    CHECK(render("amp").fileName() == amp.fileName());
    CHECK(cleanData != ampData);
    int clipped = 0;
    for (qsizetype i = 44; i + 1 < ampData.size(); i += 2) {
        const int v = static_cast<qint16>(static_cast<quint8>(ampData[i]) |
                                          (static_cast<quint8>(ampData[i + 1]) << 8));
        if (v == 32767 || v == -32768)
            ++clipped;
    }
    CHECK(clipped == 0);
    for (auto& t : A::scoreTabs(s.window))
        if (t.editor)
            t.editor->textEdit()->document()->setModified(false);
}

// The score cursor as the play position, with the real lilypond (the listener's note list) and
// fluidsynth (the audio), and a player that makes no sound.
TEST_CASE("Playback: Play starts at the cursor's note and the cursor follows the audio",
          "[.tools][app][playback]") {
    PlayableScore s;
    auto* player = new apptest::SilentPlayer;
    A::replacePlayer(s.window, player);
    auto* editor = A::scoreTabs(s.window)[0].editor.data();
    REQUIRE(editor != nullptr);

    // The build's note list became a timeline of the text's events.
    const auto& timeline = A::timeline(s.window);
    REQUIRE(!timeline.empty());
    CHECK(timeline.unmapped < 10); // only the notes of the single-chord variable emChord
    const std::string text = editor->editorText();
    const std::size_t chord = text.find("<e,\\6 h,\\5 e\\4>4");
    REQUIRE(chord != std::string::npos);
    std::optional<double> expected;
    for (const auto& cue : timeline.cues)
        if (cue.event.begin == chord && !expected)
            expected = cue.seconds;
    REQUIRE(expected);
    CHECK(*expected > 0.5); // the third bar of the score

    // Play from there.
    editor->setCursorByteOffset(chord, false);
    QTest::mouseClick(s.play, Qt::LeftButton);
    REQUIRE(apptest::waitUntil([&] { return !player->played.empty(); }, 60000));
    const auto wav = player->played[0].first;
    CHECK(player->played[0].second == std::llround(*expected * 1000.0));
    CHECK(A::following(s.window));

    // The audio is as long as the music, and its first sound is at the start of the file (the
    // timeline counts from the MIDI file's start): the leading silence is not trimmed, so a note's
    // time is a position in the file.
    const std::string bytes = apptest::slurp(wav);
    REQUIRE(bytes.size() > 44);
    const double length = static_cast<double>(bytes.size() - 44) / (44100.0 * 4.0);
    CHECK(length >= timeline.cues.back().seconds);
    std::size_t firstSound = 0;
    for (std::size_t i = 44; i + 1 < bytes.size(); i += 2) {
        const auto sample = static_cast<short>(static_cast<unsigned char>(bytes[i]) |
                                               (static_cast<unsigned char>(bytes[i + 1]) << 8));
        if (sample > 200 || sample < -200) {
            firstSound = (i - 44) / 4;
            break;
        }
    }
    CHECK(static_cast<double>(firstSound) / 44100.0 < 0.1);

    // The player's position moves the cursor over the notes.
    player->position = std::llround(*expected * 1000.0) + 20;
    A::followAt(s.window, static_cast<double>(player->position) / 1000.0);
    const auto* chordCue = zub::scoretiming::cueAt(timeline, *expected + 0.02);
    REQUIRE(chordCue != nullptr);
    CHECK(!editor->selectedByteRange());
    CHECK(editor->cursorByteOffset() == chordCue->event.end); // a cursor right behind the note
    A::followAt(s.window, timeline.cues.front().seconds);
    CHECK(editor->cursorByteOffset() == timeline.cues.front().event.end);
    A::stopPlayback(s.window);
    CHECK(editor->cursorByteOffset() == timeline.cues.front().event.end);
    // Play again resumes at the note that was sounding.
    CHECK(A::startSeconds(s.window) == timeline.cues.front().seconds);

    // At the start of the music it plays from the beginning.
    editor->setCursorByteOffset(0, false);
    player->played.clear();
    QTest::mouseClick(s.play, Qt::LeftButton);
    REQUIRE(apptest::waitUntil([&] { return !player->played.empty(); }, 60000));
    CHECK(player->played[0].second == 0);
    A::stopPlayback(s.window);
}

// Everything that sounds at a time is boxed in the preview of a real build: the tab staff's chord,
// the chord name and the lyric syllable together.
TEST_CASE("Playback: the preview boxes the notes, chord names and lyrics sounding together",
          "[.tools][app][playback]") {
    PlayableScore s("Sing");
    auto* player = new apptest::SilentPlayer;
    A::replacePlayer(s.window, player);
    auto* editor = A::scoreTabs(s.window)[0].editor.data();
    REQUIRE(editor != nullptr);
    const auto& timeline = A::timeline(s.window);
    REQUIRE(!timeline.empty());
    const std::string text = editor->editorText();

    // At the start: the tab staff's chord, the chord name c1 and the syllable "la".
    using zub::scoretiming::Kind;
    const auto now = zub::scoretiming::soundingAt(timeline, 0.05);
    const auto find = [&](Kind kind, std::string_view word) {
        return std::ranges::find_if(now, [&](const auto& sound) {
            return sound.kind == kind &&
                   text.substr(sound.range.begin, sound.range.end - sound.range.begin) == word;
        });
    };
    const auto note = find(Kind::Note, "<e, e>2");
    const auto name = find(Kind::ChordName, "c1");
    const auto syllable = find(Kind::Lyric, "la");
    REQUIRE(note != now.end());
    REQUIRE(name != now.end());
    REQUIRE(syllable != now.end());
    // The hidden melody is a sound of its own: with the default Voice instrument the playback score
    // plays it in a voice (a Note), silent it is a Devnull (a Melody sound).
    CHECK(std::ranges::any_of(now, [&](const auto& sound) {
        return (sound.kind == Kind::Melody || sound.kind == Kind::Note) && text.substr(sound.range.begin, 4) == "c''4";
    }));

    // Play and let the preview follow: each of the three has a link in the preview and is boxed.
    QTest::mouseClick(s.play, Qt::LeftButton);
    REQUIRE(apptest::waitUntil([&] { return !player->played.empty(); }, 60000));
    A::followAt(s.window, 0.05);
    int linked = 0;
    for (const auto& sound : {*note, *name, *syllable})
        for (const QPoint& position : s.pane->notePositions()) {
            const auto offset = zub::scoreregion::offsetOf(text, position.y(), position.x());
            if (offset && *offset >= sound.range.begin && *offset < sound.range.end)
                ++linked;
        }
    CHECK(linked >= 3); // a link each (the chord: one per note)
    CHECK(s.pane->soundingNoteCount() == linked);
    A::stopPlayback(s.window);
    CHECK(s.pane->soundingNoteCount() == 0);
}
