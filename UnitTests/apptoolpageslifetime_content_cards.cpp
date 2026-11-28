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

// The lifetime scenario of Edit Content, stage E1, package P13 (the cards and their stack; zadani_overeni.md 8,
// zadani_gui.md 4.14): contentcards - two objects selected (the cards of the selection), a width typed and not
// confirmed over a change of the language (order (3): the stack and its cards are built again, the GUI commits the
// value first), an opacity entered in an expanded section, a button of Arrange, a flip, the check box Fill off and on, a
// new version of the document without work (reloadDocument: the stack follows the new session), the card Locked object
// and its Unlock, then Cancel (order (1)) and the tool again. The victims of the sweep are deleted inside of any event of a
// step; a step, whose part is gone, ends without a failure.

#include "apptoolpageslifetime.h"

#include "pdfappeditcontentcards.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentselection.h"
#include "pdfapptoolgui_content.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolshell.h"
#include "pdfsettingdefinitions.h"

#include "pdfcomponentsections.h"
#include "pdffluentwidgets.h"

#include "pdfcontenteditsession.h"
#include "pdfdocumenttool_content.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

/// A translator, which is never empty: its installation and its removal send the change of the language
class ContentCardsLifetimeTranslator : public QTranslator
{
public:
    virtual bool isEmpty() const override { return false; }
};

/// The steps of the scenario contentcards
class ContentCardsLifetimeHelper
{
public:
    ContentCardsLifetimeHelper() = delete;

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

    static pdf::PDFAppEditContentCardStack* stack(const ToolPagesLifetimeRun& run)
    {
        return run.window ? run.window->findChild<pdf::PDFAppEditContentCardStack*>() : nullptr;
    }

