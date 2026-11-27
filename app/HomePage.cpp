#include "HomePage.hpp"

#include "WelcomePage.hpp"
#include "convert.hpp"

#include <QAction>
#include <QApplication>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <optional>

namespace {

constexpr int pathRole = Qt::UserRole;
constexpr int idRole = Qt::UserRole + 1;
constexpr int wishRole = Qt::UserRole + 2; // index into wishList_, -1 for a book row

// A list as tall as its rows (the page scrolls, not the card).
void fitHeight(QListWidget* list) {
    int height = 2 * list->frameWidth();
    for (int i = 0; i < list->count(); ++i)
        height += list->sizeHintForRow(i);
    list->setFixedHeight(std::max(height, list->fontMetrics().height() + 8));
}

} // namespace

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("homePage"));
    stack_ = new QStackedLayout(this);
    welcome_ = new WelcomePage(this);
    stack_->addWidget(welcome_);

    auto* scroll = scroll_ = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    home_ = new QWidget(scroll);
    auto* layout = new QVBoxLayout(home_);
    layout->setContentsMargins(24, 18, 24, 18);
    layout->setSpacing(12);
    heading_ = new QLabel(home_);
    QFont font = heading_->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() * 1.6);
    heading_->setFont(font);
    summary_ = new QLabel(home_);
    summary_->setObjectName(QStringLiteral("homeSummary"));
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    actions_ = new QWidget(home_);
    auto* actionsLayout = new QHBoxLayout(actions_);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->addStretch(1);
    layout->addWidget(heading_);
    layout->addWidget(summary_);
    layout->addWidget(actions_);

    openedCard_ = card(tr("Continue editing"), &opened_);
    addedCard_ = card(tr("Recently added"), &added_, &addedMore_);
    todoCard_ = card(tr("To do", "Home: songs with TODO notes"), &todo_, &todoMore_);
    booksCard_ = card(tr("Songbooks"), &books_);

    wishesCard_ = new QGroupBox(tr("Wishes"), home_);
    wishesCard_->setToolTip(tr("Songs wanted in the songbooks: the % TODO: lines of their song "
                               "lists"));
    auto* wishLayout = new QVBoxLayout(wishesCard_);
    wishes_ = new QTreeWidget(wishesCard_);
    wishes_->setObjectName(QStringLiteral("homeWishes"));
    wishes_->setHeaderHidden(true);
    wishes_->setRootIsDecorated(false); // one flat list
    wishes_->setMinimumHeight(200);
    wishes_->setMaximumHeight(360);
    wishLayout->addWidget(wishes_);
    auto* wishButtons = new QHBoxLayout;
    wishSearch_ = new QPushButton(tr("Find Online"), wishesCard_);
    wishSearch_->setToolTip(tr("Search the web for the song's chords (opens the browser)"));
    wishImport_ = new QPushButton(tr("Import…", "Home: import a wished song"), wishesCard_);
    wishImport_->setToolTip(tr("Import the song from a chords website"));
    wishOpen_ = new QPushButton(tr("Open Song"), wishesCard_);
    wishOpen_->setToolTip(tr("The song is in the library already: open it"));
    wishButtons->addWidget(wishSearch_);
    wishDone_ = new QPushButton(tr("Done", "a wish is fulfilled"), wishesCard_);
    wishDone_->setToolTip(tr("The song is in the library: a book's wish becomes the song in that "
                             "book, a wish of the wish list goes"));
    // Rarer: in a menu behind the row's More button, and on right-click.
    wishMove_ = new QAction(tr("Move to Wish List"), wishesCard_);
    wishMove_->setToolTip(tr("Move this wish from its songbook (a % TODO: line) to the wish list "
                             "(wishes.txt)"));
    wishRemove_ = new QAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                              tr("Remove", "a wish"), wishesCard_);
    wishRemove_->setToolTip(tr("Remove the wish (its line)"));
    auto* moreMenu = new QMenu(wishesCard_);
    moreMenu->setToolTipsVisible(true);
    moreMenu->addAction(wishMove_);
    moreMenu->addAction(wishRemove_);
    wishMore_ = new QToolButton(wishesCard_);
    wishMore_->setText(QStringLiteral("⋯"));
    wishMore_->setToolTip(tr("More"));
    wishMore_->setMenu(moreMenu);
    wishMore_->setPopupMode(QToolButton::InstantPopup);
    wishMore_->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    wishes_->setContextMenuPolicy(Qt::CustomContextMenu);
    wishAdd_ = new QPushButton(tr("Add Wish…"), wishesCard_);
    wishAdd_->setToolTip(tr("A song you want: a new line of the wish list (wishes.txt in the song "
                            "library)"));
    wishNew_ = new QPushButton(tr("New Song…", "Home: write a wished song by hand"), wishesCard_);
    wishNew_->setToolTip(tr("Write the song by hand: a new song with the wish's title and artist"));
    // Only the buttons for the selected wish show: a song still to get (Find Online, Import, New
    // Song) or one the library has (Open Song, Done).
    wishButtons->addWidget(wishImport_);
    wishButtons->addWidget(wishNew_);
    wishButtons->addWidget(wishOpen_);
    wishButtons->addWidget(wishDone_);
    wishButtons->addWidget(wishMore_);
    wishButtons->addStretch(1);
    wishButtons->addWidget(wishAdd_);
    wishLayout->addLayout(wishButtons);

    grid_ = new QGridLayout;
    grid_->setSpacing(12);
    layout->addLayout(grid_);
    layout->addStretch(1);
    scroll->setWidget(home_);
    stack_->addWidget(scroll);
    placeCards();

    for (QListWidget* list : {opened_, added_, todo_})
        connect(list, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            const QString path = item->data(pathRole).toString();
            if (!path.isEmpty())
                emit songRequested(toPath(path));
        });
    connect(books_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QString id = item->data(idRole).toString();
        if (!id.isEmpty())
            emit bookRequested(id);
    });
    connect(addedMore_, &QPushButton::clicked, this,
            [this] { emit showAllRequested(ShowAll::Added); });
    connect(todoMore_, &QPushButton::clicked, this,
            [this] { emit showAllRequested(ShowAll::Todo); });
    connect(wishes_, &QTreeWidget::currentItemChanged, this, &HomePage::updateWishButtons);
    connect(wishes_, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (const HomeWish* wish = selectedWish(); wish && !wish->song.empty())
            emit songRequested(wish->song);
    });
    connect(wishSearch_, &QPushButton::clicked, this, [this] {
        if (const HomeWish* wish = selectedWish())
            emit wishSearchRequested(*wish);
    });
    connect(wishImport_, &QPushButton::clicked, this, [this] {
        if (const HomeWish* wish = selectedWish())
            emit wishImportRequested(*wish);
    });
    connect(wishNew_, &QPushButton::clicked, this, [this] {
        if (const HomeWish* wish = selectedWish())
            emit wishNewSongRequested(*wish);
    });
    connect(wishDone_, &QPushButton::clicked, this, [this] {
        if (const HomeWish* wish = selectedWish(); wish && !wish->song.empty())
            emit wishDoneRequested(*wish);
    });
    connect(wishes_, &QWidget::customContextMenuRequested, this, [this](const QPoint& at) {
        const HomeWish* wish = selectedWish();
        if (!wish || !wishes_->itemAt(at))
            return;
        QMenu menu(this);
        for (QPushButton* button : {wishSearch_, wishImport_, wishNew_, wishOpen_, wishDone_})
            if (button->isVisible() && button->isEnabled())
                connect(menu.addAction(button->text()), &QAction::triggered, button,
                        &QPushButton::click);
        menu.addSeparator();
        for (QAction* action : {wishMove_, wishRemove_})
            if (action->isVisible() && action->isEnabled())
                menu.addAction(action);
        menu.exec(wishes_->viewport()->mapToGlobal(at));
    });
    connect(wishMove_, &QAction::triggered, this, [this] {
        if (const HomeWish* wish = selectedWish(); wish && !wish->bookId.isEmpty())
            emit wishMoveRequested(*wish);
    });
    connect(wishRemove_, &QAction::triggered, this, [this] {
        if (const HomeWish* wish = selectedWish())
            emit wishRemoveRequested(*wish);
    });
    connect(wishAdd_, &QPushButton::clicked, this, &HomePage::wishAddRequested);
    connect(wishOpen_, &QPushButton::clicked, this, [this] {
        if (const HomeWish* wish = selectedWish(); wish && !wish->song.empty())
            emit songRequested(wish->song);
    });
    updateWishButtons();
    showWelcome();
}

