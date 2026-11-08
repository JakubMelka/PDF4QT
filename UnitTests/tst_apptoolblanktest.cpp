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

// The GUI of Blank PDF (convert.blank, step 2 of phase 2, package D): the frames 337:1976,
// 337:2221, 356:9975, 1390:47108 and the start page 1392:4528, the size menu and the open
// documents (K27), the limits of the fields (K26), the replacement of the own tab by the new
// document (4.6), the keyboard, the language, both themes, right-to-left, 2x, MCP.

#include "apptoolowntabfixture.h"
#include "appmcpfixture.h"

#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppagesizepreview.h"
#include "pdfapproute.h"
#include "pdfapptoolgui_blank.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptooltab.h"
#include "pdfapptooltoolbar.h"
#include "pdftoolstartpage.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttool_merge.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolregistry.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include <cmath>

using pdf::PDFAppBlankToolGUI;
using pdf::PDFDocumentToolBlank;
using pdf::PDFDocumentToolShell;
using pdftest::AppToolOwnTabFixture;

/// Helpers of the tests of Blank PDF
class BlankGuiHelper
{
public:
    BlankGuiHelper() = delete;

    static inline const QString TOOL_ID = QStringLiteral("convert.blank");

    /// Points of millimeters (the definition of the unit: 1 in = 25.4 mm = 72 points)
    static double points(double millimeters) { return millimeters * 72.0 / 25.4; }

    /// The own tab of Blank PDF with its shell in the state Editing
    static pdf::PDFAppToolTab* openBlank(AppToolOwnTabFixture& fixture)
    {
        pdf::PDFAppToolTab* tab = fixture.createToolTab(TOOL_ID);
        if (!tab || !AppToolOwnTabFixture::getShell(tab) || AppToolOwnTabFixture::getShell(tab)->getState() != PDFDocumentToolShell::State::Editing)
        {
            return nullptr;
        }
        AppToolOwnTabFixture::settle();
        return tab;
    }

    static PDFAppBlankToolGUI* gui(const pdf::PDFAppToolTab* tab)
    {
        PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        return shell ? qobject_cast<PDFAppBlankToolGUI*>(shell->getGUI()) : nullptr;
    }

    static QRect inWindow(const QWidget* widget)
    {
        return QRect(widget->mapTo(widget->window(), QPoint(0, 0)), widget->size());
    }

    static QString rowValue(const pdf::PDFKeyValueList* list, const QString& key)
    {
        for (int i = 0; list && i < list->getRowCount(); ++i)
        {
            if (list->getKey(i) == key)
            {
                return list->getValue(i);
            }
        }
        return QString();
    }

    /// Opens the panel of the size by the button and waits for it
    static pdf::PDFAppBlankSizePanel* openSizePanel(PDFAppBlankToolGUI* gui)
    {
        QToolButton* button = gui ? gui->getSizeButton() : nullptr;
        if (!button)
        {
            return nullptr;
        }
        QTest::mouseClick(button, Qt::LeftButton);
        pdf::PDFAppBlankSizePanel* panel = gui->getSizePanel();
        if (!AppToolOwnTabFixture::waitFor([panel]() { return panel && panel->isVisible(); }))
        {
            return nullptr;
        }
        return panel;
    }

    /// Chooses the size by its row in the open panel
    static bool chooseSize(PDFAppBlankToolGUI* gui, const QString& sizeId)
    {
        if (!openSizePanel(gui))
        {
            return false;
        }
        pdf::PDFTileRow* row = gui->getSizeRow(sizeId);
        if (!row)
        {
            return false;
        }
        QTest::mouseClick(row, Qt::LeftButton);
        AppToolOwnTabFixture::settle();
        return !gui->getSizePanel()->isVisible();
    }

    /// Writes the text into the field as the user does (the old text selected) and leaves it by Enter
    static void typeIntoField(QLineEdit* field, const QString& text, bool isEnter = true)
    {
        field->setFocus(Qt::OtherFocusReason);
        field->selectAll();
        QTest::keyClicks(field, text);
        if (isEnter)
        {
            QTest::keyClick(field, Qt::Key_Return);
        }
        AppToolOwnTabFixture::settle();
    }

    /// A document of pages of the sizes in points (each with its /Rotate)
    static QString writeDocument(const QString& filePath, const std::vector<std::pair<QSizeF, pdf::PageRotation>>& pages)
    {
        pdf::PDFDocumentBuilder builder;
        for (const auto& [size, rotation] : pages)
        {
            const pdf::PDFObjectReference page = builder.appendPage(QRectF(QPointF(0, 0), size));
            if (rotation != pdf::PageRotation::None)
            {
                builder.setPageRotation(page, rotation);
            }
        }
        pdf::PDFDocument document = builder.build();
        return pdftest::AppDocumentSamples::write(document, filePath) ? filePath : QString();
    }

    static pdf::PDFAppDocumentTab* openReady(pdftest::AppDocumentFixture& fixture, const QString& filePath)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(filePath);
        if (!tab || !AppToolOwnTabFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolOwnTabFixture::settle();
        return tab;
    }

    /// Presses Create PDF and waits, until the own tab is replaced by the tab of the new document
    static pdf::PDFAppDocumentTab* createPdf(AppToolOwnTabFixture& fixture, pdf::PDFAppToolTab* toolTab)
    {
        QPointer<pdf::PDFAppToolTab> guard(toolTab);
        PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(toolTab);
        if (!shell || !shell->getToolbar()->getConfirmButton()->isEnabled())
        {
            return nullptr;
        }
        QTest::mouseClick(shell->getToolbar()->getConfirmButton(), Qt::LeftButton);
        pdf::PDFAppDocumentTab* result = nullptr;
        AppToolOwnTabFixture::waitFor([&]()
        {
            result = guard ? nullptr : qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
            return result && result->getDocument() && result->getDocument()->isReady();
        });
        AppToolOwnTabFixture::settle();
        return result;
    }

    /// The media boxes of the pages of the document
    static std::vector<QRectF> getMediaBoxes(const pdf::PDFDocument* document)
    {
        std::vector<QRectF> boxes;
        for (size_t i = 0; document && i < document->getCatalog()->getPageCount(); ++i)
        {
            boxes.push_back(document->getCatalog()->getPage(i)->getMediaBox());
        }
        return boxes;
    }

    /// The scheme and the direction of the application are those of the start of a test
    static void restoreLook()
    {
        pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
        QApplication::setLayoutDirection(Qt::LeftToRight);
    }
};

/// A language, which marks the texts of Blank PDF
class BlankTestTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }

    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray name(context);
        if (name.contains("PDFAppBlankToolGUI") || name.contains("PDFAppPageSizePreview") || name.contains("PDFAppBlankSizePanel"))
        {
            return QStringLiteral("[T] ") + QString::fromUtf8(sourceText);
        }
        return QString();
    }
};

class AppToolBlankTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void registered();
    void frameDefault();
    void sizeMenu();
    void sixteenNineIsLandscape();
    void customSize();
    void fieldLimits();
    void fieldKeys();
    void pendingTextIsRun();
    void sameSizeAsOpenDocument();
    void createReplacesTab();
    void failedRunCallout();
    void startPage();
    void helpTopic();
    void languageChange();
    void themesAndDirection();
    void largePageCount();
    void deletionInEachState();
    void mcpOverHttp();
};

void AppToolBlankTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolBlankTest::cleanup()
{
    BlankGuiHelper::restoreLook();
}

void AppToolBlankTest::registered()
{
    // The context registers the tool and its GUI (PDFAppBuiltinTools::registerAll); the
    // catalog gives it its own tab (K1)
    AppToolOwnTabFixture fixture;
    const auto tool = pdf::PDFDocumentToolRegistry::getTool(BlankGuiHelper::TOOL_ID);
    QVERIFY(std::dynamic_pointer_cast<const PDFDocumentToolBlank>(tool));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(BlankGuiHelper::TOOL_ID));
    PDFAppBlankToolGUI::registerTools();
    QCOMPARE(pdf::PDFDocumentToolRegistry::getTool(BlankGuiHelper::TOOL_ID), tool);
    QVERIFY(pdf::PDFDocumentToolCatalog::findTool(BlankGuiHelper::TOOL_ID)->hasOwnTab());
}

void AppToolBlankTest::frameDefault()
{
    // convert-blank 337:1976 at 1440 x 960: A4 portrait, 3 pages
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    QVERIFY(tab);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);

    // A new tab starts with one A4 portrait page (the defaults of the core); Undo is off (K6)
    QCOMPARE(gui->getPagesField()->text(), QStringLiteral("1"));
    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    H::typeIntoField(gui->getPagesField(), QStringLiteral("3"));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 3);
    gui->getPageView()->setFocus();
    AppToolOwnTabFixture::settle();
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getOwnTabZoomText() == QStringLiteral("53 %"); }));
    fixture.screenshot(QStringLiteral("convert-blank-337-1976"));

    // The tab "New document" with the icon of a file (K1, 337:1979)
    QCOMPARE(tab->getTabTitle(), QStringLiteral("New document"));
    QCOMPARE(tab->getTabIconName(), QStringLiteral("file"));

    // tool-actions 337:1996: Size: A4, Portrait active, Pages 3; no W and H; Create PDF
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: A4"));
    QVERIFY(gui->getPortraitButton()->isChecked());
    QVERIFY(!gui->getLandscapeButton()->isChecked());
    QVERIFY(gui->getWidthGroup()->isHidden());
    QVERIFY(gui->getHeightGroup()->isHidden());
    QCOMPARE(gui->getPagesField()->text(), QStringLiteral("3"));
    QCOMPARE(gui->getPagesField()->height(), 32);
    QCOMPARE(shell->getToolbar()->getConfirmText(), QStringLiteral("Create PDF"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());

    // sidebar-left 337:2003: the header at x 12, y 104, "Pages / 3 blank pages"; the tiles
    // 108 x 165 from y 145, two in a row 116 apart, the third 171 below the first
    QCOMPARE(gui->getPagesHeader()->getTitleLabel()->text(), QStringLiteral("Pages"));
    QCOMPARE(gui->getPagesHeader()->getSubtitleLabel()->text(), QStringLiteral("3 blank pages"));
    QCOMPARE(H::inWindow(gui->getPagesHeader()).topLeft(), QPoint(12, 104));
    QCOMPARE(gui->getPagesHeader()->width(), 275);
    const pdf::PDFListView* view = gui->getPageView();
    QCOMPARE(view->model()->rowCount(), 3);
    const QPoint viewOrigin = H::inWindow(view->viewport()).topLeft();
    const QSizeF a4(H::points(210), H::points(297));
    const auto tileOf = [view, &viewOrigin, &a4](int row)
    {
        return pdf::PDFAppBlankPageDelegate::getTileRect(view->visualRect(view->model()->index(row, 0)), a4, Qt::LeftToRight).translated(viewOrigin);
    };
    QCOMPARE(tileOf(0), QRect(12, 145, 108, 165));
    QCOMPARE(tileOf(1), QRect(128, 145, 108, 165));
    QCOMPARE(tileOf(2), QRect(12, 316, 108, 165));
    QCOMPARE(pdf::PDFAppBlankPageDelegate::getPaperSize(a4), QSize(92, 130));
    QVERIFY(view->selectionModel()->isSelected(view->model()->index(0, 0)));

    // document-canvas 337:2009: "Page 1 of 3", Fit / 100 % 120 x 32 (Fit), the paper 424 x 600 at
    // x 473.5, y 235, "210 mm" 52 above the paper's row, "297 mm" 12 after the paper
    pdf::PDFAppPageSizePreview* preview = gui->getPreview();
    QCOMPARE(preview->getTitleLabel()->text(), QStringLiteral("Page 1 of 3"));
    QCOMPARE(preview->getZoomControl()->size(), QSize(120, 32));
    QCOMPARE(preview->getZoomControl()->getCurrentIndex(), 0);
    QCOMPARE(H::inWindow(preview), QRect(306, 90, 814, 842));
    pdf::PDFAppPageSizeStage* stage = preview->getStage();
    const QRect paper = stage->getPaperRect().translated(H::inWindow(stage).topLeft());
    QCOMPARE(paper.size(), QSize(424, 600));
    QVERIFY2(qAbs(paper.left() - 473) <= 1, qPrintable(QString::number(paper.left())));
    QCOMPARE(paper.top(), 235);
    QCOMPARE(stage->getWidthLabel()->text(), QStringLiteral("210 mm"));
    QCOMPARE(stage->getHeightLabel()->text(), QStringLiteral("297 mm"));
    QCOMPARE(stage->getCaptionLabel()->text(), QStringLiteral("A4 portrait · every page of the new document has this size"));
    QCOMPARE(H::inWindow(stage->getWidthLabel()).bottom() + 1 + pdf::PDFAppPageSizeStage::GAP, paper.top());
    QCOMPARE(H::inWindow(stage->getHeightLabel()).left(), paper.right() + 1 + pdf::PDFAppPageSizeStage::GAP);
    QCOMPARE(H::inWindow(stage->getCaptionLabel()).top(), paper.bottom() + 1 + pdf::PDFAppPageSizeStage::GAP);
    QVERIFY(qAbs(H::inWindow(stage->getWidthLabel()).center().x() - H::inWindow(stage).center().x()) <= 1);

    // The zoom: 600 px of 297 mm at 96 dpi (1122.5 px) = 53 % (K12)
    QVERIFY(qAbs(preview->getZoom() - 600.0 / (297.0 / 25.4 * 96.0)) < 0.001);

    // sidebar-right 337:2010: Summary and the blue callout
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Pages")), QStringLiteral("3"));
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Page size")), QStringLiteral("210 × 297 mm"));
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Orientation")), QStringLiteral("Portrait"));
    QCOMPARE(gui->getInfoCallout()->getTitleLabel()->text(), QStringLiteral("Create PDF opens the document right away"));
    QCOMPARE(gui->getInfoCallout()->getTextLabel()->text(), QStringLiteral("The new document opens in this tab as unsaved; there is no result dialog. Nothing is written to disk until you save it."));
    QCOMPARE(H::inWindow(gui->getSummary()->parentWidget()).width(), 287);
    QVERIFY(gui->getErrorCallout()->isHidden());

    // status-bar 337:2011
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Blank PDF · 3 pages · A4 portrait · 210 × 297 mm"));
    const pdf::PDFAppDocumentStatusBar* statusBar = tab->getToolPage()->getTabView()->getStatusBar();
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 / 3"));
    QCOMPARE(statusBar->getZoomText(), QStringLiteral("53 %"));

    // A tile shows its page in the preview and in the status bar
    QTest::mouseClick(gui->getPageView()->viewport(), Qt::LeftButton, Qt::NoModifier, view->visualRect(view->model()->index(2, 0)).center());
    AppToolOwnTabFixture::settle();
    QCOMPARE(preview->getTitleLabel()->text(), QStringLiteral("Page 3 of 3"));
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 3 / 3"));

    // 100 %: the paper of the actual size at 96 dpi (793.7 x 1122.5 px), the stage scrolls
    QTest::mouseClick(preview->getZoomControl()->findChildren<QToolButton*>().value(1), Qt::LeftButton);
    AppToolOwnTabFixture::settle();
    QVERIFY(!preview->isFit());
    QCOMPARE(stage->getPaperRect().size(), QSize(794, 1123));
    QVERIFY(preview->getScrollArea()->verticalScrollBar()->maximum() > 0);
    QVERIFY(AppToolOwnTabFixture::waitFor([statusBar]() { return statusBar->getZoomText() == QStringLiteral("100 %"); }));
}

