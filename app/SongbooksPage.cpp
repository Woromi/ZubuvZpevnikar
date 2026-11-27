#include "SongbooksPage.hpp"

#include "BookAdder.hpp"
#include "BookOverview.hpp"
#include "HomeData.hpp"
#include "BookPreview.hpp"
#include "CodeTextEdit.hpp"
#include "CollectionTabs.hpp"
#include "FindBar.hpp"
#include "LabelFilterBox.hpp"
#include "PageHeader.hpp"
#include "SongEditor.hpp"
#include "SongFilterProxy.hpp"
#include "SongPickerModel.hpp"
#include "SongTableModel.hpp"
#include "SongbookExporter.hpp"
#include "SongbookSorter.hpp"
#include "SongbookTree.hpp"
#include "convert.hpp"

#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
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
#include <QFontDatabase>
#include <QFontMetricsF>
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
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTabWidget>
#include <QTimer>
#include <QTableView>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <utility>
#include <zub/core/booktranspose.hpp>
#include <zub/core/latex.hpp>
#include <zub/core/library.hpp>
#include <zub/core/package.hpp>
#include <zub/core/score.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/songbook.hpp>
#include <zub/core/songheader.hpp>
#include <zub/core/songtodo.hpp>
#include <zub/core/textfile.hpp>

namespace {
// The editor's text as bytes: raw (toPlainText would turn U+00A0 into a space), LF.
std::string rawText(const QPlainTextEdit* edit) {
    QString text = edit->document()->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return toStdString(text);
}

const QString splitterKey = QStringLiteral("songbooks/splitter");

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
// ---- Transposing a book ----

using zub::booktranspose::Key;

// A key for people: English names (`B♭`, `F♯m`) or, for a Czech/Slovak song, German (`H`, `B`).
QString displayKey(Key key, bool german) {
    std::string name = zub::booktranspose::bookKeyName(key);
    if (german && name.front() == 'B') {
        if (name.starts_with("Bb"))
            name.erase(1, 1); // B flat is B
        else
            name.front() = 'H';
    }
    QString text = toQString(name);
    text.replace(QLatin1Char('#'), QStringLiteral("♯"));
    text.replace(QLatin1Char('b'), QStringLiteral("♭"));
    return text;
}

// What the book's transposition does to one song.
struct TransposePlan {
    enum class Kind {
        Unknown, // not a song of the library
        Same,    // stays in its key (no book key, or already in it); `note` may say more
        Moves,   // moves by `semitones` (`line`, or the capo)
        Note,    // cannot be transposed: `note` says why
        Fixed    // has its own choice (the tree's Transpose…)
    };
    Kind kind = Kind::Unknown;
    QString songKey; // for people, "" without a readable key
    QString newKey;  // Moves (and Fixed with a key)
    int semitones = 0;
    int capo = 0; // the song's own capo
    bool german = false;
    QString note;
    // Moves by raising the capo (Capo mode, the capo stays in 0..9): a copy of the song, no line.
    bool viaCapo = false;
    // The book copy's line (Moves and Fixed with semitones; also for a viaCapo song whose copy
    // cannot be made).
    std::optional<zub::bookfile::SongTranspose> line;
};

TransposePlan planTranspose(const std::string& name, std::optional<zub::booktranspose::Key> target,
                            zub::booktranspose::Mode mode, const std::map<std::string, int>& fixed,
                            const zub::SongEntry* song) {
    namespace bt = zub::booktranspose;
    TransposePlan plan;
    if (!song)
        return plan;
    plan.kind = TransposePlan::Kind::Same;
    const std::string keyText = song->meta.value("key");
    plan.german = zub::score::germanNames(zub::mainLanguage(song->meta.value("lang")));
    const bool german = plan.german;
    const auto key = bt::parseKey(keyText, german);
    plan.capo = zub::score::songCapo(song->meta.value("cap"));
    if (key)
        plan.songKey = displayKey(*key, german);
    const auto moved = [&](int semitones) {
        return Key{((key->pitch + semitones) % 12 + 12) % 12, key->minor};
    };
    const auto lineFor = [&](int semitones) {
        return zub::bookfile::SongTranspose{name, semitones, false,
                                            key ? bt::keyName(moved(semitones), german)
                                                : std::string(),
                                            false};
    };
    if (const auto it = fixed.find(name); it != fixed.end()) {
        plan.kind = TransposePlan::Kind::Fixed;
        plan.semitones = it->second;
        if (key)
            plan.newKey = displayKey(moved(plan.semitones), german);
        plan.note = plan.newKey.isEmpty() ? plan.songKey : plan.newKey;
        if (plan.semitones != 0)
            plan.line = lineFor(plan.semitones);
        return plan;
    }
    // Without capo the capo goes whether the key changes or not.
    if (mode == bt::Mode::NoCapo && plan.capo > 0)
        plan.note = QObject::tr("capo %1 dropped").arg(plan.capo);
    if (!target)
        return plan;
    if (song->collection == "tinwhistle") {
        plan.kind = TransposePlan::Kind::Note;
        plan.note = QObject::tr("tin whistle: not transposed");
        return plan;
    }
    if (!key) {
        plan.kind = TransposePlan::Kind::Note;
        plan.note = keyText.empty() ? QObject::tr("no key")
                                    : QObject::tr("unreadable key: %1").arg(toQString(keyText));
        return plan;
    }
    plan.semitones = bt::shift(*key, *target);
    if (plan.semitones == 0)
        return plan;
    plan.kind = TransposePlan::Kind::Moves;
    plan.newKey = displayKey(moved(plan.semitones), german);
    plan.line = lineFor(plan.semitones);
    if (mode != bt::Mode::Capo)
        return plan;
    // A capo outside 0..9 cannot be played: the chords are re-shaped instead. The capos of the
    // body count too (read from the file).
    int lowest = plan.capo, highest = plan.capo;
    if (const auto text = zub::readTextFile(song->path))
        for (const int body : zub::songheader::bodyCapos(*text)) {
            lowest = std::min(lowest, body);
            highest = std::max(highest, body);
        }
    plan.viaCapo = lowest + plan.semitones >= 0 && highest + plan.semitones <= 9;
    if (!plan.viaCapo)
        plan.note = QObject::tr("capo would be %1: chords re-shaped")
                        .arg(lowest + plan.semitones < 0 ? lowest + plan.semitones
                                                         : highest + plan.semitones);
    return plan;
}

std::map<std::string, const zub::SongEntry*> songIndex(const SongTableModel& model) {
    std::map<std::string, const zub::SongEntry*> index;
    for (int row = 0; row < model.rowCount(); ++row)
        if (const zub::SongEntry* song = model.song(row))
            index.emplace(song->inputName, song);
    return index;
}

} // namespace

