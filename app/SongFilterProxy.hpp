#pragma once

#include <QSortFilterProxyModel>
#include <QString>
#include <optional>
#include <set>
#include <string>

// Filters the song table by a search text (case- and accent-insensitive, across all
// columns), by language, label, favourites and to songs with TODO comments. Sorting is
// locale-aware.
class SongFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit SongFilterProxy(QObject* parent = nullptr);

    void setSearchText(const QString& text);
    void setLanguage(const QString& language); // empty = all languages
    void setTodoOnly(bool todoOnly);           // only songs with TODO comments
    void setChangesOnly(bool changesOnly);     // only songs a scan found changes in
    void setFavouritesOnly(bool favouritesOnly);
    void setLabel(const QString& label); // empty = any label
    // Only the songs with these input names ("Artist/Title"); nullopt = no such filter.
    void setOnlyNames(std::optional<std::set<std::string>> names);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QStringList searchWords_; // folded; every word must match
    QString language_;
    bool todoOnly_ = false;
    bool changesOnly_ = false;
    bool favouritesOnly_ = false;
    QString label_;
    std::optional<std::set<std::string>> onlyNames_;
};
