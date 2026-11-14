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

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    friend class LineNumberArea;
    // Marks the other occurrences of the selected text (one line, case-sensitive) with a lighter
    // background than the selection; none without a selection.
    void markOccurrences();
    int areaWidth() const;
    void updateAreaWidth();
    void updateArea(const QRect& rect, int dy);
    void paintArea(QPaintEvent* event);

    QList<QAction*> extraActions_;
    QTimer* occurrenceTimer_ = nullptr;
    std::function<bool(QKeyEvent*)> keyFilter_;
    QWidget* area_ = nullptr;
    QColor background_;
    QColor number_;
    QColor currentNumber_;
};
