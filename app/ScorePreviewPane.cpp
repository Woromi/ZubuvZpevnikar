#include "ScorePreviewPane.hpp"

#include "ScoreRunner.hpp"
#include "convert.hpp"

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QScrollArea>
#include <QSvgRenderer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <map>
#include <set>
#include <utility>
#include <vector>

// LilyPond's point-and-click: `<a xlink:href="textedit:///path/score.ly:LINE:COL:COL2">` wraps the
// glyphs of a note, chord name ...; LINE is 1-based, COL the 0-based column of its text in code
// points. `point` is where the link is drawn, in SVG units.
struct NoteLink {
    int line;
    int column;
    QPointF point;
};

// A horizontal staff line of the score, in SVG units: from x1 to x2 at height y.
struct StaffLine {
    double x1, x2, y;
};

// A staff of the score (its lines grouped), in SVG units: its top and bottom line and its extent.
struct Staff {
    double top, bottom, x1, x2;
};

struct ParsedScore {
    std::vector<NoteLink> links;
    std::vector<StaffLine> staffLines;
};

namespace {

constexpr double minStaffLineLength = 8; // SVG units (a staff space is 1.5)

// "textedit:///path/file.ly:13:66:67" -> path, line, column; false for anything else.
bool parseTextEdit(const QString& href, QString& path, int& line, int& column) {
    const QString prefix = QStringLiteral("textedit://");
    if (!href.startsWith(prefix))
        return false;
    const QStringList parts = href.mid(prefix.size()).split(QLatin1Char(':'));
    if (parts.size() < 4)
        return false;
    bool lineOk = false, columnOk = false;
    line = parts[parts.size() - 3].toInt(&lineOk);
    column = parts[parts.size() - 2].toInt(&columnOk);
    if (!lineOk || !columnOk || line < 1 || column < 0)
        return false;
    path =
        QUrl::fromPercentEncoding(parts.mid(0, parts.size() - 3).join(QLatin1Char(':')).toUtf8());
    return true;
}

// The links of an engraved score that point into `scorePath` (links into the songbook's .ily
// files or LilyPond's own would jump to the wrong place). A link is drawn where its first
// transform="translate(x, y)" puts it; for a fret number that is the corner of the white patch
// behind it, so the patch's centre is used when that is the first thing in the link.
ParsedScore parseLinks(const QByteArray& svg, const QString& scorePath) {
    static const QRegularExpression translate(
        QStringLiteral(R"(translate\(\s*([-+0-9.eE]+)[\s,]+([-+0-9.eE]+)\s*\))"));
    ParsedScore parsed;
    std::vector<NoteLink>& links = parsed.links;
    QXmlStreamReader xml(svg);
    // The staff lines are `<g transform="translate(x, y)"><line y1="0" y2="0"/></g>` on their own.
    QPointF lineOrigin;
    bool haveLineOrigin = false;
    const QString xlink = QStringLiteral("http://www.w3.org/1999/xlink");
    int depth = 0;       // inside a link: its nesting level
    bool inLink = false; // a link to the score that has no point yet
    int line = 0, column = 0;
    enum class State { None, Translated } state = State::None;
    QPointF origin;
    const auto finish = [&](QPointF point) {
        links.push_back({line, column, point});
        inLink = false;
        state = State::None;
    };
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (depth == 0 && xml.name() == QLatin1String("g")) {
                const auto match = translate.match(xml.attributes().value("transform").toString());
                haveLineOrigin = match.hasMatch();
                if (haveLineOrigin)
                    lineOrigin = {match.captured(1).toDouble(), match.captured(2).toDouble()};
                continue;
            }
            if (depth == 0 && xml.name() == QLatin1String("line") && haveLineOrigin) {
                const auto a = xml.attributes();
                const double y1 = a.value("y1").toDouble(), y2 = a.value("y2").toDouble();
                const double x1 = lineOrigin.x() + a.value("x1").toDouble();
                const double x2 = lineOrigin.x() + a.value("x2").toDouble();
                // Long horizontal lines only: no ledger lines, no bar lines.
                if (y1 == y2 && x2 - x1 >= minStaffLineLength)
                    parsed.staffLines.push_back({x1, x2, lineOrigin.y() + y1});
                haveLineOrigin = false;
                continue;
            }
            if (depth == 0 && xml.name() == QLatin1String("a")) {
                QString path;
                const QString href =
                    xml.attributes().value(xlink, QLatin1String("href")).toString();
                if (parseTextEdit(href, path, line, column) && path == scorePath) {
                    inLink = true;
                    state = State::None;
                    depth = 1;
                    continue;
                }
                depth = 1; // another link: skip its content
                continue;
            }
            if (depth > 0)
                ++depth;
            if (!inLink)
                continue;
            if (state == State::None && xml.name() == QLatin1String("g")) {
                const auto match = translate.match(xml.attributes().value("transform").toString());
                if (match.hasMatch()) {
                    origin = {match.captured(1).toDouble(), match.captured(2).toDouble()};
                    state = State::Translated;
                }
            } else if (state == State::Translated) {
                QPointF point = origin;
                if (xml.name() == QLatin1String("rect")) {
                    const auto a = xml.attributes();
                    point += QPointF(a.value("x").toDouble() + a.value("width").toDouble() / 2,
                                     a.value("y").toDouble() + a.value("height").toDouble() / 2);
                }
                finish(point);
            }
        } else if (token == QXmlStreamReader::EndElement && depth > 0) {
            if (--depth == 0 && inLink) {
                // A link without any transform: nothing to point at.
                inLink = false;
                state = State::None;
            }
        }
    }
    return parsed;
}

} // namespace

