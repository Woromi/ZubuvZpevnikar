#pragma once

#include <QByteArray>
#include <QDialog>
#include <filesystem>
#include <memory>
#include <optional>
#include <zub/core/songimport.hpp>

class QDialogButtonBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTemporaryDir;
class QTimer;

// Imports one song with chords_converter: the user pastes a URL or picks a saved page, the
// converter runs in a fresh temporary directory, and the dialog is accepted with the result.
// Failures stay in the dialog with advice (bot checks: save the page, or pass a cookie).
class ImportDialog : public QDialog {
    Q_OBJECT

public:
    // `converter` may be empty or wrong; the user can choose the program in the dialog.
    explicit ImportDialog(std::filesystem::path converter, QWidget* parent = nullptr);
    ~ImportDialog() override; // kills a running conversion

    // Valid after the dialog was accepted.
    const zub::songimport::Imported& imported() const { return *imported_; }
    // The converter the dialog last used or the user chose (to remember it).
    const std::filesystem::path& converter() const { return converter_; }
    // The user picked the converter with "Choose…" (only then is it worth remembering).
    bool converterChosen() const { return converterChosen_; }

protected:
    void reject() override; // Cancel/Esc: stops a running conversion first

private:
    void startImport();
    void onFinished();
    void showFailure(const zub::songimport::Failure& failure);
    void setRunning(bool running);
    void chooseSource();
    void chooseConverter();
    void updateConverterLabel();
    void updateImportEnabled();
    bool converterExists() const;

    std::filesystem::path converter_;
    std::optional<zub::songimport::Imported> imported_;

    QLineEdit* source_ = nullptr;
    QPushButton* browse_ = nullptr;
    QLabel* converterLabel_ = nullptr;
    QPushButton* chooseConverter_ = nullptr;
    QGroupBox* botCheck_ = nullptr; // checkable; holds the cookie and user-agent fields
    QLineEdit* cookie_ = nullptr;
    QLineEdit* userAgent_ = nullptr;
    QLabel* status_ = nullptr;
    QPlainTextEdit* details_ = nullptr; // the converter's message on failure
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* importButton_ = nullptr;

    QProcess* process_ = nullptr;
    QTimer* watchdog_ = nullptr;
    std::unique_ptr<QTemporaryDir> workDir_; // one per run
    bool timedOut_ = false;
    bool converterChosen_ = false;
    bool cancelled_ = false;
    bool running_ = false;
    QByteArray out_;
    QByteArray err_;
};
