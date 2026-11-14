#pragma once

#include <QAction>
#include <QColor>
#include <QList>
#include <QPlainTextEdit>
#include <functional>

// A QPlainTextEdit with a line-number area on the left (Qt's "Code Editor" example). The
// numbers are the 1-based block numbers, which are the file's lines (the editor does not wrap).
// Enter keeps the current line's indentation.
class CodeTextEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeTextEdit(QWidget* parent = nullptr);

    // Actions added to the end of the context menu (after the standard entries); owned elsewhere.
    void setExtraMenuActions(const QList<QAction*>& actions) { extraActions_ = actions; }

    // Sees every key press first; returning true means it was handled (e.g. the song editor's
    // space inside a chord).
    void setKeyFilter(std::function<bool(QKeyEvent*)> filter) { keyFilter_ = std::move(filter); }

    void setLineNumberColors(const QColor& background, const QColor& number,
                             const QColor& currentNumber);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    friend class LineNumberArea;
    int areaWidth() const;
    void updateAreaWidth();
    void updateArea(const QRect& rect, int dy);
    void paintArea(QPaintEvent* event);

    QList<QAction*> extraActions_;
    std::function<bool(QKeyEvent*)> keyFilter_;
    QWidget* area_ = nullptr;
    QColor background_;
    QColor number_;
    QColor currentNumber_;
};
