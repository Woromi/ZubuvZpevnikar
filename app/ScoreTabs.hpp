#pragma once

#include "ScorePreviewPane.hpp"

#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <zub/core/scoreregion.hpp>
#include <zub/core/scoretiming.hpp>

class QAction;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QWidget;
class FretboardWidget;
class NoteSounds;
class PlaybackRunner;
struct PlaybackResult;
class ScoreInputBar;
class ScorePlayer;
class ScoreRunner;
struct ScoreResult;
class SongEditor;

namespace KSyntaxHighlighting {
class Repository;
} // namespace KSyntaxHighlighting

// The score tabs of the open song (LilyPond): one tab per \lilypondfile, next to the song's own
// tab of the tab widget, with the score preview, the fretboard input, playback and the actions on
// selected notes. Owns the tabs after the first, their editors and everything the score feature
// needs; the window tells it the open song and the songbook, and reacts to its signals.
class ScoreTabs : public QObject {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    // The work directories (per running instance, chosen by the window).
    struct Dirs {
        std::filesystem::path workDir;     // the score preview's build
        std::filesystem::path playbackDir; // the rendered WAV files
        std::filesystem::path soundsDir;   // the one-note sounds of fret clicks
    };
    // What only the window can do.
    struct Hooks {
        // Asks Save/Discard/Cancel for a modified editor; false if the user cancelled.
        std::function<bool(SongEditor*)> confirmClose;
        // The approval rule of builds for the risks of one score file (`key`), asking the user
        // when `askUser`; `unapproved` gets the constructs that still need approval.
        std::function<bool(const std::filesystem::path& key, std::vector<std::string> risks,
                           bool askUser, QString* unapproved)>
            approveScoreRisks;
        std::function<std::string(const SongEditor*)> inputNameOf; // "Artist/Title"
    };

    // `tabs`: the song's tab is index 0, the score tabs follow. `previewStack` holds the song's
    // PDF preview (`songPreview`, already added); the score preview is added after it.
    ScoreTabs(QTabWidget* tabs, QStackedWidget* previewStack, QWidget* songPreview,
              KSyntaxHighlighting::Repository& repository, const Dirs& dirs, Hooks hooks,
              QObject* parent);

    void setSong(SongEditor* song); // the open song, or nullptr
    void setSongbook(const std::filesystem::path& root, const std::filesystem::path& packageDir);
    // Stops the builds and deletes the score editors (before the syntax repository goes).
    void shutdown();

    // The editor of the current tab: the song's, a score's, or nullptr (no song, or a score tab
    // whose file is missing).
    SongEditor* currentEditor() const;
    bool onSongTab() const;
    bool anyScoreModified() const;
    // The score "Artist/X.ly" of the song: its file (existing, or empty) and its editor (nullptr
    // without a tab or while the file is missing).
    std::filesystem::path scorePath(const std::string& name) const;
    SongEditor* scoreEditor(const std::string& name) const;
    // The new or modified scores as (name, text): the song's PDF is built with them.
    std::vector<std::pair<std::string, std::string>> unsavedScores() const;
    void markPdfBuilt() { pdfStale_ = false; } // the song's PDF includes the scores as they are

