#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <zub/core/songcheck.hpp>
#include <zub/core/songsuggest.hpp>

class QAction;
class QWidget;
class ConverterRunner;
struct ConverterOutput;
class SongEditor;

// The song actions that chords_converter does for the open song: Tidy, Make Smaller, Join Lines,
// Check Song (with applying the picked proposals and Mark as Correct), the `pap` offer after a
// measurement, and the key check with its bar above the song. Owns the converter run and its
// state and the four toolbar actions; the window adds those to its toolbar, tells it the open
// song and the songbook, and reacts to its signals.
class SongActions : public QObject {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    // What only the window can do.
    struct Hooks {
        std::function<std::filesystem::path()> findConverter; // empty: none found
        // The approval rule of a build for the unknown commands of the song `content` (and its
        // scores), asking the user when `askUser` (with these button texts when not empty).
        std::function<bool(SongEditor*, const std::string& content, bool askUser,
                           const QString& yesText, const QString& noText)>
            approveBuild;
        std::function<std::string()> documentClass; // of the song preview (decides the paper)
        std::function<QString()> noPackageText;     // why a build cannot run
    };

    // `window`: the parent of the dialogs, and the owner of this object.
    SongActions(QWidget* window, Hooks hooks);

    void setSong(SongEditor* song); // the open song, or nullptr
    void setSongbook(const std::filesystem::path& root, const std::filesystem::path& packageDir);
    // Cancels the converter run (the song is closing; reports nothing).
    void cancelRun();
    // Stops the converter before the window goes away.
    void shutdown();

    QAction* tidyAction() const { return tidyAction_; }
    QAction* makeSmallerAction() const { return makeSmallerAction_; }
    QAction* joinLinesAction() const { return joinLinesAction_; }
    QAction* checkAction() const { return checkAction_; }
    // Enables the actions: the song's text is editable, the converter found, none running.
    // `songTab`: the song's own tab is the current one.
    void updateEnabled(bool songTab);

    // Tidy the open song (chords_converter tidy, one undo step); `note` ends the status message.
    void startTidy(const QString& note = {});
    void cancelMeasure(); // the background measurement, if one runs (the text changed ...)
    // False and a message box when the converter is older than zub::converter::kRequired.
    bool converterUsable(const std::filesystem::path& converter, const QString& action);

    // The keys Check Songs found, by song path (this session; dropped when acted on).
    void setKeyFinding(const std::filesystem::path& song, const zub::songcheck::KeyFinding& found) {
        keyFindings_[song] = found;
    }
    // checksongs.py's issues Check Songs found, by song path (this session; dropped when shown).
    void setMetadataFinding(const std::filesystem::path& song,
                            std::vector<zub::songcheck::SongIssue> issues) {
        metadataFindings_[song] = std::move(issues);
    }
    void clearScanFindings() {
        keyFindings_.clear();
        metadataFindings_.clear();
        metadataNotes_.clear();
        addedNotes_.clear();
    }
    // What the last Check Songs found for the song just opened (or open when it ended): the
    // header's missing fields are added as the converter's placeholders ("% tem={},", one undo
    // step, the cursor on the first), then the bar above the song offers a key (offerKey), else
    // says what was added and what else checksongs.py found.
    void offerFindings(SongEditor* editor);
    // The bar above the song with a key the last Check Songs found for it (to write, or to
    // confirm the written one with % nocheck key).
    void offerKey(SongEditor* editor);

signals:
    void statusMessage(const QString& text, int timeoutMs);
    void statusCleared();
    // The song's text should become this (one undo step).
    void songTextReplaceRequested(const std::string& text);
    void previewRequested();      // the song's PDF should be rebuilt
    void lineRequested(int line); // show this line of the song
    void stateChanged();          // the window's actions depend on the run state
    // The list under the editor after a Tidy: line and text of each change (replaces the old one).
    void tidyChangesListed(const std::vector<std::pair<int, QString>>& changes);

private:
    void showStatus(const QString& text, int timeoutMs = 0);
    // Whether the song may be built (without the list of what needs approval).
    bool approveBuild(SongEditor* editor, const std::string& content, bool askUser,
                      const QString& yesText = {}, const QString& noText = {});

