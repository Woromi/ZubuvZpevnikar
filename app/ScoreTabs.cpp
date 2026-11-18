#include "ScoreTabs.hpp"

#include "FretboardWidget.hpp"
#include "NoteSounds.hpp"
#include "PlaybackRunner.hpp"
#include "ScoreInputBar.hpp"
#include "ScorePlayer.hpp"
#include "ScoreRunner.hpp"
#include "SongEditor.hpp"
#include "SoundTools.hpp"
#include "convert.hpp"

#include <KSyntaxHighlighting/Repository>
#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <zub/core/fretboard.hpp>
#include <zub/core/library.hpp>
#include <zub/core/midi.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/score.hpp>
#include <zub/core/scoreregion.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/songedit.hpp>
#include <zub/core/textfile.hpp>

namespace {

const auto fretboardKey = QStringLiteral("score/fretboard");
const auto fretboardSoundKey = QStringLiteral("playback/fretboardSound");

std::filesystem::path pathOfName(const std::string& name) {
    return std::filesystem::path(std::u8string(name.begin(), name.end()));
}

// "Artist/X.ly": the name a \lilypondfile can carry that the app edits as a score.
bool validScoreName(const std::string& name) {
    return !zub::invalidInputName(name) && name.size() > 3 && name.ends_with(".ly") &&
           name[name.size() - 4] != '/';
}

} // namespace

ScoreTabs::ScoreTabs(QTabWidget* tabs, QStackedWidget* previewStack, QWidget* songPreview,
                     KSyntaxHighlighting::Repository& repository, const Dirs& dirs, Hooks hooks,
                     QObject* parent)
    : QObject(parent), tabs_(tabs), previewStack_(previewStack), songPreview_(songPreview),
      repository_(repository), hooks_(std::move(hooks)), playbackCacheDir_(dirs.playbackDir),
      scoreWorkDir_(dirs.workDir) {
    scorePreview_ = new ScorePreviewPane(previewStack_);
    previewStack_->addWidget(scorePreview_);
    noteSounds_ = new NoteSounds(dirs.soundsDir, this);
    connect(noteSounds_, &NoteSounds::failed, this,
            [this](const QString& message) { emit statusMessage(message, 8000); });

    // Scores: the song's \lilypondfile references become tabs (a moment after the text stops
    // changing); the score preview rebuilds a moment after the score text stops changing.
    scoreSyncTimer_ = new QTimer(this);
    scoreSyncTimer_->setSingleShot(true);
    scoreSyncTimer_->setInterval(600);
    connect(scoreSyncTimer_, &QTimer::timeout, this, &ScoreTabs::syncScoreTabs);
    scorePreviewTimer_ = new QTimer(this);
    scorePreviewTimer_->setSingleShot(true);
    scorePreviewTimer_->setInterval(600);
    connect(scorePreviewTimer_, &QTimer::timeout, this, [this] { requestScorePreview(); });
    // The fretboard follows the score's cursor and text a moment after they stop moving.
    fretboardTimer_ = new QTimer(this);
    fretboardTimer_->setSingleShot(true);
    fretboardTimer_->setInterval(80);
    connect(fretboardTimer_, &QTimer::timeout, this, &ScoreTabs::updateFretboard);
    fretboardAction_ = new QAction(tr("Fretboard"), this);
    fretboardAction_->setCheckable(true);
    fretboardAction_->setChecked(QSettings().value(fretboardKey, true).toBool());
    fretboardAction_->setToolTip(
        tr("Show the fretboard: click frets to stage notes (one per string), Insert "
           "(Ctrl+Return) writes them as a note or chord, Escape clears; double-click a fret "
           "inserts at once"));
    connect(fretboardAction_, &QAction::toggled, this, [this](bool shown) {
        QSettings().setValue(fretboardKey, shown);
        for (const ScoreTab& tab : scoreTabs_) {
            if (tab.inputPanel)
                tab.inputPanel->setVisible(shown);
        }
        updateFretboard();
    });
    // An on/off switch: a speaker icon shows whether a fret click sounds.
    soundAction_ = new QAction(tr("Fretboard Sound"), this);
    soundAction_->setCheckable(true);
    soundAction_->setChecked(QSettings().value(fretboardSoundKey, true).toBool());
    soundAction_->setToolTip(tr("Play the note when a fret is clicked"));
    const auto showSoundState = [this](bool on) {
        const QString icon =
            on ? QStringLiteral("audio-volume-high") : QStringLiteral("audio-volume-muted");
        soundAction_->setIcon(QIcon::fromTheme(icon));
        soundAction_->setStatusTip(on ? tr("Fret clicks play their note.")
                                      : tr("Fret clicks are silent."));
    };
    showSoundState(soundAction_->isChecked());
    connect(soundAction_, &QAction::toggled, this, [this, showSoundState](bool on) {
        QSettings().setValue(fretboardSoundKey, on);
        showSoundState(on);
        emit statusMessage(soundAction_->statusTip(), 3000);
    });
    // Actions on the selected notes of a score: one undo step each, the notes stay selected.
    highlightTimer_ = new QTimer(this);
    highlightTimer_->setSingleShot(true);
    highlightTimer_->setInterval(120);
    connect(highlightTimer_, &QTimer::timeout, this, &ScoreTabs::updateScoreHighlights);
    {
        using zub::scoreregion::Action;
        struct Entry {
            const char* name;
            QString text;
            QString tip;
            Action action;
        };
        const Entry entries[] = {
            {"Tie", tr("Tie"), tr("Tie the selected notes: ~ after each one but the last"),
             Action::Tie},
            {"Slur", tr("Slur"),
             tr("Slur over the selected notes: ( after the first, ) after the last"), Action::Slur},
            {"Glissando", tr("Glissando"), tr("Glissando after the first selected note"),
             Action::Glissando},
            {"Repeat", tr("Repeat"), tr("Play the selected notes twice: \\repeat volta 2 { ... }"),
             Action::Repeat},
            {"StrokeDown", tr("Stroke Down"),
             tr("Down stroke over every selected chord and note (replaces an up stroke)"),
             Action::StrokeDown},
            {"StrokeUp", tr("Stroke Up"),
             tr("Up stroke over every selected chord and note (replaces a down stroke)"),
             Action::StrokeUp},
            {"DeadNotes", tr("Dead Notes"),
             tr("Dead (muted) notes: \\deadNote before every selected note; again removes them"),
             Action::DeadNotes},
        };
        for (const Entry& entry : entries) {
            auto* action = new QAction(entry.text, this);
            action->setObjectName(QString::fromLatin1(entry.name));
            action->setToolTip(entry.tip);
            action->setEnabled(false);
            connect(action, &QAction::triggered, this, [this, a = entry.action] {
                if (SongEditor* editor = currentEditor(); editor && !onSongTab()) {
                    zub::scoreregion::Refusal why = zub::scoreregion::Refusal::NothingToApply;
                    editor->applyRegionAction(a, &why);
                    if (why == zub::scoreregion::Refusal::SlurPresent)
                        emit statusMessage(
                            tr("A slur is already open here or the selection has slur marks."),
                            6000);
                    if (why == zub::scoreregion::Refusal::CrossesBlock)
                        emit statusMessage(
                            tr("The selection crosses a { } block: select notes within one "
                               "block."),
                            6000);
                    updateRegionActions();
                }
            });
            regionActions_.append(action);
        }
    }
    scorePreview_->setRegionActions(regionActions_);
    connect(scorePreview_, &ScorePreviewPane::notesDragged, this, &ScoreTabs::selectScoreNotes);
    connect(scorePreview_, &ScorePreviewPane::noteClicked, this, &ScoreTabs::clickScoreNote);
    connect(scorePreview_, &ScorePreviewPane::keyCommand, this, &ScoreTabs::scoreKey);
    connect(scorePreview_, &ScorePreviewPane::noteDoubleClicked, this,
            &ScoreTabs::doubleClickScoreNote);
    scoreRunner_ = new ScoreRunner(this);
    connect(scoreRunner_, &ScoreRunner::started, scorePreview_, &ScorePreviewPane::showBuilding);
    connect(scoreRunner_, &ScoreRunner::started, this, [this] {
        // The build replaces the MIDI file: nothing to play until it is done.
        playbackRunner_->cancel();
        scorePlayer_->stop();
        stopFollowing();
        timeline_ = {};
        endedAt_.reset();
        scorePreview_->setPlayState(ScorePreviewPane::PlayState::Idle);
        scoreMidi_.clear();
        updateSoundAvailability();
    });
    // A new tempo is a new MIDI file: rebuild the preview like after an edit.
    connect(scorePreview_, &ScorePreviewPane::tempoChanged, this,
            [this] { scorePreviewTimer_->start(); });
    // ... and a new instrument is another instrument in the MIDI file.
    connect(scorePreview_, &ScorePreviewPane::instrumentChanged, this, [this] {
        if (!onSongTab() && tabs_->currentIndex() >= 1) {
            const std::string& name =
                scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)].name;
            staffChoices_[name] = scorePreview_->staffChoices();
            chordChoices_[name] = scorePreview_->chordChoice();
            melodyChoices_[name] = scorePreview_->melodyChoice();
        }
        scoreRunner_->cancel(); // a build under way has the old instrument
        forgetScoreMidi(); // the old MIDI file has the old instrument: no Play until the rebuild
        scorePreviewTimer_->start();
    });
    connect(scorePreview_, &ScorePreviewPane::saveInstrumentRequested, this,
            &ScoreTabs::saveStaffInstrument);
    connect(scoreRunner_, &ScoreRunner::finished, this, &ScoreTabs::showScoreResult);
    playbackRunner_ = new PlaybackRunner(this);
    scorePlayer_ = new ScorePlayer(this);
    followTimer_ = new QTimer(this);
    followTimer_->setInterval(30);
    connect(followTimer_, &QTimer::timeout, this, &ScoreTabs::followTick);
    connect(playbackRunner_, &PlaybackRunner::started, this, [this] {
        scorePreview_->setPlayState(ScorePreviewPane::PlayState::Preparing);
        scorePreview_->showPlaybackMessage(tr("Preparing playback…"), false);
    });
    connect(playbackRunner_, &PlaybackRunner::finished, this, &ScoreTabs::showPlaybackResult);
    connectPlayer();
    connect(scorePreview_, &ScorePreviewPane::playRequested, this, &ScoreTabs::playScore);
    connect(scorePreview_, &ScorePreviewPane::stopRequested, this, &ScoreTabs::stopPlayback);
    updateSoundAvailability();
    connect(scorePreview_, &ScorePreviewPane::rebuildRequested, this,
            [this] { requestScorePreview(true); });
    connect(scorePreview_, &ScorePreviewPane::errorActivated, this, [this](int line, int) {
        if (SongEditor* editor = currentEditor(); editor && !onSongTab())
            editor->goToLine(line);
    });
    connect(tabs_, &QTabWidget::currentChanged, this, &ScoreTabs::onTabChanged);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &ScoreTabs::closeScoreTab);
}

