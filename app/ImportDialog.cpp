#include "ImportDialog.hpp"

#include "ConverterVersion.hpp"
#include "convert.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr int watchdogSeconds = 180;
constexpr const char* supportedSites =
    "akordy.kytary.cz, pisnicky-akordy.cz, blivnik.cz, ultimate-guitar.com, guitartuna.com";
const char* errorStyle = "color: #b00020;";

using zub::songimport::FailureKind;

const auto tidyKey = QStringLiteral("import/tidy");
} // namespace

ImportDialog::ImportDialog(std::filesystem::path converter, std::filesystem::path builtIn,
                           QWidget* parent)
    : QDialog(parent), converter_(std::move(converter)), builtIn_(std::move(builtIn)),
      process_(new QProcess(this)),
      watchdog_(new QTimer(this)) {
    setWindowTitle(tr("Import Song"));
    resize(640, 300);
    setSizeGripEnabled(true);

    source_ = new QLineEdit;
    source_->setPlaceholderText(
        tr("https://akordy.kytary.cz/song/…  or a page saved from the browser"));
    source_->setClearButtonEnabled(true);
    browse_ = new QPushButton(tr("Browse…"));
    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(source_, 1);
    sourceRow->addWidget(browse_);

    auto* hint = new QLabel(tr("Supported sites: %1").arg(QString::fromLatin1(supportedSites)));
    hint->setStyleSheet(QStringLiteral("color: gray;"));
    hint->setWordWrap(true);

    converterLabel_ = new QLabel;
    chooseConverter_ = new QPushButton(tr("Choose…"));
    useBuiltIn_ = new QPushButton(tr("Use Built-in"));
    useBuiltIn_->setToolTip(tr("Use the chords_converter built with this app"));
    auto* converterRow = new QHBoxLayout;
    converterRow->addWidget(new QLabel(tr("Converter:")));
    converterRow->addWidget(converterLabel_, 1);
    converterRow->addWidget(useBuiltIn_);
    converterRow->addWidget(chooseConverter_);

    tidy_ = new QCheckBox(tr("Tidy the song (repeated choruses, chord positions)"));
    tidy_->setChecked(QSettings{}.value(tidyKey, true).toBool());

    botCheck_ = new QGroupBox(tr("Site asks for a bot check"));
    botCheck_->setCheckable(true);
    botCheck_->setChecked(false);
    cookie_ = new QLineEdit;
    cookie_->setPlaceholderText(QStringLiteral("cf_clearance=…"));
    userAgent_ = new QLineEdit;
    userAgent_->setPlaceholderText(tr("what navigator.userAgent prints in your browser"));
    auto* explain = new QLabel(tr("Copy both from the browser that passed the check. They are "
                                  "used for this import only and never stored."));
    explain->setWordWrap(true);
    explain->setStyleSheet(QStringLiteral("color: gray;"));
    auto* botContents = new QWidget;
    auto* form = new QFormLayout(botContents);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(tr("Cookie:"), cookie_);
    form->addRow(tr("User agent:"), userAgent_);
    form->addRow(explain);
    auto* botLayout = new QVBoxLayout(botCheck_);
    botLayout->addWidget(botContents);
    botContents->setVisible(false);
    connect(botCheck_, &QGroupBox::toggled, botContents, &QWidget::setVisible);

    status_ = new QLabel;
    status_->setWordWrap(true);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    details_ = new QPlainTextEdit;
    details_->setReadOnly(true);
    details_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    details_->setLineWrapMode(
        QPlainTextEdit::WidgetWidth); // the converter writes long advice lines
    details_->setMinimumHeight(110);
    details_->setVisible(false);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Cancel);
    importButton_ = buttons_->addButton(tr("Import"), QDialogButtonBox::AcceptRole);
    importButton_->setDefault(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Web address or saved page:")));
    layout->addLayout(sourceRow);
    layout->addWidget(hint);
    layout->addLayout(converterRow);
    layout->addWidget(tidy_);
    layout->addWidget(botCheck_);
    layout->addWidget(status_);
    layout->addWidget(details_, 1);
    layout->addWidget(buttons_);

    watchdog_->setSingleShot(true);
    watchdog_->setInterval(watchdogSeconds * 1000);
    connect(watchdog_, &QTimer::timeout, this, [this] {
        timedOut_ = true;
        process_->kill();
    });

    connect(process_, &QProcess::readyReadStandardOutput, this,
            [this] { out_ += process_->readAllStandardOutput(); });
    connect(process_, &QProcess::readyReadStandardError, this,
            [this] { err_ += process_->readAllStandardError(); });
    connect(process_, &QProcess::finished, this, [this] { onFinished(); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // Other errors (crash, kill) are followed by finished(); FailedToStart is not.
        if (error != QProcess::FailedToStart)
            return;
        watchdog_->stop();
        setRunning(false);
        showFailure({FailureKind::Other,
                     toStdString(tr("Cannot run %1: %2")
                                     .arg(toQString(converter_), process_->errorString()))});
    });

    connect(source_, &QLineEdit::textChanged, this, [this] { updateImportEnabled(); });
    connect(source_, &QLineEdit::returnPressed, importButton_, &QPushButton::click);
    connect(browse_, &QPushButton::clicked, this, [this] { chooseSource(); });
    connect(chooseConverter_, &QPushButton::clicked, this, [this] { chooseConverter(); });
    connect(useBuiltIn_, &QPushButton::clicked, this, [this] {
        converter_ = builtIn_;
        converterChosen_ = false;
        converterReset_ = true;
        updateConverterLabel();
        updateImportEnabled();
    });
    connect(importButton_, &QPushButton::clicked, this, [this] { startImport(); });
    connect(buttons_, &QDialogButtonBox::rejected, this, &ImportDialog::reject);

    updateConverterLabel();
    updateImportEnabled();
}

