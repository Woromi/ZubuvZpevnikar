#pragma once

// The app tests' way into private members: the tested classes declare `friend struct
// AppTestAccess;`, this struct (global namespace) holds the accessors the tests use.
#include "ActivityBar.hpp"
#include "CodeTextEdit.hpp"
#include "CollectionTabs.hpp"
#include "ConverterRunner.hpp"
#include "FindBar.hpp"
#include "MainWindow.hpp"
#include "PlaybackRunner.hpp"
#include "PreviewPane.hpp"
#include "ScorePlayer.hpp"
#include "ScorePreviewPane.hpp"
#include "ScoreTabs.hpp"
#include "SongActions.hpp"
#include "SongEditor.hpp"
#include "SongListPage.hpp"
#include "SongbookCheck.hpp"
#include "SongbookTree.hpp"
#include "LibraryDialog.hpp"
#include "AddToBooksDialog.hpp"
#include "BookAdder.hpp"
#include "BookOverview.hpp"
#include "HomePage.hpp"
#include "SongbookExporter.hpp"
#include "SongbooksPage.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <filesystem>
#include <zub/core/songsuggest.hpp>

struct AppTestAccess {
    // ---- CodeTextEdit
    static bool& blinkOn(CodeTextEdit& e) { return e.blinkOn_; }
    static int mainVirtualSpaces(const CodeTextEdit& e) { return e.mainVirtual_; }
    static QTimer* blinkTimer(CodeTextEdit& e) { return e.blinkTimer_; }

    // ---- FindBar
    static QLineEdit* findField(FindBar& b) { return b.find_; }
    static QLineEdit* replaceField(FindBar& b) { return b.replace_; }
    static QToolButton* matchCaseButton(FindBar& b) { return b.matchCase_; }
    static QToolButton* wholeWordButton(FindBar& b) { return b.wholeWord_; }
    static QToolButton* regexButton(FindBar& b) { return b.regex_; }
    static QPushButton* replaceButton(FindBar& b) { return b.replaceButton_; }
    static QPushButton* replaceAllButton(FindBar& b) { return b.replaceAllButton_; }

    // ---- MainWindow
    // The songbook without remembering it (the app's settings would otherwise point at it).
    static bool openSongbook(MainWindow& w, const std::filesystem::path& root) {
        return w.openSongbook(root, {.showErrors = false, .remember = false});
    }
    static void showView(MainWindow& w, ActivityBar::View view) { w.showView(view); }
    static SongbooksPage* songbooksPage(MainWindow& w) { return w.songbooksPage_; }
    static SongListPage* listPage(MainWindow& w) { return w.listPage_; }
    static void showTableMenu(MainWindow& w, const QPoint& at) { w.showTableMenu(at); }
    static HomePage* homePage(MainWindow& w) { return w.homePage_; }
    static const std::filesystem::path& songbookRoot(MainWindow& w) { return w.songbookRoot_; }
    static void refreshHome(MainWindow& w) { w.refreshHome(); }
    static void setPendingWish(MainWindow& w, const std::filesystem::path& song,
                               const HomeWish& wish) {
        w.pendingWish_ = MainWindow::PendingWish{song, wish};
    }
    static bool hasPendingWish(MainWindow& w) { return w.pendingWish_.has_value(); }
    static BookAdder* bookAdder(MainWindow& w) { return w.bookAdder_; }
    static QAction* addToBooksAction(MainWindow& w) { return w.addToBooksAction_; }
    // ---- HomePage
    static QListWidget* homeOpened(HomePage& p) { return p.opened_; }
    static QListWidget* homeAdded(HomePage& p) { return p.added_; }
    static QListWidget* homeTodo(HomePage& p) { return p.todo_; }
    static QListWidget* homeBooks(HomePage& p) { return p.books_; }
    static QTreeWidget* homeWishes(HomePage& p) { return p.wishes_; }
    static QPushButton* homeWishOpen(HomePage& p) { return p.wishOpen_; }
    static QWidget* homeAddedCard(HomePage& p) { return p.addedCard_; }
    static QLabel* homeSummary(HomePage& p) { return p.summary_; }
    // ---- LibraryDialog
    static QLineEdit* cloneUrl(LibraryDialog& d) { return d.url_; }
    static QLineEdit* cloneFolder(LibraryDialog& d) { return d.folder_; }
    static void cloneStart(LibraryDialog& d) { d.start(); }
    static SongFilterProxy* proxy(MainWindow& w) { return w.proxy_; }
    static SongTableModel* model(MainWindow& w) { return w.model_; }
    static SongEditor* editor(MainWindow& w) { return w.editor_; }
    static QAction* findAction(MainWindow& w) { return w.findAction_; }
    static QStackedWidget* pages(MainWindow& w) { return w.pages_; }
    static QWidget* songPage(MainWindow& w) { return w.songPage_; }
    static ActivityBar* activityBar(MainWindow& w) { return w.activityBar_; }
    static QTabWidget* tabs(MainWindow& w) { return w.tabs_; }
    static std::filesystem::path& packageDir(MainWindow& w) { return w.packageDir_; }
    static void openSong(MainWindow& w, const QModelIndex& proxyIndex) { w.openSong(proxyIndex); }
    static bool openExistingSong(MainWindow& w, const std::filesystem::path& path) {
        return w.openExistingSong(path);
    }
    static void newSong(MainWindow& w) { w.newSong(); }
    static void saveCurrent(MainWindow& w) { w.saveCurrent(); }
    static bool closeSong(MainWindow& w) { return w.closeSong(); }
    static void openSettings(MainWindow& w) { w.openSettings(); }
    static std::filesystem::path findConverter(MainWindow& w) { return w.findConverter(); }
    static void offerPapers(MainWindow& w, SongEditor* editor, const std::string& measuredText,
                            const zub::songsuggest::Suggestions& suggestions) {
        w.songActions_->offerPapers(editor, measuredText, suggestions);
    }