// The player's signals (a test stands in another player, see AppTestAccess).
void ScoreTabs::connectPlayer() {
    connect(scorePlayer_, &ScorePlayer::finished, this, [this] {
        // Played to the end: the caret stays behind the last note, and Play from there (the caret
        // unmoved) plays the whole score again.
        SongEditor* editor = currentEditor();
        if (following_ && editor && !onSongTab() && !editor->selectedByteRange())
            endedAt_ = editor->cursorByteOffset();
        else
            endedAt_.reset();
        stopFollowing();
        scorePreview_->setPlayState(ScorePreviewPane::PlayState::Idle);
        scorePreview_->showPlaybackMessage({}, false);
    });
    connect(scorePlayer_, &ScorePlayer::failed, this, [this](const QString& message) {
        stopFollowing();
        scorePreview_->setPlayState(ScorePreviewPane::PlayState::Idle);
        scorePreview_->showPlaybackMessage(tr("Cannot play: %1").arg(message), true);
    });
}

void ScoreTabs::setSong(SongEditor* song) {
    song_ = song;
}

void ScoreTabs::setSongbook(const std::filesystem::path& root,
                            const std::filesystem::path& packageDir) {
    songbookRoot_ = root;
    packageDir_ = packageDir;
}

void ScoreTabs::shutdown() {
    scoreRunner_->cancel();
    playbackRunner_->cancel();
    for (const ScoreTab& tab : scoreTabs_) {
        if (tab.editor) {
            disconnect(tab.editor, nullptr, this, nullptr);
            disconnect(tab.editor->textEdit(), nullptr, this, nullptr);
            delete tab.editor;
        }
    }
    scoreTabs_.clear();
    // The window's widgets (the tabs, the song editor) go next, while this object lives on as
    // its child: nothing may reach them from here any more.
    disconnect(tabs_, nullptr, this, nullptr);
    song_ = nullptr;
}

bool ScoreTabs::confirmCloseScores() {
    // Scores first: discarding a new one edits the song, which is asked about after.
    // Copies: saving a score may change the tabs.
    std::vector<std::pair<std::string, QPointer<SongEditor>>> scores;
    for (const ScoreTab& tab : scoreTabs_)
        if (tab.editor)
            scores.emplace_back(tab.name, tab.editor);
    for (const auto& [name, score] : scores) {
        if (!score || !(score->isModified() || score->isNew()))
            continue;
        if (const int index = scoreTabIndex(name); index > 0)
            tabs_->setCurrentIndex(index); // show what is being asked about
        if (!confirmCloseScore(name, score))
            return false;
    }
    return true;
}

