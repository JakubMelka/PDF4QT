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

// The lifetime scenarios of step 7a of phase 2 (plan 8, 9.2 with R7-17, R7-21), package P2: Document
// Statistics, Object Inspector, Document Report (reviewstatistics, reviewinspector, reviewreport). Every input
// goes through the guards of the test (the victim may be deleted inside of any of them); a step, whose part is
// gone, ends without a failure.

#include "apptoolpageslifetime.h"

#include "pdfappdocument.h"
#include "pdfappobjecttreemodel.h"
#include "pdfapptoolgui_objectinspector.h"
#include "pdfapptoolgui_report.h"
#include "pdfapptoolgui_statistics.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdocumenttool_objectinspector.h"
#include "pdfdocumenttool_report.h"
#include "pdfdocumenttool_statistics.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentrows.h"

#include <QCheckBox>
#include <QPointer>
#include <QPushButton>
#include <QToolButton>

/// The steps of the scenarios of step 7a
class ReviewALifetimeHelper
{
public:
    ReviewALifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    template<typename T>
    static T* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<T*>(shell->getGUI()) : nullptr;
    }

    /// A change of the document of the tab (an assistant): the tools compute again
    static bool changeDocument(ToolPagesLifetimeRun& run)
    {
        QPointer<pdf::PDFAppDocumentTab> tab = run.tab;
        pdf::PDFAppDocument* document = tab ? tab->getDocument() : nullptr;
        if (document && document->isReady())
        {
            document->commit(pdf::PDFModifiedDocument(document->getDocument(), nullptr, pdf::PDFModifiedDocument::Annotation), QStringLiteral("Assistant"),
                             document->getVersion(), pdf::PDFAppChangeSource::Mcp);
        }
        Steps::settle();
        return true;
    }

    // ------------------------------------------------------------- Document Statistics

    static bool statisticsReady(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIStatistics* current = gui<pdf::PDFAppToolGUIStatistics>(run); return !current || current->hasStatistics(); });
        return true;
    }

    /// View: By Object Type by its row, a row of the table by the keyboard, By Function again
    static bool statisticsView(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIStatistics* current = gui<pdf::PDFAppToolGUIStatistics>(run);
        if (!current || !Steps::click(current->getViewButton()))
        {
            return true;
        }
        Steps::settle();
        if (pdf::PDFAppToolGUIStatistics* again = gui<pdf::PDFAppToolGUIStatistics>(run); again && again->getViewMenu())
        {
            Steps::click(again->getViewMenu()->getRow(QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_TYPE)));
        }
        if (pdf::PDFAppToolGUIStatistics* again = gui<pdf::PDFAppToolGUIStatistics>(run))
        {
            Steps::key(again->getTable(), Qt::Key_Down);
        }
        if (pdf::PDFAppToolGUIStatistics* again = gui<pdf::PDFAppToolGUIStatistics>(run); again && Steps::click(again->getViewButton()))
        {
            Steps::settle();
            if (pdf::PDFAppToolGUIStatistics* last = gui<pdf::PDFAppToolGUIStatistics>(run); last && last->getViewMenu())
            {
                Steps::click(last->getViewMenu()->getRow(QString::fromLatin1(pdf::PDFDocumentToolStatistics::VIEW_FUNCTION)));
            }
        }
        Steps::settle();
        return true;
    }

    /// Export: the dialog of the CSV of the view shown, discarded
    static bool statisticsExport(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIStatistics* current = gui<pdf::PDFAppToolGUIStatistics>(run);
        if (!current || !Steps::click(current->getExportButton()))
        {
            return true;
        }
        Steps::settle();
        if (pdf::PDFAppToolGUIStatistics* again = gui<pdf::PDFAppToolGUIStatistics>(run))
        {
            Steps::click(again->getExportRow(pdf::PDFDocumentToolTableFormat::Csv));
        }
        Steps::settle();
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (pdf::PDFAppToolResultDialog* dialog = shell ? shell->getResultDialog() : nullptr)
        {
            Steps::click(dialog->getDiscardButton());
        }
        Steps::settle();
        return true;
    }

    /// The rows "dialog-reviewstatistics": the dialog of the export of the CSV of the view shown, left open
    static pdf::PDFAppToolResultDialog* statisticsExportDialog(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIStatistics* current = gui<pdf::PDFAppToolGUIStatistics>(run);
        if (!current || !Steps::click(current->getExportButton()))
        {
            return nullptr;
        }
        Steps::settle();
        if (pdf::PDFAppToolGUIStatistics* again = gui<pdf::PDFAppToolGUIStatistics>(run))
        {
            Steps::click(again->getExportRow(pdf::PDFDocumentToolTableFormat::Csv));
        }
        Steps::settle();
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? shell->getResultDialog() : nullptr;
    }

    // ------------------------------------------------------------- Object Inspector

    static bool inspectorReady(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIObjectInspector* current = gui<pdf::PDFAppToolGUIObjectInspector>(run); return !current || current->isReady(); });
        return true;
    }

    /// The view Content Streams by its row, a stream chosen (decoded in the job)
    static bool inspectorStream(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIObjectInspector* current = gui<pdf::PDFAppToolGUIObjectInspector>(run);
        if (!current || !Steps::click(current->getViewButton()))
        {
            return true;
        }
        Steps::settle();
        if (pdf::PDFAppToolGUIObjectInspector* again = gui<pdf::PDFAppToolGUIObjectInspector>(run))
        {
            Steps::click(again->getViewRow(pdf::PDFObjectInspectorView::ContentStreams));
        }
        Steps::settle();
        if (pdf::PDFAppToolGUIObjectInspector* again = gui<pdf::PDFAppToolGUIObjectInspector>(run); again && again->getTreeModel())
        {
            again->selectIndex(again->getTreeModel()->index(0, 0, QModelIndex()));
        }
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIObjectInspector* last = gui<pdf::PDFAppToolGUIObjectInspector>(run); return !last || !last->isLoadingContents(); });
        return true;
    }

    /// Pin, the pinned tab, Unpin, the copy of the contents
    static bool inspectorPin(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIObjectInspector* current = gui<pdf::PDFAppToolGUIObjectInspector>(run);
        if (!current || !Steps::click(current->getPinButton()))
        {
            return true;
        }
        Steps::settle();
        if (pdf::PDFAppToolGUIObjectInspector* again = gui<pdf::PDFAppToolGUIObjectInspector>(run))
        {
            const std::vector<QToolButton*> buttons = again->getTabButtons();
            if (buttons.size() > 1)
            {
                Steps::click(buttons.back());
            }
        }
        if (pdf::PDFAppToolGUIObjectInspector* again = gui<pdf::PDFAppToolGUIObjectInspector>(run))
        {
            again->copyContents();
        }
        if (pdf::PDFAppToolGUIObjectInspector* again = gui<pdf::PDFAppToolGUIObjectInspector>(run))
        {
            Steps::click(again->getUnpinButton());
        }
        Steps::settle();
        return true;
    }

    // ------------------------------------------------------------- Document Report

    static bool reportReady(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIReport* current = gui<pdf::PDFAppToolGUIReport>(run); return !current || !current->isPreviewPending(); });
        return true;
    }

    /// A row of a section (its preview), its check box off and on
    static bool reportSections(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIReport* current = gui<pdf::PDFAppToolGUIReport>(run);
        if (!current || !Steps::click(current->getSectionRow(pdf::PDFDocumentReportSection::PageBoxes)))
        {
            return true;
        }
        for (const bool value : { false, true })
        {
            if (pdf::PDFAppToolGUIReport* again = gui<pdf::PDFAppToolGUIReport>(run))
            {
                if (pdf::PDFTileRow* row = again->getSectionRow(pdf::PDFDocumentReportSection::Fonts))
                {
                    row->setCheckBoxChecked(value);
                }
            }
            Steps::settle();
        }
        return reportReady(run);
    }

    /// Structure as XML by the parameter, an option of the streams, Report again
    static bool reportXml(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (!shell || !shell->getModel())
        {
            return true;
        }
        shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolReport::PARAMETER_KIND), QString::fromLatin1(pdf::PDFDocumentToolReport::KIND_STRUCTURE_XML));
        Steps::settle();
        if (pdf::PDFAppToolGUIReport* again = gui<pdf::PDFAppToolGUIReport>(run))
        {
            Steps::click(again->getXmlOption("streamsText"));
        }
        reportReady(run);
        if (pdf::PDFDocumentToolShell* again = Steps::shell(run); again && again->getModel())
        {
            again->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolReport::PARAMETER_KIND), QString::fromLatin1(pdf::PDFDocumentToolReport::KIND_REPORT));
        }
        Steps::settle();
        return true;
    }

    /// Create Report: the dialog of the export, discarded
    static bool reportRun(ToolPagesLifetimeRun& run)
    {
        if (pdf::PDFAppToolResultDialog* dialog = Steps::confirmToDialog(run))
        {
            Steps::click(dialog->getDiscardButton());
        }
        Steps::settle();
        return true;
    }
};

