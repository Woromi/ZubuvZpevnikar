#pragma once

#include <QPdfLink>
#include <QPdfLinkModel>
#include <QPdfView>
#include <QPointF>
#include <QRect>
#include <vector>

// A QPdfView whose links work: QPdfView (Qt 6.10) hit-tests a click against its pages in document
// coordinates but takes the position in viewport coordinates, so a link only follows while the
// view is not scrolled. This one maps the position through the scroll bars, mirroring QPdfView's
// page layout (pages below each other in MultiPage mode, centred, `documentMargins`, `pageSpacing`),
// and jumps to the link's place on its page. The mouse's Back button goes back (QPdfPageNavigator).
class PdfLinkView : public QPdfView {
    Q_OBJECT

public:
    explicit PdfLinkView(QWidget* parent = nullptr);

    struct Hit {
        int page = -1;  // -1: no page there
        QPointF points; // the position on the page, in PDF points from its top left
    };
    // What lies under a viewport position.
    Hit pageAt(QPointF viewportPos) const;
    // The link under a viewport position (invalid if none).
    QPdfLink linkAt(QPointF viewportPos) const;
    // Scrolls to a link's destination (the page, the link's location at the top).
    void follow(const QPdfLink& link);
    // A page's rectangle in document coordinates (empty if not laid out).
    QRect pageRect(int page) const;

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::vector<QRect> layout() const;                   // every page's rectangle (document coords)
    qreal pageScale(int page, const QRect& rect) const; // pixels per PDF point

    mutable QPdfLinkModel links_;
    QPointF pressPos_;
    bool pressed_ = false;
};
