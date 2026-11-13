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
    // click's noteActivated), every link of the last successful build.
    QList<QPoint> notePositions() const;
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
    // A note of the rendered score was clicked: the position of its text in the score (1-based
    // line, 0-based column in code points).
    void noteActivated(int line, int column);
    // A rectangle was dragged over the score: the notes inside it (x = column, y = line); a
    // rectangle without notes gives an empty list.
    void regionSelected(const QList<QPoint>& positions);

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
