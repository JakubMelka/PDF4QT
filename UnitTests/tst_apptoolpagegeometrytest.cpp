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

// Crop, Resize and Set Page Boxes in the application (package B of step 3a of phase 2, plan 8):
// the frames 389:22382 ... 391:28535 (their texts, states and the parts of the canvas), the drag
// of the handles, the keys, PDFAnchorGrid. The expected values are the texts and the numbers of
// the frames and of the specification (A4 = 210 x 297 mm).

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolboxesoverlay.h"
#include "pdfapptoolcropoverlay.h"
#include "pdfapptoolgui_geometry.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolpageregistry.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"

#include "pdfanchorgrid.h"
#include "pdfcatalog.h"
#include "pdfdocumentbuilder.h"
#include "pdfcomponentpainter.h"
#include "pdfdocumenttool_geometry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QSignalSpy>
#include <QtTest>

using namespace pdftest;
using pdf::PDFDocumentToolShell;

/// Helpers of the test
class GeometryGuiHelper
{
public:
    GeometryGuiHelper() = delete;

    static inline const QString CROP_ID = QStringLiteral("pages.crop");
    static inline const QString RESIZE_ID = QStringLiteral("pages.resize");
    static inline const QString BOXES_ID = QStringLiteral("pages.boxes");

    static double mm(double millimeters) { return millimeters * 72.0 / 25.4; }

    /// A document of the pages of the sizes (points)
    static QString writePages(const QString& filePath, const std::vector<QSizeF>& sizes)
    {
        pdf::PDFDocumentBuilder builder;
        for (const QSizeF& size : sizes)
        {
            builder.appendPage(QRectF(QPointF(0, 0), size));
        }
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    static QString writePages(const QString& filePath, int count, const QSizeF& size)
    {
        return writePages(filePath, std::vector<QSizeF>(size_t(count), size));
    }

    /// Flyer_print.pdf of 391:26688: 2 pages, media 226 x 313 mm, trim 210 x 297 and art 180 x 267 mm in the middle
    static QString writeFlyer(const QString& filePath)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 2; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, mm(226), mm(313)));
            builder.setPageTrimBox(page, QRectF(mm(8), mm(8), mm(210), mm(297)));
            builder.setPageArtBox(page, QRectF(mm(23), mm(23), mm(180), mm(267)));
        }
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    static pdf::PDFAppDocumentTab* openReady(AppToolShellFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        return tab;
    }

    template<typename GUI>
    static GUI* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    static QStringList values(const pdf::PDFKeyValueList* list)
    {
        QStringList result;
        for (int i = 0; i < list->getRowCount(); ++i)
        {
            result << list->getValue(i);
        }
        return result;
    }

    static QStringList keys(const pdf::PDFKeyValueList* list)
    {
        QStringList result;
        for (int i = 0; i < list->getRowCount(); ++i)
        {
            result << list->getKey(i);
        }
        return result;
    }

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    static bool isNear(const QRectF& a, const QRectF& b, double tolerance = 0.02)
    {
        return std::abs(a.left() - b.left()) < tolerance && std::abs(a.top() - b.top()) < tolerance &&
               std::abs(a.width() - b.width()) < tolerance && std::abs(a.height() - b.height()) < tolerance;
    }

    static void setValue(PDFDocumentToolShell* shell, const char* name, const QVariant& value)
    {
        shell->getModel()->setValue(QString::fromLatin1(name), value);
        AppToolShellFixture::settle();
    }

    static int countLeak(QWidget* widget)
    {
        return AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(widget, 1.0));
    }

    /// The 48 pages of Handbook.pdf with the page 8 current (389:22382)
    static PDFDocumentToolShell* openHandbook(AppToolShellFixture& fixture, const QString& toolId, const QSizeF& size = QSizeF(595.2756, 841.8898))
    {
        pdf::PDFAppDocumentTab* tab = openReady(fixture, writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48, size));
        if (!tab)
        {
            return nullptr;
        }
        tab->getView()->goToPage(7);
        AppToolShellFixture::settle();
        PDFDocumentToolShell* shell = fixture.openTool(tab, toolId);
        AppToolShellFixture::settle();
        return shell;
    }
};

class AppToolPageGeometryTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void registered();
    void anchorGridComponent();
    void cropFrame();
    void cropDragAndKeys();
    void cropThumbnailChangesPreview();
    void cropInvalidCustomBox();
    void cropRemoveCrop();
    void cropMixedSizes();
    void cropRunKeep();
    void resizeFrame();
    void resizeAnchorMenu();
    void resizeCustomKeepSize();
    void resizeInvalidMargins();
    void boxesFrame();
    void boxesWriteToMultiSelect();
    void boxesNothingSelected();
    void boxesOutsideMedia();
    void themesAndDirection();
    void statusBarFollowsPreviewPage();
    void boxMenusHaveSwatches();
};

void AppToolPageGeometryTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolPageGeometryTest::registered()
{
    // The context registers the three tools with their GUI and their pages
    AppToolShellFixture fixture;
    for (const QString& toolId : { GeometryGuiHelper::CROP_ID, GeometryGuiHelper::RESIZE_ID, GeometryGuiHelper::BOXES_ID })
    {
        QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(toolId));
        QVERIFY(pdf::PDFAppToolPageRegistry::hasPage(pdf::PDFAppToolPageKind::Document, toolId));
    }
}

