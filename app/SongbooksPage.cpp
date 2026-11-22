#include "SongbooksPage.hpp"

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
#include <QTableView>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <zub/core/booktranspose.hpp>
#include <zub/core/latex.hpp>
#include <zub/core/library.hpp>
#include <zub/core/package.hpp>
#include <zub/core/score.hpp>
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

// What the book's key does to one song.
struct TransposePlan {
    enum class Kind {
        Unknown, // not a song of the library
        Same,    // stays as it is (no book key, or already in it)
        Moves,   // gets `line`
        Note,    // cannot be transposed: `note` says why
        Fixed    // has its own `% fixed` line
    };
    Kind kind = Kind::Unknown;
    QString songKey; // for people, "" without a readable key
    QString newKey;  // Moves (and Fixed with a key)
    int semitones = 0;
    int capo = 0; // the song's own capo
    QString note;
    // Moves by raising the capo (Capo mode, the capo stays in 0..9): the song files change, the
    // book gets no line.
    bool viaCapo = false;
    std::optional<zub::bookfile::SongTranspose> line; // Moves by re-shaping the chords
};

TransposePlan
planTranspose(const std::string& name, const zub::bookfile::TransposeSettings& settings,
              const std::map<std::string, zub::bookfile::SongTranspose>& fixed,
              const zub::SongEntry* song) {
    namespace bt = zub::booktranspose;
    TransposePlan plan;
    if (!song)
        return plan;
    plan.kind = TransposePlan::Kind::Same;
    const std::string keyText = song->meta.value("key");
    const bool german = zub::score::germanNames(zub::mainLanguage(song->meta.value("lang")));
    const auto key = bt::parseKey(keyText, german);
    plan.capo = zub::score::songCapo(song->meta.value("cap"));
    if (key)
        plan.songKey = displayKey(*key, german);
    if (const auto it = fixed.find(name); it != fixed.end()) {
        plan.kind = TransposePlan::Kind::Fixed;
        plan.semitones = it->second.semitones;
        if (key)
            plan.newKey = displayKey(
                Key{((key->pitch + plan.semitones) % 12 + 12) % 12, key->minor}, german);
        plan.note = plan.newKey.isEmpty() ? plan.songKey : plan.newKey;
        return plan;
    }
    if (!settings.key)
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
    const bool capo = settings.mode == zub::bookfile::TransposeSettings::Mode::Capo;
    plan.semitones = bt::shift(*key, *settings.key);
    if (plan.semitones == 0)
        return plan;
    // A capo outside 0..9 cannot be played: the chords are re-shaped instead (a book line).
    // The capos of the body count too (read from the file: only in Capo mode, only for a song
    // that moves).
    int lowest = plan.capo, highest = plan.capo;
    if (capo) {
        if (const auto text = zub::readTextFile(song->path))
            for (const int body : zub::songheader::bodyCapos(*text)) {
                lowest = std::min(lowest, body);
                highest = std::max(highest, body);
            }
    }
    const bool capoFits = lowest + plan.semitones >= 0 && highest + plan.semitones <= 9;
    const Key moved{((key->pitch + plan.semitones) % 12 + 12) % 12, key->minor};
    plan.kind = TransposePlan::Kind::Moves;
    plan.newKey = displayKey(moved, german);
    if (capo && !capoFits)
        plan.note = QObject::tr("capo would be %1: chords re-shaped")
                        .arg(lowest + plan.semitones < 0 ? lowest + plan.semitones
                                                         : highest + plan.semitones);
    plan.viaCapo = capo && capoFits;
    if (!plan.viaCapo)
        plan.line = zub::bookfile::SongTranspose{name, plan.semitones, false,
                                                 bt::keyName(moved, german), false};
    return plan;
}

std::map<std::string, const zub::SongEntry*> songIndex(const SongTableModel& model) {
    std::map<std::string, const zub::SongEntry*> index;
    for (int row = 0; row < model.rowCount(); ++row)
        if (const zub::SongEntry* song = model.song(row))
            index.emplace(song->inputName, song);
    return index;
}

