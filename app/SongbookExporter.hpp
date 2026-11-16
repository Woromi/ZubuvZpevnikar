#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <zub/core/songbook.hpp>

class QWidget;

// Exports a songbook: as LaTeX (a .tex file or a self-contained folder) or as a PDF built with
// lualatex (in a dialog that shows the output). Dialogs are modal to the parent widget.
class SongbookExporter : public QObject {
    Q_OBJECT

public:
    explicit SongbookExporter(QWidget* parentWindow);

    // The songbook (its songs) and the package that builds them (zub::package).
    void setSongbookRoot(const std::filesystem::path& root, const std::filesystem::path& package) {
        root_ = root;
        package_ = package;
    }

    // The songbook is copied: the dialogs run nested event loops, the caller's copy may change.
    // `titleOf` gives a song's title (for estimating the index length of a Tabor book).
    using TitleOf = std::function<std::string(std::string_view inputName)>;
    void exportLatex(const QString& id, zub::songbook::Songbook book, const TitleOf& titleOf = {});
    void exportPdf(const QString& id, zub::songbook::Songbook book, const TitleOf& titleOf = {});
    // A book of the songbook repo (`bookFile`, its current `text`; `book` holds its songs for the
    // safety check), built as it is from a copy in CacheLocation/songbooks/repo-<hash>/.
    void exportRepoPdf(const std::filesystem::path& bookFile, std::string text,
                       zub::songbook::Songbook book);

signals:
    // A short report for the status bar after a successful PDF export.
    void finished(const QString& message);

private:
    QString noPackageText() const; // why nothing can be built without a package
    void exportTexFile(const QString& stem, const zub::songbook::Songbook& book,
                       const TitleOf& titleOf);
    void exportFolder(const QString& stem, const zub::songbook::Songbook& book,
                      const TitleOf& titleOf);
    // False (after a message) if a song contains commands outside the song format or cannot be
    // read and the user does not want to build anyway. Checks the files zub::songbook::resolveSongs
    // finds.
    // `extra`: what the check of the book file itself found (shown first).
    bool confirmSafety(const zub::songbook::Resolution& resolution, const QStringList& extra = {});
    // Runs the build in a dialog showing its output and copies the PDF to `target`; on success
    // `*message` is the report for the status bar.
    bool runBuild(const zub::songbook::BuildCommand& command, const QString& title,
                  const QString& target, QString* message);

    QWidget* window_;
    std::filesystem::path root_;
    std::filesystem::path package_;
};