void AppToolPageGeometryTest::anchorGridComponent()
{
    // K14, 391:23821: 3 x 3 cells of 40 (36 compact), a click chooses (one signal), the arrows move
    // the focus cell without a choice, Space chooses it, setAnchor is silent, the picture is not
    // mirrored for right-to-left
    QWidget window;
    window.setLayoutDirection(Qt::RightToLeft);
    pdf::PDFAnchorGrid* grid = new pdf::PDFAnchorGrid(&window);
    grid->resize(grid->sizeHint());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(grid->sizeHint(), QSize(3 * 40 + 2 * 4, 3 * 40 + 2 * 4));
    QCOMPARE(grid->getCellRect(0), QRect(0, 0, 40, 40));
    QCOMPARE(grid->getCellRect(5), QRect(88, 44, 40, 40));

    QSignalSpy spy(grid, &pdf::PDFAnchorGrid::anchorChosen);
    grid->setAnchor(Qt::AlignBottom | Qt::AlignRight);
    QCOMPARE(spy.count(), 0);

    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, grid->getCellRect(0).center());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).value<Qt::Alignment>(), Qt::Alignment(Qt::AlignLeft | Qt::AlignTop));
    QCOMPARE(grid->getAnchor(), Qt::Alignment(Qt::AlignLeft | Qt::AlignTop));

    // The arrows: Right moves to the right cell of the picture also in right-to-left
    grid->setFocus();
    QTest::keyClick(grid, Qt::Key_Right);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(grid->getFocusIndex(), 1);
    QTest::keyClick(grid, Qt::Key_Down);
    QTest::keyClick(grid, Qt::Key_Space);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(grid->getAnchor(), Qt::Alignment(Qt::AlignCenter));

    // A slot may delete the grid (emit last)
    QPointer<pdf::PDFAnchorGrid> guard(grid);
    QObject::connect(grid, &pdf::PDFAnchorGrid::anchorChosen, grid, &QObject::deleteLater);
    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, grid->getCellRect(8).center());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!guard);

    pdf::PDFAnchorGrid compact;
    compact.setCellSize(pdf::PDFAnchorGrid::COMPACT_CELL_SIZE);
    QCOMPARE(compact.sizeHint(), QSize(3 * 36 + 2 * 4, 3 * 36 + 2 * 4));
}

void AppToolPageGeometryTest::cropFrame()
{
    // 389:22382: 48 A4 pages, page 8 current; Equal margins 10 mm
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::CROP_ID);
    pdf::PDFAppCropToolGUI* gui = H::gui<pdf::PDFAppCropToolGUI>(shell);
    QVERIFY(gui);

    // K48: the page of the preview is the current page
    QCOMPARE(gui->getPreviewPage(), 7);
    QCOMPARE(gui->getPageSelection()->getStatusText(), QStringLiteral("48 pages selected · 1 – 48"));

    // The toolbar: Crop: Equal margins, Remove Crop, Crop 48 Pages
    QCOMPARE(gui->getModeButton()->text(), QStringLiteral("Crop: Equal margins"));
    QCOMPARE(gui->getRemoveButton()->text(), QStringLiteral("Remove Crop"));
    QVERIFY(!gui->getRemoveButton()->isChecked());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Crop 48 Pages"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // The canvas in the place of the canvas (306, 90, 814 x 842): Page 8 · crop preview, the box,
    // the caption
    pdf::PDFAppToolPageCanvas* canvas = gui->getCanvas();
    QVERIFY(canvas && canvas->isVisible());
    QCOMPARE(H::inWindow(canvas), QRect(306, 90, 814, 842));
    QCOMPARE(canvas->getTitleLabel()->text(), QStringLiteral("Page 8 · crop preview"));
    QCOMPARE(canvas->getCaptionLabel(0)->text(), QStringLiteral("Result 190 × 277 mm · 10 mm removed on every side"));
    QCOMPARE(canvas->getStage(0)->getPageIndex(), 7);
    QVERIFY(H::isNear(gui->getOverlay()->getRect(), QRectF(H::mm(10), H::mm(10), H::mm(190), H::mm(277))));
    QVERIFY(!gui->getOverlay()->isInactive());

    // The page is rendered (the worker of the stage)
    QVERIFY(AppToolShellFixture::waitFor([canvas]() { return canvas->getStage(0)->hasPicture(); }));

    // The right panel: Margins with its note, Result, the amber callout
    QVERIFY(gui->getMarginFields()->isVisible());
    QVERIFY(!gui->getBoxFields()->isVisible());
    QCOMPARE(gui->getMarginFields()->getNoteLabel()->text(), QStringLiteral("The same margin is cut from all four sides."));
    QCOMPARE(gui->getMarginFields()->getSpinBox(QStringLiteral("margin"))->value(), 10.0);
    QCOMPARE(H::keys(gui->getResult()), QStringList({ QStringLiteral("Page 8 now"), QStringLiteral("After crop"), QStringLiteral("Pages") }));
    QCOMPARE(H::values(gui->getResult()), QStringList({ QStringLiteral("210 × 297 mm"), QStringLiteral("190 × 277 mm"), QStringLiteral("48") }));
    QCOMPARE(gui->getResult()->getRowRect(1).top() - gui->getResult()->getRowRect(0).top(), 36);
    QVERIFY(!shell->getToolbar()->getAdvancedTag());
    QVERIFY(gui->getHiddenCallout()->isVisible());
    QVERIFY(!gui->getErrorCallout()->isVisible());
    QVERIFY(!gui->getSizesCallout()->isVisible());
    QVERIFY(!gui->getRemoveCallout()->isVisible());

    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Crop Pages · 10 mm off every side · 210 × 297 mm → 190 × 277 mm · 48 pages"));
    fixture.screenshot(QStringLiteral("pages-crop"));
}

