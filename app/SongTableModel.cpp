#include "SongTableModel.hpp"

#include <QGuiApplication>
#include <QPalette>

#include "convert.hpp"

#include <QCollator>
#include <QLocale>
#include <algorithm>
#include <chrono>
#include <limits>
#include <set>
#include <zub/core/latex.hpp>
#include <zub/core/songtags.hpp>

SongTableModel::SongTableModel(QObject* parent) : QAbstractTableModel(parent) {}

QString SongTableModel::foldForSearch(const QString& text) {
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString folded;
    folded.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c == QChar(0x266D)) // ♭: lets "Eb" find "E♭"
            folded += QLatin1Char('b');
        else if (c == QChar(0x266F)) // ♯: lets "C#m" find "C♯m"
            folded += QLatin1Char('#');
        else if (c.category() != QChar::Mark_NonSpacing)
            folded += c;
    }
    return folded.toCaseFolded();
}

SongTableModel::Row SongTableModel::makeRow(zub::SongEntry entry) {
    Row row;
    row.title = toQString(zub::latex::toDisplayText(entry.meta.title));
    row.artist = toQString(zub::latex::toDisplayText(entry.meta.value("by")));
    // lang={czech, hebrew}: the first is the song's language; the column and the language filter
    // know them all (a song is found under each of its languages).
    const auto languages = zub::songLanguages(entry.meta.value("lang"));
    for (const auto& language : languages)
        row.languages << toQString(language);
    row.languagesText = row.languages.join(QStringLiteral(", "));
    row.key = toQString(zub::latex::toDisplayText(entry.meta.value("key")));
    row.tempo = toQString(zub::latex::toDisplayText(entry.meta.value("tem")));
    row.meter = toQString(zub::latex::toDisplayText(entry.meta.value("met")));
    row.capo = toQString(zub::latex::toDisplayText(entry.meta.value("cap")));
    for (const std::string& tag : zub::tags::parse(entry.meta.value("tags"))) {
        if (tag == zub::tags::kFavourite)
            row.favourite = true;
        else
            row.labels << toQString(tag);
    }
    row.labelsText = row.labels.join(QStringLiteral(", "));
    if (!entry.scores.empty()) {
        row.tabs = entry.scores.size() == 1
                       ? QStringLiteral("♪")
                       : QStringLiteral("♪ %1").arg(static_cast<int>(entry.scores.size()));
        QStringList names;
        for (const auto& score : entry.scores)
            names << toQString(score);
        row.tabsToolTip = names.join(QLatin1Char('\n'));
    }
    QStringList todos;
    for (const auto& note : entry.todos)
        todos << toQString(note);
    row.todo = todos.join(QStringLiteral("; "));
    row.todoToolTip = todos.join(QLatin1Char('\n'));
    row.searchText = foldForSearch(
        QStringList{row.title, row.artist, row.languagesText, row.key, row.labelsText, row.todo}.join(
            QLatin1Char('\n')));
    row.toolTip = toQString(entry.inputName) + QLatin1Char('\n') + toQString(entry.path);
    row.entry = std::move(entry);
    return row;
}

namespace {
constexpr std::chrono::days kRecentWithin{30};
}

void SongTableModel::setDatesOf(Row& row, const zub::songdates::Dates& dates) {
    row.dates = dates;
    row.recent = zub::songdates::isRecent(
        dates, std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()),
        kRecentWithin);
}

void SongTableModel::setSongs(std::vector<zub::SongEntry> songs) {
    const bool hadDates = hasDates_;
    hasDates_ = false;
    beginResetModel();
    rows_.clear();
    rows_.reserve(songs.size());
    for (auto& entry : songs)
        rows_.push_back(makeRow(std::move(entry)));
    endResetModel();
    if (hadDates)
        emit datesChanged();
}

void SongTableModel::addSong(zub::SongEntry song) {
    const int row = static_cast<int>(rows_.size());
    beginInsertRows({}, row, row);
    rows_.push_back(makeRow(std::move(song)));
    zub::songdates::Dates fresh;
    fresh.uncommitted = true;
    setDatesOf(rows_.back(), fresh);
    endInsertRows();
}

