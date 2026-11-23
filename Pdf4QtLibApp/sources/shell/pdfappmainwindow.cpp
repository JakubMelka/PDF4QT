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

#include "pdfappmainwindow.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentdialogs.h"
#include "pdfappdocumentregistry.h"
#include "pdfappdocumentsave.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfapphomewidgets.h"
#include "pdfappnavigator.h"
#include "pdfappsessionend.h"
#include "pdfapptab.h"
#include "pdfappassemblyloader.h"
#include "pdfapptooldocumentpage.h"
#include "pdfapptoolgui_compare.h"
#include "pdfapptoolgui_manage.h"
#include "pdfapptoolhost.h"
#include "pdfapptoolinputlist.h"
#include "pdfapptooltab.h"
#include "pdfapptopbar.h"
#include "pdfappwindowframe.h"
#include "pdfappwindowgeometry.h"
#include "pdfdatabasenotifier.h"
#include "pdfrecentassemblyrepository.h"
#include "pdfsettingdefinitions.h"
#include "pdfshortcutrepository.h"
#include "pdfworkspacerepository.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttoolguiregistry.h"
#include "pdfdocumenttoolshell.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentwindow.h"
#include "pdfdeferreddeletion.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include "pdfannotationmanipulator.h"
#include "pdfdocumenttool_manage.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QMimeData>
#include <QScreen>
#include <QStyle>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

#include <limits>
#include <memory>

namespace pdf
{

/// Helpers of the main window
class PDFAppMainWindowHelper
{
public:
    PDFAppMainWindowHelper() = delete;

    /// The main window of the process, which was active last (the thread of the
    /// user interface only): the window of the quit, when no window is active (O29)
    static QPointer<PDFAppMainWindow>& getLastActiveWindow()
    {
        static QPointer<PDFAppMainWindow> window;
        return window;
    }

    /// Geometry of the new window: the size of the frames of the design (1440 x 977),
    /// at most 90 % of the screen of the pointer, centered on it. The position is
    /// set: Qt does not center a window larger than 8 / 9 of the screen, the system
    /// placed such a window beyond the screens (Windows, two monitors at 200 %).
    static QRect getInitialGeometry(const QWidget* window)
    {
        QSize size(1440, 977);
        const QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen)
        {
            screen = window ? window->screen() : QGuiApplication::primaryScreen();
        }

        if (!screen)
        {
            return QRect(QPoint(0, 0), size.expandedTo(window ? window->minimumSize() : QSize()));
        }

        const QRect available = screen->availableGeometry();
        size = size.boundedTo(QSize(available.width() * 9 / 10, available.height() * 9 / 10));
        size = size.expandedTo(window ? window->minimumSize() : QSize());
        QRect geometry(QPoint(0, 0), size);
        geometry.moveCenter(available.center());
        return geometry;
    }

    /// Text of a list of files or errors for a message box (bounded)
    static QString createList(const QStringList& items, int maximumItems)
    {
        QStringList lines;
        for (int i = 0; i < items.size() && i < maximumItems; ++i)
        {
            lines << QDir::toNativeSeparators(items[i].left(1024));
        }
        if (items.size() > maximumItems)
        {
            lines << QCoreApplication::translate("pdf::PDFAppMainWindow", "… and %1 more").arg(items.size() - maximumItems);
        }
        return lines.join(QLatin1Char('\n'));
    }
};

PDFAppMainWindow::PDFAppMainWindow(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent, Qt::Window),
    m_context(context)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("PDF4QT"));
    setMinimumSize(720, 480);

    // PDF files dropped anywhere onto the window are opened (step 2)
    setAcceptDrops(true);
    applyInitialGeometry();

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_topBar = new PDFAppTopBar(this);
    layout->addWidget(m_topBar);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("tabContent"));
    layout->addWidget(m_stack, 1);

    m_navigator = new PDFAppNavigator(this, context);
    m_frame = new PDFAppWindowFrame(this, m_topBar);

    PDFDocumentTabBar* tabBar = m_topBar->getTabBar();
    connect(tabBar, &QTabBar::currentChanged, this, &PDFAppMainWindow::onCurrentTabChanged);
    connect(tabBar, &QTabBar::tabCloseRequested, this, &PDFAppMainWindow::onTabCloseRequested);
    connect(tabBar, &PDFDocumentTabBar::lockClicked, this, &PDFAppMainWindow::onLockClicked);
    connect(m_topBar, &PDFAppTopBar::newTabRequested, this, &PDFAppMainWindow::onNewTabRequested);
    connect(m_topBar, &PDFAppTopBar::maximizeRestoreRequested, this, &PDFAppMainWindow::onMaximizeRestoreRequested);
    connect(m_topBar, &PDFAppTopBar::moveRequested, this, &PDFAppMainWindow::onMoveRequested);

    PDFWindowControls* controls = m_topBar->getWindowControls();
    connect(controls, &PDFWindowControls::minimizeRequested, this, &PDFAppMainWindow::onMinimizeRequested);
    connect(controls, &PDFWindowControls::maximizeRestoreRequested, this, &PDFAppMainWindow::onMaximizeRestoreRequested);
    connect(controls, &PDFWindowControls::closeRequested, this, &PDFAppMainWindow::onCloseRequested);

    createCommands();
    updateShortcuts();
    updateCommandStates();

    // The senders outside the window (the context, the application, the theme): step A
    if (context)
    {
        if (PDFDatabaseNotifier* notifier = context->getNotifier())
        {
            PDFDeferredDeletion::connect(notifier, &PDFDatabaseNotifier::changed, this, &PDFAppMainWindow::onDatabaseChanged);
            PDFDeferredDeletion::connect(notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppMainWindow::onEverythingChanged);
        }
        PDFDeferredDeletion::connect(context, &PDFAppContext::openRequested, this, &PDFAppMainWindow::onOpenRequested);
    }

    // The geometry is stored also, when the application quits without the closing
    // of the window (QCoreApplication::quit, the end of the session of the system)
    PDFDeferredDeletion::connect(qApp, &QCoreApplication::aboutToQuit, this, &PDFAppMainWindow::onAboutToQuit);

    // The end of the session of the system asks about the unsaved changes of all
    // windows (step 9, N7): one handler of the process
    PDFAppSessionEnd::install();
    PDFDeferredDeletion::connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppMainWindow::update));

    // The Home tab with its Home page
    getHomeTab();
    m_navigator->navigate(PDFAppRoutes::home());

    m_frame->initialize();
    updateWindowState();

    // Files and errors of the command line are shown, when the window exists
    QMetaObject::invokeMethod(this, &PDFAppMainWindow::processCommandLine, Qt::QueuedConnection);
}

PDFAppMainWindow::~PDFAppMainWindow()
{
    // The QWindow lives until ~QWidget: it does not call the filter of a window,
    // which is being destroyed
    if (m_filteredWindow)
    {
        m_filteredWindow->removeEventFilter(this);
        m_filteredWindow = nullptr;
    }
    if (m_watchedModal)
    {
        m_watchedModal->removeEventFilter(this);
        m_watchedModal->disconnect(this);
        m_watchedModal = nullptr;
    }

    // The navigator (a child) does not use the window any more, the tabs are
    // deleted while the window is whole (their signals reach it)
    if (m_navigator)
    {
        m_navigator->detachWindow();
    }
    for (const QPointer<PDFAppTab>& tab : std::as_const(m_tabs))
    {
        if (tab)
        {
            tab->disconnect(this);
        }
    }
    m_tabIds.clear();
    delete m_stack.data();
    delete m_frame.data();
}

PDFDocumentTabBar* PDFAppMainWindow::getTabBar() const
{
    return m_topBar ? m_topBar->getTabBar() : nullptr;
}

PDFAppWorkspaceTab* PDFAppMainWindow::getHomeTab()
{
    if (!m_homeTab && m_stack && getTabBar())
    {
        PDFAppWorkspaceTab* homeTab = new PDFAppWorkspaceTab(PDFAppTabKind::Home, m_context, m_stack);
        m_homeTab = homeTab;
        addTab(homeTab, 0);
    }
    return m_homeTab;
}

PDFAppTab* PDFAppMainWindow::getCurrentTab() const
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tabBar)
    {
        return nullptr;
    }
    return findTab(tabBar->tabData(tabBar->currentIndex()).toULongLong());
}

QList<PDFAppTab*> PDFAppMainWindow::getTabs() const
{
    QList<PDFAppTab*> tabs;
    if (PDFDocumentTabBar* tabBar = getTabBar())
    {
        for (int i = 0; i < tabBar->count(); ++i)
        {
            if (PDFAppTab* tab = findTab(tabBar->tabData(i).toULongLong()))
            {
                tabs << tab;
            }
        }
    }
    return tabs;
}

PDFAppTab* PDFAppMainWindow::findTab(quint64 id) const
{
    for (const QPointer<PDFAppTab>& tab : m_tabs)
    {
        if (tab && tab->getId() == id)
        {
            return tab;
        }
    }
    return nullptr;
}

int PDFAppMainWindow::findTabIndex(quint64 id) const
{
    if (PDFDocumentTabBar* tabBar = getTabBar())
    {
        for (int i = 0; i < tabBar->count(); ++i)
        {
            if (tabBar->tabData(i).toULongLong() == id)
            {
                return i;
            }
        }
    }
    return -1;
}

PDFAppTab* PDFAppMainWindow::addTab(PDFAppTab* tab, int index)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tab || !tabBar || !m_stack)
    {
        return nullptr;
    }

    m_stack->addWidget(tab);
    m_tabs.append(tab);
    m_tabIds[tab] = tab->getId();

    connect(tab, &QObject::destroyed, this, &PDFAppMainWindow::onTabDestroyed);
    // A tab retired by somebody else than requestCloseTab (step A) leaves the lists at once, never current again
    connect(PDFDeferredDeletion::getNotifier(tab), &PDFLifetimeNotifier::aboutToBeDeleted, this, &PDFAppMainWindow::onTabAboutToBeDeleted);
    connect(tab, &PDFAppTab::pageTitleChanged, this, &PDFAppMainWindow::onPageTitleChanged);
    connect(tab, &PDFAppTab::commandStatesChanged, this, &PDFAppMainWindow::onTabCommandStatesChanged);
    if (m_navigator)
    {
        m_navigator->connectTab(tab);
    }

    // The tab bar may make the new tab current: the data of the tab are set first
    const QSignalBlocker blocker(tabBar);
    const int tabIndex = tabBar->insertTab(qBound(0, index, tabBar->count()), PDFThemeIcons::getIcon(tab->getTabIconName()), tab->getTabTitle());
    tabBar->setTabData(tabIndex, tab->getId());
    tabBar->setTabClosable(tabIndex, tab->isClosable());
    tabBar->setTabToolTip(tabIndex, tab->getTabTitle());

    // The first tab is current (the tab bar has not told it while blocked)
    if (tabBar->count() == 1)
    {
        m_stack->setCurrentWidget(tab);
    }
    return tab;
}

PDFAppTab* PDFAppMainWindow::createNewTab()
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tabBar || !m_stack || tabBar->count() >= MAXIMUM_TABS)
    {
        return nullptr;
    }

    PDFAppWorkspaceTab* tab = new PDFAppWorkspaceTab(PDFAppTabKind::NewTab, m_context, m_stack);
    return addTab(tab, tabBar->currentIndex() + 1);
}

PDFAppTab* PDFAppMainWindow::findHelpTab() const
{
    for (const QPointer<PDFAppTab>& tab : m_tabs)
    {
        if (tab && tab->getKind() == PDFAppTabKind::Help)
        {
            return tab;
        }
    }
    return nullptr;
}

PDFAppTab* PDFAppMainWindow::createHelpTab()
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tabBar || !m_stack || tabBar->count() >= MAXIMUM_TABS)
    {
        return nullptr;
    }

    PDFAppWorkspaceTab* tab = new PDFAppWorkspaceTab(PDFAppTabKind::Help, m_context, m_stack);
    return addTab(tab, tabBar->currentIndex() + 1);
}

PDFAppTab* PDFAppMainWindow::openNewTab()
{
    if (!m_navigator)
    {
        return nullptr;
    }

    QPointer<PDFAppMainWindow> guard(this);
    m_navigator->navigate(PDFAppRoutes::open(), PDFAppNavigationMode::NewTab);
    if (!guard)
    {
        return nullptr;
    }

    PDFAppTab* tab = getCurrentTab();
    return tab && tab->getKind() == PDFAppTabKind::NewTab ? tab : nullptr;
}

