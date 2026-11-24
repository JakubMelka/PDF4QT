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

#ifndef PDFDOCUMENTTOOLGUI_H
#define PDFDOCUMENTTOOLGUI_H

#include "pdfappglobal.h"
#include "pdfapptoolresultdialog.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfsecurityhandler.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <map>
#include <memory>
#include <vector>

class QBoxLayout;
class QDialog;
class QWidget;

namespace pdf
{

class PDFMessageBox;
class PDFAppContext;
class PDFAppDocument;
class PDFAppToolHost;
class PDFAppToolInputList;
class PDFAppToolParameterModel;
class PDFAppToolToolbar;
class PDFDocumentToolCatalogEntry;
class PDFDocumentToolShell;
class PDFModifiedDocument;
class PDFAppLeaveFlow;

/// The result of a step of a leave (pdfapppage.h)
enum class PDFAppLeaveResult;

/// Where the card of the progress and the callout The document is busy lie (K7):
/// the top of the right panel by default; Compress has its card inside of the section
/// Result (295:1711)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolProgressPlace
{
public:
    QBoxLayout* cardLayout = nullptr;       ///< Layout of the card, nullptr = the top of the right panel
    int cardIndex = 0;                      ///< Index of the card in the layout
    QBoxLayout* calloutLayout = nullptr;    ///< Layout of the callout, nullptr = below the card
    int calloutIndex = 0;                   ///< Index of the callout in the layout
};

/// Where the callout of a certified document lies (PDFDocumentToolShell::getCertificationCallout,
/// owner decision of 13. 11. 2026), the place of the callouts of the frames of the tool: the top of
/// the right panel by default (the red and amber callouts of Remove 388:22298, Crop 398:28158,
/// Resize 391:25272, Set Page Boxes, Insert 388:17789)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCalloutPlace
{
public:
    /// Layout of the callout, nullptr = the body of the right panel around its content
    QBoxLayout* layout = nullptr;

    /// Index in the layout. In the body of the right panel 0 = above the content, -1 = below it;
    /// in a layout of the GUI its index, -1 = at its end.
    int index = 0;
};

/// What the list of the inputs of a tool accepts (plan of step 3 of phase 2, 9.3, K41): Merge 2
/// to 1000 PDF files and images, Interleave exactly 2 PDF files in the slots Front sides and Back
/// sides, Images to PDF 1 to 1000 images, Insert Pages its sources in the tab of the document.
/// A value; PDFAppToolInputList, the chooser and the Primary button read it (package D).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolInputRules
{
public:
    int minimum = 2;                ///< Fewest usable inputs, which the tool runs with
    int maximum = 1000;             ///< Most inputs
    bool acceptsPdf = true;         ///< A PDF file or an open document is an input
    bool acceptsImages = true;      ///< An image file or the clipboard is an input
    QStringList slotTitles;         ///< Titles of the fixed places of the inputs ("Front sides", "Back sides"); empty = a free list
};

/// A text of the section of the start page of a tool without an input, which opens the tool
/// in its own tab (Blank PDF 1392:4528, Q10)
enum class PDFAppToolStartPageText
{
    Title,      ///< Title of the section ("Start a new document")
    Hint,       ///< Hint next to the title ("Blank PDF needs no document · …")
    Card,       ///< Text of the card next to the Primary button
    Note        ///< The note below the card
};

/// The state of the result of an analysis, which the tool keeps and shows itself (PDFDocumentToolGUI::
/// isResultKeptInTool; plan of step 7 of phase 2, 4.1, R7-2 - the rule of the owner of 9. 11.: after a result
/// the Primary button is Disabled, Close Enabled; the Primary button is enabled again only when the result is
/// stale)
enum class PDFAppToolResultState
{
    None,       ///< No result yet: the Primary button by isConfirmEnabled (the ready state)
    Current,    ///< The result of the inputs and the document of now is shown: the Primary button Disabled
    Stale       ///< The document or an input has changed since (492:109399): the amber callout, the Primary button again
};

/// The texts of the generic question before a tool in the tab of a document closes with work of the user, which
/// leaving loses (PDFDocumentToolGUI::getCloseQuestion; owner 2026-11-20: "Discard the 4 temporary measurements?").
/// No frame of its own: the question of Sign by Hand 435:10500 (Keep as the Primary button, Discard with the icon
/// trash) and, with a third choice, of Redact 448:17473 (Keep leading, the choice as the Primary button, Discard).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolCloseQuestion
{
public:
    QString title;              ///< "Discard the 4 temporary measurements?"; empty = no question
    QString text;               ///< What closing loses and what stays
    QString keepText;           ///< The button, which keeps the tool open ("Keep Measuring")
    QString discardText;        ///< The button, which closes the tool and loses the work ("Discard Measurements")
    QString actionText;         ///< A choice, which keeps the work elsewhere and closes ("Save as Annotations"); empty = none
    QString actionIconName;     ///< The icon of the choice

    bool isValid() const { return !title.isEmpty(); }
};

/// What differs between the tools in the shell (plan of the first batch, chapter 2.1):
/// the shell has the flow, the toolbar, the frame of the panels, the progress, the keys.
/// A thin tool derives from PDFAppDeclarativeToolGUI and describes its panels; a tool
/// with its own canvas (Compress: before and after) overrides the functions below.
/// The parts are created once and again after a change of the language; their values
/// live in the parameter model, so a new part shows the same state. The functions run
/// in the thread of the user interface. A hook may not delete the shell.
/// Deferred deletion (step A): the GUI is retired with its shell; its hook retire() (PDFRetirable) ends what the
/// GUI has outside its subtree.
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolGUI : public QObject, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFDocumentToolGUI(QObject* parent = nullptr);
    virtual ~PDFDocumentToolGUI() override;

