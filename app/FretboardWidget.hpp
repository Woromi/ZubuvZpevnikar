#pragma once

#include <QPoint>
#include <QWidget>
#include <vector>
#include <zub/core/fretboard.hpp>

// A fretboard for writing tablature: the strings of the score's tuning (string 1 on top), the nut,
// frets 0..fretCount, the markers at 3, 5, 7, 9 and 12. The positions of the note at the score's
// cursor are hollow rings in the text colour; the fret under the mouse is highlighted. Clicks stage
// positions (at most one per string, drawn as filled dots in the Highlight colour on top). A short string (the banjo's 5th)
// is drawn from its own first fret. Colours come from the palette, so it follows light and dark.
class FretboardWidget : public QWidget {
    Q_OBJECT

public:
    explicit FretboardWidget(QWidget* parent = nullptr);

    void setTuning(const zub::fretboard::Tuning& tuning);
    const zub::fretboard::Tuning& tuning() const { return tuning_; }
    // Note names of the string labels and the tooltip.
    void setNaming(bool german, bool flats);
    bool german() const { return german_; }
    bool flats() const { return flats_; }
    void setFretCount(int count); // the highest fret drawn, default 15
    // `fret` is relative to the string's own nut (zub::fretboard::Position).
    void setMarked(const std::vector<zub::fretboard::Position>& marked);
    const std::vector<zub::fretboard::Position>& marked() const { return marked_; }

    // Staged positions: a click toggles one (a click on another fret of a string that has one moves
    // it). Cleared when the tuning changes. Sorted by string.
    const std::vector<zub::fretboard::Position>& staged() const { return staged_; }
    void toggleStaged(int string, int fret);
    void stage(int string, int fret); // the string's staged position is this one
    void clearStaged();

    // The pitch of a position (MIDI) and the position under a widget point (string 0 for none):
    // fret relative to the string's own nut.
    int midiAt(int string, int fret) const;
    zub::fretboard::Position positionAt(const QPoint& point) const;
    // The centre of a position's cell, for tests.
    QPoint cellCenter(int string, int fret) const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void stagedChanged();
    // A fret was clicked (staged or unstaged by it): for the sound of the note. Not emitted when
    // the staging is loaded from the score.
    void fretClicked(int string, int fret);
    // A fret was double-clicked: it is staged, insert everything staged.
    void insertRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Geometry {
        double left;      // x of the nut
        double openWidth; // width of the open-string column left of the nut
        double cell;      // width of one fret
        double top;       // y of string 1
        double spacing;   // between strings
    };
    Geometry geometry() const;
    int stringCount() const { return static_cast<int>(tuning_.open.size()); }
    int physicalFret(int string, int fret) const;
    double cellCenterX(const Geometry& g, int physical) const;

    zub::fretboard::Tuning tuning_ = zub::fretboard::guitar();
    bool german_ = false;
    bool flats_ = false;
    int fretCount_ = 15;
    std::vector<zub::fretboard::Position> marked_;
    std::vector<zub::fretboard::Position> staged_;
    zub::fretboard::Position hover_{0, 0};
};
