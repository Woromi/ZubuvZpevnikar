#include "FindBar.hpp"

#include "CodeTextEdit.hpp"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QToolButton* toggleButton(const QString& text, const QString& toolTip, const QString& name,
                          QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setObjectName(name);
    button->setCheckable(true);
    button->setAutoRaise(true);
    return button;
}

QToolButton* plainButton(const QString& text, const QString& toolTip, const QString& name,
                         QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setObjectName(name);
    button->setAutoRaise(true);
    return button;
}

constexpr int maxMarks = 2000;

} // namespace

FindBar::FindBar(CodeTextEdit* edit, QWidget* parent) : QWidget(parent), edit_(edit) {
    setObjectName(QStringLiteral("findBar"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto* findRow = new QHBoxLayout;
    // Like VSCodium's: the arrow left of the find field opens and closes the replace row.
    expand_ = new QToolButton(this);
    expand_->setObjectName(QStringLiteral("findExpand"));
    expand_->setCheckable(true);
    expand_->setAutoRaise(true);
    expand_->setToolTip(tr("Find and replace (Ctrl+H)"));
    findRow->addWidget(expand_);
    find_ = new QLineEdit(this);
    find_->setObjectName(QStringLiteral("findField"));
    find_->setPlaceholderText(tr("Find"));
    find_->setClearButtonEnabled(true);
    findRow->addWidget(find_, 1);
    matchCase_ = toggleButton(tr("Aa"), tr("Match case"), QStringLiteral("findMatchCase"), this);
    wholeWord_ = toggleButton(tr("ab"), tr("Whole word"), QStringLiteral("findWholeWord"), this);
    regex_ = toggleButton(tr(".*"), tr("Regular expression"), QStringLiteral("findRegex"), this);
    for (QToolButton* button : {matchCase_, wholeWord_, regex_})
        findRow->addWidget(button);
    count_ = new QLabel(this);
    count_->setObjectName(QStringLiteral("findCount"));
    count_->setMinimumWidth(110);
    count_->setAlignment(Qt::AlignCenter);
    findRow->addWidget(count_);
    previous_ = plainButton(QStringLiteral("↑"), tr("Previous match (Shift+Enter, Shift+F3)"),
                            QStringLiteral("findPrevious"), this);
    next_ = plainButton(QStringLiteral("↓"), tr("Next match (Enter, F3)"),
                        QStringLiteral("findNext"), this);
    auto* close =
        plainButton(QStringLiteral("×"), tr("Close (Escape)"), QStringLiteral("findClose"), this);
    for (QToolButton* button : {previous_, next_, close})
        findRow->addWidget(button);
    layout->addLayout(findRow);

    replaceRow_ = new QWidget(this);
    auto* replaceLayout = new QHBoxLayout(replaceRow_);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    // Under the find field, not under the arrow.
    replaceLayout->addSpacing(expand_->sizeHint().width() + findRow->spacing());
    replace_ = new QLineEdit(replaceRow_);
    replace_->setObjectName(QStringLiteral("replaceField"));
    replace_->setPlaceholderText(tr("Replace"));
    replaceLayout->addWidget(replace_, 1);
    replaceButton_ = new QPushButton(tr("Replace"), replaceRow_);
    replaceButton_->setObjectName(QStringLiteral("replaceButton"));
    replaceAllButton_ = new QPushButton(tr("Replace All"), replaceRow_);
    replaceAllButton_->setObjectName(QStringLiteral("replaceAllButton"));
    replaceLayout->addWidget(replaceButton_);
    replaceLayout->addWidget(replaceAllButton_);
    layout->addWidget(replaceRow_);
    setReplaceRowVisible(false);
    hide();
    connect(expand_, &QToolButton::toggled, this, [this](bool on) {
        setReplaceRowVisible(on);
        (on ? replace_ : find_)->setFocus();
    });

    find_->installEventFilter(this);
    replace_->installEventFilter(this);
    edit_->installEventFilter(this); // read-only changes

    connect(find_, &QLineEdit::textChanged, this, [this] {
        refresh();
        selectFromStart();
    });
    for (QToolButton* button : {matchCase_, wholeWord_, regex_})
        connect(button, &QToolButton::toggled, this, [this] {
            refresh();
            selectFromStart();
        });
    connect(next_, &QToolButton::clicked, this, &FindBar::findNext);
    connect(previous_, &QToolButton::clicked, this, &FindBar::findPrevious);
    connect(close, &QToolButton::clicked, this, &FindBar::dismiss);
    connect(replaceButton_, &QPushButton::clicked, this, &FindBar::replaceCurrent);
    connect(replaceAllButton_, &QPushButton::clicked, this, &FindBar::replaceAll);
    // Changes of the text and the selection come while the document is mid-change (setPlainText
    // clears it first): searching waits for the event loop.
    pending_ = new QTimer(this);
    pending_->setSingleShot(true);
    pending_->setInterval(0);
    connect(pending_, &QTimer::timeout, this, [this] {
        if (!isVisible())
            return;
        if (textChanged_)
            refresh();
        else {
            updateMarks();
            updateCount();
        }
        textChanged_ = false;
    });
    connect(edit_->document(), &QTextDocument::contentsChanged, this, [this] {
        // Also while the bar is not shown (its tab or page is not the current one): the old
        // match positions are stale either way and are searched again before they are used.
        if (!busy_) {
            textChanged_ = true;
            if (isVisible())
                pending_->start();
        }
    });
    connect(edit_, &QPlainTextEdit::selectionChanged, this, [this] {
        if (isVisible() && !busy_)
            pending_->start();
    });

    // F3 / Shift+F3 work anywhere in the editor's widget (the editor and this bar); a hidden bar
    // opens first.
    QWidget* host = edit_->parentWidget() ? edit_->parentWidget() : edit_;
    auto* nextKey = new QShortcut(QKeySequence(Qt::Key_F3), host, nullptr, nullptr,
                                  Qt::WidgetWithChildrenShortcut);
    auto* previousKey = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), host, nullptr, nullptr,
                                      Qt::WidgetWithChildrenShortcut);
    connect(nextKey, &QShortcut::activated, this, [this] {
        if (!isVisible())
            open(false);
        else
            findNext();
    });
    connect(previousKey, &QShortcut::activated, this, [this] {
        if (!isVisible())
            open(false);
        else
            findPrevious();
    });
    updateEnabled();
}