/// The scenarios of step 7a
class ReviewALifetimeScenarios
{
public:
    ReviewALifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = ReviewALifetimeHelper;
        using Steps = ToolPagesLifetimeSteps;

        // Document Statistics: the preview, the views, the export, a change of the document (a new computation)
        ToolPagesLifetimeScenario statistics;
        statistics.name = QStringLiteral("reviewstatistics");
        statistics.toolId = QString::fromLatin1(pdf::PDFDocumentToolStatistics::TOOL_ID);
        statistics.hasPanel = true;
        statistics.prepareRun = &H::statisticsReady;   // View and Export are Disabled while the statistics are computed (480:15218)

        // The rows "dialog-": the export of the kept statistics (480:15368); Discard keeps the tool (R7-2, R7-13)
        statistics.hasResultDialog = true;
        statistics.isResultKept = true;
        statistics.openResultDialog = &H::statisticsExportDialog;
        statistics.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::statisticsReady);
            steps->push_back(&H::statisticsView);
            steps->push_back(&H::statisticsExport);
            steps->push_back(&H::changeDocument);
            steps->push_back(&H::statisticsReady);
        };
        scenarios->push_back(statistics);

        // Object Inspector: the job of the preparation, a view, the job of a stream, Pin / Unpin, a change
        ToolPagesLifetimeScenario inspector;
        inspector.name = QStringLiteral("reviewinspector");
        inspector.toolId = QString::fromLatin1(pdf::PDFDocumentToolObjectInspector::TOOL_ID);
        inspector.hasPanel = true;
        inspector.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::inspectorReady);
            steps->push_back(&H::inspectorStream);
            steps->push_back(&H::inspectorPin);
            steps->push_back(&H::changeDocument);
            steps->push_back(&H::inspectorReady);
        };
        scenarios->push_back(inspector);

        // Document Report: the job of the preview, the sections, Structure as XML, Create Report and its dialog. The
        // rows "dialog-": the dialog of the run (484:18811); Discard of the export keeps the tool open with its
        // result (R7-2, R7-13)
        ToolPagesLifetimeScenario report;
        report.name = QStringLiteral("reviewreport");
        report.toolId = QString::fromLatin1(pdf::PDFDocumentToolReport::TOOL_ID);
        report.hasPanel = true;
        report.hasResultDialog = true;
        report.isResultKept = true;
        report.prepareRun = &H::reportReady;
        report.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::reportReady);
            steps->push_back(&H::reportSections);
            steps->push_back(&H::reportXml);
            steps->push_back(&H::reportRun);
            steps->push_back(&H::changeDocument);
        };
        scenarios->push_back(report);
    }
};

void ToolPagesLifetimePackages::registerReviewAScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    ReviewALifetimeScenarios::add(scenarios);
}
