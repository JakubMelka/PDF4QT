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

// The overlay and the views of Compare (plan of step 7 of phase 2, 4.9, 8; package P6): the test of the overlay of
// the old Diff application, rewritten against PDFAppCompareView before Pdf4QtDiff is deleted (the test no longer
// compiles the sources of Pdf4QtDiff). The behavior kept is the one of the old test (eb3bc9c2): the integration of
// the canvas (the fit of the two pages of a pair on the screen), the fit of the page sizes of a pair, the manual
// scale and shift, the layout of the original sizes and the other views, the unpaired and the filtered pages, the
// controls and Reset (now the section Overlay of the right panel of the tab, 493:18777); and the drawing of the
// differences (the colors of the kinds, the bubble of a page).

#include "apptoolowntabfixture.h"

#include "pdfappcompareview.h"
#include "pdfapptoolgui_compare.h"
#include "pdfapptoolinputlist.h"
#include "pdfcomponentdelegates.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolshell.h"
#include "pdfdocumentwriter.h"
#include "pdfwidgetstyle.h"
#include "pdfthememanager.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QSlider>
#include <QtTest>

#include <array>
#include <tuple>

using namespace pdftest;

class DiffOverlayTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void viewIntegration();
    void fitPageSizes_data();
    void fitPageSizes();
    void manualScaleAndShift();
    void originalLayoutAndOtherViews();
    void unpairedAndFilteredPages();
    void hostileSequence();
    void differencesDrawing();
    void controlsAndReset();
};

/// The documents of a layout: the original pages, the revised pages and the shown document of their pages in this
/// order (the pages of PDFDiffReportBuilder::createCombinedDocument of Side by Side)
struct OverlayFixture
{
    pdf::PDFDocument original;
    pdf::PDFDocument revised;
    pdf::PDFDocument combined;
    std::vector<pdf::PDFDiffCombinedPage> pages;
    pdf::PDFDiffResult diff;
    pdf::PDFAppCompareLayout layout;

    OverlayFixture(const QList<QSizeF>& originalSizes, const QList<QSizeF>& revisedSizes, pdf::PDFDiffResult::PageSequence sequence, bool rotateOriginal = false)
    {
        pdf::PDFDocumentBuilder originalBuilder;
        pdf::PDFDocumentBuilder revisedBuilder;
        pdf::PDFDocumentBuilder combinedBuilder;
        for (int i = 0; i < originalSizes.size(); ++i)
        {
            // A nonzero origin of the media box never changes the placement
            const QRectF box(QPointF(17.0, 23.0), originalSizes[i] * (72.0 / 25.4));
            const pdf::PDFObjectReference page = originalBuilder.appendPage(box);
            const pdf::PDFObjectReference combinedPage = combinedBuilder.appendPage(box);
            if (rotateOriginal)
            {
                originalBuilder.setPageRotation(page, pdf::PageRotation::Rotate90);
                combinedBuilder.setPageRotation(combinedPage, pdf::PageRotation::Rotate90);
            }
            pdf::PDFDiffCombinedPage combinedItem;
            combinedItem.originalPage = i;
            pages.push_back(combinedItem);
        }
        for (int i = 0; i < revisedSizes.size(); ++i)
        {
            const QRectF box(QPointF(), revisedSizes[i] * (72.0 / 25.4));
            revisedBuilder.appendPage(box);
            combinedBuilder.appendPage(box);
            pdf::PDFDiffCombinedPage combinedItem;
            combinedItem.revisedPage = i;
            pages.push_back(combinedItem);
        }
        original = originalBuilder.build();
        revised = revisedBuilder.build();
        combined = combinedBuilder.build();
        diff.setPageSequence(std::move(sequence));
    }

    void update(const pdf::PDFAppCompareOverlaySettings& settings = {}, pdf::PDFDiffReportView view = pdf::PDFDiffReportView::Overlay, bool isOnlyChangedPages = false)
    {
        layout.update(view, isOnlyChangedPages, diff, &combined, pages, settings);
    }
};

/// Helpers of the test
class DiffOverlayTestHelper
{
public:
    DiffOverlayTestHelper() = delete;

    static bool closeSize(QSizeF actual, QSizeF expected)
    {
        return qAbs(actual.width() - expected.width()) < 0.001 && qAbs(actual.height() - expected.height()) < 0.001;
    }

