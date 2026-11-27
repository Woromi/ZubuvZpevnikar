#include "CodeTextEdit.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDropEvent>
#include <QFontMetricsF>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTextLayout>
#include <QMenu>
#include <QMimeData>
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
    // Only a selection the user makes (keys, mouse) marks its occurrences: one the app sets (the
    // playing note, a click in the score preview, find & replace, undo) is just a selection.
    connect(this, &QPlainTextEdit::selectionChanged, this, [this] {
        if (userInput_ > 0) {
            occurrenceTimer_->start();
        } else if (!occurrenceMarks_.isEmpty()) {
            occurrenceTimer_->stop();
            occurrenceMarks_.clear();
            updateExtraSelections();
        }
    });
    // The marks of the user's selection follow the text.
    connect(this, &QPlainTextEdit::textChanged, this, [this] {
        if (userInput_ > 0 || !occurrenceMarks_.isEmpty())
            occurrenceTimer_->start();
    });
    // The main cursor moved by anything but a multi-cursor edit: back to one cursor.
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (!keepingCursors_)
            clearExtraCursors();
        else if (!extraCursors_.isEmpty())
            restartBlink();
        // Moved by anything but Up/Down or an edit at every cursor: out of the virtual space.
        if (!keepingCursors_ && !virtualMove_ && mainVirtual_ > 0)
            setMainVirtualSpaces(0);
    });
    blinkTimer_ = new QTimer(this);
    connect(blinkTimer_, &QTimer::timeout, this, [this] {
        blinkOn_ = !blinkOn_;
        viewport()->update();
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

void CodeTextEdit::setSearchMarks(const QList<QTextEdit::ExtraSelection>& marks) {
    searchMarks_ = marks;
    updateExtraSelections();
}

void CodeTextEdit::setPlaybackMarks(const QList<QTextCursor>& ranges) {
    if (ranges.isEmpty() && playbackMarks_.isEmpty())
        return;
    QTextCharFormat format;
    // A translucent orange (the preview's "sounding" boxes): the text colour stays readable.
    format.setBackground(QColor(255, 140, 0, 110));
    playbackMarks_.clear();
    for (const QTextCursor& cursor : ranges)
        if (cursor.hasSelection())
            playbackMarks_.append({cursor, format});
    updateExtraSelections();
}

void CodeTextEdit::updateExtraSelections() {
    QList<QTextEdit::ExtraSelection> all = occurrenceMarks_;
    all.append(playbackMarks_);
    all.append(searchMarks_);
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
    growth_ = 0;
    if (extraCursors_.isEmpty())
        return;
    extraCursors_.clear();
    extraVirtual_.clear();
    updateExtraSelections();
    updateCursorDrawing();
}

void CodeTextEdit::addCursorVertically(int direction) {
    if (isReadOnly())
        return;
    // Like a code editor: the cursors grow away from the first one; the other direction takes
    // back the last one added.
    if (!extraCursors_.isEmpty() && direction != growth_) {
        extraCursors_.removeLast();
        extraVirtual_.removeLast();
        if (extraCursors_.isEmpty())
            growth_ = 0;
        updateExtraSelections();
        updateCursorDrawing();
        return;
    }
    if (multiColumn_ < 0)
        multiColumn_ = columnOf(textCursor().block(), textCursor().positionInBlock()) + mainVirtual_;
    const QTextCursor edge = extraCursors_.isEmpty() ? textCursor() : extraCursors_.last();
    const QTextBlock block = direction < 0 ? edge.block().previous() : edge.block().next();
    if (!block.isValid())
        return;
    // At the first cursor's column, past the end of a shorter line (virtual space).
    int spaces = 0;
    QTextCursor added(document());
    added.setPosition(block.position() + positionAtColumn(block, multiColumn_, spaces));
    extraCursors_.append(added);
    extraVirtual_.append(spaces);
    growth_ = direction;
    dropDuplicateCursors();
    updateExtraSelections();
    updateCursorDrawing();
}

void CodeTextEdit::dropDuplicateCursors() {
    QList<int> seen{textCursor().position()};
    QList<QTextCursor> kept;
    QList<int> keptVirtual;
    for (qsizetype i = 0; i < extraCursors_.size(); ++i) {
        const QTextCursor& cursor = extraCursors_[i];
        if (seen.contains(cursor.position()))
            continue;
        seen.append(cursor.position());
        kept.append(cursor);
        keptVirtual.append(extraVirtual_.value(i));
    }
    extraCursors_ = kept;
    extraVirtual_ = keptVirtual;
}

int CodeTextEdit::tabColumns() const {
    const qreal space = QFontMetricsF(document()->defaultFont()).horizontalAdvance(QLatin1Char(' '));
    return space > 0 ? std::max(1, qRound(tabStopDistance() / space)) : 4;
}

namespace {
// The next cursor position after `i` in `block` (a character with its combining marks, a
// surrogate pair).
int nextPosition(const QTextBlock& block, int i) {
    const QTextLayout* layout = block.layout();
    const int next = layout ? layout->nextCursorPosition(i) : i + 1;
    return next > i ? next : i + 1;
}
} // namespace

int CodeTextEdit::columnOf(const QTextBlock& block, int positionInBlock) const {
    const QString text = block.text();
    const int tab = tabColumns();
    int column = 0;
    for (int i = 0; i < positionInBlock && i < text.size(); i = nextPosition(block, i))
        column = text[i] == QLatin1Char('\t') ? (column / tab + 1) * tab : column + 1;
    return column;
}

int CodeTextEdit::positionAtColumn(const QTextBlock& block, int column, int& spaces) const {
    const QString text = block.text();
    const int tab = tabColumns();
    int at = 0;
    int i = 0;
    while (i < text.size()) {
        const int next = text[i] == QLatin1Char('\t') ? (at / tab + 1) * tab : at + 1;
        if (next > column)
            break;
        at = next;
        i = nextPosition(block, i);
    }
    spaces = i >= text.size() ? std::max(0, column - at) : 0;
    return std::min(i, static_cast<int>(text.size()));
}

void CodeTextEdit::moveKeepingColumn(QTextCursor& cursor, int& spaces, int direction) const {
    const int column = columnOf(cursor.block(), cursor.positionInBlock()) + spaces;
    const QTextBlock block =
        direction < 0 ? cursor.block().previous() : cursor.block().next();
    if (!block.isValid())
        return;
    cursor.setPosition(block.position() + positionAtColumn(block, column, spaces));
}

bool CodeTextEdit::insertsText(const QKeyEvent* event) {
    const auto modifiers = event->modifiers() &
                           ~(Qt::ShiftModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier);
    const QString text = event->text();
    return modifiers != Qt::ControlModifier && !text.isEmpty() &&
           std::ranges::all_of(text, [](QChar c) { return c.isPrint() || c == QLatin1Char('\t'); });
}

void CodeTextEdit::padVirtualSpace(QTextCursor& cursor, int& spaces) {
    if (spaces > 0 && !cursor.hasSelection() && cursor.atBlockEnd())
        cursor.insertText(QString(spaces, QLatin1Char(' ')));
    spaces = 0;
}

void CodeTextEdit::setMainVirtualSpaces(int spaces) {
    if (spaces == mainVirtual_)
        return;
    mainVirtual_ = spaces;
    updateCursorDrawing();
    viewport()->update();
}

void CodeTextEdit::editAtCursorsVirtual(const std::function<void(QTextCursor&, int&)>& edit) {
    int mainSpaces = mainVirtual_;
    editKeepingCursors([&] {
        QTextCursor main = textCursor();
        edit(main, mainSpaces);
        for (qsizetype i = 0; i < extraCursors_.size(); ++i)
            edit(extraCursors_[i], extraVirtual_[i]);
        setTextCursor(main);
    });
    setMainVirtualSpaces(mainSpaces);
    ensureCursorVisible();
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
    // These edits do not use the virtual space: the cursors are where the text is.
    std::ranges::fill(extraVirtual_, 0);
    setMainVirtualSpaces(0);
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

// A new empty line below (Ctrl+Enter, Shift+Enter) or above (Ctrl+Shift+Enter) the cursor's line,
// indented like it, the cursor on it; the line itself is not split.
void openLine(QTextCursor& cursor, bool above) {
    const QString line = cursor.block().text();
    int indent = 0;
    while (indent < line.size() &&
           (line[indent] == QLatin1Char(' ') || line[indent] == QLatin1Char('\t')))
        ++indent;
    cursor.clearSelection();
    if (above) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.insertText(line.left(indent));
        const int end = cursor.position();
        cursor.insertBlock();
        cursor.setPosition(end);
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock();
        cursor.insertText(line.left(indent));
    }
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
        int mainSpaces = mainVirtual_;
        editKeepingCursors([&] {
            QTextCursor main = textCursor();
            QList<std::pair<QTextCursor*, int*>> all{{&main, &mainSpaces}};
            for (qsizetype i = 0; i < extraCursors_.size(); ++i)
                all.append({&extraCursors_[i], &extraVirtual_[i]});
            std::ranges::sort(all, {}, [](const auto& c) { return c.first->selectionStart(); });
            for (qsizetype i = 0; i < all.size(); ++i) {
                padVirtualSpace(*all[i].first, *all[i].second);
                all[i].first->insertText(spread ? lines[i] : text);
            }
            setTextCursor(main);
        });
        setMainVirtualSpaces(mainSpaces);
        ensureCursorVisible();
        return true;
    }

    using Move = QTextCursor::MoveOperation;
    const auto move = [&](Move plain, Move word) {
        const auto mode = shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
        keepingCursors_ = true;
        QTextCursor main = textCursor();
        int mainSpaces = mainVirtual_;
        QList<std::pair<QTextCursor*, int*>> all{{&main, &mainSpaces}};
        for (qsizetype i = 0; i < extraCursors_.size(); ++i)
            all.append({&extraCursors_[i], &extraVirtual_[i]});
        for (auto [cursor, spaces] : all) {
            if (!shift && cursor->hasSelection() && (key == Qt::Key_Left || key == Qt::Key_Right)) {
                // Like a code editor: Left/Right first collapse a selection to its edge.
                cursor->setPosition(key == Qt::Key_Left ? cursor->selectionStart()
                                                        : cursor->selectionEnd());
                *spaces = 0;
                continue;
            }
            if (!shift && !ctrl && (key == Qt::Key_Up || key == Qt::Key_Down)) {
                moveKeepingColumn(*cursor, *spaces, key == Qt::Key_Up ? -1 : +1);
                continue;
            }
            if (!shift && !ctrl && key == Qt::Key_Left && *spaces > 0 && cursor->atBlockEnd()) {
                --*spaces; // back through the virtual space
                continue;
            }
            *spaces = 0;
            cursor->movePosition(ctrl ? word : plain, mode);
        }
        setTextCursor(main);
        keepingCursors_ = false;
        setMainVirtualSpaces(mainSpaces);
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
            const std::function<void(QTextCursor&)> plain = [&](QTextCursor& cursor) {
                if (!cursor.hasSelection()) {
                    if (ctrl)
                        cursor.movePosition(back ? Move::PreviousWord : Move::NextWord,
                                            QTextCursor::KeepAnchor);
                    else
                        cursor.movePosition(back ? Move::PreviousCharacter : Move::NextCharacter,
                                            QTextCursor::KeepAnchor);
                }
                cursor.removeSelectedText();
            };
            editAtCursorsVirtual([&](QTextCursor& cursor, int& spaces) {
                // Backspace in the virtual space only steps back.
                if (back && !ctrl && spaces > 0 && !cursor.hasSelection() &&
                    cursor.atBlockEnd()) {
                    --spaces;
                    return;
                }
                spaces = 0;
                if (!cursorKeyHook_ || !cursorKeyHook_(cursor, event, plain))
                    plain(cursor);
            });
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (ctrl || shift) {
                const bool above = ctrl && shift;
                editAtCursors([above](QTextCursor& cursor) { openLine(cursor, above); });
                return true;
            }
            if (!ctrl) {
                editAtCursors(insertLineBreak);
                return true;
            }
            break;
        default:
            break;
        }
    }
    // Text as QPlainTextEdit would insert it (also AltGr arriving as Ctrl+Alt).
    if (insertsText(event)) {
        const QString text = event->text();
        const std::function<void(QTextCursor&)> plain = [&](QTextCursor& cursor) {
            cursor.insertText(text);
        };
        editAtCursorsVirtual([&](QTextCursor& cursor, int& spaces) {
            padVirtualSpace(cursor, spaces);
            if (!cursorKeyHook_ || !cursorKeyHook_(cursor, event, plain))
                plain(cursor);
        });
        return true;
    }
    clearExtraCursors();
    return false;
}

