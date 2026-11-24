// MainWindow: the views, the package lookup, the language filter and the papers offer.
#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QStandardPaths>
#include <QLabel>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>
#include <catch2/catch_test_macros.hpp>
#include <zub/core/songheader.hpp>

#include "AppTestAccess.hpp"
#include "BuildFiles.hpp"
#include "DesktopIntegration.hpp"
#include "PreviewRunner.hpp"
#include "SettingsDialog.hpp"
#include "SongFilterProxy.hpp"
#include "SongTableModel.hpp"
#include "Support.hpp"

using apptest::slurp;
using A = AppTestAccess;
using V = ActivityBar::View;

TEST_CASE("Views: the open song stays open while the other pages are shown", "[app][views]") {
    apptest::Fixture fixture;
    MainWindow w;
    w.resize(1300, 560);
    w.show();
    w.activateWindow(); // the Ctrl+2 shortcut needs the active window
    REQUIRE(QTest::qWaitForWindowActive(&w));
    REQUIRE(A::openSongbook(w, fixture.root()));
    ActivityBar* bar = A::activityBar(w);
    CHECK(!bar->button(V::Song)->isEnabled());

    A::openSong(w, A::proxy(w)->index(0, 0));
    CHECK(A::pages(w)->currentWidget() == A::songPage(w));
    CHECK(bar->button(V::Song)->isEnabled());
    CHECK(bar->button(V::Song)->isChecked());
    SongEditor* editor = A::editor(w);
    REQUIRE(editor != nullptr);
    editor->textEdit()->insertPlainText("% edit\n");

    // To the list: the song stays open, edited.
    bar->button(V::Songs)->click();
    CHECK(A::pages(w)->currentWidget() == A::listPage(w));
    CHECK(A::editor(w) == editor);
    CHECK(editor->isModified());

    // The same song from the list: shown again, not reloaded.
    A::openSong(w, A::proxy(w)->index(0, 0));
    CHECK(A::editor(w) == editor);
    CHECK(A::pages(w)->currentWidget() == A::songPage(w));

    // Songbooks with the song open, and back with Ctrl+2.
    bar->button(V::Songbooks)->click();
    CHECK(A::pages(w)->currentWidget() == static_cast<QWidget*>(A::songbooksPage(w)));
    QTest::keyClick(&w, Qt::Key_2, Qt::ControlModifier);
    CHECK(A::pages(w)->currentWidget() == A::songPage(w));

    // Closing asks (modified); Discard goes to the list with the song selected.
    {
        auto answer = apptest::answerMessageBox(QMessageBox::Discard);
        CHECK(A::closeSong(w));
        REQUIRE(answer.fired());
    }
    CHECK(A::editor(w) == nullptr);
    CHECK(A::pages(w)->currentWidget() == A::listPage(w));
    CHECK(!bar->button(V::Song)->isEnabled());
    CHECK(bar->button(V::Songs)->isChecked());
    CHECK(A::listPage(w)->table()->currentIndex().row() == 0);

    // The settings dialog opens and closes.
    auto settings = apptest::answerDialog<SettingsDialog>([](SettingsDialog* d) { d->reject(); });
    A::openSettings(w);
    CHECK(settings.fired());
}

TEST_CASE("Package: a songbook without its own package builds with the bundled copy",
          "[app][package]") {
    apptest::Fixture fixture;
    MainWindow w;
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    // The fixture's zubuvzpevnik/ has no zubuvzpevnik.sty: not a package checkout. Without the
    // submodule checked out the app has no copy either.
    if (A::packageDir(w).empty())
        SKIP("the bundled package (external/zubuvzpevnik-package) is not checked out");
    CHECK(A::packageDir(w).filename() == "zubuvzpevnik-package");

    // A songbook with its own package uses that one.
    apptest::writeFile(fixture / "zubuvzpevnik/zubuvzpevnik.sty", "x");
    apptest::writeFile(fixture / "zubuvzpevnik/scripts/build.sh", "x");
    REQUIRE(A::openSongbook(w, fixture.root()));
    CHECK(A::packageDir(w) == fixture / "zubuvzpevnik");
}

