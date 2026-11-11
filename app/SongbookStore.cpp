#include "SongbookStore.hpp"

#include "convert.hpp"

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

SongbookStore::SongbookStore(const QString& directory, QObject* parent)
    : QObject(parent),
      directory_(directory.isEmpty()
                     ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                           QStringLiteral("/songbooks")
                     : directory) {}

QString SongbookStore::pathOf(const QString& id) const {
    return directory_ + QLatin1Char('/') + id + QStringLiteral(".json");
}

QString SongbookStore::slug(const QString& title) {
    QString result;
    bool pendingDash = false;
    for (const QChar c : title.normalized(QString::NormalizationForm_D)) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        const char16_t u = c.toLower().unicode();
        if ((u >= u'a' && u <= u'z') || (u >= u'0' && u <= u'9')) {
            if (pendingDash && !result.isEmpty())
                result += QLatin1Char('-');
            pendingDash = false;
            result += QChar(u);
        } else {
            pendingDash = true;
        }
    }
    return result.isEmpty() ? QStringLiteral("songbook") : result;
}

std::vector<SongbookStore::Entry> SongbookStore::list() const {
    std::vector<Entry> entries;
    const QDir dir(directory_);
    // Only lower-case ".json"; the id becomes a folder name, so it must be plain.
    const QStringList files =
        dir.entryList({QStringLiteral("*.json")}, QDir::Files | QDir::CaseSensitive, QDir::Name);
    static const QRegularExpression validId(
        QRegularExpression::anchoredPattern(QStringLiteral("[A-Za-z0-9_-]+")));
    for (const QString& name : files) {
        Entry entry;
        entry.id = name.chopped(5);
        QFile file(dir.filePath(name));
        if (!validId.match(entry.id).hasMatch()) {
            entry.error = tr("Invalid file name");
        } else if (!file.open(QIODevice::ReadOnly)) {
            entry.error = file.errorString();
        } else {
            const QByteArray bytes = file.readAll();
            auto book = zub::songbook::fromJson(std::string_view(bytes.constData(),
                                                                 static_cast<std::size_t>(bytes.size())));
            if (book)
                entry.book = std::move(*book);
            else
                entry.error = toQString(book.error());
        }
        entries.push_back(std::move(entry));
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::stable_sort(entries, [&](const Entry& a, const Entry& b) {
        const QString ta = a.error.isEmpty() ? toQString(a.book.title) : a.id;
        const QString tb = b.error.isEmpty() ? toQString(b.book.title) : b.id;
        return collator.compare(ta, tb) < 0;
    });
    return entries;
}

bool SongbookStore::save(const QString& id, const zub::songbook::Songbook& book, QString* error) {
    if (!QDir().mkpath(directory_)) {
        if (error)
            *error = QObject::tr("Cannot create %1").arg(directory_);
        return false;
    }
    const std::string json = zub::songbook::toJson(book);
    QSaveFile file(pathOf(id));
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(json.data(), static_cast<qint64>(json.size())) !=
            static_cast<qint64>(json.size()) ||
        !file.commit()) {
        if (error)
            *error = QObject::tr("Cannot write %1: %2").arg(pathOf(id), file.errorString());
        return false;
    }
    return true;
}

QString SongbookStore::create(const zub::songbook::Songbook& book, QString* error) {
    const QString base = slug(toQString(book.title));
    QString id = base;
    for (int n = 2; QFile::exists(pathOf(id)); ++n)
        id = base + QLatin1Char('-') + QString::number(n);
    return save(id, book, error) ? id : QString{};
}

bool SongbookStore::remove(const QString& id) {
    return QFile::remove(pathOf(id));
}