QGroupBox* HomePage::card(const QString& title, QListWidget** list, QPushButton** more) {
    auto* box = new QGroupBox(title, home_);
    auto* layout = new QVBoxLayout(box);
    *list = new QListWidget(box);
    (*list)->setFrameShape(QFrame::NoFrame);
    (*list)->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    (*list)->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    (*list)->setTextElideMode(Qt::ElideRight);
    (*list)->viewport()->setCursor(Qt::PointingHandCursor);
    (*list)->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(*list);
    if (more) {
        *more = new QPushButton(box);
        (*more)->setFlat(true);
        (*more)->setCursor(Qt::PointingHandCursor);
        layout->addWidget(*more, 0, Qt::AlignLeft);
    }
    layout->addStretch(1);
    return box;
}

void HomePage::showWelcome() {
    stack_->setCurrentIndex(0);
}

bool HomePage::showsWelcome() const {
    return stack_->currentIndex() == 0;
}

void HomePage::addQuickAction(QAction* action) {
    auto* button = new QToolButton(actions_);
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto* layout = static_cast<QHBoxLayout*>(actions_->layout());
    layout->insertWidget(layout->count() - 1, button);
}

void HomePage::fillSongs(QListWidget* list, const std::vector<HomeSong>& songs,
                         const QString& empty) {
    list->clear();
    for (const HomeSong& song : songs) {
        QString text = song.title;
        if (!song.artist.isEmpty())
            text += QStringLiteral(" — ") + song.artist;
        if (!song.detail.isEmpty())
            text += QStringLiteral("   ·  ") + song.detail;
        auto* item = new QListWidgetItem(text, list);
        item->setData(pathRole, toQString(song.path));
        item->setToolTip(toQString(song.path));
    }
    if (songs.empty()) {
        auto* item = new QListWidgetItem(empty, list);
        item->setFlags(Qt::NoItemFlags);
    }
    fitHeight(list);
}

