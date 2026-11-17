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

// The lifetime scenarios of step 7b of phase 2 (plan 8, 9.2 with R7-17, R7-21), package P4: Ink Coverage,
// Output Preview, Soft Proofing (reviewinkcoverage, reviewoutputpreview, reviewsoftproof). Every input goes
// through the guards of the test; the victim may be deleted inside of any of them, a step, whose part is
// gone, ends without a failure.

#include "apptoolpageslifetime.h"

#include "pdfappdocumentsoftproof.h"
#include "pdfappoutputpreviewcanvas.h"
#include "pdfapptoolgui_inkcoverage.h"
#include "pdfapptoolgui_outputpreview.h"
#include "pdfapptoolgui_softproof.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentrows.h"

#include <QStandardItemModel>
#include <QtTest>

/// The steps of the scenarios of package P4
class ReviewBLifetimeHelper
{
public:
    ReviewBLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    template<typename T>
    static T* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<T*>(shell->getGUI()) : nullptr;
    }

    /// The first row of a panel of the toolbar clicked (the panel opened by its button first)
    static bool clickPanelRow(pdf::PDFAppToolPanelPopup* panel, int index)
    {
        QPointer<pdf::PDFAppToolPanelPopup> guard(panel);
        if (!guard || !Steps::click(guard->getButton()))
        {
            return true;
        }
        Steps::settle();
        if (guard && index < int(guard->getRows().size()) && guard->getRows()[size_t(index)])
        {
            Steps::click(guard->getRows()[size_t(index)].data());
        }
        Steps::settle();
        return true;
    }

    // ------------------------------------------------------------- Ink Coverage

    /// Calculate Coverage, the rows during the run, the result
    static bool inkRun(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolToolbar* bar = Steps::toolbar(run);
        if (!bar || !Steps::click(bar->getConfirmButton()))
        {
            return true;
        }
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* shell = Steps::shell(run); return !shell || shell->getState() != pdf::PDFDocumentToolShell::State::Running; });
        Steps::settle();
        return true;
    }

    /// Calculate Coverage and Stop at once (the finished pages stay, 478:14194)
    static bool inkStop(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (shell && shell->getModel())
        {
            // Another selection: the result is stale, the Primary button runs again
            shell->getModel()->setValue(QStringLiteral("pages"), QStringLiteral("1-2"));
        }
        Steps::settle();
        pdf::PDFAppToolToolbar* bar = Steps::toolbar(run);
        if (!bar || !Steps::click(bar->getConfirmButton()))
        {
            return true;
        }
        if (pdf::PDFDocumentToolShell* running = Steps::shell(run))
        {
            running->stop();
        }
        Steps::wait(run, [&run]() { pdf::PDFDocumentToolShell* current = Steps::shell(run); return !current || current->getState() != pdf::PDFDocumentToolShell::State::Running; });
        Steps::settle();
        return true;
    }

    /// The export menu and its CSV row (the dialog of the export, discarded by Esc)
    static bool inkExport(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIInkCoverage* current = gui<pdf::PDFAppToolGUIInkCoverage>(run);
        if (!current)
        {
            return true;
        }
        clickPanelRow(current->getExportPanel(), 0);
        if (pdf::PDFDocumentToolShell* shell = Steps::shell(run))
        {
            if (QPointer<pdf::PDFAppToolResultDialog> dialog = shell->getResultDialog())
            {
                Steps::key(dialog, Qt::Key_Escape);
            }
        }
        Steps::settle();
        return true;
    }

    /// The rows "dialog-reviewinkcoverage": Calculate Coverage, then the dialog of the export of the CSV of the
    /// kept table (478:15674), left open
    static pdf::PDFAppToolResultDialog* inkExportDialog(ToolPagesLifetimeRun& run)
    {
        inkRun(run);
        pdf::PDFAppToolGUIInkCoverage* current = gui<pdf::PDFAppToolGUIInkCoverage>(run);
        if (!current)
        {
            return nullptr;
        }
        clickPanelRow(current->getExportPanel(), 0);
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? shell->getResultDialog() : nullptr;
    }

    // ------------------------------------------------------------- Output Preview

    /// The first picture
    static bool previewRendered(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]() { pdf::PDFAppToolGUIOutputPreview* current = gui<pdf::PDFAppToolGUIOutputPreview>(run); return !current || (current->getResult() && !current->isRendering()); });
        return true;
    }

    /// Every mode of the menu (the picture of the stored result)
    static bool previewModes(ToolPagesLifetimeRun& run)
    {
        for (int index = 0; index < 6; ++index)
        {
            pdf::PDFAppToolGUIOutputPreview* current = gui<pdf::PDFAppToolGUIOutputPreview>(run);
            if (!current)
            {
                return true;
            }
            clickPanelRow(current->getModePanel(), index);
        }
        return true;
    }

    /// The switches, an ink unchecked, the pointer over the page, the next page - each renders again (a new
    /// request cancels the old job)
    static bool previewChanges(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIOutputPreview* current = gui<pdf::PDFAppToolGUIOutputPreview>(run);
        if (!current)
        {
            return true;
        }
        Steps::click(current->getSimulateButton());
        current = gui<pdf::PDFAppToolGUIOutputPreview>(run);
        if (current)
        {
            Steps::click(current->getPaperButton());
        }
        current = gui<pdf::PDFAppToolGUIOutputPreview>(run);
        if (current && current->getInkModel() && current->getInkModel()->rowCount() > 0)
        {
            QStandardItem* group = current->getInkModel()->item(0);
            if (group && group->rowCount() > 0)
            {
                group->child(0)->setCheckState(Qt::Unchecked);
            }
        }
        current = gui<pdf::PDFAppToolGUIOutputPreview>(run);
        if (current)
        {
            current->setHoveredPixel(QPoint(5, 5));
        }
        current = gui<pdf::PDFAppToolGUIOutputPreview>(run);
        if (current && current->getCanvas())
        {
            Steps::key(current->getCanvas(), Qt::Key_PageDown);
        }
        Steps::settle();
        return true;
    }

    /// Close while a picture renders (its late result is dropped)
    static bool previewCloseRendering(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUIOutputPreview* current = gui<pdf::PDFAppToolGUIOutputPreview>(run);
        if (current && current->getCanvas())
        {
            Steps::key(current->getCanvas(), Qt::Key_PageUp);
        }
        if (pdf::PDFAppToolToolbar* bar = Steps::toolbar(run))
        {
            Steps::click(bar->getCancelButton());
        }
        Steps::settle();
        return true;
    }

    // ------------------------------------------------------------- Soft Proofing

    /// The switches and the menus of the intent
    static bool proofChanges(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUISoftProof* current = gui<pdf::PDFAppToolGUISoftProof>(run);
        if (!current)
        {
            return true;
        }
        Steps::click(current->getGamutButton());
        current = gui<pdf::PDFAppToolGUISoftProof>(run);
        if (current)
        {
            Steps::click(current->getProofButton());
        }
        current = gui<pdf::PDFAppToolGUISoftProof>(run);
        if (current)
        {
            clickPanelRow(current->getIntentPanel(), 1);
        }
        current = gui<pdf::PDFAppToolGUISoftProof>(run);
        if (current)
        {
            clickPanelRow(current->getProfilePanel(), 0);
        }
        return true;
    }

    /// The state of the tab kept by the API (Keep Proofing On) and the click on the item of the status bar
    static bool proofKeptItem(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolGUISoftProof* current = gui<pdf::PDFAppToolGUISoftProof>(run);
        QPointer<pdf::PDFAppDocumentSoftProof> softProof = current ? current->getSoftProof() : nullptr;
        if (softProof)
        {
            softProof->setProofing(true);
        }
        if (softProof)
        {
            softProof->setKept(true);
        }
        if (softProof && softProof->getStatusItem() && softProof->getStatusItem()->isVisible())
        {
            Steps::click(softProof->getStatusItem());
        }
        Steps::settle();
        return true;
    }
};