// The SVG, as wide as the view (but at most twice its own size, so a short score is not blown
// up), with the height that keeps its proportions.
class SvgCanvas : public QWidget {
public:
    explicit SvgCanvas(QWidget* parent = nullptr) : QWidget(parent), renderer_(this) {
        QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    // The links of the loaded SVG (set after load()). None is selectable until setSelectable().
    void setLinks(ParsedScore parsed) {
        links_ = std::move(parsed.links);
        staves_ = groupStaves(parsed.staffLines);
        linkStaff_.clear();
        for (const NoteLink& link : links_)
            linkStaff_.push_back(staffIndexOf(link.point));
        highlighted_.clear();
        selectable_.clear();
        caret_.reset();
        update();
    }
    // The links that are notes of the music (clicks, drags and the caret only know these).
    void setSelectable(const std::vector<std::pair<int, int>>& positions) {
        selectable_ = std::set<std::pair<int, int>>(positions.begin(), positions.end());
    }
    // A click on the score (shift: extend the selection), a drag over it and a double-click, like
    // in a text: the note is the one at the mouse (pickAt), `after` tells on which side of it the
    // mouse is (right of the note: the text cursor goes behind it).
    std::function<void(const NoteLink& link, bool after, bool extend)> onNoteClicked;
    std::function<void(const NoteLink& anchor, bool anchorAfter, const NoteLink& current,
                       bool currentAfter)>
        onNotesDragged;
    std::function<void(const NoteLink& link)> onNoteDoubleClicked;
    std::function<void(const QPoint& globalPosition)> onContextMenu;
    // Keys that edit like a text cursor (see ScorePreviewPane::KeyCommand): `extend` is Shift,
    // `wholeScore` is Ctrl.
    std::function<void(int command, bool extend, bool wholeScore)> onKeyCommand;

    // The leftmost (rightmost) selectable note on the staff the caret is on; none without a caret
    // or when it is not on a staff.
    std::optional<std::pair<int, int>> staffEdge(bool right) const {
        if (!caret_)
            return std::nullopt;
        const NoteLink* caretLink = findLink(caret_->first, caret_->second);
        if (!caretLink || staffOf(*caretLink) < 0)
            return std::nullopt;
        const int staff = staffOf(*caretLink);
        const NoteLink* best = nullptr;
        for (std::size_t i = 0; i < links_.size(); ++i) {
            if (linkStaff_[i] != staff || !isSelectable(links_[i]))
                continue;
            if (!best || (right ? links_[i].point.x() > best->point.x()
                                : links_[i].point.x() < best->point.x()))
                best = &links_[i];
        }
        if (!best)
            return std::nullopt;
        return std::make_pair(best->line, best->column);
    }

    // The score cursor: a vertical line before (or after) the note linked at {line, column}; none
    // clears it.
    void setCaret(std::optional<std::pair<int, int>> position, bool after = false) {
        if (position == caret_ && after == caretAfter_)
            return;
        caret_ = position;
        caretAfter_ = after;
        update();
    }
    bool hasCaret() const { return caret_.has_value(); }

    // The links that get a highlight box (the notes inside the editor's selection).
    void setHighlighted(const std::vector<std::pair<int, int>>& positions) {
        const std::set<std::pair<int, int>> next(positions.begin(), positions.end());
        if (next == highlighted_)
            return;
        highlighted_ = next;
        update();
    }
    const std::vector<NoteLink>& links() const { return links_; }
    int highlightedCount() const {
        return static_cast<int>(std::ranges::count_if(links_, [this](const NoteLink& link) {
            return highlighted_.contains({link.line, link.column});
        }));
    }

    // The widget position of a link's point, null when it is not drawn.
    QPointF toWidget(const QPointF& svgPoint) const {
        const QRectF box = renderer_.viewBoxF();
        if (!hasImage() || box.isEmpty())
            return {};
        const int width = shownWidth(this->width());
        const int height = heightForWidth(this->width()) - 2 * margin;
        return {margin + (svgPoint.x() - box.x()) / box.width() * width,
                margin + (svgPoint.y() - box.y()) / box.height() * height};
    }
    // The note a click at `position` means, like a click in a text: on the staff nearest to the
    // mouse (one under or beside it horizontally first), the selectable note nearest in x. Without
    // staff lines, the nearest selectable note. Null when nothing is selectable.
    const NoteLink* pickAt(const QPoint& position) const {
        const QPointF at = toSvg(QPointF(position));
        std::vector<bool> hasNotes(staves_.size(), false);
        for (std::size_t i = 0; i < links_.size(); ++i) {
            if (linkStaff_[i] >= 0 && isSelectable(links_[i]))
                hasNotes[static_cast<std::size_t>(linkStaff_[i])] = true;
        }
        int staff = -1;
        double bestDistance = 0;
        for (std::size_t i = 0; i < staves_.size(); ++i) {
            if (!hasNotes[i])
                continue;
            const Staff& s = staves_[i];
            const double dx = at.x() < s.x1 ? s.x1 - at.x() : at.x() > s.x2 ? at.x() - s.x2 : 0;
            const double dy = at.y() < s.top    ? s.top - at.y()
                              : at.y() > s.bottom ? at.y() - s.bottom
                                                  : 0;
            const double distance = 10 * dx + dy; // a staff beside the mouse loses to one above it
            if (staff < 0 || distance < bestDistance) {
                staff = static_cast<int>(i);
                bestDistance = distance;
            }
        }
        if (staff < 0)
            return nearestLink(position);
        const NoteLink* best = nullptr;
        for (std::size_t i = 0; i < links_.size(); ++i) {
            if (linkStaff_[i] != staff || !isSelectable(links_[i]))
                continue;
            if (!best || std::abs(links_[i].point.x() - at.x()) < std::abs(best->point.x() - at.x()))
                best = &links_[i];
        }
        return best;
    }
    // Is `position` right of the note's centre (the caret goes behind it)?
    bool isAfter(const NoteLink& link, const QPoint& position) const {
        return position.x() > toWidget(link.point).x();
    }
    // The selectable note nearest to `position`, however far.
    const NoteLink* nearestLink(const QPoint& position) const {
        const NoteLink* best = nullptr;
        double bestDistance = 0;
        for (const NoteLink& link : links_) {
            if (!isSelectable(link))
                continue;
            const QPointF at = toWidget(link.point);
            const double dx = at.x() - position.x(), dy = at.y() - position.y();
            if (!best || dx * dx + dy * dy < bestDistance) {
                bestDistance = dx * dx + dy * dy;
                best = &link;
            }
        }
        return best;
    }
    const NoteLink* findLink(int line, int column) const {
        for (const NoteLink& link : links_) {
            if (link.line == line && link.column == column)
                return &link;
        }
        return nullptr;
    }

    bool load(const QByteArray& svg) {
        QSvgRenderer probe;
        if (!probe.load(svg) || !probe.isValid() || probe.defaultSize().isEmpty())
            return false;
        svg_ = svg;
        renderer_.load(recolored(svg_));
        updateGeometry();
        update();
        return true;
    }
    void clear() {
        links_.clear();
        staves_.clear();
        linkStaff_.clear();
        highlighted_.clear();
        selectable_.clear();
        caret_.reset();
        svg_.clear();
        renderer_.load(QByteArray());
        updateGeometry();
        update();
    }
    bool hasImage() const { return renderer_.isValid() && !renderer_.defaultSize().isEmpty(); }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (!hasImage())
            return 0;
        const QSize size = renderer_.defaultSize();
        return shownWidth(width) * size.height() / size.width() + 2 * margin;
    }
    QSize sizeHint() const override {
        return hasImage() ? renderer_.defaultSize() + QSize(2 * margin, 2 * margin) : QSize(0, 0);
    }

protected:
    // Like a text: a press and release without moving far is a click (the text cursor goes before
    // the note at the mouse, Shift: the selection extends to it); moving further selects the
    // notes from the one pressed to the one at the mouse.
    void mouseMoveEvent(QMouseEvent* event) override {
        const QPoint position = event->position().toPoint();
        if (pressed_ && pressLink_ &&
            (position - pressAt_).manhattanLength() >= QApplication::startDragDistance())
            dragging_ = true;
        if (dragging_) {
            const NoteLink* nearest = pickAt(position);
            const bool after = nearest && isAfter(*nearest, position);
            if (nearest && (nearest != dragLink_ || after != dragAfter_) && onNotesDragged) {
                dragLink_ = nearest;
                dragAfter_ = after;
                onNotesDragged(*pressLink_, pressAfter_, *nearest, after);
            }
            return;
        }
        setCursor(selectable_.empty() ? Qt::ArrowCursor : Qt::IBeamCursor);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        setFocus(Qt::MouseFocusReason); // the keys edit the score from here
        pressed_ = true;
        dragging_ = false;
        dragLink_ = nullptr;
        pressAt_ = event->position().toPoint();
        pressLink_ = pickAt(pressAt_);
        pressAfter_ = pressLink_ && isAfter(*pressLink_, pressAt_);
        pressExtend_ = event->modifiers().testFlag(Qt::ShiftModifier);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !pressed_)
            return;
        const bool wasDragging = dragging_;
        const NoteLink* link = pressLink_;
        pressed_ = dragging_ = false;
        pressLink_ = dragLink_ = nullptr;
        if (!wasDragging && link && onNoteClicked)
            onNoteClicked(*link, pressAfter_, pressExtend_);
    }
    // Like a word in a text: a double-click selects the note at the mouse.
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        pressed_ = dragging_ = false;
        pressLink_ = dragLink_ = nullptr;
        if (const NoteLink* link = pickAt(event->position().toPoint()); link && onNoteDoubleClicked)
            onNoteDoubleClicked(*link);
    }
    void keyPressEvent(QKeyEvent* event) override {
        const Qt::KeyboardModifiers mods = event->modifiers();
        const bool shift = mods.testFlag(Qt::ShiftModifier);
        const bool ctrl = mods.testFlag(Qt::ControlModifier);
        const bool others = mods.testFlag(Qt::AltModifier) || mods.testFlag(Qt::MetaModifier);
        int command = -1;
        if (event->matches(QKeySequence::Undo))
            command = static_cast<int>(ScorePreviewPane::KeyCommand::Undo);
        else if (event->matches(QKeySequence::Redo))
            command = static_cast<int>(ScorePreviewPane::KeyCommand::Redo);
        else if (!others && event->key() == Qt::Key_Left)
            command = static_cast<int>(ScorePreviewPane::KeyCommand::Left);
        else if (!others && event->key() == Qt::Key_Right)
            command = static_cast<int>(ScorePreviewPane::KeyCommand::Right);
        else if (!others && event->key() == Qt::Key_Home)
            command = static_cast<int>(ScorePreviewPane::KeyCommand::Home);
        else if (!others && event->key() == Qt::Key_End)
            command = static_cast<int>(ScorePreviewPane::KeyCommand::End);
        else if (!others && !shift && !ctrl && event->key() == Qt::Key_Backspace)
            command = static_cast<int>(ScorePreviewPane::KeyCommand::Backspace);
        else if (!others && !shift && !ctrl && event->key() == Qt::Key_Delete)
            command = static_cast<int>(ScorePreviewPane::KeyCommand::Delete);
        if (command < 0 || !onKeyCommand) {
            QWidget::keyPressEvent(event);
            return;
        }
        event->accept();
        onKeyCommand(command, shift, ctrl);
    }
    void focusInEvent(QFocusEvent* event) override {
        QWidget::focusInEvent(event);
        update();
    }
    void focusOutEvent(QFocusEvent* event) override {
        QWidget::focusOutEvent(event);
        update();
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        if (onContextMenu)
            onContextMenu(event->globalPos());
    }

    void changeEvent(QEvent* event) override {
        // A new colour scheme: the score follows it.
        if (event->type() == QEvent::PaletteChange && !svg_.isEmpty()) {
            renderer_.load(recolored(svg_));
            update();
        }
        QWidget::changeEvent(event);
    }

    void paintEvent(QPaintEvent*) override {
        if (!hasImage())
            return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const int width = shownWidth(this->width());
        renderer_.render(&painter,
                         QRectF(margin, margin, width, heightForWidth(this->width()) - 2 * margin));
        QColor fill = palette().color(QPalette::Highlight);
        QColor edge = fill;
        fill.setAlpha(80);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        paintSelection(painter);
        if (caret_) {
            if (const NoteLink* link = findLink(caret_->first, caret_->second)) {
                const QPointF at = toWidget(link->point);
                const auto [top, bottom] = staffSpan(*link);
                painter.setPen(QPen(edge, 2, Qt::SolidLine, Qt::FlatCap));
                const double x = at.x() + (caretAfter_ ? 1 : -1) * highlightSize / 2;
                painter.drawLine(QPointF(x, top), QPointF(x, bottom));
            }
        }
        if (hasFocus()) { // the keys go to the score
            edge.setAlpha(140);
            painter.setPen(QPen(edge, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(rect().adjusted(1, 1, -1, -1));
        }
    }

private:
    static constexpr int margin = 12;       // space around the score
    static constexpr double highlightSize = 20; // pixels: the box around a selected note

    bool isSelectable(const NoteLink& link) const {
        return selectable_.contains({link.line, link.column});
    }
    int staffOf(const NoteLink& link) const {
        return linkStaff_[static_cast<std::size_t>(&link - links_.data())];
    }
    QPointF toSvg(const QPointF& widgetPoint) const {
        const QRectF box = renderer_.viewBoxF();
        if (!hasImage() || box.isEmpty())
            return {};
        const int width = shownWidth(this->width());
        const int height = heightForWidth(this->width()) - 2 * margin;
        return {box.x() + (widgetPoint.x() - margin) / width * box.width(),
                box.y() + (widgetPoint.y() - margin) / height * box.height()};
    }

    // The staves: staff lines that overlap horizontally, each no further than `gap` below the
    // previous one.
    static std::vector<Staff> groupStaves(std::vector<StaffLine> lines) {
        constexpr double gap = 2.5; // SVG units (a staff space is 1.5)
        std::ranges::sort(lines, {}, &StaffLine::y);
        std::vector<Staff> staves;
        for (const StaffLine& line : lines) {
            const auto staff = std::ranges::find_if(staves, [&line](const Staff& s) {
                return line.y - s.bottom <= gap && line.x1 < s.x2 && s.x1 < line.x2;
            });
            if (staff == staves.end()) {
                staves.push_back({line.y, line.y, line.x1, line.x2});
            } else {
                staff->bottom = std::max(staff->bottom, line.y);
                staff->x1 = std::min(staff->x1, line.x1);
                staff->x2 = std::max(staff->x2, line.x2);
            }
        }
        return staves;
    }
    // The staff a note drawn at `svgPoint` is on: the nearest one under it horizontally, no
    // further than a few ledger lines away; -1 for none (chord names, lyrics, no staff lines).
    int staffIndexOf(const QPointF& svgPoint) const {
        constexpr double reach = 3.5; // SVG units
        int best = -1;
        double bestDistance = reach;
        for (std::size_t i = 0; i < staves_.size(); ++i) {
            const Staff& s = staves_[i];
            if (svgPoint.x() < s.x1 - 0.5 || svgPoint.x() > s.x2 + 0.5)
                continue;
            const double distance = svgPoint.y() < s.top      ? s.top - svgPoint.y()
                                    : svgPoint.y() > s.bottom ? svgPoint.y() - s.bottom
                                                              : 0;
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = static_cast<int>(i);
            }
        }
        return best;
    }
    // The widget heights of the staff (a little beyond its top and bottom line).
    std::pair<double, double> staffSpan(const Staff& staff) const {
        return {toWidget(QPointF(staff.x1, staff.top)).y() - 4,
                toWidget(QPointF(staff.x1, staff.bottom)).y() + 4};
    }
    // The widget heights of the staff the note is on; without one, a span around the note.
    std::pair<double, double> staffSpan(const NoteLink& link) const {
        if (const int staff = staffOf(link); staff >= 0)
            return staffSpan(staves_[static_cast<std::size_t>(staff)]);
        const double y = toWidget(link.point).y();
        return {y - highlightSize * 1.5, y + highlightSize * 1.5};
    }

    // The selection like selected text: on each staff one band from its first to its last
    // selected note, over the whole staff height; a staff that the selection goes on from or to
    // another staff gets the band to its start or end. Notes off any staff get a box each.
    void paintSelection(QPainter& painter) const {
        using Position = std::pair<int, int>; // {line, column}: the text order
        std::optional<Position> first, last;
        std::map<int, std::vector<const NoteLink*>> perStaff;
        for (const NoteLink& link : links_) {
            const Position position{link.line, link.column};
            if (!highlighted_.contains(position))
                continue;
            first = first ? std::min(*first, position) : position;
            last = last ? std::max(*last, position) : position;
            perStaff[staffOf(link)].push_back(&link);
        }
        for (const auto& [staff, notes] : perStaff) {
            if (staff < 0) {
                for (const NoteLink* link : notes) {
                    const QPointF at = toWidget(link->point);
                    painter.drawRoundedRect(QRectF(at.x() - highlightSize / 2,
                                                   at.y() - highlightSize / 2, highlightSize,
                                                   highlightSize),
                                            4, 4);
                }
                continue;
            }
            const Staff& s = staves_[static_cast<std::size_t>(staff)];
            double left = 0, right = 0;
            Position staffFirst{}, staffLast{};
            bool any = false;
            for (const NoteLink* link : notes) {
                const double x = toWidget(link->point).x();
                const Position position{link->line, link->column};
                if (!any) {
                    left = right = x;
                    staffFirst = staffLast = position;
                    any = true;
                }
                left = std::min(left, x);
                right = std::max(right, x);
                staffFirst = std::min(staffFirst, position);
                staffLast = std::max(staffLast, position);
            }
            left -= highlightSize / 2;
            right += highlightSize / 2;
            if (staffFirst > *first)
                left = toWidget(QPointF(s.x1, s.top)).x();
            if (staffLast < *last)
                right = toWidget(QPointF(s.x2, s.top)).x();
            const auto [top, bottom] = staffSpan(s);
            painter.drawRoundedRect(QRectF(QPointF(left, top), QPointF(right, bottom)), 3, 3);
        }
    }
    int shownWidth(int available) const {
        return std::max(1, std::min(available - 2 * margin, renderer_.defaultSize().width() * 2));
    }

    // The score in the colours of the pane: LilyPond draws everything in currentColor (black by
    // default) except the white patches it puts behind tablature fret numbers so the staff line
    // does not cross them. Those get the background colour and currentColor the text colour, so
    // a dark colour scheme shows a light score without white squares.
    QByteArray recolored(const QByteArray& svg) const {
        const QByteArray ink = palette().color(QPalette::WindowText).name().toLatin1();
        const QByteArray paper = palette().color(QPalette::Window).name().toLatin1();
        QByteArray out = svg;
        for (const char* white : {"color=\"rgb(100.0000%, 100.0000%, 100.0000%)\"",
                                  "color=\"#ffffff\"", "color=\"white\""})
            out.replace(white, "color=\"" + paper + "\"");
        if (const auto root = out.indexOf("<svg"); root >= 0)
            out.insert(root + 4, " color=\"" + ink + "\"");
        return out;
    }

    std::vector<NoteLink> links_;
    std::set<std::pair<int, int>> highlighted_; // {line, column} of the links to highlight
    std::set<std::pair<int, int>> selectable_;  // {line, column} of the links that are notes
    std::vector<Staff> staves_;
    std::vector<int> linkStaff_; // for each link, the index of its staff in staves_ (-1: none)
    std::optional<std::pair<int, int>> caret_;  // the link the score cursor is before or after
    bool caretAfter_ = false;                   // ... after it
    bool pressAfter_ = false;                   // the press was right of the pressed note
    bool dragAfter_ = false;                    // the last reported drag position was right of it
    bool pressed_ = false;                      // the left button is down on the score
    bool dragging_ = false;                     // ... and has moved far enough: a selection
    bool pressExtend_ = false;                  // Shift was down at the press
    const NoteLink* pressLink_ = nullptr;       // the note pressed at (none: nothing selectable)
    const NoteLink* dragLink_ = nullptr;        // the note last reported while dragging
    QPoint pressAt_;
    QByteArray svg_; // as LilyPond wrote it
    QSvgRenderer renderer_;
};

