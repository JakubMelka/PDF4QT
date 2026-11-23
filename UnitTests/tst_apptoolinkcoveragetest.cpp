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

// The GUI of Ink Coverage (step 7 of phase 2, plan 2.4, 4.5, 8; package P4): the frames 478:11760 …
// 478:17237 - the empty state and the error of the page range, the table of a run with the row Total, the
// finished pages of a run (the totals as the core sums them: each page once, the ratio over the boxes), the
// kept result and the export of its table.

#include "apptoolshellfixture.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfapplocale.h"
#include "pdfapptoolgui_inkcoverage.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QJsonArray>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest>

using pdftest::AppToolShellFixture;
using RowState = pdf::PDFAppInkCoverageTableData::RowState;

/// Helpers of the test
class AppToolInkCoverageTestHelper
{
public:
    AppToolInkCoverageTestHelper() = delete;

    static constexpr const char* TOOL_ID = "review.ink-coverage";

    static QJsonObject ink(const QString& name, bool isSpot)
    {
        QJsonObject object;
        object.insert(QStringLiteral("name"), name);
        object.insert(QStringLiteral("displayName"), name);
        object.insert(QStringLiteral("isSpot"), isSpot);
        object.insert(QStringLiteral("color"), QString());
        return object;
    }

    /// A row of the core (DATA_PAGES): page from 1, the box area, per ink the area and the ratio
    static QJsonObject page(qint64 number, double boxArea, double maximumCoverage, const QJsonArray& areas, const QJsonArray& ratios)
    {
        QJsonObject object;
        object.insert(QStringLiteral("page"), number);
        object.insert(QStringLiteral("boxArea"), boxArea);
        object.insert(QStringLiteral("maximumCoverage"), maximumCoverage);
        object.insert(QStringLiteral("area"), areas);
        object.insert(QStringLiteral("ratio"), ratios);
        return object;
    }

    static pdf::PDFAppToolGUIInkCoverage* gui(pdf::PDFDocumentToolShell* shell)
    {
        return shell ? qobject_cast<pdf::PDFAppToolGUIInkCoverage*>(shell->getGUI()) : nullptr;
    }

    static pdf::PDFDocumentToolShell* openTool(AppToolShellFixture& fixture, int pageCount)
    {
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Brochure_Print.pdf"), pageCount));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        return fixture.openTool(tab, QString::fromLatin1(TOOL_ID));
    }
};

class AppToolInkCoverageTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void finishedPagesAndTotals();
    void resultRowsWithIssues();
    void tableCellTexts();
    void emptyStateAndRangeError();
    void runKeepsResultAndExports();
    void runToolOwnsTheCollector();
    void accessibleTable();
    void screenshots();
};

void AppToolInkCoverageTest::initTestCase()
{
    // The geometry of the frames is the geometry of the Fluent style (review RS7FIGMA)
    QApplication::setStyle(new pdf::PDFWidgetStyle());
}

void AppToolInkCoverageTest::finishedPagesAndTotals()
{
    // 478:11954: the pages to measure 1-4, pages 1 and 3 finished. The totals as the core (B163: each page
    // once; R7-8: the ratio is the ink inside of the boxes over the area of the boxes): Cyan areas 50 + 30 = 80,
    // ratio (50 % of 100 mm² + 10 % of 300 mm²) / 400 mm² = 20 %; Spot 0 + 60 = 60, (0 + 20 % of 300) / 400 = 15 %
    using H = AppToolInkCoverageTestHelper;
    const QJsonArray inks = { H::ink(QStringLiteral("Cyan"), false), H::ink(QStringLiteral("PANTONE 151 C"), true) };
    const QJsonArray pages = { H::page(1, 100.0, 120.0, { 50.0, 0.0 }, { 50.0, 0.0 }), H::page(3, 300.0, 250.0, { 30.0, 60.0 }, { 10.0, 20.0 }) };
    const pdf::PDFAppInkCoverageTableData data = pdf::PDFAppInkCoverageTableData::fromPages(inks, pages, { 0, 1, 2, 3 });

    QCOMPARE(data.inks.size(), size_t(2));
    QCOMPARE(data.getProcessCount(), 1);
    QCOMPARE(data.getSpotCount(), 1);
    QCOMPARE(data.rows.size(), size_t(4));
    QCOMPARE(data.measuredCount, 2);
    QCOMPARE(data.rows[0].state, RowState::Measured);
    QCOMPARE(data.rows[1].state, RowState::Pending);
    QCOMPARE(data.rows[1].page, pdf::PDFInteger(2));
    QCOMPARE(data.rows[2].state, RowState::Measured);
    QCOMPARE(data.rows[3].state, RowState::Pending);
    QCOMPARE(data.totalAreas[0], 80.0);
    QCOMPARE(data.totalAreas[1], 60.0);
    QCOMPARE(data.totalRatios[0], 20.0);
    QCOMPARE(data.totalRatios[1], 15.0);
    QCOMPARE(data.peakPage, pdf::PDFInteger(3));
    QCOMPARE(data.peakCoverage, 250.0);

    // The export of a stopped run: the finished pages only and the same totals
    const QJsonObject exported = data.toData();
    QCOMPARE(exported.value(QStringLiteral("pages")).toArray().size(), qsizetype(2));
    QCOMPARE(exported.value(QStringLiteral("total")).toObject().value(QStringLiteral("ratio")).toArray().at(0).toDouble(), 20.0);
}

