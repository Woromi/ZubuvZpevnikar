#pragma once

#include <QString>
#include <QWidget>
#include <expected>
#include <filesystem>

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
};