    template<typename T>
    static T* find(const ToolPagesLifetimeRun& run, const char* objectName)
    {
        pdf::PDFAppEditContentCardStack* cards = stack(run);
        return cards ? cards->findChild<T*>(QLatin1String(objectName)) : nullptr;
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

    /// The container of the shown page is loaded
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

    /// The first two objects of the page selected (the stack builds the cards of the selection from the event loop)
    static bool selectObjects(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentContext* current = context(run);
        const pdf::PDFContentContainerPointer container = current ? current->getContainer() : nullptr;
        if (!container)
        {
            return true;
        }

        std::vector<pdf::PDFContentNodeReference> nodes;
        for (const pdf::PDFContentNodePointer& child : container->getRoot()->getChildren())
        {
            if (child->getKind() != pdf::PDFContentNodeKind::StateRun && nodes.size() < 2)
            {
                nodes.push_back(pdf::PDFContentNodeReference{ container->getKey(), child->getId() });
            }
        }
        current->getSelection()->setNodes(nodes);
        Steps::settle();
        return true;
    }

    /// A width typed and not confirmed, then a change of the language (order (3)): the GUI commits the pending values,
    /// the shell builds the stack again
    static bool typeOverLanguage(ToolPagesLifetimeRun& run)
    {
        pdf::PDFDoubleSpinBox* width = find<pdf::PDFDoubleSpinBox>(run, "editContentStrokeWidth");
        if (width)
        {
            width->setFocus();
            QTest::keyClick(width, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(width, QStringLiteral("3"));
        }

        ContentCardsLifetimeTranslator translator;
        QCoreApplication::installTranslator(&translator);
        Steps::settle();
        QCoreApplication::removeTranslator(&translator);
        Steps::settle();
        return run.window != nullptr;
    }

    /// The section Transparency expanded, an opacity entered (one command)
    static bool enterOpacity(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentCardStack* cards = stack(run);
        pdf::PDFAppEditContentCard* card = cards ? cards->getCard(QLatin1String(pdf::PDFAppEditContentCardIds::TRANSPARENCY)) : nullptr;
        if (card && !card->getSections().empty() && card->getSections().front())
        {
            card->getSections().front()->setExpanded(true);
        }
        Steps::settle();

        pdf::PDFDoubleSpinBox* opacity = find<pdf::PDFDoubleSpinBox>(run, "editContentFillOpacity");
        if (opacity)
        {
            opacity->setFocus();
            QTest::keyClick(opacity, Qt::Key_A, Qt::ControlModifier);
            QTest::keyClicks(opacity, QStringLiteral("60"));
            opacity = find<pdf::PDFDoubleSpinBox>(run, "editContentFillOpacity");
            if (opacity)
            {
                QTest::keyClick(opacity, Qt::Key_Return);
            }
        }
        Steps::settle();
        return true;
    }

    /// A button of Arrange (Align left to the key object), then Flip horizontally of Position and size
    static bool clickArrangeAndFlip(ToolPagesLifetimeRun& run)
    {
        Steps::click(find<QToolButton>(run, "editContentArrange_obj-align-left"));
        Steps::settle();
        Steps::click(find<QToolButton>(run, "editContentFlipHorizontally"));
        Steps::settle();
        return true;
    }

    /// The check box Fill of the selected objects (FX3 item 1: a command of a check box), off and on again
    static bool switchPaint(ToolPagesLifetimeRun& run)
    {
        Steps::click(find<QCheckBox>(run, "editContentFillCheck"));
        Steps::settle();
        Steps::click(find<QCheckBox>(run, "editContentFillCheck"));
        Steps::settle();
        return true;
    }

    /// A new version of the document without work (PDFAppEditContentContext::reloadDocument): the stack follows the
    /// new session (sessionReplaced), the cards are built for the page of the new session
    static bool reloadSession(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentContext* current = context(run);
        const pdf::PDFDocument* document = current && current->getSession() ? current->getSession()->getDocument() : nullptr;
        if (!document)
        {
            return true;
        }
        current->reloadDocument(pdf::PDFDocumentPointer(new pdf::PDFDocument(*document)));
        Steps::settle();
        return waitForContainer(run);
    }

    /// The card Locked object of a locked node and its Unlock (the card goes with the next rebuild)
    static bool unlockObject(ToolPagesLifetimeRun& run)
    {
        pdf::PDFAppEditContentContext* current = context(run);
        if (!current || current->getSelection()->isEmpty())
        {
            return true;
        }

        const pdf::PDFContentNodeReference node = current->getSelection()->getNodes().front();
        current->getSession()->setNodeLocked(node, true);
        current = context(run);
        if (current)
        {
            current->getSelection()->clear();
        }
        current = context(run);
        if (current)
        {
            current->setLockedClickNode(node);
        }
        current = context(run);
        if (current)
        {
            current->setCardRequested(QLatin1String(pdf::PDFAppEditContentCardIds::LOCKED_OBJECT), true);
        }
        Steps::settle();
        Steps::click(find<QPushButton>(run, "editContentUnlock"));
        Steps::settle();
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

        // The commands of the steps ask the question of the tool: its action (discard the changes)
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

/// The scenario contentcards
class ContentCardsLifetimeScenarios
{
public:
    ContentCardsLifetimeScenarios() = delete;

    static void add(std::vector<ToolPagesLifetimeScenario>* scenarios)
    {
        using H = ContentCardsLifetimeHelper;
        using Steps = ToolPagesLifetimeSteps;

        ToolPagesLifetimeScenario cards;
        cards.name = QStringLiteral("contentcards");
        cards.toolId = QString::fromLatin1(pdf::PDFDocumentToolContent::TOOL_ID);
        cards.openDocument = &H::openDocument;
        cards.addSteps = [](std::vector<ToolPagesLifetimeStep>* steps)
        {
            steps->push_back(&Steps::openTool);
            steps->push_back(&H::waitForContainer);
            steps->push_back(&H::selectObjects);
            steps->push_back(&H::typeOverLanguage);
            steps->push_back(&H::selectObjects);
            steps->push_back(&H::enterOpacity);
            steps->push_back(&H::clickArrangeAndFlip);
            steps->push_back(&H::switchPaint);
            steps->push_back(&H::reloadSession);
            steps->push_back(&H::selectObjects);
            steps->push_back(&H::unlockObject);
            steps->push_back(&H::cancelAndReopen);
            steps->push_back(&H::waitForContainer);
        };
        scenarios->push_back(cards);
    }
};

void ToolPagesLifetimePackages::registerContentCardsScenarios(std::vector<ToolPagesLifetimeScenario>* scenarios)
{
    ContentCardsLifetimeScenarios::add(scenarios);
}
