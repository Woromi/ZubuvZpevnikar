#include "LibraryDialog.hpp"

#include "convert.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStringDecoder>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <algorithm>
#include <zub/core/library.hpp>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace {
const QString urlKey = QStringLiteral("home/cloneUrl");

// git and every git it starts (the submodules' clones) are one process group: Cancel kills them
// all, with SIGKILL, so git's own clean-up does not delete the folder either.
void killGroup(QProcess* process) {
    if (process->state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_UNIX
    if (const qint64 pid = process->processId(); pid > 0)
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
#endif
    process->kill();
    process->waitForFinished(2000);
}
} // namespace

LibraryDialog::LibraryDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("New Song Library"));
    resize(660, 460);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("A song library is the folder with your songs (songs/) and the "
                                "songbooks made of them (songbooks/)."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    create_ = new QRadioButton(tr("Create a new, empty song library"), this);
    create_->setObjectName(QStringLiteral("libraryCreate"));
    download_ = new QRadioButton(tr("Download a song library (git clone)"), this);
    download_->setObjectName(QStringLiteral("libraryDownload"));
    create_->setChecked(true);
    const bool hasGit = !git().isEmpty();
    download_->setEnabled(hasGit);
    if (!hasGit)
        download_->setToolTip(tr("git is not installed: install it to download a song library"));
    useGit_ = new QCheckBox(tr("Keep its history with git (git init)"), this);
    useGit_->setToolTip(tr("Lets the app show when songs were added; you can commit and push it "
                           "with git later"));
    useGit_->setChecked(hasGit);
    useGit_->setEnabled(hasGit);
    layout->addWidget(create_);
    auto* createOptions = new QHBoxLayout;
    createOptions->addSpacing(24);
    createOptions->addWidget(useGit_);
    createOptions->addStretch(1);
    layout->addLayout(createOptions);
    layout->addWidget(download_);
    auto* form = new QFormLayout;
    url_ = new QLineEdit(QSettings{}.value(urlKey, defaultUrl()).toString(), this);
    url_->setObjectName(QStringLiteral("cloneUrl"));
    form->addRow(tr("Repository"), url_);
    auto* folderRow = new QHBoxLayout;
    folder_ = new QLineEdit(this);
    folder_->setObjectName(QStringLiteral("cloneFolder"));
    browse_ = new QPushButton(tr("Choose…"), this);
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse_);
    form->addRow(tr("Folder"), folderRow);
    layout->addLayout(form);
    status_ = new QLabel(tr("The folder must be new or empty."), this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    output_ = new QPlainTextEdit(this);
    output_->setReadOnly(true);
    output_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(output_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    clone_ = buttons->addButton(tr("Create"), QDialogButtonBox::AcceptRole);
    clone_->setDefault(true);
    layout->addWidget(buttons);
    folder_->setText(defaultFolder());
    connect(browse_, &QPushButton::clicked, this, &LibraryDialog::chooseFolder);
    connect(clone_, &QPushButton::clicked, this, &LibraryDialog::start);
    connect(buttons, &QDialogButtonBox::rejected, this, &LibraryDialog::reject);
    connect(url_, &QLineEdit::textChanged, this, &LibraryDialog::updateButtons);
    connect(folder_, &QLineEdit::textChanged, this, &LibraryDialog::updateButtons);
    connect(download_, &QRadioButton::toggled, this, [this] {
        // The suggested folder follows the choice until the user types another one.
        const QString other = downloading() ? QDir::home().filePath(QStringLiteral("SongLibrary"))
                                            : QDir::home().filePath(QStringLiteral("ZubuvZpevnik"));
        if (folder_->text() == other)
            folder_->setText(defaultFolder());
        updateButtons();
    });
    updateButtons();
}

bool LibraryDialog::downloading() const {
    return download_->isChecked();
}

QString LibraryDialog::defaultFolder() const {
    return QDir::home().filePath(downloading() ? QStringLiteral("ZubuvZpevnik")
                                               : QStringLiteral("SongLibrary"));
}

LibraryDialog::~LibraryDialog() {
    if (process_) {
        process_->disconnect(this);
        killGroup(process_);
    }
}

QString LibraryDialog::defaultUrl() {
    return QStringLiteral("https://github.com/Woromi/ZubuvZpevnik.git");
}

QString LibraryDialog::git() {
    return QStandardPaths::findExecutable(QStringLiteral("git"));
}

QString LibraryDialog::folder() const {
    // "~/X" is the home folder's X (no shell expands it), a relative path is from the home folder.
    QString text = folder_->text().trimmed();
    if (text == QStringLiteral("~") || text.startsWith(QStringLiteral("~/")))
        text = QDir::homePath() + text.mid(1);
    return QDir::cleanPath(QDir::home().absoluteFilePath(text));
}

void LibraryDialog::chooseFolder() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Folder for the song library"), QFileInfo(folder()).absolutePath());
    if (!dir.isEmpty())
        folder_->setText(QDir(dir).filePath(QFileInfo(defaultFolder()).fileName()));
}

