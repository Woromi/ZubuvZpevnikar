#include "PdfLinkView.hpp"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QScreen>
#include <QScrollBar>
#include <algorithm>
#include <cstddef>

namespace {

// QPdfView's resolution: the primary screen's logical DPI per point.
qreal screenResolution() {
    const QScreen* screen = QGuiApplication::primaryScreen();
    return screen ? screen->logicalDotsPerInch() / 72.0 : 96.0 / 72.0;
}

} // namespace

PdfLinkView::PdfLinkView(QWidget* parent) : QPdfView(parent) {
    connect(this, &QPdfView::documentChanged, this,
            [this](QPdfDocument* document) { links_.setDocument(document); });
}

// Mirrors QPdfViewPrivate::calculateDocumentLayout (Qt 6.10): page sizes rounded to whole pixels
// first, then scaled; pages centred horizontally, one below another.
std::vector<QRect> PdfLinkView::layout() const {
    const QPdfDocument* doc = document();
    if (!doc || doc->status() != QPdfDocument::Status::Ready)
        return {};
    const int n = doc->pageCount();
    std::vector<QRect> rects(static_cast<std::size_t>(std::max(n, 0)));
    const bool multi = pageMode() == PageMode::MultiPage;
    const int current = pageNavigator()->currentPage();
    const int first = multi ? 0 : std::clamp(current, 0, std::max(n - 1, 0));
    const int last = multi ? n : std::min(first + 1, n);
    const qreal res = screenResolution();
    const QMargins margins = documentMargins();
    const QSize viewportSize = viewport()->size();

    const auto sizeOf = [&](int p) {
        const QSize base = QSizeF(doc->pagePointSize(p) * res).toSize();
        switch (zoomMode()) {
        case ZoomMode::Custom:
            return QSizeF(doc->pagePointSize(p) * res * zoomFactor()).toSize();
        case ZoomMode::FitToWidth:
            if (base.width() <= 0)
                return base;
            return base * (qreal(viewportSize.width() - margins.left() - margins.right()) /
                           qreal(base.width()));
        case ZoomMode::FitInView:
            return base.scaled(viewportSize + QSize(-margins.left() - margins.right(),
                                                    -pageSpacing()),
                               Qt::KeepAspectRatio);
        }
        return base;
    };

    int totalWidth = 0;
    int y = margins.top();
    for (int p = first; p < last; ++p) {
        const QSize size = sizeOf(p);
        totalWidth = std::max(totalWidth, size.width());
        rects[static_cast<std::size_t>(p)] = QRect(QPoint(0, y), size);
        y += size.height() + pageSpacing();
    }
    totalWidth += margins.left() + margins.right();
    for (int p = first; p < last; ++p) {
        QRect& r = rects[static_cast<std::size_t>(p)];
        r.moveLeft((std::max(totalWidth, viewportSize.width()) - r.width()) / 2);
    }
    return rects;
}

QRect PdfLinkView::pageRect(int page) const {
    const std::vector<QRect> rects = layout();
    return page >= 0 && std::size_t(page) < rects.size() ? rects[std::size_t(page)] : QRect();
}

// QPdfView draws a page at resolution x (laid-out width / width at the bare resolution).
qreal PdfLinkView::pageScale(int page, const QRect& rect) const {
    if (zoomMode() == ZoomMode::Custom)
        return screenResolution() * zoomFactor();
    const int base = QSizeF(document()->pagePointSize(page) * screenResolution()).toSize().width();
    return base > 0 ? screenResolution() * qreal(rect.width()) / qreal(base) : screenResolution();
}

PdfLinkView::Hit PdfLinkView::pageAt(QPointF viewportPos) const {
    const QPdfDocument* doc = document();
    if (!doc || doc->status() != QPdfDocument::Status::Ready)
        return {};
    const QPointF docPos =
        viewportPos + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const std::vector<QRect> rects = layout();
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const QRect& rect = rects[i];
        if (rect.isEmpty())
            continue;
        if (docPos.y() < rect.top())
            break; // pages are laid out downwards: past the position
        if (QRectF(rect).contains(docPos)) {
            const int p = static_cast<int>(i);
            return {p, (docPos - rect.topLeft()) / pageScale(p, rect)};
        }
    }
    return {};
}

QPdfLink PdfLinkView::linkAt(QPointF viewportPos) const {
    const Hit hit = pageAt(viewportPos);
    if (hit.page < 0)
        return {};
    if (links_.page() != hit.page)
        links_.setPage(hit.page);
    return links_.linkAt(hit.points);
}

void PdfLinkView::follow(const QPdfLink& link) {
    if (!link.isValid() || link.page() < 0)
        return;
    // The navigator keeps the history (Back) and scrolls to the page's top; then the link's place.
    pageNavigator()->jump(link.page(), link.location(), link.zoom());
    const QRect rect = pageRect(link.page());
    if (rect.isEmpty())
        return;
    const qreal top = rect.top() + link.location().y() * pageScale(link.page(), rect);
    verticalScrollBar()->setValue(int(top) - documentMargins().top());
}

void PdfLinkView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::BackButton) {
        if (pageNavigator()->backAvailable())
            pageNavigator()->back();
        return;
    }
    if (event->button() == Qt::ForwardButton) {
        if (pageNavigator()->forwardAvailable())
            pageNavigator()->forward();
        return;
    }
    pressed_ = event->button() == Qt::LeftButton;
    pressPos_ = event->position();
}

void PdfLinkView::mouseMoveEvent(QMouseEvent* event) {
    viewport()->setCursor(linkAt(event->position()).isValid() ? Qt::PointingHandCursor
                                                                : Qt::ArrowCursor);
}

void PdfLinkView::mouseReleaseEvent(QMouseEvent* event) {
    const bool click = pressed_ && event->button() == Qt::LeftButton &&
                       (event->position() - pressPos_).manhattanLength() < 6;
    pressed_ = false;
    if (click)
        follow(linkAt(event->position()));
}
