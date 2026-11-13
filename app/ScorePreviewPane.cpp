#include "ScorePreviewPane.hpp"

#include "ScoreRunner.hpp"
#include "convert.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QRegularExpression>
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
    }

    // The links of the loaded SVG (set after load()). None is selectable until setSelectable().
    void setLinks(ParsedScore parsed) {
        links_ = std::move(parsed.links);
        staffLines_ = std::move(parsed.staffLines);
        highlighted_.clear();
        selectable_.clear();
        caret_.reset();
        update();
    }
    // The links that are notes of the music (clicks, drags and the caret only know these).
    void setSelectable(const std::vector<std::pair<int, int>>& positions) {
        selectable_ = std::set<std::pair<int, int>>(positions.begin(), positions.end());
    }
    // A click on a note (shift: extend the selection to it) and a drag over notes (from the note
    // pressed on to the one nearest the mouse).
    std::function<void(const NoteLink& link, bool extend)> onNoteClicked;
    std::function<void(const NoteLink& anchor, const NoteLink& current)> onNotesDragged;
    std::function<void(const QPoint& globalPosition)> onContextMenu;

    // The score cursor: a vertical line before the note linked at {line, column}; none clears it.
    void setCaret(std::optional<std::pair<int, int>> position) {
        if (position == caret_)
            return;
        caret_ = position;
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
    const NoteLink* linkAt(const QPoint& position) const {
        const NoteLink* best = nullptr;
        double bestDistance = hitRadius * hitRadius;
        for (const NoteLink& link : links_) {
            if (!selectable_.contains({link.line, link.column}))
                continue;
            const QPointF at = toWidget(link.point);
            const double dx = at.x() - position.x(), dy = at.y() - position.y();
            if (dx * dx + dy * dy <= bestDistance) {
                bestDistance = dx * dx + dy * dy;
                best = &link;
            }
        }
        return best;
    }
    // The selectable note nearest to `position`, however far.
    const NoteLink* nearestLink(const QPoint& position) const {
        const NoteLink* best = nullptr;
        double bestDistance = 0;
        for (const NoteLink& link : links_) {
            if (!selectable_.contains({link.line, link.column}))
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
        staffLines_.clear();
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
    // A press on a note and release without moving far is a click (the text cursor goes to the
    // note, Shift: the selection extends to it); moving further selects the notes from the one
    // pressed to the one nearest the mouse, like selecting text.
    void mouseMoveEvent(QMouseEvent* event) override {
        const QPoint position = event->position().toPoint();
        if (pressed_ && pressLink_ &&
            (position - pressAt_).manhattanLength() >= QApplication::startDragDistance())
            dragging_ = true;
        if (dragging_) {
            if (const NoteLink* nearest = nearestLink(position);
                nearest && nearest != dragLink_ && onNotesDragged) {
                dragLink_ = nearest;
                onNotesDragged(*pressLink_, *nearest);
            }
            return;
        }
        setCursor(linkAt(position) ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton)
            return;
        pressed_ = true;
        dragging_ = false;
        dragLink_ = nullptr;
        pressAt_ = event->position().toPoint();
        pressLink_ = linkAt(pressAt_);
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
            onNoteClicked(*link, pressExtend_);
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
        for (const NoteLink& link : links_) {
            if (!highlighted_.contains({link.line, link.column}))
                continue;
            const QPointF at = toWidget(link.point);
            painter.drawRoundedRect(QRectF(at.x() - highlightSize / 2, at.y() - highlightSize / 2,
                                           highlightSize, highlightSize),
                                    4, 4);
        }
        if (caret_) {
            if (const NoteLink* link = findLink(caret_->first, caret_->second)) {
                const QPointF at = toWidget(link->point);
                const auto [top, bottom] = staffSpan(link->point);
                painter.setPen(QPen(edge, 2, Qt::SolidLine, Qt::FlatCap));
                const double x = at.x() - highlightSize / 2;
                painter.drawLine(QPointF(x, top), QPointF(x, bottom));
            }
        }
    }

private:
    static constexpr int margin = 12;       // space around the score
    static constexpr double hitRadius = 14; // pixels: how near to a note a click counts
    static constexpr double highlightSize = 20; // pixels: the box around a selected note

    // The widget heights of the top and the bottom staff line of the staff that the note at
    // `svgPoint` is on (a little beyond); without staff lines in the SVG, a span around the note.
    std::pair<double, double> staffSpan(const QPointF& svgPoint) const {
        constexpr double reach = 3.5, gap = 2.5; // SVG units: how far a staff is, between its lines
        std::vector<double> ys;
        for (const StaffLine& line : staffLines_) {
            if (line.x1 - 0.5 <= svgPoint.x() && svgPoint.x() <= line.x2 + 0.5 &&
                std::abs(line.y - svgPoint.y()) <= 12)
                ys.push_back(line.y);
        }
        std::ranges::sort(ys);
        // The group of lines (no gap above `gap`) that the note is on or nearest to.
        double top = 0, bottom = 0, best = 1e9;
        for (std::size_t i = 0; i < ys.size();) {
            std::size_t j = i + 1;
            while (j < ys.size() && ys[j] - ys[j - 1] <= gap)
                ++j;
            const double distance = svgPoint.y() < ys[i]       ? ys[i] - svgPoint.y()
                                    : svgPoint.y() > ys[j - 1] ? svgPoint.y() - ys[j - 1]
                                                               : 0;
            if (distance < best) {
                best = distance;
                top = ys[i];
                bottom = ys[j - 1];
            }
            i = j;
        }
        if (best > reach) {
            const double y = toWidget(svgPoint).y();
            return {y - highlightSize * 1.5, y + highlightSize * 1.5};
        }
        return {toWidget(QPointF(svgPoint.x(), top)).y() - 4,
                toWidget(QPointF(svgPoint.x(), bottom)).y() + 4};
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
    std::vector<StaffLine> staffLines_;
    std::optional<std::pair<int, int>> caret_;  // the link the score cursor is before
    bool pressed_ = false;                      // the left button is down on the score
    bool dragging_ = false;                     // ... and has moved far enough: a selection
    bool pressExtend_ = false;                  // Shift was down at the press
    const NoteLink* pressLink_ = nullptr;       // the note pressed on (none: empty space)
    const NoteLink* dragLink_ = nullptr;        // the note last reported while dragging
    QPoint pressAt_;
    QByteArray svg_; // as LilyPond wrote it
    QSvgRenderer renderer_;
};

ScorePreviewPane::ScorePreviewPane(QWidget* parent)
    : QWidget(parent), rebuild_(new QToolButton), logButton_(new QToolButton), status_(new QLabel),
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

    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setMinimumWidth(0);

    auto* toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 4, 4, 0);
    toolbar->addWidget(rebuild_);
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

    canvas_->onNoteClicked = [this](const NoteLink& link, bool extend) {
        emit noteClicked(QPoint(link.column, link.line), extend);
    };
    canvas_->onNotesDragged = [this](const NoteLink& anchor, const NoteLink& current) {
        emit notesDragged(QPoint(anchor.column, anchor.line), QPoint(current.column, current.line));
    };
    canvas_->onContextMenu = [this](const QPoint& at) {
        if (regionActions_.isEmpty())
            return;
        QMenu menu(this);
        menu.addActions(regionActions_);
        menu.exec(at);
    };
    connect(rebuild_, &QToolButton::clicked, this, &ScorePreviewPane::rebuildRequested);
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

void ScorePreviewPane::setCaretNote(const QPoint& position) {
    if (position.isNull())
        canvas_->setCaret(std::nullopt);
    else
        canvas_->setCaret(std::make_pair(position.y(), position.x()));
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
