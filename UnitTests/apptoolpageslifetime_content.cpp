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

// The lifetime scenario of Edit Content (stage E1, package P14; zadani.md 11.3): the tool over its parts. The
// skeleton opens it over the stubs and covers the three orders of destruction of zadani_gui.md 4.14 - (1) the shell
// ends the parts, then the GUI: the step Cancel and the victim "shell" at every site; (2) the view of the document
// dies first: the victims "tab" and "window" at every site (the view is a child of the tab created before the page
// of the tool, so ~QWidget of the tab deletes it with the parts in it first, the page with the shell and the GUI
// after it; a step cannot close the tab - the sweep needs the tab after every step); (3) a change of the language
// builds the actions and the panels again while the tool is open: the step changeLanguage.
// After the review R3 (R3-12): the scenario contentparts - order (2) as a step, the canvas page dies alone and the GUI
// still receives work (a container built in a worker, the refresh of the actions); the scenario contenthooks - the
// new paths of the shell over a test GUI with the hooks (a routed command of the window, the save question with Cancel,
// Save Without the Changes and Apply and Save with the save of the kept result).
// P14 adds two scenarios over the real GUI: contentflows (a notice shown during a change of the language, the errors of
// writing with Back to Editing and Skip Page, Clear Page, Undo and a page by the window, the save question with Cancel
// and Save Without the Changes, Apply Changes with Keep in This Tab) and contentsave (Apply and Save).
// The packages add their steps here (P14) and in apptoolpageslifetime_content_canvas.cpp (P11), _content_panels.cpp
// (P12), _content_cards.cpp (P13). Every step goes through the guards of the test: the victim may be deleted inside
// of any of its events; a step, whose part is gone, ends without a failure.

#include "apptoolpageslifetime.h"

#include "pdfappcommand.h"
#include "pdfappdocument.h"
#include "pdfappdocumenttab.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappmainwindow.h"
#include "pdfapptoolgui_content.h"
#include "pdfapptoolparametermodel.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"
#include "pdfmessagebox.h"
#include "pdfsettingdefinitions.h"

#include "pdfdocumenttool_content.h"
#include "pdfappeditcontentnotices.h"
#include "pdfappeditcontentregistry.h"

#include <QAbstractButton>
#include <QAction>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QPointer>
#include <QPushButton>
#include <QTranslator>

/// A translator, which is never empty: its installation and its removal send the change of the language
class ContentLifetimeTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }
};

/// The GUI of the scenario contenthooks (R3-12): the test GUI with the hooks of a tool with its own model, as Edit
/// Content uses them - a valid save question, a command of the application it takes (Zoom In; its handling says
/// that the states of the commands changed), work of the user on request
class ContentLifetimeHooksGUI : public pdftest::ShellTestGUI
{
public:
    ContentLifetimeHooksGUI() : pdftest::ShellTestGUI(false) { }

    virtual pdf::PDFAppToolCommandState getDocumentCommandState(const QString& commandId) const override
    {
        return commandId == pdf::PDFAppCommands::zoomIn() ? pdf::PDFAppToolCommandState::Enabled : pdf::PDFAppToolCommandState::NotHandled;
    }

    virtual bool handleDocumentCommand(const QString& commandId) override
    {
        Q_UNUSED(commandId);
        // The last call (the shell emits the change of the states into the tab: its slots may delete everything)
        updateDocumentCommands();
        return true;
    }

    virtual pdf::PDFAppToolSaveQuestion getSaveQuestion() const override
    {
        pdf::PDFAppToolSaveQuestion question;
        question.title = QStringLiteral("Save without the changes to the page content?");
        question.text = QStringLiteral("The edits made in Edit Content are not in the document yet.");
        question.saveText = QStringLiteral("Save Without the Changes");
        question.applyText = QStringLiteral("Apply and Save");
        return question;
    }

    virtual bool hasUnsavedWork() const override { return m_hasWork; }

