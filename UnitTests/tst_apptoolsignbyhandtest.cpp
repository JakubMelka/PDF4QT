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

// The GUI of Sign by Hand (step 6 of phase 2, plan 2.3, 4.5, 9; package P4): the frames 435:8619 ...
// 435:10812 (the start state, the elements by pages, Style, the Primary button Apply to Pages), the
// drawing as the value of the parameter drawing (Undo and Redo of the tool, K6.10), the drawing on the
// page by the creation tools of the scene, the style of the selected elements, Arrange, Clear All and
// the question of the close (435:10500), the scene leaving the view with the tool.

#include "apptestcertificatefolders.h"
#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappcontext.h"
#include "pdfapptoolgui_redact.h"
#include "pdfapptoolgui_signature.h"
#include "pdfapptoolgui_signbyhand.h"
#include "pdfappdocumentmenus.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolpasswordcard.h"
#include "pdfapptoolredactsearchpart.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltimestampauthoritypart.h"
#include "pdfapptooltoolbar.h"
#include "pdfdatabase.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumenttool_redact.h"
#include "pdfdocumenttool_signature.h"
#include "pdfdocumenttool_signbyhand.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdrawwidget.h"
#include "pdfmessagebox.h"
#include "pdfpagecontentelements.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QtTest>

using pdftest::AppDocumentSamples;
using pdftest::AppToolShellFixture;
using pdf::PDFAppToolDrawingPart;
using pdf::PDFAppToolGUISignByHand;
using pdf::PDFDocumentToolShell;
using pdf::PDFPageDrawing;

/// Helpers of the cases
class SignByHandTestHelper
{
public:
    SignByHandTestHelper() = delete;

    static QString toolId() { return QString::fromLatin1(pdf::PDFDocumentToolSignByHand::TOOL_ID); }

    static PDFDocumentToolShell* open(AppToolShellFixture& fixture, int pageCount = 2)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), pageCount));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolShellFixture::settle();
        PDFDocumentToolShell* shell = fixture.openTool(tab, toolId());
        AppToolShellFixture::settle();
        return shell;
    }

    static PDFAppToolGUISignByHand* gui(PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<PDFAppToolGUISignByHand*>(shell->getGUI()) : nullptr;
    }

    /// A text label on page 1, a freehand curve and a horizontal line on page 2 (435:8619)
    static PDFPageDrawing drawing()
    {
        PDFPageDrawing::Element text;
        text.kind = PDFPageDrawing::Kind::Text;
        text.pageIndex = 0;
        text.rectangle = QRectF(300, 100, 120, 20);
        text.text = QStringLiteral("Sep 30, 2026");
        text.style.penColor = QColor(Qt::black);
        text.style.fontSize = 12.0;

        PDFPageDrawing::Element curve;
        curve.kind = PDFPageDrawing::Kind::FreehandCurve;
        curve.pageIndex = 1;
        curve.points = { QPointF(320, 140), QPointF(340, 170), QPointF(360, 150), QPointF(390, 165) };
        curve.style.penColor = QColor(Qt::black);
        curve.style.penWidth = 2.5;

        PDFPageDrawing::Element line;
        line.kind = PDFPageDrawing::Kind::Line;
        line.pageIndex = 1;
        line.points = { QPointF(300, 130), QPointF(500, 130) };
        line.style.penColor = QColor(Qt::black);
        line.style.penWidth = 1.0;
        return PDFPageDrawing({ text, curve, line });
    }

    static QStringList rowTitles(const pdf::PDFAppToolDrawingElementList* list)
    {
        QStringList result;
        for (const QPointer<pdf::PDFTileRow>& row : list->getRows())
        {
            result << (row ? row->getTitle() : QString());
        }
        return result;
    }

    /// The mouse moves over the point of the page (the snapper of a pick tool follows the moves), then
    /// clicks there
    static bool moveAndClick(pdf::PDFAppDocumentView* view, int pageIndex, const QPointF& pagePoint)
    {
        bool ok = false;
        const QPoint point = AppToolShellFixture::pageToWidget(view, pageIndex, pagePoint, &ok);
        QWidget* canvas = AppToolShellFixture::canvasWidget(view);
        if (!ok || !canvas)
        {
            return false;
        }
        QMouseEvent move(QEvent::MouseMove, QPointF(point), canvas->mapToGlobal(QPointF(point)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        return AppToolShellFixture::clickPage(view, pageIndex, pagePoint);
    }

    static QStringList captionTitles(const pdf::PDFAppToolDrawingElementList* list)
    {
        QStringList result;
        for (const QPointer<pdf::PDFTileRow>& row : list->getCaptions())
        {
            result << (row ? row->getTitle() : QString());
        }
        return result;
    }
};

using H = SignByHandTestHelper;

class AppToolSignByHandTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void registered();
    void startState();
    void drawingIsParameterWithUndo();
    void drawingRoundTrip();
    void dotByClickOnPage();
    void styleOfSelection();
    void arrangeAlignLeft();
    void arrangePanelScrolls();
    void clearAllAsks();
    void discardQuestion();
    void discardQuestionAtEscape();
    void discardQuestionAgainAfterKeep();
    void elementButtonsIconOnly();
    void canaryAfterStep6Runs();
    void editText();
    void sceneLeavesView();
    void viewDiesBeforeTool();

    // Review RS6APP
    void escapeKeyFromCanvasAsks();
    void escapeEndsElementTool();
    void escapeCancelsElementDrag();
    void escapeClearsSelection();
    void drawingFrozenWhileRunning();
    void unchangedStyleIsNoStep();
    void svgErrorShown();

    // Review RS6FIGMA
    void screenshots();
};

void AppToolSignByHandTest::initTestCase()
{
    // The widgets as the user sees them (review RS6FIGMA: the cases ran in the platform style)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppToolSignByHandTest::registered()
{
    // PDFAppBuiltinTools::registerAll (the context) registers the tool and its GUI once
    AppToolShellFixture fixture;
    QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(H::toolId()));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(H::toolId()));
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(H::toolId());
    PDFAppToolGUISignByHand::registerSignByHandTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(H::toolId()), tool);
}

