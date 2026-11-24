#include "SongFilterProxy.hpp"

#include "SongTableModel.hpp"

#include <QtGlobal>

// Qt 6.10 has begin/endFilterChange (the filter's state changes between them); older Qt
// invalidates the filter after the change.
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#define ZUB_BEGIN_FILTER_CHANGE() beginFilterChange()
#define ZUB_END_FILTER_CHANGE() endFilterChange(QSortFilterProxyModel::Direction::Rows)
#else
#define ZUB_BEGIN_FILTER_CHANGE() static_cast<void>(0)
#define ZUB_END_FILTER_CHANGE() invalidateRowsFilter()
#endif

SongFilterProxy::SongFilterProxy(QObject* parent) : QSortFilterProxyModel(parent) {
    setSortLocaleAware(true);
    setSortRole(SongTableModel::SortRole);
    setSortCaseSensitivity(Qt::CaseInsensitive);
}

void SongFilterProxy::setSearchText(const QString& text) {
    ZUB_BEGIN_FILTER_CHANGE();
    searchWords_ = SongTableModel::foldForSearch(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setLanguage(const QString& language) {
    ZUB_BEGIN_FILTER_CHANGE();
    language_ = language;
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setTodoOnly(bool todoOnly) {
    ZUB_BEGIN_FILTER_CHANGE();
    todoOnly_ = todoOnly;
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setChangesOnly(bool changesOnly) {
    ZUB_BEGIN_FILTER_CHANGE();
    changesOnly_ = changesOnly;
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setFavouritesOnly(bool favouritesOnly) {
    ZUB_BEGIN_FILTER_CHANGE();
    favouritesOnly_ = favouritesOnly;
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setTabsOnly(bool tabsOnly) {
    ZUB_BEGIN_FILTER_CHANGE();
    tabsOnly_ = tabsOnly;
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setRecentOnly(bool recentOnly) {
    ZUB_BEGIN_FILTER_CHANGE();
    recentOnly_ = recentOnly;
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setLabel(const QString& label) {
    ZUB_BEGIN_FILTER_CHANGE();
    label_ = label;
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setCollection(const QString& collection) {
    ZUB_BEGIN_FILTER_CHANGE();
    collection_ = collection;
    ZUB_END_FILTER_CHANGE();
}

void SongFilterProxy::setOnlyNames(std::optional<std::set<std::string>> names) {
    ZUB_BEGIN_FILTER_CHANGE();
    onlyNames_ = std::move(names);
    ZUB_END_FILTER_CHANGE();
}

bool SongFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
    if (!language_.isEmpty() &&
        !index.data(SongTableModel::LanguageRole).toStringList().contains(language_))
        return false;
    if (todoOnly_ && !index.data(SongTableModel::HasTodoRole).toBool())
        return false;
    if (changesOnly_ && !index.data(SongTableModel::HasChangesRole).toBool())
        return false;
    if (favouritesOnly_ && !index.data(SongTableModel::FavouriteRole).toBool())
        return false;
    if (tabsOnly_ && !index.data(SongTableModel::HasTabsRole).toBool())
        return false;
    // Only while dates are known: without them (no git) the filter would hide every song,
    // behind a choice that is greyed out and cannot be undone.
    const auto* songs = qobject_cast<const SongTableModel*>(sourceModel());
    if (recentOnly_ && songs && songs->hasDates() &&
        !index.data(SongTableModel::RecentRole).toBool())
        return false;
    if (!collection_.isEmpty() &&
        index.data(SongTableModel::CollectionRole).toString() != collection_)
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
