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

// The GUI of Compare (plan of step 7 of phase 2, 2.1, 4.9, 8; package P6): the frames of 2.1 by their ids, the
// slots, Swap, the password, an unsaved tab as a snapshot, the filters, the views, F5 / F6, the stale state (Q7.17),
// the close of a source tab (FC-5), PDF4QT --compare (Q7.13), the export Disabled without differences (R7-9), the
// colors with their swap.

#include "apptoolowntabfixture.h"

#include "pdfappcomparedifferencemodel.h"
#include "pdfappcompareview.h"
#include "pdfappdocument.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappmainwindow.h"
#include "pdfapppassworddialog.h"
#include "pdfapptoolgui_compare.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltab.h"
#include "pdfappowntabtoolhost.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptooltoolbar.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include "pdfdrawwidget.h"

#include <QDir>
#include <QPainter>
#include <QThreadPool>
#include <QTranslator>
#include <QtMath>
#include <QtTest>

#include <algorithm>

using namespace pdftest;

class AppToolCompareTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void registration();
    void choiceOfTheDocuments();
    void openTabsAndUsedDocument();
    void pageRanges();
    void swapSlots();
    void resultFiltersAndList();
    void navigation();
    void exportRules();
    void colorsWithSwap();
    void sourceTabClosed();
    void passwordOfAnInput();
    void runAndStale();
    void commandLineTab();
    void framesInBothThemes();
    void figmaListAndStatusTexts();
    void figmaFilteredTexts();
    void figmaPairFitsTheCanvas();
    void figmaNoDifferencesCentered();
    void figmaReportFacts();
    void languageChange();
    void reportOfReplacedResult();
    void commandLineUnfinishedPair();
    void listDeletedDuringChange();
    void reportFactsOfTheJob();
    void figmaChosenInputRow();
    void figmaZoomInStatusBar();
};

/// Pseudo translation of the texts of PDF4QT: "[text]"
class CompareTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray contextText(context ? context : "");
        if (!contextText.startsWith("pdf") || !sourceText || !*sourceText)
        {
            return QString();
        }
        return QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText));
    }

    virtual bool isEmpty() const override { return false; }
};

/// Helpers of the test
class CompareTestHelper
{
public:
    CompareTestHelper() = delete;

    /// Pages 400 x 400 with one black rectangle each (points)
    static QString rectangles(const QString& folder, const QString& name, const QList<QRectF>& rectangles)
    {
        pdf::PDFDocumentBuilder builder;
        pdf::PDFPageContentStreamBuilder content(&builder);
        for (const QRectF& rectangle : rectangles)
        {
            QPainter* painter = content.beginNewPage(QRectF(0, 0, 400, 400));
            painter->fillRect(rectangle, Qt::black);
            content.end(painter);
        }
        pdf::PDFDocument document = builder.build();
        const QString path = folder + QLatin1Char('/') + name;
        return AppDocumentSamples::write(document, path) ? path : QString();
    }

    static bool waitLoaded(const pdf::PDFAppToolInputList* list)
    {
        return AppToolOwnTabFixture::waitFor([list]() { return list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0; });
    }

    /// A tab of Compare with the files (read before the tab is shown; an empty list = the empty tab)
    static pdf::PDFAppToolTab* open(AppToolOwnTabFixture& fixture, const QStringList& files)
    {
        pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
        list->addFiles(files);
        if (!waitLoaded(list))
        {
            delete list;
            return nullptr;
        }
        pdf::PDFAppToolTab* tab = fixture.createToolTab(QString::fromLatin1(pdf::PDFDocumentToolCompare::TOOL_ID), list);
        AppToolOwnTabFixture::settle();
        return tab;
    }

    static pdf::PDFAppToolGUICompare* gui(const pdf::PDFAppToolTab* tab)
    {
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        return shell ? qobject_cast<pdf::PDFAppToolGUICompare*>(shell->getGUI()) : nullptr;
    }

    static QString status(const pdf::PDFAppToolTab* tab)
    {
        return tab->getToolPage()->getTabView()->getToolMessage();
    }

    /// The comparison of the old Diff (start, synchronous) of the documents of the slots
    static pdf::PDFDiffResult compare(const pdf::PDFAppToolGUICompare* gui)
    {
        const pdf::PDFAppToolInput* original = gui->getSlotInput(pdf::PDFAppToolGUICompare::OriginalSlot);
        const pdf::PDFAppToolInput* revised = gui->getSlotInput(pdf::PDFAppToolGUICompare::RevisedSlot);
        pdf::PDFDiff diff(nullptr);
        diff.setOption(pdf::PDFDiff::Asynchronous, false);
        diff.setLeftDocument(original->document.data());
        diff.setRightDocument(revised->document.data());
        pdf::PDFClosedIntervalSet originalPages;
        originalPages.addInterval(0, original->pageCount - 1);
        pdf::PDFClosedIntervalSet revisedPages;
        revisedPages.addInterval(0, revised->pageCount - 1);
        diff.setPagesForLeftDocument(originalPages);
        diff.setPagesForRightDocument(revisedPages);
        diff.start();
        return diff.getResult();
    }

    /// Shows the comparison of the slots as a result of the tab
    static bool showComparison(pdf::PDFAppToolGUICompare* gui)
    {
        const pdf::PDFDiffResult result = compare(gui);
        if (!result.getResult())
        {
            return false;
        }
        gui->showResult(result, gui->getSlotInput(pdf::PDFAppToolGUICompare::OriginalSlot)->createToolInputDocument(),
                        gui->getSlotInput(pdf::PDFAppToolGUICompare::RevisedSlot)->createToolInputDocument());
        AppToolOwnTabFixture::settle();
        return true;
    }

    /// A report of 4 pages A4 with text and a chart (the data of the frames 492:19314); the revised version has
    /// a replaced text, no chart on page 2 and a new box on page 3
    static QString report(const QString& folder, const QString& name, bool isRevised)
    {
        pdf::PDFDocumentBuilder builder;
        pdf::PDFPageContentStreamBuilder content(&builder);
        for (int page = 0; page < 4; ++page)
        {
            QPainter* painter = content.beginNewPage(QRectF(0, 0, 595, 842));
            QFont font = painter->font();
            font.setPixelSize(12);
            painter->setFont(font);
            painter->setPen(Qt::black);
            painter->drawText(QPointF(60, 80), QStringLiteral("Market Analysis %1").arg(page + 1));
            painter->drawText(QPointF(60, 120), (isRevised && page == 0) ? QStringLiteral("Revenue grew by 6 % in Q4 2025.") : QStringLiteral("Revenue grew by 4 % in Q3 2025."));
            painter->drawText(QPointF(60, 140), QStringLiteral("The southern region recorded a slight decline."));
            if (!isRevised || page != 1)
            {
                painter->fillRect(QRectF(80, 300, 200, 120), QColor(120, 80, 200));
            }
            if (isRevised && page == 2)
            {
                painter->fillRect(QRectF(300, 500, 100, 40), Qt::black);
            }
            content.end(painter);
        }
        pdf::PDFDocument document = builder.build();
        const QString path = folder + QLatin1Char('/') + name;
        return AppDocumentSamples::write(document, path) ? path : QString();
    }

    /// The window with its popups and dialogs at the device pixel ratio \p ratio over the canvas color
    static QImage capture(QWidget* window, qreal ratio)
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
                const QPoint position = window->mapFromGlobal(widget->mapToGlobal(QPoint(0, 0)));
                widget->render(&painter, position, QRegion(), QWidget::DrawChildren);
            }
        }
        painter.end();
        return image;
    }

    /// Captures the window at 1x and 2x: the number of the pixels of the leak color; PDF4QT_APP_SCREENSHOTS writes
    /// <name>.png and <name>-2x.png
    static int leakPixels(QWidget* window, const QString& name)
    {
        AppToolOwnTabFixture::settle();
        const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
        int count = 0;
        for (const qreal ratio : { 1.0, 2.0 })
        {
            const QImage image = capture(window, ratio);
            count += AppToolOwnTabFixture::countLeakPixels(image);
            if (!folder.isEmpty())
            {
                QDir().mkpath(folder);
                image.save(QDir(folder).absoluteFilePath(name + (ratio > 1.0 ? QStringLiteral("-2x.png") : QStringLiteral(".png"))));
            }
        }
        return count;
    }
};

using H = CompareTestHelper;

void AppToolCompareTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolCompareTest::registration()
{
    // review.compare is implemented with its GUI; its own tab, more comparisons at once (isSingleOwnTab false)
    AppToolOwnTabFixture fixture;
    const QString toolId = QString::fromLatin1(pdf::PDFDocumentToolCompare::TOOL_ID);
    QVERIFY(pdf::PDFDocumentToolRegistry::isImplemented(toolId));
    QVERIFY(pdf::PDFDocumentToolGUIRegistry::getFactory(toolId));
    pdf::PDFAppToolTab* first = H::open(fixture, {});
    pdf::PDFAppToolTab* second = H::open(fixture, {});
    QVERIFY(first && second);
    QVERIFY(first != second);
    pdf::PDFAppToolGUICompare* gui = H::gui(first);
    QVERIFY(gui);
    QVERIFY(!gui->isSingleOwnTab());
    QCOMPARE(gui->getOwnTabIconName(), QStringLiteral("compare"));
    QCOMPARE(gui->getCancelText(), QStringLiteral("Close"));
    QVERIFY(gui->isResultKeptInTool());
}

void AppToolCompareTest::choiceOfTheDocuments()
{
    // 492:17871 → 492:18158 → 492:18424
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, {});
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(gui->getCanvas()->isChoiceShown());
    QVERIFY(!gui->getInfoCallout()->isHidden());
    QVERIFY(gui->getDocumentsSection()->isHidden());
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(gui->getConfirmText(), QStringLiteral("Compare"));
    QCOMPARE(H::status(tab), QStringLiteral("Choose the Original and the Revised document."));
    QCOMPARE(gui->getDifferencesEmptyText()->text(), QStringLiteral("Choose the two documents first."));
    QCOMPARE(gui->getOwnTabPageText(), QStringLiteral("No pages"));

    const QString original = H::rectangles(fixture.folder(), QStringLiteral("Report_v1.pdf"), { QRectF(20, 20, 50, 50), QRectF(20, 20, 50, 50) });
    const QString revised = H::rectangles(fixture.folder(), QStringLiteral("Report_v2.pdf"), { QRectF(20, 20, 50, 50), QRectF(20, 20, 50, 50), QRectF(1, 1, 5, 5) });
    gui->setSlotFile(pdf::PDFAppToolGUICompare::OriginalSlot, original);
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::status(tab), QStringLiteral("Original: Report_v1.pdf. Choose the Revised document."));
    QVERIFY(gui->getCanvas()->isChoiceShown());
    QCOMPARE(gui->getCanvas()->getSlotCard(0)->getInputRow()->getTitle(), QStringLiteral("Report_v1.pdf"));
    QVERIFY(gui->getCanvas()->getSlotCard(0)->getDropArea()->isHidden());
    QVERIFY(!gui->getCanvas()->getSlotCard(1)->getDropArea()->isHidden());

    gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, revised);
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->getCanvas()->isChoiceShown());
    QVERIFY(gui->getInfoCallout()->isHidden());
    QVERIFY(!gui->getDocumentsSection()->isHidden());
    QCOMPARE(gui->getSlotRow(pdf::PDFAppToolGUICompare::OriginalSlot)->getDescription(), QStringLiteral("Report_v1.pdf"));
    QCOMPARE(gui->getSlotRow(pdf::PDFAppToolGUICompare::RevisedSlot)->getDescription(), QStringLiteral("Report_v2.pdf"));

    // icon-tile 492:20086 / 493:19585: bg/accent-subtle with the accent icon
    QCOMPARE(gui->getSlotRow(pdf::PDFAppToolGUICompare::OriginalSlot)->getTone(), pdf::PDFTileRow::Tone::Accent);
    QCOMPARE(gui->getSlotRow(pdf::PDFAppToolGUICompare::RevisedSlot)->getTone(), pdf::PDFTileRow::Tone::Accent);
    QCOMPARE(gui->getPageRangeText(pdf::PDFAppToolGUICompare::OriginalSlot), QStringLiteral("1-2"));
    QCOMPARE(gui->getPageRangeText(pdf::PDFAppToolGUICompare::RevisedSlot), QStringLiteral("1-3"));
    QCOMPARE(gui->getPageCountLabel(pdf::PDFAppToolGUICompare::RevisedSlot)->text(), QStringLiteral("of 3"));
    QVERIFY(gui->isConfirmEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Ready to compare 2 and 3 pages. F5 starts the comparison."));
    QCOMPARE(gui->getDifferencesEmptyText()->text(), QStringLiteral("Differences appear here after you press Compare."));
    QCOMPARE(gui->getCompareView()->getTitleLabel()->text(), QStringLiteral("Not compared yet"));
    QVERIFY(!gui->getCompareView()->getInfoCallout()->isHidden());

    // Before the comparison nothing can be filtered, shown or exported (492:18424)
    QVERIFY(!gui->getMenuPanel(QStringLiteral("filter"))->getButton()->isEnabled());
    QVERIFY(!gui->getMenuPanel(QStringLiteral("show"))->getButton()->isEnabled());
    QVERIFY(gui->getMenuPanel(QStringLiteral("options"))->getButton()->isEnabled());
    QVERIFY(!gui->getMenuPanel(QStringLiteral("export"))->getButton()->isEnabled());
    QVERIFY(!gui->getNextButton()->isEnabled());

    // The pages of both inputs side by side (the document of the view is built in its job)
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return !gui->isViewDocumentPending(); }));
    AppToolOwnTabFixture::settle();
    if (gui->getCompareView()->getDocument())
    {
        QCOMPARE(gui->getCompareView()->getLayout().getLayout().size(), size_t(5));
    }
}

void AppToolCompareTest::openTabsAndUsedDocument()
{
    // OPEN TABS of an empty slot (492:18158); the document of the other slot is Disabled (FC-3); a snapshot of now
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Open.pdf"), 4));
    QVERIFY(documentTab);
    pdf::PDFAppToolTab* tab = H::open(fixture, {});
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);

    std::vector<pdf::PDFTileRow*> rows = gui->getCanvas()->getSlotCard(0)->getDocumentRows();
    QCOMPARE(rows.size(), size_t(1));
    QCOMPARE(rows.front()->getTitle(), QStringLiteral("Open.pdf"));
    QCOMPARE(rows.front()->getDescription(), QStringLiteral("4 pages"));
    QVERIFY(rows.front()->isEnabled());
    rows.front()->click();
    AppToolOwnTabFixture::settle();

    const pdf::PDFAppToolInput* input = gui->getSlotInput(pdf::PDFAppToolGUICompare::OriginalSlot);
    QVERIFY(input);
    QCOMPARE(input->kind, pdf::PDFAppToolInputKind::OpenTab);
    QCOMPARE(input->document.data(), documentTab->getDocument()->getDocument().data());
    QCOMPARE(gui->getCanvas()->getSlotCard(0)->getInputRow()->getDescription(), QStringLiteral("4 pages · open tab"));

    // The other slot offers the same document Disabled
    rows = gui->getCanvas()->getSlotCard(1)->getDocumentRows();
    QCOMPARE(rows.size(), size_t(1));
    QVERIFY(!rows.front()->isEnabled());
    QCOMPARE(rows.front()->getDescription(), QStringLiteral("Already used as the Original"));

    // Change… of the Revised (the panel Documents): another file or an open tab, the document of the Original Disabled
    gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("File.pdf"), 2));
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    Q_EMIT gui->getSlotRow(pdf::PDFAppToolGUICompare::RevisedSlot)->linkClicked();
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolPanelPopup* panel = gui->getChangePanel();
    QVERIFY(panel);
    QCOMPARE(panel->getRows().size(), size_t(2));
    QCOMPARE(panel->getRows()[0]->getTitle(), QStringLiteral("PDF file…"));
    QCOMPARE(panel->getRows()[1]->getTitle(), QStringLiteral("Open.pdf"));
    QVERIFY(!panel->getRows()[1]->isEnabled());
    panel->close();
}

