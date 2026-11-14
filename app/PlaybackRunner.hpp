#pragma once

#include <QObject>
#include <QString>
#include <filesystem>
#include <optional>

class QProcess;
class QThread;
class QTimer;

struct PlaybackRequest {
    std::filesystem::path midiFile; // the MIDI file of the latest successful score build
    QString fluidsynth;             // the executable
    std::filesystem::path soundfont;
    std::filesystem::path cacheDir; // the WAV files <key>.wav live here
    bool amp = false;               // run the rendered audio through zub::audio::applyAmp
    // The MIDI file of the staves that sound through the amp while the others (midiFile) do not:
    // rendered on its own, run through the amp, and mixed into the other audio. Empty without.
    std::filesystem::path ampMidiFile;
};

struct PlaybackResult {
    bool success = false;
    std::filesystem::path wav; // the rendered audio (success only)
    QString message;           // why it failed
    bool cached = false;       // the WAV was there already: nothing was rendered
};

// Renders a MIDI file to audio, one request at a time: fluidsynth writes <cacheDir>/<key>.wav (the
// key hashes the MIDI bytes, the soundfont and the effect with its version, so an unchanged score is answered from the cache at
// once). Runs with QProcess; a new request kills the running one and only the newest produces a
// result. The audio is trimmed and, for the amp, distorted on a worker thread (it reads the whole
// file).
class PlaybackRunner : public QObject {
    Q_OBJECT

public:
    explicit PlaybackRunner(QObject* parent = nullptr);
    ~PlaybackRunner() override;

    void request(PlaybackRequest request);
    void cancel(); // drops pending work and kills a running one; no result is emitted
    bool isRunning() const;

signals:
    void started(); // a render is needed (not emitted for a cached WAV)
    void finished(const PlaybackResult& result);

private:
    void start(PlaybackRequest request);
    void startRender(const std::filesystem::path& midi, const std::filesystem::path& wav);
    void onProcessFinished();
    void finishWith(PlaybackResult result);
    void killTree();
    void postProcess();
    void onPostProcessed(bool ok);
    void trimCache() const;

    QProcess* process_ = nullptr;
    QTimer* timeout_ = nullptr;
    bool running_ = false;
    PlaybackRequest current_;
    std::optional<PlaybackRequest> pending_; // waits for the killed process to exit
    std::filesystem::path wav_;              // the final file of the running request
    std::filesystem::path wavTemp_;          // fluidsynth writes here first
    std::filesystem::path ampTemp_;          // ... and the amp staves' audio here (second render)
    std::filesystem::path playedMidi_;       // the files fluidsynth reads (bends smoothed)
    std::filesystem::path playedAmpMidi_;
    bool renderingAmp_ = false;              // the running fluidsynth renders the amp staves
    bool timedOut_ = false;
    bool cancelled_ = false;
    bool startFailed_ = false;
    QThread* worker_ = nullptr; // trimming and the effect of the rendered file
};
