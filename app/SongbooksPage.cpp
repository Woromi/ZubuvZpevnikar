#include "SongbooksPage.hpp"

#include "SongFilterProxy.hpp"
#include "SongPickerModel.hpp"
#include "SongTableModel.hpp"
#include "SongbookExporter.hpp"
#include "SongbookSorter.hpp"
#include "SongbookTree.hpp"
#include "convert.hpp"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCursor>
#include <QBrush>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QComboBox>
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
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>

namespace {
const QString splitterKey = QStringLiteral("songbooks/splitter");

using zub::songbook::BookClass;

QString sectionLabel(const std::string& name) {
    return name.empty() ? QObject::tr("Songbook (no section)") : toQString(name);
}

std::set<std::string> namesOf(const zub::songbook::Section& section) {
    return {section.songs.begin(), section.songs.end()};
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
    root_ = root;
}

void SongbooksPage::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* bar = new QToolBar(this);
    bar->setMovable(false);
    QAction* back = bar->addAction(QIcon::fromTheme(QStringLiteral("go-previous")),
                                  tr("Back", "go back to the song list"));
    back->setToolTip(tr("Back to the song list"));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    connect(back, &QAction::triggered, this, &SongbooksPage::backRequested);
    auto* heading = new QLabel(tr("Songbooks"), bar);
    QFont font = heading->font();
    font.setBold(true);
    heading->setFont(font);
    heading->setContentsMargins(12, 0, 0, 0);
    bar->addWidget(heading);
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
    list_ = new QListWidget(pane);
    layout->addWidget(list_, 1);
    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("New"), pane);
    duplicate_ = new QPushButton(tr("Duplicate"), pane);
    delete_ = new QPushButton(tr("Delete"), pane);
    buttons->addWidget(add);
    buttons->addWidget(duplicate_);
    buttons->addWidget(delete_);
    layout->addLayout(buttons);
    connect(list_, &QListWidget::currentRowChanged, this, &SongbooksPage::onListRowChanged);
    connect(add, &QPushButton::clicked, this, &SongbooksPage::newSongbook);
    connect(duplicate_, &QPushButton::clicked, this, &SongbooksPage::duplicateSongbook);
    connect(delete_, &QPushButton::clicked, this, &SongbooksPage::deleteSongbook);
    return pane;
}

QWidget* SongbooksPage::buildEditPane() {
    editPane_ = new QWidget(this);
    auto* layout = new QVBoxLayout(editPane_);
    form_ = new QFormLayout;
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
    layout->addLayout(form_);

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

    auto* buttons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add Section"), editPane_);
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
    connect(renameSection_, &QPushButton::clicked, this, [this] { renameSection(currentSection()); });
    connect(removeSection_, &QPushButton::clicked, this, [this] { removeSection(currentSection()); });
    connect(moveUp_, &QPushButton::clicked, this, [this] { moveSection(-1); });
    connect(moveDown_, &QPushButton::clicked, this, [this] { moveSection(1); });
    connect(removeSong_, &QPushButton::clicked, this, &SongbooksPage::removeSelectedSongs);
    connect(sortAll_, &QPushButton::clicked, this, &SongbooksPage::sortAll);
    connect(tree_, &QTreeWidget::currentItemChanged, this, &SongbooksPage::onTreeSelectionChanged);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &SongbooksPage::updateButtons);
    connect(tree_, &SongbookTree::removeSelectedRequested, this, &SongbooksPage::removeSelectedSongs);
    connect(tree_, &SongbookTree::songsDropped, this,
            [this](std::vector<std::string> names, quint64 from, quint64 to, int index) {
                onSongsDropped(names, from, to, index);
            });
    connect(tree_, &SongbookTree::sortSectionRequested, this, [this](quint64 uid) {
        const int s = indexOfUid(uid);
        if (editable_ && s >= 0)
            requestSort(s, SortMode::Replace);
    });
    connect(tree_, &SongbookTree::renameSectionRequested, this, [this](quint64 uid) {
        renameSection(indexOfUid(uid));
    });
    connect(tree_, &SongbookTree::removeSectionRequested, this, [this](quint64 uid) {
        removeSection(indexOfUid(uid));
    });
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
    for (const int column : {SongTableModel::Tempo, SongTableModel::Meter, SongTableModel::Capo,
                             SongTableModel::Todo, SongTableModel::Changes})
        pickerView_->setColumnHidden(column, true);
    pickerView_->horizontalHeader()->setSectionResizeMode(SongTableModel::Title,
                                                          QHeaderView::Stretch);
    pickerView_->horizontalHeader()->setSectionResizeMode(SongTableModel::Artist,
                                                          QHeaderView::Stretch);
    layout->addWidget(pickerView_, 1);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch(1);
    exportPdf_ = new QPushButton(tr("Export PDF…"), pickerPane_);
    exportLatex_ = new QPushButton(tr("Export LaTeX…"), pickerPane_);
    buttons->addWidget(exportPdf_);
    buttons->addWidget(exportLatex_);
    layout->addLayout(buttons);

    connect(search_, &QLineEdit::textChanged, pickerFilter_, &SongFilterProxy::setSearchText);
    connect(language_, &QComboBox::currentIndexChanged, this,
            [this] { pickerFilter_->setLanguage(language_->currentData().toString()); });
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
        auto* enter = new QShortcut(QKeySequence(key), pickerView_, nullptr, nullptr,
                                    Qt::WidgetShortcut);
        connect(enter, &QShortcut::activated, this, &SongbooksPage::addPicked);
    }
    connect(exportPdf_, &QPushButton::clicked, this, [this] { exporter_->exportPdf(id_, book_, titleLookup()); });
    connect(exportLatex_, &QPushButton::clicked, this,
            [this] { exporter_->exportLatex(id_, book_, titleLookup()); });
    refreshLanguages();
    return pickerPane_;
}

