#include "SongFilterProxy.hpp"

#include "SongTableModel.hpp"

SongFilterProxy::SongFilterProxy(QObject* parent) : QSortFilterProxyModel(parent) {
    setSortLocaleAware(true);
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

bool SongFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!language_.isEmpty() && index.data(SongTableModel::LanguageRole).toString() != language_)
        return false;
    const QString haystack = index.data(SongTableModel::SearchTextRole).toString();
    for (const QString& word : searchWords_) {
        if (!haystack.contains(word))
            return false;
    }
    return true;
}