    /// Shell of the GUI: the functions below may use the getters only after it
    void attach(PDFDocumentToolShell* shell);

    PDFDocumentToolShell* getShell() const;
    PDFAppToolHost* getHost() const;
    PDFAppContext* getContext() const;
    PDFAppDocument* getDocument() const;
    PDFAppToolParameterModel* getModel() const;
    PDFDocumentToolRunner* getRunner() const;
    PDFAppToolToolbar* getToolbar() const;

    /// Entry of the catalog
    const PDFDocumentToolCatalogEntry* getTool() const;

    // ------------------------------------------------------------- parts

    /// Actions of the toolbar (tool-actions 291:1135), nullptr = none (no third divider)
    virtual QWidget* createToolActions(QWidget* parent);

    /// Content of the right panel (sidebar-right 291:1177), nullptr = no right panel
    virtual QWidget* createRightPanel(QWidget* parent);

    /// Content of the left panel, nullptr = the sidebar of the document stays
    virtual QWidget* createLeftPanel(QWidget* parent);

    /// Widget in the place of the canvas (a grid, a form), nullptr = the document stays
    virtual QWidget* createCanvasPage(QWidget* parent);

    /// Widget over the canvas (the handle of Compress), nullptr = none; it is a child
    /// of the canvas area for the time of the tool
    virtual QWidget* createOverlay(QWidget* canvasArea);

    /// Gap of the sections of the right panel: 16 of Optimize, 14 of the others (1.1)
    virtual int getRightPanelSpacing() const;

    /// Panel of the sidebar shown by the entry (Thumbnails of Compress and Structure),
    /// empty = the panel of the user
    virtual QString getInitialSidebarPanel() const;

    /// Where the progress lies; the layouts belong to the content of the right panel
    virtual PDFAppToolProgressPlace getProgressPlace() const;

    /// Where the callout of a certified document, whose certification the run removes, lies (the
    /// shell shows it, PDFDocumentToolShell::getCertificationCallout); a layout belongs to the
    /// content of the right panel. Default: the top of the right panel.
    virtual PDFAppToolCalloutPlace getCertificationCalloutPlace() const;

    /// A tool, which has its own card, keeps it enabled: the default disables the
    /// actions and the content of both panels while the tool runs (295:1711, 438:14093)
    virtual void setContentEnabled(bool isEnabled);

    // ------------------------------------------------------------- texts

    /// Text of the Primary button (default: the title of the tool) and its icon
    /// (default: the icon of the tool)
    virtual QString getConfirmText() const;
    virtual QString getConfirmIconName() const;

    /// The Primary button works: no error of the parameters, no reported error of the input
    virtual bool isConfirmEnabled() const;

    /// Message of the status bar while the user edits: "Compress · Report.pdf · 1,592
    /// pages · 24.8 MB" (291:994)
    virtual QString getStatusText() const;