void AppToolInkCoverageTest::resultRowsWithIssues()
{
    // Q7.9 b: a page with errors of the rendering is measured with the warning, a page, which cannot be
    // rendered, has no values and is not in the totals (the core totals are taken as they are)
    using H = AppToolInkCoverageTestHelper;
    QJsonObject data;
    data.insert(QStringLiteral("inks"), QJsonArray({ H::ink(QStringLiteral("Black"), false) }));
    data.insert(QStringLiteral("pages"), QJsonArray({ H::page(1, 100.0, 90.0, { 10.0 }, { 10.0 }), H::page(3, 100.0, 95.0, { 20.0 }, { 20.0 }) }));
    QJsonObject issue;
    issue.insert(QStringLiteral("page"), 3);
    issue.insert(QStringLiteral("message"), QStringLiteral("Font F7 is damaged"));
    data.insert(QStringLiteral("renderingIssues"), QJsonArray({ issue }));
    QJsonObject unmeasured;
    unmeasured.insert(QStringLiteral("page"), 2);
    unmeasured.insert(QStringLiteral("message"), QStringLiteral("Invalid content stream"));
    data.insert(QStringLiteral("unmeasuredPages"), QJsonArray({ unmeasured }));
    QJsonObject total;
    total.insert(QStringLiteral("area"), QJsonArray({ 30.0 }));
    total.insert(QStringLiteral("ratio"), QJsonArray({ 15.0 }));
    data.insert(QStringLiteral("total"), total);
    QJsonObject peak;
    peak.insert(QStringLiteral("page"), 3);
    peak.insert(QStringLiteral("coverage"), 95.0);
    data.insert(QStringLiteral("peak"), peak);

    const pdf::PDFAppInkCoverageTableData table = pdf::PDFAppInkCoverageTableData::fromResult(data);
    QCOMPARE(table.rows.size(), size_t(3));
    QCOMPARE(table.rows[0].state, RowState::Measured);
    QCOMPARE(table.rows[1].page, pdf::PDFInteger(2));
    QCOMPARE(table.rows[1].state, RowState::Unmeasured);
    QCOMPARE(table.rows[1].message, QStringLiteral("Invalid content stream"));
    QCOMPARE(table.rows[2].state, RowState::Issue);
    QCOMPARE(table.rows[2].message, QStringLiteral("Font F7 is damaged"));
    QCOMPARE(table.measuredCount, 2);
    QCOMPARE(table.totalAreas[0], 30.0);
    QCOMPARE(table.totalRatios[0], 15.0);
    QCOMPARE(table.peakPage, pdf::PDFInteger(3));
}

void AppToolInkCoverageTest::tableCellTexts()
{
    // 478:11954: a pending page "…" in its mm² columns, an empty %; 478:12696: two decimals of mm², whole %,
    // the row Total in whole mm² and %
    using H = AppToolInkCoverageTestHelper;
    const QJsonArray inks = { H::ink(QStringLiteral("Cyan"), false) };
    const QJsonArray pages = { H::page(1, 100.0, 40.0, { 26507.25 }, { 42.6 }) };
    pdf::PDFAppInkCoverageTable table(nullptr);
    table.setData(pdf::PDFAppInkCoverageTableData::fromPages(inks, pages, { 0, 1 }));

    const QLocale locale = pdf::PDFAppLocale::getUiLocale();
    QCOMPARE(table.getCellText(0, 0), pdf::PDFAppLocale::formatNumber(1));
    QCOMPARE(table.getCellText(0, 1), locale.toString(26507.25, 'f', 2));
    QCOMPARE(table.getCellText(0, 2), locale.toString(43.0, 'f', 0));
    QCOMPARE(table.getCellText(1, 1), QStringLiteral("…"));
    QCOMPARE(table.getCellText(1, 2), QString());
    QCOMPARE(table.getCellText(-1, 0), pdf::PDFAppInkCoverageTable::tr("Total"));
    QCOMPARE(table.getCellText(-1, 1), locale.toString(26507.0, 'f', 0));
    QCOMPARE(table.getCellText(-1, 2), locale.toString(43.0, 'f', 0));
}