void PDFAppMainWindow::activateTab(PDFAppTab* tab)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tab || !tabBar || PDFDeferredDeletion::isRetired(tab))
    {
        return;
    }

    const int index = findTabIndex(tab->getId());
    if (index >= 0 && index != tabBar->currentIndex())
    {
        tabBar->setCurrentIndex(index);
    }
}

bool PDFAppMainWindow::closeTab(PDFAppTab* tab)
{
    return requestCloseTab(tab) == PDFAppLeaveResult::Done;
}

PDFAppLeaveResult PDFAppMainWindow::requestCloseTab(PDFAppTab* tab)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tab || !tabBar || !tab->isClosable() || PDFDeferredDeletion::isRetired(tab))
    {
        return PDFAppLeaveResult::Refused;
    }

    if (findTabIndex(tab->getId()) < 0)
    {
        return PDFAppLeaveResult::Refused;
    }

    // The tab asks about its unsaved changes and running operations (step 3) in its flow (A0: the questions are
    // shown, no event loop; Pending = the tab closes itself after the last answer); the tab runs code of the
    // application, the tab or the window may be deleted meanwhile
    QPointer<PDFAppMainWindow> windowGuard(this);
    QPointer<PDFAppTab> tabGuard(tab);
    const PDFAppLeaveResult result = tab->requestTabClose();
    if (!windowGuard || !tabGuard)
    {
        return PDFAppLeaveResult::Refused;
    }
    if (result != PDFAppLeaveResult::Done)
    {
        return result;
    }

    if (findTabIndex(tab->getId()) < 0 || PDFDeferredDeletion::isRetired(tab))
    {
        return PDFAppLeaveResult::Refused;
    }

    // The page records what it must while the tab is whole (the use of an interactive tool; no signal)
    prepareTabClose(tab);

    // The tab leaves the lists, the tab bar and the stack, the focus goes to the new current tab; then it is
    // retired (step A): inert at once, deleted later (it may be the sender of the request - its close button, its
    // page). A close from a signal of the tab's own subtree is queued (onDocumentLoadFailed): retire cuts the rest
    // of the emission.
    QPointer<PDFAppMainWindow> guard(this);
    QPointer<PDFAppTab> closedTab(tab);
    detachTab(tab);
    if (!guard)
    {
        return PDFAppLeaveResult::Done;
    }
    PDFDeferredDeletion::retire(closedTab.data());
    updateWindowTitle();
    return PDFAppLeaveResult::Done;
}

void PDFAppMainWindow::detachTab(PDFAppTab* tab)
{
    m_tabIds.erase(tab);
    for (qsizetype i = m_tabs.size() - 1; i >= 0; --i)
    {
        if (m_tabs[i].isNull() || m_tabs[i].data() == tab)
        {
            m_tabs.removeAt(i);
        }
    }
    tab->disconnect(this);

    // The tab bar makes another tab current (its slot shows it in the stack, which takes the focus over from the
    // detached tab); the slot runs code of the window and of the tabs
    QPointer<PDFAppMainWindow> guard(this);
    QPointer<PDFAppTab> tabGuard(tab);
    const int index = findTabIndex(tab->getId());
    if (PDFDocumentTabBar* tabBar = getTabBar(); tabBar && index >= 0)
    {
        tabBar->removeTab(index);
    }
    if (!guard || !tabGuard)
    {
        return;
    }

    if (m_stack)
    {
        m_stack->removeWidget(tab);

        // A focus left in the detached tab (the stack has not moved it) goes to the current tab, as the stack
        // moves it, before retire clears it
        QWidget* focusWidget = QApplication::focusWidget();
        QWidget* current = m_stack->currentWidget();
        if (focusWidget && tab->isAncestorOf(focusWidget) && current && current != tab)
        {
            QWidget* next = current->focusWidget();
            (next ? next : current)->setFocus(Qt::OtherFocusReason);
        }
    }
}

void PDFAppMainWindow::onTabAboutToBeDeleted(QObject* object)
{
    // A tab retired by somebody else (step A): it is whole (retire emits before the shutdown), it leaves the lists,
    // the tab bar and the stack at once. The fallback emission of a tab deleted without retire comes from its
    // destructor: only onTabDestroyed handles that one (the tab is not called).
    if (m_tabIds.find(object) == m_tabIds.end() || !PDFDeferredDeletion::isRetired(object))
    {
        return;
    }

    PDFAppTab* tab = qobject_cast<PDFAppTab*>(object);
    if (!tab)
    {
        return;
    }
    QPointer<PDFAppMainWindow> guard(this);
    detachTab(tab);
    if (guard)
    {
        updateWindowTitle();
    }
}

PDFAppToolTab* PDFAppMainWindow::addToolTab(const QString& toolId, const QString& instanceId, PDFAppToolInputList* inputList)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tabBar || !m_stack || tabBar->count() >= MAXIMUM_TABS || !PDFAppToolTabPageFactory::hasOwnTabPage(toolId) ||
        !PDFAppRouteValues::documentId().accepts(instanceId) || findToolTab(instanceId))
    {
        return nullptr;
    }

    // The tab takes the list over in this call (R13)
    PDFAppToolTab* tab = new PDFAppToolTab(m_context, toolId, instanceId, inputList, m_stack);
    connect(tab, &PDFAppToolTab::tabStateChanged, this, &PDFAppMainWindow::onToolTabStateChanged);
    addTab(tab, tabBar->currentIndex() + 1);
    return tab;
}

PDFAppToolTab* PDFAppMainWindow::createToolTab(const QString& toolId, PDFAppToolInputList* inputList)
{
    // One instance of the own tab of a tool, which says so (Manage Certificates, plan of step 6 of
    // phase 2, 4.5): a second start activates the tab; the list stays with the caller
    if (PDFAppToolTab* singleTab = findSingleToolTab(toolId))
    {
        // The activation runs code of the pages (they may delete the tab or the window)
        QPointer<PDFAppToolTab> tab(singleTab);
        QPointer<PDFAppMainWindow> guard(this);
        activateTab(singleTab);
        return guard ? tab.data() : nullptr;
    }

    PDFAppToolTab* createdTab = addToolTab(toolId, PDFAppDocument::createId(), inputList);
    if (!createdTab)
    {
        return nullptr;
    }

    // The route of the tab is shown in it: code of the tool runs, the tab or the window may
    // be deleted meanwhile. Within a navigation (a page asks for the tool from its setRoute)
    // the navigator would keep only its last request: the tab asks from the event loop.
    QPointer<PDFAppToolTab> tab(createdTab);
    QPointer<PDFAppMainWindow> guard(this);
    if (m_navigator && m_navigator->isNavigating())
    {
        QMetaObject::invokeMethod(tab.data(), &PDFAppToolTab::showRoute, Qt::QueuedConnection);
    }
    else if (m_navigator)
    {
        m_navigator->navigate(tab->getRoute(), PDFAppNavigationMode::Push, tab);
    }
    return guard ? tab.data() : nullptr;
}

PDFAppToolTab* PDFAppMainWindow::createToolTabForDocument(const QString& toolId, PDFAppDocumentTab* documentTab)
{
    // A tab, which is being closed (retired, step A), starts no tool
    if (documentTab && PDFDeferredDeletion::isRetired(documentTab))
    {
        return nullptr;
    }

    PDFAppToolInputList* inputList = new PDFAppToolInputList(nullptr);
    if (documentTab && documentTab->getDocument())
    {
        inputList->addOpenDocument(documentTab->getDocument());
    }

    // The list goes to the tab in one call (R13); not taken by a tab, it stays here
    QPointer<PDFAppToolInputList> listGuard(inputList);
    PDFAppToolTab* tab = createToolTab(toolId, inputList);
    if (listGuard && !listGuard->parent())
    {
        delete listGuard.data();
    }
    return tab;
}

PDFAppToolTab* PDFAppMainWindow::findSingleToolTab(const QString& toolId) const
{
    // The GUI of the tool is made only for the answer (no parent, no shell, never shown): retired at once, deleted
    // later (step A: no synchronous deletion of an object with a hook)
    const std::shared_ptr<const PDFDocumentToolGUIFactory> factory = PDFDocumentToolGUIRegistry::getFactory(toolId);
    PDFDocumentToolGUI* gui = factory ? factory->createGUI() : nullptr;
    const bool isSingleOwnTab = gui && gui->isSingleOwnTab();
    PDFDeferredDeletion::retire(gui);
    if (!isSingleOwnTab)
    {
        return nullptr;
    }

    // An instance is a tab with its tool: a tab, whose page has lost its shell, is no instance
    for (PDFAppToolTab* tab : getToolTabs())
    {
        if (tab && tab->getToolId() == toolId && tab->getShell())
        {
            return tab;
        }
    }
    return nullptr;
}

PDFAppToolTab* PDFAppMainWindow::findToolTab(const QString& instanceId) const
{
    if (instanceId.isEmpty())
    {
        return nullptr;
    }

    for (const QPointer<PDFAppTab>& tab : m_tabs)
    {
        PDFAppToolTab* toolTab = qobject_cast<PDFAppToolTab*>(tab.data());
        if (toolTab && toolTab->getInstanceId() == instanceId)
        {
            return toolTab;
        }
    }
    return nullptr;
}

QList<PDFAppToolTab*> PDFAppMainWindow::getToolTabs() const
{
    QList<PDFAppToolTab*> toolTabs;
    for (PDFAppTab* tab : getTabs())
    {
        if (PDFAppToolTab* toolTab = qobject_cast<PDFAppToolTab*>(tab))
        {
            toolTabs << toolTab;
        }
    }
    return toolTabs;
}

PDFAppDocumentTab* PDFAppMainWindow::replaceToolTab(PDFAppToolTab* tab, QByteArray bytes, const QString& title)
{
    const int index = tab ? findTabIndex(tab->getId()) : -1;
    if (index < 0)
    {
        return nullptr;
    }

    // The document tab first at the place of the tool tab (pushed right of it), then the tool
    // tab goes: the new tab takes its index. The tool tab may be the sender (deleteLater).
    QPointer<PDFAppToolTab> toolTab(tab);
    QPointer<PDFAppMainWindow> guard(this);
    PDFAppDocumentTab* documentTab = openDocumentFromMemoryAt(std::move(bytes), title, QString(), index);
    if (!guard)
    {
        return nullptr;
    }
    QPointer<PDFAppDocumentTab> newTab(documentTab);
    if (toolTab && newTab)
    {
        toolTab->setCloseConfirmed(true);
        closeTab(toolTab);
    }
    return guard ? newTab.data() : nullptr;
}

PDFAppTab* PDFAppMainWindow::findDocumentTab(const QString& id) const
{
    if (id.isEmpty())
    {
        return nullptr;
    }

    for (const QPointer<PDFAppTab>& tab : m_tabs)
    {
        PDFAppDocumentTab* documentTab = qobject_cast<PDFAppDocumentTab*>(tab.data());
        if (documentTab && documentTab->getDocumentId() == id)
        {
            return documentTab;
        }
    }
    return nullptr;
}

QList<PDFAppDocumentTab*> PDFAppMainWindow::getDocumentTabs() const
{
    QList<PDFAppDocumentTab*> documentTabs;
    for (PDFAppTab* tab : getTabs())
    {
        if (PDFAppDocumentTab* documentTab = qobject_cast<PDFAppDocumentTab*>(tab))
        {
            documentTabs << documentTab;
        }
    }
    return documentTabs;
}

void PDFAppMainWindow::updateTab(PDFAppTab* tab)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tab || !tabBar)
    {
        return;
    }

    const int index = findTabIndex(tab->getId());
    if (index >= 0)
    {
        tabBar->setTabText(index, tab->getTabTitle());
        tabBar->setTabToolTip(index, tab->getTabTitle());

        // A document tab: the lock, the dot of unsaved changes, the password icon, the progress
        if (const PDFAppDocumentTab* documentTab = qobject_cast<const PDFAppDocumentTab*>(tab))
        {
            tabBar->setTabLocked(index, documentTab->isTabLocked());
            tabBar->setTabUnsaved(index, documentTab->isTabUnsaved());
            tabBar->setTabPasswordProtected(index, documentTab->isTabPasswordProtected());
            tabBar->setTabProgress(index, documentTab->getTabProgress());
        }

        // The own tab of a tool: its icon from its GUI, the dot of the list (K16), the
        // progress of a run (tools.md 9.3 point 15)
        if (const PDFAppToolTab* toolTab = qobject_cast<const PDFAppToolTab*>(tab))
        {
            tabBar->setTabIcon(index, PDFThemeIcons::getIcon(toolTab->getTabIconName()));
            tabBar->setTabUnsaved(index, toolTab->isTabUnsaved());
            tabBar->setTabProgress(index, toolTab->getTabProgress());
        }
    }

    if (tab == getCurrentTab())
    {
        updateWindowTitle();
    }
}

