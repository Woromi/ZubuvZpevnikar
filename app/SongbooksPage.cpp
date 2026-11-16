#include "SongbooksPage.hpp"

#include "LabelFilterBox.hpp"
#include "PageHeader.hpp"
#include "SongFilterProxy.hpp"
#include "SongPickerModel.hpp"
#include "SongTableModel.hpp"
#include "SongbookExporter.hpp"
#include "SongbookSorter.hpp"
#include "SongbookTree.hpp"
#include "convert.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>
#include <zub/core/latex.hpp>
#include <zub/core/textfile.hpp>

namespace {
const QString splitterKey = QStringLiteral("songbooks/splitter");
const QString repoPrefix = QStringLiteral("repo:");

using zub::songbook::BookClass;

QString sectionLabel(const std::string& name) {
    return name.empty() ? QObject::tr("Songbook (no section)") : toQString(name);
}

std::set<std::string> namesOf(const zub::songbook::Section& section) {
    return {section.songs.begin(), section.songs.end()};
}

std::vector<std::string> songsOf(const zub::bookfile::Environment& environment) {
    std::vector<std::string> songs;
    for (const auto& unit : environment.units)
        songs.insert(songs.end(), unit.songs.begin(), unit.songs.end());
    return songs;
}
} // namespace

SongbooksPage::SongbooksPage(SongTableModel* model, SongbookStore* store, SongbookSorter* sorter,
                             SongbookExporter* exporter, QWidget* parent)
    : QWidget(parent), model_(model), store_(store), sorter_(sorter), exporter_(exporter) {
    buildUi();
    connect(sorter_, &SongbookSorter::sorted, this, &SongbooksPage::onSorted);
    connect(sorter_, &SongbookSorter::failed, this, [this](const QString&, const QString& message) {
        sortError_ = tr("Not sorted: %1").arg(message);
        updateSorterWarning();
    });
    connect(sorter_, &SongbookSorter::warning, this, [this](const QString& message) {
        localeWarning_ = message;
        updateSorterWarning();
    });
    // The song list changes (reload, import): names and languages follow.
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        refreshLanguages();
        rebuildTree();
        updatePicker();
    });
    connect(model_, &QAbstractItemModel::rowsInserted, this, [this] {
        rebuildTree();
        updatePicker();
    });
    connect(model_, &QAbstractItemModel::dataChanged, this, [this] { rebuildTree(); });
    reloadList();
}

void SongbooksPage::setSongbookRoot(const std::filesystem::path& root) {
    if (root == root_)
        return;
    root_ = root;
    fillList(id_);
}

void SongbooksPage::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* bar = createPageHeader(tr("Songbooks"), this);
    outer->addWidget(bar);

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->addWidget(buildListPane());
    splitter_->addWidget(buildEditPane());
    splitter_->addWidget(buildPickerPane());
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 1);
    splitter_->setSizes({180, 520, 600});
    splitter_->restoreState(QSettings{}.value(splitterKey).toByteArray());
    connect(splitter_, &QSplitter::splitterMoved, this,
            [this] { QSettings{}.setValue(splitterKey, splitter_->saveState()); });
    outer->addWidget(splitter_, 1);
}

QWidget* SongbooksPage::buildListPane() {
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    list_ = new QTreeWidget(pane);
    list_->setObjectName(QStringLiteral("songbookList"));
    list_->setHeaderHidden(true);
    list_->setColumnCount(1);
    layout->addWidget(list_, 1);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("New"), pane);
    duplicate_ = new QPushButton(tr("Duplicate"), pane);
    delete_ = new QPushButton(tr("Delete"), pane);
    buttons->addWidget(add);
    buttons->addWidget(duplicate_);
    buttons->addWidget(delete_);
    layout->addLayout(buttons);
    connect(list_, &QTreeWidget::currentItemChanged, this, [this] {
        if (list_->currentItem() && listItemOf(id_) == list_->currentItem())
            return; // the same book
        onListRowChanged();
    });
    connect(add, &QPushButton::clicked, this, &SongbooksPage::newSongbook);
    connect(duplicate_, &QPushButton::clicked, this, &SongbooksPage::duplicateSongbook);
    connect(delete_, &QPushButton::clicked, this, &SongbooksPage::deleteSongbook);
    return pane;
}