void AppToolPageGeometryTest::cropDragAndKeys()
{
    // 389:23500: a drag of a handle makes the box a custom box (one undo step); the arrows move
    // the box by a unit, Shift and the arrows change its size
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::CROP_ID);
    pdf::PDFAppCropToolGUI* gui = H::gui<pdf::PDFAppCropToolGUI>(shell);
    QVERIFY(gui);
    pdf::PDFAppCropOverlay* overlay = gui->getOverlay();
    const double scale = gui->getCanvas()->getStage(0)->getScale();
    QVERIFY(scale > 0.0);

    // The right handle 50 pixels to the left
    const QPoint start = overlay->getHandleRect(pdf::PDFAppCropOverlay::Part::Right).center().toPoint();
    QSignalSpy changed(overlay, &pdf::PDFAppCropOverlay::rectChanged);
    QSignalSpy chosen(overlay, &pdf::PDFAppCropOverlay::rectChosen);
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(overlay, start - QPoint(25, 0));
    QTest::mouseMove(overlay, start - QPoint(50, 0));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, start - QPoint(50, 0));
    AppToolShellFixture::settle();
    QVERIFY(changed.count() >= 1);
    QCOMPARE(chosen.count(), 1);

    const QVariantMap values = shell->getModel()->getValues();
    QCOMPARE(values.value(QStringLiteral("mode")).toString(), QStringLiteral("custom-box"));
    QCOMPARE(values.value(QStringLiteral("left")).toDouble(), 10.0);
    QCOMPARE(values.value(QStringLiteral("top")).toDouble(), 10.0);
    QVERIFY(std::abs(values.value(QStringLiteral("width")).toDouble() - (190.0 - 50.0 / scale * 25.4 / 72.0)) < 0.02);
    QCOMPARE(gui->getModeButton()->text(), QStringLiteral("Crop: Custom box"));
    QVERIFY(gui->getBoxFields()->isVisible());

    // One undo step returns the equal margins
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValues().value(QStringLiteral("mode")).toString(), QStringLiteral("equal-margins"));

    // The keys: Right moves the box by one unit (1/72 inch), Shift + Down makes it higher
    overlay = gui->getOverlay();
    overlay->setFocus();
    QTest::keyClick(overlay, Qt::Key_Right);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValues().value(QStringLiteral("mode")).toString(), QStringLiteral("custom-box"));
    QVERIFY(std::abs(shell->getModel()->getValues().value(QStringLiteral("left")).toDouble() - (10.0 + 25.4 / 72.0)) < 0.011);
    QTest::keyClick(gui->getOverlay(), Qt::Key_Up, Qt::ShiftModifier);
    AppToolShellFixture::settle();
    QVERIFY(std::abs(shell->getModel()->getValues().value(QStringLiteral("height")).toDouble() - (277.0 - 25.4 / 72.0)) < 0.011);
}

void AppToolPageGeometryTest::cropThumbnailChangesPreview()
{
    // K48: a click on a thumbnail of the left panel shows that page in the preview
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::CROP_ID);
    pdf::PDFAppCropToolGUI* gui = H::gui<pdf::PDFAppCropToolGUI>(shell);
    QVERIFY(gui);
    pdf::PDFAppThumbnailListView* view = gui->getThumbnailView();
    QVERIFY(view && view->isVisible());
    QCOMPARE(view->currentIndex().row(), 7);

    const QModelIndex index = view->model()->index(2, 0);
    view->scrollTo(index);
    AppToolShellFixture::settle();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(index).center());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPreviewPage(), 2);
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text(), QStringLiteral("Page 3 · crop preview"));
    QCOMPARE(gui->getCanvas()->getStage(0)->getPageIndex(), 2);
    QCOMPARE(H::keys(gui->getResult()).front(), QStringLiteral("Page 3 now"));

    // The custom box is a box of the page of the preview: the run gets its number
    QCOMPARE(gui->getRunParameters().value(QStringLiteral("referencePage")).toLongLong(), 3);
}

void AppToolPageGeometryTest::cropInvalidCustomBox()
{
    // 398:28158: Left 20 + width 250 on the 210 mm wide page
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::CROP_ID);
    pdf::PDFAppCropToolGUI* gui = H::gui<pdf::PDFAppCropToolGUI>(shell);
    QVERIFY(gui);
    shell->getModel()->setValues({ { QStringLiteral("mode"), QStringLiteral("custom-box") }, { QStringLiteral("left"), 20.0 }, { QStringLiteral("top"), 25.0 },
                                   { QStringLiteral("width"), 250.0 }, { QStringLiteral("height"), 240.0 } });
    AppToolShellFixture::settle();

    QVERIFY(gui->getErrorCallout()->isVisible());
    QCOMPARE(gui->getErrorCallout()->getTitleLabel()->text(), QStringLiteral("The crop box is wider than the page"));
    QVERIFY(gui->getValuesError().contains(QStringLiteral("Left 20 mm + width 250 mm ends 60 mm outside the 210 mm wide page. The width can be 190 mm at most.")));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::values(gui->getResult()).at(1), QStringLiteral("–"));

    // K20: the box of the preview is limited to the page (190 mm wide)
    QCOMPARE(gui->getCanvas()->getCaptionLabel(0)->text(), QStringLiteral("The box in the preview is limited to the page"));
    QVERIFY(std::abs(gui->getOverlay()->getRect().width() - H::mm(190)) < 0.05);
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Crop Pages · the crop box reaches outside the page"));
    fixture.screenshot(QStringLiteral("pages-crop-invalid"));
}