void PDFAppMainWindow::updateWindowTitle()
{
    PDFAppTab* tab = getCurrentTab();
    PDFAppPage* page = tab ? tab->getPage() : nullptr;
    const QString pageTitle = page ? page->getTitle() : QString();
    setWindowTitle(pageTitle.isEmpty() ? tr("PDF4QT") : tr("%1 – PDF4QT").arg(pageTitle));
}

QAction* PDFAppMainWindow::getCommandAction(const QString& commandId) const
{
    auto it = m_actions.find(commandId);
    return it != m_actions.end() ? it->second.data() : nullptr;
}

void PDFAppMainWindow::createCommands()
{
    const PDFAppCommandRegistry* registry = m_context ? m_context->getCommandRegistry() : nullptr;
    if (!registry)
    {
        return;
    }

    // An action for every command: the commands of the window (step 1, 2) are done
    // by the window, the commands of the documents (step 3, scope Document and
    // Canvas) are dispatched to the current tab. A Canvas command has its shortcut
    // on the canvas of the document view (Qt::WidgetWithChildrenShortcut), its
    // action of the window has none (the global search and the menus run it).
    for (const PDFAppCommand* command : registry->getCommands())
    {
        const QString id = command->id;
        QAction* action = new QAction(command->getTitle(), this);
        action->setObjectName(id);
        action->setData(id);
        action->setShortcutContext(Qt::WindowShortcut);
        action->setCheckable(command->isCheckable);
        addAction(action);
        connect(action, &QAction::triggered, this, &PDFAppMainWindow::onCommandTriggered);
        m_actions[id] = action;
    }

    updateActionIcons();
}

void PDFAppMainWindow::updateActionIcons()
{
    const PDFAppCommandRegistry* registry = m_context ? m_context->getCommandRegistry() : nullptr;
    if (!registry)
    {
        return;
    }

    // Directional icons (Back, Forward, Next tab, Previous tab) are mirrored in
    // right-to-left, Qt does not mirror icons
    for (const auto& item : m_actions)
    {
        const PDFAppCommand* command = registry->findCommand(item.first);
        if (item.second && command && !command->iconName.isEmpty())
        {
            item.second->setIcon(PDFThemeIcons::getIcon(PDFAppPage::getDirectionalIconName(command->iconName, layoutDirection())));
        }
    }
}

void PDFAppMainWindow::updateShortcuts()
{
    const PDFAppCommandRegistry* registry = m_context ? m_context->getCommandRegistry() : nullptr;
    if (!registry)
    {
        return;
    }

    PDFAppCommandRegistry::UserShortcuts userShortcuts;
    if (m_context->getDatabase())
    {
        PDFDatabaseError error;
        userShortcuts = m_context->getShortcuts().getShortcuts(&error);
        if (error.isError())
        {
            qWarning("Shortcuts of the user cannot be read: %s", qPrintable(error.toString()));
        }
    }

    for (const auto& item : m_actions)
    {
        const PDFAppCommand* command = registry->findCommand(item.first);
        if (item.second && command)
        {
            // Canvas and Panel keys are actions of the canvas / of the panel
            const bool isLocal = command->scope == PDFAppCommandScope::Canvas || command->scope == PDFAppCommandScope::Panel;
            item.second->setShortcuts(isLocal ? QList<QKeySequence>() : registry->getShortcuts(item.first, userShortcuts));
        }
    }
}

void PDFAppMainWindow::updateCommandStates()
{
    const PDFAppCommandRegistry* registry = m_context ? m_context->getCommandRegistry() : nullptr;
    if (!registry)
    {
        return;
    }

    // The commands of the documents follow the current tab (a tab without a
    // document disables them); the commands of the window are always enabled
    PDFAppTab* tab = getCurrentTab();
    for (const auto& item : m_actions)
    {
        const PDFAppCommand* command = registry->findCommand(item.first);
        if (!item.second || !command || command->scope == PDFAppCommandScope::Window)
        {
            continue;
        }

        const QSignalBlocker blocker(item.second.data());
        item.second->setEnabled(tab && tab->isCommandEnabled(item.first));
        if (item.second->isCheckable())
        {
            item.second->setChecked(tab && tab->isCommandChecked(item.first));
        }
    }
}

void PDFAppMainWindow::onCommandTriggered()
{
    // The actions do not identify themselves in the signal
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action)
    {
        return;
    }

    const QString id = action->data().toString();
    PDFDocumentTabBar* tabBar = getTabBar();

    if (id == PDFAppCommands::newTab())
    {
        openNewTab();
    }
    else if (id == PDFAppCommands::closeTab())
    {
        closeTabByUser(getCurrentTab());
    }
    else if ((id == PDFAppCommands::nextTab() || id == PDFAppCommands::previousTab()) && tabBar && tabBar->count() > 0)
    {
        const int step = id == PDFAppCommands::nextTab() ? 1 : -1;
        tabBar->setCurrentIndex((tabBar->currentIndex() + step + tabBar->count()) % tabBar->count());
    }
    else if (id == PDFAppCommands::quit())
    {
        close();
    }
    else if (id == PDFAppCommands::open())
    {
        browseDocuments();
    }
    else if (id == PDFAppCommands::back() && m_navigator)
    {
        m_navigator->goBack();
    }
    else if (id == PDFAppCommands::forward() && m_navigator)
    {
        m_navigator->goForward();
    }
    else if (id == PDFAppCommands::search())
    {
        focusSearch();
    }
    else if (id == PDFAppCommands::help())
    {
        showHelp();
    }
    else if (PDFAppTab* tab = getCurrentTab())
    {
        // A command of the documents (step 3): the current tab runs it (it may
        // delete the tab or the window)
        QPointer<PDFAppMainWindow> guard(this);
        tab->executeCommand(id);
        if (guard)
        {
            updateCommandStates();
        }
    }
}

void PDFAppMainWindow::showHelp()
{
    if (!m_navigator)
    {
        return;
    }

    PDFAppTab* tab = getCurrentTab();
    const PDFAppPage* page = tab ? tab->getPage() : nullptr;
    const PDFAppRoute route = PDFAppRoutes::help(page ? page->getHelpTopic() : PDFAppRoutes::getDefaultHelpTopic());
    if (page && page->getRoute().getSegments() == route.getSegments())
    {
        // The manual on its topic (also with an anchor): no new entry of the history,
        // the page keeps its scroll position
        return;
    }

    m_navigator->navigate(route);
}

void PDFAppMainWindow::focusSearch()
{
    PDFAppTab* tab = getCurrentTab();
    PDFLineEdit* searchField = tab ? tab->getSearchField() : nullptr;
    if (!searchField)
    {
        // A tab without the search (Settings, documents later): the search of Home
        PDFAppWorkspaceTab* homeTab = getHomeTab();
        activateTab(homeTab);
        searchField = homeTab ? homeTab->getSearchField() : nullptr;
    }

    if (searchField)
    {
        searchField->setFocus(Qt::ShortcutFocusReason);
        searchField->selectAll();
    }
}

void PDFAppMainWindow::toggleMaximized()
{
    if (isFullScreen())
    {
        return;
    }

    if (isMaximized())
    {
        showNormal();
    }
    else
    {
        showMaximized();
    }
}

void PDFAppMainWindow::setFullScreenMode(bool isFullScreen)
{
    if (m_isFullScreenMode == isFullScreen)
    {
        return;
    }

    m_isFullScreenMode = isFullScreen;
    if (isFullScreen)
    {
        m_windowStateBeforeFullScreen = windowState() & ~(Qt::WindowFullScreen | Qt::WindowMinimized);
        if (m_topBar)
        {
            m_topBar->hide();
        }
        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowFullScreen);
    }
    else
    {
        if (m_topBar)
        {
            m_topBar->show();
        }
        setWindowState(m_windowStateBeforeFullScreen);
    }
    updateWindowState();
}

void PDFAppMainWindow::onCurrentTabChanged(int index)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    PDFAppTab* tab = tabBar ? findTab(tabBar->tabData(index).toULongLong()) : nullptr;
    if (tab && m_stack)
    {
        m_stack->setCurrentWidget(tab);
    }
    updateWindowTitle();
    updateCommandStates();
}

void PDFAppMainWindow::onTabCloseRequested(int index)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (tabBar)
    {
        closeTabByUser(findTab(tabBar->tabData(index).toULongLong()));
    }
}

void PDFAppMainWindow::closeTabByUser(PDFAppTab* tab)
{
    // A0: the quit of the window or the end of the session runs (Save All has no question to answer): a tab is not
    // closed by the user meanwhile - its question would stay open over the closed window; the question of the
    // window, if any, comes forward
    if (m_leaveFlow && m_leaveFlow->isRunning())
    {
        m_leaveFlow->raiseQuestion();
        return;
    }
    closeTab(tab);
}

void PDFAppMainWindow::onTabDestroyed(QObject* object)
{
    // Only the id of the tab is used (the tab is not whole any more)
    auto it = m_tabIds.find(object);
    if (it == m_tabIds.end())
    {
        return;
    }

    const quint64 id = it->second;
    m_tabIds.erase(it);
    for (qsizetype i = m_tabs.size() - 1; i >= 0; --i)
    {
        if (m_tabs[i].isNull() || m_tabs[i].data() == object)
        {
            m_tabs.removeAt(i);
        }
    }

    // The tab may be deleted with its parent (the stack of the tabs), which is not
    // whole any more: the stack is synchronized later, not from here
    const int index = findTabIndex(id);
    if (index >= 0)
    {
        if (PDFDocumentTabBar* tabBar = getTabBar())
        {
            const QSignalBlocker blocker(tabBar);
            tabBar->removeTab(index);
        }
    }
    QMetaObject::invokeMethod(this, &PDFAppMainWindow::synchronizeCurrentTab, Qt::QueuedConnection);
}

void PDFAppMainWindow::onTabCommandStatesChanged()
{
    // Only the state of the current tab is shown by the actions
    if (sender() == getCurrentTab())
    {
        updateCommandStates();
    }
}

void PDFAppMainWindow::onToolTabStateChanged()
{
    // The tab does not identify itself in the signal
    updateTab(qobject_cast<PDFAppTab*>(sender()));
}

void PDFAppMainWindow::synchronizeCurrentTab()
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (tabBar)
    {
        onCurrentTabChanged(tabBar->currentIndex());
    }
}

void PDFAppMainWindow::onNewTabRequested()
{
    openNewTab();
}

void PDFAppMainWindow::onMaximizeRestoreRequested()
{
    toggleMaximized();
}

void PDFAppMainWindow::onMinimizeRequested()
{
    showMinimized();
}

void PDFAppMainWindow::onCloseRequested()
{
    close();
}

void PDFAppMainWindow::onMoveRequested()
{
    if (m_frame)
    {
        m_frame->startSystemMove();
    }
}

void PDFAppMainWindow::onDatabaseChanged(const QString& topic, const QString& key)
{
    Q_UNUSED(key);

    if (topic == PDFShortcutRepository::getTopic())
    {
        updateShortcuts();
    }
}

void PDFAppMainWindow::onEverythingChanged()
{
    updateShortcuts();
}

void PDFAppMainWindow::onPageTitleChanged()
{
    PDFAppTab* tab = qobject_cast<PDFAppTab*>(sender());
    if (tab)
    {
        updateTab(tab);
    }
}

void PDFAppMainWindow::onOpenRequested(const PDFAppInstanceRequest& request)
{
    const PDFAppInstanceRequest requestCopy = request;
    handleOpenRequest(requestCopy);
}

void PDFAppMainWindow::handleOpenRequest(const PDFAppInstanceRequest& request)
{
    // Errors of the command line of the other process are shown by this window
    // (newapplication.md 3); the lists are bounded. A document opened before is
    // not named by the dialog of this request.
    m_lastOpenedTitle.clear();
    std::vector<PDFAppOpenFailure> failures;
    for (const QString& error : request.errors)
    {
        qWarning("Command line of another process: %s", qPrintable(error));
        if (m_forwardedErrors.size() < MAXIMUM_FORWARDED_ERRORS)
        {
            m_forwardedErrors << error;
        }
        if (failures.size() < size_t(MAXIMUM_FORWARDED_ERRORS))
        {
            PDFAppOpenFailure failure;
            failure.isOption = true;
            failure.name = error.left(1024);
            failures.push_back(std::move(failure));
        }
    }

    qInfo("Open requested by another process: %lld file(s), page %d%s", qlonglong(request.files.size()), request.page,
          request.activationToken.isEmpty() ? "" : ", with an activation token");

    // The window is raised at once, also while a modal dialog is open
    QPointer<PDFAppMainWindow> guard(this);
    PDFAppActivation::activateWindow(this, request.activationToken);
    if (!guard)
    {
        return;
    }

    if (!failures.empty())
    {
        reportOpenFailures(failures);
    }

    if (!request.files.isEmpty())
    {
        openDocuments(request.files, request.page);
        if (!guard)
        {
            return;
        }
    }

    // PDF4QT --compare original revised (Q7.13 of the plan of step 7 of phase 2): a tab of Compare
    if (request.compareFiles.size() == 2)
    {
        PDFAppToolGUICompare::openCompareTab(this, request.compareFiles);
    }
}

