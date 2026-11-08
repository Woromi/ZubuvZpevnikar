#include "MainWindow.hpp"

#include "ImportDialog.hpp"
#include "PreviewPane.hpp"
#include "PreviewRunner.hpp"
#include "SongEditor.hpp"
#include "SongFilterProxy.hpp"
#include "SongTableModel.hpp"
#include "convert.hpp"

#include <KSyntaxHighlighting/Repository>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLockFile>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <set>
#include <system_error>
#include <zub/core/latex.hpp>
#include <zub/core/library.hpp>
#include <zub/core/preview.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songimport.hpp>
#include <zub/core/textfile.hpp>

namespace {

const auto songbookPathKey = QStringLiteral("songbook/path");
const auto previewClassKey = QStringLiteral("preview/class");
const auto converterKey = QStringLiteral("converter/path");
const auto geometryKey = QStringLiteral("window/geometry");
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

    // Labels rather than showMessage(): a temporary message is cleared by menu hover.
    countLabel_ = new QLabel(this);
    statusBar()->addWidget(countLabel_);
    issuesLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(issuesLabel_);
    updateStatus();

    // Window size and pane widths as the user left them. The splitter holds no editor yet,
    // so its state is applied when a song opens.
    const QSettings settings;
    restoreGeometry(settings.value(geometryKey).toByteArray());
    songSplitterState_ = settings.value(songSplitterKey).toByteArray();
}

MainWindow::~MainWindow() {
    // Members are destroyed before QWidget deletes the children, so without this the editors
    // (and its highlighter) would outlive repository_. Disconnect first: no signal may reach
    // a half-destroyed window.
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
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

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
    connect(table_, &QAbstractItemView::activated, this, &MainWindow::openSong);

    listLayout->addWidget(filters);
    listLayout->addWidget(table_);
    table_->horizontalHeader()->setMinimumSectionSize(60);

    createPreview();

    songPage_ = new QWidget(pages_);
    auto* songLayout = new QVBoxLayout(songPage_);
    songLayout->setContentsMargins(0, 0, 0, 0);
    songLayout->setSpacing(0);
    // The editor goes in at index 0 when a song opens (createSongToolBar adds the toolbar).
    songSplitter_ = new QSplitter(Qt::Horizontal, songPage_);
    songSplitter_->addWidget(preview_);
    songLayout->addWidget(songSplitter_);

    pages_->addWidget(listPage_);
    pages_->addWidget(songPage_);
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

    QStringList candidates;
    const QString remembered = QSettings{}.value(songbookPathKey).toString();
    if (!remembered.isEmpty())
        candidates << remembered;
    // Development defaults: the sibling checkout next to this repo.
    candidates << QDir::current().filePath(QStringLiteral("../ZubuvZpevnik"))
               << QDir(QApplication::applicationDirPath())
                      .filePath(QStringLiteral("../../../../ZubuvZpevnik"));

    // Silent, and not remembered: a temporarily missing remembered folder must not be
    // replaced by a guess.
    for (const QString& candidate : candidates) {
        if (openSongbook(toPath(QDir::cleanPath(candidate)),
                         {.showErrors = false, .remember = false}))
            return;
    }
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
    // Plain English plurals; tr("%n song(s)") would need a translation file to resolve.
    const QString songs = total == 1 ? tr("1 song") : tr("%1 songs").arg(total);
    countLabel_->setText(shown == total ? songs : tr("%1 of %2").arg(shown).arg(songs));
    if (issueCount_ == 0)
        issuesLabel_->clear();
    else if (issueCount_ == 1)
        issuesLabel_->setText(tr("⚠ 1 file with issues (hover for details)"));
    else
        issuesLabel_->setText(tr("⚠ %1 files with issues (hover for details)").arg(issueCount_));
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

    songSplitter_->insertWidget(0, editor);
    // A collapsed editor would be saved in the state and every later song would open hidden.
    songSplitter_->setCollapsible(0, false);
    songSplitter_->setStretchFactor(0, 1);
    songSplitter_->setStretchFactor(1, 1);
    songSplitter_->setSizes({1, 1});
    if (!songSplitterState_.isEmpty())
        songSplitter_->restoreState(songSplitterState_);

    connect(editor, &SongEditor::modificationChanged, this, [this] {
        updateWindowTitle();
        updateSongActions();
    });
    connect(editor, &SongEditor::saved, this, [this, editor](const std::string& content) {
        refreshSongMetadata(editor, content);
        requestPreview();
    });
    connect(editor, &SongEditor::reloadedFromDisk, this, [this, editor] {
        statusBar()->showMessage(
            tr("%1 changed on disk and was reloaded.").arg(toQString(editor->path().filename())),
            5000);
        if (auto content = zub::readTextFile(editor->path()))
            refreshSongMetadata(editor, *content);
        updateSongActions();
        requestPreview();
    });

    // Toolbar state follows the editor; dies with it (context: this, disconnected on close).
    QPlainTextEdit* edit = editor->textEdit();
    connect(edit, &QPlainTextEdit::undoAvailable, this,
            [this](bool available) { undoAction_->setEnabled(available); });
    connect(edit, &QPlainTextEdit::redoAvailable, this,
            [this](bool available) { redoAction_->setEnabled(available); });
    connect(edit, &QPlainTextEdit::copyAvailable, this, [this, edit](bool available) {
        copyAction_->setEnabled(available);
        cutAction_->setEnabled(available && !edit->isReadOnly());
    });

    pages_->setCurrentWidget(songPage_);
    updateSongActions(); // after load: the editor may have made itself read-only
    updateWindowTitle();
    edit->setFocus();
    requestPreview();
}

