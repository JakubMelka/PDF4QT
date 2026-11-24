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

// The GUI of Measure (plan of step 7 of phase 2, 2.2, 4.8, 8; package P7): the frames of 2.2 by their ids, the
// snapping, the calibration, the presets, Save as Annotations as one step of Undo, measurements on more pages
// (FM-9 cancelled), a locked document and a document without bit 6.

#include "appasyncdialogs.h"
#include "apptoolshellfixture.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappleaveflow.h"
#include "pdfapptoolgui_measure.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfapptoolundotarget.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdfmeasurescalerepository.h"
#include "pdfrepositoryhelper.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_measure.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfobjectutils.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfappmeasuredialogs.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfappworkspace.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentpainter.h"
#include "pdfapplifetimetokens.h"
#include "pdfdeferreddeletion.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QPainter>
#include <QtMath>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

#include <functional>

using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;
using pdf::PDFAppMeasureDrawKind;
using pdf::PDFAppToolGUIMeasure;
using pdf::PDFDocumentToolShell;
using pdf::PDFMeasureKind;
using pdf::PDFMeasureScale;

/// Helpers of the cases
class MeasureGUITestHelper
{
public:
    MeasureGUITestHelper() = delete;

    static QString toolId() { return QString::fromLatin1(pdf::PDFDocumentToolMeasure::TOOL_ID); }

    /// Opens the file and Measure in its tab
    static PDFDocumentToolShell* open(AppToolShellFixture& fixture, const QString& filePath, pdf::PDFAppDocumentTab** openedTab = nullptr)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        if (openedTab)
        {
            *openedTab = tab;
        }
        PDFDocumentToolShell* shell = fixture.openTool(tab, toolId());
        AppToolShellFixture::settle();
        waitRead(shell ? qobject_cast<PDFAppToolGUIMeasure*>(shell->getGUI()) : nullptr);
        return shell;
    }

    /// The job of the measurement annotations of the last version is done (review RS7CCORE L9: they are read in a
    /// worker thread)
    static bool waitRead(PDFAppToolGUIMeasure* gui)
    {
        QPointer<PDFAppToolGUIMeasure> guard(gui);
        const bool isDone = AppToolShellFixture::waitFor([&guard]() { return !guard || !guard->isReadingAnnotations(); });
        AppToolShellFixture::settle();
        return isDone;
    }

    /// The sample of 2 pages of the fixture (it has no /ID: it cannot be identified, 473:89712)
    static PDFDocumentToolShell* openSimple(AppToolShellFixture& fixture, pdf::PDFAppDocumentTab** openedTab = nullptr)
    {
        return open(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Floor_Plan.pdf"), 2), openedTab);
    }

    static PDFAppToolGUIMeasure* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUIMeasure*>(shell->getGUI()) : nullptr;
    }

    /// A number format of Table 268: the label and the conversion factor
    static pdf::PDFObject numberFormat(const QString& label, pdf::PDFReal factor)
    {
        pdf::PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("NumberFormat");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("U");
        factory << label;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("C");
        factory << factor;
        factory.endDictionaryItem();
        factory.endDictionary();
        return factory.takeObject();
    }

    /// /VP of one viewport over the page: a rectilinear measure, 1 point = \p millimetersPerPoint mm (12.9.2)
    static pdf::PDFObject viewports(pdf::PDFReal millimetersPerPoint, const QString& ratio)
    {
        pdf::PDFObjectFactory factory;
        factory.beginArray();
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("Viewport");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("BBox");
        factory << QRectF(0, 0, 595, 842);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Measure");
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << pdf::WrapName("Measure");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << pdf::WrapName("RL");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("R");
        factory << ratio;
        factory.endDictionaryItem();
        for (const char* key : { "X", "D", "A" })
        {
            factory.beginDictionaryItem(key);
            factory.beginArray();
            factory << (key[0] == 'X' ? numberFormat(QStringLiteral("mm"), millimetersPerPoint) : numberFormat(QStringLiteral("mm"), 1.0));
            factory.endArray();
            factory.endDictionaryItem();
        }
        factory.endDictionary();
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endArray();
        return factory.takeObject();
    }

    /// The certification of Table 257 with the permissions \p certification (a DocMDP signature dictionary)
    static void certify(pdf::PDFDocumentBuilder& builder, int certification)
    {
        pdf::PDFObjectFactory signature;
        signature.beginDictionary();
        signature.beginDictionaryItem("Type");
        signature << pdf::WrapName("Sig");
        signature.endDictionaryItem();
        signature.beginDictionaryItem("Reference");
        signature.beginArray();
        signature.beginDictionary();
        signature.beginDictionaryItem("TransformMethod");
        signature << pdf::WrapName("DocMDP");
        signature.endDictionaryItem();
        signature.beginDictionaryItem("TransformParams");
        signature.beginDictionary();
        signature.beginDictionaryItem("P");
        signature << pdf::PDFInteger(certification);
        signature.endDictionaryItem();
        signature.endDictionary();
        signature.endDictionaryItem();
        signature.endDictionary();
        signature.endArray();
        signature.endDictionaryItem();
        signature.endDictionary();
        const pdf::PDFObjectReference signatureReference = builder.addObject(signature.takeObject());

        pdf::PDFObjectFactory catalog;
        catalog.beginDictionary();
        catalog.beginDictionaryItem("Perms");
        catalog.beginDictionary();
        catalog.beginDictionaryItem("DocMDP");
        catalog << signatureReference;
        catalog.endDictionaryItem();
        catalog.endDictionary();
        catalog.endDictionaryItem();
        catalog.endDictionary();
        builder.mergeTo(builder.getCatalogReference(), catalog.takeObject());
    }

    /// A document of 2 pages A4 with /ID[0] (14.4), optionally with a viewport 1:100 on both pages and a certification
    static QString write(const QString& filePath, bool hasViewports, int certification)
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 2; ++i)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(0, 0, 595, 842));
            if (hasViewports)
            {
                pdf::PDFObjectFactory update;
                update.beginDictionary();
                update.beginDictionaryItem("VP");
                update << viewports(100.0 * 25.4 / 72.0, QStringLiteral("1 mm = 100 mm"));
                update.endDictionaryItem();
                update.endDictionary();
                builder.mergeTo(page, update.takeObject());
            }
        }
        if (certification > 0)
        {
            certify(builder, certification);
        }
        builder.ensureDocumentId();
        pdf::PDFDocument document = builder.build();
        return AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    /// Metric 1:100 (1 mm = 100 mm)
    static PDFMeasureScale metric100()
    {
        return PDFMeasureScale(1.0, "mm", 100.0, "mm", QStringLiteral("Metric 1:100"));
    }

    /// The rows of the list: their titles and descriptions
    static QStringList rowTexts(const PDFAppToolGUIMeasure* gui)
    {
        QStringList texts;
        for (const pdf::PDFAppMeasureListModel::Row& row : gui->getListModel()->getRows())
        {
            texts << row.title + QStringLiteral(" | ") + row.description;
        }
        return texts;
    }

    /// The measurement annotations of the document
    static std::vector<pdf::PDFMeasureAnnotationInfo> annotations(pdf::PDFAppDocument* document)
    {
        const pdf::PDFDocumentPointer pdf = document->getDocument();
        std::vector<pdf::PDFInteger> pages(pdf->getCatalog()->getPageCount());
        std::iota(pages.begin(), pages.end(), pdf::PDFInteger(0));
        return pdf::PDFMeasureAnnotationInfo::readPages(pdf.data(), pages, 1000, 1000, nullptr, nullptr);
    }

    /// The canvas with the focus (Esc is the shortcut of the shell)
    static QWidget* focusCanvas(pdf::PDFAppDocumentTab* tab)
    {
        QWidget* canvas = AppToolShellFixture::canvasWidget(tab->getView());
        if (!canvas)
        {
            return nullptr;
        }
        canvas->window()->activateWindow();
        if (!QTest::qWaitForWindowActive(canvas->window()))
        {
            return nullptr;
        }
        canvas->setFocus();
        AppToolShellFixture::settle();
        return canvas;
    }
};

using H = MeasureGUITestHelper;

class AppToolMeasureTest : public QObject
{
    Q_OBJECT

private slots:
    void registered();
    void startState();
    void drawingOnMorePages();
    void drawingByMouse();
    void saveAsAnnotationsOneUndoStep();
    void annotationsStorage();
    void lockedDocument();
    void documentWithoutBit6();
    void certifiedDocument();
    void calibration();
    void presets();
    void presetDescription();
    void scaleRememberedForDocument();
    void scaleFromDocument();
    void escapeOrder();
    void quitSaveAndCloseThenKeep();
    void listClickAfterSignal();
    void exportCsv();
    void framesInBothThemes();
    void figmaToolbarTitles();
    void figmaPanelTexts();
    void figmaEmptyStateAtTop();
    void figmaListTiles();
    void figmaMenuStatusTexts();
    void figmaExportKeepsSaveAsAnnotations();

    // The fix wave of step 7c (P2S7CFIXMEASURE)
    void fixAnnotationsReadInWorker();
    void fixPointsOnPage();
    void fixCalibrationOwnDialog();
    void fixCalibrationCanceled();
    void fixCanvasCallout();
    void fixSnapMarks();
    void fixLabelsBeside();
    void fixPanelFields();
    void scaleDialogFieldsNotCut();

    // Step A (PA2b): deferred deletion of the overlay and of the question
    void retireOverlayLeavesCanvasAtOnce();
    void retireToolClosesItsQuestion();
};

void AppToolMeasureTest::registered()
{
    // PDFAppBuiltinTools::registerAll (the context) registers the tool and its GUI once
    AppToolShellFixture fixture;
    QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(H::toolId()));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(H::toolId()));
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(H::toolId());
    PDFAppToolGUIMeasure::registerMeasureTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(H::toolId()), tool);
}

void AppToolMeasureTest::startState()
{
    // 471:5490: Save as Annotations Disabled without a measurement, the list "None yet" with its empty card, the
    // drawing mode Linear on, the default scale Real size (1:1); the units m, sq. m, ° (P7 choice of the frames);
    // the document has no /ID - its scale cannot be remembered (473:89712)
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);

    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Save as Annotations"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getLeftHeader()->getTitleLabel()->text(), QStringLiteral("Measurements"));
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("None yet"));
    QVERIFY(gui->getEmptyCard()->isVisible());

    QVERIFY(gui->isDrawing());
    QCOMPARE(gui->getDrawKind(), PDFAppMeasureDrawKind::Linear);
    QVERIFY(gui->getOverlay());
    QCOMPARE(gui->getOverlay()->getInput(), std::optional<PDFAppMeasureDrawKind>(PDFAppMeasureDrawKind::Linear));
    QCOMPARE(gui->getStatusTextWithoutEscape(), QStringLiteral("Linear: click the first point. Right-click starts over, C shows the large cross."));
    QVERIFY(gui->getStatusText().startsWith(gui->getStatusTextWithoutEscape() + QStringLiteral(" · ")));

    QCOMPARE(gui->getScaleSource(), PDFAppToolGUIMeasure::ScaleSource::Default);
    QCOMPARE(gui->getScale().getScaleFactor(), 1.0);
    QCOMPARE(gui->getScale().getName(), PDFMeasureScale::getDefaultPresets().front().getName());
    QCOMPARE(gui->getScaleSubtitleLabel()->text(), PDFMeasureScale::getDefaultPresets().front().getName() + QStringLiteral(" · the default scale"));

    const pdf::PDFMeasureDisplayUnits units = gui->getUnits();
    QCOMPARE(units.length, QByteArray("m"));
    QCOMPARE(units.area, QByteArray("sqm"));
    QCOMPARE(units.angle, QByteArray("deg"));

    QVERIFY(!gui->isDocumentIdentifiable());
    QCOMPARE(gui->getShownCallout(), std::optional<PDFAppToolGUIMeasure::Callout>(PDFAppToolGUIMeasure::Callout::NotRemembered));
    QVERIFY(gui->canWriteAnnotations());

    // The 8 kinds of the panel in 3 groups (471:6294), the toolbar buttons of 471:5490
    for (const PDFAppMeasureDrawKind kind : { PDFAppMeasureDrawKind::Horizontal, PDFAppMeasureDrawKind::Vertical, PDFAppMeasureDrawKind::Linear,
                                              PDFAppMeasureDrawKind::Perimeter, PDFAppMeasureDrawKind::RectanglePerimeter, PDFAppMeasureDrawKind::Area,
                                              PDFAppMeasureDrawKind::RectangleArea, PDFAppMeasureDrawKind::Angle })
    {
        QVERIFY(gui->getKindRow(kind));
    }
    QVERIFY(gui->getShowButton() && gui->getCrossButton() && gui->getClearButton() && gui->getExportButton());
    QVERIFY(!shell->getModel()->canUndo());
}

