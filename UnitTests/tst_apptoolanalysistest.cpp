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

// The shared parts of the analysis tools of step 7 of phase 2 in the shell (plan 4.1, R7-2, R7-13, R7-14):
// a result kept in the tool (the rule of the owner of 9. 11. - after a result the Primary button Disabled,
// Close Enabled, the Primary button again only when the result is stale - 492:109399), the dialog of an
// export (the controls of the file only: 480:15368, 478:15674, 484:18811) and the formatter of the exports
// of the application (PDFAppLocale, O17). The test tool takes the id review.statistics of an analysis. The
// integration of step 7 over the real tools: the items of the status bar (K7.27), the repaired file in the
// report (FD-7) and the color management of the pictures of Object Inspector.

#include "apptoolshellfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentsoftproof.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapplocale.h"
#include "pdfappoutputpreviewcanvas.h"
#include "pdfapptoolgui_objectinspector.h"
#include "pdfapptoolgui_outputpreview.h"
#include "pdfapptoolpanelparts.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfapptoolvalueformatter.h"
#include "pdfdocumenttool_inkcoverage.h"
#include "pdfdocumenttool_objectinspector.h"
#include "pdfdocumenttool_outputpreview.h"
#include "pdfdocumenttool_report.h"
#include "pdfdocumenttool_statistics.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfdocumenttoolshell.h"
#include "pdfdocumenttooltablewriter.h"
#include "pdftoolstartpage.h"
#include "pdfcms.h"
#include "pdfdrawspacecontroller.h"
#include "pdfobjectinspector.h"

#include <QCheckBox>
#include <QFile>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QTimeZone>
#include <QtTest>

using pdftest::AppToolShellFixture;
using pdftest::ShellTestGUI;
using pdftest::ShellTestTool;
using ResultState = pdf::PDFAppToolResultState;
using ShellState = pdf::PDFDocumentToolShell::State;

/// The GUI of the test tool, which keeps its result (Ink Coverage, Document Report) and records the hooks
class AnalysisTestGUI : public ShellTestGUI
{
public:
    AnalysisTestGUI() : ShellTestGUI(false) { }

    std::vector<pdf::PDFDocumentToolResult> keptResults;
    std::vector<pdf::PDFAppToolResultOutcome> exportOutcomes;
    QString fileOpenText;

    virtual bool isResultKeptInTool() const override { return true; }
    virtual void onResultKept(const pdf::PDFDocumentToolResult& result) override { keptResults.push_back(result); }
    virtual void onExportDecided(const pdf::PDFAppToolResultOutcome& outcome) override { exportOutcomes.push_back(outcome); }

    virtual void fillResultInput(pdf::PDFAppToolResultInput* input) const override
    {
        input->title = QStringLiteral("Statistics are ready to export");
        input->fileOpenText = fileOpenText;
    }
};

class AnalysisTestGUIFactory : public pdf::PDFDocumentToolGUIFactory
{
public:
    virtual pdf::PDFDocumentToolGUI* createGUI() const override { return new AnalysisTestGUI(); }
};

/// Helpers of the test
class AppToolAnalysisTestHelper
{
public:
    AppToolAnalysisTestHelper() = delete;

    static constexpr const char* TOOL_ID = "review.statistics";