QWidget* SongbooksPage::buildEditPane() {
    editPane_ = new QWidget(this);
    auto* layout = new QVBoxLayout(editPane_);
    repoBox_ = new QWidget(editPane_);
    auto* repoRow = new QHBoxLayout(repoBox_);
    repoRow->setContentsMargins(0, 0, 0, 0);
    repoInfo_ = new QLabel(repoBox_);
    repoInfo_->setWordWrap(true);
    repoInfo_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* openFolder = new QPushButton(tr("Open Folder"), repoBox_);
    openFolder->setToolTip(tr("Show the songbook's folder (edit its .tex file there)"));
    repoRow->addWidget(repoInfo_, 1);
    repoRow->addWidget(openFolder, 0, Qt::AlignTop);
    connect(openFolder, &QPushButton::clicked, this, [this] {
        if (repo_)
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(toQString(repoPathOf(id_).parent_path())));
    });
    layout->addWidget(repoBox_);
    repoBox_->hide();
    formBox_ = new QWidget(editPane_);
    form_ = new QFormLayout(formBox_);
    form_->setContentsMargins(0, 0, 0, 0);
    title_ = new QLineEdit(editPane_);
    author_ = new QLineEdit(editPane_);
    class_ = new QComboBox(editPane_);
    class_->addItem(tr("Velký zpěvník (A4)"), static_cast<int>(BookClass::Velky));
    class_->addItem(tr("Malý zpěvník (A5 book)"), static_cast<int>(BookClass::Maly));
    class_->addItem(tr("Táborový zpěvník (A5 camp booklet)"), static_cast<int>(BookClass::Tabor));
    chorded_ = new QCheckBox(tr("Chords"), editPane_);
    twoColumns_ = new QCheckBox(tr("Two song columns"), editPane_);
    shading_ = new QCheckBox(tr("Shading"), editPane_);
    chordChart_ = new QCheckBox(tr("Chord chart at the end"), editPane_);
    printReady_ =
        new QCheckBox(tr("Ready for binding (blank leaves, pages to a multiple of 4)"), editPane_);
    campYear_ = new QLineEdit(editPane_);
    campYear_->setPlaceholderText(tr("this year"));
    form_->addRow(tr("Title"), title_);
    form_->addRow(tr("Author"), author_);
    form_->addRow(tr("Class"), class_);
    form_->addRow(chorded_);
    form_->addRow(twoColumns_);
    form_->addRow(shading_);
    form_->addRow(chordChart_);
    form_->addRow(printReady_);
    authorIndex_ = new QCheckBox(tr("Author index"), editPane_);
    form_->addRow(authorIndex_);
    auto* coverRow = new QHBoxLayout;
    coverName_ = new QLabel(editPane_);
    coverChoose_ = new QPushButton(tr("Choose…"), editPane_);
    coverClear_ = new QPushButton(tr("None"), editPane_);
    coverRow->addWidget(coverName_, 1);
    coverRow->addWidget(coverChoose_);
    coverRow->addWidget(coverClear_);
    form_->addRow(tr("Cover image"), coverRow);
    form_->addRow(tr("Camp year"), campYear_);
    connect(coverChoose_, &QPushButton::clicked, this, &SongbooksPage::chooseCover);
    connect(coverClear_, &QPushButton::clicked, this, [this] {
        if (loading_ || !editable_ || book_.coverImage.empty())
            return;
        book_.coverImage.clear();
        updateCoverLabel();
        saveBook();
    });
    layout->addWidget(formBox_);

    for (QLineEdit* edit : {title_, author_, campYear_})
        connect(edit, &QLineEdit::editingFinished, this, &SongbooksPage::applyForm);
    for (QCheckBox* box : {chorded_, twoColumns_, shading_, chordChart_, printReady_, authorIndex_})
        connect(box, &QCheckBox::toggled, this, &SongbooksPage::applyForm);
    connect(class_, &QComboBox::currentIndexChanged, this, &SongbooksPage::applyForm);

    layout->addWidget(new QLabel(tr("Sections"), editPane_));
    warning_ = new QLabel(editPane_);
    warning_->setWordWrap(true);
    warning_->setStyleSheet(QStringLiteral("color: #b36b00;"));
    warning_->hide();
    layout->addWidget(warning_);
    sorterWarning_ = new QLabel(editPane_);
    sorterWarning_->setWordWrap(true);
    sorterWarning_->setStyleSheet(QStringLiteral("color: #b36b00;"));
    sorterWarning_->hide();
    layout->addWidget(sorterWarning_);
    tree_ = new SongbookTree(editPane_);
    layout->addWidget(tree_, 1);
    sectionsHint_ = new QLabel(
        tr("The sections and everything outside the song lists are edited in the .tex file."),
        editPane_);
    sectionsHint_->setWordWrap(true);
    sectionsHint_->setStyleSheet(QStringLiteral("color: gray;"));
    sectionsHint_->hide();
    layout->addWidget(sectionsHint_);

    auto* buttons = new QHBoxLayout;
    auto* add = addSection_ = new QPushButton(tr("Add Section"), editPane_);
    renameSection_ = new QPushButton(tr("Rename Section"), editPane_);
    removeSection_ = new QPushButton(tr("Remove Section"), editPane_);
    moveUp_ = new QPushButton(tr("Move Up"), editPane_);
    moveDown_ = new QPushButton(tr("Move Down"), editPane_);
    for (QPushButton* button : {add, renameSection_, removeSection_, moveUp_, moveDown_})
        buttons->addWidget(button);
    layout->addLayout(buttons);
    auto* songButtons = new QHBoxLayout;
    sortAll_ = new QPushButton(tr("Sort Alphabetically"), editPane_);
    sortAll_->setToolTip(tr("Sort the songs of every section alphabetically again"));
    removeSong_ = new QPushButton(tr("Remove Songs"), editPane_);
    songButtons->addWidget(sortAll_);
    songButtons->addWidget(removeSong_);
    songButtons->addStretch(1);
    layout->addLayout(songButtons);
    count_ = new QLabel(editPane_);
    layout->addWidget(count_);

    connect(add, &QPushButton::clicked, this, &SongbooksPage::addSection);
    connect(renameSection_, &QPushButton::clicked, this,
            [this] { renameSection(currentSection()); });
    connect(removeSection_, &QPushButton::clicked, this,
            [this] { removeSection(currentSection()); });
    connect(moveUp_, &QPushButton::clicked, this, [this] { moveSection(-1); });
    connect(moveDown_, &QPushButton::clicked, this, [this] { moveSection(1); });
    connect(removeSong_, &QPushButton::clicked, this, &SongbooksPage::removeSelectedSongs);
    connect(sortAll_, &QPushButton::clicked, this, &SongbooksPage::sortAll);
    connect(tree_, &QTreeWidget::currentItemChanged, this, &SongbooksPage::onTreeSelectionChanged);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &SongbooksPage::updateButtons);
    connect(tree_, &SongbookTree::removeSelectedRequested, this,
            &SongbooksPage::removeSelectedSongs);
    connect(tree_, &SongbookTree::songsDropped, this,
            [this](std::vector<std::string> names, quint64 from, quint64 to, int index) {
                onSongsDropped(names, from, to, index);
            });
    connect(tree_, &SongbookTree::sortSectionRequested, this, [this](quint64 uid) {
        const int s = indexOfUid(uid);
        if (editable_ && s >= 0)
            requestSort(s, SortMode::Replace);
    });
    connect(tree_, &SongbookTree::renameSectionRequested, this,
            [this](quint64 uid) { renameSection(indexOfUid(uid)); });
    connect(tree_, &SongbookTree::removeSectionRequested, this,
            [this](quint64 uid) { removeSection(indexOfUid(uid)); });
    return editPane_;
}

