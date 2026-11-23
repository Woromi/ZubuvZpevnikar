#pragma once

#include <QDialog>
#include <QString>
#include <QStringDecoder>

class QCheckBox;
class QLabel;
class QRadioButton;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;

// New Song Library…: a song library (the folder with songs/ and songbooks/, what the app opens)
// in a folder that must not exist or be empty, either
// - created empty (zub::createSongbookRepo; then `git init` when git is there and wanted), the
//   default, or
// - downloaded: `git clone --recurse-submodules <url> <folder>` with its output, a progress line
//   and Cancel (which stops git and deletes nothing).
// Accepted when it is there; folder() is then the library to open.
class LibraryDialog : public QDialog {
    Q_OBJECT
    friend struct AppTestAccess;

public:
    explicit LibraryDialog(QWidget* parent = nullptr);
    ~LibraryDialog() override;

    QString folder() const; // absolute; "~" is the home folder, a relative path is from it
    static QString defaultUrl();
    static QString git(); // the git program, "" when there is none on PATH

    void reject() override; // stops a running clone

private:
    void chooseFolder();
    void start();
    void onFinished(int exitCode, bool crashed);
    void updateButtons();

    bool downloading() const;
    QString defaultFolder() const; // for the chosen way
    void createLibrary(const QString& target);
    void startGit(const QStringList& arguments, const QString& workingDirectory);

    QRadioButton* create_ = nullptr;
    QRadioButton* download_ = nullptr;
    QCheckBox* useGit_ = nullptr;
    bool initializing_ = false; // the running git is `git init` of a created library
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