void AppToolPageGeometryTest::cropRemoveCrop()
{
    // 405:32319: the toggle Remove Crop; the menu shows the mode, disabled; the margins disabled (K54)
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::CROP_ID);
    pdf::PDFAppCropToolGUI* gui = H::gui<pdf::PDFAppCropToolGUI>(shell);
    QVERIFY(gui);
    gui->getRemoveButton()->click();
    AppToolShellFixture::settle();

    QCOMPARE(shell->getModel()->getValues().value(QStringLiteral("mode")).toString(), QStringLiteral("remove"));
    QVERIFY(gui->getRemoveButton()->isChecked());
    QCOMPARE(gui->getModeButton()->text(), QStringLiteral("Crop: Equal margins"));
    QVERIFY(!gui->getModeButton()->isEnabled());
    QVERIFY(gui->getMarginFields()->isVisible());
    QVERIFY(!gui->getMarginFields()->isEnabled());
    QVERIFY(gui->getRemoveCallout()->isVisible());
    QVERIFY(!gui->getHiddenCallout()->isVisible());
    QCOMPARE(gui->getRemoveCallout()->getTitleLabel()->text(), QStringLiteral("Hidden content becomes visible again"));
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Remove Crop from 48 Pages"));
    QVERIFY(gui->getOverlay()->isInactive());
    QCOMPARE(gui->getCanvas()->getStage(0)->getShownBox(), pdf::PDFPageGeometry::Box::MediaBox);
    QCOMPARE(gui->getCanvas()->getCaptionLabel(0)->text(), QStringLiteral("The whole page is shown again · the dashed line is the crop that will be removed"));

    // Off: the mode of the menu again
    gui->getRemoveButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(shell->getModel()->getValues().value(QStringLiteral("mode")).toString(), QStringLiteral("equal-margins"));
}

void AppToolPageGeometryTest::cropMixedSizes()
{
    // 389:24078: 46 pages A4, 2 pages A3
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    std::vector<QSizeF> sizes(46, QSizeF(595.2756, 841.8898));
    sizes.insert(sizes.begin() + 20, 2, QSizeF(841.8898, 1190.5512));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Mixed.pdf")), sizes));
    QVERIFY(tab);
    tab->getView()->goToPage(7);
    AppToolShellFixture::settle();
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::CROP_ID);
    pdf::PDFAppCropToolGUI* gui = H::gui<pdf::PDFAppCropToolGUI>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    QVERIFY(gui->getSizesCallout()->isVisible());
    QCOMPARE(gui->getSizesCallout()->getTitleLabel()->text(), QStringLiteral("The pages have 2 sizes"));
    QVERIFY(gui->getSizesCallout()->getTextLabel()->text().startsWith(QStringLiteral("46 pages are A4, 2 pages are A3. The margin is cut from every page; the values below are shown for page 8.")));
    QCOMPARE(H::values(gui->getResult()).at(2), QStringLiteral("48 · 2 sizes"));
}

void AppToolPageGeometryTest::cropRunKeep()
{
    // 389:24633: the result dialog "Pages cropped"; Keep in this tab crops the document
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Small.pdf")), 3, QSizeF(595.2756, 841.8898)));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::CROP_ID);
    pdf::PDFAppCropToolGUI* gui = H::gui<pdf::PDFAppCropToolGUI>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();
    pdf::PDFAppToolResultInput input;
    gui->fillResultInput(&input);
    QCOMPARE(input.title, QStringLiteral("Pages cropped"));

    shell->confirm();
    QVERIFY(AppToolShellFixture::waitForState(shell, PDFDocumentToolShell::State::Deciding));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::Keep);

    // 389:24633: nothing of the toolbar below the result dialog
    QVERIFY(!gui->getRemoveButton()->isEnabled());
    dialog->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->getDocument()->getCatalog()->getPage(0)->getCropBox().width() < 595.0; }));
    QVERIFY(H::isNear(tab->getDocument()->getDocument()->getCatalog()->getPage(2)->getCropBox(), QRectF(H::mm(10), H::mm(10), H::mm(190), H::mm(277))));
}

