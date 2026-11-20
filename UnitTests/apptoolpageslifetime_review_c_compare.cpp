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

// The lifetime scenarios of step 7c of phase 2 (plan 8, R7-21), package P6: Compare in its own tab (reviewcompare) -
// the reading of the two inputs, Swap, the panel View, the run kept in the tool, the views (the job of the document of
// the view), the filters, F6, the export of the XML; Measure has apptoolpageslifetime_review_c_measure.cpp.

#include "apptoolpageslifetime.h"

#include "pdfappcompareview.h"
#include "pdfapptoolgui_compare.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptooltab.h"
#include "pdfapptooltoolbar.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttoolshell.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"

#include <QPainter>
#include <QTreeView>

/// Helpers of the scenarios of Compare
class ToolPagesLifetimeCompare
{
public:
    ToolPagesLifetimeCompare() = delete;

    /// Pages 400 x 400 with one rectangle each (the inputs differ: the rectangle moved)
    static QString rectangles(const ToolPagesLifetimeRun& run, const QString& name, const QList<QRectF>& rectangles)
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
        const QString path = run.fixture->folder() + QLatin1Char('/') + name;
        return pdftest::AppDocumentSamples::write(document, path) ? path : QString();
    }

    static pdf::PDFAppToolGUICompare* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
        return shell ? qobject_cast<pdf::PDFAppToolGUICompare*>(shell->getGUI()) : nullptr;
    }

    /// The inputs of the own tab are read
    static bool waitInputs(const ToolPagesLifetimeRun& run)
    {
        const bool isRead = ToolPagesLifetimeSteps::wait(run, [&run]()
        {
            const pdf::PDFAppToolInputList* list = run.toolTab ? run.toolTab->getInputList() : nullptr;
            return !list || (list->getLoadingCount() == 0 && list->getLoader()->getRunningCount() == 0);
        });
        ToolPagesLifetimeSteps::settle();
        return isRead;
    }

    /// Opens the own tab of Compare with two documents, which differ
    static bool openTab(ToolPagesLifetimeRun& run)
    {
        if (run.toolTab)
        {
            return waitInputs(run) && ToolPagesLifetimeSteps::shell(run) != nullptr;
        }
        if (!run.window || !run.fixture)
        {
            return false;
        }
        const QString original = rectangles(run, QStringLiteral("compare-original.pdf"), { QRectF(20, 20, 50, 50), QRectF(40, 40, 10, 10) });
        const QString revised = rectangles(run, QStringLiteral("compare-revised.pdf"), { QRectF(200, 200, 50, 50), QRectF(40, 40, 10, 10), QRectF(5, 5, 5, 5) });
        pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
        list->addFiles({ original, revised });
        run.toolTab = run.window->createToolTab(QString::fromLatin1(pdf::PDFDocumentToolCompare::TOOL_ID), list);
        ToolPagesLifetimeSteps::settle();
        return run.toolTab && waitInputs(run) && gui(run) && ToolPagesLifetimeSteps::shell(run)->getState() == pdf::PDFDocumentToolShell::State::Editing;
    }

    static bool openTabStep(ToolPagesLifetimeRun& run)
    {
        return openTab(run);
    }

    static bool swap(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUICompare* current = gui(run);
        if (!current || !ToolPagesLifetimeSteps::click(current->getSwapButton()))
        {
            return false;
        }
        ToolPagesLifetimeSteps::settle();
        return true;
    }

    static bool togglePanel(ToolPagesLifetimeRun& run)
    {
        QToolButton* button = ToolPagesLifetimeSteps::firstPanelButton(run);
        if (!ToolPagesLifetimeSteps::click(button))
        {
            return false;
        }
        ToolPagesLifetimeSteps::settle();
        button = ToolPagesLifetimeSteps::firstPanelButton(run);
        return !button || ToolPagesLifetimeSteps::key(button->window(), Qt::Key_Escape);
    }

    /// The comparison by the Primary button (F5); the result is kept in the tool (or a failure is shown there)
    static bool compare(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUICompare* current = gui(run);
        pdf::PDFAppToolToolbar* toolbar = ToolPagesLifetimeSteps::toolbar(run);
        if (!current || !toolbar || !ToolPagesLifetimeSteps::click(toolbar->getConfirmButton()))
        {
            return false;
        }
        const bool isFinished = ToolPagesLifetimeSteps::wait(run, [&run]()
        {
            const pdf::PDFAppToolGUICompare* tool = gui(run);
            const pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
            return !tool || !shell || (shell->getState() == pdf::PDFDocumentToolShell::State::Editing && tool->getContent() != pdf::PDFAppToolGUICompare::Content::Running &&
                                       tool->getContent() != pdf::PDFAppToolGUICompare::Content::Ready && !tool->isViewDocumentPending());
        });
        ToolPagesLifetimeSteps::settle();
        const pdf::PDFAppToolGUICompare* tool = gui(run);
        const pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
        if (!isFinished && tool && shell)
        {
            qWarning("Compare has not finished: content %d, state %d, pending %d", int(tool->getContent()), int(shell->getState()), int(tool->isViewDocumentPending()));
        }
        return tool != nullptr;
    }

    /// The views (each builds its document in the job), the filters, F6 and the items of Show
    static bool browse(ToolPagesLifetimeRun& run)
    {
        for (pdf::PDFDiffReportView view : { pdf::PDFDiffReportView::Overlay, pdf::PDFDiffReportView::Original, pdf::PDFDiffReportView::Revised, pdf::PDFDiffReportView::SideBySide })
        {
            pdf::PDFAppToolGUICompare* current = gui(run);
            if (!current)
            {
                return false;
            }
            current->setView(view);
            ToolPagesLifetimeSteps::settle();
        }

        // A zoom of the canvas (the zoom item of the status bar follows it from the event loop)
        pdf::PDFAppToolGUICompare* zoomed = gui(run);
        pdf::PDFWidget* widget = zoomed && zoomed->getCompareView() ? zoomed->getCompareView()->getPdfWidget() : nullptr;
        if (widget)
        {
            pdf::PDFDrawWidgetProxy* proxy = widget->getDrawWidgetProxy();
            proxy->zoom(proxy->getZoom() * 1.25);
            ToolPagesLifetimeSteps::settle();
        }
        pdf::PDFAppToolGUICompare* next = gui(run);
        if (!next || !ToolPagesLifetimeSteps::click(next->getNextButton()))
        {
            return false;
        }
        ToolPagesLifetimeSteps::settle();
        pdf::PDFAppToolGUICompare* current = gui(run);
        if (!current)
        {
            return false;
        }
        current->setShowOption(QString::fromLatin1(pdf::PDFAppToolGUICompare::SETTING_ONLY_CHANGED), true);
        ToolPagesLifetimeSteps::settle();
        current = gui(run);
        pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
        if (!current || !shell)
        {
            return false;
        }
        // The filter of the page movement (the vector graphics stay shown: the export needs a shown difference)
        shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolCompare::PARAMETER_INCLUDE_PAGE_MOVEMENT), false);
        ToolPagesLifetimeSteps::settle();
        return gui(run) != nullptr;
    }

    /// A difference selected by the list (a click: the selection model of the view emits currentChanged) - the last one
    /// (F6 of browse has selected the first one)
    static bool selectByList(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUICompare* current = gui(run);
        QPointer<QTreeView> view = current ? current->getDifferenceView() : nullptr;
        const int groups = view && view->model() ? view->model()->rowCount() : 0;
        if (groups > 0)
        {
            const QModelIndex group = view->model()->index(groups - 1, 0);
            const int rows = view->model()->rowCount(group);
            view->setCurrentIndex(rows > 0 ? view->model()->index(rows - 1, 0, group) : group);
        }
        ToolPagesLifetimeSteps::settle();
        return gui(run) != nullptr;
    }

    /// The dialog of the export of the XML of the kept result (its rows "dialog-reviewcompare")
    static pdf::PDFAppToolResultDialog* exportDialog(ToolPagesLifetimeRun& run)
    {
        if (!openTab(run))
        {
            return nullptr;
        }

        // The comparison, unless its result is shown already (Compare Again is Disabled then, Q7.17)
        pdf::PDFAppToolGUICompare* current = gui(run);
        if (current && current->getContent() != pdf::PDFAppToolGUICompare::Content::Result && !compare(run))
        {
            return nullptr;
        }
        current = gui(run);
        if (!current || !current->exportXml())
        {
            return nullptr;
        }
        ToolPagesLifetimeSteps::settle();
        pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
        return shell ? shell->getResultDialog() : nullptr;
    }

    static bool exportStep(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolResultDialog* dialog = exportDialog(run);
        if (!dialog)
        {
            return false;
        }
        QPointer<pdf::PDFAppToolResultDialog> guard(dialog);
        ToolPagesLifetimeSteps::click(dialog->getDiscardButton());
        ToolPagesLifetimeSteps::settle();
        return gui(run) != nullptr;
    }
};

void ToolPagesLifetimePackages::registerReviewCompareScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = ToolPagesLifetimeCompare;

    // Compare (492:17871 … 492:109399): two inputs, Swap, the panel View, the comparison, the views, F6, the filter, the
    // export of the XML of the kept result
    ToolPagesLifetimeScenario compare;
    compare.name = QStringLiteral("reviewcompare");
    compare.toolId = QString::fromLatin1(pdf::PDFDocumentToolCompare::TOOL_ID);
    compare.isOwnTab = true;
    compare.hasPanel = true;
    compare.hasResultDialog = true;
    compare.isResultKept = true;
    compare.openResultDialog = &H::exportDialog;
    compare.prepareRun = &H::openTab;
    compare.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&H::openTabStep);
        steps->push_back(&H::swap);
        steps->push_back(&H::togglePanel);
        steps->push_back(&H::compare);
        steps->push_back(&H::browse);
        steps->push_back(&H::selectByList);
        steps->push_back(&H::exportStep);
    };
    scenarios->push_back(std::move(compare));
}
