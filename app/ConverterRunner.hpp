#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QtGlobal>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <functional>
#include <zub/core/converter.hpp>

class QProcess;
class QTemporaryDir;
class QTimer;

// What a finished chords_converter run printed (the caller interprets it with core).
struct ConverterOutput {
    int exitCode = 0;
    std::string out;
    std::string err;
    bool operator==(const ConverterOutput&) const = default; // Qt's metatype of std::expected needs it
};

// Runs a chords_converter command (tidy, suggest, apply) on a song text with QProcess, one run at
// a time, without blocking the UI. The text goes through a temporary file, so the song on disk is
// never touched.
class ConverterRunner : public QObject {
    Q_OBJECT

public:
    explicit ConverterRunner(QObject* parent = nullptr);
    ~ConverterRunner() override; // kills a running converter

    // Builds the command line from the path of the song file.
    using CommandFactory = std::function<zub::converter::Command(const std::filesystem::path&)>;

    // Writes `content` to <temp dir>/<stem>.tex and runs the command `makeCommand` returns for
    // that file. Returns false (and emits nothing) while a run is active. Failures to start are
    // reported through finished(), from the event loop.
    bool start(const CommandFactory& makeCommand, const std::string& content, const QString& stem);
    // Kills a running converter; no result is emitted.
    void cancel();
    bool isRunning() const { return busy_; }

signals:
    void finished(const std::expected<ConverterOutput, QString>& result);

private:
    void onFinished();
    void finish(std::expected<ConverterOutput, QString> result);
    void failLater(const QString& message); // a result that does not need the process
    void release(); // detaches and kills the process, removes the temp dir

    QProcess* process_ = nullptr;
    QTimer* watchdog_ = nullptr;
    std::unique_ptr<QTemporaryDir> dir_;
    QByteArray out_;
    QByteArray err_;
    bool timedOut_ = false;
    bool busy_ = false;        // from start() until the result is emitted or cancel()
    quint64 generation_ = 0;   // cancel() invalidates results that are still queued
};