    /// A page 300 x 400 times the scale with a frame of the color 20 units inside (the drawing of the old test)
    static pdf::PDFDocument createDrawing(double scale, QColor color)
    {
        pdf::PDFDocumentBuilder builder;
        pdf::PDFPageContentStreamBuilder content(&builder);
        QPainter* painter = content.beginNewPage(QRectF(0, 0, 300 * scale, 400 * scale));
        painter->scale(scale, scale);
        painter->fillRect(QRectF(20, 20, 260, 360), color);
        painter->fillRect(QRectF(40, 40, 220, 320), Qt::white);
        content.end(painter);
        return builder.build();
    }

    /// Pages with one rectangle each at the rectangles (points)
    static pdf::PDFDocument createRectangles(const QList<QRectF>& rectangles)
    {
        pdf::PDFDocumentBuilder builder;
        pdf::PDFPageContentStreamBuilder content(&builder);
        for (const QRectF& rectangle : rectangles)
        {
            QPainter* painter = content.beginNewPage(QRectF(0, 0, 400, 400));
            painter->fillRect(rectangle, Qt::black);
            content.end(painter);
        }
        return builder.build();
    }

    /// The comparison of the old Diff (the thin wrapper start, synchronous)
    static pdf::PDFDiffResult compare(const pdf::PDFDocument& original, const pdf::PDFDocument& revised)
    {
        pdf::PDFDiff diff(nullptr);
        diff.setOption(pdf::PDFDiff::Asynchronous, false);
        diff.setLeftDocument(&original);
        diff.setRightDocument(&revised);
        pdf::PDFClosedIntervalSet originalPages;
        originalPages.addInterval(0, pdf::PDFInteger(original.getCatalog()->getPageCount()) - 1);
        pdf::PDFClosedIntervalSet revisedPages;
        revisedPages.addInterval(0, pdf::PDFInteger(revised.getCatalog()->getPageCount()) - 1);
        diff.setPagesForLeftDocument(originalPages);
        diff.setPagesForRightDocument(revisedPages);
        diff.start();
        return diff.getResult();
    }

    /// The combined document of Side by Side built by hand: the original pages, then the revised ones
    static pdf::PDFDiffCombinedDocument combine(const pdf::PDFDocument& original, const pdf::PDFDocument& revised)
    {
        pdf::PDFDiffCombinedDocument result;
        pdf::PDFDocumentBuilder builder;
        for (const pdf::PDFDocument* document : { &original, &revised })
        {
            for (size_t i = 0; i < document->getCatalog()->getPageCount(); ++i)
            {
                builder.appendPage(document->getCatalog()->getPage(i)->getMediaBox());
                pdf::PDFDiffCombinedPage page;
                (document == &original ? page.originalPage : page.revisedPage) = pdf::PDFInteger(i);
                result.pages.push_back(page);
            }
        }
        result.document = pdf::PDFDocumentPointer(new pdf::PDFDocument(builder.build()));
        return result;
    }
};

using H = DiffOverlayTestHelper;

void DiffOverlayTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void DiffOverlayTest::viewIntegration()
{
    // The canvas: the document of the pairs, Overlay with Fit - the two pages of the pair lie in one rectangle on the
    // screen and the frame of the smaller drawing (20, 20) meets the frame of the larger one (40, 40)
    const pdf::PDFDocument original = H::createDrawing(1.0, Qt::red);
    const pdf::PDFDocument revised = H::createDrawing(2.0, Qt::blue);
    const pdf::PDFDiffCombinedDocument combined = H::combine(original, revised);

    pdf::PDFAppCompareView view;
    view.resize(1200, 900);
    view.setDocument(combined);
    pdf::PDFDiffResult sequence;
    sequence.setPageSequence({ { 0, 0 } });
    view.setResult(sequence);
    view.setView(pdf::PDFDiffReportView::Overlay);
    pdf::PDFAppCompareOverlaySettings settings;
    settings.scaleMode = pdf::PDFAppCompareOverlaySettings::ScaleMode::Fit;
    view.setOverlaySettings(settings);

    pdf::PDFWidget* widget = view.getPdfWidget();
    QVERIFY(widget);
    pdf::PDFDrawWidgetProxy* proxy = widget->getDrawWidgetProxy();
    QCOMPARE(proxy->getDocument(), combined.document.data());
    widget->resize(700, 700);
    widget->getDrawWidget()->getWidget()->resize(700, 700);
    proxy->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
    const auto snapshot = proxy->getSnapshot();
    QCOMPARE(snapshot.items.size(), size_t(2));
    QCOMPARE(snapshot.items[0].rect, snapshot.items[1].rect);
    const QPointF originalCorner = snapshot.items[0].pageToDeviceMatrix.map(QPointF(20, 20));
    const QPointF revisedCorner = snapshot.items[1].pageToDeviceMatrix.map(QPointF(40, 40));
    QVERIFY(QLineF(originalCorner, revisedCorner).length() < 1.0);

    // Side by Side: the two pages next to each other, the original one left
    view.setView(pdf::PDFDiffReportView::SideBySide);
    proxy->performOperation(pdf::PDFDrawWidgetProxy::ZoomFit);
    const auto sideBySide = proxy->getSnapshot();
    QCOMPARE(sideBySide.items.size(), size_t(2));
    QVERIFY(sideBySide.items[0].rect.right() < sideBySide.items[1].rect.left());

    // The page groups of the overlay are blended: the share of the revised page
    QCOMPARE(view.getLayout().getLayout().size(), size_t(2));
    settings.blend = 30;
    view.setOverlaySettings(settings);
    QCOMPARE(view.getOverlaySettings().blend, 30);
}