    // Adds tabs for new \lilypondfile references; drops idle unreferenced ones.
    void syncScoreTabs();
    void songTextChanged(); // the song's text changed: look for references (after a moment) ...
    void updateScoreWidthBars(); // the Width radio buttons from the song's text
    // Actions on the selected notes of a score (tie, slur ...): enabled with a selection in the
    // score editor; the preview marks the notes of the selection a moment after it changes.
    void updateRegionActions();
    void removeScoreTabs(); // all, without asking
    // Asks about every modified or new score (Save/Discard/Cancel); false if the user cancelled.
    bool confirmCloseScores();
    void newScore();                // Edit → New Score…
    void updateSoundAvailability(); // fluidsynth and a soundfont: enables Play and Sound

signals:
    void statusMessage(const QString& text, int timeoutMs);
    // The song's text should become this (one undo step).
    void songTextReplaceRequested(const std::string& text);
    void songPdfRequested(); // the song's PDF should be rebuilt
    void actionsChanged();   // the window's actions depend on the current tab and editors
    void windowTitleChanged();
    void editStateChanged(); // Undo, Redo, Cut, Copy, Paste follow the current editor
    void scoreForgotten(const std::filesystem::path& path); // its approvals end

private:
    struct ScoreTab {
        std::string name;            // "Artist/X.ly", as in \lilypondfile{}
        QWidget* page = nullptr;     // the tab's widget
        QPointer<SongEditor> editor; // null while the score is missing
        ScoreInputBar* inputBar = nullptr;
        FretboardWidget* fretboard = nullptr;
        QWidget* inputPanel = nullptr; // {input bar, fretboard}; shown with fretboardAction_
        // The event (editor text range) whose positions the fretboard staging was loaded from while
        // it is the one selected note or chord: Insert replaces it then.
        std::optional<zub::scoreregion::Range> replaceTarget;
    };

    // Adds the tab of score `name` ("Artist/X.ly"). Without `editor` the score is loaded from
    // disk, or shown as missing.
    void addScoreTab(const std::string& name, SongEditor* editor = nullptr);
    void createMissingScore(const std::string& name); // a missing score's tab becomes a new score
    void attachScoreEditor(int tabIndex, SongEditor* editor);
    int scoreTabIndex(const std::string& name) const; // tab widget index, or -1
    void closeScoreTab(int tabIndex);                 // asks to save a modified score
    void dropScoreTab(int tabIndex);                  // removes the tab without asking
    void updateScoreTabTitle(const SongEditor* editor);
    std::filesystem::path newScorePath(const std::string& name) const;
    void onTabChanged();
    bool confirmCloseScore(const std::string& name, SongEditor* score);
    void removeScoreReference(const std::string& name);

    void setScoreWidth(const std::string& name, bool narrow);

    void updateFretboard(); // the current score tab's input panel
    // The music event the editor's selection touches, when it is exactly one.
    std::optional<zub::scoreregion::Range> singleSelectedEvent(SongEditor* editor) const;
    void insertStagedNotes(FretboardWidget* fretboard); // the staged positions as a note or chord
    void soundFret(FretboardWidget* fretboard, int string, int fret);

    void requestScorePreview(bool userAsked = false);
    void showScoreResult(const ScoreResult& result);
    void showStaves(SongEditor* editor, const std::string& name);
    void saveStaffInstrument(int staff, const QString& gmName);

    // Playback of the score preview (fluidsynth renders the build's MIDI file, Qt Multimedia plays
    // the WAV) and the sound of a clicked fret.
    void connectPlayer();
    void playScore();
    void stopPlayback();
    void forgetScoreMidi(); // stops Play and drops the MIDI file of the last build
    void showPlaybackResult(const PlaybackResult& result);
    // The score cursor as the play position: the timeline of the latest build (note list, MIDI
    // tempo map) tells where in the audio the cursor's note sounds, and which note sounds now.
    void buildTimeline(const ScoreResult& result);
    std::optional<double> startSeconds() const; // where Play starts for the cursor or selection
    void rememberPlayStart() { playFrom_ = startSeconds(); } // Play was asked for
    void startFollowing();                                   // the cursor follows the playing notes
    void stopFollowing();              // the cursor stays at the last note played
    void followTick();                 // a timer: the player's position -> followAt
    void followAt(double seconds);     // moves the cursor to the note sounding then, and marks all
                                       // that sounds in the preview
    void showSounding(double seconds); // the preview's "sounding" boxes for that time

    void updateScoreHighlights();
    void clickScoreNote(const QPoint& position, bool after, bool extend); // from the preview
    void selectScoreNotes(const QPoint& anchor, bool anchorAfter, const QPoint& current,
                          bool currentAfter);          // a drag in the preview
    void doubleClickScoreNote(const QPoint& position); // selects that event
    // A key pressed while the preview has the focus: moves the score cursor, extends the
    // selection, deletes events, undoes.
    void scoreKey(ScorePreviewPane::KeyCommand command, bool extend, bool wholeScore);
    void selectScoreOffsets(std::size_t anchor, std::size_t current); // caret offsets, any order
    std::optional<std::size_t> caretOffsetOf(const QPoint& position, bool after) const;
    void indexScoreEvents();

