#pragma once

#include "BookPreview.hpp"

#include <QString>
#include <QStringList>
#include <QWidget>
#include <deque>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <zub/core/bookfile.hpp>
#include <zub/core/booktranspose.hpp>
#include <zub/core/songbook.hpp>

namespace KSyntaxHighlighting {
class Repository;
}
class BookOverview;
class BookPreview;
class CodeTextEdit;
class CollectionTabs;
class FindBar;
class QAction;
class QToolButton;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QSplitter;
class QTableView;
class SongbookExporter;
class SongbookSorter;
class SongbookTree;
class LabelFilterBox;
class SongFilterProxy;
class SongPickerModel;
class SongTableModel;

// The songbook assembler for the LaTeX books of the songbook repo (songbooks/**.tex): the books
// in their folders (left); the selected one's settings, sections and songs, or its whole LaTeX
// (middle tabs); the songs to pick from and the book's preview (right tabs). Edits through the
// form, the sections and the songs are saved at once with zub::bookfile's byte-exact edits; the
// LaTeX tab has its own Save. A file changed on disk since it was read is never overwritten, and a
// locked book (zub::bookfile::lockMark) is not changed at all until it is unlocked.
class SongbooksPage : public QWidget {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    SongbooksPage(SongTableModel* model, SongbookSorter* sorter, SongbookExporter* exporter,
                  KSyntaxHighlighting::Repository& repository, QWidget* parent = nullptr);

    void setSongbookRoot(const std::filesystem::path& root);
    // The song collections (folders under songs/) for the picker's tabs.
    void setCollections(const std::vector<std::string>& collections);
    // The package the books are built with: transposing needs one that defines
    // \transposesong (else a warning shows).
    void setPackageDir(const std::filesystem::path& packageDir);
    // Before the app closes: asks to save unsaved LaTeX; false to keep the app open.
    bool confirmClose();
    // Lists the books again and reads the selected one (kept where possible) from disk.
    void reloadList();
    // Shows the book `id` (relative path; from Home), reading the list first; false if there is
    // no such book.
    bool showBook(const QString& id);
    void newSongbook(); // New Songbook… (asks)
    // The book (id) whose LaTeX tab has unsaved edits, "" none: others must not write it.
    QString bookWithUnsavedLatex() const;
    bool buildingPdf() const; // a book's PDF is being built (its work dir is in use)
    void showOverview();
    // View > Rebuild Preview (Ctrl+R) here: the Preview tab, built.
    void buildPreview(); // the Overview tab of the shown book
    // The book edits' history (every write of a book is a step; the Edit menu drives it).
    bool canUndo() const { return !undoList_.empty(); }
    bool canRedo() const { return !redoList_.empty(); }
    QString undoText() const { return undoList_.empty() ? QString() : undoList_.back().description; }
    QString redoText() const { return redoList_.empty() ? QString() : redoList_.back().description; }
    void undo();
    void redo();
    // Ctrl+F / Ctrl+H: opens the find bar of the LaTeX tab when that tab is the current one;
    // false otherwise.
    bool showFind(bool replaceMode);

signals:
    // canUndo/canRedo/undoText/redoText may have changed.
    void historyChanged();
    // The book check's Open Song: show that song (MainWindow opens it).
    void openSongRequested(const std::filesystem::path& path);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    // The Overview tab of the shown book again (only while it is shown).
    void refreshOverview();
    QWidget* buildListPane();
    QWidget* buildEditPane();
    QWidget* buildPickerPane();
    QWidget* buildLatexPane();
    QWidget* buildPreviewPane(); // the Preview tab: the transposition row and the preview

    void fillList(const QString& selectId);
    void onListRowChanged();
    QTreeWidgetItem* listItemOf(const QString& id) const;

    // id: the book's path relative to the songbook root ('/'-separated).
    std::filesystem::path pathOf(const QString& id) const;
    // Reads and parses id_'s file into text_, file_, settings_ and book_; false (warning shown)
    // on failure.
    bool loadFile();
    // Writes `text` as the book (unless the file changed on disk since it was read: then it is
    // read again and the change dropped, with a warning) and reads it back. False if not written.
    bool writeFile(const std::string& text);
    // The song lists of book_ into the file (setSongs per changed section).
    bool saveSongs();
    // A bookfile edit of text_ (setSetting, addSection, ...): written, or its error shown.
    bool applyEdit(const std::expected<std::string, std::string>& edited,
                   const QString& description = {});
    bool canEdit() const; // a book is shown, it can be edited and it is not locked
    void toggleLock();
    // Export PDF: the preview's PDF when it is of the current text, else built first.
    void exportPdf();
    // What the sorter orders in section s: its songs, or for a repo book the first song of each
    // keep-order block and the single songs (zub::bookfile::leaders); and that order back.
    std::vector<std::string> sortUnits(int s) const;
    std::vector<std::string> expandSorted(int s, const std::vector<std::string>& order) const;
    void duplicateSongbook();
    // How a book <stem> in `bookDir` names the cover `file`: its path from the book's folder or
    // the repo root, or, from elsewhere, "<stem>-cover.<ext>" (a free name) after copying it
    // there. nullopt: the copy failed.
    std::optional<std::string> coverReference(const std::filesystem::path& file,
                                              const std::filesystem::path& bookDir,
                                              const std::string& stem);
    // Writes <dir>/<stem>.tex and, unless the folder has one, an executable build.sh.
    bool writeNewBook(const std::filesystem::path& dir, const QString& stem, const std::string& tex,
                      const std::string& buildSh);

