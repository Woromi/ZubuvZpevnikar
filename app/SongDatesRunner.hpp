#pragma once

#include <QObject>
#include <filesystem>
#include <map>
#include <string>
#include <zub/core/songdates.hpp>

class QProcess;

// Reads when the songs were added from git: `git log`, then `git status`, with QProcess, one
// songbook at a time, without blocking the UI. A new start() or cancel() drops the run in
// progress (its result is never emitted). No git, no repository, or a failing command: failed(),
// and nothing else (the dates are an extra, not an error to show).
class SongDatesRunner : public QObject {
    Q_OBJECT

public:
    explicit SongDatesRunner(QObject* parent = nullptr);
    ~SongDatesRunner() override;

    void start(const std::filesystem::path& root);
    void cancel();
    bool isRunning() const { return process_ != nullptr; }

signals:
    // Paths relative to the repository (`root`), '/' separated.
    void datesReady(const std::filesystem::path& root,
                    const std::map<std::string, zub::songdates::Dates>& dates);
    void failed(const std::filesystem::path& root);

private:
    enum class Stage { Log, Status };
    void run(Stage stage);
    void onFinished(QProcess* process, Stage stage);
    void fail();
    void release(); // detaches and kills the process

    QProcess* process_ = nullptr;
    std::filesystem::path root_;
    std::map<std::string, zub::songdates::Dates> dates_;
};
