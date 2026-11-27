// Small dialogs: the Make Smaller status line, Add to Songbooks' title and reject button.
#include "AddToBooksDialog.hpp"
#include "ShrinkDialog.hpp"
#include "Support.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
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
    CHECK(ShrinkDialog::statusLine(*result) ==
          QStringLiteral("A4 ✓ 1 page   ·   A5 ✗ 2 pages   ·   Too wide: lines 45, 60, 61, 66"));
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
