#include "CodeTextEdit.hpp"

#include <QApplication>
#include <QClipboard>
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
    connect(this, &QPlainTextEdit::selectionChanged, occurrenceTimer_, qOverload<>(&QTimer::start));
    connect(this, &QPlainTextEdit::textChanged, occurrenceTimer_, qOverload<>(&QTimer::start));
    // The main cursor moved by anything but a multi-cursor edit: back to one cursor.
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (!keepingCursors_)
            clearExtraCursors();
    });
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
    occurrenceMarks_ = marks;
    updateExtraSelections();
}

void CodeTextEdit::updateExtraSelections() {
    QList<QTextEdit::ExtraSelection> all = occurrenceMarks_;
    QTextCharFormat format;
    format.setBackground(palette().color(QPalette::Highlight));
    format.setForeground(palette().color(QPalette::HighlightedText));
    for (const QTextCursor& cursor : std::as_const(extraCursors_))
        if (cursor.hasSelection())
            all.append({cursor, format});
    setExtraSelections(all);
}

QList<QTextCursor> CodeTextEdit::cursors() const {
    QList<QTextCursor> all{textCursor()};
    all.append(extraCursors_);
    return all;
}

void CodeTextEdit::clearExtraCursors() {
    multiColumn_ = -1;
    if (extraCursors_.isEmpty())
        return;
    extraCursors_.clear();
    updateExtraSelections();
    viewport()->update();
}

void CodeTextEdit::addCursorVertically(int direction) {
    if (isReadOnly())
        return;
    const QList<QTextCursor> all = cursors();
    if (multiColumn_ < 0)
        multiColumn_ = textCursor().positionInBlock();
    const QTextCursor edge =
        *std::ranges::max_element(all, [direction](const QTextCursor& a, const QTextCursor& b) {
            return direction < 0 ? a.position() > b.position() : a.position() < b.position();
        });
    const QTextBlock block = direction < 0 ? edge.block().previous() : edge.block().next();
    if (!block.isValid())
        return;
    QTextCursor added(document());
    added.setPosition(block.position() + std::min(multiColumn_, block.length() - 1));
    extraCursors_.append(added);
    dropDuplicateCursors();
    updateExtraSelections();
    viewport()->update();
}

void CodeTextEdit::dropDuplicateCursors() {
    QList<int> seen{textCursor().position()};
    QList<QTextCursor> kept;
    for (const QTextCursor& cursor : std::as_const(extraCursors_)) {
        if (seen.contains(cursor.position()))
            continue;
        seen.append(cursor.position());
        kept.append(cursor);
    }
    extraCursors_ = kept;
}

void CodeTextEdit::editKeepingCursors(const std::function<void()>& edit) {
    const bool was = keepingCursors_;
    keepingCursors_ = true;
    QTextCursor block(document());
    block.beginEditBlock();
    edit();
    block.endEditBlock();
    keepingCursors_ = was;
    dropDuplicateCursors();
    updateExtraSelections();
    viewport()->update();
}

void CodeTextEdit::editAtCursors(const std::function<void(QTextCursor&)>& edit) {
    editKeepingCursors([&] {
        QTextCursor main = textCursor();
        edit(main);
        for (QTextCursor& cursor : extraCursors_)
            edit(cursor);
        setTextCursor(main);
    });
    ensureCursorVisible();
}

namespace {

// The cursors in text order.
QList<QTextCursor> inTextOrder(QList<QTextCursor> cursors) {
    std::ranges::sort(cursors, {}, [](const QTextCursor& c) { return c.selectionStart(); });
    return cursors;
}

// Enter at `cursor`: a line break and the leading spaces and tabs of its line up to the cursor.
void insertLineBreak(QTextCursor& cursor) {
    cursor.removeSelectedText();
    const QString line = cursor.block().text();
    const int column = cursor.positionInBlock();
    int indent = 0;
    while (indent < line.size() && indent < column &&
           (line[indent] == QLatin1Char(' ') || line[indent] == QLatin1Char('\t')))
        ++indent;
    cursor.insertBlock();
    cursor.insertText(line.left(indent));
}

} // namespace