void AppToolMeasureTest::drawingOnMorePages()
{
    // FM-9 cancelled: measurements may lie on different pages, each one on the page of its first point; the
    // values by the scale of now (471:7608), the horizontal kind measures the horizontal part only
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppMeasureOverlay* overlay = gui->getOverlay();
    QVERIFY(overlay);
    gui->setScale(H::metric100());
    AppToolShellFixture::settle();

    // The document cannot be identified: the scale is used, but not remembered (473:89712)
    QCOMPARE(gui->getScaleSource(), PDFAppToolGUIMeasure::ScaleSource::Session);
    QCOMPARE(gui->getScaleSubtitleLabel()->text(), QStringLiteral("Metric 1:100 · not remembered"));

    // Linear on page 1: 72 pt = 25.4 mm on the paper = 2.54 m
    overlay->addPoint(0, QPointF(100, 100));
    QCOMPARE(overlay->getPoints().size(), size_t(1));
    QVERIFY(gui->getStatusTextWithoutEscape().startsWith(QStringLiteral("Linear: click the second point.")));
    overlay->addPoint(0, QPointF(172, 100));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasurements().size(), size_t(1));

    // Horizontal on page 2 (300 × 400 pt): the second point has the height of the first one
    gui->setDrawKind(PDFAppMeasureDrawKind::Horizontal);
    AppToolShellFixture::settle();
    overlay->addPoint(1, QPointF(200, 300));
    overlay->addPoint(1, QPointF(272, 380));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasurements().size(), size_t(2));
    QCOMPARE(gui->getMeasurements()[1].pageIndex, pdf::PDFInteger(1));
    QCOMPARE(gui->getMeasurements()[1].points, std::vector<QPointF>({ QPointF(200, 300), QPointF(272, 300) }));
    QCOMPARE(gui->getMeasurements()[1].drawKind, PDFAppMeasureDrawKind::Horizontal);

    QCOMPARE(H::rowTexts(gui), QStringList({ QStringLiteral("2.54 m | Length · page 1 · 1:100"), QStringLiteral("2.54 m | Length · page 2 · 1:100") }));
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("2 temporary"));
    QVERIFY(!gui->getEmptyCard()->isVisible());
    QVERIFY(gui->getListModel()->index(0).data(pdf::PDFComponentRoles::TagRole).toString().isEmpty());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // A temporary measurement uses the scale of now (471:7608)
    gui->setScale(PDFMeasureScale(1.0, "mm", 50.0, "mm", QStringLiteral("Metric 1:50")));
    AppToolShellFixture::settle();
    QCOMPARE(H::rowTexts(gui), QStringList({ QStringLiteral("1.27 m | Length · page 1 · 1:50"), QStringLiteral("1.27 m | Length · page 2 · 1:50") }));

    // The × of a row removes the temporary measurement; Clear removes the rest
    QVERIFY(gui->removeMeasurement(gui->getMeasurements()[0].id));
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    gui->getClearButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(gui->getMeasurements().empty());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("None yet"));
}

void AppToolMeasureTest::drawingByMouse()
{
    // The clicks on the canvas pick the points; the right button starts the drawing over (471:5490)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppMeasureOverlay* overlay = gui->getOverlay();
    QVERIFY(overlay);

    QVERIFY(AppToolShellFixture::clickPage(tab->getView(), 0, QPointF(200, 300)));
    AppToolShellFixture::settle();
    QCOMPARE(overlay->getPoints().size(), size_t(1));
    QCOMPARE(overlay->getPage(), pdf::PDFInteger(0));
    QVERIFY(AppToolShellFixture::clickPage(tab->getView(), 0, QPointF(250, 300), Qt::NoModifier, Qt::RightButton));
    AppToolShellFixture::settle();
    QVERIFY(overlay->getPoints().empty());
    QVERIFY(gui->getMeasurements().empty());

    QVERIFY(AppToolShellFixture::clickPage(tab->getView(), 0, QPointF(200, 300)));
    QVERIFY(AppToolShellFixture::clickPage(tab->getView(), 0, QPointF(300, 300)));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QCOMPARE(gui->getMeasurements()[0].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(gui->getMeasurements()[0].kind, PDFMeasureKind::Distance);
    QVERIFY(overlay->getPoints().empty());

    // The big cross (C) follows the button
    QVERIFY(!overlay->isCrossShown());
    gui->getCrossButton()->click();
    QVERIFY(overlay->isCrossShown());

    // Show hides the measurements on the canvas, the list stays
    gui->getShowButton()->click();
    QVERIFY(!overlay->isShown());
    QCOMPARE(gui->getListModel()->rowCount(), 1);
}

void AppToolMeasureTest::saveAsAnnotationsOneUndoStep()
{
    // Save as Annotations (473:90043): the temporary measurements of both pages become measurement annotations
    // (Table 178 /IT, 12.9 /Measure) in one step of Undo; the rows get the tag Annotation; Undo takes them back
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = tab->getDocument();
    gui->setScale(H::metric100());
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(gui->addMeasurement(1, PDFMeasureKind::Area, { QPointF(100, 100), QPointF(172, 100), QPointF(172, 172), QPointF(100, 172) }, PDFAppMeasureDrawKind::Area));
    AppToolShellFixture::settle();
    const int undoBefore = AppToolShellFixture::undoCount(document);

    QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(H::waitRead(gui));
    QCOMPARE(AppToolShellFixture::undoCount(document), undoBefore + 1);
    QVERIFY(gui->getMeasurements().empty());
    QVERIFY(shell->getState() == PDFDocumentToolShell::State::Editing);

    const std::vector<pdf::PDFMeasureAnnotationInfo> annotations = H::annotations(document);
    QCOMPARE(annotations.size(), size_t(2));
    QCOMPARE(annotations[0].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(annotations[0].kind, PDFMeasureKind::Distance);
    QVERIFY(annotations[0].hasMeasure);
    QVERIFY(qAbs(annotations[0].value - 2.54) < 1e-6);
    QCOMPARE(annotations[0].text, QStringLiteral("2.54 m"));
    QCOMPARE(annotations[1].pageIndex, pdf::PDFInteger(1));
    QCOMPARE(annotations[1].kind, PDFMeasureKind::Area);
    QVERIFY(qAbs(annotations[1].value - 2.54 * 2.54) < 1e-6);

    // The annotation of page 1 is a Line of the intent LineDimension (Table 178), the one of page 2 a Polygon
    // of PolygonDimension (Table 181)
    const pdf::PDFDocumentPointer pdf = document->getDocument();
    const pdf::PDFDictionary* line = pdf->getDictionaryFromObject(pdf->getObjectByReference(annotations[0].annotation));
    QVERIFY(line);
    QCOMPARE(pdf->getObject(line->get("Subtype")).getString(), QByteArray("Line"));
    QCOMPARE(pdf->getObject(line->get("IT")).getString(), QByteArray("LineDimension"));
    const pdf::PDFDictionary* polygon = pdf->getDictionaryFromObject(pdf->getObjectByReference(annotations[1].annotation));
    QVERIFY(polygon);
    QCOMPARE(pdf->getObject(polygon->get("Subtype")).getString(), QByteArray("Polygon"));
    QCOMPARE(pdf->getObject(polygon->get("IT")).getString(), QByteArray("PolygonDimension"));

    QCOMPARE(gui->getListModel()->rowCount(), 2);
    QCOMPARE(gui->getListModel()->index(0).data(pdf::PDFComponentRoles::TagRole).toString(), QStringLiteral("Annotation"));
    QCOMPARE(gui->getListModel()->index(1).data(pdf::PDFComponentRoles::TagRole).toString(), QStringLiteral("Annotation"));
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("0 temporary · 2 stored as annotations"));
    QCOMPARE(gui->getNotice(), PDFAppToolGUIMeasure::Notice::Converted);
    QCOMPARE(gui->getShownCallout(), std::optional<PDFAppToolGUIMeasure::Callout>(PDFAppToolGUIMeasure::Callout::Converted));
    QCOMPARE(gui->getStatusTextWithoutEscape(), QStringLiteral("2 measurements were stored in the document as annotations."));
    QCOMPARE(gui->getCallout(PDFAppToolGUIMeasure::Callout::Converted)->getTitleLabel()->text(), QStringLiteral("2 measurements saved as annotations"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    // Undo of the tool: both annotations go in one step (FM-10)
    shell->getUndoTarget()->undo();
    AppToolShellFixture::settle();
    QVERIFY(H::waitRead(gui));
    QVERIFY(H::annotations(document).empty());
    QCOMPARE(AppToolShellFixture::undoCount(document), undoBefore);
    QCOMPARE(gui->getListModel()->rowCount(), 0);
}

void AppToolMeasureTest::annotationsStorage()
{
    // 471:7947: Annotations in the document writes every measurement at once, one step of Undo each
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = tab->getDocument();
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_STORAGE), QString::fromLatin1(pdf::PDFDocumentToolMeasure::STORAGE_ANNOTATIONS));
    AppToolShellFixture::settle();

    // The callout of 471:7947 replaces the one of 473:89712 (the first one of the order, which applies, is the
    // not remembered scale - the scale is not remembered by an unidentified document)
    QCOMPARE(gui->getShownCallout(), std::optional<PDFAppToolGUIMeasure::Callout>(PDFAppToolGUIMeasure::Callout::NotRemembered));
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_REMEMBER_SCALE), false);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getShownCallout(), std::optional<PDFAppToolGUIMeasure::Callout>(PDFAppToolGUIMeasure::Callout::Annotations));

    const int undoBefore = AppToolShellFixture::undoCount(document);
    QVERIFY(gui->addMeasurement(1, PDFMeasureKind::Angle, { QPointF(172, 100), QPointF(100, 100), QPointF(100, 172) }, PDFAppMeasureDrawKind::Angle));
    AppToolShellFixture::settle();
    QVERIFY(H::waitRead(gui));
    QCOMPARE(AppToolShellFixture::undoCount(document), undoBefore + 1);
    QVERIFY(gui->getMeasurements().empty());
    QCOMPARE(gui->getAnnotations().size(), size_t(1));
    QCOMPARE(gui->getAnnotations()[0].kind, PDFMeasureKind::Angle);
    QCOMPARE(gui->getAnnotations()[0].pageIndex, pdf::PDFInteger(1));
    QCOMPARE(gui->getAnnotations()[0].text, QStringLiteral("90.00 °"));
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QVERIFY(H::waitRead(gui));
    QCOMPARE(AppToolShellFixture::undoCount(document), undoBefore + 2);
    QCOMPARE(H::annotations(document).size(), size_t(2));

    // The rows by their pages: page 1 first
    QCOMPARE(gui->getListModel()->getRows()[0].pageIndex, pdf::PDFInteger(0));
    QCOMPARE(gui->getListModel()->getRows()[1].pageIndex, pdf::PDFInteger(1));

    // A selected annotation is highlighted on the canvas (FM-28)
    gui->selectRow(1);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSelectedRow(), 1);
    QCOMPARE(gui->getOverlay()->getShapes().size(), size_t(1));
    QVERIFY(gui->getOverlay()->getShapes()[0].isSelected);
    QVERIFY(gui->getOverlay()->getShapes()[0].isAnnotation);
    QCOMPARE(gui->getOverlay()->getShapes()[0].pageIndex, pdf::PDFInteger(1));

    shell->getUndoTarget()->undo();
    AppToolShellFixture::settle();
    QVERIFY(H::waitRead(gui));
    QCOMPARE(H::annotations(document).size(), size_t(1));
    QCOMPARE(gui->getAnnotations().size(), size_t(1));
}

