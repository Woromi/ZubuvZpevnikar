#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <QPointer>
#include <QString>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <expected>
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
class QSplitter;
class QStackedWidget;
class QToolBar;
class QTableView;
class QWidget;
class DiffView;
class PreviewPane;
class PreviewRunner;
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
    bool confirmClose(SongEditor* editor); // asks Save/Discard/Cancel if modified
    void createSongToolBar();
    void updateSongActions();
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
    QAction* markCheckedAction_ = nullptr; // removes the import's "not checked yet" TODO line
    SongbookScan* scan_ = nullptr;         // while a songbook scan runs
    int scanFound_ = 0;                    // songs it marked so far
    QCheckBox* changesOnly_ = nullptr;       // "Only songs with possible changes"
    QAction* changesOnlyAction_ = nullptr;   // its place in the filter bar (hidden until a scan)
    QAction* joinLinesAction_ = nullptr;
    QAction* changesAction_ = nullptr;
    SongEditor* editor_ = nullptr;      // the open song, if any
    QLabel* inputLabel_ = nullptr;
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
    QLabel* countLabel_ = nullptr;
    QLabel* issuesLabel_ = nullptr;

    std::filesystem::path songbookRoot_;
    int issueCount_ = 0;
};
