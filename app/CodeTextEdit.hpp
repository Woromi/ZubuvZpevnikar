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
// Ctrl+Shift+Up/Down add cursors on the lines above/below (like a code editor's multi-cursor):
// typing, Backspace/Delete, Enter, the arrows, Home/End (with Shift and Ctrl), copy, cut and paste
// then act at every cursor, one undo step per key; Escape, a click or any other key leave only the
// main cursor.
class CodeTextEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeTextEdit(QWidget* parent = nullptr);

    // Actions added to the end of the context menu (after the standard entries); owned elsewhere.
    void setExtraMenuActions(const QList<QAction*>& actions) { extraActions_ = actions; }

    // Sees every key press first; returning true means it was handled (e.g. the song editor's
    // space inside a chord).
    void setKeyFilter(std::function<bool(QKeyEvent*)> filter) { keyFilter_ = std::move(filter); }
    // What a key press does without the filter (for a filter that wraps it, e.g. in an edit block).
    void processKey(QKeyEvent* event);

    void setLineNumberColors(const QColor& background, const QColor& number,
                             const QColor& currentNumber);

    // The main cursor first, then the extra ones.
    QList<QTextCursor> cursors() const;
    int extraCursorCount() const { return static_cast<int>(extraCursors_.size()); }
    // Adds a cursor on the line above (-1) or below (+1) the topmost/bottommost cursor, at the
    // column the first one was in (or the line's end when shorter).
    void addCursorVertically(int direction);
    void clearExtraCursors();
    // Runs `edit` (changes made through QTextCursors of the document) as one undo step, keeping
    // the extra cursors (they move with the text).
    void editKeepingCursors(const std::function<void()>& edit);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    friend class LineNumberArea;
    // Marks the other occurrences of the selected text (one line, case-sensitive) with a lighter
    // background than the selection; none without a selection.
    void markOccurrences();
    // A key while there are extra cursors; false when it is not a multi-cursor key (the extra
    // cursors are gone then).
    bool multiCursorKey(QKeyEvent* event);
    // Runs `edit` on every cursor (the main one included) as one undo step.
    void editAtCursors(const std::function<void(QTextCursor&)>& edit);
    void dropDuplicateCursors();
    void updateExtraSelections();
    int areaWidth() const;
    void updateAreaWidth();
    void updateArea(const QRect& rect, int dy);
    void paintArea(QPaintEvent* event);

    QList<QAction*> extraActions_;
    QList<QTextCursor> extraCursors_;
    QList<QTextEdit::ExtraSelection> occurrenceMarks_;
    int multiColumn_ = -1;        // the column new cursors go to
    bool keepingCursors_ = false; // our own edits move the main cursor: keep the extra ones
    QTimer* occurrenceTimer_ = nullptr;
    std::function<bool(QKeyEvent*)> keyFilter_;
    QWidget* area_ = nullptr;
    QColor background_;
    QColor number_;
    QColor currentNumber_;
};