void AppToolMeasureTest::lockedDocument()
{
    // FM-1, FM-3: a locked document keeps the temporary measurements and the CSV; Save as Annotations is
    // Disabled, the storage Annotations keeps them temporary
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = tab->getDocument();
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    document->setLocked(true);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getShownCallout(), std::optional<PDFAppToolGUIMeasure::Callout>(PDFAppToolGUIMeasure::Callout::Refusal));
    QCOMPARE(gui->getCallout(PDFAppToolGUIMeasure::Callout::Refusal)->getTitleLabel()->text(), QStringLiteral("Document is locked"));
    QVERIFY(!gui->canWriteAnnotations());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    const int undoBefore = AppToolShellFixture::undoCount(document);
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_STORAGE), QString::fromLatin1(pdf::PDFDocumentToolMeasure::STORAGE_ANNOTATIONS));
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 200), QPointF(172, 200) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(!gui->saveAsAnnotations());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasurements().size(), size_t(2));
    QCOMPARE(AppToolShellFixture::undoCount(document), undoBefore);
    QVERIFY(H::annotations(document).empty());

    // Unlocked: the measurements can be saved again
    document->setLocked(false);
    AppToolShellFixture::settle();
    QVERIFY(gui->canWriteAnnotations());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolMeasureTest::documentWithoutBit6()
{
    // 473:90704, FM-2: without bit 6 of Table 22 no annotation is written; the owner password is offered
    // (Enter Owner Password…), the measurements stay temporary
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::open(fixture, AppDocumentSamples::restricted(fixture.folder()), &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = tab->getDocument();

    QCOMPARE(gui->getShownCallout(), std::optional<PDFAppToolGUIMeasure::Callout>(PDFAppToolGUIMeasure::Callout::Refusal));
    QCOMPARE(gui->getCallout(PDFAppToolGUIMeasure::Callout::Refusal)->getTitleLabel()->text(), QStringLiteral("This document does not allow new annotations."));
    QVERIFY(gui->getOwnerPasswordLink()->isVisible());
    QVERIFY(!gui->canWriteAnnotations());

    const int undoBefore = AppToolShellFixture::undoCount(document);
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(300, 400), QPointF(372, 400) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    // The measurement of another producer (the rectangle "square" is no measurement) is not listed; the CSV has
    // the temporary one
    QCOMPARE(gui->getAnnotations().size(), size_t(0));
    QCOMPARE(gui->createData().value(QString::fromLatin1(pdf::PDFDocumentToolMeasure::DATA_MEASUREMENTS)).toArray().size(), 1);
    QCOMPARE(AppToolShellFixture::undoCount(document), undoBefore);
}

void AppToolMeasureTest::certifiedDocument()
{
    // 12.8.2.2, Table 257: a certification of P = 1 allows no new annotation (P = 3 does)
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::open(fixture, H::write(fixture.path(QStringLiteral("Certified.pdf")), false, 1));
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getShownCallout(), std::optional<PDFAppToolGUIMeasure::Callout>(PDFAppToolGUIMeasure::Callout::Refusal));
    QCOMPARE(gui->getCallout(PDFAppToolGUIMeasure::Callout::Refusal)->getTitleLabel()->text(), QStringLiteral("The certification of this document does not allow new annotations."));
    QVERIFY(!gui->getOwnerPasswordLink()->isVisible());
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(300, 400), QPointF(372, 400) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(!gui->saveAsAnnotations());

    AppToolShellFixture fixtureP3;
    PDFDocumentToolShell* shellP3 = H::open(fixtureP3, H::write(fixtureP3.path(QStringLiteral("CertifiedP3.pdf")), false, 3));
    PDFAppToolGUIMeasure* guiP3 = H::gui(shellP3);
    QVERIFY(guiP3);
    QVERIFY(guiP3->canWriteAnnotations());
}

void AppToolMeasureTest::calibration()
{
    // 471:7363, 473:6438: Calibrate Scale… - two points of a known distance, the real distance gives the scale;
    // 473:6755: a zero length cannot give a scale (Cancel | Pick Again)
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppMeasureOverlay* overlay = gui->getOverlay();
    QVERIFY(overlay);
    pdf::PDFMeasureScaleRepository repository(fixture.context->getDatabase());
    const size_t presetCount = repository.getPresets().size();

    gui->startCalibration();
    QVERIFY(gui->isCalibrating());
    QCOMPARE(overlay->getInput(), std::optional<PDFAppMeasureDrawKind>(PDFAppMeasureDrawKind::Calibrate));
    QCOMPARE(gui->getStatusTextWithoutEscape(), QStringLiteral("Calibrate: click the first point of a known distance. Right-click starts over."));
    QCOMPARE(gui->getEscapeModeName(), QStringLiteral("Calibrate"));

    // 72 pt on the paper = 25.4 mm; the real distance 2.54 m gives 1:100
    overlay->addPoint(0, QPointF(100, 100));
    overlay->addPoint(0, QPointF(100, 172));
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getCalibrationDialog() != nullptr; }));
    pdf::PDFAppMeasureScaleDialog* dialog = gui->getCalibrationDialog();
    QCOMPARE(dialog->getMode(), pdf::PDFAppMeasureScaleDialog::Mode::Calibrate);
    QCOMPARE(dialog->getPaperValueBox()->value(), 25.4);
    QCOMPARE(dialog->getPaperUnitBox()->currentData().toString(), QStringLiteral("mm"));
    dialog->getRealUnitBox()->setCurrentIndex(dialog->getRealUnitBox()->findData(QStringLiteral("m")));
    dialog->getRealValueBox()->setValue(2.54);
    QCOMPARE(dialog->getResultLabel()->text(), QStringLiteral("1 : 100"));
    QVERIFY(dialog->getAcceptButton()->isEnabled());
    dialog->getAcceptButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!gui->getCalibrationDialog());
    QVERIFY(!gui->isCalibrating());
    QVERIFY(qAbs(gui->getScale().getScaleFactor() - 100.0) < 1e-9);
    QCOMPARE(gui->getScaleSource(), PDFAppToolGUIMeasure::ScaleSource::Session);

    // No name: no new preset
    QCOMPARE(repository.getPresets().size(), presetCount);

    // A calibration with a name is saved as a preset too
    gui->startCalibration();
    overlay->addPoint(0, QPointF(100, 100));
    overlay->addPoint(0, QPointF(172, 100));
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getCalibrationDialog() != nullptr; }));
    dialog = gui->getCalibrationDialog();
    dialog->getRealUnitBox()->setCurrentIndex(dialog->getRealUnitBox()->findData(QStringLiteral("mm")));
    dialog->getRealValueBox()->setValue(254.0);
    dialog->getNameEdit()->setText(QStringLiteral("Detail 1:10"));
    dialog->getAcceptButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(qAbs(gui->getScale().getScaleFactor() - 10.0) < 1e-9);
    QCOMPARE(repository.getPresets().size(), presetCount + 1);
    QCOMPARE(repository.getPresets().back().getName(), QStringLiteral("Detail 1:10"));

    // 473:6755: the zero length - Pick Again starts the calibration again
    Q_EMIT overlay->calibrationPicked(0, 0.0);
    AppToolShellFixture::settle();
    QPointer<pdf::PDFMessageBox> zeroLength = gui->getZeroLengthDialog();
    QVERIFY(zeroLength && zeroLength->isVisible());
    QVERIFY(!gui->isCalibrating());
    QPushButton* again = zeroLength->findChild<QPushButton*>(QStringLiteral("measureZeroLengthAgain"));
    QVERIFY(again);
    QCOMPARE(again->text(), QStringLiteral("Pick Again"));
    again->click();
    AppToolShellFixture::settle();
    QVERIFY(!zeroLength);
    QVERIFY(gui->isCalibrating());
    QVERIFY(qAbs(gui->getScale().getScaleFactor() - 10.0) < 1e-9);
}

void AppToolMeasureTest::presets()
{
    // 473:7412, 473:7860: Manage Presets… - a new preset needs its name; the presets live in the database
    // (migration 4) and the panel Scale offers them (471:6943)
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFMeasureScaleRepository repository(fixture.context->getDatabase());
    QCOMPARE(repository.getPresets().size(), PDFMeasureScale::getDefaultPresets().size());

    QPointer<pdf::PDFAppMeasurePresetsDialog> dialog = gui->openPresetsDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getPresets().size(), PDFMeasureScale::getDefaultPresets().size());
    QPointer<pdf::PDFAppMeasureScaleDialog> add = dialog->addPreset();
    QVERIFY(add);
    QCOMPARE(add->getMode(), pdf::PDFAppMeasureScaleDialog::Mode::AddPreset);
    add->getPaperUnitBox()->setCurrentIndex(add->getPaperUnitBox()->findData(QStringLiteral("mm")));
    add->getRealUnitBox()->setCurrentIndex(add->getRealUnitBox()->findData(QStringLiteral("mm")));
    add->getPaperValueBox()->setValue(1.0);
    add->getRealValueBox()->setValue(250.0);
    QCOMPARE(add->getResultLabel()->text(), QStringLiteral("1 : 250"));
    QVERIFY(!add->getAcceptButton()->isEnabled());
    add->getNameEdit()->setText(QStringLiteral("Site 1:250"));
    QVERIFY(add->getAcceptButton()->isEnabled());
    add->getAcceptButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!add);
    QCOMPARE(dialog->getPresets().size(), PDFMeasureScale::getDefaultPresets().size() + 1);

    // Nothing is stored before Save Presets
    QCOMPARE(repository.getPresets().size(), PDFMeasureScale::getDefaultPresets().size());
    dialog->getSaveButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    const std::vector<PDFMeasureScale> stored = repository.getPresets();
    QCOMPARE(stored.size(), PDFMeasureScale::getDefaultPresets().size() + 1);
    QCOMPARE(stored.back().getName(), QStringLiteral("Site 1:250"));
    QVERIFY(qAbs(stored.back().getScaleFactor() - 250.0) < 1e-9);

    // The panel Scale offers the new preset
    bool isOffered = false;
    for (const pdf::PDFTileRow* row : gui->getScalePanel()->findChildren<pdf::PDFTileRow*>())
    {
        isOffered = isOffered || row->getTitle() == QStringLiteral("Site 1:250");
    }
    QVERIFY(isOffered);

    // Restore Defaults asks first; the default presets come back after Save Presets
    dialog = gui->openPresetsDialog();
    QVERIFY(dialog);
    QPointer<pdf::PDFMessageBox> question = dialog->restoreDefaults();
    QVERIFY(question);
    QVERIFY(question->isVisible());
    QPushButton* restore = nullptr;
    for (QPushButton* button : question->findChildren<QPushButton*>())
    {
        restore = button->text() == QStringLiteral("Restore Defaults") ? button : restore;
    }
    QVERIFY(restore);
    restore->click();
    AppToolShellFixture::settle();
    QVERIFY(dialog->isRestored());
    dialog->getSaveButton()->click();
    AppToolShellFixture::settle();
    QCOMPARE(repository.getPresets().size(), PDFMeasureScale::getDefaultPresets().size());
    QVERIFY(!repository.arePresetsEdited());
}

