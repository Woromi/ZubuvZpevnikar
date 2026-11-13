#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QAction;
class QButtonGroup;
class QHBoxLayout;
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
    // A row of buttons for the actions on the selected notes (tie, slur ...); the buttons follow
    // the actions' enabled state. Owned by the caller.
    void setRegionActions(const QList<QAction*>& actions);

signals:
    void restRequested();
    void barCheckRequested();

private:
    void onPicked();
    void showDuration();

    QButtonGroup* group_ = nullptr;
    QToolButton* dot_ = nullptr;
    QHBoxLayout* regionRow_ = nullptr;
    QString duration_ = QStringLiteral("4");
    bool updating_ = false;
};