    /// The part of the status message after the name of the document: "1,592 pages" (291:994),
    /// Optimize Structure "48,210 objects" (302:4072); empty = none
    virtual QString getStatusDetailText() const;

    /// The status message ends with the size of the file ("· 24.8 MB" of 291:994); the image
    /// tools of step 4 have their own sizes in the detail or none (297:3135, 304:5102, 308:6911,
    /// 341:7516)
    virtual bool isFileSizeInStatusText() const { return true; }

    /// Message of the status bar during a run: "Compress: Recompressing images · 152 of
    /// 412 · press Stop or Esc to stop" (295:1711)
    virtual QString getRunningStatusText(const QString& stepText) const;

    /// Title of the card of the progress: "Compress… 37 %" (295:2151)
    virtual QString getRunningTitle(int percentage) const;

    /// Title and step line of the card of the progress (default: getRunningTitle and the step).
    /// A tool, whose step is its title, says it there (356:13079 "Reading page 640 of 1,592",
    /// no step line).
    virtual QString getRunningCardTitle(int percentage, const QString& stepText) const;
    virtual QString getRunningCardStepText(const QString& stepText) const;

    /// Line of the time left of the card (295:2155 "About 40 seconds left", 356:13079 with
    /// the note of its tool), \p seconds -1 = not known yet, empty = no line (438:14093)
    virtual QString getRunningTimeText(int seconds) const;

    /// Text of the callout The document is busy, empty = no callout (Extract Text keeps
    /// the document free; by default only a tool of the kind ChangeDocument has it, K7)
    virtual QString getBusyText() const;

    /// Refusal of a document without the permission of the tool (339:10328, owner answer
    /// C): the title, the line below it and the text of the message box
    virtual QString getPermissionTitle() const;
    virtual QString getPermissionSubtitle() const;
    virtual QString getPermissionText() const;

    /// The permissions of Table 22, which the run with the values of now needs besides those of
    /// the catalog entry (Remove Hidden Data: bit 6 for the chosen comments, attachments and
    /// signature fields). The shell asks them by PDFAppDocumentPermissions::request when the
    /// Primary button is pressed: the owner password dialog of a document, which can be unlocked,
    /// the user confirms again after the unlock (owner 2026-11-08). Default: none.
    virtual std::vector<PDFSecurityHandler::Permission> getRunPermissions() const;

    /// The GUI shows the refusal of the permissions of its catalog entry itself (fix wave of step
    /// 3b, owner answer 1 of 12. 11.: Manage Pages in the tab of a document without Assemble,
    /// 1479:35631 - the tool opens, its Primary button is Disabled with the callout and the owner
    /// password). The shell then skips its refusal at the entry (isPermissionRefused stays false);
    /// the GUI keeps isConfirmEnabled false while the permission is missing, and the validation of
    /// the tool refuses the run anyway (no "ignore DRM" path). Default: false - the shell refuses
    /// the tool at the entry (339:10328).
    virtual bool handlesPermissionRefusal() const;

    /// Texts of the result dialog ("Pages rotated", the file of an export)
    virtual void fillResultInput(PDFAppToolResultInput* input) const;

    /// A successful result, before the result dialog opens (step 4 of phase 2: the partial result
    /// of Black & White, 307:6388 "Use partial document"). \returns true = the dialog opens now (the
    /// default); false = the tool asks the user first and answers later by
    /// PDFDocumentToolShell::answerResult - never from inside of this function. Until the answer the
    /// shell keeps the result and the operation of the runner (the document stays busy, as in the
    /// dialog). The question is the tool's own (new + QPointer, deleted with the GUI); when the GUI
    /// or the tab goes, the shell goes with it and the result is dropped by the runner.
    virtual bool confirmResult(const PDFDocumentToolResult& result);

    // ------------------------------------------------------------- own tab (plan of step 2 of phase 2, 4.1, 4.6)

    /// The result needs no decision (Blank PDF, 337:1976 "there is no result dialog"): a
    /// successful run of a NewDocument result replaces the own tab of the tool by a tab of
    /// the new document, unsaved, at the same place (PDFAppMainWindow::replaceToolTab). No
    /// Then continue with (K2). Default: false, the result dialog decides.
    virtual bool isResultDialogSkipped() const;

    /// Title of the own tab of the tool (Blank PDF "New document", K1); default: the title
    /// of the tool in the catalog ("Merge")
    virtual QString getOwnTabTitle() const;

