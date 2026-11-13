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

// The lifetime scenarios of package B of step 3a of phase 2 (Crop, Resize, Set Page Boxes): the
// drag of the box of a crop, the keys of the box, the anchor, the menus, the thumbnails of the
// page of the preview, the stage of a page, the result dialog (plan 6, 8)

#include "apptoolpageslifetime.h"
#include "mergetestpdf.h"

#include "pdfappdocument.h"
#include "pdfappundostack.h"
#include "pdfapptoolcropoverlay.h"
#include "pdfapptoolgui_geometry.h"
#include "pdfapptoolpagestage.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfanchorgrid.h"
#include "pdfdocmdppermissions.h"
#include "pdfdocumenttool_geometry.h"

#include <QCheckBox>
#include <QFile>
#include <QtTest>

/// Helpers of the scenarios of package B
class GeometryLifetimeHelper
{
public:
    GeometryLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;

    template<typename GUI>
    static GUI* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    /// The panel of the grid of the anchor (an ancestor of the grid)
    static pdf::PDFAppToolAnchorPanel* panelOf(QWidget* grid)
    {
        for (QObject* object = grid; object; object = object->parent())
        {
            if (pdf::PDFAppToolAnchorPanel* panel = qobject_cast<pdf::PDFAppToolAnchorPanel*>(object))
            {
                return panel;
            }
        }
        return nullptr;
    }

    /// A click on the thumbnail of the page in the left panel (the page of the preview)
    static bool clickThumbnail(const ToolPagesLifetimeRun& run, int page)
    {
        pdf::PDFAppPageGeometryToolGUI* current = gui<pdf::PDFAppPageGeometryToolGUI>(run);
        pdf::PDFAppThumbnailListView* view = current ? current->getThumbnailView() : nullptr;
        if (!view || !view->model() || page >= view->model()->rowCount())
        {
            return false;
        }
        QPointer<pdf::PDFAppThumbnailListView> guard(view);
        view->scrollTo(view->model()->index(page, 0));
        Steps::settle();
        if (!guard)
        {
            return true;
        }
        clickAt(guard->viewport(), guard->visualRect(guard->model()->index(page, 0)).center());
        return true;
    }

    /// A click of the mouse, whose press may delete the widget (no release then)
    static bool clickAt(QWidget* widget, const QPoint& point)
    {
        if (!widget)
        {
            return false;
        }
        QPointer<QWidget> guard(widget);
        QTest::mousePress(widget, Qt::LeftButton, Qt::NoModifier, point);
        if (guard)
        {
            QTest::mouseRelease(widget, Qt::LeftButton, Qt::NoModifier, point);
        }
        Steps::settle();
        return true;
    }

    /// A key, whose press may delete the widget (no release then)
    static bool pressKey(QWidget* widget, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        if (!widget)
        {
            return false;
        }
        QPointer<QWidget> guard(widget);
        QTest::keyPress(widget, key, modifiers);
        if (guard)
        {
            QTest::keyRelease(widget, key, modifiers);
        }
        Steps::settle();
        return true;
    }

    /// A certified document of three pages (DocMDP /P 2, ISO 32000-2 12.8.2.2) in its tab
    static pdf::PDFAppDocumentTab* openCertified(pdftest::AppToolShellFixture& fixture)
    {
        mergetest::MergeTestPdf pdf = mergetest::MergeTestPdf::createPages("C", 3, "/AcroForm << /Fields [60 0 R] /SigFlags 3 >> /Perms << /DocMDP 61 0 R >>", { { 1, "/Annots [60 0 R]" } });
        pdf.set(60, "<< /Type /Annot /Subtype /Widget /FT /Sig /T (cert) /V 61 0 R /Rect [0 0 0 0] /P 3 0 R >>");
        pdf.set(61, "<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached /ByteRange [0 0 0 0] /Contents <00> /Reference [<< /TransformMethod /DocMDP /TransformParams << /P 2 >> >>] >>");
        const QByteArray bytes = pdf.write();
        const QString path = fixture.folder() + QStringLiteral("/certified-crop.pdf");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        {
            return nullptr;
        }
        file.close();
        return fixture.open(path);
    }