void AppToolPageGeometryTest::resizeFrame()
{
    // 391:23146: 48 Letter pages to A4 with 15 mm margins, Now and After
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::RESIZE_ID, QSizeF(612, 792));
    pdf::PDFAppResizeToolGUI* gui = H::gui<pdf::PDFAppResizeToolGUI>(shell);
    QVERIFY(gui);
    shell->getModel()->setValues({ { QStringLiteral("pageSize"), QStringLiteral("a4") }, { QStringLiteral("marginLeft"), 15.0 }, { QStringLiteral("marginTop"), 15.0 },
                                   { QStringLiteral("marginRight"), 15.0 }, { QStringLiteral("marginBottom"), 15.0 } });
    AppToolShellFixture::settle();

    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Resize 48 Pages"));
    pdf::PDFAppToolPageCanvas* canvas = gui->getCanvas();
    QCOMPARE(canvas->getStageCount(), 2);
    QCOMPARE(canvas->getTitleLabel()->text(), QStringLiteral("Page 8 · before and after"));
    QCOMPARE(canvas->getStageTitleLabel(0)->text(), QStringLiteral("Now"));
    QCOMPARE(canvas->getStageTitleLabel(1)->text(), QStringLiteral("After"));
    QCOMPARE(canvas->getCaptionLabel(0)->text(), QStringLiteral("Letter · 216 × 279 mm"));
    QCOMPARE(canvas->getCaptionLabel(1)->text(), QStringLiteral("A4 · 210 × 297 mm · content scaled to 83 %"));

    // After: the sheet A4 with the dashed area inside of the margins
    QVERIFY(H::isNear(QRectF(QPointF(0, 0), canvas->getStage(1)->getSheetSize()), QRectF(0, 0, 595.2756, 841.8898), 0.05));
    const std::vector<pdf::PDFAppPageBoxesOverlay::Outline>& outlines = gui->getMarginsOverlay()->getOutlines();
    QCOMPARE(outlines.size(), size_t(1));
    QVERIFY(outlines.front().isDashed);
    QVERIFY(H::isNear(outlines.front().rect, QRectF(H::mm(15), H::mm(15), H::mm(180), H::mm(267)), 0.05));

    // Summary; the status bar
    QCOMPARE(H::values(gui->getSummary()), QStringList({ QStringLiteral("48"), QStringLiteral("Letter → A4"), QStringLiteral("Scaled to 83 %"), QStringLiteral("Auto") }));
    QCOMPARE(gui->getSummary()->getRowRect(1).top() - gui->getSummary()->getRowRect(0).top(), 36);

    // 391:23146 card Margins (1137, 129, 287 wide): the label Left at 1150, the field 1271-1411,
    // its top 12 below the top of the card - 13 from the left and right edges, 12 from the top
    pdf::PDFAppToolMarginFields* margins = gui->getMarginFields();
    QWidget* leftLabel = margins->findChild<QWidget*>(QStringLiteral("toolFieldLabel-marginLeft"));
    QWidget* leftField = margins->getEditor(QStringLiteral("marginLeft"));
    QVERIFY(leftLabel && leftField);
    QCOMPARE(leftLabel->mapTo(margins, QPoint(0, 0)).x(), 13);
    QCOMPARE(margins->width() - (leftField->mapTo(margins, QPoint(0, 0)).x() + leftField->width()), 13);
    QCOMPARE(leftField->mapTo(margins, QPoint(0, 0)).y(), 12);

    // 391:23146: Now and After at one scale - Letter (216 mm) is drawn wider than A4 (210 mm),
    // A4 (297 mm) higher than Letter (279 mm)
    QVERIFY(std::abs(canvas->getStage(0)->getScale() / canvas->getStage(1)->getScale() - 1.0) < 0.01);
    const QRectF nowRect = canvas->getStage(0)->getPageRect();
    const QRectF afterRect = canvas->getStage(1)->getSheetRect();
    QVERIFY2(std::abs(nowRect.width() / afterRect.width() - 612.0 / 595.2756) < 0.01, qPrintable(QStringLiteral("%1 %2").arg(nowRect.width()).arg(afterRect.width())));
    QVERIFY(std::abs(afterRect.height() / nowRect.height() - 841.8898 / 792.0) < 0.01);
    QVERIFY(!gui->getErrorCallout()->isVisible());
    QVERIFY(!gui->getSizeFields()->isVisible());
    QVERIFY(!gui->getKeepSizeNote()->isVisible());
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Resize Pages · Letter → A4 · content scaled to fit inside 15 mm margins · 48 pages"));
    fixture.screenshot(QStringLiteral("pages-resize"));
}

void AppToolPageGeometryTest::resizeAnchorMenu()
{
    // 391:23821: the panel of the anchor - a click on a cell closes it and writes the anchor
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::RESIZE_ID, QSizeF(612, 792));
    pdf::PDFAppResizeToolGUI* gui = H::gui<pdf::PDFAppResizeToolGUI>(shell);
    QVERIFY(gui);
    pdf::PDFAnchorGrid* grid = gui->getAnchorGrid();
    QVERIFY(grid);
    pdf::PDFAppToolAnchorPanel* panel = nullptr;
    for (QObject* object = grid; object && !panel; object = object->parent())
    {
        panel = qobject_cast<pdf::PDFAppToolAnchorPanel*>(object);
    }
    QVERIFY(panel);
    QCOMPARE(panel->getButton()->text(), QStringLiteral("Anchor: Middle center"));
    QCOMPARE(grid->getAnchor(), Qt::Alignment(Qt::AlignCenter));

    panel->getButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(panel->isVisible());
    QCOMPARE(panel->getNameLabel()->text(), QStringLiteral("Middle center"));
    QTest::mouseClick(grid, Qt::LeftButton, Qt::NoModifier, grid->getCellRect(0).center());
    AppToolShellFixture::settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(shell->getModel()->getValues().value(QStringLiteral("anchor")).toString(), QStringLiteral("top-left"));
    QCOMPARE(panel->getButton()->text(), QStringLiteral("Anchor: Top left"));

    // Undo returns the anchor, the grid follows
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(grid->getAnchor(), Qt::Alignment(Qt::AlignCenter));
}