bool CodeTextEdit::viewportEvent(QEvent* event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove: {
        ++userInput_;
        const bool handled = QPlainTextEdit::viewportEvent(event);
        --userInput_;
        return handled;
    }
    default:
        return QPlainTextEdit::viewportEvent(event);
    }
}

bool CodeTextEdit::event(QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        ++userInput_; // a selection made by the keyboard is the user's
        const bool handled = handleEvent(event);
        --userInput_;
        return handled;
    }
    return handleEvent(event);
}

bool CodeTextEdit::handleEvent(QEvent* event) {
    // Read-only: nothing is typed, so no cursor waits past a line's end.
    if (event->type() == QEvent::ReadOnlyChange && isReadOnly()) {
        std::ranges::fill(extraVirtual_, 0);
        setMainVirtualSpaces(0);
    }
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
    // With several cursors Qt's own one is hidden: all of them are drawn here, blinking together.
    // So is a cursor in the virtual space (past its line's end).
    if (ownCursorWidth_ < 0 || !hasFocus() || !blinkOn_)
        return;
    QPainter painter(viewport());
    const qreal space =
        QFontMetricsF(document()->defaultFont()).horizontalAdvance(QLatin1Char(' '));
    const auto draw = [&](const QTextCursor& cursor, int spaces) {
        const QRect rect = cursorRect(cursor);
        const int x = rect.x() + (cursor.atBlockEnd() && !cursor.hasSelection()
                                      ? qRound(spaces * space)
                                      : 0);
        painter.fillRect(x, rect.y(), std::max(1, ownCursorWidth_), rect.height(),
                         palette().text());
    };
    draw(textCursor(), mainVirtual_);
    for (qsizetype i = 0; i < extraCursors_.size(); ++i)
        draw(extraCursors_[i], extraVirtual_.value(i));
}

