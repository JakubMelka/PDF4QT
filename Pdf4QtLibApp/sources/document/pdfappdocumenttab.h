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

#ifndef PDFAPPDOCUMENTTAB_H
#define PDFAPPDOCUMENTTAB_H

#include "pdfappglobal.h"
#include "pdfapptab.h"

#include <QPointer>
#include <QTimer>

#include <vector>

class QStackedWidget;

namespace pdf
{

class PDFAppDocument;
class PDFAppDocumentCommandHandler;
class PDFAppDocumentFileCommandHandler;
class PDFAppDocumentLoader;
class PDFAppDocumentStateScreen;
class PDFAppDocumentView;
class PDFAppLockPopover;

/// The choice of a choice dialog (pdfappdocumentdialogs.h)
enum class PDFAppDocumentChoice;

/// Tab of a document (kind Document, step 3 plan 1.1). It owns the model
/// (PDFAppDocument, registered in the PDFAppDocumentRegistry of the context), the
/// persistent view (PDFAppDocumentView) and one command handler of each package.
/// Its content is a stack: the view, while the page of the tab is the route
/// adapter PDFAppDocumentPage (/document/{id}) and the document is shown, the
/// state screen (loading, password 228:8709) for a document, which is not shown
/// yet, and the page area for the other pages of the tab (properties
/// /document/{id}/properties/{section}, Page Not Found). The navigator only swaps
/// the pages, the view lives with the tab.
///
/// Lifecycle (package A): open reads the file in a worker (PDFAppDocumentLoader);
/// a file, which cannot be read, ends by the signal loadFailed (the window closes
/// the tab and lists the file in the dialog open-failed). The tab remembers the
/// reading position of its file in Recent (debounced and at its end), the state
/// of its view and the lock (recent_file.locked). Closing asks about running
/// operations (955:145813) and unsaved changes (955:144943); Save of that dialog
/// saves by package E and closes the tab, when the file is written.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppDocumentTab : public PDFAppTab
{
    Q_OBJECT

private:
    using BaseClass = PDFAppTab;

public:
    /// Delay of the writing of the reading position into Recent
    static constexpr int READING_STATE_DELAY = 1000;

    explicit PDFAppDocumentTab(PDFAppContext* context, QWidget* parent = nullptr);
    virtual ~PDFAppDocumentTab() override;

    PDFAppDocument* getDocument() const;
    PDFAppDocumentView* getView() const;
    PDFAppDocumentLoader* getLoader() const;
    PDFAppDocumentStateScreen* getStateScreen() const;

    /// Id of the document (route /document/{id})
    QString getDocumentId() const;

    /// Starts the reading of the file in a worker thread. \returns false, when the
    /// reading cannot start (the tab has opened a file already)
    bool open(const QString& filePath);

    /// Makes the document a copy of the document \p source (Edit a Copy). \returns
    /// false, when the source is not shown or the tab has a document already
    bool openCopyOf(const PDFAppDocument* source);

    /// Starts the reading of a document from the memory \p bytes (a PDF attachment,
    /// O18): the tab behaves as a copy titled \p title (no file, Save As, not in
    /// Recent). \returns false, when the tab has a document already
    bool openFromMemory(QByteArray bytes, const QString& title, const QString& originFilePath);

    /// The view is shown (the page of the tab is the document page and the
    /// document is shown)
    bool isViewShown() const;

    /// The state screen is shown (loading, password)
    bool isStateScreenShown() const;

    /// Command handlers of the packages
    std::vector<PDFAppDocumentCommandHandler*> getHandlers() const;

    /// Handler of the command, nullptr if no handler handles it
    PDFAppDocumentCommandHandler* findHandler(const QString& commandId) const;

    /// States of the tab in the tab bar (PDFDocumentTabBar)
    bool isTabLocked() const;
    bool isTabUnsaved() const;
    bool isTabPasswordProtected() const;

    /// Progress of the tab 0 - 1 (the reading, an operation, which is asked about
    /// before the tab closes), negative = none
    qreal getTabProgress() const;

    /// Shows the popover of the locked document (266:12435) below the lock of the
    /// tab (\p anchor in global coordinates). Nothing for an unlocked document.
    void showLockPopover(const QRect& anchor);

    /// The shown lock popover (nullptr, if none is shown)
    PDFAppLockPopover* getLockPopover() const;

    /// Closes the tab by its window (the questions of requestTabClose are asked)
    void requestClose();

    /// Writes the reading position and the view state into Recent now
    void writeReadingState();

    /// The tab saves the document because of a close (Save of close-unsaved) and
    /// closes, when the file is written
    bool isClosingAfterSave() const;

    virtual QString getTabTitle() const override;
    virtual QString getTabIconName() const override;

    /// The questions of the close of the tab in its flow (A0: shown, never exec; newapp_robustness.md 15.6): the
    /// page (the work of a tool), close-running, the end of the leave of the page (the choice of the question of the
    /// tool, the question about the work it could not keep), the open editors, close-unsaved; Save of close-unsaved
    /// ends the flow and saves. Pending: the tab closes itself by its window after the last answer or the save, and
    /// stays after Cancel or a failed save. Refused while the save of a close runs.
    virtual PDFAppLeaveResult requestTabClose() override;
    virtual bool executeCommand(const QString& commandId) override;
    virtual bool isCommandEnabled(const QString& commandId) const override;
    virtual bool isCommandChecked(const QString& commandId) const override;

    /// Starts the save of the document by package E. \returns false, when the
    /// document cannot be saved now (then no signal saveFinished comes).
    bool save();

    /// The handler of package E for a save request (A0: PDFAppDocumentFileCommandHandler::requestSave - its end
    /// exactly once, false from the event loop when the tab goes first); the tab emits saveFinished for it as for
    /// save. nullptr = the tab has no handler (nothing can be saved).
    PDFAppDocumentFileCommandHandler* prepareSave();

Q_SIGNALS:
    /// The file cannot be opened (the window closes the tab and reports it)
    void loadFailed(const QString& filePath, const QString& errorMessage);

    /// A save started by save has ended
    void saveFinished(bool isSaved);

    /// Edit a Copy of the lock popover or of the command (the window opens the copy)
    void editCopyRequested();

protected:
    virtual QWidget* getPageArea() const override;
    virtual QVBoxLayout* getPageLayout() const override;
    virtual void onPageChanged() override;

private:
    void updateStack();
    void onDocumentStateChanged();
    void onDocumentTitleChanged();
    void onDocumentProgressChanged();
    void onLockChanged();
    void onHandlerStateChanged();
    void onCurrentPageChanged();
    void onPasswordEntered(const QString& password);
    void onStateScreenCloseRequested();
    void onUnlockRequested();
    void onEditCopyRequested();
    void onFileSaveFinished(bool isSaved);
    void onDocumentShown();

    /// The steps of the flow of the close (PDFAppLeaveFlow::getStep), in their order
    enum class CloseStep
    {
        Page,           ///< The page asks (the work of a tool)
        Running,        ///< close-running
        FinishPages,    ///< The end of the leave of the page
        Unsaved         ///< The open editors, close-unsaved
    };

    /// The save of the close (a request of package E), its end. The flow has ended before it (the save is no step:
    /// a quit, a navigation, a lock go on meanwhile); isClosingAfterSave refuses a second close until its end.
    void startCloseSave();
    void onCloseSaveFinished(bool isSaved);

    /// The close, whose questions have all been answered, has not closed the tab (its save failed or was canceled,
    /// the window refused the tab): the tab stays, a tool, which answered its question by closing, still closes with
    /// its choice applied (PDFAppPage::finishLeave(false) of the pages, which the close has ended by true - RLC1 L4:
    /// those pages, not the current one; owner 2026-11-24; nothing runs inside the call). Nothing while another flow
    /// of the tab runs (it decides).
    void endUnfinishedClose();

    /// The flow of the close ends Done (\p flow->finish(true)); the pages it has ended by true are remembered for
    /// endUnfinishedClose (the save of the close or the window may still keep the tab)
    void finishCloseFlow(PDFAppLeaveFlow* flow);

    /// The steps of the close from \p from on (Done: the flow has ended, the caller closes the tab; Pending: a
    /// question is open; Refused: the flow has ended without the close)
    PDFAppLeaveResult continueClose(CloseStep from);

    /// The flow of the close runs in \p step
    bool isCloseStep(CloseStep step) const;

    /// The close after a later answer: Done closes the tab by its window
    void completeClose(PDFAppLeaveResult result);

    void onClosePageStepFinished(pdf::PDFAppLeaveResult result);
    void onCloseRunningChosen(pdf::PDFAppDocumentChoice choice);
    void onCloseFinishStepFinished(pdf::PDFAppLeaveResult result);
    void onCloseUnsavedChosen(pdf::PDFAppDocumentChoice choice);

    /// Running operations are stopped (background operations without a question)
    void stopOperations(bool askedOnly);

    QPointer<PDFAppDocument> m_document;
    QPointer<PDFAppDocumentLoader> m_loader;
    QPointer<QStackedWidget> m_stack;
    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocumentStateScreen> m_stateScreen;
    QPointer<QWidget> m_pageArea;
    QPointer<QVBoxLayout> m_pageLayout;
    QPointer<PDFAppLockPopover> m_lockPopover;
    QPointer<QTimer> m_readingStateTimer;
    std::vector<QPointer<PDFAppDocumentCommandHandler>> m_handlers;
    bool m_isShown = false;             ///< The document has been shown once
    bool m_isCloseConfirmed = false;    ///< The questions have been answered (closing after the last answer or the save)
    bool m_isClosingAfterSave = false;  ///< Save of close-unsaved runs (the tab closes after it)
    std::vector<QPointer<PDFAppPage>> m_closeFinishedPages;    ///< The pages ended by true by the last close, which has not closed the tab yet (endUnfinishedClose)
    bool m_isReadingStatePending = false;
};

}   // namespace pdf

#endif // PDFAPPDOCUMENTTAB_H