bool FindBar::replaceMode() const {
    return !replaceRow_->isHidden();
}

QString FindBar::countText() const {
    return count_->text();
}

findmatches::Options FindBar::options() const {
    return {matchCase_->isChecked(), wholeWord_->isChecked(), regex_->isChecked()};
}

void FindBar::open(bool replaceMode) {
    const QTextCursor cursor = edit_->textCursor();
    const QString selected = cursor.selectedText();
    // One line (a selection over lines holds U+2029), not huge.
    const bool useSelection = !selected.isEmpty() && selected.size() <= 200 &&
                              !selected.contains(QChar::ParagraphSeparator) &&
                              !selected.contains(QChar::LineSeparator);
    // Ctrl+F on an open replace bar keeps the replace row.
    setReplaceRowVisible(replaceMode || !replaceRow_->isHidden());
    show();
    updateEnabled();
    if (useSelection && find_->text() != selected)
        find_->setText(selected); // refreshes (and selects the first match from the selection)
    else
        refresh();
    find_->setFocus();
    find_->selectAll();
}

void FindBar::setReplaceRowVisible(bool visible) {
    replaceRow_->setVisible(visible);
    const QSignalBlocker blocker(expand_);
    expand_->setChecked(visible);
    expand_->setText(visible ? QStringLiteral("▾") : QStringLiteral("▸"));
}

void FindBar::dismiss() {
    hide();
    edit_->setSearchMarks({});
    edit_->setFocus();
}

void FindBar::refresh() {
    replacedNote_.clear();
    // The raw text keeps non-breaking spaces (toPlainText turns them into plain ones); its block
    // separators are U+2029, one unit like a newline, so positions are the document's.
    QString text = edit_->document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    result_ = findmatches::findAll(text, find_->text(), options());
    find_->setStyleSheet(result_.error.isEmpty() ? QString() : QStringLiteral("color: #b00020;"));
    find_->setToolTip(result_.error);
    updateMarks();
    updateCount();
}

int FindBar::currentIndex() const {
    const QTextCursor cursor = edit_->textCursor();
    if (!cursor.hasSelection())
        return -1;
    for (int i = 0; i < result_.matches.size(); ++i)
        if (result_.matches[i].start == cursor.selectionStart() &&
            result_.matches[i].end() == cursor.selectionEnd())
            return static_cast<int>(i);
    return -1;
}

void FindBar::updateMarks() {
    QList<QTextEdit::ExtraSelection> marks;
    QTextCharFormat all;
    all.setBackground(QColor(255, 200, 0, 90));
    QTextCharFormat current;
    current.setBackground(QColor(255, 140, 0, 170));
    const int currentAt = currentIndex();
    const QList<findmatches::Match>& matches = result_.matches;
    const int shown = static_cast<int>(std::min<qsizetype>(matches.size(), maxMarks));
    // The document may be mid-change (setPlainText clears it first): marks of the old text wait.
    const int length = edit_->document()->characterCount() - 1;
    for (int i = 0; i < shown; ++i) {
        if (matches[i].end() > length)
            break;
        QTextCursor cursor(edit_->document());
        cursor.setPosition(matches[i].start);
        cursor.setPosition(matches[i].end(), QTextCursor::KeepAnchor);
        marks.append({cursor, i == currentAt ? current : all});
    }
    edit_->setSearchMarks(marks);
}