void CodeTextEdit::updateCursorDrawing() {
    if (!extraCursors_.isEmpty() || mainVirtual_ > 0) {
        if (ownCursorWidth_ < 0) {
            ownCursorWidth_ = cursorWidth();
            setCursorWidth(0);
        }
        restartBlink();
    } else if (ownCursorWidth_ >= 0) {
        setCursorWidth(ownCursorWidth_);
        ownCursorWidth_ = -1;
        blinkTimer_->stop();
        viewport()->update();
    }
}

void CodeTextEdit::restartBlink() {
    // Shown right after every key and move, like Qt's own cursor; no blinking when the system
    // turns it off.
    blinkOn_ = true;
    const int flash = QApplication::cursorFlashTime();
    if (flash > 0)
        blinkTimer_->start(flash / 2);
    else
        blinkTimer_->stop();
    viewport()->update();
}

void CodeTextEdit::mousePressEvent(QMouseEvent* event) {
    clearExtraCursors();
    setMainVirtualSpaces(0);
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
    // Alt+Shift+Up/Down, as in Visual Studio (Alt+Up/Down move the line).
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) &&
        modifiers == (Qt::AltModifier | Qt::ShiftModifier)) {
        addCursorVertically(event->key() == Qt::Key_Up ? -1 : +1);
        return;
    }
    if (!extraCursors_.isEmpty() && multiCursorKey(event)) {
        if (!extraCursors_.isEmpty() || mainVirtual_ > 0)
            restartBlink();
        return;
    }
    // Text typed in the virtual space: the spaces and the key's edit are one undo step.
    QTextCursor editBlock(document());
    bool padded = false;
    if (extraCursors_.isEmpty()) {
        if (virtualSpaceKey(event))
            return;
        // (A paste is padded by insertFromMimeData, only when something is pasted.)
        if (mainVirtual_ > 0 && !isReadOnly() && !event->matches(QKeySequence::Paste)) {
            editBlock.beginEditBlock();
            padded = true;
            QTextCursor cursor = textCursor();
            int spaces = mainVirtual_;
            padVirtualSpace(cursor, spaces);
            setTextCursor(cursor); // out of the virtual space
        }
    }
    if (!keyFilter_ || !keyFilter_(event))
        processKey(event);
    if (padded)
        editBlock.endEditBlock();
}