    static ScoreTabs* scores(MainWindow& w) { return w.scores_; }
    static void syncScoreTabs(MainWindow& w) { w.scores_->syncScoreTabs(); }
    static void removeScoreTabs(MainWindow& w) { w.scores_->removeScoreTabs(); }
    static std::vector<ScoreTabs::ScoreTab>& scoreTabs(MainWindow& w) {
        return w.scores_->scoreTabs_;
    }
    static std::optional<std::pair<std::size_t, std::size_t>>& scoreStops(MainWindow& w) {
        return w.scores_->scoreStops_;
    }
    static std::optional<std::size_t>& scoreAnchor(MainWindow& w) {
        return w.scores_->scoreAnchor_;
    }
    static void scoreKey(MainWindow& w, ScorePreviewPane::KeyCommand command, bool extend,
                         bool wholeScore) {
        w.scores_->scoreKey(command, extend, wholeScore);
    }
    static void insertStagedNotes(MainWindow& w, FretboardWidget* fretboard) {
        w.scores_->insertStagedNotes(fretboard);
    }
    static void newScore(MainWindow& w) { w.scores_->newScore(); }

    // Playback position: the player is replaced by a silent one, the timeline injected (no
    // lilypond, no audio device).
    static void replacePlayer(MainWindow& w, ScorePlayer* player) {
        delete w.scores_->scorePlayer_;
        w.scores_->scorePlayer_ = player;
        player->setParent(w.scores_);
        w.scores_->connectPlayer();
    }
    static void setScoreLinksFresh(MainWindow& w, bool fresh) {
        w.scores_->scoreLinksFresh_ = fresh;
    }
    static zub::scoretiming::Timeline& timeline(MainWindow& w) { return w.scores_->timeline_; }
    static std::optional<double> startSeconds(MainWindow& w) { return w.scores_->startSeconds(); }
    static void showPlaybackResult(MainWindow& w, const PlaybackResult& result) {
        w.scores_->showPlaybackResult(result);
    }
    static void playFromCursor(MainWindow& w) { w.scores_->rememberPlayStart(); }
    static void showScoreResult(MainWindow& w, const ScoreResult& result) {
        w.scores_->showScoreResult(result);
    }
    static void followAt(MainWindow& w, double seconds) { w.scores_->followAt(seconds); }
    static bool following(MainWindow& w) { return w.scores_->following_; }
    static void stopPlayback(MainWindow& w) { w.scores_->stopPlayback(); }
    static QTimer* followTimer(MainWindow& w) { return w.scores_->followTimer_; }

