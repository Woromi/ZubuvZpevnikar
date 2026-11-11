#pragma once

#include <QObject>
#include <QString>
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

    void setSongbookRoot(const std::filesystem::path& root) { root_ = root; }

    // The songbook is copied: the dialogs run nested event loops, the caller's copy may change.
    // `titleOf` gives a song's title (for estimating the index length of a Tabor book).
    using TitleOf = std::function<std::string(std::string_view inputName)>;
    void exportLatex(const QString& id, zub::songbook::Songbook book, const TitleOf& titleOf = {});
    void exportPdf(const QString& id, zub::songbook::Songbook book, const TitleOf& titleOf = {});

signals:
    // A short report for the status bar after a successful PDF export.
    void finished(const QString& message);

private:
    void exportTexFile(const QString& stem, const zub::songbook::Songbook& book,
                       const TitleOf& titleOf);
    void exportFolder(const QString& stem, const zub::songbook::Songbook& book,
                      const TitleOf& titleOf);
    // False (after a message) if a song contains commands outside the song format or cannot be
    // read and the user does not want to build anyway. Checks the files zub::songbook::resolveSongs
    // finds.
    bool confirmSafety(const zub::songbook::Resolution& resolution);

    QWidget* window_;
    std::filesystem::path root_;
};