void AppToolMeasureTest::presetDescription()
{
    // 473:92070, 473:7024, 473:7412, 473:7860: the dialogs of a scale have the field Description (empty for a new
    // scale); a preset and the default scale keep it in the database, the column Description of Manage Presets shows it
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFMeasureScaleRepository repository(fixture.context->getDatabase());

    // 473:92070: the default scale "Real size (1:1)" with "The drawing is in the real size"; a new default keeps the
    // description of its field
    QPointer<pdf::PDFAppMeasureScaleDialog> defaultDialog = gui->openDefaultScaleDialog();
    QVERIFY(defaultDialog);
    QCOMPARE(defaultDialog->getNameEdit()->text(), QStringLiteral("Real size (1:1)"));
    QCOMPARE(defaultDialog->getDescriptionEdit()->text(), QStringLiteral("The drawing is in the real size"));
    defaultDialog->getRealValueBox()->setValue(100.0);
    defaultDialog->getNameEdit()->setText(QStringLiteral("Metric 1:100"));
    defaultDialog->getDescriptionEdit()->setText(QStringLiteral("Site plans"));
    defaultDialog->getAcceptButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!defaultDialog);
    QCOMPARE(repository.getDefaultScale().getDescription(), QStringLiteral("Site plans"));
    defaultDialog = gui->openDefaultScaleDialog();
    QVERIFY(defaultDialog);
    QCOMPARE(defaultDialog->getDescriptionEdit()->text(), QStringLiteral("Site plans"));
    defaultDialog->reject();
    AppToolShellFixture::settle();

    // 473:7024: a new scale starts without a description; a named custom scale becomes a preset with its description
    QPointer<pdf::PDFAppMeasureScaleDialog> custom = gui->openCustomScaleDialog();
    QVERIFY(custom);
    QVERIFY(custom->getDescriptionEdit()->text().isEmpty());
    custom->getPaperUnitBox()->setCurrentIndex(custom->getPaperUnitBox()->findData(QStringLiteral("mm")));
    custom->getRealUnitBox()->setCurrentIndex(custom->getRealUnitBox()->findData(QStringLiteral("mm")));
    custom->getPaperValueBox()->setValue(1.0);
    custom->getRealValueBox()->setValue(75.0);
    custom->getNameEdit()->setText(QStringLiteral("Metric 1:75"));
    custom->getDescriptionEdit()->setText(QStringLiteral("  Facade details  "));
    custom->getAcceptButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!custom);
    QCOMPARE(repository.getPresets().back().getName(), QStringLiteral("Metric 1:75"));
    QCOMPARE(repository.getPresets().back().getDescription(), QStringLiteral("Facade details"));

    // 473:7412: the column Description; Edit… shows the description of the preset and changes it; Add Preset
    // (473:7860) starts empty
    QPointer<pdf::PDFAppMeasurePresetsDialog> dialog = gui->openPresetsDialog();
    QVERIFY(dialog);
    const int last = int(dialog->getPresets().size()) - 1;
    QAbstractItemModel* model = dialog->getTable()->model();
    QCOMPARE(model->headerData(2, Qt::Horizontal).toString(), QStringLiteral("Description"));
    QCOMPARE(model->index(last, 2).data().toString(), QStringLiteral("Facade details"));
    QCOMPARE(model->index(0, 2).data().toString(), QStringLiteral("The drawing is in the real size"));
    dialog->selectPreset(last);
    QPointer<pdf::PDFAppMeasureScaleDialog> edit = dialog->editPreset();
    QVERIFY(edit);
    QCOMPARE(edit->getDescriptionEdit()->text(), QStringLiteral("Facade details"));
    edit->getDescriptionEdit()->setText(QStringLiteral("Elevations"));
    edit->getAcceptButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!edit);
    QCOMPARE(dialog->getPresets().back().getDescription(), QStringLiteral("Elevations"));
    QCOMPARE(model->index(last, 2).data().toString(), QStringLiteral("Elevations"));
    QPointer<pdf::PDFAppMeasureScaleDialog> add = dialog->addPreset();
    QVERIFY(add);
    QVERIFY(add->getDescriptionEdit()->text().isEmpty());
    add->reject();
    AppToolShellFixture::settle();
    dialog->getSaveButton()->click();
    AppToolShellFixture::settle();
    QVERIFY(!dialog);
    QCOMPARE(repository.getPresets().back().getDescription(), QStringLiteral("Elevations"));
    QCOMPARE(repository.getPresets().front().getDescription(), QStringLiteral("The drawing is in the real size"));
}

void AppToolMeasureTest::scaleRememberedForDocument()
{
    // 473:9500, Q7.4: a scale chosen for a document with a file and /ID[0] is remembered by the application (never
    // written into the document) and used the next time; Remember off keeps it for the session only
    AppToolShellFixture fixture;
    const QString filePath = H::write(fixture.path(QStringLiteral("Identified.pdf")), false, 0);
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::open(fixture, filePath, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->isDocumentIdentifiable());
    QCOMPARE(gui->getShownCallout(), std::nullopt);
    const quint64 versionBefore = tab->getDocument()->getVersion();

    gui->setScale(H::metric100());
    AppToolShellFixture::settle();
    QCOMPARE(gui->getScaleSource(), PDFAppToolGUIMeasure::ScaleSource::Remembered);
    QCOMPARE(gui->getScaleSubtitleLabel()->text(), QStringLiteral("Metric 1:100 · remembered for this document"));
    QCOMPARE(tab->getDocument()->getVersion(), versionBefore);

    pdf::PDFMeasureScaleRepository repository(fixture.context->getDatabase());
    const pdf::PDFDocumentPointer pdf = tab->getDocument()->getDocument();
    const std::optional<PDFMeasureScale> remembered = repository.findDocumentScale(pdf::PDFRepositoryHelper::normalizePath(filePath), pdf->getIdPart(0));
    QVERIFY(remembered);
    QCOMPARE(remembered->getName(), QStringLiteral("Metric 1:100"));

    // The next start of the tool uses the remembered scale
    shell->close();
    AppToolShellFixture::settle();
    QVERIFY(!shell || shell->getState() == PDFDocumentToolShell::State::Closed);
    shell = fixture.openTool(tab, H::toolId());
    AppToolShellFixture::settle();
    gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getScaleSource(), PDFAppToolGUIMeasure::ScaleSource::Remembered);
    QVERIFY(qAbs(gui->getScale().getScaleFactor() - 100.0) < 1e-9);

    // Review RS7CCORE L8: the use of the remembered scale is its last use (the LRU of 200 documents keeps the scales
    // used lately): an old time of the row becomes the time of the start of the tool
    pdf::PDFDatabaseConnection* connection = fixture.context->getDatabase()->getConnection();
    QVERIFY(connection && !connection->execute("UPDATE measure_document_scale SET last_used = 1").isError());
    shell->close();
    AppToolShellFixture::settle();
    shell = fixture.openTool(tab, H::toolId());
    AppToolShellFixture::settle();
    gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getScaleSource(), PDFAppToolGUIMeasure::ScaleSource::Remembered);
    QVERIFY(connection->queryInteger("SELECT MIN(last_used) FROM measure_document_scale", -1) > 1);

    // Remember off: the chosen scale is used, the remembered one stays
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_REMEMBER_SCALE), false);
    AppToolShellFixture::settle();
    gui->setScale(PDFMeasureScale(1.0, "mm", 50.0, "mm", QStringLiteral("Metric 1:50")));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getScaleSource(), PDFAppToolGUIMeasure::ScaleSource::Session);
    QCOMPARE(gui->getScaleSubtitleLabel()->text(), QStringLiteral("Metric 1:50 · not remembered"));
    QCOMPARE(repository.findDocumentScale(pdf::PDFRepositoryHelper::normalizePath(filePath), pdf->getIdPart(0))->getName(), QStringLiteral("Metric 1:100"));
}

void AppToolMeasureTest::scaleFromDocument()
{
    // 473:9500, 12.9.2: the one scale of the viewports of the document is the scale ("From the document"); a
    // measurement inside a viewport uses the scale of the viewport, whatever the scale of now
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::open(fixture, H::write(fixture.path(QStringLiteral("Viewports.pdf")), true, 0));
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getScaleSource(), PDFAppToolGUIMeasure::ScaleSource::Document);
    QVERIFY(qAbs(gui->getScale().getScaleFactor() - 100.0) < 1e-6);
    QCOMPARE(gui->getScaleSubtitleLabel()->text(), QStringLiteral("From the document · defined by the document"));
    QCOMPARE(gui->getScaleButton()->text(), QStringLiteral("From the document"));

    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    gui->setScale(PDFMeasureScale(1.0, "mm", 50.0, "mm", QStringLiteral("Metric 1:50")));
    AppToolShellFixture::settle();
    QCOMPARE(H::rowTexts(gui), QStringList({ QStringLiteral("2.54 m | Length · page 1 · 1:100") }));
}

void AppToolMeasureTest::listClickAfterSignal()
{
    // A click into the list selects its measurement (FM-28) after the signal currentChanged of the selection model,
    // not inside it: Qt reads the selection model after the signal, and the selection runs code of the application
    // (the page of the view, the status text), which may delete the tab (the lifetime sweep reviewmeasure crashed
    // so; CLAUDE.md lifetime)
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 200), QPointF(172, 200) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSelectedRow(), -1);

    int statusChanges = 0;
    QObject context;
    QObject::connect(gui, &PDFAppToolGUIMeasure::statusTextChanged, &context, [&statusChanges]() { ++statusChanges; });
    pdf::PDFListView* view = gui->getListView();
    QVERIFY(view && view->model() && view->model()->rowCount() == 2);
    view->setCurrentIndex(view->model()->index(1, 0));
    QCOMPARE(statusChanges, 0);
    QCOMPARE(gui->getSelectedRow(), -1);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSelectedRow(), 1);
}

