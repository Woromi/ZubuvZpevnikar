#pragma once

#include <QDialog>
#include <QString>
#include <map>
#include <string>
#include <vector>

class QComboBox;
class QDialogButtonBox;
class QLineEdit;

// File → New Song…: asks for the title and the artist (an existing one keeps its folder) and
// shows the file the song will be saved to. The rest of the header is filled in in the editor.
class NewSongDialog : public QDialog {
    Q_OBJECT

public:
    // `artistFolders`: the artists of the songbook (as shown) and the folder of their songs.
    // `collections`: the folders under songs/ (zub::collections); a choice only with two or more.
    NewSongDialog(const std::map<QString, QString>& artistFolders,
                  const std::vector<std::string>& collections, QWidget* parent = nullptr);

    QString title() const;  // as typed, trimmed
    QString artist() const; // likewise
    // "Artist/Title" in the chosen collection: the artist's existing folder, else
    // zub::songimport::fileNameOf of each; empty while either part is.
    QString inputName() const;
    QString collection() const; // the folder under songs/ the song goes to ("chords")

private:
    void update();

    std::map<QString, QString> artistFolders_;
    QLineEdit* title_ = nullptr;
    QComboBox* artist_ = nullptr;
    QComboBox* collection_ = nullptr;
    QLineEdit* file_ = nullptr; // read-only: the whole path, however long
    QDialogButtonBox* buttons_ = nullptr;
};
