#pragma once

#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>

struct ScoreResult;
class QAction;
class QLabel;
class QListWidget;
class QScrollArea;
class QToolButton;
class SvgCanvas;

// Shows the engraved SVG of the latest successful score build, the build status, and the errors
// of a failed build. A failed build keeps the last good image on screen.
class ScorePreviewPane : public QWidget {
    Q_OBJECT

public:
    explicit ScorePreviewPane(QWidget* parent = nullptr);

    void showBuilding();
    void showResult(const ScoreResult& result);
    // The score is not built (e.g. not approved): `message` is shown in place of the errors.
    void showBlocked(const QString& message);
    void clear(); // no score: empty view, no status, no errors

    // Where the note that LilyPond linked to `line` (1-based) and `column` (0-based) is drawn, in
    // widget coordinates of the score view; a null point when the score has no such link. For
    // tests.
    QPoint noteViewPosition(int line, int column) const;
    QWidget* scoreView() const; // the widget that shows the score (clicks on it jump to notes)

    // Where the notes of the score are linked to: x = 0-based column, y = 1-based line (like a
    // click's noteClicked), every link of the last successful build.
    QList<QPoint> notePositions() const;
    // The links that are notes of the music (the others are chord names, lyrics ...): only these
    // can be clicked, dragged over, or carry the caret. A new build clears them.
    void setSelectableNotes(const QList<QPoint>& positions);
    // The score cursor, a vertical line before (`after`: behind) the note at this position
    // (x = column, y = line); a null point hides it.
    void setCaretNote(const QPoint& position, bool after = false);
    bool hasCaret() const; // for tests
    // Draws a translucent box around the notes at these positions (the editor's selection);
    // empty clears them. A new build clears them too: positions refer to its text.
    void setHighlightedNotes(const QList<QPoint>& positions);
    int highlightedNoteCount() const; // how many links are drawn highlighted; for tests
    // The actions of the context menu (the owner keeps them and their enabled state).
    void setRegionActions(const QList<QAction*>& actions);

signals:
    void rebuildRequested();
    // An error located in the score was double-clicked (1-based line and column).
    void errorActivated(int line, int column);
    // A note of the rendered score was clicked: its position in the score text (x = 0-based
    // column in code points, y = 1-based line). `after`: the click was right of the note (the text
    // cursor goes behind it, else before it). `extend`: Shift was held (select up to it).
    void noteClicked(const QPoint& position, bool after, bool extend);
    // The mouse was dragged from the note `anchor` over the score; `current` is the note nearest
    // the mouse, each with the side the mouse was on. Emitted whenever that changes.
    void notesDragged(const QPoint& anchor, bool anchorAfter, const QPoint& current,
                      bool currentAfter);
    // A note was double-clicked: select exactly it.
    void noteDoubleClicked(const QPoint& position);

private:
    void showLog();

    QToolButton* rebuild_ = nullptr;
    QToolButton* logButton_ = nullptr;
    QLabel* status_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    SvgCanvas* canvas_ = nullptr;
    QListWidget* errors_ = nullptr;
    QString log_;
    QList<QAction*> regionActions_;
};
