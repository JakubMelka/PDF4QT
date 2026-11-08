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

#ifndef PDFAPPTOOLTAB_H
#define PDFAPPTOOLTAB_H

#include "pdfappglobal.h"
#include "pdfappowntabtoolhost.h"
#include "pdfapppage.h"
#include "pdfapptab.h"
#include "pdfdocumenttoolshell.h"

#include <QPointer>
#include <QString>

namespace pdf
{

class PDFAppToolInputList;
class PDFAppToolTabPage;

/// The own tab of a tool (plan of step 2 of phase 2, 4.1; tools.md 9.3 point 4): Merge
/// "Merge ●" (378:1049), Blank PDF "New document" (337:1976, K1). The route of the tab is
/// /tool/{toolId}/{instanceId}, the instance id has the form of a document id (128 bits of
/// randomness, R19); the tab shows only the page of its route (PDFAppToolTabPage).
///
/// Title and icon: the GUI of the tool (PDFDocumentToolGUI::getOwnTabTitle / getOwnTabIconName),
/// the catalog before the shell exists. The dot (K16): the GUI has a loss text (the list of
/// the inputs is not empty). The progress of a run is shown on the tab (tools.md 9.3 point 15).
/// The tab is never stored in the session or in a workspace (getOpenDocuments skips it).
///
/// Ownership (checklist point 1): the window owns the tab (deleteLater by closeTab); the tab
/// owns the list of the inputs (a child, taken over by PDFAppMainWindow::createToolTab in one
/// synchronous call, R13; an empty list is created for a route without one, 378:1657) and
/// its page; the page owns the body, the host and the shell. The GUI of the tool holds the
/// list by a QPointer. The list outlives the shell: Discard result keeps the inputs (Q9).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolTab : public PDFAppTab
{
    Q_OBJECT

private:
    using BaseClass = PDFAppTab;

public:
    /// \param context Context of the application
    /// \param toolId Tool with its own tab (PDFDocumentToolCatalogEntry::hasOwnTab)
    /// \param instanceId Id of the instance (PDFAppRouteValues::documentId)
    /// \param inputList The inputs (the tab takes it over); nullptr = an empty list
    /// \param parent Parent widget
    explicit PDFAppToolTab(PDFAppContext* context, const QString& toolId, const QString& instanceId, PDFAppToolInputList* inputList, QWidget* parent = nullptr);
    virtual ~PDFAppToolTab() override;

    const QString& getToolId() const { return m_toolId; }
    const QString& getInstanceId() const { return m_instanceId; }

    /// Route of the tab: /tool/{toolId}/{instanceId}
    PDFAppRoute getRoute() const;

    /// The inputs of the tool (never null while the tab lives)
    PDFAppToolInputList* getInputList() const;

    /// The page of the tool, nullptr before the first navigation
    PDFAppToolTabPage* getToolPage() const;

    /// The shell of the tool, nullptr before the entry and after its end
    PDFDocumentToolShell* getShell() const;

    virtual QString getTabTitle() const override;
    virtual QString getTabIconName() const override;

    /// The close of the tab asks (PDFDocumentToolShell::confirmLeave and K16): a run asks
    /// as close-running, an open result dialog refuses, a list of inputs asks whether it may
    /// be lost. The tab may be deleted meanwhile (the caller guards it).
    virtual bool confirmClose() override;

    /// Undo and Redo of the tab act on the tool (the parameter model)
    virtual bool executeCommand(const QString& commandId) override;
    virtual bool isCommandEnabled(const QString& commandId) const override;

    /// The dot of the tab (K16): something is lost by the close
    bool isTabUnsaved() const;

    /// Text of what is lost by the close of the tab ("The list of 3 files is lost."), empty = nothing
    QString getLossText() const;

    /// Progress of a run (0 - 1) shown on the tab, -1 = none
    qreal getTabProgress() const;

    /// Asks the navigator for the route of the tab (a tab created during a navigation shows
    /// its route from the event loop, PDFAppMainWindow::createToolTab)
    void showRoute();

    /// The tab closes itself without a question (an accepted result, Q9; Cancel asks)
    void setCloseConfirmed(bool isConfirmed) { m_isCloseConfirmed = isConfirmed; }
    bool isCloseConfirmed() const { return m_isCloseConfirmed; }

    /// The question of the close (K16) is open
    bool isAskingClose() const { return m_isAskingClose; }

Q_SIGNALS:
    /// The title, the icon, the dot or the progress of the tab may have changed (the window
    /// updates the tab bar); a slot may not delete the tab
    void tabStateChanged();

protected:
    virtual QWidget* getPageArea() const override;
    virtual QVBoxLayout* getPageLayout() const override;
    virtual void onPageChanged() override;

private:
    void onPageStateChanged();

    QString m_toolId;
    QString m_instanceId;
    QPointer<PDFAppToolInputList> m_inputList;
    QPointer<QWidget> m_pageArea;
    QPointer<QVBoxLayout> m_pageLayout;
    bool m_isCloseConfirmed = false;
    bool m_isAskingClose = false;
};

/// Page of the route /tool/{toolId}/{instanceId} in the own tab of the tool: the body
/// (PDFAppToolTabView), the host (PDFAppOwnTabToolHost) and the shell of the tool. Its
/// setRoute enters the shell; the shell ends with the page.
///
/// The end of the tool (closeRequested): Cancel closes the tab (the window asks K16);
/// Discard result keeps the tool with its inputs and its parameters (Q9: a new run of the
/// shell, the list stays); an accepted result closes the tab after the result is opened -
/// Then continue with starts on the new tab first, the close is the last statement (R17);
/// a result without the dialog (Blank PDF, isResultDialogSkipped) replaces the tab by the
/// tab of the new document (PDFAppMainWindow::replaceToolTab, 4.6). The tab and the shell
/// go only by deleteLater.
///
/// The route is refused (Page Not Found): an unknown tool, a tool without its own tab, a
/// tool without its GUI, or the tab of another instance.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolTabPage : public PDFAppPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppPage;

public:
    /// \param context Context of the application
    /// \param toolId Tool of the page
    /// \param parent The page area of the tab
    explicit PDFAppToolTabPage(PDFAppContext* context, const QString& toolId, QWidget* parent = nullptr);
    virtual ~PDFAppToolTabPage() override;

