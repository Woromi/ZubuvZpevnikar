#pragma once

#include <QString>
#include <QtGlobal>
#include <chrono>
#include <string>

// The build files the app keeps in its cache between builds (Settings → Build files): LilyPond's
// engraved scores (`tmp-ly`, by a hash of each score) in the song preview's work dir, and a
// songbook's work dir CacheLocation/songbooks/repo-<hash>/ (its scores and indexes). Kept (the
// default), a rebuild engraves only the scores that changed, and what no build has used for
// `unusedFor` goes by itself (keepUsed, pruneWorkDirs); else they go after every build, and every
// build engraves all its scores again (a big songbook: minutes instead of seconds).
namespace buildfiles {

bool keep(); // the setting `build/keepFiles` (default true)
void setKeep(bool keep);

QString cacheDir();     // CacheLocation (the app's)
QString songbooksDir(); // cacheDir()/songbooks: the songbooks' work dirs

// Bytes under `dir` (0 if it does not exist).
qint64 size(const QString& dir);

// Deletes `dir` with its content, only when it is a cache folder of the app's builds: a `tmp-ly`
// in the cache, a `repo-<hash>` in songbooksDir() or another instance's `preview-<pid>` in the
// cache. False when it is none or not all went.
bool remove(const QString& dir);

// How long a score or a work dir no build used is kept.
inline constexpr std::chrono::days unusedFor{30};

// After a successful build that used the scores in `scoresDir` (a `tmp-ly`) and wrote `log`: the
// files of the scores it used are marked as used now (their modification time), and the scores
// no build used for `unusedFor` are deleted (zub::scorecache; other files are left alone).
void keepUsed(const QString& scoresDir, const std::string& log);

// Songbook work dirs and other instances' preview dirs that no build changed for `unusedFor`
// (a book renamed or deleted, an instance that ended), except `inUse` (one being built).
void pruneWorkDirs(const QString& inUse = {});

} // namespace buildfiles