TEST_CASE("Language filter: a song listing several languages is shown under each", "[app][language]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    MainWindow w;
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    QComboBox* combo = A::language(*A::listPage(w));
    QStringList items;
    for (int i = 0; i < combo->count(); ++i)
        items << combo->itemText(i);
    INFO(items.join(", ").toStdString());
    CHECK(items.contains("hebrew"));
    CHECK(items.contains("czech"));

    const auto shown = [&](const QString& lang) {
        combo->setCurrentIndex(combo->findData(lang));
        QStringList titles;
        for (int r = 0; r < A::proxy(w)->rowCount(); ++r)
            titles << A::proxy(w)->index(r, SongTableModel::Title).data().toString() + " [" +
                          A::proxy(w)->index(r, SongTableModel::Language).data().toString() + "]";
        return titles;
    };
    const QStringList hebrew = shown("hebrew");
    REQUIRE(hebrew.size() == 1);
    CHECK(hebrew[0] == "Bilingual [czech, hebrew]");
    const QStringList czech = shown("czech");
    CHECK(czech.contains("Bilingual [czech, hebrew]"));
    CHECK(czech.size() > 1);
}

TEST_CASE("Papers: the bar offers the measured papers; Mark writes them, typing closes it",
          "[app][papers]") {
    apptest::Fixture fixture(apptest::Fixture::Extras::Yes);
    MainWindow w;
    w.resize(1200, 800);
    w.show();
    REQUIRE(A::openSongbook(w, fixture.root()));
    REQUIRE(A::openExistingSong(w, fixture / "songs/chords/Test/En.tex"));
    SongEditor* ed = A::editor(w);
    REQUIRE(ed != nullptr);
    const std::string text = ed->content();
    REQUIRE(!zub::songheader::option(text, "pap").has_value());

    zub::songsuggest::Suggestions fits;
    fits.fits = true;
    fits.a4 = zub::songsuggest::Measured{1, true, {}, 0};
    fits.a5 = zub::songsuggest::Measured{1, true, {}, 0};
    A::offerPapers(w, ed, text, fits);
    CHECK(ed->suggestionShown());
    CHECK(!ed->textEdit()->isReadOnly());

    // The text changed since the measurement: no bar.
    ed->hideSuggestion();
    A::offerPapers(w, ed, text + "x", fits);
    CHECK(!ed->suggestionShown());

    A::offerPapers(w, ed, text, fits);
    auto* button = ed->findChild<QWidget*>("suggestionBar")->findChild<QPushButton*>();
    REQUIRE(button != nullptr);
    CHECK(button->text() == "Mark");
    QTest::mouseClick(button, Qt::LeftButton);
    CHECK(!ed->suggestionShown());
    const std::string after = ed->content();
    CHECK(zub::songheader::option(after, "pap") == "a4,a5");
    ed->textEdit()->undo(); // one undo step
    CHECK(ed->content() == text);

    // Typing closes the bar.
    A::offerPapers(w, ed, text, fits);
    CHECK(ed->suggestionShown());
    ed->textEdit()->insertPlainText("z");
    CHECK(!ed->suggestionShown());
    ed->textEdit()->undo();

    ed->textEdit()->document()->setModified(false);

    // The song is marked a4,a5 and a5 no longer fits: the bar offers to take it off.
    REQUIRE(A::openExistingSong(w, fixture / "songs/chords/Abba/OurLastSummer.tex"));
    ed = A::editor(w);
    const std::string marked = ed->content();
    REQUIRE(zub::songheader::option(marked, "pap") == "a4,a5");
    zub::songsuggest::Suggestions tooLong;
    tooLong.a5 = zub::songsuggest::Measured{2, false, {}, 0};
    A::offerPapers(w, ed, marked, tooLong);
    REQUIRE(ed->suggestionShown());
    auto* update = ed->findChild<QWidget*>("suggestionBar")->findChild<QPushButton*>();
    CHECK(update->text() == "Update");
    QTest::mouseClick(update, Qt::LeftButton);
    CHECK(zub::songheader::option(ed->content(), "pap") == "a4");
    ed->textEdit()->document()->setModified(false);
}