SongbooksPage::SongbooksPage(SongTableModel* model, SongbookSorter* sorter,
                             SongbookExporter* exporter,
                             KSyntaxHighlighting::Repository& repository, QWidget* parent)
    : QWidget(parent), model_(model), sorter_(sorter), exporter_(exporter) {
    buildUi();
    // The LaTeX tab: the whole book, highlighted as LaTeX, in the theme of the song editor.
    auto* highlighter = new KSyntaxHighlighting::SyntaxHighlighter(latex_->document());
    highlighter->setDefinition(repository.definitionForName(QStringLiteral("LaTeX")));
    const auto theme = repository.themeForPalette(palette());
    highlighter->setTheme(theme);
    using Theme = KSyntaxHighlighting::Theme;
    latex_->setLineNumberColors(QColor::fromRgba(theme.editorColor(Theme::IconBorder)),
                                QColor::fromRgba(theme.editorColor(Theme::LineNumbers)),
                                QColor::fromRgba(theme.editorColor(Theme::CurrentLineNumber)));

    // The sorter is shared (Add to Songbooks… sorts with keys "add:N"): only this page's keys.
    connect(sorter_, &SongbookSorter::sorted, this,
            [this](const QString& key, const std::vector<std::string>& names) {
                if (!key.startsWith(QStringLiteral("add:")))
                    onSorted(key, names);
            });
    connect(sorter_, &SongbookSorter::failed, this, [this](const QString& key, const QString& message) {
        if (key.startsWith(QStringLiteral("add:")))
            return;
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

void SongbooksPage::setCollections(const std::vector<std::string>& collections) {
    pickerCollections_->setCollections(collections);
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

    // Like the Songs page: the page's name, then its actions on songbooks.
    auto* bar = createPageHeader(tr("Songbooks"), this);
    QAction* add = newBook_ =
        bar->addAction(QIcon::fromTheme(QStringLiteral("document-new")), tr("New Songbook…"));
    add->setToolTip(tr("A new songbook in the song library's songbooks/ folder"));
    duplicate_ = bar->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Duplicate…"));
    duplicate_->setToolTip(tr("A copy of this songbook under another name, e.g. next year's camp"));
    lock_ = bar->addAction(QIcon::fromTheme(QStringLiteral("object-locked")), tr("Lock"));
    lock_->setToolTip(tr("A released or printed songbook: lock it so that nothing changes it"));
    bar->addSeparator();
    check_ = bar->addAction(QIcon::fromTheme(QStringLiteral("tools-check-spelling")), tr("Check…"));
    check_->setObjectName(QStringLiteral("songbookCheck"));
    check_->setToolTip(tr("Is the songbook ready to print? (the Overview tab)"));
    exportPdf_ =
        bar->addAction(QIcon::fromTheme(QStringLiteral("application-pdf")), tr("Export PDF…"));
    outer->addWidget(bar);
    connect(add, &QAction::triggered, this, &SongbooksPage::newSongbook);
    connect(duplicate_, &QAction::triggered, this, &SongbooksPage::duplicateSongbook);
    connect(lock_, &QAction::triggered, this, &SongbooksPage::toggleLock);
    connect(check_, &QAction::triggered, this, &SongbooksPage::checkBook);
    connect(exportPdf_, &QAction::triggered, this, &SongbooksPage::exportPdf);

    bookTabs_ = new QTabWidget(this);
    bookTabs_->setDocumentMode(true);
    bookTabs_->addTab(buildEditPane(), tr("Songbook"));
    overview_ = new BookOverview(this);
    bookTabs_->addTab(overview_, tr("Overview", "is the songbook ready to print"));
    latexPane_ = buildLatexPane();
    bookTabs_->addTab(latexPane_, tr("LaTeX"));
    connect(bookTabs_, &QTabWidget::currentChanged, this, [this] { refreshOverview(); });
    connect(overview_, &BookOverview::openSongRequested, this, &SongbooksPage::openSongRequested);
    connect(overview_, &BookOverview::refreshRequested, this, &SongbooksPage::refreshOverview);
    connect(overview_, &BookOverview::removeRequested, this,
            [this](const std::vector<std::string>& names) {
                if (!canEdit())
                    return;
                bool removed = false;
                for (const std::string& name : names)
                    removed |= zub::songbook::removeSong(book_, name);
                if (removed)
                    commit();
                refreshOverview();
            });
    overviewTimer_ = new QTimer(this);
    overviewTimer_->setSingleShot(true);
    overviewTimer_->setInterval(300);
    connect(overviewTimer_, &QTimer::timeout, this, &SongbooksPage::refreshOverview);
    connect(model_, &QAbstractItemModel::dataChanged, overviewTimer_, qOverload<>(&QTimer::start));
    connect(model_, &QAbstractItemModel::modelReset, overviewTimer_, qOverload<>(&QTimer::start));
    connect(model_, &QAbstractItemModel::rowsInserted, overviewTimer_, qOverload<>(&QTimer::start));
    connect(model_, &QAbstractItemModel::rowsRemoved, overviewTimer_, qOverload<>(&QTimer::start));
    rightTabs_ = new QTabWidget(this);
    rightTabs_->setDocumentMode(true);
    rightTabs_->addTab(buildPickerPane(), tr("Add Songs"));
    rightTabs_->addTab(buildPreviewPane(), tr("Preview"));
    connect(preview_, &BookPreview::built, this, [this](const QString& id, bool ok) {
        // Only a PDF of the book as it is now (it may have changed during the build).
        const bool exporting = ok && id == pendingExport_ && id == id_ && preview_->upToDate();
        pendingExport_.clear();
        if (exporting)
            exporter_->savePdf(preview_->pdf(), toQString(pathOf(id_).stem()), preview_->report());
    });

    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->addWidget(buildListPane());
    splitter_->addWidget(bookTabs_);
    splitter_->addWidget(rightTabs_);
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
    layout->setContentsMargins(6, 6, 6, 6); // the Songs page's spacing
    list_ = new QTreeWidget(pane);
    list_->setObjectName(QStringLiteral("songbookList"));
    list_->setHeaderHidden(true);
    list_->setColumnCount(1);
    layout->addWidget(list_, 1);
    connect(list_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* current, QTreeWidgetItem* previous) {
                if (current && listItemOf(id_) == current)
                    return; // the same book
                if (latexModified()) {
                    const auto answer = QMessageBox::question(
                        this, tr("Songbooks"),
                        tr("The LaTeX of %1 is not saved. Save it?").arg(id_),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                        QMessageBox::Save);
                    if (answer == QMessageBox::Cancel ||
                        (answer == QMessageBox::Save && (saveLatex(), latexModified()))) {
                        const QSignalBlocker blocker(list_);
                        list_->setCurrentItem(previous);
                        return;
                    }
                    latexBase_.clear(); // discarded: filled again below
                    latex_->clear();
                }
                onListRowChanged();
            });
    return pane;
}

QWidget* SongbooksPage::buildEditPane() {
    editPane_ = new QWidget(this);
    auto* layout = new QVBoxLayout(editPane_);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* pathRow = new QHBoxLayout;
    pathLabel_ = new QLabel(editPane_);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* openFolder = new QPushButton(tr("Open Folder"), editPane_);
    openFolder->setToolTip(tr("Show the songbook's folder"));
    pathRow->addWidget(pathLabel_, 1);
    pathRow->addWidget(openFolder);
    layout->addLayout(pathRow);
    connect(openFolder, &QPushButton::clicked, this, [this] {
        if (!id_.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(pathOf(id_).parent_path())));
    });
    lockBar_ = new QLabel(
        tr("🔒 Released: this songbook is locked, nothing changes it. Unlock it to make changes."),
        editPane_);
    lockBar_->setWordWrap(true);
    lockBar_->setStyleSheet(QStringLiteral("color: #b36b00;"));
    lockBar_->hide();
    layout->addWidget(lockBar_);

    form_ = new QFormLayout;
    title_ = new QLineEdit(editPane_);
    author_ = new QLineEdit(editPane_);
    for (QLineEdit* edit : {title_, author_})
        edit->setToolTip(tr("LaTeX: write \\& for &, \\# for #"));
    class_ = new QComboBox(editPane_);
    class_->addItem(tr("Velký zpěvník (A4)"), QStringLiteral("velkyzpevnik"));
    class_->addItem(tr("Malý zpěvník (A5 book)"), QStringLiteral("malyzpevnik"));
    class_->addItem(tr("Táborový zpěvník (A5 camp booklet)"), QStringLiteral("taborovyzpevnik"));
    chorded_ = new QCheckBox(tr("Chords"), editPane_);
    twoColumns_ = new QCheckBox(tr("Two song columns"), editPane_);
    shading_ = new QCheckBox(tr("Shading"), editPane_);
    chordChart_ = new QCheckBox(tr("Chord chart at the end"), editPane_);
    campYear_ = new QLineEdit(editPane_);
    campYear_->setPlaceholderText(tr("this year"));
    form_->addRow(tr("Title"), title_);
    form_->addRow(tr("Author"), author_);
    form_->addRow(tr("Class"), class_);
    form_->addRow(chorded_);
    form_->addRow(twoColumns_);
    form_->addRow(shading_);
    form_->addRow(chordChart_);
    auto* coverRow = new QHBoxLayout;
    coverName_ = new QLabel(editPane_);
    coverChoose_ = new QPushButton(tr("Choose…"), editPane_);
    coverClear_ = new QPushButton(tr("None"), editPane_);
    coverRow->addWidget(coverName_, 1);
    coverRow->addWidget(coverChoose_);
    coverRow->addWidget(coverClear_);
    form_->addRow(tr("Cover image"), coverRow);
    form_->addRow(tr("Camp year"), campYear_);
    layout->addLayout(form_);

    using Field = zub::bookfile::Field;
    const auto onEdited = [this](QLineEdit* edit, Field field) {
        connect(edit, &QLineEdit::editingFinished, this,
                [this, edit, field] { applyField(field, toStdString(edit->text().trimmed())); });
    };
    onEdited(title_, Field::Title);
    onEdited(author_, Field::Author);
    onEdited(campYear_, Field::CampYear);
    const auto onToggled = [this](QCheckBox* box, Field field) {
        connect(box, &QCheckBox::toggled, this,
                [this, field](bool on) { applyField(field, on ? "1" : "0"); });
    };
    onToggled(chorded_, Field::Chorded);
    onToggled(twoColumns_, Field::TwoColumns);
    onToggled(shading_, Field::Shading);
    onToggled(chordChart_, Field::ChordChart);
    connect(class_, &QComboBox::currentIndexChanged, this,
            [this] { applyField(Field::Class, toStdString(class_->currentData().toString())); });
    connect(coverChoose_, &QPushButton::clicked, this, &SongbooksPage::chooseCover);
    connect(coverClear_, &QPushButton::clicked, this, [this] { applyField(Field::Cover, ""); });

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
    addSection_ = new QPushButton(tr("Add Section"), editPane_);
    renameSection_ = new QPushButton(tr("Rename Section"), editPane_);
    removeSection_ = new QPushButton(tr("Remove Section"), editPane_);
    moveUp_ = new QPushButton(tr("Move Up"), editPane_);
    moveDown_ = new QPushButton(tr("Move Down"), editPane_);
    for (QPushButton* button : {addSection_, renameSection_, removeSection_, moveUp_, moveDown_})
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

    connect(addSection_, &QPushButton::clicked, this, &SongbooksPage::addSection);
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
    connect(tree_, &SongbookTree::transposeSongRequested, this,
            [this](const std::string& name) { chooseSongTranspose(name); });
    connect(tree_, &SongbookTree::songsDropped, this,
            [this](std::vector<std::string> names, quint64 from, quint64 to, int index) {
                onSongsDropped(names, from, to, index);
            });
    connect(tree_, &SongbookTree::sortSectionRequested, this, [this](quint64 uid) {
        const int s = indexOfUid(uid);
        if (canEdit() && s >= 0)
            requestSort(s, SortMode::Replace);
    });
    connect(tree_, &SongbookTree::renameSectionRequested, this,
            [this](quint64 uid) { renameSection(indexOfUid(uid)); });
    connect(tree_, &SongbookTree::removeSectionRequested, this,
            [this](quint64 uid) { removeSection(indexOfUid(uid)); });
    return editPane_;
}

QWidget* SongbooksPage::buildPreviewPane() {
    namespace bt = zub::booktranspose;
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* row = new QHBoxLayout;
    row->setContentsMargins(6, 6, 6, 0);
    bookKey_ = new QComboBox(pane);
    bookKey_->addItem(tr("Keys as written"), QString());
    for (const bool minor : {false, true})
        for (int pitch = 0; pitch < 12; ++pitch) {
            const Key key{pitch, minor};
            bookKey_->addItem(displayKey(key, false), toQString(bt::bookKeyName(key)));
        }
    bookKey_->setToolTip(tr("Transposes the songs to this key for this preview and export only "
                            "(nothing is saved): each song moves the fewest semitones (minor songs "
                            "to the relative minor). Names are English: B is the German H, B♭ is "
                            "the German B."));
    transposeMode_ = new QComboBox(pane);
    transposeMode_->addItem(tr("Re-shape the chords"), int(bt::Mode::Chords));
    transposeMode_->addItem(tr("Move the capo"), int(bt::Mode::Capo));
    transposeMode_->addItem(tr("Without capo"), int(bt::Mode::NoCapo));
    transposeMode_->setToolTip(
        tr("Re-shape the chords: new chords, the capo stays (scores are transposed too).\n"
           "Move the capo: the chords stay, the capo moves (where it stays within 0 to 9).\n"
           "Without capo: no capo anywhere, every chord as it sounds, in the key.\n"
           "Only this preview and export change; the songbook and the songs stay as they are."));
    auto* keyLabel = new QLabel(tr("Transpose to"), pane);
    keyLabel->setBuddy(bookKey_);
    row->addWidget(keyLabel);
    row->addWidget(bookKey_);
    row->addWidget(transposeMode_);
    row->addStretch(1);
    layout->addLayout(row);
    transposeWarning_ = new QLabel(
        tr("The songbook's package can't transpose yet (\\transposesong): the PDF will fail to "
           "build until it does."),
        pane);
    transposeWarning_->setWordWrap(true);
    transposeWarning_->setContentsMargins(6, 4, 6, 0);
    transposeWarning_->setStyleSheet(QStringLiteral("color: #b36b00;"));
    transposeWarning_->hide();
    layout->addWidget(transposeWarning_);
    preview_ = new BookPreview(exporter_, pane);
    preview_->setComposer([this](const std::string& text) { return composeBuild(text); });
    layout->addWidget(preview_, 1);
    connect(bookKey_, &QComboBox::currentIndexChanged, this,
            [this] { onTransposeChoiceChanged(); });
    connect(transposeMode_, &QComboBox::currentIndexChanged, this,
            [this] { onTransposeChoiceChanged(); });
    return pane;
}

QWidget* SongbooksPage::buildLatexPane() {
    auto* pane = new QWidget(this);
    auto* layout = new QVBoxLayout(pane);
    layout->setContentsMargins(6, 6, 6, 6);
    auto* hint = new QLabel(tr("The whole songbook file, for anything the Songbook tab has no "
                               "setting for. Save writes it; until then the other edits wait."),
                            pane);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(hint);
    latex_ = new CodeTextEdit(pane);
    latex_->setObjectName(QStringLiteral("songbookLatex"));
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    latex_->setFont(font);
    latex_->setLineWrapMode(QPlainTextEdit::NoWrap);
    latex_->setTabStopDistance(QFontMetricsF(font).horizontalAdvance(QLatin1Char(' ')) * 4);
    layout->addWidget(latex_, 1);
    latexFind_ = new FindBar(latex_, pane);
    layout->addWidget(latexFind_);
    auto* row = new QHBoxLayout;
    latexState_ = new QLabel(pane);
    latexRevert_ = new QPushButton(tr("Revert"), pane);
    latexSave_ = new QPushButton(tr("Save"), pane);
    row->addWidget(latexState_, 1);
    row->addWidget(latexRevert_);
    row->addWidget(latexSave_);
    layout->addLayout(row);
    connect(latex_, &QPlainTextEdit::textChanged, this, [this] {
        if (!loading_)
            updateLatexState();
    });
    connect(latexSave_, &QPushButton::clicked, this, &SongbooksPage::saveLatex);
    connect(latexRevert_, &QPushButton::clicked, this, &SongbooksPage::revertLatex);
    return pane;
}

QWidget* SongbooksPage::buildPickerPane() {
    pickerPane_ = new QWidget(this);
    auto* layout = new QVBoxLayout(pickerPane_);
    layout->setContentsMargins(6, 6, 6, 6);
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
    recentOnly_ = new QCheckBox(tr("Only recently added"), pickerPane_);
    recentOnly_->setObjectName(QStringLiteral("pickerRecentOnly"));
    recentOnly_->setToolTip(tr("Songs added in the last 30 days or not committed yet (the dates "
                               "come from git)"));
    recentOnly_->setEnabled(model_->hasDates());
    labelFilter_ = new LabelFilterBox(model_, pickerPane_);
    filters->addWidget(favouritesOnly_);
    filters->addWidget(recentOnly_);
    filters->addWidget(labelFilter_);
    layout->addLayout(filters);
    layout->addWidget(onlyInSection_);

    pickerFilter_ = new SongFilterProxy(this);
    pickerFilter_->setSourceModel(model_);
    // The collections (Chords, Tin whistle, …) to pick from.
    pickerCollections_ =
        new CollectionTabs(QStringLiteral("songbooks/pickerCollection"), pickerPane_);
    pickerCollections_->setObjectName(QStringLiteral("pickerCollections"));
    connect(pickerCollections_, &CollectionTabs::collectionChanged, pickerFilter_,
            &SongFilterProxy::setCollection);
    layout->addWidget(pickerCollections_);
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
          SongTableModel::Labels, SongTableModel::Todo, SongTableModel::Changes,
          SongTableModel::Added})
        pickerView_->setColumnHidden(column, true);
    pickerView_->horizontalHeader()->setSectionResizeMode(SongTableModel::Title,
                                                          QHeaderView::Stretch);
    pickerView_->horizontalHeader()->setSectionResizeMode(SongTableModel::Artist,
                                                          QHeaderView::Stretch);
    layout->addWidget(pickerView_, 1);

    connect(search_, &QLineEdit::textChanged, pickerFilter_, &SongFilterProxy::setSearchText);
    connect(language_, &QComboBox::currentIndexChanged, this,
            [this] { pickerFilter_->setLanguage(language_->currentData().toString()); });
    connect(favouritesOnly_, &QCheckBox::toggled, pickerFilter_,
            &SongFilterProxy::setFavouritesOnly);
    connect(recentOnly_, &QCheckBox::toggled, pickerFilter_, &SongFilterProxy::setRecentOnly);
    connect(model_, &SongTableModel::datesChanged, recentOnly_,
            [this] { recentOnly_->setEnabled(model_->hasDates()); });
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
    {
        const QSignalBlocker blocker(list_);
        list_->clear();
        QTreeWidgetItem* selected = nullptr;
        std::map<std::filesystem::path, QTreeWidgetItem*> folders;
        const auto books =
            root_.empty() ? std::vector<std::filesystem::path>() : zub::bookfile::findBooks(root_);
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
            // songbooks/tabory/2025/tabor2025.tex: folders tabory, 2025 at the top level
            QTreeWidgetItem* parent = nullptr;
            std::filesystem::path folder;
            auto dir = relative.parent_path();
            if (aloneInFolder(relative))
                dir = dir.parent_path();
            for (auto part = std::next(dir.begin()); part != dir.end(); ++part) {
                folder /= *part;
                auto& item = folders[folder];
                if (!item) {
                    item = parent ? new QTreeWidgetItem(parent, {toQString(part->filename())})
                                  : new QTreeWidgetItem(list_, {toQString(part->filename())});
                    item->setFlags(Qt::ItemIsEnabled);
                    item->setIcon(0, style()->standardIcon(QStyle::SP_DirIcon));
                }
                parent = item;
            }
            const QString id = toQString(relative);
            auto* item = parent ? new QTreeWidgetItem(parent, {toQString(relative.stem())})
                                : new QTreeWidgetItem(list_, {toQString(relative.stem())});
            item->setData(0, Qt::UserRole, id);
            item->setToolTip(0, id);
            if (const auto text = zub::readTextFile(root_ / relative);
                text && zub::bookfile::readSettings(*text).locked) {
                item->setIcon(0, QIcon::fromTheme(QStringLiteral("object-locked")));
                item->setText(0, item->text(0) + QStringLiteral(" 🔒"));
            }
            if (id == selectId)
                selected = item;
        }
        list_->expandAll();
        list_->setCurrentItem(selected);
    }
    // Unsaved LaTeX of the book shown again stays (the list is filled whenever the page is shown).
    latexKeepFor_ = latexModified() ? id_ : QString();
    id_.clear(); // force loadBook() to read the selected book
    onListRowChanged();
    latexKeepFor_.clear();
}

