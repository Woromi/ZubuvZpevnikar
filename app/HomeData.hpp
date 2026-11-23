#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <expected>
#include <string>
#include <filesystem>
#include <utility>
#include <vector>

class SongTableModel;

// What the Home view shows, as plain lists: built by buildHomeData from the song model, the books
// of the songbook and the remembered songs; HomePage only shows it.
struct HomeSong {
    std::filesystem::path path;
    QString title, artist;
    QString detail; // a TODO note, a date, ... ("" none)
};

struct HomeBook {
    QString id;    // relative path, as on the Songbooks page
    QString title; // \title, else the file name
    int songs = 0;
    int wishes = 0;
    bool locked = false;
};

struct HomeWish {
    QString bookId, bookTitle; // "" for the wish list (wishes.txt)
    QString text;               // what the line says after "TODO:"
    QString name;               // the input name it holds ("" none)
    std::filesystem::path song; // the library song of that name ("" not in the library)
    int line = 0;
};

struct HomeData {
    std::filesystem::path root;
    std::vector<std::pair<QString, int>> collections; // songs per collection
    int songs = 0;
    int uncommitted = 0; // songs not committed yet (only when the dates are known)
    bool datesKnown = false;
    std::vector<HomeSong> opened;  // Continue editing, newest first
    std::vector<HomeSong> added;   // Recently added (at most kListed)
    int addedTotal = 0;
    std::vector<HomeSong> todo;    // with TODO notes (at most kListed)
    int todoTotal = 0;
    std::vector<HomeBook> books;
    std::vector<HomeWish> wishes;
    static constexpr int kListed = 10;
};

// `opened`: the remembered songs (rememberedSongs), newest first; songs no longer in the model
// are left out.
HomeData buildHomeData(const SongTableModel& model, const std::filesystem::path& root,
                       const std::vector<std::filesystem::path>& opened);

// The songs opened in the editor, remembered per songbook in the settings (paths relative to the
// songbook), newest first, at most HomeData::kListed.
std::vector<std::filesystem::path> rememberedSongs(const std::filesystem::path& root);
void rememberSong(const std::filesystem::path& root, const std::filesystem::path& song);

// The words of a name part: "NecoOLasce" -> "Neco O Lasce", "JohnLennon" -> "John Lennon",
// "21Guns" -> "21 Guns", "Song2" -> "Song 2", '_' and '-' become spaces.
QString splitWords(const QString& part);
// A wish as the list shows it: "Title — Artist" with the words split (and what the line says
// after the name in parentheses); a wish without an input name as it is written.
QString wishDisplayText(const QString& text, const QString& name);
// ---- Writing wishes. Each reads the file anew and changes it only when the wish is still on its
// line (the file may have changed since Home read it); the error says why not, for people.

// A wish is done: in a book its line becomes \input{inputName} (zub::bookfile::fulfillWish), in
// the wish list it goes. A locked book is not changed.
std::expected<void, QString> fulfilWish(const std::filesystem::path& root, const HomeWish& wish,
                                        const std::string& inputName);
// The wish removed (a book's line, or the wish list's).
std::expected<void, QString> removeWish(const std::filesystem::path& root, const HomeWish& wish);
// A new line in the wish list (the file is made when missing).
std::expected<void, QString> addWish(const std::filesystem::path& root, const QString& text);

// What to search the web for, for a wish: the artist's and the title's words ("PavelDobes/
// NecoOLasce" -> "Pavel Dobes Neco O Lasce"), other text as it is, plus `suffix` ("chords").
QString wishSearchText(const QString& text, const QString& name, const QString& suffix);
