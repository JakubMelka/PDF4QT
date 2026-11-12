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

// The lifetime scenarios of Manage Pages (step 3b of phase 2, package M3; plan 8): managegui (its
// own tab: the reading of the sources, the operations, the panels, the context menu, Details, the
// run and the tab kept after the result), managedocgui (the tab of a document: the operations, the
// run, Keep), managedialogs (the dialogs of 387:15926 and Crop, Rename, Move to, the name of a
// checkpoint), manageclose (the question 405:30894), manageclipboard (Copy, Paste, Cut),
// managecheckpoint (Save checkpoint, Restore) and managedocchange (a change of the document under
// the tool, RB2), managemissing (the dialog of the missing sources of the loader of package M4),
// manageconflict (the questions of the shared file sink 356:11997 during One PDF per group) and
// managelocked (the tab of a document without Assemble and its owner password, 1479:35631) and
// manageempty (Add of an open document in the empty state, 409:32954).

#include "apptoolpageslifetime.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappmanagecanvas.h"
#include "pdfappmanagedialogs.h"
#include "pdfappmanagepanels.h"
#include "pdfapppassworddialog.h"
#include "pdfappmcpcontroller.h"
#include "pdfappmcpdocumentresources.h"
#include "pdfapptoolgui_manage.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolinputrow.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptooloutputfiles.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptoolresultdialog.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"

#include "pdfdocumentassemblyfile.h"
#include "pdfdocumentbuilder.h"
#include "pdfdocumenttool_manage.h"
#include "pdfdocumenttool_split.h"
#include "pdfdocumentwriter.h"
#include "pdfmessagebox.h"
#include "pdfsecurityhandler.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QPushButton>
#include <QTest>
#include <QTimer>

/// Helpers of the scenarios of Manage Pages
class ToolPagesLifetimeManage
{
public:
    ToolPagesLifetimeManage() = delete;

    static QString toolId()
    {
        return QString::fromLatin1(pdf::PDFDocumentToolManage::TOOL_ID);
    }