bool SongbooksPage::buildingPdf() const {
    return preview_ && preview_->building();
}

QString SongbooksPage::bookWithUnsavedLatex() const {
    return latexModified() ? id_ : QString();
}

bool SongbooksPage::showBook(const QString& id) {
    reloadList();
    QTreeWidgetItem* item = listItemOf(id);
    if (!item)
        return false;
    list_->setCurrentItem(item);
    return id_ == id;
}

QTreeWidgetItem* SongbooksPage::listItemOf(const QString& id) const {
    if (id.isEmpty())
        return nullptr;
    for (QTreeWidgetItemIterator it(list_); *it; ++it)
        if ((*it)->data(0, Qt::UserRole).toString() == id)
            return *it;
    return nullptr;
}

std::filesystem::path SongbooksPage::pathOf(const QString& id) const {
    return root_ / toPath(id);
}

void SongbooksPage::onListRowChanged() {
    const QTreeWidgetItem* item = list_->currentItem();
    editable_ = false;
    id_.clear();
    book_ = {};
    text_.clear();
    file_ = {};
    settings_ = {};
    sectionUids_.clear();
    showWarning({});
    const QString id = item ? item->data(0, Qt::UserRole).toString() : QString();
    if (!id.isEmpty()) {
        id_ = id;
        if (loadFile())
            resetSectionUids();
    }
    section_ = 0;
    // The LaTeX tab shows the new book (or keeps the unsaved text of the same one).
    if (id_.isEmpty() || id_ != latexKeepFor_) {
        loading_ = true;
        latexBase_.clear();
        latex_->clear();
        loading_ = false;
    }
    loadBook();
}

