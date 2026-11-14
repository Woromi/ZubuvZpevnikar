#include "MainWindow.hpp"

#include "ConverterRunner.hpp"
#include "ConverterVersion.hpp"
#include "DiffView.hpp"
#include "FretboardWidget.hpp"
#include "ImportDialog.hpp"
#include "LabelFilterBox.hpp"
#include "LabelsDialog.hpp"
#include "PreviewPane.hpp"
#include "PreviewRunner.hpp"
#include "ScoreInputBar.hpp"
#include "ScorePreviewPane.hpp"
#include "ScoreRunner.hpp"
#include "ShrinkDialog.hpp"
#include "SongEditor.hpp"
#include "SongFilterProxy.hpp"
#include "SongTableModel.hpp"
#include "SongbookExporter.hpp"
#include "SongbookScan.hpp"
#include "SongbookSorter.hpp"
#include "SongbookStore.hpp"
#include "SongbooksPage.hpp"
#include "convert.hpp"

#include <KSyntaxHighlighting/Repository>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
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
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
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
#include <zub/core/preview.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/score.hpp>
#include <zub/core/scoreregion.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/songedit.hpp>
#include <zub/core/songimport.hpp>
#include <zub/core/songsuggest.hpp>
#include <zub/core/songtags.hpp>
#include <zub/core/songtidy.hpp>
#include <zub/core/songtodo.hpp>
#include <zub/core/textfile.hpp>

namespace {

const auto songbookPathKey = QStringLiteral("songbook/path");
const auto previewClassKey = QStringLiteral("preview/class");
const auto converterKey = QStringLiteral("converter/path");
const auto geometryKey = QStringLiteral("window/geometry");
const auto languageKey = QStringLiteral("ui/language");
const auto fretboardKey = QStringLiteral("score/fretboard");
const auto songSplitterKey = QStringLiteral("window/songSplitter");

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), repository_(std::make_unique<KSyntaxHighlighting::Repository>()) {
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

    // Window size and pane widths as the user left them.
    const QSettings settings;
    restoreGeometry(settings.value(geometryKey).toByteArray());
    songSplitter_->restoreState(settings.value(songSplitterKey).toByteArray());
}

MainWindow::~MainWindow() {
    converterRunner_->cancel();
    disconnect(converterRunner_, nullptr, this, nullptr);
    // Members are destroyed before QWidget deletes the children, so without this the editors
    // (and its highlighter) would outlive repository_. Disconnect first: no signal may reach
    // a half-destroyed window.
    scoreRunner_->cancel();
    for (const ScoreTab& tab : scoreTabs_) {
        if (tab.editor) {
            disconnect(tab.editor, nullptr, this, nullptr);
            disconnect(tab.editor->textEdit(), nullptr, this, nullptr);
            delete tab.editor;
        }
    }
    scoreTabs_.clear();
    if (editor_) {
        disconnect(editor_, nullptr, this, nullptr);
        disconnect(editor_->textEdit(), nullptr, this, nullptr);
        delete editor_;
        editor_ = nullptr;
    }
}

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));

    QAction* open = file->addAction(tr("&Open Songbook Folder…"));
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
    closeSongAction_->setToolTip(tr("Back to the song list (Ctrl+W)"));
    connect(closeSongAction_, &QAction::triggered, this, &MainWindow::closeSong);

    QAction* import = file->addAction(tr("&Import Song…"));
    import->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(import, &QAction::triggered, this, &MainWindow::importSong);

    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"));
    QMenu* songs = menuBar()->addMenu(tr("&Songs"));
    songs->addAction(songbooksAction_);
    QAction* scan = songs->addAction(tr("&Scan Songbook…"));
    scan->setToolTip(tr("Find the songs Tidy, Check Song or Make Smaller would change"));
    connect(scan, &QAction::triggered, this, &MainWindow::scanSongbook);
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    createEditMenu();

    // The language applies at the next start (the translators are installed in main()).
    // The choices sit directly in the menu under a heading: a lone submenu was so narrow that its
    // arrow ran into the text.
    QMenu* languageMenu = menuBar()->addMenu(tr("&Settings"));
    languageMenu->addSection(tr("Language"));
    auto* languages = new QActionGroup(this);
    languages->setExclusive(true);
    const QString current = QSettings().value(languageKey).toString();
    // The language names are not translated: everybody must find their own language.
    const std::pair<QString, QString> choices[] = {
        {QString(), tr("System Default")},
        {QStringLiteral("cs"), QStringLiteral("Čeština")},
        {QStringLiteral("en"), QStringLiteral("English")},
    };
    for (const auto& [code, name] : choices) {
        QAction* action = languageMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(code == current);
        languages->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] {
            QSettings().setValue(languageKey, code);
            QMessageBox::information(this, tr("Language"),
                                     tr("The language changes when %1 is started again.")
                                         .arg(QApplication::applicationDisplayName()));
        });
    }

    QAction* find = new QAction(this);
    find->setShortcut(QKeySequence::Find);
    connect(find, &QAction::triggered, this, [this] {
        if (editor_) // no find & replace in the editor yet
            return;
        search_->setFocus();
        search_->selectAll();
    });
    addAction(find);
}

