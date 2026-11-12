#pragma once

#include <QColor>
#include <QPlainTextEdit>

// A QPlainTextEdit with a line-number area on the left (Qt's "Code Editor" example). The
// numbers are the 1-based block numbers, which are the file's lines (the editor does not wrap).
class CodeTextEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit CodeTextEdit(QWidget* parent = nullptr);

    void setLineNumberColors(const QColor& background, const QColor& number,
                             const QColor& currentNumber);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    friend class LineNumberArea;
    int areaWidth() const;
    void updateAreaWidth();
    void updateArea(const QRect& rect, int dy);
    void paintArea(QPaintEvent* event);

    QWidget* area_ = nullptr;
    QColor background_;
    QColor number_;
    QColor currentNumber_;
};