void FindBar::updateCount() {
    const int total = static_cast<int>(result_.matches.size());
    if (!replacedNote_.isEmpty()) {
        count_->setText(replacedNote_);
    } else if (!result_.error.isEmpty()) {
        count_->setText(tr("Invalid expression"));
    } else if (find_->text().isEmpty()) {
        count_->clear();
    } else if (total == 0) {
        count_->setText(tr("No results"));
    } else {
        const int at = currentIndex();
        count_->setText(at >= 0 ? tr("%1 of %n match(es)", nullptr, total).arg(at + 1)
                                : tr("%n match(es)", nullptr, total));
    }
    previous_->setEnabled(total > 0);
    next_->setEnabled(total > 0);
}

void FindBar::updateEnabled() {
    const bool writable = !edit_->isReadOnly();
    replace_->setEnabled(writable);
    replaceButton_->setEnabled(writable);
    replaceAllButton_->setEnabled(writable);
}

void FindBar::select(const findmatches::Match& match) {
    QTextCursor cursor(edit_->document());
    cursor.setPosition(match.start);
    cursor.setPosition(match.end(), QTextCursor::KeepAnchor);
    edit_->setTextCursor(cursor);
    edit_->ensureCursorVisible();
    updateMarks();
    updateCount();
}

void FindBar::selectFromStart() {
    if (result_.matches.isEmpty())
        return;
    const int from = edit_->textCursor().selectionStart();
    const auto it = std::ranges::find_if(
        result_.matches, [from](const findmatches::Match& m) { return m.start >= from; });
    select(it != result_.matches.end() ? *it : result_.matches.first());
}

void FindBar::refreshIfStale() {
    if (textChanged_) {
        refresh();
        textChanged_ = false;
    }
}

void FindBar::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refreshIfStale(); // the text may have changed while the bar was not shown
}

void FindBar::findNext() {
    refreshIfStale();
    if (result_.matches.isEmpty())
        return;
    const int from = edit_->textCursor().selectionEnd();
    const auto it = std::ranges::find_if(
        result_.matches, [from](const findmatches::Match& m) { return m.start >= from; });
    select(it != result_.matches.end() ? *it : result_.matches.first()); // wraps
}

void FindBar::findPrevious() {
    refreshIfStale();
    if (result_.matches.isEmpty())
        return;
    const int from = edit_->textCursor().selectionStart();
    for (qsizetype i = result_.matches.size(); i-- > 0;) {
        if (result_.matches[i].start < from) {
            select(result_.matches[i]);
            return;
        }
    }
    select(result_.matches.last()); // wraps
}

void FindBar::replaceCurrent() {
    refreshIfStale();
    if (edit_->isReadOnly())
        return;
    const int at = currentIndex();
    if (at < 0) {
        findNext();
        return;
    }
    const QString text =
        findmatches::replacementFor(result_.matches[at], replace_->text(), options());
    QTextCursor cursor = edit_->textCursor();
    busy_ = true;
    cursor.insertText(text); // the selection is the match: one normal edit, like typing
    edit_->setTextCursor(cursor);
    busy_ = false;
    refresh();
    findNext();
}

void FindBar::replaceAll() {
    refreshIfStale();
    if (edit_->isReadOnly() || result_.matches.isEmpty())
        return;
    const findmatches::Options opts = options();
    const QString replacement = replace_->text();
    const QList<findmatches::Match> matches = result_.matches;
    busy_ = true;
    QTextCursor cursor(edit_->document());
    cursor.beginEditBlock();
    for (auto it = matches.crbegin(); it != matches.crend();
         ++it) { // from the end: positions stay valid
        cursor.setPosition(it->start);
        cursor.setPosition(it->end(), QTextCursor::KeepAnchor);
        cursor.insertText(findmatches::replacementFor(*it, replacement, opts));
    }
    cursor.endEditBlock();
    busy_ = false;
    refresh();
    replacedNote_ = tr("%n replaced", nullptr, static_cast<int>(matches.size()));
    updateCount();
}

bool FindBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == edit_) {
        if (event->type() == QEvent::ReadOnlyChange)
            updateEnabled();
        return false;
    }
    if (event->type() == QEvent::ShortcutOverride) {
        // Escape closes the bar before any shortcut of a parent (e.g. the score tab's) takes it.
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
        return false;
    }
    if (event->type() != QEvent::KeyPress)
        return false;
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
        dismiss();
        return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        if (watched == replace_ && key->modifiers() == Qt::NoModifier)
            replaceCurrent();
        else if (key->modifiers() & Qt::ShiftModifier)
            findPrevious();
        else
            findNext();
        return true;
    }
    return false;
}
