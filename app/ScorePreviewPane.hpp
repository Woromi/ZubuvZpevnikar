#pragma once

#include <QList>
#include <QPoint>
#include <QString>
#include <QWidget>
#include <optional>

struct ScoreResult;
class QAction;
class QComboBox;
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

    // Playback: the Play/Stop button and the tempo. Disabled with `reason` as its tooltip when
    // sound is not available (no fluidsynth or soundfont).
    enum class PlayState { Idle, Preparing, Playing };
    void setPlaybackAvailable(bool available, const QString& reason);
    void setPlayState(PlayState state);
    void showPlaybackMessage(const QString& message, bool error); // in the status line
    double tempoFactor() const; // 0.5 ... 1.25 of the song's tempo
    QWidget* playButton() const; // for tests

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
    // The leftmost (`right`: rightmost) selectable note on the staff the score cursor is on (x =
    // column, y = line); none without a score cursor (the links are stale) or off any staff.
    std::optional<QPoint> staffEdgeNote(bool right) const;
    QWidget* canvasWidget() const; // takes the keyboard focus; for tests
    // The actions of the context menu (the owner keeps them and their enabled state).
    void setRegionActions(const QList<QAction*>& actions);

    // Keys that edit the score like a text cursor, while the score has the keyboard focus.
    enum class KeyCommand { Left, Right, Home, End, Backspace, Delete, Undo, Redo };

signals:
    void rebuildRequested();
    void tempoChanged(); // the playback speed combo changed: the MIDI file must be built again
    void playRequested(); // the Play button was pressed while idle
    void stopRequested(); // ... or Stop while preparing or playing
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
    // A key of the score cursor: `extend` is Shift (select), `wholeScore` is Ctrl (Home/End go to
    // the whole score's start/end instead of the staff's).
    void keyCommand(ScorePreviewPane::KeyCommand command, bool extend, bool wholeScore);
    // A note was double-clicked: select exactly it.
    void noteDoubleClicked(const QPoint& position);

private:
    void showLog();

    QToolButton* rebuild_ = nullptr;
    QToolButton* logButton_ = nullptr;
    QToolButton* play_ = nullptr;
    QComboBox* tempo_ = nullptr;
    PlayState playState_ = PlayState::Idle;
    bool playbackAvailable_ = true;
    QString unavailableReason_;
    QLabel* status_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    SvgCanvas* canvas_ = nullptr;
    QListWidget* errors_ = nullptr;
    QString log_;
    QList<QAction*> regionActions_;
};