    /// The tab with the test tool open by the GUI, which keeps its result
    static pdf::PDFDocumentToolShell* openTool(AppToolShellFixture& fixture, std::shared_ptr<ShellTestTool>* tool = nullptr)
    {
        const QString toolId = QString::fromLatin1(TOOL_ID);
        std::shared_ptr<ShellTestTool> registered = fixture.registerShellTool(toolId);
        if (!registered)
        {
            return nullptr;
        }
        pdf::PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
        if (!pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<AnalysisTestGUIFactory>()))
        {
            return nullptr;
        }
        if (tool)
        {
            *tool = registered;
        }

        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3));
        if (!tab || !AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        return fixture.openTool(tab, toolId);
    }

    static AnalysisTestGUI* gui(pdf::PDFDocumentToolShell* shell)
    {
        return shell ? dynamic_cast<AnalysisTestGUI*>(shell->getGUI()) : nullptr;
    }

    static bool isConfirmEnabled(pdf::PDFDocumentToolShell* shell)
    {
        return shell->getToolbar() && shell->getToolbar()->getConfirmButton()->isEnabled();
    }

    /// Presses the Primary button and waits for the end of the run
    static bool confirmAndWaitForResult(pdf::PDFDocumentToolShell* shell, size_t keptCount)
    {
        shell->getToolbar()->getConfirmButton()->click();
        AnalysisTestGUI* testGui = gui(shell);
        return AppToolShellFixture::waitFor([shell, testGui, keptCount]() { return testGui->keptResults.size() == keptCount && shell->getState() != ShellState::Running; });
    }

    /// A result of an export: one CSV file of a table of the data
    static pdf::PDFDocumentToolResult createExport(QByteArray* data)
    {
        pdf::PDFDocumentToolTable table;
        table.name = QStringLiteral("byFunction");
        table.addColumn(QStringLiteral("class"), QStringLiteral("Class"), pdf::PDFDocumentToolTableValueType::Text);
        table.addColumn(QStringLiteral("count"), QStringLiteral("Count"), pdf::PDFDocumentToolTableValueType::Integer);
        table.addRow({ QStringLiteral("Image"), qint64(38) });
        pdf::PDFDocumentToolTableSection document;
        document.addTable(std::move(table));

        QString error;
        pdf::PDFDocumentToolResult result;
        result.outputs.push_back(pdf::PDFDocumentToolTableWriter::createOutput(document, pdf::PDFDocumentToolTableFormat::Csv, pdf::PDFDocumentToolTableWriter::Options(),
                                                                               QStringLiteral("Project_Report - statistics"), &error));
        *data = result.outputs.front().data;
        return result;
    }
};

class AppToolAnalysisTest : public QObject
{
    Q_OBJECT

private slots:
    void keptResultAndStaleRule();
    void exportOfKeptResult();
    void exportOfRun();
    void exportDialogIsFileOnlyOfAnalysis();
    void valueFormatter();
    void statusItemsOfAnalysisTools();
    void reportOfRepairedFile();
    void objectInspectorColorManagement();
    void startPageNoteOfAnalysis();
    void helpTopicsOfReviewTools();
};

