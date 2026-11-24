#include "BuildFiles.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

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
    if (!scores && !book)
        return false;
    return QDir(path).removeRecursively();
}

} // namespace buildfiles