bool ScoreTabs::confirmCloseScore(const std::string& name, SongEditor* score) {
    if (!score->isNew())
        return hooks_.confirmClose(score);
    const auto answer = QMessageBox::question(
        tabs_->window(), tr("Unsaved changes"),
        tr("Discard the new score %1? Its \\lilypondfile line is removed from the song too.")
            .arg(toQString(name)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        if (auto saved = score->save(); !saved) {
            QMessageBox::warning(tabs_->window(), tr("Cannot save score"), saved.error());
            return false;
        } else if (*saved == SongEditor::SaveResult::Cancelled) {
            return false;
        }
        return true;
    }
    removeScoreReference(name);
    return true;
}

void ScoreTabs::removeScoreReference(const std::string& name) {
    if (!song_ || song_->textEdit()->isReadOnly())
        return;
    emit songTextReplaceRequested(zub::songedit::removeScoreReference(song_->content(), name));
}

SongEditor* ScoreTabs::currentEditor() const {
    if (!song_)
        return nullptr;
    const int index = tabs_->currentIndex();
    if (index <= 0 || index > static_cast<int>(scoreTabs_.size()))
        return song_;
    return scoreTabs_[static_cast<std::size_t>(index - 1)].editor.data();
}

bool ScoreTabs::onSongTab() const {
    return song_ && tabs_->currentIndex() <= 0;
}

bool ScoreTabs::anyScoreModified() const {
    return std::ranges::any_of(
        scoreTabs_, [](const ScoreTab& tab) { return tab.editor && tab.editor->isModified(); });
}

SongEditor* ScoreTabs::scoreEditor(const std::string& name) const {
    const int tab = scoreTabIndex(name);
    return tab > 0 ? scoreTabs_[static_cast<std::size_t>(tab - 1)].editor.data() : nullptr;
}

std::vector<std::pair<std::string, std::string>> ScoreTabs::unsavedScores() const {
    std::vector<std::pair<std::string, std::string>> unsaved;
    for (const ScoreTab& tab : scoreTabs_) {
        if (tab.editor && (tab.editor->isModified() || tab.editor->isNew()))
            unsaved.emplace_back(tab.name, tab.editor->content());
    }
    return unsaved;
}

void ScoreTabs::songTextChanged() {
    scoreSyncTimer_->start(); // a \lilypondfile may have been typed
    updateScoreWidthBars();
    fretboardTimer_->start(); // lang or key may have changed
}

std::filesystem::path ScoreTabs::scorePath(const std::string& name) const {
    if (songbookRoot_.empty() || !validScoreName(name))
        return {};
    return zub::songbook::findScoreFile(songbookRoot_,
                                        song_ ? song_->path() : std::filesystem::path(), name);
}

std::filesystem::path ScoreTabs::newScorePath(const std::string& name) const {
    // Beside the song: in its own collection (songs/tinwhistle for a tin whistle song).
    const std::string collection =
        song_ ? zub::collectionOf(songbookRoot_, song_->path()).value_or("chords") : "chords";
    return songbookRoot_ / "songs" / collection / pathOfName(name);
}

int ScoreTabs::scoreTabIndex(const std::string& name) const {
    for (std::size_t i = 0; i < scoreTabs_.size(); ++i) {
        if (scoreTabs_[i].name == name)
            return static_cast<int>(i) + 1;
    }
    return -1;
}

void ScoreTabs::syncScoreTabs() {
    scoreSyncTimer_->stop();
    if (!song_)
        return;
    std::vector<std::string> references;
    for (std::string& name : zub::songbook::referencedFiles(song_->content())) {
        if (validScoreName(name) && std::ranges::find(references, name) == references.end())
            references.push_back(std::move(name));
    }
    std::erase_if(dismissedScores_, [&](const std::string& name) {
        return std::ranges::find(references, name) == references.end();
    });
    for (const std::string& name : references) {
        if (scoreTabIndex(name) < 0 && !dismissedScores_.contains(name))
            addScoreTab(name);
    }
    // A tab whose reference is gone stays while there is something in it worth keeping.
    for (int index = static_cast<int>(scoreTabs_.size()); index >= 1; --index) {
        const ScoreTab& tab = scoreTabs_[static_cast<std::size_t>(index - 1)];
        if (std::ranges::find(references, tab.name) != references.end())
            continue;
        if (!tab.editor || (!tab.editor->isModified() && !tab.editor->isNew()))
            dropScoreTab(index);
    }
}

void ScoreTabs::addScoreTab(const std::string& name, SongEditor* editor) {
    auto* page = new QStackedWidget(tabs_);
    // Index 0: the notice of a missing (or unreadable) file. Index 1: the editor, with room
    // under it for the fretboard.
    auto* notice = new QWidget(page);
    auto* noticeLayout = new QVBoxLayout(notice);
    auto* noticeText = new QLabel(notice);
    noticeText->setObjectName(QStringLiteral("scoreNotice"));
    noticeText->setWordWrap(true);
    noticeText->setAlignment(Qt::AlignCenter);
    auto* create = new QPushButton(tr("Create It"), notice);
    create->setObjectName(QStringLiteral("scoreCreate"));
    noticeLayout->addStretch();
    noticeLayout->addWidget(noticeText);
    noticeLayout->addWidget(create, 0, Qt::AlignCenter);
    noticeLayout->addStretch();
    page->addWidget(notice);
    auto* splitter = new QSplitter(Qt::Vertical, page);
    auto* widthBar = new QWidget(splitter);
    widthBar->setObjectName(QStringLiteral("scoreWidthBar"));
    auto* widthLayout = new QHBoxLayout(widthBar);
    widthLayout->setContentsMargins(4, 2, 4, 2);
    widthLayout->addWidget(new QLabel(tr("Width:"), widthBar));
    auto* fullButton = new QRadioButton(tr("Full"), widthBar);
    fullButton->setObjectName(QStringLiteral("scoreWidthFull"));
    auto* narrowButton = new QRadioButton(tr("Narrow"), widthBar);
    narrowButton->setObjectName(QStringLiteral("scoreWidthNarrow"));
    fullButton->setToolTip(tr("The score spans the whole width of the page (\\lilypondfile)."));
    narrowButton->setToolTip(
        tr("On A4 the score is narrowed to 80 % of the width, so that it does not run into the "
           "song's details on the right (chord diagrams, tuning, tempo). On A5 it is full width "
           "anyway (\\narrowlilypondfile)."));
    widthLayout->addWidget(fullButton);
    widthLayout->addWidget(narrowButton);
    widthLayout->addStretch();
    splitter->addWidget(widthBar);
    splitter->setCollapsible(0, false);
    splitter->setStretchFactor(0, 0);
    connect(fullButton, &QRadioButton::clicked, this, [this, name] { setScoreWidth(name, false); });
    connect(narrowButton, &QRadioButton::clicked, this,
            [this, name] { setScoreWidth(name, true); });
    // Under the editor: the input panel {input bar, fretboard}.
    auto* inputPanel = new QWidget(splitter);
    inputPanel->setObjectName(QStringLiteral("scoreInputPanel"));
    auto* panelLayout = new QVBoxLayout(inputPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);
    auto* inputBar = new ScoreInputBar(inputPanel);
    inputBar->setObjectName(QStringLiteral("scoreInputBar"));
    auto* fretboard = new FretboardWidget(inputPanel);
    fretboard->setObjectName(QStringLiteral("scoreFretboard"));
    panelLayout->addWidget(inputBar);
    panelLayout->addWidget(fretboard);
    inputBar->setRegionActions(regionActions_);
    inputPanel->setVisible(fretboardAction_->isChecked());
    splitter->addWidget(inputPanel);
    // The toggle for the panel sits in the width bar (the tab's tool row).
    auto* fretboardButton = new QToolButton(widthBar);
    fretboardButton->setObjectName(QStringLiteral("scoreFretboardToggle"));
    fretboardButton->setDefaultAction(fretboardAction_);
    fretboardButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    widthLayout->addWidget(fretboardButton);
    auto* soundButton = new QToolButton(widthBar);
    soundButton->setObjectName(QStringLiteral("scoreSoundToggle"));
    soundButton->setDefaultAction(soundAction_);
    soundButton->setToolButtonStyle(QIcon::hasThemeIcon(QStringLiteral("audio-volume-high"))
                                        ? Qt::ToolButtonTextBesideIcon
                                        : Qt::ToolButtonTextOnly);
    widthLayout->addWidget(soundButton);
    connect(fretboard, &FretboardWidget::fretClicked, this,
            [this, fretboard](int string, int fret) { soundFret(fretboard, string, fret); });
    // Insert and Clear work anywhere on the tab while something is staged.
    auto* insertAction = new QAction(tr("Insert Staged Notes"), page);
    insertAction->setObjectName(QStringLiteral("scoreInsertStaged"));
    insertAction->setShortcuts(
        {QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)});
    insertAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    insertAction->setEnabled(false);
    auto* clearAction = new QAction(tr("Clear Staged Notes"), page);
    clearAction->setObjectName(QStringLiteral("scoreClearStaged"));
    clearAction->setShortcut(QKeySequence(Qt::Key_Escape));
    clearAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    clearAction->setEnabled(false);
    page->addAction(insertAction);
    page->addAction(clearAction);
    connect(insertAction, &QAction::triggered, this,
            [this, fretboard] { insertStagedNotes(fretboard); });
    connect(clearAction, &QAction::triggered, fretboard, &FretboardWidget::clearStaged);
    connect(fretboard, &FretboardWidget::insertRequested, this,
            [this, fretboard] { insertStagedNotes(fretboard); });
    connect(inputBar, &ScoreInputBar::insertRequested, this,
            [this, fretboard] { insertStagedNotes(fretboard); });
    connect(inputBar, &ScoreInputBar::clearRequested, fretboard, &FretboardWidget::clearStaged);
    connect(inputBar, &ScoreInputBar::tuningChoiceChanged, this, &ScoreTabs::updateFretboard);
    connect(fretboard, &FretboardWidget::stagedChanged, this,
            [fretboard, inputBar, insertAction, clearAction] {
                const int count = static_cast<int>(fretboard->staged().size());
                inputBar->setStagedCount(count);
                insertAction->setEnabled(count > 0);
                clearAction->setEnabled(count > 0);
            });
    connect(inputBar, &ScoreInputBar::restRequested, this, [this] {
        if (SongEditor* current = currentEditor(); current && !onSongTab()) {
            const auto& tab = scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)];
            if (current->cursorInScoreComment()) {
                emit statusMessage(tr("The cursor is in a comment."), 5000);
                return;
            }
            current->insertScoreRest(toStdString(tab.inputBar->duration()));
            current->textEdit()->setFocus();
        }
    });
    connect(inputBar, &ScoreInputBar::barCheckRequested, this, [this] {
        if (SongEditor* current = currentEditor(); current && !onSongTab()) {
            if (current->cursorInScoreComment()) {
                emit statusMessage(tr("The cursor is in a comment."), 5000);
                return;
            }
            current->insertScoreBarCheck();
            current->textEdit()->setFocus();
        }
    });
    page->addWidget(splitter);

    const QString fileName = toQString(std::filesystem::path(pathOfName(name)).filename());
    const int index = tabs_->addTab(page, fileName);
    scoreTabs_.push_back({name, page, nullptr, inputBar, fretboard, inputPanel, {}});
    connect(create, &QPushButton::clicked, this, [this, name] { createMissingScore(name); });

    if (!editor) {
        if (const auto path = scorePath(name); !path.empty()) {
            editor = new SongEditor(path, repository_, nullptr, SongEditor::Kind::Score);
            if (auto loaded = editor->load(); !loaded) {
                delete editor;
                editor = nullptr;
                noticeText->setText(
                    tr("%1 cannot be opened: %2").arg(toQString(name), loaded.error()));
                create->hide();
            }
        } else {
            noticeText->setText(tr("%1 does not exist yet.").arg(toQString(name)));
            tabs_->setTabText(index, tr("%1 (missing)").arg(fileName));
        }
    }
    if (editor)
        attachScoreEditor(index, editor);
    updateScoreWidthBars();
}

void ScoreTabs::updateScoreWidthBars() {
    const bool enabled = song_ && !song_->textEdit()->isReadOnly();
    const std::string content = song_ ? song_->content() : std::string();
    for (const ScoreTab& tab : scoreTabs_) {
        auto* full = tab.page->findChild<QRadioButton*>(QStringLiteral("scoreWidthFull"));
        auto* narrow = tab.page->findChild<QRadioButton*>(QStringLiteral("scoreWidthNarrow"));
        if (!full || !narrow)
            continue;
        const auto isNarrow = zub::songedit::scoreIsNarrow(content, tab.name);
        // Radio buttons in one parent are exclusive on their own.
        full->setChecked(isNarrow == false);
        narrow->setChecked(isNarrow == true);
        full->setEnabled(enabled && isNarrow.has_value());
        narrow->setEnabled(enabled && isNarrow.has_value());
    }
}

