// The git dates of the songs: the Added column, the Recently added view of the song list and the
// "Only recently added" filter of the Songbooks picker. The app tests run without git (PATH is
// emptied), so the dates are injected into the model; one hidden [.tools] case runs the real git.
#include "AppTestAccess.hpp"
#include "SongFilterProxy.hpp"
#include "SongTableModel.hpp"
#include "Support.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QTableView>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <map>

using A = AppTestAccess;
namespace sd = zub::songdates;

namespace {

sd::Dates daysAgo(int days) {
    sd::Dates dates;
    dates.added = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) -
                  std::chrono::days{days};
    dates.edited = dates.added;
    return dates;
}

// Row 0 old, row 1 recent, row 2 new and not committed, the others unknown.
struct DatesWindow {
    explicit DatesWindow(apptest::Fixture& fixture) {
        window.resize(1400, 800);
        window.show();
        REQUIRE(A::openSongbook(window, fixture.root()));
        model = A::model(window);
        REQUIRE(model->rowCount() >= 4);
        root = fixture.root();
    }
    void inject() {
        std::map<std::string, sd::Dates> dates;
        const auto key = [&](int row) {
            return model->song(row)->path.lexically_relative(root).generic_string();
        };
        dates[key(0)] = daysAgo(100);
        dates[key(1)] = daysAgo(5);
        sd::Dates fresh;
        fresh.uncommitted = true;
        dates[key(2)] = fresh;
        model->setDates(root, dates);
    }
    MainWindow window;
    SongTableModel* model = nullptr;
    std::filesystem::path root;
};

} // namespace

TEST_CASE("Song dates: the model's roles and the Added column", "[app][dates]") {
    apptest::Fixture fixture;
    DatesWindow w(fixture);
    auto* model = w.model;
    CHECK_FALSE(model->hasDates());
    CHECK_FALSE(model->index(0, SongTableModel::Added).data(SongTableModel::RecentRole).toBool());

    w.inject();
    CHECK(model->hasDates());
    const auto at = [&](int row, int role) {
        return model->index(row, SongTableModel::Added).data(role);
    };
    CHECK(at(0, SongTableModel::AddedRole).toDateTime().isValid());
    CHECK_FALSE(at(0, SongTableModel::RecentRole).toBool());
    CHECK(at(1, SongTableModel::RecentRole).toBool());
    CHECK(at(2, SongTableModel::RecentRole).toBool());
    CHECK_FALSE(at(2, SongTableModel::AddedRole).toDateTime().isValid());
    CHECK(at(2, Qt::DisplayRole).toString() == QStringLiteral("new"));
    CHECK_FALSE(at(3, SongTableModel::RecentRole).toBool());
    CHECK(at(3, Qt::DisplayRole).toString().isEmpty());
    // new sorts as the newest, unknown as the oldest
    CHECK(at(2, SongTableModel::SortRole).toLongLong() >
          at(1, SongTableModel::SortRole).toLongLong());
    CHECK(at(1, SongTableModel::SortRole).toLongLong() >
          at(0, SongTableModel::SortRole).toLongLong());
    CHECK(at(0, SongTableModel::SortRole).toLongLong() >
          at(3, SongTableModel::SortRole).toLongLong());

    // an edit keeps the dates; a song saved for the first time is new and not committed
    const auto* first = model->song(1);
    CHECK(model->updateSong(first->path, first->meta, first->todos, first->scores));
    CHECK(at(1, SongTableModel::RecentRole).toBool());
    CHECK(at(1, SongTableModel::AddedRole).toDateTime().isValid());
    zub::SongEntry entry = *model->song(3);
    entry.path = w.root / "songs" / "chords" / "Test" / "Brand.tex";
    entry.inputName = "Test/Brand";
    model->addSong(entry);
    CHECK(model->index(model->rowCount() - 1, 0).data(SongTableModel::RecentRole).toBool());

    // a reload forgets them until git answers again
    w.model->clearDates();
    CHECK_FALSE(model->hasDates());
    CHECK_FALSE(at(1, SongTableModel::RecentRole).toBool());
}

TEST_CASE("Song dates: the Recently added choice of the song list", "[app][dates]") {
    apptest::Fixture fixture;
    DatesWindow w(fixture);
    SongListPage* page = A::listPage(w.window);
    QComboBox* show = A::show(*page);
    QTableView* table = page->table();
    const int recent = show->findText(QStringLiteral("Recently added"));
    REQUIRE(recent >= 0);
    const auto itemEnabled = [&] {
        return show->model()->flags(show->model()->index(recent, 0)) & Qt::ItemIsEnabled;
    };
    CHECK_FALSE(itemEnabled()); // no dates yet
    CHECK(table->isColumnHidden(SongTableModel::Added));

    w.inject();
    CHECK(itemEnabled());
    table->sortByColumn(SongTableModel::Artist, Qt::DescendingOrder);
    const int all = table->model()->rowCount();

    show->setCurrentIndex(recent);
    CHECK_FALSE(table->isColumnHidden(SongTableModel::Added));
    REQUIRE(table->model()->rowCount() == 2);
    // newest first: the song not committed yet, then the one of 5 days ago
    CHECK(table->model()
              ->index(0, SongTableModel::Added)
              .data(SongTableModel::InputNameRole)
              .toString() == QString::fromStdString(w.model->song(2)->inputName));
    CHECK(table->model()->index(1, 0).data(SongTableModel::InputNameRole).toString() ==
          QString::fromStdString(w.model->song(1)->inputName));
    CHECK(table->horizontalHeader()->sortIndicatorSection() == SongTableModel::Added);

    show->setCurrentIndex(0);
    CHECK(table->isColumnHidden(SongTableModel::Added));
    CHECK(table->model()->rowCount() == all);
    CHECK(table->horizontalHeader()->sortIndicatorSection() == SongTableModel::Artist);
    CHECK(table->horizontalHeader()->sortIndicatorOrder() == Qt::DescendingOrder);
}

