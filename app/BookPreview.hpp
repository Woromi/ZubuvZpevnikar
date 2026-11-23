#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>
#include <filesystem>
#include <functional>
#include <optional>
#include <map>
#include <string>
#include <zub/core/songbook.hpp>

class QBuffer;
class QLabel;
class QPdfDocument;
class PdfLinkView;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class SongbookExporter;

// The Songbooks page's Preview tab: the PDF of the shown book, built on a button (a whole book
// takes a minute or more) in the app's cache after the exporter's safety checks, with the build's
// output while it runs and on failure. One build at a time; a build keeps running when another
// book is shown, and its PDF is kept for its book (for this session) until the next build of it.
class BookPreview : public QWidget {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    explicit BookPreview(SongbookExporter* exporter, QWidget* parent = nullptr);
    ~BookPreview() override;

    // The book shown: its id (relative path), file, current text and title.
    void setBook(const QString& id, const std::filesystem::path& file, const std::string& text);

    // What a build typesets for the book's text: the book copy's text and copies of songs (input
    // name -> text) that stand in for theirs in this build only (transposing for one export).
    struct BuildInput {
        std::string text;
        std::map<std::string, std::string> songs;
    };
    // Makes the build input from the shown book's text (nullopt: not built, after a message);
    // without one the text is built as it is.
    using Composer = std::function<std::optional<BuildInput>(const std::string& text)>;
    void setComposer(Composer composer) { composer_ = std::move(composer); }
    // What the composer does to the shown book now: `variant` for people ("" nothing), and a
    // `signature` that tells the variants apart (a PDF built with another one is out of date).
    void setVariant(const QString& variant, const std::string& signature);

    // A build of the shown book (after the safety checks); false if it did not start.
    bool build();
    bool building() const;
    // The shown book's last PDF is of its current text.
    bool upToDate() const;
    QByteArray pdf() const; // the shown book's last PDF ("" if none)
    QString report() const; // its "N pages, M songs not marked as fitting" summary

signals:
    // A build of `id` finished (ok: a PDF was made).
    void built(const QString& id, bool ok);

private:
    struct Result {
        QByteArray pdf;
        std::string text; // what it was built from
        QString variant;       // with this composer variant (for people)
        std::string signature; // ... and its signature
        QString report;
        QString log;            // the build's output, when it failed
        bool cancelled = false; // the last build was cancelled (nothing failed)
    };
    void updateView(); // the shown book's result into the widgets
    void onFinished(int exitCode, bool crashed);

    SongbookExporter* exporter_;
    QString id_;
    std::filesystem::path file_;
    std::string text_;
    QString variant_;
    std::string signature_;
    Composer composer_;
    std::map<QString, Result> results_;

    QProcess* process_ = nullptr; // the running build
    QString buildingId_;
    std::string buildingText_;
    QString buildingVariant_;
    std::string buildingSignature_;
    std::filesystem::path buildingPdf_, buildingLog_;
    QByteArray output_;
    bool cancelled_ = false; // the running build was stopped by Cancel

    QLabel* status_ = nullptr;
    QPushButton* build_ = nullptr;
    QPushButton* cancel_ = nullptr;
    PdfLinkView* view_ = nullptr;
    QPdfDocument* document_ = nullptr;
    QBuffer* buffer_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
};
