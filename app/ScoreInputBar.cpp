#include "ScoreInputBar.hpp"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

ScoreInputBar::ScoreInputBar(QWidget* parent)
    : QWidget(parent), group_(new QButtonGroup(this)), dot_(new QToolButton) {
    group_->setExclusive(true);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
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
    layout->addSpacing(8);
    layout->addWidget(rest);
    layout->addWidget(bar);
    layout->addStretch();

    connect(group_, &QButtonGroup::idClicked, this, [this] { onPicked(); });
    connect(dot_, &QToolButton::clicked, this, [this] { onPicked(); });
    connect(rest, &QToolButton::clicked, this, &ScoreInputBar::restRequested);
    connect(bar, &QToolButton::clicked, this, &ScoreInputBar::barCheckRequested);
    showDuration();
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
