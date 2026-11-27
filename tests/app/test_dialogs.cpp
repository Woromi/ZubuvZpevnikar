// Small dialogs: the Make Smaller status line, Add to Songbooks' title and reject button.
#include "AddToBooksDialog.hpp"
#include "ShortcutsDialog.hpp"
#include "ShrinkDialog.hpp"
#include "Support.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QPushButton>
#include <QTreeWidget>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <zub/core/songsuggest.hpp>

namespace {
const std::filesystem::path converterData =
    std::filesystem::path(ZUB_TEST_DATA_DIR) / "converter";
} // namespace

TEST_CASE("Make Smaller: one status line, the note in its tooltip", "[app][dialogs]") {
    const auto result = zub::songsuggest::interpretProposals(
        0, apptest::slurp(converterData / "shrink.json"),
        apptest::slurp(converterData / "shrink.err"));
    REQUIRE(result);
    // (no translation loaded here: the plural reads "page(s)")
    const QString line = ShrinkDialog::statusLine(*result);
    CHECK(line.startsWith(QStringLiteral("A4 ✓ 1 page")));
    CHECK(line.contains(QStringLiteral("   ·   A5 ✗ 2 page")));
    CHECK(line.endsWith(QStringLiteral("   ·   Too wide: lines 45, 60, 61, 66")));
    CHECK_FALSE(line.contains(QLatin1Char('\n')));
    CHECK(ShrinkDialog::statusTooltip(*result) ==
          QStringLiteral("It fits A4: the changes for small paper only."));

    ShrinkDialog dialog(*result, std::string(), QStringLiteral("Make Smaller"),
                        QStringLiteral("a long heading"));
    auto* label = dialog.findChild<QLabel*>(QStringLiteral("shrinkStatus"));
    REQUIRE(label);
    CHECK(label->text() == ShrinkDialog::statusLine(*result));
    auto* list = dialog.findChild<QListWidget*>();
    REQUIRE(list);
    REQUIRE(list->count() == 2);
    CHECK_FALSE(list->item(0)->text().contains(QStringLiteral("poor")));
    CHECK_FALSE(list->item(0)->text().startsWith(QStringLiteral("48")));
    CHECK(list->item(1)->text().startsWith(QStringLiteral("last resort: ")));
}

TEST_CASE("Add to Songbooks: the title names the song; Cancel, Not Now after a first save",
          "[app][dialogs]") {
    apptest::Fixture fixture;
    const auto rejectText = [](AddToBooksDialog& d) {
        auto* box = d.findChild<QDialogButtonBox*>();
        for (auto* b : box->buttons())
            if (box->buttonRole(b) == QDialogButtonBox::RejectRole)
                return b->text();
        return QString();
    };
    AddToBooksDialog plain(fixture.root(), "Test/Song", QStringLiteral("My Song"), QString());
    CHECK(plain.windowTitle() == QStringLiteral("Add My Song to Songbooks"));
    CHECK(rejectText(plain) == QStringLiteral("Cancel"));
    AddToBooksDialog fresh(fixture.root(), "Test/Song", QStringLiteral("My Song"), QString(),
                           nullptr, true);
    CHECK(rejectText(fresh) == QStringLiteral("Not Now"));
}

TEST_CASE("Keyboard Shortcuts: the filter hides rows and headings without a match",
          "[app][dialogs]") {
    QMenuBar menus;
    ShortcutsDialog dialog(&menus);
    auto* filter = dialog.findChild<QLineEdit*>(QStringLiteral("shortcutsFilter"));
    auto* tree = dialog.findChild<QTreeWidget*>(QStringLiteral("shortcutsTree"));
    REQUIRE(filter);
    REQUIRE(tree);
    const auto visible = [tree] {
        QStringList texts;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (!tree->topLevelItem(i)->isHidden())
                texts << tree->topLevelItem(i)->text(0);
        return texts;
    };
    const auto all = visible();
    filter->setText(QStringLiteral("alt+n"));
    const auto some = visible();
    REQUIRE(some.size() == 2); // the Score tab heading and its one row
    CHECK(some.back() == QStringLiteral("Insert the notes picked on the fretboard"));
    filter->setText(QStringLiteral("no such shortcut"));
    CHECK(visible().isEmpty());
    filter->clear();
    CHECK(visible() == all);
}