void AppToolBlankTest::sizeMenu()
{
    // convert-blank-menus 337:2221: the panel 280 wide, the named sizes, Custom, the open document
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    QVERIFY(H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Invoice.pdf")), { { QSizeF(612, 792), pdf::PageRotation::None } })));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    H::typeIntoField(gui->getPagesField(), QStringLiteral("3"));

    pdf::PDFAppBlankSizePanel* panel = H::openSizePanel(gui);
    QVERIFY(panel);
    QVERIFY(gui->getSizeButton()->isChecked());
    fixture.screenshot(QStringLiteral("convert-blank-menus-337-2221"));
    QCOMPARE(panel->getPanelRect().width(), pdf::PDFAppBlankSizePanel::PANEL_WIDTH);

    // item-A4 337:2466 ... item-Custom 337:2503: title, dimensions, icon; A4 is checked
    const QStringList titles = { QStringLiteral("A4"), QStringLiteral("A3"), QStringLiteral("A5"), QStringLiteral("Letter"), QStringLiteral("Legal"), QStringLiteral("16:9"), QStringLiteral("Custom") };
    const QStringList descriptions = { QStringLiteral("210 × 297 mm"), QStringLiteral("297 × 420 mm"), QStringLiteral("148 × 210 mm"), QStringLiteral("216 × 279 mm"), QStringLiteral("216 × 356 mm"),
                                       QStringLiteral("Presentation · 338.7 × 190.5 mm"), QStringLiteral("Width and height fields appear in the toolbar") };
    const QStringList icons = { QStringLiteral("file-tall"), QStringLiteral("file-tall"), QStringLiteral("file-tall"), QStringLiteral("file-tall"), QStringLiteral("file-tall"),
                                QStringLiteral("presentation"), QStringLiteral("sliders") };
    const QStringList ids = { QStringLiteral("a4"), QStringLiteral("a3"), QStringLiteral("a5"), QStringLiteral("letter"), QStringLiteral("legal"), QStringLiteral("16x9"), QStringLiteral("custom") };
    QCOMPARE(int(panel->getRows().size()), int(titles.size()));
    for (qsizetype i = 0; i < titles.size(); ++i)
    {
        const pdf::PDFTileRow* row = panel->getRows()[size_t(i)];
        QCOMPARE(row, gui->getSizeRow(ids[i]));
        QCOMPARE(row->getTitle(), titles[i]);
        QCOMPARE(row->getDescription(), descriptions[i]);
        QCOMPARE(row->getIconName(), icons[i]);
        QCOMPARE(row->isChecked(), i == 0);
    }

    // caption 337:2512 and item-Invoice.pdf 337:2521: the size of its current page computed (K25)
    QCOMPARE(int(panel->getDocumentRows().size()), 1);
    const pdf::PDFTileRow* documentRow = panel->getDocumentRows().front();
    QCOMPARE(documentRow->getTitle(), QStringLiteral("Invoice.pdf"));
    QCOMPARE(documentRow->getDescription(), QStringLiteral("Current page 1 · 215.9 × 279.4 mm"));
    QCOMPARE(documentRow->getIconName(), QStringLiteral("file"));
    const pdf::PDFLabel* caption = panel->findChild<pdf::PDFLabel*>(QStringLiteral("toolPanelGroupCaption"));
    QVERIFY(caption && caption->isVisible());
    QCOMPARE(caption->text(), QStringLiteral("SAME SIZE AS AN OPEN DOCUMENT"));

    // Letter: the panel closes, the computed sizes have one decimal (K25: 612 x 792 points)
    QTest::mouseClick(gui->getSizeRow(QStringLiteral("letter")), Qt::LeftButton);
    AppToolOwnTabFixture::settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: Letter"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Blank PDF · 3 pages · Letter portrait · 215.9 × 279.4 mm"));
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Page size")), QStringLiteral("215.9 × 279.4 mm"));
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(612, 792));

    // The menu checks Letter now; Undo goes back to A4 by one step
    QVERIFY(H::openSizePanel(gui));
    QVERIFY(gui->getSizeRow(QStringLiteral("letter"))->isChecked());
    QVERIFY(!gui->getSizeRow(QStringLiteral("a4"))->isChecked());
    QTest::keyClick(panel, Qt::Key_Escape);
    AppToolOwnTabFixture::settle();
    QVERIFY(!panel->isVisible());
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: A4"));
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(H::points(210), H::points(297)));

    // A3 and A5 by the definitions of ISO 216 (5.4 of the plan)
    QVERIFY(H::chooseSize(gui, QStringLiteral("a3")));
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Page size")), QStringLiteral("297 × 420 mm"));
    QVERIFY(H::chooseSize(gui, QStringLiteral("legal")));
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(612, 1008));
}