void ScoreTabs::setScoreWidth(const std::string& name, bool narrow) {
    if (song_)
        emit songTextReplaceRequested(zub::songedit::setScoreWidth(song_->content(), name, narrow));
    updateScoreWidthBars();
    emit songPdfRequested();
}

void ScoreTabs::attachScoreEditor(int tabIndex, SongEditor* editor) {
    ScoreTab& tab = scoreTabs_[static_cast<std::size_t>(tabIndex - 1)];
    auto* page = qobject_cast<QStackedWidget*>(tab.page);
    auto* splitter = page->widget(1) ? qobject_cast<QSplitter*>(page->widget(1)) : nullptr;
    editor->setWindowTitle(toQString(editor->path().filename()));
    splitter->insertWidget(1, editor); // under the width bar
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0); // the input panel keeps its height
    splitter->setCollapsible(2, false);
    page->setCurrentWidget(splitter);
    tab.editor = editor;
    tab.fretboard->clearStaged(); // another file's score
    tabs_->setTabText(tabIndex, toQString(editor->path().filename()) +
                                    (editor->isModified() ? QStringLiteral("*") : QString()));

    connect(editor, &SongEditor::modificationChanged, this, [this, editor] {
        updateScoreTabTitle(editor);
        emit windowTitleChanged();
        emit actionsChanged();
    });
    connect(editor, &SongEditor::saved, this, [this] { emit songPdfRequested(); });
    connect(editor, &SongEditor::reloadedFromDisk, this, [this, editor] {
        emit statusMessage(
            tr("%1 changed on disk and was reloaded.").arg(toQString(editor->path().filename())),
            5000);
        if (currentEditor() == editor)
            requestScorePreview();
        emit songPdfRequested();
    });
    QPlainTextEdit* edit = editor->textEdit();
    editor->setRegionMenuActions(regionActions_);
    connect(edit, &QPlainTextEdit::selectionChanged, this, [this, editor] {
        if (currentEditor() == editor) {
            updateRegionActions();
            highlightTimer_->start();
        }
    });
    connect(edit, &QPlainTextEdit::textChanged, this, [this, editor] {
        pdfStale_ = true;
        if (currentEditor() == editor) {
            scoreLinksFresh_ = false; // the note positions of the preview are of an older text
            endedAt_.reset();
            if (following_)
                stopPlayback(); // an edit makes the timeline stale: stop, clear the marks
            scorePreview_->setHighlightedNotes({});
            scorePreview_->setSelectableNotes({});
            scorePreview_->setCaretNote({});
            scoreStops_.reset(); // the keys' stops and anchor were of the old text
            scoreAnchor_.reset();
            scorePreviewTimer_->start();
            fretboardTimer_->start();
        }
    });
    connect(edit, &QPlainTextEdit::cursorPositionChanged, this, [this, editor] {
        if (currentEditor() == editor) {
            fretboardTimer_->start();
            highlightTimer_->start(); // the score cursor follows the text cursor
        }
    });
    connect(edit, &QPlainTextEdit::undoAvailable, this, [this] { emit editStateChanged(); });
    connect(edit, &QPlainTextEdit::redoAvailable, this, [this] { emit editStateChanged(); });
    connect(edit, &QPlainTextEdit::copyAvailable, this, [this] { emit editStateChanged(); });
}

void ScoreTabs::updateScoreTabTitle(const SongEditor* editor) {
    for (std::size_t i = 0; i < scoreTabs_.size(); ++i) {
        if (scoreTabs_[i].editor == editor) {
            tabs_->setTabText(static_cast<int>(i) + 1,
                              toQString(editor->path().filename()) +
                                  (editor->isModified() ? QStringLiteral("*") : QString()));
        }
    }
}

void ScoreTabs::createMissingScore(const std::string& name) {
    const int index = scoreTabIndex(name);
    if (index < 0 || scoreTabs_[static_cast<std::size_t>(index - 1)].editor)
        return;
    auto* editor =
        new SongEditor(newScorePath(name), repository_, nullptr, SongEditor::Kind::Score);
    const auto skeleton = zub::score::newScore();
    editor->openNew(skeleton.text);
    editor->setCursorByteOffset(skeleton.cursor);
    attachScoreEditor(index, editor);
    tabs_->setCurrentIndex(index);
}

void ScoreTabs::dropScoreTab(int tabIndex) {
    if (tabIndex < 1 || tabIndex > static_cast<int>(scoreTabs_.size()))
        return;
    ScoreTab tab = scoreTabs_[static_cast<std::size_t>(tabIndex - 1)];
    if (tab.editor) {
        tab.editor->stopWatching();
        disconnect(tab.editor, nullptr, this, nullptr);
        disconnect(tab.editor->textEdit(), nullptr, this, nullptr);
        emit scoreForgotten(tab.editor->path());
    }
    {
        // The tab widget changes its current tab on removal; onTabChanged runs once, after.
        const QSignalBlocker blocker(tabs_);
        tabs_->removeTab(tabIndex);
        scoreTabs_.erase(scoreTabs_.begin() + (tabIndex - 1));
    }
    tab.page->hide();
    tab.page->deleteLater(); // the editor is its child
    onTabChanged();
    emit windowTitleChanged();
    emit actionsChanged();
}

void ScoreTabs::closeScoreTab(int tabIndex) {
    if (tabIndex < 1 || tabIndex > static_cast<int>(scoreTabs_.size()))
        return;
    // The question runs a nested event loop: tabs may be added or dropped meanwhile (syncing with
    // the song's references, a reload from disk), so keep the name, not the tab or its index.
    const std::string name = scoreTabs_[static_cast<std::size_t>(tabIndex - 1)].name;
    const QPointer<SongEditor> score = scoreTabs_[static_cast<std::size_t>(tabIndex - 1)].editor;
    if (score && !confirmCloseScore(name, score))
        return;
    tabIndex = scoreTabIndex(name);
    if (tabIndex < 1)
        return; // gone while the question was open
    // Still in the song: it stays closed until the reference goes and comes back.
    if (song_) {
        const auto references = zub::songbook::referencedFiles(song_->content());
        if (std::ranges::find(references, name) != references.end())
            dismissedScores_.insert(name);
    }
    dropScoreTab(tabIndex);
    emit songPdfRequested(); // an unsaved score that was discarded must not stay in the PDF
}

void ScoreTabs::removeScoreTabs() {
    stopPlayback();
    scoreSyncTimer_->stop();
    scorePreviewTimer_->stop();
    scoreRunner_->cancel();
    scorePreview_->clear();
    dismissedScores_.clear();
    pdfStale_ = false;
    {
        const QSignalBlocker blocker(tabs_);
        for (const ScoreTab& tab : scoreTabs_) {
            if (tab.editor) {
                tab.editor->stopWatching();
                disconnect(tab.editor, nullptr, this, nullptr);
                disconnect(tab.editor->textEdit(), nullptr, this, nullptr);
                emit scoreForgotten(tab.editor->path());
            }
            tab.page->hide();
            tab.page->deleteLater();
        }
        while (tabs_->count() > 1)
            tabs_->removeTab(1);
        scoreTabs_.clear();
        tabs_->setCurrentIndex(0);
    }
    previewStack_->setCurrentWidget(songPreview_);
}

void ScoreTabs::onTabChanged() {
    stopPlayback(); // another score or the song: the sound belongs to the score that was shown
    scoreMidi_.clear();
    const bool song = tabs_->currentIndex() <= 0;
    previewStack_->setCurrentWidget(song ? songPreview_ : scorePreview_);
    scorePreviewTimer_->stop();
    scoreLinksFresh_ = false;
    scorePreview_->setSelectableNotes({});
    scorePreview_->setCaretNote({});
    scoreStops_.reset(); // the keys' stops and anchor belong to the tab that was shown
    scoreAnchor_.reset();
    if (song) {
        scoreRunner_->cancel();
        if (pdfStale_ && song_)
            emit songPdfRequested();
    } else {
        scorePreview_->clear();
        requestScorePreview();
    }
    emit actionsChanged();
    updateFretboard();
    if (SongEditor* editor = currentEditor())
        editor->textEdit()->setFocus();
}

std::optional<zub::scoreregion::Range> ScoreTabs::singleSelectedEvent(SongEditor* editor) const {
    const auto selection = editor->selectedByteRange();
    if (!selection)
        return std::nullopt;
    std::optional<zub::scoreregion::Range> found;
    for (const auto& event : zub::scoreregion::musicEvents(editor->editorText())) {
        if (event.begin < selection->second && selection->first < event.end) {
            if (found)
                return std::nullopt;
            found = event;
        }
    }
    return found;
}

