#include "MainWindow.hpp"

#include "BuildFiles.hpp"
#include "DesktopIntegration.hpp"
#include "DiffView.hpp"
#include "FindBar.hpp"
#include "ImportDialog.hpp"
#include "LabelFilterBox.hpp"
#include "LabelsDialog.hpp"
#include "NewSongDialog.hpp"
#include "PreviewPane.hpp"
#include "PreviewRunner.hpp"
#include "ScoreTabs.hpp"
#include "SettingsDialog.hpp"
#include "SongActions.hpp"
#include "SongEditor.hpp"
#include "SongFilterProxy.hpp"
#include "SongListPage.hpp"
#include "SongTableModel.hpp"
#include "SongbookExporter.hpp"
#include "SongDatesRunner.hpp"
#include "SongbookCheck.hpp"
#include "SongbookSorter.hpp"
#include "SongbooksPage.hpp"
#include "LibraryDialog.hpp"
#include "AddToBooksDialog.hpp"
#include "BookAdder.hpp"
#include "HomeData.hpp"
#include "HomePage.hpp"
#include "WelcomePage.hpp"
#include "SoundTools.hpp"
#include "ConverterVersion.hpp"
#include "ExternalTools.hpp"
#include "convert.hpp"

#include <KSyntaxHighlighting/Repository>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <system_error>
#include <utility>
#include <zub/core/fretboard.hpp>
#include <zub/core/latex.hpp>
#include <zub/core/library.hpp>
#include <zub/core/linediff.hpp>
#include <zub/core/midi.hpp>
#include <zub/core/package.hpp>
#include <zub/core/preview.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/score.hpp>
#include <zub/core/scoreregion.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/songcheck.hpp>
#include <zub/core/songedit.hpp>
#include <zub/core/songheader.hpp>
#include <zub/core/songimport.hpp>
#include <zub/core/songsuggest.hpp>
#include <zub/core/songtags.hpp>
#include <zub/core/songtidy.hpp>
#include <zub/core/songtodo.hpp>
#include <zub/core/textfile.hpp>

namespace {

const auto songbookPathKey = QStringLiteral("songbook/path");
const auto previewClassKey = QStringLiteral("preview/class");
// Was: the converter chosen in Settings or the import dialog (removed 2026-11-24; ZUB_CONVERTER).
const auto oldConverterKey = QStringLiteral("converter/path");
const auto geometryKey = QStringLiteral("window/geometry");
const auto languageKey = QStringLiteral("ui/language");
const auto soundfontKey = QStringLiteral("playback/soundfont"); // read by findSoundTools
const auto songSplitterKey = QStringLiteral("window/songSplitter");

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), repository_(&sharedRepository()) {
    listTitle_ = QApplication::applicationDisplayName() + QLatin1Char(' ') +
                 QApplication::applicationVersion();
    setWindowTitle(listTitle_);
    resize(1200, 800);

    createCentralWidget();
    createMenus();
    createSongToolBar();
    updateSongActions();

    // Back like in a browser: the mouse's back button (seen before any child widget handles
    // the press) and Alt+Left.
    qApp->installEventFilter(this);
    // Alt+Left is Back on the list and the Songbooks page only: on the song page it moves a
    // chord (Edit menu), and a key that closes the song while typing would be a trap anyway.
    auto* back = new QAction(this);
    back->setShortcut(QKeySequence::Back);
    connect(back, &QAction::triggered, this, &MainWindow::goBack);
    addAction(back);
    const auto updateBack = [this, back] {
        back->setEnabled(pages_->currentWidget() != songPage_);
    };
    connect(pages_, &QStackedWidget::currentChanged, this, updateBack);
    updateBack();

    // Labels rather than showMessage(): a temporary message is cleared by menu hover.
    countLabel_ = new QLabel(this);
    statusBar()->addWidget(countLabel_);
    issuesLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(issuesLabel_);
    updateStatus();

    // A converter remembered by older versions is no longer used: forget it.
    QSettings{}.remove(oldConverterKey);

    // Window size and pane widths as the user left them.
    const QSettings settings;
    restoreGeometry(settings.value(geometryKey).toByteArray());
    songSplitter_->restoreState(settings.value(songSplitterKey).toByteArray());

    // The first answers of `chords_converter --version` and (on Windows) of the Python probes
    // take seconds; asked now on a worker thread, Import and Check Songs find them ready.
    // Quitting meanwhile skips what has not begun (the app waits for the task at exit).
    QThreadPool::globalInstance()->start([converter = findConverter(), windowGone = windowGone_] {
        (void)externaltools::python();
        if (!converter.empty() && !windowGone->load())
            (void)converterVersion(converter);
    });
}

KSyntaxHighlighting::Repository& MainWindow::sharedRepository() {
    // Created once and never destroyed: a highlighter must never outlive its repository
    // ("Repository got deleted while a highlighter is still active"), and editors can be deleted
    // late (deleteLater) or after the window's members. KSyntaxHighlighting recommends one
    // repository per application.
    static auto* repository = new KSyntaxHighlighting::Repository();
    return *repository;
}

MainWindow::~MainWindow() {
    windowGone_->store(true);
    songActions_->shutdown();
    // Members are destroyed before QWidget deletes the children, so without this the editors
    // (and its highlighter) would outlive repository_. Disconnect first: no signal may reach
    // a half-destroyed window.
    scores_->shutdown();
    if (editor_) {
        disconnect(editor_, nullptr, this, nullptr);
        disconnect(editor_->textEdit(), nullptr, this, nullptr);
        delete editor_;
        editor_ = nullptr;
    }
    // The helpers are children and outlive the members (repository_, approvedConstructs_): no
    // signal of theirs may reach this half-destroyed window.
    disconnect(scores_, nullptr, this, nullptr);
    disconnect(songActions_, nullptr, this, nullptr);
    disconnect(songbookCheck_, nullptr, this, nullptr);
    songDates_->cancel();
    disconnect(songDates_, nullptr, this, nullptr);
}

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));

    QAction* open = file->addAction(tr("&Open Song Library…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &MainWindow::chooseSongbookFolder);

    QAction* reload = file->addAction(tr("&Reload"));
    reload->setShortcut(QKeySequence::Refresh);
    connect(reload, &QAction::triggered, this, &MainWindow::reloadSongbook);

    file->addSeparator();
    saveAction_ = file->addAction(tr("&Save Song"));
    saveAction_->setShortcut(QKeySequence::Save);
    saveAction_->setIcon(QIcon::fromTheme(QStringLiteral("document-save")));
    connect(saveAction_, &QAction::triggered, this, &MainWindow::saveCurrent);

    closeSongAction_ = file->addAction(tr("&Close Song"));
    closeSongAction_->setShortcut(QKeySequence::Close);
    closeSongAction_->setIcon(QIcon::fromTheme(QStringLiteral("go-previous")));
    closeSongAction_->setToolTip(tr("Close the song (Ctrl+W)"));
    connect(closeSongAction_, &QAction::triggered, this, &MainWindow::closeSong);

    QAction* newSongAction =
        file->addAction(QIcon::fromTheme(QStringLiteral("document-new")), tr("&New Song…"));
    newSongAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    newSongAction->setToolTip(tr("Start a song by hand: title, artist and language, then an empty "
                                 "verse"));
    connect(newSongAction, &QAction::triggered, this, &MainWindow::newSong);

    QAction* import =
        file->addAction(QIcon::fromTheme(QStringLiteral("document-import")), tr("&Import Song…"));
    import->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    import->setToolTip(tr("Convert a song from a chords website or a saved page (Ctrl+I)"));
    connect(import, &QAction::triggered, this, &MainWindow::importSong);

    file->addSeparator();
    QAction* settings =
        file->addAction(QIcon::fromTheme(QStringLiteral("configure")), tr("Se&ttings…"));
    settings->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    connect(settings, &QAction::triggered, this, &MainWindow::openSettings);
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"));
    QMenu* songs = menuBar()->addMenu(tr("&Songs"));
    QAction* scan =
        songs->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), tr("&Check Songs…"));
    scan->setToolTip(tr("Check every song (or the ones shown): what Tidy, Check Song or Make "
                        "Smaller would change, keys, metadata"));
    connect(scan, &QAction::triggered, this, [this] { songbookCheck_->start(); });
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    // Home's actions row: the same actions.
    QAction* newBook = new QAction(QIcon::fromTheme(QStringLiteral("document-new")),
                                   tr("New Song&book…"), this);
    newBook->setToolTip(tr("Make a new songbook on the Songbooks page"));
    connect(newBook, &QAction::triggered, this, [this] {
        showView(ActivityBar::View::Songbooks);
        if (pages_->currentWidget() == songbooksPage_)
            songbooksPage_->newSongbook();
    });
    QAction* another = new QAction(QIcon::fromTheme(QStringLiteral("document-open")),
                                   tr("Open Another Song Library…"), this);
    connect(another, &QAction::triggered, this, &MainWindow::chooseSongbookFolder);
    for (QAction* action : {import, newSongAction, newBook, scan, another})
        homePage_->addQuickAction(action);
    // The Songs view's heading has the same actions as buttons.
    listPage_->addHeaderAction(newSongAction);
    listPage_->addHeaderAction(import); // the scan is an occasional check: Songs menu only

    // View: the activity bar's views from the keyboard.
    QMenu* view = menuBar()->addMenu(tr("&View"));
    const auto addView = [this, view](const QString& text, const QList<QKeySequence>& keys,
                                      ActivityBar::View target) {
        QAction* action = view->addAction(text);
        action->setShortcuts(keys);
        connect(action, &QAction::triggered, this, [this, target] { showView(target); });
        return action;
    };
    addView(tr("&Home"), {QKeySequence(QStringLiteral("Alt+Home"))}, ActivityBar::View::Home);
    addView(tr("&Songs"), {QKeySequence(QStringLiteral("Ctrl+1"))}, ActivityBar::View::Songs);
    songViewAction_ = addView(tr("The Open S&ong"), {QKeySequence(QStringLiteral("Ctrl+2"))},
                              ActivityBar::View::Song);
    addView(tr("Song&books"),
            {QKeySequence(QStringLiteral("Ctrl+3")), QKeySequence(QStringLiteral("Ctrl+B"))},
            ActivityBar::View::Songbooks);

    createEditMenu();
}