    QTabWidget* tabs_;             // song tab (index 0) + one tab per score
    QStackedWidget* previewStack_; // {song PDF preview, score preview}
    QWidget* songPreview_;
    KSyntaxHighlighting::Repository& repository_;
    Hooks hooks_;
    SongEditor* song_ = nullptr; // the open song, if any
    std::filesystem::path songbookRoot_;
    std::filesystem::path packageDir_;

    std::vector<ScoreTab> scoreTabs_;  // in tab order, from index 1
    QTimer* fretboardTimer_ = nullptr; // cursor or text moved: refresh the fretboard
    QList<QAction*> regionActions_;    // Tie, Slur ...: on the selected notes of a score
    QTimer* highlightTimer_ = nullptr; // selection moved: mark its notes in the preview
    bool scoreLinksFresh_ = false;     // the preview's note positions match the editor text
    // Of the text the preview was built from (valid while scoreLinksFresh_): the music events, the
    // event of each link ({line, column}), a link of each event, the event a selection started at.
    std::vector<zub::scoreregion::Range> scoreEvents_;
    // Every link of the preview (any kind) with its offset in the text, by offset: the links that
    // sound at a time are those inside the sounding ranges.
    std::vector<std::pair<std::size_t, QPoint>> linkOffsets_;
    std::map<std::pair<int, int>, std::size_t> linkEvent_;
    std::map<std::size_t, QPoint> eventLink_;
    // The stops (zub::scoreregion::caretStop) the keys of the preview selected from and to; valid
    // while the editor's selection is still what they stand for.
    std::optional<std::pair<std::size_t, std::size_t>> scoreStops_;
    std::optional<std::size_t> scoreAnchor_; // byte offset where a selection started
    QAction* fretboardAction_ = nullptr;     // "Fretboard": shows the input panels
    QTimer* scoreSyncTimer_ = nullptr;       // song text changed: look for new \lilypondfile
    QTimer* scorePreviewTimer_ = nullptr;    // score text changed: rebuild the score preview
    std::set<std::string> dismissedScores_;  // closed by the user while still referenced
    bool pdfStale_ = false;                  // a score changed since the song's PDF was built
    ScorePreviewPane* scorePreview_ = nullptr;
    ScoreRunner* scoreRunner_ = nullptr;
    PlaybackRunner* playbackRunner_ = nullptr;
    ScorePlayer* scorePlayer_ = nullptr;
    // Valid while scoreLinksFresh_ (the editor's text is the text the build was made from).
    zub::scoretiming::Timeline timeline_;
    std::optional<double> playFrom_; // seconds Play was asked to start at; none: the start
    QTimer* followTimer_ = nullptr;
    bool following_ = false; // the cursor moves with the playing notes
    std::optional<std::pair<std::size_t, std::size_t>> followRange_; // the note it selected
    NoteSounds* noteSounds_ = nullptr;
    bool scoreAmp_ = false; // the latest preview build's MIDI file is for the amp instrument
    std::filesystem::path
        scoreAmpMidi_; // ... and the amp staves' own file when others don't use it
    // The instrument choices of each staff (zub::midi::Instrument ids, "auto" = as the file), by
    // score name ("Artist/X.ly"): kept for the session only.
    std::map<std::string, QStringList> staffChoices_;
    std::map<std::string, QString> chordChoices_; // the chord names' instrument per score (session)
    std::map<std::string, QString> melodyChoices_; // the hidden melody's instrument (session)
    QAction* soundAction_ = nullptr;               // "Sound": clicking a fret plays the note
    std::filesystem::path scoreMidi_;              // the MIDI file of the latest successful build
    std::filesystem::path playbackCacheDir_;       // the rendered WAV files
    std::filesystem::path scoreWorkDir_;
};
