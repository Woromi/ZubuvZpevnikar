#pragma once

#include "ScorePreviewPane.hpp"

#include <QByteArray>
#include <QMainWindow>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <zub/core/scoreregion.hpp>
#include <zub/core/songedit.hpp>
#include <zub/core/songsuggest.hpp>
#include <zub/core/songtidy.hpp>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QLockFile;
class QLineEdit;
class QModelIndex;
class QPoint;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QToolBar;
class QTableView;
class QWidget;
class DiffView;
class FretboardWidget;
class LabelFilterBox;
class PreviewPane;
class PreviewRunner;
class NoteSounds;
class PlaybackRunner;
struct PlaybackResult;
class ScoreInputBar;
class ScorePlayer;
class ScoreRunner;
struct ScoreResult;
class SongEditor;
class SongbookExporter;
class SongbookScan;
class SongbookSorter;
class SongbookStore;
class SongbooksPage;
class SongFilterProxy;
class SongTableModel;
class ConverterRunner;
struct ConverterOutput;

namespace KSyntaxHighlighting {
class Repository;
} // namespace KSyntaxHighlighting

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Opens the songbook given on the command line, else the remembered one (QSettings); if
    // neither is set or valid, asks for a folder.
    void openInitialSongbook(const QString& commandLinePath);

