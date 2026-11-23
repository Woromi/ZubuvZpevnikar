#pragma once

#include <QObject>
#include <QString>
#include <filesystem>
#include <functional>

class QWidget;
class SongActions;
class SongEditor;
class SongbookScan;
class SongFilterProxy;
class SongListPage;
class SongTableModel;

// Songs → Check Songs…: the options dialog, the whole-songbook scan (SongbookScan) with its
// progress dialog, and the marks it leaves in the song list (this session only). Changes no song.
class SongbookCheck : public QObject {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    // What only the window can do.
    struct Hooks {
        std::function<std::filesystem::path()> findConverter; // empty: none found
        std::function<std::string()> documentClass; // of the song preview (decides the paper)
        std::function<QString()> noPackageText;     // why a build cannot run
        std::function<SongEditor*()> openSong;      // the open song, or nullptr
    };

    // `window`: the parent of the dialogs, and the owner of this object.
    SongbookCheck(QWidget* window, SongTableModel* model, SongFilterProxy* proxy,
                  SongListPage* listPage, SongActions* actions, Hooks hooks);

    void setSongbook(const std::filesystem::path& root, const std::filesystem::path& packageDir);
    // Asks what to look for, then runs the scan; nothing while one runs or without a songbook.
    void start();

signals:
    void statusMessage(const QString& text, int timeoutMs);
    // The scan ended: `found` songs with possible changes (the list's filter shows them).
    void finished(int found, bool cancelled);

private:
    QWidget* window_;
    SongTableModel* model_;
    SongFilterProxy* proxy_;
    SongListPage* listPage_;
    SongActions* actions_;
    Hooks hooks_;
    std::filesystem::path songbookRoot_;
    std::filesystem::path packageDir_;
    SongbookScan* scan_ = nullptr; // while a songbook scan runs
    int found_ = 0;                // songs it marked so far
};