void DiffOverlayTest::fitPageSizes_data()
{
    QTest::addColumn<QSizeF>("original");
    QTest::addColumn<QSizeF>("revised");
    QTest::addColumn<QSizeF>("expectedOriginal");
    QTest::addColumn<QSizeF>("expectedRevised");
    QTest::addColumn<bool>("rotateOriginal");
    QTest::newRow("A4 to A3") << QSizeF(210, 297) << QSizeF(297, 420) << QSizeF(210.0 * 420.0 / 297.0, 420) << QSizeF(297, 420) << false;
    QTest::newRow("A3 to A4") << QSizeF(297, 420) << QSizeF(210, 297) << QSizeF(297, 420) << QSizeF(210.0 * 420.0 / 297.0, 420) << false;
    QTest::newRow("same size") << QSizeF(210, 297) << QSizeF(210, 297) << QSizeF(210, 297) << QSizeF(210, 297) << false;
    QTest::newRow("different proportions") << QSizeF(100, 100) << QSizeF(300, 200) << QSizeF(200, 200) << QSizeF(300, 200) << false;
    QTest::newRow("rotated page") << QSizeF(100, 200) << QSizeF(400, 200) << QSizeF(400, 200) << QSizeF(400, 200) << true;
}

void DiffOverlayTest::fitPageSizes()
{
    QFETCH(QSizeF, original);
    QFETCH(QSizeF, revised);
    QFETCH(QSizeF, expectedOriginal);
    QFETCH(QSizeF, expectedRevised);
    QFETCH(bool, rotateOriginal);
    OverlayFixture fixture({ original }, { revised }, { { 0, 0 } }, rotateOriginal);
    pdf::PDFAppCompareOverlaySettings settings;
    settings.scaleMode = pdf::PDFAppCompareOverlaySettings::ScaleMode::Fit;
    fixture.update(settings);
    const auto& layout = fixture.layout.getLayout();
    QCOMPARE(layout.size(), size_t(2));
    QVERIFY(H::closeSize(layout[0].pageRectMM.size(), expectedOriginal));
    QVERIFY(H::closeSize(layout[1].pageRectMM.size(), expectedRevised));
    QCOMPARE(layout[0].pageRectMM.center(), layout[1].pageRectMM.center());
    QCOMPARE(layout[0].groupIndex, pdf::PDFAppCompareLayout::ORIGINAL_GROUP);
    QCOMPARE(layout[1].groupIndex, pdf::PDFAppCompareLayout::REVISED_GROUP);
    QCOMPARE(fixture.layout.getOriginalPageIndex(0), pdf::PDFInteger(0));
    QCOMPARE(fixture.layout.getRevisedPageIndex(1), pdf::PDFInteger(0));
    QCOMPARE(fixture.layout.getRevisedPageIndex(0), pdf::PDFInteger(-1));
    // Scaling the view never rewrites the source pages
    QVERIFY(H::closeSize(fixture.revised.getCatalog()->getPage(0)->getRotatedMediaBoxMM().size(), revised));
}