void HomePage::setData(const HomeData& home) {
    heading_->setText(toQString(home.root.filename()));
    QStringList parts;
    parts << tr("%n song(s)", nullptr, home.songs);
    if (home.collections.size() > 1)
        for (const auto& [name, count] : home.collections)
            parts << QStringLiteral("%1: %2").arg(name).arg(count);
    parts << tr("%n songbook(s)", nullptr, static_cast<int>(home.books.size()));
    if (home.uncommitted > 0)
        parts << tr("%n song(s) not committed yet", nullptr, home.uncommitted);
    summary_->setText(toQString(home.root) + QLatin1Char('\n') + parts.join(QStringLiteral(" · ")));

    fillSongs(opened_, home.opened, tr("Songs you open are listed here."));
    fillSongs(added_, home.added, tr("No song added in the last 30 days."));
    addedCard_->setVisible(home.datesKnown);
    addedMore_->setText(tr("Show all %n recently added…", nullptr, home.addedTotal));
    addedMore_->setVisible(home.addedTotal > static_cast<int>(home.added.size()));
    placeCards();
    fillSongs(todo_, home.todo, tr("No song has a TODO note."));
    todoMore_->setText(tr("Show all %n with TODOs…", nullptr, home.todoTotal));
    todoMore_->setVisible(home.todoTotal > static_cast<int>(home.todo.size()));

    books_->clear();
    for (const HomeBook& book : home.books) {
        QString text =
            book.title + QStringLiteral("   ·  ") + tr("%n song(s)", nullptr, book.songs);
        if (book.wishes > 0)
            text += QStringLiteral(", ") + tr("%n wish(es)", nullptr, book.wishes);
        if (book.toFix == 0)
            text += QStringLiteral("  ·  ") + tr("ready ✓", "a songbook ready to print");
        else if (book.toFix > 0)
            text += QStringLiteral("  ·  ") + tr("%n to fix", nullptr, book.toFix);
        if (book.locked)
            text += QStringLiteral("  🔒");
        auto* item = new QListWidgetItem(text, books_);
        item->setData(idRole, book.id);
        item->setToolTip(book.id);
    }
    if (home.books.empty()) {
        auto* item =
            new QListWidgetItem(tr("No songbooks yet: make one on the Songbooks page."), books_);
        item->setFlags(Qt::NoItemFlags);
    }
    fitHeight(books_);

    // Wishes: one flat list, the wish list's then any still in a book (marked "in <book>"). A
    // refresh keeps the chosen wish (by its book and line) and where the page is scrolled.
    std::optional<std::pair<QString, int>> chosen;
    if (const HomeWish* wish = selectedWish())
        chosen = std::pair(wish->bookId, wish->line);
    const int scrolled = scroll_->verticalScrollBar()->value();
    const int wishesScrolled = wishes_->verticalScrollBar()->value();
    wishList_ = home.wishes;
    wishes_->clear();
    for (std::size_t i = 0; i < wishList_.size(); ++i) {
        const HomeWish& wish = wishList_[i];
        QString text = wishDisplayText(wish.text, wish.name);
        if (!wish.bookId.isEmpty())
            text += QStringLiteral("   ·  ") + tr("in %1").arg(wish.bookTitle);
        if (!wish.song.empty())
            text += QStringLiteral("   ·  ") + tr("in the library");
        auto* item = new QTreeWidgetItem(wishes_, {text});
        item->setData(0, wishRole, static_cast<int>(i));
        const QString file = wish.bookId.isEmpty() ? QStringLiteral("wishes.txt") : wish.bookId;
        item->setToolTip(0, tr("%1, line %2: %3").arg(file).arg(wish.line).arg(wish.text));
    }
    if (chosen)
        for (QTreeWidgetItemIterator it(wishes_); *it; ++it) {
            const int index = (*it)->data(0, wishRole).toInt();
            if (index >= 0 && wishList_[static_cast<std::size_t>(index)].bookId == chosen->first &&
                wishList_[static_cast<std::size_t>(index)].line == chosen->second) {
                wishes_->setCurrentItem(*it);
                break;
            }
        }
    if (wishList_.empty()) {
        auto* item = new QTreeWidgetItem(wishes_, {tr("No wishes: Add Wish… writes one.")});
        item->setData(0, wishRole, -1);
        item->setFlags(Qt::NoItemFlags);
    }
    updateWishButtons();
    stack_->setCurrentIndex(1);
    // Once the new lists are laid out (a value set before would be clamped to the old range).
    QTimer::singleShot(0, this, [this, scrolled, wishesScrolled] {
        wishes_->verticalScrollBar()->setValue(wishesScrolled);
        scroll_->verticalScrollBar()->setValue(scrolled);
    });
}

