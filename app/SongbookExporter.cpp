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

std::optional<zub::songbook::BuildCommand>
SongbookExporter::prepareRepoBuild(const std::filesystem::path& bookFile, const std::string& text) {
    if (package_.empty()) {
        QMessageBox::warning(window_, tr("Export PDF"), noPackageText());
        return std::nullopt;
    }
    const auto parsed = zub::bookfile::parse(text);
    if (!parsed) {
        QMessageBox::warning(window_, tr("Export PDF"), toQString(parsed.error()));
        return std::nullopt;
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
        return std::nullopt;

    // One work dir per book file: LilyPond's cache and the index files stay between builds.
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(toQString(bookFile).toUtf8(), QCryptographicHash::Sha1)
            .toHex()
            .left(12));
    const QString workDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                            QStringLiteral("/songbooks/repo-") + hash;
    if (!QDir().mkpath(workDir)) {
        QMessageBox::warning(window_, tr("Build"), tr("Cannot create %1").arg(workDir));
        return std::nullopt;
    }
    // A stale index from an earlier song list would be used by the build.
    const QDir work(workDir);
    for (const QString& name :
         work.entryList({QStringLiteral("*.sbx"), QStringLiteral("*.sxd"), QStringLiteral("book.*")},
                        QDir::Files))
        QFile::remove(work.filePath(name));
    QString error;
    if (!writeFile(workDir + QStringLiteral("/book.tex"), text, &error)) {
        QMessageBox::warning(window_, tr("Build"), error);
        return std::nullopt;
    }
    return zub::bookfile::buildCommand(root_, package_, toPath(workDir), bookDir, "book",
                                       zub::bookfile::indexFiles(*parsed));
}

QProcess* SongbookExporter::createBuildProcess(const zub::songbook::BuildCommand& command,
                                               QObject* parent) {
    auto* process = new QProcess(parent);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setWorkingDirectory(toQString(command.workingDirectory));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (const auto& [name, value] : command.environment)
        environment.insert(toQString(name), toQString(value));
    process->setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
    // Its own process group: Cancel stops lualatex and lilypond with it.
    process->setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
    // The build must not outlive the app.
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, process,
                     [process] { killTree(process); });
    return process;
}

void SongbookExporter::startBuild(QProcess* process, const zub::songbook::BuildCommand& command) {
    QStringList arguments;
    for (const std::string& argument : command.arguments)
        arguments << toQString(argument);
    process->start(toQString(command.program), arguments);
}

void SongbookExporter::stopBuild(QProcess* process) {
    killTree(process);
}

QString SongbookExporter::buildReport(const QByteArray& output,
                                      const std::filesystem::path& log) {
    QString report;
    static const QRegularExpression pages(QStringLiteral(R"(Finished: .*\((\d+) pages?\))"));
    if (const auto match = pages.match(QString::fromUtf8(output)); match.hasMatch())
        report = tr("%n page(s)", nullptr, match.captured(1).toInt());
    QFile logFile(toQString(log));
    if (logFile.open(QIODevice::ReadOnly)) {
        const QByteArray bytes = logFile.readAll();
        const int warnings = zub::songbook::paperWarnings(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        if (warnings > 0)
            report += (report.isEmpty() ? QString() : QStringLiteral(", ")) +
                      tr("%n song(s) not marked as fitting this paper", nullptr, warnings);
    }
    return report;
}

void SongbookExporter::savePdf(const QByteArray& pdf, const QString& stem, const QString& report) {
    const QString target = QFileDialog::getSaveFileName(
        window_, tr("Export PDF"), QDir(lastExportDir()).filePath(stem + QStringLiteral(".pdf")),
        tr("PDF (*.pdf)"));
    if (target.isEmpty())
        return;
    QSettings{}.setValue(exportDirKey, QFileInfo(target).absolutePath());
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(pdf) != pdf.size() || !file.commit()) {
        QMessageBox::warning(window_, tr("Export PDF"),
                             tr("Cannot write %1: %2").arg(target, file.errorString()));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
    emit finished(report.isEmpty() ? tr("Exported %1").arg(target)
                                   : tr("Exported %1 (%2)").arg(target, report));
}
