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
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfapphomewidgets.h"
#include "pdfappnavigator.h"
#include "pdfapptab.h"
#include "pdfapptopbar.h"
#include "pdfappwindowframe.h"
#include "pdfappwindowgeometry.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfshortcutrepository.h"
#include "pdfworkspacerepository.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentwindow.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include "pdfannotationmanipulator.h"

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

namespace pdf
{

/// Helpers of the main window
class PDFAppMainWindowHelper
{
public:
    PDFAppMainWindowHelper() = delete;

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

    if (context)
    {
        if (PDFDatabaseNotifier* notifier = context->getNotifier())
        {
            connect(notifier, &PDFDatabaseNotifier::changed, this, &PDFAppMainWindow::onDatabaseChanged);
            connect(notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppMainWindow::onEverythingChanged);
        }
        connect(context, &PDFAppContext::openRequested, this, &PDFAppMainWindow::onOpenRequested);
    }

    // The geometry is stored also, when the application quits without the closing
    // of the window (QCoreApplication::quit, the end of the session of the system)
    connect(qApp, &QCoreApplication::aboutToQuit, this, &PDFAppMainWindow::onAboutToQuit);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppMainWindow::update));

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
    if (!tab || !tabBar)
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
    PDFDocumentTabBar* tabBar = getTabBar();
    if (!tab || !tabBar || !tab->isClosable())
    {
        return false;
    }

    if (findTabIndex(tab->getId()) < 0)
    {
        return false;
    }

    // The tab asks about its unsaved changes and running operations (step 3); its
    // dialog runs an event loop, the tab or the window may be deleted meanwhile
    QPointer<PDFAppMainWindow> windowGuard(this);
    QPointer<PDFAppTab> tabGuard(tab);
    const bool isConfirmed = tab->confirmClose();
    if (!windowGuard || !tabGuard)
    {
        return false;
    }
    if (!isConfirmed)
    {
        return false;
    }

    const int index = findTabIndex(tab->getId());
    if (index < 0)
    {
        return false;
    }

    // The tab may be the sender of the request (its close button, its page): deleted later
    m_tabIds.erase(tab);
    for (qsizetype i = m_tabs.size() - 1; i >= 0; --i)
    {
        if (m_tabs[i].isNull() || m_tabs[i].data() == tab)
        {
            m_tabs.removeAt(i);
        }
    }
    tab->disconnect(this);

    QPointer<PDFAppMainWindow> guard(this);
    tabBar->removeTab(index);
    if (!guard)
    {
        return true;
    }

    if (m_stack)
    {
        m_stack->removeWidget(tab);
    }
    tab->hide();
    tab->deleteLater();
    updateWindowTitle();
    return true;
}

PDFAppTab* PDFAppMainWindow::findDocumentTab(const QString& id) const
{
    // Tools with their own tab (phase 2) do not exist yet
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
        closeTab(getCurrentTab());
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
    else if (id == PDFAppCommands::help() && m_navigator)
    {
        // The manual opens in the Help tab, the current tab stays as it is
        m_navigator->navigate(PDFAppRoutes::help(PDFAppRoutes::getDefaultHelpTopic()));
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
        closeTab(findTab(tabBar->tabData(index).toULongLong()));
    }
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

    if (!commandLine.getFiles().isEmpty())
    {
        openDocuments(commandLine.getFiles(), commandLine.getPage());
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

    // A modal dialog runs its own event loop (a question of a tab, a dialog of a
    // page): the files are opened, when it closes (the window is raised already)
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
            if (m_navigator && request.page > 0)
            {
                m_navigator->navigate(PDFAppRoutes::document(document->getId(), request.page), PDFAppNavigationMode::Push, tab);
            }
            return;
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
        m_navigator->navigate(PDFAppRoutes::document(tab->getDocumentId(), page), PDFAppNavigationMode::Push, tab);
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

QWidget* PDFAppMainWindow::getBlockingModalWidget() const
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
        widget->installEventFilter(this);
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
    if (tab)
    {
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
        delete documentTab;
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

PDFAppDocumentTab* PDFAppMainWindow::openDocumentFromMemory(QByteArray bytes, const QString& title, const QString& originFilePath, const PDFAppDocument* source)
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

    // The document opens after the tab of its source (the user may have switched
    // the tab, while the attachment was decoded)
    const int index = getTabIndexAfter(source);
    PDFAppDocumentTab* documentTab = new PDFAppDocumentTab(m_context, m_stack);
    QPointer<PDFAppDocumentTab> tab(documentTab);
    connectDocumentTab(documentTab);
    addTab(documentTab, index);

    QPointer<PDFAppMainWindow> guard(this);
    if (!tab || !tab->openFromMemory(std::move(bytes), title, originFilePath))
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

    // The native dialog runs its own event loop (a request of another process, a
    // drop or the code of a page may delete the window meanwhile)
    QPointer<PDFAppMainWindow> guard(this);
    m_isBrowsingDocuments = true;
    const QStringList files = PDFAppFileDialogs::getOpenFileNames(this, folder);
    if (!guard)
    {
        return;
    }
    m_isBrowsingDocuments = false;

    if (!files.isEmpty())
    {
        openDocuments(files, 0);
    }
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

bool PDFAppMainWindow::confirmClose()
{
    // Save All has saved every document (the window is closed again by it)
    if (m_isQuitConfirmed)
    {
        return true;
    }

    // A Save All runs: the window waits for it
    if (m_quitSavingTab)
    {
        return false;
    }

    // Open editors of form fields are changes too (step 4, package D); a slot
    // may delete a tab or the window
    QPointer<PDFAppMainWindow> pendingGuard(this);
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
            if (!pendingGuard)
            {
                return false;
            }
        }
    }

    // The documents with unsaved changes: pdf4qt-quit-unsaved 955:145378
    std::vector<const PDFAppDocument*> documents;
    std::vector<QPointer<PDFAppDocumentTab>> tabs;
    for (PDFAppDocumentTab* tab : getDocumentTabs())
    {
        const PDFAppDocument* document = tab->getDocument();
        if (document && document->isReady() && document->isModified())
        {
            documents.push_back(document);
            tabs.emplace_back(tab);
        }
    }
    if (documents.empty())
    {
        return true;
    }

    QPointer<PDFAppMainWindow> guard(this);
    const PDFAppDocumentChoice choice = PDFAppDocumentChoiceDialog::run(new PDFAppQuitUnsavedDialog(documents, this));
    if (!guard)
    {
        return false;
    }

    switch (choice)
    {
        case PDFAppDocumentChoice::Discard:
            return true;

        case PDFAppDocumentChoice::Save:
            // The documents are saved one by one; the window closes after the last one
            m_quitSaveQueue = std::move(tabs);
            saveNextForQuit();
            return false;

        default:
            return false;
    }
}

void PDFAppMainWindow::saveNextForQuit()
{
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

        m_quitSavingTab = tab;
        connect(tab.data(), &PDFAppDocumentTab::saveFinished, this, &PDFAppMainWindow::onQuitSaveFinished, Qt::UniqueConnection);
        QPointer<PDFAppMainWindow> guard(this);
        const bool isStarted = tab->save();
        if (!guard)
        {
            return;
        }
        if (!isStarted)
        {
            // The document cannot be saved now (locked, no file to write): the window stays
            if (tab)
            {
                disconnect(tab.data(), &PDFAppDocumentTab::saveFinished, this, &PDFAppMainWindow::onQuitSaveFinished);
            }
            m_quitSavingTab = nullptr;
            m_quitSaveQueue.clear();
        }
        return;
    }

    // Every document is saved: the window closes
    m_quitSavingTab = nullptr;
    m_isQuitConfirmed = true;
    QPointer<PDFAppMainWindow> guard(this);
    close();
    if (guard)
    {
        m_isQuitConfirmed = false;
    }
}