bool SongbooksPage::loadFile() {
    const auto path = pathOf(id_);
    auto text = zub::readTextFile(path);
    if (!text) {
        editable_ = false;
        showWarning(tr("Cannot read %1").arg(toQString(path)));
        return false;
    }
    text_ = std::move(*text);
    settings_ = zub::bookfile::readSettings(text_);
    auto parsed = zub::bookfile::parse(text_);
    book_ = {};
    if (!parsed) {
        file_ = {};
        editable_ = false;
        showWarning(tr("Cannot read the songbook %1: %2")
                        .arg(toQString(path.filename()), toQString(parsed.error())));
        return false;
    }
    file_ = std::move(*parsed);
    book_ = songbookOf(file_); // as Home reads it
    editable_ = true;
    for (const auto& environment : file_.environments) {
        if (!environment.readOnly.empty()) {
            editable_ = false;
            showWarning(
                tr("This songbook is shown read-only: %1").arg(toQString(environment.readOnly)));
            break;
        }
    }
    return true;
}

bool SongbooksPage::writeFile(const std::string& text) {
    const auto path = pathOf(id_);
    const auto onDisk = zub::readTextFile(path);
    if (!onDisk || *onDisk != text_) {
        // Changed elsewhere (an editor, git): never overwrite it.
        const QString id = id_;
        fillList(id);
        showWarning(tr("%1 was changed outside the app: it is read again, and the last change "
                       "was not saved. Make it once more.")
                        .arg(toQString(path.filename())));
        return false;
    }
    if (text != text_) {
        if (const auto written = zub::writeTextFileAtomic(path, text); !written) {
            showWarning(tr("Not saved: %1").arg(toQString(written.error())));
            return false;
        }
    }
    // What the file now says (a keep-order block keeps its order whatever was asked).
    const auto keepUids = sectionUids_;
    if (!loadFile())
        return false;
    if (keepUids.size() == book_.sections.size())
        sectionUids_ = keepUids;
    else
        resetSectionUids();
    return true;
}

bool SongbooksPage::applyEdit(const std::expected<std::string, std::string>& edited) {
    if (!edited) {
        showWarning(tr("Not saved: %1").arg(toQString(edited.error())));
        return false;
    }
    showWarning({});
    if (!writeFile(*edited))
        return false;
    loadBook();
    return true;
}

bool SongbooksPage::canEdit() const {
    return !id_.isEmpty() && editable_ && !settings_.locked && !latexModified();
}

bool SongbooksPage::saveSongs() {
    std::string text = text_;
    for (std::size_t e = 0; e < file_.environments.size() && e < book_.sections.size(); ++e) {
        const auto& songs = book_.sections[e].songs;
        if (songs == songsOf(file_.environments[e]))
            continue;
        auto edited = zub::bookfile::setSongs(text, e, songs);
        if (!edited) {
            showWarning(tr("Not saved: %1").arg(toQString(edited.error())));
            loadFile();
            return false;
        }
        text = std::move(*edited);
    }
    return writeFile(text);
}

void SongbooksPage::toggleLock() {
    if (id_.isEmpty() || text_.empty() || latexModified() || (!editable_ && !settings_.locked))
        return;
    if (settings_.locked &&
        QMessageBox::question(
            this, tr("Unlock"),
            tr("Unlock %1? It was locked as released or printed; changes make "
               "it differ from that edition.")
                .arg(toQString(book_.title.empty() ? toStdString(id_) : book_.title))) !=
            QMessageBox::Yes)
        return;
    if (applyEdit(zub::bookfile::setLocked(text_, !settings_.locked))) {
        const QString id = id_;
        fillList(id); // the lock in the list
    }
}

void SongbooksPage::exportPdf() {
    if (id_.isEmpty() || text_.empty())
        return;
    if (preview_->upToDate()) {
        exporter_->savePdf(preview_->pdf(), toQString(pathOf(id_).stem()), preview_->report());
        return;
    }
    rightTabs_->setCurrentWidget(preview_->parentWidget());
    if (preview_->building()) {
        showWarning(tr("A preview is being built: export when it is done."));
        return;
    }
    if (preview_->build())
        pendingExport_ = id_;
}

// ---- New and duplicate ----

namespace {

// Asks where a new book goes: a folder under songbooks/ (existing or new, may have subfolders)
// and a file name. Empty on cancel.
struct Place {
    QString folder; // relative to songbooks/, '/'-separated
    QString stem;
};

bool validStem(const QString& stem) {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+$"));
    return valid.match(stem).hasMatch();
}

bool validFolder(const QString& folder) {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]+(/[A-Za-z0-9_-]+)*$"));
    return valid.match(folder).hasMatch();
}

} // namespace

void SongbooksPage::newSongbook() {
    if (root_.empty() || latexModified())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New Songbook"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* folder = new QComboBox(&dialog);
    folder->setEditable(true);
    {
        std::set<QString> folders;
        for (const auto& relative : zub::bookfile::findBooks(root_)) {
            auto dir = relative.parent_path();
            for (; std::distance(dir.begin(), dir.end()) > 1; dir = dir.parent_path())
                folders.insert(toQString(dir.lexically_relative(*relative.begin())));
        }
        for (const QString& name : folders)
            folder->addItem(name);
    }
    folder->setCurrentText(QString());
    folder->lineEdit()->setPlaceholderText(tr("e.g. tabory/2027 or interpreti/kabat"));
    auto* stem = new QLineEdit(&dialog);
    stem->setPlaceholderText(tr("letters, digits, - and _"));
    auto* bookClass = new QComboBox(&dialog);
    bookClass->addItem(tr("Velký zpěvník (A4)"), QStringLiteral("velkyzpevnik"));
    bookClass->addItem(tr("Malý zpěvník (A5 book)"), QStringLiteral("malyzpevnik"));
    bookClass->addItem(tr("Táborový zpěvník (A5 camp booklet)"), QStringLiteral("taborovyzpevnik"));
    auto* title = new QLineEdit(&dialog);
    auto* author = new QLineEdit(&dialog);
    auto* campYear = new QLineEdit(&dialog);
    campYear->setPlaceholderText(tr("this year"));
    auto* chorded = new QCheckBox(tr("Chords"), &dialog);
    chorded->setChecked(true);
    auto* twoColumns = new QCheckBox(tr("Two song columns"), &dialog);
    auto* shading = new QCheckBox(tr("Shading"), &dialog);
    auto* chordChart = new QCheckBox(tr("Chord chart at the end"), &dialog);
    chordChart->setChecked(true);
    auto* coverRow = new QHBoxLayout;
    auto* cover = new QLineEdit(&dialog);
    cover->setPlaceholderText(tr("none"));
    auto* coverChoose = new QPushButton(tr("Choose…"), &dialog);
    coverRow->addWidget(cover, 1);
    coverRow->addWidget(coverChoose);
    form->addRow(tr("Folder in songbooks/"), folder);
    form->addRow(tr("File name"), stem);
    form->addRow(tr("Class"), bookClass);
    form->addRow(tr("Title"), title);
    form->addRow(tr("Author"), author);
    form->addRow(tr("Camp year"), campYear);
    form->addRow(chorded);
    form->addRow(twoColumns);
    form->addRow(shading);
    form->addRow(chordChart);
    form->addRow(tr("Cover image"), coverRow);
    layout->addLayout(form);
    auto* note = new QLabel(tr("Writes <file>.tex and a build.sh into the folder. To build it in "
                               "the song library's CI as well, add it to .github/workflows."),
                            &dialog);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: gray;"));
    layout->addWidget(note);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    const auto updateRows = [=] {
        const QString name = bookClass->currentData().toString();
        form->setRowVisible(campYear, name == QStringLiteral("taborovyzpevnik"));
        form->setRowVisible(chordChart, name != QStringLiteral("taborovyzpevnik"));
        // The folder becomes the file name by default (velkyzpevnik/velkyzpevnik.tex).
    };
    updateRows();
    connect(bookClass, &QComboBox::currentIndexChanged, &dialog, updateRows);
    connect(folder, &QComboBox::currentTextChanged, &dialog, [stem](const QString& text) {
        if (!stem->isModified())
            stem->setText(text.section(QLatin1Char('/'), -1).remove(QLatin1Char('/')));
    });
    connect(coverChoose, &QPushButton::clicked, &dialog, [&dialog, cover, this] {
        const QString file = QFileDialog::getOpenFileName(
            &dialog, tr("Cover image"), toQString(root_), tr("Images (*.png *.jpg *.jpeg *.pdf)"));
        if (!file.isEmpty())
            cover->setText(file);
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const QString folderName = folder->currentText().trimmed();
        const QString stemName = stem->text().trimmed();
        if (!validFolder(folderName) || !validStem(stemName)) {
            QMessageBox::information(&dialog, tr("New Songbook"),
                                     tr("The folder and the file name may only use letters, "
                                        "digits, - and _ (folders separated by /)."));
            return;
        }
        if (QFileInfo::exists(
                toQString(root_ / zub::bookfile::booksFolder(root_) / toPath(folderName) /
                          toPath(stemName + QStringLiteral(".tex"))))) {
            QMessageBox::information(
                &dialog, tr("New Songbook"),
                tr("There is a songbook of this name in that folder already."));
            return;
        }
        dialog.accept();
    });
    dialog.resize(520, dialog.sizeHint().height());
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString folderName = folder->currentText().trimmed();
    const QString stemName = stem->text().trimmed();
    const std::filesystem::path dir =
        root_ / zub::bookfile::booksFolder(root_) / toPath(folderName);
    zub::bookfile::Settings settings;
    settings.bookClass = toStdString(bookClass->currentData().toString());
    settings.title = zub::songbook::escapeText(toStdString(title->text().trimmed()));
    settings.author = zub::songbook::escapeText(toStdString(author->text().trimmed()));
    settings.chorded = chorded->isChecked();
    settings.twoColumns = twoColumns->isChecked();
    settings.shading = shading->isChecked();
    settings.chordChart = chordChart->isChecked() && settings.bookClass != "taborovyzpevnik";
    if (settings.bookClass == "taborovyzpevnik")
        settings.campYear = toStdString(campYear->text().trimmed());
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) {
        QMessageBox::warning(this, tr("New Songbook"),
                             tr("Cannot create %1: %2")
                                 .arg(toQString(dir), QString::fromStdString(error.message())));
        return;
    }
    if (const QString chosen = cover->text().trimmed(); !chosen.isEmpty()) {
        const auto coverPath = coverReference(toPath(chosen), dir, toStdString(stemName));
        if (!coverPath) {
            QMessageBox::warning(this, tr("New Songbook"),
                                 tr("Cannot copy the cover image into %1.").arg(toQString(dir)));
            return;
        }
        settings.cover = *coverPath;
    }
    const auto made =
        zub::bookfile::newBook(settings, toStdString(stemName), zub::collections(root_));
    if (!made) {
        QMessageBox::warning(this, tr("New Songbook"), toQString(made.error()));
        return;
    }
    if (!writeNewBook(dir, stemName, made->tex, made->buildSh))
        return;
    // A camp booklet's back cover inputs akordy.tex (the chord chart) from the book's folder:
    // the newest one of the other camps, else an empty one to fill in.
    if (settings.bookClass == "taborovyzpevnik" && !std::filesystem::exists(dir / "akordy.tex")) {
        std::filesystem::path newest;
        for (const auto& book : zub::bookfile::findBooks(root_))
            if (const auto chart = root_ / book.parent_path() / "akordy.tex";
                chart.parent_path() != dir && std::filesystem::is_regular_file(chart))
                newest = std::max(newest, chart);
        std::error_code copyError;
        if (!newest.empty())
            std::filesystem::copy_file(newest, dir / "akordy.tex", copyError);
        if (newest.empty() || copyError)
            (void)zub::writeTextFileAtomic(
                dir / "akordy.tex", "% The chord chart of the booklet's back cover: \\gtab{...} "
                                    "lines in a center environment, as in the other camps.\n");
    }
    fillList(toQString((zub::bookfile::booksFolder(root_) / toPath(folderName) /
                        toPath(stemName + QStringLiteral(".tex")))));
}

