#include "HomeData.hpp"

#include "SongTableModel.hpp"
#include "convert.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QLocale>
#include <QSettings>
#include <algorithm>
#include <optional>
#include <map>
#include <zub/core/bookfile.hpp>
#include <zub/core/library.hpp>
#include <zub/core/songtodo.hpp>
#include <zub/core/textfile.hpp>
#include <zub/core/wishlist.hpp>

namespace {

QString settingsKey(const std::filesystem::path& root) {
    const QByteArray hash = QCryptographicHash::hash(toQString(root).toUtf8(),
                                                     QCryptographicHash::Sha1)
                                .toHex()
                                .left(16);
    return QStringLiteral("home/openedSongs/") + QString::fromLatin1(hash);
}

HomeSong songOf(const SongTableModel& model, int row, const QString& detail = {}) {
    HomeSong song;
    song.path = model.song(row)->path;
    song.title = model.index(row, SongTableModel::Title).data().toString();
    song.artist = model.index(row, SongTableModel::Artist).data().toString();
    song.detail = detail;
    return song;
}

} // namespace

HomeData buildHomeData(const SongTableModel& model, const std::filesystem::path& root,
                       const std::vector<std::filesystem::path>& opened) {
    HomeData data;
    data.root = root;
    data.datesKnown = model.hasDates();
    std::map<std::string, std::filesystem::path> byName; // input name -> song (chords first)
    std::map<std::string, std::vector<std::filesystem::path>> byStem; // file name -> songs
    std::map<QString, int> perCollection;
    struct Added {
        int row;
        QDateTime when; // invalid: not committed yet
    };
    std::vector<Added> added;
    std::vector<int> todo;
    for (int row = 0; row < model.rowCount(); ++row) {
        const zub::SongEntry* song = model.song(row);
        if (!song)
            continue;
        ++data.songs;
        ++perCollection[toQString(song->collection)];
        if (!byName.contains(song->inputName) || song->collection == "chords")
            byName[song->inputName] = song->path;
        byStem[song->path.stem().string()].push_back(song->path);
        const QModelIndex index = model.index(row, 0);
        if (data.datesKnown && index.data(SongTableModel::RecentRole).toBool()) {
            const QDateTime when = index.data(SongTableModel::AddedRole).toDateTime();
            added.push_back({row, when});
            if (!when.isValid())
                ++data.uncommitted;
        }
        if (!song->todos.empty())
            todo.push_back(row);
    }
    // In the songbook's order (chords first), then any other.
    for (const std::string& collection : zub::collections(root))
        if (const auto it = perCollection.find(toQString(collection)); it != perCollection.end()) {
            data.collections.emplace_back(it->first, it->second);
            perCollection.erase(it);
        }
    for (const auto& [name, count] : perCollection)
        data.collections.emplace_back(name, count);

    for (const auto& relative : opened) {
        const int row = model.rowOf(root / relative);
        if (row >= 0 && static_cast<int>(data.opened.size()) < HomeData::kListed)
            data.opened.push_back(songOf(model, row));
    }

    // Not committed yet first, then the newest.
    std::ranges::stable_sort(added, [](const Added& a, const Added& b) {
        if (a.when.isValid() != b.when.isValid())
            return !a.when.isValid();
        return a.when > b.when;
    });
    data.addedTotal = static_cast<int>(added.size());
    for (const Added& a : added) {
        if (static_cast<int>(data.added.size()) >= HomeData::kListed)
            break;
        data.added.push_back(songOf(model, a.row,
                                    a.when.isValid()
                                        ? QLocale().toString(a.when.date(), QLocale::ShortFormat)
                                        : QCoreApplication::translate("HomeData", "not committed yet")));
    }

    // Imported songs not checked yet first, then by title.
    std::ranges::stable_sort(todo, [&model](int a, int b) {
        const auto notChecked = [&model](int row) {
            const auto& todos = model.song(row)->todos;
            return std::ranges::find(todos, std::string(zub::todo::kNotChecked)) != todos.end();
        };
        if (notChecked(a) != notChecked(b))
            return notChecked(a);
        return model.index(a, SongTableModel::Title).data().toString().localeAwareCompare(
                   model.index(b, SongTableModel::Title).data().toString()) < 0;
    });
    data.todoTotal = static_cast<int>(todo.size());
    for (const int row : todo) {
        if (static_cast<int>(data.todo.size()) >= HomeData::kListed)
            break;
        const std::string& note = model.song(row)->todos.front();
        data.todo.push_back(songOf(
            model, row,
            note == zub::todo::kNotChecked
                ? QCoreApplication::translate("HomeData", "not checked yet (imported by the app)")
                : toQString(note)));
    }

    // The wish list first, then the books' wishes; a wish is matched to a library song by its
    // name, else by the one song with that file name (an artist folder spelled otherwise, a wish
    // of only the title: "LodJohnB").
    const auto librarySong = [&](const zub::bookfile::Wish& wish) -> std::filesystem::path {
        if (const auto it = byName.find(wish.name); !wish.name.empty() && it != byName.end())
            return it->second;
        const std::string stem =
            wish.name.empty() ? (wish.text.find(' ') == std::string::npos ? wish.text
                                                                           : std::string())
                              : wish.name.substr(wish.name.rfind('/') + 1);
        if (const auto found = byStem.find(stem);
            !stem.empty() && found != byStem.end() && found->second.size() == 1)
            return found->second.front();
        return {};
    };
    if (const auto list = zub::readTextFile(root / zub::wishlist::kFileName))
        for (const auto& wish : zub::wishlist::parse(*list)) {
            HomeWish w;
            w.text = toQString(wish.text);
            w.name = toQString(wish.name);
            w.line = static_cast<int>(wish.line);
            w.song = librarySong(wish);
            data.wishes.push_back(std::move(w));
        }
    for (const auto& relative : zub::bookfile::findBooks(root)) {
        const auto text = zub::readTextFile(root / relative);
        if (!text)
            continue;
        HomeBook book;
        book.id = toQString(relative.generic_string());
        const auto parsed = zub::bookfile::parse(*text);
        const auto settings = zub::bookfile::readSettings(*text);
        book.title = !settings.title.empty() ? toQString(settings.title)
                                             : toQString(relative.stem().string());
        book.locked = settings.locked;
        if (parsed)
            book.songs = static_cast<int>(parsed->songs().size());
        for (const auto& wish : zub::bookfile::wishes(*text)) {
            HomeWish w;
            w.bookId = book.id;
            w.bookTitle = book.title;
            w.text = toQString(wish.text);
            w.name = toQString(wish.name);
            w.line = static_cast<int>(wish.line);
            w.song = librarySong(wish);
            data.wishes.push_back(std::move(w));
            ++book.wishes;
        }
        data.books.push_back(std::move(book));
    }
    // Books of the same title are told apart by their file name.
    std::map<QString, int> titles;
    for (const HomeBook& book : data.books)
        ++titles[book.title];
    for (HomeBook& book : data.books)
        if (titles[book.title] > 1)
            book.title += QStringLiteral(" (") +
                          toQString(std::filesystem::path(toStdString(book.id)).stem().string()) +
                          QLatin1Char(')');
    for (HomeWish& wish : data.wishes)
        for (const HomeBook& book : data.books)
            if (book.id == wish.bookId)
                wish.bookTitle = book.title;
    return data;
}