std::map<std::string, zub::bookfile::SongTranspose> fixedLines(std::string_view text) {
    std::map<std::string, zub::bookfile::SongTranspose> fixed;
    for (auto& line : zub::bookfile::readTransposes(text))
        if (line.fixed)
            fixed.emplace(line.inputName, line);
    return fixed;
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
    add->setToolTip(tr("A new songbook in the songbook folder (songbooks/)"));
    duplicate_ = bar->addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Duplicate…"));
    duplicate_->setToolTip(tr("A copy of this songbook under another name, e.g. next year's camp"));
    lock_ = bar->addAction(QIcon::fromTheme(QStringLiteral("object-locked")), tr("Lock"));
    lock_->setToolTip(tr("A released or printed songbook: lock it so that nothing changes it"));
    bar->addSeparator();
    check_ = bar->addAction(QIcon::fromTheme(QStringLiteral("tools-check-spelling")), tr("Check…"));
    check_->setObjectName(QStringLiteral("songbookCheck"));
    check_->setToolTip(tr("Songs of this songbook not marked as fitting its paper (their pap)"));
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
    bookTabs_->addTab(buildLatexPane(), tr("LaTeX"));
    rightTabs_ = new QTabWidget(this);
    rightTabs_->setDocumentMode(true);
    rightTabs_->addTab(buildPickerPane(), tr("Add Songs"));
    preview_ = new BookPreview(exporter_, this);
    rightTabs_->addTab(preview_, tr("Preview"));
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
    bookKey_ = new QComboBox(editPane_);
    bookKey_->addItem(tr("None"), QString());
    for (const bool minor : {false, true})
        for (int pitch = 0; pitch < 12; ++pitch) {
            const Key key{pitch, minor};
            bookKey_->addItem(displayKey(key, false),
                              toQString(zub::booktranspose::bookKeyName(key)));
        }
    bookKey_->setToolTip(tr("Transposes the songs to this key: each song moves the fewest "
                            "semitones (minor songs to the relative minor). Names are English: "
                            "B is the German H, B♭ is the German B."));
    transposeMode_ = new QComboBox(editPane_);
    transposeMode_->addItem(tr("Chords"), QStringLiteral("chords"));
    transposeMode_->addItem(tr("Capo"), QStringLiteral("capo"));
    transposeMode_->setToolTip(
        tr("Chords re-shapes the chords in this book (and the package transposes scores where it "
           "can). Capo changes the song files themselves (their capo, key and capo notes; the "
           "chords stay), which affects every songbook using them. It is relative to each "
           "song's current key, and None does not undo it."));
    form_->addRow(tr("Key"), bookKey_);
    form_->addRow(tr("Transpose by"), transposeMode_);
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
    connect(bookKey_, &QComboBox::currentIndexChanged, this,
            [this] { onTransposeFormChanged(); });
    connect(transposeMode_, &QComboBox::currentIndexChanged, this,
            [this] { onTransposeFormChanged(); });
    connect(coverChoose_, &QPushButton::clicked, this, &SongbooksPage::chooseCover);
    connect(coverClear_, &QPushButton::clicked, this, [this] { applyField(Field::Cover, ""); });

    transposeWarning_ = new QLabel(
        tr("The songbook's package can't transpose yet (\\transposesong): the PDF will fail to "
           "build until it does."),
        editPane_);
    transposeWarning_->setWordWrap(true);
    transposeWarning_->setStyleSheet(QStringLiteral("color: #b36b00;"));
    transposeWarning_->hide();
    layout->addWidget(transposeWarning_);
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
            const QString id = toQString(relative.generic_string());
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
    book_.title = zub::latex::toDisplayText(file_.title);
    book_.author = zub::latex::toDisplayText(file_.author);
    book_.bookClass =
        zub::songbook::bookClassFromName(file_.documentClass).value_or(BookClass::Velky);
    for (const auto& environment : file_.environments)
        book_.sections.push_back({environment.section, songsOf(environment)});
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
    // A book with a key: the songs that have no line yet (added ones) get theirs in this write.
    if (zub::bookfile::readTransposeSettings(text).key) {
        std::set<std::string> withLine;
        for (const auto& line : zub::bookfile::readTransposes(text))
            withLine.insert(line.inputName);
        std::vector<std::string> fresh;
        for (const auto& section : book_.sections)
            for (const std::string& name : section.songs)
                if (!withLine.contains(name) && std::ranges::find(fresh, name) == fresh.end())
                    fresh.push_back(name);
        text = withTransposes(std::move(text), fresh);
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
    rightTabs_->setCurrentWidget(preview_);
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
                folders.insert(
                    toQString(dir.lexically_relative(*relative.begin()).generic_string()));
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
                               "the songbook's CI as well, add it to .github/workflows."),
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
                        toPath(stemName + QStringLiteral(".tex")))
                           .generic_string()));
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
        return relative.generic_string();
    };
    if (const auto own = inside(bookDir))
        return own;
    if (const auto repo = inside(root_))
        return repo;
    // <stem>-cover.<ext>, never replacing a file there (another book's cover, cover.pdf).
    std::string name = stem + "-cover" + absolute.extension().string();
    for (int n = 2; std::filesystem::exists(bookDir / name); ++n)
        name = stem + "-cover-" + std::to_string(n) + absolute.extension().string();
    std::error_code error;
    std::filesystem::copy_file(absolute, bookDir / name, std::filesystem::copy_options::none,
                               error);
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
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error) {
        QMessageBox::warning(this, tr("Duplicate"),
                             tr("Cannot create %1: %2")
                                 .arg(toQString(dir), QString::fromStdString(error.message())));
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
    const auto sourceDir = pathOf(id_).parent_path();
    if (!settings_.cover.empty() && std::filesystem::is_regular_file(sourceDir / settings_.cover) &&
        !std::filesystem::exists(dir / settings_.cover)) {
        std::filesystem::create_directories((dir / settings_.cover).parent_path(), error);
        std::filesystem::copy_file(sourceDir / settings_.cover, dir / settings_.cover, error);
        if (error)
            showWarning(
                tr("The cover %1 was not copied: %2")
                    .arg(toQString(settings_.cover), QString::fromStdString(error.message())));
    }
    if (std::filesystem::is_regular_file(sourceDir / "akordy.tex") &&
        !std::filesystem::exists(dir / "akordy.tex"))
        std::filesystem::copy_file(sourceDir / "akordy.tex", dir / "akordy.tex", error);
    if (!writeNewBook(dir, stemName, copy, script))
        return;
    fillList(toQString((zub::bookfile::booksFolder(root_) / toPath(folderName) /
                        toPath(stemName + QStringLiteral(".tex")))
                           .generic_string()));
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
    {
        const auto transpose = zub::bookfile::readTransposeSettings(text_);
        const int keyIndex =
            transpose.key
                ? bookKey_->findData(toQString(zub::booktranspose::bookKeyName(*transpose.key)))
                : 0;
        bookKey_->setCurrentIndex(std::max(keyIndex, 0));
        // Without a key the file says nothing about the mode: the choice stays (and a new
        // book starts in Chords).
        if (transpose.key || shownBook_ != id_)
            transposeMode_->setCurrentIndex(
                transpose.mode == zub::bookfile::TransposeSettings::Mode::Capo ? 1 : 0);
        shownBook_ = id_;
        for (QWidget* widget : {static_cast<QWidget*>(bookKey_), static_cast<QWidget*>(transposeMode_)})
            widget->setEnabled(canEdit());
    }
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
    updateTransposeWarning();
    updateLatexState();
    rebuildTree();
    updatePicker();
    updateButtons();
}