void AppToolCompareTest::pageRanges()
{
    // K7.3: the parser and the texts of Page selection (492:105816)
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 12), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 3) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(gui->isConfirmEnabled());

    gui->setPageRangeText(pdf::PDFAppToolGUICompare::OriginalSlot, QStringLiteral("30-12"));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getPageRangeError(pdf::PDFAppToolGUICompare::OriginalSlot), QStringLiteral("The first page (30) is after the last page (12)."));
    QCOMPARE(gui->getPageRangeErrorLabel(pdf::PDFAppToolGUICompare::OriginalSlot)->text(), gui->getPageRangeError(pdf::PDFAppToolGUICompare::OriginalSlot));
    QVERIFY(gui->getPageRangeField(pdf::PDFAppToolGUICompare::OriginalSlot)->isInvalid());
    QVERIFY(!gui->isConfirmEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Fix the pages of the Original to compare."));

    // A page after the last one
    gui->setPageRangeText(pdf::PDFAppToolGUICompare::RevisedSlot, QStringLiteral("2-4"));
    QVERIFY(!gui->getPageRangeError(pdf::PDFAppToolGUICompare::RevisedSlot).isEmpty());

    gui->setPageRangeText(pdf::PDFAppToolGUICompare::OriginalSlot, QStringLiteral("3-5, 8"));
    gui->setPageRangeText(pdf::PDFAppToolGUICompare::RevisedSlot, QStringLiteral("2-3"));
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getPageRangeError(pdf::PDFAppToolGUICompare::OriginalSlot).isEmpty());
    QVERIFY(!gui->getPageRangeField(pdf::PDFAppToolGUICompare::OriginalSlot)->isInvalid());
    QVERIFY(gui->isConfirmEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Ready to compare 4 and 2 pages. F5 starts the comparison."));

    // The run gets the ranges (PageSelection, from 1)
    const QVariantMap parameters = gui->getRunParameters();
    const auto selection = pdf::PDFDocumentToolPageSelection::fromVariant(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES)), nullptr);
    QVERIFY(selection);
    QCOMPARE(selection->pages, QStringLiteral("3-5, 8"));
    QCOMPARE(parameters.value(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_OUTPUT)).toString(), QString::fromLatin1(pdf::PDFDocumentToolCompare::OUTPUT_SUMMARY));
}

void AppToolCompareTest::swapSlots()
{
    // Swap: the documents and their ranges change places
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 5) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    gui->setPageRangeText(pdf::PDFAppToolGUICompare::RevisedSlot, QStringLiteral("2-4"));
    QCOMPARE(gui->getSlotInput(pdf::PDFAppToolGUICompare::OriginalSlot)->name, QStringLiteral("a.pdf"));

    gui->getSwapButton()->click();
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getSlotInput(pdf::PDFAppToolGUICompare::OriginalSlot)->name, QStringLiteral("b.pdf"));
    QCOMPARE(gui->getSlotInput(pdf::PDFAppToolGUICompare::RevisedSlot)->name, QStringLiteral("a.pdf"));
    QCOMPARE(gui->getPageRangeText(pdf::PDFAppToolGUICompare::OriginalSlot), QStringLiteral("2-4"));
    QCOMPARE(gui->getPageRangeText(pdf::PDFAppToolGUICompare::RevisedSlot), QStringLiteral("1-2"));
    QCOMPARE(gui->getSlotRow(pdf::PDFAppToolGUICompare::OriginalSlot)->getDescription(), QStringLiteral("b.pdf"));
}

void AppToolCompareTest::resultFiltersAndList()
{
    // 492:19314, 492:20366, 492:104504: the list grouped by pages, the counts of the menu Filter, "N shown · M hidden",
    // "All differences are hidden by the filter." (FC-9)
    AppToolOwnTabFixture fixture;
    const QString original = H::rectangles(fixture.folder(), QStringLiteral("v1.pdf"), { QRectF(20, 20, 50, 50) });
    const QString revised = H::rectangles(fixture.folder(), QStringLiteral("v2.pdf"), { QRectF(200, 200, 50, 50), QRectF(10, 10, 20, 20) });
    pdf::PDFAppToolTab* tab = H::open(fixture, { original, revised });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::showComparison(gui));
    QCOMPARE(gui->getContent(), pdf::PDFAppToolGUICompare::Content::Result);

    const pdf::PDFDiffResult& result = gui->getResult();
    QVERIFY(result.isChanged());
    int vectorCount = 0;
    int pageCount = 0;
    for (size_t i = 0; i < result.getDifferencesCount(); ++i)
    {
        vectorCount += pdf::PDFDocumentToolCompare::getCategoryId(result.getType(i)) == QLatin1String(pdf::PDFDocumentToolCompare::CATEGORY_VECTOR_GRAPHICS) ? 1 : 0;
        pageCount += pdf::PDFDocumentToolCompare::getCategoryId(result.getType(i)) == QLatin1String(pdf::PDFDocumentToolCompare::CATEGORY_PAGE_MOVEMENT) ? 1 : 0;
    }
    QVERIFY(vectorCount > 0);
    QCOMPARE(pageCount, 1);     // the second page of the revised document is added
    QCOMPARE(gui->getCategoryCount(QString::fromLatin1(pdf::PDFDocumentToolCompare::CATEGORY_VECTOR_GRAPHICS)), vectorCount);
    QCOMPARE(gui->getCategoryCount(QString::fromLatin1(pdf::PDFDocumentToolCompare::CATEGORY_TEXT)), 0);

    // The menu Filter: counts, a kind without a difference Disabled
    pdf::PDFTileRow* textRow = gui->getMenuRow(QStringLiteral("filter-includeText"));
    pdf::PDFTileRow* vectorRow = gui->getMenuRow(QStringLiteral("filter-includeVectorGraphics"));
    pdf::PDFTileRow* pageRow = gui->getMenuRow(QStringLiteral("filter-includePageMovement"));
    QVERIFY(textRow && vectorRow && pageRow);
    QVERIFY(!textRow->isEnabled());
    QVERIFY(vectorRow->isEnabled());
    QCOMPARE(vectorRow->getDescription(), pdf::PDFAppToolGUICompare::tr("%Ln differences", nullptr, vectorCount));
    QVERIFY(vectorRow->getDescription().startsWith(QString::number(vectorCount)));
    QCOMPARE(pageRow->getDescription(), pdf::PDFAppToolGUICompare::tr("%Ln differences – moved, added and removed pages", nullptr, 1));
    QVERIFY(pageRow->getDescription().startsWith(QStringLiteral("1 ")));
    QVERIFY(gui->getMenuPanel(QStringLiteral("filter"))->getButton()->isEnabled());

    // The status bar names the open menu (492:19859, 492:20402, 492:20966, 492:21515, 492:22046); closed: the result
    const QString resultStatus = H::status(tab);
    pdf::PDFAppToolToolbar* toolbar = AppToolOwnTabFixture::getShell(tab)->getToolbar();
    const std::array<std::pair<QString, QString>, 5> menuTexts = { std::make_pair(QStringLiteral("view"), QStringLiteral("View menu.")),
                                                                   std::make_pair(QStringLiteral("filter"), QStringLiteral("Filter menu – choose the kinds of differences to show.")),
                                                                   std::make_pair(QStringLiteral("show"), QStringLiteral("Show menu.")),
                                                                   std::make_pair(QStringLiteral("options"), QStringLiteral("Options of the comparison.")),
                                                                   std::make_pair(QStringLiteral("export"), QStringLiteral("Export menu.")) };
    for (const auto& [menuId, text] : menuTexts)
    {
        pdf::PDFAppToolPanelPopup* panel = toolbar->openPanel(gui->getMenuPanel(menuId)->getButton());
        QVERIFY2(panel, qPrintable(menuId));
        AppToolOwnTabFixture::settle();
        QCOMPARE(H::status(tab), text);
        panel->close();
        AppToolOwnTabFixture::settle();
        QCOMPARE(H::status(tab), resultStatus);
    }

    // The list: the pages of the differences as groups, the differences as their rows
    const pdf::PDFAppCompareDifferenceModel* model = gui->getDifferenceModel();
    int rows = 0;
    for (int group = 0; group < model->rowCount(); ++group)
    {
        rows += model->rowCount(model->index(group, 0));
    }
    QCOMPARE(size_t(rows), result.getDifferencesCount());
    QStringList groupTitles;
    for (int group = 0; group < model->rowCount(); ++group)
    {
        groupTitles << model->data(model->index(group, 0)).toString();
    }
    QVERIFY2(groupTitles.contains(QStringLiteral("Page 1")), qPrintable(groupTitles.join(QStringLiteral(" | "))));
    QVERIFY2(groupTitles.contains(QStringLiteral("Revised 2")), qPrintable(groupTitles.join(QStringLiteral(" | "))));
    QCOMPARE(gui->getDifferencesHeader()->getSubtitleLabel()->text(), pdf::PDFAppToolGUICompare::tr("%Ln differences", nullptr, int(result.getDifferencesCount())) + QStringLiteral(" ") + pdf::PDFAppToolGUICompare::tr("on %Ln pages", nullptr, 2));

    // The filter of the page movement: hidden, the counts stay of the whole result
    pdf::PDFAppToolParameterModel* parameters = AppToolOwnTabFixture::getShell(tab)->getModel();
    parameters->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_INCLUDE_PAGE_MOVEMENT), false);
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getShownResult().getDifferencesCount(), result.getDifferencesCount() - 1);
    QCOMPARE(gui->getDifferencesHeader()->getSubtitleLabel()->text(), QStringLiteral("%1 shown · 1 hidden by the filter").arg(result.getDifferencesCount() - 1));
    QCOMPARE(gui->getCategoryCount(QString::fromLatin1(pdf::PDFDocumentToolCompare::CATEGORY_PAGE_MOVEMENT)), 1);
    QVERIFY(!pageRow->isChecked());

    // All hidden
    parameters->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_INCLUDE_VECTOR_GRAPHICS), false);
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getShownResult().isSame());
    QCOMPARE(gui->getDifferencesEmptyText()->text(), QStringLiteral("All differences are hidden by the filter."));
    QVERIFY(!gui->isExportEnabled());

    // The view and the filters never make the result stale (only the options and the inputs)
    QVERIFY(!gui->isResultStaleBy(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_VIEW)));
    QVERIFY(!gui->isResultStaleBy(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_INCLUDE_TEXT)));
    QVERIFY(gui->isResultStaleBy(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_COMPARE_CHARACTERS)));
    QCOMPARE(gui->getStaleTitle(), QStringLiteral("Options changed"));
    QVERIFY(gui->isResultStaleBy(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_ORIGINAL_PAGES)));
    QCOMPARE(gui->getStaleTitle(), QStringLiteral("The documents changed"));
}