    /// Icon of the own tab (Blank PDF the icon of a file "file", 337:1976); default: the
    /// icon of the tool in the catalog
    virtual QString getOwnTabIconName() const;

    /// What is lost by the close of the own tab (K16: "The list of 3 files is lost."): a
    /// non-empty text puts the dot on the tab and the close of the tab or of the window asks
    /// with it. Default: empty, nothing is lost.
    virtual QString getOwnTabLossText() const;

    /// Value of the row Locked document of At a glance of the start page (K9; Merge "Can be
    /// merged"); empty = the text of the locked access of the catalog
    virtual QString getLockedGlanceText() const;

    /// A text of the start page of a tool without an input (Blank PDF 1392:4528); empty = the
    /// generic text of the start page. Read from a GUI made only for the text (no shell).
    virtual QString getStartPageText(PDFAppToolStartPageText text) const;

    /// Texts of the page item and of the zoom item of the status bar of the own tab (Merge
    /// "16 pages" and no zoom, K12; Blank "Page 1 / 3" and the zoom of its preview "151 %");
    /// empty = the item is hidden. Read with getStatusText (updateStatusText). In the document
    /// host the page text replaces the page item of the view (Manage "12 pages", 1479:34632),
    /// empty = the page item of the view; the zoom item stays the zoom of the view.
    virtual QString getOwnTabPageText() const;
    virtual QString getOwnTabZoomText() const;

    /// In the document host the two texts above replace both items of the view, an empty text
    /// hides its item (K7.27 of the plan of step 7 of phase 2, FD-31: Statistics, Object Inspector
    /// and Report show no page, so neither item; Output Preview its page and the zoom of its
    /// picture, which is not the zoom of the view). Default: false - the items of the view.
    virtual bool hasOwnStatusItems() const;

    /// The list of the inputs of a tool of more inputs in its own tab (owned by the tab,
    /// PDFAppToolInputList of package B2), nullptr = none (a tool in a document tab)
    PDFAppToolInputList* getInputList() const;

    /// The user has changed the list of the inputs since the tool opened (added, removed, moved an input;
    /// owner 2026-11-20: the inputs, which the tab came with, are no work - the loss text of K16 asks only
    /// after a change). False without a list.
    bool isInputListChanged() const;

    /// What the list of the inputs accepts (plan of step 3 of phase 2, K41): the minimum and the
    /// maximum by PDFDocumentToolCatalogEntry::getInputCountRange of the tool, when it takes more
    /// inputs (else, and without a shell, 2 to 1000 as today), PDF files and images, no slots.
    /// Interleave and Images to PDF override it.
    virtual PDFAppToolInputRules getInputRules() const;

    /// The new document of the result replaces the own tab of the tool by the choice "Open in
    /// this tab" of the result dialog (Images to PDF 336:3572, Q13 / R16; Blank PDF replaces it
    /// without the dialog, isResultDialogSkipped). The dialog offers Open in this tab instead
    /// of Open in a new tab (the branch is package A's, the replacement package D's). Default:
    /// false.
    virtual bool isResultInThisTab() const;

    /// The own tab of the tool stays open after an accepted result (plan of step 3b of phase 2,
    /// RB3 b: Manage Pages K31, 396:27563 "The assembly stays open"): PDFAppToolTabPage returns the
    /// tool to editing with its inputs after the result is opened, saved or its folder shown,
    /// instead of closing the tab; Then continue with still opens its tool. Default: false (the
    /// own tab closes after an accepted result, Q9 of step 2). Read only in an own tab.
    virtual bool isTabKeptAfterResult() const;

    /// The tool asks its own question before its own tab closes (RB3 c, Manage Pages 405:30894 "Save the assembly
    /// Tender_2026?" Don't Save / Cancel / Save Assembly): PDFAppToolTabPage calls requestOwnTabClose in the state
    /// Editing, before the generic question of the loss text; false = the generic question of the loss text follows
    /// (K16). Default: false.
    virtual bool hasOwnTabCloseQuestion() const;