void LibraryDialog::updateButtons() {
    const bool running = process_ != nullptr;
    clone_->setText(downloading() ? tr("Download") : tr("Create"));
    clone_->setEnabled(!running && !folder_->text().trimmed().isEmpty() &&
                       (!downloading() || !url_->text().trimmed().isEmpty()));
    url_->setEnabled(!running && downloading());
    useGit_->setEnabled(!running && !downloading() && !git().isEmpty());
    create_->setEnabled(!running);
    download_->setEnabled(!running && !git().isEmpty());
    folder_->setEnabled(!running);
    browse_->setEnabled(!running);
}

void LibraryDialog::start() {
    if (process_)
        return;
    const QString target = folder();
    const QFileInfo info(target);
    if (info.exists() &&
        (!info.isDir() || !QDir(target).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                QDir::Hidden | QDir::System))) {
        status_->setText(
            tr("%1 exists and is not an empty folder: choose another one.").arg(target));
        return;
    }
    if (!downloading()) {
        createLibrary(target);
        return;
    }
    if (git().isEmpty()) {
        status_->setText(tr("git is not installed: install it and try again."));
        return;
    }
    QSettings{}.setValue(urlKey, url_->text().trimmed());
    output_->clear();
    status_->setText(tr("Downloading…"));
    // The ext:: transport runs commands: never, whatever the user's git configuration says.
    startGit({QStringLiteral("-c"), QStringLiteral("protocol.ext.allow=never"),
              QStringLiteral("clone"), QStringLiteral("--recurse-submodules"),
              QStringLiteral("--progress"), QStringLiteral("--"), url_->text().trimmed(), target},
             {});
}

void LibraryDialog::createLibrary(const QString& target) {
    if (auto made = zub::createSongbookRepo(toPath(target)); !made) {
        status_->setText(tr("The song library could not be created: %1")
                             .arg(toQString(made.error())));
        return;
    }
    if (!useGit_->isChecked() || git().isEmpty()) {
        accept();
        return;
    }
    status_->setText(tr("Setting up git…"));
    initializing_ = true;
    startGit({QStringLiteral("init"), QStringLiteral("--quiet")}, target);
}

void LibraryDialog::startGit(const QStringList& arguments, const QString& workingDirectory) {
    decoder_ = QStringDecoder(QStringDecoder::Utf8);
    pending_.clear();
    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_UNIX
    process_->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"),
                       QStringLiteral("0")); // no password prompt
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    // ssh must not wait for a passphrase or a host key answer nobody sees.
    if (!environment.contains(QStringLiteral("GIT_SSH_COMMAND")))
        environment.insert(QStringLiteral("GIT_SSH_COMMAND"), QStringLiteral("ssh -o BatchMode=yes"));
    process_->setProcessEnvironment(environment);
    connect(process_, &QProcess::readyRead, this, [this] {
        // git's progress rewrites its line with \r: the last one goes to the status line. A line
        // (and a UTF-8 sequence) can end in the next chunk.
        pending_ += decoder_.decode(process_->readAll());
        const qsizetype end = std::max(pending_.lastIndexOf(QLatin1Char('\n')),
                                       pending_.lastIndexOf(QLatin1Char('\r')));
        if (end < 0)
            return;
        const QString text = pending_.left(end);
        pending_.remove(0, end + 1);
        const QStringList lines =
            text.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            status_->setText(lines.last().trimmed());
        for (const QString& line : lines)
            if (!line.contains(QLatin1Char('%')) || line.contains(QStringLiteral("done")))
                output_->appendPlainText(line);
    });
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        onFinished(code, status != QProcess::NormalExit);
    });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onFinished(-1, true);
    });
    if (!workingDirectory.isEmpty())
        process_->setWorkingDirectory(workingDirectory);
    process_->start(git(), arguments);
    updateButtons();
}

void LibraryDialog::onFinished(int exitCode, bool crashed) {
    if (!process_)
        return;
    pending_ += decoder_.decode(process_->readAll());
    if (!pending_.trimmed().isEmpty())
        output_->appendPlainText(pending_.trimmed());
    pending_.clear();
    process_->deleteLater();
    process_ = nullptr;
    updateButtons();
    if (initializing_) {
        // The library is there either way; without git it only has no history.
        initializing_ = false;
        accept();
        return;
    }
    if (!crashed && exitCode == 0) {
        status_->setText(tr("Done."));
        accept();
        return;
    }
    status_->setText(tr("The download failed (see the output above). A private repository needs "
                        "your access to it (a git credential helper or an SSH key). Nothing was "
                        "opened; a partly downloaded folder is left as it is: choose another "
                        "folder or empty it to try again."));
}

void LibraryDialog::reject() {
    if (process_) {
        process_->disconnect(this);
        killGroup(process_);
        process_->deleteLater();
        process_ = nullptr;
    }
    QDialog::reject();
}