void ScoreTabs::updateFretboard() {
    fretboardTimer_->stop();
    if (onSongTab() || !song_)
        return;
    const auto index = static_cast<std::size_t>(tabs_->currentIndex() - 1);
    if (index >= scoreTabs_.size())
        return;
    ScoreTab& tab = scoreTabs_[index];
    if (!tab.editor || !tab.fretboard)
        return;
    // The score's own text decides the tuning; the song's lang and key the note names.
    const std::string text = tab.editor->editorText();
    const auto meta = zub::parseSong(song_->content());
    const bool german = meta && zub::score::germanNames(meta->value("lang"));
    const bool flats = meta && zub::fretboard::prefersFlats(meta->value("key"));
    // Automatic: the tuning of the staff at the cursor; the combo can force one.
    zub::fretboard::Tuning tuning;
    switch (tab.inputBar->tuningChoice()) {
    case ScoreInputBar::TuningChoice::Guitar:
        tuning = zub::fretboard::guitar();
        break;
    case ScoreInputBar::TuningChoice::Banjo:
        tuning = zub::fretboard::banjoOpenG();
        break;
    default:
        tuning = zub::fretboard::tuningAt(text, tab.editor->cursorByteOffset()).tuning;
        break;
    }
    if (tab.fretboard->tuning().open != tuning.open ||
        tab.fretboard->tuning().shortString != tuning.shortString)
        tab.replaceTarget.reset(); // a new tuning clears the staging
    tab.fretboard->setTuning(tuning);
    tab.fretboard->setNaming(german, flats);
    // Like a letter selected in a text: the rings show the one note or chord that is selected, and
    // its positions are staged so that they can be edited and replaced. A cursor between notes
    // shows nothing.
    const auto single = singleSelectedEvent(tab.editor);
    std::vector<zub::fretboard::Position> positions;
    if (single)
        positions = zub::fretboard::positionsOfEvent(tab.editor->editorText(), single->begin,
                                                     single->end, tuning, german);
    tab.fretboard->setMarked(positions);
    if (single && !positions.empty()) {
        if (tab.replaceTarget != single) {
            tab.fretboard->clearStaged();
            for (const auto& position : positions)
                tab.fretboard->stage(position.string, position.fret);
            tab.replaceTarget = single;
        }
    } else if (tab.replaceTarget) { // moved off the event the staging came from
        tab.fretboard->clearStaged();
        tab.replaceTarget.reset();
    }
    tab.inputBar->setReplaceMode(tab.replaceTarget.has_value());
    tab.inputBar->setDuration(toQString(tab.editor->effectiveDurationAtCursor()));
}

void ScoreTabs::insertStagedNotes(FretboardWidget* fretboard) {
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab() || fretboard->staged().empty())
        return;
    ScoreTab& tab = scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)];
    if (tab.fretboard != fretboard)
        return;
    const auto target = tab.replaceTarget;
    const bool replacing = target && singleSelectedEvent(editor) == target;
    if (!replacing && editor->cursorInScoreComment()) {
        emit statusMessage(tr("The cursor is in a comment."), 5000);
        return;
    }
    // From the lowest string (the highest number) to string 1, like the real scores.
    auto positions = fretboard->staged();
    std::ranges::sort(positions, std::greater<>{}, &zub::fretboard::Position::string);
    std::vector<std::string> tokens;
    for (const auto& position : positions)
        tokens.push_back(
            zub::fretboard::noteToken(fretboard->midiAt(position.string, position.fret),
                                      position.string, fretboard->german(), fretboard->flats()));
    std::string token = tokens.front();
    if (tokens.size() > 1) {
        token = "<";
        for (std::size_t i = 0; i < tokens.size(); ++i)
            token += (i ? " " : "") + tokens[i];
        token += ">";
    }
    if (replacing) {
        // Only the notes change; the length, ties and articulations of the event stay.
        const auto replacement =
            zub::fretboard::replacePitches(editor->editorText(), target->begin, target->end, token);
        if (!replacement) {
            emit statusMessage(tr("The selected note cannot be replaced."), 5000);
            return;
        }
        const std::size_t newEnd =
            target->end + replacement->text.size() - (replacement->end - replacement->begin);
        if (!editor->replaceScoreRange(replacement->begin, replacement->end, replacement->text,
                                       target->begin, newEnd))
            return;
        tab.replaceTarget = zub::scoreregion::Range{target->begin, newEnd};
        editor->textEdit()->setFocus();
        updateFretboard();
        return;
    }
    editor->insertScoreNote(token, toStdString(tab.inputBar->duration()));
    fretboard->clearStaged();
    editor->textEdit()->setFocus();
    updateFretboard(); // now: the next input must see the new cursor and text
}

void ScoreTabs::requestScorePreview(bool userAsked) {
    scorePreviewTimer_->stop();
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab() || songbookRoot_.empty()) {
        scoreRunner_->cancel();
        scorePreview_->clear();
        forgetScoreMidi();
        return;
    }
    const std::string name = scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)].name;
    std::string content = editor->content();
    showStaves(editor, name);

    // lilypond runs Scheme: a score is code, like a song.
    QString list;
    if (!hooks_.approveScoreRisks(editor->path(), zub::safety::lilypondRisks(content), userAsked,
                                  &list)) {
        scoreRunner_->cancel();
        forgetScoreMidi();
        scorePreview_->showBlocked(
            tr("Not built automatically: the score uses %1, which scores normally don't. "
               "Building runs it with full access to your files. Check the score, then "
               "press Rebuild to build it anyway.")
                .arg(list));
        return;
    }

    zub::score::Request request;
    request.songbookRoot = songbookRoot_;
    request.workDir = scoreWorkDir_;
    request.inputName = name;
    request.content = std::move(content);
    // The note names follow the song's language, like the songbook's LaTeX does.
    if (const auto meta = zub::parseSong(song_->content()))
        request.germanNames = zub::score::germanNames(meta->value("lang"));
    request.includeDir = packageDir_ / "lilypond";
    // The build also writes the score's MIDI file, for Play.
    zub::score::MidiOptions midi;
    midi.factor = scorePreview_->tempoFactor();
    midi.instrument = zub::score::midiInstrument(request.content);
    for (const QString& id : scorePreview_->staffChoices())
        midi.staffInstruments.push_back(toStdString(id));
    midi.chordInstrument = toStdString(scorePreview_->chordChoice());
    midi.melodyInstrument = toStdString(scorePreview_->melodyChoice());
    if (const auto meta = zub::parseSong(song_->content())) {
        midi.tempo = zub::score::songTempo(meta->value("tem"));
        midi.transpose = zub::score::songCapo(meta->value("cap")); // sounds as with the capo on
    }
    request.midi = std::move(midi);
    scoreRunner_->request(std::move(request));
}

// The staves of the score in the editor for the toolbar's instrument choices, with the session's
// choices of this score (cut or padded to the staves that are there now).
void ScoreTabs::showStaves(SongEditor* editor, const std::string& name) {
    const std::string text = editor->editorText();
    QList<ScorePreviewPane::StaffInfo> infos;
    int number = 0;
    for (const zub::score::Staff& staff : zub::score::staves(text)) {
        ++number;
        ScorePreviewPane::StaffInfo info;
        const bool tab = staff.type == "TabStaff";
        if (!staff.name.empty())
            info.label = toQString(staff.name);
        else if (tab && staff.tuning.find("banjo") != std::string::npos)
            info.label = tr("Staff %1 (banjo tab)").arg(number);
        else if (tab)
            info.label = tr("Staff %1 (tab)").arg(number);
        else
            info.label = tr("Staff %1").arg(number);
        info.fileInstrument = toQString(staff.instrument);
        infos.append(info);
    }
    QStringList choices = staffChoices_[name];
    while (choices.size() < infos.size())
        choices.append(QStringLiteral("auto"));
    while (choices.size() > infos.size())
        choices.removeLast();
    staffChoices_[name] = choices;
    const auto chordChoice = chordChoices_.find(name);
    const auto melodyChoice = melodyChoices_.find(name);
    scorePreview_->setStaves(
        infos, choices, zub::score::hasChordNames(text),
        chordChoice != chordChoices_.end() ? chordChoice->second : QStringLiteral("silent"),
        zub::score::hasHiddenMelody(text),
        melodyChoice != melodyChoices_.end() ? melodyChoice->second : QStringLiteral("voice"));
}

// "Save to file" of a staff: writes the chosen General MIDI instrument into the score as one edit.
void ScoreTabs::saveStaffInstrument(int staff, const QString& gmName) {
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab() || staff < 0)
        return;
    const auto change = zub::score::saveStaffInstrument(
        editor->editorText(), static_cast<std::size_t>(staff), toStdString(gmName));
    if (!change || !editor->replaceScoreText(change->text)) {
        emit statusMessage(
            tr("The instrument cannot be written into this score (read-only, or the staff's music "
               "is a bare variable)."),
            6000);
        return;
    }
    // The file says it now: back to "as the file" for this staff.
    const std::string name = scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)].name;
    QStringList& choices = staffChoices_[name];
    if (staff < choices.size())
        choices[staff] = QStringLiteral("auto");
    showStaves(editor, name);
    emit statusMessage(tr("Instrument written into the score; save the file to keep it."), 6000);
}