TEST_CASE("Song dates: Recently added in the Songbooks picker's Show choice",
          "[app][dates][songbooks]") {
    apptest::Fixture fixture;
    DatesWindow w(fixture);
    A::showView(w.window, ActivityBar::View::Songbooks);
    SongbooksPage* page = A::songbooksPage(w.window);
    QComboBox* show = A::pickerShow(*page);
    REQUIRE(show != nullptr);
    CHECK(show->objectName() == "pickerShow");
    const int recent = show->findText("Recently added");
    REQUIRE(recent >= 0);
    const auto enabled = [&] {
        return qobject_cast<QStandardItemModel*>(show->model())->item(recent)->isEnabled();
    };
    CHECK_FALSE(enabled()); // no dates yet

    w.inject();
    CHECK(enabled());
    SongFilterProxy* filter = A::pickerFilter(*page);
    const int all = filter->rowCount();
    CHECK(all >= 4);
    show->setCurrentIndex(recent);
    CHECK(filter->rowCount() == 2);
    show->setCurrentIndex(0);
    CHECK(filter->rowCount() == all);
}

// Real git on a copy of the fixture: old commits, a recent one, an untracked song.
TEST_CASE("Song dates: read from git", "[.tools][app][dates]") {
    if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty())
        SKIP("git is not installed");
    apptest::Fixture fixture;
    const QString dir = QString::fromStdString(fixture.root().string());
    const auto git = [&](const QStringList& arguments, const QString& when) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        if (!when.isEmpty()) {
            environment.insert(QStringLiteral("GIT_AUTHOR_DATE"), when);
            environment.insert(QStringLiteral("GIT_COMMITTER_DATE"), when);
        }
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(dir);
        process.start(QStringLiteral("git"),
                      QStringList{QStringLiteral("-c"), QStringLiteral("user.name=T"),
                                  QStringLiteral("-c"), QStringLiteral("user.email=t@example.org"),
                                  QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false")} +
                          arguments);
        REQUIRE(process.waitForFinished(20000));
        INFO(process.readAllStandardError().toStdString());
        REQUIRE(process.exitCode() == 0);
    };
    const auto song = [&](const char* name) {
        const auto target = fixture / "songs" / "chords" / "Test" / name;
        std::filesystem::create_directories(target.parent_path());
        apptest::writeFile(
            target, apptest::slurp(fixture / "songs" / "chords" / "Abba" / "OurLastSummer.tex"));
    };
    git({"init", "-q"}, {});
    git({"add", "songs"}, {});
    git({"commit", "-q", "-m", "old"}, QStringLiteral("2020-01-02T10:00:00"));
    song("Recent.tex");
    git({"add", "songs"}, {});
    git({"commit", "-q", "-m", "recent"},
        QDateTime::currentDateTime().addDays(-3).toString(Qt::ISODate));
    song("Untracked.tex");

    MainWindow window;
    REQUIRE(A::openSongbook(window, fixture.root()));
    auto* model = A::model(window);
    REQUIRE(apptest::waitUntil([&] { return model->hasDates(); }, 20000));

    const auto row = [&](const char* inputName) {
        for (int i = 0; i < model->rowCount(); ++i)
            if (model->song(i)->inputName == inputName)
                return i;
        FAIL("no song " << inputName);
        return -1;
    };
    const auto recentOf = [&](int r) {
        return model->index(r, 0).data(SongTableModel::RecentRole).toBool();
    };
    const int old = row("Abba/OurLastSummer");
    CHECK(model->index(old, 0).data(SongTableModel::AddedRole).toDateTime().date().year() == 2020);
    CHECK_FALSE(recentOf(old));
    const int added = row("Test/Recent");
    CHECK(recentOf(added));
    CHECK(model->index(added, 0).data(SongTableModel::AddedRole).toDateTime().isValid());
    const int fresh = row("Test/Untracked");
    CHECK(recentOf(fresh));
    CHECK_FALSE(model->index(fresh, 0).data(SongTableModel::AddedRole).toDateTime().isValid());
    CHECK(model->index(fresh, SongTableModel::Added).data().toString() == QStringLiteral("new"));
}

TEST_CASE("Song dates: Recently added filters nothing while no dates are known", "[app][dates]") {
    SongTableModel model;
    SongFilterProxy proxy;
    proxy.setSourceModel(&model);
    model.setSongs({zub::SongEntry{"/sb/songs/chords/A/B.tex", "A/B", {}, {}, {}, "chords"}});
    proxy.setRecentOnly(true);
    // No git: the choice cannot hide every song behind a greyed-out control.
    CHECK(proxy.rowCount() == 1);
}