void AppToolSignByHandTest::startState()
{
    // 435:8908: nothing drawn - Apply to Pages Disabled, "Nothing drawn yet", "Draw your signature",
    // Style "Applies to new elements", Pen and fill, Not a digital ID signature, Arrange and Clear All
    // Disabled, Undo Disabled without a step (K6.10)
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    QVERIFY(part);

    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Apply to Pages"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getLeftHeader()->getTitleLabel()->text(), QStringLiteral("Elements"));
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("Nothing drawn yet"));
    QVERIFY(gui->getEmptyCard()->isVisible());
    QVERIFY(!part->getElementList()->isVisible());
    QCOMPARE(gui->getRightHeader()->getTitleLabel()->text(), QStringLiteral("Style"));
    QCOMPARE(gui->getRightHeader()->getSubtitleLabel()->text(), QStringLiteral("Applies to new elements"));
    QCOMPARE(part->getStylePanel()->getHeader()->getTitleLabel()->text(), QStringLiteral("Pen and fill"));
    QVERIFY(!part->getStylePanel()->isTextStyleShown());
    QCOMPARE(part->getStylePanel()->getPenColorName()->text(), QStringLiteral("Black"));
    QCOMPARE(part->getStylePanel()->getFillCombo()->currentText(), QStringLiteral("None"));
    QVERIFY(gui->getNotDigitalCallout()->isVisible());
    QCOMPARE(gui->getNotDigitalCallout()->getTitleLabel()->text(), QStringLiteral("Not a digital ID signature"));
    QVERIFY(!gui->getSignedCallout()->isVisible());
    QCOMPARE(part->getCanvasHint()->getText(), QStringLiteral("Pick a tool in the toolbar, then click or drag on the page"));
    QCOMPARE(gui->getStatusText(), QStringLiteral("Pick a tool and draw on the page"));

    // The toolbar 435:8712 ... 435:8756: six element buttons, Shapes, Arrange, Clear All
    for (const PDFAppToolDrawingPart::ElementKind kind : { PDFAppToolDrawingPart::ElementKind::Text, PDFAppToolDrawingPart::ElementKind::FreehandCurve, PDFAppToolDrawingPart::ElementKind::SvgImage,
                                                           PDFAppToolDrawingPart::ElementKind::AcceptMark, PDFAppToolDrawingPart::ElementKind::RejectMark, PDFAppToolDrawingPart::ElementKind::Dot })
    {
        QToolButton* button = part->getElementButton(kind);
        QVERIFY(button);
        QVERIFY(button->isEnabled());

        // 435:8712: an icon button - its title is its tooltip and its accessible name
        QVERIFY(button->text().isEmpty());
        QCOMPARE(button->accessibleName(), PDFAppToolDrawingPart::getKindTitle(kind) + (kind == PDFAppToolDrawingPart::ElementKind::SvgImage ? QStringLiteral("…") : QString()));
    }
    QCOMPARE(part->getShapesPanel()->getButton()->text(), QStringLiteral("Shapes"));
    QCOMPARE(part->getShapesPanel()->getRows().size(), size_t(5));
    QCOMPARE(part->getArrangePanel()->getButton()->text(), QStringLiteral("Arrange"));
    QVERIFY(!part->getArrangePanel()->getButton()->isEnabled());
    QVERIFY(!part->getClearButton()->isEnabled());
    QCOMPARE(part->getArrangePanel()->getRows().size(), size_t(16));
    QVERIFY(!shell->getModel()->canUndo());
    QVERIFY(!part->canUndo());

    // The panels of the shell: 300 / 320 (435:8619)
    QCOMPARE(shell->getLeftPanel()->width(), 300);
    QCOMPARE(shell->getRightPanel()->width(), 320);
}

void AppToolSignByHandTest::drawingIsParameterWithUndo()
{
    // The drawing is the value of the parameter drawing: one step of the model per change, Undo and
    // Redo of the tool give the scene the drawing of the step (plan 4.5, K6.10)
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    pdf::PDFAppToolParameterModel* model = shell->getModel();
    const QString parameter = QString::fromLatin1(pdf::PDFDocumentToolSignByHand::PARAMETER_DRAWING);

    part->setDrawing(H::drawing());
    AppToolShellFixture::settle();
    QCOMPARE(part->getElementCount(), 3);
    QVERIFY(model->canUndo());
    const PDFPageDrawing stored = model->getValue(parameter).value<PDFPageDrawing>();
    QCOMPARE(stored.getElements().size(), size_t(3));

    // 435:8619: Apply to Pages enabled, "3 elements on 2 pages", the rows by pages, the status
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getLeftHeader()->getSubtitleLabel()->text(), QStringLiteral("3 elements on 2 pages"));
    QVERIFY(part->getElementButton(PDFAppToolDrawingPart::ElementKind::Text)->text().isEmpty());
    QVERIFY(!gui->getEmptyCard()->isVisible());
    QCOMPARE(H::captionTitles(part->getElementList()), QStringList({ QStringLiteral("Page 1"), QStringLiteral("Page 2") }));
    QCOMPARE(H::rowTitles(part->getElementList()), QStringList({ QStringLiteral("Text label"), QStringLiteral("Freehand curve"), QStringLiteral("Horizontal line") }));
    QCOMPARE(part->getElementList()->getRows().front()->getDescription(), QStringLiteral("“Sep 30, 2026”"));
    QCOMPARE(gui->getStatusText(), QStringLiteral("3 elements on pages 1 and 2 · they become part of the pages when you apply"));
    QCOMPARE(part->getCanvasHint()->getText(), QStringLiteral("Drag elements to move them · the handles resize · double-click a text label to edit it"));
    QVERIFY(part->getClearButton()->isEnabled());

    // Undo: the scene is empty again, Apply to Pages Disabled; Redo: the three elements again
    model->undo();
    AppToolShellFixture::settle();
    QVERIFY(part->isEmpty());
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(!model->canUndo());
    model->redo();
    AppToolShellFixture::settle();
    QCOMPARE(part->getElementCount(), 3);
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // The drawing of the run is the drawing of the scene
    const PDFPageDrawing run = gui->getRunParameters().value(parameter).value<PDFPageDrawing>();
    QCOMPARE(run.getElements().size(), size_t(3));
    QCOMPARE(run.getPageIndices(), std::vector<pdf::PDFInteger>({ 0, 1 }));
}

void AppToolSignByHandTest::drawingRoundTrip()
{
    // The conversion of plan 10.3 (toDrawing): every kind keeps its geometry, its text and its style
    // through the scene (the coordinates are points of the default user space of the page)
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();

    std::vector<PDFPageDrawing::Element> elements;
    auto add = [&elements](PDFPageDrawing::Kind kind, QRectF rect)
    {
        PDFPageDrawing::Element element;
        element.kind = kind;
        element.pageIndex = 0;
        element.rectangle = rect;
        element.style.penColor = QColor(0xef, 0x44, 0x44);
        element.style.penWidth = 2.0;
        elements.push_back(element);
        return &elements.back();
    };
    add(PDFPageDrawing::Kind::Rectangle, QRectF(10, 20, 100, 40))->style.fillColor = QColor(Qt::white);
    add(PDFPageDrawing::Kind::RoundedRectangle, QRectF(10, 80, 100, 40));
    add(PDFPageDrawing::Kind::AcceptMark, QRectF(200, 20, 24, 24));
    add(PDFPageDrawing::Kind::RejectMark, QRectF(240, 20, 24, 24));
    add(PDFPageDrawing::Kind::Dot, QRectF(298, 18, 4, 4));
    PDFPageDrawing::Element* text = add(PDFPageDrawing::Kind::Text, QRectF(10, 200, 150, 30));
    text->text = QStringLiteral("Jane Novak");
    text->style.fontSize = 13.0;
    text->style.alignment = PDFPageDrawing::Alignment::Center;
    text->style.angle = 15.0;
    PDFPageDrawing::Element* line = add(PDFPageDrawing::Kind::Line, QRectF());
    line->points = { QPointF(10, 300), QPointF(110, 350) };
    PDFPageDrawing::Element* vertical = add(PDFPageDrawing::Kind::Line, QRectF());
    vertical->points = { QPointF(150, 300), QPointF(150, 400) };

    part->setDrawing(PDFPageDrawing(elements));
    AppToolShellFixture::settle();
    const PDFPageDrawing result = part->toDrawing();
    QCOMPARE(result.getElements().size(), elements.size());

    const std::vector<PDFPageDrawing::Element>& out = result.getElements();
    QCOMPARE(out[0].kind, PDFPageDrawing::Kind::Rectangle);
    QCOMPARE(out[0].rectangle, QRectF(10, 20, 100, 40));
    QCOMPARE(out[0].style.fillColor.rgb(), QColor(Qt::white).rgb());
    QCOMPARE(out[0].style.penColor.rgb(), QColor(0xef, 0x44, 0x44).rgb());
    QCOMPARE(out[0].style.penWidth, 2.0);
    QCOMPARE(out[1].kind, PDFPageDrawing::Kind::RoundedRectangle);
    QCOMPARE(out[1].cornerRadius, 10.0);        // a quarter of the shorter side (the scene draws it so)
    QCOMPARE(out[2].kind, PDFPageDrawing::Kind::AcceptMark);
    QCOMPARE(out[2].rectangle, QRectF(200, 20, 24, 24));
    QVERIFY(out[2].svgData.isEmpty());          // the core writes its own mark
    QCOMPARE(out[3].kind, PDFPageDrawing::Kind::RejectMark);
    QCOMPARE(out[4].kind, PDFPageDrawing::Kind::Dot);
    QCOMPARE(out[4].rectangle.center(), QPointF(300, 20));
    QCOMPARE(out[4].rectangle.width(), 4.0);
    QCOMPARE(out[4].style.fillColor.rgb(), out[4].style.penColor.rgb());
    QCOMPARE(out[5].kind, PDFPageDrawing::Kind::Text);
    QCOMPARE(out[5].text, QStringLiteral("Jane Novak"));
    QCOMPARE(out[5].rectangle, QRectF(10, 200, 150, 30));
    QCOMPARE(out[5].style.fontSize, 13.0);
    QCOMPARE(out[5].style.alignment, PDFPageDrawing::Alignment::Center);
    QCOMPARE(out[5].style.angle, 15.0);
    QCOMPARE(out[6].kind, PDFPageDrawing::Kind::Line);
    QCOMPARE(out[6].points, std::vector<QPointF>({ QPointF(10, 300), QPointF(110, 350) }));
    QCOMPARE(part->getElementKind(part->getElementIds()[7]), PDFAppToolDrawingPart::ElementKind::VerticalLine);
}