ScorePreviewPane::ScorePreviewPane(QWidget* parent)
    : QWidget(parent), rebuild_(new QToolButton), logButton_(new QToolButton), play_(new QToolButton), tempo_(new QComboBox), status_(new QLabel),
      scroll_(new QScrollArea), canvas_(new SvgCanvas), errors_(new QListWidget) {
    rebuild_->setText(tr("Rebuild"));
    if (QIcon::hasThemeIcon(QStringLiteral("view-refresh"))) {
        rebuild_->setIcon(QIcon::fromTheme(QStringLiteral("view-refresh")));
        rebuild_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    rebuild_->setToolTip(tr("Rebuild the score (Ctrl+R)"));
    rebuild_->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));

    logButton_->setText(tr("Log"));
    logButton_->setEnabled(false);

    play_->setObjectName(QStringLiteral("scorePlay"));
    play_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Space")));
    tempo_->setObjectName(QStringLiteral("scoreTempo"));
    tempo_->setToolTip(tr("Playback speed, as a share of the song's tempo"));
    for (const int percent : {50, 75, 100, 125})
        tempo_->addItem(tr("%1 %").arg(percent), percent);
    tempo_->setCurrentIndex(
        std::max(0, tempo_->findData(QSettings().value(QStringLiteral("playback/tempoPercent"), 100)
                                         .toInt())));
    setPlayState(PlayState::Idle);

    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setMinimumWidth(0);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 4, 4, 0);
    toolbar->addWidget(rebuild_);
    toolbar->addWidget(play_);
    toolbar->addWidget(tempo_);
    toolbar->addWidget(status_, 1);
    toolbar->addWidget(logButton_);

    scroll_->setWidget(canvas_);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);

    errors_->setWordWrap(true);
    errors_->setMaximumHeight(errors_->fontMetrics().lineSpacing() * 6 + 8);
    errors_->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(toolbar);
    layout->addWidget(scroll_, 1);
    layout->addWidget(errors_);

    canvas_->onNoteClicked = [this](const NoteLink& link, bool after, bool extend) {
        emit noteClicked(QPoint(link.column, link.line), after, extend);
    };
    canvas_->onNotesDragged = [this](const NoteLink& anchor, bool anchorAfter,
                                     const NoteLink& current, bool currentAfter) {
        emit notesDragged(QPoint(anchor.column, anchor.line), anchorAfter,
                          QPoint(current.column, current.line), currentAfter);
    };
    canvas_->onNoteDoubleClicked = [this](const NoteLink& link) {
        emit noteDoubleClicked(QPoint(link.column, link.line));
    };
    canvas_->onKeyCommand = [this](int command, bool extend, bool wholeScore) {
        emit keyCommand(static_cast<KeyCommand>(command), extend, wholeScore);
    };
    canvas_->onContextMenu = [this](const QPoint& at) {
        if (regionActions_.isEmpty())
            return;
        QMenu menu(this);
        menu.addActions(regionActions_);
        menu.exec(at);
    };
    connect(rebuild_, &QToolButton::clicked, this, &ScorePreviewPane::rebuildRequested);
    connect(play_, &QToolButton::clicked, this, [this] {
        if (playState_ == PlayState::Idle)
            emit playRequested();
        else
            emit stopRequested();
    });
    connect(tempo_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(QStringLiteral("playback/tempoPercent"), tempo_->currentData());
        emit tempoChanged();
    });
    connect(logButton_, &QToolButton::clicked, this, [this] { showLog(); });
    connect(errors_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        const QVariant position = item->data(Qt::UserRole);
        if (position.isValid())
            emit errorActivated(position.toPoint().y(), position.toPoint().x());
    });
}