/// The scenarios of package P4
class ReviewBLifetimeScenarios
{
public:
    ReviewBLifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = ReviewBLifetimeHelper;
        using Steps = ToolPagesLifetimeSteps;

        // Ink Coverage: the run with its rows, a stopped run, the export
        ToolPagesLifetimeScenario ink;
        ink.name = QStringLiteral("reviewinkcoverage");
        ink.toolId = QStringLiteral("review.ink-coverage");

        // The only panel (Export, 478:13429) is Disabled until a table is measured (478:11760): its panel is
        // opened by the step inkExport after the run, not by the rows "panel-" of an entered tool
        ink.hasPanel = false;

        // The rows "dialog-": the export of the kept table; Discard keeps the tool (R7-2, R7-13)
        ink.hasResultDialog = true;
        ink.isResultKept = true;
        ink.openResultDialog = &H::inkExportDialog;
        ink.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::inkRun);
            steps->push_back(&H::inkExport);
            steps->push_back(&H::inkStop);
        };
        scenarios->push_back(ink);

        // Output Preview: the job of the picture, the modes, the changes, the close during a job
        ToolPagesLifetimeScenario preview;
        preview.name = QStringLiteral("reviewoutputpreview");
        preview.toolId = QStringLiteral("review.output-preview");
        preview.hasPanel = true;
        preview.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::previewRendered);
            steps->push_back(&H::previewModes);
            steps->push_back(&H::previewChanges);
            steps->push_back(&H::previewCloseRendering);
        };
        scenarios->push_back(preview);

        // Soft Proofing: the switches, the menus, the kept state and its item of the status bar
        ToolPagesLifetimeScenario proof;
        proof.name = QStringLiteral("reviewsoftproof");
        proof.toolId = QStringLiteral("review.soft-proof");
        proof.hasPanel = true;
        proof.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::proofChanges);
            steps->push_back(&H::proofKeptItem);
        };
        scenarios->push_back(proof);
    }
};

void ToolPagesLifetimePackages::registerReviewBScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    ReviewBLifetimeScenarios::add(scenarios);
}