void MainWindow::createCentralWidget() {
    model_ = new SongTableModel(this);
    proxy_ = new SongFilterProxy(this);
    proxy_->setSourceModel(model_);

    pages_ = new QStackedWidget(this);

    listPage_ = new SongListPage(model_, proxy_, pages_);
    connect(listPage_, &SongListPage::songActivated, this, &MainWindow::openSong);
    connect(listPage_, &SongListPage::favouriteClicked, this, &MainWindow::toggleFavourite);
    connect(listPage_, &SongListPage::contextMenuRequested, this, &MainWindow::showTableMenu);

    createPreview();

    songPage_ = new QWidget(pages_);
    auto* songLayout = new QVBoxLayout(songPage_);
    songLayout->setContentsMargins(0, 0, 0, 0);
    songLayout->setSpacing(0);
    // The editor goes in at index 0 of editorArea_ when a song opens (createSongToolBar adds
    // the toolbar).
    songSplitter_ = new QSplitter(Qt::Horizontal, songPage_);
    auto* editorHost = new QWidget(songSplitter_);
    auto* hostLayout = new QVBoxLayout(editorHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);
    // Tab "Song": the editor (or the Changes view) and the Tidy list. A score of the song gets a
    // tab of its own, after it.
    tabs_ = new QTabWidget(editorHost);
    tabs_->setDocumentMode(true);
    tabs_->tabBar()->setAutoHide(true); // no tab bar for a song without scores
    tabs_->setTabsClosable(true);
    auto* songTab = new QWidget(tabs_);
    auto* songTabLayout = new QVBoxLayout(songTab);
    songTabLayout->setContentsMargins(0, 0, 0, 0);
    songTabLayout->setSpacing(0);
    editorArea_ = new QStackedWidget(songTab);
    diffView_ = new DiffView(editorArea_);
    editorArea_->addWidget(diffView_);
    songTabLayout->addWidget(editorArea_, 1);
    tabs_->addTab(songTab, tr("Song"));
    tabs_->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);
    tabs_->tabBar()->setTabButton(0, QTabBar::LeftSide, nullptr);
    hostLayout->addWidget(tabs_, 1);

    tidyPanel_ = new QWidget(songTab);
    auto* tidyLayout = new QVBoxLayout(tidyPanel_);
    tidyLayout->setContentsMargins(4, 2, 4, 2);
    tidyLayout->setSpacing(2);
    auto* tidyHeader = new QHBoxLayout;
    tidyHeader->addWidget(new QLabel(tr("Tidy changed:"), tidyPanel_), 1);
    auto* tidyClose = new QToolButton(tidyPanel_);
    tidyClose->setText(QStringLiteral("×"));
    tidyClose->setAutoRaise(true);
    tidyClose->setToolTip(tr("Hide this list"));
    tidyHeader->addWidget(tidyClose);
    tidyLayout->addLayout(tidyHeader);
    tidyList_ = new QListWidget(tidyPanel_);
    tidyList_->setMaximumHeight(130);
    tidyLayout->addWidget(tidyList_);
    tidyPanel_->hide();
    songTabLayout->addWidget(tidyPanel_);
    connect(tidyClose, &QToolButton::clicked, this, &MainWindow::clearTidyChanges);
    connect(tidyList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { jumpToLine(item->data(Qt::UserRole).toInt()); });

    previewStack_ = new QStackedWidget(songSplitter_);
    previewStack_->addWidget(preview_);

    SongActions::Hooks actionHooks;
    actionHooks.findConverter = [this] { return findConverter(); };
    actionHooks.approveBuild = [this](SongEditor* editor, const std::string& content, bool askUser,
                                      const QString& yesText, const QString& noText) {
        return approveBuild(editor, content, askUser, nullptr, yesText, noText);
    };
    actionHooks.documentClass = [this] { return toStdString(preview_->documentClass()); };
    actionHooks.noPackageText = [this] { return noPackageText(); };
    songActions_ = new SongActions(this, std::move(actionHooks));
    SongbookCheck::Hooks checkHooks;
    checkHooks.findConverter = [this] { return findConverter(); };
    checkHooks.documentClass = [this] { return toStdString(preview_->documentClass()); };
    checkHooks.noPackageText = [this] { return noPackageText(); };
    checkHooks.openSong = [this] { return editor_; };
    songDates_ = new SongDatesRunner(this);
    connect(songDates_, &SongDatesRunner::datesReady, this,
            [this](const std::filesystem::path& root, const auto& dates) {
                if (root == songbookRoot_)
                    model_->setDates(root, dates);
            });
    // No git, or no repository there: no dates, and nothing to tell the user (the Recently added
    // choices stay disabled).
    connect(songDates_, &SongDatesRunner::failed, this, [this](const std::filesystem::path& root) {
        if (root == songbookRoot_)
            model_->clearDates();
    });
    songbookCheck_ = new SongbookCheck(this, model_, proxy_, listPage_, songActions_,
                                       std::move(checkHooks));
    // The results are in the song list: shown there (filtered), unless a song is being edited.
    connect(songbookCheck_, &SongbookCheck::finished, this, [this](int found, bool cancelled) {
        if (found > 0) {
            if (pages_->currentWidget() != songPage_)
                showView(ActivityBar::View::Songs);
        } else if (!cancelled) {
            QMessageBox::information(this, tr("Check Songs"),
                                     tr("No song with possible changes was found."));
        }
    });
    connect(songbookCheck_, &SongbookCheck::statusMessage, this,
            [this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
    connect(songActions_, &SongActions::statusMessage, this,
            [this](const QString& text, int timeoutMs) { statusBar()->showMessage(text, timeoutMs); });
    connect(songActions_, &SongActions::statusCleared, this, [this] { statusBar()->clearMessage(); });
    connect(songActions_, &SongActions::songTextReplaceRequested, this,
            [this](const std::string& text) { replaceSongText(text); });
    connect(songActions_, &SongActions::previewRequested, this, [this] { requestPreview(); });
    connect(songActions_, &SongActions::lineRequested, this, &MainWindow::jumpToLine);
    connect(songActions_, &SongActions::stateChanged, this, &MainWindow::updateSongActions);
    connect(songActions_, &SongActions::tidyChangesListed, this,
            [this](const std::vector<std::pair<int, QString>>& changes) {
                tidyList_->clear();
                for (const auto& [line, text] : changes) {
                    auto* item = new QListWidgetItem(text, tidyList_);
                    item->setData(Qt::UserRole, line);
                }
                tidyPanel_->setVisible(!changes.empty());
            });

    ScoreTabs::Hooks hooks;
    hooks.confirmClose = [this](SongEditor* editor) { return confirmClose(editor); };
    hooks.approveScoreRisks = [this](const std::filesystem::path& key,
                                     std::vector<std::string> risks, bool askUser,
                                     QString* unapproved) {
        return approveRisks({{key, {}, std::move(risks)}}, askUser, unapproved, {}, {}, true);
    };
    hooks.inputNameOf = [this](const SongEditor* editor) { return inputNameOf(editor); };
    scores_ = new ScoreTabs(tabs_, previewStack_, preview_, *repository_, scoreDirs_,
                            std::move(hooks), this);
    connect(scores_, &ScoreTabs::statusMessage, this, [this](const QString& text, int timeoutMs) {
        statusBar()->showMessage(text, timeoutMs);
    });
    connect(scores_, &ScoreTabs::songTextReplaceRequested, this,
            [this](const std::string& text) { replaceSongText(text); });
    connect(scores_, &ScoreTabs::songPdfRequested, this, [this] { requestPreview(); });
    connect(scores_, &ScoreTabs::actionsChanged, this, &MainWindow::updateSongActions);
    connect(scores_, &ScoreTabs::windowTitleChanged, this, &MainWindow::updateWindowTitle);
    connect(scores_, &ScoreTabs::editStateChanged, this, &MainWindow::updateEditState);
    connect(scores_, &ScoreTabs::scoreForgotten, this,
            [this](const std::filesystem::path& path) { approvedConstructs_.erase(path); });

    songSplitter_->addWidget(editorHost);
    songSplitter_->addWidget(previewStack_);
    // A collapsed editor would be saved in the state and every later start would open it hidden.
    songSplitter_->setCollapsible(0, false);
    songSplitter_->setStretchFactor(0, 1);
    songSplitter_->setStretchFactor(1, 1);
    songSplitter_->setSizes({1, 1});
    songLayout->addWidget(songSplitter_);

    songbookSorter_ = new SongbookSorter(this);
    bookAdder_ = new BookAdder(songbookSorter_, model_, this);
    connect(bookAdder_, &BookAdder::finished, this,
            [this](const QStringList& added, const QStringList& notes) {
                QStringList parts;
                // What the same Apply took out (the song moved between lists) stays in view.
                if (!takenOutMessage_.isEmpty())
                    parts << std::exchange(takenOutMessage_, QString());
                if (!added.isEmpty())
                    parts << tr("Added to %1.").arg(added.join(QStringLiteral(", ")));
                parts << notes;
                if (!parts.isEmpty())
                    statusBar()->showMessage(parts.join(QLatin1Char(' ')), 15000);
                if (!notes.isEmpty() && added.isEmpty())
                    QMessageBox::warning(this, tr("Add to Songbooks"),
                                         notes.join(QLatin1Char('\n')));
                // The page reads the books again (not over unsaved LaTeX: that book was skipped).
                if (songbooksPage_->bookWithUnsavedLatex().isEmpty())
                    songbooksPage_->reloadList();
                homeTimer_->start();
            });
    songbookExporter_ = new SongbookExporter(this);
    songbooksPage_ =
        new SongbooksPage(model_, songbookSorter_, songbookExporter_, *repository_, pages_);
    connect(songbooksPage_, &SongbooksPage::openSongRequested, this,
            [this](const std::filesystem::path& path) { openExistingSong(path); });
    connect(songbookExporter_, &SongbookExporter::finished, this,
            [this](const QString& message) { statusBar()->showMessage(message, 15000); });

    pages_->addWidget(listPage_);
    pages_->addWidget(songPage_);
    pages_->addWidget(songbooksPage_);
    homePage_ = new HomePage(pages_);
    pages_->addWidget(homePage_);
    WelcomePage* welcome = homePage_->welcome();
    connect(welcome, &WelcomePage::chooseRequested, this, &MainWindow::chooseSongbookFolder);
    connect(welcome, &WelcomePage::retryRequested, this, [this](const QString& path) {
        if (!openSongbook(toPath(path), {.showErrors = false}))
            showWelcome(path); // the page says why
        else
            showView(ActivityBar::View::Home);
    });
    connect(welcome, &WelcomePage::newLibraryRequested, this, &MainWindow::newLibrary);
    connect(homePage_, &HomePage::songRequested, this, [this](std::filesystem::path path) {
        if (openExistingSong(path))
            showView(ActivityBar::View::Song);
    });
    connect(homePage_, &HomePage::showAllRequested, this, [this](HomePage::ShowAll which) {
        showView(ActivityBar::View::Songs);
        listPage_->setShow(which == HomePage::ShowAll::Added ? SongListPage::Recent
                                                             : SongListPage::Todo);
    });
    connect(homePage_, &HomePage::bookRequested, this, [this](const QString& id) {
        showView(ActivityBar::View::Songbooks);
        if (songbooksPage_->showBook(id))
            songbooksPage_->showOverview(); // what the card's "N to fix" is about
    });
    connect(homePage_, &HomePage::wishSearchRequested, this, [this](HomeWish wish) {
        // Only on the user's click: the browser searches, the app contacts nothing itself.
        QDesktopServices::openUrl(wishSearchUrl(wish));
    });
    connect(homePage_, &HomePage::wishImportRequested, this,
            [this](HomeWish wish) { importSongFor(wish); });
    connect(homePage_, &HomePage::wishMoveRequested, this, [this](HomeWish wish) {
        // changeWish shows a failure itself (std::expected is [[nodiscard]] with MSVC).
        (void)changeWish(wish, [&] { return moveWishToList(songbookRoot_, wish); });
        refreshHome();
    });
    connect(homePage_, &HomePage::wishNewSongRequested, this,
            [this](HomeWish wish) { newSongFor(wish); });
    connect(homePage_, &HomePage::wishDoneRequested, this, [this](HomeWish wish) {
        (void)changeWish(wish,
                         [&] { return fulfilWish(songbookRoot_, wish, inputNameOfPath(wish.song)); });
        refreshHome();
    });
    connect(homePage_, &HomePage::wishRemoveRequested, this, [this](HomeWish wish) {
        const auto answer = QMessageBox::question(
            this, tr("Wishes"),
            tr("Remove the wish \"%1\"?").arg(wishDisplayText(wish.text, wish.name)));
        if (answer != QMessageBox::Yes)
            return;
        (void)changeWish(wish, [&] { return removeWish(songbookRoot_, wish); });
        refreshHome();
    });
    connect(homePage_, &HomePage::wishAddRequested, this, [this] {
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, tr("Add Wish"),
            tr("The song you want, e.g. Artist/Title (a note may follow), or any text:"),
            QLineEdit::Normal, {}, &ok);
        if (!ok || text.trimmed().isEmpty())
            return;
        if (auto added = addWish(songbookRoot_, text); !added)
            QMessageBox::warning(this, tr("Wishes"), added.error());
        refreshHome();
    });
    homeTimer_ = new QTimer(this);
    homeTimer_->setSingleShot(true);
    homeTimer_->setInterval(200);
    connect(homeTimer_, &QTimer::timeout, this, [this] {
        if (pages_->currentWidget() != homePage_ || songbookRoot_.empty())
            return;
        if (QApplication::activeModalWidget()) { // later: a dialog may be acting on Home's lists
            homeTimer_->start();
            return;
        }
        refreshHome();
    });
    connect(model_, &QAbstractItemModel::modelReset, homeTimer_, qOverload<>(&QTimer::start));
    connect(model_, &QAbstractItemModel::rowsInserted, homeTimer_, qOverload<>(&QTimer::start));
    connect(model_, &QAbstractItemModel::rowsRemoved, homeTimer_, qOverload<>(&QTimer::start));
    connect(model_, &QAbstractItemModel::dataChanged, homeTimer_, qOverload<>(&QTimer::start));
    connect(model_, &SongTableModel::datesChanged, homeTimer_, qOverload<>(&QTimer::start));

    // The activity bar at the left edge, like a code editor's: the views and Settings.
    activityBar_ = new ActivityBar(this);
    connect(activityBar_, &ActivityBar::viewRequested, this, &MainWindow::showView);
    connect(activityBar_, &ActivityBar::settingsRequested, this, &MainWindow::openSettings);
    auto* central = new QWidget(this);
    auto* centralLayout = new QHBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(activityBar_);
    centralLayout->addWidget(pages_, 1);
    setCentralWidget(central);

    connect(proxy_, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateStatus);
    connect(proxy_, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateStatus);
    connect(proxy_, &QAbstractItemModel::modelReset, this, &MainWindow::updateStatus);
    connect(proxy_, &QAbstractItemModel::layoutChanged, this, &MainWindow::updateStatus);

    updateLanguageFilter();
}