void ScorePreviewPane::showBuilding() {
    status_->setStyleSheet({});
    status_->setText(tr("Building…"));
    errors_->clear();
    errors_->hide();
}

void ScorePreviewPane::showResult(const ScoreResult& result) {
    log_ = result.log;
    logButton_->setEnabled(!log_.isEmpty());

    bool success = result.success;
    QString message = result.message;
    if (success && !canvas_->load(result.svg)) {
        success = false;
        message = tr("The build produced an image that cannot be displayed.");
    }
    if (success)
        canvas_->setLinks(parseLinks(result.svg, toQString(result.scoreCopy)));
    if (success) {
        status_->setStyleSheet({});
        status_->setText(
            tr("Built in %1 s").arg(static_cast<double>(result.elapsedMs) / 1000.0, 0, 'f', 1));
        errors_->clear();
        errors_->hide();
        return;
    }

    QString text = tr("Build failed");
    if (canvas_->hasImage())
        text += tr(" — showing the last successful build");
    status_->setText(text);
    status_->setStyleSheet(QStringLiteral("color: #c0392b;"));

    errors_->clear();
    for (const zub::score::Error& error : result.errors) {
        const bool located = error.file == result.scoreCopy;
        const QString head = located
                                 ? tr("Line %1: %2").arg(error.line).arg(toQString(error.message))
                                 : QStringLiteral("%1:%2: %3")
                                       .arg(toQString(error.file))
                                       .arg(error.line)
                                       .arg(toQString(error.message));
        auto* item = new QListWidgetItem(head, errors_);
        if (located) {
            item->setData(Qt::UserRole, QPoint(error.column, error.line));
            item->setToolTip(tr("Double-click to jump to the line"));
        }
    }
    if (errors_->count() == 0)
        new QListWidgetItem(message, errors_);
    errors_->show();
}