void DiffOverlayTest::manualScaleAndShift()
{
    OverlayFixture fixture({ { 100, 200 }, { 100, 200 } }, { { 200, 400 }, { 200, 400 } }, { { 0, 0 }, { 1, 1 } });
    pdf::PDFAppCompareOverlaySettings settings;
    settings.scaleMode = pdf::PDFAppCompareOverlaySettings::ScaleMode::Manual;
    settings.originalScale = 2.0;
    settings.revisedScale = 0.75;
    settings.revisedOffsetMM = QPointF(12.5, -160.0);
    fixture.update(settings);
    const auto& layout = fixture.layout.getLayout();
    QCOMPARE(layout.size(), size_t(4));
    QVERIFY(H::closeSize(layout[0].pageRectMM.size(), QSizeF(200, 400)));
    QVERIFY(H::closeSize(layout[1].pageRectMM.size(), QSizeF(150, 300)));
    QCOMPARE(layout[1].pageRectMM.center() - layout[0].pageRectMM.center(), settings.revisedOffsetMM);
    const QRectF firstPair = layout[0].pageRectMM.united(layout[1].pageRectMM);
    const QRectF secondPair = layout[2].pageRectMM.united(layout[3].pageRectMM);
    QCOMPARE(firstPair.top(), 0.0);
    QVERIFY(qAbs(secondPair.top() - firstPair.bottom() - pdf::PDFAppCompareLayout::PAGE_GAP) < 0.001);
    QCOMPARE(fixture.layout.getPageIndexFromRevised(1), pdf::PDFInteger(3));
    QCOMPARE(fixture.layout.getPageIndexFromOriginal(1), pdf::PDFInteger(1));
}

void DiffOverlayTest::originalLayoutAndOtherViews()
{
    // Original sizes: each page keeps its size, both aligned at the top
    OverlayFixture fixture({ { 100, 200 } }, { { 200, 400 } }, { { 0, 0 } });
    fixture.update();
    const auto original = fixture.layout.getLayout();
    QVERIFY(H::closeSize(original[0].pageRectMM.size(), QSizeF(100, 200)));
    QVERIFY(H::closeSize(original[1].pageRectMM.size(), QSizeF(200, 400)));
    QCOMPARE(original[0].pageRectMM.top(), original[1].pageRectMM.top());

    // The overlay changes only the overlay: Side by Side, Original Only and Revised Only stay
    pdf::PDFAppCompareOverlaySettings settings;
    settings.scaleMode = pdf::PDFAppCompareOverlaySettings::ScaleMode::Manual;
    settings.originalScale = 3.0;
    settings.revisedScale = 2.0;
    settings.revisedOffsetMM = QPointF(100, 100);
    for (pdf::PDFDiffReportView view : { pdf::PDFDiffReportView::SideBySide, pdf::PDFDiffReportView::Original, pdf::PDFDiffReportView::Revised })
    {
        fixture.update({}, view);
        const auto expected = fixture.layout.getLayout();
        fixture.update(settings, view);
        QVERIFY(fixture.layout.getLayout() == expected);
    }
    fixture.update(settings);
    fixture.update();
    QVERIFY(fixture.layout.getLayout() == original);

    // Side by Side: the original left of the axis, the revised right of it, 10 mm apart, one row; one document: its
    // pages only, centered
    fixture.update({}, pdf::PDFDiffReportView::SideBySide);
    const auto sideBySide = fixture.layout.getLayout();
    QCOMPARE(sideBySide.size(), size_t(2));
    QCOMPARE(sideBySide[0].pageRectMM.right(), -pdf::PDFAppCompareLayout::PAGE_GAP);
    QCOMPARE(sideBySide[1].pageRectMM.left(), pdf::PDFAppCompareLayout::PAGE_GAP);
    QCOMPARE(sideBySide[0].pageRectMM.top(), sideBySide[1].pageRectMM.top());
    fixture.update({}, pdf::PDFDiffReportView::Revised);
    QCOMPARE(fixture.layout.getLayout().size(), size_t(1));
    QCOMPARE(fixture.layout.getLayout()[0].pageIndex, pdf::PDFInteger(1));
    QCOMPARE(fixture.layout.getLayout()[0].pageRectMM.center().x(), 0.0);
}