void MainWindow::saveCurrent() {
    SongEditor* editor = editor_;
    if (!editor)
        return;
    if (auto saved = editor->save(); !saved) {
        QMessageBox::warning(this, tr("Cannot save song"), saved.error());
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
            QMessageBox::warning(this, tr("Cannot save song"), saved.error());
            return false;
        } else if (*saved == SongEditor::SaveResult::Cancelled) {
            return false;
        }
    }
    return true;
}

bool MainWindow::closeSong() {
    SongEditor* editor = editor_;
    if (!editor)
        return true;
    if (!confirmClose(editor))
        return false;
    const std::filesystem::path path = editor->path();
    // No more reload dialogs from an editor that is gone but not yet deleted.
    editor->stopWatching();
    approvedConstructs_.erase(path); // approvals last as long as the song is open
    disconnect(editor, nullptr, this, nullptr);
    disconnect(editor->textEdit(), nullptr, this, nullptr);
    songSplitterState_ = songSplitter_->saveState();
    editor_ = nullptr;
    editor->setParent(nullptr); // out of the splitter now; the next song takes index 0
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
    if (editor_ && !confirmClose(editor_)) {
        event->ignore();
        return;
    }
    if (editor_)
        songSplitterState_ = songSplitter_->saveState();
    QSettings settings;
    settings.setValue(geometryKey, saveGeometry());
    settings.setValue(songSplitterKey, songSplitterState_);
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
            if (editor_)
                slot(editor_->textEdit());
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

    // The song's input name ("Artist/Title"), at the right end.
    inputLabel_ = new QLabel(songToolBar_);
    inputLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    inputLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    songToolBar_->addWidget(inputLabel_);

    static_cast<QVBoxLayout*>(songPage_->layout())->insertWidget(0, songToolBar_);
}