void AppToolMeasureTest::escapeOrder()
{
    // Owner 2026-11-17 (the order of Redact): Esc ends the points of an unfinished drawing, then the drawing
    // mode, then the selection, then it leaves the tool
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppMeasureOverlay* overlay = gui->getOverlay();
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    gui->selectRow(0);
    overlay->addPoint(0, QPointF(300, 300));
    QCOMPARE(overlay->getPoints().size(), size_t(1));

    QWidget* canvas = H::focusCanvas(tab);
    QVERIFY(canvas);
    QTest::keyClick(canvas, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(overlay->getPoints().empty());
    QVERIFY(gui->isDrawing());
    QCOMPARE(gui->getSelectedRow(), 0);

    QCOMPARE(gui->getEscapeModeName(), QStringLiteral("Linear"));
    QTest::keyClick(canvas, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!gui->isDrawing());
    QVERIFY(!overlay->getInput().has_value());
    QCOMPARE(gui->getSelectedRow(), 0);
    QCOMPARE(gui->getEscapeModeHint(), QStringLiteral("Esc clears the selection"));

    QTest::keyClick(canvas, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getSelectedRow(), -1);
    QVERIFY(shell && shell->getState() == PDFDocumentToolShell::State::Editing);

    // A kind of the menu turns the drawing mode on again
    gui->setDrawKind(PDFAppMeasureDrawKind::Angle);
    QVERIFY(gui->isDrawing());
    QCOMPARE(overlay->getInput(), std::optional<PDFAppMeasureDrawKind>(PDFAppMeasureDrawKind::Angle));
    QTest::keyClick(canvas, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!gui->isDrawing());

    // Nothing is left to end: Esc leaves the tool - with the temporary measurement it asks first (owner 2026-11-20):
    // Keep Measuring, Save and Close (decision 2: Primary, the icon of Save as Annotations), Discard Measurement
    // (RESCUX L2: one measurement) closes it
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QTest::keyClick(canvas, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(shell && shell->getState() == PDFDocumentToolShell::State::Editing);
    QPointer<QDialog> question = shell->getCloseQuestion();
    QVERIFY(question && question->isVisible());
    QPushButton* action = question->findChild<QPushButton*>(QStringLiteral("toolCloseAction"));
    QVERIFY(action);
    QCOMPARE(action->text(), QStringLiteral("Save and Close"));
    QVERIFY(action->isDefault());
    QVERIFY(!action->icon().isNull());
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Save as Annotations"));
    QPushButton* discard = question->findChild<QPushButton*>(QStringLiteral("toolCloseDiscard"));
    QVERIFY(discard);
    QCOMPARE(discard->text(), QStringLiteral("Discard Measurement"));
    discard->click();
    QVERIFY(AppToolShellFixture::waitFor([&shell]() { return !shell || shell->getState() != PDFDocumentToolShell::State::Editing; }));
    fixture.closeMessageBoxes();
}

void AppToolMeasureTest::quitSaveAndCloseThenKeep()
{
    // Owner 2026-11-20, decision 8: Save and Close of the first tab and Keep Measuring of the second cancel the quit -
    // nothing is saved while the quit waits. Owner 2026-11-24: the answer of the first tool is final - the canceled
    // quit keeps the window, the first measurement becomes an annotation (one Undo step) and the first tool closes; the
    // second tool keeps its temporary measurement (its own Keep). When the quit goes on, the measurement becomes an
    // annotation before the question of the unsaved documents; Cancel there keeps the window, the answered tools close.
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* firstTab = nullptr;
    pdf::PDFAppDocumentTab* secondTab = nullptr;
    QPointer<PDFDocumentToolShell> firstShell = H::openSimple(fixture, &firstTab);
    QPointer<PDFDocumentToolShell> secondShell = H::open(fixture, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Second_Plan.pdf"), 2), &secondTab);
    QPointer<PDFAppToolGUIMeasure> first = H::gui(firstShell);
    QPointer<PDFAppToolGUIMeasure> second = H::gui(secondShell);
    QVERIFY(first && second);
    QVERIFY(first->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(second->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();

    // Clicks the button of the open question of the tool (A0: the quit has shown it and returned) and settles
    const auto answer = [](QPointer<PDFDocumentToolShell> shell, const QString& buttonName)
    {
        QDialog* question = shell ? shell->getCloseQuestion() : nullptr;
        QPushButton* button = question && question->isVisible() ? question->findChild<QPushButton*>(buttonName) : nullptr;
        if (button)
        {
            button->click();
            AppToolShellFixture::settle();
        }
        return button != nullptr;
    };

    // While the quit asks the second tool, Save and Close of the first one is only remembered by its shell (decision
    // 8): no annotation yet, the measurement is still temporary, no pending edit of the document stands for it
    const int firstUndoCount = AppToolShellFixture::undoCount(firstTab->getDocument());
    pdftest::NestedEventLoopGuard loopGuard;
    QVERIFY(!fixture.window->close());
    QVERIFY(answer(firstShell, QStringLiteral("toolCloseAction")));                 // Save and Close
    QVERIFY(secondShell && secondShell->getCloseQuestion() && secondShell->getCloseQuestion()->isVisible());
    const size_t annotationsDuringWait = H::annotations(firstTab->getDocument()).size();
    const size_t measurementsDuringWait = first->getMeasurements().size();
    const bool isPendingEditDuringWait = firstTab->getDocument()->hasPendingEdits();
    const bool isDeferredDuringWait = firstShell && firstShell->hasDeferredCloseQuestionAction();
    QVERIFY(answer(secondShell, QStringLiteral("toolCloseKeep")));                  // Keep Measuring
    QCOMPARE(annotationsDuringWait, size_t(0));
    QCOMPARE(measurementsDuringWait, size_t(1));
    QVERIFY(!isPendingEditDuringWait);
    QVERIFY(isDeferredDuringWait);

    // The canceled quit keeps the window; the answer of the first tool is applied (one Undo step of the document, an
    // unsaved change) and the first tool closes; the second tool keeps its temporary measurement and nothing of it
    // is in its document
    QVERIFY(fixture.window->isVisible());
    QVERIFY(!pdf::PDFAppLeaveFlow::isRunningIn(fixture.window.data()));
    QVERIFY(!firstShell || firstShell->getState() == PDFDocumentToolShell::State::Closed);
    QVERIFY(!firstTab->getView()->isToolModeActive());
    QCOMPARE(H::annotations(firstTab->getDocument()).size(), size_t(1));
    QCOMPARE(AppToolShellFixture::undoCount(firstTab->getDocument()), firstUndoCount + 1);
    QVERIFY(firstTab->getDocument()->isModified());
    QVERIFY(secondShell && second);
    QCOMPARE(secondShell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(second->getMeasurements().size(), size_t(1));
    QVERIFY(H::annotations(secondTab->getDocument()).empty());

    // The answer is applied once (RLC2 L-1): a later commit of the pending edits (an undo, a save) writes nothing more
    firstTab->getDocument()->commitPendingEdits();
    AppToolShellFixture::settle();
    QCOMPARE(H::annotations(firstTab->getDocument()).size(), size_t(1));
    QCOMPARE(AppToolShellFixture::undoCount(firstTab->getDocument()), firstUndoCount + 1);

    // The quit goes on with the tool opened again in the first tab: the new measurement is an annotation, when the
    // unsaved documents are asked; Cancel there keeps the window - both tools answered by closing, both close (Save
    // and Close kept its annotation, Discard Measurement discarded the temporary measurement of the second tool)
    firstShell = fixture.openTool(firstTab, H::toolId());
    AppToolShellFixture::settle();
    first = H::gui(firstShell);
    QVERIFY(first && H::waitRead(first));
    QVERIFY(first->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 300), QPointF(172, 300) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QVERIFY(!fixture.window->close());
    QVERIFY(answer(firstShell, QStringLiteral("toolCloseAction")));
    QVERIFY(answer(secondShell, QStringLiteral("toolCloseDiscard")));
    QWidget* unsaved = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        unsaved = widget->objectName() == QStringLiteral("quitUnsavedDialog") && widget->isVisible() ? widget : unsaved;
    }
    QVERIFY(unsaved);
    QCOMPARE(H::annotations(firstTab->getDocument()).size(), size_t(2));
    unsaved->findChild<QPushButton*>(QStringLiteral("cancelButton"))->click();
    AppToolShellFixture::settle();
    QVERIFY(fixture.window->isVisible());
    QVERIFY(!firstShell || firstShell->getState() == PDFDocumentToolShell::State::Closed);
    QVERIFY(!secondShell || secondShell->getState() == PDFDocumentToolShell::State::Closed);
    QVERIFY(!firstTab->getView()->isToolModeActive());
    QVERIFY(!secondTab->getView()->isToolModeActive());
    QCOMPARE(H::annotations(firstTab->getDocument()).size(), size_t(2));
    QVERIFY(H::annotations(secondTab->getDocument()).empty());
    fixture.closeMessageBoxes();
}

void AppToolMeasureTest::exportCsv()
{
    // 473:91038: Export CSV - the data of the list (annotations and temporary measurements of every page) in
    // the export dialog, never a run of the tool
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_STORAGE), QString::fromLatin1(pdf::PDFDocumentToolMeasure::STORAGE_ANNOTATIONS));
    QVERIFY(gui->addMeasurement(1, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QVERIFY(H::waitRead(gui));
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_STORAGE), QString::fromLatin1(pdf::PDFDocumentToolMeasure::STORAGE_TEMPORARY));
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(100, 172) }, PDFAppMeasureDrawKind::Vertical));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getAnnotations().size(), size_t(1));
    QCOMPARE(gui->getMeasurements().size(), size_t(1));

    // The rows of the data in the order of the pages: the temporary one of page 1, the annotation of page 2
    const QJsonArray rows = gui->createData().value(QString::fromLatin1(pdf::PDFDocumentToolMeasure::DATA_MEASUREMENTS)).toArray();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].toObject().value(QStringLiteral("page")).toInt(), 1);
    QCOMPARE(rows[1].toObject().value(QStringLiteral("page")).toInt(), 2);
    QCOMPARE(rows[0].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("0.03 m"));
    QCOMPARE(rows[1].toObject().value(QStringLiteral("text")).toString(), QStringLiteral("0.03 m"));

    const int undoBefore = AppToolShellFixture::undoCount(tab->getDocument());
    gui->getExportButton()->click();
    pdf::PDFAppToolResultDialog* dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppToolResultDialog>();
    QVERIFY(dialog);
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("1 temporary, 1 annotation"));
    QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("Page, Type, Value, Unit, Text, Scale"));
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("Floor_Plan - measurements.csv"));
    dialog->close();
    AppToolShellFixture::settle();
    QCOMPARE(AppToolShellFixture::undoCount(tab->getDocument()), undoBefore);
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
}

