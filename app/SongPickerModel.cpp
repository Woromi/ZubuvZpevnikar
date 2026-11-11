#include "SongPickerModel.hpp"

#include "SongTableModel.hpp"

#include <QApplication>
#include <QBrush>
#include <QMimeData>
#include <QPalette>
#include <algorithm>
#include <utility>
#include <vector>

SongPickerModel::SongPickerModel(QObject* parent) : QIdentityProxyModel(parent) {}

void SongPickerModel::setCurrent(std::set<std::string> inSection) {
    current_ = std::move(inSection);
    refresh();
}

void SongPickerModel::setOtherSections(std::map<std::string, QString> namesOfOtherSections) {
    others_ = std::move(namesOfOtherSections);
    refresh();
}

void SongPickerModel::refresh() {
    const int rows = rowCount();
    if (rows > 0)
        emit dataChanged(index(0, 0), index(rows - 1, columnCount() - 1));
}

QString SongPickerModel::inputNameOf(const QModelIndex& index) const {
    return QIdentityProxyModel::data(index, SongTableModel::InputNameRole).toString();
}

QVariant SongPickerModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid())
        return {};
    if (role == Qt::CheckStateRole) {
        if (index.column() != SongTableModel::Title)
            return {};
        return current_.contains(inputNameOf(index).toStdString()) ? Qt::Checked : Qt::Unchecked;
    }
    if (role == Qt::ForegroundRole || role == Qt::ToolTipRole) {
        const std::string name = inputNameOf(index).toStdString();
        if (!current_.contains(name)) {
            const auto it = others_.find(name);
            if (it != others_.end()) {
                if (role == Qt::ForegroundRole)
                    return QBrush(QApplication::palette().color(QPalette::PlaceholderText));
                return tr("Also in section: %1").arg(it->second);
            }
        }
    }
    return QIdentityProxyModel::data(index, role);
}

bool SongPickerModel::setData(const QModelIndex&, const QVariant&, int) {
    return false;
}

Qt::ItemFlags SongPickerModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    if (index.column() == SongTableModel::Title)
        flags |= Qt::ItemIsUserCheckable;
    return flags;
}

QStringList SongPickerModel::mimeTypes() const {
    return {songsMimeType()};
}

QMimeData* SongPickerModel::mimeData(const QModelIndexList& indexes) const {
    std::vector<std::pair<int, QString>> rows;
    for (const QModelIndex& index : indexes)
        if (index.isValid())
            rows.emplace_back(index.row(), inputNameOf(index));
    std::ranges::sort(rows);
    rows.erase(std::ranges::unique(rows).begin(), rows.end());
    QStringList names;
    for (const auto& row : rows)
        names << row.second;
    auto* mime = new QMimeData;
    mime->setData(songsMimeType(), names.join(QLatin1Char('\n')).toUtf8());
    return mime;
}