QWidget* SongbooksPage::buildPickerPane() {
    pickerPane_ = new QWidget(this);
    auto* layout = new QVBoxLayout(pickerPane_);
    pickerHeader_ = new QLabel(pickerPane_);
    pickerHeader_->setWordWrap(true);
    pickerHeader_->setText(
        tr("Select songs (Ctrl/Shift for several), then Add — or drag them onto the songbook."));
    layout->addWidget(pickerHeader_);

    auto* targetRow = new QHBoxLayout;
    target_ = new QComboBox(pickerPane_);
    target_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    addPicked_ = new QPushButton(tr("Add"), pickerPane_);
    removePicked_ = new QPushButton(tr("Remove"), pickerPane_);
    targetRow->addWidget(new QLabel(tr("Add to:"), pickerPane_));
    targetRow->addWidget(target_, 1);
    targetRow->addWidget(addPicked_);
    targetRow->addWidget(removePicked_);
    layout->addLayout(targetRow);

    auto* filters = new QHBoxLayout;
    search_ = new QLineEdit(pickerPane_);
    search_->setPlaceholderText(tr("Search title, artist…"));
    search_->setClearButtonEnabled(true);
    language_ = new QComboBox(pickerPane_);
    language_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    onlyInSection_ = new QCheckBox(tr("Only songs in this section"), pickerPane_);
    filters->addWidget(search_, 1);
    filters->addWidget(language_);
    favouritesOnly_ = new QCheckBox(tr("Only favourites"), pickerPane_);
    favouritesOnly_->setToolTip(tr("Songs marked with ★"));
    labelFilter_ = new LabelFilterBox(model_, pickerPane_);
    filters->addWidget(favouritesOnly_);
    filters->addWidget(labelFilter_);
    layout->addLayout(filters);
    layout->addWidget(onlyInSection_);

    pickerFilter_ = new SongFilterProxy(this);
    pickerFilter_->setSourceModel(model_);
    picker_ = new SongPickerModel(this);
    picker_->setSourceModel(pickerFilter_);
    pickerView_ = new QTableView(pickerPane_);
    pickerView_->setModel(picker_);
    pickerView_->setSortingEnabled(true);
    pickerView_->sortByColumn(SongTableModel::Title, Qt::AscendingOrder);
    pickerView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pickerView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pickerView_->setDragEnabled(true);
    pickerView_->setDragDropMode(QAbstractItemView::DragOnly);
    pickerView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pickerView_->verticalHeader()->hide();
    for (const int column :
         {SongTableModel::Tempo, SongTableModel::Meter, SongTableModel::Capo, SongTableModel::Tabs,
          SongTableModel::Labels, SongTableModel::Todo, SongTableModel::Changes})
        pickerView_->setColumnHidden(column, true);
    pickerView_->horizontalHeader()->setSectionResizeMode(SongTableModel::Title,
                                                          QHeaderView::Stretch);
    pickerView_->horizontalHeader()->setSectionResizeMode(SongTableModel::Artist,
                                                          QHeaderView::Stretch);
    layout->addWidget(pickerView_, 1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    check_ = new QPushButton(tr("Check…"), pickerPane_);
    check_->setObjectName(QStringLiteral("songbookCheck"));
    check_->setToolTip(tr("Songs of this songbook not marked as fitting its paper (their pap)"));
    exportPdf_ = new QPushButton(tr("Export PDF…"), pickerPane_);
    exportLatex_ = new QPushButton(tr("Export LaTeX…"), pickerPane_);
    buttons->addWidget(check_);
    buttons->addWidget(exportPdf_);
    buttons->addWidget(exportLatex_);
    layout->addLayout(buttons);

    connect(search_, &QLineEdit::textChanged, pickerFilter_, &SongFilterProxy::setSearchText);
    connect(language_, &QComboBox::currentIndexChanged, this,
            [this] { pickerFilter_->setLanguage(language_->currentData().toString()); });
    connect(favouritesOnly_, &QCheckBox::toggled, pickerFilter_,
            &SongFilterProxy::setFavouritesOnly);
    connect(labelFilter_, &LabelFilterBox::labelChanged, pickerFilter_, &SongFilterProxy::setLabel);
    connect(onlyInSection_, &QCheckBox::toggled, this, [this] { updatePicker(); });
    connect(pickerView_, &QTableView::clicked, this, &SongbooksPage::onPickerClicked);
    connect(addPicked_, &QPushButton::clicked, this, &SongbooksPage::addPicked);
    connect(removePicked_, &QPushButton::clicked, this, &SongbooksPage::removePicked);
    connect(target_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (loading_ || index < 0 || index >= static_cast<int>(book_.sections.size()))
            return;
        section_ = index;
        updatePicker();
        updateButtons();
    });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* enter =
            new QShortcut(QKeySequence(key), pickerView_, nullptr, nullptr, Qt::WidgetShortcut);
        connect(enter, &QShortcut::activated, this, &SongbooksPage::addPicked);
    }
    connect(check_, &QPushButton::clicked, this, &SongbooksPage::checkBook);
    connect(exportPdf_, &QPushButton::clicked, this,
            [this] {
                if (repo_)
                    exporter_->exportRepoPdf(repoPathOf(id_), repoText_, book_);
                else
                    exporter_->exportPdf(id_, book_, titleLookup());
            });
    connect(exportLatex_, &QPushButton::clicked, this,
            [this] { exporter_->exportLatex(id_, book_, titleLookup()); });
    refreshLanguages();
    return pickerPane_;
}

void SongbooksPage::refreshLanguages() {
    const QString current = language_->currentData().toString();
    std::set<QString> languages;
    for (int row = 0; row < model_->rowCount(); ++row) {
        // Every language of every song: also the ones a song is only tagged with.
        for (const QString& lang :
             model_->index(row, 0).data(SongTableModel::LanguageRole).toStringList())
            languages.insert(lang);
    }
    const QSignalBlocker blocker(language_);
    language_->clear();
    language_->addItem(tr("All languages"), QString{});
    for (const QString& lang : languages)
        language_->addItem(lang, lang);
    const int index = language_->findData(current);
    language_->setCurrentIndex(index < 0 ? 0 : index);
    pickerFilter_->setLanguage(language_->currentData().toString());
}

// ---- The list of songbooks ----

void SongbooksPage::reloadList() {
    fillList(id_);
}

void SongbooksPage::fillList(const QString& selectId) {
    entries_ = store_->list();
    {
        const QSignalBlocker blocker(list_);
        list_->clear();
        QTreeWidgetItem* selected = nullptr;
        const auto addBook = [&](QTreeWidgetItem* parent, const QString& id, const QString& text) {
            auto* item = new QTreeWidgetItem(parent, {text});
            item->setData(0, Qt::UserRole, id);
            if (id == selectId)
                selected = item;
            return item;
        };
        const auto addRoot = [&](const QString& text) {
            auto* item = new QTreeWidgetItem(list_, {text});
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            item->setFlags(Qt::ItemIsEnabled);
            item->setExpanded(true);
            return item;
        };

        if (!root_.empty()) {
            QTreeWidgetItem* repoRoot = addRoot(tr("Songbooks"));
            repoRoot->setToolTip(0, tr("The songbooks of the songbook folder (songbooks/)"));
            std::map<std::filesystem::path, QTreeWidgetItem*> folders;
            const auto books = zub::bookfile::findBooks(root_);
            // A folder holding nothing but its one book of the same name (velkyzpevnik/
            // velkyzpevnik.tex) is shown as that book alone.
            const auto aloneInFolder = [&](const std::filesystem::path& relative) {
                const auto dir = relative.parent_path();
                if (relative.stem() != dir.filename())
                    return false;
                return std::ranges::count_if(books, [&](const std::filesystem::path& other) {
                           const auto [end, _] = std::ranges::mismatch(dir, other.parent_path());
                           return end == dir.end();
                       }) == 1;
            };
            for (const auto& relative : books) {
                // songbooks/tabory/2025/tabor2025.tex: folders tabory, 2025 under the root
                QTreeWidgetItem* parent = repoRoot;
                std::filesystem::path folder;
                auto dir = relative.parent_path();
                if (aloneInFolder(relative))
                    dir = dir.parent_path();
                for (auto part = std::next(dir.begin()); part != dir.end(); ++part) {
                    folder /= *part;
                    auto& item = folders[folder];
                    if (!item) {
                        item = new QTreeWidgetItem(parent, {toQString(part->filename())});
                        item->setFlags(Qt::ItemIsEnabled);
                        item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                        item->setExpanded(true);
                    }
                    parent = item;
                }
                auto* item = addBook(parent, repoPrefix + toQString(relative.generic_string()),
                                     toQString(relative.stem()));
                item->setToolTip(0, toQString(relative.generic_string()));
            }
        }
        QTreeWidgetItem* external = addRoot(tr("External"));
        external->setToolTip(0, tr("Songbooks kept by this app, outside the songbook folder"));
        for (const auto& entry : entries_) {
            if (entry.error.isEmpty()) {
                addBook(external, entry.id, toQString(entry.book.title));
            } else {
                auto* item = addBook(external, entry.id, entry.id);
                item->setForeground(0, Qt::red);
                item->setToolTip(0, entry.error);
            }
        }
        list_->expandAll();
        list_->setCurrentItem(selected);
    }
    id_.clear(); // force loadBook() to read the selected entry
    onListRowChanged();
}