void PDFAppMainWindow::processCommandLine()
{
    if (!m_context)
    {
        return;
    }

    // The options, which were not used, are listed with the files, which cannot be
    // opened (open-failed 1028:24297); a missing file is reported by its opening
    const PDFAppCommandLine& commandLine = m_context->getCommandLine();
    std::vector<PDFAppOpenFailure> failures;
    for (const PDFAppCommandLineError& error : commandLine.getErrors())
    {
        PDFAppOpenFailure failure;
        switch (error.kind)
        {
            case PDFAppCommandLineError::Kind::FileNotFound:
                continue;

            case PDFAppCommandLineError::Kind::NotAFile:
                failure.isOption = false;
                failure.name = error.argument;
                failure.reason = tr("A folder, not a file");
                break;

            default:
                failure.isOption = true;
                failure.name = error.argument.left(260);
                failure.reason = error.getMessage();
                break;
        }
        failures.push_back(std::move(failure));
    }
    if (!failures.empty())
    {
        reportOpenFailures(failures);
    }

    QPointer<PDFAppMainWindow> guard(this);
    if (!commandLine.getFiles().isEmpty())
    {
        openDocuments(commandLine.getFiles(), commandLine.getPage());
        if (!guard)
        {
            return;
        }
    }

    // PDF4QT --compare original revised (Q7.13 of the plan of step 7 of phase 2): a tab of Compare with both inputs,
    // the comparison starts when both are read
    if (commandLine.getCompareFiles().size() == 2)
    {
        PDFAppToolGUICompare::openCompareTab(this, commandLine.getCompareFiles());
    }
}

void PDFAppMainWindow::openDocuments(const QStringList& files, int page)
{
    std::vector<PDFAppOpenRequest> requests;
    requests.reserve(size_t(files.size()));
    for (const QString& file : files)
    {
        PDFAppOpenRequest request;
        request.filePath = file;
        request.page = page;
        requests.push_back(std::move(request));
    }
    openDocuments(requests);
}

void PDFAppMainWindow::openDocuments(const std::vector<PDFAppOpenRequest>& requests)
{
    for (const PDFAppOpenRequest& request : requests)
    {
        const QString& file = request.filePath;
        if (!m_requestedFiles.contains(file) && m_requestedFiles.size() < PDFAppInstanceProtocol::MAXIMUM_FILES)
        {
            m_requestedFiles << file;
        }
    }

    // A modal dialog is open (a question of a tab, a dialog of a page): the files
    // are opened, when it closes (the window is raised already)
    if (QWidget* modalWidget = getBlockingModalWidget())
    {
        for (const PDFAppOpenRequest& request : requests)
        {
            if (m_pendingRequests.size() < size_t(PDFAppInstanceProtocol::MAXIMUM_FILES))
            {
                m_pendingRequests.push_back(request);
            }
        }
        watchModalWidget(modalWidget);
        return;
    }

    // The files are recorded in Recent (Home, Open and Recent read them) and their
    // thumbnails are made by the thumbnailer (worker thread); the pages read them
    // after the notification of the database
    PDFAppThumbnailer* thumbnailer = m_context ? m_context->getThumbnailer() : nullptr;
    const bool hasDatabase = m_context && m_context->getDatabase();
    int recorded = 0;
    int opened = 0;
    std::vector<PDFAppOpenFailure> failures;
    QPointer<PDFAppMainWindow> guard(this);

    // The dialog open-failed names a document of this request only (a document
    // opened by an earlier request is not "open" by this one)
    m_lastOpenedTitle.clear();

    // An empty tab New tab, from which the documents are opened, is replaced by the first one
    QPointer<PDFAppTab> replacedTab;
    if (PDFAppTab* currentTab = getCurrentTab(); currentTab && currentTab->getKind() == PDFAppTabKind::NewTab)
    {
        replacedTab = currentTab;
    }

    for (const PDFAppOpenRequest& request : requests)
    {
        // At most the number of the files of one request
        if (opened >= PDFAppInstanceProtocol::MAXIMUM_FILES)
        {
            break;
        }
        ++opened;

        const QString& file = request.filePath;
        const QFileInfo fileInfo(file);
        if (!fileInfo.isFile())
        {
            PDFAppOpenFailure failure;
            failure.name = file.left(1024);
            failure.reason = fileInfo.isDir() ? tr("A folder, not a file") : tr("File not found");
            failures.push_back(std::move(failure));
            continue;
        }

        // An assembly file of Manage Pages opens the tool (plan of step 3b of phase 2, RB3 e)
        if (PDFAppAssemblyLoader::isAssemblyFile(file))
        {
            QString assemblyError;
            const bool isOpened = openAssembly(file, &assemblyError);
            if (!guard)
            {
                return;
            }
            if (!isOpened)
            {
                PDFAppOpenFailure failure;
                failure.name = file.left(1024);
                failure.reason = assemblyError.isEmpty() ? tr("Not a PDF document") : assemblyError;
                failures.push_back(std::move(failure));
            }
            continue;
        }

        // A file without the extension .pdf and without the signature of a PDF is refused
        if (!PDFAppFileDrop::isPdfFile(file))
        {
            PDFAppOpenFailure failure;
            failure.name = file.left(1024);
            failure.reason = tr("Not a PDF document");
            failures.push_back(std::move(failure));
            continue;
        }

        // Only an existing file is recorded (a missing file of a command line is not)
        if (hasDatabase && recorded < PDFAppInstanceProtocol::MAXIMUM_FILES)
        {
            ++recorded;
            QString storedPath;
            const PDFDatabaseError error = m_context->getRecent().recordOpened(file, &storedPath);
            if (error.isError())
            {
                qWarning("The file cannot be recorded in Recent: %s", qPrintable(error.toString()));
            }
            else if (thumbnailer && !storedPath.isEmpty())
            {
                thumbnailer->requestThumbnail(storedPath);
            }
        }

        openDocument(request, &failures, &replacedTab);
        if (!guard)
        {
            return;
        }
    }

    if (!failures.empty())
    {
        reportOpenFailures(failures);
    }
}

bool PDFAppMainWindow::openAssembly(const QString& filePath, QString* errorText)
{
    if (errorText)
    {
        errorText->clear();
    }

    // The own tab of Manage Pages (plan of step 3b of phase 2, 4.6, RB3 e): its GUI takes the file
    // from the list of the inputs at its start and loads it (PDFAppAssemblyLoader - the dialog of
    // the sources, the bin, B24). Without the tool (not registered) the file is refused.
    const QString toolId = QString::fromLatin1(PDFDocumentToolManage::TOOL_ID);
    if (!PDFAppToolTabPageFactory::hasOwnTabPage(toolId))
    {
        if (errorText)
        {
            *errorText = tr("Manage Pages is not available");
        }
        return false;
    }

    const QString absolutePath = QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());

    // The file is recorded in the recent assemblies (409:32023, 409:32739, the page Open) as an
    // opened PDF is recorded in Recent before it is read; its page count comes with the GUI,
    // when the assembly is read (PDFRecentAssemblyRepository::recordOpened)
    if (m_context && m_context->getDatabase())
    {
        const PDFDatabaseError error = PDFRecentAssemblyRepository(m_context->getDatabase()).recordOpened(absolutePath, -1);
        if (error.isError())
        {
            qWarning("The assembly cannot be recorded in the recent assemblies: %s", qPrintable(error.toString()));
        }
    }

    // An assembly open already (also being loaded) is not opened a second time: its tab becomes
    // current - of this window first, else of another window, which comes to the front. The
    // activation runs code of the application (the tab bar, the pages): it is the last call.
    if (PDFAppToolTab* openTab = PDFAppManageToolGUI::findAssemblyTab(this, absolutePath))
    {
        activateTab(openTab);
        return true;
    }
    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(widget);
        PDFAppToolTab* openTab = window && window != this ? PDFAppManageToolGUI::findAssemblyTab(window, absolutePath) : nullptr;
        if (openTab)
        {
            QPointer<PDFAppMainWindow> windowGuard(window);
            window->activateTab(openTab);
            if (windowGuard)
            {
                PDFAppActivation::activateWindow(windowGuard.data(), QString());
            }
            return true;
        }
    }

    // The list goes to the tab in one call (R13); not taken by a tab, it stays here. The tab may
    // show its route at once: the file is on the list before.
    PDFAppToolInputList* inputList = new PDFAppToolInputList(nullptr);
    PDFAppAssemblyLoader::setRequestedFile(inputList, absolutePath);
    QPointer<PDFAppToolInputList> listGuard(inputList);
    QPointer<PDFAppMainWindow> guard(this);
    PDFAppToolTab* tab = createToolTab(toolId, inputList);
    if (listGuard && !listGuard->parent())
    {
        delete listGuard.data();
    }
    if (!guard)
    {
        return false;
    }

    if (!tab && errorText)
    {
        *errorText = tr("Too many tabs are open");
    }
    return tab != nullptr;
}

void PDFAppMainWindow::openDocument(const PDFAppOpenRequest& request, std::vector<PDFAppOpenFailure>* failures, QPointer<PDFAppTab>* replacedTab)
{
    // A file open in the window already: its tab (and the page of the request)
    const QString canonicalPath = QFileInfo(request.filePath).canonicalFilePath();
    PDFAppDocumentRegistry* registry = m_context ? m_context->getDocumentRegistry() : nullptr;
    if (PDFAppDocument* document = registry ? registry->findByCanonicalPath(canonicalPath) : nullptr)
    {
        if (PDFAppTab* tab = findDocumentTab(document->getId()))
        {
            m_lastOpenedTitle = tab->getTabTitle();
            activateTab(tab);
            if (!request.toolId.isEmpty() && PDFAppToolDocumentPage::isRefusedByLock(request.toolId, document))
            {
                // T3 is the answer to a manual route: the tab of the file says the reason
                PDFAppDocumentTab* documentTab = qobject_cast<PDFAppDocumentTab*>(tab);
                PDFAppDocumentView* view = documentTab ? documentTab->getView() : nullptr;
                if (PDFAppDocumentStatusBar* statusBar = view ? view->getStatusBar() : nullptr)
                {
                    statusBar->showMessage(PDFAppToolDocumentPage::getLockRefusalText(request.toolId), PDFAppDocumentToolHost::MESSAGE_TIMEOUT);
                }
            }
            else if (m_navigator && !request.toolId.isEmpty())
            {
                m_navigator->navigate(PDFAppRoutes::documentTool(document->getId(), request.toolId), PDFAppNavigationMode::Push, tab);
            }
            else if (m_navigator && request.page > 0)
            {
                m_navigator->navigate(PDFAppRoutes::document(document->getId(), request.page), PDFAppNavigationMode::Push, tab);
            }
            return;
        }

        // F13: a document of another window of a tool: its tab is activated, this window
        // does not read the file a second time
        if (!request.toolId.isEmpty())
        {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                PDFAppMainWindow* window = qobject_cast<PDFAppMainWindow*>(widget);
                PDFAppTab* tab = window && window != this ? window->findDocumentTab(document->getId()) : nullptr;
                if (tab)
                {
                    m_lastOpenedTitle = tab->getTabTitle();
                    window->activateTab(tab);
                    PDFAppActivation::activateWindow(window, QString());
                    return;
                }
            }
        }
    }

    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tabBar || !m_stack || tabBar->count() >= MAXIMUM_TABS)
    {
        PDFAppOpenFailure failure;
        failure.name = request.filePath.left(1024);
        failure.reason = tr("There are too many tabs");
        failures->push_back(std::move(failure));
        return;
    }

    // The page: the page of the request (--page, a workspace), else the last page
    // of the file in Recent (the route ignores a page out of range)
    int page = qMax(0, request.page);
    if (page == 0 && m_context && m_context->getDatabase())
    {
        if (const std::optional<PDFRecentFile> file = m_context->getRecent().getFile(canonicalPath))
        {
            page = file->lastPage > 1 ? file->lastPage : 0;
        }
    }

    // The tab is shown at once (the state Loading), the file is read in a worker
    QPointer<PDFAppMainWindow> guard(this);
    PDFAppDocumentTab* documentTab = new PDFAppDocumentTab(m_context, m_stack);
    QPointer<PDFAppDocumentTab> tab(documentTab);
    connectDocumentTab(documentTab);

    QPointer<PDFAppTab> emptyTab = replacedTab ? *replacedTab : nullptr;
    const int emptyTabIndex = emptyTab ? findTabIndex(emptyTab->getId()) : -1;
    addTab(documentTab, emptyTabIndex >= 0 ? emptyTabIndex : tabBar->currentIndex() + 1);
    tab->open(canonicalPath.isEmpty() ? request.filePath : canonicalPath);
    if (!guard || !tab)
    {
        return;
    }

    m_lastOpenedTitle = tab->getTabTitle();
    if (m_navigator)
    {
        // A route of a tool waits until the document is shown (PDFAppDocumentPage)
        m_navigator->navigate(request.toolId.isEmpty() ? PDFAppRoutes::document(tab->getDocumentId(), page) : PDFAppRoutes::documentTool(tab->getDocumentId(), request.toolId),
                              PDFAppNavigationMode::Push, tab);
        if (!guard)
        {
            return;
        }
    }

    // The empty New tab gives its place to the document
    if (emptyTab && replacedTab)
    {
        *replacedTab = nullptr;
        closeTab(emptyTab);
    }
}