bool CodeTextEdit::virtualSpaceKey(QKeyEvent* event) {
    if (isReadOnly()) {
        setMainVirtualSpaces(0);
        return false;
    }
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    if ((key == Qt::Key_Up || key == Qt::Key_Down) && modifiers == Qt::NoModifier &&
        !textCursor().hasSelection()) {
        QTextCursor cursor = textCursor();
        int spaces = mainVirtual_;
        moveKeepingColumn(cursor, spaces, key == Qt::Key_Up ? -1 : +1);
        virtualMove_ = true;
        setTextCursor(cursor);
        virtualMove_ = false;
        setMainVirtualSpaces(spaces);
        if (spaces > 0)
            restartBlink();
        ensureCursorVisible();
        return true;
    }
    if (mainVirtual_ == 0)
        return false;
    if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt ||
        key == Qt::Key_Meta || key == Qt::Key_AltGr)
        return true; // a modifier alone changes nothing
    if (!textCursor().atBlockEnd() || textCursor().hasSelection()) {
        setMainVirtualSpaces(0);
        return false;
    }
    if ((key == Qt::Key_Left || key == Qt::Key_Backspace) && modifiers == Qt::NoModifier) {
        setMainVirtualSpaces(mainVirtual_ - 1);
        restartBlink();
        return true;
    }
    // Typed text is padded by keyPressEvent, a paste by insertFromMimeData.
    if (!insertsText(event) && !event->matches(QKeySequence::Paste))
        setMainVirtualSpaces(0);
    return false;
}

