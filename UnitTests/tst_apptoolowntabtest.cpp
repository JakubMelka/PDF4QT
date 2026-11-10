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

// The own tab of a tool (plan of step 2 of phase 2, 4.1 - 4.6, R13, R16, R17, Q9, K1, K12,
// K16, K33): the route /tool/{toolId}/{instanceId}, the tab, its host and the layout shared
// with the document view, the close and the quit questions, the result dialog of a new
// document (Open in a new tab after the tab, the partial branch 378:4376, the written bytes
// of the run), Blank PDF without the dialog, the cache of the previews of a tool without a
// document. The test tool takes the ids pages.merge and convert.blank of the catalog.

#include "apptoolowntabfixture.h"
#include "appmcpfixture.h"

#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentstatusbar.h"
#include "pdfapphelpcatalog.h"
#include "pdfappdocumenttab.h"
#include "pdfapphistory.h"
#include "pdfappmainwindow.h"
#include "pdfappowntabtoolhost.h"
#include "pdfapppagenotfound.h"
#include "pdfapproute.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptoolmodelayout.h"
#include "pdfapptooltab.h"
#include "pdfapptooltoolbar.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdocumenttoolrunner.h"

#include "pdfcatalog.h"
#include "pdfcomponentpanels.h"
#include "pdfdocumentreader.h"
#include "pdfdocumenttoolregistry.h"
#include "pdfmcpcall.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSplitter>
#include <QTimer>
#include <QtTest>

using pdftest::AppToolOwnTabFixture;
using pdftest::OwnTabTestTool;

class AppToolOwnTabTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();

    void routeOfNewTab();
    void routeWithoutTabOpensEmptyTab();
    void routeOfInvalidInstanceOrToolIsNotFound();
    void documentRouteOpensOwnTab();
    void moreInstances();
    void titleIconAndDot();
    void layoutOfOwnTab();
    void statusBarWithoutDocument();
    void undoAndRedoOfTool();
    void closeAsksForInputs();
    void closeAsksDuringRun();
    void closeRefusedWhileDeciding();
    void quitAsksForInputs();
    void quitStopsRunWhenAsked();
    void acceptOpensNewTabAfterToolTab();
    void writtenBytesOfRunAreOpened();
    void discardKeepsTool();
    void continueWithStartsOnNewTab();
    void partialResultDialog();
    void blankReplacesTab();
    void blankFailureKeepsTab();
    void previewCacheOfGeneration();
    void deleteTabInEveryState_data();
    void deleteTabInEveryState();
    void themesAndRightToLeft();
    void mcpNewDocumentOverHttp();
};

/// Helpers of the test
class OwnTabTestHelper
{
public:
    OwnTabTestHelper() = delete;

    /// Clicks the button of the dialog of the object name as soon as it is shown (a dialog
    /// of PDFAppDocumentChoiceDialog::run, which has its own event loop)
    static void answer(const QString& dialogName, const QString& buttonName, bool* isSeen = nullptr)
    {
        QTimer* timer = new QTimer(qApp);
        timer->setInterval(5);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, dialogName, buttonName, isSeen]()
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                if (widget->objectName() == dialogName && widget->isVisible())
                {
                    if (QPushButton* button = widget->findChild<QPushButton*>(buttonName))
                    {
                        if (isSeen)
                        {
                            *isSeen = true;
                        }
                        timer->stop();
                        timer->deleteLater();
                        button->click();
                        return;
                    }
                }
            }
        });
        timer->start();

        // Nothing is left behind, when no dialog comes
        QTimer::singleShot(10000, timer, &QTimer::deleteLater);
    }

    /// The open result dialog of the window, nullptr = none
    static pdf::PDFAppToolResultDialog* resultDialog()
    {
        return AppToolOwnTabFixture::waitForWidget<pdf::PDFAppToolResultDialog>();
    }

    /// Runs the tool of the tab and waits for its result dialog
    static pdf::PDFAppToolResultDialog* runToDialog(pdf::PDFAppToolTab* tab)
    {
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        if (!shell)
        {
            return nullptr;
        }
        shell->confirm();
        if (!AppToolOwnTabFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Deciding))
        {
            return nullptr;
        }
        return shell->getResultDialog();
    }

    /// The tab bar index of the tab, -1 = none
    static int indexOf(pdf::PDFAppMainWindow* window, const pdf::PDFAppTab* tab)
    {
        return int(window->getTabs().indexOf(const_cast<pdf::PDFAppTab*>(tab)));
    }

    /// The text of the tab bar of the tab
    static QString tabText(pdf::PDFAppMainWindow* window, const pdf::PDFAppTab* tab)
    {
        const int index = indexOf(window, tab);
        return index >= 0 ? window->getTabBar()->tabText(index) : QString();
    }

    /// A list with the documents of the tabs as its inputs (R11: their snapshots)
    static pdf::PDFAppToolInputList* createList(const std::vector<pdf::PDFAppDocumentTab*>& tabs)
    {
        pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
        for (pdf::PDFAppDocumentTab* tab : tabs)
        {
            list->addOpenDocument(tab->getDocument());
        }
        return list;
    }

    /// Ready document tab of \p pageCount pages
    static pdf::PDFAppDocumentTab* openReady(AppToolOwnTabFixture& fixture, int pageCount)
    {
        pdf::PDFAppDocumentTab* tab = fixture.openDocument(pageCount);
        if (!tab || !AppToolOwnTabFixture::waitFor([tab]() { return tab->getDocument() && tab->getDocument()->isReady(); }))
        {
            return nullptr;
        }
        AppToolOwnTabFixture::settle();
        return tab;
    }
};

using H = OwnTabTestHelper;

void AppToolOwnTabTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    qRegisterMetaType<pdf::PDFDocumentToolResult>();
}

void AppToolOwnTabTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppToolOwnTabTest::routeOfNewTab()
{
    // 4.1: /tool/{toolId}/{instanceId}, the instance of the form of a document id (R19), the
    // tab current, the list taken over by the tab (R13), the page of F1 the topic of the tool
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppToolInputList* list = new pdf::PDFAppToolInputList(nullptr);
    pdf::PDFAppToolTab* tab = fixture.createToolTab(QStringLiteral("pages.merge"), list);
    QVERIFY(tab);

    QCOMPARE(tab->getKind(), pdf::PDFAppTabKind::Tool);
    QCOMPARE(list->parent(), tab);
    QCOMPARE(tab->getInputList(), list);
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    const QStringList segments = tab->getHistory().getCurrentEntry()->route.getSegments();
    QCOMPARE(segments.size(), 3);
    QCOMPARE(segments[0], QStringLiteral("tool"));
    QCOMPARE(segments[1], QStringLiteral("pages.merge"));
    QCOMPARE(segments[2], tab->getInstanceId());
    QVERIFY(pdf::PDFAppRouteValues::documentId().accepts(tab->getInstanceId()));
    QCOMPARE(fixture.window->findToolTab(tab->getInstanceId()), tab);
    QVERIFY(!fixture.window->findDocumentTab(tab->getInstanceId()));

    QVERIFY(qobject_cast<pdf::PDFAppToolTabPage*>(tab->getPage()));
    // F1: the topic of the tool, as the page of the tool in a document tab (the first batch, 5)
    const pdf::PDFDocumentToolCatalogEntry* merge = pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("pages.merge"));
    QCOMPARE(tab->getPage()->getHelpTopic(), pdf::PDFAppHelpCatalog::getBuiltin()->getToolTopic(*merge));
    QVERIFY(AppToolOwnTabFixture::getShell(tab));
    QCOMPARE(AppToolOwnTabFixture::getShell(tab)->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QVERIFY(AppToolOwnTabFixture::getShell(tab)->getHost()->isOwnTab());
    QVERIFY(!AppToolOwnTabFixture::getShell(tab)->getHost()->getDocument());

    // The tab is no document of the session or of a workspace; the tab after it
    QVERIFY(fixture.window->getOpenDocuments().empty());
    QCOMPARE(fixture.window->getTabIndexAfter(static_cast<const pdf::PDFAppTab*>(tab)), H::indexOf(fixture.window, tab) + 1);
}

void AppToolOwnTabTest::routeWithoutTabOpensEmptyTab()
{
    // R13: a route of the history or of a link without its tab opens the empty state of a
    // new tab of that instance (378:1657); the same route again shows the same tab
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    const int tabCount = int(fixture.window->getTabs().size());
    const QString instanceId = pdf::PDFAppDocument::createId();
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::toolTab(QStringLiteral("pages.merge"), instanceId));
    AppToolOwnTabFixture::settle();

    pdf::PDFAppToolTab* tab = fixture.window->findToolTab(instanceId);
    QVERIFY(tab);
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount + 1);
    QCOMPARE(tab->getInputList()->getCount(), 0);
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    QVERIFY(AppToolOwnTabFixture::getShell(tab));

    fixture.window->activateTab(fixture.window->getHomeTab());
    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::toolTab(QStringLiteral("pages.merge"), instanceId));
    AppToolOwnTabFixture::settle();
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount + 1);
    QCOMPARE(fixture.window->getCurrentTab(), tab);
}

void AppToolOwnTabTest::routeOfInvalidInstanceOrToolIsNotFound()
{
    // R19: the instance has the form of a document id; a tool without its own tab has no
    // such route: Page Not Found, no tab
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    std::shared_ptr<pdftest::ShellTestTool> rotate = fixture.registerShellTool(QStringLiteral("pages.rotate"));
    QVERIFY(rotate);
    const int tabCount = int(fixture.window->getTabs().size());

    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::toolTab(QStringLiteral("pages.merge"), QStringLiteral("abc")));
    AppToolOwnTabFixture::settle();
    QVERIFY(fixture.notFoundPage());
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount);

    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::toolTab(QStringLiteral("pages.rotate"), pdf::PDFAppDocument::createId()));
    AppToolOwnTabFixture::settle();
    QVERIFY(fixture.notFoundPage());
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount);
    QVERIFY(fixture.window->getToolTabs().isEmpty());
}

void AppToolOwnTabTest::documentRouteOpensOwnTab()
{
    // 4.1: the panel Tools, Then continue with and /document/{id}/tool/pages.merge open a new
    // tab of the tool with this document as its first input; the document tab keeps its
    // document and its history has no route of the tool
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 3);
    QVERIFY(documentTab);

    fixture.window->getNavigator()->navigate(pdf::PDFAppRoutes::documentTool(documentTab->getDocumentId(), QStringLiteral("pages.merge")));
    QVERIFY(AppToolOwnTabFixture::waitFor([&fixture]() { return !fixture.window->getToolTabs().isEmpty() && fixture.window->getToolTabs().front()->getShell(); }));
    AppToolOwnTabFixture::settle();

    pdf::PDFAppToolTab* tab = fixture.window->getToolTabs().front();
    QCOMPARE(fixture.window->getCurrentTab(), tab);
    QCOMPARE(tab->getInputList()->getCount(), 1);
    const pdf::PDFAppToolInput* input = tab->getInputList()->getInput(0);
    QCOMPARE(input->kind, pdf::PDFAppToolInputKind::OpenTab);
    QCOMPARE(input->documentId, documentTab->getDocumentId());
    QCOMPARE(input->pageCount, pdf::PDFInteger(3));

    const pdf::PDFAppHistory& history = documentTab->getHistory();
    QCOMPARE(history.getCurrentEntry()->route.getSegments(), QStringList({ QStringLiteral("document"), documentTab->getDocumentId() }));
    for (int i = 0; i < history.getCount(); ++i)
    {
        QVERIFY(!history.getEntry(i)->route.getSegments().contains(QStringLiteral("tool")));
    }
    QVERIFY(!documentTab->getView()->isToolModeActive());
}

void AppToolOwnTabTest::moreInstances()
{
    // 4.1: every tab its own instance with its own shell and runner
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppToolTab* first = fixture.createToolTab(QStringLiteral("pages.merge"));
    pdf::PDFAppToolTab* second = fixture.createToolTab(QStringLiteral("pages.merge"));
    QVERIFY(first && second && first != second);
    QVERIFY(first->getInstanceId() != second->getInstanceId());
    QVERIFY(AppToolOwnTabFixture::getShell(first) != AppToolOwnTabFixture::getShell(second));
    QVERIFY(AppToolOwnTabFixture::getShell(first)->getRunner() != AppToolOwnTabFixture::getShell(second)->getRunner());
    QCOMPARE(first->getInputList() != second->getInputList(), true);
    QCOMPARE(fixture.window->getToolTabs().size(), qsizetype(2));
}