void AppToolMeasureTest::framesInBothThemes()
{
    // The frames of 2.2 (471:5490 … 473:92070) in both themes, at 1x and 2x and right-to-left: no pixel of the leak
    // color #A000FF (CLAUDE.md, Fluent); PDF4QT_APP_SCREENSHOTS=<folder> writes them by the frame names. The style of
    // the application (the other cases run without it)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    auto capture = [](QWidget* window, qreal ratio)
    {
        QImage image(QSize(qCeil(window->width() * ratio), qCeil(window->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(ratio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        QPainter painter(&image);
        window->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren | QWidget::DrawWindowBackground);
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget != window && widget->isVisible() && !widget->isAncestorOf(window) && (widget->windowType() == Qt::Popup || widget->windowType() == Qt::Dialog))
            {
                widget->render(&painter, window->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0))), QRegion(), QWidget::DrawChildren);
            }
        }
        painter.end();
        return image;
    };
    auto leakPixels = [&folder, &capture](QWidget* window, const QString& name)
    {
        AppToolShellFixture::settle();
        int count = 0;
        for (const qreal ratio : { 1.0, 2.0 })
        {
            const QImage image = capture(window, ratio);
            count += AppToolShellFixture::countLeakPixels(image);
            if (!folder.isEmpty())
            {
                QDir().mkpath(folder);
                image.save(QDir(folder).absoluteFilePath(name + (ratio > 1.0 ? QStringLiteral("-2x.png") : QStringLiteral(".png"))));
            }
        }
        return count;
    };

    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("-light") : QStringLiteral("-dark");
        AppToolShellFixture fixture;
        QWidget* window = fixture.window;
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);   // after the context, which applies its theme
        pdf::PDFAppDocumentTab* tab = nullptr;
        PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
        PDFAppToolGUIMeasure* gui = H::gui(shell);
        QVERIFY(gui);
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-empty") + suffix), 0);

        gui->getOverlay()->addPoint(0, QPointF(40, 60));
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-drawing") + suffix), 0);
        gui->setScale(PDFMeasureScale(1.0, "mm", 50.0, "mm", QStringLiteral("Metric 1:50")));
        gui->getOverlay()->addPoint(0, QPointF(260, 60));
        QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(40, 60), QPointF(40, 260) }, PDFAppMeasureDrawKind::Vertical));
        QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Area, { QPointF(60, 80), QPointF(200, 80), QPointF(200, 200), QPointF(60, 200) }, PDFAppMeasureDrawKind::Area));
        QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Perimeter, { QPointF(210, 80), QPointF(280, 80), QPointF(280, 150), QPointF(210, 150) }, PDFAppMeasureDrawKind::Perimeter));
        QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Angle, { QPointF(280, 300), QPointF(200, 300), QPointF(200, 220) }, PDFAppMeasureDrawKind::Angle));
        gui->selectRow(1);
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure") + suffix), 0);

        pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
        const std::array<std::pair<QToolButton*, QString>, 3> menus = { std::make_pair(gui->getKindButton(), QStringLiteral("review-measure-menus-tool")),
                                                                        std::make_pair(gui->getScaleButton(), QStringLiteral("review-measure-menus-scale")),
                                                                        std::make_pair(gui->getStorageButton(), QStringLiteral("review-measure-menus-store")) };
        for (const auto& [button, name] : menus)
        {
            pdf::PDFAppToolPanelPopup* panel = toolbar->openPanel(button);
            QVERIFY2(panel, qPrintable(name));
            QCOMPARE(leakPixels(window, name + suffix), 0);
            panel->close();
            AppToolShellFixture::settle();
        }

        QApplication::setLayoutDirection(Qt::RightToLeft);
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-rtl") + suffix), 0);
        QApplication::setLayoutDirection(Qt::LeftToRight);

        gui->startCalibration();
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-calibrate") + suffix), 0);
        gui->getOverlay()->addPoint(0, QPointF(100, 100));
        gui->getOverlay()->addPoint(0, QPointF(100, 172));
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getCalibrationDialog() != nullptr; }));
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-calibrate-dialog") + suffix), 0);
        gui->getCalibrationDialog()->reject();
        AppToolShellFixture::settle();

        QPointer<pdf::PDFAppMeasureScaleDialog> custom = gui->openCustomScaleDialog();
        QVERIFY(custom);
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-custom-scale") + suffix), 0);
        custom->reject();
        AppToolShellFixture::settle();
        QPointer<pdf::PDFAppMeasureScaleDialog> defaultScale = gui->openDefaultScaleDialog();
        QVERIFY(defaultScale);
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-default-scale") + suffix), 0);
        defaultScale->reject();
        AppToolShellFixture::settle();
        QPointer<pdf::PDFAppMeasurePresetsDialog> presets = gui->openPresetsDialog();
        QVERIFY(presets);
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-presets") + suffix), 0);
        presets->reject();
        AppToolShellFixture::settle();

        gui->getExportButton()->click();
        pdf::PDFAppToolResultDialog* dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppToolResultDialog>();
        QVERIFY(dialog);
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-export") + suffix), 0);
        dialog->close();
        AppToolShellFixture::settle();

        QVERIFY(gui->saveAsAnnotations());
        QVERIFY(H::waitRead(gui));
        QCOMPARE(leakPixels(window, QStringLiteral("review-measure-converted") + suffix), 0);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolMeasureTest::figmaToolbarTitles()
{
    // 471:5490 at 1440 px: the whole toolbar - "Measure", "Linear", the scale, "Temporary", "Export CSV" with their
    // titles, Show / the cross / Clear as icons (named for a screen reader), Tutorial, "Close"
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
    QCOMPARE(toolbar->width(), 1440);
    QCOMPARE(toolbar->getCollapseLevel(), 0);
    QCOMPARE(gui->getKindButton()->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QCOMPARE(gui->getKindButton()->text(), QStringLiteral("Linear"));
    QCOMPARE(gui->getStorageButton()->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QCOMPARE(gui->getExportButton()->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
    QCOMPARE(gui->getExportButton()->text(), QStringLiteral("Export CSV"));
    const std::array<std::pair<QToolButton*, QString>, 3> icons = { std::make_pair(gui->getShowButton(), QStringLiteral("Show measurements")),
                                                                    std::make_pair(gui->getCrossButton(), QStringLiteral("Large cross (C)")),
                                                                    std::make_pair(gui->getClearButton(), QStringLiteral("Clear temporary measurements")) };
    for (const auto& [button, title] : icons)
    {
        QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);
        QCOMPARE(button->accessibleName(), title);
        QCOMPARE(button->toolTip(), title);
    }
    QCOMPARE(toolbar->getCancelButton()->text(), QStringLiteral("Close"));
}

void AppToolMeasureTest::figmaPanelTexts()
{
    // 471:5490: "Remember the scale per document", "Bold", "Italic" (never "Enable"), "10 pt", "100 %", "10 %"; the rows
    // of Units 42 px apart as the rows of Labels (Lengths 312, Areas 354, Angles 396)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    QVERIFY(H::gui(shell));
    QWidget* scaleCard = tab->findChild<QWidget*>(QStringLiteral("measureScaleCard"));
    QWidget* unitsCard = tab->findChild<QWidget*>(QStringLiteral("measureUnitsCard"));
    QWidget* labelsCard = tab->findChild<QWidget*>(QStringLiteral("measureLabelsCard"));
    QVERIFY(scaleCard && unitsCard && labelsCard);
    auto texts = [](QWidget* card)
    {
        QStringList result;
        for (const QCheckBox* checkBox : card->findChildren<QCheckBox*>())
        {
            result << checkBox->text();
        }
        return result;
    };
    QCOMPARE(texts(scaleCard), QStringList({ QStringLiteral("Remember the scale per document") }));
    QCOMPARE(texts(labelsCard), QStringList({ QStringLiteral("Bold"), QStringLiteral("Italic") }));

    const QList<pdf::PDFDoubleSpinBox*> sizes = labelsCard->findChildren<pdf::PDFDoubleSpinBox*>();
    QCOMPARE(sizes.size(), 1);
    QCOMPARE(sizes.front()->getDisplaySuffix(), QStringLiteral("pt"));
    const QList<pdf::PDFSpinBox*> opacities = labelsCard->findChildren<pdf::PDFSpinBox*>();
    QCOMPARE(opacities.size(), 2);
    for (const pdf::PDFSpinBox* opacity : opacities)
    {
        QCOMPARE(opacity->getDisplaySuffix(), QStringLiteral("%"));
    }

    std::vector<int> tops;
    for (const QComboBox* comboBox : unitsCard->findChildren<QComboBox*>())
    {
        tops.push_back(comboBox->mapTo(unitsCard, QPoint(0, 0)).y());
    }
    std::sort(tops.begin(), tops.end());
    QCOMPARE(tops.size(), size_t(3));
    QCOMPARE(tops[1] - tops[0], 42);
    QCOMPARE(tops[2] - tops[1], 42);
}

void AppToolMeasureTest::figmaEmptyStateAtTop()
{
    // 471:5490: "Measurements" / "None yet" at the top of the panel (y 14 of the panel = 104 of the window), the ruler
    // 34 px below the header (empty-list at 55, its icon at +24), the text 6 px below the icon
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QWidget* window = fixture.window;
    QWidget* header = gui->getLeftHeader();
    QWidget* empty = gui->getEmptyCard();
    QVERIFY(empty->isVisible());
    const pdf::PDFIconBadge* icon = empty->findChild<pdf::PDFIconBadge*>();
    const pdf::PDFAppParagraph* text = empty->findChild<pdf::PDFAppParagraph*>();
    QVERIFY(icon && text);
    const int headerBottom = header->mapTo(window, QPoint(0, header->height())).y();
    QCOMPARE(header->mapTo(window, QPoint(0, 0)).y(), 104);
    QCOMPARE(icon->mapTo(window, QPoint(0, 0)).y() - headerBottom, 34);
    QCOMPARE(text->mapTo(window, QPoint(0, 0)).y() - icon->mapTo(window, QPoint(0, icon->height())).y(), 6);
}

void AppToolMeasureTest::figmaListTiles()
{
    // icon-tile 471:6127 … 471:6177: every row has the accent tile (bg/accent-subtle, the accent icon), selected or not
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Angle, { QPointF(172, 100), QPointF(100, 100), QPointF(100, 172) }, PDFAppMeasureDrawKind::Angle));
    gui->selectRow(1);
    AppToolShellFixture::settle();
    for (int row = 0; row < 2; ++row)
    {
        QCOMPARE(gui->getListModel()->index(row).data(pdf::PDFComponentRoles::TileToneRole).toInt(), int(pdf::PDFComponentPainter::TileRowTone::Accent));
    }
}

void AppToolMeasureTest::figmaMenuStatusTexts()
{
    // 471:6294 "Kind of measurement" with the captions "LENGTH", "PERIMETER AND AREA", "ANGLE"; 471:6943 "Scale of the
    // drawing."; 471:7608 "Where new measurements are kept."; a closed menu gives the status of the drawing back
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    const QString drawingStatus = tab->getView()->getToolMessage();
    QCOMPARE(drawingStatus, gui->getStatusText());

    const std::array<std::pair<QToolButton*, QString>, 3> menus = { std::make_pair(gui->getKindButton(), QStringLiteral("Kind of measurement")),
                                                                    std::make_pair(gui->getScaleButton(), QStringLiteral("Scale of the drawing.")),
                                                                    std::make_pair(gui->getStorageButton(), QStringLiteral("Where new measurements are kept.")) };
    for (const auto& [button, text] : menus)
    {
        pdf::PDFAppToolPanelPopup* panel = shell->getToolbar()->openPanel(button);
        QVERIFY(panel);
        AppToolShellFixture::settle();
        QCOMPARE(tab->getView()->getToolMessage(), text);
        if (button == gui->getKindButton())
        {
            QStringList captions;
            for (const pdf::PDFLabel* label : panel->findChildren<pdf::PDFLabel*>())
            {
                if (label->text() == label->text().toUpper() && !label->text().isEmpty())
                {
                    captions << label->text();
                }
            }
            QCOMPARE(captions, QStringList({ QStringLiteral("LENGTH"), QStringLiteral("PERIMETER AND AREA"), QStringLiteral("ANGLE") }));
        }
        panel->close();
        AppToolShellFixture::settle();
        QCOMPARE(tab->getView()->getToolMessage(), drawingStatus);
    }
}