    // The selected songbook into the form, the tree, the LaTeX tab and the picker.
    void loadBook();
    bool saveBook();
    void applyField(zub::bookfile::Field field, const std::string& value);
    void updateFormVisibility();
    void updateCoverLabel();
    // ---- Transposing a book for its preview and export (zub::booktranspose::Mode). Nothing is
    // saved: each book's choice lives for the session and goes into the build copy only.
    struct TransposeChoice {
        std::optional<zub::booktranspose::Key> key; // nullopt: the songs keep their keys
        zub::booktranspose::Mode mode = zub::booktranspose::Mode::Chords;
        std::map<std::string, int> fixed; // a song's own shift (the tree's Transpose…), 0 = none
        // Changes the build (Capo mode without a key does nothing).
        bool active() const {
            return key || mode == zub::booktranspose::Mode::NoCapo || !fixed.empty();
        }
        bool isDefault() const {
            return !key && mode == zub::booktranspose::Mode::Chords && fixed.empty();
        }
    };
    TransposeChoice transposeChoice() const; // the shown book's
    void onTransposeChoiceChanged();         // the Key / mode combos
    void showTransposeChoice();              // the combos and the preview from the choice
    // The preview's composer: the book copy's text with a \transposesong line per moved song
    // (and `transposecapos` without capo) and, in Capo mode, copies of the songs whose capo rises.
    std::optional<BookPreview::BuildInput> composeBuild(const std::string& text);
    std::vector<std::string> allSongNames() const; // each once, in book order
    // The tree's Transpose…: asks, then setSongTranspose.
    void chooseSongTranspose(const std::string& name);
    // A song's own choice for this session: `semitones` (0 = do not transpose), nullopt = follow
    // the book.
    void setSongTranspose(const std::string& name, std::optional<int> semitones);
    void updateTransposeWarning();
    void chooseCover();
    // The LaTeX tab.
    void saveLatex();
    void revertLatex();
    bool latexModified() const;
    // The LaTeX tab (unless it holds unsaved edits) and the preview's status from text_.
    void refreshFileViews();
    void updateLatexState();
    void rebuildTree();
    void onTreeSelectionChanged();
    int currentSection() const;
    int indexOfUid(quint64 uid) const; // -1: none
    void fillTargetCombo();
    void updatePicker();
    void updateButtons();
    void refreshLanguages();

    void addSection();
    void renameSection(int index);
    void removeSection(int index);
    void moveSection(int delta);
    void removeSelectedSongs();
    bool commit(const QString& description = {}); // save, then show the changes; false if nothing was saved
    // The undo history: every write of a book that changed it (writeFile) is an entry.
    struct HistoryEntry {
        QString id;
        std::string before;
        std::string after;
        QString description; // "remove 2 songs"; may be empty
    };
    // Writes the newest entry of `from` back (undo: its `before`, redo: its `after`) and moves it
    // to `to`.
    void stepHistory(std::deque<HistoryEntry>& from, std::deque<HistoryEntry>& to, bool undo);
    void updateHistoryActions(); // emits historyChanged
    void selectDefaultBook();    // the last used book, else the first
    void showEmptyState();       // the "No songbooks yet" page while there is no book
    void openSongNamed(const std::string& inputName); // openSongRequested for an input name
    bool askSectionName(const QString& title, const QString& initial, int ignoreSection,
                        QString* name);

    // The names of the picker's selected songs.
    std::vector<std::string> pickerSelection() const;
    void onPickerClicked(const QModelIndex& index);
    void addPicked();
    void removePicked();
    // Adds `names` to section `target`: `from` >= 0 moves them out of that section, -1 copies from
    // the picker. They are shown at the end at once and sorted into place when the sorter answers.
    void addSongs(int target, const std::vector<std::string>& names, int from);
    void reorderWithin(int section, const std::vector<std::string>& names, int index);
    void onSongsDropped(const std::vector<std::string>& names, quint64 fromUid, quint64 toUid,
                        int index);
    void sortAll();