ImportDialog::~ImportDialog() {
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->blockSignals(true);
        process_->kill();
        process_->waitForFinished(2000);
    }
}

bool ImportDialog::converterExists() const {
    return !converter_.empty() && QFileInfo(toQString(converter_)).isExecutable() &&
           QFileInfo(toQString(converter_)).isFile();
}

bool ImportDialog::tidy() const {
    return tidy_->isChecked();
}

void ImportDialog::updateConverterLabel() {
    if (converterExists()) {
        const QString path = toQString(converter_);
        const QFontMetrics metrics(converterLabel_->font());
        converterLabel_->setStyleSheet({});
        QString shown = converter_ == builtIn_ ? tr("built-in (%1)").arg(path) : path;
        shown = metrics.elidedText(shown, Qt::ElideMiddle, 380);
        const auto answer = converterVersion(converter_);
        QString tip = path;
        if (answer.version) {
            shown += QStringLiteral("  ") +
                     tr("version %1").arg(toQString(zub::converter::toString(*answer.version)));
        } else {
            shown += QStringLiteral("  ") + tr("version unknown");
        }
        const QString problem = converterProblem(converter_);
        if (!problem.isEmpty()) {
            // Importing still works with an older converter; tidy and Make Smaller refuse it.
            converterLabel_->setStyleSheet(QString::fromLatin1(errorStyle));
            tip += QStringLiteral("\n") + problem;
        }
        converterLabel_->setText(shown);
        converterLabel_->setToolTip(tip);
    } else {
        converterLabel_->setStyleSheet(QString::fromLatin1(errorStyle));
        converterLabel_->setText(tr("chords_converter not found — build the app with "
                                    "ZUB_BUILD_CONVERTER, or choose it"));
        converterLabel_->setToolTip(toQString(converter_));
    }
    useBuiltIn_->setVisible(!builtIn_.empty() && converter_ != builtIn_);
    converterLabel_->setMinimumWidth(0);
    converterLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

void ImportDialog::updateImportEnabled() {
    importButton_->setEnabled(!running_ && !source_->text().trimmed().isEmpty() &&
                              converterExists());
}

void ImportDialog::chooseSource() {
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Choose a saved page"), {},
        tr("Saved pages and text (*.html *.htm *.txt);;All files (*)"));
    if (!file.isEmpty())
        source_->setText(file);
}

void ImportDialog::chooseConverter() {
    const QString file =
        QFileDialog::getOpenFileName(this, tr("Choose chords_converter"), toQString(converter_));
    if (file.isEmpty())
        return;
    converter_ = toPath(file);
    converterChosen_ = true;
    converterReset_ = false;
    updateConverterLabel();
    updateImportEnabled();
}

void ImportDialog::setRunning(bool running) {
    running_ = running;
    source_->setEnabled(!running);
    browse_->setEnabled(!running);
    chooseConverter_->setEnabled(!running);
    useBuiltIn_->setEnabled(!running);
    tidy_->setEnabled(!running);
    botCheck_->setEnabled(!running);
    updateImportEnabled();
    if (running) {
        status_->setStyleSheet({});
        status_->setText(tr("Converting… a slow site can take up to a minute and a half."));
        details_->setVisible(false);
    }
}