void AppToolMeasureTest::figmaExportKeepsSaveAsAnnotations()
{
    // 473:91038 / 473:91414 (designer 17. 11., R7-2): during Export CSV Save as Annotations stays Default while temporary
    // measurements exist - the export is no run of the tool, nothing became Disabled by a result; Close stays Enabled
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    gui->getExportButton()->click();
    pdf::PDFAppToolResultDialog* dialog = AppToolShellFixture::waitForWidget<pdf::PDFAppToolResultDialog>();
    QVERIFY(dialog);
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(shell->getToolbar()->getCancelButton()->isEnabled());
    dialog->close();
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolMeasureTest::fixAnnotationsReadInWorker()
{
    // Review RS7CCORE L9: the measurement annotations of a new version are read in one job of a worker thread, never in
    // the GUI thread - right after the change the list still has the annotations read before; the read of the last
    // version is taken (a change cancels the read of the version before)
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(gui->getAnnotations().empty());
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_STORAGE), QString::fromLatin1(pdf::PDFDocumentToolMeasure::STORAGE_ANNOTATIONS));
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_REMEMBER_SCALE), false);
    AppToolShellFixture::settle();

    // Two versions one after the other, without a pause between them
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(gui->isReadingAnnotations());
    QVERIFY(gui->getAnnotations().empty());
    QVERIFY(gui->addMeasurement(1, PDFMeasureKind::Distance, { QPointF(100, 200), QPointF(172, 200) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(gui->isReadingAnnotations());
    QVERIFY(gui->getAnnotations().empty());

    QVERIFY(H::waitRead(gui));
    QCOMPARE(gui->getAnnotations().size(), size_t(2));
    QCOMPARE(gui->getListModel()->rowCount(), 2);
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("0 temporary · 2 stored as annotations"));
}

void AppToolMeasureTest::fixPointsOnPage()
{
    // Review RS7CCORE L4: a measurement lies on its page - a point picked beyond the edge of the page (the next points
    // of a drawing belong to the page of its first point) is placed on the edge of its MediaBox, as MCP refuses a
    // point outside of it (ISO 32000-2, 14.11.2)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    const QRectF mediaBox = tab->getDocument()->getDocument()->getCatalog()->getPage(0)->getMediaBox().normalized();
    pdf::PDFAppMeasureOverlay* overlay = gui->getOverlay();

    overlay->addPoint(0, QPointF(100, 100));
    overlay->addPoint(0, QPointF(mediaBox.right() + 300, mediaBox.bottom() + 50));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasurements().size(), size_t(1));
    QCOMPARE(gui->getMeasurements()[0].points, std::vector<QPointF>({ QPointF(100, 100), QPointF(mediaBox.right(), mediaBox.bottom()) }));

    // A horizontal drawing keeps the height of its first point, its end on the edge
    gui->setDrawKind(PDFAppMeasureDrawKind::Horizontal);
    overlay->addPoint(0, QPointF(200, 300));
    overlay->addPoint(0, QPointF(mediaBox.left() - 40, 500));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getMeasurements().size(), size_t(2));
    QCOMPARE(gui->getMeasurements()[1].points, std::vector<QPointF>({ QPointF(200, 300), QPointF(mediaBox.left(), 300) }));
}

void AppToolMeasureTest::fixCalibrationOwnDialog()
{
    // Review RS7CAPP L3: the picked line of Calibrate Scale… goes to its own dialog (473:6438) - an open Custom Scale…
    // dialog (473:7024) neither takes it nor is changed by it
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QPointer<pdf::PDFAppMeasureScaleDialog> custom = gui->openCustomScaleDialog();
    QVERIFY(custom);
    QCOMPARE(custom->getMode(), pdf::PDFAppMeasureScaleDialog::Mode::Custom);

    gui->startCalibration();
    Q_EMIT gui->getOverlay()->calibrationPicked(0, 72.0);
    AppToolShellFixture::settle();
    QPointer<pdf::PDFAppMeasureScaleDialog> calibration = gui->getCalibrationDialog();
    QVERIFY(calibration);
    QVERIFY(calibration != custom);
    QCOMPARE(calibration->getMode(), pdf::PDFAppMeasureScaleDialog::Mode::Calibrate);
    QCOMPARE(calibration->getPaperValueBox()->value(), 25.4);
    QVERIFY(custom && custom->isVisible());
    QCOMPARE(gui->getScaleDialog(), custom.data());

    calibration->reject();
    custom->reject();
    AppToolShellFixture::settle();
}

void AppToolMeasureTest::fixCalibrationCanceled()
{
    // Review RS7CAPP L5: 473:6438 shows the dialog of the calibration with Calibrate on (the status "Enter the real
    // distance between the two picked points."); Cancel ends the calibration as Cancel of 473:6755 does - the
    // drawing goes on with the kind of the toolbar, the scale stays
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    const PDFMeasureScale scale = gui->getScale();
    gui->startCalibration();
    gui->getOverlay()->addPoint(0, QPointF(100, 100));
    gui->getOverlay()->addPoint(0, QPointF(100, 172));
    QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getCalibrationDialog() != nullptr; }));
    QVERIFY(gui->isCalibrating());
    QCOMPARE(gui->getStatusTextWithoutEscape(), QStringLiteral("Enter the real distance between the two picked points."));

    gui->getCalibrationDialog()->reject();
    AppToolShellFixture::settle();
    QVERIFY(!gui->isCalibrating());
    QVERIFY(gui->isDrawing());
    QCOMPARE(gui->getOverlay()->getInput(), std::optional<PDFAppMeasureDrawKind>(PDFAppMeasureDrawKind::Linear));
    QVERIFY(!gui->getScaleButton()->isChecked());
    QCOMPARE(gui->getScale(), scale);
    QCOMPARE(gui->getStatusTextWithoutEscape(), QStringLiteral("Linear: click the first point. Right-click starts over, C shows the large cross."));

    // 473:6755: the zero length says so in the status bar
    gui->startCalibration();
    Q_EMIT gui->getOverlay()->calibrationPicked(0, 0.0);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getStatusTextWithoutEscape(), QStringLiteral("The picked line has a zero length."));
    gui->getZeroLengthDialog()->reject();
    AppToolShellFixture::settle();
    QVERIFY(!gui->isCalibrating());
}

void AppToolMeasureTest::fixCanvasCallout()
{
    // Review RS7CFIGMA L1, 471:5490: the blue callout over the canvas at 20, 20, as wide as the canvas less 2 × 20 -
    // "Pick two points to measure a distance" / "Points snap to the corners and edges of the drawing. Choose another
    // kind of measurement in the toolbar." - in the empty state; it goes with the first point (471:5731) and with the
    // first measurement (471:5975), the other kinds have their titles
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppMeasureOverlay* overlay = gui->getOverlay();
    QPointer<pdf::PDFAppToolCallout> callout = overlay->getCallout();
    QVERIFY(callout);
    QVERIFY(callout->isVisible());
    QCOMPARE(callout->getTitleLabel()->text(), QStringLiteral("Pick two points to measure a distance"));
    QCOMPARE(callout->getTextLabel()->text(), QStringLiteral("Points snap to the corners and edges of the drawing. Choose another kind of measurement in the toolbar."));
    QCOMPARE(callout->geometry().topLeft(), QPoint(20, 20));
    QCOMPARE(callout->width(), callout->parentWidget()->width() - 40);
    QVERIFY(callout->testAttribute(Qt::WA_TransparentForMouseEvents));

    gui->setDrawKind(PDFAppMeasureDrawKind::Area);
    AppToolShellFixture::settle();
    QCOMPARE(callout->getTitleLabel()->text(), QStringLiteral("Click the corners of a shape to measure its area"));

    // The first point hides it, starting over shows it again
    overlay->addPoint(0, QPointF(100, 100));
    QVERIFY(!callout->isVisible());
    overlay->cancelToolDrag();
    QVERIFY(callout->isVisible());

    // The drawing mode off (Esc) and a measurement hide it
    QVERIFY(gui->handleEscape());
    QVERIFY(!callout->isVisible());
    gui->setDrawKind(PDFAppMeasureDrawKind::Linear);
    QVERIFY(callout->isVisible());
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QVERIFY(!callout->isVisible());

    // The callout goes with the tool
    shell->close();
    AppToolShellFixture::settle();
    QVERIFY(!callout);
}

void AppToolMeasureTest::fixSnapMarks()
{
    // Review RS7CFIGMA L2, 471:5731: while drawing only the snapped point is marked (snap-point, a square of 10) -
    // never every snap point of the page (the corners of the page are snap points too)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppMeasureOverlay* overlay = gui->getOverlay();
    QWidget* canvas = AppToolShellFixture::canvasWidget(tab->getView());
    QVERIFY(canvas);

    // The top left corner of the page (a snap point of the page, Table 31 MediaBox)
    const QRectF mediaBox = tab->getDocument()->getDocument()->getCatalog()->getPage(0)->getMediaBox().normalized();
    bool ok = false;
    const QPoint corner = AppToolShellFixture::pageToWidget(tab->getView(), 0, QPointF(mediaBox.left(), mediaBox.bottom()), &ok);
    QVERIFY(ok);
    auto moveTo = [canvas](const QPoint& position)
    {
        QMouseEvent move(QEvent::MouseMove, position, canvas->mapToGlobal(position), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &move);
    };
    QVERIFY(AppToolShellFixture::waitFor([&]() { moveTo(corner + QPoint(2, 2)); return overlay->getSnapMarks().size() == 1; }));
    const std::vector<QRectF> marks = overlay->getSnapMarks();
    QCOMPARE(marks.front().size(), QSizeF(10, 10));
    QVERIFY((marks.front().center() - QPointF(corner)).manhattanLength() <= 2.0);

    // What the overlay draws over the canvas lies inside of the mark: no other snap point
    QImage image(canvas->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        overlay->drawPostRendering(&painter, image.rect());
    }
    const QRect allowed = marks.front().toAlignedRect().adjusted(-2, -2, 2, 2);
    int drawnPixels = 0;
    int outsidePixels = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (qAlpha(image.pixel(x, y)) > 0)
            {
                ++drawnPixels;
                outsidePixels += allowed.contains(x, y) ? 0 : 1;
            }
        }
    }
    QVERIFY(drawnPixels > 0);
    QCOMPARE(outsidePixels, 0);

    // A shape: its first corner is the close target (471:6681, a ring of 14) besides the snapped point
    gui->setDrawKind(PDFAppMeasureDrawKind::Area);
    overlay->addPoint(0, QPointF(100, 100));
    overlay->addPoint(0, QPointF(200, 100));
    QCOMPARE(overlay->getSnapMarks().front().size(), QSizeF(14, 14));
}