void AppToolBlankTest::sixteenNineIsLandscape()
{
    // convert-blank-16-9 1390:47108, Q14: 16:9 switches to Landscape (960 x 540 points), one undo step
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    H::typeIntoField(gui->getPagesField(), QStringLiteral("3"));
    QVERIFY(gui->getPortraitButton()->isChecked());

    QVERIFY(H::chooseSize(gui, QStringLiteral("16x9")));
    gui->getPageView()->setFocus();
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getOwnTabZoomText() == QStringLiteral("50 %"); }));
    fixture.screenshot(QStringLiteral("convert-blank-16-9-1390-47108"));
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: 16:9"));
    QVERIFY(gui->getLandscapeButton()->isChecked());
    QVERIFY(!gui->getPortraitButton()->isChecked());
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(960, 540));
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Page size")), QStringLiteral("338.7 × 190.5 mm"));
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Orientation")), QStringLiteral("Landscape"));
    QCOMPARE(gui->getPreview()->getStage()->getCaptionLabel()->text(), QStringLiteral("16:9 landscape · every page of the new document has this size"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Blank PDF · 3 pages · 16:9 landscape · 338.7 × 190.5 mm"));

    // 1390:47108: the paper about 640 x 360 (960 points at 96 dpi = 1280 px, 50 %), limited by
    // the width of the stage (774) less its margins (2 x 35) and the height text after it; the
    // tiles 108 x 87
    const QSize paper = gui->getPreview()->getStage()->getPaperRect().size();
    QVERIFY2(qAbs(paper.width() - 640) <= 8, qPrintable(QString::number(paper.width())));
    QVERIFY(qAbs(paper.height() - qRound(paper.width() * 540.0 / 960.0)) <= 1);
    QCOMPARE(paper.width() + pdf::PDFAppPageSizeStage::GAP + gui->getPreview()->getStage()->getHeightLabel()->width(), 774 - 2 * pdf::PDFAppPageSizeStage::MARGIN_HORIZONTAL);
    QCOMPARE(pdf::PDFAppBlankPageDelegate::getTileSize(QSizeF(960, 540)), QSize(108, 87));
    const pdf::PDFListView* view = gui->getPageView();
    QCOMPARE(view->visualRect(view->model()->index(2, 0)).top() - view->visualRect(view->model()->index(0, 0)).top(), 87 + pdf::PDFAppBlankPageDelegate::ROW_GAP);

    // Portrait turns it to 190.5 x 338.7 mm; one Undo is Landscape again, one more is A4 portrait
    QTest::mouseClick(gui->getPortraitButton(), Qt::LeftButton);
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(540, 960));
    QVERIFY(gui->getPortraitButton()->isChecked());
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Page size")), QStringLiteral("190.5 × 338.7 mm"));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(960, 540));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: A4"));
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(H::points(210), H::points(297)));
    QVERIFY(gui->getPortraitButton()->isChecked());
}

void AppToolBlankTest::customSize()
{
    // convert-blank-custom 356:9975: Size: Custom with W 200 and H 280 mm
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    H::typeIntoField(gui->getPagesField(), QStringLiteral("3"));

    // Custom starts with the size shown so far: the page does not jump
    QVERIFY(H::chooseSize(gui, QStringLiteral("custom")));
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: Custom"));
    QVERIFY(gui->getWidthGroup()->isVisible());
    QVERIFY(gui->getHeightGroup()->isVisible());
    QCOMPARE(gui->getWidthField()->text(), QStringLiteral("210"));
    QCOMPARE(gui->getHeightField()->text(), QStringLiteral("297"));
    QCOMPARE(gui->getWidthField()->getSuffix(), QStringLiteral("mm"));
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Page size")), QStringLiteral("210 × 297 mm"));

    // tool-actions 356:9995: Size: Custom, W, H, divider, the orientation, divider, Pages
    QVERIFY(H::inWindow(gui->getWidthGroup()).left() > H::inWindow(gui->getSizeButton()).right());
    QVERIFY(H::inWindow(gui->getHeightGroup()).left() > H::inWindow(gui->getWidthGroup()).right());
    QVERIFY(H::inWindow(gui->getPortraitButton()).left() > H::inWindow(gui->getHeightGroup()).right());
    QCOMPARE(gui->getWidthField()->width(), 75);

    H::typeIntoField(gui->getWidthField(), QStringLiteral("200"));
    H::typeIntoField(gui->getHeightField(), QStringLiteral("280"));
    gui->getPageView()->setFocus();
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getOwnTabZoomText() == QStringLiteral("57 %"); }));
    fixture.screenshot(QStringLiteral("convert-blank-custom-356-9975"));
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Blank PDF · 3 pages · custom size 200 × 280 mm"));
    QCOMPARE(gui->getPreview()->getStage()->getCaptionLabel()->text(), QStringLiteral("Custom · 200 × 280 mm · every page of the new document has this size"));
    QCOMPARE(gui->getPreview()->getStage()->getWidthLabel()->text(), QStringLiteral("200 mm"));
    QCOMPARE(gui->getPreview()->getStage()->getHeightLabel()->text(), QStringLiteral("280 mm"));
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(H::points(200), H::points(280)));
    QVERIFY(gui->getPortraitButton()->isChecked());

    // The paper fits the height of 600: its width follows the proportions of 200 : 280
    QCOMPARE(gui->getPreview()->getStage()->getPaperRect().size(), QSize(qRound(600.0 * 200.0 / 280.0), 600));

    // Landscape of a custom size swaps its sides (one undo step)
    QTest::mouseClick(gui->getLandscapeButton(), Qt::LeftButton);
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getWidthField()->text(), QStringLiteral("280"));
    QCOMPARE(gui->getHeightField()->text(), QStringLiteral("200"));
    QVERIFY(gui->getLandscapeButton()->isChecked());
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Orientation")), QStringLiteral("Landscape"));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(gui->getWidthField()->text(), QStringLiteral("200"));
    QCOMPARE(gui->getHeightField()->text(), QStringLiteral("280"));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(gui->getHeightField()->text(), QStringLiteral("297"));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(gui->getWidthField()->text(), QStringLiteral("210"));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: A4"));
    QVERIFY(gui->getWidthGroup()->isHidden());
}