    /// The question of the tool before its own tab closes, without a nested event loop (A0, RR3-M5), called only while
    /// hasOwnTabCloseQuestion. Done = the tab closes without another question (Don't Save, nothing to lose); Refused =
    /// the tab stays (Cancel, the question could not be asked; or Save: the tool has started its asynchronous save and
    /// closes the tab by closeOwnTab, when it has succeeded); Pending = the question of the tool is open (shown by
    /// PDFAppDocumentChoiceDialog::ask / PDFAppDialogAnswer - show, never exec): the tool reports its answer exactly
    /// once by \p flow->finishStep(this GUI, Done / Refused), never inside this call; a question deleted without an
    /// answer is Refused; a GUI deleted before it reports is Refused (the waiter of the step). The caller waits by
    /// PDFAppLeaveFlow::waitForStep(gui, caller, slot) right after the call. The function never deletes the shell.
    /// Default: Done (a tool without its own question).
    virtual PDFAppLeaveResult requestOwnTabClose(PDFAppLeaveFlow* flow);

    /// Text of the button Cancel of the toolbar (RB3 f, K2 of step 3: Manage Pages "Close" in its
    /// own tab - the assembly is no run to cancel; "Cancel" in the tab of a document, where it
    /// closes the tool, not the tab). Default: empty = "Cancel" of the toolbar.
    virtual QString getCancelText() const;

    /// Closes the own tab of the tool as Cancel does (the close of the tab asks requestOwnTabClose
    /// again): the end of a save started by requestOwnTabClose (Save). Nothing outside of an own tab
    /// and outside of the state Editing. The shell, the GUI and the tab may be deleted by it (the
    /// last call of the caller).
    void closeOwnTab();

    /// One instance of the own tab of the tool in a window (Manage Certificates, plan of step 6 of
    /// phase 2, 4.5): a second start activates the tab (PDFAppMainWindow::createToolTab). Read from a
    /// GUI made only for it (no shell). Default: false.
    virtual bool isSingleOwnTab() const;

    // ------------------------------------------------------------- tool in the tab of a document

    /// The user has made work in this session of the tool, which leaving the tool loses (owner 2026-11-20:
    /// "if the user changed nothing, the tool closes without a question"): temporary measurements, a drawing,
    /// marks added now, an edited assembly, a list of sources. Not work: the values of the parameters, a result,
    /// which a run computes again, a run (Stop asks), a change written into the document already (its Undo step).
    /// The shell asks askClose only while it is true - in every way of leaving the tool in the tab of a document
    /// (Cancel, Esc, Discard result, the close of the tab or of the window, another route or tool). Default: false.
    virtual bool hasUnsavedWork() const;

    /// The texts of the generic question of askClose (PDFAppToolCloseQuestion); an invalid one = no generic
    /// question. Read only while hasUnsavedWork. Default: none.
    virtual PDFAppToolCloseQuestion getCloseQuestion() const;

    /// The third choice of the generic question (PDFAppToolCloseQuestion::actionText, Measure "Save as
    /// Annotations"): the GUI keeps the work elsewhere. \returns true = the work is kept, the tool closes;
    /// false = the tool stays (a part could not be kept, the owner password is asked). It may commit to the
    /// document (its slots run); it never deletes the shell itself. Called at the answer, or - when a leave
    /// waits for the answer (PDFDocumentToolShell::deferCloseQuestionAction) - by PDFDocumentToolShell::
    /// finishLeave at the end of the leave (the tool still edits): when the leave goes on, or from the event loop
    /// when it was canceled later (owner 2026-11-24: the answer is final, the tool then closes when it returns true).
    /// Default: false.
    virtual bool applyCloseQuestionAction();

    /// applyCloseQuestionAction could not keep all of the work (owner 2026-11-21: Measure - measurements, which
    /// cannot be stored as annotations): the question about the rest, opened (new + QPointer, WA_DeleteOnClose,
    /// open - never exec) and returned; the shell waits for it as for askClose (PDFDocumentToolShell::
    /// askIncompleteQuestion). Its Keep rejects it (the tool stays open with the rest), its closing answer discards
    /// the rest and calls PDFDocumentToolShell::closeAfterQuestion. It never deletes the shell. \returns nullptr =
    /// nothing to ask (the tool stays open with the rest, it has told why). Default: nullptr.
    virtual QDialog* askIncompleteClose();