void ScoreTabs::updateSoundAvailability() {
    const SoundTools tools = findSoundTools();
    QString reason = tools.missingHint();
    if (reason.isEmpty() && scoreMidi_.empty())
        reason = tr("The score has no MIDI file yet: it is being built, or the build failed.");
    scorePreview_->setPlaybackAvailable(reason.isEmpty(), reason);
    if (soundAction_) {
        soundAction_->setEnabled(tools.available());
        soundAction_->setToolTip(tools.available() ? tr("Play the note when a fret is clicked")
                                                   : tools.missingHint());
    }
}

// The MIDI file of an earlier build must not be played when no build of the shown score follows.
void ScoreTabs::forgetScoreMidi() {
    stopPlayback();
    scoreMidi_.clear();
    updateSoundAvailability();
}

void ScoreTabs::stopPlayback() {
    playbackRunner_->cancel();
    scorePlayer_->stop();
    stopFollowing();
    scorePreview_->setPlayState(ScorePreviewPane::PlayState::Idle);
    scorePreview_->showPlaybackMessage({}, false);
}

// Plays the MIDI file of the latest successful preview build: that build was approved (the
// preview's gate), and playing runs only fluidsynth.
void ScoreTabs::playScore() {
    if (scoreMidi_.empty() || onSongTab())
        return;
    const SoundTools tools = findSoundTools();
    if (!tools.available()) {
        updateSoundAvailability();
        return;
    }
    PlaybackRequest request;
    request.midiFile = scoreMidi_;
    request.fluidsynth = tools.fluidsynth;
    request.soundfont = tools.soundfont;
    request.cacheDir = playbackCacheDir_;
    request.amp = scoreAmp_;
    request.ampMidiFile = scoreAmpMidi_;
    rememberPlayStart();
    stopFollowing();
    scorePlayer_->stop();
    playbackRunner_->request(std::move(request));
}

void ScoreTabs::showPlaybackResult(const PlaybackResult& result) {
    if (onSongTab() || !currentEditor())
        return;
    if (!result.success) {
        scorePreview_->setPlayState(ScorePreviewPane::PlayState::Idle);
        scorePreview_->showPlaybackMessage(result.message, true);
        return;
    }
    scorePreview_->setPlayState(ScorePreviewPane::PlayState::Playing);
    scorePreview_->showPlaybackMessage(tr("Playing"), false);
    playingWav_ = result.wav;
    endedAt_.reset();
    seekMs_ = -1;
    scorePlayer_->play(result.wav, std::llround(playFrom_.value_or(0.0) * 1000.0));
    startFollowing();
}

// The timeline of the latest build: the playback score's note list (when each note sounds and
// where it is written), mapped onto the music events of the editor's text and the MIDI file's
// tempo map. Without one Play starts at the beginning and the cursor stays where it is.
void ScoreTabs::buildTimeline(const ScoreResult& result) {
    timeline_ = {};
    SongEditor* editor = currentEditor();
    if (!scoreLinksFresh_ || !editor || result.notes.empty() || result.midi.empty())
        return;
    const auto notes = zub::readTextFile(result.notes);
    const auto copy = zub::readTextFile(result.scoreCopy);
    const auto midi = zub::readTextFile(result.midi);
    if (!notes || !copy || !midi)
        return;
    const std::string text = editor->editorText();
    auto built = zub::scoretiming::buildTimeline(
        {*notes, *midi, *copy, text, result.scoreCopy, result.generatedFromLine});
    if (built)
        timeline_ = std::move(*built);
}

// Where Play starts: the cursor's note (the start of a selection), its first pass if it is
// repeated; the beginning without a timeline (the text changed since the build) or when the cursor
// is before the music.
std::optional<double> ScoreTabs::startSeconds() const {
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab() || !scoreLinksFresh_ || timeline_.empty())
        return std::nullopt;
    const auto selection = editor->selectedByteRange();
    const std::size_t caret = selection ? selection->first : editor->cursorByteOffset();
    if (!selection && endedAt_ == caret)
        return 0.0; // the caret is where the end of the playback left it: again from the start
    return zub::scoretiming::startSeconds(timeline_, caret);
}

void ScoreTabs::startFollowing() {
    followOffset_.reset();
    following_ = scoreLinksFresh_ && !timeline_.empty();
    if (following_)
        followTimer_->start();
}

void ScoreTabs::stopFollowing() {
    following_ = false;
    seekMs_ = -1;
    followTimer_->stop();
    scorePreview_->setSoundingNotes({});
    if (markedEditor_)
        markedEditor_->setPlaybackMarks({});
    markedEditor_.clear();
}

void ScoreTabs::followTick() {
    const qint64 position = scorePlayer_->positionMs();
    // Just after a seek the player may still report the old position (or 0): wait for it.
    if (seekMs_ >= 0) {
        if (position >= seekMs_ - 50 || (seekGuard_.isValid() && seekGuard_.elapsed() > 500))
            seekMs_ = -1;
        else
            return;
    }
    if (position >= 0)
        followAt(static_cast<double>(position) / 1000.0);
}

// The cursor goes right behind the note that began last at `seconds` (a plain cursor: the preview's
// caret line stands behind that note, the orange boxes and the editor's marks show what sounds).
// Cue, cursor, marks and boxes all come from this one time. Following ends when the text changed
// (then the playback stops too: the timeline is stale). When the user moved the cursor or selected
// (a click in the preview or the editor, a key in the preview) the playback restarts from there,
// like Play at that caret (a selection: at its first note), and the following goes on.
void ScoreTabs::followAt(double seconds) {
    SongEditor* editor = currentEditor();
    if (!following_)
        return;
    if (!editor || onSongTab() || !scoreLinksFresh_) {
        stopFollowing();
        return;
    }
    if (followOffset_ && (editor->selectedByteRange() || editor->cursorByteOffset() != followOffset_)) {
        followOffset_.reset();
        if (const auto start = startSeconds(); start && !playingWav_.empty()) {
            playFrom_ = start;
            scorePlayer_->play(playingWav_, std::llround(*start * 1000.0));
            seekMs_ = std::llround(*start * 1000.0);
            seekGuard_.start();
            seconds = *start;
        }
    }
    showSounding(seconds);
    const auto* cue = zub::scoretiming::cueAt(timeline_, seconds);
    // Of the events that begin together (the latest in the text first) the caret goes behind the
    // last one the preview shows: a hidden melody's voice has no note there.
    if (cue && !eventLink_.empty()) {
        const auto shown = [this](const zub::scoretiming::Cue& c) {
            const auto event = zub::scoreregion::eventAt(scoreEvents_, c.event.begin);
            return event && eventLink_.contains(*event);
        };
        for (const auto* c = cue; c >= timeline_.cues.data() && std::abs(c->seconds - cue->seconds) < 1e-6;
             --c)
            if (shown(*c)) {
                cue = c;
                break;
            }
    }
    if (!cue || followOffset_ == cue->event.end)
        return;
    scoreAnchor_ = cue->event.end;
    scoreStops_.reset();
    editor->selectByteRange(cue->event.end, cue->event.end, false); // a cursor; keeps it in view
    followOffset_ = editor->cursorByteOffset();
    updateScoreHighlights(); // now, not on the highlight timer: the caret line and the boxes agree
}

// Everything that sounds at `seconds` in all parts (the staves' notes, chord names, the melody,
// lyric syllables): marked in the editor's text, and boxed in the preview on its links inside those
// texts (links that do not exist are skipped).
void ScoreTabs::showSounding(double seconds) {
    QList<QPoint> notes;
    QList<ScorePreviewPane::SoundingText> texts;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;
    for (const auto& sound : zub::scoretiming::soundingAt(timeline_, seconds)) {
        ranges.emplace_back(sound.range.begin, sound.range.end);
        auto link = std::ranges::lower_bound(linkOffsets_, sound.range.begin, {},
                                             [](const auto& l) { return l.first; });
        for (; link != linkOffsets_.end() && link->first < sound.range.end; ++link) {
            if (sound.kind == zub::scoretiming::Kind::Note ||
                sound.kind == zub::scoretiming::Kind::Melody)
                notes.append(link->second);
            else
                texts.append({link->second, static_cast<int>(sound.range.end - sound.range.begin)});
        }
    }
    scorePreview_->setSoundingNotes(notes, texts);
    if (SongEditor* editor = currentEditor()) {
        editor->setPlaybackMarks(ranges);
        markedEditor_ = editor;
    }
}