void AppToolBlankTest::fieldLimits()
{
    // K26 (Q17, no frame): Pages 1 - 10 000, a side 3 - 14 400 points (PDF Reference 1.7, Table
    // C.1); a text outside has the state Error, the status says why, Create PDF is disabled and
    // the model keeps its value
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QPushButton* confirm = shell->getToolbar()->getConfirmButton();
    const QString pagesError = QStringLiteral("Blank PDF · The number of pages must be a whole number from 1 to 10,000.");

    for (const QString& text : { QStringLiteral("0"), QStringLiteral("10001"), QStringLiteral("2.5"), QStringLiteral("abc"), QString(), QStringLiteral("-3") })
    {
        H::typeIntoField(gui->getPagesField(), text);
        QVERIFY2(gui->getPagesField()->isInvalid(), qPrintable(text));
        QVERIFY2(!confirm->isEnabled(), qPrintable(text));
        QCOMPARE(shell->getStatusMessage(), pagesError);
        QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 1);
    }
    fixture.screenshot(QStringLiteral("convert-blank-error"));
    H::typeIntoField(gui->getPagesField(), QStringLiteral("10000"));
    QVERIFY(!gui->getPagesField()->isInvalid());
    QVERIFY(confirm->isEnabled());
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 10000);
    QCOMPARE(gui->getPagesField()->text(), QStringLiteral("10,000"));
    H::typeIntoField(gui->getPagesField(), QStringLiteral("1"));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 1);

    // The sides: 1.05 mm is 2.98 points, 1.06 mm 3.005; 5,080 mm is 14 400 points
    QVERIFY(H::chooseSize(gui, QStringLiteral("custom")));
    const QString widthError = QStringLiteral("Blank PDF · The width must be a number from 1.1 to 5,080 mm.");
    for (const QString& text : { QStringLiteral("1.05"), QStringLiteral("5080.1"), QStringLiteral("0"), QStringLiteral("x") })
    {
        H::typeIntoField(gui->getWidthField(), text);
        QVERIFY2(gui->getWidthField()->isInvalid(), qPrintable(text));
        QVERIFY(!confirm->isEnabled());
        QCOMPARE(shell->getStatusMessage(), widthError);
        QCOMPARE(shell->getModel()->getValue(QStringLiteral("width")).toDouble(), 210.0);
    }
    H::typeIntoField(gui->getWidthField(), QStringLiteral("1.06"));
    QVERIFY(!gui->getWidthField()->isInvalid());
    QVERIFY(confirm->isEnabled());
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("width")).toDouble(), 1.06);
    H::typeIntoField(gui->getHeightField(), QStringLiteral("5080"));
    QVERIFY(!gui->getHeightField()->isInvalid());
    QCOMPARE(gui->getEffectivePageSize().height(), 14400.0);
    QCOMPARE(H::rowValue(gui->getSummary(), QStringLiteral("Page size")), QStringLiteral("1.1 × 5,080 mm"));

    // Two errors: both fields are invalid, the first one is said; the error of a field, which
    // a named size hides, is gone
    H::typeIntoField(gui->getHeightField(), QStringLiteral("9999"));
    H::typeIntoField(gui->getWidthField(), QStringLiteral("0"));
    QCOMPARE(shell->getStatusMessage(), widthError);
    QVERIFY(H::chooseSize(gui, QStringLiteral("a5")));
    QVERIFY(confirm->isEnabled());
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Blank PDF · 1 page · A5 portrait · 148 × 210 mm"));
}

void AppToolBlankTest::fieldKeys()
{
    // Up / Down by one, Page Up / Page Down by ten, within the limits; each one undo step
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    pdf::PDFToolBarField* pages = gui->getPagesField();

    pages->setFocus();
    QTest::keyClick(pages, Qt::Key_Down);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 1);
    QTest::keyClick(pages, Qt::Key_Up);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 2);
    QCOMPARE(pages->text(), QStringLiteral("2"));
    QTest::keyClick(pages, Qt::Key_PageUp);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 12);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(pages->text(), QStringLiteral("2"));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(pages->text(), QStringLiteral("1"));

    // A text being written is the start of the step
    QTest::keyClicks(pages, QStringLiteral("0"));
    QCOMPARE(pages->text(), QStringLiteral("10"));
    QTest::keyClick(pages, Qt::Key_Down);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 9);

    // The upper limit holds
    H::typeIntoField(pages, QStringLiteral("9995"));
    QTest::keyClick(pages, Qt::Key_PageUp);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 10000);

    // The sides of a custom size by one millimeter
    QVERIFY(H::chooseSize(gui, QStringLiteral("custom")));
    gui->getWidthField()->setFocus();
    QTest::keyClick(gui->getWidthField(), Qt::Key_Up);
    QCOMPARE(gui->getWidthField()->text(), QStringLiteral("211"));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("width")).toDouble(), 211.0);

    // Every part of the toolbar takes the keyboard focus (focus order of the toolbar)
    for (QWidget* widget : std::initializer_list<QWidget*>{ gui->getSizeButton(), gui->getWidthField(), gui->getHeightField(), gui->getPortraitButton(), gui->getLandscapeButton(), pages })
    {
        QVERIFY(widget->focusPolicy() & Qt::TabFocus);
    }
}

void AppToolBlankTest::pendingTextIsRun()
{
    // A valid text, which has not been left (Ctrl+Enter in the field), is the value of the run
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    H::typeIntoField(gui->getPagesField(), QStringLiteral("5"), false);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pageCount")).toLongLong(), 1);
    QCOMPARE(gui->getPageModel()->getPageCount(), 5);
    QCOMPARE(shell->getStatusMessage(), QStringLiteral("Blank PDF · 5 pages · A4 portrait · 210 × 297 mm"));

    QPointer<pdf::PDFAppToolTab> guard(tab);
    QTest::keyClick(gui->getPagesField(), Qt::Key_Return, Qt::ControlModifier);
    pdf::PDFAppDocumentTab* result = nullptr;
    QVERIFY(AppToolOwnTabFixture::waitFor([&]()
    {
        result = guard ? nullptr : qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
        return result && result->getDocument() && result->getDocument()->isReady();
    }));
    QCOMPARE(result->getDocument()->getPageCount(), pdf::PDFInteger(5));
}

void AppToolBlankTest::sameSizeAsOpenDocument()
{
    // K27 (Q17, no frame): the size of the current page of an open document of the window -
    // its crop box turned by /Rotate - fills Custom in one undo step
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    QVERIFY(H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Invoice.pdf")), { { QSizeF(612, 792), pdf::PageRotation::None } })));
    pdf::PDFAppDocumentTab* turned = H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Turned.pdf")),
                                                                         { { QSizeF(612, 792), pdf::PageRotation::None }, { QSizeF(H::points(210), H::points(297)), pdf::PageRotation::Rotate90 } }));
    QVERIFY(turned);
    QVERIFY(H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Tiny.pdf")), { { QSizeF(2, 2), pdf::PageRotation::None } })));
    turned->getView()->goToPage(1);
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);

    pdf::PDFAppBlankSizePanel* panel = H::openSizePanel(gui);
    QVERIFY(panel);
    QCOMPARE(int(panel->getDocumentRows().size()), 3);
    QCOMPARE(panel->getDocumentRows()[1]->getTitle(), QStringLiteral("Turned.pdf"));
    QCOMPARE(panel->getDocumentRows()[1]->getDescription(), QStringLiteral("Current page 2 · 297 × 210 mm"));
    QTest::mouseClick(panel->getDocumentRows()[1], Qt::LeftButton);
    AppToolOwnTabFixture::settle();
    QVERIFY(!panel->isVisible());
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: Custom"));
    QCOMPARE(gui->getWidthField()->text(), QStringLiteral("297"));
    QCOMPARE(gui->getHeightField()->text(), QStringLiteral("210"));
    QVERIFY(gui->getLandscapeButton()->isChecked());
    QVERIFY(qAbs(gui->getEffectivePageSize().width() - H::points(297)) < 1e-4);
    QVERIFY(qAbs(gui->getEffectivePageSize().height() - H::points(210)) < 1e-4);
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: A4"));

    // A page outside of the limits of a side (2 points) is refused: the size stays
    panel = H::openSizePanel(gui);
    QVERIFY(panel);
    QCOMPARE(panel->getDocumentRows()[2]->getTitle(), QStringLiteral("Tiny.pdf"));
    QTest::mouseClick(panel->getDocumentRows()[2], Qt::LeftButton);
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSizeButton()->text(), QStringLiteral("Size: A4"));
    QCOMPARE(gui->getEffectivePageSize(), QSizeF(H::points(210), H::points(297)));

    // Without an open document the group is hidden
    fixture.window->closeTab(turned);
    for (pdf::PDFAppDocumentTab* documentTab : fixture.window->getDocumentTabs())
    {
        fixture.window->closeTab(documentTab);
    }
    AppToolOwnTabFixture::settle();
    panel = H::openSizePanel(gui);
    QVERIFY(panel);
    QVERIFY(panel->getDocumentRows().empty());
    const pdf::PDFLabel* caption = panel->findChild<pdf::PDFLabel*>(QStringLiteral("toolPanelGroupCaption"));
    QVERIFY(caption && !caption->isVisible());
}