void SongbooksPage::refreshLanguages() {
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
        for (const auto& entry : entries_) {
            auto* item = new QListWidgetItem;
            item->setData(Qt::UserRole, entry.id);
            if (entry.error.isEmpty()) {
                item->setText(toQString(entry.book.title));
            } else {
                item->setText(entry.id);
                item->setForeground(Qt::red);
                item->setToolTip(entry.error);
            }
            list_->addItem(item);
        }
        int row = -1;
        for (std::size_t i = 0; i < entries_.size(); ++i)
            if (entries_[i].id == selectId)
                row = static_cast<int>(i);
        list_->setCurrentRow(row);
    }
    id_.clear(); // force loadBook() to read the selected entry
    onListRowChanged();
}

void SongbooksPage::onListRowChanged() {
    const int row = list_->currentRow();
    editable_ = false;
    id_.clear();
    book_ = {};
    sectionUids_.clear();
    if (row >= 0 && static_cast<std::size_t>(row) < entries_.size()) {
        const auto& entry = entries_[static_cast<std::size_t>(row)];
        id_ = entry.id;
        if (entry.error.isEmpty()) {
            book_ = entry.book;
            zub::songbook::normalize(book_);
            resetSectionUids();
            editable_ = true;
            showWarning({});
        } else {
            showWarning(tr("Cannot read this songbook (the file is left alone): %1").arg(entry.error));
        }
    } else {
        showWarning({});
    }
    section_ = 0;
    loadBook();
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
    if (!editable_)
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
    if (id_.isEmpty())
        return;
    const int row = list_->currentRow();
    const QString name = list_->item(row)->text();
    if (QMessageBox::question(this, tr("Delete songbook"),
                              tr("Delete the songbook %1? This cannot be undone.").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes)
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
    editPane_->setEnabled(editable_);
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
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Cover image"), dir, tr("Images (*.png *.jpg *.jpeg *.pdf)"));
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
    QString error;
    if (!store_->save(id_, book_, &error)) {
        showWarning(error);
        return false;
    }
    for (auto& entry : entries_) {
        if (entry.id == id_) {
            entry.book = book_;
            const int row = static_cast<int>(&entry - entries_.data());
            list_->item(row)->setText(toQString(book_.title));
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
        tree_->setRootUid(sectionUids_.empty() ? 0 : sectionUids_.front());
        QTreeWidgetItem* selectedItem = nullptr;
        for (std::size_t s = 0; s < book_.sections.size() && s < sectionUids_.size(); ++s) {
            const auto& section = book_.sections[s];
            QTreeWidgetItem* header = nullptr;
            if (s > 0) {
                header = tree_->addSection(sectionUids_[s],
                                           toQString(section.name) + QStringLiteral(" — ") +
                                               tr("%n song(s)", nullptr,
                                                  static_cast<int>(section.songs.size())));
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
    const int named = std::max(0, static_cast<int>(book_.sections.size()) - 1);
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
    duplicate_->setEnabled(editable_);
    delete_->setEnabled(selected);
    bool hasSongs = false;
    for (const auto& section : book_.sections)
        hasSongs = hasSongs || !section.songs.empty();
    exportPdf_->setEnabled(editable_ && hasSongs);
    exportLatex_->setEnabled(editable_ && hasSongs);
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
    const QString text = QInputDialog::getText(this, title, tr("Section name:"), QLineEdit::Normal,
                                               initial, &ok)
                             .trimmed();
    if (!ok)
        return false;
    if (text.isEmpty()) {
        QMessageBox::information(this, title, tr("The section needs a name."));
        return false;
    }
    for (int s = 1; s < static_cast<int>(book_.sections.size()); ++s) {
        if (s != ignoreSection && toQString(book_.sections[static_cast<std::size_t>(s)].name) == text) {
            QMessageBox::information(this, title,
                                     tr("There is a section with this name already."));
            return false;
        }
    }
    *name = text;
    return true;
}

void SongbooksPage::addSection() {
    QString name;
    if (!editable_ || !askSectionName(tr("Add Section"), {}, -1, &name))
        return;
    book_.sections.push_back({toStdString(name), {}});
    sectionUids_.push_back(nextUid_++);
    section_ = static_cast<int>(book_.sections.size()) - 1;
    commit();
}

void SongbooksPage::renameSection(int s) {
    if (!editable_ || s < 1 || s >= static_cast<int>(book_.sections.size()))
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
    if (!editable_ || s < 1 || s >= static_cast<int>(book_.sections.size()))
        return;
    const auto& section = book_.sections[static_cast<std::size_t>(s)];
    if (!section.songs.empty() &&
        QMessageBox::question(this, tr("Remove Section"),
                              tr("Remove the section %1 and its %n song(s) from the songbook?", nullptr,
                                 static_cast<int>(section.songs.size()))
                                  .arg(toQString(section.name))) != QMessageBox::Yes)
        return;
    jobs_.erase(id_ + QLatin1Char('/') + QString::number(sectionUids_[static_cast<std::size_t>(s)]));
    book_.sections.erase(book_.sections.begin() + s);
    sectionUids_.erase(sectionUids_.begin() + s);
    section_ = std::min(s, static_cast<int>(book_.sections.size()) - 1);
    commit();
}

void SongbooksPage::moveSection(int delta) {
    const int from = currentSection();
    const int to = from + delta;
    if (!editable_ || from < 1 || to < 1 || to >= static_cast<int>(book_.sections.size()))
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
    const QRect box =
        pickerView_->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option,
                                             pickerView_);
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
    const auto removed = std::ranges::remove_if(songs, [&](const std::string& s) {
        return remove.contains(s);
    });
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

void SongbooksPage::requestSort(int sectionIndex, SortMode mode, const std::set<std::string>& pending) {
    const auto& section = book_.sections[static_cast<std::size_t>(sectionIndex)];
    const QString key = id_ + QLatin1Char('/') +
                        QString::number(sectionUids_[static_cast<std::size_t>(sectionIndex)]);
    if (section.songs.size() < 2) {
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
    for (const auto& name : section.songs) {
        zub::songbook::SortEntry entry;
        const auto it = byName.find(name);
        if (it != byName.end()) {
            entry.title = it->second->meta.title;
            entry.lang = it->second->meta.value("lang");
        } else {
            const auto slash = name.rfind('/');
            entry.title = slash == std::string::npos ? name : name.substr(slash + 1);
        }
        entries.push_back(std::move(entry));
    }
    sorter_->sort(key, section.songs, std::move(entries));
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
            index = sectionBySongs(book_);
            if (index < 0)
                return;
        }
        auto& songs = book_.sections[static_cast<std::size_t>(index)].songs;
        if (!sameSongs(songs)) {
            // Changed meanwhile: sort again (the pending names still in the section).
            std::set<std::string> pending;
            for (const auto& name : job.pending)
                if (std::ranges::find(songs, name) != songs.end())
                    pending.insert(name);
            requestSort(index, job.mode, pending);
            return;
        }
        const auto order = orderFor(job, songs, names);
        if (order == songs)
            return;
        songs = order;
        commit();
        return;
    }
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