void AppToolAnalysisTest::keptResultAndStaleRule()
{
    // R7-2 (owner 9. 11.): after a result the Primary button is Disabled, Close Enabled; Primary again only in
    // the stale state - a changed input or a changed document - with the amber callout (492:109399)
    using H = AppToolAnalysisTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    AnalysisTestGUI* gui = H::gui(shell);
    QVERIFY(gui);
    QCOMPARE(shell->getResultState(), ResultState::None);
    QVERIFY(H::isConfirmEnabled(shell));

    // The run: no result dialog, the tool stays with its result, the decision of the runner has ended
    QVERIFY(H::confirmAndWaitForResult(shell, 1));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), ShellState::Editing);
    QVERIFY(!shell->getResultDialog());
    QCOMPARE(shell->getResultState(), ResultState::Current);
    QCOMPARE(gui->keptResults.front().facts.size(), size_t(1));
    QCOMPARE(gui->getResultState(), ResultState::Current);
    QVERIFY(!H::isConfirmEnabled(shell));
    QVERIFY(shell->getToolbar()->getCancelButton()->isEnabled());
    QVERIFY(!shell->getStaleCallout());
    QCOMPARE(shell->getRunner()->getState(), pdf::PDFDocumentToolRunner::State::Idle);

    // Ctrl+Enter does not run it again either
    shell->confirm();
    AppToolShellFixture::settle();
    QCOMPARE(gui->keptResults.size(), size_t(1));

    // A changed input: stale - the callout and the Primary button
    shell->getModel()->setValue(QStringLiteral("repeat"), 2);
    AppToolShellFixture::settle();
    QCOMPARE(shell->getResultState(), ResultState::Stale);
    QVERIFY(H::isConfirmEnabled(shell));
    QVERIFY(shell->getStaleCallout());
    QVERIFY(shell->getStaleCallout()->isVisible());

    // A new run: current again, the callout is gone
    QVERIFY(H::confirmAndWaitForResult(shell, 2));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getResultState(), ResultState::Current);
    QVERIFY(!shell->getStaleCallout());
    QVERIFY(!H::isConfirmEnabled(shell));

    // A changed document (an assistant): stale
    pdf::PDFAppDocument* document = shell->getHost()->getDocument();
    QVERIFY(document->commit(pdf::PDFModifiedDocument(document->getDocument(), nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Assistant"),
                             document->getVersion(), pdf::PDFAppChangeSource::Mcp));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getResultState(), ResultState::Stale);
    QVERIFY(H::isConfirmEnabled(shell));

    // An input of the GUI outside of the model (Compare: Swap) and the format of a later export
    QVERIFY(H::confirmAndWaitForResult(shell, 3));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getResultState(), ResultState::Current);
    QVERIFY(!gui->isResultStaleBy(QStringLiteral("format")));
    QVERIFY(gui->isResultStaleBy(QStringLiteral("repeat")));
    gui->markResultStale();
    QCOMPARE(shell->getResultState(), ResultState::Stale);
    QVERIFY(shell->getStaleCallout());
    QVERIFY(H::isConfirmEnabled(shell));

    // A failed run keeps the result stale (the old result is still not the one of the document)
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("fail"));
    shell->getToolbar()->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getState() == ShellState::Editing && shell->getRunner()->getState() == pdf::PDFDocumentToolRunner::State::Idle; }));
    AppToolShellFixture::settle();
    QCOMPARE(gui->keptResults.size(), size_t(3));
    QCOMPARE(shell->getResultState(), ResultState::Stale);
}

void AppToolAnalysisTest::exportOfKeptResult()
{
    // The export of the data of the kept result (480:15368): the dialog of the file only, the name with the
    // extension of the format, the folder of the document; the file is the bytes of the export; the tool stays
    using H = AppToolAnalysisTestHelper;
    AppToolShellFixture fixture;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture);
    QVERIFY(shell);
    AnalysisTestGUI* gui = H::gui(shell);
    QVERIFY(H::confirmAndWaitForResult(shell, 1));
    AppToolShellFixture::settle();

    QByteArray data;
    QVERIFY(!gui->openExport(pdf::PDFDocumentToolResult()));
    QVERIFY(gui->openExport(H::createExport(&data)));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), ShellState::Deciding);
    QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::FileOnly);
    QVERIFY(dialog->isExport());
    QVERIFY(!dialog->getContinueWith());
    QVERIFY(!dialog->getOpenCheckBox()->isVisible());
    QCOMPARE(dialog->getFileNameField()->text(), QStringLiteral("Project_Report - statistics.csv"));
    QCOMPARE(QDir::fromNativeSeparators(dialog->getFolderLabel()->getPath()), QDir::fromNativeSeparators(QDir(fixture.folder()).absolutePath()));
    QCOMPARE(dialog->getConfirmButton()->text(), QStringLiteral("Save File"));
    QCOMPARE(dialog->getDiscardButton()->text(), QStringLiteral("Discard"));

    // A name without the extension gets the one of the format (never .pdf)
    dialog->getFileNameField()->setText(QStringLiteral("exported"));
    QCOMPARE(QFileInfo(dialog->getTargetFilePath()).fileName(), QStringLiteral("exported.csv"));
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog.isNull(); }));
    AppToolShellFixture::settle();

    const QString filePath = QDir(fixture.folder()).filePath(QStringLiteral("exported.csv"));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), data);

    // The tool stays with its result (still current), the GUI learns the file; the export is no new use
    QCOMPARE(shell->getState(), ShellState::Editing);
    QCOMPARE(shell->getResultState(), ResultState::Current);
    QVERIFY(!H::isConfirmEnabled(shell));
    QCOMPARE(gui->exportOutcomes.size(), size_t(1));
    QCOMPARE(gui->exportOutcomes.front().action, pdf::PDFAppToolResultOutcome::Action::FileSaved);
    QCOMPARE(QFileInfo(gui->exportOutcomes.front().filePath).fileName(), QStringLiteral("exported.csv"));
    QVERIFY(!gui->exportOutcomes.front().isFileOpenRequested);

    // Discard keeps the tool too
    QVERIFY(gui->openExport(H::createExport(&data)));
    AppToolShellFixture::settle();
    dialog = shell->getResultDialog();
    QVERIFY(dialog);
    dialog->getDiscardButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog.isNull(); }));
    AppToolShellFixture::settle();
    QCOMPARE(shell->getState(), ShellState::Editing);
    QCOMPARE(gui->exportOutcomes.size(), size_t(2));
    QCOMPARE(gui->exportOutcomes.back().action, pdf::PDFAppToolResultOutcome::Action::Discarded);
}