QPoint ScorePreviewPane::noteViewPosition(int line, int column) const {
    const NoteLink* link = canvas_->findLink(line, column);
    return link ? canvas_->toWidget(link->point).toPoint() : QPoint();
}

QList<QPoint> ScorePreviewPane::notePositions() const {
    QList<QPoint> positions;
    for (const NoteLink& link : canvas_->links())
        positions.append(QPoint(link.column, link.line));
    return positions;
}

std::optional<QPoint> ScorePreviewPane::staffEdgeNote(bool right) const {
    const auto edge = canvas_->staffEdge(right);
    if (!edge)
        return std::nullopt;
    return QPoint(edge->second, edge->first);
}

QWidget* ScorePreviewPane::canvasWidget() const {
    return canvas_;
}

void ScorePreviewPane::setHighlightedNotes(const QList<QPoint>& positions) {
    std::vector<std::pair<int, int>> list;
    for (const QPoint& position : positions)
        list.emplace_back(position.y(), position.x());
    canvas_->setHighlighted(list);
}

void ScorePreviewPane::setSelectableNotes(const QList<QPoint>& positions) {
    std::vector<std::pair<int, int>> list;
    for (const QPoint& position : positions)
        list.emplace_back(position.y(), position.x());
    canvas_->setSelectable(list);
}

