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

#ifndef PDFAPPTOOLDOCUMENTPAGE_H
#define PDFAPPTOOLDOCUMENTPAGE_H

#include "pdfappdocumentpage.h"
#include "pdfdocumenttoolshell.h"

#include <QPointer>
#include <QString>

namespace pdf
{

class PDFAppToolHost;

/// Route adapter of a tool in the tab of a document (/document/{id}/tool/{toolId}, plan
/// of the first batch, chapter 2.1): a document page, so the tab shows it with its
/// persistent view. Its setRoute enters the shell of the tool, the shell ends with
/// the page: a navigation first asks the shell (requestLeave, F14), Cancel and an accepted
/// result ask for the route of the document themselves (F15).
///
/// The route is refused (Page Not Found, T3): another document, a tool, which is not
/// available for a locked document, a tool working without a document. A document
/// shown later (the opening of a file) is checked again, then the tab goes to the
/// document with a status message; a display version of an XFA form the same (F5).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDocumentPage : public PDFAppDocumentPage
{
    Q_OBJECT

private:
    using BaseClass = PDFAppDocumentPage;

public:
    /// \param context Context of the application
    /// \param toolId Tool of the page
    /// \param parent The tab
    explicit PDFAppToolDocumentPage(PDFAppContext* context, const QString& toolId, QWidget* parent = nullptr);
    virtual ~PDFAppToolDocumentPage() override;

    const QString& getToolId() const { return m_toolId; }

    /// Shell of the tool, nullptr until the document is shown and after its end
    PDFDocumentToolShell* getShell() const { return m_shell.data(); }

    virtual QString getTitle() const override;
    virtual QString getHelpTopic() const override;

    /// A navigation leaves the tool (F14; A0 without a nested event loop): the shell asks (a run, the unsaved work -
    /// PDFDocumentToolShell::requestLeave), then the leave ends here (requestFinishLeave(true): the choice of the
    /// question, which changes the document, or the question about the work it could not keep) and the tool closes.
    /// Pending: the page reports its step by \p flow->finishStep(this page) after the answers.
    virtual PDFAppLeaveResult requestLeave(const PDFAppRoute& target, PDFAppLeaveFlow* flow) override;

    /// The tab of the document closes or the window quits: the tool, which is edited, asks its question of
    /// the unsaved work (PDFDocumentToolShell::requestLeave, owner 2026-11-20); the tool is not ended here
    virtual PDFAppLeaveResult requestClose(PDFAppLeaveFlow* flow) override;

    /// The tool has unsaved work of the user (PDFDocumentToolShell::hasUnsavedWork)
    virtual bool hasUnsavedWork() const override;

    /// The stamp of the work of the tool (PDFDocumentToolShell::getWorkStamp)
    virtual quint64 getWorkStamp() const override;

    /// The tab closes or the window quits: the use of an interactive tool is recorded while the tab is whole
    /// (PDFDocumentToolShell::recordInteractiveUse)
    virtual void prepareClose() override;

    /// A question of the tool is open (PDFDocumentToolShell::isQuestionOpen)
    virtual bool isQuestionOpen() const override;

    /// The open tool stays for its own route (requestLeave); every other route of the tab leaves it
    virtual bool isLeftBy(const PDFAppRoute& target) const override;

    /// A tool, which a locked document ends (PDFDocumentToolLockedAccess::Unavailable), asks its question of the
    /// unsaved work before the lock (PDFDocumentToolShell::requestLeave; owner 2026-11-20, decision 6)
    virtual PDFAppLeaveResult requestDocumentLock(PDFAppLeaveFlow* flow) override;

    /// The end of the leave accepted by requestClose / requestDocumentLock (PDFDocumentToolShell::requestFinishLeave)
    virtual PDFAppLeaveResult requestFinishLeave(bool isProceeding, PDFAppLeaveFlow* flow) override;

    /// The end of the leave without a flow (PDFDocumentToolShell::finishLeave; false never asks)
    virtual bool finishLeave(bool isProceeding) override;

    /// The route of the tool: the view is not a part of it
    virtual PDFAppRoute getCurrentRoute() const override;

protected:
    virtual bool isRouteOfPage(const PDFAppRoute& route) const override;
    virtual void applyRoute() override;

private:
    /// The reason, why the tool cannot run on the document now, empty = none
    QString getRefusalText() const;

public:
    /// The lock of the document does not allow the tool (Unavailable of the catalog, T3)
    static bool isRefusedByLock(const QString& toolId, const PDFAppDocument* document);

    /// The reason of a tool refused by the lock of the document
    static QString getLockRefusalText(const QString& toolId);

private:

    /// Leaves the tool for the document (setRoute runs in a navigation: the navigator does
    /// the route, which this asks for, after it)
    void leaveToDocument(const QString& message);

    /// A tool with its own tab: the new tab of the tool gets the document as its input,
    /// this tab goes back to the document
    void openOwnTab();
    void onCloseRequested(const pdf::PDFAppToolCloseRequest& request);

    /// What the page does after the answer of the shell
    enum class LeaveStage
    {
        None,
        Forward,        ///< The step of the shell is the step of the page (close, lock, the end of a leave)
        Leave,          ///< requestLeave: the end of the leave and the close of the tool follow
        LeaveFinish     ///< requestLeave: the close of the tool follows
    };

    /// The rest of requestLeave after the shell has accepted: the end of the leave, the close of the tool
    PDFAppLeaveResult continueLeave(PDFAppLeaveFlow* flow);
    PDFAppLeaveResult closeAfterLeave();

    /// The page waits for the Pending step of the shell (Pending; Refused, when it cannot wait)
    PDFAppLeaveResult waitForShell(PDFAppLeaveFlow* flow, LeaveStage stage);

    /// A leave still waits for the step of this page (a wait of a flow ended meanwhile is forgotten)
    bool isWaitingForShell();
    PDFAppLeaveResult forwardShellStep(PDFAppLeaveResult result, PDFAppLeaveFlow* flow);
    void onShellStepFinished(pdf::PDFAppLeaveResult result);

    QString m_toolId;
    QPointer<PDFAppToolHost> m_host;
    QPointer<PDFDocumentToolShell> m_shell;
    QPointer<PDFAppLeaveFlow> m_leaveFlow;      ///< The flow, whose step of the shell the page waits for; not owned
    LeaveStage m_leaveStage = LeaveStage::None;
};

/// Factory of the page of a tool in a document tab
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolDocumentPageFactory : public PDFAppPageFactory
{
public:
    /// The tool depends on the route: nullptr
    virtual PDFAppPage* createPage(PDFAppContext* context, QWidget* parent) const override;

    /// The page of the tool of the route; nullptr (Page Not Found) without the
    /// implementation or the GUI of the tool
    virtual PDFAppPage* createPageForRoute(PDFAppContext* context, QWidget* parent, const PDFAppRoute& route) const override;
};

}   // namespace pdf

#endif // PDFAPPTOOLDOCUMENTPAGE_H