void AppToolOwnTabTest::titleIconAndDot()
{
    // K1: Blank PDF in its tab "New document" with the icon of a file (337:1976); K16: the
    // dot of the tab says, that the list of the inputs is not empty (378:1049 "Merge ●",
    // 378:1657 without the dot)
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("convert.blank"), true, QStringLiteral("New document")));

    pdf::PDFAppToolTab* blank = fixture.createToolTab(QStringLiteral("convert.blank"));
    QVERIFY(blank);
    QCOMPARE(H::tabText(fixture.window, blank), QStringLiteral("New document"));
    QCOMPARE(blank->getTabIconName(), QStringLiteral("file"));

    pdf::PDFAppToolTab* merge = fixture.createToolTab(QStringLiteral("pages.merge"));
    QVERIFY(merge);
    QCOMPARE(H::tabText(fixture.window, merge), pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("pages.merge"))->getTitle());
    QCOMPARE(merge->getTabIconName(), pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("pages.merge"))->getIconName());
    QVERIFY(!fixture.window->getTabBar()->isTabUnsaved(H::indexOf(fixture.window, merge)));

    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QVERIFY(!merge->getInputList()->addOpenDocument(documentTab->getDocument()).id.isEmpty());
    AppToolOwnTabFixture::settle();
    QVERIFY(fixture.window->getTabBar()->isTabUnsaved(H::indexOf(fixture.window, merge)));
    QCOMPARE(merge->getLossText(), QStringLiteral("The list of 1 files is lost."));
}

void AppToolOwnTabTest::layoutOfOwnTab()
{
    // 4.2 and the frame 378:1049 (1440 x 960): the toolbar 52, the left panel 300, the
    // splitter 6, the canvas 814, the right panel 320, the status bar 28 - the layout of the
    // document view, one implementation
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppToolTab* tab = fixture.createToolTab(QStringLiteral("pages.merge"));
    QVERIFY(tab);
    AppToolOwnTabFixture::settle();

    pdf::PDFAppToolTabPage* page = tab->getToolPage();
    pdf::PDFAppToolModeLayout* layout = page->getTabView()->getLayout();
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QCOMPARE(page->getTabView()->width(), 1440);
    QCOMPARE(layout->getToolbarStack()->height(), 52);
    QCOMPARE(layout->getToolbarPart(), shell->getToolbar());
    QCOMPARE(shell->getLeftPanel()->width(), 300);
    QCOMPARE(layout->getSplitter()->handleWidth(), 6);
    QCOMPARE(layout->getCanvasArea()->width(), 814);
    QCOMPARE(shell->getRightPanel()->width(), 320);
    QCOMPARE(layout->getStatusBar()->height(), 28);
    QCOMPARE(layout->getCanvasWidget(), shell->getCanvasPage());
    QCOMPARE(layout->getStatusBar()->mapTo(page->getTabView(), QPoint(0, 0)).y() + 28, page->getTabView()->height());
}

void AppToolOwnTabTest::statusBarWithoutDocument()
{
    // 4.2: the status bar of the tab has the message of the tool and the items of its GUI;
    // the zoom item is hidden without a text (K12: Merge has no zoom)
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 4);
    QVERIFY(documentTab);
    pdf::PDFAppToolTab* tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    QVERIFY(tab);
    AppToolOwnTabFixture::settle();

    pdf::PDFAppDocumentStatusBar* statusBar = tab->getToolPage()->getTabView()->getStatusBar();
    QCOMPARE(statusBar->getMessage(), AppToolOwnTabFixture::getShell(tab)->getStatusMessage());
    QCOMPARE(statusBar->getPageText(), QStringLiteral("4 pages"));
    QVERIFY(statusBar->getZoomText().isEmpty());
    QVERIFY(statusBar->getZoomItem()->isHidden());
}

void AppToolOwnTabTest::undoAndRedoOfTool()
{
    // Owner rule "Undo" of the first batch: Undo and Redo of the tab act on the parameters of
    // the tool; off while it runs
    AppToolOwnTabFixture fixture;
    std::shared_ptr<OwnTabTestTool> tool = fixture.registerOwnTabTool(QStringLiteral("convert.blank"), true);
    QVERIFY(tool);
    pdf::PDFAppToolTab* tab = fixture.createToolTab(QStringLiteral("convert.blank"));
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::undo()));

    QVERIFY(!shell->getToolbar()->getUndoButton()->isEnabled());
    QVERIFY(shell->getModel()->setValue(QStringLiteral("pages"), 7).isEmpty());
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::undo()));
    QVERIFY(shell->getToolbar()->getUndoButton()->isEnabled());
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pages")).toInt(), 1);
    QVERIFY(tab->isCommandEnabled(pdf::PDFAppCommands::redo()));
    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::redo()));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pages")).toInt(), 7);

    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolOwnTabFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Running));
    QVERIFY(!tab->isCommandEnabled(pdf::PDFAppCommands::undo()));
    QVERIFY(!tab->executeCommand(pdf::PDFAppCommands::undo()));
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pages")).toInt(), 7);
    shell->stop();
    tool->setHold(false);
    QVERIFY(AppToolOwnTabFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
}

void AppToolOwnTabTest::closeAsksForInputs()
{
    // K16: the close of a tab with inputs asks; Cancel keeps it, Close Tab closes it; an empty
    // list closes without a question
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    QVERIFY(tab);

    bool isSeen = false;
    H::answer(QStringLiteral("toolTabCloseDialog"), QStringLiteral("cancelButton"), &isSeen);
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(isSeen);
    QVERIFY(tab);
    QCOMPARE(AppToolOwnTabFixture::getShell(tab)->getState(), pdf::PDFDocumentToolShell::State::Editing);

    // Cancel of the toolbar asks the same; refused, the tool takes Cancel again (resumeEditing)
    isSeen = false;
    H::answer(QStringLiteral("toolTabCloseDialog"), QStringLiteral("cancelButton"), &isSeen);
    AppToolOwnTabFixture::getShell(tab)->cancel();
    QVERIFY(AppToolOwnTabFixture::waitFor([&isSeen]() { return isSeen; }));
    AppToolOwnTabFixture::settle();
    QVERIFY(tab);
    isSeen = false;
    H::answer(QStringLiteral("toolTabCloseDialog"), QStringLiteral("closeTabButton"), &isSeen);
    AppToolOwnTabFixture::getShell(tab)->cancel();
    QVERIFY(AppToolOwnTabFixture::waitFor([&isSeen]() { return isSeen; }));
    AppToolOwnTabFixture::settle();
    QVERIFY(!tab);

    QPointer<pdf::PDFAppToolTab> empty = fixture.createToolTab(QStringLiteral("pages.merge"));
    QVERIFY(empty);
    isSeen = false;
    H::answer(QStringLiteral("toolTabCloseDialog"), QStringLiteral("cancelButton"), &isSeen);
    QVERIFY(fixture.window->closeTab(empty));
    AppToolOwnTabFixture::settle();
    QVERIFY(!isSeen);
    QVERIFY(!empty);
}