protected:
    void closeEvent(QCloseEvent* event) override;
    // The mouse's back button anywhere in this window goes back, like in a browser.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createMenus();
    void createCentralWidget();
    void chooseSongbookFolder();
    struct OpenOptions {
        bool showErrors = true;    // a message box when the folder is not a songbook
        bool remember = true;      // save it as the songbook for the next start
        bool closeEditors = false; // close open songs (asking to save) once the folder is valid
    };
    bool openSongbook(const std::filesystem::path& root, OpenOptions options);
    void reloadSongbook();
    void updateLanguageFilter();
    void updateStatus();

    // The open song (one at a time)
    void openSong(const QModelIndex& proxyIndex);
    // Loads `path` into a new editor, warning on failure (nullptr). An empty `title` is taken
    // from the song header, else the file name.
    SongEditor* openEditor(const std::filesystem::path& path, const QString& title);
    void showSong(SongEditor* editor, const QString& title, const QString& toolTip);
    void importSong();
    std::filesystem::path findConverter() const;
    std::filesystem::path builtInConverter() const; // empty if the app was built without it
    void saveCurrent();
    bool closeSong();                      // false if the user cancelled
    // Back to the song list from the song page (closing the song) or the Songbooks page; the
    // mouse's back button and Alt+Left.
    void goBack();
    bool confirmClose(SongEditor* editor); // asks Save/Discard/Cancel if modified
    bool confirmCloseAll();
    bool confirmCloseScore(const std::string& name, SongEditor* score);
    void removeScoreReference(const std::string& name);
    void replaceSongText(const std::string& after); // one undo step in the song editor
    void updateScoreWidthBars();
    void updateFretboard(); // the current score tab's input panel
    // The music event the editor's selection touches, when it is exactly one.
    std::optional<zub::scoreregion::Range> singleSelectedEvent(SongEditor* editor) const;
    // Actions on the selected notes of a score (tie, slur ...): enabled with a selection in the
    // score editor; the preview marks the notes of the selection a moment after it changes.
    void updateRegionActions();
    void updateScoreHighlights();
    void clickScoreNote(const QPoint& position, bool after, bool extend); // from the preview
    void selectScoreNotes(const QPoint& anchor, bool anchorAfter, const QPoint& current,
                          bool currentAfter); // a drag in the preview
    void doubleClickScoreNote(const QPoint& position); // selects that event
    // A key pressed while the preview has the focus: moves the score cursor, extends the
    // selection, deletes events, undoes.
    void scoreKey(ScorePreviewPane::KeyCommand command, bool extend, bool wholeScore);
    void selectScoreOffsets(std::size_t anchor, std::size_t current); // caret offsets, any order
    std::optional<std::size_t> caretOffsetOf(const QPoint& position, bool after) const;
    void indexScoreEvents();
    void insertStagedNotes(FretboardWidget* fretboard); // the staged positions as a note or chord
    void setScoreWidth(const std::string& name, bool narrow); // the song and all its score tabs; false if the user cancelled
    void createSongToolBar();
    void updateSongActions();
    void updateEditState(); // Undo, Redo, Cut, Copy, Paste: from the current editor
    void createEditMenu(); // Edit: chords and parts
    void moveChord(int direction, zub::songedit::Step step);
    void showPartMenu(); // Edit → Insert Part…, at the cursor

    // Scores (LilyPond): one tab per \lilypondfile of the open song, next to the song.
    // The editor of the current tab: the song's, a score's, or nullptr (no song, or a score
    // tab whose file is missing).
    SongEditor* currentEditor() const;
    bool onSongTab() const;
    bool anyModified() const;
    void
    syncScoreTabs(); // adds tabs for new \lilypondfile references; drops idle unreferenced ones
    // Adds the tab of score `name` ("Artist/X.ly"). Without `editor` the score is loaded from
    // disk, or shown as missing.
    void addScoreTab(const std::string& name, SongEditor* editor = nullptr);
    void createMissingScore(const std::string& name); // a missing score's tab becomes a new score
    void attachScoreEditor(int tabIndex, SongEditor* editor);
    int scoreTabIndex(const std::string& name) const; // tab widget index, or -1
    void closeScoreTab(int tabIndex);                 // asks to save a modified score
    void dropScoreTab(int tabIndex);                  // removes the tab without asking
    void removeScoreTabs();                           // all, without asking
    void updateScoreTabTitle(const SongEditor* editor);
    std::filesystem::path scorePath(const std::string& name) const; // existing file or empty
    std::filesystem::path newScorePath(const std::string& name) const;
    void newScore(); // Edit → New Score…
    void onTabChanged();
    void requestScorePreview(bool userAsked = false);
    void showScoreResult(const ScoreResult& result);
      // it, Qt Multimedia plays it) and the sound of a clicked fret.
    void playScore();
    void stopPlayback();
    void showPlaybackResult(const PlaybackResult& result);
    void updateSoundAvailability(); // fluidsynth and a soundfont: enables Play and Sound
    void soundFret(FretboardWidget* fretboard, int string, int fret);
    // Labels (the `tags` header option; "fav" = favourite). `change` maps a song's current tags to
    // its new ones. An open song gets the new header in its editor (one undo step, unsaved); any
    // other song is rewritten on disk, if it round-trips, and its table row updated. Returns an
    // error text, empty on success.
    QString writeSongTags(
        const std::filesystem::path& path,
        const std::function<std::vector<std::string>(const std::vector<std::string>&)>& change);
    // Applies `change` to every song, then lists the failures in a message box.
    void changeSongTags(
        const std::vector<std::filesystem::path>& paths,
        const std::function<std::vector<std::string>(const std::vector<std::string>&)>& change);
    void toggleFavourite(const std::filesystem::path& path);
    void editLabels(const std::vector<std::filesystem::path>& paths);
    std::vector<std::filesystem::path> selectedSongPaths() const; // in the song table
    void showTableMenu(const QPoint& position);
    void updateLabelActions(); // enabled state and ★ check of the song toolbar's actions
    void updateMarkChecked(); // enabled while the editor text has the "not checked yet" line
    // Songs → Scan Songbook…: runs tidy/check/shrink on many songs and marks the ones with
    // possible changes in the list (this session only); changes nothing.
    void scanSongbook();
    void showSongbooks(); // the songbook assembler page
    // Tidy (chords_converter tidy on the open song, one undo step) and the Changes view
    void startTidy(const QString& note = {});
    void onConverterFinished(const std::expected<ConverterOutput, QString>& output);
    void onTidyFinished(const std::expected<ConverterOutput, QString>& output);
    // Make Smaller, Join Lines, Check Song: shrink, join or check (a dialog to pick proposals),
    // then apply the picked ones (one undo step)
    enum class SongAction { Shrink, Join, Check };
    void startProposals(SongAction action);
    QString actionName(SongAction action) const;
    void onSuggestFinished(const std::expected<ConverterOutput, QString>& output);
    void startApply(std::vector<std::string> ids, const QString& description);
    void onApplyFinished(const std::expected<ConverterOutput, QString>& output);
    // Takes over the state of the run that just finished; the editor it belongs to, or nullptr
    // (after a status message) when the song changed meanwhile. `input` gets the bytes sent.
    SongEditor* takeRun(std::string& input, const QString& what);
    // False and a message box when the converter is older than zub::converter::kRequired.
    bool converterUsable(const std::filesystem::path& converter, const QString& action);
    void clearTidyChanges();
    void showChanges(bool show); // the diff of the saved file vs the editor text, or the editor
    void refreshChanges();
    QString noChangesText() const; // the Changes view's text when nothing changed
    void jumpToLine(int line);
    void updateWindowTitle();
    void refreshSongMetadata(SongEditor* editor, const std::string& content);

    // Preview
    void createPreview();
    // Builds the current editor's text; clears the pane without one. A song with commands
    // outside the song format is built only when `userAsked` (Rebuild) and confirmed.
    void requestPreview(bool userAsked = false);
    // Whether the song may be built: it has no commands outside the song format that the user
    // has not approved, or (askUser) the user approves them now (remembered while the song is
    // open). `unapproved` gets the commands that still need approval, comma-separated.
    bool approveBuild(SongEditor* editor, const std::string& content, bool askUser,
                      QString* unapproved = nullptr, const QString& yesText = {},
                      const QString& noText = {});
    // Same rule for score files: the .ly texts a build would run (`texts`: name, content).
    struct RiskSource {
        std::filesystem::path key; // approvals are remembered per file
        QString label;             // shown before the construct when it is not the song
        std::vector<std::string> risks;
    };
    bool approveRisks(const std::vector<RiskSource>& sources, bool askUser, QString* unapproved,
                      const QString& yesText, const QString& noText, bool forScore = false);
    // The risks of the song `content` and of the scores it references (editor text, and the
    // file on disk when that differs).
    std::vector<RiskSource> songBuildRisks(SongEditor* editor, const std::string& content) const;
    std::string inputNameOf(const SongEditor* editor) const; // "Artist/Title"

    SongTableModel* model_ = nullptr;
    SongFilterProxy* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QWidget* listPage_ = nullptr;
    QWidget* songPage_ = nullptr;
    SongbooksPage* songbooksPage_ = nullptr; // the songbook assembler
    SongbookStore* songbookStore_ = nullptr;
    SongbookSorter* songbookSorter_ = nullptr;
    SongbookExporter* songbookExporter_ = nullptr;
    QAction* songbooksAction_ = nullptr;
    QToolBar* songToolBar_ = nullptr;
    QSplitter* songSplitter_ = nullptr; // editor area | preview
    bool songPageShown_ = false;        // the splitter has been laid out (its state is worth saving)
    QStackedWidget* editorArea_ = nullptr; // {editor, diff view}
    QTabWidget* tabs_ = nullptr;           // song tab (index 0) + one tab per score
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
    std::vector<ScoreTab> scoreTabs_;        // in tab order, from index 1
    QTimer* fretboardTimer_ = nullptr;       // cursor or text moved: refresh the fretboard
    QList<QAction*> regionActions_;          // Tie, Slur ...: on the selected notes of a score
    QTimer* highlightTimer_ = nullptr;       // selection moved: mark its notes in the preview
    bool scoreLinksFresh_ = false;           // the preview's note positions match the editor text
    // Of the text the preview was built from (valid while scoreLinksFresh_): the music events, the
    // event of each link ({line, column}), a link of each event, the event a selection started at.
    std::vector<zub::scoreregion::Range> scoreEvents_;
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
    QStackedWidget* previewStack_ = nullptr; // {song PDF preview, score preview}
    ScorePreviewPane* scorePreview_ = nullptr;
    ScoreRunner* scoreRunner_ = nullptr;
    PlaybackRunner* playbackRunner_ = nullptr;
    ScorePlayer* scorePlayer_ = nullptr;
    NoteSounds* noteSounds_ = nullptr;
    QAction* soundAction_ = nullptr; // "Sound": clicking a fret plays the note
    std::filesystem::path scoreMidi_;        // the MIDI file of the latest successful build
    std::filesystem::path playbackCacheDir_; // the rendered WAV files
    std::filesystem::path scoreWorkDir_;
    DiffView* diffView_ = nullptr;
    QWidget* tidyPanel_ = nullptr; // "Tidy changed:" list under the editor area
    QListWidget* tidyList_ = nullptr;
    ConverterRunner* converterRunner_ = nullptr;
    enum class RunKind { Tidy, Suggest, Apply };
    RunKind runKind_ = RunKind::Tidy;
    QPointer<SongEditor> runEditor_; // the editor the running converter belongs to
    std::string runInput_;           // the bytes that were sent
    QString tidyNote_;               // appended to the status message when a tidy is done
    QString applyDescription_;       // of the proposal being applied
    SongAction songAction_ = SongAction::Shrink; // the action whose shrink/join/check/apply runs
    bool applyRestructures_ = false; // the proposals being applied make a part a chorus: Tidy after
    QAction* tidyAction_ = nullptr;
    QAction* makeSmallerAction_ = nullptr;
    QAction* checkAction_ = nullptr;
    QAction* labelsAction_ = nullptr;      // Labels… of the open song
    QAction* favouriteAction_ = nullptr;   // ★ checkable: the open song is a favourite
    QTimer* favouriteTimer_ = nullptr;     // syncs the ★ with the editor text after typing
    QAction* markCheckedAction_ = nullptr; // removes the import's "not checked yet" TODO line
    SongbookScan* scan_ = nullptr;         // while a songbook scan runs
    int scanFound_ = 0;                    // songs it marked so far
    QCheckBox* changesOnly_ = nullptr;       // "Only songs with possible changes"
    QAction* changesOnlyAction_ = nullptr;   // its place in the filter bar (hidden until a scan)
    QAction* joinLinesAction_ = nullptr;
    QAction* changesAction_ = nullptr;
    SongEditor* editor_ = nullptr;      // the open song, if any
    QLabel* inputLabel_ = nullptr;
    std::vector<QAction*> editActions_;     // Edit menu: enabled while the text can be edited
    std::vector<QAction*> songEditActions_; // ... and only those of the song (chords, parts)
    QAction* closeSongAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* cutAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QString listTitle_; // window title on the list page
    // Syntax definitions and themes, shared by all editors (must outlive them).
    std::unique_ptr<KSyntaxHighlighting::Repository> repository_;
    PreviewPane* preview_ = nullptr;
    PreviewRunner* previewRunner_ = nullptr;
    std::filesystem::path previewWorkDir_;
    std::unique_ptr<QLockFile> previewLock_; // the work dir is this instance's while held
    // Per song, the unknown commands the user agreed to build anyway (this session only).
    std::map<std::filesystem::path, std::set<std::string>> approvedConstructs_;
    QLineEdit* search_ = nullptr;
    QComboBox* language_ = nullptr;
    LabelFilterBox* labelFilter_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QLabel* issuesLabel_ = nullptr;

    std::filesystem::path songbookRoot_;
    int issueCount_ = 0;
};