void DiffOverlayTest::unpairedAndFilteredPages()
{
    OverlayFixture fixture({ { 100, 200 }, { 150, 300 } }, { { 200, 400 }, { 300, 600 } }, { { 0, 0 }, { 1, -1 }, { -1, 1 } });
    pdf::PDFAppCompareOverlaySettings settings;
    settings.scaleMode = pdf::PDFAppCompareOverlaySettings::ScaleMode::Manual;
    settings.originalScale = 2.0;
    settings.revisedScale = 2.0;
    settings.revisedOffsetMM = QPointF(-20, 40);
    fixture.update(settings);
    const auto& layout = fixture.layout.getLayout();
    QCOMPARE(layout.size(), size_t(4));

    // A page without its pair: its own size, no group, no shift
    QVERIFY(H::closeSize(layout[2].pageRectMM.size(), QSizeF(150, 300)));
    QVERIFY(H::closeSize(layout[3].pageRectMM.size(), QSizeF(300, 600)));
    QCOMPARE(layout[2].groupIndex, pdf::PDFInteger(-1));
    QCOMPARE(layout[3].groupIndex, pdf::PDFInteger(-1));

    // Only Pages with Differences: no difference, no page
    fixture.update(settings, pdf::PDFDiffReportView::Overlay, true);
    QVERIFY(fixture.layout.getLayout().empty());

    // No document, no layout
    fixture.layout.update(pdf::PDFDiffReportView::Overlay, false, fixture.diff, nullptr, fixture.pages, settings);
    QVERIFY(fixture.layout.getLayout().empty());
}

void DiffOverlayTest::hostileSequence()
{
    // A page sequence naming pages, which the shown document does not hold (a stale or hostile result), is skipped:
    // never an index out of range
    OverlayFixture fixture({ { 100, 200 } }, { { 100, 200 } }, { { 0, 0 }, { 5, 7 }, { -3, 1 }, { std::numeric_limits<pdf::PDFInteger>::max(), -1 } });
    for (pdf::PDFDiffReportView view : { pdf::PDFDiffReportView::SideBySide, pdf::PDFDiffReportView::Original, pdf::PDFDiffReportView::Revised, pdf::PDFDiffReportView::Overlay })
    {
        fixture.update({}, view);
        for (const auto& item : fixture.layout.getLayout())
        {
            QVERIFY(item.pageIndex >= 0 && item.pageIndex < 2);
        }
    }
    fixture.update({}, pdf::PDFDiffReportView::SideBySide);
    QCOMPARE(fixture.layout.getLayout().size(), size_t(2));

    // The pages of a shown document, which name no page of an input, are no pages of the layout
    fixture.pages = { pdf::PDFDiffCombinedPage(), pdf::PDFDiffCombinedPage() };
    fixture.update({}, pdf::PDFDiffReportView::SideBySide);
    QVERIFY(fixture.layout.getLayout().empty());
}

void DiffOverlayTest::differencesDrawing()
{
    // A rectangle moved on the first page and a page added: the removed rectangle is on the original page in the color
    // Removed, the added one on the revised page in the color Added, the added page has the bubble "+"
    const pdf::PDFDocument original = H::createRectangles({ QRectF(20, 20, 50, 50) });
    const pdf::PDFDocument revised = H::createRectangles({ QRectF(200, 200, 50, 50), QRectF(100, 300, 20, 20) });
    const pdf::PDFDiffResult result = H::compare(original, revised);
    QVERIFY2(result.getResult(), qPrintable(result.getResult().getErrorMessage()));
    QVERIFY(result.isChanged());

    pdf::PDFAppCompareDrawInterface drawInterface;
    drawInterface.setResult(result);
    pdf::PDFDiffReportColors colors;
    colors.removed = QColor(1, 2, 3);
    colors.added = QColor(4, 5, 6);
    colors.replaced = QColor(7, 8, 9);
    colors.moved = QColor(10, 11, 12);

    const std::vector<pdf::PDFAppCompareDrawInterface::Rectangle> removed = drawInterface.getRectangles(0, true);
    QVERIFY(!removed.empty());
    for (const auto& rectangle : removed)
    {
        QCOMPARE(pdf::PDFAppCompareDrawInterface::getColor(result, rectangle.index, colors), colors.removed);
        // In the space of the page (y up): the painter rectangle 20, 20, 50, 50 of the page 400 high
        QVERIFY(rectangle.rect.intersects(QRectF(20, 330, 50, 50)));
    }
    const std::vector<pdf::PDFAppCompareDrawInterface::Rectangle> added = drawInterface.getRectangles(0, false);
    QVERIFY(!added.empty());
    for (const auto& rectangle : added)
    {
        QCOMPARE(pdf::PDFAppCompareDrawInterface::getColor(result, rectangle.index, colors), colors.added);
    }

    bool hasAddedPage = false;
    for (size_t i = 0; i < result.getDifferencesCount(); ++i)
    {
        if (result.getType(i) == pdf::PDFDiffResult::Type::PageAdded)
        {
            hasAddedPage = true;
            QCOMPARE(result.getRightPage(i), pdf::PDFInteger(1));
            QCOMPARE(pdf::PDFAppCompareDrawInterface::getBubbleText(result, i), QStringLiteral("+"));
            QCOMPARE(pdf::PDFAppCompareDrawInterface::getColor(result, i, colors), colors.added);
        }
        else
        {
            QVERIFY(pdf::PDFAppCompareDrawInterface::getBubbleText(result, i).isEmpty());
        }
    }
    QVERIFY(hasAddedPage);
    QVERIFY(pdf::PDFAppCompareDrawInterface::getBubbleText(result, result.getDifferencesCount()).isEmpty());
    QVERIFY(!pdf::PDFAppCompareDrawInterface::getColor(result, result.getDifferencesCount(), colors).isValid());
}