void AppToolOwnTabTest::closeAsksDuringRun()
{
    // 4.1 Close, F14: a run asks as close-running with the text without a document (4.2);
    // Keep Running keeps the run, Stop and Leave stops it and the tab goes
    AppToolOwnTabFixture fixture;
    std::shared_ptr<OwnTabTestTool> tool = fixture.registerOwnTabTool(QStringLiteral("convert.blank"), true);
    QVERIFY(tool);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("convert.blank"));
    QVERIFY(tab);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolOwnTabFixture::waitForState(shell, pdf::PDFDocumentToolShell::State::Running));
    QVERIFY(AppToolOwnTabFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));

    QString text;
    QTimer* reader = new QTimer(fixture.window);
    reader->setInterval(2);
    connect(reader, &QTimer::timeout, reader, [&text]()
    {
        for (QWidget* widget : QApplication::topLevelWidgets())
        {
            if (widget->objectName() == QLatin1String("toolLeaveRunningDialog") && widget->isVisible())
            {
                for (QLabel* label : widget->findChildren<QLabel*>())
                {
                    if (label->text().contains(QLatin1String("still running")))
                    {
                        text = label->text();
                    }
                }
            }
        }
    });
    reader->start();
    bool isSeen = false;
    H::answer(QStringLiteral("toolLeaveRunningDialog"), QStringLiteral("keepRunningButton"), &isSeen);
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(isSeen);
    QVERIFY(tab);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Running);
    const QString title = pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("convert.blank"))->getTitle();
    QCOMPARE(text, QStringLiteral("%1 is still running (0 % done). Leaving the tool stops it. Nothing has been written yet.").arg(title));
    delete reader;

    isSeen = false;
    H::answer(QStringLiteral("toolLeaveRunningDialog"), QStringLiteral("stopAndLeaveButton"), &isSeen);
    QVERIFY(fixture.window->closeTab(tab));
    QVERIFY(isSeen);
    AppToolOwnTabFixture::settle();
    QVERIFY(!tab);
    QVERIFY(AppToolOwnTabFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    tool->setHold(false);
}

void AppToolOwnTabTest::closeRefusedWhileDeciding()
{
    // F2: the open result dialog decides first, the tab stays
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    QPointer<pdf::PDFAppToolResultDialog> dialog = H::runToDialog(tab);
    QVERIFY(dialog);
    QVERIFY(!fixture.window->closeTab(tab));
    QVERIFY(tab);
    QVERIFY(dialog && dialog->isVisible());
    dialog->reject();
    AppToolOwnTabFixture::settle();
}

void AppToolOwnTabTest::quitAsksForInputs()
{
    // K16 "Ukončení aplikace stejně": the window asks for the inputs of the tool tab; Cancel
    // keeps the window and the tool as it is
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    QVERIFY(tab);

    bool isSeen = false;
    H::answer(QStringLiteral("toolTabCloseDialog"), QStringLiteral("cancelButton"), &isSeen);
    QVERIFY(!fixture.window->close());
    QVERIFY(isSeen);
    QVERIFY(fixture.window && fixture.window->isVisible());
    QVERIFY(tab);
    QCOMPARE(AppToolOwnTabFixture::getShell(tab)->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(tab->getInputList()->getCount(), 1);
}

void AppToolOwnTabTest::quitStopsRunWhenAsked()
{
    // The quit asks every tool tab: Stop and Leave stops the run at once - a quit canceled by a
    // later question (the inputs of another tool tab) keeps that tool editing, nothing runs
    AppToolOwnTabFixture fixture;
    std::shared_ptr<OwnTabTestTool> tool = fixture.registerOwnTabTool(QStringLiteral("pages.merge"));
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QPointer<pdf::PDFAppToolTab> running = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    QPointer<pdf::PDFAppToolTab> listed = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    QVERIFY(running && listed);
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(running);
    tool->setHold(true);
    shell->confirm();
    QVERIFY(AppToolOwnTabFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));

    // The first tool tab in the order of the tab bar asks first
    const bool isRunningFirst = H::indexOf(fixture.window, running) < H::indexOf(fixture.window, listed);
    QVERIFY(isRunningFirst);
    bool isRunningSeen = false;
    bool isListSeen = false;
    H::answer(QStringLiteral("toolLeaveRunningDialog"), QStringLiteral("stopAndLeaveButton"), &isRunningSeen);
    H::answer(QStringLiteral("toolTabCloseDialog"), QStringLiteral("cancelButton"), &isListSeen);
    QVERIFY(!fixture.window->close());
    QVERIFY(isRunningSeen && isListSeen);
    QVERIFY(AppToolOwnTabFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    tool->setHold(false);
    AppToolOwnTabFixture::settle();
    QVERIFY(running && listed);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(shell->getRunner()->getState(), pdf::PDFDocumentToolRunner::State::Idle);
}

void AppToolOwnTabTest::acceptOpensNewTabAfterToolTab()
{
    // 4.5 and Q9: Open in a new tab opens the new document after the own tab of the tool
    // (the anchor), the tool tab closes, the new document is the current tab
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppDocumentTab* first = H::openReady(fixture, 3);
    pdf::PDFAppDocumentTab* second = H::openReady(fixture, 2);
    QVERIFY(first && second);

    // The tool tab between the document tabs: a new tab goes after it, not after the current
    fixture.window->activateTab(first);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ first, second }));
    QVERIFY(tab);
    const int toolIndex = H::indexOf(fixture.window, tab);
    QCOMPARE(toolIndex, H::indexOf(fixture.window, first) + 1);

    QPointer<pdf::PDFAppToolResultDialog> dialog = H::runToDialog(tab);
    QVERIFY(dialog);
    QCOMPARE(dialog->getResultKind(), pdf::PDFAppToolResultDialog::ResultKind::NewDocument);
    QCOMPARE(dialog->getSelectedChoice(), pdf::PDFAppToolResultDialog::Choice::NewTab);
    QVERIFY(!dialog->isPartial());
    QVERIFY(dialog->getContinueWith());

    // The current tab may change under the modal dialog (an assistant activates a document):
    // the anchor is the tool tab, not the current tab
    fixture.window->activateTab(second);
    dialog->confirm();
    QVERIFY(AppToolOwnTabFixture::waitFor([&tab]() { return tab.isNull(); }));
    AppToolOwnTabFixture::settle();

    pdf::PDFAppDocumentTab* newTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getTabs().value(toolIndex));
    QVERIFY(newTab);
    QVERIFY(newTab != first && newTab != second);
    QCOMPARE(fixture.window->getCurrentTab(), newTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([newTab]() { return newTab->getDocument()->isReady(); }));
    QCOMPARE(newTab->getDocument()->getPageCount(), pdf::PDFInteger(5));
    QCOMPARE(H::indexOf(fixture.window, second), toolIndex + 1);
}