void SongbooksPage::setPackageDir(const std::filesystem::path& packageDir) {
    packageTransposes_ = !packageDir.empty() && zub::package::definesTransposesong(packageDir);
    updateTransposeWarning();
}

void SongbooksPage::updateTransposeWarning() {
    const bool keyed = !id_.isEmpty() && !text_.empty() &&
                       zub::bookfile::readTransposeSettings(text_).key.has_value();
    transposeWarning_->setVisible(keyed && !packageTransposes_);
}

std::vector<std::string> SongbooksPage::allSongNames() const {
    std::vector<std::string> names;
    for (const auto& section : book_.sections)
        for (const std::string& name : section.songs)
            if (std::ranges::find(names, name) == names.end())
                names.push_back(name);
    return names;
}

std::string SongbooksPage::withTransposes(std::string text,
                                          const std::vector<std::string>& names) const {
    const auto settings = zub::bookfile::readTransposeSettings(text);
    const auto songs = songIndex(*model_);
    const auto fixed = fixedLines(text);
    for (const std::string& name : names) {
        const auto it = songs.find(name);
        const auto plan = planTranspose(name, settings, fixed, it == songs.end() ? nullptr : it->second);
        if (plan.kind == TransposePlan::Kind::Fixed)
            continue; // the song's own choice
        // A song that is not in the library (a wish, a typo) has no key to go by: no line.
        if (auto edited = zub::bookfile::setTranspose(text, name, plan.line))
            text = std::move(*edited);
    }
    return text;
}