void AppToolAnalysisTest::exportOfRun()
{
    // Document Report (484:18811): the run writes the file of the export; the dialog opens after the run with
    // the check box of the tool ("Open the report when it is saved"); after it the tool keeps its result
    using H = AppToolAnalysisTestHelper;
    AppToolShellFixture fixture;
    std::shared_ptr<ShellTestTool> tool;
    pdf::PDFDocumentToolShell* shell = H::openTool(fixture, &tool);
    QVERIFY(shell);
    AnalysisTestGUI* gui = H::gui(shell);
    gui->fileOpenText = QStringLiteral("Open the report when it is saved");

    QByteArray data;
    tool->setOutputFiles(H::createExport(&data).outputs);
    shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("files"));
    shell->getToolbar()->getConfirmButton()->click();
    QVERIFY(AppToolShellFixture::waitFor([shell]() { return shell->getResultDialog() != nullptr; }));
    AppToolShellFixture::settle();
    QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog();
    QCOMPARE(gui->keptResults.size(), size_t(1));
    QVERIFY(dialog->isExport());
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::FileOnly);
    QVERIFY(dialog->getOpenCheckBox()->isVisible());
    QCOMPARE(dialog->getOpenCheckBox()->text(), gui->fileOpenText);
    QCOMPARE(shell->getRunner()->getState(), pdf::PDFDocumentToolRunner::State::Waiting);

    dialog->getFileNameField()->setText(QStringLiteral("report"));
    dialog->getOpenCheckBox()->setChecked(true);
    dialog->confirm();
    QVERIFY(AppToolShellFixture::waitFor([&dialog]() { return dialog.isNull(); }));
    AppToolShellFixture::settle();

    QFile file(QDir(fixture.folder()).filePath(QStringLiteral("report.csv")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), data);
    QCOMPARE(shell->getState(), ShellState::Editing);
    QCOMPARE(shell->getResultState(), ResultState::Current);
    QCOMPARE(shell->getRunner()->getState(), pdf::PDFDocumentToolRunner::State::Idle);
    QCOMPARE(gui->exportOutcomes.size(), size_t(1));
    QVERIFY(gui->exportOutcomes.front().isFileOpenRequested);
}

