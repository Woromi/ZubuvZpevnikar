#include "SettingsDialog.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
    : QDialog(parent), state_(state), converter_(state.converter), soundfont_(state.soundfont) {
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

    converterLine_ = pathLine(this);
    converterLine_->setObjectName(QStringLiteral("settingsConverter"));
    auto* chooseConverter = new QPushButton(tr("Choose…"), this);
    useBuiltIn_ = new QPushButton(tr("Use Built-in"), this);
    useBuiltIn_->setToolTip(tr("The converter built with this app, the version it is tested with"));
    connect(chooseConverter, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Choose chords_converter"),
            converter_.isEmpty() ? QString() : QFileInfo(converter_).absolutePath());
        if (path.isEmpty())
            return;
        converterChanged_ = true;
        converter_ = path;
        showConverter(path, true);
    });
    connect(useBuiltIn_, &QPushButton::clicked, this, [this] {
        converterChanged_ = true;
        converter_.clear();
        showConverter(state_.builtInConverter, false);
    });
    form->addRow(tr("&Converter:"), row(this, {converterLine_, chooseConverter, useBuiltIn_}));
    showConverter(state.converter, state.converterChosen);

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

QString SettingsDialog::language() const {
    return language_->currentData().toString();
}

void SettingsDialog::showConverter(const QString& path, bool chosen) {
    converterLine_->setText(path);
    converterLine_->setPlaceholderText(tr("not found: choose one"));
    converterLine_->setToolTip(chosen ? tr("Chosen by you") : tr("Found automatically"));
    useBuiltIn_->setEnabled(chosen && !state_.builtInConverter.isEmpty());
}

void SettingsDialog::showSoundfont(const QString& path, bool chosen) {
    soundfontLine_->setText(path);
    soundfontLine_->setPlaceholderText(
        chosen ? QString() : tr("found automatically when Play needs it (or none installed)"));
    soundfontLine_->setToolTip(chosen ? tr("Chosen by you") : tr("Found automatically"));
    automatic_->setEnabled(chosen);
}
