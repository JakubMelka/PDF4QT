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
#include "pdfconstants.h"
#include "pdfcompiler.h"
#include "pdfdocumentbuilder.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfannotation.h"
#include "pdfitemmodels.h"
#include "pdfoptionalcontent.h"
#include "pdfprogress.h"

#include <QtTest>
#include <QLayout>
#include <QPainter>
#include <QScrollBar>
#include <QThread>

#include <algorithm>
#include <vector>

class DrawSpaceTest : public QObject
{
    Q_OBJECT

private slots:
    void fitWidthFillsViewportWidth();
    void fitHeightFillsViewportHeight();
    void fitPageFillsViewport();
    void fitIsIndependentOfLogicalSizeZooming();
    void fitDoesNotOscillate();
    void compilerServesRequestsOfIdleWorker();
    void compilerStoppedDuringCompilation();
    void activityDiesBeforeItsHolders();
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

    /// Multiplier of the time limits: PDF4QT_TEST_TIMEOUT_FACTOR (at most 1000), or 20
    /// under valgrind
    static int getTimeoutFactor()
    {
        bool ok = false;
        const int factor = qEnvironmentVariableIntValue("PDF4QT_TEST_TIMEOUT_FACTOR", &ok);
        if (ok && factor > 0)
        {
            return qMin(factor, 1000);
        }
        if (qEnvironmentVariable("LD_PRELOAD").contains(QLatin1String("vgpreload")))
        {
            return 20;
        }
        return 1;
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

/// A document of empty pages except of the page \p heavyPage, which has many
/// operators (its compilation takes a while and polls the cancellation per operator)
static pdf::PDFDocument createDocumentWithHeavyPage(int pageCount, int heavyPage)
{
    pdf::PDFDocumentBuilder builder;

    for (int i = 0; i < pageCount; ++i)
    {
        const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595, 842));

        if (i == heavyPage)
        {
            pdf::PDFPageContentStreamBuilder content(&builder);
            QPainter* painter = content.begin(page);
            painter->setPen(Qt::NoPen);
            for (int rectangle = 0; rectangle < 20000; ++rectangle)
            {
                painter->setBrush(QColor::fromRgb(rectangle % 256, (rectangle / 256) % 256, 128));
                painter->drawRect(QRectF((rectangle % 100) * 5.0, (rectangle / 100) % 160 * 5.0, 4.0, 4.0));
            }
            content.end(painter);
        }
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

void DrawSpaceTest::compilerServesRequestsOfIdleWorker()
{
    // The worker of the asynchronous page compiler waits on a wait condition, when
    // it has no task; the wait unlocks the mutex of the tasks (inside of Qt) and the
    // GUI thread locks it meanwhile to add a task and to set the active pages. A
    // page requested while the worker waits wakes the worker up, is compiled and
    // announced by pageImageChanged; a persistent request is not cancelled by the
    // active pages (pdfcompiler.h). Under ThreadSanitizer the test also checks, that
    // the wait is announced to the detector (PDFThreadAnnotations::wait): without
    // it, the next lock of the GUI thread is reported as a double lock of the mutex
    // and the accesses of the tasks as data races.
    constexpr pdf::PDFInteger PAGE_COUNT = 20;
    DrawSpaceFixture fixture(createDocument(PAGE_COUNT), pdf::PageLayout::OneColumn, QSize(800, 600));
    pdf::PDFAsynchronousPageCompiler* compiler = fixture.widget.getDrawWidgetProxy()->getCompiler();
    QCOMPARE(compiler->getState(), pdf::PDFAsynchronousPageCompiler::State::Active);

    std::vector<pdf::PDFInteger> announcedPages;
    QObject context;
    QObject::connect(compiler, &pdf::PDFAsynchronousPageCompiler::pageImageChanged, &context, [&announcedPages](bool all, const std::vector<pdf::PDFInteger>& pages)
    {
        if (!all)
        {
            announcedPages.insert(announcedPages.end(), pages.cbegin(), pages.cend());
        }
    });

    const int timeout = 10000 * DrawSpaceFixture::getTimeoutFactor();
    using CompileMode = pdf::PDFAsynchronousPageCompiler::CompileMode;

    // The pages at the end of the document: neither displayed nor prefetched by the
    // widget (it shows the first pages)
    for (pdf::PDFInteger pageIndex : { 12, 14, 16, 18 })
    {
        // Nothing to compile: the worker reaches its wait
        QTest::qWait(50);
        QVERIFY2(!compiler->getCompiledPage(pageIndex, CompileMode::None), qPrintable(QString("Page %1 is compiled before its request").arg(pageIndex)));

        announcedPages.clear();
        compiler->getCompiledPage(pageIndex, CompileMode::Persistent);
        compiler->setActivePages({ 0 });

        QTRY_VERIFY_WITH_TIMEOUT(compiler->getCompiledPage(pageIndex, CompileMode::None) != nullptr, timeout);
        QVERIFY(std::find(announcedPages.cbegin(), announcedPages.cend(), pageIndex) != announcedPages.cend());
    }
}

void DrawSpaceTest::compilerStoppedDuringCompilation()
{
    // stop() stops the compilation of a page, which is running in other threads
    // (pdfcompiler.h): it announces the stopping by the state of the compiler, which
    // the compiling threads poll (isOperationCancelled) while stop() changes it, and
    // waits for the worker. A stopped compilation is thrown away, it never puts a
    // part of the page into the cache: after a restart the page is compiled whole,
    // equal to an uninterrupted compilation. Under ThreadSanitizer the test also
    // checks, that the state is safe to read from the compiling threads.
    constexpr int PAGE_COUNT = 20;
    constexpr int HEAVY_PAGE = 15;
    DrawSpaceFixture fixture(createDocumentWithHeavyPage(PAGE_COUNT, HEAVY_PAGE), pdf::PageLayout::OneColumn, QSize(800, 600));
    pdf::PDFAsynchronousPageCompiler* compiler = fixture.widget.getDrawWidgetProxy()->getCompiler();
    QCOMPARE(compiler->getState(), pdf::PDFAsynchronousPageCompiler::State::Active);

    const int timeout = 30000 * DrawSpaceFixture::getTimeoutFactor();
    using CompileMode = pdf::PDFAsynchronousPageCompiler::CompileMode;
    using State = pdf::PDFAsynchronousPageCompiler::State;

    // The uninterrupted compilation
    compiler->getCompiledPage(HEAVY_PAGE, CompileMode::Persistent);
    QTRY_VERIFY_WITH_TIMEOUT(compiler->getCompiledPage(HEAVY_PAGE, CompileMode::None) != nullptr, timeout);
    const pdf::PDFPrecompiledPage* page = compiler->getCompiledPage(HEAVY_PAGE, CompileMode::None);
    QVERIFY(page->isValid());
    QVERIFY(page->getErrors().isEmpty());
    const qint64 wholePageSize = page->getMemoryConsumptionEstimate();

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        // A new compilation (the cache is cleared), stopped while it runs
        compiler->stop(true);
        QCOMPARE(compiler->getState(), State::Inactive);
        compiler->start();
        QCOMPARE(compiler->getState(), State::Active);
        QVERIFY(!compiler->getCompiledPage(HEAVY_PAGE, CompileMode::Persistent));
        QThread::msleep(5);
        compiler->stop(false);
        QCOMPARE(compiler->getState(), State::Inactive);
        QVERIFY(!compiler->getCompiledPage(HEAVY_PAGE, CompileMode::None));

        // After the restart the page is compiled whole
        compiler->start();
        const pdf::PDFPrecompiledPage* restartedPage = compiler->getCompiledPage(HEAVY_PAGE, CompileMode::Persistent);
        if (!restartedPage)
        {
            QTRY_VERIFY_WITH_TIMEOUT(compiler->getCompiledPage(HEAVY_PAGE, CompileMode::None) != nullptr, timeout);
            restartedPage = compiler->getCompiledPage(HEAVY_PAGE, CompileMode::None);
        }
        QVERIFY(restartedPage->getErrors().isEmpty());
        QCOMPARE(restartedPage->getMemoryConsumptionEstimate(), wholePageSize);
    }
}

void DrawSpaceTest::activityDiesBeforeItsHolders()
{
    // Owner 2026-11-07: the holders of an optional content activity of another owner (the
    // draw space of a view, the tree of the layers, the annotations) keep it by QPointer -
    // an activity deleted first is null for them (every layer drawn, no state shown), never
    // freed memory (UnitTestsAppMcp catalogRerunsOnNewVersions read a deleted one)
    pdf::PDFDocumentBuilder builder;
    builder.appendPage(QRectF(0, 0, 200, 200));
    const auto name = [](const char* text) { return pdf::PDFObject::createName(QByteArray(text)); };
    pdf::PDFDictionaryBuilder layer;
    layer.setEntry(pdf::PDFInplaceOrMemoryString("Type"), name("OCG"));
    layer.setEntry(pdf::PDFInplaceOrMemoryString("Name"), pdf::PDFObject::createString(QByteArray("Layer")));
    const pdf::PDFObjectReference layerReference = builder.addObject(pdf::PDFObject::createDictionary(std::move(layer)));
    pdf::PDFArrayBuilder groups;
    groups.appendItem(pdf::PDFObject::createReference(layerReference));
    pdf::PDFArrayBuilder order;
    order.appendItem(pdf::PDFObject::createReference(layerReference));
    pdf::PDFDictionaryBuilder configuration;
    configuration.setEntry(pdf::PDFInplaceOrMemoryString("Order"), pdf::PDFObject::createArray(std::move(order)));
    pdf::PDFDictionaryBuilder properties;
    properties.setEntry(pdf::PDFInplaceOrMemoryString("OCGs"), pdf::PDFObject::createArray(std::move(groups)));
    properties.setEntry(pdf::PDFInplaceOrMemoryString("D"), pdf::PDFObject::createDictionary(std::move(configuration)));
    builder.setCatalogOptionalContentProperties(builder.addObject(pdf::PDFObject::createDictionary(std::move(properties))));
    DrawSpaceFixture fixture(builder.build(), pdf::PageLayout::OneColumn, QSize(400, 400));
    pdf::PDFDocument& document = fixture.document;
    QVERIFY(document.getCatalog()->getOptionalContentProperties()->isValid());

    // The widget is never shown: no page is compiled, nothing reads the activity in a worker
    pdf::PDFOptionalContentActivity* activity = new pdf::PDFOptionalContentActivity(&document, pdf::OCUsage::View, nullptr);
    const pdf::PDFModifiedDocument modified(&document, activity, pdf::PDFModifiedDocument::Reset);
    fixture.widget.setDocument(modified, {});
    const pdf::PDFDrawWidgetProxy* proxy = fixture.widget.getDrawWidgetProxy();
    pdf::PDFOptionalContentTreeItemModel model(nullptr);
    model.setDocument(modified);
    model.setActivity(activity);
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFCMSManager cmsManager(nullptr);
    pdf::PDFAnnotationManager annotationManager(&fontCache, nullptr, &cmsManager, activity, pdf::PDFMeshQualitySettings(), pdf::PDFRenderer::getDefaultFeatures(),
                                                pdf::PDFAnnotationManager::Target::View, nullptr);

    // The layer is on by the default configuration (ISO 32000-2, 8.11.4.3: /BaseState ON)
    QCOMPARE(proxy->getOptionalContentActivity(), activity);
    QCOMPARE(annotationManager.getOptionalActivity(), activity);
    QCOMPARE(model.rowCount(QModelIndex()), 1);
    const QModelIndex row = model.index(0, 0, QModelIndex());
    QCOMPARE(row.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));

    delete activity;

    // Nobody holds the deleted activity: no state is shown, nothing can be switched
    QVERIFY(proxy->getOptionalContentActivity() == nullptr);
    QVERIFY(annotationManager.getOptionalActivity() == nullptr);
    QCOMPARE(row.data(Qt::CheckStateRole).toInt(), int(Qt::PartiallyChecked));
    QVERIFY(!model.setData(row, int(Qt::Unchecked), Qt::CheckStateRole));
}

QTEST_MAIN(DrawSpaceTest)

#include "tst_drawspacetest.moc"
