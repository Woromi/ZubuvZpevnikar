#include "CodeTextEdit.hpp"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTimer>
#include <algorithm>

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeTextEdit* edit) : QWidget(edit), edit_(edit) {}
    QSize sizeHint() const override { return {edit_->areaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* event) override { edit_->paintArea(event); }

private:
    CodeTextEdit* edit_;
};

CodeTextEdit::CodeTextEdit(QWidget* parent)
    : QPlainTextEdit(parent), area_(new LineNumberArea(this)) {
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeTextEdit::updateArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, area_, qOverload<>(&QWidget::update));
    // Other occurrences of the selected text are marked, like in code editors; after a short
    // pause, so dragging a selection over a long text stays smooth.
    occurrenceTimer_ = new QTimer(this);
    occurrenceTimer_->setSingleShot(true);
    occurrenceTimer_->setInterval(80);
    connect(occurrenceTimer_, &QTimer::timeout, this, &CodeTextEdit::markOccurrences);
    connect(this, &QPlainTextEdit::selectionChanged, occurrenceTimer_,
            qOverload<>(&QTimer::start));
    connect(this, &QPlainTextEdit::textChanged, occurrenceTimer_, qOverload<>(&QTimer::start));
    updateAreaWidth();
}

void CodeTextEdit::markOccurrences() {
    QList<QTextEdit::ExtraSelection> marks;
    const QTextCursor selection = textCursor();
    const QString text = selection.selectedText();
    // One line (a selection over lines holds U+2029), not only blanks, not huge.
    if (!text.isEmpty() && text.size() <= 200 && !text.contains(QChar::ParagraphSeparator) &&
        !text.trimmed().isEmpty()) {
        QColor background = palette().color(QPalette::Highlight);
        background.setAlpha(70); // lighter than the selection itself
        QTextCharFormat format;
        format.setBackground(background);
        constexpr int maxMarks = 2000;
        QTextCursor found = document()->find(text, 0, QTextDocument::FindCaseSensitively);
        while (!found.isNull() && marks.size() < maxMarks) {
            if (found.selectionStart() != selection.selectionStart())
                marks.append({found, format});
            found = document()->find(text, found, QTextDocument::FindCaseSensitively);
        }
    }
    setExtraSelections(marks);
}

void CodeTextEdit::setLineNumberColors(const QColor& background, const QColor& number,
                                       const QColor& currentNumber) {
    background_ = background;
    number_ = number;
    currentNumber_ = currentNumber;
    area_->update();
}

int CodeTextEdit::areaWidth() const {
    int digits = 3;
    for (int max = std::max(1, blockCount()); max >= 1000; max /= 10)
        ++digits;
    return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeTextEdit::updateAreaWidth() {
    setViewportMargins(areaWidth(), 0, 0, 0);
    const QRect cr = contentsRect();
    area_->setGeometry(QRect(cr.left(), cr.top(), areaWidth(), cr.height()));
}

void CodeTextEdit::updateArea(const QRect& rect, int dy) {
    if (dy)
        area_->scroll(0, dy);
    else
        area_->update(0, rect.y(), area_->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateAreaWidth();
}

void CodeTextEdit::contextMenuEvent(QContextMenuEvent* event) {
    if (extraActions_.isEmpty()) {
        QPlainTextEdit::contextMenuEvent(event);
        return;
    }
    QMenu* menu = createStandardContextMenu(event->pos());
    menu->addSeparator();
    menu->addActions(extraActions_);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

void CodeTextEdit::keyPressEvent(QKeyEvent* event) {
    if (keyFilter_ && keyFilter_(event))
        return;
    processKey(event);
}

void CodeTextEdit::processKey(QKeyEvent* event) {
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    const auto others = event->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    if (!enter || others != Qt::NoModifier || isReadOnly()) {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    // The new line starts with the current line's leading spaces and tabs (only up to the cursor,
    // when the cursor stands inside them), as one undo step with the line break.
    QTextCursor cursor = textCursor();
    // A selection made downwards starts on an earlier line than the cursor stands on.
    const QTextBlock startBlock = document()->findBlock(cursor.selectionStart());
    const QString line = startBlock.text();
    const int column = cursor.selectionStart() - startBlock.position();
    int indent = 0;
    while (indent < line.size() && indent < column &&
           (line[indent] == QLatin1Char(' ') || line[indent] == QLatin1Char('\t')))
        ++indent;
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    cursor.insertBlock();
    cursor.insertText(line.left(indent));
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureCursorVisible();
}

void CodeTextEdit::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    area_->setGeometry(QRect(cr.left(), cr.top(), areaWidth(), cr.height()));
}

void CodeTextEdit::paintArea(QPaintEvent* event) {
    QPainter painter(area_);
    painter.fillRect(event->rect(),
                     background_.isValid() ? background_ : palette().window().color());
    const int current = textCursor().blockNumber();
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal bottom = top + blockBoundingRect(block).height();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            painter.setPen(
                number == current
                    ? (currentNumber_.isValid() ? currentNumber_ : palette().text().color())
                    : (number_.isValid() ? number_ : palette().placeholderText().color()));
            painter.drawText(0, static_cast<int>(top), area_->width() - 4, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + blockBoundingRect(block).height();
        ++number;
    }
}
