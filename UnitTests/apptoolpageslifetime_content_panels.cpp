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

// The lifetime scenario of Edit Content, stage E1, package P12 (the tabs Objects and Pages, the menus;
// zadani_overeni.md 8, zadani_gui.md 4.14): contentpanels - a row selected in the tree, the context menu open over a
// change of the language (order (3): the panels are built again, the menu is closed), a row of the menu, a drop of a
// row (the command from the event loop), the filter typed, a tile of the Pages tab clicked, Delete in the tree, then
// Cancel (order (1)) and the tool again. The victims of the sweep are deleted inside of any event of a step; a step,
// whose part is gone, ends without a failure.

#include "apptoolpageslifetime.h"

#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentmenus.h"
#include "pdfappeditcontentpanels_objects.h"
#include "pdfappeditcontentpanels_pages.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfapptoolgui_content.h"
#include "pdfapptoolpagegrid.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"
#include "pdfsettingdefinitions.h"

#include "pdffluentwidgets.h"

#include "pdfdocumenttool_content.h"

#include <QAction>
#include <QCoreApplication>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QTimer>
#include <QTranslator>
#include <QtTest>

#include <memory>

/// A translator, which is never empty: its installation and its removal send the change of the language
class ContentPanelsLifetimeTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }
};

/// The steps of the scenario contentpanels
class ContentPanelsLifetimeHelper
{
public:
    ContentPanelsLifetimeHelper() = delete;

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

    static pdf::PDFAppEditContentObjectsPanel* objectsPanel(const ToolPagesLifetimeRun& run)
    {
        return run.window ? run.window->findChild<pdf::PDFAppEditContentObjectsPanel*>() : nullptr;
    }

    static pdf::PDFAppEditContentPagesPanel* pagesPanel(const ToolPagesLifetimeRun& run)
    {
        return run.window ? run.window->findChild<pdf::PDFAppEditContentPagesPanel*>() : nullptr;
    }

    static pdf::PDFAppEditContentObjectsModel* model(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentContext* current = context(run);
        return current ? qobject_cast<pdf::PDFAppEditContentObjectsModel*>(current->getTabState(QString::fromLatin1(pdf::PDFAppEditContentTabEntry::TAB_OBJECTS))) : nullptr;
    }