QTreeWidgetItem* SongbooksPage::listItemOf(const QString& id) const {
    if (id.isEmpty())
        return nullptr;
    for (QTreeWidgetItemIterator it(list_); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == id)
            return *it;
    return nullptr;
}

bool SongbooksPage::isRepoId(const QString& id) {
    return id.startsWith(repoPrefix);
}

std::filesystem::path SongbooksPage::repoPathOf(const QString& id) const {
    return root_ / toPath(id.mid(repoPrefix.size()));
}

void SongbooksPage::onListRowChanged() {
    const QTreeWidgetItem* item = list_->currentItem();
    editable_ = false;
    repo_ = false;
    id_.clear();
    book_ = {};
    repoText_.clear();
    repoBook_ = {};
    sectionUids_.clear();
    const QString id = item ? item->data(0, Qt::UserRole).toString() : QString();
    if (isRepoId(id)) {
        id_ = id;
        repo_ = true;
        if (loadRepoBook()) {
            resetSectionUids();
            editable_ = true;
            showWarning({});
            for (const auto& environment : repoBook_.environments) {
                if (!environment.readOnly.empty()) {
                    editable_ = false;
                    showWarning(tr("This songbook is shown read-only: %1")
                                    .arg(toQString(environment.readOnly)));
                    break;
                }
            }
        }
    } else if (!id.isEmpty()) {
        for (const auto& entry : entries_) {
            if (entry.id != id)
                continue;
            id_ = entry.id;
            if (entry.error.isEmpty()) {
                book_ = entry.book;
                zub::songbook::normalize(book_);
                resetSectionUids();
                editable_ = true;
                showWarning({});
            } else {
                showWarning(
                    tr("Cannot read this songbook (the file is left alone): %1").arg(entry.error));
            }
        }
    } else {
        showWarning({});
    }
    section_ = 0;
    loadBook();
}

bool SongbooksPage::loadRepoBook() {
    const auto path = repoPathOf(id_);
    auto text = zub::readTextFile(path);
    if (!text) {
        showWarning(tr("Cannot read %1").arg(toQString(path)));
        return false;
    }
    auto parsed = zub::bookfile::parse(*text);
    if (!parsed) {
        showWarning(
            tr("Cannot read the songbook %1: %2").arg(toQString(path), toQString(parsed.error())));
        return false;
    }
    repoText_ = std::move(*text);
    repoBook_ = std::move(*parsed);
    book_ = {};
    book_.title = zub::latex::toDisplayText(repoBook_.title);
    book_.author = zub::latex::toDisplayText(repoBook_.author);
    book_.bookClass = zub::songbook::bookClassFromName(repoBook_.documentClass)
                          .value_or(zub::songbook::BookClass::Velky);
    for (const auto& environment : repoBook_.environments) {
        book_.sections.push_back({environment.section, songsOf(environment)});
    }
    return true;
}

bool SongbooksPage::saveRepoBook() {
    const auto path = repoPathOf(id_);
    const auto onDisk = zub::readTextFile(path);
    if (!onDisk || *onDisk != repoText_) {
        // Changed elsewhere (an editor, git): never overwrite it.
        const QString id = id_;
        fillList(id);
        showWarning(tr("%1 was changed outside the app: it is read again, and the last change "
                       "was not saved. Make it once more.")
                        .arg(toQString(path.filename())));
        return false;
    }
    std::string text = repoText_;
    for (std::size_t e = 0; e < repoBook_.environments.size() && e < book_.sections.size(); ++e) {
        const auto& songs = book_.sections[e].songs;
        if (songs == songsOf(repoBook_.environments[e]))
            continue;
        auto edited = zub::bookfile::setSongs(text, e, songs);
        if (!edited) {
            showWarning(tr("Not saved: %1").arg(toQString(edited.error())));
            loadRepoBook();
            return false;
        }
        text = std::move(*edited);
    }
    if (text != repoText_) {
        QSaveFile file(toQString(path));
        const QByteArray bytes = QByteArray::fromStdString(text);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
            !file.commit()) {
            showWarning(tr("Cannot write %1: %2").arg(toQString(path), file.errorString()));
            loadRepoBook();
            return false;
        }
    }
    // What the file now says (a keep-order block keeps its order whatever was asked).
    const auto keepUids = sectionUids_;
    if (!loadRepoBook())
        return false;
    if (keepUids.size() == book_.sections.size())
        sectionUids_ = keepUids;
    else
        resetSectionUids();
    return true;
}

void SongbooksPage::newSongbook() {
    zub::songbook::Songbook book;
    book.title = toStdString(tr("New songbook"));
    book.sections.push_back({});
    QString error;
    const QString id = store_->create(book, &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("Songbooks"), error);
        return;
    }
    fillList(id);
    title_->setFocus();
    title_->selectAll();
}

void SongbooksPage::duplicateSongbook() {
    if (!editable_ || repo_)
        return;
    zub::songbook::Songbook copy = book_;
    copy.title += toStdString(tr(" (copy)"));
    QString error;
    const QString id = store_->create(copy, &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("Songbooks"), error);
        return;
    }
    fillList(id);
}

