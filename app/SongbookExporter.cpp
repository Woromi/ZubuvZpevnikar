#include "SongbookExporter.hpp"

#include "convert.hpp"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <algorithm>
#include <set>
#include <zub/core/bookfile.hpp>
#include <zub/core/safety.hpp>
#include <zub/core/song.hpp>
#include <zub/core/textfile.hpp>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace {
const QString exportDirKey = QStringLiteral("songbooks/exportDir");

// Stops the build and what it started (lualatex, lilypond): the build leads its own process
// group on Unix; SIGTERM to the group, 500 ms, SIGKILL. Waits for the process to exit.
void killTree(QProcess* process) {
    if (process->state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_UNIX
    const qint64 pid = process->processId();
    if (pid > 0) {
        ::kill(-static_cast<pid_t>(pid), SIGTERM);
        if (process->waitForFinished(500))
            return;
        ::kill(-static_cast<pid_t>(pid), SIGKILL);
    }
#endif
    process->kill();
    process->waitForFinished(2000);
}

bool writeFile(const QString& path, const std::string& content, QString* error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = QObject::tr("Cannot create the folder of %1").arg(path);
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(content.data(), static_cast<qint64>(content.size())) !=
            static_cast<qint64>(content.size()) ||
        !file.commit()) {
        *error = QObject::tr("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}


QString lastExportDir() {
    const QString dir = QSettings{}.value(exportDirKey).toString();
    return dir.isEmpty() || !QDir(dir).exists() ? QDir::homePath() : dir;
}

} // namespace

SongbookExporter::SongbookExporter(QWidget* parentWindow) : QObject(parentWindow), window_(parentWindow) {}

QString SongbookExporter::noPackageText() const {
    return tr("There is no package to build the songbook with: the songbook's zubuvzpevnik/ is not "
              "checked out (git submodule update --init), and this app has no copy of its own.");
}

bool SongbookExporter::confirmSafety(const zub::songbook::Resolution& resolution,
                                     const QStringList& extra) {
    QStringList lines = extra;
    int unsafeSongs = static_cast<int>(extra.size());
    for (const auto& song : resolution.songs) {
        if (song.path.empty())
            continue;
        QString what;
        const auto text = zub::readTextFile(song.path);
        if (!text) {
            what = tr("cannot read the file");
        } else {
            // The song's own commands, and what its scores would run through LilyPond.
            auto unknown = zub::safety::unknownConstructs(*text);
            for (std::string& risk : zub::songbook::scoreRisks(root_, song.path, *text))
                unknown.push_back(std::move(risk));
            if (unknown.empty())
                continue;
            QStringList commands;
            for (const std::string& command : unknown)
                commands << toQString(command);
            what = commands.join(QStringLiteral(", "));
        }
        ++unsafeSongs;
        if (lines.size() < 20)
            lines << toQString(song.inputName) + QStringLiteral(": ") + what;
    }
    if (unsafeSongs == 0)
        return true;
    if (unsafeSongs > lines.size())
        lines << tr("… and %n more", nullptr, unsafeSongs - static_cast<int>(lines.size()));
    QMessageBox box(QMessageBox::Warning, tr("Unknown commands in songs"),
                    tr("Some songs use commands outside the song format or cannot be read:\n\n%1"
                       "\n\nBuild anyway? These commands run with your rights "
                       "(lualatex -shell-escape).")
                        .arg(lines.join(QLatin1Char('\n'))),
                    QMessageBox::NoButton, window_);
    QPushButton* build = box.addButton(tr("Build"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == build;
}

void SongbookExporter::exportRepoPdf(const std::filesystem::path& bookFile, std::string text,
                                     zub::songbook::Songbook book) {
    if (package_.empty()) {
        QMessageBox::warning(window_, tr("Export PDF"), noPackageText());
        return;
    }
    const auto parsed = zub::bookfile::parse(text);
    if (!parsed) {
        QMessageBox::warning(window_, tr("Export PDF"), toQString(parsed.error()));
        return;
    }
    // The book file is LaTeX that runs too, and so is the chord chart a Tabor class inputs.
    const std::filesystem::path bookDir = bookFile.parent_path();
    QStringList bookRisks;
    const auto addRisks = [&](const QString& name, const std::string& content) {
        const auto unknown = zub::safety::unknownBookConstructs(content);
        if (unknown.empty())
            return;
        QStringList commands;
        for (const std::string& command : unknown)
            commands << toQString(command);
        bookRisks << name + QStringLiteral(": ") + commands.join(QStringLiteral(", "));
    };
    addRisks(toQString(bookFile.filename()), text);
    std::vector<std::string> inputs = zub::bookfile::inputs(text);
    if (const auto akordy = zub::readTextFile(bookDir / "akordy.tex")) {
        addRisks(QStringLiteral("akordy.tex"), *akordy);
        for (std::string& name : zub::bookfile::inputs(*akordy))
            if (std::ranges::find(inputs, name) == inputs.end())
                inputs.push_back(std::move(name));
    }
    // Every \input of the book, wherever it is, must be a song of the songbook (the build finds
    // the song folders first): those are what the song check below reads.
    zub::songbook::Songbook all;
    all.sections.push_back({});
    for (const std::string& name : inputs) {
        const bool song = !name.empty() && !zub::invalidInputName(name) &&
                          (std::filesystem::is_regular_file(root_ / "songs/chords" / (name + ".tex")) ||
                           std::filesystem::is_regular_file(root_ / "songs/tinwhistle" / (name + ".tex")));
        if (song)
            all.sections.front().songs.push_back(name);
        else
            bookRisks << tr("\\input{%1}: not a song of the songbook").arg(toQString(name));
    }
    // An index file lying in the book's folder or the repo root would be read by the first run.
    for (const auto& dir : {bookDir, root_}) {
        const QDir folder(toQString(dir));
        for (const QString& name :
             folder.entryList({QStringLiteral("*.sbx"), QStringLiteral("*.sxd")}, QDir::Files))
            bookRisks << tr("%1: an index file the build would read").arg(folder.filePath(name));
    }
    const auto resolution = zub::songbook::resolveSongs(all, root_);
    if (!confirmSafety(resolution, bookRisks))
        return;

    const QString stem = toQString(bookFile.stem());
    const QString target = QFileDialog::getSaveFileName(
        window_, tr("Export PDF"), QDir(lastExportDir()).filePath(stem + QStringLiteral(".pdf")),
        tr("PDF (*.pdf)"));
    if (target.isEmpty())
        return;
    QSettings{}.setValue(exportDirKey, QFileInfo(target).absolutePath());

    // One work dir per book file: LilyPond's cache and the index files stay between builds.
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(toQString(bookFile).toUtf8(), QCryptographicHash::Sha1)
            .toHex()
            .left(12));
    const QString workDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                            QStringLiteral("/songbooks/repo-") + hash;
    if (!QDir().mkpath(workDir)) {
        QMessageBox::warning(window_, tr("Export PDF"), tr("Cannot create %1").arg(workDir));
        return;
    }
    // A stale index from an earlier song list would be used by the build.
    const QDir work(workDir);
    for (const QString& name :
         work.entryList({QStringLiteral("*.sbx"), QStringLiteral("*.sxd"), QStringLiteral("book.*")},
                        QDir::Files))
        QFile::remove(work.filePath(name));
    QString error;
    if (!writeFile(workDir + QStringLiteral("/book.tex"), text, &error)) {
        QMessageBox::warning(window_, tr("Export PDF"), error);
        return;
    }
    const auto command = zub::bookfile::buildCommand(root_, package_, toPath(workDir), bookDir,
                                                     "book", zub::bookfile::indexFiles(*parsed));
    QString message;
    if (!runBuild(command, book.title.empty() ? stem : toQString(book.title), target, &message))
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    emit finished(message);
}

bool SongbookExporter::runBuild(const zub::songbook::BuildCommand& command, const QString& title,
                                const QString& target, QString* result) {
    QStringList arguments;
    for (const std::string& argument : command.arguments)
        arguments << toQString(argument);

    // State the process lambdas use lives before the dialog: ~QDialog destroys the QProcess.
    QByteArray output;
    QStringDecoder decoder(QStringDecoder::Utf8);
    bool succeeded = false;
    bool finishedRun = false;
    bool cancelled = false;
    QString message;

    QDialog dialog(window_);
    dialog.setWindowTitle(tr("Building %1…").arg(title));
    dialog.resize(800, 500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* status = new QLabel(tr("Building the songbook (this can take minutes)…"), &dialog);
    auto* bar = new QProgressBar(&dialog);
    bar->setRange(0, 0);
    auto* log = new QPlainTextEdit(&dialog);
    log->setReadOnly(true);
    log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log->setMaximumBlockCount(20000);
    auto* buttons = new QDialogButtonBox(&dialog);
    QPushButton* cancel = buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    QPushButton* openLog = buttons->addButton(tr("Open Log"), QDialogButtonBox::ActionRole);
    openLog->hide();
    layout->addWidget(status);
    layout->addWidget(bar);
    layout->addWidget(log, 1);
    layout->addWidget(buttons);

    auto* process = new QProcess(&dialog);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(toQString(command.workingDirectory));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto& [name, value] : command.environment)
        environment.insert(toQString(name), toQString(value));
    process->setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
    process->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif

    const auto fail = [&](const QString& text) {
        finishedRun = true;
        status->setText(text);
        bar->setRange(0, 1);
        bar->setValue(0);
        cancel->setText(tr("Close"));
        openLog->setVisible(QFileInfo::exists(toQString(command.log)));
    };

    QObject::connect(process, &QProcess::readyRead, &dialog, [&] {
        const QByteArray bytes = process->readAll();
        output += bytes;
        log->moveCursor(QTextCursor::End);
        log->insertPlainText(QString(decoder(bytes)));
        log->moveCursor(QTextCursor::End);
    });
    QObject::connect(process, &QProcess::errorOccurred, &dialog, [&](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail(tr("Cannot start the build: %1").arg(process->errorString()));
    });
    QObject::connect(process, &QProcess::finished, &dialog, [&](int code, QProcess::ExitStatus st) {
        const QByteArray rest = process->readAll();
        if (!rest.isEmpty()) {
            output += rest;
            log->moveCursor(QTextCursor::End);
            log->insertPlainText(QString(decoder(rest)));
        }
        if (cancelled)
            return;
        const QString pdf = toQString(command.pdf);
        if (st != QProcess::NormalExit || code != 0 || !QFileInfo::exists(pdf)) {
            fail(st != QProcess::NormalExit ? tr("Build crashed")
                 : code != 0                ? tr("Build failed (exit %1)").arg(code)
                                            : tr("Build failed (no PDF was written)"));
            return;
        }
        // Copy beside the target, then replace it in one step: a failure keeps the old file.
        const QString part = target + QStringLiteral(".part");
        QFile::remove(part);
        std::error_code renameError;
        if (!QFile::copy(pdf, part)) {
            QFile::remove(part);
            fail(tr("Cannot write %1").arg(target));
            return;
        }
        std::filesystem::rename(toPath(part), toPath(target), renameError);
        if (renameError) {
            QFile::remove(part);
            fail(tr("Cannot write %1: %2")
                     .arg(target, QString::fromStdString(renameError.message())));
            return;
        }
        message = tr("Exported %1").arg(target);
        static const QRegularExpression pages(QStringLiteral(R"(Finished: .*\((\d+) pages?\))"));
        const auto match = pages.match(QString::fromUtf8(output));
        if (match.hasMatch())
            message += tr(" (%n page(s))", nullptr, match.captured(1).toInt());
        QFile logFile(toQString(command.log));
        if (logFile.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = logFile.readAll();
            const int warnings = zub::songbook::paperWarnings(
                std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
            if (warnings > 0)
                message += tr(", %n song(s) not marked as fitting this paper", nullptr, warnings);
        }
        succeeded = true;
        dialog.accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, &dialog, [&] {
        if (!finishedRun) {
            cancelled = true;
            killTree(process);
        }
        dialog.reject();
    });
    QObject::connect(openLog, &QPushButton::clicked, &dialog, [&] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(toQString(command.log)));
    });
    // Closing the window (Escape, X) while building is a cancel.
    QObject::connect(&dialog, &QDialog::rejected, &dialog, [&] {
        if (process->state() != QProcess::NotRunning) {
            cancelled = true;
            killTree(process);
        }
    });

    // The build must not outlive the app.
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, process,
                     [process] { killTree(process); });

    process->start(toQString(command.program), arguments);
    dialog.exec();
    if (process->state() != QProcess::NotRunning)
        killTree(process);
    // The dialog destroys the process after the widgets the lambdas use.
    process->disconnect();

    if (succeeded)
        *result = message;
    return succeeded;
}