void MainWindow::openInitialSongbook(const QString& commandLinePath) {
    if (!commandLinePath.isEmpty()) {
        // For this run only: the remembered library stays for the next start.
        if (!openSongbook(toPath(commandLinePath), {.showErrors = false, .remember = false}))
            showWelcome(commandLinePath);
        else
            showView(ActivityBar::View::Home);
        viewHistory_.clear(); // the start: nothing to go back to
        return;
    }

    // Only the remembered folder: no guesses. If it is missing (e.g. an unplugged disk), the
    // welcome page says so; it stays remembered unless the user picks another one.
    const QString remembered = QSettings{}.value(songbookPathKey).toString();
    if (remembered.isEmpty()) {
        showWelcome();
        return;
    }
    const QString path = QDir::cleanPath(remembered);
    if (!openSongbook(toPath(path), {.showErrors = false, .remember = false}))
        showWelcome(path);
    else
        showView(ActivityBar::View::Home);
    viewHistory_.clear(); // the start: nothing to go back to
}

void MainWindow::showWelcome(const QString& path) {
    WelcomePage* welcomePage = homePage_->welcome();
    if (path.isEmpty()) {
        welcomePage->showFirstStart();
    } else {
        // Why, from the quick check alone (a second full scan would only repeat the first).
        const auto valid = zub::validateSongbookRepo(toPath(path));
        welcomePage->showMissing(path, valid ? QString() : toQString(valid.error()));
    }
    if (songbookRoot_.empty()) {
        homePage_->showWelcome();
        pages_->setCurrentWidget(homePage_);
        activityBar_->setCurrent(ActivityBar::View::Home);
        updateWindowTitle();
    }
}

void MainWindow::refreshHome() {
    homePage_->setData(buildHomeData(*model_, songbookRoot_, rememberedSongs(songbookRoot_)));
}

void MainWindow::newLibrary() {
    LibraryDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (openSongbook(toPath(dialog.folder()), {.closeEditors = true}))
        showView(ActivityBar::View::Home);
}

void MainWindow::chooseSongbookFolder() {
    const QString start = songbookRoot_.empty() ? QDir::homePath() : toQString(songbookRoot_);
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Open Song Library (ZubuvZpevnik)"), start);
    // Open editors belong to the old songbook; they are closed only once the new one is valid.
    if (!dir.isEmpty() && openSongbook(toPath(dir), {.closeEditors = true}))
        showView(ActivityBar::View::Home);
}

bool MainWindow::openSongbook(const std::filesystem::path& path, OpenOptions options) {
    // Absolute, so a remembered "--songbook ." still works when started from elsewhere.
    std::error_code ec;
    auto root = std::filesystem::absolute(path, ec).lexically_normal();
    if (ec)
        root = path;
    if (root.has_relative_path() && !root.has_filename())
        root = root.parent_path(); // drop a trailing separator

    auto library = zub::scanLibrary(root);
    if (!library) {
        if (options.showErrors) {
            QMessageBox::warning(
                this, tr("Not a song library"),
                tr("Cannot open the song library:\n%1").arg(toQString(library.error())));
        }
        return false;
    }
    if (options.closeEditors && (!closeSong() || !songbooksPage_->confirmClose()))
        return false;

    songbookRoot_ = root;
    // Always the app's own copy: a library's zubuvzpevnik/ would run with the user's rights
    // (lualatex -shell-escape) unchecked, so a library from someone else could bring its own.
    packageDir_ = bundledPackage();
    scores_->setSongbook(songbookRoot_, packageDir_);
    songActions_->setSongbook(songbookRoot_, packageDir_);
    songbookCheck_->setSongbook(songbookRoot_, packageDir_);
    songbooksPage_->setPackageDir(packageDir_);
    songbooksPage_->setSongbookRoot(root);
    songbookExporter_->setSongbookRoot(root, packageDir_);
    songbookSorter_->setTools(packageDir_,
                              QStandardPaths::findExecutable(QStringLiteral("texlua")));
    if (options.remember)
        QSettings{}.setValue(songbookPathKey, toQString(root));

    issueCount_ = static_cast<int>(library->issues.size());
    QStringList issueLines;
    for (const auto& issue : library->issues) {
        issueLines << toQString(issue.path.lexically_relative(root)) + QStringLiteral(": ") +
                          toQString(issue.message);
    }
    issuesLabel_->setToolTip(issueLines.join(QLatin1Char('\n')));

    model_->setSongs(std::move(library->songs));
    songDates_->start(root); // the dates arrive later (git, in the background)
    const auto collections = zub::collections(root); // Chords, Tin whistle, …
    listPage_->setCollections(collections);
    songbooksPage_->setCollections(collections);
    updateLanguageFilter();
    preview_->setDocumentClasses(
        zub::preview::documentClasses(packageDir_),
        // The main songbook's class unless another was chosen.
        QSettings{}.value(previewClassKey, QStringLiteral("velkyzpevnik")).toString());
    updateStatus();
    if (packageDir_.empty())
        statusBar()->showMessage(noPackageText(), 20000);
    listTitle_ = QApplication::applicationDisplayName() + QStringLiteral(" — ") + toQString(root);
    if (pages_->currentWidget() == homePage_)
        showView(ActivityBar::View::Home); // the cards of this songbook
    updateWindowTitle();
    return true;
}

void MainWindow::reloadSongbook() {
    if (!songbookRoot_.empty())
        openSongbook(songbookRoot_, {.showErrors = true, .remember = false});
}

void MainWindow::showSongbooks() {
    showView(ActivityBar::View::Songbooks);
}

std::optional<ActivityBar::View> MainWindow::currentView() const {
    using View = ActivityBar::View;
    const QWidget* page = pages_->currentWidget();
    if (page == homePage_)
        return View::Home;
    if (page == listPage_)
        return View::Songs;
    if (page == songPage_)
        return View::Song;
    if (page == songbooksPage_)
        return View::Songbooks;
    return std::nullopt;
}

void MainWindow::showView(ActivityBar::View view) {
    const auto from = currentView();
    switchView(view);
    const auto to = currentView();
    if (from && to && *from != *to) {
        viewHistory_.push_back(*from);
        if (viewHistory_.size() > 50)
            viewHistory_.erase(viewHistory_.begin());
    }
}

void MainWindow::switchView(ActivityBar::View view) {
    using View = ActivityBar::View;
    if (view == View::Song && !editor_)
        view = View::Songs;
    if (view == View::Songbooks) {
        if (songbookRoot_.empty()) { // nothing to assemble from: stay where we are
            activityBar_->setCurrent(pages_->currentWidget() == songPage_   ? View::Song
                                     : pages_->currentWidget() == homePage_ ? View::Home
                                                                            : View::Songs);
            return;
        }
        if (pages_->currentWidget() != songbooksPage_)
            songbooksPage_->reloadList();
    }
    // No songbook yet: Home's welcome panel stands in for the list.
    if (songbookRoot_.empty() && view == View::Songs)
        view = View::Home;
    if (view == View::Home) {
        if (songbookRoot_.empty())
            homePage_->showWelcome();
        else
            refreshHome();
        pages_->setCurrentWidget(homePage_);
        activityBar_->setCurrent(view);
        updateWindowTitle();
        return;
    }
    pages_->setCurrentWidget(view == View::Songs  ? static_cast<QWidget*>(listPage_)
                             : view == View::Song ? songPage_
                                                  : static_cast<QWidget*>(songbooksPage_));
    activityBar_->setCurrent(view);
    updateWindowTitle();
    if (view == View::Song) {
        if (SongEditor* editor = currentEditor())
            editor->textEdit()->setFocus();
    }
}

void MainWindow::updateLanguageFilter() {
    listPage_->updateLanguages();
}

void MainWindow::updateStatus() {
    if (songbookRoot_.empty()) {
        countLabel_->setText(tr("No song library open. Use File → Open Song Library…"));
        issuesLabel_->clear();
        return;
    }
    const int shown = proxy_->rowCount();
    const int total = model_->rowCount();
    const QString songs = tr("%n song(s)", nullptr, total);
    countLabel_->setText(shown == total ? songs : tr("%1 of %2").arg(shown).arg(songs));
    if (issueCount_ == 0)
        issuesLabel_->clear();
    else
        issuesLabel_->setText(
            tr("⚠ %n file(s) with issues (hover for details)", nullptr, issueCount_));
}

