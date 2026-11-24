#pragma once

#include <QAction>
#include <QColor>
#include <QList>
#include <QPlainTextEdit>
#include <functional>

class QTimer;

// A QPlainTextEdit with a line-number area on the left (Qt's "Code Editor" example). The
// numbers are the 1-based block numbers, which are the file's lines (the editor does not wrap).
// Enter keeps the current line's indentation. Selecting text marks its other occurrences.
// (Only a selection the user makes with the keys or the mouse; one set by the app is not marked.)
// Alt+Shift+Up/Down add cursors on the lines above/below, the other direction takes them back
// (like Visual Studio's multi-line cursor):
// typing, Backspace/Delete, Enter, the arrows, Home/End (with Shift and Ctrl), copy, cut and paste
// then act at every cursor, one undo step per key; Escape, a click or any other key leave only the
// main cursor.
// Virtual space: Up/Down (and adding cursors) keep the column; on a shorter line the cursor
// stands past its end, and typed or pasted text there is preceded by the spaces up to it. Nothing
// is written until then: moving on leaves the line as it was. Left and Backspace step back
// through the virtual space; any other key leaves it (the cursor is at the line's end again).
class CodeTextEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeTextEdit(QWidget* parent = nullptr);

    // Actions added to the end of the context menu (after the standard entries); owned elsewhere.
    void setExtraMenuActions(const QList<QAction*>& actions) { extraActions_ = actions; }

    // Sees every key press first; returning true means it was handled (e.g. the song editor's
    // space inside a chord).
    void setKeyFilter(std::function<bool(QKeyEvent*)> filter) { keyFilter_ = std::move(filter); }
    // With several cursors, the key filter's rules per cursor: typed text, Backspace and Delete
    // ask `hook(cursor, event, plainEdit)` at each cursor; it makes the edit there itself (and
    // returns true, e.g. a space typed in a chord goes after it) or leaves it to `plainEdit`.
    using CursorKeyHook = std::function<bool(QTextCursor& cursor, QKeyEvent* event,
                                             const std::function<void(QTextCursor&)>& plainEdit)>;
    void setCursorKeyHook(CursorKeyHook hook) { cursorKeyHook_ = std::move(hook); }
    // What a key press does without the filter (for a filter that wraps it, e.g. in an edit block).
    void processKey(QKeyEvent* event);

    void setLineNumberColors(const QColor& background, const QColor& number,
                             const QColor& currentNumber);

    // The main cursor first, then the extra ones.
    QList<QTextCursor> cursors() const;
    int extraCursorCount() const { return static_cast<int>(extraCursors_.size()); }
    // Adds a cursor on the line above (-1) or below (+1) the last one added (or the main one), at
    // the column the first one was in (or the line's end when shorter); the cursors grow in the
    // direction of the first one added, the other direction removes the last one added.
    void addCursorVertically(int direction);
    void clearExtraCursors();
    // The find bar's matches: drawn after the marks of the selection's occurrences, under the
    // multi-cursor selections.
    void setSearchMarks(const QList<QTextEdit::ExtraSelection>& marks);
    // What sounds while a score plays: drawn like the search marks (not a selection), in the
    // preview's orange, readable on a light and a dark background; empty clears them.
    void setPlaybackMarks(const QList<QTextCursor>& ranges);
    // Runs `edit` (changes made through QTextCursors of the document) as one undo step, keeping
    // the extra cursors (they move with the text).
    void editKeepingCursors(const std::function<void()>& edit);
    // Runs `edit` on every cursor (the main one included) as one undo step; each cursor ends
    // where `edit` leaves it.
    void editAtCursors(const std::function<void(QTextCursor&)>& edit);

protected:
    bool event(QEvent* event) override;
    bool viewportEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    bool handleEvent(QEvent* event); // event() without the user-input bookkeeping
    friend class LineNumberArea;
    friend struct AppTestAccess;
    // Marks the other occurrences of the selected text (one line, case-sensitive) with a lighter
    // background than the selection; none without a selection.
    void markOccurrences();
    // A key while there are extra cursors; false when it is not a multi-cursor key (the extra
    // cursors are gone then).
    bool multiCursorKey(QKeyEvent* event);
    void dropDuplicateCursors();
    // Without extra cursors: plain Up/Down keep the column (virtual space), Left/Backspace step
    // back in it, typed text is padded. True when the key was handled here.
    bool virtualSpaceKey(QKeyEvent* event);
    // `cursor` one line up (-1) or down (+1) at its column (`spaces` past the line's end
    // included); `spaces` becomes what is past the new line's end.
    void moveKeepingColumn(QTextCursor& cursor, int& spaces, int direction) const;
    // Columns as shown: a tab to the next tab stop, a character with its combining marks (or a
    // surrogate pair) one column.
    int columnOf(const QTextBlock& block, int positionInBlock) const;
    // The cursor position in `block` at `column` (never inside a character or before a tab's
    // stop it does not reach), and the spaces past the line's end still missing to it.
    int positionAtColumn(const QTextBlock& block, int column, int& spaces) const;
    int tabColumns() const;
    // A key that QPlainTextEdit inserts as text (its own rule: printable, not with Ctrl).
    static bool insertsText(const QKeyEvent* event);
    // Writes the virtual spaces of `cursor` (when it still stands at its line's end) and clears
    // them.
    static void padVirtualSpace(QTextCursor& cursor, int& spaces);
    // Runs `edit` on every cursor with its virtual spaces, as one undo step (editAtCursors).
    void editAtCursorsVirtual(const std::function<void(QTextCursor&, int&)>& edit);
    void setMainVirtualSpaces(int spaces);
    void updateExtraSelections();
    // Hides Qt's cursor while there are extra cursors (paintEvent draws them all), shows it after.
    void updateCursorDrawing();
    void restartBlink();
    int areaWidth() const;
    void updateAreaWidth();
    void updateArea(const QRect& rect, int dy);
    void paintArea(QPaintEvent* event);

    QList<QAction*> extraActions_;
    QList<QTextCursor> extraCursors_;
    QList<int> extraVirtual_; // per extra cursor: its virtual spaces past the line's end
    int mainVirtual_ = 0;     // the main cursor's
    bool virtualMove_ = false; // the main cursor is moved here: its virtual spaces stay
    QList<QTextEdit::ExtraSelection> occurrenceMarks_;
    QList<QTextEdit::ExtraSelection> searchMarks_;
    QList<QTextEdit::ExtraSelection> playbackMarks_;
    QTimer* blinkTimer_ = nullptr;
    bool blinkOn_ = true;
    int ownCursorWidth_ = -1;     // Qt's cursor width while it is hidden, else -1
    int growth_ = 0;              // -1 up, +1 down: where the extra cursors were added
    int multiColumn_ = -1;        // the column new cursors go to
    bool keepingCursors_ = false; // our own edits move the main cursor: keep the extra ones
    QTimer* occurrenceTimer_ = nullptr;
    int userInput_ = 0; // > 0 while a key or mouse event of the user is handled
    std::function<bool(QKeyEvent*)> keyFilter_;
    CursorKeyHook cursorKeyHook_;
    QWidget* area_ = nullptr;
    QColor background_;
    QColor number_;
    QColor currentNumber_;
};