std::optional<std::string> SongbooksPage::coverReference(const std::filesystem::path& file,
                                                         const std::filesystem::path& bookDir,
                                                         const std::string& stem) {
    // Inside the book's folder: its own name there; inside the repo: from the repo root (both on
    // TEXINPUTS); anywhere else: copied into the book's folder as cover.<ext>.
    const auto absolute = std::filesystem::weakly_canonical(file);
    const auto inside = [&](const std::filesystem::path& dir) -> std::optional<std::string> {
        const auto relative = absolute.lexically_relative(std::filesystem::weakly_canonical(dir));
        if (relative.empty() || *relative.begin() == "..")
            return std::nullopt;
        return toUtf8(relative);
    };
    if (const auto own = inside(bookDir))
        return own;
    if (const auto repo = inside(root_))
        return repo;
    // <stem>-cover.<ext>, never replacing a file there (another book's cover, cover.pdf).
    std::string name = stem + "-cover" + toUtf8(absolute.extension());
    for (int n = 2; std::filesystem::exists(bookDir / pathFromUtf8(name)); ++n)
        name = stem + "-cover-" + std::to_string(n) + toUtf8(absolute.extension());
    std::error_code error;
    std::filesystem::copy_file(absolute, bookDir / pathFromUtf8(name),
                               std::filesystem::copy_options::none, error);
    if (error)
        return std::nullopt;
    return name;
}

bool SongbooksPage::writeNewBook(const std::filesystem::path& dir, const QString& stem,
                                 const std::string& tex, const std::string& buildSh) {
    const auto texPath = dir / toPath(stem + QStringLiteral(".tex"));
    const auto shPath = dir / "build.sh";
    if (const auto written = zub::writeTextFileAtomic(texPath, tex); !written) {
        QMessageBox::warning(this, tr("Songbooks"), toQString(written.error()));
        return false;
    }
    // A folder may hold several books (malyzpevnik/): its build.sh is kept.
    if (!std::filesystem::exists(shPath)) {
        if (const auto written = zub::writeTextFileAtomic(shPath, buildSh); !written) {
            QMessageBox::warning(this, tr("Songbooks"), toQString(written.error()));
            return false;
        }
        std::error_code error;
        std::filesystem::permissions(shPath,
                                     std::filesystem::perms::owner_exec |
                                         std::filesystem::perms::group_exec |
                                         std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add, error);
    }
    return true;
}

void SongbooksPage::duplicateSongbook() {
    if (id_.isEmpty() || text_.empty() || latexModified())
        return;
    bool ok = false;
    // The folder under songbooks/ (a book right in it has its own, named like it).
    QString current = id_.section(QLatin1Char('/'), 1, -2);
    if (current.isEmpty())
        current = toQString(pathOf(id_).stem()) + QStringLiteral("-copy");
    const QString target =
        QInputDialog::getText(
            this, tr("Duplicate"),
            tr("The copy's folder and file name under songbooks/ (e.g. tabory/2027/tabor2027):"),
            QLineEdit::Normal,
            current + QLatin1Char('/') + toQString(pathOf(id_).stem()) + QStringLiteral("-copy"),
            &ok)
            .trimmed();
    if (!ok || target.isEmpty())
        return;
    const QString folderName = target.section(QLatin1Char('/'), 0, -2);
    const QString stemName = target.section(QLatin1Char('/'), -1);
    if (!validFolder(folderName) || !validStem(stemName)) {
        QMessageBox::information(this, tr("Duplicate"),
                                 tr("The folder and the file name may only use letters, digits, "
                                    "- and _ (folders separated by /)."));
        return;
    }
    const std::filesystem::path dir =
        root_ / zub::bookfile::booksFolder(root_) / toPath(folderName);
    if (std::filesystem::exists(dir / toPath(stemName + QStringLiteral(".tex")))) {
        QMessageBox::information(this, tr("Duplicate"),
                                 tr("There is a songbook of this name in that folder already."));
        return;
    }
    // The copy is a new edition: not locked. A cover and a camp's akordy.tex beside the original
    // are copied along.
    const std::string copy = zub::bookfile::setLocked(text_, false);
    const auto parsed = zub::bookfile::parse(copy);
    const auto script = zub::bookfile::buildScriptFor(
        toStdString(stemName),
        zub::songbook::bookClassFromName(zub::bookfile::readSettings(copy).bookClass)
            .value_or(BookClass::Velky),
        parsed ? zub::bookfile::indexFiles(*parsed) : std::vector<std::string>{"obsah"},
        zub::collections(root_));
    if (!script) { // before the folder is made: a refusal leaves nothing behind
        QMessageBox::warning(this, tr("Duplicate"), toQString(script.error()));
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) {
        QMessageBox::warning(this, tr("Duplicate"),
                             tr("Cannot create %1: %2")
                                 .arg(toQString(dir), QString::fromStdString(error.message())));
        return;
    }
    const auto sourceDir = pathOf(id_).parent_path();
    // Only files inside the book's folder are copied into the library (which may be pushed):
    // the cover's name comes from the book, so a plain relative path (no "..", nothing absolute)
    // with no link on the way out of the folder.
    const auto insideSource = [&sourceDir](const std::filesystem::path& file) {
        std::error_code ec;
        const auto base = std::filesystem::canonical(sourceDir, ec);
        if (ec)
            return false;
        const auto real = std::filesystem::canonical(file, ec);
        if (ec || std::filesystem::is_symlink(file, ec))
            return false;
        const auto relative = real.lexically_relative(base);
        return !relative.empty() && *relative.begin() != "..";
    };
    const auto cover = pathFromUtf8(settings_.cover);
    if (!settings_.cover.empty() && zub::safety::isPlainFilePath(settings_.cover) &&
        std::filesystem::is_regular_file(sourceDir / cover) && insideSource(sourceDir / cover) &&
        !std::filesystem::exists(dir / cover)) {
        std::filesystem::create_directories((dir / cover).parent_path(), error);
        std::filesystem::copy_file(sourceDir / cover, dir / cover, error);
        if (error)
            showWarning(
                tr("The cover %1 was not copied: %2")
                    .arg(toQString(settings_.cover), QString::fromStdString(error.message())));
    }
    if (std::filesystem::is_regular_file(sourceDir / "akordy.tex") &&
        insideSource(sourceDir / "akordy.tex") && !std::filesystem::exists(dir / "akordy.tex"))
        std::filesystem::copy_file(sourceDir / "akordy.tex", dir / "akordy.tex", error);
    if (!writeNewBook(dir, stemName, copy, *script))
        return;
    fillList(toQString((zub::bookfile::booksFolder(root_) / toPath(folderName) /
                        toPath(stemName + QStringLiteral(".tex")))));
}

// ---- The selected songbook ----