    static pdf::PDFAppManageToolGUI* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
        return shell ? qobject_cast<pdf::PDFAppManageToolGUI*>(shell->getGUI()) : nullptr;
    }

    /// The own tab with two PDF files and an image; their pages are the assembly
    static bool openTab(ToolPagesLifetimeRun& run)
    {
        if (!run.window || !run.fixture)
        {
            return false;
        }
        QImage image(QSize(40, 30), QImage::Format_RGB32);
        image.fill(QColor(120, 160, 200));
        const QString png = run.fixture->folder() + QStringLiteral("/manage-image.png");
        image.save(png, "PNG");
        pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
        list->addFiles({ pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("manage-a.pdf"), 3),
                         pdftest::AppDocumentSamples::simple(run.fixture->folder(), QStringLiteral("manage-b.pdf"), 2), png });
        run.toolTab = run.window->createToolTab(toolId(), list);
        ToolPagesLifetimeSteps::settle();
        return run.toolTab && ToolPagesLifetimeSteps::wait(run, [&run]()
        {
            const pdf::PDFAppManageToolGUI* current = gui(run);
            return current && current->getAssembly().getItems().size() == 6;
        }) && settleCatalog(run);
    }

    /// The listing of the MCP resources follows the new tab by a timer: it is rebuilt in this
    /// step, not in a later one (a replay finds the emission of the catalog in its step)
    static bool settleCatalog(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppMcpController* controller = run.fixture && run.fixture->context ? run.fixture->context->getMcpController() : nullptr;
        const pdf::PDFAppMcpHost* host = controller ? controller->getHost() : nullptr;
        if (host)
        {
            ToolPagesLifetimeSteps::wait(run, [host]()
            {
                const pdf::PDFAppMcpDocumentWatcher* watcher = pdf::PDFAppMcpDocumentWatcher::find(host);
                return !watcher || !watcher->isRebuildPending();
            });
            ToolPagesLifetimeSteps::settle();
        }
        return true;
    }

    /// The Primary button of the tool runs it (the prepareRun of the scenario of the run; managegui
    /// and managedocgui share the function): the tool open in the tab of a document gets a changed
    /// order, without a tool the own tab opens
    static bool prepareRun(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppManageToolGUI* current = gui(run);
        if (!current)
        {
            return !run.toolTab && openTab(run);
        }
        if (!current->isDocumentHost())
        {
            return true;
        }
        if (!select(run, { 2 }) || !gui(run))
        {
            return false;
        }
        gui(run)->moveSelectionBefore(gui(run)->getAssembly().getItems().front().id);
        ToolPagesLifetimeSteps::settle();
        return gui(run) != nullptr;
    }

    /// The items at the indices are selected
    static bool select(const ToolPagesLifetimeRun& run, std::vector<int> indices)
    {
        pdf::PDFAppManageToolGUI* current = gui(run);
        if (!current)
        {
            return false;
        }
        pdf::PDFDocumentAssembly::Selection selection;
        for (const int index : indices)
        {
            if (size_t(index) >= current->getAssembly().getItems().size())
            {
                return false;
            }
            selection.push_back(current->getAssembly().getItems()[size_t(index)].id);
        }
        current->setSelection(selection);
        ToolPagesLifetimeSteps::settle();
        return true;
    }

    /// A key on the canvas (the actions of the key widget reach it)
    static bool canvasKey(const ToolPagesLifetimeRun& run, int keyCode, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        pdf::PDFAppManageToolGUI* current = gui(run);
        return current && current->getCanvas() && ToolPagesLifetimeSteps::key(current->getCanvas()->getPageView(), keyCode, modifiers);
    }

    /// The open dialog of the GUI closed by Esc
    static bool closeDialog(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppManageToolGUI* current = gui(run);
        pdf::PDFAppManageDialog* dialog = current ? current->getDialog() : nullptr;
        if (!dialog)
        {
            return false;
        }
        ToolPagesLifetimeSteps::key(dialog, Qt::Key_Escape);
        ToolPagesLifetimeSteps::settle();
        return true;
    }

    /// The run, its result dialog and its confirmation; the own tab stays (K31) or the tool of the
    /// tab of a document ends
    static void addRunSteps(std::vector<ToolPagesLifetimeStep>* steps, bool isOwnTab)
    {
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            return ToolPagesLifetimeSteps::confirmToDialog(run) != nullptr;
        });
        steps->push_back([isOwnTab](ToolPagesLifetimeRun& run)
        {
            pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
            pdf::PDFAppToolResultDialog* dialog = shell ? shell->getResultDialog() : nullptr;
            if (!dialog || !ToolPagesLifetimeSteps::confirmDialog(run, dialog))
            {
                return false;
            }
            if (!isOwnTab)
            {
                return true;
            }
            return ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFDocumentToolShell* current = ToolPagesLifetimeSteps::shell(run);
                return current && current->getState() == pdf::PDFDocumentToolShell::State::Editing;
            });
        });
    }

    /// One PDF per group into a folder of the fixture of this run (never a folder of the user, never
    /// shared by the processes of the runner), the names "ask_1" - "ask_6", Existing files Ask; the
    /// files of the first three names exist (the questions of 356:11997)
    static bool setConflictOutput(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppManageToolGUI* current = gui(run);
        if (!current || !run.fixture)
        {
            return false;
        }
        const QString folder = QDir(run.fixture->folder()).filePath(QStringLiteral("manage-output"));
        QDir().mkpath(folder);
        for (int i = 1; i <= 3; ++i)
        {
            QFile file(QDir(folder).filePath(QStringLiteral("ask_%1.pdf").arg(i)));
            if (!file.exists() && file.open(QIODevice::WriteOnly))
            {
                file.write("EXISTING");
            }
        }

        // Each value is one undo step of the model (a slot may change the GUI)
        current->setOutput(QString::fromLatin1(pdf::PDFDocumentToolManage::OUTPUT_PER_GROUP));
        ToolPagesLifetimeSteps::settle();
        const std::pair<const char*, QString> values[] = {
            { pdf::PDFDocumentToolFileOutput::PARAMETER_FOLDER, folder },
            { pdf::PDFDocumentToolFileOutput::PARAMETER_FILE_NAME, QStringLiteral("ask_#") },
            { pdf::PDFDocumentToolFileOutput::PARAMETER_EXISTING_FILES, QStringLiteral("ask") },
        };
        for (const auto& [name, value] : values)
        {
            pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
            QPointer<pdf::PDFAppToolParameterModel> model = shell ? shell->getModel() : nullptr;
            if (!model)
            {
                return false;
            }
            model->setValue(QString::fromLatin1(name), value);
            ToolPagesLifetimeSteps::settle();
        }
        current = gui(run);
        return current && current->getOutput() == QLatin1String(pdf::PDFDocumentToolManage::OUTPUT_PER_GROUP);
    }

    /// The scenario of the tab of a document opens the tool there
    static bool openDocumentTool(ToolPagesLifetimeRun& run)
    {
        return ToolPagesLifetimeSteps::openTool(run) && gui(run) && gui(run)->isDocumentHost();
    }
};