void AppToolOwnTabTest::writtenBytesOfRunAreOpened()
{
    // 4.5 "one write": the bytes the run has written (PDFDocumentToolOutputDocument::writtenData)
    // are the new tab; the test tool writes a document of 6 pages beside its document of 2
    AppToolOwnTabFixture fixture;
    std::shared_ptr<OwnTabTestTool> tool = fixture.registerOwnTabTool(QStringLiteral("pages.merge"));
    QVERIFY(tool);
    tool->setWrittenPages(6);
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    const int toolIndex = H::indexOf(fixture.window, tab);
    QPointer<pdf::PDFAppToolResultDialog> dialog = H::runToDialog(tab);
    QVERIFY(dialog);
    dialog->confirm();
    QVERIFY(AppToolOwnTabFixture::waitFor([&tab]() { return tab.isNull(); }));
    pdf::PDFAppDocumentTab* newTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getTabs().value(toolIndex));
    QVERIFY(newTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([newTab]() { return newTab->getDocument()->isReady(); }));
    QCOMPARE(newTab->getDocument()->getPageCount(), pdf::PDFInteger(6));
}

void AppToolOwnTabTest::discardKeepsTool()
{
    // Q9: Discard result keeps the tab with its inputs and its parameters, the tool edits
    // again and runs again
    AppToolOwnTabFixture fixture;
    std::shared_ptr<OwnTabTestTool> tool = fixture.registerOwnTabTool(QStringLiteral("pages.merge"));
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(shell->getModel()->setValue(QStringLiteral("pages"), 9).isEmpty());

    QPointer<pdf::PDFAppToolResultDialog> dialog = H::runToDialog(tab);
    QVERIFY(dialog);
    dialog->getDiscardButton()->click();
    AppToolOwnTabFixture::settle();
    QVERIFY(tab);
    QCOMPARE(AppToolOwnTabFixture::getShell(tab), shell);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(tab->getInputList()->getCount(), 1);
    QCOMPARE(shell->getModel()->getValue(QStringLiteral("pages")).toInt(), 9);
    QCOMPARE(shell->getRunner()->getState(), pdf::PDFDocumentToolRunner::State::Idle);

    // Cancel works again after the dialog (no close request is pending)
    const int executions = tool->getExecuteCount();
    QVERIFY(H::runToDialog(tab));
    QCOMPARE(tool->getExecuteCount(), executions + 1);
    shell->getResultDialog()->reject();
    AppToolOwnTabFixture::settle();
    QVERIFY(tab);
}

void AppToolOwnTabTest::continueWithStartsOnNewTab()
{
    // R17: Then continue with starts on the new tab first, then the tool tab closes; the next
    // tool runs on the new document (Compress, a successor of Merge in the catalog)
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    std::shared_ptr<pdftest::ShellTestTool> compress = fixture.registerShellTool(QStringLiteral("optimize.compress"));
    QVERIFY(compress);
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    const int toolIndex = H::indexOf(fixture.window, tab);
    QPointer<pdf::PDFAppToolResultDialog> dialog = H::runToDialog(tab);
    QVERIFY(dialog);
    const int compressIndex = int(dialog->getContinueToolIds().indexOf(QStringLiteral("optimize.compress")));
    QVERIFY(compressIndex >= 0);
    dialog->getContinueWith()->setSelectedChoice(compressIndex);
    dialog->confirm();
    QVERIFY(AppToolOwnTabFixture::waitFor([&tab]() { return tab.isNull(); }));

    pdf::PDFAppDocumentTab* newTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getTabs().value(toolIndex));
    QVERIFY(newTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([newTab]() { return AppToolOwnTabFixture::getShell(newTab) != nullptr; }));
    QCOMPARE(AppToolOwnTabFixture::getShell(newTab)->getTool()->getId(), QStringLiteral("optimize.compress"));
    QCOMPARE(fixture.window->getCurrentTab(), newTab);
}

void AppToolOwnTabTest::partialResultDialog()
{
    // Q5 and 378:4376: an input left out by the run - the badge alert-triangle in amber, the
    // title, the subtitle, the fact Left out "<name> · <pages> pages", the amber callout with the
    // reason of the core, no Then continue with (tools.md 9.3 point 12)
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
    pdf::PDFAppDocumentTab* first = H::openReady(fixture, 3);
    pdf::PDFAppDocumentTab* second = H::openReady(fixture, 9);
    QVERIFY(first && second);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ first, second }));
    QVERIFY(AppToolOwnTabFixture::getShell(tab)->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("partial")).isEmpty());
    const QString name = tab->getInputList()->getInput(1)->name;

    QPointer<pdf::PDFAppToolResultDialog> dialog = H::runToDialog(tab);
    QVERIFY(dialog);
    QVERIFY(dialog->isPartial());
    AppToolOwnTabFixture::screenshotDialog(dialog, QStringLiteral("pages-merge-done-partial-378-4376-dialog"));
    // 378:4376 in the window of the frame (1440 x 960): the whole body is shown, the options
    // are not cut off below a scroll bar (the reason of the warning is set before the dialog
    // is shown and has to be measured)
    // callout-amber 378:5028: the text 378:5033 Regular 11 in lines of 13, 4 below the head
    const pdf::PDFAppParagraph* warningText = dialog->getPartialWarning()->findChild<pdf::PDFAppParagraph*>(QStringLiteral("partialWarningText"));
    QVERIFY(warningText && !warningText->isHidden());
    QCOMPARE(warningText->font().pixelSize(), 11);
    QCOMPARE(warningText->getLineHeight(), 13);
    QCOMPARE(dialog->getPartialWarning()->layout()->spacing(), 4);
    QCOMPARE(dialog->getScrollArea()->verticalScrollBar()->maximum(), 0);
    QVERIFY(!dialog->getScrollArea()->verticalScrollBar()->isVisible());
    QCOMPARE(dialog->getScrollArea()->viewport()->height(), dialog->getScrollArea()->widget()->heightForWidth(dialog->getScrollArea()->viewport()->width()));
    const QString title = pdf::PDFDocumentToolCatalog::findTool(QStringLiteral("pages.merge"))->getTitle();
    QCOMPARE(dialog->findChild<pdf::PDFAppParagraph*>(QStringLiteral("toolResultTitle"))->text(), QStringLiteral("%1 finished without 1 file").arg(title));
    QCOMPARE(dialog->findChild<pdf::PDFAppParagraph*>(QStringLiteral("toolResultSubtitle"))->text(), QStringLiteral("The new document is ready, but it is not complete."));
    QCOMPARE(dialog->findChild<pdf::PDFIconBadge*>(QStringLiteral("toolResultBadge"))->getIconName(), QStringLiteral("alert-triangle"));
    QVERIFY(!dialog->getContinueWith());
    QVERIFY(dialog->getPartialWarning());
    QCOMPARE(dialog->getPartialWarning()->findChild<pdf::PDFLabel*>(QStringLiteral("partialWarningTitle"))->text(), QStringLiteral("%1 could not be copied").arg(name));
    QVERIFY(dialog->getPartialWarning()->findChild<pdf::PDFAppParagraph*>(QStringLiteral("partialWarningText"))->text().startsWith(QStringLiteral("The file cannot be read: test")));

    bool hasLeftOut = false;
    for (int i = 0; i < dialog->getFacts()->getRowCount(); ++i)
    {
        hasLeftOut = hasLeftOut || (dialog->getFacts()->getKey(i) == QStringLiteral("Left out") && dialog->getFacts()->getValue(i) == QStringLiteral("%1 · 9 pages").arg(name));
    }
    QVERIFY(hasLeftOut);
    QCOMPARE(dialog->getStatusText(), QStringLiteral("%1 finished without 1 file · the incomplete document is waiting for your decision").arg(title));
    dialog->reject();
    AppToolOwnTabFixture::settle();
}

