#include "SongbookExporter.hpp"

#include "convert.hpp"

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
#include <zub/core/safety.hpp>
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

// What generate() should treat as existing: the songs resolveSongs found.
std::function<bool(std::string_view)> existsIn(const zub::songbook::Resolution& resolution) {
    std::set<std::string> found;
    for (const auto& song : resolution.songs)
        if (!song.path.empty())
            found.insert(song.inputName);
    return [found = std::move(found)](std::string_view name) {
        return found.contains(std::string(name));
    };
}

// For the hint in an exported .tex: a single-quoted shell word on one line.
std::string shellQuote(std::string text) {
    std::ranges::replace(text, '\n', ' ');
    std::ranges::replace(text, '\r', ' ');
    std::string quoted = "'";
    for (const char c : text) {
        if (c == '\'')
            quoted += "'\\''";
        else
            quoted += c;
    }
    return quoted + "'";
}

const QString exportMarker = QStringLiteral(".zubuvzpevnikar-export");
// Folders exported before the app was renamed (2026-11) carry this marker instead.
const QString oldExportMarker = QStringLiteral(".zubuvzpevnikui-export");

QString lastExportDir() {
    const QString dir = QSettings{}.value(exportDirKey).toString();
    return dir.isEmpty() || !QDir(dir).exists() ? QDir::homePath() : dir;
}

// What the export dialog asked for.
struct LatexChoice {
    bool folder = false;
    QString stem;
};

std::optional<LatexChoice> askLatexChoice(QWidget* parent, const QString& id) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Export LaTeX"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* texOnly = new QRadioButton(
        QObject::tr("Just the .tex file (builds inside a ZubuvZpevnik checkout)"), &dialog);
    auto* folder = new QRadioButton(
        QObject::tr("A folder with everything it needs (builds anywhere with LuaLaTeX and LilyPond)"),
        &dialog);
    texOnly->setChecked(true);
    layout->addWidget(texOnly);
    layout->addWidget(folder);
    layout->addWidget(new QLabel(QObject::tr("File name (without .tex):"), &dialog));
    auto* stem = new QLineEdit(id, &dialog);
    stem->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[A-Za-z0-9_-]+")), stem));
    layout->addWidget(stem);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    const auto validate = [&] {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(stem->hasAcceptableInput());
    };
    QObject::connect(stem, &QLineEdit::textChanged, &dialog, validate);
    validate();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return std::nullopt;
    return LatexChoice{folder->isChecked(), stem->text()};
}

// `skipped` may also hold the invalid names (generate() leaves those out as well).
QString skippedText(const std::vector<std::string>& skipped,
                    const std::vector<std::string>& invalid) {
    QString text;
    QStringList names;
    for (const std::string& name : skipped)
        if (std::ranges::find(invalid, name) == invalid.end())
            names << toQString(name);
    if (!names.isEmpty())
        text += QObject::tr("\n\nSkipped (not found in the songbook): %1")
                    .arg(names.join(QStringLiteral(", ")));
    names.clear();
    for (const std::string& name : invalid)
        names << toQString(name);
    if (!names.isEmpty())
        text += QObject::tr("\n\nSkipped (not a valid song name): %1")
                    .arg(names.join(QStringLiteral(", ")));
    return text;
}

QString warningsText(const std::vector<std::string>& warnings) {
    QString text;
    for (const std::string& warning : warnings)
        text += QStringLiteral("\n\n") + toQString(warning);
    return text;
}
} // namespace

SongbookExporter::SongbookExporter(QWidget* parentWindow) : QObject(parentWindow), window_(parentWindow) {}

void SongbookExporter::exportLatex(const QString& id, zub::songbook::Songbook book,
                                   const TitleOf& titleOf) {
    const auto choice = askLatexChoice(window_, id);
    if (!choice)
        return;
    if (choice->folder)
        exportFolder(choice->stem, book, titleOf);
    else
        exportTexFile(choice->stem, book, titleOf);
}