void PDFAppMainWindow::connectDocumentTab(PDFAppDocumentTab* tab)
{
    connect(tab, &PDFAppDocumentTab::loadFailed, this, &PDFAppMainWindow::onDocumentLoadFailed);
    connect(tab, &PDFAppDocumentTab::editCopyRequested, this, &PDFAppMainWindow::onEditCopyRequested);
}

QWidget* PDFAppMainWindow::getBlockingModalWidget()
{
    // The message boxes, which only inform (open-failed, the errors of a drop), do
    // not stop the opening: they are not questions, whose answer the code waits for
    QWidget* modalWidget = QApplication::activeModalWidget();
    if (modalWidget && modalWidget->property(NON_BLOCKING_PROPERTY).toBool())
    {
        return nullptr;
    }
    return modalWidget;
}

void PDFAppMainWindow::watchModalWidget(QWidget* widget)
{
    if (m_watchedModal == widget)
    {
        return;
    }

    if (m_watchedModal)
    {
        m_watchedModal->removeEventFilter(this);
        m_watchedModal->disconnect(this);
    }
    m_watchedModal = widget;
    if (widget)
    {
        // A foreign widget (a dialog of another owner): the filter is removed also by retire of the window (step A)
        PDFDeferredDeletion::installEventFilter(widget, this, this);
        connect(widget, &QObject::destroyed, this, &PDFAppMainWindow::onWatchedModalDestroyed);
    }
}

void PDFAppMainWindow::onWatchedModalDestroyed()
{
    // Only the pointer is forgotten (the widget is not whole any more)
    m_watchedModal = nullptr;
    QMetaObject::invokeMethod(this, &PDFAppMainWindow::processPendingRequests, Qt::QueuedConnection);
}

void PDFAppMainWindow::processPendingRequests()
{
    if (m_pendingRequests.empty())
    {
        return;
    }

    // Another modal dialog may be open (a question after the closed one)
    if (QWidget* modalWidget = getBlockingModalWidget())
    {
        watchModalWidget(modalWidget);
        return;
    }

    watchModalWidget(nullptr);
    std::vector<PDFAppOpenRequest> requests;
    requests.swap(m_pendingRequests);
    openDocuments(requests);
}

void PDFAppMainWindow::reportOpenFailures(const std::vector<PDFAppOpenFailure>& failures)
{
    for (const PDFAppOpenFailure& failure : failures)
    {
        if (m_pendingFailures.size() < size_t(PDFAppInstanceProtocol::MAXIMUM_FILES))
        {
            m_pendingFailures.push_back(failure);
        }
    }

    // The failures of the files read in the workers come one by one: one dialog
    if (!m_failureTimer)
    {
        m_failureTimer = new QTimer(this);
        m_failureTimer->setSingleShot(true);
        m_failureTimer->setInterval(OPEN_FAILURE_DELAY);
        connect(m_failureTimer.data(), &QTimer::timeout, this, &PDFAppMainWindow::showOpenFailures);
    }
    m_failureTimer->start();
}

PDFAppOpenFailedDialog* PDFAppMainWindow::getOpenFailedDialog() const
{
    return m_openFailedDialog.data();
}

void PDFAppMainWindow::showOpenFailures()
{
    if (m_pendingFailures.empty() || m_openFailedDialog)
    {
        // A shown dialog is followed by the next one (onOpenFailedDialogFinished)
        return;
    }

    PDFAppOpenFailedDialog* dialog = new PDFAppOpenFailedDialog(m_pendingFailures, m_lastOpenedTitle, this);
    m_pendingFailures.clear();
    m_lastOpenedTitle.clear();
    m_openFailedDialog = dialog;
    dialog->setProperty(NON_BLOCKING_PROPERTY, true);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::finished, this, &PDFAppMainWindow::onOpenFailedDialogFinished);
    dialog->open();
}

void PDFAppMainWindow::onOpenFailedDialogFinished()
{
    m_openFailedDialog = nullptr;
    if (!m_pendingFailures.empty() && m_failureTimer)
    {
        m_failureTimer->start();
    }
}

void PDFAppMainWindow::onDocumentLoadFailed(const QString& filePath, const QString& errorMessage)
{
    // The tab of a file, which cannot be read, closes and the file is reported
    PDFAppDocumentTab* tab = qobject_cast<PDFAppDocumentTab*>(sender());
    PDFAppOpenFailure failure;
    failure.name = filePath.left(1024);
    failure.reason = errorMessage.left(1024);
    reportOpenFailures({ failure });

    if (tab && m_lastOpenedTitle == tab->getTabTitle())
    {
        m_lastOpenedTitle.clear();
    }
    if (tab && tab->isLeaving())
    {
        // A question of the tab is open (a navigation, a lock): the tab closes after its answer
        closeFailedTabAfterFlow(tab);
        return;
    }
    if (tab)
    {
        // The signal comes from the emission of stateChanged of the document of the tab: the close (retire, step A)
        // would cut the rest of that emission - the view, the tools and the MCP waiters would not see the failure.
        // The tab closes from the event loop (closeFailedTabs reads it again).
        m_failedTabs.emplace_back(tab);
        QMetaObject::invokeMethod(this, &PDFAppMainWindow::closeFailedTabs, Qt::QueuedConnection);
    }
}

void PDFAppMainWindow::onFailedTabFlowFinished()
{
    // The slot runs inside of the end of the flow (its waiter may still run): the tabs close from the event loop
    QMetaObject::invokeMethod(this, &PDFAppMainWindow::closeFailedTabs, Qt::QueuedConnection);
}

void PDFAppMainWindow::closeFailedTabAfterFlow(PDFAppDocumentTab* tab)
{
    m_failedTabs.emplace_back(tab);
    connect(tab->getLeaveFlow(), &PDFAppLeaveFlow::finished, this, &PDFAppMainWindow::onFailedTabFlowFinished, Qt::UniqueConnection);
}

void PDFAppMainWindow::closeFailedTabs()
{
    // Each tab is read again: closed meanwhile, a new flow (it closes after that one), a document loaded anew
    std::vector<QPointer<PDFAppDocumentTab>> tabs = std::move(m_failedTabs);
    m_failedTabs.clear();
    QPointer<PDFAppMainWindow> guard(this);
    for (const QPointer<PDFAppDocumentTab>& tab : tabs)
    {
        if (!guard)
        {
            return;
        }
        if (!tab || findTabIndex(tab->getId()) < 0)
        {
            continue;
        }
        const PDFAppDocument* document = tab->getDocument();
        if (!document || document->getState() != PDFAppDocumentState::Failed)
        {
            continue;
        }
        if (tab->isLeaving())
        {
            // A flow started after the failure, before this queued close (a later receiver of the same failure, an
            // assistant): the tab closes at the end of that flow (RA-L2)
            closeFailedTabAfterFlow(tab.data());
            continue;
        }
        closeTab(tab);
    }
}

void PDFAppMainWindow::onEditCopyRequested()
{
    QPointer<PDFAppDocumentTab> tab = qobject_cast<PDFAppDocumentTab*>(sender());
    if (!tab)
    {
        return;
    }

    // An open editor of a form field belongs to the copy too (step 4, package D);
    // a slot of the commit may delete the tab or the window
    QPointer<PDFAppMainWindow> guard(this);
    if (PDFAppDocument* document = tab->getDocument())
    {
        document->commitPendingEdits();
    }
    if (guard && tab)
    {
        openDocumentCopy(tab->getDocument());
    }
}

PDFAppDocumentTab* PDFAppMainWindow::openDocumentCopy(const PDFAppDocument* source)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!source || !source->isReady() || !tabBar || !m_stack || tabBar->count() >= MAXIMUM_TABS)
    {
        return nullptr;
    }

    // The copy opens after the tab of its source
    const int index = getTabIndexAfter(source);

    PDFAppDocumentTab* documentTab = new PDFAppDocumentTab(m_context, m_stack);
    QPointer<PDFAppDocumentTab> tab(documentTab);
    if (!documentTab->openCopyOf(source))
    {
        // Never added nor shown: retired, deleted later (step A)
        PDFDeferredDeletion::retire(tab.data());
        return nullptr;
    }
    connectDocumentTab(documentTab);
    addTab(documentTab, index);

    QPointer<PDFAppMainWindow> guard(this);
    if (m_navigator && tab)
    {
        m_navigator->navigate(PDFAppRoutes::document(tab->getDocumentId()), PDFAppNavigationMode::Push, tab);
    }
    return guard ? tab.data() : nullptr;
}

int PDFAppMainWindow::getTabIndexAfter(const PDFAppDocument* source) const
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tabBar)
    {
        return 0;
    }

    if (source)
    {
        for (PDFAppDocumentTab* documentTab : getDocumentTabs())
        {
            if (documentTab->getDocument() == source)
            {
                return findTabIndex(documentTab->getId()) + 1;
            }
        }
    }
    return tabBar->currentIndex() + 1;
}

int PDFAppMainWindow::getTabIndexAfter(const PDFAppTab* tab) const
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tabBar)
    {
        return 0;
    }

    const int index = tab ? findTabIndex(tab->getId()) : -1;
    return index >= 0 ? index + 1 : tabBar->currentIndex() + 1;
}

PDFAppDocumentTab* PDFAppMainWindow::openDocumentFromMemory(QByteArray bytes, const QString& title, const QString& originFilePath, const PDFAppDocument* source)
{
    // The document opens after the tab of its source (the user may have switched
    // the tab, while the attachment was decoded)
    return openDocumentFromMemoryAt(std::move(bytes), title, originFilePath, getTabIndexAfter(source));
}

PDFAppDocumentTab* PDFAppMainWindow::openDocumentFromMemoryAt(QByteArray bytes, const QString& title, const QString& originFilePath, int index)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tabBar || !m_stack || tabBar->count() >= MAXIMUM_TABS)
    {
        PDFAppOpenFailure failure;
        failure.name = title.left(1024);
        failure.reason = tr("There are too many tabs");
        reportOpenFailures({ failure });
        return nullptr;
    }

    // The tab is created without a parent: the signals of its construction (the registry of the
    // documents, the view) run code of the application, which may delete the window - a tab of
    // the stack would go with it half constructed. The stack takes it over in addTab.
    QPointer<PDFAppMainWindow> guard(this);
    PDFAppDocumentTab* documentTab = new PDFAppDocumentTab(m_context, nullptr);
    QPointer<PDFAppDocumentTab> tab(documentTab);
    if (!guard || !m_stack)
    {
        // Never added: retired, deleted later (step A)
        PDFDeferredDeletion::retire(tab.data());
        return nullptr;
    }
    if (!tab)
    {
        return nullptr;
    }
    connectDocumentTab(documentTab);
    addTab(documentTab, index);

    if (!guard || !tab || !tab->openFromMemory(std::move(bytes), title, originFilePath))
    {
        if (guard && tab)
        {
            closeTab(tab);
        }
        return nullptr;
    }
    if (!guard || !tab)
    {
        return nullptr;
    }

    if (m_navigator)
    {
        m_navigator->navigate(PDFAppRoutes::document(tab->getDocumentId()), PDFAppNavigationMode::Push, tab);
    }
    return guard ? tab.data() : nullptr;
}

