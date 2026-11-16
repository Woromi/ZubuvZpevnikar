#pragma once

#include <QByteArray>
#include <QString>
#include <QWidget>
#include <filesystem>
#include <map>
#include <string>
#include <zub/core/songbook.hpp>

class QBuffer;
class QLabel;
class QPdfDocument;
class QPdfView;
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

public:
    explicit BookPreview(SongbookExporter* exporter, QWidget* parent = nullptr);
    ~BookPreview() override;

    // The book shown: its id (relative path), file, current text and title.
    void setBook(const QString& id, const std::filesystem::path& file, const std::string& text);

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
    std::map<QString, Result> results_;

    QProcess* process_ = nullptr; // the running build
    QString buildingId_;
    std::string buildingText_;
    std::filesystem::path buildingPdf_, buildingLog_;
    QByteArray output_;
    bool cancelled_ = false; // the running build was stopped by Cancel

    QLabel* status_ = nullptr;
    QPushButton* build_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QPdfView* view_ = nullptr;
    QPdfDocument* document_ = nullptr;
    QBuffer* buffer_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
};