void ScoreTabs::soundFret(FretboardWidget* fretboard, int string, int fret) {
    if (!soundAction_->isChecked() || !soundAction_->isEnabled())
        return;
    const SoundTools tools = findSoundTools();
    const bool banjo = fretboard->tuning().shortString != 0;
    // Frets count from the capo, like the tablature: the note sounds as with the capo on.
    int capo = 0;
    if (song_)
        if (const auto meta = zub::parseSong(song_->content()))
            capo = zub::score::songCapo(meta->value("cap"));
    // The chosen instrument; "automatic" is the banjo for a banjo fretboard, else the steel guitar.
    int program = banjo ? zub::midi::programBanjo : zub::midi::programSteelGuitar;
    bool amp = false;
    // The staff the cursor is in (else the first tab staff, else the first): its choice, else the
    // instrument the file sets for it.
    if (SongEditor* scoreEditor = currentEditor(); scoreEditor && !onSongTab()) {
        const auto staves = zub::score::staves(scoreEditor->editorText());
        std::size_t at = staves.size();
        const std::size_t cursor = scoreEditor->cursorByteOffset();
        for (std::size_t i = 0; i < staves.size() && at == staves.size(); ++i)
            if (staves[i].begin <= cursor && cursor < staves[i].end)
                at = i;
        for (std::size_t i = 0; i < staves.size() && at == staves.size(); ++i)
            if (staves[i].type == "TabStaff")
                at = i;
        if (at == staves.size() && !staves.empty())
            at = 0;
        if (at < staves.size()) {
            const QStringList choices = scorePreview_->staffChoices();
            const auto* chosen =
                at < static_cast<std::size_t>(choices.size())
                    ? zub::midi::findInstrument(toStdString(choices[static_cast<qsizetype>(at)]))
                    : nullptr;
            if (!chosen)
                chosen = zub::midi::findInstrumentByName(staves[at].instrument);
            if (chosen) {
                program = chosen->program;
                amp = chosen->amp;
            }
        }
    }
    noteSounds_->play(tools, program, fretboard->midiAt(string, fret) + capo, amp);
}

void ScoreTabs::showScoreResult(const ScoreResult& result) {
    // Only the score that is shown: the user may have moved on while it was built.
    if (onSongTab() || !currentEditor())
        return;
    if (scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)].name != result.inputName)
        return;
    scorePreview_->showResult(result);
    scoreMidi_ = result.midi;
    scoreAmp_ = result.amp; // of the build the MIDI file belongs to
    scoreAmpMidi_ = result.ampMidi;
    updateSoundAvailability();
    scoreLinksFresh_ = result.success && !scorePreviewTimer_->isActive();
    buildTimeline(result);
    indexScoreEvents();
    updateScoreHighlights();
}

// The music events of the score text the preview was built from, and which links of the preview
// belong to which event. Only links into music events are selectable: chord names, lyrics, marks
// and the definition of a variable (every use of it links there) are not.
void ScoreTabs::indexScoreEvents() {
    scoreEvents_.clear();
    linkEvent_.clear();
    eventLink_.clear();
    linkOffsets_.clear();
    scoreAnchor_.reset();
    SongEditor* editor = currentEditor();
    QList<QPoint> selectable;
    if (scoreLinksFresh_ && editor && !onSongTab()) {
        const std::string text = editor->editorText();
        scoreEvents_ = zub::scoreregion::musicEvents(text);
        for (const QPoint& position : scorePreview_->notePositions()) {
            const auto offset = zub::scoreregion::offsetOf(text, position.y(), position.x());
            if (offset)
                linkOffsets_.emplace_back(*offset, position);
            const auto event =
                offset ? zub::scoreregion::eventAt(scoreEvents_, *offset) : std::nullopt;
            if (!event)
                continue;
            linkEvent_[{position.y(), position.x()}] = *event;
            eventLink_.try_emplace(*event, position);
            selectable.append(position);
        }
    }
    scorePreview_->setSelectableNotes(selectable);
    std::ranges::sort(linkOffsets_, {}, [](const auto& link) { return link.first; });
}

void ScoreTabs::updateRegionActions() {
    SongEditor* editor = currentEditor();
    const bool enabled = editor && !onSongTab() && !editor->textEdit()->isReadOnly() &&
                         editor->selectedByteRange().has_value();
    for (QAction* action : regionActions_)
        action->setEnabled(enabled);
}

// The notes of the preview whose events the editor's selection touches get a highlight box; with
// no selection the score cursor stands where the text cursor is: behind the event that ends
// there, else before the event the cursor is in or before, else (past the last event) behind it.
void ScoreTabs::updateScoreHighlights() {
    highlightTimer_->stop();
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab() || !scoreLinksFresh_) {
        scorePreview_->setHighlightedNotes({});
        scorePreview_->setCaretNote({});
        return;
    }
    const auto range = editor->selectedByteRange();
    QList<QPoint> inside;
    QPoint caret;
    bool after = false;
    if (range) {
        std::vector<bool> touched(scoreEvents_.size());
        for (std::size_t i = 0; i < scoreEvents_.size(); ++i)
            touched[i] =
                scoreEvents_[i].begin < range->second && range->first < scoreEvents_[i].end;
        for (const auto& [key, event] : linkEvent_) {
            if (touched[event])
                inside.append(QPoint(key.second, key.first));
        }
    } else if (!scoreEvents_.empty()) {
        const std::size_t offset = editor->cursorByteOffset();
        std::optional<std::size_t> event;
        const auto ending =
            std::ranges::find_if(scoreEvents_, [offset](const auto& e) { return e.end == offset; });
        if (ending != scoreEvents_.end()) {
            event = static_cast<std::size_t>(ending - scoreEvents_.begin());
            after = true;
        } else if (event = zub::scoreregion::eventAt(scoreEvents_, offset); !event) {
            const auto next = std::ranges::find_if(
                scoreEvents_, [offset](const auto& e) { return e.begin >= offset; });
            if (next != scoreEvents_.end()) {
                event = static_cast<std::size_t>(next - scoreEvents_.begin());
            } else { // past the last event
                event = scoreEvents_.size() - 1;
                after = true;
            }
        }
        if (const auto link = event ? eventLink_.find(*event) : eventLink_.end();
            link != eventLink_.end())
            caret = link->second;
    }
    scorePreview_->setHighlightedNotes(inside);
    scorePreview_->setCaretNote(caret, after);
}

// The text position of a click on the note at `position`: its event's begin, or its end when the
// click was right of the note.
std::optional<std::size_t> ScoreTabs::caretOffsetOf(const QPoint& position, bool after) const {
    const auto found = linkEvent_.find({position.y(), position.x()});
    if (found == linkEvent_.end())
        return std::nullopt;
    const auto& event = scoreEvents_[found->second];
    return after ? event.end : event.begin;
}

// A click on a note of the rendered score: the text cursor goes before it (or behind it when the
// click was right of the note; the score cursor follows), with Shift the selection extends from
// where it started.
void ScoreTabs::clickScoreNote(const QPoint& position, bool after, bool extend) {
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab() || !scoreLinksFresh_)
        return;
    const auto offset = caretOffsetOf(position, after);
    if (!offset)
        return;
    if (extend) {
        selectScoreOffsets(scoreAnchor_.value_or(editor->cursorByteOffset()), *offset);
        return;
    }
    scoreAnchor_ = *offset;
    scoreStops_.reset();
    editor->setCursorByteOffset(*offset, false); // the preview keeps the keyboard
    updateScoreHighlights();
}

