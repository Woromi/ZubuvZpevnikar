#include "SongTableModel.hpp"

#include "convert.hpp"

#include <QCollator>
#include <algorithm>
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
    row.language = toQString(entry.meta.value("lang"));
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
    QStringList todos;
    for (const auto& note : entry.todos)
        todos << toQString(note);
    row.todo = todos.join(QStringLiteral("; "));
    row.todoToolTip = todos.join(QLatin1Char('\n'));
    row.searchText = foldForSearch(
        QStringList{row.title, row.artist, row.language, row.key, row.labelsText, row.todo}.join(
            QLatin1Char('\n')));
    row.toolTip = toQString(entry.inputName) + QLatin1Char('\n') + toQString(entry.path);
    row.entry = std::move(entry);
    return row;
}

void SongTableModel::setSongs(std::vector<zub::SongEntry> songs) {
    beginResetModel();
    rows_.clear();
    rows_.reserve(songs.size());
    for (auto& entry : songs)
        rows_.push_back(makeRow(std::move(entry)));
    endResetModel();
}

void SongTableModel::addSong(zub::SongEntry song) {
    const int row = static_cast<int>(rows_.size());
    beginInsertRows({}, row, row);
    rows_.push_back(makeRow(std::move(song)));
    endInsertRows();
}

bool SongTableModel::updateSong(const std::filesystem::path& path, zub::SongMetadata meta,
                                std::vector<std::string> todos) {
    const auto it = std::ranges::find(rows_, path, [](const Row& row) { return row.entry.path; });
    if (it == rows_.end())
        return false;
    zub::SongEntry entry = std::move(it->entry);
    entry.meta = std::move(meta);
    entry.todos = std::move(todos);
    *it = makeRow(std::move(entry));
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
    emit dataChanged(index(row, Changes), index(row, Changes));
}

void SongTableModel::clearScanResults() {
    for (Row& row : rows_) {
        row.changes.clear();
        row.changesToolTip.clear();
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
        case Favourite:
            return row.favourite ? QStringLiteral("★") : QString();
        case Artist:
            return row.artist;
        case Language:
            return row.language;
        case Key:
            return row.key;
        case Tempo:
            return row.tempo;
        case Meter:
            return row.meter;
        case Capo:
            return row.capo;
        case Labels:
            return row.labelsText;
        case Todo:
            return row.todo;
        case Changes:
            return row.changes;
        default:
            return {};
        }
    case Qt::ToolTipRole:
        if (index.column() == Todo && !row.todoToolTip.isEmpty())
            return row.todoToolTip;
        if (index.column() == Changes && !row.changesToolTip.isEmpty())
            return row.changesToolTip;
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
        return data(index, Qt::DisplayRole);
    case HasChangesRole:
        return !row.changes.isEmpty();
    case FavouriteRole:
        return row.favourite;
    case LabelsRole:
        return row.labels;
    case LanguageRole:
        return row.language;
    case SearchTextRole:
        return row.searchText;
    case InputNameRole:
        return toQString(row.entry.inputName);
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
    case Labels:
        return tr("Labels");
    case Todo:
        return tr("TODO");
    case Changes:
        return tr("Possible changes");
    default:
        return {};
    }
}
