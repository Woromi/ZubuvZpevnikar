#pragma once

#include <QString>
#include <QtGlobal>

// The build files the app keeps in its cache between builds (Settings → Build files): LilyPond's
// engraved scores (`tmp-ly`, by a hash of each score) in the song preview's work dir, and a
// songbook's work dir CacheLocation/songbooks/repo-<hash>/ (its scores and indexes). Kept (the
// default), a rebuild engraves only the scores that changed; else they go after every build, and
// every build engraves all its scores again (a big songbook: minutes instead of seconds).
namespace buildfiles {

bool keep(); // the setting `build/keepFiles` (default true)
void setKeep(bool keep);

QString cacheDir();     // CacheLocation (the app's)
QString songbooksDir(); // cacheDir()/songbooks: the songbooks' work dirs

// Bytes under `dir` (0 if it does not exist).
qint64 size(const QString& dir);

// Deletes `dir` with its content, only when it is a cache folder of the app's builds: a `tmp-ly`
// in the cache or a `repo-<hash>` in songbooksDir(). False when it is none or not all went.
bool remove(const QString& dir);

} // namespace buildfiles