    /// The document of the scenario with the developer switch on (the tool is registered only then, zadani.md 11.2)
    static pdf::PDFAppDocumentTab* openDocument(pdftest::AppToolShellFixture& fixture)
    {
        if (fixture.context->getSettings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), true).isError() ||
            !pdftest::AppToolShellFixture::waitFor([]() { return pdf::PDFAppToolGUIContent::isContentToolRegistered(); }))
        {
            return nullptr;
        }
        return fixture.openDocument(3);
    }

    /// The container of the shown page is loaded and the rows follow it
    static bool waitForRows(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]()
        {
            pdf::PDFAppEditContentContext* current = context(run);
            pdf::PDFAppEditContentObjectsModel* objects = model(run);
            return !current || !objects || (current->getContainer() && objects->getContainer() == current->getContainer());
        });
        Steps::settle();
        return true;
    }

    /// A row of the tree selected by the view (the relay selects the node from the event loop)
    static bool selectRow(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentObjectsPanel* panel = objectsPanel(run);
        pdf::PDFTreeView* tree = panel ? panel->getTree() : nullptr;
        if (!tree || !tree->model() || tree->model()->rowCount() == 0)
        {
            return true;
        }
        const QModelIndex index = tree->model()->index(0, 0);
        tree->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        Steps::settle();
        return true;
    }

    /// The context menu of the selection open, then a change of the language (order (3)): the panels are built again,
    /// the menu closes
    static bool contextMenuOverLanguage(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentContext* current = context(run);
        if (!current)
        {
            return true;
        }
        current->requestContextMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(200, 200));
        Steps::settle();

        ContentPanelsLifetimeTranslator translator;
        QCoreApplication::installTranslator(&translator);
        Steps::settle();
        QCoreApplication::removeTranslator(&translator);
        Steps::settle();
        return run.window != nullptr;
    }

    /// A row of the context menu (Hide of the selection): the action runs from the event loop
    static bool menuRow(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        pdf::PDFAppEditContentMenus* menus = current ? current->getMenus() : nullptr;
        QMenu* menu = menus ? menus->openMenu(QString::fromLatin1(pdf::PDFAppEditContentMenuIds::CONTEXT), QPoint(200, 200)) : nullptr;
        if (!menu)
        {
            return true;
        }
        for (QAction* action : menu->actions())
        {
            if (action->property(pdf::PDFAppEditContentMenus::ACTION_PROPERTY).toString() == QLatin1String(pdf::PDFAppEditContentActionIds::HIDE) && action->isEnabled())
            {
                action->trigger();
                break;
            }
        }
        Steps::settle();
        return true;
    }

    /// The second row dropped in front of the first one (the Reorder comes from the event loop)
    static bool dropRow(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentObjectsModel* objects = model(run);
        if (!objects || objects->rowCount() < 2)
        {
            return true;
        }
        std::unique_ptr<QMimeData> data(objects->mimeData({ objects->index(1, 0) }));
        if (data)
        {
            objects->dropMimeData(data.get(), Qt::MoveAction, 0, 0, QModelIndex());
        }
        Steps::settle();
        waitForRows(run);
        return true;
    }

    /// A filter typed into the field of the header (given to the model, when the typing pauses), then cleared
    static bool typeFilter(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentObjectsPanel* panel = objectsPanel(run);
        if (!panel || !Steps::click(panel->getFilterButton()))
        {
            return true;
        }
        panel = objectsPanel(run);
        if (panel && panel->getFilterField())
        {
            QTest::keyClicks(panel->getFilterField(), QStringLiteral("path"));
        }
        Steps::wait(run, [&run]()
        {
            pdf::PDFAppEditContentObjectsModel* objects = model(run);
            return !objects || objects->isFiltered();
        });
        panel = objectsPanel(run);
        if (panel)
        {
            Steps::click(panel->getFilterButton());
        }
        Steps::settle();
        return true;
    }

    /// A tile of the tab Pages: a request of the page
    static bool clickTile(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentPagesPanel* panel = pagesPanel(run);
        pdf::PDFAppToolPageGrid* grid = panel ? panel->getGrid() : nullptr;
        if (grid)
        {
            Q_EMIT grid->pageClicked(1, Qt::NoModifier);
        }
        Steps::settle();
        return true;
    }

    /// Delete in the tree (DELETE_SELECTION from the event loop)
    static bool deleteKey(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentContext* current = context(run);
        pdf::PDFAppEditContentObjectsModel* objects = model(run);
        if (current && objects && objects->rowCount() > 0)
        {
            current->getSelection()->setNode(objects->getNode(objects->index(0, 0)));
        }
        pdf::PDFAppEditContentObjectsPanel* panel = objectsPanel(run);
        if (panel && panel->getTree())
        {
            Steps::key(panel->getTree(), Qt::Key_Delete);
        }
        Steps::settle();
        waitForRows(run);
        return true;
    }

    /// Order (1): Cancel - the shell ends the parts, then the GUI with its context; then the tool opens again
    static bool cancelAndReopen(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolToolbar* toolbar = Steps::toolbar(run);
        if (!toolbar)
        {
            return true;
        }

        // The work of the steps (a drop, a delete) asks the question of the tool: its action (discard the changes)
        QPointer<QTimer> answer = Steps::answerToolQuestionLater(run, QStringLiteral("toolCloseAction"));
        const bool isClicked = Steps::click(toolbar->getCancelButton());
        Steps::wait(run, [&run]() { return gui(run) == nullptr; });
        Steps::settle();
        delete answer.data();
        if (!isClicked || !run.tab || gui(run))
        {
            return true;
        }
        Steps::openTool(run);
        return true;
    }
};

/// The scenario contentpanels
class ContentPanelsLifetimeScenarios
{
public:
    ContentPanelsLifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = ContentPanelsLifetimeHelper;
        using Steps = ToolPagesLifetimeSteps;

        ToolPagesLifetimeScenario panels;
        panels.name = QStringLiteral("contentpanels");
        panels.toolId = QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID);
        panels.openDocument = &H::openDocument;
        panels.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::waitForRows);
            steps->push_back(&H::selectRow);
            steps->push_back(&H::contextMenuOverLanguage);
            steps->push_back(&H::menuRow);
            steps->push_back(&H::dropRow);
            steps->push_back(&H::typeFilter);
            steps->push_back(&H::clickTile);
            steps->push_back(&H::deleteKey);
            steps->push_back(&H::cancelAndReopen);
            steps->push_back(&H::waitForRows);
        };
        scenarios->push_back(panels);
    }
};

void ToolPagesLifetimePackages::registerContentPanelsScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    ContentPanelsLifetimeScenarios::add(scenarios);
}