void AppToolInkCoverageTest::emptyStateAndRangeError()
{
    // 478:11760: the empty state of the pages to measure, Calculate Coverage; 478:17237: a page range, which
    // is not valid - the empty state says it, the Primary button Disabled
    using H = AppToolInkCoverageTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture, 3);
    pdf::PDFAppToolGUIInkCoverage* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(gui->getContent(), pdf::PDFAppToolGUIInkCoverage::Content::None);
    QVERIFY(!gui->getCanvasPage()->isTableShown());
    QCOMPARE(gui->getCanvasPage()->getEmptyTitle()->text(), pdf::PDFAppToolGUIInkCoverage::tr("Ink coverage of %Ln pages", "", 3));
    QCOMPARE(shell->getToolbar()->getConfirmButton()->text(), pdf::PDFAppToolGUIInkCoverage::tr("Calculate Coverage"));
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
    // 478:11760: Close, never Cancel - the tool changes nothing in the document (RS7FIGMA)
    QCOMPARE(shell->getToolbar()->getCancelButton()->text(), pdf::PDFAppToolGUIInkCoverage::tr("Close"));
    QVERIFY(!gui->getExportPanel()->getButton()->isEnabled());
    QCOMPARE(gui->getSummaryRows()->getValue(0), pdf::PDFAppToolGUIInkCoverage::tr("%Ln to measure", "", 3));
    QCOMPARE(gui->getSummaryRows()->getValue(1), pdf::PDFAppToolGUIInkCoverage::tr("Not known yet"));
    QCOMPARE(gui->getStatusText(), pdf::PDFAppToolGUIInkCoverage::tr("Ready to calculate the ink coverage of %Ln pages.", "", 3));

    // A page out of the document: no page to measure
    shell->getModel()->setValue(QStringLiteral("pages"), QStringLiteral("7"));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getCanvasPage()->getEmptyTitle()->text(), pdf::PDFAppToolGUIInkCoverage::tr("Fix the page selection first"));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(gui->getStatusText(), pdf::PDFAppToolGUIInkCoverage::tr("The page selection is not valid."));
}

void AppToolInkCoverageTest::runKeepsResultAndExports()
{
    // 478:12696: the run gives the table of every page with the row Total; the result is kept (R7-2: the
    // Primary button Disabled, Calculate Again); 478:15674: the export of the table (CSV) - the data of the
    // kept result; a change of the pages makes it stale
    using H = AppToolInkCoverageTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture, 3);
    pdf::PDFAppToolGUIInkCoverage* gui = H::gui(shell);
    QVERIFY(gui);
    shell->getToolbar()->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([shell, gui]() { return shell->getState() == pdf::PDFDocumentToolShell::State::Editing && gui->getContent() != pdf::PDFAppToolGUIInkCoverage::Content::Running; }));
    AppToolShellFixture::settle();
    QCOMPARE(gui->getContent(), pdf::PDFAppToolGUIInkCoverage::Content::Result);
    QCOMPARE(shell->getResultState(), pdf::PDFAppToolResultState::Current);
    QVERIFY(gui->getCanvasPage()->isTableShown());

    const pdf::PDFAppInkCoverageTableData& data = gui->getTableData();
    QCOMPARE(data.rows.size(), size_t(3));
    QCOMPARE(data.measuredCount, 3);
    QVERIFY(data.getProcessCount() == 4);
    QCOMPARE(data.inks[0].name, QStringLiteral("Cyan"));
    QCOMPARE(gui->getSummaryRows()->getValue(0), pdf::PDFAppToolGUIInkCoverage::tr("%1 of %2").arg(pdf::PDFAppLocale::formatNumber(3), pdf::PDFAppLocale::formatNumber(3)));
    QVERIFY(!shell->getToolbar()->getConfirmButton()->isEnabled());
    QCOMPARE(shell->getToolbar()->getConfirmButton()->text(), pdf::PDFAppToolGUIInkCoverage::tr("Calculate Again"));
    QVERIFY(gui->getExportPanel()->getButton()->isEnabled());
    QCOMPARE(gui->getCanvasPage()->getTable()->getCellText(-1, 0), pdf::PDFAppInkCoverageTable::tr("Total"));

    // The export: the dialog of the file only, the name of 478:15674
    QVERIFY(gui->exportTable(pdf::PDFDocumentToolTableFormat::Csv));
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    pdf::PDFAppToolResultDialog* dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->getFileNameField());
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("Brochure_Print - ink coverage.csv"));
    dialog->reject();
    AppToolShellFixture::settle();

    // Another page selection: the result is stale, the Primary button runs again
    shell->getModel()->setValue(QStringLiteral("pages"), QStringLiteral("1-2"));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getResultState(), pdf::PDFAppToolResultState::Stale);
    QVERIFY(shell->getToolbar()->getConfirmButton()->isEnabled());
}