    // Offers the `pap` header after a measurement of `measuredText` (a bar above the editor).
    void offerPapers(SongEditor* editor, const std::string& measuredText,
                     const zub::songsuggest::Suggestions& suggestions);
    // checkkeys.py on `content` (the song's current text, in a temporary copy), in the
    // background: keyFindings_ follows (a missing key or one the chords disagree with, unless
    // confirmed with % nocheck key), then `done` gets the finding (nullopt: none, or the check
    // could not run).
    void checkKey(SongEditor* editor, const std::string& content,
                  std::function<void(std::optional<zub::songcheck::KeyFinding>)> done);
    QString keyFindingText(const zub::songcheck::KeyFinding& finding,
                           const std::string& content) const;

    void onConverterFinished(const std::expected<ConverterOutput, QString>& output);
    void onTidyFinished(const std::expected<ConverterOutput, QString>& output);
    // Make Smaller, Join Lines, Check Song: shrink, join or check (a dialog to pick proposals),
    // then apply the picked ones (one undo step)
    enum class SongAction { Shrink, Join, Check };
    void startProposals(SongAction action);
    QString actionName(SongAction action) const;
    void onSuggestFinished(const std::expected<ConverterOutput, QString>& output);
    void startApply(std::vector<std::string> ids, const QString& description);
    void onApplyFinished(const std::expected<ConverterOutput, QString>& output);
    // After a proposal was applied: measures the new text in the background (the same shrink
    // --fit --measure; no dialog) and offers the `pap` header from its result alone.
    void startMeasure(SongEditor* editor);
    void onMeasureFinished(const std::expected<ConverterOutput, QString>& output);
    // Takes over the state of the run that just finished; the editor it belongs to, or nullptr
    // (after a status message) when the song changed meanwhile. `input` gets the bytes sent.
    SongEditor* takeRun(std::string& input, const QString& what);

    QWidget* window_; // parent of the dialogs
    Hooks hooks_;
    SongEditor* song_ = nullptr; // the open song, if any
    std::filesystem::path songbookRoot_;
    std::filesystem::path packageDir_;

    ConverterRunner* converterRunner_ = nullptr;
    enum class RunKind { Tidy, Suggest, Apply, Measure };
    RunKind runKind_ = RunKind::Tidy;
    QPointer<SongEditor> runEditor_; // the editor the running converter belongs to
    std::string runInput_;           // the bytes that were sent
    QString tidyNote_;               // appended to the status message when a tidy is done
    QString applyDescription_;       // of the proposal being applied
    SongAction songAction_ = SongAction::Shrink; // the action whose shrink/join/check/apply runs
    bool measuredRun_ = false;       // the running/last shrink or join measured (was approved)
    bool measureAfterApply_ = false; // measure the text again once the proposals are applied
    bool applyRestructures_ = false; // the proposals being applied make a part a chorus: Tidy after
    QAction* tidyAction_ = nullptr;
    QAction* makeSmallerAction_ = nullptr;
    QAction* joinLinesAction_ = nullptr;
    QAction* checkAction_ = nullptr;
    // Keys the last Check Songs found, by song path (this session; dropped when acted on).
    std::map<std::filesystem::path, zub::songcheck::KeyFinding> keyFindings_;
    std::map<std::filesystem::path, std::vector<zub::songcheck::SongIssue>> metadataFindings_;
    std::map<std::filesystem::path, QString> metadataNotes_; // the bar's text, until dismissed
    std::map<std::filesystem::path, QString> addedNotes_; // "added ...": shown once
    void addPlaceholders(SongEditor* editor); // metadataFindings_ into the text and notes
    void showMetadataNote(SongEditor* editor);
};
