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
#include <QWidget>

#include <map>

namespace pdf
{

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

    explicit PDFAppMainWindow(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppMainWindow() override;

    PDFAppContext* getContext() const { return m_context; }
    PDFAppNavigator* getNavigator() const { return m_navigator; }
    PDFAppTopBar* getTopBar() const { return m_topBar; }
    PDFAppWindowFrame* getFrame() const { return m_frame; }
    PDFDocumentTabBar* getTabBar() const;

    /// The Home tab (it is created again, if the application has deleted it)
    PDFAppWorkspaceTab* getHomeTab();

    /// Current tab
    PDFAppTab* getCurrentTab() const;

    /// Tabs in the order of the tab bar
    QList<PDFAppTab*> getTabs() const;

    /// Adds a tab New tab after the current one (not shown, no route yet).
    /// \returns nullptr, if there are too many tabs.
    PDFAppTab* createNewTab();

    /// Command New tab: a new tab New tab with /open, activated
    PDFAppTab* openNewTab();

    /// Makes the tab current
    void activateTab(PDFAppTab* tab);

    /// Closes the tab (not the Home tab). \returns false, if it is not closed.
    bool closeTab(PDFAppTab* tab);

    /// Tab of the document id or of the instance of a tool (step 3, phase 2:
    /// there are no such tabs yet - nullptr)
    PDFAppTab* findDocumentTab(const QString& id) const;

    /// Updates the title and the icon of the tab in the tab bar and the window title
    void updateTab(PDFAppTab* tab);

    /// Action of the command (nullptr for an unknown command)
    QAction* getCommandAction(const QString& commandId) const;

    /// Ctrl+K: the global search of the current tab, or of the Home tab
    void focusSearch();

    /// Maximizes or restores the window
    void toggleMaximized();

    /// Files of the command line and of other processes waiting for the opening
    /// of documents (step 3) and the errors of the command lines of other processes
    const QStringList& getRequestedFiles() const { return m_requestedFiles; }
    const QStringList& getForwardedErrors() const { return m_forwardedErrors; }

    /// Processes a request of another process (the slot of PDFAppContext::openRequested)
    void handleOpenRequest(const pdf::PDFAppInstanceRequest& request);

Q_SIGNALS:
    /// The window has started to close (the server of the instance is suspended)
    void closing();

protected:
    virtual bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    virtual void changeEvent(QEvent* event) override;
    virtual void closeEvent(QCloseEvent* event) override;
    virtual void paintEvent(QPaintEvent* event) override;
    virtual bool event(QEvent* event) override;

    /// Asked, when the window is being closed: false = the user has canceled the
    /// quitting (step 3: unsaved documents, pdf4qt-quit-unsaved). Until then
    /// nothing cancels it.
    virtual bool confirmClose();

private:
    PDFAppTab* addTab(PDFAppTab* tab, int index);
    PDFAppTab* findTab(quint64 id) const;
    int findTabIndex(quint64 id) const;
    void createCommands();
    void updateShortcuts();
    void updateWindowState();
    void updateWindowTitle();

    /// STEP 3 HOOK: opening of documents is the step 3 of phase 1 (document tab).
    /// Until then the files are kept in getRequestedFiles and listed in a message box.
    void openDocuments(const QStringList& files, int page);

    /// Errors of a command line in a message box (note 958:28033)
    void showCommandLineErrors(const QStringList& errors);

    /// Message box of the window, which does not block the caller
    void showMessage(int badge, const QString& title, const QString& text, const QString& details);

    /// Files and errors of the own command line (after the window is shown)
    void processCommandLine();

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

    QPointer<PDFAppContext> m_context;
    QPointer<PDFAppTopBar> m_topBar;
    QPointer<QStackedWidget> m_stack;
    QPointer<PDFAppNavigator> m_navigator;
    QPointer<PDFAppWindowFrame> m_frame;
    QPointer<PDFAppWorkspaceTab> m_homeTab;
    QList<QPointer<PDFAppTab>> m_tabs;
    std::map<QObject*, quint64> m_tabIds;
    std::map<QString, QPointer<QAction>> m_actions;
    QStringList m_requestedFiles;
    QStringList m_forwardedErrors;
    bool m_isClosing = false;
};

}   // namespace pdf

#endif // PDFAPPMAINWINDOW_H