void SongbooksPage::deleteSongbook() {
    if (id_.isEmpty() || repo_)
        return;
    const QTreeWidgetItem* item = listItemOf(id_);
    const QString name = item ? item->text(0) : id_;
    if (QMessageBox::question(this, tr("Delete songbook"),
                              tr("Delete the songbook %1? This cannot be undone.").arg(name),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;
    if (!store_->remove(id_)) {
        QMessageBox::warning(this, tr("Songbooks"), tr("Cannot delete the songbook file."));
        return;
    }
    const QString none;
    fillList(none);
}

// ---- The selected songbook ----

void SongbooksPage::loadBook() {
    loading_ = true;
    editPane_->setEnabled(editable_ || repo_);
    formBox_->setVisible(!repo_);
    repoBox_->setVisible(repo_);
    sectionsHint_->setVisible(repo_);
    tree_->setEnabled(editable_ || !repo_);
    tree_->setSectionEditing(!repo_);
    for (QPushButton* button : {addSection_, renameSection_, removeSection_, moveUp_, moveDown_})
        button->setVisible(!repo_);
    if (repo_) {
        const QString paper = toQString(zub::songbook::paperName(book_.bookClass)).toUpper();
        repoInfo_->setText(
            tr("<b>%1</b><br>%2 · class %3 (%4)")
                .arg(book_.title.empty() ? QString() : toQString(book_.title).toHtmlEscaped(),
                     id_.mid(repoPrefix.size()).toHtmlEscaped(),
                     toQString(repoBook_.documentClass).toHtmlEscaped(), paper));
    }
    pickerPane_->setEnabled(editable_);
    title_->setText(toQString(book_.title));
    author_->setText(toQString(book_.author));
    class_->setCurrentIndex(class_->findData(static_cast<int>(book_.bookClass)));
    chorded_->setChecked(book_.chorded);
    twoColumns_->setChecked(book_.twoColumns);
    shading_->setChecked(book_.shading);
    chordChart_->setChecked(book_.chordChart);
    printReady_->setChecked(book_.printReady);
    campYear_->setText(toQString(book_.campYear));
    authorIndex_->setChecked(book_.authorIndex);
    loading_ = false;
    updateCoverLabel();
    updateFormVisibility();
    rebuildTree();
    updatePicker();
    updateButtons();
}

void SongbooksPage::updateFormVisibility() {
    form_->setRowVisible(chordChart_, book_.bookClass != BookClass::Tabor);
    form_->setRowVisible(printReady_, book_.bookClass == BookClass::Maly);
    form_->setRowVisible(campYear_, book_.bookClass == BookClass::Tabor);
    form_->setRowVisible(authorIndex_, book_.bookClass != BookClass::Tabor);
}

void SongbooksPage::updateCoverLabel() {
    coverClear_->setEnabled(editable_ && !book_.coverImage.empty());
    coverChoose_->setEnabled(editable_);
    if (book_.coverImage.empty()) {
        coverName_->setText(tr("None"));
        coverName_->setStyleSheet(QStringLiteral("color: gray;"));
        coverName_->setToolTip(QString());
        return;
    }
    const QString path = toQString(book_.coverImage);
    const bool missing = !QFileInfo(path).isFile();
    coverName_->setText(QFileInfo(path).fileName());
    coverName_->setStyleSheet(missing ? QStringLiteral("color: red;") : QString());
    coverName_->setToolTip(missing ? tr("%1 does not exist").arg(path) : path);
}

void SongbooksPage::chooseCover() {
    if (loading_ || !editable_)
        return;
    QSettings settings;
    const QString key = QStringLiteral("songbooks/coverDir");
    QString dir = settings.value(key).toString();
    if (dir.isEmpty() || !QDir(dir).exists())
        dir = QDir::homePath();
    const QString file = QFileDialog::getOpenFileName(this, tr("Cover image"), dir,
                                                      tr("Images (*.png *.jpg *.jpeg *.pdf)"));
    if (file.isEmpty())
        return;
    settings.setValue(key, QFileInfo(file).absolutePath());
    const std::string path = toStdString(QFileInfo(file).absoluteFilePath());
    if (path == book_.coverImage)
        return;
    book_.coverImage = path;
    updateCoverLabel();
    saveBook();
}

std::function<std::string(std::string_view)> SongbooksPage::titleLookup() const {
    std::map<std::string, std::string, std::less<>> titles;
    for (int row = 0; row < model_->rowCount(); ++row)
        if (const zub::SongEntry* song = model_->song(row))
            titles.emplace(song->inputName, song->meta.title);
    return [titles = std::move(titles)](std::string_view name) {
        const auto it = titles.find(name);
        return it == titles.end() ? std::string() : it->second;
    };
}

bool SongbooksPage::saveBook() {
    if (!editable_ || id_.isEmpty())
        return false;
    if (repo_)
        return saveRepoBook();
    QString error;
    if (!store_->save(id_, book_, &error)) {
        showWarning(error);
        return false;
    }
    for (auto& entry : entries_) {
        if (entry.id == id_) {
            entry.book = book_;
            if (QTreeWidgetItem* item = listItemOf(id_))
                item->setText(0, toQString(book_.title));
        }
    }
    return true;
}

void SongbooksPage::applyForm() {
    if (loading_ || !editable_)
        return;
    zub::songbook::Songbook book = book_;
    book.title = toStdString(title_->text());
    book.author = toStdString(author_->text());
    book.bookClass = static_cast<BookClass>(class_->currentData().toInt());
    book.chorded = chorded_->isChecked();
    book.twoColumns = twoColumns_->isChecked();
    book.shading = shading_->isChecked();
    book.chordChart = chordChart_->isChecked();
    book.printReady = printReady_->isChecked();
    book.campYear = toStdString(campYear_->text().trimmed());
    book.authorIndex = authorIndex_->isChecked();
    if (book == book_)
        return;
    book_ = std::move(book);
    updateFormVisibility();
    saveBook();
}

// ---- Sections ----

int SongbooksPage::currentSection() const {
    return std::clamp(section_, 0, std::max(0, static_cast<int>(book_.sections.size()) - 1));
}

int SongbooksPage::indexOfUid(quint64 uid) const {
    const auto it = std::ranges::find(sectionUids_, uid);
    return it == sectionUids_.end() ? -1 : static_cast<int>(it - sectionUids_.begin());
}

void SongbooksPage::commit() {
    saveBook();
    rebuildTree();
    updatePicker();
}

void SongbooksPage::rebuildTree() {
    std::map<std::string, int> rowOfName;
    for (int row = 0; row < model_->rowCount(); ++row)
        if (const zub::SongEntry* song = model_->song(row))
            rowOfName.emplace(song->inputName, row);

    const int selectedSection = currentSection();
    int total = 0;
    {
        const QSignalBlocker blocker(tree_);
        tree_->clear();
        // A repo book may start with a named section: then every section has its header and
        // nothing is outside them.
        const bool rootPart = !book_.sections.empty() && book_.sections.front().name.empty();
        tree_->setRootUid(sectionUids_.empty() || !rootPart ? 0 : sectionUids_.front());
        QTreeWidgetItem* selectedItem = nullptr;
        for (std::size_t s = 0; s < book_.sections.size() && s < sectionUids_.size(); ++s) {
            const auto& section = book_.sections[s];
            QTreeWidgetItem* header = nullptr;
            if (s > 0 || !rootPart) {
                header = tree_->addSection(
                    sectionUids_[s],
                    toQString(section.name) + QStringLiteral(" — ") +
                        tr("%n song(s)", nullptr, static_cast<int>(section.songs.size())));
                if (static_cast<int>(s) == selectedSection)
                    selectedItem = header;
            }
            for (const std::string& name : section.songs) {
                const auto it = rowOfName.find(name);
                if (it != rowOfName.end()) {
                    const QString title =
                        model_->index(it->second, SongTableModel::Title).data().toString();
                    const QString artist =
                        model_->index(it->second, SongTableModel::Artist).data().toString();
                    tree_->addSong(header, sectionUids_[s], name,
                                   title + QStringLiteral(" — ") + artist, false);
                } else {
                    tree_->addSong(header, sectionUids_[s], name,
                                   tr("%1 (not in the songbook)").arg(toQString(name)), true);
                }
                ++total;
            }
            if (header)
                header->setExpanded(true);
        }
        if (selectedItem)
            tree_->setCurrentItem(selectedItem);
    }
    fillTargetCombo();
    int named = 0;
    for (const auto& section : book_.sections)
        named += section.name.empty() ? 0 : 1;
    count_->setText(named == 0 ? tr("%n song(s)", nullptr, total)
                               : tr("%n song(s)", nullptr, total) + QStringLiteral(" ") +
                                     tr("in %n section(s)", nullptr, named));
    updateButtons();
}

void SongbooksPage::fillTargetCombo() {
    const QSignalBlocker blocker(target_);
    target_->clear();
    for (const auto& section : book_.sections)
        target_->addItem(sectionLabel(section.name));
    target_->setCurrentIndex(book_.sections.empty() ? -1 : currentSection());
}

void SongbooksPage::onTreeSelectionChanged() {
    if (const QTreeWidgetItem* item = tree_->currentItem()) {
        const int index = indexOfUid(SongbookTree::uidOf(item));
        if (index >= 0) {
            section_ = index;
            const QSignalBlocker blocker(target_);
            target_->setCurrentIndex(index);
        }
    }
    updatePicker();
    updateButtons();
}

void SongbooksPage::updatePicker() {
    if (!editable_ || book_.sections.empty()) {
        picker_->setCurrent({});
        picker_->setOtherSections({});
        pickerFilter_->setOnlyNames(std::nullopt);
        return;
    }
    const int current = currentSection();
    const auto& section = book_.sections[static_cast<std::size_t>(current)];
    std::map<std::string, QStringList> others;
    for (int s = 0; s < static_cast<int>(book_.sections.size()); ++s) {
        if (s == current)
            continue;
        const auto& other = book_.sections[static_cast<std::size_t>(s)];
        for (const auto& name : other.songs)
            others[name] << sectionLabel(other.name);
    }
    std::map<std::string, QString> joined;
    for (const auto& [name, sections] : others)
        joined[name] = sections.join(QStringLiteral(", "));
    picker_->setCurrent(namesOf(section));
    picker_->setOtherSections(std::move(joined));
    pickerFilter_->setOnlyNames(onlyInSection_->isChecked()
                                    ? std::optional<std::set<std::string>>(namesOf(section))
                                    : std::nullopt);
}

void SongbooksPage::updateButtons() {
    const bool selected = !id_.isEmpty();
    duplicate_->setEnabled(editable_ && !repo_);
    delete_->setEnabled(selected && !repo_);
    bool hasSongs = false;
    for (const auto& section : book_.sections)
        hasSongs = hasSongs || !section.songs.empty();
    check_->setEnabled(editable_ && hasSongs);
    exportPdf_->setEnabled(editable_ && hasSongs);
    exportLatex_->setEnabled(editable_ && hasSongs && !repo_);
    sortAll_->setEnabled(editable_ && hasSongs);

    const int section = currentSection();
    const int count = static_cast<int>(book_.sections.size());
    renameSection_->setEnabled(editable_ && section > 0);
    removeSection_->setEnabled(editable_ && section > 0);
    removeSong_->setEnabled(editable_ && !tree_->selectedSongs().empty());
    moveUp_->setEnabled(editable_ && section > 1);
    moveDown_->setEnabled(editable_ && section > 0 && section + 1 < count);
    addPicked_->setEnabled(editable_ && count > 0);
    removePicked_->setEnabled(editable_ && count > 0);
}

bool SongbooksPage::askSectionName(const QString& title, const QString& initial, int ignoreSection,
                                   QString* name) {
    bool ok = false;
    const QString text =
        QInputDialog::getText(this, title, tr("Section name:"), QLineEdit::Normal, initial, &ok)
            .trimmed();
    if (!ok)
        return false;
    if (text.isEmpty()) {
        QMessageBox::information(this, title, tr("The section needs a name."));
        return false;
    }
    for (int s = 1; s < static_cast<int>(book_.sections.size()); ++s) {
        if (s != ignoreSection &&
            toQString(book_.sections[static_cast<std::size_t>(s)].name) == text) {
            QMessageBox::information(this, title, tr("There is a section with this name already."));
            return false;
        }
    }
    *name = text;
    return true;
}

void SongbooksPage::addSection() {
    QString name;
    if (!editable_ || repo_ || !askSectionName(tr("Add Section"), {}, -1, &name))
        return;
    book_.sections.push_back({toStdString(name), {}});
    sectionUids_.push_back(nextUid_++);
    section_ = static_cast<int>(book_.sections.size()) - 1;
    commit();
}

void SongbooksPage::renameSection(int s) {
    if (!editable_ || repo_ || s < 1 || s >= static_cast<int>(book_.sections.size()))
        return;
    auto& section = book_.sections[static_cast<std::size_t>(s)];
    QString name;
    if (!askSectionName(tr("Rename Section"), toQString(section.name), s, &name))
        return;
    section.name = toStdString(name);
    section_ = s;
    commit();
}

void SongbooksPage::removeSection(int s) {
    if (!editable_ || repo_ || s < 1 || s >= static_cast<int>(book_.sections.size()))
        return;
    const auto& section = book_.sections[static_cast<std::size_t>(s)];
    if (!section.songs.empty() &&
        QMessageBox::question(this, tr("Remove Section"),
                              tr("Remove the section %1 and its %n song(s) from the songbook?",
                                 nullptr, static_cast<int>(section.songs.size()))
                                  .arg(toQString(section.name))) != QMessageBox::Yes)
        return;
    jobs_.erase(id_ + QLatin1Char('/') +
                QString::number(sectionUids_[static_cast<std::size_t>(s)]));
    book_.sections.erase(book_.sections.begin() + s);
    sectionUids_.erase(sectionUids_.begin() + s);
    section_ = std::min(s, static_cast<int>(book_.sections.size()) - 1);
    commit();
}

void SongbooksPage::moveSection(int delta) {
    const int from = currentSection();
    const int to = from + delta;
    if (!editable_ || repo_ || from < 1 || to < 1 || to >= static_cast<int>(book_.sections.size()))
        return;
    std::swap(book_.sections[static_cast<std::size_t>(from)],
              book_.sections[static_cast<std::size_t>(to)]);
    std::swap(sectionUids_[static_cast<std::size_t>(from)],
              sectionUids_[static_cast<std::size_t>(to)]);
    section_ = to;
    commit();
}

void SongbooksPage::removeSelectedSongs() {
    if (!editable_)
        return;
    bool changed = false;
    for (const auto& [uid, name] : tree_->selectedSongs()) {
        const int s = indexOfUid(uid);
        if (s < 0)
            continue;
        auto& songs = book_.sections[static_cast<std::size_t>(s)].songs;
        const auto it = std::ranges::find(songs, name);
        if (it == songs.end())
            continue;
        songs.erase(it);
        changed = true;
    }
    if (changed)
        commit();
}

void SongbooksPage::checkBook() {
    if (!editable_)
        return;
    // The songs of the library by input name: their pap and file.
    std::map<std::string, const zub::SongEntry*> byName;
    for (int row = 0; row < model_->rowCount(); ++row)
        if (const auto* song = model_->song(row))
            byName.emplace(song->inputName, song);
    const auto papOf = [&](std::string_view name) -> std::optional<std::string> {
        const auto it = byName.find(std::string(name));
        if (it == byName.end())
            return std::nullopt;
        return it->second->meta.value("pap");
    };
    const QString paper = toQString(zub::songbook::paperName(book_.bookClass)).toUpper();
    const auto unfit = zub::songbook::notMarkedFitting(book_, papOf);
    if (unfit.empty()) {
        QMessageBox::information(
            this, tr("Check"),
            tr("Every song of this songbook is marked as fitting %1.").arg(paper));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Check: %1").arg(toQString(book_.title)));
    auto* layout = new QVBoxLayout(&dialog);
    auto* label = new QLabel(&dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    auto* list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("songbookCheckList"));
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    const auto fill = [&] {
        list->clear();
        const auto now = zub::songbook::notMarkedFitting(book_, papOf);
        for (const std::string& name : now) {
            const auto* song = byName.at(name);
            const QString pap = toQString(song->meta.value("pap"));
            auto* item = new QListWidgetItem(
                tr("%1 — %2  (pap: %3)")
                    .arg(toQString(zub::latex::toDisplayText(song->meta.title)),
                         toQString(zub::latex::toDisplayText(song->meta.value("by"))),
                         pap.isEmpty() ? tr("none") : pap),
                list);
            item->setData(Qt::UserRole, toQString(name));
        }
        label->setText(tr("%n song(s) of this songbook not marked as fitting %1: nobody has "
                          "checked them at that size. Open one to measure it (Make Smaller "
                          "offers its pap), or take it out of the songbook.",
                          nullptr, static_cast<int>(now.size()))
                           .arg(paper));
        list->setCurrentRow(0);
    };
    fill();
    layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QPushButton* open = buttons->addButton(tr("Open Song"), QDialogButtonBox::ActionRole);
    QPushButton* remove =
        buttons->addButton(tr("Remove from the Songbook"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemSelectionChanged, &dialog, [=] {
        open->setEnabled(list->selectedItems().size() == 1);
        remove->setEnabled(!list->selectedItems().isEmpty());
    });
    connect(open, &QPushButton::clicked, &dialog, [&] {
        const auto items = list->selectedItems();
        if (items.size() != 1)
            return;
        const auto* song = byName.at(toStdString(items.front()->data(Qt::UserRole).toString()));
        const std::filesystem::path path = song->path;
        dialog.accept();
        emit openSongRequested(path);
    });
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        bool removed = false;
        for (QListWidgetItem* item : list->selectedItems())
            removed |=
                zub::songbook::removeSong(book_, toStdString(item->data(Qt::UserRole).toString()));
        if (!removed)
            return;
        commit();
        if (zub::songbook::notMarkedFitting(book_, papOf).empty())
            dialog.accept();
        else
            fill();
    });
    dialog.resize(560, 360);
    dialog.exec();
}

// ---- The picker ----

std::vector<std::string> SongbooksPage::pickerSelection() const {
    std::vector<int> rows;
    std::map<int, std::string> nameOfRow;
    for (const QModelIndex& index : pickerView_->selectionModel()->selectedIndexes()) {
        if (index.column() != SongTableModel::Title)
            continue;
        rows.push_back(index.row());
        nameOfRow[index.row()] = index.data(SongTableModel::InputNameRole).toString().toStdString();
    }
    std::ranges::sort(rows);
    std::vector<std::string> names;
    for (const int row : rows)
        if (const std::string& name = nameOfRow[row]; !name.empty())
            names.push_back(name);
    return names;
}

void SongbooksPage::onPickerClicked(const QModelIndex& index) {
    if (!editable_ || book_.sections.empty() || index.column() != SongTableModel::Title)
        return;
    if (QApplication::keyboardModifiers() & (Qt::ControlModifier | Qt::ShiftModifier))
        return;
    // Only a click on the check box itself toggles the song; elsewhere a click selects.
    QStyleOptionViewItem option;
    option.initFrom(pickerView_);
    option.rect = pickerView_->visualRect(index);
    option.features = QStyleOptionViewItem::HasCheckIndicator | QStyleOptionViewItem::HasDisplay;
    option.checkState = Qt::Checked;
    option.text = index.data().toString();
    option.font = pickerView_->font();
    option.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
    const QRect box = pickerView_->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,
                                                           &option, pickerView_);
    if (!box.contains(pickerView_->viewport()->mapFromGlobal(QCursor::pos())))
        return;
    const std::string name = index.data(SongTableModel::InputNameRole).toString().toStdString();
    if (name.empty())
        return;
    const int s = currentSection();
    auto& songs = book_.sections[static_cast<std::size_t>(s)].songs;
    const auto it = std::ranges::find(songs, name);
    if (it == songs.end()) {
        addSongs(s, {name}, -1);
    } else {
        songs.erase(it);
        commit();
    }
}

void SongbooksPage::addPicked() {
    if (!editable_ || book_.sections.empty())
        return;
    addSongs(currentSection(), pickerSelection(), -1);
}

void SongbooksPage::removePicked() {
    if (!editable_ || book_.sections.empty())
        return;
    auto& songs = book_.sections[static_cast<std::size_t>(currentSection())].songs;
    const auto names = pickerSelection();
    const std::set<std::string> remove(names.begin(), names.end());
    const auto removed =
        std::ranges::remove_if(songs, [&](const std::string& s) { return remove.contains(s); });
    if (removed.empty())
        return;
    songs.erase(removed.begin(), removed.end());
    commit();
}

// ---- Adding, moving and sorting ----

void SongbooksPage::addSongs(int target, const std::vector<std::string>& names, int from) {
    if (!editable_ || target < 0 || target >= static_cast<int>(book_.sections.size()))
        return;
    auto& songs = book_.sections[static_cast<std::size_t>(target)].songs;
    std::set<std::string> added;
    bool changed = false;
    for (const std::string& name : names) {
        if (from >= 0 && from < static_cast<int>(book_.sections.size())) {
            auto& source = book_.sections[static_cast<std::size_t>(from)].songs;
            const auto it = std::ranges::find(source, name);
            if (it == source.end())
                continue; // not there (any more): nothing to move
            source.erase(it);
            changed = true;
        }
        if (std::ranges::find(songs, name) == songs.end()) {
            songs.push_back(name);
            added.insert(name);
            changed = true;
        }
    }
    if (!changed)
        return;
    section_ = target;
    commit();
    if (!added.empty())
        requestSort(target, SortMode::Merge, added);
}

void SongbooksPage::reorderWithin(int s, const std::vector<std::string>& names, int index) {
    if (!editable_ || s < 0 || s >= static_cast<int>(book_.sections.size()))
        return;
    auto& songs = book_.sections[static_cast<std::size_t>(s)].songs;
    const std::set<std::string> moving(names.begin(), names.end());
    std::vector<std::string> moved, rest;
    std::size_t restBefore = 0;
    for (std::size_t i = 0; i < songs.size(); ++i) {
        if (moving.contains(songs[i])) {
            moved.push_back(songs[i]);
        } else {
            rest.push_back(songs[i]);
            if (static_cast<int>(i) < index)
                ++restBefore;
        }
    }
    rest.insert(rest.begin() + static_cast<std::ptrdiff_t>(restBefore), moved.begin(), moved.end());
    if (rest == songs)
        return;
    songs = std::move(rest);
    section_ = s;
    commit();
}

void SongbooksPage::onSongsDropped(const std::vector<std::string>& names, quint64 fromUid,
                                   quint64 toUid, int index) {
    const int to = indexOfUid(toUid);
    if (!editable_ || to < 0)
        return;
    if (fromUid == 0) {
        addSongs(to, names, -1);
    } else if (fromUid == toUid) {
        reorderWithin(to, names, index);
    } else if (const int from = indexOfUid(fromUid); from >= 0) {
        addSongs(to, names, from);
    }
}

void SongbooksPage::sortAll() {
    if (!editable_)
        return;
    for (int s = 0; s < static_cast<int>(book_.sections.size()); ++s)
        requestSort(s, SortMode::Replace);
}

void SongbooksPage::requestSort(int sectionIndex, SortMode mode,
                                const std::set<std::string>& pending) {
    const auto units = sortUnits(sectionIndex);
    const QString key = id_ + QLatin1Char('/') +
                        QString::number(sectionUids_[static_cast<std::size_t>(sectionIndex)]);
    if (units.size() < 2) {
        jobs_.erase(key);
        return;
    }
    const auto existing = jobs_.find(key);
    if (mode == SortMode::Replace) {
        jobs_[key] = {SortMode::Replace, {}};
    } else if (existing == jobs_.end()) {
        jobs_[key] = {SortMode::Merge, pending};
    } else if (existing->second.mode == SortMode::Merge) {
        existing->second.pending.insert(pending.begin(), pending.end());
    } // else: a pending Replace sorts everything anyway

    std::map<std::string, const zub::SongEntry*> byName;
    for (int row = 0; row < model_->rowCount(); ++row)
        if (const zub::SongEntry* song = model_->song(row))
            byName.emplace(song->inputName, song);
    std::vector<zub::songbook::SortEntry> entries;
    for (const auto& name : units) {
        zub::songbook::SortEntry entry;
        const auto it = byName.find(name);
        if (it != byName.end()) {
            entry.title = it->second->meta.title;
            entry.lang = zub::mainLanguage(it->second->meta.value("lang")); // the first of a list
        } else {
            const auto slash = name.rfind('/');
            entry.title = slash == std::string::npos ? name : name.substr(slash + 1);
        }
        entries.push_back(std::move(entry));
    }
    sorter_->sort(key, units, std::move(entries));
}

std::vector<std::string> SongbooksPage::sortUnits(int s) const {
    const auto index = static_cast<std::size_t>(s);
    if (repo_ && index < repoBook_.environments.size())
        return zub::bookfile::leaders(repoBook_.environments[index]);
    return book_.sections[index].songs;
}

std::vector<std::string> SongbooksPage::expandSorted(int s,
                                                     const std::vector<std::string>& order) const {
    const auto index = static_cast<std::size_t>(s);
    if (repo_ && index < repoBook_.environments.size())
        return zub::bookfile::expandLeaders(repoBook_.environments[index], order);
    return order;
}

std::vector<std::string> SongbooksPage::orderFor(const SortJob& job,
                                                 const std::vector<std::string>& current,
                                                 const std::vector<std::string>& sorted) const {
    if (job.mode == SortMode::Replace)
        return sorted;
    std::vector<std::string> base, added;
    for (const auto& name : current)
        (job.pending.contains(name) ? added : base).push_back(name);
    return zub::songbook::mergeIntoOrder(base, sorted, added);
}

void SongbooksPage::resetSectionUids() {
    sectionUids_.clear();
    for (std::size_t i = 0; i < book_.sections.size(); ++i)
        sectionUids_.push_back(nextUid_++);
}

// Key: "<songbook id>/<section uid>".
void SongbooksPage::onSorted(const QString& key, const std::vector<std::string>& names) {
    const qsizetype slash = key.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return;
    const QString id = key.left(slash);
    bool ok = false;
    const quint64 uid = key.mid(slash + 1).toULongLong(&ok);
    if (!ok)
        return;
    sortError_.clear();
    updateSorterWarning();

    SortJob job; // Replace when unknown
    if (const auto it = jobs_.find(key); it != jobs_.end()) {
        job = it->second;
        jobs_.erase(it);
    }

    std::vector<std::string> sortedNames = names;
    std::ranges::sort(sortedNames);
    const auto sameSongs = [&](const std::vector<std::string>& songs) {
        std::vector<std::string> copy = songs;
        std::ranges::sort(copy);
        return copy == sortedNames;
    };
    // The only section of `book` holding exactly the sorted songs, or -1.
    const auto sectionBySongs = [&](const zub::songbook::Songbook& book) {
        int found = -1;
        for (int s = 0; s < static_cast<int>(book.sections.size()); ++s) {
            if (!sameSongs(book.sections[static_cast<std::size_t>(s)].songs))
                continue;
            if (found >= 0)
                return -1;
            found = s;
        }
        return found;
    };

    if (id == id_ && editable_) {
        int index = indexOfUid(uid);
        if (index < 0) {
            // The songbook was loaded again since the request: find the section by its songs.
            int found = -1;
            for (int s = 0; s < static_cast<int>(book_.sections.size()) && found != -2; ++s)
                if (sameSongs(sortUnits(s)))
                    found = found == -1 ? s : -2; // -2: more than one
            index = found;
            if (index < 0)
                return;
        }
        auto& songs = book_.sections[static_cast<std::size_t>(index)].songs;
        const auto units = sortUnits(index);
        if (!sameSongs(units)) {
            // Changed meanwhile: sort again (the pending names still in the section).
            std::set<std::string> pending;
            for (const auto& name : job.pending)
                if (std::ranges::find(units, name) != units.end())
                    pending.insert(name);
            requestSort(index, job.mode, pending);
            return;
        }
        const auto order = expandSorted(index, orderFor(job, units, names));
        if (order == songs)
            return;
        songs = order;
        commit();
        return;
    }
    if (isRepoId(id))
        return; // a repo book no longer shown: sorted when it is sorted again
    // Another songbook was selected meanwhile: update it (and the page's copy) and its file.
    for (auto& entry : entries_) {
        if (entry.id != id || !entry.error.isEmpty())
            continue;
        const int index = sectionBySongs(entry.book);
        if (index < 0)
            return;
        auto& songs = entry.book.sections[static_cast<std::size_t>(index)].songs;
        const auto order = orderFor(job, songs, names);
        if (order == songs)
            return;
        songs = order;
        store_->save(id, entry.book);
        return;
    }
}

void SongbooksPage::showWarning(const QString& text) {
    warning_->setText(text);
    warning_->setVisible(!text.isEmpty());
}

void SongbooksPage::updateSorterWarning() {
    QStringList lines;
    for (const QString& text : {localeWarning_, sortError_})
        if (!text.isEmpty())
            lines << text;
    sorterWarning_->setText(lines.join(QLatin1Char('\n')));
    sorterWarning_->setVisible(!lines.isEmpty());
}
