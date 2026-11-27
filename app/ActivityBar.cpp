#include "ActivityBar.hpp"

#include "KeyTips.hpp"

#include <QButtonGroup>
#include <QIcon>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// A theme icon, else one of the style's own (not every desktop theme has them all).
QIcon icon(const QWidget* widget, const char* name, QStyle::StandardPixmap fallback) {
    return QIcon::fromTheme(QString::fromLatin1(name), widget->style()->standardIcon(fallback));
}

} // namespace

ActivityBar::ActivityBar(QWidget* parent) : QWidget(parent), group_(new QButtonGroup(this)) {
    setObjectName(QStringLiteral("activityBar"));
    setAutoFillBackground(true);
    // A shade apart from the views, like a code editor's bar.
    QPalette shade = palette();
    shade.setColor(QPalette::Window, shade.color(QPalette::Window).darker(108));
    setPalette(shade);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 6, 3, 6);
    layout->setSpacing(4);
    group_->setExclusive(true);

    addButton(View::Home, icon(this, "go-home", QStyle::SP_DirHomeIcon), keytips::withKey(tr("Home"), QKeySequence(QStringLiteral("Alt+Home"))));
    addButton(View::Songs, icon(this, "view-list-details", QStyle::SP_FileDialogListView),
              keytips::withKey(tr("Songs"), QKeySequence(QStringLiteral("Ctrl+1"))));
    addButton(View::Song, icon(this, "document-edit", QStyle::SP_FileIcon),
              keytips::withKey(tr("The open song"), QKeySequence(QStringLiteral("Ctrl+2"))));
    addButton(View::Songbooks, icon(this, "view-media-playlist", QStyle::SP_DirIcon),
              keytips::withKey(tr("Songbooks"), QKeySequence(QStringLiteral("Ctrl+3"))));
    layout->addStretch(1);

    settings_ = new QToolButton(this);
    settings_->setObjectName(QStringLiteral("activitySettings"));
    settings_->setIcon(icon(this, "configure", QStyle::SP_FileDialogDetailedView));
    settings_->setIconSize(QSize(24, 24));
    settings_->setAutoRaise(true);
    settings_->setToolTip(keytips::withKey(tr("Settings"), QKeySequence(QStringLiteral("Ctrl+,"))));
    connect(settings_, &QToolButton::clicked, this, &ActivityBar::settingsRequested);
    layout->addWidget(settings_);

    connect(group_, &QButtonGroup::idClicked, this,
            [this](int id) { emit viewRequested(static_cast<View>(id)); });
    setCurrent(View::Songs);
}

QToolButton* ActivityBar::addButton(View view, const QIcon& buttonIcon, const QString& toolTip) {
    auto* button = new QToolButton(this);
    button->setIcon(buttonIcon);
    button->setIconSize(QSize(24, 24));
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setToolTip(toolTip);
    group_->addButton(button, static_cast<int>(view));
    static_cast<QVBoxLayout*>(layout())->addWidget(button);
    return button;
}

void ActivityBar::setCurrent(View view) {
    if (QAbstractButton* button = group_->button(static_cast<int>(view)))
        button->setChecked(true);
}

void ActivityBar::setSongEnabled(bool open) {
    group_->button(static_cast<int>(View::Song))->setEnabled(open);
}

QToolButton* ActivityBar::button(View view) const {
    return static_cast<QToolButton*>(group_->button(static_cast<int>(view)));
}