void AppToolInkCoverageTest::runToolOwnsTheCollector()
{
    // RS7APP L3: the finished pages of a run of the GUI go to the collector of that run only - the GUI gives the
    // run its own tool with the collector (PDFAppToolRunEnvironment::runTool), never a collector found by the
    // pointer of the document: a run of the catalog over the same document (an assistant, MCP) never feeds it
    using H = AppToolInkCoverageTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture, 2);
    pdf::PDFAppToolGUIInkCoverage* gui = H::gui(shell);
    QVERIFY(gui);
    pdf::PDFAppDocument* document = gui->getDocument();
    QVERIFY(document);

    pdf::PDFAppToolRunEnvironment environment;
    gui->prepareEnvironment(&environment);
    const std::shared_ptr<const pdf::PDFAppInkCoverageTool> runTool = std::dynamic_pointer_cast<const pdf::PDFAppInkCoverageTool>(environment.runTool);
    QVERIFY(runTool && runTool->getCollector());
    QCOMPARE(runTool->getId(), QString::fromLatin1(H::TOOL_ID));

    // The catalog has its own tool; its run over the same document leaves the collector of the GUI empty
    const std::shared_ptr<const pdf::PDFDocumentTool> catalogTool = pdf::PDFDocumentToolRegistry::getTool(QString::fromLatin1(H::TOOL_ID));
    QVERIFY(catalogTool);
    QVERIFY(catalogTool != environment.runTool);
    const pdf::PDFDocumentToolResult catalogResult = catalogTool->run(pdf::PDFDocumentToolRunner::createContext(document->getSnapshot()), QVariantMap(), nullptr, nullptr);
    QVERIFY2(catalogResult.isSuccess(), qPrintable(catalogResult.message));
    QCOMPARE(runTool->getCollector()->getPageCount(), 0);

    // A runner with the environment of the run: its run is measured by the tool of the run, the two pages go to
    // the collector (the live rows 478:11954 and the pages after Stop 478:14194 read it)
    QPointer<pdf::PDFDocumentToolRunner> runner = new pdf::PDFDocumentToolRunner(document, catalogTool, document);
    const QScopeGuard deleteRunner([&runner]() { pdftest::AppDocumentFixture::deleteNow(runner.data()); });
    runner->setEnvironment(environment);
    QSignalSpy finished(runner.data(), &pdf::PDFDocumentToolRunner::runFinished);
    QVERIFY(runner->run(QVariantMap()));
    QVERIFY(AppToolShellFixture::waitFor([&finished]() { return finished.count() == 1; }));
    QCOMPARE(runTool->getCollector()->getPageCount(), 2);
    runner->endDecision();
}

