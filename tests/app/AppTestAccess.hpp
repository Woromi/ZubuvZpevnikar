#pragma once

// The app tests' way into private members: the tested classes declare `friend struct
// AppTestAccess;`, this struct (global namespace) holds the accessors the tests use.
#include "ActivityBar.hpp"
#include "CodeTextEdit.hpp"
#include "CollectionTabs.hpp"
#include "ConverterRunner.hpp"
#include "MainWindow.hpp"
#include "PreviewPane.hpp"
#include "ScorePreviewPane.hpp"
#include "ScoreTabs.hpp"
#include "SongActions.hpp"
#include "SongEditor.hpp"
#include "SongListPage.hpp"
#include "SongbookCheck.hpp"
#include "SongbookTree.hpp"
#include "SongbooksPage.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <filesystem>
#include <zub/core/songsuggest.hpp>

struct AppTestAccess {
    // ---- CodeTextEdit
    static bool& blinkOn(CodeTextEdit& e) { return e.blinkOn_; }
    static QTimer* blinkTimer(CodeTextEdit& e) { return e.blinkTimer_; }

    // ---- MainWindow
    // The songbook without remembering it (the app's settings would otherwise point at it).
    static bool openSongbook(MainWindow& w, const std::filesystem::path& root) {
        return w.openSongbook(root, {.showErrors = false, .remember = false});
    }
    static void showView(MainWindow& w, ActivityBar::View view) { w.showView(view); }
    static SongbooksPage* songbooksPage(MainWindow& w) { return w.songbooksPage_; }
    static SongListPage* listPage(MainWindow& w) { return w.listPage_; }
    static SongFilterProxy* proxy(MainWindow& w) { return w.proxy_; }
    static SongTableModel* model(MainWindow& w) { return w.model_; }
    static SongEditor* editor(MainWindow& w) { return w.editor_; }
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
    static bool tidyRunning(MainWindow& w) {
        return w.songActions_->runKind_ == SongActions::RunKind::Tidy;
    }
    static QAction* tidyAction(MainWindow& w) { return w.songActions_->tidyAction_; }
    static QAction* checkAction(MainWindow& w) { return w.songActions_->checkAction_; }
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

    // ---- SongListPage
    static CollectionTabs* collections(SongListPage& p) { return p.collections_; }
    static QComboBox* language(SongListPage& p) { return p.language_; }

    // ---- SongbooksPage
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
    static QWidget* editPane(SongbooksPage& p) { return p.editPane_; }
    static QLabel* warning(SongbooksPage& p) { return p.warning_; }
    static QPushButton* addSectionButton(SongbooksPage& p) { return p.addSection_; }
    static QAction* check(SongbooksPage& p) { return p.check_; }
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
    static void checkBook(SongbooksPage& p) { p.checkBook(); }
    static void sortAll(SongbooksPage& p) { p.sortAll(); }
    static bool sorting(SongbooksPage& p) { return !p.jobs_.empty(); }
    static QLabel* sorterWarning(SongbooksPage& p) { return p.sorterWarning_; }
};
