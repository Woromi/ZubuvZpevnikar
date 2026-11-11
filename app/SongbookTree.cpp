#include "SongbookTree.hpp"

#include "SongPickerModel.hpp"
#include "convert.hpp"

#include <QAction>
#include <QBrush>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFont>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <algorithm>
#include <map>

namespace {
constexpr int uidRole = Qt::UserRole;
constexpr int nameRole = Qt::UserRole + 1;
constexpr int sectionRole = Qt::UserRole + 2; // true on a section's item

const QString treeMimeType = QStringLiteral("application/x-zubuvzpevnik-tree-songs");
} // namespace

SongbookTree::SongbookTree(QWidget* parent) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, &SongbookTree::showMenu);
}

QTreeWidgetItem* SongbookTree::addSection(quint64 uid, const QString& text) {
    auto* item = new QTreeWidgetItem(this);
    item->setText(0, text);
    item->setData(0, uidRole, uid);
    item->setData(0, sectionRole, true);
    item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    QFont font = item->font(0);
    font.setBold(true);
    item->setFont(0, font);
    return item;
}

QTreeWidgetItem* SongbookTree::addSong(QTreeWidgetItem* section, quint64 uid,
                                       const std::string& inputName, const QString& text,
                                       bool missing) {
    auto* item = section ? new QTreeWidgetItem(section) : new QTreeWidgetItem(this);
    item->setText(0, text);
    item->setToolTip(0, toQString(inputName));
    item->setData(0, uidRole, uid);
    item->setData(0, nameRole, toQString(inputName));
    item->setData(0, sectionRole, false);
    if (missing)
        item->setForeground(0, QBrush(Qt::red));
    return item;
}

bool SongbookTree::isSection(const QTreeWidgetItem* item) const {
    return item && item->data(0, sectionRole).toBool();
}

quint64 SongbookTree::uidOf(const QTreeWidgetItem* item) {
    return item ? item->data(0, uidRole).toULongLong() : 0;
}

std::vector<std::pair<quint64, std::string>> SongbookTree::selectedSongs() const {
    std::vector<std::pair<quint64, std::string>> songs;
    const auto visit = [&](QTreeWidgetItem* item) {
        if (!isSection(item) && item->isSelected())
            songs.emplace_back(uidOf(item), toStdString(item->data(0, nameRole).toString()));
    };
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = topLevelItem(i);
        visit(top);
        for (int j = 0; j < top->childCount(); ++j)
            visit(top->child(j));
    }
    return songs;
}

int SongbookTree::rootSongCount() const {
    int count = 0;
    for (int i = 0; i < topLevelItemCount(); ++i)
        if (!isSection(topLevelItem(i)))
            ++count;
    return count;
}

// ---- Drag and drop ----

QStringList SongbookTree::mimeTypes() const {
    return {treeMimeType, SongPickerModel::songsMimeType()};
}

bool SongbookTree::ours(const QMimeData* mime) {
    return mime && (mime->hasFormat(treeMimeType) || mime->hasFormat(SongPickerModel::songsMimeType()));
}

void SongbookTree::startDrag(Qt::DropActions) {
    QStringList lines;
    for (const auto& [uid, name] : selectedSongs())
        lines << QString::number(uid) + QLatin1Char('\t') + toQString(name);
    if (lines.isEmpty())
        return;
    auto* mime = new QMimeData;
    mime->setData(treeMimeType, lines.join(QLatin1Char('\n')).toUtf8());
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction, Qt::MoveAction);
}

SongbookTree::Target SongbookTree::dropTarget(const QPoint& position) const {
    QTreeWidgetItem* item = itemAt(position);
    if (!item)
        return {rootUid_, rootSongCount()};
    if (isSection(item))
        return {uidOf(item), item->childCount()};
    QTreeWidgetItem* parent = item->parent();
    const int row = parent ? parent->indexOfChild(item) : indexOfTopLevelItem(item);
    const QRect rect = visualRect(indexFromItem(item));
    const bool below = position.y() > rect.center().y();
    return {uidOf(item), row + (below ? 1 : 0)};
}

void SongbookTree::dragEnterEvent(QDragEnterEvent* event) {
    if (!ours(event->mimeData())) {
        event->ignore();
        return;
    }
    QTreeWidget::dragEnterEvent(event);
    event->acceptProposedAction();
}

void SongbookTree::dragMoveEvent(QDragMoveEvent* event) {
    if (!ours(event->mimeData())) {
        event->ignore();
        return;
    }
    QTreeWidget::dragMoveEvent(event); // the drop indicator, auto scroll
    event->acceptProposedAction();
}

void SongbookTree::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (!ours(mime)) {
        event->ignore();
        return;
    }
    const Target target = dropTarget(event->position().toPoint());
    // No default handling: the page changes the songbook and rebuilds the tree.
    event->acceptProposedAction();
    stopAutoScroll();
    setState(QAbstractItemView::NoState);
    viewport()->update();

    // Source section uid -> names, in drag order (0: the picker).
    std::vector<std::pair<quint64, std::vector<std::string>>> groups;
    const auto add = [&](quint64 uid, const std::string& name) {
        for (auto& group : groups) {
            if (group.first == uid) {
                group.second.push_back(name);
                return;
            }
        }
        groups.push_back({uid, {name}});
    };
    if (mime->hasFormat(treeMimeType)) {
        for (const QString& line : QString::fromUtf8(mime->data(treeMimeType)).split(QLatin1Char('\n'))) {
            const qsizetype tab = line.indexOf(QLatin1Char('\t'));
            if (tab > 0 && tab + 1 < line.size())
                add(line.left(tab).toULongLong(), toStdString(line.mid(tab + 1)));
        }
    } else {
        for (const QString& line : QString::fromUtf8(mime->data(SongPickerModel::songsMimeType()))
                                       .split(QLatin1Char('\n'), Qt::SkipEmptyParts))
            add(0, toStdString(line));
    }
    // The reorder inside the target section first: the others append to it.
    std::ranges::stable_sort(groups, [&](const auto& a, const auto& b) {
        return (a.first == target.uid) > (b.first == target.uid);
    });
    for (const auto& [from, names] : groups)
        emit songsDropped(names, from, target.uid, target.index);
}

void SongbookTree::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete) {
        emit removeSelectedRequested();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

// ---- Context menu ----

void SongbookTree::showMenu(const QPoint& position) {
    QTreeWidgetItem* item = itemAt(position);
    if (!item)
        return;
    if (!item->isSelected())
        setCurrentItem(item); // also what the page follows
    const quint64 uid = uidOf(item);
    QMenu menu(this);
    if (isSection(item)) {
        connect(menu.addAction(tr("Sort This Section Alphabetically")), &QAction::triggered, this,
                [this, uid] { emit sortSectionRequested(uid); });
        connect(menu.addAction(tr("Rename Section…")), &QAction::triggered, this,
                [this, uid] { emit renameSectionRequested(uid); });
        connect(menu.addAction(tr("Remove Section")), &QAction::triggered, this,
                [this, uid] { emit removeSectionRequested(uid); });
    } else {
        if (uid == rootUid_)
            connect(menu.addAction(tr("Sort the Songs Outside Sections")), &QAction::triggered, this,
                    [this, uid] { emit sortSectionRequested(uid); });
        connect(menu.addAction(tr("Remove from the Songbook")), &QAction::triggered, this,
                [this] { emit removeSelectedRequested(); });
    }
    menu.exec(viewport()->mapToGlobal(position));
}