void PDFAppMainWindow::onLockClicked(int index)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    PDFAppDocumentTab* tab = tabBar ? qobject_cast<PDFAppDocumentTab*>(findTab(tabBar->tabData(index).toULongLong())) : nullptr;
    if (!tab)
    {
        return;
    }

    // The popover is anchored below the tab at the lock (266:12010)
    QPointer<PDFAppDocumentTab> guard(tab);
    activateTab(tab);
    if (!guard || !tabBar)
    {
        return;
    }

    const QRect lockRect = tabBar->getTabLockRect(index);
    const QRect tabRect = tabBar->tabRect(index);
    if (!lockRect.isEmpty())
    {
        const QRect anchor(tabBar->mapToGlobal(QPoint(lockRect.left(), tabRect.top())), QSize(lockRect.width(), tabRect.height()));
        tab->showLockPopover(anchor);
    }
}

void PDFAppMainWindow::browseDocuments(const QString& folder)
{
    if (m_isBrowsingDocuments)
    {
        return;
    }

    // A0: the dialog is shown and the call returns (no event loop); the files come to onBrowseDocumentsChosen (a
    // window deleted meanwhile gets nothing: the request is its child). The flag is set first: the request runs
    // code (a test handler) before it returns.
    m_isBrowsingDocuments = true;
    if (!PDFAppFileDialogs::requestOpenDocumentFileNames(this, folder, this, &PDFAppMainWindow::onBrowseDocumentsChosen))
    {
        m_isBrowsingDocuments = false;
    }
}

void PDFAppMainWindow::onBrowseDocumentsChosen(const QStringList& files)
{
    m_isBrowsingDocuments = false;

    // The page shown now decides, where a file goes (as after the return of the dialog)
    if (!files.isEmpty())
    {
        openDocumentsOfPage(files);
    }
}

void PDFAppMainWindow::openDocumentsOfPage(const QStringList& files)
{
    // The start page of a tool: one file goes to the tool (the mode One), others only open
    PDFAppTab* tab = getCurrentTab();
    const PDFAppPage* page = tab ? tab->getPage() : nullptr;
    QString toolId = page ? page->getOpenFilesToolId() : QString();

    std::vector<PDFAppOpenRequest> requests;
    for (const QString& file : files)
    {
        PDFAppOpenRequest request;
        request.filePath = file;
        request.toolId = toolId;
        toolId.clear();
        requests.push_back(std::move(request));
    }
    openDocuments(requests);
}

std::vector<PDFAppOpenDocument> PDFAppMainWindow::getOpenDocuments() const
{
    std::vector<PDFAppOpenDocument> documents;
    for (PDFAppDocumentTab* tab : getDocumentTabs())
    {
        PDFAppDocument* document = tab->getDocument();
        if (!document)
        {
            continue;
        }

        PDFAppOpenDocument openDocument;
        openDocument.path = document->getFilePath();
        openDocument.title = tab->getTabTitle();
        openDocument.pageCount = int(qMin<PDFInteger>(document->getPageCount(), std::numeric_limits<int>::max()));
        const PDFInteger pageIndex = tab->getView() ? tab->getView()->getDisplayedPageIndex() : -1;
        openDocument.page = pageIndex >= 0 ? int(qMin<PDFInteger>(pageIndex + 1, std::numeric_limits<int>::max())) : 0;
        openDocument.isModified = document->isModified();
        documents.push_back(std::move(openDocument));
    }
    return documents;
}

QWidget* PDFAppMainWindow::getDialogHost() const
{
    return m_stack.data();
}

QWidget* PDFAppMainWindow::findDialogHost(const QWidget* widget)
{
    const PDFAppMainWindow* window = widget ? qobject_cast<const PDFAppMainWindow*>(widget->window()) : nullptr;
    return window ? window->getDialogHost() : nullptr;
}

void PDFAppMainWindow::prepareDialog(PDFMessageBox* messageBox, const QWidget* widget)
{
    if (QWidget* host = messageBox ? findDialogHost(widget) : nullptr)
    {
        messageBox->setScrimTarget(host);
    }
}

void PDFAppMainWindow::showMessage(int badge, const QString& title, const QString& text, const QString& details)
{
    // The message box does not block the caller (a request of another process,
    // the start of the application); it deletes itself, when it is closed
    PDFMessageBox* messageBox = new PDFMessageBox(static_cast<PDFThemeIcons::Badge>(badge), title, text, QDialogButtonBox::Ok, this);
    messageBox->setObjectName(QStringLiteral("messageBox"));
    prepareDialog(messageBox, this);
    if (!details.isEmpty())
    {
        messageBox->setDetailedText(details);
    }
    messageBox->setProperty(NON_BLOCKING_PROPERTY, true);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->open();
}

PDFAppLeaveFlow* PDFAppMainWindow::getLeaveFlow()
{
    if (!m_leaveFlow)
    {
        m_leaveFlow = new PDFAppLeaveFlow(this);
        connect(m_leaveFlow.data(), &PDFAppLeaveFlow::finished, this, &PDFAppMainWindow::onLeaveFlowFinished);
    }
    return m_leaveFlow.data();
}

PDFAppLeaveResult PDFAppMainWindow::requestQuit()
{
    // The flow has ended Done (its last answer, Save All): the window is closed again by it
    if (m_isQuitConfirmed)
    {
        return PDFAppLeaveResult::Done;
    }

    // A0: one flow of the window (a question of the quit is open, Save All runs, the question of the end of the
    // session is open): the second request is refused, the question comes forward
    PDFAppLeaveFlow* flow = getLeaveFlow();
    if (PDFAppLeaveFlow* runningFlow = PDFAppLeaveFlow::findRunning(this); runningFlow && runningFlow != flow)
    {
        // A flow of a tab asks (its close, a navigation, a lock): the quit is refused, its question comes forward
        runningFlow->raiseQuestion();
        return PDFAppLeaveResult::Refused;
    }
    if (!flow->start(PDFAppLeaveFlow::Goal::QuitWindow))
    {
        flow->raiseQuestion();
        return PDFAppLeaveResult::Refused;
    }

    // The tools in the tabs of the documents ask first about the unsaved work of the user, each shown with its
    // question (owner 2026-11-20). Their closing answers end nothing here: a later Keep cancels the quit and
    // every tool keeps its work (decision 8). Work discarded at the end of the system session is not asked again
    // only by the close, which the allowed end of the session makes (PDFAppSessionEnd::isSessionEnding).
    const bool isSessionEnding = PDFAppSessionEnd::isSessionEnding();
    std::vector<QPointer<QObject>> toolPages;
    for (PDFAppDocumentTab* tab : getDocumentTabs())
    {
        if (PDFAppPage* page = tab->getPage(); page && page->hasUnsavedWork() && !(isSessionEnding && PDFAppSessionEnd::isToolWorkDiscarded(page)))
        {
            toolPages.emplace_back(page);
        }
    }
    flow->setQueue(toolPages);
    return continueQuit(QuitStep::ToolPages);
}

PDFAppLeaveResult PDFAppMainWindow::continueQuit(QuitStep from)
{
    QPointer<PDFAppMainWindow> guard(this);
    QPointer<PDFAppLeaveFlow> flow = getLeaveFlow();

    // The pages of the tools, one by one (A0: each question is shown, its answer continues in onQuitStepFinished).
    // A slot may delete a tab or the window. Every page, which has accepted, waits for the end of the leave
    // (PDFAppLeaveFlow::requestFinishPages): false when the quit is canceled, true before the question of the unsaved
    // documents.
    if (from <= QuitStep::ToolPages)
    {
        flow->setStep(int(QuitStep::ToolPages));
        while (QObject* object = flow->takeNext())
        {
            QPointer<PDFAppPage> page = qobject_cast<PDFAppPage*>(object);
            if (!page)
            {
                continue;
            }
            PDFAppLeaveResult result = page->requestClose(flow);
            if (!guard || !flow)
            {
                return PDFAppLeaveResult::Refused;
            }
            if (result == PDFAppLeaveResult::Pending)
            {
                if (page && flow->waitForStep(page.data(), this, &PDFAppMainWindow::onQuitStepFinished))
                {
                    flow->setSubject(page.data());
                    return PDFAppLeaveResult::Pending;
                }
                result = PDFAppLeaveResult::Refused;
            }
            if (result != PDFAppLeaveResult::Done)
            {
                flow->finish(false);
                return PDFAppLeaveResult::Refused;
            }
            flow->addAcceptedPage(page.data());
        }

        // Then the own tabs of the tools, each shown with its question (K16: the list of the inputs is lost; a run
        // asks as close-running and Stop stops it at once - after the questions above, so a Keep there never comes
        // after a stopped run; an open result dialog keeps the window)
        std::vector<QPointer<QObject>> toolTabs;
        for (PDFAppToolTab* toolTab : getToolTabs())
        {
            toolTabs.emplace_back(toolTab);
        }
        flow->setQueue(toolTabs);
    }

    if (from <= QuitStep::ToolTabs)
    {
        flow->setStep(int(QuitStep::ToolTabs));
        while (QObject* object = flow->takeNext())
        {
            QPointer<PDFAppToolTab> toolTab = qobject_cast<PDFAppToolTab*>(object);
            QPointer<PDFAppToolTabPage> page = toolTab && !toolTab->isCloseConfirmed() ? toolTab->getToolPage() : nullptr;
            if (!page)
            {
                continue;
            }
            PDFAppLeaveResult result = page->requestClose(flow);
            if (!guard || !flow)
            {
                return PDFAppLeaveResult::Refused;
            }
            if (result == PDFAppLeaveResult::Pending)
            {
                if (page && flow->waitForStep(page.data(), this, &PDFAppMainWindow::onQuitStepFinished))
                {
                    flow->setSubject(nullptr);
                    return PDFAppLeaveResult::Pending;
                }
                result = PDFAppLeaveResult::Refused;
            }
            if (result != PDFAppLeaveResult::Done)
            {
                flow->finish(false);
                return PDFAppLeaveResult::Refused;
            }
        }
    }

    // The quit goes on: the choices of the questions of the tools, which change their documents (Redact "Remove New
    // Marks", Measure "Save and Close"), are applied before the question of the unsaved documents - Save All then
    // saves them; Cancel there keeps the applied changes (Undo takes them back). A tool, which could not keep all of
    // its work (a measurement, which cannot be stored), asks about the rest; its Keep cancels the quit (owner
    // 2026-11-21): the window stays. The slots of the commits may delete a tab or the window.
    if (from <= QuitStep::FinishPages)
    {
        flow->setStep(int(QuitStep::FinishPages));
        PDFAppLeaveResult result = flow->requestFinishPages(true);
        if (!guard || !flow)
        {
            return PDFAppLeaveResult::Refused;
        }
        if (result == PDFAppLeaveResult::Pending)
        {
            if (flow->waitForStep(flow.data(), this, &PDFAppMainWindow::onQuitStepFinished))
            {
                return PDFAppLeaveResult::Pending;
            }
            result = PDFAppLeaveResult::Refused;
        }
        if (result != PDFAppLeaveResult::Done)
        {
            flow->finish(false);
            return PDFAppLeaveResult::Refused;
        }
    }

    // Open editors of form fields are changes too (step 4, package D); a slot
    // may delete a tab or the window
    std::vector<QPointer<PDFAppDocument>> pendingDocuments;
    for (PDFAppDocumentTab* tab : getDocumentTabs())
    {
        pendingDocuments.emplace_back(tab->getDocument());
    }
    for (const QPointer<PDFAppDocument>& document : pendingDocuments)
    {
        if (document)
        {
            document->commitPendingEdits();
            if (!guard || !flow)
            {
                return PDFAppLeaveResult::Refused;
            }
        }
    }

    // The documents with unsaved changes: pdf4qt-quit-unsaved 955:145378 (not the
    // changes discarded at the end of the session in the close, which the allowed end of the session makes - the
    // question of the end of the session; a quit of the user asks about them, H1 of the review of A0)
    const bool isSessionEnding = PDFAppSessionEnd::isSessionEnding();
    std::vector<const PDFAppDocument*> documents;
    std::vector<QPointer<PDFAppDocumentTab>> tabs;
    for (PDFAppDocumentTab* tab : getDocumentTabs())
    {
        const PDFAppDocument* document = tab->getDocument();
        if (document && document->isReady() && document->isModified() && !(isSessionEnding && PDFAppSessionEnd::isDiscarded(document)))
        {
            documents.push_back(document);
            tabs.emplace_back(tab);
        }
    }
    if (documents.empty())
    {
        flow->finish(true);
        return guard ? PDFAppLeaveResult::Done : PDFAppLeaveResult::Refused;
    }

    // A0: the question is shown (exec's modality, no event loop); the answer comes to onQuitUnsavedChosen
    flow->setStep(int(QuitStep::Unsaved));
    m_quitSaveQueue = std::move(tabs);
    PDFAppQuitUnsavedDialog* dialog = new PDFAppQuitUnsavedDialog(documents, this);
    flow->setQuestion(dialog);
    PDFAppDocumentChoiceDialog::ask(dialog, this, &PDFAppMainWindow::onQuitUnsavedChosen);
    return PDFAppLeaveResult::Pending;
}

