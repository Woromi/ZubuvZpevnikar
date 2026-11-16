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

// Builds a songbook of the songbook repo as a PDF with lualatex (in a dialog that shows the
// output), after the safety checks. Dialogs are modal to the parent widget.
class SongbookExporter : public QObject {
    Q_OBJECT

public:
    explicit SongbookExporter(QWidget* parentWindow);

    // The songbook (its songs) and the package that builds them (zub::package).
    void setSongbookRoot(const std::filesystem::path& root, const std::filesystem::path& package) {
        root_ = root;
        package_ = package;
    }

    // A book of the songbook repo (`bookFile`, its current `text`; `book` gives the title), built
    // as it is from a copy in CacheLocation/songbooks/repo-<hash>/ after checking the book file,
    // a Tabor book's akordy.tex, every \input (a song of the songbook, then the song itself) and
    // stray index files next to it.
    void exportRepoPdf(const std::filesystem::path& bookFile, std::string text,
                       zub::songbook::Songbook book);

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
    // Runs the build in a dialog showing its output and copies the PDF to `target`; on success
    // `*message` is the report for the status bar.
    bool runBuild(const zub::songbook::BuildCommand& command, const QString& title,
                  const QString& target, QString* message);

    QWidget* window_;
    std::filesystem::path root_;
    std::filesystem::path package_;
};
