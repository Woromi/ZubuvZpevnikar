// Song collections (the folders under songs/): the song list's tabs and the picker's tabs.
#include <QAbstractItemModel>
#include <QTabBar>
#include <QTableView>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>

#include "AppTestAccess.hpp"
#include "Support.hpp"

using A = AppTestAccess;

TEST_CASE("Collections: the song list has a tab per folder under songs/", "[app][collections]") {
    apptest::Fixture fixture;
    MainWindow w;
    w.resize(1400, 800);
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    CollectionTabs* tabs = A::collections(*A::listPage(w));
    REQUIRE(tabs->count() == 3); // All, chords, whistle2
    CHECK(tabs->isVisible());
    CHECK(tabs->tabText(0) == "All");
    CHECK(tabs->tabText(1) == "Chords");
    CHECK(tabs->tabText(2) == "whistle2");

    auto* table = A::listPage(w)->table();
    const auto rows = [&](int tab) {
        tabs->setCurrentIndex(tab);
        return table->model()->rowCount();
    };
    const int all = rows(0);
    const int chords = rows(1);
    const int whistle = rows(2);
    CHECK(all == 8);
    CHECK(whistle == 1);
    CHECK(chords + whistle == all);
}

TEST_CASE("Collections: a book inputting a whistle song does not miss it", "[app][collections]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    MainWindow w;
    w.resize(1400, 800);
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    A::showView(w, ActivityBar::View::Songbooks);
    SongbooksPage* page = A::songbooksPage(w);
    A::list(*page)->setCurrentItem(A::listItemOf(*page, "songbooks/whistle/whistle.tex"));
    REQUIRE(A::id(*page) == "songbooks/whistle/whistle.tex");

    int songs = 0, missing = 0;
    for (QTreeWidgetItemIterator it(A::tree(*page)); *it; ++it) {
        if (A::tree(*page)->isSection(*it))
            continue;
        ++songs;
        if ((*it)->text(0).contains("not in the songbook"))
            ++missing;
    }
    CHECK(songs == 1);
    CHECK(missing == 0);
}