void AppToolAnalysisTest::exportDialogIsFileOnlyOfAnalysis()
{
    // R7-13: only one file of data of an analysis is an export; the files of an export tool (Extract Text,
    // 339:9601) keep their dialog of the choices, a result without an output is the analysis dialog
    using Dialog = pdf::PDFAppToolResultDialog;
    pdf::PDFDocumentToolOutputTraits analysis(pdf::PDFDocumentToolCatalog::findTool(u"review.statistics"));
    pdf::PDFDocumentToolOutputTraits files(pdf::PDFDocumentToolCatalog::findTool(u"convert.extract-text"));
    QByteArray data;
    const pdf::PDFDocumentToolResult exportResult = AppToolAnalysisTestHelper::createExport(&data);
    QVERIFY(Dialog::isAnalysisExport(analysis, exportResult));
    QCOMPARE(Dialog::getResultKind(analysis, exportResult), Dialog::ResultKind::FileOnly);
    QVERIFY(!Dialog::isAnalysisExport(files, exportResult));
    QCOMPARE(Dialog::getResultKind(files, exportResult), Dialog::ResultKind::Files);
    QCOMPARE(Dialog::getResultKind(analysis, pdf::PDFDocumentToolResult()), Dialog::ResultKind::Analysis);

    pdf::PDFDocumentToolResult two = exportResult;
    two.outputs.push_back(exportResult.outputs.front());
    QVERIFY(!Dialog::isAnalysisExport(analysis, two));
}

void AppToolAnalysisTest::valueFormatter()
{
    // R7-14, O17: the numbers and the dates of the exports for reading in the language of the user interface;
    // ISO 8601 and RFC 2822 never translated
    const QLocale previous = pdf::PDFAppLocale::getUiLocale();
    pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::German, QLocale::Germany));
    const pdf::PDFAppToolValueFormatter formatter;
    QCOMPARE(formatter.formatInteger(1234567), QStringLiteral("1.234.567"));
    QCOMPARE(formatter.formatReal(1234.5, 2), QStringLiteral("1.234,50"));
    QVERIFY(formatter.formatReal(std::numeric_limits<double>::infinity(), 2).isEmpty());
    const QDateTime dateTime(QDate(2026, 11, 17), QTime(14, 5, 0), QTimeZone::fromSecondsAheadOfUtc(3600));
    QCOMPARE(formatter.formatDateTime(dateTime, pdf::PDFDocumentToolDateFormat::Iso), QStringLiteral("2026-11-17T14:05:00+01:00"));
    QCOMPARE(formatter.formatDateTime(dateTime, pdf::PDFDocumentToolDateFormat::Short), pdf::PDFAppLocale::formatDateTime(dateTime));

    // The writer takes it for Text, never for CSV
    pdf::PDFDocumentToolTable table;
    table.addColumn(QStringLiteral("share"), QStringLiteral("Share"), pdf::PDFDocumentToolTableValueType::Real, 2);
    table.addRow({ 1234.5 });
    pdf::PDFDocumentToolTableSection document;
    document.addTable(std::move(table));
    const pdf::PDFDocumentToolTableWriter::Options options = pdf::PDFAppToolValueFormatter::createOptions(pdf::PDFDocumentToolDateFormat::Short);
    QVERIFY(QString::fromUtf8(pdf::PDFDocumentToolTableWriter::write(document, pdf::PDFDocumentToolTableFormat::Text, options).data).contains(QStringLiteral("1.234,50")));
    QCOMPARE(pdf::PDFDocumentToolTableWriter::write(document, pdf::PDFDocumentToolTableFormat::Csv, options).data, QByteArray("Share\r\n1234.50\r\n"));
    pdf::PDFAppLocale::setUiLocale(previous);
}

