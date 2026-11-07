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
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QToolBar>
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
const auto splitterKey = QStringLiteral("window/splitter");

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), repository_(std::make_unique<KSyntaxHighlighting::Repository>()) {
    setWindowTitle(QApplication::applicationDisplayName() + QLatin1Char(' ') +
                   QApplication::applicationVersion());
    resize(1200, 800);

    createCentralWidget();
    createMenus();

    // Labels rather than showMessage(): a temporary message is cleared by menu hover.
    countLabel_ = new QLabel(this);
    statusBar()->addWidget(countLabel_);
    issuesLabel_ = new QLabel(this);
    statusBar()->addPermanentWidget(issuesLabel_);
    updateStatus();

    // Window size and pane widths as the user left them.
    const QSettings settings;
    restoreGeometry(settings.value(geometryKey).toByteArray());
    splitter_->restoreState(settings.value(splitterKey).toByteArray());
}

MainWindow::~MainWindow() {
    // Members are destroyed before QWidget deletes the children, so without this the editors
    // (and their highlighters) would outlive repository_. Disconnect first: removing the tabs
    // emits currentChanged, which must not reach a half-destroyed window.
    disconnect(tabs_, nullptr, this, nullptr);
    delete tabs_;
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
    QAction* save = file->addAction(tr("&Save Song"));
    save->setShortcut(QKeySequence::Save);
    connect(save, &QAction::triggered, this, &MainWindow::saveCurrent);

    QAction* closeSong = file->addAction(tr("&Close Song"));
    closeSong->setShortcut(QKeySequence::Close);

    QAction* import = file->addAction(tr("&Import Song…"));
    import->setShortcut(QKeySequence(QStringLiteral("Ctrl+I")));
    connect(import, &QAction::triggered, this, &MainWindow::importSong);
    connect(closeSong, &QAction::triggered, this, [this] {
        if (tabs_->currentIndex() >= 0)
            closeTab(tabs_->currentIndex());
    });

    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);

    QAction* find = new QAction(this);
    find->setShortcut(QKeySequence::Find);
    connect(find, &QAction::triggered, this, [this] {
        search_->setFocus();
        search_->selectAll();
    });
    addAction(find);
}

void MainWindow::createCentralWidget() {
    model_ = new SongTableModel(this);
    proxy_ = new SongFilterProxy(this);
    proxy_->setSourceModel(model_);

    QToolBar* filters = addToolBar(tr("Filter"));
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

    table_ = new QTableView(this);
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

    tabs_ = new QTabWidget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->setDocumentMode(true);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);

    createPreview();

    auto* splitter = splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(table_);
    splitter->addWidget(tabs_);
    splitter->addWidget(preview_);
    // Growing the window shares the extra width by these factors, so they match the sizes.
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 4);
    splitter->setStretchFactor(2, 4);
    // Stretch factors alone lose to the table's wide size hint: set the first split explicitly.
    splitter->setSizes({3, 4, 4});
    table_->horizontalHeader()->setMinimumSectionSize(60);
    setCentralWidget(splitter);

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
    if (options.closeEditors && !closeAllTabs())
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
    setWindowTitle(QApplication::applicationDisplayName() + QStringLiteral(" — ") +
                   toQString(root));
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

    if (SongEditor* open = findEditor(song->path)) {
        tabs_->setCurrentWidget(open);
        open->textEdit()->setFocus();
        return;
    }

    auto* editor = new SongEditor(song->path, *repository_, tabs_);
    if (auto loaded = editor->load(); !loaded) {
        delete editor;
        QMessageBox::warning(this, tr("Cannot open song"), loaded.error());
        return;
    }
    addEditorTab(editor, toQString(zub::latex::toDisplayText(song->meta.title)),
                 toQString(song->inputName));
}

SongEditor* MainWindow::findEditor(const std::filesystem::path& path) const {
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* editor = qobject_cast<SongEditor*>(tabs_->widget(i));
        if (editor && editor->path() == path)
            return editor;
    }
    return nullptr;
}

void MainWindow::addEditorTab(SongEditor* editor, const QString& title, const QString& toolTip) {
    // The tab label is the editor's window title plus a "*" while modified.
    editor->setWindowTitle(title);
    const int index = tabs_->addTab(editor, QString{});
    tabs_->setTabToolTip(index, toolTip);
    updateTabTitle(editor);
    tabs_->setCurrentIndex(index);
    editor->textEdit()->setFocus();

    connect(editor, &SongEditor::modificationChanged, this,
            [this, editor] { updateTabTitle(editor); });
    connect(editor, &SongEditor::saved, this, [this, editor](const std::string& content) {
        refreshSongMetadata(editor, content);
        requestPreviewIfCurrent(editor);
    });
    connect(editor, &SongEditor::reloadedFromDisk, this, [this, editor] {
        statusBar()->showMessage(
            tr("%1 changed on disk and was reloaded.").arg(toQString(editor->path().filename())),
            5000);
        if (auto content = zub::readTextFile(editor->path()))
            refreshSongMetadata(editor, *content);
        requestPreviewIfCurrent(editor);
    });
}