TEST_CASE("Build files: kept by default; Settings can drop them after every build",
          "[app][buildfiles]") {
    apptest::Fixture fixture;
    MainWindow w;
    REQUIRE(A::openSongbook(w, fixture.root()));
    CHECK(buildfiles::keep());
    apptest::KeepBuildFiles restore;
    const auto scores = A::previewWorkDir(w) / "tmp-ly";
    const auto book = std::filesystem::path(buildfiles::songbooksDir().toStdString()) / "repo-1";
    const auto makeFiles = [&] {
        apptest::writeFile(scores / "abc.pdf", "score");
        apptest::writeFile(book / "tmp-ly/abc.pdf", "book score");
    };
    makeFiles();

    // Kept: a preview build (no lualatex here: it does not start) leaves them.
    REQUIRE(A::openExistingSong(w, fixture / "songs/chords/Abba/OurLastSummer.tex"));
    REQUIRE(apptest::waitUntil(
        [&] { return A::status(*A::preview(w))->text().contains(QStringLiteral("failed")); },
        5000));
    CHECK(std::filesystem::exists(scores / "abc.pdf"));

    // The dialog shows their size; choosing "build everything again" deletes them.
    QString sizeText;
    const QString size = QLocale().formattedDataSize(A::buildFilesSize(w));
    auto settings = apptest::answerDialog<SettingsDialog>([&](SettingsDialog* d) {
        sizeText = d->findChild<QLabel*>(QStringLiteral("settingsBuildFilesSize"))->text();
        d->findChild<QRadioButton*>(QStringLiteral("settingsRebuildAll"))->setChecked(true);
        d->accept();
    });
    A::openSettings(w);
    REQUIRE(settings.fired());
    INFO(sizeText.toStdString());
    CHECK(sizeText.contains(size));
    CHECK(!buildfiles::keep());
    CHECK(!std::filesystem::exists(scores));
    CHECK(!std::filesystem::exists(book));

    // From now on a preview build drops the scores when it ends.
    apptest::writeFile(scores / "abc.pdf", "score");
    A::requestPreview(w);
    CHECK(apptest::waitUntil([&] { return !std::filesystem::exists(scores); }, 5000));

    // Delete Now, back to keeping them.
    makeFiles();
    auto again = apptest::answerDialog<SettingsDialog>([&](SettingsDialog* d) {
        auto* now = d->findChild<QPushButton*>(QStringLiteral("settingsDeleteBuildFiles"));
        CHECK(now->isEnabled());
        now->click();
        d->findChild<QRadioButton*>(QStringLiteral("settingsKeepBuilds"))->setChecked(true);
        d->accept();
    });
    A::openSettings(w);
    REQUIRE(again.fired());
    CHECK(!std::filesystem::exists(scores));
    CHECK(!std::filesystem::exists(book));
    CHECK(buildfiles::keep());
}

TEST_CASE("Build files: only the app's cache folders are deleted", "[app][buildfiles]") {
    apptest::Fixture fixture;
    MainWindow w;
    const auto cache = std::filesystem::path(buildfiles::cacheDir().toStdString());
    // Accepted: a tmp-ly in the cache, a repo-* in songbooks/.
    apptest::writeFile(cache / "preview/tmp-ly/a", "a");
    CHECK(buildfiles::remove(QString::fromStdString((cache / "preview/tmp-ly").string())));
    CHECK(!std::filesystem::exists(cache / "preview/tmp-ly"));
    const auto book = cache / "songbooks/repo-abc";
    apptest::writeFile(book / "book.tex", "x");
    CHECK(buildfiles::remove(QString::fromStdString(book.string())));
    CHECK(!std::filesystem::exists(book));
    // Links into the cache that point elsewhere are refused.
    const auto target = fixture / "target";
    apptest::writeFile(target / "keep", "k");
    std::filesystem::create_directory_symlink(target, cache / "preview/tmp-ly");
    std::filesystem::create_directory_symlink(target, cache / "songbooks/repo-link");
    CHECK(!buildfiles::remove(QString::fromStdString((cache / "preview/tmp-ly").string())));
    CHECK(!buildfiles::remove(QString::fromStdString((cache / "songbooks/repo-link").string())));
    CHECK(std::filesystem::exists(target / "keep"));
    // A repo-* deeper than songbooks/ is no work dir.
    apptest::writeFile(cache / "songbooks/x/repo-y/f", "f");
    CHECK(!buildfiles::remove(QString::fromStdString((cache / "songbooks/x/repo-y").string())));
    const auto elsewhere = fixture / "tmp-ly";
    apptest::writeFile(elsewhere / "x", "x");
    CHECK(!buildfiles::remove(QString::fromStdString(elsewhere.string())));
    CHECK(std::filesystem::exists(elsewhere / "x"));
    const auto other = std::filesystem::path(buildfiles::cacheDir().toStdString()) / "playback";
    apptest::writeFile(other / "x.wav", "x");
    CHECK(!buildfiles::remove(QString::fromStdString(other.string())));
    CHECK(std::filesystem::exists(other / "x.wav"));
}