void MainWindow::openSong(const QModelIndex& proxyIndex) {
    const zub::SongEntry* song = model_->song(proxy_->mapToSource(proxyIndex).row());
    if (!song)
        return;
    // Copies: closing the song may change the model.
    const std::filesystem::path path = song->path;
    const QString title = toQString(zub::latex::toDisplayText(song->meta.title));
    const QString inputName = toQString(song->inputName);

    // The open song: just show it again (its edits and undo history stay).
    if (editor_ && editor_->path() == path) {
        showView(ActivityBar::View::Song);
        return;
    }
    if (!closeSong())
        return;
    if (SongEditor* editor = openEditor(path, title))
        showSong(editor, title, inputName);
}

SongEditor* MainWindow::openEditor(const std::filesystem::path& path, const QString& title) {
    auto* editor = new SongEditor(path, *repository_);
    if (auto loaded = editor->load(); !loaded) {
        delete editor;
        QMessageBox::warning(this, tr("Cannot open song"), loaded.error());
        return nullptr;
    }
    QString shownTitle = title;
    if (shownTitle.isEmpty()) {
        // A song whose header does not parse still opens, titled by its file name.
        const auto meta = zub::parseSong(editor->content());
        shownTitle =
            meta ? toQString(zub::latex::toDisplayText(meta->title)) : toQString(path.stem());
    }
    editor->setWindowTitle(shownTitle);
    return editor;
}

void MainWindow::noteSongOpenedFrom() {
    if (const auto view = currentView(); view && *view != ActivityBar::View::Song)
        songOpenedFrom_ = *view;
}

void MainWindow::showSong(SongEditor* editor, const QString& title, const QString& toolTip) {
    noteSongOpenedFrom();
    if (editor_ && editor_ != editor)
        editor_->hideSuggestion();
    editor_ = editor;
    scores_->setSong(editor);
    songActions_->setSong(editor);
    editor->setWindowTitle(title.isEmpty() ? editor->windowTitle() : title);
    inputLabel_->setText(toolTip);

    editorArea_->insertWidget(0, editor);
    editorArea_->setCurrentWidget(editor);
    tabs_->setCurrentIndex(0);
    songPageShown_ = true;

    connect(editor, &SongEditor::modificationChanged, this, [this] {
        updateWindowTitle();
        updateSongActions();
    });
    connect(
        editor, &SongEditor::firstSaved, this,
        [this, path = editor->path()] {
            // The wish at once (also while the window closes), the question afterwards.
            fulfilPendingWish(path);
            QMetaObject::invokeMethod(
                this, [this, path, root = songbookRoot_] { onFirstSave(path, root); },
                Qt::QueuedConnection);
        });
    connect(editor, &SongEditor::saved, this, [this, editor](const std::string& content) {
        changesAction_->setChecked(false);
        refreshSongMetadata(editor, content);
        scores_->syncScoreTabs();
        requestPreview();
    });
    connect(editor, &SongEditor::reloadedFromDisk, this, [this, editor] {
        statusBar()->showMessage(
            tr("%1 changed on disk and was reloaded.").arg(toQString(editor->path().filename())),
            5000);
        if (auto content = zub::readTextFile(editor->path()))
            refreshSongMetadata(editor, *content);
        clearTidyChanges(); // its line numbers were for the old text
        updateSongActions();
        scores_->syncScoreTabs();
        requestPreview();
    });

    // Toolbar state follows the editor; dies with it (context: this, disconnected on close).
    QPlainTextEdit* edit = editor->textEdit();
    connect(edit, &QPlainTextEdit::undoAvailable, this, [this] { updateEditState(); });
    connect(edit, &QPlainTextEdit::textChanged, this, [this] {
        refreshChanges();
        updateMarkChecked();
        favouriteTimer_->start();
        scores_->songTextChanged(); // a \lilypondfile may have been typed, lang or key changed
        songActions_->cancelMeasure(); // its result would be for the old text
    });
    connect(edit, &QPlainTextEdit::redoAvailable, this, [this] { updateEditState(); });
    connect(edit, &QPlainTextEdit::copyAvailable, this, [this] { updateEditState(); });

    showView(ActivityBar::View::Song);
    songActions_->offerFindings(editor); // what the last Check Songs found for it
    scores_->syncScoreTabs();
    updateSongActions(); // after load: the editor may have made itself read-only
    updateWindowTitle();
    edit->setFocus();
    requestPreview();
}

void MainWindow::saveCurrent() {
    SongEditor* editor = currentEditor();
    if (!editor)
        return;
    if (auto saved = editor->save(); !saved) {
        QMessageBox::warning(this,
                             editor == editor_ ? tr("Cannot save song") : tr("Cannot save score"),
                             saved.error());
        return;
    } else if (*saved != SongEditor::SaveResult::Saved) {
        return;
    }
    statusBar()->showMessage(tr("Saved %1").arg(toQString(editor->path().filename())), 3000);
}

bool MainWindow::confirmClose(SongEditor* editor) {
    if (!editor->isModified())
        return true;
    const auto answer = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("Save changes to \"%1\" before closing?").arg(editor->windowTitle()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        if (auto saved = editor->save(); !saved) {
            QMessageBox::warning(this,
                                 editor->kind() == SongEditor::Kind::Score ? tr("Cannot save score")
                                                                           : tr("Cannot save song"),
                                 saved.error());
            return false;
        } else if (*saved == SongEditor::SaveResult::Cancelled) {
            return false;
        }
    }
    return true;
}

bool MainWindow::confirmCloseAll() {
    // Scores first: discarding a new one edits the song, which is asked about after.
    if (!scores_->confirmCloseScores())
        return false;
    return !(editor_ && !confirmClose(editor_));
}

void MainWindow::replaceSongText(const std::string& edited) {
    if (!editor_ || editor_->textEdit()->isReadOnly())
        return;
    // content() has CRLF for a CRLF song, the document holds bare '\n': compare and measure on
    // the document's text.
    const auto lf = [](std::string text) {
        for (std::size_t at = 0; (at = text.find("\r\n", at)) != std::string::npos;)
            text.erase(at, 1);
        return text;
    };
    const std::string before = lf(editor_->content());
    const std::string afterLf = lf(edited);
    if (afterLf == before)
        return;
    const std::string& after = afterLf;
    // One undo step: replace only the changed middle part of the text.
    std::size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix])
        ++prefix;
    std::size_t suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
           before[before.size() - 1 - suffix] == after[after.size() - 1 - suffix])
        ++suffix;
    // Never split a UTF-8 sequence.
    const auto continuation = [](char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; };
    while (prefix > 0 && prefix < before.size() && continuation(before[prefix]))
        --prefix;
    while (suffix > 0 && continuation(before[before.size() - suffix]))
        --suffix;
    const auto chars = [](const std::string& text, std::size_t bytes) {
        return static_cast<int>(
            QString::fromUtf8(text.data(), static_cast<qsizetype>(bytes)).size());
    };
    const int from = chars(before, prefix);
    const int to = from + chars(before.substr(prefix, before.size() - prefix - suffix),
                                before.size() - prefix - suffix);
    const QString replacement = QString::fromUtf8(
        after.data() + prefix, static_cast<qsizetype>(after.size() - prefix - suffix));
    QTextCursor cursor(editor_->textEdit()->document());
    cursor.beginEditBlock();
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);
    cursor.endEditBlock();
}

