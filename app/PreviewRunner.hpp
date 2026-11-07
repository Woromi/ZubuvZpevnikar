#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <optional>
#include <string>
#include <zub/core/preview.hpp>

class QProcess;
class QTimer;

struct PreviewResult {
    bool success = false;
    QByteArray pdf;                           // the built PDF (success only)
    std::optional<zub::preview::Error> error; // the first TeX error, if the log has one
    QString message;                          // failures that are not TeX errors: lualatex
                                              // missing, prepare() failed, timeout, crash
    std::string inputName;                    // the request this result belongs to
    QString log;                              // the full build log (may be empty)
    qint64 elapsedMs = 0;
};

// Runs one preview build at a time with QProcess. A new request while a build runs kills
// that build, and only the newest request produces a result.
class PreviewRunner : public QObject {
    Q_OBJECT

public:
    explicit PreviewRunner(QObject* parent = nullptr);
    ~PreviewRunner() override; // kills a running build

    void request(zub::preview::Request request);
    void cancel(); // drops pending work and kills a running build; no result is emitted
    bool isRunning() const;

signals:
    void started();
    void finished(const PreviewResult& result);

private:
    void start(zub::preview::Request request, bool announce);
    void killTree();
    void onProcessFinished();

    QProcess* process_ = nullptr;
    QTimer* timeout_ = nullptr;
    QElapsedTimer clock_;
    std::optional<zub::preview::Command> command_; // the running build
    std::optional<zub::preview::Request> pending_; // waits for the killed build to exit
    std::string inputName_;                        // of the running build
    bool timedOut_ = false;
    bool cancelled_ = false;
};
