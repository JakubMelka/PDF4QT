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
#include "pdfappfiledrop.h"
#include "pdfappnavigator.h"
#include "pdfapptab.h"
#include "pdfapptopbar.h"
#include "pdfappwindowframe.h"
#include "pdfdatabasenotifier.h"
#include "pdfshortcutrepository.h"

#include "pdfcomponentpanels.h"
#include "pdfcomponentwindow.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"

#include <QAction>
#include <QCloseEvent>
#include <QCursor>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWindow>

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

    /// Commands bound to the actions of the window (step 1)
    static QStringList getWindowCommands()
    {
        return { PDFAppCommands::newTab(), PDFAppCommands::closeTab(), PDFAppCommands::nextTab(), PDFAppCommands::previousTab(),
                 PDFAppCommands::quit(), PDFAppCommands::open(), PDFAppCommands::back(), PDFAppCommands::forward(), PDFAppCommands::search(),
                 PDFAppCommands::help() };
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
    setGeometry(PDFAppMainWindowHelper::getInitialGeometry(this));

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
    connect(m_topBar, &PDFAppTopBar::newTabRequested, this, &PDFAppMainWindow::onNewTabRequested);
    connect(m_topBar, &PDFAppTopBar::maximizeRestoreRequested, this, &PDFAppMainWindow::onMaximizeRestoreRequested);
    connect(m_topBar, &PDFAppTopBar::moveRequested, this, &PDFAppMainWindow::onMoveRequested);

    PDFWindowControls* controls = m_topBar->getWindowControls();
    connect(controls, &PDFWindowControls::minimizeRequested, this, &PDFAppMainWindow::onMinimizeRequested);
    connect(controls, &PDFWindowControls::maximizeRestoreRequested, this, &PDFAppMainWindow::onMaximizeRestoreRequested);
    connect(controls, &PDFWindowControls::closeRequested, this, &PDFAppMainWindow::onCloseRequested);

    createCommands();
    updateShortcuts();

    if (context)
    {
        if (PDFDatabaseNotifier* notifier = context->getNotifier())
        {
            connect(notifier, &PDFDatabaseNotifier::changed, this, &PDFAppMainWindow::onDatabaseChanged);
            connect(notifier, &PDFDatabaseNotifier::everythingChanged, this, &PDFAppMainWindow::onEverythingChanged);
        }
        connect(context, &PDFAppContext::openRequested, this, &PDFAppMainWindow::onOpenRequested);
    }
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
    // Document tabs (step 3) and tools with their own tab (phase 2) do not exist yet
    Q_UNUSED(id);
    return nullptr;
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

    for (const QString& id : PDFAppMainWindowHelper::getWindowCommands())
    {
        const PDFAppCommand* command = registry->findCommand(id);
        if (!command)
        {
            continue;
        }

        QAction* action = new QAction(command->getTitle(), this);
        action->setObjectName(id);
        action->setData(id);
        action->setShortcutContext(Qt::WindowShortcut);
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
        if (item.second)
        {
            item.second->setShortcut(registry->getShortcut(item.first, userShortcuts));
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

void PDFAppMainWindow::onCurrentTabChanged(int index)
{
    PDFDocumentTabBar* tabBar = getTabBar();
    PDFAppTab* tab = tabBar ? findTab(tabBar->tabData(index).toULongLong()) : nullptr;
    if (tab && m_stack)
    {
        m_stack->setCurrentWidget(tab);
    }
    updateWindowTitle();
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
    // (newapplication.md 3); the lists are bounded
    for (const QString& error : request.errors)
    {
        qWarning("Command line of another process: %s", qPrintable(error));
        if (m_forwardedErrors.size() < MAXIMUM_FORWARDED_ERRORS)
        {
            m_forwardedErrors << error;
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

    if (!request.errors.isEmpty())
    {
        showCommandLineErrors(request.errors);
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

    const PDFAppCommandLine& commandLine = m_context->getCommandLine();
    const QStringList errors = commandLine.getErrorMessages();
    if (!errors.isEmpty())
    {
        showCommandLineErrors(errors);
    }

    if (!commandLine.getFiles().isEmpty())
    {
        openDocuments(commandLine.getFiles(), commandLine.getPage());
    }
}

void PDFAppMainWindow::openDocuments(const QStringList& files, int page)
{
    // ===================================================================================
    // STEP 3 HOOK: documents are opened by the document tab of step 3 (newapplication.md 4).
    // Until then the files are listed in a message box and kept in getRequestedFiles.
    // ===================================================================================
    Q_UNUSED(page);

    // Step 2: the files are recorded in Recent (so Home, Open and Recent have real
    // data) and their thumbnails are made by the thumbnailer (worker thread); the
    // pages read them after the notification of the database
    PDFAppThumbnailer* thumbnailer = m_context ? m_context->getThumbnailer() : nullptr;
    const bool hasDatabase = m_context && m_context->getDatabase();
    int recorded = 0;

    for (const QString& file : files)
    {
        if (!m_requestedFiles.contains(file) && m_requestedFiles.size() < PDFAppInstanceProtocol::MAXIMUM_FILES)
        {
            m_requestedFiles << file;
        }

        // Only an existing file (a missing file of a command line is not recorded),
        // at most the number of the files of one request
        if (!hasDatabase || recorded >= PDFAppInstanceProtocol::MAXIMUM_FILES || !QFileInfo(file).isFile())
        {
            continue;
        }
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

    showMessage(int(PDFThemeIcons::Badge::Information), tr("Documents cannot be opened yet"),
                tr("This version of PDF4QT does not open documents yet. The following files were requested:"),
                PDFAppMainWindowHelper::createList(files, 20));
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
    // ===================================================================================
    // STEP 3 HOOK: the document tabs of step 3 give their documents here.
    // ===================================================================================
    return std::vector<PDFAppOpenDocument>();
}

void PDFAppMainWindow::showCommandLineErrors(const QStringList& errors)
{
    showMessage(int(PDFThemeIcons::Badge::Warning), tr("Some arguments of the command line were not used"),
                PDFAppMainWindowHelper::createList(errors, 10), QString());
}

void PDFAppMainWindow::showMessage(int badge, const QString& title, const QString& text, const QString& details)
{
    // The message box does not block the caller (a request of another process,
    // the start of the application); it deletes itself, when it is closed
    PDFMessageBox* messageBox = new PDFMessageBox(static_cast<PDFThemeIcons::Badge>(badge), title, text, QDialogButtonBox::Ok, this);
    messageBox->setObjectName(QStringLiteral("messageBox"));
    if (!details.isEmpty())
    {
        messageBox->setDetailedText(details);
    }
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    messageBox->open();
}

bool PDFAppMainWindow::confirmClose()
{
    // STEP 3 HOOK: questions about unsaved documents (pdf4qt-quit-unsaved 955:145378)
    return true;
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
    event->accept();
}

void PDFAppMainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    // Only local files are offered for a drop (nothing is read during the drag)
    if (PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    event->ignore();
}

void PDFAppMainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    event->ignore();
}

void PDFAppMainWindow::dropEvent(QDropEvent* event)
{
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
