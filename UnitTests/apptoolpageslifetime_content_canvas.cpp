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

// The lifetime scenarios of Edit Content, stage E1, package P11 (the canvas: view, scene, tools, Preview;
// zadani_overeni.md 8, the row of UnitTestsAppToolPagesLifetime): a drag of an object in progress, the handle of a
// gesture while its command runs, Preview with its job in a worker, the zoom, a page switch, a change of the language
// during a drag, and the three orders of destruction of zadani_gui.md 4.14 over the real canvas. Every input goes
// through the guards of the test: the victim may be deleted inside of any event; a step, whose part is gone, ends
// without a failure.

#include "apptoolpageslifetime.h"

#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentpreview.h"
#include "pdfappeditcontentscene.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontenttools.h"
#include "pdfappeditcontentview.h"
#include "pdfapptoolgui_content.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"
#include "pdfsettingdefinitions.h"

#include "pdfdocumenttool_content.h"

#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QPointer>
#include <QTranslator>

/// A translator, which is never empty: its installation and its removal send the change of the language
class ContentCanvasLifetimeTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }
};

/// The steps of the scenarios of the canvas
class ContentCanvasLifetimeHelper
{
public:
    ContentCanvasLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;
    using GUI = pdf::PDFAppToolGUIContent;

    static GUI* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
    }

    static pdf::PDFAppEditContentContext* context(const ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        return current ? current->getEditContext() : nullptr;
    }

    static pdf::PDFAppEditContentView* view(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentContext* current = context(run);
        return current ? current->getView() : nullptr;
    }

    /// The document of the scenario with the developer switch on; its pages get two rectangles to drag
    static pdf::PDFAppDocumentTab* openDocument(pdftest::AppToolShellFixture& fixture)
    {
        if (fixture.context->getSettings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), true).isError() ||
            !pdftest::AppToolShellFixture::waitFor([]() { return pdf::PDFAppToolGUIContent::isContentToolRegistered(); }))
        {
            return nullptr;
        }
        return fixture.openDocument(3);
    }

    static bool waitForContainer(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]()
        {
            pdf::PDFAppEditContentContext* current = context(run);
            return !current || current->getContainer();
        });
        Steps::settle();
        return true;
    }

    /// A mouse event at the point of the page on the viewport of the view (nothing, when the view is gone)
    static void sendMouse(const ToolPagesLifetimeRun& run, QEvent::Type type, const QPointF& pagePoint, Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QPointer<pdf::PDFAppEditContentView> current = view(run);
        if (!current)
        {
            return;
        }
        QWidget* viewport = current->viewport();
        const QPoint position = current->mapFromScene(pagePoint);
        QMouseEvent event(type, QPointF(position), viewport->mapToGlobal(QPointF(position)), button, buttons, modifiers);
        QApplication::sendEvent(viewport, &event);
    }

    /// The middle of the paper of the shown page
    static QPointF paperCenter(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentView* current = view(run);
        return current ? current->getContentScene()->getPaperRectangle().center() : QPointF();
    }

    /// A rubber band over the whole paper selects every object; then a drag of the selection with Ctrl (a copy) is
    /// left in progress
    static bool selectAndStartDrag(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentView* current = view(run);
        if (!current)
        {
            return true;
        }
        const QRectF paper = current->getContentScene()->getPaperRectangle();
        const QPointF from = paper.topLeft() - QPointF(10, 10);
        const QPointF to = paper.bottomRight() + QPointF(10, 10);
        sendMouse(run, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        sendMouse(run, QEvent::MouseMove, (from + to) * 0.5, Qt::NoButton, Qt::LeftButton);
        sendMouse(run, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        sendMouse(run, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        Steps::settle();

        const QPointF center = paperCenter(run);
        sendMouse(run, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        sendMouse(run, QEvent::MouseMove, center + QPointF(20, 0), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
        sendMouse(run, QEvent::MouseMove, center + QPointF(40, 10), Qt::NoButton, Qt::LeftButton, Qt::ControlModifier);
        Steps::settle();
        return true;
    }

    /// The release of the drag: its command runs (the slots of the model may close everything)
    static bool finishDrag(ToolPagesLifetimeRun& run)
    {
        const QPointF center = paperCenter(run);
        sendMouse(run, QEvent::MouseButtonRelease, center + QPointF(40, 10), Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
        Steps::settle();
        return true;
    }

    /// A change of the language while a drag of the tool Select is in progress (order 3 of zadani_gui.md 4.14)
    static bool changeLanguageDuringDrag(ToolPagesLifetimeRun& run)
    {
        const QPointF center = paperCenter(run);
        sendMouse(run, QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
        sendMouse(run, QEvent::MouseMove, center + QPointF(30, 30), Qt::NoButton, Qt::LeftButton);
        ContentCanvasLifetimeTranslator translator;
        QCoreApplication::installTranslator(&translator);
        Steps::settle();
        QCoreApplication::removeTranslator(&translator);
        Steps::settle();
        sendMouse(run, QEvent::MouseButtonRelease, center + QPointF(30, 30), Qt::LeftButton, Qt::NoButton);
        Steps::settle();
        return run.window != nullptr;
    }

    /// The keys of the canvas: an arrow (a command), the zoom, Esc of a drag
    static bool keysAndZoom(ToolPagesLifetimeRun& run)
    {
        QPointer<pdf::PDFAppEditContentView> current = view(run);
        if (!current)
        {
            return true;
        }
        current->setFocus();
        Steps::key(current, Qt::Key_Right);
        if (current)
        {
            current->zoomIn();
        }
        if (current)
        {
            current->fitPage();
        }
        Steps::settle();
        return true;
    }

    /// Preview on: a job renders in a worker; the step leaves it running (the next step or the victim ends it)
    static bool previewOn(ToolPagesLifetimeRun& run)
    {
        if (pdf::PDFAppEditContentContext* current = context(run))
        {
            current->setPreviewActive(true);
        }
        Steps::settle();
        return true;
    }

    /// Waits for the picture of Preview, then switches it off
    static bool previewOff(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]()
        {
            pdf::PDFAppEditContentContext* current = context(run);
            return !current || !current->getPreview()->isRendering();
        });
        if (pdf::PDFAppEditContentContext* current = context(run))
        {
            current->setPreviewActive(false);
        }
        Steps::settle();
        return true;
    }

    /// Another page while the old one is shown with a selection (the items of the old page are retired)
    static bool switchPage(ToolPagesLifetimeRun& run)
    {
        QPointer<pdf::PDFAppEditContentContext> current = context(run);
        if (!current)
        {
            return true;
        }
        current->setPage(current->getPageIndex() == 0 ? 1 : 0);
        Steps::settle();
        return waitForContainer(run);
    }

    /// Cancel with Preview rendering (a late picture finds the preview retired), then the tool opens again
    static bool cancelWhileRendering(ToolPagesLifetimeRun& run)
    {
        if (pdf::PDFAppEditContentContext* current = context(run))
        {
            current->setPreviewActive(true);
        }
        pdf::PDFAppToolToolbar* toolbar = Steps::toolbar(run);
        if (!toolbar || !Steps::click(toolbar->getCancelButton()))
        {
            return true;
        }
        Steps::settle();
        if (!run.tab || gui(run))
        {
            return true;
        }
        Steps::openTool(run);
        return true;
    }
};

/// The scenarios of the canvas
class ContentCanvasLifetimeScenarios
{
public:
    ContentCanvasLifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = ContentCanvasLifetimeHelper;
        using Steps = ToolPagesLifetimeSteps;

        ToolPagesLifetimeScenario canvas;
        canvas.name = QStringLiteral("contentcanvas");
        canvas.toolId = QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID);
        canvas.openDocument = &H::openDocument;
        canvas.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::waitForContainer);
            steps->push_back(&H::selectAndStartDrag);
            steps->push_back(&H::finishDrag);
            steps->push_back(&H::keysAndZoom);
            steps->push_back(&H::changeLanguageDuringDrag);
            steps->push_back(&H::switchPage);
        };
        scenarios->push_back(canvas);

        ToolPagesLifetimeScenario preview;
        preview.name = QStringLiteral("contentpreview");
        preview.toolId = QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID);
        preview.openDocument = &H::openDocument;
        preview.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::waitForContainer);
            steps->push_back(&H::previewOn);
            steps->push_back(&H::previewOff);
            steps->push_back(&H::cancelWhileRendering);
            steps->push_back(&H::waitForContainer);
        };
        scenarios->push_back(preview);
    }
};

void ToolPagesLifetimePackages::registerContentCanvasScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    ContentCanvasLifetimeScenarios::add(scenarios);
}
