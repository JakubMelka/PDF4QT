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

#ifndef PDFAPPMAINWINDOW_H
#define PDFAPPMAINWINDOW_H

#include "pdfappglobal.h"
#include "pdfappfiledrop.h"
#include "pdfappinstance.h"
#include "pdfappnavigator.h"
#include "pdfapproutetable.h"
#include "pdfapptab.h"
#include "pdfapptopbar.h"
#include "pdfappwindowframe.h"

#include <QList>
#include <QPointer>
#include <QStringList>
#include <QAction>
#include <QStackedWidget>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <map>
#include <vector>

namespace pdf
{

class PDFMessageBox;
class PDFAppDocument;
class PDFAppDocumentTab;
class PDFAppOpenFailedDialog;
class PDFAppOpenFailure;
class PDFAppToolInputList;
class PDFAppToolTab;

/// Main window of the application (newapplication.md 3, decisions 9 and 12): one
/// window per process without the frame of the system, the top bar with the tabs
/// (the Home tab is not closable, tabs New tab and later documents and tools),
/// the New tab button and the window controls, below it the content of the
/// current tab. The window owns the navigator, binds the commands of the
/// registry to its actions with the shortcuts of the user (updated by the
/// notifier of the database), receives the requests of other processes (raises
/// and activates itself, shows the errors of their command lines) and suspends
/// the server of the running instance while it is being closed.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppMainWindow : public QWidget
{
    Q_OBJECT

private:
    using BaseClass = QWidget;

public:
    /// Maximal number of errors of other processes kept by the window
    static constexpr int MAXIMUM_FORWARDED_ERRORS = 100;

    /// Maximal number of tabs (the window refuses more New tabs)
    static constexpr int MAXIMUM_TABS = 200;

    /// Delay, which joins the failures of the files read in the workers into one dialog
    static constexpr int OPEN_FAILURE_DELAY = 100;

    explicit PDFAppMainWindow(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppMainWindow() override;

    PDFAppContext* getContext() const { return m_context; }
    PDFAppNavigator* getNavigator() const { return m_navigator; }
    PDFAppTopBar* getTopBar() const { return m_topBar; }
    PDFAppWindowFrame* getFrame() const { return m_frame; }
    PDFDocumentTabBar* getTabBar() const;

    /// The Home tab (it is created again, if the application has deleted it)
    PDFAppWorkspaceTab* getHomeTab();

    /// Body of the window below the top bar (the content of the tabs): the dialogs
    /// of the window cover it by their scrim and center their card over it, the top
    /// bar with the tabs and the buttons of the window stays uncovered (Figma
    /// app-body of pdf4qt-recent-clear-history 941:135742)
    QWidget* getDialogHost() const;

    /// Body of the main window of the widget (nullptr outside of a main window)
    static QWidget* findDialogHost(const QWidget* widget);

    /// Prepares a dialog of the window of the widget: the scrim covers the body
    /// of the window (nothing happens outside of a main window or for a null box)
    static void prepareDialog(PDFMessageBox* messageBox, const QWidget* widget);

    /// Local files are dragged over the window (the drop zones are highlighted;
    /// it ends by the leave of the drag, by the drop and by Esc, which the system
    /// reports as the leave)
    bool isFileDragActive() const { return m_isFileDragActive; }

    /// Current tab
    PDFAppTab* getCurrentTab() const;

    /// Tabs in the order of the tab bar
    QList<PDFAppTab*> getTabs() const;

    /// Adds a tab New tab after the current one (not shown, no route yet).
    /// \returns nullptr, if there are too many tabs.
    PDFAppTab* createNewTab();

    /// Command New tab: a new tab New tab with /open, activated
    PDFAppTab* openNewTab();

    /// The Help tab of the window (nullptr, if there is none)
    PDFAppTab* findHelpTab() const;

    /// Adds the Help tab after the current tab (not shown, no route yet).
    /// \returns nullptr, if there are too many tabs.
    PDFAppTab* createHelpTab();

    /// Makes the tab current
    void activateTab(PDFAppTab* tab);

    /// The modal dialog, for which the opening of a document and the dialog of the result of a
    /// tool wait (a message box, which only informs, is not one), nullptr if none
    static QWidget* getBlockingModalWidget();

    /// Closes the tab (not the Home tab). \returns false, if it is not closed.
    bool closeTab(PDFAppTab* tab);

    /// Tab of the document id (step 3); nullptr if there is none (the own tab of a tool
    /// is found by findToolTab)
    PDFAppTab* findDocumentTab(const QString& id) const;

    /// Own tab of a tool (plan of step 2 of phase 2, 4.1, R13): a new tab after the current
    /// one with the route /tool/{toolId}/{a new instance id}, which is shown in it and
    /// made current in this call (within a navigation the route is shown after it). The tab
    /// takes \p inputList over (setParent in this one synchronous call; nullptr = an empty
    /// list, the empty state 378:1657). The navigation may run code of the application.
    /// \returns the tab; nullptr for a tool without its own tab or its GUI, or too many tabs:
    /// \p inputList then stays with the caller.
    PDFAppToolTab* createToolTab(const QString& toolId, PDFAppToolInputList* inputList);

    /// Adds an own tab of a tool of the instance (the route /tool/{toolId}/{instanceId} of
    /// the history or of a link, which has no tab: the empty state 378:1657) after the
    /// current tab, not shown, no route yet. \returns nullptr as createToolTab.
    PDFAppToolTab* addToolTab(const QString& toolId, const QString& instanceId, PDFAppToolInputList* inputList);

    /// createToolTab with the snapshot of the document of \p documentTab as the first input
    /// (R11: a document, which is not read yet, is not added); a tool with its own tab asked
    /// for over a document (the panel Tools, Then continue with, /document/{id}/tool/{toolId})
    PDFAppToolTab* createToolTabForDocument(const QString& toolId, PDFAppDocumentTab* documentTab);

    /// Own tab of the instance of a tool, nullptr if there is none
    PDFAppToolTab* findToolTab(const QString& instanceId) const;

    /// Own tabs of tools of the window in the order of the tab bar
    QList<PDFAppToolTab*> getToolTabs() const;

    /// The own tab of a tool, whose result needs no decision (Blank PDF, 4.6), is replaced
    /// by a document tab at the same index, which reads the document from the memory
    /// \p bytes and behaves as a copy titled \p title (no file, Save As "<title>.pdf", not
    /// recorded in Recent, unsaved); the new tab becomes current, the tool tab is removed
    /// from the tab bar and deleted later (it may be the sender). Nothing asks: the tool
    /// has accepted its result. \returns the new tab, nullptr (the tool tab stays) when
    /// \p tab is not a tab of this window.
    PDFAppDocumentTab* replaceToolTab(PDFAppToolTab* tab, QByteArray bytes, const QString& title);

    /// Document tabs of the window in the order of the tab bar
    QList<PDFAppDocumentTab*> getDocumentTabs() const;

    /// Updates the enabled / checked state of the actions of the commands of the
    /// documents from the current tab (Document and Canvas scope)
    void updateCommandStates();

    /// Updates the title and the icon of the tab in the tab bar and the window title
    void updateTab(PDFAppTab* tab);

    /// Action of the command (nullptr for an unknown command)
    QAction* getCommandAction(const QString& commandId) const;

    /// Ctrl+K: the global search of the current tab, or of the Home tab
    void focusSearch();

    /// F1: the topic of the user manual of the page of the current tab (the default
    /// topic without a page) in the Help tab, the current tab stays as it is. A page
    /// showing its topic stays with its anchor and its scroll position. The navigation
    /// may delete the window.
    void showHelp();

    /// Maximizes or restores the window
    void toggleMaximized();

    /// Full screen of a document (F11, step 3): the top bar is hidden and the window
    /// covers the screen; false shows the top bar and restores the previous state
    /// of the window (normal or maximized)
    void setFullScreenMode(bool isFullScreen);
    bool isFullScreenMode() const { return m_isFullScreenMode; }

    /// Files of the command line and of other processes waiting for the opening
    /// of documents (step 3) and the errors of the command lines of other processes
    const QStringList& getRequestedFiles() const { return m_requestedFiles; }
    const QStringList& getForwardedErrors() const { return m_forwardedErrors; }

    /// Processes a request of another process (the slot of PDFAppContext::openRequested)
    void handleOpenRequest(const pdf::PDFAppInstanceRequest& request);

    /// Opens the documents (absolute paths; \p page from 1, 0 = the last page of
    /// Recent): the command line, other processes, the pages Home / Open / Recent
    /// and the files dropped onto the window. Each existing file is recorded in
    /// Recent (PDFRecentRepository::recordOpened) and its thumbnail is requested.
    /// The files are kept in getRequestedFiles (bounded) for the tests.
    void openDocuments(const QStringList& files, int page);

    /// Opens the documents of the requests (a page per file - workspaces): a file,
    /// which is open in the window already, activates its tab (and goes to the
    /// page, if the request has one), any other file gets a new document tab after
    /// the current tab (an empty tab New tab is replaced by it), which reads the
    /// file in a worker thread. The page: the page of the request (--page, a
    /// workspace), else the last page of the file in Recent; a page out of range is
    /// ignored. The files, which cannot be opened (missing, not a PDF, damaged),
    /// are listed in the dialog open-failed (1028:24297). While a modal dialog is
    /// open, the requests wait and are opened, when it closes.
    void openDocuments(const std::vector<PDFAppOpenRequest>& requests);

    /// Opens the files chosen or dropped while the current page is shown: the first one
    /// goes to the tool of the page (PDFAppPage::getOpenFilesToolId)
    void openDocumentsOfPage(const QStringList& files);

    /// Opens an assembly file of Manage Pages (plan of step 3b of phase 2, 4.6, RB3 e): a double
    /// click on the file, the command line, the Open dialog and a drop come through
    /// openDocuments, which hands every file of the suffix .pdf4qtassembly
    /// (PDFAppAssemblyLoader::isAssemblyFile) here. The own tab of pages.manage (createToolTab),
    /// its GUI loads the file (PDFAppAssemblyLoader), the file is recorded in the recent
    /// assemblies. A file open in a tab already (PDFAppManageToolGUI::findAssemblyTab: also while
    /// it loads) activates that tab, of this window or another one. Code of the application
    /// runs: the window may be deleted meanwhile (the caller guards it).
    /// \param filePath The assembly file (absolute)
    /// \param errorText Translated reason, when it is not opened (empty = the default reason of the caller)
    /// \returns true, when the file is opened (or its open has started)
    bool openAssembly(const QString& filePath, QString* errorText);

    /// Requests waiting for the closing of a modal dialog
    size_t getPendingRequestCount() const { return m_pendingRequests.size(); }

    /// Lists the failures in the dialog open-failed (the failures of the files read
    /// in the workers are joined into one dialog; a dialog, which is open, is
    /// followed by the next one, when it closes)
    void reportOpenFailures(const std::vector<PDFAppOpenFailure>& failures);

    /// The dialog open-failed, which is shown (nullptr if none)
    PDFAppOpenFailedDialog* getOpenFailedDialog() const;

    /// Edit a Copy: a new document tab after the tab of the document with a copy
    /// of its current version (no file, modified). \returns the tab, nullptr if
    /// the document is not shown or there are too many tabs.
    PDFAppDocumentTab* openDocumentCopy(const PDFAppDocument* source);

    /// Index of a new tab after the tab of \p source (after the current tab, when
    /// \p source is not shown in this window)
    int getTabIndexAfter(const PDFAppDocument* source) const;

    /// Index of a new tab after \p tab (the own tab of a tool, plan of step 2 of phase 2,
    /// 4.5), after the current tab, when \p tab is null or not a tab of this window
    int getTabIndexAfter(const PDFAppTab* tab) const;

    /// A PDF attachment (O18): a new document tab after the tab of \p source (the
    /// current tab, when \p source is not shown in this window), which reads the
    /// document from the memory \p bytes (no temporary file) and behaves as a copy
    /// titled \p title (no file, Save As, not recorded in Recent).
    /// \returns the tab, nullptr when there are too many tabs.
    PDFAppDocumentTab* openDocumentFromMemory(QByteArray bytes, const QString& title, const QString& originFilePath, const PDFAppDocument* source = nullptr);

    /// The same at the index \p index of the tab bar (after the own tab of a tool, 4.5)
    PDFAppDocumentTab* openDocumentFromMemoryAt(QByteArray bytes, const QString& title, const QString& originFilePath, int index);

    /// Stores the documents of the window as the last session (Restore last
    /// session of Home / Open): the files with their current pages
    void saveLastSession();

    /// Stores the geometry of the visible window (PDFAppWindowGeometry, the state
    /// setting window.geometry): its normal geometry, maximized (also below the
    /// full screen and the minimized state) and its screen. Called, when the
    /// closing of the window is accepted and when the application quits; the last
    /// window closed in any process wins.
    void storeGeometry();

    /// The main window of the process, which was active last (O29; the end of the
    /// session of the system stores its state - PDFAppSessionEnd), nullptr if none
    static PDFAppMainWindow* getLastActiveMainWindow();

    /// Command Open… (Ctrl+O): the native dialog of the system for PDF files in the
    /// folder (empty = the default folder of the system); the chosen files are
    /// opened (openDocuments). The dialog runs its own event loop: a second request
    /// while it is shown does nothing, the window may be deleted meanwhile.
    void browseDocuments(const QString& folder = QString());

    /// A dialog of browseDocuments is shown
    bool isBrowsingDocuments() const { return m_isBrowsingDocuments; }

    /// Documents open in the window, in the order of their tabs (Save as workspace
    /// 941:136915)
    std::vector<PDFAppOpenDocument> getOpenDocuments() const;

Q_SIGNALS:
    /// The window has started to close (the server of the instance is suspended)
    void closing();

protected:
    virtual bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void closeEvent(QCloseEvent* event) override;
    virtual void moveEvent(QMoveEvent* event) override;
    virtual void resizeEvent(QResizeEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool event(QEvent* event) override;
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

    /// Drag and drop of PDF files onto the window (PDFAppFileDrop): local files
    /// only, a PDF by its extension or content, folders and other files refused.
    /// Annotations dragged over a tab of the tab bar switch to the tab after a
    /// short delay (the drop goes to the canvas of the tab, note 1030:161012).
    virtual void dragEnterEvent(QDragEnterEvent* event) override;
    virtual void dragMoveEvent(QDragMoveEvent* event) override;
    virtual void dragLeaveEvent(QDragLeaveEvent* event) override;
    virtual void dropEvent(QDropEvent* event) override;

    /// Asked, when the window is being closed: false = the user has canceled the
    /// quitting. Documents with unsaved changes are listed in pdf4qt-quit-unsaved
    /// 955:145378: Quit Without Saving quits, Save All saves the documents one by
    /// one by package E and quits, when all of them are saved (a failed or
    /// canceled save keeps the window open), Cancel keeps the window.
    virtual bool confirmClose();

private:
    PDFAppTab* addTab(PDFAppTab* tab, int index);
    PDFAppTab* findTab(quint64 id) const;
    int findTabIndex(quint64 id) const;
    void createCommands();
    void updateActionIcons();
    void updateShortcuts();
    void updateWindowFilter();

    /// Buttons back / forward of the mouse: a press navigates, the release and
    /// the double click are taken too. \returns true, if the event is taken.
    bool handleNavigationButton(QEvent* event);
    void updateWindowState();
    void updateWindowTitle();

    /// Remembers the geometry of the visible window in the normal state
    void updateNormalGeometry();

    /// The normal geometry of the window (also of a maximized, full screen or
    /// minimized one): the remembered one, unless the system has moved the window
    /// to another screen meanwhile
    QRect getRememberedNormalGeometry() const;

    /// The geometry of a new window: the first window of the process takes the
    /// stored geometry (window.geometry) placed on the screens (maximized as it
    /// was), another window is cascaded from a visible main window of the process,
    /// else the default (getInitialGeometry). Set before the window is shown.
    void applyInitialGeometry();

    /// A visible main window of the process other than this one (the active first)
    const PDFAppMainWindow* findOtherWindow() const;


    /// Message box of the window, which does not block the caller
    void showMessage(int badge, const QString& title, const QString& text, const QString& details);

    /// The drop zones of the pages show, that files are dragged over the window
    void setFileDragActive(bool isActive);

    /// Annotations dragged over a tab, which is not current: the tab becomes
    /// current, when the pointer rests on it (SH_TabBar_ChangeCurrentDelay)
    bool updateDragTabSwitch(const QMimeData* mimeData, const QPoint& windowPosition);
    void stopDragTabSwitch();
    void onDragTabSwitchTimeout();

    /// Files and errors of the own command line (after the window is shown)
    void processCommandLine();

    /// Opens one document of openDocuments (an error is appended to \p failures);
    /// \p replacedTab is an empty New tab, which the first new document replaces
    void openDocument(const PDFAppOpenRequest& request, std::vector<PDFAppOpenFailure>* failures, QPointer<PDFAppTab>* replacedTab);

    /// The requests waiting for a modal dialog are opened (or wait for the next one)
    void processPendingRequests();
    void watchModalWidget(QWidget* widget);

    /// Dynamic property of a message box of the window, which only informs
    static constexpr const char* NON_BLOCKING_PROPERTY = "pdf4qtNonBlocking";
    void onWatchedModalDestroyed();

    void showOpenFailures();
    void onOpenFailedDialogFinished();
    void onDocumentLoadFailed(const QString& filePath, const QString& errorMessage);
    void onEditCopyRequested();
    void onLockClicked(int index);
    void connectDocumentTab(PDFAppDocumentTab* tab);

    /// Save All of quit-unsaved: the next document is saved, the window closes after the last one
    void saveNextForQuit();
    void onQuitSaveFinished(bool isSaved);

    /// The application quits without the closing of the window: the geometry of
    /// the active main window wins (the other visible windows store theirs only,
    /// when no main window of the process is active)
    void onAboutToQuit();
    void onCurrentTabChanged(int index);
    void onTabCloseRequested(int index);
    void onTabDestroyed(QObject* object);
    void synchronizeCurrentTab();
    void onNewTabRequested();
    void onMaximizeRestoreRequested();
    void onMinimizeRequested();
    void onCloseRequested();
    void onMoveRequested();
    void onCommandTriggered();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void onOpenRequested(const pdf::PDFAppInstanceRequest& request);
    void onPageTitleChanged();
    void onTabCommandStatesChanged();
    void onToolTabStateChanged();

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppTopBar> m_topBar;
    QPointer<QStackedWidget> m_stack;
    bool m_isFileDragActive = false;
    QPointer<QTimer> m_dragTabTimer;    ///< Switches to the tab under the dragged annotations
    int m_dragTabIndex = -1;            ///< Tab under the dragged annotations (-1 = none)
    QPointer<PDFAppNavigator> m_navigator;
    QPointer<PDFAppWindowFrame> m_frame;
    QPointer<QWindow> m_filteredWindow;
    QPointer<PDFAppWorkspaceTab> m_homeTab;
    QList<QPointer<PDFAppTab>> m_tabs;
    std::map<QObject*, quint64> m_tabIds;
    std::map<QString, QPointer<QAction>> m_actions;
    QStringList m_requestedFiles;
    QStringList m_forwardedErrors;
    bool m_isClosing = false;
    bool m_isBrowsingDocuments = false;
    std::vector<PDFAppOpenRequest> m_pendingRequests;
    QPointer<QWidget> m_watchedModal;
    std::vector<PDFAppOpenFailure> m_pendingFailures;
    QPointer<QTimer> m_failureTimer;
    QPointer<PDFAppOpenFailedDialog> m_openFailedDialog;
    QString m_lastOpenedTitle;
    bool m_isQuitConfirmed = false;
    std::vector<QPointer<PDFAppDocumentTab>> m_quitSaveQueue;
    QPointer<PDFAppDocumentTab> m_quitSavingTab;
    bool m_isFullScreenMode = false;
    Qt::WindowStates m_windowStateBeforeFullScreen = Qt::WindowNoState;

    /// The geometry of the window in the normal state (the last one seen, else the
    /// initial geometry): QWidget::normalGeometry of a window created maximized is
    /// off by the frame of the maximized window on Windows (Qt subtracts the frame
    /// margins of the maximized state from the normal placement)
    QRect m_normalGeometry;
};

}   // namespace pdf

#endif // PDFAPPMAINWINDOW_H
