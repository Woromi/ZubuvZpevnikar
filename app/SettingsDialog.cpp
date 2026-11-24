#include "SettingsDialog.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {

QLineEdit* pathLine(QWidget* parent) {
    auto* line = new QLineEdit(parent);
    line->setReadOnly(true);
    line->setMinimumWidth(380);
    return line;
}

QWidget* row(QWidget* parent, std::initializer_list<QWidget*> widgets) {
    auto* box = new QWidget(parent);
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    for (QWidget* widget : widgets)
        layout->addWidget(widget, widget == *widgets.begin() ? 1 : 0);
    return box;
}

} // namespace

SettingsDialog::SettingsDialog(const State& state, QWidget* parent)
    : QDialog(parent), state_(state), soundfont_(state.soundfont) {
    setWindowTitle(tr("Settings"));
    auto* form = new QFormLayout;

    // The language names are not translated: everybody must find their own language.
    language_ = new QComboBox(this);
    language_->setObjectName(QStringLiteral("settingsLanguage"));
    language_->addItem(tr("System Default"), QString());
    language_->addItem(QStringLiteral("Čeština"), QStringLiteral("cs"));
    language_->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language_->setCurrentIndex(std::max(0, language_->findData(state.language)));
    auto* languageNote = new QLabel(
        tr("Applies when %1 is started again.").arg(QApplication::applicationDisplayName()), this);
    form->addRow(tr("&Language:"), row(this, {language_, languageNote}));

    soundfontLine_ = pathLine(this);
    soundfontLine_->setObjectName(QStringLiteral("settingsSoundfont"));
    auto* chooseSoundfont = new QPushButton(tr("Choose…"), this);
    automatic_ = new QPushButton(tr("Automatic"), this);
    automatic_->setToolTip(tr("The first General MIDI soundfont found in the usual places"));
    connect(chooseSoundfont, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose a General MIDI soundfont"),
            soundfont_.isEmpty() ? QStringLiteral("/usr/share/sounds/sf2")
                                 : QFileInfo(soundfont_).absolutePath(),
            tr("Soundfonts (*.sf2 *.sf3)"));
        if (path.isEmpty())
            return;
        soundfontChanged_ = true;
        soundfont_ = path;
        showSoundfont(path, true);
    });
    connect(automatic_, &QPushButton::clicked, this, [this] {
        soundfontChanged_ = true;
        soundfont_.clear();
        showSoundfont({}, false);
    });
    form->addRow(tr("&Soundfont:"), row(this, {soundfontLine_, chooseSoundfont, automatic_}));
    showSoundfont(state.soundfont, state.soundfontChosen);

    // Only an AppImage puts itself into the menu (an installed app has its entry already).
    menuEntry_ = new QCheckBox(tr("Show Zpěvníkář in the application menu"), this);
    menuEntry_->setObjectName(QStringLiteral("settingsMenuEntry"));
    menuEntry_->setToolTip(tr("Its launcher entry and icon in ~/.local/share, following the "
                              "AppImage when it is moved or updated"));
    menuEntry_->setChecked(state.menuEntry);
    if (state.appImage)
        form->addRow(tr("AppImage:"), menuEntry_);
    else
        menuEntry_->hide();

    // Build files: LilyPond's engraved scores and the songbooks' work dirs in the cache.
    keepBuilds_ = new QRadioButton(
        tr("&Keep them: a rebuild engraves only the scores that changed (faster, uses disk space)"),
        this);
    keepBuilds_->setObjectName(QStringLiteral("settingsKeepBuilds"));
    rebuildAll_ = new QRadioButton(
        tr("&Build everything again each time: nothing is kept (less disk space, a songbook "
           "takes minutes)"),
        this);
    rebuildAll_->setObjectName(QStringLiteral("settingsRebuildAll"));
    (state.keepBuildFiles ? keepBuilds_ : rebuildAll_)->setChecked(true);
    buildFilesSize_ = new QLabel(this);
    buildFilesSize_->setObjectName(QStringLiteral("settingsBuildFilesSize"));
    deleteBuildFiles_ = new QPushButton(tr("Delete Now", "the build files"), this);
    deleteBuildFiles_->setObjectName(QStringLiteral("settingsDeleteBuildFiles"));
    deleteBuildFiles_->setToolTip(
        tr("Delete the build files now (not those of a build that is running)"));
    connect(deleteBuildFiles_, &QPushButton::clicked, this, [this] {
        if (state_.deleteBuildFiles)
            showBuildFilesSize(state_.deleteBuildFiles());
    });
    auto* builds = new QWidget(this);
    auto* buildsLayout = new QVBoxLayout(builds);
    buildsLayout->setContentsMargins(0, 0, 0, 0);
    buildsLayout->addWidget(keepBuilds_);
    buildsLayout->addWidget(rebuildAll_);
    buildsLayout->addWidget(row(this, {buildFilesSize_, deleteBuildFiles_}));
    form->addRow(tr("Build files:"), builds);
    showBuildFilesSize(state.buildFilesBytes);

    // The package is not a choice: the songbook's own, else the app's copy (zub::package).
    auto* package = pathLine(this);
    package->setObjectName(QStringLiteral("settingsPackage"));
    package->setText(state.package);
    package->setPlaceholderText(tr("none: the song library's zubuvzpevnik/ is not checked out"));
    auto* packageNote = new QLabel(state.package.isEmpty() ? QString()
                                   : state.ownPackage      ? tr("the song library's own")
                                                           : tr("the app's copy"),
                                   this);
    form->addRow(tr("Package:"), row(this, {package, packageNote}));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

bool SettingsDialog::menuEntry() const {
    return menuEntry_->isChecked();
}

bool SettingsDialog::keepBuildFiles() const {
    return keepBuilds_->isChecked();
}

void SettingsDialog::showBuildFilesSize(qint64 bytes) {
    buildFilesSize_->setText(tr("They take %1 now.").arg(QLocale().formattedDataSize(bytes)));
    deleteBuildFiles_->setEnabled(bytes > 0 && state_.deleteBuildFiles);
}

QString SettingsDialog::language() const {
    return language_->currentData().toString();
}

void SettingsDialog::showSoundfont(const QString& path, bool chosen) {
    soundfontLine_->setText(path);
    soundfontLine_->setPlaceholderText(
        chosen ? QString() : tr("found automatically when Play needs it (or none installed)"));
    soundfontLine_->setToolTip(chosen ? tr("Chosen by you") : tr("Found automatically"));
    automatic_->setEnabled(chosen);
}
