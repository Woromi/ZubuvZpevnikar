#pragma once

#include <QDialog>
#include <QString>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QLineEdit;
class QPushButton;

// File → Settings… and the activity bar's gear: the UI language, the General MIDI soundfont, whether build files are kept (buildfiles), and (to read) the package the songs are
// built with. The dialog only collects the choices; MainWindow stores them (Delete Now deletes at
// once, through State::deleteBuildFiles).
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    struct State {
        QString language;             // "" = system default, "cs", "en"
        QString soundfont;            // the one in use ("" = none found)
        bool soundfontChosen = false; // picked by the user
        QString package;              // the package in use ("" = none)
        bool appImage = false;  // runs as an AppImage (the menu entry is the app's own)
        bool menuEntry = true;  // it puts itself into the application menu
        bool keepBuildFiles = true;
        qint64 buildFilesBytes = 0;
        std::function<qint64()> deleteBuildFiles; // deletes what no build uses; the bytes left
    };
    explicit SettingsDialog(const State& state, QWidget* parent = nullptr);

    // What to store after OK: the language code; for the soundfont a path to remember, "" to
    // forget the choice (found automatically), or unchanged.
    QString language() const;
    bool soundfontChanged() const { return soundfontChanged_; }
    QString soundfont() const { return soundfont_; } // "" = found automatically
    bool keepBuildFiles() const;
    bool menuEntry() const;

private:
    void showSoundfont(const QString& path, bool chosen);
    void showBuildFilesSize(qint64 bytes);

    State state_;
    QComboBox* language_ = nullptr;
    QLineEdit* soundfontLine_ = nullptr;
    QPushButton* automatic_ = nullptr;
    QCheckBox* menuEntry_ = nullptr;
    QRadioButton* keepBuilds_ = nullptr;
    QRadioButton* rebuildAll_ = nullptr;
    QLabel* buildFilesSize_ = nullptr;
    QPushButton* deleteBuildFiles_ = nullptr;
    bool soundfontChanged_ = false;
    QString soundfont_;
};
