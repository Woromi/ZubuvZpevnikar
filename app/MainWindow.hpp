#pragma once

#include <QMainWindow>
#include <QString>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

class QComboBox;
class QLabel;
class QLockFile;
class QLineEdit;
class QModelIndex;
class QSplitter;
class QTabWidget;
class QTableView;
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

    // Editor tabs
    void openSong(const QModelIndex& proxyIndex);
    SongEditor* findEditor(const std::filesystem::path& path) const;
    void addEditorTab(SongEditor* editor, const QString& title, const QString& toolTip);
    void importSong();
    std::filesystem::path findConverter() const;
    SongEditor* currentEditor() const;
    void saveCurrent();
    bool closeTab(int index);              // false if the user cancelled
    bool closeAllTabs();                   // false if the user cancelled
    bool confirmClose(SongEditor* editor); // asks Save/Discard/Cancel if modified
    void updateTabTitle(SongEditor* editor);
    void refreshSongMetadata(SongEditor* editor, const std::string& content);

    // Preview
    void createPreview();
    // Builds the current editor's text; clears the pane without one. A song with commands
    // outside the song format is built only when `userAsked` (Rebuild) and confirmed.
    void requestPreview(bool userAsked = false);
    void requestPreviewIfCurrent(SongEditor* editor);
    std::string inputNameOf(const SongEditor* editor) const; // "Artist/Title"

    SongTableModel* model_ = nullptr;
    SongFilterProxy* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QSplitter* splitter_ = nullptr;
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
