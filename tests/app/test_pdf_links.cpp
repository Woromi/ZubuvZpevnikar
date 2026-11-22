// PdfLinkView: links in a scrolled PDF view follow (QPdfView's own hit test ignores scrolling).
#include <QBuffer>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QScrollBar>
#include <QTest>
#include <catch2/catch_test_macros.hpp>

#include "PdfLinkView.hpp"
#include "Support.hpp"

namespace {

// A PDF of `pages` A5 pages; page `from` has a link (100,100)-(200,130) in PDF coordinates (origin
// bottom left) to the top of page `to`.
QByteArray linkedPdf(int pages, int from, int to) {
    QByteArray out = "%PDF-1.4\n";
    QList<qsizetype> offsets;
    const auto object = [&](const QByteArray& body) {
        offsets.append(out.size());
        out += QByteArray::number(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };
    // 1: catalog, 2: pages, 3: the link, 4..: pages
    const int firstPage = 4;
    QByteArray kids;
    for (int i = 0; i < pages; ++i)
        kids += QByteArray::number(firstPage + i) + " 0 R ";
    object("<< /Type /Catalog /Pages 2 0 R >>");
    object("<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pages) + " >>");
    object("<< /Type /Annot /Subtype /Link /Rect [100 465 200 495] /Border [0 0 0] /Dest [" +
           QByteArray::number(firstPage + to) + " 0 R /XYZ 0 595 0] >>");
    for (int i = 0; i < pages; ++i)
        object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 420 595]" +
               QByteArray(i == from ? " /Annots [3 0 R]" : "") + " >>");
    const qsizetype xref = out.size();
    out += "xref\n0 " + QByteArray::number(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (const qsizetype o : offsets)
        out += QByteArray::number(o).rightJustified(10, '0') + " 00000 n \n";
    out += "trailer\n<< /Size " + QByteArray::number(offsets.size() + 1) +
           " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return out;
}

struct View {
    QBuffer buffer;
    QPdfDocument document;
    PdfLinkView view;
    explicit View(const QByteArray& pdf) {
        buffer.setData(pdf);
        buffer.open(QIODevice::ReadOnly);
        document.load(&buffer);
        view.setDocument(&document);
        view.setPageMode(QPdfView::PageMode::MultiPage);
        view.setZoomMode(QPdfView::ZoomMode::FitToWidth);
        view.resize(400, 500);
        view.show();
        REQUIRE(QTest::qWaitForWindowExposed(&view));
    }
    // The link's centre ((150,120) from the page's top left, in points) on screen.
    QPoint linkCentre(int page) {
        const QRect rect = view.pageRect(page);
        const qreal scale = rect.width() / 420.0;
        return (QPointF(rect.topLeft()) + QPointF(150, 595 - 480) * scale -
                QPointF(view.horizontalScrollBar()->value(), view.verticalScrollBar()->value()))
            .toPoint();
    }
};

} // namespace

TEST_CASE("PdfLinkView: a link on a scrolled-to page follows", "[app][pdf]") {
    View v(linkedPdf(5, 3, 1));
    REQUIRE(v.document.status() == QPdfDocument::Status::Ready);
    v.view.verticalScrollBar()->setValue(v.view.pageRect(3).top());
    QCoreApplication::processEvents();
    const QPoint at = v.linkCentre(3);
    REQUIRE(v.view.rect().contains(at));
    CHECK(v.view.pageAt(at).page == 3);
    CHECK(v.view.linkAt(at).isValid());
    CHECK(v.view.linkAt(at + QPoint(0, 200)).isValid() == false);

    QTest::mouseClick(v.view.viewport(), Qt::LeftButton, {}, at);
    QCoreApplication::processEvents();
    CHECK(v.view.pageNavigator()->currentPage() == 1);
    const int top = v.view.pageRect(1).top();
    CHECK(std::abs(v.view.verticalScrollBar()->value() - top) <= 8);

    // Back returns to the page with the link.
    QTest::mousePress(v.view.viewport(), Qt::BackButton, {}, QPoint(10, 10));
    QCoreApplication::processEvents();
    CHECK(v.view.pageNavigator()->currentPage() == 3);
}

TEST_CASE("PdfLinkView: a drag over a link does not follow it", "[app][pdf]") {
    View v(linkedPdf(3, 0, 2));
    const QPoint at = v.linkCentre(0);
    REQUIRE(v.view.linkAt(at).isValid());
    QTest::mousePress(v.view.viewport(), Qt::LeftButton, {}, at + QPoint(0, 40));
    QTest::mouseRelease(v.view.viewport(), Qt::LeftButton, {}, at);
    CHECK(v.view.verticalScrollBar()->value() == 0);
    QTest::mouseClick(v.view.viewport(), Qt::LeftButton, {}, at);
    CHECK(v.view.pageNavigator()->currentPage() == 2);
}