std::vector<std::filesystem::path> rememberedSongs(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> songs;
    for (const QString& entry : QSettings{}.value(settingsKey(root)).toStringList())
        songs.push_back(toPath(entry));
    return songs;
}

void rememberSong(const std::filesystem::path& root, const std::filesystem::path& song) {
    if (root.empty())
        return;
    // Lexically, as the model builds its paths from the root (no symlinks resolved, no disk).
    const auto relative = song.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty() || *relative.begin() == ".." || relative.is_absolute())
        return;
    QStringList list = QSettings{}.value(settingsKey(root)).toStringList();
    const QString entry = toQString(relative.generic_string());
    list.removeAll(entry);
    list.prepend(entry);
    while (list.size() > HomeData::kListed)
        list.removeLast();
    QSettings{}.setValue(settingsKey(root), list);
}

QString splitWords(const QString& part) {
    QString words;
    for (qsizetype i = 0; i < part.size(); ++i) {
        const QChar c = part[i];
        if (c == QLatin1Char('_') || c == QLatin1Char('-')) {
            words += QLatin1Char(' ');
            continue;
        }
        if (i > 0) {
            const QChar before = part[i - 1];
            const bool nextLower = i + 1 < part.size() && part[i + 1].isLower();
            // "NecoOLasce": a capital after a small letter or a digit starts a word, and so does
            // the last capital of a run that a small letter follows ("O Lasce").
            const bool wordStart =
                (c.isUpper() && (before.isLower() || before.isDigit() ||
                                 (before.isUpper() && nextLower))) ||
                (c.isDigit() && before.isLetter());
            if (wordStart)
                words += QLatin1Char(' ');
        }
        words += c;
    }
    return words.simplified();
}

namespace {

// The artist's and the title's words of an input name, and what the line says after it.
struct WishParts {
    QString artist, title, rest;
};

WishParts wishParts(const QString& text, const QString& name) {
    WishParts parts;
    const qsizetype slash = name.lastIndexOf(QLatin1Char('/'));
    parts.artist = splitWords(name.left(slash));
    parts.title = splitWords(name.mid(slash + 1));
    parts.rest = text.mid(name.size()).trimmed();
    return parts;
}

} // namespace

QString wishDisplayText(const QString& text, const QString& name) {
    if (name.isEmpty())
        return text;
    const WishParts parts = wishParts(text, name);
    QString display = parts.title + QStringLiteral(" — ") + parts.artist;
    if (!parts.rest.isEmpty())
        display += QStringLiteral("  (") + parts.rest + QLatin1Char(')');
    return display;
}