void AppToolBlankTest::createReplacesTab()
{
    // 4.6: Create PDF replaces the own tab at its place by the new unsaved document "Untitled"
    // without a result dialog; its pages have the size of the table of 5.4 exactly
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppDocumentTab* first = fixture.openDocument(1);
    QVERIFY(first);
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    const int index = int(fixture.window->getTabs().indexOf(tab));
    H::typeIntoField(gui->getPagesField(), QStringLiteral("3"));
    QVERIFY(H::chooseSize(gui, QStringLiteral("a5")));
    QTest::mouseClick(gui->getLandscapeButton(), Qt::LeftButton);
    AppToolOwnTabFixture::settle();

    QPointer<pdf::PDFAppToolTab> guard(tab);
    pdf::PDFAppDocumentTab* result = H::createPdf(fixture, tab);
    QVERIFY(result);
    QVERIFY(!guard);
    QVERIFY(fixture.window->getToolTabs().isEmpty());
    QCOMPARE(int(fixture.window->getTabs().indexOf(result)), index);
    QCOMPARE(fixture.window->getCurrentTab(), result);
    QCOMPARE(result->getDocument()->getTitle(), QStringLiteral("Untitled"));
    QVERIFY(result->getDocument()->getFilePath().isEmpty());
    QVERIFY(!AppToolOwnTabFixture::findDialog<pdf::PDFAppToolResultDialog>());

    // A5 landscape: 595.276 x 419.528 points on every page (ISO 216: 148 x 210 mm)
    const std::vector<QRectF> boxes = H::getMediaBoxes(result->getDocument()->getDocument().data());
    QCOMPARE(int(boxes.size()), 3);
    for (const QRectF& box : boxes)
    {
        QVERIFY(qAbs(box.width() - H::points(210)) < 1e-3);
        QVERIFY(qAbs(box.height() - H::points(148)) < 1e-3);
    }
}

void AppToolBlankTest::failedRunCallout()
{
    // 4.6 (no frame): a failed run says its reason in the red callout of the right panel, the
    // tab stays; a new value hides it
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    gui->onRunNotAccepted(pdf::PDFDocumentToolResult::createFailed(QStringLiteral("The disk is full.")));
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getErrorCallout()->isVisible());
    QCOMPARE(gui->getErrorCallout()->getTitleLabel()->text(), QStringLiteral("The new document could not be created"));
    QCOMPARE(gui->getErrorCallout()->getTextLabel()->text(), QStringLiteral("The disk is full."));
    QVERIFY(H::inWindow(gui->getErrorCallout()).top() > H::inWindow(gui->getInfoCallout()).bottom());
    QVERIFY(tab);
    QVERIFY(H::chooseSize(gui, QStringLiteral("a3")));
    QVERIFY(gui->getErrorCallout()->isHidden());
}

void AppToolBlankTest::startPage()
{
    // pdf4qt-tool-start-blank 1392:4528 (Q10): Start a new document, Open Blank PDF, the note,
    // At a glance; the button opens the own tab
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    fixture.navigate(QStringLiteral("/tools/convert.blank"));
    AppToolOwnTabFixture::settle();
    pdf::PDFToolStartPage* page = fixture.page<pdf::PDFToolStartPage>();
    QVERIFY(page);
    fixture.screenshot(QStringLiteral("pdf4qt-tool-start-blank-1392-4528"));

    // workspace-header 1392:4567: the breadcrumb and the Tag have the title of the category
    // ("Create & Convert"), not its short chip of the catalog page ("Convert", tools.md 3)
    QCOMPARE(page->getHeaderTag(), QStringLiteral("Create & Convert"));
    const QList<pdf::PDFAppBreadcrumbItem> breadcrumbs = page->getHeaderBreadcrumbs();
    QCOMPARE(breadcrumbs.size(), 3);
    QCOMPARE(breadcrumbs.at(1).text, QStringLiteral("Create & Convert"));
    QCOMPARE(breadcrumbs.at(1).route.toString(), QStringLiteral("/tools?category=convert"));
    QCOMPARE(breadcrumbs.at(2).text, QStringLiteral("Blank PDF"));

    QWidget* section = page->findChild<QWidget*>(QStringLiteral("toolChooseDocument"));
    QVERIFY(section);
    QStringList texts;
    for (const QLabel* label : section->findChildren<QLabel*>())
    {
        texts << label->text();
    }
    QVERIFY2(texts.contains(QStringLiteral("Start a new document")), qPrintable(texts.join(QLatin1Char('|'))));
    QVERIFY(texts.contains(QStringLiteral("Blank PDF needs no document · size, orientation and pages are set in the tool")));
    QVERIFY(texts.contains(QStringLiteral("A new document with empty pages · nothing is written to disk until you save it")));
    QVERIFY(texts.contains(QStringLiteral("Blank PDF opens in its own tab “New document”. Create PDF turns this tab into the new unsaved document; the open documents are not changed.")));
    QPushButton* open = page->getOpenToolButton();
    QVERIFY(open);
    QCOMPARE(open->text(), QStringLiteral("Open Blank PDF"));
    QCOMPARE(open->height(), 36);
    const QWidget* card = page->findChild<QWidget*>(QStringLiteral("toolOpenToolCard"));
    QVERIFY(card);
    QCOMPARE(card->height(), 54);

    // at-a-glance 1392:4708
    const pdf::PDFKeyValueList* facts = page->findChild<pdf::PDFKeyValueList*>(QStringLiteral("toolFacts"));
    QVERIFY(facts);
    QCOMPARE(H::rowValue(facts, QStringLiteral("Input")), QStringLiteral("No document"));
    QCOMPARE(H::rowValue(facts, QStringLiteral("Result")), QStringLiteral("One new document"));
    QCOMPARE(H::rowValue(facts, QStringLiteral("Open documents")), QStringLiteral("Not used"));
    QCOMPARE(H::rowValue(facts, QStringLiteral("Locked document")), QStringLiteral("Not used"));
    QCOMPARE(H::rowValue(facts, QStringLiteral("Signatures")), QStringLiteral("Not affected"));

    QTest::mouseClick(open, Qt::LeftButton);
    AppToolOwnTabFixture::settle();
    QCOMPARE(fixture.window->getToolTabs().size(), 1);
    pdf::PDFAppToolTab* tab = fixture.window->getToolTabs().front();
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    QCOMPARE(tab->getTabTitle(), QStringLiteral("New document"));
    QVERIFY(H::gui(tab));
}

