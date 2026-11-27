#include "ScoreInputBar.hpp"

#include "KeyTips.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

ScoreInputBar::ScoreInputBar(QWidget* parent)
    : QWidget(parent), group_(new QButtonGroup(this)), dot_(new QToolButton) {
    group_->setExclusive(true);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 2, 4, 2);
    outer->setSpacing(2);
    auto* layout = new QHBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    outer->addLayout(layout);
    layout->addWidget(new QLabel(tr("Length:")));
    for (const int length : {1, 2, 4, 8, 16}) {
        auto* button = new QToolButton;
        button->setObjectName(QStringLiteral("scoreLength%1").arg(length));
        button->setText(QString::number(length));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(tr("Note length 1/%1 (%2)")
                               .arg(length)
                               .arg(QString::number(length == 16 ? 6 : length)));
        group_->addButton(button, length);
        layout->addWidget(button);
    }
    dot_->setObjectName(QStringLiteral("scoreDot"));
    dot_->setText(tr("Dot"));
    dot_->setCheckable(true);
    dot_->setAutoRaise(true);
    dot_->setToolTip(tr("Dotted note: one and a half times as long (.)"));
    layout->addWidget(dot_);

    auto* rest = new QToolButton;
    rest->setObjectName(QStringLiteral("scoreRest"));
    rest->setText(tr("Rest"));
    rest->setToolTip(tr("Insert a rest of the chosen length (R)"));
    auto* bar = new QToolButton;
    bar->setObjectName(QStringLiteral("scoreBarCheck"));
    bar->setText(tr("Bar line"));
    bar->setToolTip(tr("Insert a bar check (|)"));
    insert_ = new QToolButton;
    insert_->setObjectName(QStringLiteral("scoreInsert"));
    insert_->setText(tr("Insert"));
    insert_->setToolTip(
        tr("Insert the staged fretboard notes at the cursor, as a note or a chord "
           "(%1). Double-clicking a fret stages it and inserts at once.")
            .arg(QKeySequence(Qt::CTRL | Qt::Key_Return).toString(QKeySequence::NativeText)));
    insert_->setEnabled(false);
    clear_ = new QToolButton;
    clear_->setObjectName(QStringLiteral("scoreClear"));
    clear_->setText(tr("Clear"));
    clear_->setToolTip(keytips::withKey(tr("Remove the staged fretboard notes"), QKeySequence(Qt::Key_Escape)));
    clear_->setEnabled(false);
    layout->addSpacing(8);
    layout->addWidget(insert_);
    layout->addWidget(clear_);
    layout->addSpacing(8);
    layout->addWidget(rest);
    layout->addWidget(bar);
    layout->addStretch();
    layout->addWidget(new QLabel(tr("Tuning:")));
    tuning_ = new QComboBox;
    tuning_->setObjectName(QStringLiteral("scoreTuning"));
    tuning_->addItem(tr("Automatic"));
    tuning_->addItem(tr("Guitar"));
    tuning_->addItem(tr("Banjo (open G)"));
    tuning_->setToolTip(tr("Automatic follows the staff at the cursor (its stringTunings); Guitar "
                           "or Banjo force the fretboard's tuning for this score."));
    layout->addWidget(tuning_);

    connect(group_, &QButtonGroup::idClicked, this, [this] { onPicked(); });
    connect(dot_, &QToolButton::clicked, this, [this] { onPicked(); });
    connect(insert_, &QToolButton::clicked, this, &ScoreInputBar::insertRequested);
    connect(clear_, &QToolButton::clicked, this, &ScoreInputBar::clearRequested);
    connect(rest, &QToolButton::clicked, this, &ScoreInputBar::restRequested);
    connect(bar, &QToolButton::clicked, this, &ScoreInputBar::barCheckRequested);
    connect(tuning_, &QComboBox::currentIndexChanged, this, &ScoreInputBar::tuningChoiceChanged);
    showDuration();
}

ScoreInputBar::TuningChoice ScoreInputBar::tuningChoice() const {
    return static_cast<TuningChoice>(tuning_->currentIndex());
}

void ScoreInputBar::setStagedCount(int count) {
    insert_->setEnabled(count > 0);
    clear_->setEnabled(count > 0);
}

void ScoreInputBar::setReplaceMode(bool replace) {
    if (replace) {
        insert_->setText(tr("Replace"));
        insert_->setToolTip(
            tr("Replace the selected note or chord by the staged fretboard notes; "
               "its length and articulations stay (%1). Double-clicking "
               "a fret stages it and replaces at once.")
                .arg(QKeySequence(Qt::CTRL | Qt::Key_Return).toString(QKeySequence::NativeText)));
    } else {
        insert_->setText(tr("Insert"));
        insert_->setToolTip(
            tr("Insert the staged fretboard notes at the cursor, as a note or a chord "
               "(%1). Double-clicking a fret stages it and inserts at once.")
                .arg(QKeySequence(Qt::CTRL | Qt::Key_Return).toString(QKeySequence::NativeText)));
    }
}

void ScoreInputBar::onPicked() {
    if (updating_)
        return;
    if (group_->checkedId() < 0)
        return;
    duration_ = QString::number(group_->checkedId()) +
                (dot_->isChecked() ? QStringLiteral(".") : QString());
}

void ScoreInputBar::setDuration(const QString& duration) {
    duration_ = duration;
    showDuration();
}

void ScoreInputBar::showDuration() {
    updating_ = true;
    const bool dotted = duration_.endsWith(QLatin1Char('.'));
    QString digits = duration_;
    digits.remove(QLatin1Char('.'));
    bool ok = false;
    const int length = digits.toInt(&ok);
    QAbstractButton* button = ok ? group_->button(length) : nullptr;
    group_->setExclusive(false);
    for (QAbstractButton* other : group_->buttons())
        other->setChecked(false);
    group_->setExclusive(true);
    if (button)
        button->setChecked(true);
    dot_->setChecked(dotted);
    updating_ = false;
}

bool ScoreInputBar::isInputKey(const QKeyEvent* event) {
    const auto modifiers = event->modifiers();
    // Ctrl+Alt is AltGr on Windows ("|" is AltGr+W on a Czech keyboard).
    const bool altGr = (modifiers & Qt::ControlModifier) && (modifiers & Qt::AltModifier);
    if ((modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) && !altGr)
        return false;
    static const QString keys = QStringLiteral("12486.rR|");
    return event->text().size() == 1 && keys.contains(event->text());
}

bool ScoreInputBar::handleKey(const QString& text) {
    if (text == QLatin1String(".")) {
        toggleDot();
    } else if (text == QLatin1String("r") || text == QLatin1String("R")) {
        emit restRequested();
    } else if (text == QLatin1String("|")) {
        emit barCheckRequested();
    } else if (text == QLatin1String("6")) {
        pickLength(QStringLiteral("16"));
    } else if (text == QLatin1String("1") || text == QLatin1String("2") ||
               text == QLatin1String("4") || text == QLatin1String("8")) {
        pickLength(text);
    } else {
        return false;
    }
    return true;
}

void ScoreInputBar::pickLength(const QString& length) {
    bool ok = false;
    const int value = length.toInt(&ok);
    if (!ok || !group_->button(value))
        return;
    group_->button(value)->click();
}

void ScoreInputBar::toggleDot() {
    dot_->click();
}
