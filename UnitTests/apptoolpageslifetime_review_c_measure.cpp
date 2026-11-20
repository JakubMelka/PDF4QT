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

// The lifetime scenarios of step 7c of phase 2 (plan 8, R7-21), package P7: Measure and its dialogs
// (reviewmeasure, measurepresetsdialog). Every step goes through the guards of the test: the victim may be
// deleted inside of any of its events; a step, whose part is gone, ends without a failure.

#include "apptoolpageslifetime.h"

#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapptoolgui_measure.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfapptoolundotarget.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"

#include <QAction>
#include <QPushButton>
#include <QToolButton>

/// The steps of the scenarios of Measure
class MeasureLifetimeHelper
{
public:
    MeasureLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;
    using GUI = pdf::PDFAppToolGUIMeasure;

    static GUI* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    /// The points of a measurement picked by the overlay (the pick is emitted from the event loop)
    static bool pick(ToolPagesLifetimeRun& run, pdf::PDFInteger pageIndex, const std::vector<QPointF>& points)
    {
        for (const QPointF& point : points)
        {
            GUI* current = gui(run);
            QPointer<pdf::PDFAppMeasureOverlay> overlay = current ? current->getOverlay() : nullptr;
            if (!overlay)
            {
                return true;
            }
            overlay->addPoint(pageIndex, point);
        }
        Steps::settle();
        return true;
    }

    /// Linear on page 1 by the overlay
    static bool drawLinear(ToolPagesLifetimeRun& run)
    {
        return pick(run, 0, { QPointF(100, 100), QPointF(172, 100) });
    }

    /// Area on page 2: three corners, closed by the first point
    static bool drawArea(ToolPagesLifetimeRun& run)
    {
        if (GUI* current = gui(run))
        {
            current->setDrawKind(pdf::PDFAppMeasureDrawKind::Area);
        }
        Steps::settle();
        return pick(run, 1, { QPointF(100, 100), QPointF(200, 100), QPointF(200, 200), QPointF(100, 100) });
    }

