#pragma once

#include <QWidget>
#include <string>
#include <vector>

struct PreviewResult;
class QBuffer;
class QComboBox;
class QLabel;
class QListWidget;
class QPdfDocument;
class QPdfView;
class QToolButton;

// Shows the PDF of the latest successful preview build, the build status, and the first
// error of a failed build. A failed build keeps the last good PDF on screen.
class PreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit PreviewPane(QWidget* parent = nullptr);

    // Fills the class chooser and selects `current` (or the first class if it is missing).
    // Does not emit documentClassChanged.
    void setDocumentClasses(const std::vector<std::string>& classes, const QString& current);
    QString documentClass() const;

    void showBuilding();
    void showResult(const PreviewResult& result);
    void clear(); // no song selected: empty view, no status, no error

signals:
    void documentClassChanged(const QString& documentClass);
    void rebuildRequested();
    // An error located in the song was double-clicked; inputName is the song it belongs to.
    void errorActivated(const QString& inputName, int songLine);

private:
    void showLog();

    QComboBox* classes_ = nullptr;
    QToolButton* rebuild_ = nullptr;
    QToolButton* logButton_ = nullptr;
    QLabel* status_ = nullptr;
    QPdfView* view_ = nullptr;
    QPdfDocument* document_ = nullptr;
    // QPdfDocument reads from it, so it lives as long as the document
    QBuffer* pdfBuffer_ = nullptr;
    QListWidget* errors_ = nullptr;
    QString log_;
    QString errorInputName_; // the song whose error the list shows
};