    const QString& getToolId() const { return m_toolId; }

    /// The own tab of the page (nullptr outside of one)
    PDFAppToolTab* getToolTab() const;

    PDFAppToolTabView* getTabView() const { return m_view.data(); }
    PDFAppOwnTabToolHost* getHost() const { return m_host.data(); }

    /// Shell of the tool, nullptr before the entry and after its end
    PDFDocumentToolShell* getShell() const { return m_shell.data(); }

    virtual QString getTitle() const override;
    virtual QString getHelpTopic() const override;
    virtual bool setRoute(const PDFAppRoute& route) override;
    virtual bool prepareLeave(const PDFAppRoute& target) override;

    /// The question of the close of the tab (see PDFAppToolTab::confirmClose)
    bool confirmClose();

Q_SIGNALS:
    /// The title, the dot or the progress of the tab may have changed
    void stateChanged();

private:
    /// Creates and enters the shell. \returns false, when the tool cannot run (the page
    /// shows nothing then)
    bool startShell();
    void deleteShell();

    /// Closes the shell (its mode, its run) before the tab goes. \returns false, when this
    /// page has been deleted meanwhile
    bool closeShell();
    void onCloseRequested(const pdf::PDFAppToolCloseRequest& request);
    void onShellStateChanged();
    void onProgressChanged();
    void onRestartQueued();

    QString m_toolId;
    QPointer<PDFAppToolTabView> m_view;
    QPointer<PDFAppOwnTabToolHost> m_host;
    QPointer<PDFDocumentToolShell> m_shell;
    bool m_isRestartQueued = false;
};

/// Factory of the page of a tool in its own tab (PDFAppToolPageKind::OwnTab)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolTabPageFactory : public PDFAppPageFactory
{
public:
    /// The tool depends on the route: nullptr
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;

    /// The page of the tool of the route; nullptr (Page Not Found) without the
    /// implementation, the GUI or the own tab of the tool
    virtual PDFAppPage* createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const override;

    /// The tool has its own tab and its GUI
    static bool hasOwnTabPage(const QString& toolId);
};

}   // namespace pdf

#endif // PDFAPPTOOLTAB_H