void AppToolCompareTest::navigation()
{
    // F6 / Shift+F6 (the old Next / Previous Difference), "Difference 1 of N", the list follows
    AppToolOwnTabFixture fixture;
    const QString original = H::rectangles(fixture.folder(), QStringLiteral("v1.pdf"), { QRectF(20, 20, 50, 50) });
    const QString revised = H::rectangles(fixture.folder(), QStringLiteral("v2.pdf"), { QRectF(200, 200, 50, 50), QRectF(10, 10, 20, 20) });
    pdf::PDFAppToolTab* tab = H::open(fixture, { original, revised });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::showComparison(gui));
    const size_t count = gui->getShownResult().getDifferencesCount();
    QVERIFY(count >= 2);

    QVERIFY(!gui->getSelectedDifference());
    QVERIFY(gui->getNextButton()->isEnabled());
    QVERIFY(!gui->getPreviousButton()->isEnabled());
    gui->getNextButton()->click();
    QCOMPARE(gui->getSelectedDifference(), std::optional<size_t>(0));
    QCOMPARE(gui->getCompareView()->getDetailLabel()->text(), QStringLiteral("Difference 1 of %1").arg(count));
    QCOMPARE(gui->getDifferenceModel()->getDifferenceIndex(gui->getDifferenceView()->currentIndex()), std::optional<size_t>(0));
    gui->goToNextDifference();
    QCOMPARE(gui->getSelectedDifference(), std::optional<size_t>(1));
    QVERIFY(gui->getPreviousButton()->isEnabled());
    gui->goToPreviousDifference();
    QCOMPARE(gui->getSelectedDifference(), std::optional<size_t>(0));

    // The last difference: no next one
    gui->selectDifference(count - 1);
    QVERIFY(!gui->canGoToNextDifference());
    gui->goToNextDifference();
    QCOMPARE(gui->getSelectedDifference(), std::optional<size_t>(count - 1));

    // A click into the list selects the difference. Qt reads the selection model after its signal currentChanged:
    // the code of the application (the status text, which may delete the tab) runs after the signal, not inside it
    // (CLAUDE.md lifetime; the measure list crashed so in the lifetime sweep)
    int statusChanges = 0;
    QObject context;
    QObject::connect(gui, &pdf::PDFAppToolGUICompare::statusTextChanged, &context, [&statusChanges]() { ++statusChanges; });
    const QModelIndex first = gui->getDifferenceModel()->findIndex(0);
    gui->getDifferenceView()->setCurrentIndex(first);
    QCOMPARE(statusChanges, 0);
    AppToolOwnTabFixture::settle();
    QVERIFY(statusChanges > 0);
    QCOMPARE(gui->getSelectedDifference(), std::optional<size_t>(0));

    // The keys F6 and Shift+F6 are actions of the tab
    QAction* next = tab->findChild<QAction*>(QStringLiteral("compareNextAction"));
    QAction* previous = tab->findChild<QAction*>(QStringLiteral("comparePreviousAction"));
    QVERIFY(next && previous);
    QCOMPARE(next->shortcut(), QKeySequence(Qt::Key_F6));
    QCOMPARE(previous->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F6));
    next->trigger();
    QCOMPARE(gui->getSelectedDifference(), std::optional<size_t>(1));
}

void AppToolCompareTest::exportRules()
{
    // R7-9: no export without differences; the XML of the shown differences opens the dialog of the export
    AppToolOwnTabFixture fixture;
    const QString same = H::rectangles(fixture.folder(), QStringLiteral("same.pdf"), { QRectF(20, 20, 50, 50) });
    const QString sameCopy = H::rectangles(fixture.folder(), QStringLiteral("same_copy.pdf"), { QRectF(20, 20, 50, 50) });
    pdf::PDFAppToolTab* tab = H::open(fixture, { same, sameCopy });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(!gui->isExportEnabled());
    QVERIFY(H::showComparison(gui));
    QCOMPARE(gui->getContent(), pdf::PDFAppToolGUICompare::Content::NoDifferences);
    QVERIFY(!gui->isExportEnabled());
    QVERIFY(!gui->exportXml());
    QVERIFY(!gui->getMenuPanel(QStringLiteral("export"))->getButton()->isEnabled());
    QCOMPARE(gui->getDifferencesEmptyText()->text(), QStringLiteral("The compared pages are identical."));
    QVERIFY(!gui->getCompareView()->isPagesShown());
    QCOMPARE(gui->getCompareView()->getMessageTitle()->text(), QStringLiteral("No differences found"));
    QCOMPARE(H::status(tab), QStringLiteral("No differences found between the compared pages."));

    // Differences: the export is enabled and the XML opens the dialog with one file of the shown differences
    const QString other = H::rectangles(fixture.folder(), QStringLiteral("other.pdf"), { QRectF(200, 200, 50, 50) });
    gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, other);
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QVERIFY(H::showComparison(gui));
    QVERIFY(gui->isExportEnabled());
    QVERIFY(gui->getMenuRow(QStringLiteral("exportXml"))->isEnabled());
    QVERIFY(gui->exportXml());
    pdf::PDFAppToolResultDialog* dialog = AppToolOwnTabFixture::getShell(tab)->getResultDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->isExport());
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("same vs other - differences.xml"));

    // 492:108839: the texts of the dialog, no option to open the XML file, Save File; the facts of the list
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultTitle"))->text(), QStringLiteral("List of differences is ready"));
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultSubtitle"))->text(), QStringLiteral("Nothing has been written yet – choose where to save the XML file."));
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save File"));
    QVERIFY(!dialog->getOpenCheckBox() || dialog->getOpenCheckBox()->isHidden());
    QCOMPARE(dialog->getStatusText(), QStringLiteral("The list of differences is ready to be saved."));
    const pdf::PDFDiffResult expected = H::compare(gui);
    pdf::PDFKeyValueList* facts = dialog->getFacts();
    QVERIFY(facts);
    QCOMPARE(facts->getRowCount(), 3);
    QCOMPARE(facts->getKey(0), QStringLiteral("Differences"));
    QCOMPARE(facts->getValue(0), QStringLiteral("%1 shown differences").arg(expected.getDifferencesCount()));
    QCOMPARE(facts->getKey(1), QStringLiteral("Content"));
    QCOMPARE(facts->getValue(1), QStringLiteral("Type, pages and changed text"));
    QCOMPARE(facts->getKey(2), QStringLiteral("Page sequence"));
    QCOMPARE(facts->getValue(2), QStringLiteral("%1 page pairs").arg(expected.getPageSequence().size()));

    // An existing file of the name (492:108278): the confirm button replaces it
    QFile existing(dialog->getTargetFilePath());
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.close();
    dialog->getConfirmButton()->click();
    AppToolOwnTabFixture::settle();
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Replace File"));
    QVERIFY(!dialog->getNameError()->isHidden());
    dialog->getDiscardButton()->click();
    AppToolOwnTabFixture::settle();
}