void AppToolOwnTabTest::blankReplacesTab()
{
    // 4.6 and K1: Blank PDF opens its document in this tab without the result dialog, unsaved,
    // titled Untitled (Save As "Untitled.pdf"); the bytes written by the run are the document
    AppToolOwnTabFixture fixture;
    std::shared_ptr<OwnTabTestTool> tool = fixture.registerOwnTabTool(QStringLiteral("convert.blank"), true, QStringLiteral("New document"));
    QVERIFY(tool);
    for (const int writtenPages : { 0, 4 })
    {
        tool->setWrittenPages(writtenPages);
        QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("convert.blank"));
        QVERIFY(tab);
        const int index = H::indexOf(fixture.window, tab);
        pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
        QVERIFY(shell->getModel()->setValue(QStringLiteral("pages"), 3).isEmpty());

        bool isDialogSeen = false;
        QTimer watcher;
        watcher.setInterval(1);
        connect(&watcher, &QTimer::timeout, &watcher, [&isDialogSeen]()
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                isDialogSeen = isDialogSeen || (qobject_cast<pdf::PDFAppToolResultDialog*>(widget) && widget->isVisible());
            }
        });
        watcher.start();
        shell->confirm();
        QVERIFY(AppToolOwnTabFixture::waitFor([&tab]() { return tab.isNull(); }));
        watcher.stop();
        QVERIFY(!isDialogSeen);

        pdf::PDFAppDocumentTab* newTab = qobject_cast<pdf::PDFAppDocumentTab*>(fixture.window->getTabs().value(index));
        QVERIFY(newTab);
        QCOMPARE(fixture.window->getCurrentTab(), newTab);
        QVERIFY(AppToolOwnTabFixture::waitFor([newTab]() { return newTab->getDocument()->isReady(); }));
        QCOMPARE(newTab->getDocument()->getTitle(), QStringLiteral("Untitled"));
        QVERIFY(newTab->getDocument()->getFilePath().isEmpty());
        QVERIFY(newTab->getDocument()->isCopy());
        QCOMPARE(newTab->getDocument()->getPageCount(), pdf::PDFInteger(writtenPages > 0 ? writtenPages : 3));
        QVERIFY(fixture.window->getToolTabs().isEmpty());
    }
}

void AppToolOwnTabTest::blankFailureKeepsTab()
{
    // 4.6: an error is a state of the panel, the tab stays with the tool
    AppToolOwnTabFixture fixture;
    QVERIFY(fixture.registerOwnTabTool(QStringLiteral("convert.blank"), true, QStringLiteral("New document")));
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("convert.blank"));
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QVERIFY(shell->getModel()->setValue(QStringLiteral("mode"), QStringLiteral("fail")).isEmpty());
    shell->confirm();
    QVERIFY(AppToolOwnTabFixture::waitFor([&tab]() { return tab && AppToolOwnTabFixture::getOwnTabGUI(tab)->notAcceptedCount == 1; }));
    AppToolOwnTabFixture::settle();
    QVERIFY(tab);
    QCOMPARE(shell->getState(), pdf::PDFDocumentToolShell::State::Editing);
    QCOMPARE(fixture.window->getDocumentTabs().size(), qsizetype(0));
}

void AppToolOwnTabTest::previewCacheOfGeneration()
{
    // K33: a tool without a document keys its previews by the generation of its inputs - the
    // same parameters after a change of an input under the same id compute again
    AppToolOwnTabFixture fixture;
    std::shared_ptr<OwnTabTestTool> tool = fixture.registerOwnTabTool(QStringLiteral("pages.merge"));
    QVERIFY(tool);
    tool->setPreview(true);
    pdf::PDFDocumentToolRunner runner(nullptr, tool, nullptr);
    pdf::PDFAppToolRunEnvironment environment;
    environment.additionalDocuments.push_back(pdf::PDFDocumentToolInputDocument::create(OwnTabTestTool::createDocument(2), QByteArray(), QString(), true));
    runner.setEnvironment(environment);
    QSignalSpy finished(&runner, &pdf::PDFDocumentToolRunner::previewFinished);
    QSignalSpy dropped(&runner, &pdf::PDFDocumentToolRunner::previewDropped);
    const QVariantMap parameters = { { QStringLiteral("mode"), QStringLiteral("new") }, { QStringLiteral("pages"), 1 } };

    runner.setInputGeneration(1);
    runner.requestPreview(parameters);
    QVERIFY(AppToolOwnTabFixture::waitFor([&finished]() { return finished.count() == 1; }));
    QCOMPARE(tool->getPreviewCount(), 1);

    // The same generation: the cache
    runner.requestPreview(parameters);
    QCOMPARE(finished.count(), 2);
    QCOMPARE(tool->getPreviewCount(), 1);

    // A new generation drops the shown preview and computes the same parameters again
    runner.setInputGeneration(2);
    QCOMPARE(dropped.count(), 1);
    QVERIFY(AppToolOwnTabFixture::waitFor([&finished]() { return finished.count() == 3; }));
    QCOMPARE(tool->getPreviewCount(), 2);
    QVERIFY(runner.getPreview());
}

void AppToolOwnTabTest::deleteTabInEveryState_data()
{
    QTest::addColumn<int>("state");
    QTest::newRow("editing") << int(pdf::PDFDocumentToolShell::State::Editing);
    QTest::newRow("running") << int(pdf::PDFDocumentToolShell::State::Running);
    QTest::newRow("deciding") << int(pdf::PDFDocumentToolShell::State::Deciding);
}

