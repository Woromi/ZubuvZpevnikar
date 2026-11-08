#include "MainWindow.hpp"

#include "ConverterRunner.hpp"
#include "ConverterVersion.hpp"
#include "DiffView.hpp"
#include "ImportDialog.hpp"
#include "PreviewPane.hpp"
#include "PreviewRunner.hpp"
#include "ShrinkDialog.hpp"
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
#include <QListWidget>
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
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <set>
#include <system_error>
#include <zub/core/latex.hpp>
#include <zub/core/library.hpp>
#include <zub/core/linediff.hpp>
#include <zub/core/preview.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/song.hpp>
#include <zub/core/songimport.hpp>
#include <zub/core/songsuggest.hpp>
#include <zub/core/songtidy.hpp>
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
    // The editor goes in at index 0 of editorArea_ when a song opens (createSongToolBar adds
    // the toolbar).
    songSplitter_ = new QSplitter(Qt::Horizontal, songPage_);
    auto* editorHost = new QWidget(songSplitter_);
    auto* hostLayout = new QVBoxLayout(editorHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);
    editorArea_ = new QStackedWidget(editorHost);
    diffView_ = new DiffView(editorArea_);
    editorArea_->addWidget(diffView_);
    hostLayout->addWidget(editorArea_, 1);

    tidyPanel_ = new QWidget(editorHost);
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
    hostLayout->addWidget(tidyPanel_);
    connect(tidyClose, &QToolButton::clicked, this, &MainWindow::clearTidyChanges);
    connect(tidyList_, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem* item) { jumpToLine(item->data(Qt::UserRole).toInt()); });

    converterRunner_ = new ConverterRunner(this);
    connect(converterRunner_, &ConverterRunner::finished, this, &MainWindow::onConverterFinished);

    songSplitter_->addWidget(editorHost);
    songSplitter_->addWidget(preview_);
    // A collapsed editor would be saved in the state and every later start would open it hidden.
    songSplitter_->setCollapsible(0, false);
    songSplitter_->setStretchFactor(0, 1);
    songSplitter_->setStretchFactor(1, 1);
    songSplitter_->setSizes({1, 1});
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

    editorArea_->insertWidget(0, editor);
    editorArea_->setCurrentWidget(editor);
    songPageShown_ = true;

    connect(editor, &SongEditor::modificationChanged, this, [this] {
        updateWindowTitle();
        updateSongActions();
    });
    connect(editor, &SongEditor::saved, this, [this, editor](const std::string& content) {
        changesAction_->setChecked(false);
        refreshSongMetadata(editor, content);
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
        requestPreview();
    });

    // Toolbar state follows the editor; dies with it (context: this, disconnected on close).
    QPlainTextEdit* edit = editor->textEdit();
    connect(edit, &QPlainTextEdit::undoAvailable, this,
            [this](bool available) { undoAction_->setEnabled(available); });
    connect(edit, &QPlainTextEdit::textChanged, this, [this] { refreshChanges(); });
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
    if (converterRunner_->isRunning())
        statusBar()->clearMessage(); // "Tidying…"; cancel() reports nothing
    converterRunner_->cancel();
    runEditor_.clear();
    clearTidyChanges();
    changesAction_->setChecked(false);
    // No more reload dialogs from an editor that is gone but not yet deleted.
    editor->stopWatching();
    approvedConstructs_.erase(path); // approvals last as long as the song is open
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
    if (editor_ && !confirmClose(editor_)) {
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

    restructureAction_ = new QAction(QIcon::fromTheme(QStringLiteral("view-list-tree")),
                                     tr("Restructure"), this);
    restructureAction_->setToolTip(
        tr("Find parts typed as a verse that have the chorus's words, and make one a chorus"));
    connect(restructureAction_, &QAction::triggered, this,
            [this] { startProposals(SongAction::Restructure); });
    songToolBar_->addAction(restructureAction_);

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

void MainWindow::updateSongActions() {
    const bool open = editor_ != nullptr;
    closeSongAction_->setEnabled(open);
    saveAction_->setEnabled(open && editor_->isModified());
    const bool modified = open && editor_->isModified();
    changesAction_->setEnabled(modified);
    if (!modified && changesAction_->isChecked())
        changesAction_->setChecked(false);
    if (!open) {
        for (QAction* action :
             {undoAction_, redoAction_, cutAction_, copyAction_, pasteAction_, tidyAction_,
              makeSmallerAction_, joinLinesAction_, restructureAction_})
            action->setEnabled(false);
        return;
    }
    const QPlainTextEdit* edit = editor_->textEdit();
    const bool readOnly = edit->isReadOnly();
    const bool selection = edit->textCursor().hasSelection();
    // While the Changes view is shown, the editor is hidden: no edits it can't show.
    const bool editable = !readOnly && !changesAction_->isChecked();
    undoAction_->setEnabled(editable && edit->document()->isUndoAvailable());
    redoAction_->setEnabled(editable && edit->document()->isRedoAvailable());
    copyAction_->setEnabled(selection);
    cutAction_->setEnabled(selection && editable);
    pasteAction_->setEnabled(editable);
    const bool converterFree =
        !readOnly && !converterRunner_->isRunning() && !findConverter().empty();
    tidyAction_->setEnabled(converterFree);
    for (QAction* action : {makeSmallerAction_, joinLinesAction_, restructureAction_})
        action->setEnabled(converterFree && !songbookRoot_.empty());
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
        statusBar()->showMessage(tr("%1 discarded: the song changed while it ran.").arg(what), 8000);
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
    else if (tidied.changes.size() == 1)
        message = tr("Tidy: 1 change");
    else
        message = tr("Tidy: %1 changes").arg(tidied.changes.size());
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
    request.songContent = std::move(content);
    request.documentClass = toStdString(preview_->documentClass());
    previewRunner_->request(std::move(request));
}

bool MainWindow::approveBuild(SongEditor* editor, const std::string& content, bool askUser,
                              QString* unapprovedList, const QString& yesText,
                              const QString& noText) {
    std::set<std::string>& approved = approvedConstructs_[editor->path()];
    QStringList unapproved;
    for (const std::string& construct : zub::safety::unknownConstructs(content)) {
        if (!approved.contains(construct))
            unapproved << toQString(construct);
    }
    if (unapproved.isEmpty())
        return true;
    const QString list = unapproved.join(QStringLiteral(", "));
    if (unapprovedList)
        *unapprovedList = list;
    bool build = false;
    if (askUser) {
        QMessageBox box(QMessageBox::Warning, tr("Build this song?"),
                        tr("This song uses commands that songs normally don't: %1").arg(list),
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
    for (const QString& construct : unapproved)
        approved.insert(toStdString(construct));
    return true;
}

QString MainWindow::actionName(SongAction action) const {
    switch (action) {
    case SongAction::Shrink:
        return tr("Make Smaller");
    case SongAction::Restructure:
        return tr("Restructure");
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
            return zub::songsuggest::suggestCommand(converter, file, paper, measure, root);
        };
        status = tr("Looking for ways to make the song smaller…");
        break;
    }
    case SongAction::Restructure:
        // Restructuring needs no measuring: nothing is built.
        command = [converter, root](const std::filesystem::path& file) {
            return zub::songsuggest::suggestCommand(converter, file, std::nullopt, false, root);
        };
        status = tr("Looking for parts to restructure…");
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
    std::expected<zub::songsuggest::Suggestions, QString> result = std::unexpected(output.error_or({}));
    if (output) {
        auto interpreted = zub::songsuggest::interpretSuggest(output->exitCode, output->out, output->err);
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
    zub::songsuggest::Suggestions& suggestions = *result;
    // Each button shows its own kind: restructuring is listed apart from fitting the page.
    const std::string wanted = action == SongAction::Restructure ? "restructure" : "fit";
    std::erase_if(suggestions.proposals,
                  [&wanted](const auto& proposal) { return proposal.category != wanted; });
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
        case SongAction::Restructure:
            text = tr("Nothing to restructure: no part outside a chorus has the words of a "
                      "chorus.");
            break;
        case SongAction::Join:
            text = tr("No lines can be joined: every part of a kind needs an even number of "
                      "lines, and the joined lines must still fit the column on A4 and A5.");
            break;
        }
        if (!summary.isEmpty() && action != SongAction::Restructure)
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
    case SongAction::Restructure:
        heading = tr("These parts have the words of a chorus. Only you can tell whether one is the "
                     "chorus sung again (the melody may differ). Pick one to make it a chorus, "
                     "chords kept; Tidy can then shorten the copies:");
        break;
    case SongAction::Join:
        heading = tr("Pick the parts whose lines to join in pairs (checked: the joined lines fit "
                     "the column on A4 and A5):");
        break;
    }
    ShrinkDialog dialog(suggestions, input, name, heading, this);
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
    std::vector<std::string> ids;
    for (const auto& proposal : chosen)
        ids.push_back(proposal.id);
    const QString description =
        chosen.size() == 1
            ? toQString(chosen.front().description)
            : tr("%n changes", nullptr, static_cast<int>(chosen.size()));
    runEditor_ = editor.data();
    runInput_ = input;
    songAction_ = action;
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
    SongEditor* editor = takeRun(input, actionName(action));
    if (!editor) {
        updateSongActions();
        return;
    }
    std::expected<std::string, QString> result = std::unexpected(output.error_or({}));
    if (output) {
        auto interpreted = zub::songsuggest::interpretApply(output->exitCode, output->out, output->err);
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
    // The saving comes after a restructuring: tidy makes the chorus's copies \insertchorus.
    if (action == SongAction::Restructure) {
        const QPointer<SongEditor> restructured = editor; // the song may close meanwhile
        const auto answer = QMessageBox::question(
            this, tr("Restructure"),
            tr("Done. Run Tidy now, so that copies of a chorus become \\insertchorus? (One more "
               "undo step.)"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes && restructured && restructured == editor_)
            startTidy();
    }
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
    editor->openNew(imported.content);
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