    /// The question before the tool in the tab of a document closes (plan of step 6 of phase 2: Sign
    /// by Hand 435:10500 "Discard the drawing?", Redact Content 448:17473 "Keep the 12 marks in the
    /// document?"; owner 2026-11-20: every tool with unsaved work). The shell asks it only while
    /// hasUnsavedWork, before Cancel, Esc, Discard result, the close of the tab or of the window, or
    /// another route close the tool (PDFDocumentToolShell::cancel, requestLeave; never twice at once). A
    /// tool, which has something to lose, opens its question (new + QPointer, WA_DeleteOnClose, open -
    /// never exec) and returns it: the tool stays, the answer closes it later by PDFDocumentToolShell::
    /// closeAfterQuestion or leaves it open (reject). A question already open is returned too. The function
    /// never deletes the shell. \returns the open question; nullptr = nothing to ask, the tool closes.
    /// Default: the generic question of getCloseQuestion (Keep rejects it, Discard closes the tool, the third
    /// choice applyCloseQuestionAction and closes the tool when it returns true). A0: a leave waits for the question
    /// by PDFAppLeaveFlow::waitForQuestion (its end on finished, or on its deletion without an answer), never in a loop.
    virtual QDialog* askClose();

    /// The generic question of askClose, which is open (nullptr = none)
    PDFMessageBox* getGenericCloseQuestion() const { return m_closeQuestion.data(); }

    // ------------------------------------------------------------- use of an interactive tool

    /// The tool is used interactively, its session is its use, there is no run to record (owner 2026-11-20:
    /// Measure, Statistics, Object Inspector, Output Preview, Soft Proofing, Manage Certificates). Leaving it
    /// records the use (Recent tools) once per opening, when the user did something in it (markUsed, a change of
    /// a parameter of his, an export) or it was shown for 10 s (PDFDocumentToolShell::recordInteractiveUse).
    /// Default: false - the use is the accepted result of a run (newapp_tools.md 3).
    virtual bool isInteractive() const;

    /// The user has done something in an interactive tool, which is no change of a parameter (a measurement, a
    /// selected object, an action of a certificate): PDFDocumentToolShell::markInteractiveUse. No signal.
    void markUsed();

    /// Esc of the tool (owner 2026-11-17): the shell asks the GUI, while the tool is edited, no popup
    /// and no modal dialog is open and no widget of the tool has a drag (IPDFAppToolDragSource, those
    /// the shell cancels itself). The GUI ends a drag of its own, which is no widget (Redact Area /
    /// Text), or its active widget tool / input mode on the canvas (Redact Area / Text / Search, a
    /// creation tool of a drawing, a crop mode, ...) and returns true: the tool stays open. Without
    /// any of them it returns false and the shell leaves the tool (with its question, askClose). The
    /// function never deletes the shell. Default: false.
    virtual bool handleEscape();

    /// The name of the active widget tool / input mode, which Esc ends (handleEscape), empty = none
    /// (owner 2026-11-17: the status text says, what Esc does now). Default: empty.
    virtual QString getEscapeModeName() const;

    /// What Esc does now by handleEscape, empty = it does nothing there (the shell leaves the tool).
    /// Default: "Esc ends <mode>" by getEscapeModeName; a GUI, whose Esc does another thing (Manage
    /// Pages: "Esc clears the search"), says it here and handleEscape does it under the same condition.
    virtual QString getEscapeModeHint() const;

    /// What Esc does now, for a status text which mentions Esc (owner 2026-11-17: always true): a drag
    /// of a widget of the tool "Esc cancels the drag", then getEscapeModeHint ("Esc ends Area"),
    /// otherwise "Esc leaves the tool" - the order of PDFDocumentToolShell::onEscape
    QString getEscapeHint() const;

    /// The Primary button of the toolbar is shown (K6.11 of step 6: Manage Certificates is an
    /// analysis without a run, 436:9423 has no button). The shell keeps the button hidden in every
    /// mode of the toolbar and Ctrl+Enter does nothing. Default: true.
    virtual bool isConfirmShown() const;

    /// Undo and Redo of the toolbar are shown: a tool without a history (Manage Certificates, 436:9398,
    /// 436:12086: no parameters, nothing to take back) has none. Default: true.
    virtual bool isUndoShown() const;

    /// The tool changes the document of its tab while it is open (Redact Content: its marks are
    /// Redact annotations, P5 open point 3): Undo and Redo of the tool take back those changes in one
    /// order with the steps of the parameters (PDFAppToolCompositeUndoTarget). The changes are made by
    /// commitDocumentChange. Read once at the entry. Default: false.
    virtual bool hasDocumentUndoSteps() const;

