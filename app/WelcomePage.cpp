#include "WelcomePage.hpp"

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

WelcomePage::WelcomePage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("welcomePage"));
    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    // A column of a fixed width in the middle: a wrapped label gets the height its text needs.
    auto* box = new QWidget(this);
    box->setFixedWidth(560);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel(QApplication::applicationDisplayName(), box);
    QFont font = title->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.8);
    title->setFont(font);
    title->setAlignment(Qt::AlignCenter);
    message_ = new QLabel(box);
    message_->setObjectName(QStringLiteral("welcomeMessage"));
    message_->setWordWrap(true);
    message_->setAlignment(Qt::AlignCenter);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* buttons = new QHBoxLayout;
    retry_ = new QPushButton(tr("Try Again"), box);
    retry_->setObjectName(QStringLiteral("welcomeRetry"));
    choose_ = new QPushButton(box);
    choose_->setObjectName(QStringLiteral("welcomeChoose"));
    clone_ = new QPushButton(tr("Get the Songbook…"), box);
    clone_->setObjectName(QStringLiteral("welcomeClone"));
    clone_->setToolTip(tr("Download the songbook repository (git clone) into a folder of your "
                          "choice and open it"));
    buttons->addStretch(1);
    buttons->addWidget(retry_);
    buttons->addWidget(choose_);
    buttons->addWidget(clone_);
    buttons->addStretch(1);
    column->addWidget(title);
    column->addSpacing(12);
    column->addWidget(message_);
    column->addSpacing(12);
    column->addLayout(buttons);
    outer->addWidget(box, 0, Qt::AlignHCenter);
    outer->addStretch(2);
    connect(choose_, &QPushButton::clicked, this, &WelcomePage::chooseRequested);
    connect(retry_, &QPushButton::clicked, this, [this] { emit retryRequested(path_); });
    connect(clone_, &QPushButton::clicked, this, &WelcomePage::cloneRequested);
    showFirstStart();
}

void WelcomePage::showFirstStart() {
    path_.clear();
    message_->setText(tr("Open the folder of your songbook: a ZubuvZpevnik checkout, the folder "
                         "with songs/chords/ in it. It is remembered for the next start. Without "
                         "one yet, Get the Songbook… downloads it."));
    choose_->setText(tr("Open Songbook Folder…"));
    retry_->hide();
    choose_->setDefault(true);
}

void WelcomePage::showMissing(const QString& path, const QString& reason) {
    path_ = path;
    message_->setText(tr("The songbook folder cannot be opened:\n%1\n\n%2\n\nIf it is on a disk "
                         "that is not connected, connect it and try again.")
                          .arg(path, reason));
    choose_->setText(tr("Choose Another…"));
    retry_->show();
    retry_->setDefault(true);
}

void WelcomePage::setCloneAvailable(bool available, const QString& why) {
    clone_->setEnabled(available);
    clone_->setToolTip(available ? tr("Download the songbook repository (git clone) into a folder "
                                      "of your choice and open it")
                                 : why);
}