QString wishSearchText(const QString& text, const QString& name, const QString& suffix) {
    if (name.isEmpty())
        return text.simplified() + QLatin1Char(' ') + suffix;
    const WishParts parts = wishParts(text, name);
    return parts.artist + QLatin1Char(' ') + parts.title + QLatin1Char(' ') + suffix;
}

namespace {

QString fileError(const std::filesystem::path& file, const std::string& why) {
    return QCoreApplication::translate("HomeData", "%1: %2")
        .arg(toQString(file.filename()), toQString(why));
}

// The wish's file, read now; for a book also refused when locked.
std::expected<std::pair<std::filesystem::path, std::string>, QString>
readWishFile(const std::filesystem::path& root, const HomeWish& wish) {
    const std::filesystem::path file = wish.bookId.isEmpty()
                                           ? root / zub::wishlist::kFileName
                                           : root / toPath(wish.bookId);
    auto text = zub::readTextFile(file);
    if (!text)
        return std::unexpected(fileError(file, text.error()));
    if (!wish.bookId.isEmpty() && zub::bookfile::readSettings(*text).locked)
        return std::unexpected(QCoreApplication::translate(
                                   "HomeData", "%1 is locked (released): its wishes stay.")
                                   .arg(wish.bookTitle));
    return std::pair{file, std::move(*text)};
}

// The wish's line in the file now: its own line when it still says the same, else the one line
// that says the same (lines above it were added or removed meanwhile); nullopt when gone or
// ambiguous.
std::optional<std::size_t> lineNow(const std::string& text, const HomeWish& wish) {
    const auto all = wish.bookId.isEmpty() ? zub::wishlist::parse(text) : zub::bookfile::wishes(text);
    std::optional<std::size_t> found;
    int same = 0;
    for (const auto& w : all) {
        if (toQString(w.text) != wish.text)
            continue;
        if (static_cast<int>(w.line) == wish.line)
            return w.line;
        found = w.line;
        ++same;
    }
    return same == 1 ? found : std::nullopt;
}

std::expected<void, QString> write(const std::filesystem::path& file,
                                   const std::expected<std::string, std::string>& edited) {
    if (!edited)
        return std::unexpected(fileError(file, edited.error()));
    if (auto written = zub::writeTextFileAtomic(file, *edited); !written)
        return std::unexpected(fileError(file, written.error()));
    return {};
}

QString changedError(const HomeWish& wish) {
    return QCoreApplication::translate("HomeData",
                                       "The wish \"%1\" is no longer in its file (or is there "
                                       "twice now). Nothing was written.")
        .arg(wish.text);
}

} // namespace

std::expected<void, QString> fulfilWish(const std::filesystem::path& root, const HomeWish& wish,
                                        const std::string& inputName) {
    auto read = readWishFile(root, wish);
    if (!read)
        return std::unexpected(read.error());
    const auto& [file, text] = *read;
    const auto line = lineNow(text, wish);
    if (!line)
        return std::unexpected(changedError(wish));
    if (wish.bookId.isEmpty())
        return write(file, zub::wishlist::remove(text, *line, toStdString(wish.text)));
    return write(file, zub::bookfile::fulfillWish(text, *line, inputName));
}

std::expected<void, QString> removeWish(const std::filesystem::path& root, const HomeWish& wish) {
    auto read = readWishFile(root, wish);
    if (!read)
        return std::unexpected(read.error());
    const auto& [file, text] = *read;
    const auto line = lineNow(text, wish);
    if (!line)
        return std::unexpected(changedError(wish));
    if (wish.bookId.isEmpty())
        return write(file, zub::wishlist::remove(text, *line, toStdString(wish.text)));
    return write(file, zub::bookfile::removeWish(text, *line));
}

std::expected<void, QString> addWish(const std::filesystem::path& root, const QString& text) {
    const std::filesystem::path file = root / zub::wishlist::kFileName;
    std::string current;
    if (std::filesystem::exists(file)) {
        auto read = zub::readTextFile(file);
        if (!read)
            return std::unexpected(fileError(file, read.error()));
        current = std::move(*read);
    }
    return write(file, zub::wishlist::add(current, toStdString(text)));
}

std::expected<void, QString> moveWishToList(const std::filesystem::path& root,
                                            const HomeWish& wish) {
    if (wish.bookId.isEmpty())
        return {};
    auto read = readWishFile(root, wish);
    if (!read)
        return std::unexpected(read.error());
    if (!lineNow(read->second, wish))
        return std::unexpected(changedError(wish));
    if (auto added = addWish(root, wish.text); !added)
        return added;
    if (auto removed = removeWish(root, wish); !removed)
        return std::unexpected(
            QCoreApplication::translate("HomeData",
                                        "The wish is in the wish list now, but still in %1: %2")
                .arg(wish.bookTitle, removed.error()));
    return {};
}
