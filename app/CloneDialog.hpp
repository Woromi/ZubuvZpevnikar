#pragma once

#include <QDialog>
#include <QString>
#include <QStringDecoder>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;

// Get the Songbook…: `git clone --recurse-submodules <url> <folder>` with its output, a progress
// line and Cancel. The folder must not exist or be empty. Accepted when the clone succeeded;
// folder() is then the songbook to open. Cancel stops git and deletes nothing.
class CloneDialog : public QDialog {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    explicit CloneDialog(QWidget* parent = nullptr);
    ~CloneDialog() override;

    QString folder() const; // absolute; "~" is the home folder, a relative path is from it
    static QString defaultUrl();
    static QString git(); // the git program, "" when there is none on PATH

    void reject() override; // stops a running clone

private:
    void chooseFolder();
    void start();
    void onFinished(int exitCode, bool crashed);
    void updateButtons();

    QLineEdit* url_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QPushButton* browse_ = nullptr;
    QPushButton* clone_ = nullptr;
    QLabel* status_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QProcess* process_ = nullptr;
    QStringDecoder decoder_{QStringDecoder::Utf8};
    QString pending_; // output after the last line break
};