void ScorePreviewPane::setCaretNote(const QPoint& position, bool after) {
    if (position.isNull())
        canvas_->setCaret(std::nullopt);
    else
        canvas_->setCaret(std::make_pair(position.y(), position.x()), after);
}

bool ScorePreviewPane::hasCaret() const {
    return canvas_->hasCaret();
}

int ScorePreviewPane::highlightedNoteCount() const {
    return canvas_->highlightedCount();
}

void ScorePreviewPane::setRegionActions(const QList<QAction*>& actions) {
    regionActions_ = actions;
}

QWidget* ScorePreviewPane::scoreView() const {
    return canvas_;
}

void ScorePreviewPane::showBlocked(const QString& message) {
    canvas_->clear();
    log_.clear();
    logButton_->setEnabled(false);
    status_->setStyleSheet({});
    status_->setText(tr("Not built"));
    errors_->clear();
    new QListWidgetItem(message, errors_);
    errors_->show();
}

void ScorePreviewPane::clear() {
    canvas_->clear();
    status_->setStyleSheet({});
    status_->clear();
    errors_->clear();
    errors_->hide();
    log_.clear();
    logButton_->setEnabled(false);
}

void ScorePreviewPane::setPlaybackAvailable(bool available, const QString& reason) {
    playbackAvailable_ = available;
    unavailableReason_ = reason;
    setPlayState(playState_);
}

