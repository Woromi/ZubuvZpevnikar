#pragma once

// Helpers of the app tests: a songbook fixture copy, dialogs answered by themselves, waiting.
#include "ScorePlayer.hpp"
#include "convert.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
#include <catch2/catch_tostring.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace Catch {
// Failures print the text, not {?}.
template <> struct StringMaker<QString> {
    static std::string convert(const QString& text) { return '"' + text.toStdString() + '"'; }
};
} // namespace Catch

namespace apptest {

// A score player without an audio device: records what it is asked to play (the real one talks
// to the sound server). Install it with AppTestAccess::replacePlayer.
class SilentPlayer : public ScorePlayer {
public:
    void play(const std::filesystem::path& file, qint64 startMs) override {
        played.push_back({file, startMs});
        playing = true;
    }
    void stop() override { playing = false; }
    bool isPlaying() const override { return playing; }
    qint64 positionMs() const override { return position; }
    std::vector<std::pair<std::filesystem::path, qint64>> played;
    bool playing = false;
    qint64 position = 0;
};

// The whole file as bytes in a string ("" when missing).
std::string slurp(const std::filesystem::path& path);
void writeFile(const std::filesystem::path& path, const std::string& text);
bool has(const std::string& text, const std::string& part);

// A private copy of tests/data/songbook (never the shared fixture itself), optionally with the
// app tests' extra files (tests/app/data/extras: the Test/Cz and Test/En songs with scores, a song
// listing two languages, a book with a whistle song).
class Fixture {
public:
    enum class Extras { No, Yes };
    explicit Fixture(Extras extras = Extras::No);
    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path operator/(const std::filesystem::path& part) const {
        return root_ / part;
    }

private:
    QTemporaryDir dir_;
    std::filesystem::path root_;
};

// Polls (processing events) until `condition` holds; false after `timeoutMs`.
bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 10000);

// Calls `answer` on the first visible top-level widget that `matches` (a modal dialog's exec()
// blocks the test, so something else must press its button). Check fired() after the blocking call.
class Responder {
public:
    Responder(std::function<bool(QWidget*)> matches, std::function<void(QWidget*)> answer);
    ~Responder();
    Responder(const Responder&) = delete;
    Responder& operator=(const Responder&) = delete;
    bool fired() const { return state_->fired; }

private:
    struct State {
        bool fired = false;
    };
    std::shared_ptr<State> state_;
    QObject* timer_ = nullptr;
};

// Presses `button` of the next message box.
Responder answerMessageBox(QMessageBox::StandardButton button);
// Presses the message box's button with this text.
Responder answerMessageBoxText(const QString& buttonText);
// Types `text` into the next input dialog and accepts it.
Responder answerInputDialog(const QString& text);
// Runs `answer` on the next visible dialog of this class (found by its object name when given).
template <class Dialog>
Responder answerDialog(std::function<void(Dialog*)> answer, const QString& objectName = {}) {
    return Responder(
        [objectName](QWidget* w) {
            auto* d = qobject_cast<Dialog*>(w);
            return d && (objectName.isEmpty() || d->objectName() == objectName);
        },
        [answer = std::move(answer)](QWidget* w) { answer(static_cast<Dialog*>(w)); });
}
// Runs `answer` on the next visible QDialog with this window title.
Responder answerTitledDialog(const QString& title, std::function<void(QDialog*)> answer);

} // namespace apptest
