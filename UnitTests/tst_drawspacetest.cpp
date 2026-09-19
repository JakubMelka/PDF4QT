// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "pdfcms.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfprogress.h"

#include <QtTest>
#include <QLayout>
#include <QScrollBar>

class DrawSpaceTest : public QObject
{
    Q_OBJECT

private slots:
    void fitWidthFillsViewportWidth();
    void fitHeightFillsViewportHeight();
    void fitPageFillsViewport();
    void fitIsIndependentOfLogicalSizeZooming();
    void fitDoesNotOscillate();
};

struct DrawSpaceFixture
{
    pdf::PDFDocument document;
    pdf::PDFCMSManager cms{nullptr};
    pdf::PDFProgress progress{nullptr};
    pdf::PDFWidget widget{&cms, pdf::RendererEngine::QPainter, nullptr};

    explicit DrawSpaceFixture(pdf::PDFDocument doc, pdf::PageLayout pageLayout, QSize size) :
        document(std::move(doc))
    {
        widget.resize(size);
        widget.getDrawWidgetProxy()->setProgress(&progress);
        widget.setDocument(pdf::PDFModifiedDocument(&document, nullptr), {});
        widget.getDrawWidgetProxy()->setPageLayout(pageLayout);
        settle();
    }

    ~DrawSpaceFixture()
    {
        widget.setDocument(pdf::PDFModifiedDocument(), {});
    }

    /// The widget is never shown, so the grid layout must be activated by hand.
    /// Showing or hiding a scrollbar resizes the draw widget, which recalculates
    /// the draw space again - a few rounds are needed before the sizes settle.
    void settle()
    {
        for (int i = 0; i < 4; ++i)
        {
            widget.layout()->activate();
            widget.getDrawWidgetProxy()->update();
        }
    }

    void performOperation(pdf::PDFDrawWidgetProxy::Operation operation)
    {
        widget.getDrawWidgetProxy()->performOperation(operation);
        settle();
    }

    /// Returns the area which is really available for the pages, i.e. the widget
    /// decreased by the scrollbars which are visible.
    QSize getAvailableSize() const
    {
        QSize size = widget.size();

        if (!widget.getVerticalScrollbar()->isHidden())
        {
            size.rwidth() -= widget.getVerticalScrollbar()->sizeHint().width();
        }

        if (!widget.getHorizontalScrollbar()->isHidden())
        {
            size.rheight() -= widget.getHorizontalScrollbar()->sizeHint().height();
        }

        return size;
    }

    /// Returns the size of the first page in pixels, as it is displayed now
    QSizeF getPageSizeInPixels() const
    {
        const pdf::PDFDrawWidgetProxy* proxy = widget.getDrawWidgetProxy();
        const QSizeF pageSizeMM = document.getCatalog()->getPage(0)->getRotatedMediaBoxMM().size();

        return QSizeF(proxy->transformDeviceSpaceToPixel(pageSizeMM.width()),
                      proxy->transformDeviceSpaceToPixel(pageSizeMM.height()));
    }
};

static pdf::PDFDocument createDocument(int pageCount)
{
    pdf::PDFDocumentBuilder builder;

    for (int i = 0; i < pageCount; ++i)
    {
        // A4 portrait, in the default user space units (1/72 inch)
        builder.appendPage(QRectF(0, 0, 595, 842));
    }

    return builder.build();
}

// The fit must use the whole available area. A tolerance of a few pixels covers
// the margin which keeps the rounding of the page rectangles from making a
// scrollbar appear.
static constexpr qreal FIT_TOLERANCE = 3.0;

#define QVERIFY_FITS(actual, available) \
    do { \
        QVERIFY2((actual) <= (available), \
                 qPrintable(QString("%1 does not fit into %2").arg(actual).arg(available))); \
        QVERIFY2((actual) >= (available) - FIT_TOLERANCE, \
                 qPrintable(QString("%1 wastes too much of %2").arg(actual).arg(available))); \
    } while (false)

void DrawSpaceTest::fitWidthFillsViewportWidth()
{
    DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::OneColumn, QSize(800, 600));
    fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);

    QVERIFY_FITS(fixture.getPageSizeInPixels().width(), fixture.getAvailableSize().width());
}

void DrawSpaceTest::fitHeightFillsViewportHeight()
{
    DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::OneColumn, QSize(800, 600));
    fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFitHeight);

    QVERIFY_FITS(fixture.getPageSizeInPixels().height(), fixture.getAvailableSize().height());
}

void DrawSpaceTest::fitPageFillsViewport()
{
    // Single page layout uses the vertical scrollbar to switch the pages, so its
    // space must be reserved by the fit even though nothing overflows vertically.
    DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::SinglePage, QSize(800, 600));
    fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);

    const QSizeF pageSize = fixture.getPageSizeInPixels();
    const QSize availableSize = fixture.getAvailableSize();

    QVERIFY(pageSize.width() <= availableSize.width());
    QVERIFY(pageSize.height() <= availableSize.height());

    // The page is higher than wide, so the height is the limiting dimension
    QVERIFY_FITS(pageSize.height(), availableSize.height());

    // No page of the document may be clipped horizontally
    QVERIFY(fixture.widget.getHorizontalScrollbar()->isHidden());
}

void DrawSpaceTest::fitIsIndependentOfLogicalSizeZooming()
{
    // The zoom hint and the page layout must derive the pixel per millimeter ratio
    // from the same place. When they do not (issue #391 - the layout used the primary
    // screen, while the hint used the screen of the widget), the fitted page does not
    // fill the viewport. Logical size zooming changes that ratio, so it detects the
    // two sources of truth without a second monitor.
    for (bool logicalSizeZooming : { false, true })
    {
        DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::OneColumn, QSize(800, 600));

        pdf::PDFRenderer::Features features = fixture.widget.getDrawWidgetProxy()->getFeatures();
        features.setFlag(pdf::PDFRenderer::LogicalSizeZooming, logicalSizeZooming);
        fixture.widget.getDrawWidgetProxy()->setFeatures(features);
        fixture.settle();

        fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFitWidth);

        QVERIFY_FITS(fixture.getPageSizeInPixels().width(), fixture.getAvailableSize().width());
    }
}

void DrawSpaceTest::fitDoesNotOscillate()
{
    // The fit reserves the space of the scrollbars which will become visible. The
    // reservation must not be derived from the scrollbars which are visible right
    // now - the zoom would keep switching between the two states forever.
    DrawSpaceFixture fixture(createDocument(3), pdf::PageLayout::SinglePage, QSize(800, 600));
    fixture.performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);

    const pdf::PDFReal zoom = fixture.widget.getDrawWidgetProxy()->getZoom();

    for (int i = 0; i < 5; ++i)
    {
        fixture.widget.layout()->activate();
        fixture.widget.getDrawWidgetProxy()->update();
        QCOMPARE(fixture.widget.getDrawWidgetProxy()->getZoom(), zoom);
    }
}

QTEST_MAIN(DrawSpaceTest)

#include "tst_drawspacetest.moc"
