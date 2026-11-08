#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <zub/core/songsuggest.hpp>

class ConverterRunner;

// What a songbook scan looks for (the Scan Songbook dialog).
struct ScanOptions {
    bool tidy = true;    // what Tidy would change
    bool check = true;   // corrections: typos, apostrophes, a chorus typed as a verse
    bool shrink = false; // songs that don't fit `paper` (typesets every song: slow)
    std::optional<zub::songsuggest::Paper> paper;
    std::filesystem::path converter;
    std::filesystem::path songbookRoot;
};

// What the scan found in one song, as text for the song list.
struct ScanFinding {
    QStringList tidy;   // Tidy's change descriptions
    QStringList check;  // Check Song's proposals
    QStringList shrink; // Make Smaller's proposals, for a song that does not fit
    QString pages;      // "A5: 2 pages" when it does not fit
    QStringList errors; // runs that failed, songs not measured
    bool any() const {
        return !tidy.isEmpty() || !check.isEmpty() || !shrink.isEmpty() || !errors.isEmpty();
    }
};

// Runs chords_converter tidy / check / shrink --measure on many songs, one run at a time, without
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

    int stepCount() const { return static_cast<int>(steps_.size()); }

signals:
    void progress(int done, int total, const QString& what);
    // Only for songs where something was found (or something failed).
    void songScanned(const QString& path, const ScanFinding& finding);
    void finished(bool cancelled);

private:
    enum class Kind { Tidy, Check, Shrink };
    struct Step {
        std::size_t song;
        Kind kind;
    };

    void next();
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
    bool cancelled_ = false;
};
