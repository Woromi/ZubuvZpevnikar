#pragma once

#include <QMainWindow>
#include <QByteArray>
#include <QString>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

class QAction;
class QComboBox;
class QLabel;
class QLockFile;
class QLineEdit;
class QModelIndex;
class QSplitter;
class QStackedWidget;
class QToolBar;
class QTableView;
class QWidget;
class PreviewPane;
class PreviewRunner;
class SongEditor;
class SongFilterProxy;
class SongTableModel;

namespace KSyntaxHighlighting {
class Repository;
} // namespace KSyntaxHighlighting

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Opens the songbook given on the command line, else the remembered one, else a guess
    // next to this repo; if none is valid (or the command-line one is not), asks for a folder.
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
    void saveCurrent();
    bool closeSong();                      // false if the user cancelled
    bool confirmClose(SongEditor* editor); // asks Save/Discard/Cancel if modified
    void createSongToolBar();
    void updateSongActions();
    void updateWindowTitle();
    void refreshSongMetadata(SongEditor* editor, const std::string& content);

    // Preview
    void createPreview();
    // Builds the current editor's text; clears the pane without one. A song with commands
    // outside the song format is built only when `userAsked` (Rebuild) and confirmed.
    void requestPreview(bool userAsked = false);
    std::string inputNameOf(const SongEditor* editor) const; // "Artist/Title"

    SongTableModel* model_ = nullptr;
    SongFilterProxy* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QWidget* listPage_ = nullptr;
    QWidget* songPage_ = nullptr;
    QToolBar* songToolBar_ = nullptr;
    QSplitter* songSplitter_ = nullptr; // editor | preview
    QByteArray songSplitterState_;      // restored when a song opens
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