void AppToolOwnTabTest::deleteTabInEveryState()
{
    // The tab deleted directly in each state: the run is canceled, the dialog goes, the inputs
    // with the tab (the list is its child), nothing is left in the window
    QFETCH(int, state);
    AppToolOwnTabFixture fixture;
    std::shared_ptr<OwnTabTestTool> tool = fixture.registerOwnTabTool(QStringLiteral("pages.merge"));
    QVERIFY(tool);
    pdf::PDFAppDocumentTab* documentTab = H::openReady(fixture, 2);
    QVERIFY(documentTab);
    QPointer<pdf::PDFAppToolTab> tab = fixture.createToolTab(QStringLiteral("pages.merge"), H::createList({ documentTab }));
    QPointer<pdf::PDFAppToolInputList> list = tab->getInputList();
    pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
    QPointer<pdf::PDFAppToolResultDialog> dialog;
    if (state == int(pdf::PDFDocumentToolShell::State::Running))
    {
        tool->setHold(true);
        shell->confirm();
        QVERIFY(AppToolOwnTabFixture::waitFor([&tool]() { return tool->getHeldCount() == 1; }));
    }
    else if (state == int(pdf::PDFDocumentToolShell::State::Deciding))
    {
        dialog = H::runToDialog(tab);
        QVERIFY(dialog);
    }

    delete tab.data();
    AppToolOwnTabFixture::settle();
    QVERIFY(!list);
    QVERIFY(!dialog);
    QVERIFY(AppToolOwnTabFixture::waitFor([&tool]() { return tool->getHeldCount() == 0; }));
    tool->setHold(false);
    QVERIFY(fixture.window->getToolTabs().isEmpty());
    QVERIFY(!documentTab->getDocument()->isBlockedByOperation());
}

void AppToolOwnTabTest::themesAndRightToLeft()
{
    // Both themes without the leak color (a violet pixel is a bug); right to left the left
    // panel lies at the right edge and the right panel at the left edge
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        for (const Qt::LayoutDirection direction : { Qt::LeftToRight, Qt::RightToLeft })
        {
            pdf::PDFThemeManager::getInstance()->setScheme(scheme);
            QApplication::setLayoutDirection(direction);
            AppToolOwnTabFixture fixture;
            QVERIFY(fixture.registerOwnTabTool(QStringLiteral("pages.merge")));
            pdf::PDFAppToolTab* tab = fixture.createToolTab(QStringLiteral("pages.merge"));
            QVERIFY(tab);
            AppToolOwnTabFixture::settle();

            pdf::PDFAppToolTabView* view = tab->getToolPage()->getTabView();
            const QImage image = AppToolOwnTabFixture::render(view, 1.0);
            QCOMPARE(AppToolOwnTabFixture::countLeakPixels(image), 0);

            pdf::PDFDocumentToolShell* shell = AppToolOwnTabFixture::getShell(tab);
            const int leftX = shell->getLeftPanel()->mapTo(view, QPoint(0, 0)).x();
            const int rightX = shell->getRightPanel()->mapTo(view, QPoint(0, 0)).x();
            if (direction == Qt::LeftToRight)
            {
                QCOMPARE(leftX, 0);
                QCOMPARE(rightX, 1440 - 320);
            }
            else
            {
                // QSplitterPrivate::setGeo places a right-to-left widget 1 px to the right of
                // its place (the comment of PDFAppDocumentSplitter::paintEvent): the document
                // view has the same 1 px
                QVERIFY(qAbs(leftX - (1440 - 300)) <= 1);
                QCOMPARE(rightX, 0);
            }
            fixture.screenshot(QStringLiteral("pdf4qt-tool-own-tab-%1-%2").arg(scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark"),
                                                                              direction == Qt::LeftToRight ? QStringLiteral("ltr") : QStringLiteral("rtl")));
        }
    }
}

/// The test tool of the own tab registered in the registry of the tools for MCP (no GUI is
/// needed); the tool registered before comes back with the destructor
class OwnTabMcpRegistration
{
public:
    explicit OwnTabMcpRegistration(const QString& toolId) :
        m_toolId(toolId),
        m_previous(pdf::PDFDocumentToolRegistry::getTool(toolId))
    {
        pdf::PDFDocumentToolRegistry::unregisterTool(toolId);
        tool = std::make_shared<OwnTabTestTool>(toolId);
        isRegistered = pdf::PDFDocumentToolRegistry::registerTool(tool) == pdf::PDFDocumentToolRegistration::Registered;
    }

    ~OwnTabMcpRegistration()
    {
        pdf::PDFDocumentToolRegistry::unregisterTool(m_toolId);
        if (m_previous)
        {
            pdf::PDFDocumentToolRegistry::registerTool(m_previous);
        }
    }

    std::shared_ptr<OwnTabTestTool> tool;
    bool isRegistered = false;

private:
    QString m_toolId;
    std::shared_ptr<const pdf::PDFDocumentTool> m_previous;
};

/// Calls of the catalog tools over HTTP
class OwnTabMcpHelper
{
public:
    OwnTabMcpHelper() = delete;

    static QJsonObject call(pdftest::AppMcpFixture& fixture, const QString& toolName, const QString& action, QJsonObject arguments)
    {
        arguments.insert(QStringLiteral("action"), action);
        const std::optional<pdf::PDFMcpCallResult> result = fixture.callTool(toolName, arguments);
        if (!result)
        {
            return QJsonObject({ { QStringLiteral("missing"), true }, { QStringLiteral("isErrorResult"), true } });
        }
        QJsonObject answer = QJsonDocument::fromJson(result->result).object();
        answer.insert(QStringLiteral("isErrorResult"), result->isError);
        return answer;
    }

    static QString text(const QJsonObject& answer)
    {
        QString value;
        for (const QJsonValue& block : answer.value(QStringLiteral("content")).toArray())
        {
            value += block.toObject().value(QStringLiteral("text")).toString();
        }
        return value;
    }

    static pdf::PDFAppDocumentTab* findTab(pdf::PDFAppMainWindow* window, const QString& documentId)
    {
        for (pdf::PDFAppDocumentTab* tab : window->getDocumentTabs())
        {
            if (tab->getDocument() && tab->getDocument()->getId() == documentId)
            {
                return tab;
            }
        }
        return nullptr;
    }
};