bool PDFAppMainWindow::isQuitStep(QuitStep step) const
{
    return m_leaveFlow && m_leaveFlow->isRunning() && m_leaveFlow->getGoal() == PDFAppLeaveFlow::Goal::QuitWindow && m_leaveFlow->getStep() == int(step);
}

void PDFAppMainWindow::completeQuit(PDFAppLeaveResult result)
{
    // The last answer (or the last save) has come later than the close: the window closes now (the last call)
    if (result != PDFAppLeaveResult::Done)
    {
        return;
    }
    m_isQuitConfirmed = true;
    QPointer<PDFAppMainWindow> guard(this);
    close();
    if (guard)
    {
        m_isQuitConfirmed = false;
    }
}

void PDFAppMainWindow::onQuitStepFinished(PDFAppLeaveResult result)
{
    PDFAppLeaveFlow* flow = m_leaveFlow.data();
    QuitStep step = QuitStep::ToolPages;
    if (isQuitStep(QuitStep::ToolPages))
    {
        step = QuitStep::ToolPages;
    }
    else if (isQuitStep(QuitStep::ToolTabs))
    {
        step = QuitStep::ToolTabs;
    }
    else if (isQuitStep(QuitStep::FinishPages))
    {
        step = QuitStep::FinishPages;
    }
    else
    {
        return;
    }

    if (result != PDFAppLeaveResult::Done)
    {
        flow->finish(false);
        return;
    }

    // A page of a tool has accepted: it waits for the end of the leave; the queue goes on (the tabs, the pages may
    // have gone meanwhile - the queue holds them weakly)
    if (step == QuitStep::ToolPages)
    {
        if (PDFAppPage* page = qobject_cast<PDFAppPage*>(flow->getSubject()))
        {
            flow->addAcceptedPage(page);
        }
        flow->setSubject(nullptr);
    }
    completeQuit(continueQuit(step == QuitStep::FinishPages ? QuitStep::Unsaved : step));
}

void PDFAppMainWindow::onQuitUnsavedChosen(PDFAppDocumentChoice choice)
{
    if (!isQuitStep(QuitStep::Unsaved))
    {
        return;
    }

    PDFAppLeaveFlow* flow = m_leaveFlow.data();
    switch (choice)
    {
        case PDFAppDocumentChoice::Discard:
            m_quitSaveQueue.clear();
            flow->finish(true);
            completeQuit(PDFAppLeaveResult::Done);
            return;

        case PDFAppDocumentChoice::Save:
            // The documents are saved one by one; the window closes after the last one
            flow->setStep(int(QuitStep::Saving));
            saveNextForQuit();
            return;

        default:
            break;
    }
    m_quitSaveQueue.clear();
    flow->finish(false);
}

void PDFAppMainWindow::onLeaveFlowFinished(bool isDone)
{
    // The quit has ended without closing the window (Cancel, Keep, a failed save; also in a later answer): the held
    // requests are opened and the server listens again
    if (!m_isQuitFlowActive)
    {
        return;
    }
    m_isQuitFlowActive = false;
    if (!isDone)
    {
        resumeAfterQuit();
    }
}

void PDFAppMainWindow::resumeAfterQuit()
{
    // The user has canceled the quitting: the held requests are opened and the server listens again (another
    // instance may have taken the name meanwhile, then this one runs without the forwarding)
    m_isClosing = false;
    QPointer<PDFAppInstanceServer> server = m_context ? m_context->getInstanceServer() : nullptr;
    if (server)
    {
        QString error;
        if (!server->resume(PDFAppInstanceServer::DEFAULT_RETRY_LIMIT, &error))
        {
            qInfo("The running instance does not receive forwarded files now: %s", qPrintable(error));
        }
    }
}

void PDFAppMainWindow::prepareTabsClose()
{
    for (PDFAppTab* tab : getTabs())
    {
        prepareTabClose(tab);
    }
}

void PDFAppMainWindow::prepareTabClose(PDFAppTab* tab)
{
    if (PDFAppPage* page = tab ? tab->getPage() : nullptr)
    {
        page->prepareClose();
    }

    // The tool of an own tab records its use too (its shell outlives the page in the destruction of the tab)
    const PDFAppToolTab* toolTab = qobject_cast<const PDFAppToolTab*>(tab);
    if (PDFDocumentToolShell* shell = toolTab ? toolTab->getShell() : nullptr)
    {
        shell->recordInteractiveUse();
    }
}

bool PDFAppMainWindow::isToolQuestionOpen() const
{
    for (PDFAppTab* tab : getTabs())
    {
        // A tool in a document tab (its page), an own tab of a tool (its close question, the question of its run)
        const PDFAppPage* page = tab ? tab->getPage() : nullptr;
        const PDFAppToolTab* toolTab = qobject_cast<const PDFAppToolTab*>(tab);
        const PDFDocumentToolShell* toolShell = toolTab ? toolTab->getShell() : nullptr;
        if ((page && page->isQuestionOpen()) || (toolTab && toolTab->isAskingClose()) || (toolShell && toolShell->isQuestionOpen()))
        {
            return true;
        }
    }
    return false;
}

void PDFAppMainWindow::saveNextForQuit()
{
    // Save All is a step of the flow of the quit (a flow ended meanwhile saves nothing more)
    if (!isQuitStep(QuitStep::Saving))
    {
        m_quitSaveQueue.clear();
        return;
    }

    while (!m_quitSaveQueue.empty())
    {
        QPointer<PDFAppDocumentTab> tab = m_quitSaveQueue.front();
        m_quitSaveQueue.erase(m_quitSaveQueue.begin());

        // A tab closed meanwhile, a document saved meanwhile
        PDFAppDocument* document = tab ? tab->getDocument() : nullptr;
        if (!document || !document->isReady() || !document->isModified())
        {
            continue;
        }

        // A0: a save request of package E (its end exactly once, never inside the call: false also when nothing can
        // be saved or the tab goes first - a deleted tab never leaves the quit waiting)
        m_quitSavingTab = tab;
        PDFAppDocumentFileCommandHandler* handler = tab->prepareSave();
        if (!handler || !handler->requestSave(this, &PDFAppMainWindow::onQuitSaveFinished))
        {
            // Nothing can be saved (no package E): the window stays
            m_quitSavingTab = nullptr;
            m_quitSaveQueue.clear();
            m_leaveFlow->finish(false);
        }
        return;
    }

    // Every document is saved: the window closes
    m_quitSavingTab = nullptr;
    if (!isQuitStep(QuitStep::Saving))
    {
        return;
    }
    m_leaveFlow->finish(true);
    completeQuit(PDFAppLeaveResult::Done);
}

void PDFAppMainWindow::onQuitSaveFinished(bool isSaved)
{
    if (!isQuitStep(QuitStep::Saving))
    {
        return;
    }
    QPointer<PDFAppDocumentTab> tab = m_quitSavingTab;
    m_quitSavingTab = nullptr;

    // A canceled or failed save keeps the window open (its error has been shown), also a tab deleted during its save
    // (not saved): the flow of the quit ends
    if (!isSaved || !tab)
    {
        m_quitSaveQueue.clear();
        if (isQuitStep(QuitStep::Saving))
        {
            m_leaveFlow->finish(false);
        }
        return;
    }

    // A save, which was running before Save All, has written an older version: the
    // document is saved again (the next one in the queue is itself)
    if (tab->getDocument() && tab->getDocument()->isModified())
    {
        m_quitSaveQueue.insert(m_quitSaveQueue.begin(), tab);
    }

    // The next document from the event loop (the saver of the tab finishes its signal first)
    QMetaObject::invokeMethod(this, &PDFAppMainWindow::saveNextForQuit, Qt::QueuedConnection);
}

void PDFAppMainWindow::saveLastSession()
{
    if (!m_context || !m_context->getDatabase())
    {
        return;
    }

    // The files of the document tabs with their pages (a copy without a file is not a part)
    std::vector<PDFWorkspaceDocument> documents;
    for (const PDFAppOpenDocument& openDocument : getOpenDocuments())
    {
        if (!openDocument.path.isEmpty() && documents.size() < size_t(PDFWorkspaceRepository::MAXIMUM_DOCUMENTS))
        {
            PDFWorkspaceDocument document;
            document.path = openDocument.path;
            document.page = qMax(0, openDocument.page);
            documents.push_back(std::move(document));
        }
    }

    const PDFDatabaseError error = m_context->getWorkspaces().saveLastSession(documents);
    if (error.isError())
    {
        qWarning("The last session cannot be stored: %s", qPrintable(error.toString()));
    }
}

void PDFAppMainWindow::storeGeometry()
{
    if (!m_context || !m_context->getDatabase() || !isVisible())
    {
        return;
    }

    // Full screen is stored as the state below it, minimized as the state, which
    // the window restores (Qt keeps the maximized flag of a minimized window)
    const bool isMaximizedState = m_isFullScreenMode ? m_windowStateBeforeFullScreen.testFlag(Qt::WindowMaximized) : windowState().testFlag(Qt::WindowMaximized);
    PDFAppWindowGeometry geometry = PDFAppWindowGeometry::fromWindow(this, isMaximizedState);
    if (geometry.isValid())
    {
        geometry = PDFAppWindowGeometry(getRememberedNormalGeometry(), isMaximizedState, geometry.getScreenName(), geometry.getScreenGeometry());
    }

    const QString text = geometry.toText();
    if (text.isEmpty())
    {
        return;
    }

    PDFSettingsRepository settings = m_context->getSettings();
    if (settings.getString(PDFSettingKeys::windowGeometry()) == text)
    {
        return;
    }

    const PDFDatabaseError error = settings.setValue(PDFSettingKeys::windowGeometry(), text);
    if (error.isError())
    {
        qWarning("The geometry of the window cannot be stored: %s", qPrintable(error.toString()));
    }
}

PDFAppMainWindow* PDFAppMainWindow::getLastActiveMainWindow()
{
    return PDFAppMainWindowHelper::getLastActiveWindow().data();
}

void PDFAppMainWindow::onAboutToQuit()
{
    // The slots of aboutToQuit run in the order of the creation of the windows: an
    // inactive window gives the place to the active one, which may store later
    // No active window at the quit (another application is active, a dialog of
    // its own): the window, which was active last (review RPKT 96-10)
    const PDFAppMainWindow* activeWindow = qobject_cast<const PDFAppMainWindow*>(QApplication::activeWindow());
    if (!activeWindow || !activeWindow->isVisible())
    {
        activeWindow = PDFAppMainWindowHelper::getLastActiveWindow().data();
    }
    if (activeWindow && activeWindow != this && activeWindow->isVisible())
    {
        return;
    }
    storeGeometry();
}

QRect PDFAppMainWindow::getRememberedNormalGeometry() const
{
    // The normal geometry of a maximized, full screen or minimized window is the one
    // seen last in the normal state; the normal geometry of Qt is taken only when
    // the system has moved the window to another screen (Win+Shift+Arrow)
    const QRect normalGeometry = this->normalGeometry().isValid() ? this->normalGeometry() : geometry();
    if (m_normalGeometry.isValid() && windowState().testAnyFlags(Qt::WindowMaximized | Qt::WindowMinimized | Qt::WindowFullScreen) &&
        QGuiApplication::screenAt(m_normalGeometry.center()) == QGuiApplication::screenAt(normalGeometry.center()))
    {
        return m_normalGeometry;
    }
    return normalGeometry;
}

const PDFAppMainWindow* PDFAppMainWindow::findOtherWindow() const
{
    const PDFAppMainWindow* activeWindow = qobject_cast<const PDFAppMainWindow*>(QApplication::activeWindow());
    if (activeWindow && activeWindow != this && activeWindow->isVisible())
    {
        return activeWindow;
    }

    for (QWidget* widget : QApplication::topLevelWidgets())
    {
        const PDFAppMainWindow* window = qobject_cast<const PDFAppMainWindow*>(widget);
        if (window && window != this && window->isVisible())
        {
            return window;
        }
    }
    return nullptr;
}