    // The preview and the score preview
    static bool scoreLinksFresh(MainWindow& w) { return w.scores_->scoreLinksFresh_; }
    static ScorePreviewPane* scorePreview(MainWindow& w) { return w.scores_->scorePreview_; }
    static const std::vector<zub::scoreregion::Range>& scoreEvents(MainWindow& w) {
        return w.scores_->scoreEvents_;
    }
    static const std::map<std::size_t, QPoint>& eventLink(MainWindow& w) {
        return w.scores_->eventLink_;
    }
    static const std::map<std::pair<int, int>, std::size_t>& linkEvent(MainWindow& w) {
        return w.scores_->linkEvent_;
    }
    static const QList<QAction*>& regionActions(MainWindow& w) { return w.scores_->regionActions_; }
    static PreviewRunner* previewRunner(MainWindow& w) { return w.previewRunner_; }
    static void requestPreview(MainWindow& w) { w.requestPreview(true); }
    static qint64 buildFilesSize(MainWindow& w) { return w.buildFilesSize(); }
    static PreviewPane* preview(MainWindow& w) { return w.preview_; }
    static const std::filesystem::path& previewWorkDir(MainWindow& w) { return w.previewWorkDir_; }
    static const std::filesystem::path& scoreMidi(MainWindow& w) { return w.scores_->scoreMidi_; }
    static const std::filesystem::path& scoreAmpMidi(MainWindow& w) {
        return w.scores_->scoreAmpMidi_;
    }
    static bool scoreAmp(MainWindow& w) { return w.scores_->scoreAmp_; }
    static std::map<std::string, QStringList>& staffChoices(MainWindow& w) {
        return w.scores_->staffChoices_;
    }
    // Converter actions
    static ConverterRunner* converterRunner(MainWindow& w) {
        return w.songActions_->converterRunner_;
    }
    static void startMeasure(MainWindow& w, SongEditor* editor) {
        w.songActions_->startMeasure(editor);
    }
    static void startTidy(MainWindow& w) { w.songActions_->startTidy(); }
    static SongActions* songActions(MainWindow& w) { return w.songActions_; }
    static bool tidyRunning(MainWindow& w) {
        return w.songActions_->runKind_ == SongActions::RunKind::Tidy;
    }
    static QAction* tidyAction(MainWindow& w) { return w.songActions_->tidyAction_; }
    static QAction* checkAction(MainWindow& w) { return w.songActions_->checkAction_; }
    static QWidget* changesPanel(MainWindow& w) { return w.tidyPanel_; }
    static QString changesTitle(MainWindow& w) { return w.tidyTitle_->text(); }
    static int changesCount(MainWindow& w) { return w.tidyList_->count(); }
    static void scanSongbook(MainWindow& w) { w.songbookCheck_->start(); }
    static bool scanRunning(MainWindow& w) { return w.songbookCheck_->scan_ != nullptr; }
    static const std::map<std::filesystem::path, zub::songcheck::KeyFinding>&
    keyFindings(MainWindow& w) {
        return w.songActions_->keyFindings_;
    }

    // ---- ScorePreviewPane
    static auto& staves(ScorePreviewPane& p) { return p.staves_; }
    static QFrame* instrumentsPopup(ScorePreviewPane& p) { return p.instrumentsPopup_; }

    // ---- SongEditor
    static QLabel* suggestionLabel(SongEditor& e) { return e.suggestionLabel_; }
    static QPushButton* suggestionButton(SongEditor& e) { return e.suggestionButton_; }
    static QPushButton* suggestionOtherButton(SongEditor& e) { return e.suggestionOtherButton_; }

    // ---- PreviewPane
    static QLabel* status(PreviewPane& p) { return p.status_; }
    static QComboBox* classes(PreviewPane& p) { return p.classes_; }

    // ---- SongListPage
    static CollectionTabs* collections(SongListPage& p) { return p.collections_; }
    static QComboBox* language(SongListPage& p) { return p.language_; }

    static QComboBox* show(SongListPage& p) { return p.show_; }

