#include "SongbookCheck.hpp"

#include "SongActions.hpp"
#include "SongEditor.hpp"
#include "SongFilterProxy.hpp"
#include "SongListPage.hpp"
#include "SongTableModel.hpp"
#include "SongbookScan.hpp"
#include "convert.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QList>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <vector>
#include <zub/core/package.hpp>
#include <zub/core/preview.hpp>
#include <zub/core/songsuggest.hpp>

SongbookCheck::SongbookCheck(QWidget* window, SongTableModel* model, SongFilterProxy* proxy,
                             SongListPage* listPage, SongActions* actions, Hooks hooks)
    : QObject(window), window_(window), model_(model), proxy_(proxy), listPage_(listPage),
      actions_(actions), hooks_(std::move(hooks)) {}

void SongbookCheck::setSongbook(const std::filesystem::path& root,
                                const std::filesystem::path& packageDir) {
    songbookRoot_ = root;
    packageDir_ = packageDir;
}

void SongbookCheck::start() {
    if (scan_ || songbookRoot_.empty())
        return;
    const QString name = tr("Scan Songs");
    const std::filesystem::path converter = hooks_.findConverter();
    const auto paper = zub::preview::paperOf(packageDir_, hooks_.documentClass());
    const QString paperName = !paper                                  ? tr("its paper")
                              : *paper == zub::songsuggest::Paper::A5 ? QStringLiteral("A5")
                                                                      : QStringLiteral("A4");

    // The settings: what to look for, and in which songs.
    QDialog dialog(window_);
    dialog.setWindowTitle(name);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(
        tr("Look for songs where a song action would change something, or a check finds "
           "something. Nothing is changed: the songs are marked in the list (this session only), "
           "and you decide song by song."),
        &dialog));
    auto* tidy =
        new QCheckBox(tr("Tidy: a chorus typed again, chords inside a syllable, spaces"), &dialog);
    auto* check = new QCheckBox(
        tr("Find Mistakes: typos, apostrophes typed two ways, a chorus typed as a verse"), &dialog);
    auto* shrink = new QCheckBox(
        tr("Make Smaller: songs that don't fit %1 (the preview's paper). Typesets every song: "
           "about 2 seconds each, some 8 minutes for the whole song library.")
            .arg(paperName),
        &dialog);
    QSettings settings;
    tidy->setChecked(settings.value(QStringLiteral("scan/tidy"), true).toBool());
    check->setChecked(settings.value(QStringLiteral("scan/check"), true).toBool());
    shrink->setChecked(settings.value(QStringLiteral("scan/shrink"), false).toBool());
    // The package's scripts, once for the whole songbook (a second each).
    auto* keysMissing =
        new QCheckBox(tr("Keys: a key for the songs that have none (from their chords)"), &dialog);
    auto* keysStated = new QCheckBox(
        tr("Keys: written keys the chords disagree with (unless confirmed)"), &dialog);
    auto* metadata = new QCheckBox(
        tr("Metadata: the header's keys and values (missing tempo, meter …)"), &dialog);
    keysMissing->setChecked(settings.value(QStringLiteral("scan/keysMissing"), true).toBool());
    keysStated->setChecked(settings.value(QStringLiteral("scan/keysStated"), false).toBool());
    metadata->setChecked(settings.value(QStringLiteral("scan/metadata"), false).toBool());
    if (packageDir_.empty()) { // measuring typesets every song; the scripts are the package's
        for (QCheckBox* box : {shrink, keysMissing, keysStated, metadata}) {
            box->setChecked(false);
            box->setEnabled(false);
            box->setToolTip(hooks_.noPackageText());
        }
    }
    if (converter.empty()) {
        for (QCheckBox* box : {tidy, check, shrink}) {
            box->setChecked(false);
            box->setEnabled(false);
            box->setToolTip(tr("Needs chords_converter, which was not found: it is built with the "
                               "app (ZUB_BUILD_CONVERTER)."));
        }
    }
    for (QCheckBox* box : {tidy, check, shrink, keysMissing, keysStated, metadata})
        layout->addWidget(box);
    const int shown = proxy_->rowCount();
    const int all = model_->rowCount();
    auto* allSongs = new QRadioButton(tr("All %n song(s)", nullptr, all), &dialog);
    auto* shownSongs =
        new QRadioButton(tr("The %n song(s) shown in the list", nullptr, shown), &dialog);
    allSongs->setChecked(true);
    shownSongs->setEnabled(shown != all);
    layout->addWidget(allSongs);
    layout->addWidget(shownSongs);
    layout->addWidget(
        new QLabel(tr("Unsaved edits are not checked: the check reads the files."), &dialog));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    QPushButton* start = buttons->addButton(tr("Check"), QDialogButtonBox::AcceptRole);
    start->setDefault(true);
    const QList<QCheckBox*> boxes{tidy, check, shrink, keysMissing, keysStated, metadata};
    const auto updateStart = [=] {
        start->setEnabled(std::ranges::any_of(boxes, [](QCheckBox* b) { return b->isChecked(); }));
    };
    for (QCheckBox* box : boxes)
        connect(box, &QCheckBox::toggled, &dialog, updateStart);
    updateStart();
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return;
    // A box switched off for want of a package or a converter keeps the choice as it was.
    const std::pair<QCheckBox*, const char*> remembered[] = {{tidy, "scan/tidy"},
                                                             {check, "scan/check"},
                                                             {shrink, "scan/shrink"},
                                                             {keysMissing, "scan/keysMissing"},
                                                             {keysStated, "scan/keysStated"},
                                                             {metadata, "scan/metadata"}};
    for (const auto& [box, key] : remembered)
        if (box->isEnabled())
            settings.setValue(QString::fromLatin1(key), box->isChecked());
    if ((tidy->isChecked() || check->isChecked() || shrink->isChecked()) &&
        !actions_->converterUsable(converter, name))
        return;

    std::vector<SongbookScan::Song> songs;
    if (shownSongs->isChecked()) {
        for (int row = 0; row < proxy_->rowCount(); ++row) {
            if (const auto* song = model_->song(proxy_->mapToSource(proxy_->index(row, 0)).row()))
                songs.push_back({song->path, song->inputName});
        }
    } else {
        for (int row = 0; row < model_->rowCount(); ++row) {
            if (const auto* song = model_->song(row))
                songs.push_back({song->path, song->inputName});
        }
    }
    ScanOptions options;
    options.tidy = tidy->isChecked();
    options.check = check->isChecked();
    options.shrink = shrink->isChecked();
    options.keysMissing = keysMissing->isChecked();
    options.keysStated = keysStated->isChecked();
    options.metadata = metadata->isChecked();
    options.paper = paper;
    options.converter = converter;
    options.songbookRoot = songbookRoot_;
    options.packageDir = packageDir_;

    model_->clearScanResults();
    actions_->clearScanFindings();
    found_ = 0;
    scan_ = new SongbookScan(options, std::move(songs), this);
    auto* progress =
        new QProgressDialog(tr("Checking…"), tr("Cancel"), 0, scan_->stepCount(), window_);
    progress->setWindowTitle(name);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setMinimumWidth(480);
    connect(progress, &QProgressDialog::canceled, scan_, &SongbookScan::cancel);
    connect(scan_, &SongbookScan::progress, progress,
            [progress](int done, int, const QString& what) {
                progress->setValue(done);
                progress->setLabelText(what);
            });
    connect(scan_, &SongbookScan::songScanned, this,
            [this](const QString& path, const ScanFinding& finding) {
                QStringList summary;
                QStringList details;
                const auto section = [&details](const QString& title, const QStringList& lines) {
                    if (lines.isEmpty())
                        return;
                    details << title;
                    for (const QString& line : lines)
                        details << QStringLiteral("  • ") + line;
                };
                if (!finding.tidy.isEmpty())
                    summary << tr("Tidy %1").arg(finding.tidy.size());
                if (!finding.check.isEmpty())
                    summary << tr("Check %1").arg(finding.check.size());
                if (!finding.shrink.isEmpty())
                    summary << (finding.pages.isEmpty() ? tr("Too big")
                                                        : tr("Too big (%1)").arg(finding.pages));
                if (!finding.keys.isEmpty())
                    summary << tr("Key");
                // What it is, not just how many (the tooltip lists them all too).
                if (!finding.metadata.isEmpty())
                    summary << tr("Metadata: %1").arg(finding.metadata.join(QStringLiteral("; ")));
                if (!finding.errors.isEmpty())
                    summary << tr("Errors %1").arg(finding.errors.size());
                section(tr("Tidy:"), finding.tidy);
                section(tr("Find Mistakes:"), finding.check);
                section(finding.pages.isEmpty() ? tr("Make Smaller:")
                                                : tr("Make Smaller (%1):").arg(finding.pages),
                        finding.shrink);
                section(tr("Key:"), finding.keys);
                section(tr("Metadata:"), finding.metadata);
                section(tr("Problems:"), finding.errors);
                model_->setScanResult(toPath(path), summary.join(QStringLiteral(" · ")),
                                      details.join(QLatin1Char('\n')));
                ++found_;
            });
    connect(scan_, &SongbookScan::keyFound, this,
            [this](const QString& path, const zub::songcheck::KeyFinding& finding) {
                actions_->setKeyFinding(toPath(path), finding);
            });
    connect(scan_, &SongbookScan::metadataFound, this,
            [this](const QString& path, const std::vector<zub::songcheck::SongIssue>& issues) {
                actions_->setMetadataFinding(toPath(path), issues);
            });
    connect(scan_, &SongbookScan::problem, this,
            [this](const QString& message) { emit statusMessage(message, 15000); });
    connect(scan_, &SongbookScan::finished, this, [this, progress](bool cancelled) {
        progress->reset();
        progress->deleteLater();
        scan_->deleteLater();
        scan_ = nullptr;
        // Shown alone only when there is something to show: with nothing found, the filter
        // would hide every song and the list would look empty.
        listPage_->showScanResults(found_ > 0);
        if (SongEditor* editor = hooks_.openSong())
            actions_->offerFindings(editor); // the open song may have findings now
        emit statusMessage(
            (cancelled ? tr("Check cancelled. ") : QString()) +
                tr("%n song(s) with possible changes: open one to see them and decide.", nullptr,
                   found_),
            15000);
        emit finished(found_, cancelled);
    });
    scan_->start();
}