void MainWindow::createCentralWidget() {
    model_ = new SongTableModel(this);
    proxy_ = new SongFilterProxy(this);
    proxy_->setSourceModel(model_);

    pages_ = new QStackedWidget(this);

    listPage_ = new QWidget(pages_);
    auto* listLayout = new QVBoxLayout(listPage_);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    QToolBar* filters = new QToolBar(tr("Filter"), listPage_);
    filters->setMovable(false);

    search_ = new QLineEdit(filters);
    search_->setPlaceholderText(tr("Search title, artist, key… (Ctrl+F)"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(320);
    connect(search_, &QLineEdit::textChanged, proxy_, &SongFilterProxy::setSearchText);
    filters->addWidget(search_);

    language_ = new QComboBox(filters);
    language_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(language_, &QComboBox::currentIndexChanged, this,
            [this] { proxy_->setLanguage(language_->currentData().toString()); });
    filters->addWidget(language_);

    auto* favouritesOnly = new QCheckBox(tr("Only favourites"), filters);
    favouritesOnly->setToolTip(tr("Songs marked with ★"));
    connect(favouritesOnly, &QCheckBox::toggled, proxy_, &SongFilterProxy::setFavouritesOnly);
    filters->addWidget(favouritesOnly);

    labelFilter_ = new LabelFilterBox(model_, filters);
    connect(labelFilter_, &LabelFilterBox::labelChanged, proxy_, &SongFilterProxy::setLabel);
    filters->addWidget(labelFilter_);

    auto* todoOnly = new QCheckBox(tr("Only songs with TODOs"), filters);
    todoOnly->setToolTip(tr("Songs with a TODO comment, e.g. imported songs not checked yet"));
    connect(todoOnly, &QCheckBox::toggled, proxy_, &SongFilterProxy::setTodoOnly);
    filters->addWidget(todoOnly);

    // Shown after a songbook scan (Songs → Scan Songbook…).
    changesOnly_ = new QCheckBox(tr("Only songs with possible changes"), filters);
    changesOnly_->setToolTip(tr("Songs the last Scan Songbook found changes in (this session)"));
    connect(changesOnly_, &QCheckBox::toggled, proxy_, &SongFilterProxy::setChangesOnly);
    changesOnlyAction_ = filters->addWidget(changesOnly_);
    changesOnlyAction_->setVisible(false);

    filters->addSeparator();
    songbooksAction_ =
        new QAction(QIcon::fromTheme(QStringLiteral("view-media-playlist"),
                                     QIcon::fromTheme(QStringLiteral("folder-documents"))),
                    tr("&Songbooks…"), this);
    songbooksAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    songbooksAction_->setToolTip(tr("Assemble songbooks (Ctrl+B)"));
    connect(songbooksAction_, &QAction::triggered, this, &MainWindow::showSongbooks);
    filters->addAction(songbooksAction_);
    filters->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // Breathing room: the widgets sat edge to edge.
    filters->layout()->setSpacing(8);
    filters->setContentsMargins(6, 4, 6, 4);

    table_ = new QTableView(listPage_);
    table_->setModel(proxy_);
    table_->setSortingEnabled(true);
    table_->sortByColumn(SongTableModel::Title, Qt::AscendingOrder);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setSectionResizeMode(SongTableModel::Title, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(SongTableModel::Artist, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(SongTableModel::Todo, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(SongTableModel::Changes, QHeaderView::Stretch);
    table_->setColumnHidden(SongTableModel::Changes, true); // until a songbook scan
    table_->horizontalHeader()->setSectionResizeMode(SongTableModel::Favourite,
                                                     QHeaderView::ResizeToContents);
    // Click the ★ to toggle the favourite; right-click for the labels.
    connect(table_, &QAbstractItemView::clicked, this, [this](const QModelIndex& index) {
        if (index.column() != SongTableModel::Favourite)
            return;
        if (const auto* song = model_->song(proxy_->mapToSource(index).row()))
            toggleFavourite(song->path);
    });
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QWidget::customContextMenuRequested, this, &MainWindow::showTableMenu);
    connect(table_, &QAbstractItemView::activated, this, &MainWindow::openSong);

    listLayout->addWidget(filters);
    listLayout->addWidget(table_);
    table_->horizontalHeader()->setMinimumSectionSize(60);

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
    scorePreview_ = new ScorePreviewPane(previewStack_);
    previewStack_->addWidget(scorePreview_);

    converterRunner_ = new ConverterRunner(this);
    connect(converterRunner_, &ConverterRunner::finished, this, &MainWindow::onConverterFinished);

    // Scores: the song's \lilypondfile references become tabs (a moment after the text stops
    // changing); the score preview rebuilds a moment after the score text stops changing.
    scoreSyncTimer_ = new QTimer(this);
    scoreSyncTimer_->setSingleShot(true);
    scoreSyncTimer_->setInterval(600);
    connect(scoreSyncTimer_, &QTimer::timeout, this, &MainWindow::syncScoreTabs);
    scorePreviewTimer_ = new QTimer(this);
    scorePreviewTimer_->setSingleShot(true);
    scorePreviewTimer_->setInterval(600);
    connect(scorePreviewTimer_, &QTimer::timeout, this, [this] { requestScorePreview(); });
    // The fretboard follows the score's cursor and text a moment after they stop moving.
    fretboardTimer_ = new QTimer(this);
    fretboardTimer_->setSingleShot(true);
    fretboardTimer_->setInterval(80);
    connect(fretboardTimer_, &QTimer::timeout, this, &MainWindow::updateFretboard);
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
    // Actions on the selected notes of a score: one undo step each, the notes stay selected.
    highlightTimer_ = new QTimer(this);
    highlightTimer_->setSingleShot(true);
    highlightTimer_->setInterval(120);
    connect(highlightTimer_, &QTimer::timeout, this, &MainWindow::updateScoreHighlights);
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
                        statusBar()->showMessage(
                            tr("A slur is already open here or the selection has slur marks."),
                            6000);
                    if (why == zub::scoreregion::Refusal::CrossesBlock)
                        statusBar()->showMessage(
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
    connect(scorePreview_, &ScorePreviewPane::notesDragged, this, &MainWindow::selectScoreNotes);
    connect(scorePreview_, &ScorePreviewPane::noteClicked, this, &MainWindow::clickScoreNote);
    connect(scorePreview_, &ScorePreviewPane::noteDoubleClicked, this,
            &MainWindow::doubleClickScoreNote);
    scoreRunner_ = new ScoreRunner(this);
    connect(scoreRunner_, &ScoreRunner::started, scorePreview_, &ScorePreviewPane::showBuilding);
    connect(scoreRunner_, &ScoreRunner::finished, this, &MainWindow::showScoreResult);
    connect(scorePreview_, &ScorePreviewPane::rebuildRequested, this,
            [this] { requestScorePreview(true); });
    connect(scorePreview_, &ScorePreviewPane::errorActivated, this, [this](int line, int) {
        if (SongEditor* editor = currentEditor(); editor && !onSongTab())
            editor->goToLine(line);
    });
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeScoreTab);

    songSplitter_->addWidget(editorHost);
    songSplitter_->addWidget(previewStack_);
    // A collapsed editor would be saved in the state and every later start would open it hidden.
    songSplitter_->setCollapsible(0, false);
    songSplitter_->setStretchFactor(0, 1);
    songSplitter_->setStretchFactor(1, 1);
    songSplitter_->setSizes({1, 1});
    songLayout->addWidget(songSplitter_);

    songbookStore_ = new SongbookStore({}, this);
    songbookSorter_ = new SongbookSorter(this);
    songbookExporter_ = new SongbookExporter(this);
    songbooksPage_ =
        new SongbooksPage(model_, songbookStore_, songbookSorter_, songbookExporter_, pages_);
    connect(songbooksPage_, &SongbooksPage::backRequested, this, [this] {
        pages_->setCurrentWidget(listPage_);
        updateWindowTitle();
    });
    connect(songbookExporter_, &SongbookExporter::finished, this,
            [this](const QString& message) { statusBar()->showMessage(message, 15000); });

    pages_->addWidget(listPage_);
    pages_->addWidget(songPage_);
    pages_->addWidget(songbooksPage_);
    setCentralWidget(pages_);

    connect(proxy_, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateStatus);
    connect(proxy_, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateStatus);
    connect(proxy_, &QAbstractItemModel::modelReset, this, &MainWindow::updateStatus);
    connect(proxy_, &QAbstractItemModel::layoutChanged, this, &MainWindow::updateStatus);

    updateLanguageFilter();
}

void MainWindow::openInitialSongbook(const QString& commandLinePath) {
    if (!commandLinePath.isEmpty()) {
        if (!openSongbook(toPath(commandLinePath), {}))
            chooseSongbookFolder();
        return;
    }

    // Only the remembered folder: no guesses. If it is missing (e.g. an unplugged disk), say
    // so; it stays remembered unless the user picks another one.
    const QString remembered = QSettings{}.value(songbookPathKey).toString();
    if (!remembered.isEmpty() &&
        openSongbook(toPath(QDir::cleanPath(remembered)), {.showErrors = true, .remember = false}))
        return;
    chooseSongbookFolder();
}

void MainWindow::chooseSongbookFolder() {
    const QString start = songbookRoot_.empty() ? QDir::homePath() : toQString(songbookRoot_);
    const QString dir =
        QFileDialog::getExistingDirectory(this, tr("Open Songbook Folder (ZubuvZpevnik)"), start);
    // Open editors belong to the old songbook; they are closed only once the new one is valid.
    if (!dir.isEmpty())
        openSongbook(toPath(dir), {.closeEditors = true});
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
                this, tr("Not a songbook folder"),
                tr("Cannot open the songbook:\n%1").arg(toQString(library.error())));
        }
        return false;
    }
    if (options.closeEditors && !closeSong())
        return false;

    songbookRoot_ = root;
    songbooksPage_->setSongbookRoot(root);
    songbookExporter_->setSongbookRoot(root);
    songbookSorter_->setTools(root, QStandardPaths::findExecutable(QStringLiteral("texlua")));
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
    updateLanguageFilter();
    preview_->setDocumentClasses(
        zub::preview::documentClasses(root),
        // The main songbook's class unless another was chosen.
        QSettings{}.value(previewClassKey, QStringLiteral("velkyzpevnik")).toString());
    updateStatus();
    listTitle_ = QApplication::applicationDisplayName() + QStringLiteral(" — ") + toQString(root);
    updateWindowTitle();
    return true;
}

void MainWindow::reloadSongbook() {
    if (!songbookRoot_.empty())
        openSongbook(songbookRoot_, {.showErrors = true, .remember = false});
}

void MainWindow::showSongbooks() {
    if (editor_ || songbookRoot_.empty()) // a song is open: back to the list first
        return;
    songbooksPage_->reloadList();
    pages_->setCurrentWidget(songbooksPage_);
}

void MainWindow::updateLanguageFilter() {
    const QString current = language_->currentData().toString();
    std::set<QString> languages;
    for (int row = 0; row < model_->rowCount(); ++row) {
        const QString lang = model_->index(row, 0).data(SongTableModel::LanguageRole).toString();
        if (!lang.isEmpty())
            languages.insert(lang);
    }

    const QSignalBlocker blocker(language_);
    language_->clear();
    language_->addItem(tr("All languages"), QString{});
    for (const QString& lang : languages)
        language_->addItem(lang, lang);
    const int index = language_->findData(current);
    language_->setCurrentIndex(index >= 0 ? index : 0);
    proxy_->setLanguage(language_->currentData().toString());
}

void MainWindow::updateStatus() {
    if (songbookRoot_.empty()) {
        countLabel_->setText(tr("No songbook open. Use File → Open Songbook Folder…"));
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

void MainWindow::showSong(SongEditor* editor, const QString& title, const QString& toolTip) {
    editor_ = editor;
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
    connect(editor, &SongEditor::saved, this, [this, editor](const std::string& content) {
        changesAction_->setChecked(false);
        refreshSongMetadata(editor, content);
        syncScoreTabs();
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
        syncScoreTabs();
        requestPreview();
    });

    // Toolbar state follows the editor; dies with it (context: this, disconnected on close).
    QPlainTextEdit* edit = editor->textEdit();
    connect(edit, &QPlainTextEdit::undoAvailable, this, [this] { updateEditState(); });
    connect(edit, &QPlainTextEdit::textChanged, this, [this] {
        refreshChanges();
        updateMarkChecked();
        favouriteTimer_->start();
        scoreSyncTimer_->start(); // a \lilypondfile may have been typed
        updateScoreWidthBars();
        fretboardTimer_->start(); // lang or key may have changed
    });
    connect(edit, &QPlainTextEdit::redoAvailable, this, [this] { updateEditState(); });
    connect(edit, &QPlainTextEdit::copyAvailable, this, [this] { updateEditState(); });

    pages_->setCurrentWidget(songPage_);
    syncScoreTabs();
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
    return !(editor_ && !confirmClose(editor_));
}

bool MainWindow::confirmCloseScore(const std::string& name, SongEditor* score) {
    if (!score->isNew())
        return confirmClose(score);
    const auto answer = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("Discard the new score %1? Its \\lilypondfile line is removed from the song too.")
            .arg(toQString(name)),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        if (auto saved = score->save(); !saved) {
            QMessageBox::warning(this, tr("Cannot save score"), saved.error());
            return false;
        } else if (*saved == SongEditor::SaveResult::Cancelled) {
            return false;
        }
        return true;
    }
    removeScoreReference(name);
    return true;
}

void MainWindow::removeScoreReference(const std::string& name) {
    if (!editor_ || editor_->textEdit()->isReadOnly())
        return;
    replaceSongText(zub::songedit::removeScoreReference(editor_->content(), name));
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
    if (pages_->currentWidget() == songPage_) {
        closeSong();
    } else if (pages_->currentWidget() == songbooksPage_) {
        pages_->setCurrentWidget(listPage_);
        updateWindowTitle();
    }
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
    if (converterRunner_->isRunning())
        statusBar()->clearMessage(); // "Tidying…"; cancel() reports nothing
    converterRunner_->cancel();
    runEditor_.clear();
    clearTidyChanges();
    changesAction_->setChecked(false);
    // No more reload dialogs from an editor that is gone but not yet deleted.
    editor->stopWatching();
    approvedConstructs_.erase(path); // approvals last as long as the song is open
    removeScoreTabs();
    disconnect(editor, nullptr, this, nullptr);
    disconnect(editor->textEdit(), nullptr, this, nullptr);
    editor_ = nullptr;
    editor->setParent(nullptr); // out of the stack now; the next song takes index 0
    editor->deleteLater();

    pages_->setCurrentWidget(listPage_);
    updateSongActions();
    updateWindowTitle();
    requestPreview(); // cancels the build and clears the pane

    // Back on the list with the song that was open selected.
    const int row = model_->rowOf(path);
    if (row >= 0) {
        const QModelIndex index = proxy_->mapFromSource(model_->index(row, 0));
        if (index.isValid()) {
            table_->selectRow(index.row());
            table_->setCurrentIndex(index);
            table_->scrollTo(index);
        }
    }
    table_->setFocus();
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmCloseAll()) {
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
    songToolBar_->addSeparator();

    tidyAction_ = new QAction(QIcon::fromTheme(QStringLiteral("tools-wizard")), tr("Tidy"), this);
    tidyAction_->setToolTip(tr(
        "Tidy the song with chords_converter: repeated choruses, chord positions (one undo step)"));
    connect(tidyAction_, &QAction::triggered, this, [this] { startTidy(); });
    songToolBar_->addAction(tidyAction_);

    makeSmallerAction_ =
        new QAction(QIcon::fromTheme(QStringLiteral("zoom-out")), tr("Make Smaller"), this);
    makeSmallerAction_->setToolTip(tr("Ask chords_converter for changes that make the song print "
                                      "smaller; apply one (one undo step), then ask again"));
    connect(makeSmallerAction_, &QAction::triggered, this,
            [this] { startProposals(SongAction::Shrink); });
    songToolBar_->addAction(makeSmallerAction_);

    joinLinesAction_ = new QAction(QIcon::fromTheme(QStringLiteral("format-justify-fill")),
                                   tr("Join Lines"), this);
    joinLinesAction_->setToolTip(
        tr("Join every two lines of a kind of part into one, where they still fit the column "
           "(typesets the song on A4 and A5: a few seconds)"));
    connect(joinLinesAction_, &QAction::triggered, this,
            [this] { startProposals(SongAction::Join); });
    songToolBar_->addAction(joinLinesAction_);

    checkAction_ = new QAction(QIcon::fromTheme(QStringLiteral("tools-check-spelling")),
                               tr("Check Song"), this);
    checkAction_->setToolTip(
        tr("Look for mistakes: typos where a line's copies differ, apostrophes typed two ways, "
           "a chorus typed as a verse"));
    connect(checkAction_, &QAction::triggered, this, [this] { startProposals(SongAction::Check); });
    songToolBar_->addAction(checkAction_);

    markCheckedAction_ =
        new QAction(QIcon::fromTheme(QStringLiteral("checkmark")), tr("Mark Checked"), this);
    markCheckedAction_->setToolTip(
        tr("The song is ready for the songbook: remove its \"not checked yet\" TODO line "
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
    add(tr("Insert Part…"),
        {QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)},
        [this] { showPartMenu(); });
    edit->addSeparator();
    add(tr("New Score…"), {}, [this] { newScore(); });
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
    if (!editor_)
        return nullptr;
    const int index = tabs_->currentIndex();
    if (index <= 0 || index > static_cast<int>(scoreTabs_.size()))
        return editor_;
    return scoreTabs_[static_cast<std::size_t>(index - 1)].editor.data();
}

bool MainWindow::onSongTab() const {
    return editor_ && tabs_->currentIndex() <= 0;
}

bool MainWindow::anyModified() const {
    if (!editor_)
        return false;
    return editor_->isModified() || std::ranges::any_of(scoreTabs_, [](const ScoreTab& tab) {
               return tab.editor && tab.editor->isModified();
           });
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
    updateScoreWidthBars();
    updateRegionActions();
    SongEditor* current = currentEditor();
    const bool songTab = onSongTab();
    closeSongAction_->setEnabled(open);
    saveAction_->setEnabled(current && current->isModified());
    saveAction_->setText(current && !songTab ? tr("&Save Score") : tr("&Save Song"));
    const bool modified = open && editor_->isModified();
    changesAction_->setEnabled(modified && songTab);
    if (!modified && changesAction_->isChecked())
        changesAction_->setChecked(false);
    updateEditState();
    if (!open) {
        for (QAction* action : {tidyAction_, makeSmallerAction_, joinLinesAction_, checkAction_,
                                markCheckedAction_, favouriteAction_, labelsAction_})
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
    // The song actions work on the song's text.
    const bool converterFree = songTab && !editor_->textEdit()->isReadOnly() &&
                               !converterRunner_->isRunning() && !findConverter().empty();
    tidyAction_->setEnabled(converterFree);
    for (QAction* action : {makeSmallerAction_, joinLinesAction_})
        action->setEnabled(converterFree && !songbookRoot_.empty());
    checkAction_->setEnabled(converterFree); // check needs no songbook
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
        model_->updateSong(path, std::move(*newMeta), zub::todo::notes(updated));
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
    for (const QModelIndex& index : table_->selectionModel()->selectedRows()) {
        if (const auto* song = model_->song(proxy_->mapToSource(index).row()))
            paths.push_back(song->path);
    }
    return paths;
}

void MainWindow::showTableMenu(const QPoint& position) {
    const QModelIndex index = table_->indexAt(position);
    if (!index.isValid())
        return;
    if (!table_->selectionModel()->isRowSelected(index.row(), {}))
        table_->selectRow(index.row());
    QMenu menu(this);
    QAction* labels = menu.addAction(tr("Labels…"));
    if (menu.exec(table_->viewport()->mapToGlobal(position)) == labels)
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

void MainWindow::startTidy(const QString& note) {
    SongEditor* editor = editor_;
    if (!editor || editor->textEdit()->isReadOnly() || converterRunner_->isRunning())
        return;
    const std::filesystem::path converter = findConverter();
    if (converter.empty()) {
        statusBar()->showMessage(tr("Tidy needs chords_converter, which was not found."), 8000);
        return;
    }
    if (!converterUsable(converter, tr("Tidy")))
        return;
    runKind_ = RunKind::Tidy;
    runEditor_ = editor;
    runInput_ = editor->content();
    tidyNote_ = note;
    // Before start(): a failure to start may be reported from inside it, and the result
    // handler then replaces this message.
    statusBar()->showMessage(tr("Tidying…"));
    if (!converterRunner_->start(
            [converter](const std::filesystem::path& file) {
                return zub::songtidy::prepare(converter, file);
            },
            runInput_, toQString(editor->path().stem())))
        return;
    updateSongActions();
}

void MainWindow::onConverterFinished(const std::expected<ConverterOutput, QString>& output) {
    switch (runKind_) {
    case RunKind::Tidy:
        onTidyFinished(output);
        break;
    case RunKind::Suggest:
        onSuggestFinished(output);
        break;
    case RunKind::Apply:
        onApplyFinished(output);
        break;
    }
}

bool MainWindow::converterUsable(const std::filesystem::path& converter, const QString& action) {
    const QString problem = converterProblem(converter);
    if (problem.isEmpty())
        return true;
    QMessageBox::warning(this, action, problem);
    return false;
}

SongEditor* MainWindow::takeRun(std::string& input, const QString& what) {
    SongEditor* editor = runEditor_.data();
    input = std::move(runInput_);
    runEditor_.clear();
    runInput_.clear();
    if (!editor || editor != editor_ || editor->content() != input) {
        // The song was closed, switched or edited while the converter ran: its result no longer
        // fits the text.
        statusBar()->showMessage(tr("%1 discarded: the song changed while it ran.").arg(what),
                                 8000);
        return nullptr;
    }
    return editor;
}

void MainWindow::onTidyFinished(const std::expected<ConverterOutput, QString>& output) {
    std::string input;
    const QString note = std::move(tidyNote_);
    tidyNote_.clear();
    const auto finishUp = [this] { updateSongActions(); };

    SongEditor* editor = takeRun(input, tr("Tidy"));
    if (!editor) {
        finishUp();
        return;
    }
    std::expected<zub::songtidy::Tidied, QString> result = std::unexpected(output.error_or({}));
    if (output) {
        auto interpreted = zub::songtidy::interpret(output->exitCode, output->out, output->err);
        if (interpreted)
            result = std::move(*interpreted);
        else
            result = std::unexpected(toQString(interpreted.error()));
    }
    if (!result) {
        statusBar()->clearMessage();
        QMessageBox::warning(this, tr("Tidy failed"), result.error());
        finishUp();
        return;
    }

    const zub::songtidy::Tidied& tidied = *result;
    editor->replaceContent(tidied.content);
    // Against what the editor now holds, not the raw output: a CRLF song keeps CRLF even where
    // the converter wrote a lone LF.
    const std::string after = editor->content();
    const auto lines = zub::diff::lines(input, after);

    tidyList_->clear();
    for (const auto& change : tidied.changes) {
        const int line = zub::diff::mapOldLine(lines, change.line);
        auto* item = new QListWidgetItem(
            tr("line %1: %2").arg(line).arg(toQString(change.description)), tidyList_);
        item->setData(Qt::UserRole, line);
    }
    tidyPanel_->setVisible(!tidied.changes.empty());

    QString message;
    if (tidied.changes.empty())
        message = tr("Tidy: nothing to change");
    else
        message = tr("Tidy: %n change(s)", nullptr, static_cast<int>(tidied.changes.size()));
    // tidy moves chords to their syllable's start only for these languages (it hyphenates), and
    // skips that rule silently otherwise: say so, or "nothing to change" looks like "all fine".
    std::string lang;
    if (const auto meta = zub::parseSong(after)) {
        if (const auto it = meta->keys.find("lang"); it != meta->keys.end())
            lang = it->second;
    }
    if (lang.empty())
        message += tr(" (chord positions not checked: set lang in the song header)");
    else if (lang != "czech" && lang != "slovak" && lang != "english")
        message += tr(" (chord positions are only checked for lang czech, slovak or english)");
    if (!note.isEmpty())
        message += QStringLiteral(" — ") + note;
    statusBar()->showMessage(message, 10000);
    finishUp();
    requestPreview();
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
    if (!model_->updateSong(editor->path(), *meta, todos)) {
        // A new song's first save: it joins the table now.
        model_->addSong({editor->path(), inputNameOf(editor), std::move(*meta), std::move(todos)});
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
    scoreWorkDir_ = toPath(QDir(cache).absoluteFilePath(
        dirName == QStringLiteral("preview")
            ? QStringLiteral("score")
            : QStringLiteral("score-%1").arg(QApplication::applicationPid())));
}

std::string MainWindow::inputNameOf(const SongEditor* editor) const {
    // songs/chords/Artist/Title.tex -> "Artist/Title"
    const auto relative =
        editor->path().lexically_relative(songbookRoot_ / "songs" / "chords").replace_extension();
    const auto u8 = relative.generic_u8string();
    return {u8.begin(), u8.end()};
}

namespace {

std::filesystem::path pathOfName(const std::string& name) {
    return std::filesystem::path(std::u8string(name.begin(), name.end()));
}

// "Artist/X.ly": the name a \lilypondfile can carry that the app edits as a score.
bool validScoreName(const std::string& name) {
    return !zub::invalidInputName(name) && name.size() > 3 && name.ends_with(".ly") &&
           name[name.size() - 4] != '/';
}

} // namespace

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
        if (!resolved || !scorePath(*resolved).empty())
            continue;
        const std::string& name = *resolved;
        const int tab = scoreTabIndex(name);
        if (tab > 0 && scoreTabs_[static_cast<std::size_t>(tab - 1)].editor)
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
    request.workDir = previewWorkDir_;
    request.inputName = inputNameOf(editor);
    request.tinwhistleSong = zub::songbook::inTinwhistleTree(songbookRoot_, editor->path());
    request.songContent = std::move(content);
    request.documentClass = toStdString(preview_->documentClass());
    // Unsaved scores are built as they are in their editors.
    for (const ScoreTab& tab : scoreTabs_) {
        if (tab.editor && (tab.editor->isModified() || tab.editor->isNew()))
            request.fileOverrides.emplace_back(tab.name, tab.editor->content());
    }
    pdfStale_ = false;
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
    sources.push_back({editor->path(), {}, zub::safety::unknownConstructs(content)});
    for (const std::string& reference : zub::songbook::referencedFiles(content)) {
        const auto resolved = zub::songbook::resolveScoreReference(reference);
        if (!resolved) {
            // Built from wherever TEXINPUTS finds it, so it cannot be checked: needs confirmation.
            sources.push_back({std::filesystem::path(editor->path().native() + "\\lilypondfile{" +
                                                     reference + "}"),
                               {},
                               {"\\lilypondfile{" + reference + "}"}});
            continue;
        }
        const std::string& name = *resolved;
        // What the editor holds, and the file on disk (the converter's measuring builds that).
        std::vector<std::string> texts;
        std::filesystem::path key = scorePath(name);
        const int tab = scoreTabIndex(name);
        if (tab > 0) {
            if (const SongEditor* score = scoreTabs_[static_cast<std::size_t>(tab - 1)].editor) {
                texts.push_back(score->content());
                key = score->path();
            }
        }
        if (!scorePath(name).empty()) {
            if (auto disk = zub::readTextFile(scorePath(name)))
                texts.push_back(std::move(*disk));
        }
        RiskSource source{key, toQString(name), {}};
        for (const std::string& text : texts) {
            for (std::string& risk : zub::safety::lilypondRisks(text)) {
                if (std::ranges::find(source.risks, risk) == source.risks.end())
                    source.risks.push_back(std::move(risk));
            }
        }
        if (!source.risks.empty() && !key.empty())
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

// ---- Scores ----

std::filesystem::path MainWindow::scorePath(const std::string& name) const {
    if (songbookRoot_.empty() || !validScoreName(name))
        return {};
    return zub::songbook::findScoreFile(songbookRoot_,
                                        editor_ ? editor_->path() : std::filesystem::path(), name);
}

std::filesystem::path MainWindow::newScorePath(const std::string& name) const {
    const bool tinwhistle =
        editor_ && zub::songbook::inTinwhistleTree(songbookRoot_, editor_->path());
    return songbookRoot_ / (tinwhistle ? "songs/tinwhistle" : "songs/chords") / pathOfName(name);
}

int MainWindow::scoreTabIndex(const std::string& name) const {
    for (std::size_t i = 0; i < scoreTabs_.size(); ++i) {
        if (scoreTabs_[i].name == name)
            return static_cast<int>(i) + 1;
    }
    return -1;
}

void MainWindow::syncScoreTabs() {
    scoreSyncTimer_->stop();
    if (!editor_)
        return;
    std::vector<std::string> references;
    for (std::string& name : zub::songbook::referencedFiles(editor_->content())) {
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

void MainWindow::addScoreTab(const std::string& name, SongEditor* editor) {
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
    // Insert and Clear work anywhere on the tab while something is staged.
    auto* insertAction = new QAction(tr("Insert Staged Notes"), page);
    insertAction->setObjectName(QStringLiteral("scoreInsertStaged"));
    insertAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Return),
                                QKeySequence(Qt::CTRL | Qt::Key_Enter)});
    insertAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    insertAction->setEnabled(false);
    auto* clearAction = new QAction(tr("Clear Staged Notes"), page);
    clearAction->setObjectName(QStringLiteral("scoreClearStaged"));
    clearAction->setShortcut(QKeySequence(Qt::Key_Escape));
    clearAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    clearAction->setEnabled(false);
    page->addAction(insertAction);
    page->addAction(clearAction);
    connect(insertAction, &QAction::triggered, this, [this, fretboard] { insertStagedNotes(fretboard); });
    connect(clearAction, &QAction::triggered, fretboard, &FretboardWidget::clearStaged);
    connect(fretboard, &FretboardWidget::insertRequested, this,
            [this, fretboard] { insertStagedNotes(fretboard); });
    connect(inputBar, &ScoreInputBar::insertRequested, this,
            [this, fretboard] { insertStagedNotes(fretboard); });
    connect(inputBar, &ScoreInputBar::clearRequested, fretboard, &FretboardWidget::clearStaged);
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
                statusBar()->showMessage(tr("The cursor is in a comment."), 5000);
                return;
            }
            current->insertScoreRest(toStdString(tab.inputBar->duration()));
            current->textEdit()->setFocus();
        }
    });
    connect(inputBar, &ScoreInputBar::barCheckRequested, this, [this] {
        if (SongEditor* current = currentEditor(); current && !onSongTab()) {
            if (current->cursorInScoreComment()) {
                statusBar()->showMessage(tr("The cursor is in a comment."), 5000);
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
            editor = new SongEditor(path, *repository_, nullptr, SongEditor::Kind::Score);
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

void MainWindow::updateScoreWidthBars() {
    const bool enabled = editor_ && !editor_->textEdit()->isReadOnly();
    const std::string content = editor_ ? editor_->content() : std::string();
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

void MainWindow::setScoreWidth(const std::string& name, bool narrow) {
    if (editor_)
        replaceSongText(zub::songedit::setScoreWidth(editor_->content(), name, narrow));
    updateScoreWidthBars();
    requestPreview();
}

void MainWindow::attachScoreEditor(int tabIndex, SongEditor* editor) {
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
        updateWindowTitle();
        updateSongActions();
    });
    connect(editor, &SongEditor::saved, this, [this] { requestPreview(); });
    connect(editor, &SongEditor::reloadedFromDisk, this, [this, editor] {
        statusBar()->showMessage(
            tr("%1 changed on disk and was reloaded.").arg(toQString(editor->path().filename())),
            5000);
        if (currentEditor() == editor)
            requestScorePreview();
        requestPreview();
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
            scorePreview_->setHighlightedNotes({});
            scorePreview_->setSelectableNotes({});
            scorePreview_->setCaretNote({});
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
    connect(edit, &QPlainTextEdit::undoAvailable, this, [this] { updateEditState(); });
    connect(edit, &QPlainTextEdit::redoAvailable, this, [this] { updateEditState(); });
    connect(edit, &QPlainTextEdit::copyAvailable, this, [this] { updateEditState(); });
}

void MainWindow::updateScoreTabTitle(const SongEditor* editor) {
    for (std::size_t i = 0; i < scoreTabs_.size(); ++i) {
        if (scoreTabs_[i].editor == editor) {
            tabs_->setTabText(static_cast<int>(i) + 1,
                              toQString(editor->path().filename()) +
                                  (editor->isModified() ? QStringLiteral("*") : QString()));
        }
    }
}

void MainWindow::createMissingScore(const std::string& name) {
    const int index = scoreTabIndex(name);
    if (index < 0 || scoreTabs_[static_cast<std::size_t>(index - 1)].editor)
        return;
    auto* editor =
        new SongEditor(newScorePath(name), *repository_, nullptr, SongEditor::Kind::Score);
    const auto skeleton = zub::score::newScore();
    editor->openNew(skeleton.text);
    editor->setCursorByteOffset(skeleton.cursor);
    attachScoreEditor(index, editor);
    tabs_->setCurrentIndex(index);
}

void MainWindow::dropScoreTab(int tabIndex) {
    if (tabIndex < 1 || tabIndex > static_cast<int>(scoreTabs_.size()))
        return;
    ScoreTab tab = scoreTabs_[static_cast<std::size_t>(tabIndex - 1)];
    if (tab.editor) {
        tab.editor->stopWatching();
        disconnect(tab.editor, nullptr, this, nullptr);
        disconnect(tab.editor->textEdit(), nullptr, this, nullptr);
        approvedConstructs_.erase(tab.editor->path());
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
    updateWindowTitle();
    updateSongActions();
}

void MainWindow::closeScoreTab(int tabIndex) {
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
    if (editor_) {
        const auto references = zub::songbook::referencedFiles(editor_->content());
        if (std::ranges::find(references, name) != references.end())
            dismissedScores_.insert(name);
    }
    dropScoreTab(tabIndex);
    requestPreview(); // an unsaved score that was discarded must not stay in the PDF
}

void MainWindow::removeScoreTabs() {
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
                approvedConstructs_.erase(tab.editor->path());
            }
            tab.page->hide();
            tab.page->deleteLater();
        }
        while (tabs_->count() > 1)
            tabs_->removeTab(1);
        scoreTabs_.clear();
        tabs_->setCurrentIndex(0);
    }
    previewStack_->setCurrentWidget(preview_);
}

void MainWindow::onTabChanged() {
    const bool song = tabs_->currentIndex() <= 0;
    previewStack_->setCurrentWidget(song ? static_cast<QWidget*>(preview_) : scorePreview_);
    scorePreviewTimer_->stop();
    scoreLinksFresh_ = false;
    scorePreview_->setSelectableNotes({});
    scorePreview_->setCaretNote({});
    if (song) {
        scoreRunner_->cancel();
        if (pdfStale_ && editor_)
            requestPreview();
    } else {
        scorePreview_->clear();
        requestScorePreview();
    }
    updateSongActions();
    updateFretboard();
    if (SongEditor* editor = currentEditor())
        editor->textEdit()->setFocus();
}

std::optional<zub::scoreregion::Range> MainWindow::singleSelectedEvent(SongEditor* editor) const {
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

void MainWindow::updateFretboard() {
    fretboardTimer_->stop();
    if (onSongTab() || !editor_)
        return;
    const auto index = static_cast<std::size_t>(tabs_->currentIndex() - 1);
    if (index >= scoreTabs_.size())
        return;
    ScoreTab& tab = scoreTabs_[index];
    if (!tab.editor || !tab.fretboard)
        return;
    // The score's own text decides the tuning; the song's lang and key the note names.
    const std::string text = tab.editor->content();
    const auto meta = zub::parseSong(editor_->content());
    const bool german = meta && zub::score::germanNames(meta->value("lang"));
    const bool flats = meta && zub::fretboard::prefersFlats(meta->value("key"));
    const auto tuning = zub::fretboard::tuningOf(text);
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

void MainWindow::insertStagedNotes(FretboardWidget* fretboard) {
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab() || fretboard->staged().empty())
        return;
    ScoreTab& tab = scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)];
    if (tab.fretboard != fretboard)
        return;
    const auto target = tab.replaceTarget;
    const bool replacing = target && singleSelectedEvent(editor) == target;
    if (!replacing && editor->cursorInScoreComment()) {
        statusBar()->showMessage(tr("The cursor is in a comment."), 5000);
        return;
    }
    // From the lowest string (the highest number) to string 1, like the real scores.
    auto positions = fretboard->staged();
    std::ranges::sort(positions, std::greater<>{}, &zub::fretboard::Position::string);
    std::vector<std::string> tokens;
    for (const auto& position : positions)
        tokens.push_back(zub::fretboard::noteToken(fretboard->midiAt(position.string, position.fret),
                                                   position.string, fretboard->german(),
                                                   fretboard->flats()));
    std::string token = tokens.front();
    if (tokens.size() > 1) {
        token = "<";
        for (std::size_t i = 0; i < tokens.size(); ++i)
            token += (i ? " " : "") + tokens[i];
        token += ">";
    }
    if (replacing) {
        // Only the notes change; the length, ties and articulations of the event stay.
        const auto replacement = zub::fretboard::replacePitches(editor->editorText(), target->begin,
                                                                target->end, token);
        if (!replacement) {
            statusBar()->showMessage(tr("The selected note cannot be replaced."), 5000);
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

void MainWindow::requestScorePreview(bool userAsked) {
    scorePreviewTimer_->stop();
    SongEditor* editor = currentEditor();
    if (!editor || onSongTab() || songbookRoot_.empty()) {
        scoreRunner_->cancel();
        scorePreview_->clear();
        return;
    }
    const std::string name = scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)].name;
    std::string content = editor->content();

    // lilypond runs Scheme: a score is code, like a song.
    QString list;
    if (!approveRisks({{editor->path(), {}, zub::safety::lilypondRisks(content)}}, userAsked, &list,
                      {}, {}, true)) {
        scoreRunner_->cancel();
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
    if (const auto meta = zub::parseSong(editor_->content()))
        request.germanNames = zub::score::germanNames(meta->value("lang"));
    request.includeDir = songbookRoot_ / "zubuvzpevnik" / "lilypond";
    scoreRunner_->request(std::move(request));
}

void MainWindow::showScoreResult(const ScoreResult& result) {
    // Only the score that is shown: the user may have moved on while it was built.
    if (onSongTab() || !currentEditor())
        return;
    if (scoreTabs_[static_cast<std::size_t>(tabs_->currentIndex() - 1)].name != result.inputName)
        return;
    scorePreview_->showResult(result);
    scoreLinksFresh_ = result.success && !scorePreviewTimer_->isActive();
    indexScoreEvents();
    updateScoreHighlights();
}

// The music events of the score text the preview was built from, and which links of the preview
// belong to which event. Only links into music events are selectable: chord names, lyrics, marks
// and the definition of a variable (every use of it links there) are not.
void MainWindow::indexScoreEvents() {
    scoreEvents_.clear();
    linkEvent_.clear();
    eventLink_.clear();
    scoreAnchor_.reset();
    SongEditor* editor = currentEditor();
    QList<QPoint> selectable;
    if (scoreLinksFresh_ && editor && !onSongTab()) {
        const std::string text = editor->editorText();
        scoreEvents_ = zub::scoreregion::musicEvents(text);
        for (const QPoint& position : scorePreview_->notePositions()) {
            const auto offset = zub::scoreregion::offsetOf(text, position.y(), position.x());
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
}

void MainWindow::updateRegionActions() {
    SongEditor* editor = currentEditor();
    const bool enabled = editor && !onSongTab() && !editor->textEdit()->isReadOnly() &&
                         editor->selectedByteRange().has_value();
    for (QAction* action : regionActions_)
        action->setEnabled(enabled);
}

// The notes of the preview whose events the editor's selection touches get a highlight box; with
// no selection the score cursor stands where the text cursor is: behind the event that ends
// there, else before the event the cursor is in or before, else (past the last event) behind it.
void MainWindow::updateScoreHighlights() {
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
            touched[i] = scoreEvents_[i].begin < range->second && range->first < scoreEvents_[i].end;
        for (const auto& [key, event] : linkEvent_) {
            if (touched[event])
                inside.append(QPoint(key.second, key.first));
        }
    } else if (!scoreEvents_.empty()) {
        const std::size_t offset = editor->cursorByteOffset();
        std::optional<std::size_t> event;
        const auto ending = std::ranges::find_if(
            scoreEvents_, [offset](const auto& e) { return e.end == offset; });
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
std::optional<std::size_t> MainWindow::caretOffsetOf(const QPoint& position, bool after) const {
    const auto found = linkEvent_.find({position.y(), position.x()});
    if (found == linkEvent_.end())
        return std::nullopt;
    const auto& event = scoreEvents_[found->second];
    return after ? event.end : event.begin;
}

// A click on a note of the rendered score: the text cursor goes before it (or behind it when the
// click was right of the note; the score cursor follows), with Shift the selection extends from
// where it started.
void MainWindow::clickScoreNote(const QPoint& position, bool after, bool extend) {
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
    editor->setCursorByteOffset(*offset);
    updateScoreHighlights();
}

// A drag over the preview: the notes between where it started and the position of the mouse.
void MainWindow::selectScoreNotes(const QPoint& anchor, bool anchorAfter, const QPoint& current,
                                  bool currentAfter) {
    if (!scoreLinksFresh_)
        return;
    const auto a = caretOffsetOf(anchor, anchorAfter);
    const auto b = caretOffsetOf(current, currentAfter);
    if (a && b)
        selectScoreOffsets(*a, *b);
}

// Like a word in a text: a double-click selects exactly that event.
void MainWindow::doubleClickScoreNote(const QPoint& position) {
    SongEditor* editor = currentEditor();
    const auto found = linkEvent_.find({position.y(), position.x()});
    if (!editor || onSongTab() || !scoreLinksFresh_ || found == linkEvent_.end())
        return;
    const auto& event = scoreEvents_[found->second];
    scoreAnchor_ = event.begin;
    editor->selectByteRange(event.begin, event.end);
    updateScoreHighlights();
}

// The editor selects the events that lie between the two text positions (either order): whole
// notes and chords, in text order. The anchor stays where the selection started. Between two
// events, or at one position, only the text cursor goes there.
void MainWindow::selectScoreOffsets(std::size_t anchor, std::size_t current) {
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
    if (first)
        editor->selectByteRange(scoreEvents_[*first].begin, scoreEvents_[*last].end);
    else
        editor->setCursorByteOffset(current);
    updateScoreHighlights();
}

void MainWindow::newScore() {
    if (!onSongTab() || editor_->textEdit()->isReadOnly() || songbookRoot_.empty())
        return;
    const std::string inputName = inputNameOf(editor_);
    const auto slash = inputName.rfind('/');
    if (slash == std::string::npos) {
        statusBar()->showMessage(tr("The song is not in an artist folder."), 5000);
        return;
    }
    const std::string folder = inputName.substr(0, slash + 1); // "Artist/"
    const std::string stem = toStdString(toQString(editor_->path().stem()));
    const auto taken = [&](const std::string& file) {
        const std::string name = folder + file;
        return scoreTabIndex(name) >= 0 || !scorePath(name).empty();
    };
    // The file is <Song><Part>.ly (the songbook's naming): only the part name is asked for.
    QDialog dialog(this);
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

    auto* score =
        new SongEditor(newScorePath(name), *repository_, nullptr, SongEditor::Kind::Score);
    const auto skeleton = zub::score::newScore();
    score->openNew(skeleton.text);
    score->setCursorByteOffset(skeleton.cursor);

    // The reference goes on a new line at the cursor, as one undo step of the song.
    QPlainTextEdit* edit = editor_->textEdit();
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
    statusBar()->showMessage(
        tr("New score %1. Save it with Ctrl+S to create the file.").arg(toQString(name)), 8000);
}

QString MainWindow::actionName(SongAction action) const {
    switch (action) {
    case SongAction::Shrink:
        return tr("Make Smaller");
    case SongAction::Check:
        return tr("Check Song");
    case SongAction::Join:
        return tr("Join Lines");
    }
    return {};
}

void MainWindow::startProposals(SongAction action) {
    SongEditor* editor = editor_;
    if (!editor || editor->textEdit()->isReadOnly() || converterRunner_->isRunning() ||
        songbookRoot_.empty())
        return;
    const QString name = actionName(action);
    const std::filesystem::path converter = findConverter();
    if (converter.empty()) {
        statusBar()->showMessage(tr("%1 needs chords_converter, which was not found.").arg(name),
                                 8000);
        return;
    }
    if (!converterUsable(converter, name))
        return;
    const std::string content = editor->content();
    const std::filesystem::path root = songbookRoot_;
    std::function<zub::converter::Command(const std::filesystem::path&)> command;
    QString status;
    switch (action) {
    case SongAction::Shrink: {
        // --measure builds the song with lualatex -shell-escape: the same rule as for a preview.
        // Declined, the proposals are still useful, just without page counts.
        const bool measure = approveBuild(editor, content, true, nullptr, tr("Measure"),
                                          tr("Suggest Without Measuring"));
        if (measure)
            requestPreview(); // approved now: the pane's "Not built automatically" message goes
        // No paper known (a class without a paper option): no --fit, the converter measures both.
        const auto paper =
            zub::preview::paperOf(songbookRoot_, toStdString(preview_->documentClass()));
        command = [converter, paper, measure, root](const std::filesystem::path& file) {
            return zub::songsuggest::shrinkCommand(converter, file, paper, measure, root);
        };
        status = tr("Looking for ways to make the song smaller…");
        break;
    }
    case SongAction::Check:
        // check builds nothing: no approval needed
        command = [converter](const std::filesystem::path& file) {
            return zub::songsuggest::checkCommand(converter, file);
        };
        status = tr("Checking the song…");
        break;
    case SongAction::Join:
        // join always typesets the song: without the approval it can't run.
        if (!approveBuild(editor, content, true, nullptr, tr("Build"), tr("Cancel")))
            return;
        requestPreview();
        command = [converter, root](const std::filesystem::path& file) {
            return zub::songsuggest::joinCommand(converter, file, root);
        };
        status = tr("Checking which lines can be joined (typesetting the song, a few seconds)…");
        break;
    }

    runKind_ = RunKind::Suggest;
    songAction_ = action;
    runEditor_ = editor;
    runInput_ = content;
    statusBar()->showMessage(status);
    if (!converterRunner_->start(command, content, toQString(editor->path().stem())))
        return;
    updateSongActions();
}

void MainWindow::onSuggestFinished(const std::expected<ConverterOutput, QString>& output) {
    const SongAction action = songAction_;
    const QString name = actionName(action);
    std::string input;
    QPointer<SongEditor> editor = takeRun(input, name);
    if (!editor) {
        updateSongActions();
        return;
    }
    std::expected<zub::songsuggest::Suggestions, QString> result =
        std::unexpected(output.error_or({}));
    if (output) {
        auto interpreted =
            zub::songsuggest::interpretProposals(output->exitCode, output->out, output->err);
        if (interpreted)
            result = std::move(*interpreted);
        else
            result = std::unexpected(toQString(interpreted.error()));
    }
    updateSongActions();
    statusBar()->clearMessage();
    if (!result) {
        QMessageBox::warning(this, tr("%1 failed").arg(name), result.error());
        return;
    }
    const zub::songsuggest::Suggestions& suggestions = *result;
    if (suggestions.proposals.empty()) {
        const QString summary = ShrinkDialog::summary(suggestions);
        QString text;
        switch (action) {
        case SongAction::Shrink:
            text = tr("No more changes to suggest.");
            if (suggestions.fits == true) {
                const bool a5 = suggestions.a5.has_value();
                text = tr("The song fits on %1 already.").arg(a5 ? tr("A5") : tr("A4")) +
                       QStringLiteral(" ") + text;
            }
            break;
        case SongAction::Check:
            text = tr("No mistakes found.");
            break;
        case SongAction::Join:
            text = tr("No lines can be joined: every part of a kind needs an even number of "
                      "lines, and the joined lines must still fit the column on A4 and A5.");
            break;
        }
        if (!summary.isEmpty() && action != SongAction::Check)
            text += QStringLiteral("\n\n") + summary;
        QMessageBox::information(this, name, text);
        return;
    }
    QString heading;
    switch (action) {
    case SongAction::Shrink:
        heading = tr("Pick a change, most readable first (score 0–100: how readable the song "
                     "stays):");
        break;
    case SongAction::Check:
        heading = tr("Possible mistakes, in song order. A part with a chorus's words: only you can "
                     "tell whether it is the chorus sung again (the melody may differ); made a "
                     "chorus, Tidy runs after it to shorten the copies.");
        break;
    case SongAction::Join:
        heading = tr("Pick the parts whose lines to join in pairs (checked: the joined lines fit "
                     "the column on A4 and A5):");
        break;
    }
    ShrinkDialog dialog(suggestions, input, name, heading, this, action == SongAction::Check);
    // Show where each proposal is (moving the cursor does not change the text).
    connect(&dialog, &ShrinkDialog::proposalSelected, this, &MainWindow::jumpToLine);
    if (const int line = suggestions.proposals.front().line; line > 0)
        jumpToLine(line);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // The same text the suggestions were computed for (the dialog is modal, but the editor may
    // have been closed or changed by other means).
    if (!editor || editor != editor_ || editor->content() != input) {
        statusBar()->showMessage(tr("%1 discarded: the song changed.").arg(name), 8000);
        return;
    }
    const auto chosen = dialog.selected();
    if (chosen.empty())
        return;
    if (dialog.dismissed()) {
        // Mark as Correct: the "% nocheck" lines go in locally, as one undo step.
        std::vector<zub::songsuggest::Edit> marks;
        for (const auto& proposal : chosen)
            if (proposal.dismiss)
                marks.push_back(*proposal.dismiss);
        const auto marked = zub::songoutline::applyEdits(input, marks);
        if (!marked) {
            QMessageBox::warning(this, tr("%1 failed").arg(name), toQString(marked.error()));
            return;
        }
        editor->replaceContent(*marked); // one undo step; nothing is saved
        statusBar()->showMessage(
            chosen.size() == 1
                ? tr("Marked as correct: %1").arg(toQString(chosen.front().description))
                : tr("Marked as correct: %n finding(s)", nullptr, static_cast<int>(chosen.size())),
            10000);
        updateSongActions();
        requestPreview();
        return;
    }
    std::vector<std::string> ids;
    for (const auto& proposal : chosen)
        ids.push_back(proposal.id);
    const QString description = chosen.size() == 1
                                    ? toQString(chosen.front().description)
                                    : tr("%n changes", nullptr, static_cast<int>(chosen.size()));
    runEditor_ = editor.data();
    runInput_ = input;
    songAction_ = action;
    applyRestructures_ = std::ranges::any_of(
        chosen, [](const auto& proposal) { return proposal.category == "restructure"; });
    startApply(std::move(ids), description);
}

void MainWindow::startApply(std::vector<std::string> ids, const QString& description) {
    const std::filesystem::path converter = findConverter();
    if (converter.empty() || !runEditor_) {
        runEditor_.clear();
        runInput_.clear();
        return;
    }
    runKind_ = RunKind::Apply;
    applyDescription_ = description;
    statusBar()->showMessage(tr("Applying…"));
    if (!converterRunner_->start(
            [converter, ids = std::move(ids)](const std::filesystem::path& file) {
                return zub::songsuggest::applyCommand(converter, file, ids);
            },
            runInput_, toQString(runEditor_->path().stem())))
        return;
    updateSongActions();
}

void MainWindow::onApplyFinished(const std::expected<ConverterOutput, QString>& output) {
    std::string input;
    const QString description = std::move(applyDescription_);
    applyDescription_.clear();
    const SongAction action = songAction_;
    const bool restructured = std::exchange(applyRestructures_, false);
    SongEditor* editor = takeRun(input, actionName(action));
    if (!editor) {
        updateSongActions();
        return;
    }
    std::expected<std::string, QString> result = std::unexpected(output.error_or({}));
    if (output) {
        auto interpreted =
            zub::songsuggest::interpretApply(output->exitCode, output->out, output->err);
        if (interpreted)
            result = std::move(*interpreted);
        else
            result = std::unexpected(toQString(interpreted.error()));
    }
    updateSongActions();
    if (!result) {
        statusBar()->clearMessage();
        QMessageBox::warning(this, tr("%1 failed").arg(actionName(action)), result.error());
        return;
    }
    editor->replaceContent(*result); // one undo step; nothing is saved
    statusBar()->showMessage(tr("Applied: %1").arg(description), 10000);
    updateSongActions();
    requestPreview();
    // The saving comes after a restructuring: tidy makes the chorus's copies \insertchorus
    // (its own undo step).
    if (restructured && editor == editor_)
        startTidy(tr(" (after making a part a chorus)"));
}

namespace {

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
        QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("chords_converter"));
    return usableConverter(path) ? toPath(path) : std::filesystem::path{};
}

std::filesystem::path MainWindow::findConverter() const {
    // The built-in converter is the version this app is tested with. Another one is used only
    // when the user chose it in the import dialog; PATH and ~/.cargo/bin are fallbacks for an
    // app built without it (ZUB_BUILD_CONVERTER=OFF).
    const auto builtIn = builtInConverter();
    QStringList candidates{
        QSettings{}.value(converterKey).toString(),
        builtIn.empty() ? QString{} : toQString(builtIn),
        QStandardPaths::findExecutable(QStringLiteral("chords_converter")),
        // cargo install puts it here, which is not always on PATH for GUI apps
        QDir::home().filePath(QStringLiteral(".cargo/bin/chords_converter")),
    };
    for (const QString& candidate : candidates) {
        if (usableConverter(candidate))
            return toPath(candidate);
    }
    return {};
}

void MainWindow::importSong() {
    if (songbookRoot_.empty()) {
        QMessageBox::information(this, tr("Import Song"), tr("Open a songbook folder first."));
        return;
    }
    const auto builtIn = builtInConverter();
    ImportDialog dialog(findConverter(), builtIn, this);
    const int result = dialog.exec();
    // Remember only a converter the user picked, and never the built-in one: a remembered
    // automatic find would shadow the built-in converter after an update.
    if (dialog.converterReset() || (dialog.converterChosen() && dialog.converter() == builtIn))
        QSettings{}.remove(converterKey);
    else if (dialog.converterChosen())
        QSettings{}.setValue(converterKey, toQString(dialog.converter()));
    if (result != QDialog::Accepted)
        return;
    const zub::songimport::Imported& imported = dialog.imported();
    const bool tidy = dialog.tidy();

    const auto dest = zub::songimport::destination(songbookRoot_, imported.inputName);
    std::filesystem::path target = dest.path;
    if (const auto existing = zub::songimport::existingSong(songbookRoot_, imported.inputName)) {
        QMessageBox box(QMessageBox::Question, tr("Song already exists"),
                        tr("The songbook already has %1.")
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
            // Already open: keep it as it is (edits, undo history, approvals).
            if (editor_ && editor_->path() == *existing) {
                editor_->textEdit()->setFocus();
                return;
            }
            // Load first, so a song that fails to load doesn't cost the open one.
            SongEditor* editor = openEditor(*existing, {});
            if (!editor)
                return;
            if (!closeSong()) {
                delete editor;
                return;
            }
            showSong(editor, {}, toQString(inputNameOf(editor)));
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
        startTidy(message);
}

void MainWindow::scanSongbook() {
    if (scan_ || songbookRoot_.empty())
        return;
    const QString name = tr("Scan Songbook");
    const std::filesystem::path converter = findConverter();
    if (converter.empty()) {
        QMessageBox::warning(this, name,
                             tr("The scan needs chords_converter, which was not found."));
        return;
    }
    if (!converterUsable(converter, name))
        return;
    const auto paper = zub::preview::paperOf(songbookRoot_, toStdString(preview_->documentClass()));
    const QString paperName = !paper                                  ? tr("its paper")
                              : *paper == zub::songsuggest::Paper::A5 ? QStringLiteral("A5")
                                                                      : QStringLiteral("A4");

    // The settings: what to look for, and in which songs.
    QDialog dialog(this);
    dialog.setWindowTitle(name);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        tr("Look for songs where a song action would change something. Nothing is changed: the "
           "songs are marked in the list (this session only), and you decide song by song."),
        &dialog));
    auto* tidy =
        new QCheckBox(tr("Tidy: a chorus typed again, chords inside a syllable, spaces"), &dialog);
    auto* check = new QCheckBox(
        tr("Check Song: typos, apostrophes typed two ways, a chorus typed as a verse"), &dialog);
    auto* shrink = new QCheckBox(
        tr("Make Smaller: songs that don't fit %1 (the preview's paper). Typesets every song: "
           "about 2 seconds each, some 8 minutes for the whole songbook.")
            .arg(paperName),
        &dialog);
    QSettings settings;
    tidy->setChecked(settings.value(QStringLiteral("scan/tidy"), true).toBool());
    check->setChecked(settings.value(QStringLiteral("scan/check"), true).toBool());
    shrink->setChecked(settings.value(QStringLiteral("scan/shrink"), false).toBool());
    for (QCheckBox* box : {tidy, check, shrink})
        layout->addWidget(box);
    const int shown = proxy_->rowCount();
    const int all = model_->rowCount();
    auto* allSongs = new QRadioButton(tr("All %n song(s)", nullptr, all), &dialog);
    auto* shownSongs =
        new QRadioButton(tr("The %n song(s) shown in the list", nullptr, shown), &dialog);
    allSongs->setChecked(true);
    shownSongs->setEnabled(shown != all);
    layout->addWidget(allSongs);
    layout->addWidget(shownSongs);
    layout->addWidget(
        new QLabel(tr("Unsaved edits are not scanned: the scan reads the files."), &dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* start = buttons->addButton(tr("Scan"), QDialogButtonBox::AcceptRole);
    start->setDefault(true);
    const auto updateStart = [=] {
        start->setEnabled(tidy->isChecked() || check->isChecked() || shrink->isChecked());
    };
    for (QCheckBox* box : {tidy, check, shrink})
        connect(box, &QCheckBox::toggled, &dialog, updateStart);
    updateStart();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    settings.setValue(QStringLiteral("scan/tidy"), tidy->isChecked());
    settings.setValue(QStringLiteral("scan/check"), check->isChecked());
    settings.setValue(QStringLiteral("scan/shrink"), shrink->isChecked());

    std::vector<SongbookScan::Song> songs;
    if (shownSongs->isChecked()) {
        for (int row = 0; row < proxy_->rowCount(); ++row) {
            if (const auto* song = model_->song(proxy_->mapToSource(proxy_->index(row, 0)).row()))
                songs.push_back({song->path, song->inputName});
        }
    } else {
        for (int row = 0; row < model_->rowCount(); ++row) {
            if (const auto* song = model_->song(row))
                songs.push_back({song->path, song->inputName});
        }
    }
    ScanOptions options;
    options.tidy = tidy->isChecked();
    options.check = check->isChecked();
    options.shrink = shrink->isChecked();
    options.paper = paper;
    options.converter = converter;
    options.songbookRoot = songbookRoot_;

    model_->clearScanResults();
    scanFound_ = 0;
    scan_ = new SongbookScan(options, std::move(songs), this);
    auto* progress =
        new QProgressDialog(tr("Scanning…"), tr("Cancel"), 0, scan_->stepCount(), this);
    progress->setWindowTitle(name);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setMinimumWidth(480);
    connect(progress, &QProgressDialog::canceled, scan_, &SongbookScan::cancel);
    connect(scan_, &SongbookScan::progress, progress,
            [progress](int done, int, const QString& what) {
                progress->setValue(done);
                progress->setLabelText(what);
            });
    connect(scan_, &SongbookScan::songScanned, this,
            [this](const QString& path, const ScanFinding& finding) {
                QStringList summary;
                QStringList details;
                const auto section = [&details](const QString& title, const QStringList& lines) {
                    if (lines.isEmpty())
                        return;
                    details << title;
                    for (const QString& line : lines)
                        details << QStringLiteral("  • ") + line;
                };
                if (!finding.tidy.isEmpty())
                    summary << tr("Tidy %1").arg(finding.tidy.size());
                if (!finding.check.isEmpty())
                    summary << tr("Check %1").arg(finding.check.size());
                if (!finding.shrink.isEmpty())
                    summary << (finding.pages.isEmpty() ? tr("Too big")
                                                        : tr("Too big (%1)").arg(finding.pages));
                if (!finding.errors.isEmpty())
                    summary << tr("Errors %1").arg(finding.errors.size());
                section(tr("Tidy:"), finding.tidy);
                section(tr("Check Song:"), finding.check);
                section(finding.pages.isEmpty() ? tr("Make Smaller:")
                                                : tr("Make Smaller (%1):").arg(finding.pages),
                        finding.shrink);
                section(tr("Problems:"), finding.errors);
                model_->setScanResult(toPath(path), summary.join(QStringLiteral(" · ")),
                                      details.join(QLatin1Char('\n')));
                ++scanFound_;
            });
    connect(scan_, &SongbookScan::finished, this, [this, progress](bool cancelled) {
        progress->reset();
        progress->deleteLater();
        scan_->deleteLater();
        scan_ = nullptr;
        table_->setColumnHidden(SongTableModel::Changes, false);
        changesOnlyAction_->setVisible(true);
        changesOnly_->setChecked(true);
        statusBar()->showMessage(
            (cancelled ? tr("Scan cancelled. ") : QString()) +
                tr("%n song(s) with possible changes: open one and use Tidy, Check Song or Make "
                   "Smaller to decide.",
                   nullptr, scanFound_),
            15000);
    });
    scan_->start();
}
