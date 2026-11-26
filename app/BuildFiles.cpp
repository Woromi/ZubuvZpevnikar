#include "BuildFiles.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <filesystem>
#include <set>
#include <system_error>
#include <zub/core/scorecache.hpp>

namespace buildfiles {

namespace {
const auto keepKey = QStringLiteral("build/keepFiles");
} // namespace

bool keep() {
    return QSettings{}.value(keepKey, true).toBool();
}

void setKeep(bool keepFiles) {
    if (keepFiles)
        QSettings{}.remove(keepKey);
    else
        QSettings{}.setValue(keepKey, false);
}

QString cacheDir() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

QString songbooksDir() {
    return cacheDir() + QStringLiteral("/songbooks");
}

qint64 size(const QString& dir) {
    qint64 bytes = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        bytes += it.fileInfo().size();
    }
    return bytes;
}

bool remove(const QString& dir) {
    const QString cache = cacheDir();
    if (cache.isEmpty())
        return false;
    const QFileInfo info(dir);
    if (!info.exists())
        return true;
    if (!info.isDir() || info.isSymLink())
        return false;
    const QString path = info.canonicalFilePath();
    // A cache that does not exist has no canonical path (and must never become "/").
    const QString canonicalCache = QFileInfo(cache).canonicalFilePath();
    if (canonicalCache.isEmpty())
        return false;
    const QString books = QFileInfo(songbooksDir()).canonicalFilePath();
    const QString parent = QFileInfo(info.absolutePath()).canonicalFilePath();
    const bool scores = info.fileName() == QStringLiteral("tmp-ly") &&
                        (parent == canonicalCache ||
                         parent.startsWith(canonicalCache + QLatin1Char('/')));
    const bool book = !books.isEmpty() && info.fileName().startsWith(QStringLiteral("repo-")) &&
                      QFileInfo(info.absolutePath()).canonicalFilePath() == books;
    const bool otherPreview = (info.fileName().startsWith(QStringLiteral("preview-")) ||
                               info.fileName().startsWith(QStringLiteral("score-"))) &&
                              parent == canonicalCache;
    if (!scores && !book && !otherPreview)
        return false;
    return QDir(path).removeRecursively();
}

namespace {

namespace fs = std::filesystem;

fs::path nativePath(const QString& path) {
    return fs::path(path.toStdU16String());
}

} // namespace

void keepUsed(const QString& scoresDir, const std::string& log) {
    const std::set<std::string> used = zub::scorecache::usedScores(log);
    const auto now = fs::file_time_type::clock::now();
    const auto oldest = now - unusedFor;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(nativePath(scoresDir), ec)) {
        if (!entry.is_regular_file(ec))
            continue;
        const auto u8 = entry.path().filename().u8string();
        const auto score = zub::scorecache::scoreOf(std::string(u8.begin(), u8.end()));
        if (!score)
            continue;
        if (used.contains(*score))
            fs::last_write_time(entry.path(), now, ec);
        else if (const auto changed = entry.last_write_time(ec); !ec && changed < oldest)
            fs::remove(entry.path(), ec);
    }
}

void pruneWorkDirs(const QString& inUse) {
    const auto oldest = fs::file_time_type::clock::now() - unusedFor;
    const QString keep = inUse.isEmpty() ? QString() : QFileInfo(inUse).canonicalFilePath();
    // This instance's own work dirs stay whatever their age (a pid of an old run reused).
    const QString ownPid = QString::number(QCoreApplication::applicationPid());
    const auto prune = [&](const QString& folder, const QString& pattern) {
        const QDir dir(folder);
        for (const QString& name : dir.entryList({pattern}, QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString path = dir.filePath(name);
            if (!keep.isEmpty() && QFileInfo(path).canonicalFilePath() == keep)
                continue;
            if (name.section(QLatin1Char('-'), 1) == ownPid)
                continue;
            // A build rewrites files in it, which changes the folder's own time.
            std::error_code ec;
            const auto changed = fs::last_write_time(nativePath(path), ec);
            if (!ec && changed < oldest)
                remove(path);
        }
    };
    prune(songbooksDir(), QStringLiteral("repo-*"));
    prune(cacheDir(), QStringLiteral("preview-*"));
    prune(cacheDir(), QStringLiteral("score-*")); // a second instance's score previews
}

} // namespace buildfiles