void MainWindow::goBack() {
    // The song stays open when Back leaves it (the bar or Ctrl+2 bring it back, Ctrl+W closes it).
    using View = ActivityBar::View;
    const auto current = currentView();
    while (!viewHistory_.empty()) {
        const View view = viewHistory_.back();
        viewHistory_.pop_back();
        if ((view == View::Song && !editor_) || (current && view == *current))
            continue;
        switchView(view);
        if (currentView() != current)
            return;
    }
    switchView(View::Songs);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent*>(event)->button() == Qt::BackButton) {
        // Only for this window: a dialog on top (import, export, a question) keeps it.
        const auto* widget = qobject_cast<QWidget*>(watched);
        if (widget && widget->window() == this) {
            goBack();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::closeSong() {
    SongEditor* editor = editor_;
    if (!editor)
        return true;
    if (!confirmCloseAll())
        return false;
    const std::filesystem::path path = editor->path();
    // Closed without saving: its wish stays a wish.
    if (pendingWish_ && pendingWish_->song == path && editor->isNew())
        pendingWish_.reset();
    songActions_->cancelRun();
    clearTidyChanges();
    changesAction_->setChecked(false);
    // No more reload dialogs from an editor that is gone but not yet deleted.
    editor->stopWatching();
    approvedConstructs_.erase(path); // approvals last as long as the song is open
    scores_->removeScoreTabs();
    disconnect(editor, nullptr, this, nullptr);
    disconnect(editor->textEdit(), nullptr, this, nullptr);
    editor_ = nullptr;
    scores_->setSong(nullptr);
    songActions_->setSong(nullptr);
    editor->setParent(nullptr); // out of the stack now; the next song takes index 0
    editor->deleteLater();

    // Where the song was opened from (the list, Home, Songbooks), else Back.
    if (pages_->currentWidget() == songPage_) {
        if (songOpenedFrom_)
            showView(*songOpenedFrom_);
        else
            goBack();
    }
    songOpenedFrom_.reset();
    updateSongActions();
    updateWindowTitle();
    requestPreview(); // cancels the build and clears the pane

    // The list with the song that was open selected.
    listPage_->selectSong(path);
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    closing_ = true; // a song saved now asks nothing more (Add to Songbooks)
    if (!songbooksPage_->confirmClose() || !confirmCloseAll()) {
        closing_ = false;
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue(geometryKey, saveGeometry());
    // Before the song page was shown the splitter has no real sizes; keep what is stored.
    if (songPageShown_)
        settings.setValue(songSplitterKey, songSplitter_->saveState());
    event->accept();
}

void MainWindow::createSongToolBar() {
    songToolBar_ = new QToolBar(tr("Song"), songPage_);
    songToolBar_->setMovable(false);
    songToolBar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    const auto editAction = [this](const QString& text, const QString& icon, const QString& toolTip,
                                   auto&& slot) {
        auto* action = new QAction(QIcon::fromTheme(icon), text, this);
        action->setToolTip(toolTip);
        connect(action, &QAction::triggered, this, [this, slot] {
            if (SongEditor* editor = currentEditor())
                slot(editor->textEdit());
        });
        songToolBar_->addAction(action);
        return action;
    };

    songToolBar_->addAction(closeSongAction_);
    songToolBar_->addAction(saveAction_);
    addToBooksAction_ = new QAction(QIcon::fromTheme(QStringLiteral("list-add")),
                                    tr("Add to Songbooks…"), this);
    addToBooksAction_->setToolTip(tr("Put the song into songbooks (where their index puts it)"));
    connect(addToBooksAction_, &QAction::triggered, this, [this] {
        if (editor_ && !editor_->isNew())
            addToSongbooks(editor_->path(), false);
    });
    songToolBar_->addAction(addToBooksAction_);
    songToolBar_->addSeparator();
    undoAction_ = editAction(tr("Undo"), QStringLiteral("edit-undo"), tr("Undo (Ctrl+Z)"),
                             [](QPlainTextEdit* e) { e->undo(); });
    redoAction_ = editAction(tr("Redo"), QStringLiteral("edit-redo"), tr("Redo (Ctrl+Shift+Z)"),
                             [](QPlainTextEdit* e) { e->redo(); });
    songToolBar_->addSeparator();
    cutAction_ = editAction(tr("Cut"), QStringLiteral("edit-cut"), tr("Cut (Ctrl+X)"),
                            [](QPlainTextEdit* e) { e->cut(); });
    copyAction_ = editAction(tr("Copy"), QStringLiteral("edit-copy"), tr("Copy (Ctrl+C)"),
                             [](QPlainTextEdit* e) { e->copy(); });
    pasteAction_ = editAction(tr("Paste"), QStringLiteral("edit-paste"), tr("Paste (Ctrl+V)"),
                              [](QPlainTextEdit* e) { e->paste(); });
    songToolBar_->addAction(findAction_);
    songToolBar_->addSeparator();

    for (QAction* action : {songActions_->tidyAction(), songActions_->makeSmallerAction(),
                            songActions_->joinLinesAction(), songActions_->checkAction()})
        songToolBar_->addAction(action);

    markCheckedAction_ =
        new QAction(QIcon::fromTheme(QStringLiteral("checkmark")), tr("Mark Checked"), this);
    markCheckedAction_->setToolTip(
        tr("The song is checked: remove its \"not checked yet\" TODO line "
           "(one undo step; save to keep it)"));
    connect(markCheckedAction_, &QAction::triggered, this, [this] {
        if (!editor_ || editor_->textEdit()->isReadOnly())
            return;
        const std::string content = editor_->content();
        if (!zub::todo::hasNotCheckedMark(content))
            return;
        editor_->replaceContent(zub::todo::removeNotCheckedMark(content));
        statusBar()->showMessage(tr("Marked as checked. Save to keep it."), 8000);
    });
    songToolBar_->addAction(markCheckedAction_);

    favouriteAction_ = new QAction(QStringLiteral("★"), this);
    favouriteAction_->setCheckable(true);
    favouriteAction_->setToolTip(tr("Favourite: add or remove the label \"fav\" "
                                    "(one undo step; save to keep it)"));
    // triggered, not toggled: syncing the check with the text must not write to the text.
    connect(favouriteAction_, &QAction::triggered, this, [this] {
        if (editor_)
            toggleFavourite(editor_->path());
    });
    songToolBar_->addAction(favouriteAction_);
    favouriteTimer_ = new QTimer(this);
    favouriteTimer_->setSingleShot(true);
    favouriteTimer_->setInterval(300);
    connect(favouriteTimer_, &QTimer::timeout, this, &MainWindow::updateLabelActions);

    labelsAction_ = new QAction(QIcon::fromTheme(QStringLiteral("tag")), tr("Labels…"), this);
    labelsAction_->setToolTip(tr("Edit the song's labels (one undo step; save to keep them)"));
    connect(labelsAction_, &QAction::triggered, this, [this] {
        if (editor_)
            editLabels({editor_->path()});
    });
    songToolBar_->addAction(labelsAction_);

    changesAction_ =
        new QAction(QIcon::fromTheme(QStringLiteral("document-compare")), tr("Changes"), this);
    changesAction_->setCheckable(true);
    changesAction_->setToolTip(
        tr("Show what changed since the song was saved (an import: since it was imported)"));
    connect(changesAction_, &QAction::toggled, this, &MainWindow::showChanges);
    songToolBar_->addAction(changesAction_);

    // The song's input name ("Artist/Title"), at the right end.
    inputLabel_ = new QLabel(songToolBar_);
    inputLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    inputLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    songToolBar_->addWidget(inputLabel_);

    static_cast<QVBoxLayout*>(songPage_->layout())->insertWidget(0, songToolBar_);
}

void MainWindow::createEditMenu() {
    QMenu* edit = menuBar()->addMenu(tr("Ed&it"));
    // Find works on every page: the Songs page's search, or the find bar of the editor in view.
    findAction_ = edit->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), tr("&Find…"));
    findAction_->setShortcut(QKeySequence::Find);
    findAction_->setToolTip(tr("Find in the text (Ctrl+F)"));
    connect(findAction_, &QAction::triggered, this, [this] { showFind(false); });
    QAction* replaceAction = edit->addAction(tr("&Replace…"));
    replaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
    connect(replaceAction, &QAction::triggered, this, [this] { showFind(true); });
    edit->addSeparator();
    // songOnly: the action edits the song's text (chords, parts), not a score's.
    const auto add = [this, edit](const QString& text, const QList<QKeySequence>& keys,
                                  const std::function<void()>& run, bool songOnly = true) {
        QAction* action = edit->addAction(text);
        action->setShortcuts(keys);
        action->setShortcutContext(Qt::WindowShortcut);
        connect(action, &QAction::triggered, this, run);
        (songOnly ? songEditActions_ : editActions_).push_back(action);
    };
    add(tr("Insert Chord"), {QKeySequence(Qt::ALT | Qt::Key_A)}, [this] {
        if (editor_)
            editor_->insertChord();
    });
    add(tr("Insert Multivoice"), {QKeySequence(Qt::ALT | Qt::Key_M)}, [this] {
        if (editor_)
            editor_->insertMultivoice();
    });
    add(tr("Insert Role"), {QKeySequence(Qt::ALT | Qt::Key_L)}, [this] {
        if (editor_)
            editor_->insertRole();
    });
    add(tr("Remove Element at Cursor"), {QKeySequence(Qt::ALT | Qt::Key_Delete)}, [this] {
        if (editor_ && !editor_->removeElement())
            statusBar()->showMessage(tr("Nothing to remove at the cursor"), 3000);
    });
    edit->addSeparator();
    // Like VSCodium: Alt+Up/Down move the lines with the cursor or the selection as a block,
    // Ctrl+Up/Down scroll the view by a line without moving the cursor.
    add(
        tr("Move Line Up"), {QKeySequence(Qt::ALT | Qt::Key_Up)},
        [this] {
            if (SongEditor* editor = currentEditor())
                editor->moveLines(-1);
        },
        false);
    add(
        tr("Move Line Down"), {QKeySequence(Qt::ALT | Qt::Key_Down)},
        [this] {
            if (SongEditor* editor = currentEditor())
                editor->moveLines(+1);
        },
        false);
    add(
        tr("Toggle Comment"), {QKeySequence(Qt::CTRL | Qt::Key_Slash)},
        [this] {
            if (SongEditor* editor = currentEditor())
                editor->toggleComments();
        },
        false);
    for (const auto& [key, step] : {std::pair{Qt::Key_Up, -1}, std::pair{Qt::Key_Down, +1}}) {
        auto* scroll = new QAction(this);
        scroll->setShortcut(QKeySequence(Qt::CTRL | key));
        scroll->setShortcutContext(Qt::WindowShortcut);
        connect(scroll, &QAction::triggered, this, [this, step] {
            if (SongEditor* editor = currentEditor();
                editor && pages_->currentWidget() == songPage_) {
                QScrollBar* bar = editor->textEdit()->verticalScrollBar();
                bar->setValue(bar->value() + step * bar->singleStep());
            }
        });
        addAction(scroll);
    }
    edit->addSeparator();
    using zub::songedit::Step;
    // Arrows: the same keys on every layout. Alt+Left is Back elsewhere (disabled on this page).
    add(tr("Move Chord Left"), {QKeySequence(Qt::ALT | Qt::Key_Left)},
        [this] { moveChord(-1, Step::Letter); });
    add(tr("Move Chord Right"), {QKeySequence(Qt::ALT | Qt::Key_Right)},
        [this] { moveChord(+1, Step::Letter); });
    add(tr("Move Chord to Previous Word"), {QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Left)},
        [this] { moveChord(-1, Step::Word); });
    add(tr("Move Chord to Next Word"), {QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_Right)},
        [this] { moveChord(+1, Step::Word); });
    edit->addSeparator();
    add(tr("Insert Part…"), {QKeySequence(Qt::CTRL | Qt::Key_P)}, [this] { showPartMenu(); });
    edit->addSeparator();
    add(tr("New Score…"), {}, [this] { scores_->newScore(); });
}

void MainWindow::showFind(bool replaceMode) {
    QWidget* page = pages_->currentWidget();
    if (page == listPage_) {
        if (!replaceMode)
            listPage_->focusSearch();
    } else if (page == songPage_) {
        if (SongEditor* current = currentEditor())
            current->findBar()->open(replaceMode);
    } else if (page == songbooksPage_) {
        songbooksPage_->showFind(replaceMode);
    }
}

void MainWindow::moveChord(int direction, zub::songedit::Step step) {
    if (editor_ && !editor_->moveChord(direction, step))
        statusBar()->showMessage(tr("No chord at the cursor"), 3000);
}

void MainWindow::showPartMenu() {
    if (!editor_)
        return;
    using zub::songedit::Part;
    auto* menu = new QMenu(editor_->textEdit());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QPointer<SongEditor> editor = editor_;
    const auto add = [&](const QString& text, Part part) {
        connect(menu->addAction(text), &QAction::triggered, menu, [editor, part] {
            if (editor)
                editor->insertPart(part);
        });
    };
    add(tr("Verse"), Part::Verse);
    add(tr("Unnumbered Verse"), Part::VerseUnnumbered);
    add(tr("Chorus"), Part::Chorus);
    add(tr("Pre-chorus"), Part::PreChorus);
    add(tr("Bridge"), Part::Bridge);
    add(tr("Intro"), Part::Intro);
    add(tr("Outro"), Part::Outro);
    add(tr("Instrumental"), Part::Instrumental);
    menu->addSeparator();
    add(tr("Chorus Again (\\insertchorus)"), Part::ChorusAgain);
    add(tr("Chorus Reminder (\\insertchorus*)"), Part::ChorusReminder);
    if (!editor_->hasSongHeader()) {
        menu->addSeparator();
        add(tr("Song Header"), Part::SongHeader);
    }
    QPlainTextEdit* edit = editor_->textEdit();
    menu->popup(edit->viewport()->mapToGlobal(edit->cursorRect().bottomLeft()));
}

SongEditor* MainWindow::currentEditor() const {
    return scores_->currentEditor();
}

bool MainWindow::onSongTab() const {
    return scores_->onSongTab();
}

bool MainWindow::anyModified() const {
    return editor_ && (editor_->isModified() || scores_->anyScoreModified());
}

void MainWindow::updateEditState() {
    SongEditor* current = currentEditor();
    if (!current) {
        for (QAction* action : {undoAction_, redoAction_, cutAction_, copyAction_, pasteAction_})
            action->setEnabled(false);
        return;
    }
    const QPlainTextEdit* edit = current->textEdit();
    const bool selection = edit->textCursor().hasSelection();
    // While the Changes view is shown, the song editor is hidden: no edits it can't show.
    const bool editable =
        !edit->isReadOnly() && !(current == editor_ && changesAction_->isChecked());
    undoAction_->setEnabled(editable && edit->document()->isUndoAvailable());
    redoAction_->setEnabled(editable && edit->document()->isRedoAvailable());
    copyAction_->setEnabled(selection);
    cutAction_->setEnabled(selection && editable);
    pasteAction_->setEnabled(editable);
}

