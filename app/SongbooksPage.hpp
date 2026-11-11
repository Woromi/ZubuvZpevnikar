#pragma once

#include <QString>
#include <QWidget>
#include <filesystem>
#include <functional>
#include <string_view>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <zub/core/songbook.hpp>
#include "SongbookStore.hpp"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSplitter;
class QTableView;
class SongbookExporter;
class SongbookSorter;
class SongbookTree;
class SongFilterProxy;
class SongPickerModel;
class SongTableModel;

// The songbook assembler: the songbooks the app manages (left), the selected one's settings and
// sections (middle), and the song list to pick from (right). Edits are saved at once.
class SongbooksPage : public QWidget {
    Q_OBJECT

public:
    SongbooksPage(SongTableModel* model, SongbookStore* store, SongbookSorter* sorter,
                  SongbookExporter* exporter, QWidget* parent = nullptr);

    void setSongbookRoot(const std::filesystem::path& root);
    // Reads the songbooks from the store again, keeping the selection where possible.
    void reloadList();

signals:
    void backRequested();

private:
    void buildUi();
    QWidget* buildListPane();
    QWidget* buildEditPane();
    QWidget* buildPickerPane();

    void fillList(const QString& selectId);
    void onListRowChanged();
    void newSongbook();
    void duplicateSongbook();
    void deleteSongbook();

    // The selected songbook into the form, the tree and the picker.
    void loadBook();
    bool saveBook();
    void applyForm();               // form -> book_, saved
    void updateFormVisibility();
    void updateCoverLabel();
    void chooseCover();
    std::function<std::string(std::string_view)> titleLookup() const; // titles of the library songs, for the exporter
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
    void commit(); // save, then show the changes
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
    SongbookStore* store_;
    SongbookSorter* sorter_;
    SongbookExporter* exporter_;
    std::filesystem::path root_;

    std::vector<SongbookStore::Entry> entries_;
    QString id_;                   // the selected songbook ("" = none)
    zub::songbook::Songbook book_; // its content (valid when editable_)
    bool editable_ = false;
    int section_ = 0;
    // Identifies the sections of book_ for sort requests (parallel to book_.sections).
    std::vector<quint64> sectionUids_;
    quint64 nextUid_ = 1;
    QString localeWarning_; // from the sorter, stays
    QString sortError_;     // the last "Not sorted: ..."; cleared by the next sorter result
    std::map<QString, SortJob> jobs_; // by sorter key "<songbook id>/<section uid>"
    bool loading_ = false; // filling widgets: ignore their change signals

    QSplitter* splitter_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* duplicate_ = nullptr;
    QPushButton* delete_ = nullptr;

    QWidget* editPane_ = nullptr;
    QFormLayout* form_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* author_ = nullptr;
    QComboBox* class_ = nullptr;
    QCheckBox* chorded_ = nullptr;
    QCheckBox* twoColumns_ = nullptr;
    QCheckBox* shading_ = nullptr;
    QCheckBox* chordChart_ = nullptr;
    QCheckBox* printReady_ = nullptr;
    QLineEdit* campYear_ = nullptr;
    QLabel* coverName_ = nullptr;
    QPushButton* coverChoose_ = nullptr;
    QPushButton* coverClear_ = nullptr;
    QCheckBox* authorIndex_ = nullptr;
    QLabel* warning_ = nullptr;
    QLabel* sorterWarning_ = nullptr;
    SongbookTree* tree_ = nullptr;
    QPushButton* removeSection_ = nullptr;
    QPushButton* renameSection_ = nullptr;
    QPushButton* moveUp_ = nullptr;
    QPushButton* moveDown_ = nullptr;
    QPushButton* removeSong_ = nullptr;
    QPushButton* sortAll_ = nullptr;
    QLabel* count_ = nullptr;

    QWidget* pickerPane_ = nullptr;
    QLabel* pickerHeader_ = nullptr;
    QComboBox* target_ = nullptr;
    QPushButton* addPicked_ = nullptr;
    QPushButton* removePicked_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* language_ = nullptr;
    QCheckBox* onlyInSection_ = nullptr;
    SongFilterProxy* pickerFilter_ = nullptr;
    SongPickerModel* picker_ = nullptr;
    QTableView* pickerView_ = nullptr;
    QPushButton* exportPdf_ = nullptr;
    QPushButton* exportLatex_ = nullptr;
};