void AppToolInkCoverageTest::accessibleTable()
{
    // RS7APP L8: a screen reader reads the painted table - the row Total and every page with the texts of its cells,
    // a pending page "not measured yet"; a document of 10,000 pages names the first MAXIMUM_ACCESSIBLE_ROWS only
    using H = AppToolInkCoverageTestHelper;
    const QJsonArray inks = { H::ink(QStringLiteral("Cyan"), false) };
    const QJsonArray pages = { H::page(1, 100.0, 40.0, { 26507.25 }, { 42.6 }) };
    pdf::PDFAppInkCoverageTable table(nullptr);
    QVERIFY(table.accessibleDescription().isEmpty());
    table.setData(pdf::PDFAppInkCoverageTableData::fromPages(inks, pages, { 0, 1 }));

    const QString page1 = pdf::PDFAppInkCoverageTable::tr("Page %1").arg(table.getCellText(0, 0));
    const QString page2 = pdf::PDFAppInkCoverageTable::tr("Page %1").arg(table.getCellText(1, 0));
    const QString total = pdf::PDFAppInkCoverageTable::tr("Total");
    const auto cells = [&table](int row)
    {
        return pdf::PDFAppInkCoverageTable::tr("%1 %2 mm², %3 %", "Accessible text of the cells of an ink: the ink, the area, the ratio")
               .arg(QStringLiteral("Cyan"), table.getCellText(row, 1), table.getCellText(row, 2));
    };
    const QStringList expected = {
        pdf::PDFAppInkCoverageTable::tr("%1: %2", "Accessible text of a row of Ink Coverage: the row, its cells").arg(total, cells(-1)),
        pdf::PDFAppInkCoverageTable::tr("%1: %2", "Accessible text of a row of Ink Coverage: the row, its cells").arg(page1, cells(0)),
        pdf::PDFAppInkCoverageTable::tr("%1: not measured yet", "Accessible text of a pending page of Ink Coverage").arg(page2)
    };
    QCOMPARE(table.accessibleDescription(), expected.join(QStringLiteral("; ")));
    QVERIFY(table.accessibleDescription().contains(table.getCellText(0, 1)));

    // Many pages: the first ones named, the rest counted
    std::vector<pdf::PDFInteger> manyPages;
    for (int i = 0; i < pdf::PDFAppInkCoverageTable::MAXIMUM_ACCESSIBLE_ROWS + 10; ++i)
    {
        manyPages.push_back(i);
    }
    table.setData(pdf::PDFAppInkCoverageTableData::fromPages(inks, pages, manyPages));
    const QStringList parts = table.accessibleDescription().split(QStringLiteral("; "));
    QCOMPARE(parts.size(), 1 + pdf::PDFAppInkCoverageTable::MAXIMUM_ACCESSIBLE_ROWS + 1);
    QCOMPARE(parts.last(), pdf::PDFAppInkCoverageTable::tr("%Ln more pages", "Accessible text of Ink Coverage: the pages after the named ones", 10));
}

void AppToolInkCoverageTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder> (review RS7FIGMA): the states of Ink Coverage under the ids of their frames
    // (478:11760 … 478:17237) in both themes, at 1x / 2x and right-to-left, for the comparison with Figma; no pixel
    // of the leak color in any of them (Fluent)
    if (qEnvironmentVariableIsEmpty("PDF4QT_APP_SCREENSHOTS"))
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    using H = AppToolInkCoverageTestHelper;
    const QLocale locale = pdf::PDFAppLocale::getUiLocale();
    pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    const QScopeGuard localeGuard([locale]() { pdf::PDFAppLocale::setUiLocale(locale); });
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

        pdf::PDFDocumentToolShell* shell = H::openTool(fixture, 4);
        pdf::PDFAppToolGUIInkCoverage* gui = H::gui(shell);
        QVERIFY(gui);
        shot(QStringLiteral("478-11760"));

        shell->getModel()->setValue(QStringLiteral("pages"), QStringLiteral("7"));
        shot(QStringLiteral("478-17237"));
        shell->getModel()->setValue(QStringLiteral("pages"), QString());
        AppToolShellFixture::settle();

        shell->getToolbar()->getConfirmButton()->click();
        QVERIFY(AppToolShellFixture::waitFor([shell, gui]() { return shell->getState() == pdf::PDFDocumentToolShell::State::Editing && gui->getContent() != pdf::PDFAppToolGUIInkCoverage::Content::Running; }, 60000));
        shot(QStringLiteral("478-12696"));

        QTest::mouseClick(gui->getExportPanel()->getButton(), Qt::LeftButton);
        shot(QStringLiteral("478-13429"));
        if (QWidget* popup = QApplication::activePopupWidget())
        {
            popup->close();
        }
        AppToolShellFixture::settle();

        QVERIFY(gui->exportTable(pdf::PDFDocumentToolTableFormat::Csv));
        QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
        shot(QStringLiteral("478-15674"));
        AppToolShellFixture::screenshotDialog(shell->getResultDialog(), QStringLiteral("478-15674-dialog") + suffix);
        shell->getResultDialog()->reject();
        AppToolShellFixture::settle();

        fixture.closeMessageBoxes();
        QVERIFY2(leaks.isEmpty(), qPrintable(leaks.join(QStringLiteral(", "))));
    }
}

QTEST_MAIN(AppToolInkCoverageTest)

#include "tst_apptoolinkcoveragetest.moc"