void AppToolMeasureTest::fixLabelsBeside()
{
    // Review RS7CFIGMA L3, L4: the label of a horizontal line lies above it (471:6681: 2 above), of the line being drawn
    // below it (471:5731: 6 below), of a vertical line at its right (471:6681: 8 right, centered), of a slanted line on
    // its middle (471:5975); the live label of a shape "Click the first point to close · A = …" lies 8 above the middle
    // of its first edge (471:6681)
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppMeasureOverlay* overlay = gui->getOverlay();
    const QTransform matrix(1.0, 0.0, 0.0, -1.0, 0.0, 1000.0);   // the page y up, the device y down

    pdf::PDFAppMeasureShape horizontal;
    horizontal.kind = PDFMeasureKind::Distance;
    horizontal.points = { QPointF(100, 100), QPointF(172, 100) };
    horizontal.text = QStringLiteral("11.20 m");
    const QRectF above = overlay->getLabelRect(horizontal, matrix, false);
    QVERIFY(qAbs(above.bottom() - (900.0 - 2.0)) < 0.01);
    QVERIFY(qAbs(above.center().x() - 136.0) < 0.01);
    const QRectF below = overlay->getLabelRect(horizontal, matrix, true);
    QVERIFY(qAbs(below.top() - (900.0 + 6.0)) < 0.01);

    pdf::PDFAppMeasureShape vertical = horizontal;
    vertical.points = { QPointF(100, 100), QPointF(100, 172) };
    const QRectF right = overlay->getLabelRect(vertical, matrix, false);
    QVERIFY(qAbs(right.left() - (100.0 + 8.0)) < 0.01);
    QVERIFY(qAbs(right.center().y() - 864.0) < 0.01);

    pdf::PDFAppMeasureShape slanted = horizontal;
    slanted.points = { QPointF(100, 100), QPointF(172, 172) };
    QVERIFY((overlay->getLabelRect(slanted, matrix, false).center() - QPointF(136.0, 864.0)).manhattanLength() < 0.01);

    // The live label of an area of 3 corners in square inches: half of a square of 72 pt = 0.50 sq. in
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_AREA_UNIT), QStringLiteral("sqin"));
    gui->setDrawKind(PDFAppMeasureDrawKind::Area);
    AppToolShellFixture::settle();
    overlay->addPoint(0, QPointF(100, 100));
    overlay->addPoint(0, QPointF(172, 100));
    QCOMPARE(overlay->getPreviewLabelText(), overlay->getPreviewText());
    overlay->addPoint(0, QPointF(172, 172));
    QCOMPARE(overlay->getPreviewLabelText(), QStringLiteral("Click the first point to close · A = 0.50 sq. in"));

    pdf::PDFAppMeasureShape shape;
    shape.kind = PDFMeasureKind::Area;
    shape.points = { QPointF(100, 100), QPointF(172, 100), QPointF(172, 172) };
    shape.text = overlay->getPreviewLabelText();
    const QRectF shapeLabel = overlay->getLabelRect(shape, matrix, true);
    QVERIFY(qAbs(shapeLabel.bottom() - (900.0 - 8.0)) < 0.01);
    QVERIFY(qAbs(shapeLabel.center().x() - 136.0) < 0.01);
}

void AppToolMeasureTest::scaleDialogFieldsNotCut()
{
    // Owner 2026-11-20 (screenshot of Default Scale): the unit combos of "Distance on the paper" and "Real
    // distance" were cut at the trailing edge - the shared field row fixed the value with its unit to one field
    // width (140). 473:6438: the value 130 and the unit 90 side by side, both whole inside of the card
    AppToolShellFixture fixture;
    PDFDocumentToolShell* shell = H::openSimple(fixture);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);

    QPointer<pdf::PDFAppMeasureScaleDialog> dialog = gui->openDefaultScaleDialog();
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    AppToolShellFixture::settle();

    for (QWidget* field : std::initializer_list<QWidget*>{ dialog->getPaperValueBox(), dialog->getPaperUnitBox(),
                                                          dialog->getRealValueBox(), dialog->getRealUnitBox() })
    {
        QVERIFY(field);
        QWidget* controls = field->parentWidget();
        QVERIFY(controls);
        const QRect inControls = QRect(field->mapTo(controls, QPoint(0, 0)), field->size());
        QVERIFY2(controls->rect().contains(inControls), qPrintable(field->objectName()));

        // The whole field is visible in the dialog: none of its parents cuts it
        const QRect inDialog = QRect(field->mapTo(dialog, QPoint(0, 0)), field->size());
        QVERIFY2(dialog->rect().contains(inDialog), qPrintable(field->objectName()));
        for (QWidget* parent = controls; parent && parent != dialog; parent = parent->parentWidget())
        {
            const QRect inParent = QRect(field->mapTo(parent, QPoint(0, 0)), field->size());
            QVERIFY2(parent->rect().contains(inParent), qPrintable(field->objectName() + QStringLiteral(" in ") + parent->objectName()));
        }
    }
    QCOMPARE(dialog->getPaperValueBox()->width(), 130);
    QCOMPARE(dialog->getPaperUnitBox()->width(), 90);

    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolMeasureTest::fixPanelFields()
{
    // Review RS7CFIGMA L5, 471:5490: Font "Inter" (the font of the theme is the default), the combos of Units 150, Font
    // 170, Size 110 with "10" (no decimals), the opacities 100; 471:6681: the hint of the temporary measurements right
    // under the rows (10 below the list, which is as high as its rows)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getFontBox()->currentText(), QStringLiteral("Inter"));
    QCOMPARE(gui->getFontBox()->currentIndex(), 0);
    QCOMPARE(gui->getFontBox()->width(), 170);
    QVERIFY(gui->getFontBox()->itemData(0).toString().isEmpty());
    int interItems = 0;
    for (int i = 0; i < gui->getFontBox()->count(); ++i)
    {
        interItems += gui->getFontBox()->itemText(i).compare(QStringLiteral("Inter"), Qt::CaseInsensitive) == 0 ? 1 : 0;
    }
    QCOMPARE(interItems, 1);

    QWidget* unitsCard = tab->findChild<QWidget*>(QStringLiteral("measureUnitsCard"));
    QWidget* labelsCard = tab->findChild<QWidget*>(QStringLiteral("measureLabelsCard"));
    QVERIFY(unitsCard && labelsCard);
    const QList<QComboBox*> units = unitsCard->findChildren<QComboBox*>();
    QCOMPARE(units.size(), 3);
    for (const QComboBox* comboBox : units)
    {
        QCOMPARE(comboBox->width(), 150);
    }
    const QList<pdf::PDFDoubleSpinBox*> sizes = labelsCard->findChildren<pdf::PDFDoubleSpinBox*>();
    QCOMPARE(sizes.size(), 1);
    QCOMPARE(sizes.front()->width(), 110);
    QCOMPARE(sizes.front()->textFromValue(sizes.front()->value()), QStringLiteral("10"));
    for (const pdf::PDFSpinBox* opacity : labelsCard->findChildren<pdf::PDFSpinBox*>())
    {
        QCOMPARE(opacity->width(), 100);
    }

    QVERIFY(!gui->getListHint()->isVisible());
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 200), QPointF(172, 200) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    QWidget* window = fixture.window;
    pdf::PDFListView* view = gui->getListView();
    pdf::PDFAppParagraph* hint = gui->getListHint();
    QVERIFY(hint->isVisible());

    // Owner 2026-11-20 (decision 3, Figma 473:6603 changed to it): the measurements live while the tool is open
    QCOMPARE(hint->text(), QStringLiteral("Temporary measurements are lost when the tool is closed. Save them as annotations to keep them in the document."));
    QCOMPARE(view->height(), view->sizeHint().height());
    const int rowsHeight = 2 * (view->sizeHintForRow(0) + 2 * view->spacing());
    QVERIFY2(view->height() >= rowsHeight && view->height() <= rowsHeight + 24, qPrintable(QString::number(view->height())));
    QCOMPARE(hint->mapTo(window, QPoint(0, 0)).y() - view->mapTo(window, QPoint(0, view->height())).y(), 10);
}

void AppToolMeasureTest::retireOverlayLeavesCanvasAtOnce()
{
    // Step A (p2a_common.md, retire steps 2-9): the overlay is retired with its tool; its input and draw interfaces
    // leave the canvas at once (tokens), not at its later deletion - a click in between reaches no inert overlay -,
    // its callout of the canvas area goes with it (setOwner), nothing from the theme or the proxy reaches it
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    PDFDocumentToolShell* shell = H::openSimple(fixture, &tab);
    PDFAppToolGUIMeasure* gui = H::gui(shell);
    QVERIFY(gui && tab);
    QPointer<pdf::PDFAppMeasureOverlay> overlay = gui->getOverlay();
    QVERIFY(overlay);
    QPointer<pdf::PDFAppToolCallout> callout = overlay->getCallout();
    QVERIFY(callout && callout->isVisible());

    pdf::PDFWidget* widget = tab->getView()->getPdfWidget();
    pdf::PDFDrawWidgetProxy* proxy = widget->getDrawWidgetProxy();
    const pdf::IDrawWidgetInputInterface* input = overlay.data();
    const auto inputCount = [&]() { return std::count(widget->getInputInterfaces().cbegin(), widget->getInputInterfaces().cend(), input); };
    QCOMPARE(inputCount(), 1);

    // The draw interface is registered by its token (the set of the proxy is private)
    pdf::PDFAppCanvasDrawToken* drawTokenObject = nullptr;
    for (pdf::PDFLifetimeToken* token : overlay->findChildren<pdf::PDFLifetimeToken*>())
    {
        drawTokenObject = drawTokenObject ? drawTokenObject : dynamic_cast<pdf::PDFAppCanvasDrawToken*>(token);
    }
    QPointer<pdf::PDFLifetimeToken> drawToken = drawTokenObject;
    QVERIFY(drawToken && !drawToken->isReleased());
    QCOMPARE(drawTokenObject->getProxy(), proxy);
    QVERIFY(pdf::PDFDeferredDeletion::getConnectionCount(overlay.data()) >= 3);

    pdf::PDFDeferredDeletion::retire(overlay.data());

    // At once, before the event loop: the overlay is still alive (inert), deleted later
    QVERIFY(overlay);
    QCOMPARE(inputCount(), 0);
    QVERIFY(drawToken && drawToken->isReleased());
    QVERIFY(!drawTokenObject->getProxy());
    QCOMPARE(pdf::PDFDeferredDeletion::getConnectionCount(overlay.data()), 0);
    QVERIFY(callout && pdf::PDFDeferredDeletion::isRetired(callout.data()));
    QVERIFY(!callout->isVisible());

    AppToolShellFixture::settle();
    QVERIFY(!overlay);
    QVERIFY(!callout);
    QCOMPARE(inputCount(), 0);
}

void AppToolMeasureTest::retireToolClosesItsQuestion()
{
    // Step A: the question of askClose belongs to the GUI (setOwner); when the tool goes without an answer (the shell
    // is retired - a close of the tab, MCP), the question closes at once and unanswered: no Save and Close, no
    // Discard reaches the tool, nothing is written into the document
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = nullptr;
    QPointer<PDFDocumentToolShell> shell = H::openSimple(fixture, &tab);
    QPointer<PDFAppToolGUIMeasure> gui = H::gui(shell);
    QVERIFY(gui && tab);
    QVERIFY(gui->addMeasurement(0, PDFMeasureKind::Distance, { QPointF(100, 100), QPointF(172, 100) }, PDFAppMeasureDrawKind::Linear));
    AppToolShellFixture::settle();
    const size_t annotationCount = H::annotations(tab->getDocument()).size();

    QPointer<QDialog> question = gui->askClose();
    QVERIFY(question && question->isVisible());
    QCOMPARE(question->objectName(), QStringLiteral("toolCloseQuestion"));
    QCOMPARE(pdf::PDFDeferredDeletion::getOwner(question.data()), static_cast<QObject*>(gui.data()));
    QSignalSpy finished(question.data(), &QDialog::finished);

    pdf::PDFDeferredDeletion::retire(shell.data());

    QVERIFY(question);
    QVERIFY(pdf::PDFDeferredDeletion::isRetired(question.data()));
    QVERIFY(!question->isVisible());
    QCOMPARE(finished.count(), 0);

    AppToolShellFixture::settle();
    QVERIFY(!question);
    QVERIFY(!gui);
    QCOMPARE(H::annotations(tab->getDocument()).size(), annotationCount);
}

QTEST_MAIN(AppToolMeasureTest)

#include "tst_apptoolmeasuretest.moc"
