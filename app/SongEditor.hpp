#pragma once

#include <QList>
#include <QString>
#include <QWidget>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <zub/core/scoreregion.hpp>
#include <zub/core/songedit.hpp>

class QAction;
class QFileSystemWatcher;
class QLabel;
class QPlainTextEdit;

namespace KSyntaxHighlighting {
class Repository;
class SyntaxHighlighter;
} // namespace KSyntaxHighlighting

// Edits one song .tex file, or one score .ly file (Kind::Score: LilyPond highlighting, score
// wording in the dialogs, no song-editing helpers). Saving is byte-exact: what was not edited is
// written back unchanged (non-breaking spaces, tabs, a missing final newline, CRLF line endings).
class SongEditor : public QWidget {
    Q_OBJECT

public:
    enum class Kind { Song, Score };

    // `repository` is shared by all editors and must outlive them.
    SongEditor(std::filesystem::path path, KSyntaxHighlighting::Repository& repository,
               QWidget* parent = nullptr, Kind kind = Kind::Song);
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

    // Whether the editor would keep `bytes` exactly (no BOM, valid UTF-8, one kind of line
    // ending, no Unicode line separators). A file that does not is opened read-only and must not
    // be rewritten behind the editor's back either.
    static bool roundTrips(const std::string& bytes);

    Kind kind() const { return kind_; }
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
    // Moves the cursor to the 1-based `line` and the 0-based `column` (code points, as LilyPond
    // counts them; past the end of the line: its end), centres it and focuses the editor.
    void goToLineColumn(int line, int column);
    // Puts the cursor at `byteOffset` of the editor text (UTF-8, LF line ends) and focuses it.
    void setCursorByteOffset(std::size_t byteOffset);
    QPlainTextEdit* textEdit() const { return edit_; }

    // Song-editing helpers (zub::songedit on the cursor's line); each is one undo step and does
    // nothing in a read-only editor.
    // Inserts "\[]" at the cursor, or wraps a selection within one line in a chord.
    void insertChord();
    // Insert a multivoice (around a one-line selection), or a role at the end of the cursor's
    // line.
    void insertMultivoice();
    void insertRole();
    // Removes the chord, role or multivoice at the cursor; false when there is none.
    bool removeElement();
    // Moves the chord at the cursor; false when there is none or it cannot move.
    bool moveChord(int direction, zub::songedit::Step step);
    // Inserts a part template: around the selected lines, else at the start of the cursor's
    // line when that is empty, else on a new line after it.
    void insertPart(zub::songedit::Part part);
    bool hasSongHeader() const; // the text has a \beginsong
    // Moves the cursor's line, or every line the selection touches, one line up (-1) or down
    // (+1), keeping the selection on the moved lines; false at the top or bottom.
    bool moveLines(int direction);

    // Score editing from the fretboard (Kind::Score), each one undo step on the cursor's line,
    // nothing in a read-only editor. `duration` is what the note should have ("4", "8."); it is
    // written only when it differs from effectiveDuration() (empty: keep the effective one).
    void insertScoreNote(const std::string& token, const std::string& duration);
    void insertScoreRest(const std::string& duration);
    void insertScoreBarCheck();
    // Is the text cursor in a `%` comment or a string of its line (no note can be inserted)?
    bool cursorInScoreComment() const;
    // The cursor as a byte offset into content(), and the duration in force there.
    std::size_t cursorByteOffset() const;
    std::string effectiveDurationAtCursor() const;

    // The document as UTF-8 with LF line ends, whatever the file uses (the offsets of the
    // functions below and of zub::scoreregion refer to it).
    std::string editorText() const;
    // The selection as byte offsets into the editor text (UTF-8, LF line ends); none without one.
    std::optional<std::pair<std::size_t, std::size_t>> selectedByteRange() const;
    // Selects [begin, end) of the editor text, shows it and focuses the editor.
    void selectByteRange(std::size_t begin, std::size_t end);
    // Applies a zub::scoreregion action to the selected notes (Kind::Score): ONE undo step, the
    // result stays selected. False when nothing changed (no selection, read-only, nothing to do);
    // `why` (when given) tells why the core refused.
    bool applyRegionAction(zub::scoreregion::Action action,
                           zub::scoreregion::Refusal* why = nullptr);
    // Added to the end of the editor's context menu.
    void setRegionMenuActions(const QList<QAction*>& actions);

signals:
    void modificationChanged(bool modified);
    // After a successful save(); `content` is the UTF-8 that was written.
    void saved(const std::string& content);
    // The file was reloaded because it changed outside the app.
    void reloadedFromDisk();

private:
    void onFileChanged();
    std::string currentContent() const; // editor text -> file bytes
    void keepCarriedDuration(const std::string& written, const std::string& effective);
    void setContent(const std::string& bytes);
    void replaceDocumentText(const QString& text); // LF line ends; the differing middle only
    void watch();
    void onDirectoryChanged();
    void updateBanner();
    void checkLossless(const std::string& bytes);

    std::filesystem::path path_;
    Kind kind_ = Kind::Song;
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
    bool isNew_ = false;     // opened with openNew() and not saved yet
    std::string newContent_; // what openNew() was given, as the editor holds it
};