const HomeWish* HomePage::selectedWish() const {
    const QTreeWidgetItem* item = wishes_->currentItem();
    if (!item)
        return nullptr;
    const int index = item->data(0, wishRole).toInt();
    if (index < 0 || index >= static_cast<int>(wishList_.size()))
        return nullptr;
    return &wishList_[static_cast<std::size_t>(index)];
}

void HomePage::updateWishButtons() {
    const HomeWish* wish = selectedWish();
    const bool inLibrary = wish && !wish->song.empty();
    for (QPushButton* button : {wishSearch_, wishImport_, wishNew_}) {
        button->setVisible(!inLibrary);
        button->setEnabled(wish != nullptr);
    }
    wishOpen_->setVisible(inLibrary);
    wishDone_->setVisible(inLibrary);
    wishRemove_->setEnabled(wish != nullptr);
    wishMove_->setVisible(wish && !wish->bookId.isEmpty()); // a book's wish only
    wishMore_->setEnabled(wish != nullptr);
}

void HomePage::placeCards() {
    const int columns = width() >= 900 ? 2 : 1;
    // Recently added is hidden without dates: the cards left are paired so no half stays empty.
    const bool addedShown = !addedCard_->isHidden();
    const int key = columns * 2 + (addedShown ? 1 : 0);
    if (key == layoutKey_)
        return;
    layoutKey_ = key;
    for (QWidget* card : {static_cast<QWidget*>(openedCard_), static_cast<QWidget*>(addedCard_),
                          static_cast<QWidget*>(todoCard_), static_cast<QWidget*>(booksCard_),
                          static_cast<QWidget*>(wishesCard_)})
        grid_->removeWidget(card);
    std::vector<QWidget*> cards{openedCard_};
    if (addedShown)
        cards.push_back(addedCard_);
    cards.push_back(todoCard_);
    cards.push_back(booksCard_);
    int row = 0;
    int column = 0;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const bool last = i + 1 == cards.size();
        // the last card alone in its row takes both columns
        const int span = columns == 2 && last && column == 0 ? 2 : 1;
        grid_->addWidget(cards[i], row, column, 1, span);
        if (++column == columns || span == 2) {
            column = 0;
            ++row;
        }
    }
    if (column != 0)
        ++row;
    grid_->addWidget(wishesCard_, row, 0, 1, columns);
}

void HomePage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    placeCards();
}
