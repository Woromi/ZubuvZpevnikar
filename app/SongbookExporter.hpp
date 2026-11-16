#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <zub/core/songbook.hpp>

class QProcess;
class QWidget;

// Building a songbook of the songbook repo as a PDF with lualatex: the safety checks and the
// work dir, the build process (run by the page's preview), and saving the PDF. Dialogs are modal
// to the parent widget.
class SongbookExporter : public QObject {
    Q_OBJECT

public:
    explicit SongbookExporter(QWidget* parentWindow);

    // The songbook (its songs) and the package that builds them (zub::package).
    void setSongbookRoot(const std::filesystem::path& root, const std::filesystem::path& package) {
        root_ = root;
        package_ = package;
    }

    // Before building a book of the songbook repo (`bookFile` with its current `text`): the
    // safety checks of the book file, a Tabor book's akordy.tex, every \input (a song of the
    // songbook, then the song itself) and stray index files next to it (asking when something is
    // unknown), then the work dir CacheLocation/songbooks/repo-<hash>/ with the text as book.tex.
    // nullopt (after a message) when it is not to be built.
    std::optional<zub::songbook::BuildCommand> prepareRepoBuild(
        const std::filesystem::path& bookFile, const std::string& text);
    // A process for `command` (environment, working dir, its own process group, stopped when the
    // app quits); startBuild runs it, stopBuild stops it and what it started.
    static QProcess* createBuildProcess(const zub::songbook::BuildCommand& command,
                                        QObject* parent);
    static void startBuild(QProcess* process, const zub::songbook::BuildCommand& command);
    static void stopBuild(QProcess* process);
    // "N pages, M songs not marked as fitting this paper" from a build's output and log.
    static QString buildReport(const QByteArray& output, const std::filesystem::path& log);
    // Asks where to save `pdf` (named <stem>.pdf), writes it and opens it.
    void savePdf(const QByteArray& pdf, const QString& stem, const QString& report);

signals:
    // A short report for the status bar after a successful PDF export.
    void finished(const QString& message);

private:
    QString noPackageText() const; // why nothing can be built without a package
    // False (after a message) if a song contains commands outside the song format or cannot be
    // read and the user does not want to build anyway. Checks the files zub::songbook::resolveSongs
    // finds.
    // `extra`: what the check of the book file itself found (shown first).
    bool confirmSafety(const zub::songbook::Resolution& resolution, const QStringList& extra = {});

    QWidget* window_;
    std::filesystem::path root_;
    std::filesystem::path package_;
};
