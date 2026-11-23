#include "BookOverview.hpp"

#include "convert.hpp"

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
constexpr int nameRole = Qt::UserRole;     // the song's input name
constexpr int pathRole = Qt::UserRole + 1; // its file ("" not in the library)
} // namespace

BookOverview::BookOverview(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    heading_ = new QLabel(this);
    heading_->setObjectName(QStringLiteral("overviewHeading"));
    QFont font = heading_->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.3);
    heading_->setFont(font);
    heading_->setWordWrap(true);
    layout->addWidget(heading_);
    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("overviewTree"));
    tree_->setHeaderHidden(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(tree_, 1);
    auto* buttons = new QHBoxLayout;
    open_ = new QPushButton(tr("Open Song"), this);
    remove_ = new QPushButton(tr("Remove from the Songbook"), this);
    refresh_ = new QPushButton(tr("Refresh", "check the songbook again"), this);
    refresh_->setToolTip(tr("Check the songbook again (songs edited outside the app)"));
    buttons->addWidget(open_);
    buttons->addWidget(remove_);
    buttons->addStretch(1);
    buttons->addWidget(refresh_);
    layout->addLayout(buttons);

    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &BookOverview::updateButtons);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const QString path = item->data(0, pathRole).toString();
        if (item->parent() && !path.isEmpty())
            emit openSongRequested(toPath(path));
    });
    connect(open_, &QPushButton::clicked, this, [this] {
        const auto items = tree_->selectedItems();
        if (items.size() == 1 && !items.front()->data(0, pathRole).toString().isEmpty())
            emit openSongRequested(toPath(items.front()->data(0, pathRole).toString()));
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        const auto names = selectedNames();
        if (!names.empty())
            emit removeRequested(names);
    });
    connect(refresh_, &QPushButton::clicked, this, &BookOverview::refreshRequested);
    updateButtons();
}

void BookOverview::setChecks(const QString& bookId, const QString& heading, bool ready,
                             const std::vector<Check>& checks) {
    // Rows the user collapsed stay collapsed on a refresh of the same book (an empty row is no
    // choice of the user's); another book starts open.
    if (bookId != bookId_) {
        collapsed_.clear();
        bookId_ = bookId;
    } else {
        for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
            const QTreeWidgetItem* row = tree_->topLevelItem(i);
            const QString title = row->data(0, nameRole).toString();
            std::erase(collapsed_, title);
            if (row->childCount() > 0 && !row->isExpanded())
                collapsed_.push_back(title);
        }
    }
    const std::vector<QString> collapsed = collapsed_;
    heading_->setText(heading);
    heading_->setStyleSheet(ready ? QStringLiteral("color: #2e7d32;") : QString());
    tree_->clear();
    for (const Check& check : checks) {
        const QString mark = check.songs.empty()                   ? QStringLiteral("✓")
                             : check.severity == Severity::Blocker ? QStringLiteral("✗")
                             : check.severity == Severity::ToFix   ? QStringLiteral("⚠")
                                                                   : QStringLiteral("•");
        auto* row = new QTreeWidgetItem(
            tree_, {QStringLiteral("%1  %2 (%3)").arg(mark, check.title).arg(check.songs.size())});
        row->setData(0, nameRole, check.title);
        row->setFlags(Qt::ItemIsEnabled);
        if (!check.songs.empty()) {
            QFont bold = row->font(0);
            bold.setBold(check.severity != Severity::Note);
            row->setFont(0, bold);
            const QColor color = check.severity == Severity::Blocker ? QColor(0xc6, 0x28, 0x28)
                                 : check.severity == Severity::ToFix
                                     ? QColor(0xb3, 0x6b, 0x00)
                                     : palette().color(QPalette::Text);
            row->setForeground(0, color);
        } else {
            row->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
        }
        for (const Song& song : check.songs) {
            auto* item = new QTreeWidgetItem(row, {song.text});
            item->setData(0, nameRole, toQString(song.inputName));
            item->setData(0, pathRole, toQString(song.path));
            item->setToolTip(0, toQString(song.inputName));
        }
        row->setExpanded(!check.songs.empty() && std::find(collapsed.begin(), collapsed.end(),
                                                           check.title) == collapsed.end());
    }
    updateButtons();
}

void BookOverview::setRemoveEnabled(bool enabled) {
    removeEnabled_ = enabled;
    updateButtons();
}

std::vector<std::string> BookOverview::selectedNames() const {
    std::vector<std::string> names;
    for (const QTreeWidgetItem* item : tree_->selectedItems()) {
        if (!item->parent())
            continue;
        const std::string name = toStdString(item->data(0, nameRole).toString());
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
            names.push_back(name);
    }
    return names;
}

void BookOverview::updateButtons() {
    const auto items = tree_->selectedItems();
    open_->setEnabled(items.size() == 1 && items.front()->parent() &&
                      !items.front()->data(0, pathRole).toString().isEmpty());
    remove_->setEnabled(removeEnabled_ && !selectedNames().empty());
}