void SongbooksPage::onTransposeFormChanged() {
    if (loading_)
        return;
    if (!canEdit()) {
        loadBook(); // back to what the file says
        return;
    }
    zub::bookfile::TransposeSettings settings;
    settings.key = zub::booktranspose::bookKey(toStdString(bookKey_->currentData().toString()));
    settings.mode = transposeMode_->currentData().toString() == QStringLiteral("capo")
                        ? zub::bookfile::TransposeSettings::Mode::Capo
                        : zub::bookfile::TransposeSettings::Mode::Chords;
    // Capo mode raises the capo in the song files themselves (they are shared by every songbook):
    // ask first, then write them; the book keeps only the settings line (and a re-shape line
    // for a song whose capo would leave 0..9).
    QStringList notes;
    if (settings.key && settings.mode == zub::bookfile::TransposeSettings::Mode::Capo) {
        const auto songs = songIndex(*model_);
        const auto fixed = fixedLines(text_);
        std::vector<std::filesystem::path> paths;
        for (const std::string& name : allSongNames()) {
            const auto it = songs.find(name);
            const auto plan =
                planTranspose(name, settings, fixed, it == songs.end() ? nullptr : it->second);
            if (plan.kind == TransposePlan::Kind::Moves && plan.viaCapo &&
                std::ranges::find(paths, it->second->path) == paths.end())
                paths.push_back(it->second->path);
        }
        if (!paths.empty()) {
            const auto answer = QMessageBox::question(
                this, tr("Transpose by capo"),
                tr("%n song file(s) will change: their capo, key and capo notes are raised so "
                   "that the songs sound in %1. This affects every songbook that uses them.\n\n"
                   "The chords stay as they are. None does not undo this; to go back, choose the "
                   "old key again.",
                   nullptr, static_cast<int>(paths.size()))
                    .arg(toQString(zub::booktranspose::bookKeyName(*settings.key))),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes) {
                loadBook(); // back to what the file says
                return;
            }
            notes = moveSongsByCapo(paths, *settings.key);
        }
    }
    // One write: the settings line, then each song's line from it (None drops every line that is
    // not a song's own `% fixed` choice).
    const std::string text =
        withTransposes(zub::bookfile::setTransposeSettings(text_, settings), allSongNames());
    showWarning({});
    writeFile(text); // not written: the form shows what the file says
    loadBook();
    if (!notes.isEmpty())
        showWarning(tr("Not changed:") + QLatin1Char('\n') + notes.join(QLatin1Char('\n')));
}

QStringList SongbooksPage::moveSongsByCapo(const std::vector<std::filesystem::path>& paths,
                                           zub::booktranspose::Key target) {
    namespace bt = zub::booktranspose;
    QStringList skipped;
    for (const auto& path : paths) {
        const QString name = toQString(path.filename());
        if (songModified_ && songModified_(path)) {
            skipped << tr("%1: open in the editor with unsaved changes").arg(name);
            continue;
        }
        auto content = zub::readTextFile(path);
        if (!content) {
            skipped << tr("%1: %2").arg(name, toQString(content.error()));
            continue;
        }
        if (!SongEditor::roundTrips(*content)) {
            skipped << tr("%1: the file can't be rewritten safely").arg(name);
            continue;
        }
        const auto meta = zub::parseSong(*content);
        if (!meta) {
            skipped << tr("%1: the song header has a problem: %2")
                           .arg(name, toQString(meta.error()));
            continue;
        }
        const bool german = zub::score::germanNames(zub::mainLanguage(meta->value("lang")));
        const auto key = bt::parseKey(meta->value("key"), german);
        if (!key)
            continue; // the file changed since the tree was built
        const int semitones = bt::shift(*key, target);
        if (semitones == 0)
            continue;
        const int cap = zub::score::songCapo(meta->value("cap")) + semitones;
        bool fits = cap >= 0 && cap <= 9;
        for (const int body : zub::songheader::bodyCapos(*content))
            fits = fits && body + semitones >= 0 && body + semitones <= 9;
        if (!fits) {
            skipped << tr("%1: the capo would leave 0..9").arg(name);
            continue;
        }
        // The shapes stay, the song sounds `semitones` higher: cap (none at 0, as songs without
        // a capo have no cap=), each \capo{n} and the sounding key.
        std::string updated = zub::songheader::shiftBodyCapos(*content, semitones);
        updated = zub::songheader::setOption(
            updated, "cap",
            cap == 0 ? std::nullopt
                     : std::optional<std::string>("{" + std::to_string(cap) + "}"));
        // Every key of the value moves (`Cm\\rightarrow C`); the rest of the text stays.
        if (const auto value = zub::songheader::option(updated, "key"))
            updated = zub::songheader::setOption(
                updated, "key", "{" + bt::shiftKeys(*value, semitones, german) + "}");
        // Only if the file is still what was read.
        const auto again = zub::readTextFile(path);
        if (!again || *again != *content) {
            skipped << tr("%1: changed on disk meanwhile").arg(name);
            continue;
        }
        if (const auto written = zub::writeTextFileAtomic(path, updated); !written) {
            skipped << tr("%1: %2").arg(name, toQString(written.error()));
            continue;
        }
        if (auto newMeta = zub::parseSong(updated))
            model_->updateSong(path, std::move(*newMeta), zub::todo::notes(updated),
                               zub::songbook::referencedFiles(updated));
    }
    return skipped;
}