    /// Two clicks on the canvas, a right click between them starts over
    static bool drawByMouse(ToolPagesLifetimeRun& run)
    {
        if (GUI* current = gui(run))
        {
            current->setDrawKind(pdf::PDFAppMeasureDrawKind::Linear);
        }
        QPointer<pdf::PDFAppDocumentTab> tab = run.tab;
        if (tab && tab->getView())
        {
            pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, QPointF(200, 300));
        }
        if (tab && tab->getView())
        {
            pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, QPointF(250, 300), Qt::NoModifier, Qt::RightButton);
        }
        if (tab && tab->getView())
        {
            pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, QPointF(200, 300));
        }
        if (tab && tab->getView())
        {
            pdftest::AppDocumentFixture::clickPage(tab->getView(), 0, QPointF(300, 350));
        }
        Steps::settle();
        return true;
    }

    /// A row of the list selected by the view, then the × of a temporary measurement
    static bool selectAndRemove(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        QPointer<pdf::PDFListView> view = current ? current->getListView() : nullptr;
        if (view && view->model() && view->model()->rowCount() > 0)
        {
            view->setCurrentIndex(view->model()->index(0, 0));
        }
        Steps::settle();
        current = gui(run);
        if (current && !current->getMeasurements().empty())
        {
            current->removeMeasurement(current->getMeasurements().front().id);
        }
        Steps::settle();
        return true;
    }

    /// Show, the big cross and back
    static bool toggles(ToolPagesLifetimeRun& run)
    {
        for (int i = 0; i < 2; ++i)
        {
            GUI* current = gui(run);
            if (!current || !Steps::click(current->getShowButton()))
            {
                return true;
            }
            current = gui(run);
            if (!current || !Steps::click(current->getCrossButton()))
            {
                return true;
            }
        }
        Steps::settle();
        return true;
    }

    /// A preset of the panel Scale chosen by its row
    static bool chooseScale(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getScaleButton()))
        {
            return true;
        }
        Steps::settle();
        current = gui(run);
        QPointer<pdf::PDFAppToolPanelPopup> panel = current ? current->getScalePanel() : nullptr;
        if (panel)
        {
            const QList<pdf::PDFTileRow*> rows = panel->findChildren<pdf::PDFTileRow*>();
            if (rows.size() > 1)
            {
                Steps::click(rows[1]);
            }
        }
        Steps::settle();
        return true;
    }

    /// Save as Annotations by the Primary button (one step of Undo), then Undo of the tool
    static bool saveAndUndo(ToolPagesLifetimeRun& run)
    {
        drawLinear(run);
        pdf::PDFAppToolToolbar* toolbar = Steps::toolbar(run);
        if (!toolbar || !Steps::click(toolbar->getConfirmButton()))
        {
            return true;
        }
        Steps::settle();
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (shell && shell->getUndoTarget() && shell->getUndoTarget()->canUndo())
        {
            shell->getUndoTarget()->undo();
        }
        Steps::settle();
        return true;
    }

    /// The storage Annotations in the document: a measurement written at once
    static bool annotationsStorage(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        if (!shell || !shell->getModel())
        {
            return true;
        }
        shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_STORAGE), QString::fromLatin1(pdf::PDFDocumentToolMeasure::STORAGE_ANNOTATIONS));
        Steps::settle();
        drawLinear(run);
        shell = Steps::shell(run);
        if (shell && shell->getModel())
        {
            shell->getModel()->setValue(QString::fromLatin1(pdf::PDFDocumentToolMeasure::PARAMETER_STORAGE), QString::fromLatin1(pdf::PDFDocumentToolMeasure::STORAGE_TEMPORARY));
        }
        Steps::settle();
        return true;
    }

    /// Calibrate Scale…: two points, the dialog (473:6438) with a real distance, Use Scale
    static bool calibrate(ToolPagesLifetimeRun& run)
    {
        if (GUI* current = gui(run))
        {
            current->startCalibration();
        }
        pick(run, 0, { QPointF(100, 100), QPointF(100, 172) });
        Steps::wait(run, [&run]() { GUI* current = gui(run); return !current || current->getCalibrationDialog(); });
        GUI* current = gui(run);
        QPointer<pdf::PDFAppMeasureScaleDialog> dialog = current ? current->getCalibrationDialog() : nullptr;
        if (dialog && dialog->getRealValueBox())
        {
            dialog->getRealValueBox()->setValue(2540.0);
        }
        if (dialog)
        {
            Steps::click(dialog->getAcceptButton());
        }
        Steps::settle();
        return true;
    }

    /// Calibrate Scale… canceled in its dialog (review RS7CAPP L5: Cancel ends the calibration from the event loop)
    static bool calibrateCanceled(ToolPagesLifetimeRun& run)
    {
        if (GUI* current = gui(run))
        {
            current->startCalibration();
        }
        pick(run, 0, { QPointF(120, 100), QPointF(120, 172) });
        Steps::wait(run, [&run]() { GUI* current = gui(run); return !current || current->getCalibrationDialog(); });
        GUI* current = gui(run);
        QPointer<pdf::PDFAppMeasureScaleDialog> dialog = current ? current->getCalibrationDialog() : nullptr;
        if (dialog)
        {
            dialog->reject();
        }
        Steps::settle();
        return true;
    }

    /// A zero length of the calibration (473:6755): Pick Again
    static bool zeroLength(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        QPointer<pdf::PDFAppMeasureOverlay> overlay = current ? current->getOverlay() : nullptr;
        if (!overlay)
        {
            return true;
        }
        Q_EMIT overlay->calibrationPicked(0, 0.0);
        Steps::settle();
        current = gui(run);
        QPointer<pdf::PDFMessageBox> dialog = current ? current->getZeroLengthDialog() : nullptr;
        if (dialog)
        {
            Steps::click(dialog->findChild<QPushButton*>(QStringLiteral("measureZeroLengthAgain")));
        }
        Steps::settle();
        return true;
    }

    /// Esc of the shell: the points of a drawing, Calibrate, the drawing mode, the selection
    static bool escapes(ToolPagesLifetimeRun& run)
    {
        pick(run, 0, { QPointF(300, 300) });
        if (GUI* current = gui(run))
        {
            current->selectRow(0);
        }
        for (int i = 0; i < 3; ++i)
        {
            pdf::PDFDocumentToolShell* shell = Steps::shell(run);
            QAction* escape = shell ? shell->findChild<QAction*>(QStringLiteral("toolEscape")) : nullptr;
            if (!escape)
            {
                break;
            }
            escape->trigger();
            Steps::settle();
        }
        if (GUI* current = gui(run))
        {
            current->setDrawKind(pdf::PDFAppMeasureDrawKind::Linear);
        }
        Steps::settle();
        return true;
    }

    /// The document changes (an assistant): the annotations are read again
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

    /// A locked document and back (the callouts and the Primary button follow the lock)
    static bool lock(ToolPagesLifetimeRun& run)
    {
        QPointer<pdf::PDFAppDocumentTab> tab = run.tab;
        if (pdf::PDFAppDocument* document = tab ? tab->getDocument() : nullptr)
        {
            document->setLocked(true);
        }
        Steps::settle();
        if (pdf::PDFAppDocument* document = tab ? tab->getDocument() : nullptr)
        {
            document->setLocked(false);
        }
        Steps::settle();
        return true;
    }

    /// Clear of the toolbar
    static bool clear(ToolPagesLifetimeRun& run)
    {
        drawLinear(run);
        if (GUI* current = gui(run))
        {
            Steps::click(current->getClearButton());
        }
        Steps::settle();
        return true;
    }

    /// Export CSV (473:91038) with a temporary measurement: the export dialog of the shell
    static pdf::PDFAppToolResultDialog* exportDialog(ToolPagesLifetimeRun& run)
    {
        drawLinear(run);
        GUI* current = gui(run);
        if (!current || !Steps::click(current->getExportButton()))
        {
            return nullptr;
        }
        Steps::settle();
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? shell->getResultDialog() : nullptr;
    }

    /// Export CSV and Discard of its dialog (the tool stays)
    static bool exportAndDiscard(ToolPagesLifetimeRun& run)
    {
        QPointer<pdf::PDFAppToolResultDialog> dialog = exportDialog(run);
        if (dialog)
        {
            dialog->reject();
        }
        Steps::settle();
        return true;
    }

    // ------------------------------------------------------------- the dialogs of the scales

    /// Custom Scale… (473:7024) with a name: Use Scale (a new preset too)
    static bool customScale(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        QPointer<pdf::PDFAppMeasureScaleDialog> dialog = current ? current->openCustomScaleDialog() : nullptr;
        Steps::settle();
        if (dialog && dialog->getNameEdit())
        {
            dialog->getNameEdit()->setText(QStringLiteral("Custom 1:75"));
        }
        if (dialog && dialog->getRealValueBox())
        {
            dialog->getRealValueBox()->setValue(75.0);
        }
        if (dialog)
        {
            Steps::click(dialog->getAcceptButton());
        }
        Steps::settle();
        return true;
    }

    /// The default scale (473:92070, Change… of the card): Set as Default
    static bool defaultScale(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        QPointer<pdf::PDFAppMeasureScaleDialog> dialog = current ? current->openDefaultScaleDialog() : nullptr;
        Steps::settle();
        if (dialog && dialog->getRealValueBox())
        {
            dialog->getRealValueBox()->setValue(20.0);
        }
        if (dialog)
        {
            Steps::click(dialog->getAcceptButton());
        }
        Steps::settle();
        return true;
    }

    /// Manage Presets… (473:7412): Add… (473:7860) with its name, Edit…, Remove, Restore Defaults (its question),
    /// Save Presets
    static bool presets(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        QPointer<pdf::PDFAppMeasurePresetsDialog> dialog = current ? current->openPresetsDialog() : nullptr;
        Steps::settle();
        if (!dialog)
        {
            return true;
        }

        QPointer<pdf::PDFAppMeasureScaleDialog> add = dialog->addPreset();
        Steps::settle();
        if (add && add->getNameEdit())
        {
            add->getNameEdit()->setText(QStringLiteral("Site 1:250"));
        }
        if (add && add->getRealValueBox())
        {
            add->getRealValueBox()->setValue(250.0);
        }
        if (add)
        {
            Steps::click(add->getAcceptButton());
        }
        Steps::settle();

        if (dialog)
        {
            dialog->selectPreset(0);
        }
        QPointer<pdf::PDFAppMeasureScaleDialog> edit = dialog ? dialog->editPreset() : nullptr;
        Steps::settle();
        if (edit)
        {
            Steps::click(edit->getAcceptButton());
        }
        Steps::settle();

        if (dialog)
        {
            dialog->selectPreset(1);
            Steps::click(dialog->getRemoveButton());
        }
        Steps::settle();

        QPointer<pdf::PDFMessageBox> question = dialog ? dialog->restoreDefaults() : nullptr;
        Steps::settle();
        if (question)
        {
            for (QPushButton* button : question->findChildren<QPushButton*>())
            {
                if (button->text() == pdf::PDFAppMeasurePresetsDialog::tr("Restore Defaults"))
                {
                    Steps::click(button);
                    break;
                }
            }
        }
        Steps::settle();

        if (dialog)
        {
            Steps::click(dialog->getSaveButton());
        }
        Steps::settle();
        return true;
    }

    /// The presets dialog canceled with an open Add… (the child dialog goes with it)
    static bool presetsCanceled(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        QPointer<pdf::PDFAppMeasurePresetsDialog> dialog = current ? current->openPresetsDialog() : nullptr;
        Steps::settle();
        if (dialog)
        {
            dialog->addPreset();
        }
        Steps::settle();
        if (dialog)
        {
            dialog->reject();
        }
        Steps::settle();
        return true;
    }
};

