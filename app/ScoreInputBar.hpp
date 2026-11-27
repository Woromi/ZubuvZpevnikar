#pragma once

#include <QString>
#include <QWidget>

class QAction;
class QButtonGroup;
class QComboBox;
class QKeyEvent;
class QToolButton;

// The note length for the next note and the buttons for a rest and a bar check, above the
// fretboard. It shows the length in force at the score's cursor (setDuration); picking another one
// sets it for the next note.
class ScoreInputBar : public QWidget {
    Q_OBJECT

public:
    explicit ScoreInputBar(QWidget* parent = nullptr);

    // "4", "8." ...: the chosen length. Lengths without a button (32, 64) are kept as they are.
    QString duration() const { return duration_; }
    // Shows `duration` (the one in force at the cursor) without emitting anything.
    void setDuration(const QString& duration);
    // The number of staged fretboard positions: Insert and Clear are enabled while it is not 0.
    void setStagedCount(int count);
    // The Insert button reads "Replace" while the staged notes replace the selected note or chord.
    void setReplaceMode(bool replace);

    // Picks a length ("1" ... "16", "8." with the dot) as a click on its button would; the dot
    // stays as it is when `length` has none of its own. Lengths without a button are ignored.
    void pickLength(const QString& length);
    // Toggles the dot like a click on its button.
    void toggleDot();
    // The keys of note input while the score preview or the fretboard has the focus (not the text
    // editor): 1 2 4 8 6 (16) pick a length, . the dot, r a rest, | a bar check. True for those.
    static bool isInputKey(const QKeyEvent* event);
    bool handleKey(const QString& text);
    // The keys named in the Insert and Clear tooltips (as shown to the user, from the actions).
    void setKeyHints(const QString& insertKey, const QString& clearKey);

    enum class TuningChoice { Automatic, Guitar, Banjo };
    // The fretboard's tuning as the combo has it (per score tab, session only). Automatic follows
    // the staff at the cursor.
    TuningChoice tuningChoice() const;

signals:
    void restRequested();
    void barCheckRequested();
    void insertRequested();
    void clearRequested();
    void tuningChoiceChanged();

private:
    void onPicked();
    void showDuration();

    QButtonGroup* group_ = nullptr;
    QToolButton* dot_ = nullptr;
    QToolButton* insert_ = nullptr;
    QToolButton* clear_ = nullptr;
    QComboBox* tuning_ = nullptr;
    QString insertKey_, clearKey_;
    bool replace_ = false;
    void showInsertTip();
    QString duration_ = QStringLiteral("4");
    bool updating_ = false;
};