void AppToolAnalysisTest::statusItemsOfAnalysisTools()
{
    // K7.27 (FD-31): Statistics, Object Inspector and Report show no page of the document, so the status bar has
    // neither the page item nor the zoom item of the view behind them; a tool over the view keeps both (status-bar
    // 291:1064 "Page 1 / 3", the zoom of the view); Output Preview shows its own page "Page 1 of 3" and the zoom of
    // its picture (474:8386), never the zoom of the view behind its canvas
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 3));
    QVERIFY(tab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppDocumentStatusBar* statusBar = view->getStatusBar();
    view->setZoom(0.37);
    AppToolShellFixture::settle();

    for (const char* toolId : { pdf::PDFDocumentToolStatistics::TOOL_ID, pdf::PDFDocumentToolObjectInspector::TOOL_ID, pdf::PDFDocumentToolReport::TOOL_ID })
    {
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(toolId));
        QVERIFY2(shell, toolId);
        AppToolShellFixture::settle();
        QVERIFY2(view->isToolModeActive(), toolId);
        QVERIFY2(!statusBar->getPageItem()->isVisible(), toolId);
        QVERIFY2(!statusBar->getZoomItem()->isVisible(), toolId);
        shell->close();
        AppToolShellFixture::settle();
        QVERIFY(!view->isToolModeActive());
    }

    // A tool over the view (Ink Coverage, 478:12696): the page and the zoom of the view
    {
        QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolInkCoverage::TOOL_ID));
        QVERIFY(shell);
        AppToolShellFixture::settle();
        QVERIFY(statusBar->getPageItem()->isVisible());
        QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 / 3"));
        QVERIFY(statusBar->getZoomItem()->isVisible());
        QCOMPARE(statusBar->getZoomText(), QStringLiteral("37 %"));
        shell->close();
        AppToolShellFixture::settle();
    }

    // Output Preview: the zoom of the picture = its width over the width of the page at the zoom 100 % of the view
    // (the media box 300 pt = 300 / 72 inch, PDFDrawWidgetProxy::getPixelPerMM pixels per millimeter)
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolOutputPreview::TOOL_ID));
    QVERIFY(shell);
    pdf::PDFAppToolGUIOutputPreview* outputPreview = qobject_cast<pdf::PDFAppToolGUIOutputPreview*>(shell->getGUI());
    QVERIFY(outputPreview);
    QVERIFY(AppToolShellFixture::waitFor([outputPreview]() { return outputPreview->getResult() && !outputPreview->isRendering(); }));
    AppToolShellFixture::settle();

    const pdf::PDFAppOutputPreviewPicture* picture = outputPreview->getCanvas()->getPicture();
    const double pageWidth = 300.0 / 72.0 * 25.4 * view->getProxy()->getPixelPerMM();
    const auto expectedZoom = [picture, pageWidth]() { return QStringLiteral("%1 %").arg(pdf::PDFAppLocale::formatNumber(qRound(picture->getImageRect().width() / pageWidth * 100.0))); };
    QVERIFY(!picture->getImageRect().isEmpty());
    QCOMPARE(statusBar->getPageText(), QStringLiteral("Page 1 of 3"));
    QVERIFY(statusBar->getZoomItem()->isVisible());
    QCOMPARE(statusBar->getZoomText(), expectedZoom());
    QVERIFY(statusBar->getZoomText() != QStringLiteral("37 %"));

    // A smaller window: a smaller picture, its zoom follows
    const QString wideZoom = statusBar->getZoomText();
    tab->window()->resize(1100, 700);
    QVERIFY(AppToolShellFixture::waitFor([statusBar, &wideZoom]() { return statusBar->getZoomText() != wideZoom; }));
    QCOMPARE(statusBar->getZoomText(), expectedZoom());

    // The end of the tool gives the items back to the view (out of the mode: none without page labels)
    shell->close();
    AppToolShellFixture::settle();
    QVERIFY(!statusBar->getPageItem()->isVisible());
    QVERIFY(!statusBar->getZoomItem()->isVisible());
    QVERIFY(!view->hasToolStatusItems());
}