/// The scenarios reviewmeasure and measurepresetsdialog
class MeasureLifetimeScenarios
{
public:
    MeasureLifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = MeasureLifetimeHelper;
        using Steps = ToolPagesLifetimeSteps;

        // Measure: the drawing (overlay, mouse, more pages), the list, the toggles, the panel Scale, Save as
        // Annotations and Undo, the storage Annotations, the calibration and its zero length, Esc, a new version of
        // the document, the lock, Clear, Export CSV
        ToolPagesLifetimeScenario measure;
        measure.name = QStringLiteral("reviewmeasure");
        measure.toolId = QString::fromLatin1(pdf::PDFDocumentToolMeasure::TOOL_ID);
        measure.hasPanel = true;
        measure.prepareRun = &H::drawLinear;

        // The rows "dialog-": the export of the list (473:91038); Discard returns to the tool
        measure.hasResultDialog = true;
        measure.isResultKept = true;
        measure.openResultDialog = &H::exportDialog;
        measure.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::drawLinear);
            steps->push_back(&H::drawArea);
            steps->push_back(&H::drawByMouse);
            steps->push_back(&H::selectAndRemove);
            steps->push_back(&H::toggles);
            steps->push_back(&H::chooseScale);
            steps->push_back(&H::saveAndUndo);
            steps->push_back(&H::annotationsStorage);
            steps->push_back(&H::calibrate);
            steps->push_back(&H::calibrateCanceled);
            steps->push_back(&H::zeroLength);
            steps->push_back(&H::escapes);
            steps->push_back(&H::changeDocument);
            steps->push_back(&H::lock);
            steps->push_back(&H::exportAndDiscard);
            steps->push_back(&H::clear);
        };
        scenarios->push_back(measure);

        // The dialogs of the scales: Custom Scale…, the default scale, Manage Presets… with its child dialogs
        ToolPagesLifetimeScenario presets;
        presets.name = QStringLiteral("measurepresetsdialog");
        presets.toolId = QString::fromLatin1(pdf::PDFDocumentToolMeasure::TOOL_ID);
        presets.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::customScale);
            steps->push_back(&H::defaultScale);
            steps->push_back(&H::presets);
            steps->push_back(&H::presetsCanceled);
        };
        scenarios->push_back(presets);
    }
};

void ToolPagesLifetimePackages::registerReviewMeasureScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    MeasureLifetimeScenarios::add(scenarios);
}