    // ---- SongbooksPage
    static SongFilterProxy* pickerFilter(SongbooksPage& p) { return p.pickerFilter_; }
    static QComboBox* pickerShow(SongbooksPage& p) { return p.pickerShow_; }
    static QTableView* pickerView(SongbooksPage& p) { return p.pickerView_; }
    static QPushButton* addPicked(SongbooksPage& p) { return p.addPicked_; }
    static QPushButton* removePicked(SongbooksPage& p) { return p.removePicked_; }
    static QTreeWidget* list(SongbooksPage& p) { return p.list_; }
    static QTreeWidgetItem* listItemOf(SongbooksPage& p, const QString& id) {
        return p.listItemOf(id);
    }
    static SongbookTree* tree(SongbooksPage& p) { return p.tree_; }
    static QLineEdit* title(SongbooksPage& p) { return p.title_; }
    static QCheckBox* chorded(SongbooksPage& p) { return p.chorded_; }
    static QCheckBox* chordChart(SongbooksPage& p) { return p.chordChart_; }
    static zub::bookfile::Settings& settings(SongbooksPage& p) { return p.settings_; }
    static zub::songbook::Songbook& book(SongbooksPage& p) { return p.book_; }
    static QString& id(SongbooksPage& p) { return p.id_; }
    static int& section(SongbooksPage& p) { return p.section_; }
    static QTabWidget* bookTabs(SongbooksPage& p) { return p.bookTabs_; }
    static CodeTextEdit* latex(SongbooksPage& p) { return p.latex_; }
    static FindBar* latexFind(SongbooksPage& p) { return p.latexFind_; }
    static QWidget* editPane(SongbooksPage& p) { return p.editPane_; }
    static QLabel* warning(SongbooksPage& p) { return p.warning_; }
    static QComboBox* bookKey(SongbooksPage& p) { return p.bookKey_; }
    static QComboBox* transposeMode(SongbooksPage& p) { return p.transposeMode_; }
    static QLabel* transposeWarning(SongbooksPage& p) { return p.transposeWarning_; }
    static void setSongTranspose(SongbooksPage& p, const std::string& name,
                                 std::optional<int> semitones) {
        p.setSongTranspose(name, semitones);
    }
    static std::optional<BookPreview::BuildInput> composeBuild(SongbooksPage& p) {
        return p.composeBuild(p.text_);
    }
    static SongbookExporter* exporter(SongbooksPage& p) { return p.exporter_; }
    static BookPreview* bookPreview(SongbooksPage& p) { return p.preview_; }
    static QLabel* bookPreviewStatus(BookPreview& b) { return b.status_; }
    // The work dir of the book's last build.
    static std::filesystem::path bookWorkDir(BookPreview& b) { return b.buildingPdf_.parent_path(); }
    // Pretends the shown book was built (a PDF of its current text, without transposing).
    static void fakeBookBuild(BookPreview& b, const std::string& signature = {}) {
        auto& result = b.results_[b.id_];
        result.pdf = "%PDF-1.4 not a real one";
        result.text = b.text_;
        result.signature = signature;
        result.report = QStringLiteral("1 page");
        b.updateView();
    }
    static QPushButton* addSectionButton(SongbooksPage& p) { return p.addSection_; }
    static QPushButton* removeSongButton(SongbooksPage& p) { return p.removeSong_; }
    static QToolButton* sectionMore(SongbooksPage& p) { return p.sectionMore_; }
    static QAction* moveUpAction(SongbooksPage& p) { return p.moveUp_; }
    static QAction* moveDownAction(SongbooksPage& p) { return p.moveDown_; }
    static QPushButton* coverClear(SongbooksPage& p) { return p.coverClear_; }
    static QStackedWidget* stack(SongbooksPage& p) { return p.stack_; }
    static QPushButton* emptyNew(SongbooksPage& p) { return p.emptyNew_; }
    static QPushButton* buildPreviewButton(BookPreview& b) { return b.build_; }
    static BookOverview* overview(SongbooksPage& p) { return p.overview_; }
    static QWidget* latexPane(SongbooksPage& p) { return p.latexPane_; }
    static QTreeWidget* overviewTree(BookOverview& o) { return o.tree_; }
    static QLabel* overviewHeading(BookOverview& o) { return o.heading_; }
    static QPushButton* overviewRemove(BookOverview& o) { return o.remove_; }
    static bool canEdit(SongbooksPage& p) { return p.canEdit(); }
    static int currentSection(SongbooksPage& p) { return p.currentSection(); }
    static bool latexModified(SongbooksPage& p) { return p.latexModified(); }
    static void applyTitle(SongbooksPage& p) { emit p.title_->editingFinished(); }
    static void addSection(SongbooksPage& p) { p.addSection(); }
    static void renameSection(SongbooksPage& p, int i) { p.renameSection(i); }
    static void removeSection(SongbooksPage& p, int i) { p.removeSection(i); }
    static void moveSection(SongbooksPage& p, int delta) { p.moveSection(delta); }
    static void addSongs(SongbooksPage& p, int target, const std::vector<std::string>& names) {
        p.addSongs(target, names, -1);
    }
    static void toggleLock(SongbooksPage& p) { p.toggleLock(); }
    static void saveLatex(SongbooksPage& p) { p.saveLatex(); }
    static void revertLatex(SongbooksPage& p) { p.revertLatex(); }
    static void reloadList(SongbooksPage& p) { p.reloadList(); }
    static bool commit(SongbooksPage& p) { return p.commit(); }
    static void newSongbook(SongbooksPage& p) { p.newSongbook(); }
    static void duplicateSongbook(SongbooksPage& p) { p.duplicateSongbook(); }
    static void sortAll(SongbooksPage& p) { p.sortAll(); }
    static QTabWidget* rightTabs(SongbooksPage& p) { return p.rightTabs_; }
    static void undoEdit(SongbooksPage& p) { p.undo(); }
    static void redoEdit(SongbooksPage& p) { p.redo(); }
    static void removeSelectedSongs(SongbooksPage& p) { p.removeSelectedSongs(); }
    static bool sorting(SongbooksPage& p) { return !p.jobs_.empty(); }
    static QLabel* sorterWarning(SongbooksPage& p) { return p.sorterWarning_; }
};
