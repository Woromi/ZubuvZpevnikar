#include "FretboardWidget.hpp"

#include "convert.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace {

constexpr int labelWidth = 34;
constexpr int rowHeight = 20;
constexpr int marginTop = 8;
constexpr int marginBottom = 18; // fret numbers

} // namespace

FretboardWidget::FretboardWidget(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void FretboardWidget::setTuning(const zub::fretboard::Tuning& tuning) {
    if (tuning.open == tuning_.open && tuning.shortString == tuning_.shortString)
        return;
    tuning_ = tuning;
    hover_ = {0, 0};
    updateGeometry();
    update();
}

void FretboardWidget::setNaming(bool german, bool flats) {
    if (german == german_ && flats == flats_)
        return;
    german_ = german;
    flats_ = flats;
    update();
}

void FretboardWidget::setFretCount(int count) {
    fretCount_ = std::clamp(count, 1, 24);
    update();
}

void FretboardWidget::setMarked(const std::vector<zub::fretboard::Position>& marked) {
    if (marked == marked_)
        return;
    marked_ = marked;
    update();
}

int FretboardWidget::midiAt(int string, int fret) const {
    if (string < 1 || string > stringCount())
        return 0;
    return tuning_.open[static_cast<std::size_t>(string - 1)] + fret;
}

int FretboardWidget::physicalFret(int string, int fret) const {
    return string == tuning_.shortString ? fret + zub::fretboard::shortStringStartFret : fret;
}

QSize FretboardWidget::sizeHint() const {
    return {labelWidth + 40 * (fretCount_ + 1) + 8,
            marginTop + rowHeight * std::max(1, stringCount()) + marginBottom};
}

QSize FretboardWidget::minimumSizeHint() const {
    return {labelWidth + 16 * (fretCount_ + 1), sizeHint().height()};
}

FretboardWidget::Geometry FretboardWidget::geometry() const {
    Geometry g;
    g.left = labelWidth + 18.0;
    g.openWidth = 18.0;
    g.cell = std::max(8.0, (width() - g.left - 6.0) / fretCount_);
    g.top = marginTop + rowHeight / 2.0;
    g.spacing = rowHeight;
    return g;
}

double FretboardWidget::cellCenterX(const Geometry& g, int physical) const {
    // Fret 0 is the open string left of the nut, fret n sits between wire n-1 and wire n.
    return physical == 0 ? g.left - g.openWidth / 2 : g.left + (physical - 0.5) * g.cell;
}

QPoint FretboardWidget::cellCenter(int string, int fret) const {
    const Geometry g = geometry();
    return {static_cast<int>(std::lround(cellCenterX(g, physicalFret(string, fret)))),
            static_cast<int>(std::lround(g.top + (string - 1) * g.spacing))};
}

zub::fretboard::Position FretboardWidget::positionAt(const QPoint& point) const {
    const Geometry g = geometry();
    const int string = static_cast<int>(std::lround((point.y() - g.top) / g.spacing)) + 1;
    if (string < 1 || string > stringCount())
        return {0, 0};
    if (std::abs(point.y() - (g.top + (string - 1) * g.spacing)) > g.spacing / 2)
        return {0, 0};
    int physical;
    if (point.x() < g.left - g.openWidth)
        return {0, 0};
    if (point.x() < g.left)
        physical = 0;
    else
        physical = static_cast<int>((point.x() - g.left) / g.cell) + 1;
    if (physical > fretCount_)
        return {0, 0};
    const int fret =
        string == tuning_.shortString ? physical - zub::fretboard::shortStringStartFret : physical;
    // The short string has no frets below its start (and no open position).
    if (fret < 0 ||
        (string == tuning_.shortString && physical < zub::fretboard::shortStringStartFret))
        return {0, 0};
    return {string, fret};
}

void FretboardWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const Geometry g = geometry();
    const QColor ink = palette().color(QPalette::WindowText);
    const QColor paper = palette().color(QPalette::Window);
    const QColor accent = palette().color(QPalette::Highlight);
    const QColor wood = palette().color(QPalette::AlternateBase);
    QColor faint = ink;
    faint.setAlphaF(0.35f);
    QColor strong = ink;
    strong.setAlphaF(0.8f);

    const double bottom = g.top + (stringCount() - 1) * g.spacing;
    const double right = g.left + fretCount_ * g.cell;
    const double boardTop = g.top - g.spacing / 2;
    const double boardBottom = bottom + g.spacing / 2;
    p.fillRect(QRectF(g.left, boardTop, right - g.left, boardBottom - boardTop), wood);

    // Position markers: single dots at 3, 5, 7, 9, 15 and a double dot at 12.
    p.setPen(Qt::NoPen);
    p.setBrush(faint);
    const double mid = (g.top + bottom) / 2;
    for (const int f : {3, 5, 7, 9, 15, 12}) {
        if (f > fretCount_)
            continue;
        const double x = cellCenterX(g, f);
        const double r = 3.5;
        if (f == 12) {
            const double offset = g.spacing * std::max(0.5, (stringCount() - 1) / 4.0);
            p.drawEllipse(QPointF(x, mid - offset), r, r);
            p.drawEllipse(QPointF(x, mid + offset), r, r);
        } else {
            p.drawEllipse(QPointF(x, mid), r, r);
        }
    }

    // Fret wires, and the nut.
    p.setPen(QPen(faint, 1));
    for (int f = 1; f <= fretCount_; ++f)
        p.drawLine(QPointF(g.left + f * g.cell, boardTop),
                   QPointF(g.left + f * g.cell, boardBottom));
    p.setPen(QPen(strong, 4, Qt::SolidLine, Qt::FlatCap));
    p.drawLine(QPointF(g.left, boardTop), QPointF(g.left, boardBottom));

    // Fret numbers.
    QFont small = font();
    small.setPointSizeF(std::max(6.0, small.pointSizeF() * 0.8));
    p.setFont(small);
    p.setPen(strong);
    for (const int f : {3, 5, 7, 9, 12, 15}) {
        if (f <= fretCount_)
            p.drawText(QRectF(g.left + (f - 1) * g.cell, boardBottom + 1, g.cell, marginBottom - 2),
                       Qt::AlignHCenter | Qt::AlignTop, QString::number(f));
    }

    // Strings (thicker towards the bass), their names, and the hovered cell.
    p.setFont(font());
    for (int s = 1; s <= stringCount(); ++s) {
        const double y = g.top + (s - 1) * g.spacing;
        const double x0 = s == tuning_.shortString
                              ? g.left + (zub::fretboard::shortStringStartFret - 1) * g.cell
                              : g.left - g.openWidth;
        p.setPen(QPen(strong, 1.0 + 0.25 * (s - 1)));
        p.drawLine(QPointF(x0, y), QPointF(right, y));
        p.setPen(ink);
        const QString name = toQString(zub::fretboard::pitchName(
            tuning_.open[static_cast<std::size_t>(s - 1)], german_, flats_));
        p.drawText(QRectF(2, y - rowHeight / 2.0, labelWidth - 4, rowHeight),
                   Qt::AlignRight | Qt::AlignVCenter, name);
    }

    if (hover_.string > 0) {
        const QPointF c = cellCenter(hover_.string, hover_.fret);
        QColor hoverColor = accent;
        hoverColor.setAlphaF(0.35f);
        p.setPen(QPen(accent, 1));
        p.setBrush(hoverColor);
        p.drawRoundedRect(QRectF(c.x() - 9, c.y() - 8, 18, 16), 4, 4);
    }

    // The marked positions: filled dots with the fret number.
    for (const auto& m : marked_) {
        if (m.string < 1 || m.string > stringCount() || m.fret < 0)
            continue;
        const int physical = physicalFret(m.string, m.fret);
        if (physical > fretCount_)
            continue;
        const QPointF c = cellCenter(m.string, m.fret);
        p.setPen(QPen(paper, 1.5));
        p.setBrush(accent);
        p.drawEllipse(c, 8.0, 8.0);
        p.setPen(palette().color(QPalette::HighlightedText));
        p.setFont(small);
        p.drawText(QRectF(c.x() - 8, c.y() - 8, 16, 16), Qt::AlignCenter, QString::number(m.fret));
    }
}

void FretboardWidget::mouseMoveEvent(QMouseEvent* event) {
    const auto position = positionAt(event->position().toPoint());
    if (!(position == hover_)) {
        hover_ = position;
        update();
    }
}

void FretboardWidget::leaveEvent(QEvent*) {
    hover_ = {0, 0};
    update();
}

void FretboardWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton)
        return;
    const auto position = positionAt(event->position().toPoint());
    if (position.string > 0)
        emit clicked(position.string, position.fret, event->modifiers());
}

bool FretboardWidget::event(QEvent* event) {
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const auto position = positionAt(help->pos());
        if (position.string > 0) {
            const QString name = toQString(
                zub::fretboard::pitchName(midiAt(position.string, position.fret), german_, flats_));
            QToolTip::showText(help->globalPos(),
                               tr("%1: click inserts the note, Ctrl+click adds it to the chord "
                                  "before the cursor.")
                                   .arg(name),
                               this);
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(event);
}
