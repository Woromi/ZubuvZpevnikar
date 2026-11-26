#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <map>
#include <zub/core/songcheck.hpp>
#include <zub/core/songsuggest.hpp>

class ConverterRunner;
class QProcess;

// What a check of the songs looks for (the Check Songs dialog).
struct ScanOptions {
    bool tidy = true;    // what Tidy would change
    bool check = true;   // corrections: typos, apostrophes, a chorus typed as a verse
    bool shrink = false; // songs that don't fit `paper` (typesets every song: slow)
    // The package's scripts (once for the whole songbook, before the songs):
    bool keysMissing = false; // checkkeys.py: a key for songs that have none
    bool keysStated = false;  // checkkeys.py: written keys the chords disagree with
    bool metadata = false;    // checksongs.py: the header's keys and values
    std::optional<zub::songsuggest::Paper> paper;
    std::filesystem::path converter;
    std::filesystem::path songbookRoot;
    std::filesystem::path packageDir; // measuring typesets with it (zub::package)
};

// What the scan found in one song, as text for the song list.
struct ScanFinding {
    QStringList tidy;   // Tidy's change descriptions
    QStringList check;  // Check Song's proposals
    QStringList shrink; // Make Smaller's proposals, for a song that does not fit
    QStringList keys;     // checkkeys.py: a key to write or a written one to look at
    QStringList metadata; // checksongs.py's issues
    QString pages;      // "A5: 2 pages" when it does not fit
    QStringList errors; // runs that failed, songs not measured
    bool any() const {
        return !tidy.isEmpty() || !check.isEmpty() || !shrink.isEmpty() || !keys.isEmpty() ||
               !metadata.isEmpty() || !errors.isEmpty();
    }
};

// Runs the package's checkkeys.py / checksongs.py --json once for the whole songbook, then
// chords_converter tidy / check / shrink --measure on many songs, one run at a time, without
// blocking the UI. Reads the song files; changes nothing. A song with commands outside the song
// format is not measured (that builds it with lualatex -shell-escape); tidy and check build
// nothing.
class SongbookScan : public QObject {
    Q_OBJECT

public:
    struct Song {
        std::filesystem::path path;
        std::string inputName; // "Artist/Title", for the progress text
    };

    SongbookScan(ScanOptions options, std::vector<Song> songs, QObject* parent = nullptr);

    void start();
    void cancel(); // stops the running converter; finished(true) follows from the event loop

    int stepCount() const { return static_cast<int>(scripts_.size() + steps_.size()); }

signals:
    void progress(int done, int total, const QString& what);
    // Only for songs where something was found (or something failed).
    void songScanned(const QString& path, const ScanFinding& finding);
    // A key the open song may be offered (a missing one, or a written one the chords disagree
    // with and that is not confirmed with % nocheck key).
    void keyFound(const QString& path, const zub::songcheck::KeyFinding& finding);
    // checksongs.py's issues of a song (missing fields to add as placeholders when it is opened).
    void metadataFound(const QString& path, const std::vector<zub::songcheck::SongIssue>& issues);
    void problem(const QString& message); // a script that failed: for the status bar
    void finished(bool cancelled);

private:
    enum class Kind { Tidy, Check, Shrink };
    struct Step {
        std::size_t song;
        Kind kind;
    };

    void next();
    void nextScript();
    void onScriptFinished();
    void takeScriptFindings(std::size_t song); // into finding_
    void onRunFinished(const QString& error, int exitCode, const std::string& out,
                       const std::string& err);
    void endSong(std::size_t song);

    ScanOptions options_;
    std::vector<Song> songs_;
    std::vector<Step> steps_;
    std::size_t current_ = 0;
    ScanFinding finding_; // of the song being scanned
    std::string content_; // of the song being scanned
    ConverterRunner* runner_ = nullptr;
    enum class Script { Keys, Metadata };
    std::vector<Script> scripts_;
    std::size_t scriptAt_ = 0;
    QProcess* process_ = nullptr;
    // What the scripts found, by the song's path (utf-8), until the song is reported.
    std::map<std::string, ScanFinding> scriptFindings_;
    bool cancelled_ = false;
};