void ToolPagesLifetimePackages::registerManageScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    using M = ToolPagesLifetimeManage;

    // managegui (387:12569): the sources are read, Group, Rotate right (F4), the panel of the
    // assembly, the context menu, Details, the search; Create PDF, Open in a new tab, the tab stays
    ToolPagesLifetimeScenario gui;
    gui.name = QStringLiteral("managegui");
    gui.toolId = M::toolId();
    gui.isOwnTab = true;
    gui.hasResultDialog = true;
    gui.hasPanel = true;
    gui.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::openTab(run); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!M::select(run, { 0, 1, 2 }) || !M::gui(run))
            {
                return false;
            }
            M::gui(run)->group();
            ToolPagesLifetimeSteps::settle();
            return M::select(run, { 1 }) && M::canvasKey(run, Qt::Key_F4);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            QToolButton* button = ToolPagesLifetimeSteps::firstPanelButton(run);
            if (!ToolPagesLifetimeSteps::click(button))
            {
                return false;
            }
            ToolPagesLifetimeSteps::settle();
            button = ToolPagesLifetimeSteps::firstPanelButton(run);
            return !button || ToolPagesLifetimeSteps::key(button->window(), Qt::Key_Escape);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            pdf::PDFAppManageCanvas* canvas = current ? current->getCanvas() : nullptr;
            if (!canvas)
            {
                return false;
            }
            Q_EMIT canvas->getPageView()->customContextMenuRequested(QPoint(30, 30));
            ToolPagesLifetimeSteps::settle();
            current = M::gui(run);
            if (current && current->getContextMenu())
            {
                current->getContextMenu()->close();
            }
            ToolPagesLifetimeSteps::settle();
            current = M::gui(run);
            if (!current || !current->getCanvas())
            {
                return false;
            }
            current->getCanvas()->setDetailsShown(true);
            current->getCanvas()->setSearchText(QStringLiteral("image"));
            ToolPagesLifetimeSteps::settle();
            current = M::gui(run);
            if (!current || !current->getCanvas())
            {
                return false;
            }
            current->getCanvas()->setSearchText(QString());
            current->getCanvas()->setDetailsShown(false);
            ToolPagesLifetimeSteps::settle();
            return true;
        });
        M::addRunSteps(steps, true);
    };
    gui.prepareRun = &M::prepareRun;
    scenarios->push_back(std::move(gui));

    // managedocgui (1479:34632): the tool in the tab of the document - Move, Remove, Restore all,
    // the panel Add; Rearrange Pages, Keep
    ToolPagesLifetimeScenario docGui;
    docGui.name = QStringLiteral("managedocgui");
    docGui.toolId = M::toolId();
    docGui.hasResultDialog = true;
    docGui.hasPanel = true;
    docGui.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::openDocumentTool(run); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!M::select(run, { 2 }) || !M::gui(run))
            {
                return false;
            }
            M::gui(run)->moveSelectionBefore(M::gui(run)->getAssembly().getItems().front().id);
            ToolPagesLifetimeSteps::settle();
            return M::select(run, { 1 }) && M::canvasKey(run, Qt::Key_Delete);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!M::gui(run))
            {
                return false;
            }
            M::gui(run)->restoreAll();
            ToolPagesLifetimeSteps::settle();
            QToolButton* button = ToolPagesLifetimeSteps::firstPanelButton(run);
            if (!ToolPagesLifetimeSteps::click(button))
            {
                return false;
            }
            ToolPagesLifetimeSteps::settle();
            button = ToolPagesLifetimeSteps::firstPanelButton(run);
            return !button || ToolPagesLifetimeSteps::key(button->window(), Qt::Key_Escape);
        });
        M::addRunSteps(steps, false);
    };
    docGui.prepareRun = &M::prepareRun;
    scenarios->push_back(std::move(docGui));

    // managedialogs (387:15926 and the dialogs without a frame): each opened and closed by Esc,
    // the last one accepted by Enter
    ToolPagesLifetimeScenario dialogs;
    dialogs.name = QStringLiteral("managedialogs");
    dialogs.toolId = M::toolId();
    dialogs.isOwnTab = true;
    dialogs.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::openTab(run) && M::select(run, { 0 }); });
        using Opener = void (pdf::PDFAppManageToolGUI::*)();
        const Opener openers[] = { &pdf::PDFAppManageToolGUI::openRangeDialog, &pdf::PDFAppManageToolGUI::openEveryDialog, &pdf::PDFAppManageToolGUI::openNumbersDialog,
                                   &pdf::PDFAppManageToolGUI::openPropertiesDialog, &pdf::PDFAppManageToolGUI::openCropDialog, &pdf::PDFAppManageToolGUI::openRenameDialog,
                                   &pdf::PDFAppManageToolGUI::openMoveDialog };
        for (const Opener opener : openers)
        {
            steps->push_back([opener](ToolPagesLifetimeRun& run)
            {
                pdf::PDFAppManageToolGUI* current = M::gui(run);
                if (!current)
                {
                    return false;
                }
                (current->*opener)();
                ToolPagesLifetimeSteps::settle();
                return M::closeDialog(run);
            });
        }
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            if (!current)
            {
                return false;
            }
            current->openCheckpointDialog();
            ToolPagesLifetimeSteps::settle();
            current = M::gui(run);
            pdf::PDFAppManageDialog* dialog = current ? current->getDialog() : nullptr;
            if (!dialog || !dialog->getAcceptButton())
            {
                return false;
            }
            ToolPagesLifetimeSteps::click(dialog->getAcceptButton());
            ToolPagesLifetimeSteps::settle();
            return true;
        });
    };
    scenarios->push_back(std::move(dialogs));

    // manageclose (405:30894): the changed assembly asks at the close of its tab; Cancel keeps it,
    // Don't Save closes it
    ToolPagesLifetimeScenario close;
    close.name = QStringLiteral("manageclose");
    close.toolId = M::toolId();
    close.isOwnTab = true;
    close.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::openTab(run); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!run.window || !run.toolTab)
            {
                return false;
            }
            ToolPagesLifetimeSteps::answerLater(run, QStringLiteral("cancelButton"));
            run.window->closeTab(run.toolTab);
            ToolPagesLifetimeSteps::settle();
            return run.window && run.toolTab;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // 409:32023: another assembly asks the same question first; Cancel keeps this one
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            if (!current || !run.fixture)
            {
                return false;
            }
            ToolPagesLifetimeSteps::answerLater(run, QStringLiteral("cancelButton"));
            const bool isLoading = current->openAssemblyFile(run.fixture->folder() + QStringLiteral("/manage-other.pdf4qtassembly"));
            ToolPagesLifetimeSteps::settle();
            return !isLoading && run.window && run.toolTab;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Don't Save closes the tab: it deletes the parts of the tool in its destructor
            // (PDFAppToolModeLayout::partDestroyed from a dying child of the window - a deferred
            // site of the harness)
            if (!run.window || !run.toolTab)
            {
                return false;
            }
            ToolPagesLifetimeSteps::answerLater(run, QStringLiteral("closeTabButton"));
            run.window->closeTab(run.toolTab);
            ToolPagesLifetimeSteps::settle();
            return run.toolTab.isNull();
        });
    };
    scenarios->push_back(std::move(close));

    // manageclipboard (4.6): Copy, Paste after the selection, Cut, Paste
    ToolPagesLifetimeScenario clipboard;
    clipboard.name = QStringLiteral("manageclipboard");
    clipboard.toolId = M::toolId();
    clipboard.isOwnTab = true;
    clipboard.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::openTab(run) && M::select(run, { 0 }); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            return M::canvasKey(run, Qt::Key_C, Qt::ControlModifier) && M::select(run, { 2 }) && M::canvasKey(run, Qt::Key_V, Qt::ControlModifier);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            return M::select(run, { 1 }) && M::canvasKey(run, Qt::Key_X, Qt::ControlModifier) && M::select(run, { 0 }) && M::canvasKey(run, Qt::Key_V, Qt::ControlModifier);
        });
    };
    scenarios->push_back(std::move(clipboard));

    // managecheckpoint (B22, B23): a checkpoint, a removal, the restoration (an undo step)
    ToolPagesLifetimeScenario checkpoint;
    checkpoint.name = QStringLiteral("managecheckpoint");
    checkpoint.toolId = M::toolId();
    checkpoint.isOwnTab = true;
    checkpoint.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::openTab(run); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            if (!current)
            {
                return false;
            }
            current->saveCheckpoint(QStringLiteral("All files added"));
            ToolPagesLifetimeSteps::settle();
            return M::select(run, { 0 }) && M::canvasKey(run, Qt::Key_Delete);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            if (!current || current->getCheckpoints().empty())
            {
                return false;
            }
            current->restoreCheckpoint(current->getCheckpoints().front().id);
            ToolPagesLifetimeSteps::settle();
            return true;
        });
    };
    scenarios->push_back(std::move(checkpoint));

    // managedocchange (RB2): an assistant adds a page to the document below the open tool; the
    // assembly and its undo steps follow the page objects
    ToolPagesLifetimeScenario docChange;
    docChange.name = QStringLiteral("managedocchange");
    docChange.toolId = M::toolId();
    docChange.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!M::openDocumentTool(run) || !M::select(run, { 2 }))
            {
                return false;
            }
            M::gui(run)->moveSelectionBefore(M::gui(run)->getAssembly().getItems().front().id);
            ToolPagesLifetimeSteps::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppDocument* document = run.tab ? run.tab->getDocument() : nullptr;
            if (!document || !document->getDocument())
            {
                return false;
            }
            pdf::PDFDocumentBuilder builder(document->getDocument().data());
            builder.appendPage(QRectF(0, 0, 200, 200));
            const pdf::PDFDocumentPointer changed(new pdf::PDFDocument(builder.build()));
            document->commit(pdf::PDFModifiedDocument(changed, nullptr, pdf::PDFModifiedDocument::Reset), QStringLiteral("Assistant"), document->getVersion(), pdf::PDFAppChangeSource::Mcp);
            ToolPagesLifetimeSteps::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            if (!current || !current->getModel())
            {
                return false;
            }
            current->getModel()->undo();
            ToolPagesLifetimeSteps::settle();
            return true;
        });
    };
    scenarios->push_back(std::move(docChange));

    // managemissing (387:16980, RS3BAPP): another assembly, whose source is missing, asks to save
    // this one (Don't Save), then its sources dialog; Locate… finds the file (the job of the
    // loader), Open Assembly replaces the assembly as an undo step; Undo brings back the first
    // assembly with its own file state
    ToolPagesLifetimeScenario missing;
    missing.name = QStringLiteral("managemissing");
    missing.toolId = M::toolId();
    missing.isOwnTab = true;
    missing.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::openTab(run); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            if (!current || !run.fixture)
            {
                return false;
            }
            pdf::PDFDocumentAssembly::Source source;
            source.id = QStringLiteral("s1");
            source.name = QStringLiteral("manage-missing.pdf");
            source.path = run.fixture->folder() + QStringLiteral("/manage-missing.pdf");
            source.pageCount = 3;
            pdf::PDFDocumentAssembly::Item item;
            item.id = QStringLiteral("i1");
            pdf::PDFDocumentAssembly::Page page;
            page.plan.input = 0;
            page.plan.pageIndex = 2;
            item.pages.push_back(page);
            QString error;
            pdf::PDFDocumentAssemblyFile::Content content;
            content.assembly = pdf::PDFDocumentAssembly::create({ source }, { item }, { }, &error);
            const QString assemblyPath = run.fixture->folder() + QStringLiteral("/manage-missing.pdf4qtassembly");
            QFile file(assemblyPath);
            const QByteArray data = pdf::PDFDocumentAssemblyFile::write(content, run.fixture->folder());
            if (!error.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
            {
                return false;
            }
            file.close();
            ToolPagesLifetimeSteps::answerLater(run, QStringLiteral("closeTabButton"));
            if (!current->openAssemblyFile(assemblyPath))
            {
                return false;
            }
            ToolPagesLifetimeSteps::settle();
            return ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFAppManageToolGUI* gui = M::gui(run);
                return !gui || gui->getSourcesDialog();
            }) && M::gui(run);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            // Locate… after its file dialog: the check of the chosen file in the job of the loader
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            pdf::PDFAppManageSourcesDialog* dialog = current ? current->getSourcesDialog() : nullptr;
            pdf::PDFAppAssemblyLoader* loader = current ? current->findChild<pdf::PDFAppAssemblyLoader*>() : nullptr;
            if (!dialog || !loader || !run.fixture || !loader->locate(dialog->getResult(), QStringLiteral("s1"), run.fixture->folder() + QStringLiteral("/manage-a.pdf")))
            {
                return false;
            }
            return ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFAppManageToolGUI* gui = M::gui(run);
                const pdf::PDFAppManageSourcesDialog* sourcesDialog = gui ? gui->getSourcesDialog() : nullptr;
                return !sourcesDialog || sourcesDialog->getOpenButton()->isEnabled();
            });
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            pdf::PDFAppManageSourcesDialog* dialog = current ? current->getSourcesDialog() : nullptr;
            if (!dialog)
            {
                return false;
            }
            ToolPagesLifetimeSteps::click(dialog->getOpenButton());
            ToolPagesLifetimeSteps::settle();
            return ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFAppManageToolGUI* gui = M::gui(run);
                return !gui || gui->getAssembly().getItems().size() == 1;
            });
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            if (!current || !current->getModel())
            {
                return false;
            }
            current->getModel()->undo();
            ToolPagesLifetimeSteps::settle();
            return true;
        });
    };
    scenarios->push_back(std::move(missing));

    // manageconflict (356:11997 with the texts of Manage, plan 2 "the conflict of a file during the
    // writing", RS3BAPP): One PDF per group (six items, six files "ask_1" - "ask_6") into a folder,
    // where the first three exist, Existing files Ask; the run asks three times - Rename, Overwrite,
    // Skip, each question a nested dialog of the run - then the dialog of the written files (R3
    // 396:27563) is closed and the own tab stays (K31). The victims are deleted also while a
    // question waits (the close of the tab = Stop Export, R3).
    ToolPagesLifetimeScenario conflict;
    conflict.name = QStringLiteral("manageconflict");
    conflict.toolId = M::toolId();
    conflict.isOwnTab = true;
    conflict.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::openTab(run); });
        steps->push_back([](ToolPagesLifetimeRun& run) { return M::setConflictOutput(run); });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!ToolPagesLifetimeSteps::click(ToolPagesLifetimeSteps::toolbar(run) ? ToolPagesLifetimeSteps::toolbar(run)->getConfirmButton() : nullptr))
            {
                return false;
            }
            int questions = 0;
            for (const char* answer : { "rename", "overwrite", "skip" })
            {
                QPointer<pdf::PDFAppToolFileConflictDialog> dialog;
                ToolPagesLifetimeSteps::wait(run, [&run, &dialog]()
                {
                    dialog = qobject_cast<pdf::PDFAppToolFileConflictDialog*>(QApplication::activeModalWidget());
                    const pdf::PDFDocumentToolShell* current = ToolPagesLifetimeSteps::shell(run);
                    return dialog || !current || current->getResultDialog();
                });
                if (!dialog)
                {
                    break;
                }
                ++questions;
                const QByteArray name(answer);
                ToolPagesLifetimeSteps::click(name == "rename" ? dialog->getRenameButton() : (name == "overwrite" ? dialog->getOverwriteButton() : dialog->getSkipButton()));
            }
            ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFDocumentToolShell* current = ToolPagesLifetimeSteps::shell(run);
                return !current || current->getResultDialog();
            });
            pdf::PDFDocumentToolShell* shell = ToolPagesLifetimeSteps::shell(run);
            pdf::PDFAppToolResultDialog* dialog = shell ? shell->getResultDialog() : nullptr;
            if (!dialog || questions != 3 || !ToolPagesLifetimeSteps::click(dialog->getCloseButton()))
            {
                return false;
            }
            return ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFDocumentToolShell* current = ToolPagesLifetimeSteps::shell(run);
                return current && current->getState() == pdf::PDFDocumentToolShell::State::Editing && !current->getResultDialog();
            });
        });
    };
    scenarios->push_back(std::move(conflict));

    // managelocked (owner answer 1 of 12. 11., 1479:35631; P2S3BFIXGUI): the tab of a document
    // without Assemble - the tool opens with Rearrange Pages Disabled and the red callout; Enter
    // owner password… opens the shared owner password dialog, the password unlocks the document
    // and the tool goes on enabled
    ToolPagesLifetimeScenario locked;
    locked.name = QStringLiteral("managelocked");
    locked.toolId = M::toolId();
    locked.openDocument = [](pdftest::AppToolShellFixture& fixture) -> pdf::PDFAppDocumentTab*
    {
        pdf::PDFDocumentBuilder builder;
        for (int i = 0; i < 3; ++i)
        {
            builder.appendPage(QRectF(0, 0, 300, 400));
        }
        pdf::PDFSecurityHandlerFactory::SecuritySettings settings;
        settings.algorithm = pdf::PDFSecurityHandlerFactory::AES_256;
        settings.ownerPassword = pdftest::AppDocumentSamples::getOwnerPassword();
        settings.permissions = 0xFFFFFFFCu & ~uint32_t(pdf::PDFSecurityHandler::Permission::Assemble);
        builder.setSecurityHandler(pdf::PDFSecurityHandlerFactory::createSecurityHandler(settings));
        pdf::PDFDocument document = builder.build();
        const QString path = fixture.folder() + QStringLiteral("/manage-no-assemble.pdf");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !bool(pdf::PDFDocumentWriter(nullptr).write(&file, &document)))
        {
            return nullptr;
        }
        file.close();
        return fixture.open(path);
    };
    locked.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            return M::openDocumentTool(run) && !M::gui(run)->isConfirmEnabled();
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            pdf::PDFAppManageResultPanel* panel = current ? current->getResultPanel() : nullptr;
            if (!panel || !ToolPagesLifetimeSteps::click(panel->getCalloutLink()))
            {
                return false;
            }
            ToolPagesLifetimeSteps::settle();
            return true;
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppPasswordDialog* dialog = nullptr;
            for (pdf::PDFAppPasswordDialog* candidate : run.window ? run.window->findChildren<pdf::PDFAppPasswordDialog*>() : QList<pdf::PDFAppPasswordDialog*>())
            {
                dialog = candidate->isVisible() ? candidate : dialog;
            }
            if (!dialog)
            {
                return false;
            }
            // Typed: the dialog enables Unlock by the edit of its field
            QTest::keyClicks(dialog->getPasswordField(), pdftest::AppDocumentSamples::getOwnerPassword());
            ToolPagesLifetimeSteps::click(dialog->getUnlockButton());
            ToolPagesLifetimeSteps::settle();
            return ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFAppManageToolGUI* gui = M::gui(run);
                return !gui || gui->isConfirmEnabled();
            });
        });
    };
    scenarios->push_back(std::move(locked));

    // manageempty (409:32739 / 409:32954, fix wave 3b F4): the own tab without files shows the
    // open document of the window in the shared row of "Open documents"; its Add makes it a source
    // (the empty state is made again and its old row goes while it emits)
    ToolPagesLifetimeScenario empty;
    empty.name = QStringLiteral("manageempty");
    empty.toolId = M::toolId();
    empty.isOwnTab = true;
    empty.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
    {
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            if (!run.window)
            {
                return false;
            }
            run.toolTab = run.window->createToolTab(M::toolId(), new pdf::PDFAppToolInputList(nullptr));
            ToolPagesLifetimeSteps::settle();
            return run.toolTab && ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFAppManageToolGUI* current = M::gui(run);
                const QWidget* state = current && current->getCanvas() ? current->getCanvas()->getEmptyState() : nullptr;
                return state && state->findChild<pdf::PDFAppToolOpenDocumentRow*>();
            }) && M::settleCatalog(run);
        });
        steps->push_back([](ToolPagesLifetimeRun& run)
        {
            pdf::PDFAppManageToolGUI* current = M::gui(run);
            QWidget* state = current && current->getCanvas() ? current->getCanvas()->getEmptyState() : nullptr;
            pdf::PDFAppToolOpenDocumentRow* row = state ? state->findChild<pdf::PDFAppToolOpenDocumentRow*>() : nullptr;
            if (!row || !ToolPagesLifetimeSteps::click(row->getAddButton()))
            {
                return false;
            }
            ToolPagesLifetimeSteps::settle();
            return ToolPagesLifetimeSteps::wait(run, [&run]()
            {
                const pdf::PDFAppManageToolGUI* gui = M::gui(run);
                return !gui || !gui->getAssembly().isEmpty();
            });
        });
    };
    scenarios->push_back(std::move(empty));
}