void MainWindow::updateSongActions() {
    const bool open = editor_ != nullptr;
    scores_->updateScoreWidthBars();
    scores_->updateRegionActions();
    SongEditor* current = currentEditor();
    const bool songTab = onSongTab();
    closeSongAction_->setEnabled(open);
    addToBooksAction_->setEnabled(open && !editor_->isNew() && !songbookRoot_.empty());
    activityBar_->setSongEnabled(open);
    songViewAction_->setEnabled(open); // like the bar's button
    saveAction_->setEnabled(current && current->isModified());
    saveAction_->setText(current && !songTab ? tr("&Save Score") : tr("&Save Song"));
    const bool modified = open && editor_->isModified();
    changesAction_->setEnabled(modified && songTab);
    if (!modified && changesAction_->isChecked())
        changesAction_->setChecked(false);
    updateEditState();
    songActions_->updateEnabled(songTab);
    if (!open) {
        for (QAction* action : {markCheckedAction_, favouriteAction_, labelsAction_})
            action->setEnabled(false);
        for (QAction* action : editActions_)
            action->setEnabled(false);
        for (QAction* action : songEditActions_)
            action->setEnabled(false);
        return;
    }
    const bool readOnly = current ? current->textEdit()->isReadOnly() : true;
    const bool editable =
        current && !readOnly && !(current == editor_ && changesAction_->isChecked());
    for (QAction* action : editActions_)
        action->setEnabled(editable);
    for (QAction* action : songEditActions_)
        action->setEnabled(editable && songTab);
    updateMarkChecked();
    updateLabelActions();
}

void MainWindow::updateLabelActions() {
    const bool editable = editor_ && onSongTab() && !editor_->textEdit()->isReadOnly() &&
                          !changesAction_->isChecked();
    favouriteAction_->setEnabled(editable);
    labelsAction_->setEnabled(editable);
    bool favourite = false;
    if (editor_) {
        if (const auto meta = zub::parseSong(editor_->content()))
            favourite = zub::tags::isFavourite(zub::tags::parse(meta->value("tags")));
    }
    favouriteAction_->setChecked(favourite);
}

QString MainWindow::writeSongTags(
    const std::filesystem::path& path,
    const std::function<std::vector<std::string>(const std::vector<std::string>&)>& change) {
    const QString name = toQString(path.filename());
    const bool open = editor_ && editor_->path() == path;
    if (open && editor_->textEdit()->isReadOnly())
        return tr("%1: the song is open read-only.").arg(name);
    std::string content;
    if (open) {
        content = editor_->content();
    } else if (auto bytes = zub::readTextFile(path)) {
        content = std::move(*bytes);
        if (!SongEditor::roundTrips(content))
            return tr("%1: the file can't be rewritten safely (byte-order mark, invalid UTF-8, "
                      "mixed line endings or Unicode line separators).")
                .arg(name);
    } else {
        return toQString(bytes.error());
    }
    const auto meta = zub::parseSong(content);
    if (!meta)
        return tr("%1: the song header has a problem: %2").arg(name, toQString(meta.error()));
    const auto current = zub::tags::parse(meta->value("tags"));
    const auto wanted = change(current);
    if (wanted == current)
        return {};
    for (const std::string& tag : wanted) {
        if (zub::tags::invalidTag(tag))
            return tr("%1: \"%2\" cannot be a label.").arg(name, toQString(tag));
    }
    const std::string updated = zub::tags::setTags(content, wanted);
    if (open) {
        editor_->replaceContent(updated);
        updateLabelActions();
        return {};
    }
    if (auto written = zub::writeTextFileAtomic(path, updated); !written)
        return toQString(written.error());
    if (auto newMeta = zub::parseSong(updated))
        model_->updateSong(path, std::move(*newMeta), zub::todo::notes(updated),
                           zub::songbook::referencedFiles(updated));
    return {};
}

void MainWindow::changeSongTags(
    const std::vector<std::filesystem::path>& paths,
    const std::function<std::vector<std::string>(const std::vector<std::string>&)>& change) {
    QStringList failures;
    bool anyOpen = false;
    for (const auto& path : paths) {
        const QString error = writeSongTags(path, change);
        if (!error.isEmpty())
            failures << error;
        anyOpen = anyOpen || (editor_ && editor_->path() == path);
    }
    if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("Labels"),
                             tr("These songs could not be changed:") + QStringLiteral("\n\n") +
                                 failures.join(QLatin1Char('\n')));
    } else if (anyOpen) {
        statusBar()->showMessage(tr("Labels changed. Save to keep them."), 8000);
    }
}

void MainWindow::toggleFavourite(const std::filesystem::path& path) {
    changeSongTags({path}, [](const std::vector<std::string>& tags) {
        auto result = tags;
        const std::string fav{zub::tags::kFavourite};
        if (const auto it = std::ranges::find(result, fav); it != result.end())
            result.erase(it);
        else
            result.insert(result.begin(), fav);
        return result;
    });
    updateLabelActions(); // a failed toggle must not leave the ★ flipped
}

void MainWindow::editLabels(const std::vector<std::filesystem::path>& paths) {
    std::vector<std::vector<std::string>> songs;
    QStringList known = model_->labels();
    for (const auto& path : paths) {
        std::optional<zub::SongMetadata> meta;
        if (editor_ && editor_->path() == path) {
            if (auto parsed = zub::parseSong(editor_->content()))
                meta = std::move(*parsed);
        } else if (const int row = model_->rowOf(path); row >= 0) {
            meta = model_->song(row)->meta;
        }
        songs.push_back(meta ? zub::tags::parse(meta->value("tags")) : std::vector<std::string>{});
        for (const std::string& tag : songs.back()) {
            const QString label = toQString(tag);
            if (tag != zub::tags::kFavourite && !known.contains(label))
                known << label;
        }
    }
    if (songs.empty())
        return;
    LabelsDialog dialog(known, songs, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    changeSongTags(paths,
                   [&dialog](const std::vector<std::string>& tags) { return dialog.apply(tags); });
}

std::vector<std::filesystem::path> MainWindow::selectedSongPaths() const {
    std::vector<std::filesystem::path> paths;
    for (const QModelIndex& index : listPage_->table()->selectionModel()->selectedRows()) {
        if (const auto* song = model_->song(proxy_->mapToSource(index).row()))
            paths.push_back(song->path);
    }
    return paths;
}

void MainWindow::showTableMenu(const QPoint& position) {
    QTableView* table = listPage_->table();
    const QModelIndex index = table->indexAt(position);
    if (!index.isValid())
        return;
    if (!table->selectionModel()->isRowSelected(index.row(), {}))
        table->selectRow(index.row());
    QMenu menu(this);
    QAction* labels = menu.addAction(tr("Labels…"));
    if (menu.exec(table->viewport()->mapToGlobal(position)) == labels)
        editLabels(selectedSongPaths());
}

void MainWindow::updateMarkChecked() {
    // Searched in the document: cheap enough for every keystroke of a song-sized text.
    const bool marked = onSongTab() && !editor_->textEdit()->isReadOnly() &&
                        !changesAction_->isChecked() &&
                        !editor_->textEdit()
                             ->document()
                             ->find(QStringLiteral("% TODO: ") + toQString(zub::todo::kNotChecked))
                             .isNull();
    markCheckedAction_->setEnabled(marked);
}

void MainWindow::clearTidyChanges() {
    tidyList_->clear();
    tidyPanel_->hide();
}

void MainWindow::showChanges(bool show) {
    if (!editor_)
        return;
    if (show) {
        diffView_->setTexts(editor_->baselineContent(), editor_->content(), noChangesText());
        editorArea_->setCurrentWidget(diffView_);
    } else {
        editorArea_->setCurrentWidget(editor_);
    }
    updateSongActions();
}

QString MainWindow::noChangesText() const {
    return editor_ && editor_->isNew() ? tr("No changes since the import")
                                       : tr("No unsaved changes");
}

void MainWindow::refreshChanges() {
    if (editor_ && changesAction_->isChecked())
        diffView_->setTexts(editor_->baselineContent(), editor_->content(), noChangesText());
}

void MainWindow::jumpToLine(int line) {
    if (!editor_)
        return;
    tabs_->setCurrentIndex(0);         // the line is in the song
    changesAction_->setChecked(false); // the editor must be showing
    editor_->goToLine(line);
}

void MainWindow::updateWindowTitle() {
    if (!editor_) {
        setWindowModified(false);
        setWindowTitle(listTitle_);
        return;
    }
    // "[*]" is replaced by Qt with a marker while the window is modified.
    setWindowTitle(editor_->windowTitle() + QStringLiteral("[*] — ") +
                   QApplication::applicationDisplayName());
    setWindowModified(anyModified());
}

void MainWindow::refreshSongMetadata(SongEditor* editor, const std::string& content) {
    auto meta = zub::parseSong(content);
    if (!meta) {
        statusBar()->showMessage(
            tr("%1: the song header has a problem: %2")
                .arg(toQString(editor->path().filename()), toQString(meta.error())),
            8000);
        return;
    }
    editor->setWindowTitle(toQString(zub::latex::toDisplayText(meta->title)));
    if (editor == editor_) {
        inputLabel_->setText(toQString(inputNameOf(editor)));
        updateWindowTitle();
    }
    auto todos = zub::todo::notes(content);
    auto scores = zub::songbook::referencedFiles(content);
    if (!model_->updateSong(editor->path(), *meta, todos, scores)) {
        // A new song's first save: it joins the table now.
        model_->addSong({editor->path(), inputNameOf(editor), std::move(*meta), std::move(todos),
                         std::move(scores),
                         zub::collectionOf(songbookRoot_, editor->path()).value_or("chords")});
        updateLanguageFilter();
    }
}

void MainWindow::createPreview() {
    preview_ = new PreviewPane(this);
    previewRunner_ = new PreviewRunner(this);

    connect(previewRunner_, &PreviewRunner::started, preview_, &PreviewPane::showBuilding);
    connect(previewRunner_, &PreviewRunner::finished, preview_, &PreviewPane::showResult);
    connect(preview_, &PreviewPane::rebuildRequested, this, [this] { requestPreview(true); });
    connect(preview_, &PreviewPane::documentClassChanged, this, [this](const QString& name) {
        QSettings{}.setValue(previewClassKey, name);
        requestPreview();
    });
    connect(preview_, &PreviewPane::errorActivated, this,
            [this](const QString& inputName, int line) {
                // Only if the error belongs to the song that is open now.
                SongEditor* editor = editor_;
                if (editor && toQString(inputNameOf(editor)) == inputName)
                    jumpToLine(line);
            });

    // One work dir per running instance: two instances building in the same dir would
    // overwrite each other's files. The first instance gets the shared dir (and keeps
    // LilyPond's cache between sessions); others get their own.
    QString cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cache.isEmpty())
        cache = QDir(QDir::tempPath()).filePath(QApplication::applicationName());
    QDir().mkpath(cache);
    previewLock_ =
        std::make_unique<QLockFile>(QDir(cache).filePath(QStringLiteral("preview.lock")));
    previewLock_->setStaleLockTime(0);
    const QString dirName = previewLock_->tryLock(0)
                                ? QStringLiteral("preview")
                                : QStringLiteral("preview-%1").arg(QApplication::applicationPid());
    previewWorkDir_ = toPath(QDir(cache).absoluteFilePath(dirName));
    // The score preview's work dir follows the same rule.
    scoreDirs_.workDir = toPath(QDir(cache).absoluteFilePath(
        dirName == QStringLiteral("preview")
            ? QStringLiteral("score")
            : QStringLiteral("score-%1").arg(QApplication::applicationPid())));
    scoreDirs_.playbackDir = toPath(QDir(cache).absoluteFilePath(QStringLiteral("playback")));
    scoreDirs_.soundsDir = toPath(QDir(cache).absoluteFilePath(QStringLiteral("sounds")));
}

