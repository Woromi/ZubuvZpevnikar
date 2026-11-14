#include "ScoreInputBar.hpp"

#include <QAction>
#include <QButtonGroup>
#include <QHBoxLayout>
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
    regionRow_ = new QHBoxLayout;
    regionRow_->setContentsMargins(0, 0, 0, 0);
    outer->addLayout(regionRow_);
    layout->addWidget(new QLabel(tr("Length:")));
    for (const int length : {1, 2, 4, 8, 16}) {
        auto* button = new QToolButton;
        button->setObjectName(QStringLiteral("scoreLength%1").arg(length));
        button->setText(QString::number(length));
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolTip(tr("Note length 1/%1").arg(length));
        group_->addButton(button, length);
        layout->addWidget(button);
    }
    dot_->setObjectName(QStringLiteral("scoreDot"));
    dot_->setText(tr("Dot"));
    dot_->setCheckable(true);
    dot_->setAutoRaise(true);
    dot_->setToolTip(tr("Dotted note: one and a half times as long"));
    layout->addWidget(dot_);

    auto* rest = new QToolButton;
    rest->setObjectName(QStringLiteral("scoreRest"));
    rest->setText(tr("Rest"));
    rest->setToolTip(tr("Insert a rest of the chosen length"));
    auto* bar = new QToolButton;
    bar->setObjectName(QStringLiteral("scoreBarCheck"));
    bar->setText(tr("Bar line"));
    bar->setToolTip(tr("Insert a bar check (|)"));
    insert_ = new QToolButton;
    insert_->setObjectName(QStringLiteral("scoreInsert"));
    insert_->setText(tr("Insert"));
    insert_->setToolTip(tr("Insert the staged fretboard notes at the cursor, as a note or a chord "
                           "(Ctrl+Return). Double-clicking a fret stages it and inserts at once."));
    insert_->setEnabled(false);
    clear_ = new QToolButton;
    clear_->setObjectName(QStringLiteral("scoreClear"));
    clear_->setText(tr("Clear"));
    clear_->setToolTip(tr("Remove the staged fretboard notes (Escape)"));
    clear_->setEnabled(false);
    layout->addSpacing(8);
    layout->addWidget(insert_);
    layout->addWidget(clear_);
    layout->addSpacing(8);
    layout->addWidget(rest);
    layout->addWidget(bar);
    layout->addStretch();

    connect(group_, &QButtonGroup::idClicked, this, [this] { onPicked(); });
    connect(dot_, &QToolButton::clicked, this, [this] { onPicked(); });
    connect(insert_, &QToolButton::clicked, this, &ScoreInputBar::insertRequested);
    connect(clear_, &QToolButton::clicked, this, &ScoreInputBar::clearRequested);
    connect(rest, &QToolButton::clicked, this, &ScoreInputBar::restRequested);
    connect(bar, &QToolButton::clicked, this, &ScoreInputBar::barCheckRequested);
    showDuration();
}

void ScoreInputBar::setStagedCount(int count) {
    insert_->setEnabled(count > 0);
    clear_->setEnabled(count > 0);
}

void ScoreInputBar::setReplaceMode(bool replace) {
    if (replace) {
        insert_->setText(tr("Replace"));
        insert_->setToolTip(tr("Replace the selected note or chord by the staged fretboard notes; "
                               "its length and articulations stay (Ctrl+Return). Double-clicking "
                               "a fret stages it and replaces at once."));
    } else {
        insert_->setText(tr("Insert"));
        insert_->setToolTip(
            tr("Insert the staged fretboard notes at the cursor, as a note or a chord "
               "(Ctrl+Return). Double-clicking a fret stages it and inserts at once."));
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

void ScoreInputBar::setRegionActions(const QList<QAction*>& actions) {
    while (QLayoutItem* item = regionRow_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    if (actions.isEmpty())
        return;
    regionRow_->addWidget(new QLabel(tr("Selection:")));
    for (QAction* action : actions) {
        auto* button = new QToolButton;
        button->setObjectName(QStringLiteral("scoreRegion") + action->objectName());
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setAutoRaise(true);
        regionRow_->addWidget(button);
    }
    regionRow_->addStretch();
}
