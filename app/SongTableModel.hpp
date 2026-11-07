#pragma once

#include <QAbstractTableModel>
#include <QString>
#include <vector>
#include <zub/core/library.hpp>

// Table of the songs of a zub::Library: one row per song, display text already converted
// from TeX.
class SongTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { Title, Artist, Language, Key, ColumnCount };
    enum Role {
        LanguageRole = Qt::UserRole, // raw lang value, e.g. "czech"
        SearchTextRole,              // all columns, case- and accent-folded, for filtering
        InputNameRole,               // "Artist/Title"
    };

    explicit SongTableModel(QObject* parent = nullptr);

    void setSongs(std::vector<zub::SongEntry> songs);
    const zub::SongEntry* song(int row) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // Lower-cases, removes diacritics and spells ♭/♯ as b/#, so "uz" matches "Už" and
    // "eb" matches "E♭".
    static QString foldForSearch(const QString& text);

private:
    struct Row {
        zub::SongEntry entry;
        QString title, artist, language, key, searchText, toolTip;
    };
    std::vector<Row> rows_;
};