    /// Commits a change of the document of the tab as a change of the tool (PDFAppDocument::commit,
    /// PDFAppChangeSource::Tool: one Undo step of the document, the tab becomes unsaved); with
    /// hasDocumentUndoSteps the step is also a step of Undo of the tool. \returns the commit was
    /// accepted. The slots of the document run: this GUI and the shell may be deleted meanwhile (the
    /// caller checks its guard).
    bool commitDocumentChange(const PDFModifiedDocument& modified, const QString& description, quint64 expectedVersion);

    // ------------------------------------------------------------- analysis kept in the tool (step 7)

    /// The tool shows the result of its run itself and stays open (plan of step 7 of phase 2, 4.1: Ink
    /// Coverage, Document Report, Compare; R7-2). The shell then opens no result dialog after a successful
    /// run: it records the use, stores the last values, gives the result to onResultKept, ends the decision of
    /// the runner and returns to Editing with the result state Current - the Primary button Disabled until the
    /// result is stale. A result, which is an export (PDFAppToolResultDialog::isAnalysisExport: Document Report
    /// writes its file in the run, 484:18811), opens the dialog of the export first; its decision returns to
    /// Editing too. Default: false (the result dialog decides, the tool closes after an accepted result).
    virtual bool isResultKeptInTool() const;

    /// A successful result of a run kept in the tool (isResultKeptInTool): the GUI stores it and shows it. Called
    /// before the dialog of an export opens; it may not delete the shell. Default: nothing.
    virtual void onResultKept(const PDFDocumentToolResult& result);

    /// A change of the parameter makes the kept result stale (R7-2: "the input changed"). Default: every
    /// parameter except "format" (the format of a later export, PDFDocumentToolTableWriter::PARAMETER_FORMAT).
    virtual bool isResultStaleBy(const QString& parameterName) const;

    /// Title and text of the amber callout of a stale result (492:109399 "Options changed"); the GUI says what
    /// changed. Defaults: "The document changed" and the text with the text of the Primary button.
    virtual QString getStaleTitle() const;
    virtual QString getStaleText() const;

    /// Where the callout of a stale result lies (default: the top of the right panel)
    virtual PDFAppToolCalloutPlace getStaleCalloutPlace() const;

    /// The decision of the dialog of an export (openExport, or the export of a run): FileSaved with the file
    /// (PDFAppToolResultOutcome::filePath, isFileOpenRequested - the GUI opens it, 484:18811), Discarded. The tool
    /// stays open in both cases. Called as the last thing of the decision; it may not delete the shell.
    /// Default: nothing.
    virtual void onExportDecided(const PDFAppToolResultOutcome& outcome);

    /// The state of the kept result (None without a shell)
    PDFAppToolResultState getResultState() const;

    /// The inputs of the GUI outside of the model have changed (Compare: Swap, Change…): a Current result
    /// becomes Stale (the callout, the Primary button). Nothing in another state.
    void markResultStale();

    /// Opens the dialog of an export of the data of the kept result (Export of the toolbar of Statistics, Ink
    /// Coverage; 480:15368, 478:15674): \p exportResult has one output file (the static writeOutput of the
    /// tool), the texts come from fillResultInput. The export never runs the tool again (plan 4.1). Only in
    /// the state Editing. \returns the dialog was opened.
    bool openExport(const PDFDocumentToolResult& exportResult);

    // ------------------------------------------------------------- flow

    /// Parameters of a run or of a preview (default: the values of the model)
    virtual QVariantMap getRunParameters() const;

    /// Parameters of a preview (default: getRunParameters). A tool, whose preview does not
    /// depend on its parameters (the scan of Remove Hidden Data, the links of Remove External
    /// Links), gives the same ones: a change of a parameter takes the cached preview.
    virtual QVariantMap getPreviewParameters() const;

    /// The Primary button of a GUI, which does its own action and never runs the tool (Keep Proofing On of
    /// Soft Proofing, RS7APP L4): the shell calls it on the confirmation (the button, Ctrl+Enter) after its
    /// checks of the state and of isConfirmEnabled, before anything of a run (no permission, no environment,
    /// no runner). \returns true, when the GUI has taken the confirmation - the shell then does nothing more;
    /// false (the default) = the tool runs. The call is the last thing the shell does (its slots may delete
    /// the shell with this GUI); an action, which closes the tool, is queued.
    virtual bool confirmWithoutRun();