    /// The work comes (the Primary button and the save question follow it: the shell refreshes the actions)
    void addWork()
    {
        m_hasWork = true;
        updateConfirmState();
    }

private:
    bool m_hasWork = false;
};

class ContentLifetimeHooksGUIFactory : public pdf::PDFDocumentToolGUIFactory
{
public:
    virtual pdf::PDFDocumentToolGUI* createGUI() const override { return new ContentLifetimeHooksGUI(); }
};

/// The steps of the scenario of Edit Content
class ContentLifetimeHelper
{
public:
    ContentLifetimeHelper() = delete;

    using Steps = ToolPagesLifetimeSteps;
    using GUI = pdf::PDFAppToolGUIContent;

    static GUI* gui(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? qobject_cast<GUI*>(shell->getGUI()) : nullptr;
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

    /// The container of the shown page is built by the core (the stubs ignore it)
    static bool waitForContainer(ToolPagesLifetimeRun& run)
    {
        Steps::wait(run, [&run]()
        {
            GUI* current = gui(run);
            return !current || !current->getEditContext() || current->getEditContext()->getContainer();
        });
        Steps::settle();
        return true;
    }

    /// Order (3): a change of the language while the tool is open - the shell builds the actions and the panels again
    static bool changeLanguage(ToolPagesLifetimeRun& run)
    {
        ContentLifetimeTranslator translator;
        QCoreApplication::installTranslator(&translator);
        Steps::settle();
        QCoreApplication::removeTranslator(&translator);
        Steps::settle();
        return run.window != nullptr;
    }

    /// Order (1): Cancel - the shell ends the parts, then the GUI with its context; then the tool opens again
    static bool cancelAndReopen(ToolPagesLifetimeRun& run)
    {
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

    /// Order (2) of zadani_gui.md 4.14 (R3-12): the view of the document dies first - its parts (here the canvas page
    /// of the tool, deleted at once as ~QWidget of the view deletes it) are gone while the GUI and its context live and
    /// still receive work: the container of another page built in a worker (containerLoaded comes from the event loop
    /// after the parts are gone), the refresh of the actions of the window (updateDocumentCommands)
    static bool partsDieFirst(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        GUI* current = gui(run);
        pdf::PDFAppEditContentContext* context = current ? current->getEditContext() : nullptr;
        if (!shell || !context)
        {
            return true;
        }

        QPointer<pdf::PDFAppEditContentContext> guard(context);
        context->setPage(context->getPageIndex() == 0 ? 1 : 0);
        if (!guard || !gui(run))
        {
            return true;
        }
        gui(run)->updateDocumentCommands();
        if (!guard || !Steps::shell(run))
        {
            return true;
        }

        QWidget* canvasPage = Steps::shell(run)->getCanvasPage();
        if (!canvasPage)
        {
            return false;
        }
        {
            // The deliberate synchronous deletion of order (2): ~QWidget of the view deletes its children at once (the
            // view of the tool is a retirable of the package P11)
            pdf::PDFDeferredDeletion::SynchronousDeletionScope scope;
            delete canvasPage;
        }
        Steps::settle();
        waitForContainer(run);

        // The discovery run checks, that the GUI outlived its canvas and got the container of the other page
        GUI* after = gui(run);
        return after && !after->getCanvas() && after->getEditContext() && after->getEditContext()->getContainer();
    }

    // ---- the scenario contentflows (P14): the flows of the real GUI of the tool

    /// The first object of the page shown (a text object or a path of the sample page), not valid = none
    static pdf::PDFContentNodeReference firstObject(const pdf::PDFAppEditContentContext* context)
    {
        pdf::PDFContentNodeReference reference;
        const pdf::PDFContentContainerPointer container = context ? context->getContainer() : nullptr;
        if (!container || !container->getRoot())
        {
            return reference;
        }
        // The sample page draws its text inside of groups (q ... Q of the painter): the first object in the order of
        // the content, a walk of the tree of the test document
        std::vector<const pdf::PDFContentNode*> stack = { container->getRoot().get() };
        while (!stack.empty())
        {
            const pdf::PDFContentNode* current = stack.back();
            stack.pop_back();
            if (current->getKind() == pdf::PDFContentNodeKind::TextObject || current->getKind() == pdf::PDFContentNodeKind::Path)
            {
                reference.container = container->getKey();
                reference.node = current->getId();
                break;
            }
            const std::vector<pdf::PDFContentNodePointer>& children = current->getChildren();
            for (auto it = children.crbegin(); it != children.crend(); ++it)
            {
                stack.push_back(it->get());
            }
        }
        return reference;
    }

    /// zadani_gui.md 4.21 (integration row 26): a new version of the document of the tab under the tool without work -
    /// the GUI loads the session again (PDFAppEditContentContext::reloadDocument): every part connects to the new
    /// session (sessionReplaced), the page is shown again; the old session goes from the event loop with its builds
    static bool reloadDocument(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        pdf::PDFAppDocument* document = run.tab ? run.tab->getDocument() : nullptr;
        if (!current || !current->getEditContext() || !document || !document->getDocument() || current->hasUnsavedWork())
        {
            return true;
        }

        const pdf::PDFDocumentPointer newDocument(new pdf::PDFDocument(*document->getDocument()));
        document->restore(newDocument, pdf::PDFModifiedDocument::Reset);
        Steps::settle();
        waitForContainer(run);

        // The discovery run checks, that the session follows the new version
        current = gui(run);
        return current && current->getEditContext() && current->getEditContext()->getDocumentPointer() == newDocument;
    }

    /// Work of the user: the first object of the page is deleted (one undo step; Apply Changes, the save question)
    static bool addContentWork(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        pdf::PDFAppEditContentContext* context = current ? current->getEditContext() : nullptr;
        const pdf::PDFContentNodeReference node = firstObject(context);
        if (!context || !node.isValid())
        {
            return true;
        }
        context->execute(pdf::PDFContentCommandFactory::createDelete({ node }));
        Steps::settle();

        // The discovery run checks, that the step did what it says
        return gui(run) && gui(run)->hasUnsavedWork();
    }

    /// Preview by its button of the toolbar (\p isOn): the notice of the state Preview comes and goes
    static bool clickPreview(ToolPagesLifetimeRun& run, bool isOn)
    {
        GUI* current = gui(run);
        QAbstractButton* preview = current ? current->getActionButton(QString::fromLatin1(pdf::PDFAppEditContentActionIds::PREVIEW)) : nullptr;
        if (!preview)
        {
            return true;
        }
        Steps::click(preview);
        Steps::settle();
        current = gui(run);
        return current && current->getEditContext() && current->getEditContext()->isPreviewActive() == isOn && current->getNotices()->getCurrent().has_value() == isOn;
    }

    static bool previewOn(ToolPagesLifetimeRun& run)
    {
        return clickPreview(run, true);
    }

    static bool previewOff(ToolPagesLifetimeRun& run)
    {
        return clickPreview(run, false);
    }

    /// The errors of writing of a failed run (RE-19): the dialog -apply-error of the GUI, answered by \p buttonName
    static bool answerWriteError(ToolPagesLifetimeRun& run, const QString& buttonName)
    {
        GUI* current = gui(run);
        if (!current || !current->getEditContext())
        {
            return true;
        }

        QJsonArray errors;
        errors.append(QJsonObject({ { QStringLiteral("text"), QStringLiteral("The page cannot be written.") } }));
        QJsonArray pages;
        pages.append(QJsonObject({ { QStringLiteral("page"), qint64(current->getEditContext()->getPageIndex() + 1) }, { QStringLiteral("errors"), errors } }));
        pdf::PDFDocumentToolResult result = pdf::PDFDocumentToolResult::createFailed(QStringLiteral("1 page cannot be written."));
        result.data.insert(QString::fromLatin1(pdf::PDFDocumentToolContent::DATA_PAGE_ERRORS), pages);
        current->onRunNotAccepted(result);
        if (!gui(run))
        {
            return true;
        }

        pdf::PDFMessageBox* dialog = gui(run)->getApplyErrorDialog();
        const bool isAnswered = dialog && Steps::click(dialog->findChild<QPushButton*>(buttonName));
        Steps::settle();
        return isAnswered;
    }

    static bool writeErrorBack(ToolPagesLifetimeRun& run)
    {
        return answerWriteError(run, QStringLiteral("editContentApplyErrorBack"));
    }

    /// Skip Page of the page of the work: the run again (queued) writes nothing - the tool stays with its work
    static bool writeErrorSkip(ToolPagesLifetimeRun& run)
    {
        if (!answerWriteError(run, QStringLiteral("editContentApplyErrorSkip")))
        {
            return false;
        }
        Steps::wait(run, [&run]()
        {
            pdf::PDFDocumentToolShell* current = Steps::shell(run);
            return !current || current->getState() == pdf::PDFDocumentToolShell::State::Editing;
        });
        Steps::settle();
        return Steps::shell(run) && Steps::shell(run)->getState() == pdf::PDFDocumentToolShell::State::Editing;
    }

    /// Clear Page: the question of the GUI, answered by Clear Page
    static bool clearPage(ToolPagesLifetimeRun& run)
    {
        GUI* current = gui(run);
        pdf::PDFMessageBox* question = current ? current->openClearPageQuestion() : nullptr;
        if (!question)
        {
            return true;
        }
        Steps::click(question->findChild<QPushButton*>(QStringLiteral("editContentClearConfirm")));
        Steps::settle();
        return true;
    }

    /// Undo of the tool through the window (the own undo target of the GUI)
    static bool undoByWindow(ToolPagesLifetimeRun& run)
    {
        if (run.tab)
        {
            run.tab->executeCommand(pdf::PDFAppCommands::undo());
        }
        Steps::settle();
        return true;
    }

    /// The page of the tool by the command of the application (the rules of a switch of the page, from the event loop)
    static bool nextPage(ToolPagesLifetimeRun& run)
    {
        if (run.tab)
        {
            run.tab->executeCommand(pdf::PDFAppCommands::nextPage());
        }
        Steps::settle();
        return true;
    }

    /// Apply Changes by the toolbar, then Keep in This Tab: the tool closes with the result
    static bool applyAndKeep(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppToolResultDialog* dialog = Steps::confirmToDialog(run);
        return dialog && Steps::confirmDialog(run, dialog);
    }

    /// Save of the user with the work of the real GUI: the question of the tool, answered by Apply and Save, then Keep
    /// in This Tab - the tool closes and the page saves the kept result
    static bool saveQuestionApplyContent(ToolPagesLifetimeRun& run)
    {
        if (!answerSaveQuestion(run, QStringLiteral("toolSaveApply")))
        {
            return false;
        }
        QPointer<pdf::PDFDocumentToolShell> guard(Steps::shell(run));
        Steps::wait(run, [&guard]() { return !guard || guard->getResultDialog() != nullptr; });
        if (!guard || !guard->getResultDialog() || !Steps::confirmDialog(run, guard->getResultDialog()))
        {
            return false;
        }
        Steps::wait(run, [&run]()
        {
            const pdf::PDFAppDocument* document = run.tab ? run.tab->getDocument() : nullptr;
            return !document || !document->isModified();
        });
        Steps::settle();
        const pdf::PDFAppDocument* document = run.tab ? run.tab->getDocument() : nullptr;
        return document && !document->isModified();
    }

    // ---- the scenario contenthooks: the hooks of a tool with its own model in the shell (R3-12)

    /// The test tool with the GUI of the hooks under the id of the scenario, then the document
    static pdf::PDFAppDocumentTab* openHooksDocument(pdftest::AppToolShellFixture& fixture)
    {
        const QString toolId = QStringLiteral("pages.rotate");
        if (!fixture.registerShellTool(toolId))
        {
            return nullptr;
        }
        pdf::PDFDocumentToolGUIRegistry::unregisterFactory(toolId);
        if (!pdf::PDFDocumentToolGUIRegistry::registerFactory(toolId, std::make_shared<ContentLifetimeHooksGUIFactory>()))
        {
            return nullptr;
        }
        return fixture.openDocument(3);
    }

    static ContentLifetimeHooksGUI* hooksGUI(const ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        return shell ? dynamic_cast<ContentLifetimeHooksGUI*>(shell->getGUI()) : nullptr;
    }

    /// The work comes: the save question exists, the shell refreshes the actions of the window; a run of the tool
    /// changes the title of the document
    static bool addWork(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        ContentLifetimeHooksGUI* current = hooksGUI(run);
        if (!shell || !current)
        {
            return true;
        }
        shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("title"));
        if (Steps::shell(run))
        {
            shell->getModel()->setValue(QStringLiteral("title"), QStringLiteral("Applied by the tool"));
        }
        if ((current = hooksGUI(run)) != nullptr)
        {
            current->addWork();
        }
        Steps::settle();

        // The discovery run checks, that the step did what it says (the replay ignores the answer)
        return Steps::shell(run) && Steps::shell(run)->hasSaveQuestion();
    }

    /// The command of the application, which the tool takes: by its action of the window (the shell asks the GUI,
    /// the GUI says the states changed, the shell emits into the tab)
    static bool routedCommand(ToolPagesLifetimeRun& run)
    {
        QAction* action = run.window ? run.window->getCommandAction(pdf::PDFAppCommands::zoomIn()) : nullptr;
        const bool isEnabled = action && action->isEnabled();
        if (isEnabled)
        {
            action->trigger();
        }
        Steps::settle();
        return isEnabled;
    }

    /// Save of the user asks the question of the tool; \p buttonName answers it by the mouse. \returns the question
    /// was there and answered.
    static bool answerSaveQuestion(ToolPagesLifetimeRun& run, const QString& buttonName)
    {
        if (!run.tab || !Steps::shell(run))
        {
            return false;
        }
        run.tab->executeCommand(pdf::PDFAppCommands::save());
        Steps::settle();
        pdf::PDFDocumentToolShell* shell = Steps::shell(run);
        pdf::PDFMessageBox* question = shell ? shell->getSaveQuestion() : nullptr;
        const bool isAnswered = question && question->isVisible() && Steps::click(question->findChild<QPushButton*>(buttonName));
        Steps::settle();
        return isAnswered;
    }

    static bool saveQuestionCancel(ToolPagesLifetimeRun& run)
    {
        return answerSaveQuestion(run, QStringLiteral("toolSaveCancel"));
    }

    static bool saveQuestionWithoutChanges(ToolPagesLifetimeRun& run)
    {
        return answerSaveQuestion(run, QStringLiteral("toolSaveWithoutChanges"));
    }

    /// Apply and Save: the run, Keep in This Tab, the tool closes, the page saves the kept result
    static bool saveQuestionApply(ToolPagesLifetimeRun& run)
    {
        if (!answerSaveQuestion(run, QStringLiteral("toolSaveApply")))
        {
            return false;
        }
        QPointer<pdf::PDFDocumentToolShell> guard(Steps::shell(run));
        Steps::wait(run, [&guard]() { return !guard || guard->getResultDialog() != nullptr; });
        if (!guard || !guard->getResultDialog() || !Steps::confirmDialog(run, guard->getResultDialog()))
        {
            return false;
        }

        // The kept result is saved (the title of the run made the document modified)
        Steps::wait(run, [&run]()
        {
            const pdf::PDFAppDocument* document = run.tab ? run.tab->getDocument() : nullptr;
            return !document || !document->isModified();
        });
        Steps::settle();
        const pdf::PDFAppDocument* document = run.tab ? run.tab->getDocument() : nullptr;
        return document && !document->isModified() && document->getDocument()->getInfo()->title == QStringLiteral("Applied by the tool");
    }
};

/// The scenario content
class ContentLifetimeScenarios
{
public:
    ContentLifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = ContentLifetimeHelper;
        using Steps = ToolPagesLifetimeSteps;