bool CodeTextEdit::multiCursorKey(QKeyEvent* event) {
    const Qt::KeyboardModifiers modifiers =
        event->modifiers() & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    const bool shift = modifiers & Qt::ShiftModifier;
    const bool ctrl = modifiers & Qt::ControlModifier;
    const bool alt = modifiers & (Qt::AltModifier | Qt::MetaModifier);
    const int key = event->key();
    if (key == Qt::Key_Escape && modifiers == Qt::NoModifier) {
        clearExtraCursors();
        return true;
    }
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
        key == Qt::Key_Meta || key == Qt::Key_AltGr)
        return true; // a modifier alone changes nothing
    if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) {
        QStringList parts;
        for (const QTextCursor& cursor : inTextOrder(cursors()))
            parts.append(
                cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n')));
        if (std::ranges::any_of(parts, [](const QString& part) { return !part.isEmpty(); })) {
            QApplication::clipboard()->setText(parts.join(QLatin1Char('\n')));
            if (event->matches(QKeySequence::Cut) && !isReadOnly())
                editAtCursors([](QTextCursor& cursor) { cursor.removeSelectedText(); });
        }
        return true;
    }
    if (isReadOnly()) {
        clearExtraCursors();
        return false;
    }
    if (event->matches(QKeySequence::Paste)) {
        const QString text = QApplication::clipboard()->text();
        QStringList lines = text.split(QLatin1Char('\n'));
        if (lines.size() > 1 && lines.last().isEmpty())
            lines.removeLast();
        // As many lines as cursors (e.g. copied from as many cursors): one line each, in text
        // order.
        const bool spread = lines.size() == cursors().size();
        editKeepingCursors([&] {
            QTextCursor main = textCursor();
            QList<QTextCursor*> all{&main};
            for (QTextCursor& extra : extraCursors_)
                all.append(&extra);
            std::ranges::sort(all, {}, [](const QTextCursor* c) { return c->selectionStart(); });
            for (qsizetype i = 0; i < all.size(); ++i)
                all[i]->insertText(spread ? lines[i] : text);
            setTextCursor(main);
        });
        ensureCursorVisible();
        return true;
    }

    using Move = QTextCursor::MoveOperation;
    const auto move = [&](Move plain, Move word) {
        const auto mode = shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
        keepingCursors_ = true;
        QTextCursor main = textCursor();
        for (QTextCursor* cursor : [&] {
                 QList<QTextCursor*> all{&main};
                 for (QTextCursor& extra : extraCursors_)
                     all.append(&extra);
                 return all;
             }()) {
            if (!shift && cursor->hasSelection() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
                // Like a code editor: Left/Right first collapse a selection to its edge.
                cursor->setPosition(key == Qt::Key_Left ? cursor->selectionStart()
                                                        : cursor->selectionEnd());
                continue;
            }
            cursor->movePosition(ctrl ? word : plain, mode);
        }
        setTextCursor(main);
        keepingCursors_ = false;
        dropDuplicateCursors();
        updateExtraSelections();
        viewport()->update();
        ensureCursorVisible();
        return true;
    };
    if (!alt) {
        switch (key) {
        case Qt::Key_Left:
            return move(Move::Left, Move::PreviousWord);
        case Qt::Key_Right:
            return move(Move::Right, Move::NextWord);
        case Qt::Key_Up:
            if (!ctrl)
                return move(Move::Up, Move::Up);
            break;
        case Qt::Key_Down:
            if (!ctrl)
                return move(Move::Down, Move::Down);
            break;
        case Qt::Key_Home:
            if (!ctrl)
                return move(Move::StartOfLine, Move::StartOfLine);
            break;
        case Qt::Key_End:
            if (!ctrl)
                return move(Move::EndOfLine, Move::EndOfLine);
            break;
        case Qt::Key_Backspace:
        case Qt::Key_Delete: {
            const bool back = key == Qt::Key_Backspace;
            editAtCursors([&](QTextCursor& cursor) {
                if (!cursor.hasSelection()) {
                    if (ctrl)
                        cursor.movePosition(back ? Move::PreviousWord : Move::NextWord,
                                            QTextCursor::KeepAnchor);
                    else
                        cursor.movePosition(back ? Move::PreviousCharacter : Move::NextCharacter,
                                            QTextCursor::KeepAnchor);
                }
                cursor.removeSelectedText();
            });
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (!ctrl) {
                editAtCursors(insertLineBreak);
                return true;
            }
            break;
        default:
            break;
        }
        const QString text = event->text();
        if (!ctrl && !text.isEmpty() && std::ranges::all_of(text, [](QChar c) {
                return c.isPrint() || c == QLatin1Char('\t');
            })) {
            editAtCursors([&](QTextCursor& cursor) { cursor.insertText(text); });
            return true;
        }
    }
    clearExtraCursors();
    return false;
}

bool CodeTextEdit::event(QEvent* event) {
    // With several cursors, copy, cut and paste and Escape act at all of them, before any
    // shortcut of the window gets these keys.
    if (event->type() == QEvent::ShortcutOverride && !extraCursors_.isEmpty()) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->matches(QKeySequence::Copy) || key->matches(QKeySequence::Cut) ||
            key->matches(QKeySequence::Paste) || key->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
    }
    return QPlainTextEdit::event(event);
}

void CodeTextEdit::paintEvent(QPaintEvent* event) {
    QPlainTextEdit::paintEvent(event);
    if (extraCursors_.isEmpty())
        return;
    QPainter painter(viewport());
    for (const QTextCursor& cursor : std::as_const(extraCursors_)) {
        const QRect rect = cursorRect(cursor);
        painter.fillRect(rect.x(), rect.y(), std::max(1, cursorWidth()), rect.height(),
                         palette().text());
    }
}

void CodeTextEdit::mousePressEvent(QMouseEvent* event) {
    clearExtraCursors();
    QPlainTextEdit::mousePressEvent(event);
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
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) &&
        modifiers == (Qt::ControlModifier | Qt::ShiftModifier)) {
        addCursorVertically(event->key() == Qt::Key_Up ? -1 : +1);
        return;
    }
    if (!extraCursors_.isEmpty() && multiCursorKey(event))
        return;
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
