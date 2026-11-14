#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <zub/core/score.hpp>

class QProcess;
class QTimer;

struct ScoreResult {
    bool success = false;
    QByteArray svg;                        // the engraved score (success only)
    std::filesystem::path midi;            // the score's MIDI file; empty if there is none
    std::vector<zub::score::Error> errors; // LilyPond's errors, in log order
    QString message;                       // failures that are not LilyPond errors: lilypond
                                           // missing, prepare() failed, timeout, crash
    std::string inputName;                 // "Artist/X.ly": the request this result belongs to
    std::filesystem::path scoreCopy;       // the file errors() locations refer to
    QString log;                           // LilyPond's output
    qint64 elapsedMs = 0;
    bool amp = false;                      // the request's MIDI file is for the amp instrument
};

// Runs one score build (lilypond to SVG) at a time with QProcess. A new request while a build
// runs kills that build, and only the newest request produces a result.
class ScoreRunner : public QObject {
    Q_OBJECT

public:
    explicit ScoreRunner(QObject* parent = nullptr);
    ~ScoreRunner() override; // kills a running build

    // `amp`: the MIDI file of this build is for the amp instrument; the result carries it back.
    void request(zub::score::Request request, bool amp = false);
    void cancel(); // drops pending work and kills a running build; no result is emitted
    bool isRunning() const;

signals:
    void started();
    void finished(const ScoreResult& result);

private:
    void start(zub::score::Request request, bool announce, bool amp);
    void killTree();
    void onProcessFinished();
    void startPending();

    QProcess* process_ = nullptr;
    QTimer* timeout_ = nullptr;
    QElapsedTimer clock_;
    std::optional<zub::score::Command> command_; // the running build
    std::optional<zub::score::Request> pending_; // waits for the killed build to exit
    bool pendingAmp_ = false;
    bool amp_ = false;                           // of the running build
    std::string inputName_;                      // of the running build
    bool timedOut_ = false;
    bool cancelled_ = false;
};
