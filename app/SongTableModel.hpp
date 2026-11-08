#pragma once

#include <QAbstractTableModel>
#include <QString>
#include <filesystem>
#include <vector>
#include <zub/core/library.hpp>

// Table of the songs of a zub::Library: one row per song, display text already converted
// from TeX.
class SongTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { Title, Artist, Language, Key, Todo, ColumnCount };
    enum Role {
        LanguageRole = Qt::UserRole, // raw lang value, e.g. "czech"
        SearchTextRole,              // all columns, case- and accent-folded, for filtering
        InputNameRole,               // "Artist/Title"
        HasTodoRole,                 // bool: the song has TODO comments
    };

    explicit SongTableModel(QObject* parent = nullptr);

    void setSongs(std::vector<zub::SongEntry> songs);
    const zub::SongEntry* song(int row) const;
    // Replaces the metadata and TODO notes of the song stored at `path` (e.g. after it was
    // edited). Returns false if no song in the model has that path.
    bool updateSong(const std::filesystem::path& path, zub::SongMetadata meta,
                    std::vector<std::string> todos);
    // Row of the song stored at `path`, -1 if absent.
    int rowOf(const std::filesystem::path& path) const;
    // Adds a song (e.g. a newly imported one after its first save).
    void addSong(zub::SongEntry song);

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
        QString title, artist, language, key, todo, todoToolTip, searchText, toolTip;
    };
    static Row makeRow(zub::SongEntry entry);
    std::vector<Row> rows_;
};