void SongTableModel::setDates(const std::filesystem::path& root,
                              const std::map<std::string, zub::songdates::Dates>& dates) {
    for (Row& row : rows_) {
        const auto it = dates.find(row.entry.path.lexically_relative(root).generic_string());
        setDatesOf(row, it == dates.end() ? zub::songdates::Dates{} : it->second);
    }
    hasDates_ = true;
    if (!rows_.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    emit datesChanged();
}

void SongTableModel::clearDates() {
    const bool had = hasDates_;
    hasDates_ = false;
    for (Row& row : rows_)
        setDatesOf(row, {});
    if (!rows_.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    if (had)
        emit datesChanged();
}

bool SongTableModel::updateSong(const std::filesystem::path& path, zub::SongMetadata meta,
                                std::vector<std::string> todos,
                                std::vector<std::string> scores) {
    const auto it = std::ranges::find(rows_, path, [](const Row& row) { return row.entry.path; });
    if (it == rows_.end())
        return false;
    zub::SongEntry entry = std::move(it->entry);
    entry.meta = std::move(meta);
    entry.todos = std::move(todos);
    entry.scores = std::move(scores);
    const auto dates = it->dates; // kept: they come from git, not from the text
    const bool recent = it->recent;
    *it = makeRow(std::move(entry));
    it->dates = dates;
    it->recent = recent;
    const int row = static_cast<int>(it - rows_.begin());
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    return true;
}

void SongTableModel::setScanResult(const std::filesystem::path& path, const QString& summary,
                                   const QString& details) {
    const int row = rowOf(path);
    if (row < 0)
        return;
    Row& r = rows_[static_cast<std::size_t>(row)];
    r.changes = summary;
    r.changesToolTip = summary.isEmpty() ? QString() : details;
    r.changeCount = 0;
    if (!summary.isEmpty())
        for (const QStringView line : QStringView(details).split(QLatin1Char('\n')))
            if (line.startsWith(QStringLiteral("  \u2022 ")))
                ++r.changeCount;
    emit dataChanged(index(row, Changes), index(row, Changes));
}

void SongTableModel::clearScanResults() {
    for (Row& row : rows_) {
        row.changes.clear();
        row.changesToolTip.clear();
        row.changeCount = 0;
    }
    if (!rows_.empty())
        emit dataChanged(index(0, Changes), index(rowCount() - 1, Changes));
}

QStringList SongTableModel::labels() const {
    std::set<QString> unique;
    for (const Row& row : rows_)
        unique.insert(row.labels.begin(), row.labels.end());
    QStringList sorted(unique.begin(), unique.end());
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::sort(sorted, collator);
    return sorted;
}

int SongTableModel::rowOf(const std::filesystem::path& path) const {
    const auto it = std::ranges::find(rows_, path, [](const Row& row) { return row.entry.path; });
    return it == rows_.end() ? -1 : static_cast<int>(it - rows_.begin());
}

const zub::SongEntry* SongTableModel::song(int row) const {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
        return nullptr;
    return &rows_[static_cast<std::size_t>(row)].entry;
}

int SongTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int SongTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant SongTableModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid))
        return {};
    const Row& row = rows_[static_cast<std::size_t>(index.row())];

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Title:
            return row.title;
        case Favourite: // a hollow star invites the click (shown muted)
            return row.favourite ? QStringLiteral("★") : QStringLiteral("☆");
        case Artist:
            return row.artist;
        case Language:
            return row.languagesText;
        case Key:
            return row.key;
        case Tempo:
            return row.tempo;
        case Meter:
            return row.meter;
        case Capo:
            return row.capo;
        case Tabs:
            return row.tabs;
        case Labels:
            return row.labelsText;
        case Todo:
            return row.todo;
        case Changes:
            return row.changes;
        case Added:
            if (row.dates.added)
                return QLocale().toString(toDateTime(*row.dates.added).date(),
                                          QLocale::ShortFormat);
            return row.dates.uncommitted ? tr("new", "a song not committed to git yet")
                                         : QString();
        default:
            return {};
        }
    case Qt::TextAlignmentRole:
        if (index.column() == Favourite || index.column() == Tabs)
            return Qt::AlignCenter;
        return {};
    case Qt::ForegroundRole:
        if (index.column() == Favourite && !row.favourite)
            return QGuiApplication::palette().color(QPalette::PlaceholderText);
        return {};
    case Qt::ToolTipRole:
        if (index.column() == Todo && !row.todoToolTip.isEmpty())
            return row.todoToolTip;
        if (index.column() == Changes && !row.changesToolTip.isEmpty())
            return row.changesToolTip;
        if (index.column() == Tabs && !row.tabsToolTip.isEmpty())
            return row.tabsToolTip;
        if (index.column() == Added) {
            QStringList lines;
            if (row.dates.added)
                lines << tr("Added: %1").arg(QLocale().toString(toDateTime(*row.dates.added),
                                                                QLocale::ShortFormat));
            else if (row.dates.uncommitted)
                lines << tr("Not committed to git yet");
            if (row.dates.edited)
                lines << tr("Last changed: %1")
                             .arg(QLocale().toString(toDateTime(*row.dates.edited),
                                                     QLocale::ShortFormat));
            if (!lines.isEmpty())
                return lines.join(QLatin1Char('\n'));
        }
        if (index.column() == Favourite)
            return row.favourite ? tr("A favourite: click to unmark it")
                                 : tr("Click to mark it as a favourite");
        return row.toolTip;
    case HasTodoRole:
        return !row.entry.todos.empty();
    case SortRole:
        if (index.column() == Favourite) // favourites together, first when ascending
            return row.favourite ? 0 : 1;
        // Tempo and capo as numbers (90 before 120); a song without one sorts first.
        if (index.column() == Tempo || index.column() == Capo) {
            const QString& text = index.column() == Tempo ? row.tempo : row.capo;
            qsizetype digits = 0;
            while (digits < text.size() && text[digits].isDigit())
                ++digits;
            return digits > 0 ? text.left(digits).toInt() : -1;
        }
        // Possible changes by their number (9 before 10), not as text; tabs likewise.
        if (index.column() == Changes)
            return row.changeCount;
        if (index.column() == Tabs)
            return static_cast<int>(row.entry.scores.size());
        // New and not committed: newest; unknown: oldest.
        if (index.column() == Added)
            return row.dates.added ? static_cast<qlonglong>(row.dates.added->time_since_epoch().count())
                   : row.dates.uncommitted ? std::numeric_limits<qlonglong>::max()
                                           : qlonglong{-1};
        return data(index, Qt::DisplayRole);
    case AddedRole:
        return row.dates.added ? QVariant(toDateTime(*row.dates.added)) : QVariant(QDateTime());
    case RecentRole:
        return row.recent;
    case HasChangesRole:
        return !row.changes.isEmpty();
    case HasTabsRole:
        return !row.entry.scores.empty();
    case FavouriteRole:
        return row.favourite;
    case LabelsRole:
        return row.labels;
    case LanguageRole:
        return row.languages;
    case SearchTextRole:
        return row.searchText;
    case InputNameRole:
        return toQString(row.entry.inputName);
    case CollectionRole:
        return toQString(row.entry.collection);
    default:
        return {};
    }
}

QVariant SongTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::ToolTipRole)
        return section == Favourite ? QVariant(tr("Favourite")) : QVariant();
    if (role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Title:
        return tr("Title");
    case Favourite:
        return QStringLiteral("★");
    case Artist:
        return tr("Artist");
    case Language:
        return tr("Language");
    case Key:
        return tr("Key");
    case Tempo:
        return tr("Tempo");
    case Meter:
        return tr("Meter");
    case Capo:
        return tr("Capo");
    case Tabs:
        return tr("Tabs", "column: the song includes scores (tablature)");
    case Labels:
        return tr("Labels");
    case Todo:
        return tr("TODO");
    case Changes:
        return tr("Possible changes");
    case Added:
        return tr("Added");
    default:
        return {};
    }
}
