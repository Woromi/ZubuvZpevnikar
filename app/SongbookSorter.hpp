#pragma once

#include <QObject>
#include <QString>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <zub/core/songbook.hpp>

class QProcess;
class QTemporaryDir;

// Puts the songs of a section in the songbook's title index order by running the songbook's own
// scripts/songidx.lua with texlua (asynchronously, one run at a time).
class SongbookSorter : public QObject {
    Q_OBJECT

public:
    explicit SongbookSorter(QObject* parent = nullptr);
    ~SongbookSorter() override;

    // The ZubuvZpevnik checkout and the texlua executable ("" = not found).
    void setTools(const std::filesystem::path& songbookRoot, const QString& texlua);

    // `entries` runs parallel to `inputNames`. A newer request with the same `key` replaces a
    // pending or running one (its result is dropped).
    void sort(const QString& key, std::vector<std::string> inputNames,
              std::vector<zub::songbook::SortEntry> entries);

signals:
    void sorted(const QString& key, std::vector<std::string> inputNames);
    void failed(const QString& key, const QString& message);
    // Once, before the first multi-song sort: no Czech locale is installed, so the order may differ
    // from the real book's index. Emitted queued.
    void warning(const QString& message);

private:
    struct Request {
        QString key;
        std::vector<std::string> names;
        std::vector<zub::songbook::SortEntry> entries;
    };
    void startNext();
    void finishRun(int exitCode, bool crashed, const QString& processError);
    void startLocaleDetection(); // `locale -a`, asynchronous, at most 2 s; requests wait for it
    void finishLocale(const QString& output, bool ran);
    void stopRun();

    std::filesystem::path root_;
    QString texlua_;
    bool localeDone_ = false;
    bool localeWarned_ = false;
    bool localeUnavailable_ = false; // `locale -a` ran and listed no Czech locale
    QProcess* localeProcess_ = nullptr;
    QString locale_;
    std::deque<Request> pending_;
    std::optional<Request> running_;
    bool runningStale_ = false;
    QProcess* process_ = nullptr;
    std::unique_ptr<QTemporaryDir> dir_;
    QByteArray output_;
};
