#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <map>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

// File → New Song…: asks for the title, the artist (an existing one keeps its folder) and the
// language, and shows the file the song will be saved to.
class NewSongDialog : public QDialog {
    Q_OBJECT

public:
    // `artistFolders`: the artists of the songbook (as shown) and the folder of their songs;
    // `languages`: the languages in use, offered first.
    NewSongDialog(const std::map<QString, QString>& artistFolders, const QStringList& languages,
                  QWidget* parent = nullptr);

    QString title() const;    // as typed, trimmed
    QString artist() const;   // likewise
    QString language() const; // "" = left to fill in
    // "Artist/Title" for songs/chords/: the artist's existing folder, else
    // zub::songimport::fileNameOf of each; empty while either part is.
    QString inputName() const;

private:
    void update();

    std::map<QString, QString> artistFolders_;
    QLineEdit* title_ = nullptr;
    QComboBox* artist_ = nullptr;
    QComboBox* language_ = nullptr;
    QLabel* file_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};
