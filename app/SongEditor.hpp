#pragma once

#include <QString>
#include <QWidget>
#include <expected>
#include <filesystem>
#include <string>

class QFileSystemWatcher;
class QLabel;
class QPlainTextEdit;

namespace KSyntaxHighlighting {
class Repository;
class SyntaxHighlighter;
} // namespace KSyntaxHighlighting

// Edits one song .tex file. Saving is byte-exact: what was not edited is written back
// unchanged (non-breaking spaces, tabs, a missing final newline, CRLF line endings).
class SongEditor : public QWidget {
    Q_OBJECT

public:
    // `repository` is shared by all editors and must outlive them.
    SongEditor(std::filesystem::path path, KSyntaxHighlighting::Repository& repository,
               QWidget* parent = nullptr);
    ~SongEditor() override;

    // Loads the file into the editor (clears the modified state). On failure the editor
    // stays empty and the message is returned.
    std::expected<void, QString> load();

    // For a song that is not on disk yet (an import): shows `content` as unsaved. The file and
    // its folder are created by the first save(); if the path exists by then, save() asks
    // before overwriting it. Nothing is watched until then.
    void openNew(const std::string& content);

    enum class SaveResult { Saved, Cancelled };

    // Writes the editor content to the file (zub::writeTextFileAtomic) and clears the
    // modified state. Emits saved() on Saved. If the file changed on disk since it was
    // loaded, asks before overwriting; Cancelled means nothing was written. A read-only
    // (lossy) editor refuses to save and returns an error.
    std::expected<SaveResult, QString> save();

    // Stops watching the file and its directory for good (the tab is going away).
    void stopWatching();

    const std::filesystem::path& path() const { return path_; }
    bool isModified() const;
    // The editor text as it would be saved (UTF-8).
    std::string content() const { return currentContent(); }
    // The bytes last loaded from or saved to disk; empty for a song that is not saved yet.
    const std::string& savedContent() const { return diskContent_; }
    // What the Changes view compares against: the saved bytes, or for a song opened with
    // openNew() and not saved yet, the content it was opened with (e.g. the raw import).
    const std::string& baselineContent() const { return isNew_ ? newContent_ : diskContent_; }
    bool isNew() const { return isNew_; }
    // Replaces the editor text with `bytes` (file bytes, converted like load() does) as ONE
    // undo step; only the differing middle is replaced, so cursor and scroll stay. The document
    // becomes modified. Does nothing for equal content or a read-only (lossy) editor.
    void replaceContent(const std::string& bytes);
    // Moves the cursor to the start of the 1-based `line`, centres it and focuses the editor.
    void goToLine(int line);
    QPlainTextEdit* textEdit() const { return edit_; }

signals:
    void modificationChanged(bool modified);
    // After a successful save(); `content` is the UTF-8 that was written.
    void saved(const std::string& content);
    // The file was reloaded because it changed outside the app.
    void reloadedFromDisk();

private:
    void onFileChanged();
    std::string currentContent() const; // editor text -> file bytes
    void setContent(const std::string& bytes);
    void watch();
    void onDirectoryChanged();
    void updateBanner();
    void checkLossless(const std::string& bytes);

    std::filesystem::path path_;
    QPlainTextEdit* edit_ = nullptr;
    KSyntaxHighlighting::SyntaxHighlighter* highlighter_ = nullptr;
    QFileSystemWatcher* watcher_ = nullptr;
    std::string diskContent_; // bytes last loaded from or saved to disk
    bool crlf_ = false;       // the file used CRLF line endings
    bool askingAboutReload_ = false;
    QLabel* banner_ = nullptr;
    bool lossy_ = false; // the file does not round-trip: editor is read-only
    QString lossyReason_;
    bool missing_ = false; // the file vanished from disk
    bool modifiedBeforeMissing_ = false;
    bool stopped_ = false;
    bool isNew_ = false; // opened with openNew() and not saved yet
    std::string newContent_; // what openNew() was given, as the editor holds it
};