void PDFAppMainWindow::onQuitSaveFinished(bool isSaved)
{
    PDFAppDocumentTab* tab = qobject_cast<PDFAppDocumentTab*>(sender());
    if (!tab || tab != m_quitSavingTab)
    {
        return;
    }
    disconnect(tab, &PDFAppDocumentTab::saveFinished, this, &PDFAppMainWindow::onQuitSaveFinished);
    m_quitSavingTab = nullptr;

    // A canceled or failed save keeps the window open (its error has been shown)
    if (!isSaved)
    {
        m_quitSaveQueue.clear();
        return;
    }

    // A save, which was running before Save All, has written an older version: the
    // document is saved again (the next one in the queue is itself)
    if (tab->getDocument() && tab->getDocument()->isModified())
    {
        m_quitSaveQueue.insert(m_quitSaveQueue.begin(), QPointer<PDFAppDocumentTab>(tab));
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

void PDFAppMainWindow::onAboutToQuit()
{
    // The slots of aboutToQuit run in the order of the creation of the windows: an
    // inactive window gives the place to the active one, which may store later
    const PDFAppMainWindow* activeWindow = qobject_cast<const PDFAppMainWindow*>(QApplication::activeWindow());
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
    // The server stops listening before the questions about unsaved changes and it
    // holds the requests of the connections, which it has accepted before; a
    // process started meanwhile runs by itself
    QPointer<PDFAppInstanceServer> server = m_context ? m_context->getInstanceServer() : nullptr;
    if (server && !m_isClosing)
    {
        server->suspend();
    }
    m_isClosing = true;

    QPointer<PDFAppMainWindow> guard(this);
    Q_EMIT closing();
    if (!guard)
    {
        return;
    }

    const bool isConfirmed = confirmClose();
    if (!guard)
    {
        return;
    }

    if (!isConfirmed)
    {
        // The user has canceled the quitting: the held requests are opened and the
        // server listens again (another instance may have taken the name meanwhile,
        // then this one runs without the forwarding)
        m_isClosing = false;
        if (server)
        {
            QString error;
            if (!server->resume(PDFAppInstanceServer::DEFAULT_RETRY_LIMIT, &error))
            {
                qInfo("The running instance does not receive forwarded files now: %s", qPrintable(error));
            }
        }
        event->ignore();
        return;
    }

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
    const PDFAppFileDrop::Result result = PDFAppFileDrop::getFiles(event->mimeData());
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
                    tr("Only PDF files on this computer can be opened. These items were left out:"),
                    PDFAppMainWindowHelper::createList(items, 20));
    }

    if (!result.files.isEmpty())
    {
        openDocuments(result.files, 0);
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
        window->installEventFilter(this);
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