void AppToolCompareTest::colorsWithSwap()
{
    // The colors of the four kinds (the tokens diff/*): a color of another kind swaps (two kinds never share one); the
    // colors are the last values of the tool
    const pdf::PDFDiffReportColors defaults;
    const pdf::PDFDiffReportColors swapped = pdf::PDFAppToolGUICompare::chooseColor(defaults, pdf::PDFAppToolGUICompare::AddedColor, defaults.removed);
    QCOMPARE(swapped.added, defaults.removed);
    QCOMPARE(swapped.removed, defaults.added);
    QCOMPARE(swapped.replaced, defaults.replaced);
    QCOMPARE(swapped.moved, defaults.moved);
    const pdf::PDFDiffReportColors custom = pdf::PDFAppToolGUICompare::chooseColor(defaults, pdf::PDFAppToolGUICompare::MovedColor, QColor(1, 2, 3));
    QCOMPARE(custom.moved, QColor(1, 2, 3));
    QCOMPARE(custom.removed, defaults.removed);

    // The swatches of a row: red, orange, green, blue, black of the annotation colors, the default of the kind at its hue
    const std::array<QColor, 5> swatches = pdf::PDFAppCompareColorRow::getSwatchColors(defaults.added, 2);
    QCOMPARE(swatches[2], QColor(0x7D, 0xFA, 0x00));
    QCOMPARE(swatches[4], pdf::PDFThemeColors().getColor(pdf::PDFThemeColors::AnnotationBlack));

    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1), AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 1) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QCOMPARE(gui->getColors().moved, QColor(0xA8, 0x55, 0xF7));
    QVERIFY(gui->getColorRow(pdf::PDFAppToolGUICompare::RemovedColor)->getSwatch(0)->isSelected());
    gui->getColorRow(pdf::PDFAppToolGUICompare::RemovedColor)->getSwatch(1)->click();
    QCOMPARE(gui->getColors().removed, pdf::PDFThemeColors().getColor(pdf::PDFThemeColors::AnnotationOrange));
    QVERIFY(gui->getColorRow(pdf::PDFAppToolGUICompare::RemovedColor)->getSwatch(1)->isSelected());
    QVERIFY(!gui->getColorRow(pdf::PDFAppToolGUICompare::RemovedColor)->getSwatch(0)->isSelected());
    QCOMPARE(gui->getColors().replaced, defaults.replaced);

    // A new tab reads the stored colors
    pdf::PDFAppToolTab* second = H::open(fixture, {});
    pdf::PDFAppToolGUICompare* secondGui = H::gui(second);
    QVERIFY(secondGui);
    QCOMPARE(secondGui->getColors().removed, gui->getColors().removed);
}

void AppToolCompareTest::sourceTabClosed()
{
    // FC-5: the close of the source tab of a slot empties the slot (the state of one input, 492:18158)
    AppToolOwnTabFixture fixture;
    pdf::PDFAppDocumentTab* documentTab = fixture.open(AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Source.pdf"), 2));
    QVERIFY(documentTab);
    const QString documentId = documentTab->getDocument()->getId();
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    gui->setSlotDocument(pdf::PDFAppToolGUICompare::RevisedSlot, documentId);
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->isSlotComparable(pdf::PDFAppToolGUICompare::RevisedSlot));
    QVERIFY(!gui->getCanvas()->isChoiceShown());

    QVERIFY(fixture.window->closeTab(documentTab));
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return !gui->getSlotInput(pdf::PDFAppToolGUICompare::RevisedSlot); }));
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getCanvas()->isChoiceShown());
    QCOMPARE(gui->getSlotInput(pdf::PDFAppToolGUICompare::OriginalSlot)->name, QStringLiteral("a.pdf"));
    QCOMPARE(H::status(tab), QStringLiteral("Original: a.pdf. Choose the Revised document."));
}

void AppToolCompareTest::passwordOfAnInput()
{
    // 492:106538: an encrypted input asks for its password at once (the dialog of step 2); 492:106266: a damaged file
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 2) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, AppDocumentSamples::userPassword(fixture.folder()));
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->getPasswordDialog());
    QCOMPARE(gui->getPasswordDialog()->getInputId(), gui->getSlotInput(pdf::PDFAppToolGUICompare::RevisedSlot)->id);
    QVERIFY(!gui->isConfirmEnabled());
    QVERIFY(H::status(tab).endsWith(QStringLiteral("is encrypted.")));
    gui->getPasswordDialog()->close();
    AppToolOwnTabFixture::settle();

    QFile damaged(fixture.folder() + QStringLiteral("/Scan_2019.pdf"));
    QVERIFY(damaged.open(QFile::WriteOnly));
    damaged.write("%PDF-1.7\nnot a document");
    damaged.close();
    gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, damaged.fileName());
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getCanvas()->getSlotCard(1)->getErrorCallout()->getTitleLabel()->text(), QStringLiteral("Scan_2019.pdf cannot be read"));
    QVERIFY(!gui->getCanvas()->getSlotCard(1)->getDropArea()->isHidden());
    QCOMPARE(H::status(tab), QStringLiteral("Scan_2019.pdf cannot be read."));
}

void AppToolCompareTest::runAndStale()
{
    // The run of the tool (PDFDocumentToolCompare::compare of the core, package P5): the result is kept in the tool,
    // Compare Again Disabled after it, Primary when an option changes (Q7.17, 492:109399)
    AppToolOwnTabFixture fixture;
    const QString original = H::rectangles(fixture.folder(), QStringLiteral("v1.pdf"), { QRectF(20, 20, 50, 50) });
    const QString revised = H::rectangles(fixture.folder(), QStringLiteral("v2.pdf"), { QRectF(200, 200, 50, 50) });
    pdf::PDFAppToolTab* tab = H::open(fixture, { original, revised });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(gui && shell);
    shell->confirm();
    QVERIFY(AppToolOwnTabFixture::waitFor([gui, shell]() { return shell->getState() == pdf::PDFDocumentToolShell::State::Editing && gui->getContent() != pdf::PDFAppToolGUICompare::Content::Running && gui->getContent() != pdf::PDFAppToolGUICompare::Content::Ready; }));
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getContent(), pdf::PDFAppToolGUICompare::Content::Result);
    QVERIFY(gui->getResult().isChanged());
    QCOMPARE(gui->getResultState(), pdf::PDFAppToolResultState::Current);
    QCOMPARE(gui->getConfirmText(), QStringLiteral("Compare Again"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());

    // An option: stale, the amber callout "Options changed", Compare Again enabled
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_COMPARE_CHARACTERS), true);
    AppToolOwnTabFixture::settle();
    QCOMPARE(gui->getResultState(), pdf::PDFAppToolResultState::Stale);
    QVERIFY(shell->getStaleCallout());
    QCOMPARE(shell->getStaleCallout()->getTitleLabel()->text(), QStringLiteral("Options changed"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(H::status(tab), QStringLiteral("Options changed – compare again to refresh the differences."));
}

void AppToolCompareTest::commandLineTab()
{
    // Q7.13: PDF4QT --compare a.pdf b.pdf opens a tab of Compare with both inputs and the comparison starts
    AppToolOwnTabFixture fixture;
    const QString original = H::rectangles(fixture.folder(), QStringLiteral("cl1.pdf"), { QRectF(20, 20, 50, 50) });
    const QString revised = H::rectangles(fixture.folder(), QStringLiteral("cl2.pdf"), { QRectF(20, 20, 50, 50) });
    QVERIFY(!pdf::PDFAppToolGUICompare::openCompareTab(fixture.window, { original }));
    pdf::PDFAppToolTab* tab = pdf::PDFAppToolGUICompare::openCompareTab(fixture.window, { original, revised });
    QVERIFY(tab);
    QCOMPARE(tab->getToolId(), QString::fromLatin1(pdf::PDFDocumentToolCompare::TOOL_ID));
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QCOMPARE(tab->getInputList()->getCount(), 2);
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return gui->getContent() == pdf::PDFAppToolGUICompare::Content::NoDifferences || gui->getContent() == pdf::PDFAppToolGUICompare::Content::Result || gui->getContent() == pdf::PDFAppToolGUICompare::Content::Failed; }));
    QCOMPARE(gui->getContent(), pdf::PDFAppToolGUICompare::Content::NoDifferences);
    QVERIFY(!tab->getInputList()->property(pdf::PDFAppToolGUICompare::START_PROPERTY).toBool());
}

