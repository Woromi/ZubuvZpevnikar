#pragma once

#include <QWidget>
#include <filesystem>

class LabelFilterBox;
class QAction;
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

signals:
    void songActivated(const QModelIndex& proxyIndex);         // double-click or Enter
    void favouriteClicked(const std::filesystem::path& path);  // a click on the ★
    void contextMenuRequested(const QPoint& viewportPosition); // of the table

private:
    enum Show { All, Favourites, Tabs, Todo, Changes };
    void applyShow();

    SongTableModel* model_;
    SongFilterProxy* proxy_;
    QToolBar* header_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* language_ = nullptr;
    LabelFilterBox* labelFilter_ = nullptr;
    QComboBox* show_ = nullptr;
    QTableView* table_ = nullptr;
};
