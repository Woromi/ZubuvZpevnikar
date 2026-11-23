#pragma once

#include "HomeData.hpp"

#include <QWidget>
#include <filesystem>
#include <vector>

class QAction;
class QGroupBox;
class QGridLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedLayout;
class QTreeWidget;
class WelcomePage;

// The Home view, where the app starts: with a songbook open, cards of what to work on (songs
// opened lately, recently added songs, songs with TODOs, the songbooks and their wishes) and the
// main actions; without one, the welcome panel. It only shows a HomeData: MainWindow builds it
// and acts on the signals.
class HomePage : public QWidget {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    enum class ShowAll { Added, Todo };

    explicit HomePage(QWidget* parent = nullptr);

    WelcomePage* welcome() const { return welcome_; }
    void showWelcome();                 // the panel without a songbook
    void setData(const HomeData& home); // the cards (and shows them)
    bool showsWelcome() const;
    // A button of the actions row that triggers `action` (its text, icon and enabled state).
    void addQuickAction(QAction* action);

signals:
    // By value: a slot may open dialogs while Home refreshes its lists.
    void songRequested(std::filesystem::path path);
    void showAllRequested(HomePage::ShowAll which);
    void bookRequested(const QString& id);
    void wishSearchRequested(HomeWish wish); // Find Online
    void wishImportRequested(HomeWish wish);  // Import…
    void wishNewSongRequested(HomeWish wish); // New Song… (written by hand)
    void wishDoneRequested(HomeWish wish);   // Done (the song is in the library)
    void wishRemoveRequested(HomeWish wish); // Remove
    void wishAddRequested();                 // Add Wish…

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    QGroupBox* card(const QString& title, QListWidget** list, QPushButton** more = nullptr);
    void fillSongs(QListWidget* list, const std::vector<HomeSong>& songs, const QString& empty);
    void placeCards(); // two columns when there is room, else one
    const HomeWish* selectedWish() const;
    void updateWishButtons();

    QStackedLayout* stack_ = nullptr;
    WelcomePage* welcome_ = nullptr;
    QWidget* home_ = nullptr;
    class QScrollArea* scroll_ = nullptr;
    QLabel* heading_ = nullptr;
    QLabel* summary_ = nullptr;
    QWidget* actions_ = nullptr;
    QGridLayout* grid_ = nullptr;
    QGroupBox* openedCard_ = nullptr;
    QGroupBox* addedCard_ = nullptr;
    QGroupBox* todoCard_ = nullptr;
    QGroupBox* booksCard_ = nullptr;
    QGroupBox* wishesCard_ = nullptr;
    QListWidget* opened_ = nullptr;
    QListWidget* added_ = nullptr;
    QListWidget* todo_ = nullptr;
    QListWidget* books_ = nullptr;
    QPushButton* addedMore_ = nullptr;
    QPushButton* todoMore_ = nullptr;
    QTreeWidget* wishes_ = nullptr;
    QPushButton* wishSearch_ = nullptr;
    QPushButton* wishImport_ = nullptr;
    QPushButton* wishOpen_ = nullptr;
    QPushButton* wishDone_ = nullptr;
    QPushButton* wishNew_ = nullptr;
    QPushButton* wishRemove_ = nullptr;
    QPushButton* wishAdd_ = nullptr;
    std::vector<HomeWish> wishList_;
    int columns_ = 0;
};