std::string MainWindow::inputNameOf(const SongEditor* editor) const {
    return inputNameOfPath(editor->path());
}

std::string MainWindow::inputNameOfPath(const std::filesystem::path& path) const {
    // songs/<collection>/Artist/Title.tex -> "Artist/Title"
    const std::string collection = zub::collectionOf(songbookRoot_, path).value_or("chords");
    const auto relative =
        path.lexically_relative(songbookRoot_ / "songs" / collection).replace_extension();
    const auto u8 = relative.generic_u8string();
    return {u8.begin(), u8.end()};
}

QUrl MainWindow::wishSearchUrl(const HomeWish& wish) const {
    const QString text = wishSearchText(wish.text, wish.name, tr("chords", "a word of a web search"));
    return QUrl(QStringLiteral("https://www.google.com/search?q=") +
                QString::fromLatin1(QUrl::toPercentEncoding(text)));
}

void MainWindow::fulfilPendingWish(const std::filesystem::path& path) {
    if (!pendingWish_ || pendingWish_->song != path || songbookRoot_.empty())
        return;
    const HomeWish wish = pendingWish_->wish;
    pendingWish_.reset();
    if (auto done = changeWish(wish, [&] {
            return fulfilWish(songbookRoot_, wish, inputNameOfPath(path));
        });
        done)
        statusBar()->showMessage(
            tr("The wish %1 is done.").arg(wishDisplayText(wish.text, wish.name)), 10000);
}

std::expected<void, QString>
MainWindow::changeWish(const HomeWish& wish,
                       const std::function<std::expected<void, QString>()>& change) {
    // One writer at a time: not a book with unsaved LaTeX edits on the Songbooks page.
    const QString busy = songbooksPage_->bookWithUnsavedLatex();
    std::expected<void, QString> result;
    if (!wish.bookId.isEmpty() && wish.bookId == busy)
        result = std::unexpected(tr("%1 has unsaved LaTeX edits on the Songbooks page: save or "
                                    "revert them first.")
                                     .arg(wish.bookTitle));
    else
        result = change();
    if (!result)
        QMessageBox::warning(this, tr("Wishes"), result.error());
    else if (!wish.bookId.isEmpty() && busy.isEmpty())
        songbooksPage_->reloadList(); // its book changed
    homeTimer_->start();
    return result;
}

void MainWindow::onFirstSave(const std::filesystem::path& path,
                             const std::filesystem::path& root) {
    updateSongActions();
    // Not while closing, nor for a song of a library left meanwhile.
    if (closing_ || songbookRoot_.empty() || root != songbookRoot_ ||
        !zub::collectionOf(songbookRoot_, path))
        return;
    addToSongbooks(path, true);
}

void MainWindow::addToSongbooks(const std::filesystem::path& path, bool afterNew) {
    if (songbookRoot_.empty())
        return;
    if (bookAdder_->busy()) {
        statusBar()->showMessage(
            tr("Still adding a song to songbooks: try Add to Songbooks… again in a moment."),
            10000);
        return;
    }
    const int row = model_->rowOf(path);
    const QString title = row >= 0 ? model_->index(row, SongTableModel::Title).data().toString()
                                   : toQString(path.stem());
    const std::string name = inputNameOfPath(path);
    AddToBooksDialog dialog(songbookRoot_, name, title, songbooksPage_->bookWithUnsavedLatex(),
                            this);
    if (afterNew && !dialog.anyAvailable())
        return;
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (const auto removals = dialog.removals(); !removals.empty()) {
        QStringList removed;
        QStringList notes;
        BookAdder::remove(name, removals, removed, notes);
        QStringList parts;
        if (!removed.isEmpty())
            parts << tr("Taken out of %1.").arg(removed.join(QStringLiteral(", ")));
        parts << notes;
        statusBar()->showMessage(parts.join(QLatin1Char(' ')), 15000);
        if (!notes.isEmpty() && removed.isEmpty())
            QMessageBox::warning(this, tr("Add to Songbooks"), notes.join(QLatin1Char('\n')));
        if (songbooksPage_->bookWithUnsavedLatex().isEmpty())
            songbooksPage_->reloadList();
        homeTimer_->start();
        if (!dialog.targets().empty())
            takenOutMessage_ = parts.join(QLatin1Char(' ')); // the adder's report replaces it
    }
    const auto targets = dialog.targets();
    if (!targets.empty())
        bookAdder_->add(name, targets);
}

void MainWindow::requestPreview(bool userAsked) {
    SongEditor* editor = editor_;
    if (!editor || songbookRoot_.empty()) {
        previewRunner_->cancel(); // a build for a closed song must not fill the pane
        preview_->clear();
        return;
    }

    std::string content = editor->content();
    // A reference to a score that exists nowhere would fail the build with a LaTeX error.
    for (const std::string& reference : zub::songbook::referencedFiles(content)) {
        const auto resolved = zub::songbook::resolveScoreReference(reference);
        if (!resolved || !scores_->scorePath(*resolved).empty())
            continue;
        const std::string& name = *resolved;
        if (scores_->scoreEditor(name))
            continue;
        previewRunner_->cancel();
        preview_->clear();
        PreviewResult missing;
        missing.inputName = inputNameOf(editor);
        missing.message = tr("The score %1 does not exist: create it, or remove its "
                             "\\lilypondfile line from the song.")
                              .arg(toQString(name));
        preview_->showResult(missing);
        return;
    }
    // Songs build with -shell-escape, so a song is code, and so are its scores. Unknown commands
    // (e.g. from an imported song) are never built automatically, and only after confirmation on
    // Rebuild.
    QString list;
    if (!approveBuild(editor, content, userAsked, &list)) {
        previewRunner_->cancel();
        preview_->clear(); // the PDF shown may be another song's
        PreviewResult blocked;
        blocked.inputName = inputNameOf(editor);
        blocked.message =
            tr("Not built automatically: the song uses %1, which songs normally don't. "
               "Building runs it with full access to your files. Check the song, then "
               "press Rebuild to build it anyway.")
                .arg(list);
        preview_->showResult(blocked);
        return;
    }

    zub::preview::Request request;
    request.songbookRoot = songbookRoot_;
    request.packageDir = packageDir_;
    request.workDir = previewWorkDir_;
    request.inputName = inputNameOf(editor);
    request.collection = zub::collectionOf(songbookRoot_, editor->path()).value_or("");
    request.songContent = std::move(content);
    request.documentClass = toStdString(preview_->documentClass());
    // Unsaved scores are built as they are in their editors.
    request.fileOverrides = scores_->unsavedScores();
    scores_->markPdfBuilt();
    previewRunner_->request(std::move(request));
}

bool MainWindow::approveBuild(SongEditor* editor, const std::string& content, bool askUser,
                              QString* unapprovedList, const QString& yesText,
                              const QString& noText) {
    return approveRisks(songBuildRisks(editor, content), askUser, unapprovedList, yesText, noText);
}

std::vector<MainWindow::RiskSource> MainWindow::songBuildRisks(SongEditor* editor,
                                                               const std::string& content) const {
    std::vector<RiskSource> sources;
    // The song's own commands and what its scores on disk would run, every copy a build could
    // find (zub::songbook::scoreRisks: also calls it cannot check, and files beside a score that
    // stand in for the package's includes).
    auto ownRisks = zub::safety::unknownConstructs(content);
    for (std::string& risk : zub::songbook::scoreRisks(songbookRoot_, editor->path(), content))
        ownRisks.push_back(std::move(risk));
    sources.push_back({editor->path(), {}, std::move(ownRisks)});
    // Files in the library's folders a build could load by name (a probed hyperref.cfg, ...).
    if (auto shadows = zub::songbook::texinputsShadows(songbookRoot_); !shadows.empty())
        sources.push_back({songbookRoot_, {}, std::move(shadows)});
    // Scores open in an editor are built as they are there (unsaved ones from override/).
    for (const std::string& reference : zub::songbook::referencedFiles(content)) {
        const auto resolved = zub::songbook::resolveScoreReference(reference);
        if (!resolved)
            continue; // reported by scoreRisks
        const SongEditor* score = scores_->scoreEditor(*resolved);
        if (!score)
            continue;
        RiskSource source{score->path(), toQString(*resolved), zub::safety::lilypondRisks(score->content())};
        if (!source.risks.empty())
            sources.push_back(std::move(source));
    }
    return sources;
}

bool MainWindow::approveRisks(const std::vector<RiskSource>& sources, bool askUser,
                              QString* unapprovedList, const QString& yesText,
                              const QString& noText, bool forScore) {
    QStringList unapproved;
    std::vector<std::pair<std::filesystem::path, std::string>> toApprove;
    for (const RiskSource& source : sources) {
        const std::set<std::string>& approved = approvedConstructs_[source.key];
        for (const std::string& construct : source.risks) {
            if (approved.contains(construct))
                continue;
            toApprove.emplace_back(source.key, construct);
            unapproved << (source.label.isEmpty() ? QString()
                                                  : source.label + QStringLiteral(": ")) +
                              toQString(construct);
        }
    }
    if (unapproved.isEmpty())
        return true;
    const QString list = unapproved.join(QStringLiteral(", "));
    if (unapprovedList)
        *unapprovedList = list;
    bool build = false;
    if (askUser) {
        QMessageBox box(
            QMessageBox::Warning, forScore ? tr("Build this score?") : tr("Build this song?"),
            forScore ? tr("This score uses code that scores normally don't: %1").arg(list)
                     : tr("This song uses commands that songs normally don't: %1").arg(list),
            QMessageBox::Yes | QMessageBox::Cancel, this);
        if (!yesText.isEmpty())
            box.button(QMessageBox::Yes)->setText(yesText);
        if (!noText.isEmpty())
            box.button(QMessageBox::Cancel)->setText(noText);
        box.setDefaultButton(QMessageBox::Cancel);
        build = box.exec() == QMessageBox::Yes;
    }
    if (!build)
        return false;
    for (const auto& [key, construct] : toApprove)
        approvedConstructs_[key].insert(construct);
    return true;
}

namespace {

// The converter's file name (chords_converter.exe on Windows).
QString converterFileName() {
#ifdef Q_OS_WIN
    return QStringLiteral("chords_converter.exe");
#else
    return QStringLiteral("chords_converter");
#endif
}

bool usableConverter(const QString& path) {
    const QFileInfo info(path);
    // Absolute only: PATH may contain "." or empty entries.
    return !path.isEmpty() && info.isAbsolute() && info.isFile() && info.isExecutable();
}

} // namespace

std::filesystem::path MainWindow::builtInConverter() const {
    // Built from the external/chords_converter submodule and put next to the app by CMake
    // (in the build dir, and by cmake --install).
    const QString path =
        QDir(QApplication::applicationDirPath()).filePath(converterFileName());
    return usableConverter(path) ? toPath(path) : std::filesystem::path{};
}

QString MainWindow::noPackageText() const {
    return tr("No package to build the songs with: this app has no copy of the zubuvzpevnik "
              "package (external/zubuvzpevnik-package: git submodule update --init).");
}

