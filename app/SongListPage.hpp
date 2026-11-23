#pragma once

#include <QWidget>
#include <filesystem>
#include <string>
#include <vector>

class LabelFilterBox;
class QAction;
class CollectionTabs;
class QComboBox;
class QLineEdit;
class QModelIndex;
class QTableView;
class QToolBar;
class SongFilterProxy;
class SongTableModel;

// The Songs view: a heading with the actions on songs (New Song, Import, Scan …), the filters
// (search, language, label, and one "Show" choice: all, favourites, with TODOs, with possible
// changes after a scan) and the table. MainWindow owns the model and the proxy.
class SongListPage : public QWidget {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    SongListPage(SongTableModel* model, SongFilterProxy* proxy, QWidget* parent = nullptr);

    QTableView* table() const { return table_; }
    void addHeaderAction(QAction* action); // a button in the heading, text beside the icon
    void focusSearch();
    void updateLanguages(); // every language of the model, the choice kept while it exists
    // After a songbook scan: the "Possible changes" column and the Show choice for it, chosen
    // when the scan found something (with nothing found the list must not look empty).
    void showScanResults(bool found);
    // Selects and shows the song stored at `path` (e.g. the one that was just open).
    void selectSong(const std::filesystem::path& path);
    // The song collections (folders under songs/) as tabs over the list; hidden with one.
    void setCollections(const std::vector<std::string>& collections);
    // The Show choice (e.g. from Home's "Show all"); the search and the language filter are
    // cleared so that all of them show.
    enum Show { All, Favourites, Tabs, Todo, Recent, Changes };
    void setShow(Show show);

signals:
    void songActivated(const QModelIndex& proxyIndex);         // double-click or Enter
    void favouriteClicked(const std::filesystem::path& path);  // a click on the ★
    void contextMenuRequested(const QPoint& viewportPosition); // of the table

private:
    void applyShow();
    void updateRecentChoice(); // enabled while the model knows the dates

    SongTableModel* model_;
    SongFilterProxy* proxy_;
    QToolBar* header_ = nullptr;
    CollectionTabs* collections_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* language_ = nullptr;
    LabelFilterBox* labelFilter_ = nullptr;
    QComboBox* show_ = nullptr;
    QTableView* table_ = nullptr;
    bool recentShown_ = false;                    // the Recently added view is on
    int sortColumnBefore_ = 0;                    // the sort to restore when it goes off
    Qt::SortOrder sortOrderBefore_ = Qt::AscendingOrder;
};