void SongbooksPage::chooseSongTranspose(const std::string& name) {
    if (!canEdit())
        return;
    const auto fixed = fixedLines(text_);
    const auto current = fixed.find(name);

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
    if (current != fixed.end() && current->second.semitones != 0) {
        shift->setChecked(true);
        spin->setValue(current->second.semitones);
    } else if (current != fixed.end()) {
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

bool SongbooksPage::setSongTranspose(const std::string& name, std::optional<int> semitones) {
    if (!canEdit())
        return false;
    std::string text = text_;
    if (!semitones) {
        auto cleared = zub::bookfile::setTranspose(text, name, std::nullopt);
        if (!cleared) {
            showWarning(tr("Not saved: %1").arg(toQString(cleared.error())));
            return false;
        }
        text = withTransposes(std::move(*cleared), {name});
    } else {
        const auto songs = songIndex(*model_);
        const auto it = songs.find(name);
        // An override is always a re-shape (the book's capo mode edits song files; this is one
        // song's choice for this book only).
        zub::bookfile::SongTranspose line{name, *semitones, false, {}, true};
        if (it != songs.end()) {
            const auto& meta = it->second->meta;
            const bool german = zub::score::germanNames(zub::mainLanguage(meta.value("lang")));
            if (const auto key = zub::booktranspose::parseKey(meta.value("key"), german))
                line.key = zub::booktranspose::keyName(
                    {((key->pitch + *semitones) % 12 + 12) % 12, key->minor}, german);
        }
        auto edited = zub::bookfile::setTranspose(text, name, line);
        if (!edited) {
            showWarning(tr("Not saved: %1").arg(toQString(edited.error())));
            return false;
        }
        text = std::move(*edited);
    }
    showWarning({});
    const bool written = writeFile(text);
    loadBook();
    return written;
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
    coverName_->setToolTip(found ? QString() : tr("Not found beside the songbook or in the repo"));
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
    if (bookTabs_->currentIndex() != 1 || !latexFind_)
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
    bookTabs_->setTabText(1, modified ? tr("LaTeX ●") : tr("LaTeX"));
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
        bookTabs_->setCurrentIndex(1); // keep showing what needs fixing
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
    // Songs show their key only in a book with a key (or a song with a line of its own).
    const auto transposeSettings = zub::bookfile::readTransposeSettings(text_);
    const auto songs = songIndex(*model_);
    const auto fixed = fixedLines(text_);

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
                    const auto plan =
                        planTranspose(name, transposeSettings, fixed, songs.at(name));
                    using Kind = TransposePlan::Kind;
                    if (plan.kind == Kind::Fixed) {
                        text += QStringLiteral("  ") + tr("kept: %1").arg(plan.note);
                    } else if (transposeSettings.key && plan.kind == Kind::Moves) {
                        text += QStringLiteral("  ") + plan.songKey + QStringLiteral(" → ") +
                                plan.newKey;
                        if (plan.viaCapo)
                            text += QStringLiteral(" ") +
                                    tr("(capo %1)").arg(
                                        (plan.semitones > 0 ? QStringLiteral("+") : QString()) +
                                        QString::number(plan.semitones));
                        else if (!plan.note.isEmpty())
                            text += QStringLiteral(" (") + plan.note + QStringLiteral(")");
                    } else if (transposeSettings.key && plan.kind == Kind::Same) {
                        text += QStringLiteral("  ") + plan.songKey;
                    } else if (transposeSettings.key && plan.kind == Kind::Note) {
                        text += QStringLiteral("  (") +
                                (plan.songKey.isEmpty() ? QString() : plan.songKey + QStringLiteral(", ")) +
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
    if (id_.isEmpty() || book_.sections.empty())
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
    connect(list, &QListWidget::itemSelectionChanged, &dialog, [=, this] {
        open->setEnabled(list->selectedItems().size() == 1);
        remove->setEnabled(canEdit() && !list->selectedItems().isEmpty());
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
    open->setEnabled(list->selectedItems().size() == 1);
    remove->setEnabled(canEdit() && !list->selectedItems().isEmpty());
    connect(remove, &QPushButton::clicked, &dialog, [&] {
        if (!canEdit())
            return; // locked, or the LaTeX tab holds unsaved text
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