void DiffOverlayTest::controlsAndReset()
{
    // The section Overlay of the tab (493:18777, 493:19325): only in the view Overlay; three page sizes (never the
    // names of colors); the scales only for Manual; the blend "50 % / 50 %"; Reset gives the original sizes, 100 %, no
    // shift and the blend 50; the values stay while another view is shown
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
    list->addFiles({ AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 2) });
    QVERIFY(AppToolOwnTabFixture::waitFor([list]() { return list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0; }));
    pdf::PDFAppToolTab* tab = fixture.createToolTab(QString::fromLatin1(pdf::PDFDocumentToolCompare::TOOL_ID), list);
    QVERIFY(tab);
    AppToolOwnTabFixture::settle();
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(shell);
    auto* gui = qobject_cast<pdf::PDFAppToolGUICompare*>(shell->getGUI());
    QVERIFY(gui);

    QComboBox* mode = gui->getPageSizesCombo();
    QSlider* blend = gui->getBlendSlider();
    QDoubleSpinBox* originalScale = gui->getScaleSpinBox(pdf::PDFAppToolGUICompare::OriginalSlot);
    QDoubleSpinBox* revisedScale = gui->getScaleSpinBox(pdf::PDFAppToolGUICompare::RevisedSlot);
    QDoubleSpinBox* horizontal = gui->getOffsetSpinBox(0);
    QDoubleSpinBox* vertical = gui->getOffsetSpinBox(1);
    QAbstractButton* reset = gui->getOverlayResetLink();
    QVERIFY(mode && blend && originalScale && revisedScale && horizontal && vertical && reset);
    QCOMPARE(mode->count(), 3);
    QVERIFY(gui->getOverlaySection()->isHidden());

    // combo-page-sizes 493:19641 (170) and the spin boxes 493:19647 ... (110); the list panel-page-sizes 493:19892 (320):
    // the rows of the Fluent menu items with the icon, the title, the description and the check of the chosen mode
    QCOMPARE(mode->width(), 170);
    QCOMPARE(originalScale->width(), 110);
    QCOMPARE(horizontal->width(), 110);
    QCOMPARE(mode->view()->minimumWidth(), 320);
    const pdf::PDFTileRowDelegate* delegate = qobject_cast<const pdf::PDFTileRowDelegate*>(mode->itemDelegate());
    QVERIFY(delegate);
    QCOMPARE(delegate->getKind(), pdf::PDFTileRowDelegate::Kind::MenuItem);
    const std::array<std::tuple<pdf::PDFAppCompareOverlaySettings::ScaleMode, QString, QString, QString>, 3> modes = {
        std::make_tuple(pdf::PDFAppCompareOverlaySettings::ScaleMode::Original, QStringLiteral("Original sizes"), QStringLiteral("Pages keep their sizes, aligned at the top"), QStringLiteral("page-same")),
        std::make_tuple(pdf::PDFAppCompareOverlaySettings::ScaleMode::Fit, QStringLiteral("Fit smaller page to larger"), QStringLiteral("The smaller page is enlarged proportionally, both centered"), QStringLiteral("fit-image")),
        std::make_tuple(pdf::PDFAppCompareOverlaySettings::ScaleMode::Manual, QStringLiteral("Manual scale"), QStringLiteral("Each document gets its own percentage"), QStringLiteral("scale"))
    };
    for (const auto& [scaleMode, title, description, iconName] : modes)
    {
        const int index = mode->findData(int(scaleMode));
        QVERIFY(index >= 0);
        QCOMPARE(mode->itemText(index), title);
        QCOMPARE(mode->itemData(index, pdf::PDFComponentRoles::SubtitleRole).toString(), description);
        QCOMPARE(mode->itemData(index, pdf::PDFComponentRoles::IconNameRole).toString(), iconName);
        const pdf::PDFComponentPainter::TileRow row = delegate->getContent(mode->model()->index(index, 0));
        QCOMPARE(row.chosen, scaleMode == pdf::PDFAppCompareOverlaySettings::ScaleMode::Original);
    }

    gui->setView(pdf::PDFDiffReportView::Overlay);
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->getOverlaySection()->isHidden());
    QCOMPARE(gui->getCompareView()->getView(), pdf::PDFDiffReportView::Overlay);
    QCOMPARE(gui->getBlendLabel()->text(), QStringLiteral("50 % / 50 %"));
    QVERIFY(!originalScale->isVisibleTo(gui->getOverlaySection()));

    // The list is wider than the combo (320 of panel-page-sizes)
    mode->showPopup();
    QVERIFY(mode->view()->isVisible());
    QVERIFY(mode->view()->window()->width() >= 320);
    mode->hidePopup();
    AppToolOwnTabFixture::settle();

    mode->setCurrentIndex(mode->findData(int(pdf::PDFAppCompareOverlaySettings::ScaleMode::Manual)));
    QCOMPARE(mode->itemData(mode->findData(int(pdf::PDFAppCompareOverlaySettings::ScaleMode::Manual)), Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QCOMPARE(mode->itemData(mode->findData(int(pdf::PDFAppCompareOverlaySettings::ScaleMode::Original)), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
    QVERIFY(originalScale->isVisibleTo(gui->getOverlaySection()));
    QVERIFY(revisedScale->isVisibleTo(gui->getOverlaySection()));
    originalScale->setValue(141.42);
    revisedScale->setValue(75.0);
    horizontal->setValue(-12.5);
    vertical->setValue(25.0);
    blend->setValue(80);
    QVERIFY(qAbs(gui->getOverlaySettings().originalScale - 1.4142) < 0.000001);
    QCOMPARE(gui->getOverlaySettings().revisedScale, 0.75);
    QCOMPARE(gui->getOverlaySettings().revisedOffsetMM, QPointF(-12.5, 25));
    QCOMPARE(gui->getOverlaySettings().blend, 80);
    QCOMPARE(gui->getBlendLabel()->text(), QStringLiteral("20 % / 80 %"));
    QCOMPARE(gui->getCompareView()->getOverlaySettings(), gui->getOverlaySettings());

    // Another view keeps the values and hides the section
    gui->setView(pdf::PDFDiffReportView::Original);
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getOverlaySection()->isHidden());
    gui->setView(pdf::PDFDiffReportView::Overlay);
    AppToolOwnTabFixture::settle();
    QCOMPARE(originalScale->value(), 141.42);

    reset->click();
    QCOMPARE(gui->getOverlaySettings().scaleMode, pdf::PDFAppCompareOverlaySettings::ScaleMode::Original);
    QCOMPARE(gui->getOverlaySettings().originalScale, 1.0);
    QCOMPARE(gui->getOverlaySettings().revisedScale, 1.0);
    QCOMPARE(gui->getOverlaySettings().revisedOffsetMM, QPointF());
    QCOMPARE(gui->getOverlaySettings().blend, 50);
    QCOMPARE(blend->value(), 50);
    QCOMPARE(mode->currentIndex(), mode->findData(int(pdf::PDFAppCompareOverlaySettings::ScaleMode::Original)));
    QVERIFY(!originalScale->isVisibleTo(gui->getOverlaySection()));

    // A new input resets the overlay (the old Diff)
    mode->setCurrentIndex(mode->findData(int(pdf::PDFAppCompareOverlaySettings::ScaleMode::Fit)));
    QCOMPARE(gui->getOverlaySettings().scaleMode, pdf::PDFAppCompareOverlaySettings::ScaleMode::Fit);
    gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("c.pdf"), 3));
    QCOMPARE(gui->getOverlaySettings().scaleMode, pdf::PDFAppCompareOverlaySettings::ScaleMode::Original);
}

QTEST_MAIN(DiffOverlayTest)

#include "tst_diffoverlaytest.moc"