void AppToolSignByHandTest::dotByClickOnPage()
{
    // The creation tool of the scene over the view: a click on the page creates the dot at the point
    // of the page (the default user space), on that page, as one step of the model
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, 1);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    pdf::PDFAppDocumentView* view = fixture.currentDocumentTab()->getView();

    QTest::mouseClick(part->getElementButton(PDFAppToolDrawingPart::ElementKind::Dot), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(part->getActiveElementKind(), PDFAppToolDrawingPart::ElementKind::Dot);
    QVERIFY(part->getElementButton(PDFAppToolDrawingPart::ElementKind::Dot)->isChecked());

    const QPointF pagePoint(200, 300);
    QVERIFY(H::moveAndClick(view, 0, pagePoint));
    AppToolShellFixture::settle();
    QCOMPARE(part->getElementCount(), 1);
    const PDFPageDrawing drawing = part->toDrawing();
    QCOMPARE(drawing.getElements().front().kind, PDFPageDrawing::Kind::Dot);
    QCOMPARE(drawing.getElements().front().pageIndex, pdf::PDFInteger(0));
    const QPointF center = drawing.getElements().front().rectangle.center();
    QVERIFY2(QLineF(center, pagePoint).length() < 2.0, qPrintable(QStringLiteral("%1 %2").arg(center.x()).arg(center.y())));
    QVERIFY(shell->getModel()->canUndo());
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolSignByHandTest::styleOfSelection()
{
    // Style (435:8874): the selected element shows its style; a color of the swatches changes the
    // selected element as one step and the next element too
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    part->setDrawing(H::drawing());
    AppToolShellFixture::settle();

    // The curve selected: the header "Freehand curve", the pen of the curve
    const pdf::PDFInteger curveId = part->getElementIds()[1];
    part->selectElements({ curveId });
    AppToolShellFixture::settle();
    QCOMPARE(gui->getRightHeader()->getSubtitleLabel()->text(), QStringLiteral("Freehand curve"));
    QVERIFY(!part->getStylePanel()->isTextStyleShown());
    QCOMPARE(part->getStylePanel()->getPenWidthSpin()->value(), 2.5);
    QVERIFY(part->getElementList()->getRows()[1]->isChecked());

    const QColor red = QColor(0xef, 0x44, 0x44);
    pdf::PDFAppToolParameterModel* model = shell->getModel();
    part->setPenColor(red);
    AppToolShellFixture::settle();
    QCOMPARE(part->toDrawing().getElements()[1].style.penColor.rgb(), red.rgb());
    QCOMPARE(part->toDrawing().getElements()[0].style.penColor.rgb(), QColor(Qt::black).rgb());
    QCOMPARE(part->getNextStyle().penColor.rgb(), red.rgb());

    // One step: Undo gives the black curve back
    model->undo();
    AppToolShellFixture::settle();
    QCOMPARE(part->toDrawing().getElements()[1].style.penColor.rgb(), QColor(Qt::black).rgb());

    // The text label selected: the text style (435:10179 "Text")
    part->selectElements({ part->getElementIds()[0] });
    AppToolShellFixture::settle();
    QVERIFY(part->getStylePanel()->isTextStyleShown());
    QCOMPARE(part->getStylePanel()->getHeader()->getTitleLabel()->text(), QStringLiteral("Text"));
    QCOMPARE(part->getStylePanel()->getFontSizeSpin()->value(), 12.0);
}

void AppToolSignByHandTest::arrangeAlignLeft()
{
    // Arrange (434:5772, Align left: "To the leftmost edge") over the selected elements, one step
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();

    PDFPageDrawing::Element first;
    first.kind = PDFPageDrawing::Kind::Rectangle;
    first.rectangle = QRectF(100, 100, 50, 20);
    PDFPageDrawing::Element second = first;
    second.rectangle = QRectF(160, 200, 80, 30);
    part->setDrawing(PDFPageDrawing({ first, second }));
    AppToolShellFixture::settle();

    QVERIFY(!part->getArrangeRow(PDFAppToolDrawingPart::ArrangeOperation::AlignLeft)->isEnabled());
    part->selectElements({ part->getElementIds()[0], part->getElementIds()[1] });
    AppToolShellFixture::settle();
    QVERIFY(part->getArrangeRow(PDFAppToolDrawingPart::ArrangeOperation::AlignLeft)->isEnabled());
    QCOMPARE(part->getArrangeRow(PDFAppToolDrawingPart::ArrangeOperation::AlignLeft)->getDescription(), QStringLiteral("To the leftmost edge"));

    part->arrange(PDFAppToolDrawingPart::ArrangeOperation::AlignLeft);
    AppToolShellFixture::settle();
    const PDFPageDrawing arranged = part->toDrawing();
    const std::vector<PDFPageDrawing::Element>& elements = arranged.getElements();
    QCOMPARE(elements[0].rectangle.left(), 100.0);
    QCOMPARE(elements[1].rectangle.left(), 100.0);
    QCOMPARE(elements[1].rectangle.size(), QSizeF(80, 30));
}

void AppToolSignByHandTest::arrangePanelScrolls()
{
    // RS6FIGMA (434:5421 / 435:9472, coordinator's decision): Arrange has 18 rows; a panel taller than its
    // screen stays inside of the screen and scrolls in one column - every row can be reached
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();

    // Arrange works over a selection of elements (its button is enabled then)
    PDFPageDrawing::Element first;
    first.kind = PDFPageDrawing::Kind::Rectangle;
    first.rectangle = QRectF(100, 100, 50, 20);
    PDFPageDrawing::Element second = first;
    second.rectangle = QRectF(160, 200, 80, 30);
    part->setDrawing(PDFPageDrawing({ first, second }));
    AppToolShellFixture::settle();
    part->selectElements({ part->getElementIds()[0], part->getElementIds()[1] });
    AppToolShellFixture::settle();

    // A screen of 500 points (the offscreen platform has a large one)
    pdf::PDFAppDocumentPanelPopup::setMaximumPanelHeight(500);
    const auto resetHeight = qScopeGuard([]() { pdf::PDFAppDocumentPanelPopup::setMaximumPanelHeight(0); });

    pdf::PDFAppToolPanelPopup* panel = part->getArrangePanel();
    QVERIFY(panel && panel->getButton());
    QVERIFY(!panel->getScrollArea());
    QVERIFY(shell->getToolbar()->openPanel(panel->getButton()));
    AppToolShellFixture::settle();
    QVERIFY(panel->isVisible());
    QVERIFY2(panel->getPanelRect().height() <= 500, qPrintable(QString::number(panel->getPanelRect().height())));

    const QList<pdf::PDFTileRow*> rows = panel->findChildren<pdf::PDFTileRow*>();
    QVERIFY(rows.size() >= 16);
    QScrollArea* scrollArea = panel->getScrollArea();
    QVERIFY(scrollArea);
    QVERIFY(scrollArea->verticalScrollBar()->maximum() > 0);

    // One column: every row has the same left edge; the last row of In a grid is reachable
    int left = -1;
    for (const pdf::PDFTileRow* row : rows)
    {
        left = left < 0 ? row->x() : left;
        QCOMPARE(row->x(), left);
    }
    pdf::PDFTileRow* last = part->getArrangeRow(PDFAppToolDrawingPart::ArrangeOperation::InAGrid);
    QVERIFY(last);
    scrollArea->ensureWidgetVisible(last);
    const QRect lastRect(last->mapTo(scrollArea->viewport(), QPoint(0, 0)), last->size());
    QVERIFY(scrollArea->viewport()->rect().contains(lastRect));

    // A small panel keeps its natural layout
    pdf::PDFAppToolPanelPopup* draw = part->getShapesPanel() ? part->getShapesPanel() : part->getDrawPanel();
    QVERIFY(draw && draw->getButton());
    panel->close();
    AppToolShellFixture::settle();
    QVERIFY(shell->getToolbar()->openPanel(draw->getButton()));
    AppToolShellFixture::settle();
    QVERIFY(!draw->getScrollArea());
}

void AppToolSignByHandTest::clearAllAsks()
{
    // Clear All asks (the style of 435:10500): Keep Drawing keeps it, Clear All removes every element
    // as one step, Undo brings them back
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    part->setDrawing(H::drawing());
    AppToolShellFixture::settle();

    QTest::mouseClick(part->getClearButton(), Qt::LeftButton);
    AppToolShellFixture::settle();
    pdf::PDFMessageBox* dialog = gui->getClearDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getActionButtons().size(), qsizetype(2));
    QCOMPARE(dialog->getActionButtons()[0]->text(), QStringLiteral("Keep Drawing"));
    QCOMPARE(dialog->getActionButtons()[1]->text(), QStringLiteral("Clear All"));
    QTest::mouseClick(dialog->getActionButtons()[0], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!gui->getClearDialog());
    QCOMPARE(part->getElementCount(), 3);

    gui->askClearAll();
    AppToolShellFixture::settle();
    QTest::mouseClick(gui->getClearDialog()->getActionButtons()[1], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(part->isEmpty());
    shell->getModel()->undo();
    AppToolShellFixture::settle();
    QCOMPARE(part->getElementCount(), 3);
}

void AppToolSignByHandTest::discardQuestion()
{
    // 435:10500: "Discard the drawing?", Keep Drawing (Primary) and Discard Drawing (Secondary) in the
    // order of the frame; Discard Drawing closes the tool, the document stays as it was
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    QVERIFY(!gui->askDiscard());    // nothing drawn: nothing to ask
    gui->getDrawingPart()->setDrawing(H::drawing());
    AppToolShellFixture::settle();

    QVERIFY(gui->askDiscard());
    AppToolShellFixture::settle();
    pdf::PDFMessageBox* dialog = gui->getDiscardDialog();
    QVERIFY(dialog);
    const QList<QPushButton*> buttons = dialog->getActionButtons();
    QCOMPARE(buttons.size(), qsizetype(2));
    QCOMPARE(buttons[0]->text(), QStringLiteral("Keep Drawing"));
    QVERIFY(buttons[0]->isDefault());
    QCOMPARE(buttons[1]->text(), QStringLiteral("Discard Drawing"));

    QPointer<PDFDocumentToolShell> guard(shell);
    pdf::PDFAppDocument* document = fixture.currentDocumentTab()->getDocument();
    QTest::mouseClick(buttons[1], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!guard || guard->getState() == PDFDocumentToolShell::State::Closed);
    QVERIFY(!document->isModified());
}

void AppToolSignByHandTest::discardQuestionAtEscape()
{
    // 435:10500 at Cancel / Esc of the shell (PDFDocumentToolGUI::askClose): the tool stays until the
    // answer; Esc again keeps the one question; Keep Drawing returns; Discard Drawing closes the tool and
    // the document stays as it was. Without a drawing Esc closes the tool at once.
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    QPointer<PDFDocumentToolShell> guard(shell);
    pdf::PDFAppDocumentTab* tab = fixture.currentDocumentTab();
    gui->getDrawingPart()->setDrawing(H::drawing());
    AppToolShellFixture::settle();

    shell->cancel();
    AppToolShellFixture::settle();
    QPointer<pdf::PDFMessageBox> dialog = gui->getDiscardDialog();
    QVERIFY(dialog && dialog->isVisible());
    QVERIFY(guard && guard->getState() == PDFDocumentToolShell::State::Editing);
    shell->cancel();
    QCOMPARE(gui->getDiscardDialog(), dialog.data());
    QCOMPARE(AppToolShellFixture::getShell(tab), shell);

    // Keep Drawing
    QTest::mouseClick(dialog->getActionButtons()[0], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!dialog || !dialog->isVisible());
    QVERIFY(guard && guard->getState() == PDFDocumentToolShell::State::Editing);
    QCOMPARE(gui->getDrawingPart()->getElementCount(), 3);

    // Discard Drawing
    shell->cancel();
    AppToolShellFixture::settle();
    dialog = gui->getDiscardDialog();
    QVERIFY(dialog);
    QTest::mouseClick(dialog->getActionButtons()[1], Qt::LeftButton);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
    AppToolShellFixture::settle();
    QVERIFY(!tab->getDocument()->isModified());

    // Nothing drawn: Esc closes the tool without a question
    PDFDocumentToolShell* second = fixture.openTool(tab, H::toolId());
    AppToolShellFixture::settle();
    QVERIFY(second);
    second->cancel();
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
}

void AppToolSignByHandTest::discardQuestionAgainAfterKeep()
{
    // RESCCODE L1: Keep Drawing hides the question, which is deleted only later (WA_DeleteOnClose, deleteLater).
    // An Esc before that asks again: a new visible question, never the answered hidden one (the tool would stay
    // without a question to answer)
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    QPointer<PDFDocumentToolShell> guard(shell);
    gui->getDrawingPart()->setDrawing(H::drawing());
    AppToolShellFixture::settle();

    shell->cancel();
    QPointer<pdf::PDFMessageBox> first = gui->getDiscardDialog();
    QVERIFY(first && first->isVisible());
    first->getActionButtons()[0]->click();      // Keep Drawing, no event loop: the dialog is hidden, not deleted
    QVERIFY(first && !first->isVisible());

    shell->cancel();
    pdf::PDFMessageBox* second = gui->getDiscardDialog();
    QVERIFY(second);
    QVERIFY(second->isVisible());
    QVERIFY(second != first.data());
    QVERIFY(guard->getCloseQuestion() == second);
    QVERIFY(guard->getState() == PDFDocumentToolShell::State::Editing);

    // Its Discard Drawing closes the tool
    QTest::mouseClick(second->getActionButtons()[1], Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(!guard || guard->getState() == PDFDocumentToolShell::State::Closed);
}

void AppToolSignByHandTest::elementButtonsIconOnly()
{
    // Toolbar button 190:538 (435:8712 ... 435:8739): the element buttons are icon buttons 36 x 36 with
    // the icon 20 at every width of the toolbar (the other actions lose their texts only when narrow)
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(shell->getToolbar()->getCollapseLevel(), 0);
    QToolButton* button = gui->getDrawingPart()->getElementButton(PDFAppToolDrawingPart::ElementKind::Text);
    QVERIFY(button);
    QVERIFY(pdf::PDFWidgetStyleProperties::isSet(button, pdf::PDFWidgetStyleProperties::ICON_ONLY));
    QCOMPARE(button->iconSize(), QSize(20, 20));
    QCOMPARE(button->toolButtonStyle(), Qt::ToolButtonIconOnly);

    // Shapes keeps its text and the icon 18 of a menu button at the full width
    QToolButton* shapes = gui->getDrawingPart()->getShapesPanel()->getButton();
    QCOMPARE(shapes->iconSize(), QSize(18, 18));
    QCOMPARE(shapes->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
}

/// The messages of the application (qDebug, qWarning, ...) during the canary
class Step6CanaryLog
{
public:
    Step6CanaryLog() = delete;

    static QStringList& messages()
    {
        static QStringList value;
        return value;
    }

    static void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
    {
        Q_UNUSED(type);
        Q_UNUSED(context);
        messages() << message;
    }

    /// Types the text as the user does (a key event per character)
    static void type(QWidget* widget, const QString& text)
    {
        for (const QChar character : text)
        {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(character));
            QApplication::sendEvent(widget, &press);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
            QApplication::sendEvent(widget, &release);
        }
        AppToolShellFixture::settle();
    }

    /// The database exported whole (the last values, Recent tools, every table) has no canary (UTF-8, UTF-16)
    static bool isDatabaseClean(AppToolShellFixture& fixture, const QString& canary)
    {
        const QString exported = fixture.path(QStringLiteral("step6-canary.pdf4qtbackup"));
        QFile::remove(exported);
        if (fixture.window->getContext()->getDatabase()->exportBackup(exported).isError())
        {
            return false;
        }
        QFile backup(exported);
        if (!backup.open(QIODevice::ReadOnly))
        {
            return false;
        }
        const QByteArray bytes = backup.readAll();
        const QByteArray utf16(reinterpret_cast<const char*>(canary.utf16()), canary.size() * 2);
        return !bytes.isEmpty() && !bytes.contains(canary.toUtf8()) && !bytes.contains(utf16);
    }
};

void AppToolSignByHandTest::canaryAfterStep6Runs()
{
    // Plan of step 6 of phase 2, 6.2, R6-11, R6-19 (P2S6INTAPP item 11): after a run of Sign by Hand, Sign
    // with Certificate, Add Timestamp and Redact Content nothing typed or drawn as a secret - the password
    // of the .pfx file, the text of the drawing, the search term of the redaction - is in the database
    // (the last values of the tools, Recent tools, every table) or in a message of the log
    const QString canary = QStringLiteral("Canary6Q2xStep6");
    Step6CanaryLog::messages().clear();
    const QtMessageHandler previousHandler = qInstallMessageHandler(&Step6CanaryLog::handler);

    pdftest::AppTestCertificateFolders folders;
    QVERIFY(!pdftest::AppTestCertificateFolders::create(folders.getApplicationFolder(), QStringLiteral("canary.pfx"), QStringLiteral("Canary Signer"), canary + QStringLiteral("-password"), 2048).isEmpty());
    AppToolShellFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* tab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Canary.pdf"), 2));
    QVERIFY(tab && AppToolShellFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }));
    AppToolShellFixture::settle();

    // 1. Sign by Hand: a text label of the canary, applied and kept in the tab
    {
        QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, H::toolId());
        AppToolShellFixture::settle();
        PDFAppToolGUISignByHand* gui = H::gui(shell);
        QVERIFY(gui);
        PDFPageDrawing::Element text;
        text.kind = PDFPageDrawing::Kind::Text;
        text.pageIndex = 0;
        text.rectangle = QRectF(300, 100, 160, 20);
        text.text = canary + QStringLiteral("-drawn");
        text.style.penColor = QColor(Qt::black);
        text.style.fontSize = 12.0;
        gui->getDrawingPart()->setDrawing(PDFPageDrawing({ text }));
        AppToolShellFixture::settle();
        QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell || shell->getResultDialog(); }));
        QVERIFY(shell);
        shell->getResultDialog()->confirm();
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
        AppToolShellFixture::settle();
        QVERIFY(tab->getDocument()->isModified());
    }

    // 2. Sign with Certificate: the password of the .pfx file typed into its field; the signed copy saved
    {
        QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, pdf::PDFDocumentToolSignature::getToolId(pdf::PDFDocumentToolSignature::Mode::Sign));
        AppToolShellFixture::settle();
        pdf::PDFAppToolGUISignature* gui = shell ? qobject_cast<pdf::PDFAppToolGUISignature*>(shell->getGUI()) : nullptr;
        QVERIFY(gui);
        QVERIFY(shell->getModel()->setValues({ { QString::fromLatin1(pdf::PDFDocumentToolSignature::PARAMETER_TYPE), QString::fromLatin1(pdf::PDFDocumentToolSignature::TYPE_SIGNATURE) },
                                               { QString::fromLatin1(pdf::PDFDocumentToolSignature::PARAMETER_APPEARANCE), QString::fromLatin1(pdf::PDFDocumentToolSignature::APPEARANCE_INVISIBLE) } }).isEmpty());
        Step6CanaryLog::type(gui->getPasswordCard()->getPasswordField(), canary + QStringLiteral("-password"));
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getPasswordState() != pdf::PDFAppToolGUISignature::PasswordState::NotChecked; }));
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getToolbar()->getConfirmButton()->isEnabled(); }));
        QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell || shell->getResultDialog(); }, 60000));
        QVERIFY(shell);
        QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
        QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::FileOnly);
        dialog->getOpenCheckBox()->setChecked(false);
        dialog->confirm();
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
        AppToolShellFixture::settle();
        QVERIFY(QFileInfo::exists(fixture.path(QStringLiteral("Canary_SIGNED.pdf"))));
    }

    // 3. Add Timestamp: the authority (a local address, no internet) refuses - the run ends with its error
    {
        QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, pdf::PDFDocumentToolSignature::getToolId(pdf::PDFDocumentToolSignature::Mode::Timestamp));
        AppToolShellFixture::settle();
        pdf::PDFAppToolGUISignature* gui = shell ? qobject_cast<pdf::PDFAppToolGUISignature*>(shell->getGUI()) : nullptr;
        QVERIFY(gui);
        gui->getAuthorityPart()->setUrl(QStringLiteral("http://127.0.0.1:9/tsr"));
        AppToolShellFixture::settle();
        QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell || shell->getState() == PDFDocumentToolShell::State::Editing; }, 60000));
        fixture.closeMessageBoxes();
        AppToolShellFixture::settle();
        if (shell)
        {
            shell->cancel();
            AppToolShellFixture::settle();
        }
    }

    // 4. Redact Content: the search term, a mark of a match and of an area; the copy saved
    {
        QPointer<PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolRedact::TOOL_ID));
        AppToolShellFixture::settle();
        pdf::PDFAppToolGUIRedact* gui = shell ? qobject_cast<pdf::PDFAppToolGUIRedact*>(shell->getGUI()) : nullptr;
        QVERIFY(gui);
        gui->setMode(pdf::PDFAppToolGUIRedact::Mode::Search);
        pdf::PDFLineEdit* field = gui->getSearchPart()->getField();
        field->setText(canary + QStringLiteral("-search"));
        QTest::keyClick(field, Qt::Key_Return);
        QVERIFY(AppToolShellFixture::waitFor([gui]() { return gui->getSearchPart()->getState() == pdf::PDFAppToolRedactSearchPart::State::Finished; }));
        gui->setMode(pdf::PDFAppToolGUIRedact::Mode::None);
        QVERIFY(gui->addAreaMark(0, QRectF(20, 20, 40, 40)));
        AppToolShellFixture::settle();
        QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return !shell || shell->getResultDialog(); }, 60000));
        QVERIFY(shell);
        QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
        dialog->getOpenCheckBox()->setChecked(false);
        dialog->confirm();
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return !AppToolShellFixture::getShell(tab); }));
        AppToolShellFixture::settle();
    }

    qInstallMessageHandler(previousHandler);
    QVERIFY(Step6CanaryLog::isDatabaseClean(fixture, canary));
    for (const QString& message : Step6CanaryLog::messages())
    {
        QVERIFY2(!message.contains(canary), qPrintable(message));
    }
}