void AppToolAnalysisTest::reportOfRepairedFile()
{
    // FD-7 (plan 2.10): Basic information of the report says "Repaired when opened" for a file, which the reader
    // had to restore from its bytes (a wrong startxref); the flag goes from the reading of the tab
    // (PDFAppDocument::getRestoration) through the snapshot of the run (PDFDocumentToolInputDocument::isRestored)
    // to PDFDocumentReport::Options::isRepaired. An intact file has no such row.
    AppToolShellFixture fixture;
    const QString intact = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Intact.pdf"), 2);
    const QString damaged = pdftest::AppDocumentSamples::damagedCopy(intact, QStringLiteral("Damaged.pdf"));
    QVERIFY(!damaged.isEmpty());

    for (const QString& file : { intact, damaged })
    {
        const bool isDamaged = file == damaged;
        pdf::PDFAppDocumentTab* tab = fixture.open(file);
        QVERIFY(tab);
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
        QCOMPARE(tab->getDocument()->getRestoration() != pdf::PDFAppDocumentRestoration::None, isDamaged);

        const pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolRunner::createContext(tab->getDocument()->getSnapshot());
        QVariantMap parameters;
        parameters.insert(QStringLiteral("format"), QStringLiteral("text"));
        const pdf::PDFDocumentToolReport tool;
        const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
        QVERIFY2(result.isSuccess(), qPrintable(result.message));

        const QJsonObject basic = result.data.value(QStringLiteral("sections")).toObject().value(QStringLiteral("basicInformation")).toObject();
        QCOMPARE(basic.value(QStringLiteral("repaired")).toBool(), isDamaged);
        QCOMPARE(result.outputs.size(), size_t(1));
        QCOMPARE(QString::fromUtf8(result.outputs.front().data).contains(QStringLiteral("Repaired when opened")), isDamaged);
    }
}

void AppToolAnalysisTest::objectInspectorColorManagement()
{
    // Plan 4.3 (P2): the picture of an image is converted by the color management of the tab (its settings, the
    // output intents of the document), not by the generic one, and never with the soft proof of the tab (Q7.10:
    // the proof is only the view of the tab)
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 1));
    QVERIFY(tab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    QPointer<pdf::PDFDocumentToolShell> shell = fixture.openTool(tab, QString::fromLatin1(pdf::PDFDocumentToolObjectInspector::TOOL_ID));
    QVERIFY(shell);
    pdf::PDFAppToolGUIObjectInspector* inspectorGui = qobject_cast<pdf::PDFAppToolGUIObjectInspector*>(shell->getGUI());
    QVERIFY(inspectorGui);
    QVERIFY(AppToolShellFixture::waitFor([inspectorGui]() { return inspectorGui->getInspector() != nullptr; }));

    const pdf::PDFCMSManager* manager = tab->getView()->getCMSManager();
    QVERIFY(manager);
    const pdf::PDFCMSPointer cms = inspectorGui->getInspector()->getCMS();
    QVERIFY(cms);
    QVERIFY(cms->isCompatible(manager->getSettings().withoutProofing()));
    shell->close();
    AppToolShellFixture::settle();
}

void AppToolAnalysisTest::startPageNoteOfAnalysis()
{
    // FD-29 (plan 2.9): the note of the start page 277:2096 "Nothing changes until you press …" does not fit a tool,
    // which changes nothing (Statistics has no Primary button at all); a tool, whose every result is an analysis,
    // says "Nothing in the document changes." - also one with a Primary button (Report, Ink Coverage). A tool,
    // which may change the document, keeps the text of the frame (Measure of 7c will: ChangeDocument besides
    // Analysis in its possible kinds)
    const auto noteOf = [](const QString& toolId)
    {
        AppToolShellFixture fixture;
        fixture.navigate(QStringLiteral("/tools/") + toolId);
        AppToolShellFixture::settle();
        pdf::PDFToolStartPage* page = fixture.page<pdf::PDFToolStartPage>();
        const QLabel* note = page ? page->findChild<QLabel*>(QStringLiteral("toolStartNoteText")) : nullptr;
        return note ? note->text() : QString();
    };

    QCOMPARE(noteOf(QString::fromLatin1(pdf::PDFDocumentToolStatistics::TOOL_ID)), QStringLiteral("The tool opens in the document’s tab. Nothing in the document changes; a locked document can be used too."));
    QCOMPARE(noteOf(QString::fromLatin1(pdf::PDFDocumentToolReport::TOOL_ID)), QStringLiteral("The tool opens in the document’s tab. Nothing in the document changes; a locked document can be used too."));
    QCOMPARE(noteOf(QString::fromLatin1(pdf::PDFDocumentToolInkCoverage::TOOL_ID)), QStringLiteral("The tool opens in the document’s tab. Nothing in the document changes; a locked document can be used too."));
    QVERIFY(noteOf(QStringLiteral("pages.rotate")).startsWith(QStringLiteral("The tool opens in the document’s tab. Nothing changes until you press Rotate Pages; ")));

    const pdf::PDFDocumentToolCatalogEntry* measure = pdf::PDFDocumentToolCatalog::findTool(u"review.measure");
    QVERIFY(measure);
    QVERIFY(measure->getPossibleResultKinds().size() > 1);
}