void PDFAppMainWindow::applyInitialGeometry()
{
    const std::vector<PDFAppWindowGeometry::Screen> screens = PDFAppWindowGeometry::getScreens();

    // Another window of the process (a forwarded request, a new window): next to
    // it, never exactly over it
    if (const PDFAppMainWindow* otherWindow = findOtherWindow())
    {
        const QRect otherGeometry = otherWindow->getRememberedNormalGeometry();
        setGeometry(QRect(PDFAppWindowGeometry::cascade(otherGeometry, screens).topLeft(), otherGeometry.size().expandedTo(minimumSize())));
        m_normalGeometry = geometry();
        return;
    }

    // The first window: the geometry of the last closed window (a value, which
    // cannot be placed, is the default)
    if (m_context && m_context->getDatabase())
    {
        const PDFAppWindowGeometry stored = PDFAppWindowGeometry::fromText(m_context->getSettings().getString(PDFSettingKeys::windowGeometry()));
        QRect geometry;
        if (stored.place(screens, minimumSize(), &geometry))
        {
            setGeometry(geometry);
            m_normalGeometry = geometry;
            if (stored.isMaximized())
            {
                // Shown maximized at once; the geometry above is the normal geometry
                // of Restore (QWidget::setWindowState keeps it)
                setWindowState(windowState() | Qt::WindowMaximized);
            }
            return;
        }
    }

    setGeometry(PDFAppMainWindowHelper::getInitialGeometry(this));
    m_normalGeometry = this->geometry();
}

void PDFAppMainWindow::moveEvent(QMoveEvent* event)
{
    BaseClass::moveEvent(event);
    updateNormalGeometry();
}

void PDFAppMainWindow::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateNormalGeometry();
}

void PDFAppMainWindow::updateNormalGeometry()
{
    // The system maximizes a window (its button, a double click of the caption,
    // Win+Up, Aero Snap at the top edge) by a new geometry, which Qt delivers before
    // the new state: the native state of the window decides
    if (isVisible() && !windowState().testAnyFlags(Qt::WindowMaximized | Qt::WindowMinimized | Qt::WindowFullScreen) &&
        (!m_frame || m_frame->isNativeNormalState()))
    {
        m_normalGeometry = geometry();
    }
}

void PDFAppMainWindow::closeEvent(QCloseEvent* event)
{
    // A0: a flow of the window runs (a question of the quit or of the end of the session is open, Save All runs):
    // the second close is refused and the question comes forward (the flow keeps the server suspended)
    if (!m_isQuitConfirmed && m_leaveFlow && m_leaveFlow->isRunning())
    {
        m_leaveFlow->raiseQuestion();
        event->ignore();
        return;
    }

    // The server stops listening before the questions about unsaved changes and it
    // holds the requests of the connections, which it has accepted before; a
    // process started meanwhile runs by itself
    QPointer<PDFAppInstanceServer> server = m_context ? m_context->getInstanceServer() : nullptr;
    if (server && !m_isClosing)
    {
        server->suspend();
    }
    m_isClosing = true;

    // The close after the last answer of a Pending quit (completeQuit) has announced it already: once per quit
    QPointer<PDFAppMainWindow> guard(this);
    if (!m_isQuitConfirmed)
    {
        Q_EMIT closing();
        if (!guard)
        {
            return;
        }
    }

    // The questions of the quit (A0: shown, no event loop). Pending: the window closes itself after the last answer
    // or the last save (completeQuit), the end of the flow without the quit resumes the server (onLeaveFlowFinished).
    m_isQuitFlowActive = true;
    const PDFAppLeaveResult result = requestQuit();
    if (!guard)
    {
        return;
    }
    if (result == PDFAppLeaveResult::Pending)
    {
        event->ignore();
        return;
    }

    if (result != PDFAppLeaveResult::Done)
    {
        // The user has canceled the quitting (the end of the flow has resumed the server already)
        if (m_isQuitFlowActive)
        {
            m_isQuitFlowActive = false;
            resumeAfterQuit();
        }
        event->ignore();
        return;
    }
    m_isQuitFlowActive = false;

    // The quitting is accepted: the held and the incoming requests are refused, so
    // their processes start by themselves (an acknowledged request would be lost)
    if (server)
    {
        server->quit();
    }

    // The documents of the window are the last session (Restore last session), the
    // geometry of the window is the geometry of the next start
    saveLastSession();
    storeGeometry();

    // The pages record what they must while their tabs are whole (the use of an interactive tool)
    prepareTabsClose();
    event->accept();
}

void PDFAppMainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    // Only local files are offered for a drop (nothing is read during the drag)
    if (PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        setFileDragActive(true);
        return;
    }

    // Annotations of a canvas: the window follows the drag over the tab bar (the
    // drop itself belongs to a canvas, never to the window)
    if (updateDragTabSwitch(event->mimeData(), event->position().toPoint()))
    {
        event->accept();
        return;
    }
    event->ignore();
    setFileDragActive(false);
}

void PDFAppMainWindow::dragLeaveEvent(QDragLeaveEvent* event)
{
    // The pointer has left the window, or the drag was canceled (Esc)
    setFileDragActive(false);
    stopDragTabSwitch();
    BaseClass::dragLeaveEvent(event);
}

bool PDFAppMainWindow::updateDragTabSwitch(const QMimeData* mimeData, const QPoint& windowPosition)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    const bool isAnnotationDrag = mimeData && mimeData->hasFormat(QString::fromLatin1(PDFAnnotationManipulator::getMimeType()));
    if (!isAnnotationDrag || !tabBar)
    {
        stopDragTabSwitch();
        return false;
    }

    // The tab under the pointer becomes current after the delay of the style; a
    // move over the same tab does not start the delay again
    const int index = tabBar->isVisible() ? tabBar->tabAt(tabBar->mapFrom(this, windowPosition)) : -1;
    if (index < 0 || index == tabBar->currentIndex() || !tabBar->isTabEnabled(index))
    {
        stopDragTabSwitch();
        return true;
    }

    if (!m_dragTabTimer)
    {
        m_dragTabTimer = new QTimer(this);
        m_dragTabTimer->setSingleShot(true);
        connect(m_dragTabTimer.data(), &QTimer::timeout, this, &PDFAppMainWindow::onDragTabSwitchTimeout);
    }
    if (index != m_dragTabIndex || !m_dragTabTimer->isActive())
    {
        m_dragTabIndex = index;
        m_dragTabTimer->start(qMax(0, tabBar->style()->styleHint(QStyle::SH_TabBar_ChangeCurrentDelay, nullptr, tabBar)));
    }
    return true;
}

void PDFAppMainWindow::stopDragTabSwitch()
{
    m_dragTabIndex = -1;
    if (m_dragTabTimer)
    {
        m_dragTabTimer->stop();
    }
}

void PDFAppMainWindow::onDragTabSwitchTimeout()
{
    // The tabs may have changed meanwhile (a closed tab)
    PDFDocumentTabBar* tabBar = getTabBar();
    const int index = m_dragTabIndex;
    m_dragTabIndex = -1;
    if (tabBar && index >= 0 && index < tabBar->count() && tabBar->isTabEnabled(index))
    {
        tabBar->setCurrentIndex(index);
    }
}

void PDFAppMainWindow::setFileDragActive(bool isActive)
{
    if (m_isFileDragActive == isActive)
    {
        return;
    }

    // The drop zones of all tabs (a hidden tab shows it, when it is switched to)
    m_isFileDragActive = isActive;
    for (PDFAppDropZone* dropZone : findChildren<PDFAppDropZone*>())
    {
        dropZone->setDropActive(isActive);
    }
}

void PDFAppMainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }

    // Annotations over the tab bar: no drop here, the tab under the pointer becomes current
    updateDragTabSwitch(event->mimeData(), event->position().toPoint());
    event->ignore();
}

void PDFAppMainWindow::dropEvent(QDropEvent* event)
{
    setFileDragActive(false);
    stopDragTabSwitch();
    if (!PDFAppFileDrop::hasLocalFiles(event->mimeData()) && event->mimeData() &&
        event->mimeData()->hasFormat(QString::fromLatin1(PDFAnnotationManipulator::getMimeType())))
    {
        // Annotations dropped outside of a canvas: nothing happens
        event->ignore();
        return;
    }
    // PDF files and the assembly files of Manage Pages (openDocuments hands them to openAssembly)
    const PDFAppFileDrop::Result result = PDFAppFileDrop::getFiles(event->mimeData(), PDFAppFileDrop::MAXIMUM_FILES, false, true);
    event->setDropAction(Qt::CopyAction);
    event->accept();

    // The refused items are listed (folders, other files, files of other computers)
    if (!result.refused.empty())
    {
        QStringList items;
        for (const PDFAppFileDrop::RefusedItem& item : result.refused)
        {
            items << tr("%1 – %2").arg(item.text, PDFAppFileDrop::getRefusalText(item.refusal));
        }
        showMessage(int(PDFThemeIcons::Badge::Warning), tr("Some items were not opened"),
                    tr("Only PDF files and assemblies on this computer can be opened. These items were left out:"),
                    PDFAppMainWindowHelper::createList(items, 20));
    }

    if (!result.files.isEmpty())
    {
        openDocumentsOfPage(result.files);
    }
}

bool PDFAppMainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (m_frame && m_frame->handleNativeEvent(eventType, message, result))
    {
        return true;
    }
    return BaseClass::nativeEvent(eventType, message, result);
}

void PDFAppMainWindow::updateWindowState()
{
    if (!m_topBar)
    {
        return;
    }

    if (PDFWindowControls* controls = m_topBar->getWindowControls())
    {
        controls->setMaximized(isMaximized() || isFullScreen());
        controls->setResizable(minimumSize() != maximumSize());
    }
}

void PDFAppMainWindow::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::WindowStateChange)
    {
        updateWindowState();
    }
    else if (event->type() == QEvent::LayoutDirectionChange)
    {
        updateActionIcons();
    }
    else if (event->type() == QEvent::ActivationChange && isActiveWindow())
    {
        PDFAppMainWindowHelper::getLastActiveWindow() = this;
    }
}

bool PDFAppMainWindow::event(QEvent* event)
{
    // Back / forward buttons of the mouse sent to the window itself (the buttons
    // pressed over any child are taken by the filter of the QWindow)
    if (handleNavigationButton(event))
    {
        return true;
    }

    // The native window is created, when the window is shown (or again after a
    // change of its flags): the frame changes it then (initialize never creates it)
    if (event->type() == QEvent::WinIdChange && m_frame && internalWinId())
    {
        m_frame->initialize();
    }

    if (event->type() == QEvent::WinIdChange || event->type() == QEvent::Show)
    {
        updateWindowFilter();
    }

    return BaseClass::event(event);
}

bool PDFAppMainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // Events of the QWindow come before Qt delivers them to the child under the
    // pointer: a child (the search field) would take the buttons back / forward
    if (watched && watched == m_filteredWindow && handleNavigationButton(event))
    {
        return true;
    }

    // The modal dialog, for which the opening of files waits, has been closed
    if (watched && watched == m_watchedModal && event->type() == QEvent::Hide)
    {
        QMetaObject::invokeMethod(this, &PDFAppMainWindow::processPendingRequests, Qt::QueuedConnection);
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppMainWindow::updateWindowFilter()
{
    QWindow* window = windowHandle();
    if (window == m_filteredWindow)
    {
        return;
    }

    if (m_filteredWindow)
    {
        m_filteredWindow->removeEventFilter(this);
    }
    m_filteredWindow = window;
    if (window)
    {
        // The QWindow is no child of the widget: the filter goes through the helper (step A)
        PDFDeferredDeletion::installEventFilter(window, this, this);
    }
}

bool PDFAppMainWindow::handleNavigationButton(QEvent* event)
{
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease && event->type() != QEvent::MouseButtonDblClick)
    {
        return false;
    }

    const QMouseEvent* mouseEvent = static_cast<const QMouseEvent*>(event);
    const Qt::MouseButton button = mouseEvent->button();
    if (button != Qt::BackButton && button != Qt::ForwardButton)
    {
        return false;
    }

    // The press navigates, the release and the double click of the button are
    // taken too (no child gets a part of the click); the navigation may delete
    // anything, nothing is touched after it
    if (event->type() == QEvent::MouseButtonPress && m_navigator)
    {
        if (button == Qt::BackButton)
        {
            m_navigator->goBack();
        }
        else
        {
            m_navigator->goForward();
        }
    }
    return true;
}

void PDFAppMainWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // The background of a top level widget would be the leak color of the palette
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgCanvas));
}

}   // namespace pdf

#include "moc_pdfappmainwindow.cpp"