void CodeTextEdit::insertFromMimeData(const QMimeData* source) {
    if (mainVirtual_ == 0 || !extraCursors_.isEmpty() || isReadOnly() || !source ||
        !source->hasText() || source->text().isEmpty()) {
        setMainVirtualSpaces(0);
        QPlainTextEdit::insertFromMimeData(source);
        return;
    }
    // Pasted in the virtual space: the spaces first, one undo step with the text.
    QTextCursor editBlock(document());
    editBlock.beginEditBlock();
    QTextCursor cursor = textCursor();
    int spaces = mainVirtual_;
    padVirtualSpace(cursor, spaces);
    setTextCursor(cursor);
    QPlainTextEdit::insertFromMimeData(source);
    editBlock.endEditBlock();
}

void CodeTextEdit::inputMethodEvent(QInputMethodEvent* event) {
    // Dead keys and input methods (ó, ň, ...) commit text without a key press: padded too.
    const bool text = !event->commitString().isEmpty() || !event->preeditString().isEmpty();
    if (!text || mainVirtual_ == 0 || !extraCursors_.isEmpty() || isReadOnly()) {
        QPlainTextEdit::inputMethodEvent(event);
        return;
    }
    QTextCursor editBlock(document());
    editBlock.beginEditBlock();
    QTextCursor cursor = textCursor();
    int spaces = mainVirtual_;
    padVirtualSpace(cursor, spaces);
    setTextCursor(cursor);
    QPlainTextEdit::inputMethodEvent(event);
    editBlock.endEditBlock();
}

void CodeTextEdit::dropEvent(QDropEvent* event) {
    setMainVirtualSpaces(0); // dropped where the mouse is, not at the cursor
    QPlainTextEdit::dropEvent(event);
}

void CodeTextEdit::processKey(QKeyEvent* event) {
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    const auto modifiers = event->modifiers() & ~Qt::KeypadModifier;
    // Shift+Enter and Ctrl+Enter: a new line below this one; Ctrl+Shift+Enter: above it.
    if (enter && !isReadOnly() &&
        (modifiers == Qt::ShiftModifier || modifiers == Qt::ControlModifier ||
         modifiers == (Qt::ControlModifier | Qt::ShiftModifier))) {
        QTextCursor cursor = textCursor();
        cursor.beginEditBlock();
        openLine(cursor, modifiers == (Qt::ControlModifier | Qt::ShiftModifier));
        cursor.endEditBlock();
        setTextCursor(cursor);
        ensureCursorVisible();
        return;
    }
    if (!enter || modifiers != Qt::NoModifier || isReadOnly()) {
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