void AppToolCompareTest::framesInBothThemes()
{
    // The frames of 2.1 (492:17871 … 492:109399, 493:18777) in both themes, at 1x and 2x and right-to-left: no pixel
    // of the leak color #A000FF (CLAUDE.md, Fluent); PDF4QT_APP_SCREENSHOTS=<folder> writes them by the frame names
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("-light") : QStringLiteral("-dark");
        AppToolOwnTabFixture fixture;
        QWidget* window = fixture.window;
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);   // after the context, which applies its theme
        const QString original = H::report(fixture.folder(), QStringLiteral("Report_v1.pdf"), false);
        const QString revised = H::report(fixture.folder(), QStringLiteral("Report_v2.pdf"), true);

        pdf::PDFAppToolTab* tab = H::open(fixture, {});
        pdf::PDFAppToolGUICompare* gui = H::gui(tab);
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        QVERIFY(gui && shell);
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-empty") + suffix), 0);
        gui->setSlotFile(pdf::PDFAppToolGUICompare::OriginalSlot, original);
        QVERIFY(H::waitLoaded(tab->getInputList()));
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-empty-one") + suffix), 0);
        gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, revised);
        QVERIFY(H::waitLoaded(tab->getInputList()));
        QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return !gui->isViewDocumentPending(); }));
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare") + suffix), 0);

        shell->confirm();
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-running") + suffix), 0);
        QVERIFY(AppToolOwnTabFixture::waitFor([gui, shell]() { return shell->getState() == pdf::PDFDocumentToolShell::State::Editing && gui->getContent() == pdf::PDFAppToolGUICompare::Content::Result; }));
        gui->selectDifference(0);
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-done") + suffix), 0);

        pdf::PDFAppToolToolbar* toolbar = shell->getToolbar();
        for (const QString& menuId : { QStringLiteral("view"), QStringLiteral("filter"), QStringLiteral("show"), QStringLiteral("options"), QStringLiteral("export") })
        {
            pdf::PDFAppToolPanelPopup* panel = toolbar->openPanel(gui->getMenuPanel(menuId)->getButton());
            QVERIFY2(panel, qPrintable(menuId));
            QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-menus-") + menuId + suffix), 0);
            panel->close();
            AppToolOwnTabFixture::settle();
        }

        const std::array<std::pair<pdf::PDFDiffReportView, QString>, 3> views = { std::make_pair(pdf::PDFDiffReportView::Original, QStringLiteral("review-compare-original")),
                                                                                  std::make_pair(pdf::PDFDiffReportView::Revised, QStringLiteral("review-compare-revised")),
                                                                                  std::make_pair(pdf::PDFDiffReportView::Overlay, QStringLiteral("review-compare-overlay")) };
        for (const auto& [view, name] : views)
        {
            gui->setView(view);
            QCOMPARE(H::leakPixels(window, name + suffix), 0);
        }
        gui->setView(pdf::PDFDiffReportView::SideBySide);

        shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_INCLUDE_TEXT), false);
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-filtered") + suffix), 0);
        shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_INCLUDE_TEXT), true);

        QVERIFY(gui->exportXml());
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-xml-done") + suffix), 0);
        shell->getResultDialog()->getDiscardButton()->click();
        AppToolOwnTabFixture::settle();
        QVERIFY(gui->exportReport());
        QVERIFY(AppToolOwnTabFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-report-done") + suffix), 0);
        shell->getResultDialog()->getDiscardButton()->click();
        AppToolOwnTabFixture::settle();

        QApplication::setLayoutDirection(Qt::RightToLeft);
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-done-rtl") + suffix), 0);
        QApplication::setLayoutDirection(Qt::LeftToRight);

        shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_COMPARE_CHARACTERS), true);
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-stale") + suffix), 0);

        pdf::PDFAppToolTab* same = H::open(fixture, { original, H::report(fixture.folder(), QStringLiteral("Report_copy.pdf"), false) });
        pdf::PDFAppToolGUICompare* sameGui = H::gui(same);
        QVERIFY(sameGui);
        AppToolOwnTabFixture::getShell(same)->confirm();
        QVERIFY(AppToolOwnTabFixture::waitFor([sameGui]() { return sameGui->getContent() == pdf::PDFAppToolGUICompare::Content::NoDifferences; }));
        QCOMPARE(H::leakPixels(window, QStringLiteral("review-compare-no-differences") + suffix), 0);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolCompareTest::languageChange()
{
    // The canvas of the tab stays over a change of the language (the shell makes only the panels again): the choice of
    // the documents (492:18158) and the cards of the slots follow the language, as the rest of the tool
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, {});
    QVERIFY(tab);
    QVERIFY(H::gui(tab));
    const pdf::PDFAppCompareCanvas* canvas = H::gui(tab)->getCanvas();
    QVERIFY(canvas);
    QCOMPARE(canvas->getChoiceTitle()->text(), QStringLiteral("Choose the two documents to compare"));
    QCOMPARE(canvas->getSlotCard(pdf::PDFAppToolGUICompare::RevisedSlot)->getChooseButton()->text(), QStringLiteral("Choose File…"));

    CompareTestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::gui(tab)->getCanvas(), canvas);
    QCOMPARE(canvas->getChoiceTitle()->text(), QStringLiteral("[Choose the two documents to compare]"));
    QCOMPARE(canvas->getSlotCard(pdf::PDFAppToolGUICompare::RevisedSlot)->getChooseButton()->text(), QStringLiteral("[Choose File…]"));
    QCOMPARE(canvas->getSlotCard(pdf::PDFAppToolGUICompare::OriginalSlot)->getPasswordButton()->text(), QStringLiteral("[Enter Password…]"));

    // Back to the source language
    QVERIFY(QCoreApplication::removeTranslator(&translator));
    AppToolOwnTabFixture::settle();
    QCOMPARE(canvas->getChoiceTitle()->text(), QStringLiteral("Choose the two documents to compare"));
    QCOMPARE(canvas->getSlotCard(pdf::PDFAppToolGUICompare::RevisedSlot)->getChooseButton()->text(), QStringLiteral("Choose File…"));
}

void AppToolCompareTest::reportOfReplacedResult()
{
    // Report as PDF… is made in a job of the shown result; when another result replaces it before the job ends (a new
    // comparison after a change of the options), the report is never offered - its dialog would save the pages of the
    // previous result under the facts of the new one (FC-4: a result of other settings is not exported as the shown one)
    AppToolOwnTabFixture fixture;
    const QString original = H::rectangles(fixture.folder(), QStringLiteral("original.pdf"), { QRectF(20, 20, 50, 50) });
    const QString revised = H::rectangles(fixture.folder(), QStringLiteral("revised.pdf"), { QRectF(200, 200, 50, 50) });
    pdf::PDFAppToolTab* tab = H::open(fixture, { original, revised });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::showComparison(gui));
    QVERIFY(gui->isExportEnabled());
    QVERIFY(gui->exportReport());
    QVERIFY(!gui->isExportEnabled());

    // A new result in the same turn of the event loop (the end of the job is not delivered yet)
    const pdf::PDFDiffResult result = H::compare(gui);
    gui->showResult(result, gui->getSlotInput(pdf::PDFAppToolGUICompare::OriginalSlot)->createToolInputDocument(),
                    gui->getSlotInput(pdf::PDFAppToolGUICompare::RevisedSlot)->createToolInputDocument());
    QThreadPool::globalInstance()->waitForDone();
    AppToolOwnTabFixture::settle();
    QVERIFY(!AppToolOwnTabFixture::getShell(tab)->getResultDialog());
    QVERIFY(gui->isExportEnabled());

    // The report of the shown result is offered (492:107143)
    QVERIFY(gui->exportReport());
    QThreadPool::globalInstance()->waitForDone();
    QVERIFY(AppToolOwnTabFixture::waitFor([tab]() { return AppToolOwnTabFixture::getShell(tab)->getResultDialog() != nullptr; }));
    pdf::PDFAppToolResultDialog* dialog = AppToolOwnTabFixture::getShell(tab)->getResultDialog();
    QCOMPARE(dialog->findChild<pdf::PDFLabel*>(QStringLiteral("toolResultTitle"))->text(), QStringLiteral("Comparison report is ready"));
    dialog->getDiscardButton()->click();
    AppToolOwnTabFixture::settle();
}

