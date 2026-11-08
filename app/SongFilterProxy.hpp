#pragma once

#include <QSortFilterProxyModel>
#include <QString>

// Filters the song table by a search text (case- and accent-insensitive, across all
// columns), by language and to songs with TODO comments. Sorting is locale-aware.
class SongFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit SongFilterProxy(QObject* parent = nullptr);

    void setSearchText(const QString& text);
    void setLanguage(const QString& language); // empty = all languages
    void setTodoOnly(bool todoOnly);           // only songs with TODO comments

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QStringList searchWords_; // folded; every word must match
    QString language_;
    bool todoOnly_ = false;
};