void AppToolBlankTest::helpTopic()
{
    // The help topic of the tool (F1 of /tool/convert.blank/{instance} and of its start page)
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    QVERIFY(tab);
    QCOMPARE(tab->getToolPage()->getHelpTopic(), QStringLiteral("tool-convert-blank"));
    fixture.pressHelpKey();
    QCOMPARE(fixture.getShownHelpTopic(), QStringLiteral("tool-convert-blank"));
}

void AppToolBlankTest::languageChange()
{
    // A new language: the parts are made again with the values of the model, a text being
    // written survives with its error; the canvas follows
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::chooseSize(gui, QStringLiteral("custom")));
    H::typeIntoField(gui->getWidthField(), QStringLiteral("abc"), false);
    H::typeIntoField(gui->getPagesField(), QStringLiteral("7"), false);
    QVERIFY(H::openSizePanel(gui));

    BlankTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    AppToolOwnTabFixture::settle();
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getSizeButton() && gui->getSizeButton()->text().startsWith(QStringLiteral("[T] ")); }));
    QCOMPARE(gui->getPagesHeader()->getTitleLabel()->text(), QStringLiteral("[T] Pages"));
    QCOMPARE(gui->getPagesField()->text(), QStringLiteral("7"));
    QCOMPARE(gui->getWidthField()->text(), QStringLiteral("abc"));
    QVERIFY(gui->getWidthField()->isInvalid());
    QVERIFY(!AppToolOwnTabFixture::getShell(tab)->getToolbar()->getConfirmButton()->isEnabled());
    QVERIFY(gui->getPreview()->getTitleLabel()->text().startsWith(QStringLiteral("[T] ")));
    QCOMPARE(gui->getPreview()->getZoomControl()->findChildren<QToolButton*>().value(0)->text(), QStringLiteral("[T] Fit"));
    QCoreApplication::removeTranslator(&translator);
    AppToolOwnTabFixture::settle();
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getSizeButton() && gui->getSizeButton()->text() == QStringLiteral("Size: Custom"); }));
    QCOMPARE(gui->getPreview()->getTitleLabel()->text(), QStringLiteral("Page 1 of 7"));
}

void AppToolBlankTest::themesAndDirection()
{
    // Both themes and right-to-left at 1x and 2x: no pixel of the leak color, the height at the
    // leading side of the paper in right-to-left, the first tile at the right
    using H = BlankGuiHelper;
    for (const bool isDark : { false, true })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            QApplication::setLayoutDirection(direction);
            AppToolOwnTabFixture fixture(QSize(1440, 960));
            pdf::PDFThemeManager::getInstance()->setScheme(isDark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
            AppToolOwnTabFixture::settle();
            QVERIFY(H::openReady(fixture, H::writeDocument(fixture.path(QStringLiteral("Invoice.pdf")), { { QSizeF(612, 792), pdf::PageRotation::None } })));
            pdf::PDFAppToolTab* tab = H::openBlank(fixture);
            PDFAppBlankToolGUI* gui = H::gui(tab);
            QVERIFY(gui);
            H::typeIntoField(gui->getPagesField(), QStringLiteral("3"));
            gui->getPageView()->setFocus();
            AppToolOwnTabFixture::settle();

            const QString suffix = QStringLiteral("%1%2").arg(isDark ? QStringLiteral("-dark") : QString(), direction == Qt::RightToLeft ? QStringLiteral("-rtl") : QString());
            fixture.screenshot(QStringLiteral("convert-blank-337-1976") + suffix);
            QWidget* body = tab->getToolPage()->getTabView();
            for (const qreal ratio : { 1.0, 2.0 })
            {
                QCOMPARE(pdftest::AppToolShellFixture::countLeakPixels(pdftest::AppToolShellFixture::render(body, ratio)), 0);
            }
            pdf::PDFAppBlankSizePanel* panel = H::openSizePanel(gui);
            QVERIFY(panel);
            fixture.screenshot(QStringLiteral("convert-blank-menus-337-2221") + suffix);
            QCOMPARE(pdftest::AppToolShellFixture::countLeakPixels(pdftest::AppToolShellFixture::render(panel, 2.0)), 0);
            QTest::keyClick(panel, Qt::Key_Escape);
            AppToolOwnTabFixture::settle();

            const pdf::PDFAppPageSizeStage* stage = gui->getPreview()->getStage();
            const QRect paper = stage->getPaperRect();
            const QRect height = stage->getHeightLabel()->geometry();
            const pdf::PDFListView* view = gui->getPageView();
            const QRect firstTile = pdf::PDFAppBlankPageDelegate::getTileRect(view->visualRect(view->model()->index(0, 0)), QSizeF(H::points(210), H::points(297)), direction);
            if (direction == Qt::RightToLeft)
            {
                QCOMPARE(height.right() + 1 + pdf::PDFAppPageSizeStage::GAP, paper.left());
                QVERIFY(firstTile.right() > view->viewport()->width() - pdf::PDFAppBlankPageDelegate::TILE_WIDTH);
            }
            else
            {
                QCOMPARE(paper.right() + 1 + pdf::PDFAppPageSizeStage::GAP, height.left());
                QCOMPARE(firstTile.left(), 0);
            }
        }
    }
}