void AppToolCompareTest::figmaListAndStatusTexts()
{
    // 492:19314: a row names the change without its page (the page is the title of its group) - "Text 'Q3 2025'
    // replaced by 'Q4 2025'", "Vector graphics added"; the status "14 differences found on 6 pages. F6 next
    // difference, Shift+F6 previous."; the tooltip keeps the whole message
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { H::report(fixture.folder(), QStringLiteral("Report_v1.pdf"), false), H::report(fixture.folder(), QStringLiteral("Report_v2.pdf"), true) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::showComparison(gui));

    // Page 1: "4" → "6" and "Q3" → "Q4" (words); page 2: the chart is gone; page 3: a new box
    const pdf::PDFAppCompareDifferenceModel* model = gui->getDifferenceModel();
    QStringList groups;
    QStringList rows;
    for (int group = 0; group < model->rowCount(); ++group)
    {
        const QModelIndex groupIndex = model->index(group, 0);
        groups << groupIndex.data().toString();
        for (int row = 0; row < model->rowCount(groupIndex); ++row)
        {
            rows << model->index(row, 0, groupIndex).data().toString();
            QVERIFY(model->index(row, 0, groupIndex).data(Qt::ToolTipRole).toString().contains(QStringLiteral("page")));
        }
    }
    QCOMPARE(groups, QStringList({ QStringLiteral("Page 1"), QStringLiteral("Page 2"), QStringLiteral("Page 3") }));
    QCOMPARE(rows, QStringList({ QStringLiteral("Text '4' replaced by '6'"), QStringLiteral("Text 'Q3' replaced by 'Q4'"), QStringLiteral("Vector graphics removed"), QStringLiteral("Vector graphics added") }));
    QCOMPARE(H::status(tab), QStringLiteral("4 differences found on 3 pages. F6 next difference, Shift+F6 previous."));
    QCOMPARE(gui->getDifferencesHeader()->getSubtitleLabel()->text(), QStringLiteral("4 differences on 3 pages"));
}

void AppToolCompareTest::figmaFilteredTexts()
{
    // 492:104504: "Filter · 4 of 5", "12 shown · 2 hidden by the filter", the status "12 differences shown, 2 hidden
    // by the filter. Only pages with differences." and "Only pages with differences · 4 of 25 page pairs"
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { H::report(fixture.folder(), QStringLiteral("Report_v1.pdf"), false), H::report(fixture.folder(), QStringLiteral("Report_v2.pdf"), true) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::showComparison(gui));
    QToolButton* filterButton = gui->getMenuPanel(QStringLiteral("filter"))->getButton();
    QCOMPARE(filterButton->text(), QStringLiteral("Filter"));

    // The vector graphics hidden: 2 of the 4 differences (the texts of page 1) are shown
    AppToolOwnTabFixture::getShell(tab)->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_INCLUDE_VECTOR_GRAPHICS), false);
    AppToolOwnTabFixture::settle();
    QCOMPARE(filterButton->text(), QStringLiteral("Filter · 4 of 5"));
    QCOMPARE(gui->getDifferencesHeader()->getSubtitleLabel()->text(), QStringLiteral("2 shown · 2 hidden by the filter"));
    QCOMPARE(H::status(tab), QStringLiteral("2 differences shown, 2 hidden by the filter."));

    // Only Pages with Differences: page 1 of the 4 pairs
    gui->setShowOption(QString::fromLatin1(pdf::PDFAppToolGUICompare::SETTING_ONLY_CHANGED), true);
    AppToolOwnTabFixture::settle();
    QCOMPARE(H::status(tab), QStringLiteral("2 differences shown, 2 hidden by the filter. Only pages with differences."));
    QCOMPARE(gui->getCompareView()->getDetailLabel()->text(), QStringLiteral("Only pages with differences · 1 of 4 page pairs"));
}

void AppToolCompareTest::figmaPairFitsTheCanvas()
{
    // 492:19314: both pages of a pair are seen whole, "Original · …" over the left page, "Revised · …" over the right
    // one; two equal A4 pages side by side need at most half of the zoom, at which one page fills the width
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { H::report(fixture.folder(), QStringLiteral("Report_v1.pdf"), false), H::report(fixture.folder(), QStringLiteral("Report_v2.pdf"), true) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::showComparison(gui));
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return !gui->isViewDocumentPending(); }));
    AppToolOwnTabFixture::settle();

    pdf::PDFAppCompareView* view = gui->getCompareView();
    QCOMPARE(gui->getView(), pdf::PDFDiffReportView::SideBySide);
    const pdf::PDFInteger original = view->getLayout().getPageIndexFromOriginal(0);
    const pdf::PDFInteger revised = view->getLayout().getPageIndexFromRevised(0);
    QVERIFY(original >= 0 && revised >= 0);
    pdf::PDFDrawWidgetProxy* proxy = view->getPdfWidget()->getDrawWidgetProxy();
    const double onePageFitsWidth = proxy->getZoomHintForPage(pdf::PDFDrawWidgetProxy::ZoomHint::FitWidth, original);
    QVERIFY2(view->getZoom() <= 0.5 * onePageFitsWidth + 1e-9, qPrintable(QStringLiteral("%1 > %2").arg(view->getZoom()).arg(0.5 * onePageFitsWidth)));
    const std::vector<pdf::PDFInteger> visible = view->getPdfWidget()->getDrawWidget()->getCurrentPages();
    QVERIFY(std::find(visible.cbegin(), visible.cend(), original) != visible.cend());
    QVERIFY(std::find(visible.cbegin(), visible.cend(), revised) != visible.cend());

    // Original Only: one page fits
    gui->setView(pdf::PDFDiffReportView::Original);
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return !gui->isViewDocumentPending(); }));
    AppToolOwnTabFixture::settle();
    QVERIFY(view->getZoom() > 0.5 * onePageFitsWidth);
}

void AppToolCompareTest::figmaNoDifferencesCentered()
{
    // 492:105498 empty-state 492:105810: "No differences found" in the middle of the canvas (y 359 of 842 for 124)
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { H::report(fixture.folder(), QStringLiteral("Report_v1.pdf"), false), H::report(fixture.folder(), QStringLiteral("Report_copy.pdf"), false) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::showComparison(gui));
    QCOMPARE(gui->getContent(), pdf::PDFAppToolGUICompare::Content::NoDifferences);
    const QWidget* badge = gui->getCompareView()->findChild<QWidget*>(QStringLiteral("compareViewMessageBadge"));
    const QWidget* text = gui->getCompareView()->getMessageText();
    QVERIFY(badge && text && badge->isVisible());
    const QWidget* page = badge->parentWidget();
    const int above = badge->geometry().top();
    const int below = page->height() - text->geometry().bottom() - 1;
    QVERIFY2(qAbs(above - below) <= 2, qPrintable(QStringLiteral("above %1, below %2").arg(above).arg(below)));
}

void AppToolCompareTest::figmaReportFacts()
{
    // 492:107143: the facts of the report - "25 page pairs, side by side", "14 marked as annotations"; one document:
    // its pages
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { H::report(fixture.folder(), QStringLiteral("Report_v1.pdf"), false), H::report(fixture.folder(), QStringLiteral("Report_v2.pdf"), true) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(gui && shell);
    QVERIFY(H::showComparison(gui));

    const std::array<std::pair<pdf::PDFDiffReportView, QString>, 2> views = { std::make_pair(pdf::PDFDiffReportView::SideBySide, QStringLiteral("4 page pairs, side by side")),
                                                                              std::make_pair(pdf::PDFDiffReportView::Revised, QStringLiteral("4 revised pages")) };
    for (const auto& [view, text] : views)
    {
        gui->setView(view);
        AppToolOwnTabFixture::settle();
        QVERIFY(gui->exportReport());
        QVERIFY(AppToolOwnTabFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
        pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
        QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("Pages"));
        QCOMPARE(dialog->getFacts()->getValue(0), text);
        QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("4 marked as annotations"));
        dialog->getDiscardButton()->click();
        AppToolOwnTabFixture::settle();
        QVERIFY(AppToolOwnTabFixture::waitFor([shell]() { return shell->getResultDialog() == nullptr; }));
    }
}