void AppToolSignByHandTest::editText()
{
    // R6-24 (the old Edit Item): the text of a label changes as one step; a double click on its row
    // opens the form of the text
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    part->setDrawing(H::drawing());
    AppToolShellFixture::settle();

    const pdf::PDFInteger textId = part->getElementIds()[0];
    QVERIFY(!part->setElementText(part->getElementIds()[1], QStringLiteral("no")));
    QVERIFY(part->setElementText(textId, QStringLiteral("Jane Novak")));
    AppToolShellFixture::settle();
    QCOMPARE(part->toDrawing().getElements()[0].text, QStringLiteral("Jane Novak"));
    QCOMPARE(part->getElementList()->getRows().front()->getDescription(), QStringLiteral("“Jane Novak”"));

    QTest::mouseDClick(part->getElementList()->getRows().front(), Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(part->getEditDialog());
    pdf::PDFLineEdit* field = part->getEditDialog()->findChild<pdf::PDFLineEdit*>(QStringLiteral("drawingEditTextField"));
    QVERIFY(field);
    QCOMPARE(field->text(), QStringLiteral("Jane Novak"));
    field->setText(QStringLiteral("J. Novak"));
    qobject_cast<QDialog*>(part->getEditDialog())->accept();
    AppToolShellFixture::settle();
    QCOMPARE(part->toDrawing().getElements()[0].text, QStringLiteral("J. Novak"));
}

void AppToolSignByHandTest::sceneLeavesView()
{
    // Ownership (7.2): the scene is an input interface of the widget of the view while the tool is
    // open and leaves it with the tool (the view would keep its address)
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFPageContentScene* scene = gui->getDrawingPart()->getScene();
    pdf::PDFWidget* widget = fixture.currentDocumentTab()->getView()->getPdfWidget();
    const auto& interfaces = widget->getInputInterfaces();
    QVERIFY(std::find(interfaces.cbegin(), interfaces.cend(), scene) != interfaces.cend());
    QVERIFY(scene->isActive());

    shell->cancel();
    AppToolShellFixture::settle();
    QVERIFY(AppToolShellFixture::waitFor([widget, scene]() { const auto& list = widget->getInputInterfaces(); return std::find(list.cbegin(), list.cend(), scene) == list.cend(); }));
}

void AppToolSignByHandTest::viewDiesBeforeTool()
{
    // ASan A: the view (its widget, the proxy is a child of it) dies while the tool is open. The slot of
    // the destroyed proxy only forgets it (CLAUDE.md lifetime); the scene reports its deactivation once,
    // when the part leaves the view after the widget is gone - never to a widget inside of its destructor
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    QPointer<PDFAppToolDrawingPart> part = gui->getDrawingPart();
    QVERIFY(part);
    QPointer<pdf::PDFPageContentScene> scene = part->getScene();
    QVERIFY(scene && scene->isActive());
    QPointer<pdf::PDFWidget> widget = fixture.currentDocumentTab()->getView()->getPdfWidget();
    QVERIFY(widget);

    // (activity, the widget was still there)
    std::vector<std::pair<bool, bool>> reports;
    QObject context;
    QObject::connect(scene.data(), &pdf::PDFPageContentScene::sceneActiveStateChanged, &context, [&reports, &widget](bool isActive) { reports.emplace_back(isActive, !widget.isNull()); });

    delete fixture.currentDocumentTab();
    QVERIFY(!widget);
    if (part)
    {
        QVERIFY(!part->isAttached());
        QVERIFY(scene);
        QVERIFY(!scene->widget());
        delete part.data();
    }

    QCOMPARE(reports.size(), size_t(1));
    QVERIFY(!reports.front().first);
    QVERIFY(!reports.front().second);
}

void AppToolSignByHandTest::escapeKeyFromCanvasAsks()
{
    // Review RS6APP: Esc pressed on the page (the focus in the canvas, where the user draws) is the Esc
    // of the shell - with a drawing the question 435:10500 opens (the scene of the drawing must not take
    // the shortcut of Esc, Delete or Select All when it has nothing to do with it)
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    gui->getDrawingPart()->setDrawing(H::drawing());
    AppToolShellFixture::settle();

    pdf::PDFAppDocumentTab* tab = fixture.currentDocumentTab();
    pdf::PDFWidget* widget = tab->getView()->getPdfWidget();
    widget->setFocus();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    QWidget* focus = QApplication::focusWidget();
    QVERIFY(focus && (focus == widget || widget->isAncestorOf(focus)));
    QTest::keyClick(focus, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QPointer<pdf::PDFMessageBox> dialog = gui->getDiscardDialog();
    QVERIFY(dialog && dialog->isVisible());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(gui->getDrawingPart()->getElementCount(), 3);
}

void AppToolSignByHandTest::escapeEndsElementTool()
{
    // Owner 2026-11-17: Esc with an active creation tool of the drawing ends the tool only (the button
    // unchecked, no question, the tool stays open); the next Esc is the Esc of the tool (the question
    // 435:10500 with a drawing)
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    part->setDrawing(H::drawing());
    AppToolShellFixture::settle();
    QTest::mouseClick(part->getElementButton(PDFAppToolDrawingPart::ElementKind::Dot), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(part->getActiveElementKind(), PDFAppToolDrawingPart::ElementKind::Dot);

    pdf::PDFWidget* widget = fixture.currentDocumentTab()->getView()->getPdfWidget();
    widget->setFocus();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    QWidget* focus = QApplication::focusWidget();
    QVERIFY(focus);
    QTest::keyClick(focus, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!part->getActiveElementKind().has_value());
    QVERIFY(!part->getElementButton(PDFAppToolDrawingPart::ElementKind::Dot)->isChecked());
    QVERIFY(!gui->getDiscardDialog());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(part->getElementCount(), 3);

    focus = QApplication::focusWidget();
    QVERIFY(focus);
    QTest::keyClick(focus, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QPointer<pdf::PDFMessageBox> dialog = gui->getDiscardDialog();
    QVERIFY(dialog && dialog->isVisible());
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolSignByHandTest::escapeCancelsElementDrag()
{
    // Owner 2026-11-17: Esc during a drag of an element of the drawing (the scene takes the key from the
    // shortcut of the tool) puts the element back; the tool stays open, no question
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    PDFPageDrawing::Element text;
    text.kind = PDFPageDrawing::Kind::Text;
    text.pageIndex = 0;
    text.rectangle = QRectF(150, 280, 120, 30);
    text.text = QStringLiteral("Signed");
    text.style.penColor = QColor(Qt::black);
    text.style.fontSize = 12.0;
    part->setDrawing(PDFPageDrawing({ text }));
    AppToolShellFixture::settle();

    pdf::PDFAppDocumentView* view = fixture.currentDocumentTab()->getView();
    const QPointF center(210, 295);
    QVERIFY(H::moveAndClick(view, 0, center));
    AppToolShellFixture::settle();

    pdf::PDFWidget* widget = view->getPdfWidget();
    widget->setFocus();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    QWidget* canvas = AppToolShellFixture::canvasWidget(view);
    bool ok = false;
    const QPoint start = AppToolShellFixture::pageToWidget(view, 0, center, &ok);
    QVERIFY(ok);
    const QPoint end = start + QPoint(60, 40);

    // A drag with Esc in its middle (\p isEscaped) or without it
    auto drag = [&](bool isEscaped)
    {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(start), canvas->mapToGlobal(QPointF(start)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &press);
        for (int i = 1; i <= 4; ++i)
        {
            const QPointF position = QPointF(start) + QPointF(end - start) * (i / 4.0);
            QMouseEvent move(QEvent::MouseMove, position, canvas->mapToGlobal(position), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas, &move);
        }
        if (isEscaped)
        {
            QWidget* focus = QApplication::focusWidget();
            QTest::keyClick(focus ? focus : canvas, Qt::Key_Escape);
            AppToolShellFixture::settle();
        }
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end), canvas->mapToGlobal(QPointF(end)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &release);
        AppToolShellFixture::settle();
    };

    drag(true);
    QCOMPARE(part->toDrawing().getElements().front().rectangle, text.rectangle);
    QVERIFY(!gui->getDiscardDialog());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);

    // The same drag without Esc moves the element (the drag above was a drag)
    QVERIFY(H::moveAndClick(view, 0, center));
    AppToolShellFixture::settle();
    drag(false);
    QVERIFY(part->toDrawing().getElements().front().rectangle != text.rectangle);
}

void AppToolSignByHandTest::escapeClearsSelection()
{
    // Owner 2026-11-18: one Esc ends only the first of a drag, the creation tool, the selection and the
    // tool. With elements selected (in the scene and in the list) Esc deselects them, the drawing and
    // the tool stay; the next Esc is the Esc of the tool (the question 435:10500). The hint of the tool
    // (getEscapeHint) follows the same order.
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    part->setDrawing(H::drawing());
    AppToolShellFixture::settle();
    const std::vector<pdf::PDFInteger> ids = part->getElementIds();
    QVERIFY(ids.size() >= 2);
    part->selectElements({ ids[0], ids[1] });
    AppToolShellFixture::settle();
    QCOMPARE(part->getSelectedElements().size(), size_t(2));
    QCOMPARE(gui->getEscapeHint(), QStringLiteral("Esc clears the selection"));

    // A creation tool with a selection: the tool first
    QTest::mouseClick(part->getElementButton(PDFAppToolDrawingPart::ElementKind::Dot), Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(gui->getEscapeHint(), QStringLiteral("Esc ends Dot"));
    pdf::PDFWidget* widget = fixture.currentDocumentTab()->getView()->getPdfWidget();
    widget->setFocus();
    QVERIFY(QTest::qWaitForWindowActive(fixture.window.data()));
    QWidget* focus = QApplication::focusWidget();
    QVERIFY(focus);
    QTest::keyClick(focus, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(!part->getActiveElementKind().has_value());

    // Elements selected (also after the tool ended): Esc deselects them in the scene and in the list
    part->selectElements({ ids[0], ids[1] });
    AppToolShellFixture::settle();
    QCOMPARE(gui->getEscapeHint(), QStringLiteral("Esc clears the selection"));
    int checkedRows = 0;
    for (const QPointer<pdf::PDFTileRow>& row : part->getElementList()->getRows())
    {
        checkedRows += row && row->isChecked() ? 1 : 0;
    }
    QCOMPARE(checkedRows, 2);
    focus = QApplication::focusWidget();
    QVERIFY(focus);
    QTest::keyClick(focus, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QVERIFY(part->getSelectedElements().empty());
    for (const QPointer<pdf::PDFTileRow>& row : part->getElementList()->getRows())
    {
        QVERIFY(row && !row->isChecked());
    }
    QVERIFY(!gui->getDiscardDialog());
    QCOMPARE(shell->getState(), PDFDocumentToolShell::State::Editing);
    QCOMPARE(part->getElementCount(), 3);
    QCOMPARE(gui->getEscapeHint(), QStringLiteral("Esc leaves the tool"));

    // Nothing to end: the Esc of the tool (its question with a drawing)
    focus = QApplication::focusWidget();
    QVERIFY(focus);
    QTest::keyClick(focus, Qt::Key_Escape);
    AppToolShellFixture::settle();
    QPointer<pdf::PDFMessageBox> dialog = gui->getDiscardDialog();
    QVERIFY(dialog && dialog->isVisible());
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolSignByHandTest::drawingFrozenWhileRunning()
{
    // Review RS6APP (K6.8): while the tool runs, the drawing on the page is no input - the creation tool
    // ends, the element buttons are Disabled, a click on the page creates nothing (the run has taken the
    // drawing already); afterwards the drawing takes the input again
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture, 1);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    pdf::PDFAppDocumentView* view = fixture.currentDocumentTab()->getView();
    QToolButton* dotButton = part->getElementButton(PDFAppToolDrawingPart::ElementKind::Dot);
    QTest::mouseClick(dotButton, Qt::LeftButton);
    AppToolShellFixture::settle();
    QCOMPARE(part->getActiveElementKind(), PDFAppToolDrawingPart::ElementKind::Dot);

    // The shell disables the content of the tool at the start of the run (onRunStarted)
    gui->setContentEnabled(false);
    AppToolShellFixture::settle();
    QVERIFY(!part->getActiveElementKind());
    QVERIFY(!dotButton->isEnabled());
    part->startElement(PDFAppToolDrawingPart::ElementKind::Dot);
    QVERIFY(!part->getActiveElementKind());
    QVERIFY(H::moveAndClick(view, 0, QPointF(200, 300)));
    AppToolShellFixture::settle();
    QCOMPARE(part->getElementCount(), 0);
    QVERIFY(!shell->getModel()->canUndo());

    // The end of the run
    gui->setContentEnabled(true);
    AppToolShellFixture::settle();
    QVERIFY(dotButton->isEnabled());
    QTest::mouseClick(dotButton, Qt::LeftButton);
    AppToolShellFixture::settle();
    QVERIFY(H::moveAndClick(view, 0, QPointF(200, 300)));
    AppToolShellFixture::settle();
    QCOMPARE(part->getElementCount(), 1);
}

void AppToolSignByHandTest::unchangedStyleIsNoStep()
{
    // Review RS6APP (K6.10: Undo only for a change): a style the selected element has already (the
    // focus leaving a spin box, the same swatch again) is no step of Undo
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();
    part->setDrawing(H::drawing());
    AppToolShellFixture::settle();
    pdf::PDFAppToolParameterModel* model = shell->getModel();
    const quint64 steps = model->getRecordedStepCount();

    part->selectElements({ part->getElementIds()[1] });
    AppToolShellFixture::settle();
    part->setPenWidth(2.5);
    part->setPenColor(QColor(Qt::black));
    AppToolShellFixture::settle();
    QCOMPARE(model->getRecordedStepCount(), steps);

    // A real change is one step
    part->setPenWidth(4.0);
    AppToolShellFixture::settle();
    QCOMPARE(model->getRecordedStepCount(), steps + 1);
    QCOMPARE(part->toDrawing().getElements()[1].style.penWidth, 4.0);
}

void AppToolSignByHandTest::svgErrorShown()
{
    // Review RS6APP: an SVG image, which cannot be used (plan 7.2 - here an empty file), says why in a
    // message; no tool is active and its button is not checked
    AppToolShellFixture fixture(QSize(1440, 960));
    PDFDocumentToolShell* shell = H::open(fixture);
    PDFAppToolGUISignByHand* gui = H::gui(shell);
    QVERIFY(gui);
    PDFAppToolDrawingPart* part = gui->getDrawingPart();

    const QString path = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("empty.svg"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    pdf::PDFAppPanelFileDialogs::setOpenHandler([path](QWidget*, const QString&, const QString&, const QString&) { return path; });
    const QScopeGuard restore([]() { pdf::PDFAppPanelFileDialogs::setOpenHandler(pdf::PDFAppPanelFileDialogs::FileHandler()); });

    QToolButton* button = part->getElementButton(PDFAppToolDrawingPart::ElementKind::SvgImage);
    QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
    AppToolShellFixture::settle();
    pdf::PDFMessageBox* dialog = fixture.window->findChild<pdf::PDFMessageBox*>(QStringLiteral("drawingSvgErrorDialog"));
    QVERIFY(dialog && dialog->isVisible());
    QStringList texts;
    for (const QLabel* label : dialog->findChildren<QLabel*>())
    {
        texts << label->text();
    }
    QVERIFY2(texts.contains(QStringLiteral("The SVG image is empty.")), qPrintable(texts.join(QLatin1Char('|'))));
    QVERIFY(!part->getActiveElementKind());
    QVERIFY(!button->isChecked());
    dialog->reject();
    AppToolShellFixture::settle();
}

void AppToolSignByHandTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS6FIGMA): the states of Sign by Hand under the ids of
    // their frames (435:8619 ... 435:10812) in both themes, at 1x / 2x and right-to-left, for the
    // comparison with Figma; no pixel of the leak color in any of them (Fluent)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    for (const char* variant : { "light", "dark", "rtl" })
    {
        const QString variantName = QString::fromLatin1(variant);
        const QString suffix = variantName == QStringLiteral("light") ? QString() : QStringLiteral("-") + variantName;
        QApplication::setLayoutDirection(variantName == QStringLiteral("rtl") ? Qt::RightToLeft : Qt::LeftToRight);
        const QScopeGuard guard([]() { QApplication::setLayoutDirection(Qt::LeftToRight); pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light); });

        AppToolShellFixture fixture(QSize(1440, 960));
        pdf::PDFThemeManager::getInstance()->setScheme(variantName == QStringLiteral("dark") ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
        AppToolShellFixture::settle();
        QStringList leaks;
        const auto shot = [&fixture, &suffix, &leaks](const QString& frame)
        {
            AppToolShellFixture::settle();
            if (AppToolShellFixture::countLeakPixels(fixture.screenshot(frame + suffix)) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto shotWidget = [&suffix, &leaks](QWidget* widget, const QString& frame)
        {
            AppToolShellFixture::settle();
            AppToolShellFixture::screenshotDialog(widget, frame + suffix);
            if (widget && AppToolShellFixture::countLeakPixels(AppToolShellFixture::render(widget, 1.0)) > 0)
            {
                leaks << frame + suffix;
            }
        };
        const auto openMenu = [&shot, &shotWidget](QToolButton* button, const QString& frame)
        {
            QVERIFY(button);
            QTest::mouseClick(button, Qt::LeftButton);
            AppToolShellFixture::settle();
            shot(frame);
            if (QWidget* popup = QApplication::activePopupWidget())
            {
                shotWidget(popup, frame + QStringLiteral("-popup"));
                popup->close();
            }
            AppToolShellFixture::settle();
        };

        PDFDocumentToolShell* shell = H::open(fixture);
        PDFAppToolGUISignByHand* gui = H::gui(shell);
        QVERIFY(gui);
        PDFAppToolDrawingPart* part = gui->getDrawingPart();
        shot(QStringLiteral("435-8908"));

        part->setDrawing(H::drawing());
        AppToolShellFixture::settle();
        const std::vector<pdf::PDFInteger> ids = part->getElementIds();
        QCOMPARE(ids.size(), size_t(3));
        part->selectElements({ ids[1] });
        shot(QStringLiteral("435-8619"));
        openMenu(part->getShapesPanel()->getButton(), QStringLiteral("435-9472-shapes"));
        openMenu(part->getArrangePanel()->getButton(), QStringLiteral("435-9472-arrange"));
        part->selectElements({ ids[0] });
        shot(QStringLiteral("435-10179"));

        QVERIFY(gui->askDiscard());
        AppToolShellFixture::settle();
        QVERIFY(gui->getDiscardDialog());
        shot(QStringLiteral("435-10500"));
        shotWidget(gui->getDiscardDialog(), QStringLiteral("435-10500-dialog"));
        QTest::mouseClick(gui->getDiscardDialog()->getActionButtons()[0], Qt::LeftButton);
        AppToolShellFixture::settle();

        QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
        shell->confirm();
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() && shell->getResultDialog()->isVisible(); }, 60000));
        shot(QStringLiteral("435-10812"));
        shotWidget(shell->getResultDialog(), QStringLiteral("435-10812-dialog"));
        shell->getResultDialog()->reject();
        AppToolShellFixture::settle();

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolSignByHandTest)

#include "tst_apptoolsignbyhandtest.moc"