void SongbookExporter::exportTexFile(const QString& stem, const zub::songbook::Songbook& book,
                                     const TitleOf& titleOf) {
    const QString dir = QFileDialog::getExistingDirectory(window_, tr("Export LaTeX to"),
                                                          lastExportDir());
    if (dir.isEmpty())
        return;
    QSettings{}.setValue(exportDirKey, dir);

    const auto resolution = zub::songbook::resolveSongs(book, root_);
    const auto generated = zub::songbook::generate(book, existsIn(resolution), titleOf);
    const QString texPath = QDir(dir).filePath(stem + QStringLiteral(".tex"));
    const QString akordyPath = QDir(dir).filePath(QStringLiteral("akordy.tex"));
    QStringList existing;
    if (QFileInfo::exists(texPath))
        existing << texPath;
    if (generated.akordy && QFileInfo::exists(akordyPath))
        existing << akordyPath;
    const QString coverPath =
        generated.coverSource ? QDir(dir).filePath(toQString(generated.coverName)) : QString();
    if (!coverPath.isEmpty() && QFileInfo::exists(coverPath))
        existing << coverPath;
    if (!existing.isEmpty() &&
        QMessageBox::question(window_, tr("Export LaTeX"),
                              tr("Replace the existing file(s)?\n%1", nullptr, static_cast<int>(existing.size()))
                                  .arg(existing.join(QLatin1Char('\n')))) !=
            QMessageBox::Yes)
        return;

    const std::string d = toStdString(dir);
    std::string indexArguments;
    for (const std::string& index : generated.indexes)
        indexArguments += "-i " + index + " ";
    const std::string header =
        "% Build it in a ZubuvZpevnik checkout, from an empty folder (it writes there):\n"
        "%   TEXINPUTS=\".:\"" + shellQuote(d) +
        "\":$REPO/songs/chords:$REPO/songs/tinwhistle:$REPO:$REPO/zubuvzpevnik:\" \\\n"
        "%     bash $REPO/scripts/build.sh " + indexArguments +
        shellQuote(d + "/" + toStdString(stem) + ".tex") + "\n";
    QString error;
    if (!writeFile(texPath, header + generated.tex, &error) ||
        (generated.akordy && !writeFile(akordyPath, *generated.akordy, &error))) {
        QMessageBox::warning(window_, tr("Export LaTeX"), error);
        return;
    }
    if (generated.coverSource) {
        QFile::remove(coverPath);
        if (!QFile::copy(toQString(*generated.coverSource), coverPath)) {
            QMessageBox::warning(window_, tr("Export LaTeX"),
                                 tr("Cannot copy the cover image to %1").arg(coverPath));
            return;
        }
        QFile::setPermissions(coverPath, QFile::permissions(coverPath) | QFileDevice::WriteOwner);
    }
    QMessageBox::information(window_, tr("Export LaTeX"),
                             tr("Written %1").arg(texPath) +
                                 skippedText(generated.skipped, resolution.invalid) +
                                 warningsText(generated.warnings));
}

