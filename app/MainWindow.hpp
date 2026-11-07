#pragma once

#include <QMainWindow>
#include <QString>
#include <filesystem>

class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;
class SongFilterProxy;
class SongTableModel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Opens the songbook given on the command line, else the remembered one, else a guess
    // next to this repo; if none is valid (or the command-line one is not), asks for a folder.
    void openInitialSongbook(const QString& commandLinePath);

private:
    void createMenus();
    void createCentralWidget();
    void chooseSongbookFolder();
    struct OpenOptions {
        bool showErrors = true; // a message box when the folder is not a songbook
        bool remember = true;   // save it as the songbook for the next start
    };
    bool openSongbook(const std::filesystem::path& root, OpenOptions options);
    void reloadSongbook();
    void updateLanguageFilter();
    void updateStatus();

    SongTableModel* model_ = nullptr;
    SongFilterProxy* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* language_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QLabel* issuesLabel_ = nullptr;

    std::filesystem::path songbookRoot_;
    int issueCount_ = 0;
};