    enum class SortMode {
        Replace, // the sorted order is the new order
        Merge    // `pending` songs go to their alphabetical place in the existing order
    };
    struct SortJob {
        SortMode mode = SortMode::Replace;
        std::set<std::string> pending;
    };
    void requestSort(int sectionIndex, SortMode mode, const std::set<std::string>& pending = {});
    std::vector<std::string> orderFor(const SortJob& job, const std::vector<std::string>& current,
                                      const std::vector<std::string>& sorted) const;
    void onSorted(const QString& key, const std::vector<std::string>& names);
    void showWarning(const QString& text);
    void updateSorterWarning();
    void resetSectionUids();

    SongTableModel* model_;
    SongbookSorter* sorter_;
    SongbookExporter* exporter_;
    std::filesystem::path root_;

    QString id_;                       // the selected songbook ("" = none)
    std::string text_;                 // its file as last read or written
    zub::bookfile::Book file_;         // parsed from text_
    zub::bookfile::Settings settings_; // read from text_
    zub::songbook::Songbook book_;     // its title, class and song lists (from file_)
    bool editable_ = false;            // read and parsed, no section read-only
    std::string latexBase_;            // the LaTeX tab's text when it was filled
    QString latexKeepFor_;             // fillList: the book whose unsaved LaTeX is kept
    int section_ = 0;
    // Identifies the sections of book_ for sort requests (parallel to book_.sections).
    std::vector<quint64> sectionUids_;
    quint64 nextUid_ = 1;
    QString localeWarning_; // from the sorter, stays
    QString sortError_;     // the last "Not sorted: ..."; cleared by the next sorter result
    std::map<QString, SortJob> jobs_; // by sorter key "<songbook id>/<section uid>"
    bool loading_ = false;            // filling widgets: ignore their change signals

    QSplitter* splitter_ = nullptr;
    QStackedWidget* stack_ = nullptr; // the panes, or the empty state
    QPushButton* emptyNew_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QAction* newBook_ = nullptr;
    QAction* duplicate_ = nullptr;
    QAction* lock_ = nullptr;
    std::deque<HistoryEntry> undoList_; // newest last
    std::deque<HistoryEntry> redoList_;
    QString nextDescription_; // what the next writeFile does, for its entry

    QTabWidget* bookTabs_ = nullptr; // Songbook | LaTeX
    QWidget* editPane_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* lockBar_ = nullptr;
    QPushButton* addSection_ = nullptr;
    QFormLayout* form_ = nullptr;
    CodeTextEdit* latex_ = nullptr;
    FindBar* latexFind_ = nullptr;
    QPushButton* latexSave_ = nullptr;
    QPushButton* latexRevert_ = nullptr;
    QLabel* latexState_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* author_ = nullptr;
    QComboBox* class_ = nullptr; // hidden: holds the classes' names
    QLabel* classLabel_ = nullptr;
    QCheckBox* chorded_ = nullptr;
    QCheckBox* twoColumns_ = nullptr;
    QCheckBox* shading_ = nullptr;
    QCheckBox* chordChart_ = nullptr;
    QLineEdit* campYear_ = nullptr;
    QLabel* coverName_ = nullptr;
    QPushButton* coverChoose_ = nullptr;
    QPushButton* coverClear_ = nullptr;
    QLabel* warning_ = nullptr;
    QComboBox* bookKey_ = nullptr;
    QComboBox* transposeMode_ = nullptr;
    std::map<QString, TransposeChoice> transposeChoices_; // by book id, this session only
    QLabel* transposeWarning_ = nullptr;
    bool packageTransposes_ = false; // the package in use defines \transposesong
    QLabel* sorterWarning_ = nullptr;
    SongbookTree* tree_ = nullptr;
    QAction* removeSection_ = nullptr;
    QAction* renameSection_ = nullptr;
    QAction* moveUp_ = nullptr;
    QAction* moveDown_ = nullptr;
    QAction* sortAll_ = nullptr;
    QToolButton* sectionMore_ = nullptr; // the ⋯ menu
    QPushButton* removeSong_ = nullptr;  // only while songs are selected
    QLabel* count_ = nullptr;

    QTabWidget* rightTabs_ = nullptr; // Add Songs | Preview
    BookPreview* preview_ = nullptr;
    BookOverview* overview_ = nullptr;
    QWidget* latexPane_ = nullptr; // the LaTeX tab
    QTimer* overviewTimer_ = nullptr; // refreshes the Overview a moment after the songs changed
    QString pendingExport_; // the book whose preview build ends in Export PDF
    QWidget* pickerPane_ = nullptr;
    QComboBox* target_ = nullptr;
    QPushButton* addPicked_ = nullptr;
    QPushButton* removePicked_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* language_ = nullptr;
    QComboBox* pickerShow_ = nullptr;
    LabelFilterBox* labelFilter_ = nullptr;
    QCheckBox* onlyInSection_ = nullptr;
    SongFilterProxy* pickerFilter_ = nullptr;
    CollectionTabs* pickerCollections_ = nullptr;
    SongPickerModel* picker_ = nullptr;
    QTableView* pickerView_ = nullptr;
    QAction* exportPdf_ = nullptr;
};
