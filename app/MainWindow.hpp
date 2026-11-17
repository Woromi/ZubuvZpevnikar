#pragma once

#include "ActivityBar.hpp"
#include "ScoreTabs.hpp"

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
#include <zub/core/songcheck.hpp>
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
class SongListPage;
class PreviewPane;
class PreviewRunner;
class SongEditor;
class SongbookExporter;
class SongbookScan;
class SongbookSorter;
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
    friend struct AppTestAccess;

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
    // The views of the activity bar: the song list, the open song (Song falls back to the list
    // without one) and the songbooks. Switching keeps the open song open.
    void showView(ActivityBar::View view);
    void openSettings(); // File → Settings…, the bar's gear
    void updateStatus();

    // The open song (one at a time)
    void openSong(const QModelIndex& proxyIndex);
    // Loads `path` into a new editor, warning on failure (nullptr). An empty `title` is taken
    // from the song header, else the file name.
    SongEditor* openEditor(const std::filesystem::path& path, const QString& title);
    void showSong(SongEditor* editor, const QString& title, const QString& toolTip);
    void importSong();
    void newSong(); // File → New Song…: an empty song by hand, unsaved
    // Shows the song at `path` (already open: kept as it is), closing the open one; false if
    // it could not be loaded or the user kept the open one.
    bool openExistingSong(const std::filesystem::path& path);
    std::filesystem::path findConverter() const;
    std::filesystem::path builtInConverter() const; // empty if the app was built without it
    std::filesystem::path bundledPackage() const;   // the app's copy of the package, or empty
    QString noPackageText() const; // why a build cannot run: no package anywhere
    void saveCurrent();
    bool closeSong();                      // false if the user cancelled
    // Back to the song list from the song page (closing the song) or the Songbooks page; the
    // mouse's back button and Alt+Left.
    void goBack();
    bool confirmClose(SongEditor* editor); // asks Save/Discard/Cancel if modified
    bool confirmCloseAll();
    // Offers the `pap` header after a measurement of `measuredText` (a bar above the editor).
    void offerPapers(SongEditor* editor, const std::string& measuredText,
                     const zub::songsuggest::Suggestions& suggestions);
    void replaceSongText(const std::string& after); // one undo step in the song editor
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
    void scanSongbook(); // Songs → Check Songs…
    // The bar above the song with a key the last Check Songs found for it (to write, or to
    // confirm the written one with % nocheck key).
    void offerKey(SongEditor* editor);
    // checkkeys.py on `content` (the song's current text, in a temporary copy), in the
    // background: keyFindings_ follows (a missing key or one the chords disagree with, unless
    // confirmed with % nocheck key), then `done` gets the finding (nullopt: none, or the check
    // could not run).
    void checkKey(SongEditor* editor, const std::string& content,
                  std::function<void(std::optional<zub::songcheck::KeyFinding>)> done);
    QString keyFindingText(const zub::songcheck::KeyFinding& finding,
                           const std::string& content) const;
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
    // After a proposal was applied: measures the new text in the background (the same shrink
    // --fit --measure; no dialog) and offers the `pap` header from its result alone.
    void startMeasure(SongEditor* editor);
    void onMeasureFinished(const std::expected<ConverterOutput, QString>& output);
    void cancelMeasure(); // the background measurement, if one runs (the text changed ...)
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
    QStackedWidget* pages_ = nullptr;
    SongListPage* listPage_ = nullptr;
    ActivityBar* activityBar_ = nullptr;
    QWidget* songPage_ = nullptr;
    SongbooksPage* songbooksPage_ = nullptr; // the songbook assembler
    SongbookSorter* songbookSorter_ = nullptr;
    SongbookExporter* songbookExporter_ = nullptr;
    QToolBar* songToolBar_ = nullptr;
    QSplitter* songSplitter_ = nullptr; // editor area | preview
    bool songPageShown_ = false;        // the splitter has been laid out (its state is worth saving)
    QStackedWidget* editorArea_ = nullptr; // {editor, diff view}
    QTabWidget* tabs_ = nullptr;           // song tab (index 0) + one tab per score
    ScoreTabs* scores_ = nullptr;          // the tabs after the song's (LilyPond scores)
    ScoreTabs::Dirs scoreDirs_;            // chosen in createPreview, used for scores_
    QStackedWidget* previewStack_ = nullptr; // {song PDF preview, score preview}
    DiffView* diffView_ = nullptr;
    QWidget* tidyPanel_ = nullptr; // "Tidy changed:" list under the editor area
    QListWidget* tidyList_ = nullptr;
    ConverterRunner* converterRunner_ = nullptr;
    enum class RunKind { Tidy, Suggest, Apply, Measure };
    RunKind runKind_ = RunKind::Tidy;
    QPointer<SongEditor> runEditor_; // the editor the running converter belongs to
    std::string runInput_;           // the bytes that were sent
    QString tidyNote_;               // appended to the status message when a tidy is done
    QString applyDescription_;       // of the proposal being applied
    SongAction songAction_ = SongAction::Shrink; // the action whose shrink/join/check/apply runs
    bool measuredRun_ = false;       // the running/last shrink or join measured (was approved)
    bool measureAfterApply_ = false; // measure the text again once the proposals are applied
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
    // Keys the last Check Songs found, by song path (this session; dropped when acted on).
    std::map<std::filesystem::path, zub::songcheck::KeyFinding> keyFindings_;
    QAction* joinLinesAction_ = nullptr;
    QAction* changesAction_ = nullptr;
    SongEditor* editor_ = nullptr;      // the open song, if any
    QLabel* inputLabel_ = nullptr;
    std::vector<QAction*> editActions_;     // Edit menu: enabled while the text can be edited
    std::vector<QAction*> songEditActions_; // ... and only those of the song (chords, parts)
    QAction* closeSongAction_ = nullptr;
    QAction* songViewAction_ = nullptr; // View > The Open Song: only with a song open
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
    QLabel* countLabel_ = nullptr;
    QLabel* issuesLabel_ = nullptr;

    std::filesystem::path songbookRoot_;
    // The package that builds the songs: the songbook's own (zubuvzpevnik/, a submodule), else
    // the app's bundled copy; empty when neither is there (previews then fail with a message).
    std::filesystem::path packageDir_;
    int issueCount_ = 0;
};