TEST_CASE("Converter: ZUB_CONVERTER is the only way to choose one; Settings has no choice",
          "[app][converter]") {
    apptest::Fixture fixture;
    // A remembered converter of older versions is forgotten.
    QSettings().setValue("converter/path", "/usr/bin/true");
    MainWindow w;
    CHECK(!QSettings().contains("converter/path"));
    CHECK(A::findConverter(w).empty()); // empty PATH, nothing next to the test binary

    const auto fake = fixture / "bin/chords_converter";
    apptest::writeFile(fake, "#!/bin/sh\n");
    std::filesystem::permissions(fake, std::filesystem::perms::owner_all);
    qputenv("ZUB_CONVERTER", QFile::encodeName(QString::fromStdString(fake.string())));
    CHECK(A::findConverter(w) == fake);
    qputenv("ZUB_CONVERTER", "relative/chords_converter"); // only an absolute path counts
    CHECK(A::findConverter(w).empty());
    qunsetenv("ZUB_CONVERTER");

    bool noConverterRow = false;
    auto settings = apptest::answerDialog<SettingsDialog>([&](SettingsDialog* d) {
        noConverterRow = d->findChild<QWidget*>(QStringLiteral("settingsConverter")) == nullptr;
        d->reject();
    });
    A::openSettings(w);
    REQUIRE(settings.fired());
    CHECK(noConverterRow);
}

TEST_CASE("Desktop: an AppImage puts itself into the application menu", "[app][desktop]") {
    apptest::Fixture fixture;
    namespace di = desktopintegration;
    const QString app = QStringLiteral(ZUB_TEST_DATA_DIR "/../../app");
    di::Source source{app + QStringLiteral("/zpevnikar.desktop"),
                      {{48, app + QStringLiteral("/icons/zpevnikar-48.png")},
                       {256, app + QStringLiteral("/icons/zpevnikar-256.png")}}};
    const auto image = fixture / "Apps/Zpevnikar-1.0-x86_64.AppImage";
    apptest::writeFile(image, "not really an image");
    const QString imagePath = toQString(image);
    const auto entry = [] { return slurp(toPath(di::entryPath())); };

    CHECK(di::install(imagePath, source) == di::Result::Added);
    CHECK(entry().contains("Exec=\"" + image.generic_string() + "\"\n"));
    CHECK(entry().contains("Name=Zpěvníkář\n"));
    const QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    CHECK(QFileInfo(data + "/icons/hicolor/256x256/apps/zpevnikar.png").isFile());
    CHECK(di::install(imagePath, source) == di::Result::Unchanged);
    // Moved or updated: the entry follows it.
    const auto moved = fixture / "Zpevnikar-1.1-x86_64.AppImage";
    apptest::writeFile(moved, "newer");
    CHECK(di::install(toQString(moved), source) == di::Result::Updated);
    CHECK(entry().contains("TryExec=" + moved.generic_string() + "\n"));
    CHECK(di::remove());
    CHECK_FALSE(QFileInfo::exists(di::entryPath()));

    // An entry installed otherwise is left alone, also by remove().
    apptest::writeFile(toPath(di::entryPath()), "[Desktop Entry]\nExec=zpevnikar\n");
    CHECK(di::install(imagePath, source) == di::Result::NotOurs);
    CHECK_FALSE(di::remove());
    CHECK(entry() == "[Desktop Entry]\nExec=zpevnikar\n");

    // The Settings choice is shown only when running as an AppImage.
    MainWindow w;
    bool shown = true;
    auto settings = apptest::answerDialog<SettingsDialog>([&](SettingsDialog* d) {
        shown = d->findChild<QCheckBox*>(QStringLiteral("settingsMenuEntry"))->isVisibleTo(d);
        d->reject();
    });
    qunsetenv("APPIMAGE");
    A::openSettings(w);
    REQUIRE(settings.fired());
    CHECK_FALSE(shown);
}