void MainWindow::updateSongActions() {
    const bool open = editor_ != nullptr;
    closeSongAction_->setEnabled(open);
    saveAction_->setEnabled(open && editor_->isModified());
    if (!open) {
        for (QAction* action : {undoAction_, redoAction_, cutAction_, copyAction_, pasteAction_})
            action->setEnabled(false);
        return;
    }
    const QPlainTextEdit* edit = editor_->textEdit();
    const bool readOnly = edit->isReadOnly();
    const bool selection = edit->textCursor().hasSelection();
    undoAction_->setEnabled(edit->document()->isUndoAvailable());
    redoAction_->setEnabled(edit->document()->isRedoAvailable());
    copyAction_->setEnabled(selection);
    cutAction_->setEnabled(selection && !readOnly);
    pasteAction_->setEnabled(!readOnly);
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
    setWindowModified(editor_->isModified());
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
    if (!model_->updateSong(editor->path(), *meta)) {
        // A new song's first save: it joins the table now.
        model_->addSong({editor->path(), inputNameOf(editor), std::move(*meta)});
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
                    editor->goToLine(line);
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
}

std::string MainWindow::inputNameOf(const SongEditor* editor) const {
    // songs/chords/Artist/Title.tex -> "Artist/Title"
    const auto relative =
        editor->path().lexically_relative(songbookRoot_ / "songs" / "chords").replace_extension();
    const auto u8 = relative.generic_u8string();
    return {u8.begin(), u8.end()};
}

void MainWindow::requestPreview(bool userAsked) {
    SongEditor* editor = editor_;
    if (!editor || songbookRoot_.empty()) {
        previewRunner_->cancel(); // a build for a closed song must not fill the pane
        preview_->clear();
        return;
    }

    std::string content = editor->content();
    // Songs build with -shell-escape, so a song is code. Unknown commands (e.g. from an
    // imported song) are never built automatically, and only after confirmation on Rebuild.
    std::set<std::string>& approved = approvedConstructs_[editor->path()];
    QStringList unapproved;
    for (const std::string& construct : zub::safety::unknownConstructs(content)) {
        if (!approved.contains(construct))
            unapproved << toQString(construct);
    }
    if (!unapproved.isEmpty()) {
        const QString list = unapproved.join(QStringLiteral(", "));
        const bool build =
            userAsked &&
            QMessageBox::warning(
                this, tr("Build this song?"),
                tr("This song uses commands that songs normally don't: %1").arg(list),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes;
        if (!build) {
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
        for (const QString& construct : unapproved)
            approved.insert(toStdString(construct));
    }

    zub::preview::Request request;
    request.songbookRoot = songbookRoot_;
    request.workDir = previewWorkDir_;
    request.inputName = inputNameOf(editor);
    request.songContent = std::move(content);
    request.documentClass = toStdString(preview_->documentClass());
    previewRunner_->request(std::move(request));
}

std::filesystem::path MainWindow::findConverter() const {
    const auto usable = [](const QString& path) {
        const QFileInfo info(path);
        // Absolute only: PATH may contain "." or empty entries.
        return !path.isEmpty() && info.isAbsolute() && info.isFile() && info.isExecutable();
    };
    QStringList candidates{
        QSettings{}.value(converterKey).toString(),
        QStandardPaths::findExecutable(QStringLiteral("chords_converter")),
        // cargo install puts it here, which is not always on PATH for GUI apps
        QDir::home().filePath(QStringLiteral(".cargo/bin/chords_converter")),
    };
    if (!songbookRoot_.empty()) {
        // The sibling checkout next to the songbook: ../chords_converter
        candidates << toQString(songbookRoot_.parent_path() / "chords_converter" / "target" /
                                "release" / "chords_converter");
    }
    for (const QString& candidate : candidates) {
        if (usable(candidate))
            return toPath(candidate);
    }
    return {};
}

void MainWindow::importSong() {
    if (songbookRoot_.empty()) {
        QMessageBox::information(this, tr("Import Song"), tr("Open a songbook folder first."));
        return;
    }
    ImportDialog dialog(findConverter(), this);
    const int result = dialog.exec();
    // Remember only a converter the user picked: a remembered automatic find would shadow a
    // newer build found later.
    if (dialog.converterChosen())
        QSettings{}.setValue(converterKey, toQString(dialog.converter()));
    if (result != QDialog::Accepted)
        return;
    const zub::songimport::Imported& imported = dialog.imported();

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
    editor->openNew(imported.content);
    const auto meta = zub::parseSong(imported.content);
    const QString title =
        meta ? toQString(zub::latex::toDisplayText(meta->title)) : toQString(target.stem());
    showSong(editor, title, toQString(dest.inputName) + tr(" (imported, not saved)"));
    QStringList notes;
    for (const std::string& warning : imported.warnings)
        notes << toQString(warning);
    statusBar()->showMessage(
        notes.isEmpty()
            ? tr("Imported %1. Check it, then save with Ctrl+S.").arg(toQString(imported.inputName))
            : notes.join(QStringLiteral(" ")),
        10000);
}