SongEditor* MainWindow::currentEditor() const {
    return qobject_cast<SongEditor*>(tabs_->currentWidget());
}

void MainWindow::saveCurrent() {
    SongEditor* editor = currentEditor();
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
    tabs_->setCurrentWidget(editor);
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

bool MainWindow::closeTab(int index) {
    auto* editor = qobject_cast<SongEditor*>(tabs_->widget(index));
    if (!editor)
        return true;
    if (!confirmClose(editor))
        return false;
    // No more reload dialogs from a tab that is gone but not yet deleted.
    editor->stopWatching();
    approvedConstructs_.erase(editor->path()); // approvals last as long as the tab
    tabs_->removeTab(tabs_->indexOf(editor));  // the index may have moved during the dialog
    editor->deleteLater();
    return true;
}

bool MainWindow::closeAllTabs() {
    {
        // Each closed tab would otherwise start (and kill) a build of the next one.
        const QSignalBlocker blocker(tabs_);
        while (tabs_->count() > 0) {
            if (!closeTab(tabs_->count() - 1))
                break;
        }
    }
    requestPreview();
    return tabs_->count() == 0;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    const QSignalBlocker blocker(tabs_); // confirmClose() switches tabs: no builds for those
    for (int i = 0; i < tabs_->count(); ++i) {
        auto* editor = qobject_cast<SongEditor*>(tabs_->widget(i));
        if (editor && !confirmClose(editor)) {
            event->ignore();
            return;
        }
    }
    QSettings settings;
    settings.setValue(geometryKey, saveGeometry());
    settings.setValue(splitterKey, splitter_->saveState());
    event->accept();
}

void MainWindow::updateTabTitle(SongEditor* editor) {
    const int index = tabs_->indexOf(editor);
    if (index >= 0)
        tabs_->setTabText(index, editor->windowTitle() + (editor->isModified() ? " *" : ""));
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
    updateTabTitle(editor);
    tabs_->setTabToolTip(tabs_->indexOf(editor), toQString(inputNameOf(editor)));
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
                SongEditor* editor = currentEditor();
                if (editor && toQString(inputNameOf(editor)) == inputName)
                    editor->goToLine(line);
            });
    // Also fires when the first tab is added and when the last one is closed.
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { requestPreview(); });

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
    SongEditor* editor = currentEditor();
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

void MainWindow::requestPreviewIfCurrent(SongEditor* editor) {
    if (editor == currentEditor())
        requestPreview();
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
            if (SongEditor* open = findEditor(*existing)) {
                tabs_->setCurrentWidget(open);
            } else {
                auto* editor = new SongEditor(*existing, *repository_, tabs_);
                if (auto loaded = editor->load(); !loaded) {
                    delete editor;
                    QMessageBox::warning(this, tr("Cannot open song"), loaded.error());
                    return;
                }
                // A song whose header does not parse still opens, titled by its file name.
                const auto meta = zub::parseSong(editor->content());
                addEditorTab(editor,
                             meta ? toQString(zub::latex::toDisplayText(meta->title))
                                  : toQString(existing->stem()),
                             toQString(existing->lexically_relative(songbookRoot_)));
            }
            return;
        }
        if (box.clickedButton() != importAnyway)
            return;
        target = *existing;
    }
    if (SongEditor* open = findEditor(target)) {
        tabs_->setCurrentWidget(open);
        QMessageBox::information(this, tr("Import Song"),
                                 tr("This song is already open. Close it first to import it "
                                    "again."));
        return;
    }

    // Downloaded content is untrusted: it must not inherit "build anyway" approvals given
    // earlier in the session to whatever was at this path.
    approvedConstructs_.erase(target);
    auto* editor = new SongEditor(target, *repository_, tabs_);
    editor->openNew(imported.content);
    const auto meta = zub::parseSong(imported.content);
    const QString title =
        meta ? toQString(zub::latex::toDisplayText(meta->title)) : toQString(target.stem());
    addEditorTab(editor, title, toQString(dest.inputName) + tr(" (imported, not saved)"));
    QStringList notes;
    for (const std::string& warning : imported.warnings)
        notes << toQString(warning);
    statusBar()->showMessage(
        notes.isEmpty()
            ? tr("Imported %1. Check it, then save with Ctrl+S.").arg(toQString(imported.inputName))
            : notes.join(QStringLiteral(" ")),
        10000);
}