void SongbooksPage::loadBook() {
    loading_ = true;
    const bool shown = !id_.isEmpty() && !text_.empty();
    using Field = zub::bookfile::Field;
    const auto editableField = [&](Field field) {
        return canEdit() && !settings_.notEditable.contains(field);
    };
    pathLabel_->setText(shown ? id_ : QString());
    title_->setText(toQString(settings_.title));
    author_->setText(toQString(settings_.author));
    campYear_->setText(toQString(settings_.campYear));
    const int classIndex = class_->findData(toQString(settings_.bookClass));
    class_->setCurrentIndex(classIndex);
    chorded_->setChecked(settings_.chorded);
    twoColumns_->setChecked(settings_.twoColumns);
    shading_->setChecked(settings_.shading);
    chordChart_->setChecked(settings_.chordChart);
    const std::pair<QWidget*, Field> fields[] = {
        {title_, Field::Title},           {author_, Field::Author},
        {campYear_, Field::CampYear},     {chorded_, Field::Chorded},
        {twoColumns_, Field::TwoColumns}, {shading_, Field::Shading},
        {chordChart_, Field::ChordChart}, {coverChoose_, Field::Cover},
        {coverClear_, Field::Cover}};
    for (const auto& [widget, field] : fields) {
        widget->setEnabled(editableField(field));
        widget->setToolTip(settings_.notEditable.contains(field)
                               ? tr("Written in a form the form cannot change: edit it in the "
                                    "LaTeX tab")
                               : QString());
    }
    // The class is chosen when a songbook is made: the classes take different title pages and
    // covers, so a switch here would leave a book that does not build.
    class_->setEnabled(false);
    class_->setToolTip(tr("Chosen when the songbook is made (New Songbook…); to change it, edit "
                          "the LaTeX tab, including the cover and title page commands"));
    lockBar_->setVisible(shown && settings_.locked);
    loading_ = false;
    refreshFileViews();
    updateCoverLabel();
    updateFormVisibility();
    showTransposeChoice();
    updateLatexState();
    rebuildTree();
    updatePicker();
    updateButtons();
    refreshOverview();
}

void SongbooksPage::setPackageDir(const std::filesystem::path& packageDir) {
    packageTransposes_ = !packageDir.empty() && zub::package::definesTransposesong(packageDir);
    updateTransposeWarning();
}

void SongbooksPage::updateTransposeWarning() {
    const auto choice = transposeChoice();
    const bool lines = choice.key || !choice.fixed.empty();
    transposeWarning_->setVisible(!id_.isEmpty() && lines && !packageTransposes_);
}

SongbooksPage::TransposeChoice SongbooksPage::transposeChoice() const {
    const auto it = transposeChoices_.find(id_);
    if (it == transposeChoices_.end())
        return {};
    // A song's own choice counts only while the song is in the book (it may have been removed).
    TransposeChoice choice = it->second;
    const auto names = allSongNames();
    std::erase_if(choice.fixed, [&names](const auto& entry) {
        return std::ranges::find(names, entry.first) == names.end();
    });
    return choice;
}

std::vector<std::string> SongbooksPage::allSongNames() const {
    std::vector<std::string> names;
    for (const auto& section : book_.sections)
        for (const std::string& name : section.songs)
            if (std::ranges::find(names, name) == names.end())
                names.push_back(name);
    return names;
}

void SongbooksPage::showTransposeChoice() {
    namespace bt = zub::booktranspose;
    const auto choice = transposeChoice();
    {
        const QSignalBlocker keyBlocker(bookKey_);
        const QSignalBlocker modeBlocker(transposeMode_);
        const int keyIndex =
            choice.key ? bookKey_->findData(toQString(bt::bookKeyName(*choice.key))) : 0;
        bookKey_->setCurrentIndex(std::max(keyIndex, 0));
        transposeMode_->setCurrentIndex(
            std::max(transposeMode_->findData(int(choice.mode)), 0));
    }
    const bool shown = !id_.isEmpty() && !text_.empty();
    bookKey_->setEnabled(shown);
    transposeMode_->setEnabled(shown);

    // What the preview builds, for people, and the signature that tells builds apart.
    QString variant;
    if (choice.key) {
        const QString key = displayKey(*choice.key, false);
        variant = choice.mode == bt::Mode::Capo     ? tr("transposed to %1 by the capo").arg(key)
                  : choice.mode == bt::Mode::NoCapo ? tr("transposed to %1 without capo").arg(key)
                                                    : tr("transposed to %1").arg(key);
    } else if (choice.mode == bt::Mode::NoCapo) {
        variant = tr("without capo");
    }
    if (!choice.fixed.empty())
        variant += (variant.isEmpty() ? QString() : QStringLiteral(", ")) +
                   tr("%n song(s) with their own transposition", nullptr,
                      static_cast<int>(choice.fixed.size()));
    std::string signature;
    if (choice.active()) {
        // Capo mode without a key changes nothing: the mode counts only where it does.
        const bool modeMatters = choice.key || choice.mode == bt::Mode::NoCapo;
        signature = (choice.key ? bt::bookKeyName(*choice.key) : std::string("-")) + "/" +
                    (modeMatters ? std::to_string(int(choice.mode)) : std::string("-"));
        for (const auto& [name, semitones] : choice.fixed)
            signature += "/" + name + "=" + std::to_string(semitones);
    }
    preview_->setVariant(variant, signature);
    updateTransposeWarning();
}

void SongbooksPage::onTransposeChoiceChanged() {
    if (loading_ || id_.isEmpty())
        return;
    TransposeChoice& choice = transposeChoices_[id_];
    choice.key = zub::booktranspose::bookKey(toStdString(bookKey_->currentData().toString()));
    choice.mode = static_cast<zub::booktranspose::Mode>(transposeMode_->currentData().toInt());
    if (choice.isDefault())
        transposeChoices_.erase(id_);
    showTransposeChoice();
    rebuildTree();
}

std::optional<BookPreview::BuildInput> SongbooksPage::composeBuild(const std::string& text) {
    namespace bt = zub::booktranspose;
    BookPreview::BuildInput input{text, {}};
    const auto choice = transposeChoice();
    if (!choice.active())
        return input;
    QStringList problems;
    if (choice.mode == bt::Mode::NoCapo) {
        if (auto edited = zub::bookfile::addClassOption(input.text, "transposecapos"))
            input.text = std::move(*edited);
        else
            problems << toQString(edited.error());
    }
    const auto songs = songIndex(*model_);
    for (const std::string& name : allSongNames()) {
        const auto it = songs.find(name);
        const auto plan = planTranspose(name, choice.key, choice.mode, choice.fixed,
                                        it == songs.end() ? nullptr : it->second);
        using Kind = TransposePlan::Kind;
        // Without a key the book's own lines stay, except where a song has its own choice.
        if (plan.kind == Kind::Unknown || (!choice.key && plan.kind != Kind::Fixed))
            continue;
        std::optional<zub::bookfile::SongTranspose> line = plan.line;
        if (plan.kind == Kind::Moves && plan.viaCapo) {
            const auto songText = zub::readTextFile(it->second->path);
            if (songText)
                if (auto raised = bt::raiseCapo(*songText, plan.semitones, plan.german)) {
                    input.songs[name] = std::move(*raised);
                    line.reset();
                }
        }
        auto edited = zub::bookfile::setTranspose(input.text, name, line);
        if (edited)
            input.text = std::move(*edited);
        else
            problems << toQString(name) + QStringLiteral(": ") + toQString(edited.error());
    }
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, tr("Transpose"),
                             tr("The songbook can't be transposed:\n\n%1")
                                 .arg(problems.join(QLatin1Char('\n'))));
        return std::nullopt;
    }
    return input;
}