void AppToolOwnTabTest::mcpNewDocumentOverHttp()
{
    // Plan 7, R20, K34: documentIds in the order of the result (at least two, checked by the
    // bridge), open_new_tab in the window of the first input after its tab, save_file only
    // .pdf and never over an existing file; a tool without an input (Blank) the same
    using M = OwnTabMcpHelper;
    pdftest::AppMcpFixture fixture;
    QVERIFY(fixture.setPermissions(pdf::PDFAppMcpPermission::Allow));
    OwnTabMcpRegistration merge(QStringLiteral("pages.merge"));
    OwnTabMcpRegistration blank(QStringLiteral("convert.blank"));
    QVERIFY(merge.isRegistered && blank.isRegistered);
    QVERIFY(fixture.startServer());
    fixture.setTransport(pdftest::AppMcpTransport::Http);

    pdf::PDFAppDocumentTab* first = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("first.pdf"), 3));
    pdf::PDFAppDocumentTab* second = fixture.open(pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("second.pdf"), 2));
    QVERIFY(first && second);
    QVERIFY(AppToolOwnTabFixture::waitFor([first, second]() { return first->getDocument()->isReady() && second->getDocument()->isReady(); }));
    const QString firstId = first->getDocument()->getId();
    const QString secondId = second->getDocument()->getId();
    const QString openNewTab = QStringLiteral("open_new_tab");

    // One document is too few, an unknown id names its index, documentId is not the input
    QJsonObject answer = M::call(fixture, QStringLiteral("pages"), QStringLiteral("merge"), { { QStringLiteral("documentIds"), QJsonArray({ firstId }) }, { QStringLiteral("result"), openNewTab } });
    QVERIFY(answer.value(QStringLiteral("isErrorResult")).toBool());
    // K41 of step 3: the bounds of Merge (2 to 1000 inputs, PDFDocumentToolMerge::MAXIMUM_INPUT_COUNT)
    QVERIFY2(M::text(answer).contains(QStringLiteral("2 to 1000 open documents")), qPrintable(M::text(answer)));
    answer = M::call(fixture, QStringLiteral("pages"), QStringLiteral("merge"), { { QStringLiteral("documentIds"), QJsonArray({ firstId, pdf::PDFAppDocument::createId() }) }, { QStringLiteral("result"), openNewTab } });
    QVERIFY(answer.value(QStringLiteral("isErrorResult")).toBool());
    QVERIFY2(M::text(answer).startsWith(QStringLiteral("documentIds[1]: ")), qPrintable(M::text(answer)));
    answer = M::call(fixture, QStringLiteral("pages"), QStringLiteral("merge"), { { QStringLiteral("documentId"), firstId }, { QStringLiteral("result"), openNewTab } });
    QVERIFY(answer.value(QStringLiteral("isErrorResult")).toBool());
    QCOMPARE(merge.tool->getExecuteCount(), 0);

    // open_new_tab: the inputs in their order (the second one first), the new tab after the
    // tab of the first input, its document named in the answer
    const int tabCount = int(fixture.window->getTabs().size());
    answer = M::call(fixture, QStringLiteral("pages"), QStringLiteral("merge"), { { QStringLiteral("documentIds"), QJsonArray({ secondId, firstId }) }, { QStringLiteral("result"), openNewTab } });
    QVERIFY2(!answer.value(QStringLiteral("isErrorResult")).toBool(), qPrintable(M::text(answer)));
    QCOMPARE(merge.tool->getLastInputCount(), 2);
    const QString newId = answer.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("merge")).toObject().value(QStringLiteral("documentId")).toString();
    QVERIFY(!newId.isEmpty() && newId != firstId && newId != secondId);
    QCOMPARE(int(fixture.window->getTabs().size()), tabCount + 1);
    pdf::PDFAppDocumentTab* newTab = M::findTab(fixture.window, newId);
    QVERIFY(newTab);
    QCOMPARE(H::indexOf(fixture.window, newTab), H::indexOf(fixture.window, second) + 1);
    QVERIFY(AppToolOwnTabFixture::waitFor([newTab]() { return newTab->getDocument()->isReady(); }));
    QCOMPARE(newTab->getDocument()->getPageCount(), pdf::PDFInteger(5));
    QVERIFY(newTab->getDocument()->getFilePath().isEmpty());

    // save_file: a .pdf of the whole result, never over an existing file, never another extension
    const QString path = fixture.path(QStringLiteral("merged-by-assistant.pdf"));
    answer = M::call(fixture, QStringLiteral("pages"), QStringLiteral("merge"), { { QStringLiteral("documentIds"), QJsonArray({ firstId, secondId }) }, { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), path } });
    QVERIFY2(!answer.value(QStringLiteral("isErrorResult")).toBool(), qPrintable(M::text(answer)));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    pdf::PDFDocumentReader reader(nullptr, nullptr, true, false);
    const pdf::PDFDocument written = reader.readFromBuffer(file.readAll());
    QCOMPARE(written.getCatalog()->getPageCount(), size_t(5));
    file.close();

    const int executions = merge.tool->getExecuteCount();
    answer = M::call(fixture, QStringLiteral("pages"), QStringLiteral("merge"), { { QStringLiteral("documentIds"), QJsonArray({ firstId, secondId }) }, { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), path } });
    QVERIFY(answer.value(QStringLiteral("isErrorResult")).toBool());
    answer = M::call(fixture, QStringLiteral("pages"), QStringLiteral("merge"), { { QStringLiteral("documentIds"), QJsonArray({ firstId, secondId }) }, { QStringLiteral("result"), QStringLiteral("save_file") }, { QStringLiteral("path"), fixture.path(QStringLiteral("merged.bat")) } });
    QVERIFY(answer.value(QStringLiteral("isErrorResult")).toBool());
    QVERIFY(!QFile::exists(fixture.path(QStringLiteral("merged.bat"))));
    QCOMPARE(merge.tool->getExecuteCount(), executions);

    // Blank PDF: no document, open_new_tab in the active window
    answer = M::call(fixture, QStringLiteral("convert"), QStringLiteral("blank"), { { QStringLiteral("result"), openNewTab }, { QStringLiteral("blank"), QJsonObject({ { QStringLiteral("pages"), 4 } }) } });
    QVERIFY2(!answer.value(QStringLiteral("isErrorResult")).toBool(), qPrintable(M::text(answer)));
    QCOMPARE(blank.tool->getLastInputCount(), 0);
    const QString blankId = answer.value(QStringLiteral("structuredContent")).toObject().value(QStringLiteral("blank")).toObject().value(QStringLiteral("documentId")).toString();
    pdf::PDFAppDocumentTab* blankTab = M::findTab(fixture.window, blankId);
    QVERIFY(blankTab);
    QVERIFY(AppToolOwnTabFixture::waitFor([blankTab]() { return blankTab->getDocument()->isReady(); }));
    QCOMPARE(blankTab->getDocument()->getPageCount(), pdf::PDFInteger(4));
}

QTEST_MAIN(AppToolOwnTabTest)

#include "tst_apptoolowntabtest.moc"
