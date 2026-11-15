#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLineEdit;
class QPushButton;

// File → Settings… and the activity bar's gear: the UI language, the converter, the General MIDI
// soundfont, and (to read) the package the songs are built with. The dialog only collects the
// choices; MainWindow stores them.
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    struct State {
        QString language;             // "" = system default, "cs", "en"
        QString converter;            // the one in use ("" = none found)
        QString builtInConverter;     // built with the app ("" = none)
        bool converterChosen = false; // `converter` was picked by the user (remembered)
        QString soundfont;            // the one in use ("" = none found)
        bool soundfontChosen = false; // picked by the user
        QString package;              // the package in use ("" = none)
        bool ownPackage = false;      // it is the songbook's own (else the app's copy)
    };
    explicit SettingsDialog(const State& state, QWidget* parent = nullptr);

    // What to store after OK: the language code; for the converter and the soundfont a path
    // to remember, "" to forget the choice (built-in / found automatically), or unchanged.
    QString language() const;
    bool converterChanged() const { return converterChanged_; }
    QString converter() const { return converter_; } // "" = back to the built-in one
    bool soundfontChanged() const { return soundfontChanged_; }
    QString soundfont() const { return soundfont_; } // "" = found automatically

private:
    void showConverter(const QString& path, bool chosen);
    void showSoundfont(const QString& path, bool chosen);

    State state_;
    QComboBox* language_ = nullptr;
    QLineEdit* converterLine_ = nullptr;
    QPushButton* useBuiltIn_ = nullptr;
    QLineEdit* soundfontLine_ = nullptr;
    QPushButton* automatic_ = nullptr;
    bool converterChanged_ = false;
    QString converter_;
    bool soundfontChanged_ = false;
    QString soundfont_;
};
