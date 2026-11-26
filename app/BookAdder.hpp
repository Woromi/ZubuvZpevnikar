#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <zub/core/songbook.hpp>

class SongbookSorter;
class SongTableModel;

// Adds a song to sections of songbooks (Add to Songbooks…): for each, the section's order with the
// song from the shared SongbookSorter (the book's title index order, zub::songbook::mergeIntoOrder:
// the rest keeps its order), then zub::bookfile::setSongs, written only when the file is still what
// was read. One section at a time. Without a sorted order (no texlua, no Czech locale) the song
// goes at the end of the section.
// What the title index sorts `names` by (the title, the main language; a name without a library
// song by its file name): the Songbooks page and Add to Songbooks sort alike.
std::vector<zub::songbook::SortEntry> sortEntriesFor(const SongTableModel& model,
                                                     const std::vector<std::string>& names);

class BookAdder : public QObject {
    Q_OBJECT

public:
    struct Target {
        std::filesystem::path file; // the book
        QString label;              // for people: the book (and the section)
        std::size_t environment = 0;
    };

    BookAdder(SongbookSorter* sorter, const SongTableModel* model, QObject* parent = nullptr);

    // Queues the song for each target; finished() follows when all are done.
    void add(const std::string& inputName, const std::vector<Target>& targets);
    // Takes the song out of each target now (setSongs without it: a line with a note above it is
    // commented out), written only over what was read. The labels of the lists it left go to
    // `removed`, what went otherwise to `notes`.
    static void remove(const std::string& inputName, const std::vector<Target>& targets,
                       QStringList& removed, QStringList& notes);
    bool busy() const { return current_.has_value() || !queue_.empty(); }

signals:
    // `added`: the labels of the sections the song went into; `notes`: what went otherwise.
    void finished(const QStringList& added, const QStringList& notes);

private:
    struct Job {
        std::string inputName;
        Target target;
        std::string text; // the book as read
        QString key;      // the sorter's
    };
    void next();
    void place(const std::vector<std::string>& sortedUnits, const QString& note);

    SongbookSorter* sorter_;
    const SongTableModel* model_;
    std::deque<Job> queue_;
    std::optional<Job> current_;
    QStringList added_, notes_;
    int serial_ = 0;
};
