#pragma once

#include <QObject>
#include <QString>
#include <vector>
#include <zub/core/songbook.hpp>

// The songbooks the app manages: one <id>.json per songbook in the app data folder.
class SongbookStore : public QObject {
    Q_OBJECT

public:
    struct Entry {
        QString id;
        zub::songbook::Songbook book;
        QString error; // not empty: the file could not be read; `book` is meaningless
    };

    // `directory` empty: AppDataLocation/songbooks.
    explicit SongbookStore(const QString& directory = {}, QObject* parent = nullptr);

    QString directory() const { return directory_; }

    // Sorted by title (locale-aware). A file that fails to parse is listed with its error.
    std::vector<Entry> list() const;
    // Writes <id>.json atomically. On failure returns false and sets *error.
    bool save(const QString& id, const zub::songbook::Songbook& book, QString* error = nullptr);
    // Saves under a new unique id made from the title; "" (and *error) on failure.
    QString create(const zub::songbook::Songbook& book, QString* error = nullptr);
    bool remove(const QString& id);

    // Lower-case ASCII letters, digits and '-' (diacritics folded); "songbook" if nothing is left.
    static QString slug(const QString& title);

private:
    QString pathOf(const QString& id) const;
    QString directory_;
};
