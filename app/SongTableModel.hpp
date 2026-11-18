#pragma once

#include <QAbstractTableModel>
#include <QDateTime>
#include <QString>
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <zub/core/library.hpp>
#include <zub/core/songdates.hpp>

// Table of the songs of a zub::Library: one row per song, display text already converted
// from TeX.
class SongTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        Favourite, // ★ for a favourite, ☆ otherwise: first, like a checkbox
        Title,
        Artist,
        Language,
        Key,
        Tempo,
        Meter,
        Capo,
        Tabs,   // ♪ when the song includes a score (\lilypondfile), ♪ n for several
        Labels, // the tags except "fav"
        Todo,
        Changes,
        Added, // when the song was added (git); last, so no other column moves
        ColumnCount
    };
    enum Role {
        LanguageRole = Qt::UserRole, // QStringList: every language of lang={...}, the main one first
        SearchTextRole,              // all columns, case- and accent-folded, for filtering
        InputNameRole,               // "Artist/Title"
        HasTodoRole,                 // bool: the song has TODO comments
        HasChangesRole,              // bool: a songbook scan found possible changes
        SortRole,                    // what the column sorts by: numbers for Tempo and Capo
        FavouriteRole,               // bool: the song's tags include "fav"
        LabelsRole,                  // QStringList: the tags except "fav"
        HasTabsRole,                 // bool: the song includes a score
        CollectionRole,              // QString: the folder under songs/ ("chords", "tinwhistle")
        AddedRole,  // QDateTime of the commit that added the song; invalid when unknown or new
        RecentRole, // bool: added in the last 30 days, or new and not committed yet
    };

    explicit SongTableModel(QObject* parent = nullptr);

    void setSongs(std::vector<zub::SongEntry> songs);
    const zub::SongEntry* song(int row) const;
    // Replaces the metadata and TODO notes of the song stored at `path` (e.g. after it was
    // edited). Returns false if no song in the model has that path.
    bool updateSong(const std::filesystem::path& path, zub::SongMetadata meta,
                    std::vector<std::string> todos, std::vector<std::string> scores);
    // Row of the song stored at `path`, -1 if absent.
    int rowOf(const std::filesystem::path& path) const;
    // What a songbook scan found in the song at `path` (this session only): a short summary for
    // the Changes column and the details for its tooltip. Empty `summary` clears it. The column
    // sorts by the number of items listed in `details` (its "  • " lines).
    void setScanResult(const std::filesystem::path& path, const QString& summary,
                       const QString& details);
    void clearScanResults();
    // Every label (tag except "fav") of any song, sorted for the current locale.
    QStringList labels() const;
    // Adds a song (e.g. a newly imported one after its first save): it counts as new and not
    // committed.
    void addSong(zub::SongEntry song);
    // The git dates by path relative to `root` ('/'); songs the map lacks stay unknown. Gives
    // hasDates() = true (also after an empty map: git answered) and refilters.
    void setDates(const std::filesystem::path& root, const std::map<std::string, zub::songdates::Dates>& dates);
    // Without dates (no git, not a repository, or until they are read). setSongs does this.
    void clearDates();
    bool hasDates() const { return hasDates_; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    // Lower-cases, removes diacritics and spells ♭/♯ as b/#, so "uz" matches "Už" and
    // "eb" matches "E♭".
    static QString foldForSearch(const QString& text);

signals:
    void datesChanged(); // hasDates() or the dates changed

private:
    struct Row {
        zub::SongEntry entry;
        QString title, artist, key, tempo, meter, capo, todo, todoToolTip, searchText,
            toolTip, labelsText;
        QStringList labels;
        bool favourite = false;
        QString tabs, tabsToolTip; // "♪" / "♪ 2", and the score names
        QString changes, changesToolTip; // from a songbook scan
        QStringList languages;           // every language of lang={...}, the song's own first
        QString languagesText;           // the same, joined for the column
        int changeCount = 0;             // the items listed in changesToolTip
        zub::songdates::Dates dates;
        bool recent = false;
    };
    static Row makeRow(zub::SongEntry entry);
    static void setDatesOf(Row& row, const zub::songdates::Dates& dates);
    std::vector<Row> rows_;
    bool hasDates_ = false;
};
