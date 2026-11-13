#include "SongFilterProxy.hpp"

#include "SongTableModel.hpp"

SongFilterProxy::SongFilterProxy(QObject* parent) : QSortFilterProxyModel(parent) {
    setSortLocaleAware(true);
    setSortRole(SongTableModel::SortRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void SongFilterProxy::setSearchText(const QString& text) {
    beginFilterChange();
    searchWords_ = SongTableModel::foldForSearch(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void SongFilterProxy::setLanguage(const QString& language) {
    beginFilterChange();
    language_ = language;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void SongFilterProxy::setTodoOnly(bool todoOnly) {
    beginFilterChange();
    todoOnly_ = todoOnly;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void SongFilterProxy::setChangesOnly(bool changesOnly) {
    beginFilterChange();
    changesOnly_ = changesOnly;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void SongFilterProxy::setFavouritesOnly(bool favouritesOnly) {
    beginFilterChange();
    favouritesOnly_ = favouritesOnly;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void SongFilterProxy::setLabel(const QString& label) {
    beginFilterChange();
    label_ = label;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

void SongFilterProxy::setOnlyNames(std::optional<std::set<std::string>> names) {
    beginFilterChange();
    onlyNames_ = std::move(names);
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
}

bool SongFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!language_.isEmpty() && index.data(SongTableModel::LanguageRole).toString() != language_)
        return false;
    if (todoOnly_ && !index.data(SongTableModel::HasTodoRole).toBool())
        return false;
    if (changesOnly_ && !index.data(SongTableModel::HasChangesRole).toBool())
        return false;
    if (favouritesOnly_ && !index.data(SongTableModel::FavouriteRole).toBool())
        return false;
    if (!label_.isEmpty() &&
        !index.data(SongTableModel::LabelsRole).toStringList().contains(label_))
        return false;
    if (onlyNames_ &&
        !onlyNames_->contains(index.data(SongTableModel::InputNameRole).toString().toStdString()))
        return false;
    const QString haystack = index.data(SongTableModel::SearchTextRole).toString();
    for (const QString& word : searchWords_) {
        if (!haystack.contains(word))
            return false;
    }
    return true;
}