    /// Inputs of a run besides the snapshot (default: as the host fills them)
    virtual void prepareEnvironment(PDFAppToolRunEnvironment* environment) const;

    /// A parameter has changed (also by Undo and Redo)
    virtual void onParametersChanged(const QString& name);

    /// The preview has changed: the new one, nullptr = none is valid any more
    virtual void onPreviewChanged(const PDFDocumentToolResult* preview);

    /// The preview computes
    virtual void onPreviewProgress(int percentage, const QString& stepText);

    /// A run has started or it has ended without a result to decide about
    virtual void onRunStateChanged(bool isRunning);

    /// prepareEnvironment was called for a run, which did not start (the runner was busy, the GUI
    /// disabled its confirmation meanwhile): what the GUI keeps for that run is dropped (S5.1 of
    /// step 5 of phase 2: a password lives only until its run starts). The default does nothing.
    virtual void onRunRefused();

    /// The run has ended with NothingToDo or Failed; the default shows an info bar over
    /// the canvas (295:2182 is a state of the panel: Compress overrides it), a lack of memory
    /// says the shell by its dialog after this function, for every tool. \returns nothing, the
    /// tool stays open.
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result);

    /// A new version of the document is shown (the facts of the panels)
    virtual void onDocumentChanged();

    /// The language has changed: the shell creates the parts again after it
    virtual void retranslate();

    // ------------------------------------------------------------- input errors

    /// Error of an input, which the model cannot hold (a page range being written),
    /// empty = valid; the Primary button is disabled while any input has an error.
    /// \p key names the input.
    void setInputError(const QString& key, const QString& error);

    /// Says, that the texts and the states of the Primary button may have changed
    void updateConfirmState();

    /// Says, that the message of the status bar may have changed
    void updateStatusText();

    /// Says, that the title, the icon or the loss text of the own tab may have changed
    void updateOwnTabState();

Q_SIGNALS:
    /// The state or the text of the Primary button has changed
    void confirmStateChanged();

    /// The message of the status bar has changed
    void statusTextChanged();

    /// The title, the icon or the loss text of the own tab may have changed (the tab
    /// updates its title and its dot); a slot may not delete the shell
    void ownTabStateChanged();

protected:
    /// The default hook of PDFDeferredDeletion::retire (step 4, PDFRetirable): the pure computations of the GUI are
    /// canceled (watchers, previews), its questions and dialogs parented to the window close without an answer
    /// (setOwner), its overlays and parts leave the view. Emits none of the GUI's signals, opens no dialog and
    /// waits for nothing. A tool overrides it for its own outside work and calls BaseClass::retire() first.
    virtual void retire() override;

private:
    /// The buttons of the generic question of askClose: the answer is the last call (the shell and this GUI go)
    void onCloseQuestionDiscardClicked();
    void onCloseQuestionActionClicked();

    QPointer<PDFDocumentToolShell> m_shell;
    std::map<QString, QString> m_inputErrors;
    QPointer<PDFMessageBox> m_closeQuestion;    ///< The generic question of askClose (new, WA_DeleteOnClose; closed by the destructor)
    QStringList m_entryInputIds;                ///< The ids of the inputs at attach (isInputListChanged)
};

/// Factory of the GUI of a tool: one class per tool, no lambdas
class PDF4QTLIBAPPSHARED_EXPORT PDFDocumentToolGUIFactory
{
public:
    PDFDocumentToolGUIFactory() = default;
    virtual ~PDFDocumentToolGUIFactory();

    PDFDocumentToolGUIFactory(const PDFDocumentToolGUIFactory&) = delete;
    PDFDocumentToolGUIFactory& operator=(const PDFDocumentToolGUIFactory&) = delete;

    /// Creates the GUI of the tool (the shell owns it)
    virtual PDFDocumentToolGUI* createGUI() const = 0;
};

/// Factory of a GUI class with the default constructor
template<typename T>
class PDFDocumentToolGUIFactoryOf : public PDFDocumentToolGUIFactory
{
public:
    virtual PDFDocumentToolGUI* createGUI() const override { return new T(); }
};

}   // namespace pdf

#endif // PDFDOCUMENTTOOLGUI_H
