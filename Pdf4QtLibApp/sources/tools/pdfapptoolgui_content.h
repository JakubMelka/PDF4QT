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

#ifndef PDFAPPTOOLGUI_CONTENT_H
#define PDFAPPTOOLGUI_CONTENT_H

#include "pdfappeditcontentcards.h"
#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentkeyboard.h"
#include "pdfappeditcontentmenus.h"
#include "pdfappeditcontentnotices.h"
#include "pdfappeditcontentview.h"
#include "pdfdocumenttoolgui.h"

#include <QPointer>

#include <map>
#include <set>
#include <vector>

class QAbstractButton;
class QButtonGroup;
class QSpinBox;
class QStackedWidget;

namespace pdf
{

class PDFAppEditContentRegistry;
class PDFAppEditContentUndoTarget;
class PDFAppEditContentToolEntry;
class PDFAppToolPanelBuilder;
class PDFAppToolPanelPopup;
class PDFMessageBox;

// Edit Content, stage E1, package P14 (GUI of the tool, shell, keys). Plan: zadani_gui.md 4.14
// (classes and ownership), 4.20 (toolbar, menus, keys), 4.21 (Apply, result, errors, leaving),
// 4.27 (edge situations); zadani.md KE.2, KE.3, KE.16, 11.2 (release behind the developer
// switch), 12.2 (QE.2). Notes: pe1_skeleton_notes.md.

/// The GUI of Edit Content (edit.content): a tool with its own model (the editing session of the
/// core), its own canvas and its own Undo.
///
/// Children - they live as long as the GUI (created by ensureContext, at the latest when the
/// shell asks getOwnUndoTarget, which it does in enter() right after attach and before the mode
/// is entered):
///   - PDFAppEditContentContext with its children: the session, the options, the selection, the
///     tool controller, the preview, the states of the left tabs (pdfappeditcontentcontext.h);
///   - PDFAppEditContentUndoTarget (getOwnUndoTarget), PDFAppEditContentNotices,
///     PDFAppEditContentMenus, PDFAppEditContentKeyboard.
/// Parts - the shell builds them by the create functions, destroys them (also before the GUI: the
/// view of the document may die first) and builds the actions and the panels again after a
/// change of the language; the GUI holds them only by QPointer and they keep no state:
///   - createToolActions: the tool buttons of the tool entries of the registry (an exclusive
///     group of icon check buttons, PDFAppToolPanelBuilder::addToolButton), Shape with its menu,
///     the toggle Multiple | Find, View (a panel filled by PDFAppEditContentMenus::fillPanel),
///     the toggle Preview | Clear Page; Find, View, Preview and Clear Page lose their texts first
///     in a narrow window (PDFAppToolToolbar::NARROW_FIRST_PROPERTY);
///   - createLeftPanel: the tab strip and the panels of the tab entries of the registry;
///   - createRightPanel: PDFAppEditContentCardStack;
///   - createCanvasPage: PDFAppEditContentCanvas with the notice widget (created once per entry).
/// The three orders of destruction (zadani_gui.md 4.14) are a lifetime scenario
/// (apptoolpageslifetime_content.cpp).
///
/// The hook retire(): nothing is emitted, no dialog opens. The context, the controller, the
/// preview, the menus and the keys end their outside work in their own hooks (children are
/// retired with the GUI). The cards are parts retired in the same retire: their connections to
/// the context are cut before the hooks run (PDFDeferredDeletion::retire, step 3), so no signal
/// of the GUI would reach them - each card forgets its pending value in its own hook, and the
/// retired context refuses every command (isEditingAllowed). R3-9.
///
/// retranslate: calls PDFAppEditContentContext::commitPendingEdits first - the shell builds
/// the actions and the panels again right after it (order (3) of zadani_gui.md 4.14), and a value
/// typed into a field of a card and not confirmed yet is committed as its command, never lost.
///
/// P14 (over the skeleton): the toolbar, Apply Changes (getRunParameters with the snapshot, the
/// result, the dialog of the errors of writing with Skip Page / Back to Editing), the close and
/// save questions, the rules of a page switch (pageRequested of the context: typing ends, the
/// pending values are committed, the group is left, the selection is cleared, then setPage), the
/// status texts, the notices of the states (requested again from the event loop after a change
/// of the language), Clear Page, Go to page, the reaction to a document changed under the tool.
/// confirmStateChanged is emitted whenever the undo stack crosses its clean point (also by Undo
/// and Redo): Apply Changes, the save question and the deferred reload follow it.
///
/// The dialogs of the tool (Clear Page, Go to page, -apply-error) are new PDFMessageBox objects
/// owned logically by the GUI (PDFDeferredDeletion::setOwner, WA_DeleteOnClose, open - never
/// exec); their buttons answer into member slots; the run after Skip Page and a tool of the menu
/// Shape start from the event loop.
///
/// Bounds (hostile input): the object counts of a page (Clear Page, the complex page) visit each
/// node of the container once (an explicit stack over a tree); the recognition of a scan goes at
/// most 8 levels deep and stops at the second object of a level; the dialog -apply-error lists at
/// most MAXIMUM_LISTED_ERRORS errors and counts the rest.
///
/// Threads: GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolGUIContent : public PDFDocumentToolGUI
{
    Q_OBJECT

private:
    using BaseClass = PDFDocumentToolGUI;

public:
    explicit PDFAppToolGUIContent(QObject* parent = nullptr);
    virtual ~PDFAppToolGUIContent() override;

    /// Registers the core tool edit.content and this GUI, or removes both
    /// (PDFDocumentToolRegistry, PDFDocumentToolGUIRegistry with the page of the tool). Until the
    /// tool is released (after stage E7, QE.13) it is registered only while the developer mode of
    /// the application is on: PDFAppContext calls this with the value of the setting
    /// advanced.developerMode when the storage opens and when the setting changes, and with false
    /// when the storage closes. With false the catalog, the route of the tool (Page Not Found) and
    /// MCP are as without this tool. Process wide, GUI thread; calling it again with the same value
    /// does nothing.
    static void registerContentTools(bool isRegistered);

    /// The tool is registered (registerContentTools(true) was the last call)
    static bool isContentToolRegistered();

    /// The registry of the parts, which the contexts of the GUIs created from now on use
    /// (PDFAppEditContentContext); nullptr (the default) = PDFAppEditContentRegistry::getInstance.
    /// A test of the skeleton or of a package gives the real tool a registry of its own (test
    /// entries), and must set nullptr again before the registry dies; it must outlive every tool
    /// opened with it. GUI thread.
    static void setRegistry(const PDFAppEditContentRegistry* registry);

    /// The context of the tool; nullptr before the GUI is attached to a shell with a document
    PDFAppEditContentContext* getEditContext() const { return m_context.data(); }

    /// Children and parts (tests; nullptr = not created yet or gone)
    PDFAppEditContentNotices* getNotices() const { return m_notices.data(); }
    PDFAppEditContentMenus* getMenus() const { return m_menus.data(); }
    PDFAppEditContentKeyboard* getKeyboard() const { return m_keyboard.data(); }
    PDFAppEditContentCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppEditContentCardStack* getCardStack() const { return m_cardStack.data(); }
    QWidget* getLeftPanelWidget() const { return m_leftPanelWidget.data(); }
    QWidget* getToolActionsWidget() const { return m_toolActionsWidget.data(); }

    /// The objects of a page, from which it is a complex page (zadani.md QE.10 (a): 20 000 objects; the integration of
    /// stage E6 adjusts it after the measurement)
    static constexpr PDFInteger COMPLEX_PAGE_OBJECT_COUNT = 20000;

    /// The most errors of writing listed in the dialog -apply-error (hostile input: one row per error); the rest is
    /// told by a count
    static constexpr int MAXIMUM_LISTED_ERRORS = 20;

    // ---- parts
    virtual QWidget* createToolActions(QWidget* parent) override;   ///< The tool buttons of the registry, Multiple, the buttons of the TOOLBAR entries, View (zadani_gui.md 4.20)
    virtual QWidget* createLeftPanel(QWidget* parent) override;     ///< The tab strip (Sidebar tab) and the panels of the tab entries; the tab shown is the option LEFT_TAB
    virtual QWidget* createRightPanel(QWidget* parent) override;    ///< PDFAppEditContentCardStack
    virtual QWidget* createCanvasPage(QWidget* parent) override;    ///< PDFAppEditContentCanvas with the notice; shows the page of the view of the host; installs the keys
    virtual void setContentEnabled(bool isEnabled) override;        ///< PDFAppEditContentContext::setRunning

    // ---- texts and states of the shell
    virtual QString getConfirmText() const override;                ///< "Apply Changes"
    virtual QString getConfirmIconName() const override;            ///< "check" (edit-content-v2)
    virtual bool isConfirmEnabled() const override;                 ///< The undo stack is not at its clean point (RE-16: locks and hidden objects are no work)
    virtual QString getStatusText() const override;                 ///< Preview, the active tool, the state of the page; else the default
    virtual QString getOwnTabPageText() const override;             ///< "Page 3 of 1,592"
    virtual QString getOwnTabZoomText() const override;             ///< "151 %" of the view of the tool; empty without a view
    virtual bool hasOwnStatusItems() const override;                ///< true
    virtual bool hasUnsavedWork() const override;                   ///< The undo stack is not at its clean point
    virtual PDFAppToolCloseQuestion getCloseQuestion() const override;  ///< The frame -discard
    virtual bool handleEscape() override;                           ///< KE.16: the drag and the mode of the active tool, the group, the selection
    virtual QString getEscapeModeName() const override;             ///< The mode label of the active tool
    virtual QString getEscapeModeHint() const override;             ///< As handleEscape: the drag, the mode, the group, the selection

    // ---- the three hooks of the tool with its own model (pdfdocumenttoolgui.h)
    virtual PDFAppToolUndoTarget* getOwnUndoTarget() override;

    /// The commands of the application routed to the tool (zadani_gui.md 4.20, way 2): only
    /// PDFAppEditContentContext::getApplicationCommandState / handleApplicationCommand (the active
    /// tool, then the action entry of the command; NotHandled without a context). A package adds
    /// a routed command by an action entry (PDFAppEditContentActionEntry::getApplicationCommandIds),
    /// never here. Real code of the skeleton; Undo, Redo, Save and Save As stay with the shell.
    virtual PDFAppToolCommandState getDocumentCommandState(const QString& commandId) const override;
    virtual bool handleDocumentCommand(const QString& commandId) override;

    virtual PDFAppToolSaveQuestion getSaveQuestion() const override;                                    ///< The frame -save-prompt

    /// The automatic refresh does not reload the document while the session holds work (zadani_gui.md 4.21;
    /// R3-6): hasUnsavedWork. Real code of the skeleton (P14 keeps it as long as hasUnsavedWork is the policy).
    virtual bool isDocumentReloadDeferred() const override;

    // ---- flow
    virtual QVariantMap getRunParameters() const override;          ///< mode apply, the snapshot of the session, the pages skipped after an error of writing
    virtual bool confirmWithoutRun() override;                      ///< Commits the pending values of the cards (they belong to the run); never takes the confirmation
    virtual void onRunNotAccepted(const PDFDocumentToolResult& result) override;    ///< The dialog -apply-error for the errors of the pages (RE-19), a notice otherwise
    virtual void fillResultInput(PDFAppToolResultInput* input) const override;      ///< The texts of the frame -done
    virtual void onDocumentChanged() override;                      ///< zadani_gui.md 4.21: the notice of a document changed under the tool
    virtual void retranslate() override;                            ///< commitPendingEdits first (see the class), then the notices of the states again

    // ---- requests of the action entries of P14 (pdfappeditcontentkeyboard.cpp) and tests

    /// Clear Page: the question -clear-confirm (open, never exec); its Clear Page executes ClearPage of the page
    /// shown. \returns the question, nullptr = nothing to clear.
    PDFMessageBox* openClearPageQuestion();

    /// Go to page of the application (Ctrl+G): the question of the page number; its Go to Page asks the page by
    /// PDFAppEditContentContext::requestPage. \returns the question, nullptr = a document of one page.
    PDFMessageBox* openGoToPage();

    /// The open dialogs of the tool (tests; nullptr = none)
    PDFMessageBox* getClearPageQuestion() const { return m_clearQuestion.data(); }
    PDFMessageBox* getGoToPageQuestion() const { return m_goToPageQuestion.data(); }
    PDFMessageBox* getApplyErrorDialog() const { return m_applyErrorDialog.data(); }

    /// The pages (from 0) the next run leaves out (Skip Page of the dialog -apply-error; cleared by the next change
    /// of the model)
    const std::set<PDFInteger>& getSkippedPages() const { return m_skippedPages; }

    /// The tool buttons and the buttons of the action entries of the toolbar by their identifiers (tests; the
    /// buttons of the actions shown now)
    QAbstractButton* getToolButton(const QString& toolId) const;
    QAbstractButton* getActionButton(const QString& actionId) const;
    QAbstractButton* getViewButton() const;
    QAbstractButton* getShapeButton() const;
    QAbstractButton* getMultipleButton() const { return m_multipleButton.data(); }

    /// The tab buttons of the left panel by the identifiers of the tabs (tests)
    QAbstractButton* getTabButton(const QString& tabId) const;

    /// The notice widget of the canvas (tests; nullptr = none)
    PDFAppEditContentNotice* getNoticeWidget() const { return m_noticeWidget.data(); }

protected:
    virtual void retire() override;

private:
    /// Creates the context and the children of the GUI, when the GUI is attached to a shell with
    /// a document and they do not exist yet. \returns the context exists.
    bool ensureContext();

    /// The editing became allowed or not (a container loaded, Preview, a run): the states of the routed application
    /// commands follow it (the default isEnabled of an entry), so the actions of the window are refreshed
    /// (updateDocumentCommands). A change of the selection or of the clipboard is told by P14 / the packages the same way.
    void onEditingAllowedChanged();

    // ---- slots of the children (the context, the session, the options, the controller, the selection)
    void onPageRequested(pdf::PDFInteger pageIndex);
    void onPageChanged();
    void onContainerChanged();
    void onPreviewActiveChanged();
    void onCommandRefused(const QString& text);
    void onModelChanged();
    void onOptionsChanged(const QString& name);
    void onToolStateChanged();
    void onSelectionChanged();

    // ---- slots of the parts
    void onToolButtonClicked(QAbstractButton* button);
    void onMultipleToggled(bool isChecked);
    void onActionButtonClicked();
    void onViewButtonToggled(bool isChecked);
    void onTabClicked(int index);
    void onViewChanged();

    // ---- slots of the dialogs
    void onClearPageConfirmed();
    void onGoToPageConfirmed();
    void onApplyErrorBack();
    void onApplyErrorSkip();
    void onSkipRunQueued();
    void onRetranslateQueued();
    void onShapeRowClicked();
    void onShapeToolQueued();

    /// The button Shape with its panel of the shape tools (the tool entries of the place ShapeMenu)
    void addShapeMenu(PDFAppToolPanelBuilder* builder, const std::vector<const PDFAppEditContentToolEntry*>& shapes);

    /// The states of the buttons of the toolbar: the checked tool, the visibility, the enabled and the checked state
    /// of the buttons of the entries
    void updateToolbarStates();

    /// The tab of the left panel shown (the option LEFT_TAB)
    void updateLeftTab();

    /// Requests or hides the notices of the states (conformance, Preview, the page, the stale document) with their
    /// texts of now; \p isPageEntered - a new page is shown: its states are entered again (a closed notice comes back)
    void updateStateNotices(bool isPageEntered);

    /// The node of an error of writing, which Back to Editing selects, when its container is loaded
    void selectPendingErrorNode();

    /// The dialog -apply-error for the errors of the pages of a failed run (DATA_PAGE_ERRORS)
    void openApplyErrorDialog(const PDFDocumentToolResult& result);

    /// The parent of the dialogs of the tool (the key widget of the host)
    QWidget* getDialogParent() const;

    QPointer<PDFAppEditContentContext> m_context;           ///< A child
    QPointer<PDFAppEditContentUndoTarget> m_undoTarget;     ///< A child
    QPointer<PDFAppEditContentNotices> m_notices;           ///< A child
    QPointer<PDFAppEditContentMenus> m_menus;               ///< A child
    QPointer<PDFAppEditContentKeyboard> m_keyboard;         ///< A child
    QPointer<PDFAppEditContentCanvas> m_canvas;             ///< A part
    QPointer<PDFAppEditContentCardStack> m_cardStack;       ///< A part
    QPointer<QWidget> m_leftPanelWidget;                    ///< A part
    QPointer<QWidget> m_toolActionsWidget;                  ///< A part

    // The widgets of the parts (held only by QPointer; the parts are built again after a change of the language)
    QPointer<QButtonGroup> m_toolGroup;
    std::map<QString, QPointer<QAbstractButton>> m_toolButtons;
    std::map<QString, QPointer<QAbstractButton>> m_actionButtons;
    QPointer<QAbstractButton> m_multipleButton;
    QPointer<PDFAppToolPanelPopup> m_viewPanel;
    QPointer<PDFAppToolPanelPopup> m_shapePanel;
    QString m_pendingShapeToolId;                           ///< The row of Shape clicked, its tool is started from the event loop
    QPointer<QButtonGroup> m_tabGroup;
    QPointer<QStackedWidget> m_tabStack;
    std::vector<QString> m_tabIds;                          ///< The tab of each index of m_tabGroup and m_tabStack
    QPointer<PDFAppEditContentNotice> m_noticeWidget;

    // The dialogs of the tool (new, WA_DeleteOnClose, owned logically by this GUI: retire closes them)
    QPointer<PDFMessageBox> m_clearQuestion;
    QPointer<PDFMessageBox> m_goToPageQuestion;
    QPointer<QSpinBox> m_goToPageField;
    QPointer<PDFMessageBox> m_applyErrorDialog;

    std::set<PDFInteger> m_skippedPages;        ///< The pages the next run leaves out
    std::vector<PDFInteger> m_errorPages;       ///< The pages (from 0) of the open dialog -apply-error, which Skip leaves out
    PDFInteger m_errorPage = -1;                ///< The first page with an error (Back to Editing)
    QString m_errorNodeId;                      ///< The identifier of the first object with an error (PDFContentJson::writeNodeId)
    QString m_pendingErrorNodeId;               ///< Back to Editing: selected when the container of the page is loaded
    bool m_isClean = true;                      ///< The undo stack was at its clean point (confirmStateChanged at a crossing)
    bool m_isDocumentStale = false;             ///< The document changed under the tool (onDocumentChanged, the error of the fingerprint)
    bool m_isScannedPage = false;               ///< The page shown is a scan (measured by updateStateNotices)
    bool m_isComplexPage = false;               ///< The page shown has COMPLEX_PAGE_OBJECT_COUNT objects (measured by updateStateNotices)
};

}   // namespace pdf

#endif // PDFAPPTOOLGUI_CONTENT_H