    /// A drag of the right handle of the box of the crop by the mouse
    static bool dragHandle(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppCropToolGUI* current = gui<pdf::PDFAppCropToolGUI>(run);
        QPointer<pdf::PDFAppCropOverlay> overlay = current ? current->getOverlay() : nullptr;
        if (!overlay)
        {
            return false;
        }
        const QPoint start = overlay->getHandleRect(pdf::PDFAppCropOverlay::Part::Right).center().toPoint();
        QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, start);
        for (int step = 1; step <= 3 && overlay; ++step)
        {
            QTest::mouseMove(overlay, start - QPoint(10 * step, 0));
        }
        if (overlay)
        {
            QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, start - QPoint(30, 0));
        }
        Steps::settle();
        return true;
    }
};

void ToolPagesLifetimePackages::registerGeometryScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using H = GeometryLifetimeHelper;
    using Steps = ToolPagesLifetimeSteps;

    // Crop Pages: the thumbnail of the preview, the drag and the keys of the box, the menu of the
    // mode, Remove Crop, Undo / Redo, the result dialog Keep
    ToolPagesLifetimeScenario crop;
    crop.name = QStringLiteral("crop");
    crop.toolId = QStringLiteral("pages.crop");
    crop.hasResultDialog = true;
    crop.hasPanel = true;
    crop.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickThumbnail(run, 1); });
        steps->push_back(&H::dragHandle);
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppCropToolGUI* current = H::gui<pdf::PDFAppCropToolGUI>(run);
            return current && H::pressKey(current->getOverlay(), Qt::Key_Right) && H::pressKey(H::gui<pdf::PDFAppCropToolGUI>(run) ? H::gui<pdf::PDFAppCropToolGUI>(run)->getOverlay() : nullptr, Qt::Key_Down, Qt::ShiftModifier);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The menu of the mode: Equal margins
            pdf::PDFAppCropToolGUI* current = H::gui<pdf::PDFAppCropToolGUI>(run);
            if (!current || !Steps::click(current->getModeButton()))
            {
                return false;
            }
            current = H::gui<pdf::PDFAppCropToolGUI>(run);
            return current && Steps::click(current->getModeRow(QStringLiteral("equal-margins")));
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppCropToolGUI* current = H::gui<pdf::PDFAppCropToolGUI>(run);
            return current && Steps::click(current->getRemoveButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getUndoButton() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getRedoButton() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppCropToolGUI* current = H::gui<pdf::PDFAppCropToolGUI>(run);
            return current && Steps::click(current->getRemoveButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::confirmDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(crop);

    // Resize Pages: the menus of the toolbar, the anchor of the grid by the mouse and the keys, the
    // Custom size, Keep size, the result dialog Keep
    ToolPagesLifetimeScenario resize;
    resize.name = QStringLiteral("resize");
    resize.toolId = QStringLiteral("pages.resize");
    resize.hasResultDialog = true;
    resize.hasPanel = true;
    resize.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickThumbnail(run, 2); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // The panel of the anchor: a click on the cell at the top left, then the keys
            pdf::PDFAppResizeToolGUI* current = H::gui<pdf::PDFAppResizeToolGUI>(run);
            QPointer<pdf::PDFAnchorGrid> grid = current ? current->getAnchorGrid() : nullptr;
            pdf::PDFAppToolAnchorPanel* panel = grid ? H::panelOf(grid) : nullptr;
            if (!panel || !Steps::click(panel->getButton()))
            {
                return false;
            }
            if (grid)
            {
                H::clickAt(grid, grid->getCellRect(0).center());
            }
            current = H::gui<pdf::PDFAppResizeToolGUI>(run);
            grid = current ? current->getAnchorGrid() : nullptr;
            panel = grid ? H::panelOf(grid) : nullptr;
            if (panel && Steps::click(panel->getButton()) && grid)
            {
                H::pressKey(grid, Qt::Key_Right);
                if (grid)
                {
                    H::pressKey(grid, Qt::Key_Space);
                }
            }
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFDocumentToolShell* shell = Steps::shell(run);
            if (!shell || !shell->getModel())
            {
                return false;
            }
            shell->getModel()->setValue(QStringLiteral("pageSize"), QStringLiteral("custom"));
            Steps::settle();
            shell = Steps::shell(run);
            if (shell && shell->getModel())
            {
                shell->getModel()->setValue(QStringLiteral("content"), QStringLiteral("keep-size"));
                Steps::settle();
            }
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getUndoButton() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::confirmDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(resize);

    // Set Page Boxes: Write to (the rows stay open), the toggle of the boxes, Based on, the result
    // dialog Keep
    ToolPagesLifetimeScenario boxes;
    boxes.name = QStringLiteral("boxes");
    boxes.toolId = QStringLiteral("pages.boxes");
    boxes.hasResultDialog = true;
    boxes.hasPanel = true;
    boxes.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back(&Steps::openTool);
        steps->push_back([](ToolPagesLifetimeRun& run) { return H::clickThumbnail(run, 1); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppBoxesToolGUI* current = H::gui<pdf::PDFAppBoxesToolGUI>(run);
            if (!current || !Steps::click(current->getWriteToButton()))
            {
                return false;
            }
            current = H::gui<pdf::PDFAppBoxesToolGUI>(run);
            if (!current || !Steps::click(current->getWriteToRow(pdf::PDFPageGeometry::Box::TrimBox)))
            {
                return false;
            }
            current = H::gui<pdf::PDFAppBoxesToolGUI>(run);
            return current && Steps::click(current->getWriteToRow(pdf::PDFPageGeometry::Box::ArtBox));
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppBoxesToolGUI* current = H::gui<pdf::PDFAppBoxesToolGUI>(run);
            if (!current || !Steps::click(current->getShowBoxesButton()))
            {
                return false;
            }
            current = H::gui<pdf::PDFAppBoxesToolGUI>(run);
            return current && Steps::click(current->getShowBoxesButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::click(Steps::toolbar(run) ? Steps::toolbar(run)->getUndoButton() : nullptr); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return Steps::confirmDialog(run, Steps::confirmToDialog(run)); });
    };
    scenarios->push_back(boxes);

    // certifiedcrop (owner decision of 13. 11. 2026): Crop Pages on a certified document - the
    // callout of the shell (PDFDocumentToolShell::getCertificationCallout) is made at the entry,
    // removed by a change of the document, which removes the certification, made again by Undo
    // and by a change of the language; the run ends in the result dialog of the copy, which
    // opens the copy in a new tab (its view is built inside of the emissions of the dialog)
    ToolPagesLifetimeScenario certified;
    certified.name = QStringLiteral("certifiedcrop");
    certified.toolId = QStringLiteral("pages.crop");
    certified.hasResultDialog = true;
    certified.openDocument = &H::openCertified;
    certified.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFDocumentToolShell* shell = Steps::openTool(run) ? Steps::shell(run) : nullptr;
            return shell && shell->getCertificationCallout();
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Remove Crop and back: two changes of the parameters (the callout is updated by each)
            pdf::PDFAppCropToolGUI* current = H::gui<pdf::PDFAppCropToolGUI>(run);
            if (!current || !Steps::click(current->getRemoveButton()))
            {
                return false;
            }
            current = H::gui<pdf::PDFAppCropToolGUI>(run);
            return current && Steps::click(current->getRemoveButton());
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Another tool removes the certification below the open tool, then the certified
            // version comes back (the Undo of the document is refused while a tool is open)
            pdf::PDFAppDocument* document = run.tab ? run.tab->getDocument() : nullptr;
            if (!document || !document->getDocument())
            {
                return false;
            }
            const pdf::PDFDocumentPointer certified = document->getDocument();
            const pdf::PDFDocumentPointer changed(new pdf::PDFDocument(pdf::PDFDocMDPPermissions::removeCertification(*certified, nullptr)));
            QPointer<pdf::PDFAppDocument> guard(document);
            document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp);
            Steps::settle();
            if (guard)
            {
                guard->getUndoStack()->undo();
                guard->commit(pdf::PDFModifiedDocument(certified, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), guard->getVersion(), pdf::PDFAppChangeSource::Mcp);
                Steps::settle();
            }
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Save as a copy, the only choice of a certified document (Q12); "Open the copy"
            // stays checked: the copy opens in a new tab of the window
            QPointer<pdf::PDFAppToolResultDialog> dialog = Steps::confirmToDialog(run);
            if (!dialog || !dialog->getOpenCheckBox() || !dialog->getOpenCheckBox()->isChecked())
            {
                return false;
            }
            if (!Steps::confirmDialog(run, dialog.data()))
            {
                return false;
            }
            // The copy has its tab next to the tab of the certified document
            const QList<pdf::PDFAppDocumentTab*> tabs = run.window ? run.window->getDocumentTabs() : QList<pdf::PDFAppDocumentTab*>();
            return tabs.size() == 2 && tabs.contains(run.tab.data());
        });
    };
    scenarios->push_back(certified);
}