void AppToolPageGeometryTest::resizeCustomKeepSize()
{
    // 391:24594: Custom 200 x 280 mm, Keep size - Orientation and the options of the scaling disabled, the note
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::RESIZE_ID, QSizeF(612, 792));
    pdf::PDFAppResizeToolGUI* gui = H::gui<pdf::PDFAppResizeToolGUI>(shell);
    QVERIFY(gui);
    shell->getModel()->setValues({ { QStringLiteral("pageSize"), QStringLiteral("custom") }, { QStringLiteral("width"), 200.0 }, { QStringLiteral("height"), 280.0 },
                                   { QStringLiteral("content"), QStringLiteral("keep-size") } });
    AppToolShellFixture::settle();

    QVERIFY(gui->getSizeFields()->isVisible());
    QCOMPARE(gui->getSizeFields()->getSpinBox(QStringLiteral("width"))->value(), 200.0);
    QVERIFY(gui->getKeepSizeNote()->isVisible());
    QVERIFY(!gui->getContentFields()->getCheckBox(QStringLiteral("preserveAspectRatio"))->isEnabled());
    QVERIFY(!gui->getContentFields()->getCheckBox(QStringLiteral("scaleAnnotations"))->isEnabled());
    QVERIFY(gui->getSummary()->isVisible());
    QCOMPARE(gui->getCanvas()->getCaptionLabel(1)->text(), QStringLiteral("Custom · 200 × 280 mm · content not scaled"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Resize Pages · Letter → 200 × 280 mm · content keeps its size · 48 pages"));
}

void AppToolPageGeometryTest::resizeInvalidMargins()
{
    // 391:25272: 120 + 120 mm of margins on A4 - the red callout, the red margins, no Primary button
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::openHandbook(fixture, H::RESIZE_ID, QSizeF(612, 792));
    pdf::PDFAppResizeToolGUI* gui = H::gui<pdf::PDFAppResizeToolGUI>(shell);
    QVERIFY(gui);
    shell->getModel()->setValues({ { QStringLiteral("pageSize"), QStringLiteral("a4") }, { QStringLiteral("marginLeft"), 120.0 }, { QStringLiteral("marginRight"), 120.0 } });
    AppToolShellFixture::settle();

    QVERIFY(gui->getErrorCallout()->isVisible());
    QCOMPARE(gui->getErrorCallout()->getTitleLabel()->text(), QStringLiteral("Margins exceed the page size"));
    QVERIFY(gui->getValuesError().contains(QStringLiteral("The left and right margins together (240 mm) are wider than the A4 page (210 mm). Reduce the margins or choose a larger size.")));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getCanvas()->getCaptionLabel(1)->text(), QStringLiteral("A4 · the margins leave no room for the content"));
    QCOMPARE(gui->getMarginsOverlay()->getOutlines().front().color, pdf::PDFThemeColors::AccentRedIcon);
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Resize Pages · the margins are wider than the page"));
}

