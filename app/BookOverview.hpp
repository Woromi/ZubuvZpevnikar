#pragma once

#include <QString>
#include <QWidget>
#include <filesystem>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;

// The Songbooks page's Overview tab: is the book ready to print? A heading ("Ready to print" or
// "N to fix") and one row per check with its songs under it (zub::songbook::Readiness, rows made
// by SongbooksPage). Open Song, Remove from the Songbook, Refresh.
class BookOverview : public QWidget {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    enum class Severity { Blocker, ToFix, Note };
    struct Song {
        std::string inputName;
        std::filesystem::path path; // empty: not in the library
        QString text;               // "Title — Artist  ·  detail"
    };
    struct Check {
        Severity severity;
        QString title;
        std::vector<Song> songs;
    };

    explicit BookOverview(QWidget* parent = nullptr);

    // The checks of the book `bookId` (rows the user collapsed stay so while it is the same book).
    void setChecks(const QString& bookId, const QString& heading, bool ready,
                   const std::vector<Check>& checks);
    void setRemoveEnabled(bool enabled); // the book can be edited

signals:
    void openSongRequested(const std::filesystem::path& path);
    void removeRequested(const std::vector<std::string>& inputNames);
    void refreshRequested();

private:
    std::vector<std::string> selectedNames() const;
    void updateButtons();

    QLabel* heading_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QPushButton* open_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* refresh_ = nullptr;
    bool removeEnabled_ = false;
    QString bookId_;
    std::vector<QString> collapsed_; // titles of the rows the user collapsed
};