        ToolPagesLifetimeScenario content;
        content.name = QStringLiteral("content");
        content.toolId = QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID);
        content.openDocument = &H::openDocument;
        content.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::waitForContainer);
            steps->push_back(&H::changeLanguage);
            steps->push_back(&H::cancelAndReopen);
            steps->push_back(&H::waitForContainer);
        };
        scenarios->push_back(content);

        // Order (2) with queued work for the GUI after its parts are gone (R3-12)
        ToolPagesLifetimeScenario parts;
        parts.name = QStringLiteral("contentparts");
        parts.toolId = QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID);
        parts.openDocument = &H::openDocument;
        parts.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::waitForContainer);
            steps->push_back(&H::partsDieFirst);
            steps->push_back(&H::changeLanguage);
            steps->push_back(&H::cancelAndReopen);
            steps->push_back(&H::waitForContainer);
        };
        scenarios->push_back(parts);

        // The hooks of a tool with its own model, as Edit Content uses them: a routed command, the save question
        // with its three answers (R3-12)
        ToolPagesLifetimeScenario hooks;
        hooks.name = QStringLiteral("contenthooks");
        hooks.toolId = QStringLiteral("pages.rotate");
        hooks.openDocument = &H::openHooksDocument;
        hooks.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::addWork);
            steps->push_back(&H::routedCommand);
            steps->push_back(&H::saveQuestionCancel);
            steps->push_back(&H::saveQuestionWithoutChanges);
            steps->push_back(&H::saveQuestionApply);
        };
        scenarios->push_back(hooks);

        // P14: the flows of the real GUI - a notice shown during a change of the language, the errors of writing with
        // Back to Editing and Skip Page (a run again), Clear Page, Undo and a page by the window, the save question
        // over the real GUI with Cancel and Save Without the Changes, the run with Keep in This Tab
        ToolPagesLifetimeScenario flows;
        flows.name = QStringLiteral("contentflows");
        flows.toolId = QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID);
        flows.openDocument = &H::openDocument;
        flows.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::waitForContainer);
            steps->push_back(&H::previewOn);
            steps->push_back(&H::changeLanguage);
            steps->push_back(&H::previewOff);
            steps->push_back(&H::reloadDocument);
            steps->push_back(&H::addContentWork);
            steps->push_back(&H::writeErrorBack);
            steps->push_back(&H::writeErrorSkip);
            steps->push_back(&H::clearPage);
            steps->push_back(&H::undoByWindow);
            steps->push_back(&H::nextPage);
            steps->push_back(&H::waitForContainer);
            steps->push_back(&H::saveQuestionCancel);
            steps->push_back(&H::saveQuestionWithoutChanges);
            steps->push_back(&H::applyAndKeep);
        };
        scenarios->push_back(flows);

        // P14: Apply and Save of the save question over the real GUI (the result is kept, the page saves it)
        ToolPagesLifetimeScenario save;
        save.name = QStringLiteral("contentsave");
        save.toolId = QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID);
        save.openDocument = &H::openDocument;
        save.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::waitForContainer);
            steps->push_back(&H::addContentWork);
            steps->push_back(&H::saveQuestionApplyContent);
        };
        scenarios->push_back(save);
    }
};

void ToolPagesLifetimePackages::registerContentScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    ContentLifetimeScenarios::add(scenarios);
}