std::filesystem::path MainWindow::bundledPackage() const {
    // Installed next to the app by cmake --install (share/zpevnikar/zubuvzpevnik), else, for a
    // program run from its build dir (the app, the tests), the external/zubuvzpevnik-package
    // submodule of the source tree it was built from. Never for an installed copy: on another
    // machine anyone could create the build machine's path (D:/a/... on Windows) and put code
    // there that builds run under -shell-escape.
    const QString appDir = QApplication::applicationDirPath();
    QStringList candidates{QDir(appDir).filePath(QStringLiteral("../share/zpevnikar/zubuvzpevnik"))};
#ifdef ZUB_BUNDLED_PACKAGE_DIR
    const QString fromBuild = QDir(QStringLiteral(ZUB_BUILD_DIR)).relativeFilePath(appDir);
    if (QDir::isRelativePath(fromBuild) && !fromBuild.startsWith(QLatin1String("..")))
        candidates << QStringLiteral(ZUB_BUNDLED_PACKAGE_DIR);
#endif
    for (const QString& candidate : candidates) {
        const auto path = toPath(QDir::cleanPath(candidate));
        if (zub::package::isPackage(path))
            return path;
    }
    return {};
}

void MainWindow::openSettings() {
    SettingsDialog::State state;
    QSettings settings;
    state.language = settings.value(languageKey).toString();
    const SoundTools tools = findSoundTools();
    state.soundfont = toQString(tools.soundfont);
    state.soundfontChosen = !settings.value(soundfontKey).toString().isEmpty() &&
                            state.soundfont == settings.value(soundfontKey).toString();
    state.package = toQString(packageDir_);
    state.appImage = !desktopintegration::appImage().isEmpty();
    state.menuEntry = desktopintegration::enabled();
    state.keepBuildFiles = buildfiles::keep();
    state.buildFilesBytes = buildFilesSize();
    state.deleteBuildFiles = [this] { return deleteBuildFiles(); };

    SettingsDialog dialog(state, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (dialog.language() != state.language) {
        settings.setValue(languageKey, dialog.language());
        QMessageBox::information(this, tr("Language"),
                                 tr("The language changes when %1 is started again.")
                                     .arg(QApplication::applicationDisplayName()));
    }
    if (dialog.soundfontChanged()) {
        if (dialog.soundfont().isEmpty())
            settings.remove(soundfontKey);
        else
            settings.setValue(soundfontKey, dialog.soundfont());
        scores_->updateSoundAvailability();
    }
    if (state.appImage && dialog.menuEntry() != state.menuEntry) {
        desktopintegration::setEnabled(dialog.menuEntry());
        const bool done =
            dialog.menuEntry()
                ? desktopintegration::install(desktopintegration::appImage(),
                                              desktopintegration::bundled()) !=
                      desktopintegration::Result::Failed
                : desktopintegration::remove();
        if (!done)
            QMessageBox::warning(this, tr("Settings"),
                                 tr("The application menu entry %1 could not be changed.")
                                     .arg(desktopintegration::entryPath()));
    }
    if (dialog.keepBuildFiles() != state.keepBuildFiles) {
        buildfiles::setKeep(dialog.keepBuildFiles());
        // A running build drops its own when it ends (the setting is read then).
        if (!dialog.keepBuildFiles())
            deleteBuildFiles();
    }
}

qint64 MainWindow::buildFilesSize() const {
    qint64 bytes = buildfiles::size(buildfiles::songbooksDir());
    if (!previewWorkDir_.empty())
        bytes += buildfiles::size(toQString(previewWorkDir_ / "tmp-ly"));
    return bytes;
}

qint64 MainWindow::deleteBuildFiles() {
    if (!previewWorkDir_.empty() && !previewRunner_->isRunning())
        buildfiles::remove(toQString(previewWorkDir_ / "tmp-ly"));
    // While this window builds a book, all stay (the work dir of a build is per book file). A
    // build of another running instance is not known here: it may fail and has to run again.
    if (!songbooksPage_->buildingPdf()) {
        const QDir books(buildfiles::songbooksDir());
        for (const QString& name :
             books.entryList({QStringLiteral("repo-*")}, QDir::Dirs | QDir::NoDotAndDotDot))
            buildfiles::remove(books.filePath(name));
    }
    return buildFilesSize();
}

std::filesystem::path MainWindow::findConverter() const {
    // The built-in converter is the version this app is tested with. ZUB_CONVERTER (an absolute
    // path) overrides it, to try a converter build before its pin is bumped; PATH and
    // ~/.cargo/bin are fallbacks for an app built without it (ZUB_BUILD_CONVERTER=OFF).
    const auto builtIn = builtInConverter();
    QStringList candidates{
        qEnvironmentVariable("ZUB_CONVERTER"),
        builtIn.empty() ? QString{} : toQString(builtIn),
        QStandardPaths::findExecutable(QStringLiteral("chords_converter")),
        // cargo install puts it here, which is not always on PATH for GUI apps
        QDir::home().filePath(QStringLiteral(".cargo/bin/") + converterFileName()),
    };
    for (const QString& candidate : candidates) {
        if (usableConverter(candidate))
            return toPath(candidate);
    }
    return {};
}

void MainWindow::importSongFor(const std::optional<HomeWish>& wish) {
    if (songbookRoot_.empty()) {
        QMessageBox::information(this, tr("Import Song"), tr("Open a song library first."));
        return;
    }
    ImportDialog dialog(findConverter(), builtInConverter(), this);
    if (wish)
        dialog.setWish(wishDisplayText(wish->text, wish->name), wishSearchUrl(*wish));
    const int result = dialog.exec();
    if (result != QDialog::Accepted)
        return;
    const zub::songimport::Imported& imported = dialog.imported();
    const bool tidy = dialog.tidy();

    const auto dest = zub::songimport::destination(songbookRoot_, imported.inputName);
    std::filesystem::path target = dest.path;
    if (const auto existing = zub::songimport::existingSong(songbookRoot_, imported.inputName)) {
        QMessageBox box(QMessageBox::Question, tr("Song already exists"),
                        tr("The song library already has %1.")
                            .arg(toQString(existing->lexically_relative(songbookRoot_))),
                        QMessageBox::Cancel, this);
        box.setInformativeText(tr("Open the existing song, or open the import in its place "
                                  "(nothing is overwritten until you save, and saving asks)?"));
        QPushButton* openExisting = box.addButton(tr("Open Existing"), QMessageBox::AcceptRole);
        QPushButton* importAnyway =
            box.addButton(tr("Import Anyway"), QMessageBox::DestructiveRole);
        box.setDefaultButton(openExisting);
        box.exec();
        if (box.clickedButton() == openExisting) {
            openExistingSong(*existing);
            return;
        }
        if (box.clickedButton() != importAnyway)
            return;
        target = *existing;
    }

    // Only now, with the import accepted, is the open song closed (asking to save).
    if (!closeSong())
        return;
    // Downloaded content is untrusted: it must not inherit "build anyway" approvals given
    // earlier in the session to whatever was at this path.
    approvedConstructs_.erase(target);
    auto* editor = new SongEditor(target, *repository_);
    // Not ready for the songbook until the user says so (Mark Checked): a TODO line under the
    // header, which the song list can filter on.
    editor->openNew(zub::todo::addNotCheckedMark(imported.content));
    const auto meta = zub::parseSong(imported.content);
    const QString title =
        meta ? toQString(zub::latex::toDisplayText(meta->title)) : toQString(target.stem());
    showSong(editor, title, toQString(dest.inputName) + tr(" (imported, not saved)"));
    if (wish)
        pendingWish_ = PendingWish{target, *wish}; // fulfilled by the first save
    QStringList notes;
    for (const std::string& warning : imported.warnings)
        notes << toQString(warning);
    const QString message =
        notes.isEmpty()
            ? tr("Imported %1. Check it, then save with Ctrl+S.").arg(toQString(imported.inputName))
            : notes.join(QStringLiteral(" "));
    statusBar()->showMessage(message, 10000);
    // A separate step on the new editor, so Ctrl+Z shows the raw draft; its message keeps the
    // converter's warnings.
    if (tidy)
        songActions_->startTidy(message);
}

bool MainWindow::openExistingSong(const std::filesystem::path& path) {
    // Already open: keep it as it is (edits, undo history, approvals).
    if (editor_ && editor_->path() == path) {
        noteSongOpenedFrom();
        rememberSong(songbookRoot_, path); // for Home's Continue editing
        editor_->textEdit()->setFocus();
        return true;
    }
    // Load first, so a song that fails to load doesn't cost the open one.
    SongEditor* editor = openEditor(path, {});
    if (!editor)
        return false;
    if (!closeSong()) {
        delete editor;
        return false;
    }
    showSong(editor, {}, toQString(inputNameOf(editor)));
    rememberSong(songbookRoot_, path); // for Home's Continue editing
    return true;
}

void MainWindow::newSongFor(const std::optional<HomeWish>& wish) {
    if (songbookRoot_.empty()) {
        QMessageBox::information(this, tr("New Song"), tr("Open a song library first."));
        return;
    }
    // The songbook's artists keep their folders (some are named by hand: "SuchySlitr").
    std::map<QString, QString> artistFolders;
    for (int row = 0; row < model_->rowCount(); ++row) {
        const auto* song = model_->song(row);
        if (!song)
            continue;
        const QString artist =
            model_->index(row, SongTableModel::Artist).data().toString().trimmed();
        if (!artist.isEmpty())
            artistFolders.emplace(artist, toQString(song->path.parent_path().filename()));
    }
    NewSongDialog dialog(artistFolders, zub::collections(songbookRoot_), this);
    if (wish) {
        // "PavelDobes/NecoOLasce": the artist's folder and the title's words; any other text
        // is the title.
        const qsizetype slash = wish->name.lastIndexOf(QLatin1Char('/'));
        if (wish->name.isEmpty())
            dialog.setSong(wish->text, {});
        else
            dialog.setSong(splitWords(wish->name.mid(slash + 1)),
                           splitWords(wish->name.left(slash)), wish->name.left(slash));
    }
    if (dialog.exec() != QDialog::Accepted)
        return;
    const std::string inputName = toStdString(dialog.inputName());
    if (const auto existing = zub::songimport::existingSong(songbookRoot_, inputName)) {
        QMessageBox box(QMessageBox::Question, tr("Song already exists"),
                        tr("The song library already has %1.")
                            .arg(toQString(existing->lexically_relative(songbookRoot_))),
                        QMessageBox::Cancel, this);
        QPushButton* openExisting = box.addButton(tr("Open Existing"), QMessageBox::AcceptRole);
        box.setDefaultButton(openExisting);
        box.exec();
        if (box.clickedButton() == openExisting)
            openExistingSong(*existing);
        return;
    }
    if (!closeSong())
        return;
    const auto dest = zub::songimport::destination(songbookRoot_, inputName,
                                                   toStdString(dialog.collection()));
    approvedConstructs_.erase(dest.path); // nothing of an earlier file at this path carries over
    const auto song =
        zub::songimport::newSong(toStdString(dialog.title()), toStdString(dialog.artist()));
    auto* editor = new SongEditor(dest.path, *repository_);
    editor->openNew(song.text);
    showSong(editor, dialog.title(), toQString(dest.inputName) + tr(" (new, not saved)"));
    if (wish)
        pendingWish_ = PendingWish{dest.path, *wish}; // fulfilled by the first save
    editor->setCursorByteOffset(song.cursor);
    statusBar()->showMessage(
        tr("New song %1: write it, then save with Ctrl+S.").arg(toQString(dest.inputName)), 10000);
}