void SongbookExporter::exportFolder(const QString& stem, const zub::songbook::Songbook& book,
                                    const TitleOf& titleOf) {
    const QString parent = QFileDialog::getExistingDirectory(
        window_, tr("Create the folder %1 in").arg(stem), lastExportDir());
    if (parent.isEmpty())
        return;
    QSettings{}.setValue(exportDirKey, parent);
    const QString target = QDir(parent).filePath(stem);

    auto plan = zub::songbook::planFolderExport(book, root_, toStdString(stem), titleOf);
    if (!plan) {
        QMessageBox::warning(window_, tr("Export LaTeX"), toQString(plan.error()));
        return;
    }

    // The target must be exactly <parent>/<stem>: no symlink, not inside (or around) the songbook.
    const QFileInfo targetInfo(target);
    const QString parentCanonical = QDir(parent).canonicalPath();
    const QString rootCanonical = QDir(toQString(root_)).canonicalPath();
    const QString targetCanonical = parentCanonical + QLatin1Char('/') + stem;
    const auto inside = [](const QString& path, const QString& dir) {
        return !dir.isEmpty() && (path == dir || path.startsWith(dir + QLatin1Char('/')));
    };
    if (targetInfo.isSymLink() || parentCanonical.isEmpty() ||
        targetInfo.absoluteDir().canonicalPath() != parentCanonical) {
        QMessageBox::warning(window_, tr("Export LaTeX"),
                             tr("%1 is a link or not directly in the chosen folder, so it is left "
                                "alone. Choose another file name or folder.")
                                 .arg(target));
        return;
    }
    if (inside(targetCanonical, rootCanonical) || inside(rootCanonical, targetCanonical)) {
        QMessageBox::warning(window_, tr("Export LaTeX"),
                             tr("%1 would be inside the ZubuvZpevnik checkout (or contain it). "
                                "Choose another folder.")
                                 .arg(target));
        return;
    }

    QDir targetDir(target);
    if (targetDir.exists() &&
        !targetDir.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        // Only replace a folder an earlier export made (it holds the marker).
        const auto isMarker = [&targetDir](const QString& name) {
            const QFileInfo marker(targetDir.filePath(name));
            return marker.isFile() && !marker.isSymLink();
        };
        if (!isMarker(exportMarker) && !isMarker(oldExportMarker)) {
            QMessageBox::warning(window_, tr("Export LaTeX"),
                                 tr("%1 is not empty and was not made by an earlier export, so it "
                                    "is left alone. Choose another file name or folder.")
                                     .arg(target));
            return;
        }
        if (QMessageBox::question(window_, tr("Export LaTeX"),
                                  tr("Everything inside %1 will be deleted and written again.")
                                      .arg(target)) != QMessageBox::Yes)
            return;
        if (!targetDir.removeRecursively()) {
            QMessageBox::warning(window_, tr("Export LaTeX"), tr("Cannot remove %1").arg(target));
            return;
        }
    }
    if (!QDir().mkpath(target)) {
        QMessageBox::warning(window_, tr("Export LaTeX"), tr("Cannot create %1").arg(target));
        return;
    }
    // First, so that even a failed export leaves a folder the next one may replace.
    {
        QString error;
        if (!writeFile(targetDir.filePath(exportMarker),
                       "Exported by Zpěvníkář (ZubuvZpevnikar); this folder is replaced by the next export of "
                       "the same name.\n",
                       &error)) {
            QMessageBox::warning(window_, tr("Export LaTeX"), error);
            return;
        }
    }

    QStringList missing;
    for (const auto& copy : plan->copies) {
        const QString from = toQString(copy.from);
        const QString to = QDir(target).filePath(toQString(copy.to));
        if (!QFileInfo(from).isFile()) {
            missing << toQString(copy.from);
            continue;
        }
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (!QFile::copy(from, to)) {
            QMessageBox::warning(window_, tr("Export LaTeX"), tr("Cannot copy %1 to %2").arg(from, to));
            return;
        }
        // Copies keep the source's permissions; make sure the folder stays writable for the user.
        QFile::setPermissions(to, QFile::permissions(to) | QFileDevice::WriteOwner);
    }
    for (const auto& [relative, content] : plan->written) {
        QString error;
        if (!writeFile(QDir(target).filePath(toQString(relative)), content, &error)) {
            QMessageBox::warning(window_, tr("Export LaTeX"), error);
            return;
        }
    }
    const auto makeExecutable = [](const QString& path) {
        QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner |
                                        QFileDevice::ExeGroup | QFileDevice::ExeOther);
    };
    makeExecutable(QDir(target).filePath(QStringLiteral("build.sh")));
    const QDir scripts(QDir(target).filePath(QStringLiteral("scripts")));
    for (const QString& name : scripts.entryList({QStringLiteral("*.sh")}, QDir::Files))
        makeExecutable(scripts.filePath(name));

    // Songs in the book that were found: the plan's skipped list names the rest.
    std::set<std::string> all;
    for (const auto& section : book.sections)
        all.insert(section.songs.begin(), section.songs.end());
    const int songs = static_cast<int>(all.size() - plan->skipped.size() - plan->invalid.size());
    QString text = tr("Exported to %1\n%n song(s)", nullptr, songs).arg(target) +
                   skippedText(plan->skipped, plan->invalid) + warningsText(plan->warnings);
    if (!missing.isEmpty())
        text += tr("\n\nMissing files (not copied): %1").arg(missing.join(QStringLiteral(", ")));

    QMessageBox box(QMessageBox::Information, tr("Export LaTeX"), text, QMessageBox::Close, window_);
    QPushButton* open = box.addButton(tr("Open Folder"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

bool SongbookExporter::confirmSafety(const zub::songbook::Resolution& resolution) {
    QStringList lines;
    int unsafeSongs = 0;
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

void SongbookExporter::exportPdf(const QString& id, zub::songbook::Songbook book,
                                 const TitleOf& titleOf) {
    const auto resolution = zub::songbook::resolveSongs(book, root_);
    if (!confirmSafety(resolution))
        return;
    const QString target = QFileDialog::getSaveFileName(
        window_, tr("Export PDF"), QDir(lastExportDir()).filePath(id + QStringLiteral(".pdf")),
        tr("PDF (*.pdf)"));
    if (target.isEmpty())
        return;
    QSettings{}.setValue(exportDirKey, QFileInfo(target).absolutePath());

    const QString workDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                            QStringLiteral("/songbooks/") + id;
    if (!QDir().mkpath(workDir)) {
        QMessageBox::warning(window_, tr("Export PDF"), tr("Cannot create %1").arg(workDir));
        return;
    }
    const auto generated = zub::songbook::generate(book, existsIn(resolution), titleOf);
    QString error;
    if (!writeFile(workDir + QStringLiteral("/book.tex"), generated.tex, &error) ||
        (generated.akordy && !writeFile(workDir + QStringLiteral("/akordy.tex"), *generated.akordy, &error))) {
        QMessageBox::warning(window_, tr("Export PDF"), error);
        return;
    }
    // A stale index from an earlier song list would be used by the build.
    for (const char* stale : {"obsah.sbx", "obsahAutor.sbx", "book.pdf", "book.aux", "book.toc",
                              "book.out", "book.log", "book.sxc"})
        QFile::remove(workDir + QLatin1Char('/') + QLatin1String(stale));
    const QDir work(workDir);
    for (const QString& name : work.entryList({QStringLiteral("idxs*.sbx"), QStringLiteral("idxs*.sxd"),
                                               QStringLiteral("cover.*")},
                                              QDir::Files))
        QFile::remove(work.filePath(name));
    if (generated.coverSource &&
        !QFile::copy(toQString(*generated.coverSource), work.filePath(toQString(generated.coverName)))) {
        QMessageBox::warning(window_, tr("Export PDF"), tr("Cannot copy the cover image to %1").arg(workDir));
        return;
    }

    const auto command =
        zub::songbook::buildCommand(book, root_, toPath(workDir), "book", generated.indexes);
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
    dialog.setWindowTitle(tr("Building %1…").arg(toQString(book.title)));
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

    if (succeeded) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(target));
        emit finished(message);
        if (!generated.warnings.empty())
            QMessageBox::warning(window_, tr("Export PDF"),
                                 message + warningsText(generated.warnings));
    }
}