void AppToolBlankTest::largePageCount()
{
    // 10 000 pages (the limit): rows of a model, no widget per page; the tiles and the preview
    // follow without a stall, the last page can be shown
    using H = BlankGuiHelper;
    AppToolOwnTabFixture fixture(QSize(1440, 960));
    pdf::PDFAppToolTab* tab = H::openBlank(fixture);
    PDFAppBlankToolGUI* gui = H::gui(tab);
    QVERIFY(gui);
    QElapsedTimer timer;
    timer.start();
    H::typeIntoField(gui->getPagesField(), QStringLiteral("10000"));
    QVERIFY2(timer.elapsed() < 5000 * pdftest::AppDocumentFixture::getTimeoutFactor(), qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(gui->getPageModel()->getPageCount(), 10000);
    QCOMPARE(gui->getPagesHeader()->getSubtitleLabel()->text(), QStringLiteral("10,000 blank pages"));
    QVERIFY(gui->getPageView()->findChildren<QWidget*>().size() < 20);

    pdf::PDFListView* view = gui->getPageView();
    const QModelIndex last = view->model()->index(9999, 0);
    view->setCurrentIndex(last);
    QVERIFY(AppToolOwnTabFixture::waitFor([view, &last]()
    {
        view->scrollTo(last);
        return view->viewport()->rect().intersects(view->visualRect(last));
    }));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getPreview()->getTitleLabel()->text(), QStringLiteral("Page 10,000 of 10,000"));
    QCOMPARE(tab->getToolPage()->getTabView()->getStatusBar()->getPageText(), QStringLiteral("Page 10,000 / 10,000"));

    // Fewer pages: the shown page is the last one left
    H::typeIntoField(gui->getPagesField(), QStringLiteral("2"));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getCurrentPage(), 1);
    QCOMPARE(gui->getPreview()->getTitleLabel()->text(), QStringLiteral("Page 2 of 2"));
}

void AppToolBlankTest::deletionInEachState()
{
    // The window or the tab goes in each state of the tool: the open menu, a text being written,
    // a running Create PDF; nothing crashes, the window works afterwards
    using H = BlankGuiHelper;
    for (int state = 0; state < 3; ++state)
    {
        for (const bool isWindow : { false, true })
        {
            AppToolOwnTabFixture fixture(QSize(1440, 960));
            QVERIFY(fixture.openDocument(1));
            QPointer<pdf::PDFAppToolTab> tab = H::openBlank(fixture);
            PDFAppBlankToolGUI* gui = H::gui(tab);
            QVERIFY(gui);
            QPointer<pdf::PDFAppBlankSizePanel> panel = gui->getSizePanel();
            switch (state)
            {
                case 0:
                    QVERIFY(H::openSizePanel(gui));
                    break;
                case 1:
                    H::typeIntoField(gui->getPagesField(), QStringLiteral("x"), false);
                    break;
                default:
                    H::typeIntoField(gui->getPagesField(), QStringLiteral("2000"));
                    AppToolOwnTabFixture::getShell(tab)->confirm();
                    break;
            }

            if (isWindow)
            {
                delete fixture.window.data();
                AppToolOwnTabFixture::settle();
                QVERIFY(!tab);
                QVERIFY(!panel);
                continue;
            }

            delete tab.data();
            AppToolOwnTabFixture::settle();
            QVERIFY(!panel);
            QVERIFY(fixture.window);
            QVERIFY(fixture.window->getToolTabs().isEmpty());
            for (const pdf::PDFAppDocumentTab* documentTab : fixture.window->getDocumentTabs())
            {
                QVERIFY(documentTab->getDocument()->getTitle() != QStringLiteral("Untitled"));
            }
            QVERIFY(H::openBlank(fixture));
        }
    }
}

void AppToolBlankTest::mcpOverHttp()
{
    // convert.blank over HTTP (convert.md): open_new_tab by default, save_file of a .pdf, the
    // errors of the given parameters
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    QVERIFY(fixture.startServer());
    fixture.setTransport(pdftest::AppMcpTransport::Http);
    const auto call = [&fixture](const QJsonObject& arguments)
    {
        QJsonObject full = arguments;
        full.insert(QStringLiteral("action"), QStringLiteral("blank"));
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(QStringLiteral("convert"), full);
        return result ? QJsonDocument::fromJson(result->result).object() : QJsonObject();
    };
    const auto isError = [](const QJsonObject& answer) { return answer.isEmpty() || answer.value(QStringLiteral("isError")).toBool(); };

    // The first example of convert.md: three A4 pages in a new tab
    const int tabCount = int(fixture.window->getTabs().size());
    QJsonObject answer = call({ { QStringLiteral("blank"), QJsonObject({ { QStringLiteral("pageCount"), 3 } }) } });
    QVERIFY2(!isError(answer), QJsonDocument(answer).toJson().constData());
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount + 1);
    pdf::PDFAppDocumentTab* newTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getCurrentTab());
    QVERIFY(newTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([newTab]() { return newTab->getDocument()->isReady(); }));
    QCOMPARE(newTab->getDocument()->getPageCount(), pdf::PDFInteger(3));
    QCOMPARE(answer.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("blank")).toObject().value(QStringLiteral("documentId")).toString(), newTab->getDocument()->getId());

    // The second example: 16:9 landscape written as a file (960 x 540 points)
    const QString path = fixture.path(QStringLiteral("Slides.pdf"));
    answer = call({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), path },
                    { QStringLiteral("blank"), QJsonObject({ { QStringLiteral("pageCount"), 10 }, { QStringLiteral("pageSize"), QStringLiteral("16x9") }, { QStringLiteral("orientation"), QStringLiteral("landscape") } }) } });
    QVERIFY2(!isError(answer), QJsonDocument(answer).toJson().constData());
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    pdf::PDFDocumentReader reader(nullptr, nullptr, true, false);
    const pdf::PDFDocument written = reader.readFromBuffer(file.readAll());
    QCOMPARE(written.getCatalog()->getPageCount(), size_t(10));
    for (const QRectF& box : BlankGuiHelper::getMediaBoxes(&written))
    {
        QCOMPARE(box.size(), QSizeF(960, 540));
    }

    // Owner answer Q22: 16:9 without an orientation is landscape too (as the GUI, Q14)
    const QString presentation = fixture.path(QStringLiteral("Presentation.pdf"));
    answer = call({ { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), presentation },
                    { QStringLiteral("blank"), QJsonObject({ { QStringLiteral("pageCount"), 2 }, { QStringLiteral("pageSize"), QStringLiteral("16x9") } }) } });
    QVERIFY2(!isError(answer), QJsonDocument(answer).toJson().constData());
    QFile presentationFile(presentation);
    QVERIFY(presentationFile.open(QIODevice::ReadOnly));
    const pdf::PDFDocument presentationDocument = reader.readFromBuffer(presentationFile.readAll());
    QCOMPARE(presentationDocument.getCatalog()->getPageCount(), size_t(2));
    for (const QRectF& box : BlankGuiHelper::getMediaBoxes(&presentationDocument))
    {
        QCOMPARE(box.size(), QSizeF(960, 540));
    }

    // custom with orientation, and width with a named size, are errors which say what to do
    answer = call({ { QStringLiteral("blank"), QJsonObject({ { QStringLiteral("pageSize"), QStringLiteral("custom") }, { QStringLiteral("width"), 200 }, { QStringLiteral("height"), 280 }, { QStringLiteral("orientation"), QStringLiteral("landscape") } }) } });
    QVERIFY(isError(answer));
    answer = call({ { QStringLiteral("blank"), QJsonObject({ { QStringLiteral("pageSize"), QStringLiteral("a4") }, { QStringLiteral("width"), 200 } }) } });
    QVERIFY(isError(answer));
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount + 1);
}

QTEST_MAIN(AppToolBlankTest)

#include "tst_apptoolblanktest.moc"