void ImportDialog::startImport() {
    if (running_)
        return;
    imported_.reset();
    timedOut_ = false;
    cancelled_ = false;
    out_.clear();
    err_.clear();
    details_->clear();
    details_->setVisible(false);
    status_->setStyleSheet({});

    workDir_ = std::make_unique<QTemporaryDir>();
    if (!workDir_->isValid()) {
        status_->setStyleSheet(QString::fromLatin1(errorStyle));
        status_->setText(
            tr("Cannot create a temporary directory: %1").arg(workDir_->errorString()));
        return;
    }
    const auto command = zub::songimport::prepare(
        {converter_, toPath(workDir_->path()), toStdString(source_->text().trimmed())});
    if (!command) {
        status_->setStyleSheet(QString::fromLatin1(errorStyle));
        status_->setText(toQString(command.error()));
        return;
    }

    QStringList arguments;
    for (const auto& argument : command->arguments)
        arguments << toQString(argument);
    auto environment = QProcessEnvironment::systemEnvironment();
    // The user's own session might export these; only the dialog's fields count.
    environment.remove(QStringLiteral("CHORDS_COOKIE"));
    environment.remove(QStringLiteral("CHORDS_USER_AGENT"));
    if (botCheck_->isChecked()) {
        if (const QString cookie = cookie_->text().trimmed(); !cookie.isEmpty())
            environment.insert(QStringLiteral("CHORDS_COOKIE"), cookie);
        if (const QString agent = userAgent_->text().trimmed(); !agent.isEmpty())
            environment.insert(QStringLiteral("CHORDS_USER_AGENT"), agent);
    }
    process_->setProcessEnvironment(environment);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    process_->setWorkingDirectory(toQString(command->workingDirectory));
    process_->setProgram(toQString(command->program));
    process_->setArguments(arguments);

    setRunning(true);
    watchdog_->start();
    process_->start();
}

void ImportDialog::onFinished() {
    watchdog_->stop();
    out_ += process_->readAllStandardOutput();
    err_ += process_->readAllStandardError();
    setRunning(false);

    if (cancelled_) {
        status_->setStyleSheet({});
        status_->setText(tr("Cancelled."));
        return;
    }
    const std::string stderrText = err_.toStdString();
    if (timedOut_) {
        showFailure({FailureKind::Other,
                     toStdString(tr("The converter did not finish within 3 minutes and was "
                                    "stopped."))});
        return;
    }
    if (process_->exitStatus() == QProcess::CrashExit) {
        showFailure({FailureKind::Other, toStdString(tr("chords_converter crashed.")) + "\n" + stderrText});
        return;
    }
    auto result = zub::songimport::interpret(process_->exitCode(), out_.toStdString(), stderrText,
                                             toPath(workDir_->path()));
    if (result) {
        imported_ = std::move(*result);
        // Remembered only for an import that happened, not for a click in a cancelled dialog.
        QSettings{}.setValue(tidyKey, tidy_->isChecked());
        accept();
        return;
    }
    showFailure(result.error());
}

void ImportDialog::showFailure(const zub::songimport::Failure& failure) {
    QString summary;
    switch (failure.kind) {
    case FailureKind::BotCheck:
        summary = tr("The site answered with a bot check instead of the song.");
        botCheck_->setChecked(true);
        break;
    case FailureKind::UnsupportedSite:
        summary = tr("No parser knows this site. Supported: %1. You can also save the page and "
                     "choose the file.")
                      .arg(QString::fromLatin1(supportedSites));
        break;
    case FailureKind::Network:
        summary = tr("Could not reach the site. Check the address and your connection.");
        break;
    case FailureKind::Http:
        summary = tr("The site answered with an error (see details).");
        break;
    case FailureKind::InvalidInput:
        summary = tr("That is not a web address or a readable file.");
        break;
    case FailureKind::Other:
        summary = tr("The import failed.");
        break;
    }
    status_->setStyleSheet(QString::fromLatin1(errorStyle));
    status_->setText(summary);
    details_->setPlainText(toQString(failure.message));
    details_->setVisible(true);
}

void ImportDialog::reject() {
    if (running_ || process_->state() != QProcess::NotRunning) {
        cancelled_ = true;
        watchdog_->stop();
        process_->kill();
        process_->waitForFinished(2000);
        setRunning(false);
        status_->setStyleSheet({});
        status_->setText(tr("Cancelled."));
        return;
    }
    QDialog::reject();
}
