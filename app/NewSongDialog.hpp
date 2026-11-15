#pragma once

#include <QDialog>
#include <QString>
#include <map>

class QComboBox;
class QDialogButtonBox;
class QLineEdit;

// File → New Song…: asks for the title and the artist (an existing one keeps its folder) and
// shows the file the song will be saved to. The rest of the header is filled in in the editor.
class NewSongDialog : public QDialog {
    Q_OBJECT

public:
    // `artistFolders`: the artists of the songbook (as shown) and the folder of their songs.
    explicit NewSongDialog(const std::map<QString, QString>& artistFolders,
                           QWidget* parent = nullptr);

    QString title() const;    // as typed, trimmed
    QString artist() const;   // likewise
    // "Artist/Title" for songs/chords/: the artist's existing folder, else
    // zub::songimport::fileNameOf of each; empty while either part is.
    QString inputName() const;

private:
    void update();

    std::map<QString, QString> artistFolders_;
    QLineEdit* title_ = nullptr;
    QComboBox* artist_ = nullptr;
    QLineEdit* file_ = nullptr; // read-only: the whole path, however long
    QDialogButtonBox* buttons_ = nullptr;
};