void AppToolAnalysisTest::helpTopicsOfReviewTools()
{
    // F1 opens the topic of the current page (phase 2 common rules; plan 7.2): each of the six tools of 7a / 7b its
    // own topic tool-review-<name>; the item of a kept soft proof in the status bar (485:17529) the topic of Soft
    // Proofing, which it opens; the view of the document itself its topic of reading
    const std::vector<std::pair<QString, QString>> tools = {
        { QString::fromLatin1(pdf::PDFDocumentToolStatistics::TOOL_ID), QStringLiteral("tool-review-statistics") },
        { QString::fromLatin1(pdf::PDFDocumentToolObjectInspector::TOOL_ID), QStringLiteral("tool-review-object-inspector") },
        { QString::fromLatin1(pdf::PDFDocumentToolReport::TOOL_ID), QStringLiteral("tool-review-report") },
        { QString::fromLatin1(pdf::PDFDocumentToolInkCoverage::TOOL_ID), QStringLiteral("tool-review-ink-coverage") },
        { QString::fromLatin1(pdf::PDFDocumentToolOutputPreview::TOOL_ID), QStringLiteral("tool-review-output-preview") },
        { QString::fromLatin1(pdf::PDFAppDocumentSoftProof::TOOL_ID), QStringLiteral("tool-review-soft-proof") } };
    for (const auto& [toolId, topic] : tools)
    {
        AppToolShellFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
        QVERIFY(tab);
        QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
        QVERIFY2(fixture.openTool(tab, toolId), qPrintable(toolId));
        AppToolShellFixture::settle();
        fixture.pressHelpKey();
        AppToolShellFixture::settle();
        QCOMPARE(fixture.getShownHelpTopic(), topic);
    }

    // The item of the kept soft proof: F1 on it, F1 on the canvas
    AppToolShellFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("Project_Report.pdf"), 2));
    QVERIFY(tab);
    QVERIFY(AppToolShellFixture::waitFor([tab]() { return tab->getDocument()->isReady(); }));
    pdf::PDFAppDocumentSoftProof* softProof = pdf::PDFAppDocumentSoftProof::getSoftProof(tab->getView());
    QVERIFY(softProof);
    softProof->setProofing(true);
    softProof->setKept(true);
    AppToolShellFixture::settle();
    QToolButton* statusItem = softProof->getStatusItem();
    QVERIFY(statusItem && statusItem->isVisible());
    statusItem->setFocus(Qt::TabFocusReason);
    AppToolShellFixture::settle();
    QVERIFY(statusItem->hasFocus());
    fixture.pressHelpKey();
    AppToolShellFixture::settle();
    QCOMPARE(fixture.getShownHelpTopic(), QStringLiteral("tool-review-soft-proof"));

    fixture.window->activateTab(tab);
    AppToolShellFixture::settle();
    pdftest::AppDocumentFixture::canvasWidget(tab->getView())->setFocus(Qt::OtherFocusReason);
    AppToolShellFixture::settle();
    fixture.pressHelpKey();
    AppToolShellFixture::settle();
    QCOMPARE(fixture.getShownHelpTopic(), QStringLiteral("read-documents"));
}

QTEST_MAIN(AppToolAnalysisTest)

#include "tst_apptoolanalysistest.moc"