void AppToolPageGeometryTest::boxesFrame()
{
    // 391:26688: Flyer_print.pdf, bleed = trim + 3 mm
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeFlyer(fixture.path(QStringLiteral("Flyer_print.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::BOXES_ID);
    pdf::PDFAppBoxesToolGUI* gui = H::gui<pdf::PDFAppBoxesToolGUI>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    QCOMPARE(gui->getWriteToButton()->text(), QStringLiteral("Write to: Bleed"));
    QVERIFY(gui->getShowBoxesButton()->isChecked());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Set Boxes on 2 Pages"));
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text(), QStringLiteral("Page 1 · page boxes"));
    QCOMPARE(gui->getCanvas()->getStage(0)->getShownBox(), pdf::PDFPageGeometry::Box::MediaBox);
    QCOMPARE(gui->getCanvas()->getCaptionLabel(0)->text(), QStringLiteral("The thick red line is the new bleed box"));

    // Boxes of page 1: the bleed box with its new size, the others as they are
    QCOMPARE(H::values(gui->getBoxesList()), QStringList({ QStringLiteral("226 × 313 mm"), QStringLiteral("226 × 313 mm"), QStringLiteral("→ 216 × 303 mm"),
                                                           QStringLiteral("210 × 297 mm"), QStringLiteral("180 × 267 mm") }));
    QCOMPARE(gui->getMarginFields()->getNoteLabel()->text(), QStringLiteral("Positive values make the box larger than the trim box, negative values smaller."));

    // The outlines: media solid, crop and art dashed, the new bleed thick
    const std::vector<pdf::PDFAppPageBoxesOverlay::Outline>& outlines = gui->getOverlay()->getOutlines();
    QCOMPARE(outlines.size(), size_t(5));
    const pdf::PDFAppPageBoxesOverlay::Outline& bleed = outlines[2];
    QVERIFY(bleed.isThick);
    QCOMPARE(bleed.color, pdf::PDFThemeColors::AccentRedIcon);
    QVERIFY(H::isNear(bleed.rect, QRectF(H::mm(5), H::mm(5), H::mm(216), H::mm(303)), 0.05));
    QVERIFY(outlines[1].isDashed && outlines[4].isDashed && !outlines[0].isDashed);

    // The legend: the written box bold
    const std::vector<pdf::PDFAppPageBoxesLegend::Item>& legend = gui->getLegend()->getItems();
    QCOMPARE(legend.size(), size_t(5));
    QCOMPARE(legend[2].title, QStringLiteral("Bleed box"));
    QVERIFY(legend[2].isBold && !legend[3].isBold);

    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Set Page Boxes · bleed box = trim box + 3 mm · 2 pages · the content is not changed"));

    // 391:26688: the Tag Advanced next to the title of the toolbar; the rows of Boxes of page 1
    // 36 apart
    pdf::PDFTag* advanced = shell->getToolbar()->getAdvancedTag();
    QVERIFY(advanced);
    QCOMPARE(advanced->text(), QStringLiteral("Advanced"));
    QVERIFY(advanced->isVisible());
    QVERIFY(advanced->mapTo(fixture.window, QPoint(0, 0)).x() > shell->getToolbar()->getTitleLabel()->mapTo(fixture.window, QPoint(shell->getToolbar()->getTitleLabel()->width(), 0)).x());
    QCOMPARE(gui->getBoxesList()->getRowRect(1).top() - gui->getBoxesList()->getRowRect(0).top(), 36);
    fixture.screenshot(QStringLiteral("pages-boxes"));

    // The toggle hides the boxes of the preview
    gui->getShowBoxesButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(gui->getOverlay()->getOutlines().empty());
}

void AppToolPageGeometryTest::boxesWriteToMultiSelect()
{
    // K19: Write to is a multi-select list, the rows stay open
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeFlyer(fixture.path(QStringLiteral("Flyer_print.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::BOXES_ID);
    pdf::PDFAppBoxesToolGUI* gui = H::gui<pdf::PDFAppBoxesToolGUI>(shell);
    QVERIFY(gui);
    AppToolShellFixture::settle();

    gui->getWriteToButton()->click();
    AppToolShellFixture::settle();
    pdf::PDFTileRow* trim = gui->getWriteToRow(pdf::PDFPageGeometry::Box::TrimBox);
    QVERIFY(trim && trim->isVisible());
    QTest::mouseClick(trim, Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(trim->isVisible());
    QVERIFY(trim->isChecked());
    QCOMPARE(shell->getModel()->getValues().value(QStringLiteral("writeTo")).toString(), QStringLiteral("bleed,trim"));
    QCOMPARE(gui->getWriteToButton()->text(), QStringLiteral("Write to: Bleed, Trim"));
    QCOMPARE(gui->getCanvas()->getCaptionLabel(0)->text(), QStringLiteral("The thick lines are the new boxes"));
}

void AppToolPageGeometryTest::boxesNothingSelected()
{
    // 391:28085: Write to: nothing - the red callout, no Primary button, the bleed box "not set"
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeFlyer(fixture.path(QStringLiteral("Flyer_print.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::BOXES_ID);
    pdf::PDFAppBoxesToolGUI* gui = H::gui<pdf::PDFAppBoxesToolGUI>(shell);
    QVERIFY(gui);
    gui->toggleWriteTo(pdf::PDFPageGeometry::Box::BleedBox);
    AppToolShellFixture::settle();

    QCOMPARE(gui->getWriteToButton()->text(), QStringLiteral("Write to: nothing"));
    QVERIFY(gui->getErrorCallout()->isVisible());
    QCOMPARE(gui->getErrorCallout()->getTitleLabel()->text(), QStringLiteral("No box is selected"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::values(gui->getBoxesList()).at(2), QStringLiteral("not set"));
    QCOMPARE(gui->getCanvas()->getCaptionLabel(0)->text(), QStringLiteral("No box will be written – choose one in Write to"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Set Page Boxes · choose at least one box to write"));
}

void AppToolPageGeometryTest::boxesOutsideMedia()
{
    // 391:27638: + 10 mm - the amber warning with the largest margin (8 mm); the Primary button stays
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeFlyer(fixture.path(QStringLiteral("Flyer_print.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::BOXES_ID);
    pdf::PDFAppBoxesToolGUI* gui = H::gui<pdf::PDFAppBoxesToolGUI>(shell);
    QVERIFY(gui);
    shell->getModel()->setValues({ { QStringLiteral("marginLeft"), 10.0 }, { QStringLiteral("marginTop"), 10.0 }, { QStringLiteral("marginRight"), 10.0 }, { QStringLiteral("marginBottom"), 10.0 } });
    AppToolShellFixture::settle();

    QVERIFY(gui->getWarningCallout()->isVisible());
    QCOMPARE(gui->getWarningCallout()->getTitleLabel()->text(), QStringLiteral("The bleed box is larger than the media box"));
    QCOMPARE(gui->getWarningCallout()->getTextLabel()->text(), QStringLiteral("No box may reach outside the media box – viewers and printers clip it. Add Media box to Write to, or reduce the margins to 8 mm or less."));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::values(gui->getBoxesList()).at(2), QStringLiteral("→ 230 × 317 mm"));
    QCOMPARE(gui->getCanvas()->getCaptionLabel(0)->text(), QStringLiteral("The new bleed box reaches outside the media box"));
}

void AppToolPageGeometryTest::themesAndDirection()
{
    // No color of the system in the parts, both themes, right-to-left: the page and its box are
    // never mirrored (plan 8)
    using H = GeometryGuiHelper;
    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            for (const QString& toolId : { H::CROP_ID, H::RESIZE_ID, H::BOXES_ID })
            {
                // The fixture sets the light scheme of its context: the theme of the row comes after it
                AppToolShellFixture fixture(QSize(1440, 960));
                pdf::PDFThemeManager::getInstance()->setTheme(theme);
                AppToolShellFixture::settle();
                QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), theme == QLatin1String("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
                PDFDocumentToolShell* shell = H::openHandbook(fixture, toolId);
                QVERIFY(shell);
                QCOMPARE(H::countLeak(fixture.window), 0);
                const QString suffix = QStringLiteral("-%1%2").arg(theme, direction == Qt::RightToLeft ? QStringLiteral("-rtl") : QString());
                fixture.screenshot(toolId.mid(6) + suffix);
                if (pdf::PDFAppCropToolGUI* crop = H::gui<pdf::PDFAppCropToolGUI>(shell))
                {
                    // The left margin of the box is at the left of the page also in right-to-left
                    const QRectF page = crop->getCanvas()->getStage(0)->getPageRect();
                    const QRectF box = crop->getOverlay()->getBoxRect();
                    QVERIFY(std::abs((box.left() - page.left()) - (page.right() - box.right())) < 1.0);
                    QVERIFY(H::isNear(crop->getOverlay()->getRect(), QRectF(H::mm(10), H::mm(10), H::mm(190), H::mm(277))));
                }
            }
        }
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
    pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light"));
}

void AppToolPageGeometryTest::statusBarFollowsPreviewPage()
{
    // K48 / 389:22382: the page item of the status bar ("Page 8 / 48") says the page of the
    // preview; a click on another thumbnail changes it; leaving the tool restores the page of
    // the entry
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writePages(fixture.path(QStringLiteral("Handbook.pdf")), 48, QSizeF(595.2756, 841.8898)));
    QVERIFY(tab);
    tab->getView()->goToPage(7);
    AppToolShellFixture::settle();
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::CROP_ID);
    AppToolShellFixture::settle();
    pdf::PDFAppCropToolGUI* gui = H::gui<pdf::PDFAppCropToolGUI>(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getPreviewPage(), pdf::PDFInteger(7));
    QCOMPARE(tab->getView()->getStatusBar()->getPageText(), QStringLiteral("Page 8 / 48"));

    gui->setPreviewPage(19);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getCanvas()->getTitleLabel()->text().left(7), QStringLiteral("Page 20"));
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getView()->getStatusBar()->getPageText() == QStringLiteral("Page 20 / 48"); }));

    // A click on the thumbnail of page 3
    pdf::PDFAppThumbnailListView* view = gui->getThumbnailView();
    const QModelIndex index = view->model()->index(2, 0);
    view->scrollTo(index);
    AppToolShellFixture::settle();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(index).center());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getPreviewPage(), pdf::PDFInteger(2));
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getView()->getStatusBar()->getPageText() == QStringLiteral("Page 3 / 48"); }));

    shell->cancel();
    AppToolShellFixture::settle();
    QCOMPARE(tab->getView()->getCurrentPageIndex(), pdf::PDFInteger(7));
}

void AppToolPageGeometryTest::boxMenusHaveSwatches()
{
    // 391:27131: every row of the menus Based on and Write to has the swatch of the color of its
    // box in its tile - the colors of the outlines and of the legend (media slate, crop blue,
    // bleed red, trim green, art violet)
    using H = GeometryGuiHelper;
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = H::openReady(fixture, H::writeFlyer(fixture.path(QStringLiteral("Flyer_print.pdf"))));
    QVERIFY(tab);
    PDFDocumentToolShell* shell = fixture.openTool(tab, H::BOXES_ID);
    pdf::PDFAppBoxesToolGUI* gui = H::gui<pdf::PDFAppBoxesToolGUI>(shell);
    QVERIFY(gui);

    const std::pair<pdf::PDFPageGeometry::Box, pdf::PDFThemeColors::Color> colors[] = {
        { pdf::PDFPageGeometry::Box::MediaBox, pdf::PDFThemeColors::AccentNeutralIcon },
        { pdf::PDFPageGeometry::Box::CropBox, pdf::PDFThemeColors::AccentBlueIcon },
        { pdf::PDFPageGeometry::Box::BleedBox, pdf::PDFThemeColors::AccentRedIcon },
        { pdf::PDFPageGeometry::Box::TrimBox, pdf::PDFThemeColors::AccentGreenIcon },
        { pdf::PDFPageGeometry::Box::ArtBox, pdf::PDFThemeColors::AccentVioletIcon },
    };
    pdf::PDFAppToolPanelPopup* basedOnPanel = fixture.window->findChild<pdf::PDFAppToolPanelPopup*>(QStringLiteral("toolPanel-basedOn"));
    QVERIFY(basedOnPanel);
    pdf::PDFAppToolPanelPopup* writeToPanel = fixture.window->findChild<pdf::PDFAppToolPanelPopup*>(QStringLiteral("toolPanel-writeTo"));
    QVERIFY(writeToPanel);
    for (pdf::PDFAppToolPanelPopup* panel : { basedOnPanel, writeToPanel })
    {
        // The menu as the user sees it: open (its rows have their size)
        QVERIFY(shell->getToolbar()->openPanel(panel->getButton()));
        AppToolShellFixture::settle();
        QVERIFY(panel->isVisible());
        for (const auto& [box, color] : colors)
        {
            const QString title = pdf::PDFDocumentToolSetPageBoxes::getBoxTitle(box);
            pdf::PDFTileRow* row = nullptr;
            for (pdf::PDFTileRow* candidate : panel->findChildren<pdf::PDFTileRow*>())
            {
                if (candidate->getTitle() == title)
                {
                    row = candidate;
                }
            }
            QVERIFY2(row, qPrintable(title));
            QWidget* swatch = row->findChild<QWidget*>(QStringLiteral("toolBoxSwatch"));
            QVERIFY2(swatch, qPrintable(title));
            const QRectF tile = pdf::PDFComponentPainter::getTileRowTileRect(QRectF(row->rect()), row->getContent(), row->getComponentState());
            QCOMPARE(swatch->geometry(), tile.toAlignedRect());
            const QImage picture = AppToolShellFixture::render(row, 1.0);
            QCOMPARE(QColor(picture.pixel(swatch->geometry().center())), pdf::PDFStylePainter::color(color));
        }
        panel->close();
        AppToolShellFixture::settle();
    }
}

QTEST_MAIN(AppToolPageGeometryTest)

#include "tst_apptoolpagegeometrytest.moc"