void AppToolCompareTest::commandLineUnfinishedPair()
{
    // Q7.13: PDF4QT --compare starts the comparison of its two files only. a.pdf twice: the second one is refused as
    // the same file (FC-3), the Revised stays empty and the request ends - a later choice of the user does not start
    // the comparison by itself (492:18424 "Ready to compare", F5 starts it)
    AppToolOwnTabFixture fixture;
    const QString original = H::rectangles(fixture.folder(), QStringLiteral("twice.pdf"), { QRectF(20, 20, 50, 50) });
    const QString revised = H::rectangles(fixture.folder(), QStringLiteral("chosen.pdf"), { QRectF(200, 200, 50, 50) });
    pdf::PDFAppToolTab* tab = pdf::PDFAppToolGUICompare::openCompareTab(fixture.window, { original, original });
    QVERIFY(tab);
    AppToolOwnTabFixture::settle();
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(gui && shell);
    QCOMPARE(tab->getInputList()->getCount(), 1);
    QVERIFY(gui->getCanvas()->isChoiceShown());

    gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, revised);
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    AppToolOwnTabFixture::settle();
    QVERIFY(gui->isConfirmEnabled());
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(gui->getContent(), pdf::PDFAppToolGUICompare::Content::Ready);
    QVERIFY(H::status(tab).endsWith(QStringLiteral("F5 starts the comparison.")));
    QVERIFY(!tab->getInputList()->property(pdf::PDFAppToolGUICompare::START_PROPERTY).isValid());

    // A pair with a file, which cannot be read (492:106266): the request ends, a new Original does not start it
    QFile damaged(fixture.folder() + QStringLiteral("/Scan_2019.pdf"));
    QVERIFY(damaged.open(QFile::WriteOnly));
    damaged.write("%PDF-1.7\nnot a document");
    damaged.close();
    pdf::PDFAppToolTab* second = pdf::PDFAppToolGUICompare::openCompareTab(fixture.window, { damaged.fileName(), revised });
    QVERIFY(second);
    AppToolOwnTabFixture::settle();
    QVERIFY(H::waitLoaded(second->getInputList()));
    AppToolOwnTabFixture::settle();
    pdf::PDFAppToolGUICompare* secondGui = H::gui(second);
    QVERIFY(secondGui);
    QCOMPARE(second->getInputList()->getCount(), 2);
    QVERIFY(!second->getInputList()->property(pdf::PDFAppToolGUICompare::START_PROPERTY).isValid());
    secondGui->setSlotFile(pdf::PDFAppToolGUICompare::OriginalSlot, original);
    QVERIFY(H::waitLoaded(second->getInputList()));
    AppToolOwnTabFixture::settle();
    AppToolOwnTabFixture::settle();
    QVERIFY(secondGui->isConfirmEnabled());
    QCOMPARE(secondGui->getContent(), pdf::PDFAppToolGUICompare::Content::Ready);
}

void AppToolCompareTest::listDeletedDuringChange()
{
    // A change of a slot by the GUI is one change of the list (its signals wait for the end of the change); the change
    // ends on every return - also when a slot of the list deletes the list in the middle of it
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("a.pdf"), 1) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    gui->setSlotFile(pdf::PDFAppToolGUICompare::RevisedSlot, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("b.pdf"), 1));
    QVERIFY(H::waitLoaded(tab->getInputList()));
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->isChangingList());
    QVERIFY(gui->isSlotComparable(pdf::PDFAppToolGUICompare::RevisedSlot));

    // Change… of the Original: the old input is removed first - its signal deletes the list
    QPointer<pdf::PDFAppToolInputList> list(tab->getInputList());
    QObject context;
    QObject::connect(list.data(), &pdf::PDFAppToolInputList::inputsChanged, &context, [&list]() { delete list.data(); });
    gui->setSlotFile(pdf::PDFAppToolGUICompare::OriginalSlot, AppDocumentSamples::simple(fixture.folder(), QStringLiteral("c.pdf"), 1));
    QVERIFY(!list);
    QVERIFY(!gui->isChangingList());
    AppToolOwnTabFixture::settle();
    QVERIFY(!gui->getSlotInput(pdf::PDFAppToolGUICompare::OriginalSlot));
}

void AppToolCompareTest::reportFactsOfTheJob()
{
    // 492:107143: the facts of the dialog of the report are of the result, the filter and the view, from which the
    // report is made (when Report as PDF… is clicked); a change of the view or of the filter before the job of the
    // report ends is not in the report, so it is not in its facts
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { H::report(fixture.folder(), QStringLiteral("Report_v1.pdf"), false), H::report(fixture.folder(), QStringLiteral("Report_v2.pdf"), true) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(gui && shell);
    QVERIFY(H::showComparison(gui));
    QCOMPARE(gui->getView(), pdf::PDFDiffReportView::SideBySide);
    QCOMPARE(gui->getShownResult().getDifferencesCount(), size_t(4));
    QVERIFY(gui->exportReport());

    // The same turn of the event loop (the end of the job is not delivered yet): Revised Only, the vector graphics hidden
    gui->setView(pdf::PDFDiffReportView::Revised);
    shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_INCLUDE_VECTOR_GRAPHICS), false);
    QCOMPARE(gui->getShownResult().getDifferencesCount(), size_t(2));
    QThreadPool::globalInstance()->waitForDone();
    QVERIFY(AppToolOwnTabFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("Pages"));
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("4 page pairs, side by side"));
    QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("4 marked as annotations"));
    dialog->getDiscardButton()->click();
    AppToolOwnTabFixture::settle();
}

void AppToolCompareTest::figmaChosenInputRow()
{
    // 492:18382 (492:18158 empty-one): the chosen input of a slot has the fill bg/accent-subtle of a chosen row, the
    // tile bg/accent-subtle with icon/accent and Change…; it is a text (no click), the empty slot has no such row
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Report_v1.pdf"), 2) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    pdf::PDFTileRow* row = gui->getCanvas()->getSlotCard(pdf::PDFAppToolGUICompare::OriginalSlot)->getInputRow();
    QVERIFY(row && row->isVisible());
    QVERIFY(gui->getCanvas()->getSlotCard(pdf::PDFAppToolGUICompare::RevisedSlot)->getInputRow()->isHidden());
    QVERIFY(!row->isClickable());
    QVERIFY(row->isStaticChosen());
    QVERIFY(row->getContent().chosen);
    QCOMPARE(row->getTone(), pdf::PDFTileRow::Tone::Accent);
    QCOMPARE(row->getLinkText(), QStringLiteral("Change…"));

    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        const QColor surface = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgSurface);
        const QColor accent = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgAccentSubtle);
        const qreal alpha = accent.alphaF();
        const QColor expected = QColor::fromRgbF(accent.redF() * alpha + surface.redF() * (1.0 - alpha), accent.greenF() * alpha + surface.greenF() * (1.0 - alpha),
                                                 accent.blueF() * alpha + surface.blueF() * (1.0 - alpha));
        QImage image(row->size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(surface);
        QPainter painter(&image);
        row->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        painter.end();
        const QColor fill = image.pixelColor(4, row->height() / 2);
        QVERIFY2(qAbs(fill.red() - expected.red()) <= 3 && qAbs(fill.green() - expected.green()) <= 3 && qAbs(fill.blue() - expected.blue()) <= 3,
                 qPrintable(QStringLiteral("%1 instead of %2").arg(fill.name(), expected.name())));
        QCOMPARE(AppToolOwnTabFixture::countLeakPixels(image), 0);
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppToolCompareTest::figmaZoomInStatusBar()
{
    // 492:19314 status-zoom "151 %": the zoom item of the status bar follows the zoom of the canvas (the wheel, the
    // keys, a fit), not only a change of the page
    AppToolOwnTabFixture fixture;
    pdf::PDFAppToolTab* tab = H::open(fixture, { H::report(fixture.folder(), QStringLiteral("Report_v1.pdf"), false), H::report(fixture.folder(), QStringLiteral("Report_v2.pdf"), true) });
    pdf::PDFAppToolGUICompare* gui = H::gui(tab);
    QVERIFY(gui);
    QVERIFY(H::showComparison(gui));
    QVERIFY(AppToolOwnTabFixture::waitFor([gui]() { return !gui->isViewDocumentPending(); }));
    AppToolOwnTabFixture::settle();

    pdf::PDFAppDocumentStatusBar* statusBar = tab->getToolPage()->getTabView()->getStatusBar();
    pdf::PDFAppCompareView* view = gui->getCompareView();
    QVERIFY(statusBar && view);
    const pdf::PDFInteger page = view->getCurrentPageIndex();
    const QString before = statusBar->getZoomText();
    QCOMPARE(before, QStringLiteral("%1 %").arg(qRound64(view->getZoom() * 100.0)));

    // The user zooms in a little (the page of the view stays)
    view->getPdfWidget()->getDrawWidgetProxy()->zoom(view->getZoom() * 1.25);
    AppToolOwnTabFixture::settle();
    QCOMPARE(view->getCurrentPageIndex(), page);
    QVERIFY(statusBar->getZoomText() != before);
    QCOMPARE(statusBar->getZoomText(), QStringLiteral("%1 %").arg(qRound64(view->getZoom() * 100.0)));
}

QTEST_MAIN(AppToolCompareTest)

#include "tst_apptoolcomparetest.moc"