void ScorePreviewPane::setPlayState(PlayState state) {
    playState_ = state;
    const bool idle = state == PlayState::Idle;
    const QString text = idle ? tr("Play") : tr("Stop");
    play_->setText(text);
    const char* icon = idle ? "media-playback-start" : "media-playback-stop";
    if (QIcon::hasThemeIcon(QString::fromLatin1(icon))) {
        play_->setIcon(QIcon::fromTheme(QString::fromLatin1(icon)));
        play_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    } else {
        play_->setIcon({});
        play_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    play_->setEnabled(playbackAvailable_ || !idle);
    if (idle && !playbackAvailable_)
        play_->setToolTip(unavailableReason_);
    else
        play_->setToolTip(idle ? tr("Play the score (Ctrl+Space)")
                               : tr("Stop playing (Ctrl+Space)"));
}

void ScorePreviewPane::showPlaybackMessage(const QString& message, bool error) {
    status_->setStyleSheet(error ? QStringLiteral("color: #c0392b;") : QString());
    status_->setText(message);
}

double ScorePreviewPane::tempoFactor() const {
    return tempo_->currentData().toInt() / 100.0;
}

QWidget* ScorePreviewPane::playButton() const {
    return play_;
}

void ScorePreviewPane::showLog() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Build log"));
    dialog->resize(800, 600);

    auto* text = new QPlainTextEdit;
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::NoWrap);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    text->setPlainText(log_);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(text);
    dialog->show();
}
