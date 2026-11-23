#include "BookAdder.hpp"

#include "SongTableModel.hpp"
#include "SongbookSorter.hpp"
#include "convert.hpp"

#include <QCoreApplication>
#include <QTimer>
#include <algorithm>
#include <map>
#include <zub/core/bookfile.hpp>
#include <zub/core/library.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/textfile.hpp>


BookAdder::BookAdder(SongbookSorter* sorter, const SongTableModel* model, QObject* parent)
    : QObject(parent), sorter_(sorter), model_(model) {
    connect(sorter_, &SongbookSorter::sorted, this,
            [this](const QString& key, const std::vector<std::string>& names) {
                if (current_ && key == current_->key)
                    place(names, {});
            });
    connect(sorter_, &SongbookSorter::failed, this,
            [this](const QString& key, const QString& message) {
                if (current_ && key == current_->key)
                    place({}, message);
            });
}

void BookAdder::add(const std::string& inputName, const std::vector<Target>& targets) {
    for (const Target& target : targets)
        queue_.push_back({inputName, target, {}, {}});
    if (!current_)
        QTimer::singleShot(0, this, &BookAdder::next);
}

void BookAdder::next() {
    while (!current_ && !queue_.empty()) {
        Job job = std::move(queue_.front());
        queue_.pop_front();
        const QString label = job.target.label;
        auto text = zub::readTextFile(job.target.file);
        if (!text) {
            notes_ << label + QStringLiteral(": ") + toQString(text.error());
            continue;
        }
        const auto book = zub::bookfile::parse(*text);
        if (!book || job.target.environment >= book->environments.size()) {
            notes_ << label + QStringLiteral(": ") +
                          (book ? tr("the section is gone") : toQString(book.error()));
            continue;
        }
        const auto& environment = book->environments[job.target.environment];
        if (std::ranges::count(environment.songs(), job.inputName) > 0)
            continue; // there already
        job.text = std::move(*text);
        job.key = QStringLiteral("add:%1").arg(++serial_);

        // The units (a keep-order block by its first song) and the song, with their titles.
        std::vector<std::string> units = zub::bookfile::leaders(environment);
        units.push_back(job.inputName);
        auto entries = sortEntriesFor(*model_, units);
        current_ = std::move(job);
        sorter_->sort(current_->key, std::move(units), std::move(entries));
    }
    if (!current_) {
        const QStringList added = added_, notes = notes_;
        added_.clear();
        notes_.clear();
        emit finished(added, notes);
    }
}

void BookAdder::place(const std::vector<std::string>& sortedUnits, const QString& note) {
    Job job = std::move(*current_);
    current_.reset();
    const QString label = job.target.label;
    // Written only over what was read: the book may have changed while it was sorted.
    const auto now = zub::readTextFile(job.target.file);
    const auto book = zub::bookfile::parse(job.text);
    if (!now || *now != job.text || !book) {
        notes_ << label + QStringLiteral(": ") + tr("changed meanwhile, not added");
        QTimer::singleShot(0, this, &BookAdder::next);
        return;
    }
    const auto& environment = book->environments[job.target.environment];
    std::vector<std::string> base = zub::bookfile::leaders(environment);
    std::vector<std::string> order;
    if (sortedUnits.empty()) {
        order = base;
        order.push_back(job.inputName); // not sorted: at the end
        notes_ << label + QStringLiteral(": ") +
                      tr("added at the end of the section (%1)").arg(note);
    } else {
        order = zub::songbook::mergeIntoOrder(base, sortedUnits, {job.inputName});
    }
    const auto edited = zub::bookfile::setSongs(job.text, job.target.environment,
                                                zub::bookfile::expandLeaders(environment, order));
    if (!edited) {
        notes_ << label + QStringLiteral(": ") + toQString(edited.error());
    } else if (auto written = zub::writeTextFileAtomic(job.target.file, *edited); !written) {
        notes_ << label + QStringLiteral(": ") + toQString(written.error());
    } else {
        added_ << label;
    }
    QTimer::singleShot(0, this, &BookAdder::next);
}

std::vector<zub::songbook::SortEntry> sortEntriesFor(const SongTableModel& model,
                                                     const std::vector<std::string>& names) {
    std::map<std::string, const zub::SongEntry*> byName;
    for (int row = 0; row < model.rowCount(); ++row)
        if (const zub::SongEntry* song = model.song(row))
            byName.emplace(song->inputName, song);
    std::vector<zub::songbook::SortEntry> entries;
    for (const auto& name : names) {
        zub::songbook::SortEntry entry;
        if (const auto it = byName.find(name); it != byName.end()) {
            entry.title = it->second->meta.title;
            entry.lang = zub::mainLanguage(it->second->meta.value("lang")); // the first of a list
        } else {
            const auto slash = name.rfind('/');
            entry.title = slash == std::string::npos ? name : name.substr(slash + 1);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}