// Keys of the preview, like a text cursor over the music events of the score text (not over the
// links of the preview, which are stale after an edit): Left/Right move by one event, Shift
// selects, Home/End go to the start/end of the staff (Ctrl: of the score), Backspace/Delete
// delete the selected events or the one before/after the cursor, Ctrl+Z / Ctrl+Shift+Z undo.
void ScoreTabs::scoreKey(ScorePreviewPane::KeyCommand command, bool extend, bool wholeScore) {
    using Key = ScorePreviewPane::KeyCommand;
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab())
        return;
    if (command == Key::Undo || command == Key::Redo) {
        scoreStops_.reset(); // the text changes under the keys' stops
        scoreAnchor_.reset();
        if (command == Key::Undo)
            editor->textEdit()->undo();
        else
            editor->textEdit()->redo();
        return;
    }
    const std::string text = editor->editorText();
    const auto events = zub::scoreregion::musicEvents(text);
    if (events.empty())
        return;
    const std::size_t count = events.size();
    const std::size_t cursor = editor->cursorByteOffset();
    const auto selection = editor->selectedByteRange();
    const std::size_t low = selection ? selection->first : cursor;
    const std::size_t high = selection ? selection->second : cursor;

    if (command == Key::Backspace || command == Key::Delete) {
        std::optional<std::pair<std::size_t, std::size_t>> range = selection;
        if (!range && command == Key::Backspace) { // the event that ends at or before the cursor
            const std::size_t stop = zub::scoreregion::caretStop(events, cursor);
            if (stop > 0)
                range = std::make_pair(events[stop - 1].begin, events[stop - 1].end);
        } else if (!range) { // the event that ends after the cursor
            const auto next =
                std::ranges::find_if(events, [cursor](const auto& e) { return e.end > cursor; });
            if (next != events.end())
                range = std::make_pair(next->begin, next->end);
        }
        if (!range)
            return;
        zub::scoreregion::RemoveRefusal why = zub::scoreregion::RemoveRefusal::NothingToRemove;
        if (!editor->deleteScoreEvents(*range, &why) &&
            why == zub::scoreregion::RemoveRefusal::UnclosedChord)
            emit statusMessage(tr("A chord is not closed (a > is missing): nothing was deleted."),
                               6000);
        scoreStops_.reset();
        scoreAnchor_ = editor->cursorByteOffset();
        updateScoreHighlights();
        return;
    }

    // The stops the selection stands between: the keys' own when the selection is still theirs
    // (stops kept from an older text with more events give no span).
    std::size_t anchorStop = zub::scoreregion::caretStop(events, low);
    std::size_t activeStop = zub::scoreregion::caretStop(events, high);
    const auto kept =
        scoreStops_ ? zub::scoreregion::stopSpan(events, scoreStops_->first, scoreStops_->second)
                    : std::nullopt;
    if (kept && kept->begin == low && kept->end == high) {
        anchorStop = scoreStops_->first;
        activeStop = scoreStops_->second;
    } else if (!selection) {
        anchorStop = activeStop = zub::scoreregion::caretStop(events, cursor);
    }

    // Where the move goes: a stop, or (Home/End on a staff) an event's begin or end.
    std::size_t targetStop = activeStop;
    std::optional<std::size_t> targetOffset;
    if (command == Key::Left || command == Key::Right) {
        if (!extend && selection) { // like a text: collapse to the selection's side
            targetOffset = command == Key::Left ? low : high;
            targetStop = zub::scoreregion::caretStop(events, *targetOffset);
        } else {
            targetStop = command == Key::Left ? (activeStop > 0 ? activeStop - 1 : 0)
                                              : std::min(activeStop + 1, count);
        }
    } else {
        const bool end = command == Key::End;
        std::optional<std::size_t> event;
        if (!wholeScore) {
            if (const auto edge = scorePreview_->staffEdgeNote(end)) {
                if (const auto found = linkEvent_.find({edge->y(), edge->x()});
                    found != linkEvent_.end() && found->second < count)
                    event = found->second;
            }
        }
        if (!event)
            event = end ? count - 1 : 0;
        targetOffset = end ? events[*event].end : events[*event].begin;
        targetStop = end ? *event + 1 : *event;
    }

    if (extend) {
        const auto span = zub::scoreregion::stopSpan(events, anchorStop, targetStop);
        if (!span)
            return;
        scoreStops_ = std::make_pair(anchorStop, targetStop);
        scoreAnchor_ = zub::scoreregion::stopOffset(events, anchorStop);
        if (span->begin < span->end)
            editor->selectByteRange(span->begin, span->end, false);
        else
            editor->setCursorByteOffset(span->begin, false);
    } else {
        const std::size_t offset =
            targetOffset.value_or(zub::scoreregion::stopOffset(events, targetStop));
        scoreStops_.reset();
        scoreAnchor_ = offset;
        editor->setCursorByteOffset(offset, false);
    }
    updateScoreHighlights();
}

// A drag over the preview: the notes between where it started and the position of the mouse.
void ScoreTabs::selectScoreNotes(const QPoint& anchor, bool anchorAfter, const QPoint& current,
                                 bool currentAfter) {
    if (!scoreLinksFresh_)
        return;
    const auto a = caretOffsetOf(anchor, anchorAfter);
    const auto b = caretOffsetOf(current, currentAfter);
    if (a && b)
        selectScoreOffsets(*a, *b);
}

// Like a word in a text: a double-click selects exactly that event.
void ScoreTabs::doubleClickScoreNote(const QPoint& position) {
    SongEditor* editor = currentEditor();
    const auto found = linkEvent_.find({position.y(), position.x()});
    if (!editor || onSongTab() || !scoreLinksFresh_ || found == linkEvent_.end())
        return;
    const auto& event = scoreEvents_[found->second];
    scoreAnchor_ = event.begin;
    scoreStops_.reset();
    editor->selectByteRange(event.begin, event.end, false);
    updateScoreHighlights();
}

// The editor selects the events that lie between the two text positions (either order): whole
// notes and chords, in text order. The anchor stays where the selection started. Between two
// events, or at one position, only the text cursor goes there.
void ScoreTabs::selectScoreOffsets(std::size_t anchor, std::size_t current) {
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab())
        return;
    scoreAnchor_ = anchor;
    const auto [low, high] = std::minmax(anchor, current);
    std::optional<std::size_t> first, last;
    for (std::size_t i = 0; i < scoreEvents_.size(); ++i) {
        if (scoreEvents_[i].begin >= low && scoreEvents_[i].end <= high) {
            if (!first)
                first = i;
            last = i;
        }
    }
    scoreStops_.reset();
    if (first)
        editor->selectByteRange(scoreEvents_[*first].begin, scoreEvents_[*last].end, false);
    else
        editor->setCursorByteOffset(current, false);
    updateScoreHighlights();
}

void ScoreTabs::newScore() {
    if (!onSongTab() || song_->textEdit()->isReadOnly() || songbookRoot_.empty())
        return;
    const std::string inputName = hooks_.inputNameOf(song_);
    const auto slash = inputName.rfind('/');
    if (slash == std::string::npos) {
        emit statusMessage(tr("The song is not in an artist folder."), 5000);
        return;
    }
    const std::string folder = inputName.substr(0, slash + 1); // "Artist/"
    const std::string stem = toStdString(toQString(song_->path().stem()));
    const auto taken = [&](const std::string& file) {
        const std::string name = folder + file;
        return scoreTabIndex(name) >= 0 || !scorePath(name).empty();
    };
    // The file is <Song><Part>.ly (the songbook's naming): only the part name is asked for.
    QDialog dialog(tabs_->window());
    dialog.setWindowTitle(tr("New Score"));
    auto* part = new QLineEdit;
    part->setPlaceholderText(tr("e.g. Intro, Solo, Verse"));
    part->setToolTip(tr("The score's file is the song's file name followed by the part name. "
                        "Leave it empty for the song's only score."));
    auto* fileLabel = new QLabel;
    fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto* layout = new QFormLayout(&dialog);
    layout->addRow(tr("Part name:"), part);
    layout->addRow(fileLabel);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    std::string file;
    const auto check = [&] {
        // Accents go (NFD, then the combining marks dropped): "kytarové intro" -> KytaroveIntro.
        QString typed = part->text().normalized(QString::NormalizationForm_D);
        typed.removeIf([](QChar c) { return c.category() == QChar::Mark_NonSpacing; });
        const auto name = zub::score::partName(toStdString(typed));
        file.clear();
        QString message;
        if (!name) {
            message = tr("Use only letters, digits and spaces.");
        } else {
            file = zub::score::scoreFileName(stem, *name);
            if (!validScoreName(folder + file))
                message = tr("\"%1\" is not a valid name for a score file.")
                              .arg(toQString(folder + file));
            else if (taken(file))
                message = tr("%1 already exists.").arg(toQString(folder + file));
        }
        const bool ok = message.isEmpty();
        fileLabel->setText(ok ? tr("File: %1").arg(toQString(folder + file)) : message);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
        if (!ok)
            file.clear();
    };
    connect(part, &QLineEdit::textChanged, &dialog, check);
    if (taken(zub::score::scoreFileName(stem, "")))
        part->setText(QStringLiteral("Intro"));
    check();
    dialog.resize(std::max(dialog.sizeHint().width(), 420), dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted || file.empty())
        return;
    const std::string name = folder + file;

    auto* score = new SongEditor(newScorePath(name), repository_, nullptr, SongEditor::Kind::Score);
    const auto skeleton = zub::score::newScore();
    score->openNew(skeleton.text);
    score->setCursorByteOffset(skeleton.cursor);

    // The reference goes on a new line at the cursor, as one undo step of the song.
    QPlainTextEdit* edit = song_->textEdit();
    QTextCursor cursor = edit->textCursor();
    const QString reference =
        QStringLiteral("\\lilypondfile{") + toQString(name) + QLatin1Char('}');
    cursor.beginEditBlock();
    if (cursor.block().text().trimmed().isEmpty()) {
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.insertText(reference);
    } else {
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertText(QLatin1Char('\n') + reference);
    }
    cursor.endEditBlock();
    edit->setTextCursor(cursor);

    dismissedScores_.erase(name);
    addScoreTab(name, score);
    tabs_->setCurrentIndex(scoreTabIndex(name));
    score->textEdit()->setFocus();
    emit statusMessage(
        tr("New score %1. Save it with Ctrl+S to create the file.").arg(toQString(name)), 8000);
}