void SongbooksPage::chooseSongTranspose(const std::string& name) {
    const auto choice = transposeChoice();
    const auto current = choice.fixed.find(name);

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Transpose %1").arg(toQString(name)));
    auto* layout = new QVBoxLayout(&dialog);
    auto* follow = new QRadioButton(tr("Follow the songbook"), &dialog);
    auto* shift = new QRadioButton(tr("Transpose by"), &dialog);
    auto* none = new QRadioButton(tr("Don't transpose"), &dialog);
    auto* spin = new QSpinBox(&dialog);
    spin->setRange(-11, 11);
    spin->setSuffix(tr(" semitones"));
    spin->setSpecialValueText(QString());
    if (current != choice.fixed.end() && current->second != 0) {
        shift->setChecked(true);
        spin->setValue(current->second);
    } else if (current != choice.fixed.end()) {
        none->setChecked(true);
    } else {
        follow->setChecked(true);
    }
    auto* shiftRow = new QHBoxLayout;
    shiftRow->addWidget(shift);
    shiftRow->addWidget(spin);
    layout->addWidget(follow);
    layout->addLayout(shiftRow);
    layout->addWidget(none);
    auto* hint = new QLabel(tr("For this songbook's preview and export only; nothing is saved."),
                            &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    connect(spin, &QSpinBox::valueChanged, shift, [shift] { shift->setChecked(true); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (follow->isChecked())
        setSongTranspose(name, std::nullopt);
    else
        setSongTranspose(name, none->isChecked() ? 0 : spin->value());
}

void SongbooksPage::setSongTranspose(const std::string& name, std::optional<int> semitones) {
    if (id_.isEmpty())
        return;
    TransposeChoice& choice = transposeChoices_[id_];
    if (semitones)
        choice.fixed[name] = *semitones;
    else
        choice.fixed.erase(name);
    if (choice.isDefault())
        transposeChoices_.erase(id_);
    showTransposeChoice();
    rebuildTree();
}

void SongbooksPage::updateFormVisibility() {
    const bool tabor = settings_.bookClass == "taborovyzpevnik";
    form_->setRowVisible(chordChart_, !tabor);
    form_->setRowVisible(campYear_, tabor);
}

void SongbooksPage::updateCoverLabel() {
    if (settings_.cover.empty()) {
        coverName_->setText(tr("None"));
        coverName_->setStyleSheet(QStringLiteral("color: gray;"));
        coverName_->setToolTip(QString());
        return;
    }
    // Found as the build finds it: beside the book or from the repo root, with or without an
    // extension of its own.
    bool found = false;
    for (const auto& dir : {pathOf(id_).parent_path(), root_})
        for (const char* extension : {"", ".png", ".jpg", ".jpeg", ".pdf"})
            found = found || std::filesystem::is_regular_file(dir / (settings_.cover + extension));
    coverName_->setText(toQString(settings_.cover));
    coverName_->setStyleSheet(found ? QString() : QStringLiteral("color: red;"));
    coverName_->setToolTip(found ? QString() : tr("Not found beside the songbook or in the song library"));
}

void SongbooksPage::chooseCover() {
    if (loading_ || !canEdit())
        return;
    const auto dir = pathOf(id_).parent_path();
    const QString file = QFileDialog::getOpenFileName(this, tr("Cover image"), toQString(dir),
                                                      tr("Images (*.png *.jpg *.jpeg *.pdf)"));
    if (file.isEmpty())
        return;
    const auto reference =
        coverReference(toPath(file), dir, toStdString(toQString(pathOf(id_).stem())));
    if (!reference) {
        showWarning(tr("Cannot copy the cover image into %1.").arg(toQString(dir)));
        return;
    }
    applyField(zub::bookfile::Field::Cover, *reference);
}

void SongbooksPage::applyField(zub::bookfile::Field field, const std::string& value) {
    if (loading_ || !canEdit())
        return;
    applyEdit(zub::bookfile::setSetting(text_, field, value));
}

bool SongbooksPage::saveBook() {
    if (!canEdit())
        return false;
    return saveSongs();
}

// ---- The LaTeX tab ----

bool SongbooksPage::confirmClose() {
    if (!latexModified())
        return true;
    const auto answer = QMessageBox::question(
        this, tr("Songbooks"), tr("The LaTeX of %1 is not saved. Save it?").arg(id_),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save) {
        saveLatex();
        return !latexModified();
    }
    return true;
}

void SongbooksPage::refreshFileViews() {
    // The LaTeX tab follows the file unless it holds unsaved edits.
    if (!latexModified()) {
        QString text = QString::fromStdString(text_);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        if (toStdString(text) != latexBase_ || rawText(latex_) != toStdString(text)) {
            const bool wasLoading = std::exchange(loading_, true);
            latexBase_ = toStdString(text);
            // Keep the place in the text: a song added in the Songbook tab moves little.
            const int position = latex_->textCursor().position();
            const int scroll = latex_->verticalScrollBar()->value();
            latex_->setPlainText(text);
            QTextCursor cursor = latex_->textCursor();
            cursor.setPosition(std::min(position, static_cast<int>(text.size())));
            latex_->setTextCursor(cursor);
            latex_->verticalScrollBar()->setValue(scroll);
            loading_ = wasLoading;
        }
    }
    const bool shown = !id_.isEmpty() && !text_.empty();
    preview_->setBook(id_, shown ? pathOf(id_) : std::filesystem::path(), text_);
}

bool SongbooksPage::showFind(bool replaceMode) {
    if (bookTabs_->currentWidget() != latexPane_ || !latexFind_)
        return false;
    latexFind_->open(replaceMode);
    return true;
}

bool SongbooksPage::latexModified() const {
    return latex_ && !id_.isEmpty() && !text_.empty() && rawText(latex_) != latexBase_;
}

void SongbooksPage::updateLatexState() {
    const bool modified = latexModified();
    const bool shown = !id_.isEmpty() && !text_.empty();
    // A file the editor would not give back byte for byte (mixed line ends, invalid UTF-8)
    // is only shown.
    latex_->setReadOnly(!shown || settings_.locked || !SongEditor::roundTrips(text_));
    latexSave_->setEnabled(modified);
    latexRevert_->setEnabled(modified);
    latexState_->setText(modified ? tr("Not saved: the Songbook tab and Add Songs wait until it "
                                       "is saved or reverted.")
                                  : QString());
    latexState_->setStyleSheet(QStringLiteral("color: #b36b00;"));
    bookTabs_->setTabText(bookTabs_->indexOf(latexPane_), modified ? tr("LaTeX ●") : tr("LaTeX"));
    // One writer at a time: the other edits wait for the LaTeX.
    editPane_->setEnabled(shown && !modified);
    pickerPane_->setEnabled(canEdit());
    updateButtons();
}

void SongbooksPage::saveLatex() {
    if (!latexModified() || settings_.locked)
        return;
    std::string text = rawText(latex_);
    if (text_.find("\r\n") != std::string::npos) { // the file's line ends
        std::string crlf;
        for (const char c : text) {
            if (c == '\n')
                crlf += '\r';
            crlf += c;
        }
        text = std::move(crlf);
    }
    // Changed on disk meanwhile: keep what was typed (Revert shows the file's version).
    if (const auto onDisk = zub::readTextFile(pathOf(id_)); !onDisk || *onDisk != text_) {
        QMessageBox::warning(this, tr("LaTeX"),
                             tr("%1 was changed outside the app since it was read. Your text is "
                                "kept here; Revert shows the file as it is now.")
                                 .arg(toQString(pathOf(id_).filename())));
        // Read it again (all of it: songs, settings, lock), keeping the typed text: Revert
        // shows the file as it is now, and a Save works from it.
        if (onDisk) {
            loadFile();
            resetSectionUids();
            updateLatexState();
        }
        return;
    }
    const std::string base = latexBase_;
    latexBase_ = rawText(latex_); // what is being saved
    // Written but not readable as a songbook (a LaTeX mistake) is still saved: the tab says so.
    if (!writeFile(text) && text_ != text) {
        latexBase_ = base; // still not saved
        updateLatexState();
        return;
    }
    resetSectionUids();
    latexBase_ = rawText(latex_); // saved: the file says the same now
    loadBook();
    if (!editable_ || text_.empty())
        bookTabs_->setCurrentWidget(latexPane_); // keep showing what needs fixing
}

void SongbooksPage::revertLatex() {
    loadFile(); // the file as it is now
    loading_ = true;
    QString text = QString::fromStdString(text_);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    latexBase_ = toStdString(text);
    latex_->setPlainText(text);
    loading_ = false;
    loadBook();
}

// ---- Sections ----

int SongbooksPage::currentSection() const {
    return std::clamp(section_, 0, std::max(0, static_cast<int>(book_.sections.size()) - 1));
}

int SongbooksPage::indexOfUid(quint64 uid) const {
    const auto it = std::ranges::find(sectionUids_, uid);
    return it == sectionUids_.end() ? -1 : static_cast<int>(it - sectionUids_.begin());
}

bool SongbooksPage::commit() {
    const bool saved = saveBook();
    refreshFileViews(); // the song lists changed the file
    rebuildTree();
    updatePicker();
    return saved;
}

void SongbooksPage::rebuildTree() {
    std::map<std::string, int> rowOfName;
    for (int row = 0; row < model_->rowCount(); ++row)
        if (const zub::SongEntry* song = model_->song(row))
            rowOfName.emplace(song->inputName, row);
    // Songs show their key only while the preview transposes (or a song has its own choice).
    const auto choice = transposeChoice();
    const auto songs = songIndex(*model_);

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
                    sectionLabel(section.name) + QStringLiteral(" — ") +
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
                    QString text = title + QStringLiteral(" — ") + artist;
                    bool dim = false;
                    const auto plan = planTranspose(name, choice.key, choice.mode, choice.fixed,
                                                    songs.at(name));
                    using Kind = TransposePlan::Kind;
                    const auto withNote = [&plan] {
                        return plan.note.isEmpty()
                                   ? QString()
                                   : QStringLiteral(" (") + plan.note + QStringLiteral(")");
                    };
                    if (plan.kind == Kind::Fixed) {
                        text += QStringLiteral("  ") + tr("kept: %1").arg(plan.note);
                    } else if (plan.kind == Kind::Moves) {
                        text += QStringLiteral("  ") + plan.songKey + QStringLiteral(" → ") +
                                plan.newKey;
                        if (plan.viaCapo)
                            text += QStringLiteral(" ") +
                                    tr("(capo %1)").arg(
                                        (plan.semitones > 0 ? QStringLiteral("+") : QString()) +
                                        QString::number(plan.semitones));
                        else
                            text += withNote();
                    } else if (choice.key && plan.kind == Kind::Same) {
                        text += QStringLiteral("  ") + plan.songKey + withNote();
                    } else if (plan.kind == Kind::Same && !plan.note.isEmpty()) {
                        text += QStringLiteral(" ") + withNote();
                    } else if (choice.key && plan.kind == Kind::Note) {
                        text += QStringLiteral("  (") +
                                (plan.songKey.isEmpty() ? QString()
                                                        : plan.songKey + QStringLiteral(", ")) +
                                plan.note + QStringLiteral(")");
                        dim = true;
                    }
                    auto* item = tree_->addSong(header, sectionUids_[s], name, text, false);
                    if (dim)
                        item->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
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
    if (id_.isEmpty() || book_.sections.empty()) {
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
    const bool shown = !id_.isEmpty() && !text_.empty();
    const bool can = canEdit();
    duplicate_->setEnabled(shown && !latexModified());
    newBook_->setEnabled(!root_.empty() && !latexModified());
    lock_->setEnabled(shown && (editable_ || settings_.locked) && !latexModified());
    lock_->setText(settings_.locked ? tr("Unlock") : tr("Lock"));
    bool hasSongs = false;
    for (const auto& section : book_.sections)
        hasSongs = hasSongs || !section.songs.empty();
    // A locked or read-only book is still checked and built as it is.
    check_->setEnabled(shown && hasSongs);
    exportPdf_->setEnabled(shown && !book_.sections.empty());
    sortAll_->setEnabled(can && hasSongs);

    const int section = currentSection();
    const int count = static_cast<int>(book_.sections.size());
    const bool named =
        section < count && !book_.sections[static_cast<std::size_t>(section)].name.empty();
    addSection_->setEnabled(can);
    renameSection_->setEnabled(can && named);
    removeSection_->setEnabled(can && count > 1);
    removeSong_->setEnabled(can && !tree_->selectedSongs().empty());
    moveUp_->setEnabled(can && section > 0);
    moveDown_->setEnabled(can && section + 1 < count);
    addPicked_->setEnabled(can && count > 0);
    removePicked_->setEnabled(can && count > 0);
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
    for (int s = 0; s < static_cast<int>(book_.sections.size()); ++s) {
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
    if (!canEdit() || !askSectionName(tr("Add Section"), {}, -1, &name))
        return;
    const int after = book_.sections.empty() ? -1 : currentSection();
    const auto uids = sectionUids_;
    if (!applyEdit(zub::bookfile::addSection(text_, toStdString(name),
                                             after < 0 ? std::string::npos
                                                       : static_cast<std::size_t>(after))))
        return;
    // The new one goes after the current section: the others keep their ids.
    if (uids.size() + 1 == book_.sections.size()) {
        sectionUids_ = uids;
        sectionUids_.insert(sectionUids_.begin() + (after + 1), nextUid_++);
    }
    section_ = after + 1;
    rebuildTree();
    updatePicker();
}

void SongbooksPage::renameSection(int s) {
    if (!canEdit() || s < 0 || s >= static_cast<int>(book_.sections.size()))
        return;
    const auto& section = book_.sections[static_cast<std::size_t>(s)];
    if (section.name.empty())
        return; // the part outside sections has no \songsection to rename
    QString name;
    if (!askSectionName(tr("Rename Section"), toQString(section.name), s, &name))
        return;
    section_ = s;
    applyEdit(zub::bookfile::renameSection(text_, static_cast<std::size_t>(s), toStdString(name)));
}

void SongbooksPage::removeSection(int s) {
    if (!canEdit() || s < 0 || s >= static_cast<int>(book_.sections.size()) ||
        book_.sections.size() < 2)
        return;
    const auto& section = book_.sections[static_cast<std::size_t>(s)];
    if (QMessageBox::question(this, tr("Remove Section"),
                              tr("Remove the section %1 and its %n song(s) from the songbook?",
                                 nullptr, static_cast<int>(section.songs.size()))
                                  .arg(sectionLabel(section.name))) != QMessageBox::Yes)
        return;
    jobs_.erase(id_ + QLatin1Char('/') +
                QString::number(sectionUids_[static_cast<std::size_t>(s)]));
    auto uids = sectionUids_;
    uids.erase(uids.begin() + s);
    if (!applyEdit(zub::bookfile::removeSection(text_, static_cast<std::size_t>(s))))
        return;
    if (uids.size() == book_.sections.size())
        sectionUids_ = uids;
    section_ = std::min(s, static_cast<int>(book_.sections.size()) - 1);
    rebuildTree();
    updatePicker();
}

void SongbooksPage::moveSection(int delta) {
    const int from = currentSection();
    const int to = from + delta;
    if (!canEdit() || to < 0 || to >= static_cast<int>(book_.sections.size()))
        return;
    auto uids = sectionUids_;
    std::swap(uids[static_cast<std::size_t>(from)], uids[static_cast<std::size_t>(to)]);
    if (!applyEdit(zub::bookfile::moveSection(text_, static_cast<std::size_t>(from), delta)))
        return;
    if (uids.size() == book_.sections.size())
        sectionUids_ = uids;
    section_ = to;
    rebuildTree();
    updatePicker();
}

void SongbooksPage::removeSelectedSongs() {
    if (!canEdit())
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
    showOverview();
}

void SongbooksPage::showOverview() {
    bookTabs_->setCurrentWidget(overview_);
    refreshOverview();
}

void SongbooksPage::refreshOverview() {
    if (!overview_ || bookTabs_->currentWidget() != overview_)
        return; // computed when shown (it reads the songs' files)
    overview_->setRemoveEnabled(canEdit());
    if (id_.isEmpty() || text_.empty()) {
        overview_->setChecks(id_, {}, false, {});
        return;
    }
    const auto facts = songFacts(*model_, root_);
    const auto checked = checkSongbook(root_, pathOf(id_), text_, facts);
    if (!checked) {
        overview_->setChecks(id_, tr("The songbook cannot be read: see the LaTeX tab."), false, {});
        return;
    }
    if (book_.sections.empty() ||
        std::ranges::all_of(book_.sections, [](const auto& s) { return s.songs.empty(); })) {
        overview_->setChecks(id_, tr("No songs in this songbook yet."), false, {});
        return;
    }
    const auto& r = *checked;
    const auto rows = songRows(*model_);
    using Severity = BookOverview::Severity;
    const auto songsOf = [&](const std::vector<std::string>& names,
                             const std::function<QString(const zub::songbook::SongFacts&)>& detail) {
        std::vector<BookOverview::Song> out;
        for (const std::string& name : names) {
            BookOverview::Song song{name, {}, toQString(name)};
            if (const auto it = rows.find(name); it != rows.end()) {
                song.path = model_->song(it->second)->path;
                song.text = model_->index(it->second, SongTableModel::Title).data().toString() +
                            QStringLiteral(" — ") +
                            model_->index(it->second, SongTableModel::Artist).data().toString();
            }
            if (detail) {
                const QString more = detail(facts(name));
                if (!more.isEmpty())
                    song.text += QStringLiteral("   ·  ") + more;
            }
            out.push_back(std::move(song));
        }
        return out;
    };
    const QString paper = toQString(zub::songbook::paperName(book_.bookClass)).toUpper();
    std::vector<BookOverview::Check> checks;
    {
        std::vector<BookOverview::Song> files;
        for (const std::string& problem : r.files)
            files.push_back({{}, {}, toQString(problem)});
        checks.push_back(
            {Severity::Blocker, tr("The build would ask about the songbook's files"), files});
    }
    checks.push_back({Severity::Blocker, tr("Not in the song library"),
                      songsOf(r.missing, [](const auto&) { return QString(); })});
    checks.push_back({Severity::Blocker, tr("The build would ask about these"),
                      songsOf(r.unsafe, [](const zub::songbook::SongFacts& f) {
                          QStringList what;
                          for (const std::string& risk : f.risks)
                              what << toQString(risk);
                          return what.join(QStringLiteral(", "));
                      })});
    checks.push_back({Severity::ToFix, tr("Not marked as fitting %1").arg(paper),
                      songsOf(r.notFitting, [this](const zub::songbook::SongFacts& f) {
                          return tr("pap: %1").arg(f.pap.empty() ? tr("none") : toQString(f.pap));
                      })});
    checks.push_back({Severity::ToFix, tr("Imported, not checked yet"), songsOf(r.notChecked, {})});
    checks.push_back({Severity::Note, tr("TODO notes"),
                      songsOf(r.todo, [](const zub::songbook::SongFacts& f) {
                          return f.todos.empty() ? QString() : toQString(f.todos.front());
                      })});
    checks.push_back({Severity::Note, tr("Possible changes (Scan Songs)"),
                      songsOf(r.changes, [](const zub::songbook::SongFacts& f) {
                          return toQString(f.changes);
                      })});
    checks.push_back({Severity::Note, tr("No key", "musical key"), songsOf(r.noKey, {})});

    QString heading =
        r.ready() ? tr("Ready to print")
                  : tr("%n to fix before printing", nullptr, static_cast<int>(r.toFix()));
    if (r.notes() > 0)
        heading += QStringLiteral("  ·  ") +
                   tr("%n note(s)", nullptr, static_cast<int>(r.notes()));
    overview_->setChecks(id_, heading, r.ready(), checks);
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
    if (!canEdit() || book_.sections.empty() || index.column() != SongTableModel::Title)
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
    if (!canEdit() || book_.sections.empty())
        return;
    addSongs(currentSection(), pickerSelection(), -1);
}

void SongbooksPage::removePicked() {
    if (!canEdit() || book_.sections.empty())
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
    if (!canEdit() || target < 0 || target >= static_cast<int>(book_.sections.size()))
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
    // Not saved (a repo book changed on disk is read again): nothing to sort.
    if (commit() && !added.empty())
        requestSort(target, SortMode::Merge, added);
}

void SongbooksPage::reorderWithin(int s, const std::vector<std::string>& names, int index) {
    if (!canEdit() || s < 0 || s >= static_cast<int>(book_.sections.size()))
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
    if (!canEdit() || to < 0)
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
    if (!canEdit())
        return;
    for (int s = 0; s < static_cast<int>(book_.sections.size()); ++s)
        requestSort(s, SortMode::Replace);
}

void SongbooksPage::requestSort(int sectionIndex, SortMode mode,
                                const std::set<std::string>& pending) {
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(book_.sections.size()) ||
        sectionIndex >= static_cast<int>(sectionUids_.size()))
        return;
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

    auto entries = sortEntriesFor(*model_, units);
    sorter_->sort(key, units, std::move(entries));
}

std::vector<std::string> SongbooksPage::sortUnits(int s) const {
    const auto index = static_cast<std::size_t>(s);
    if (index < file_.environments.size())
        return zub::bookfile::leaders(file_.environments[index]);
    return book_.sections[index].songs;
}

std::vector<std::string> SongbooksPage::expandSorted(int s,
                                                     const std::vector<std::string>& order) const {
    const auto index = static_cast<std::size_t>(s);
    if (index < file_.environments.size())
        return zub::bookfile::expandLeaders(file_.environments[index], order);
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

    if (id == id_ && canEdit()) {
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
    // Another songbook was selected meanwhile: it is sorted when it is sorted again.
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
