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

#ifndef PDFAPPEDITCONTENTCONTEXT_H
#define PDFAPPEDITCONTENTCONTEXT_H

#include "pdfappeditcontentview.h"
#include "pdfappglobal.h"
#include "pdfcms.h"
#include "pdfcontentcommands.h"
#include "pdfcontenteditsession.h"
#include "pdfdeferreddeletion.h"
#include "pdfdocument.h"
#include "pdfdocumenttoolgui.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QTransform>
#include <QVariant>
#include <QVariantMap>

#include <map>
#include <optional>
#include <vector>

namespace pdf
{

class PDFAppEditContentCanvas;
class PDFAppEditContentPreview;
class PDFAppEditContentRegistry;
class PDFAppEditContentScene;
class PDFAppEditContentSelection;
class PDFAppEditContentToolController;
class PDFAppEditContentView;
class PDFAppToolParameterModel;
class PDFDocumentToolGUI;
struct PDFContentDisplayCounts;

// Edit Content, stage E1, skeleton. Plan: zadani_gui.md 4.14 (classes and ownership), 4.19
// (pending values of the cards), 4.26 (stored options), 4.27 (edge situations); zadani.md KE.28,
// 12.2 (QE.2); zadani_overeni.md 6.3. Notes: pe1_skeleton_notes.md sections 1 - 3. Real code of
// the skeleton; changed only by the coordinator.

/// A notice of the tool (zadani_gui.md 4.15 "Notices"; KE.28): one place at the bottom edge of
/// the canvas, one notice at a time. A value; who wants a notice gives it to
/// PDFAppEditContentContext::showNotice, the notices of P14 (pdfappeditcontentnotices.*) decide
/// what is shown.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentNoticeRequest
{
public:
    /// When the notice leaves by itself
    enum class Kind
    {
        State,  ///< A state of the tool (Preview, a scanned page, a complex page, a read-only page): shown until hideNotice of its id; closed by the user it comes back, when the state is entered again
        Event   ///< Something has just happened (a fallback font, a converted color, a click on a locked object): it leaves with the next change of the selection
    };

    enum class Severity
    {
        Information,
        Warning,
        Error       ///< The red notice (edit-content-v2-text-missing-glyph)
    };

    QString id;                 ///< Identifier of the kind of the notice ("preview", "conformance", "locked-object"); a notice shown again replaces the one of the same id
    Kind kind = Kind::Event;
    Severity severity = Severity::Information;
    QString title;              ///< Translated; may be empty
    QString text;               ///< Translated; wrapped by the widget
    QString actionId;           ///< An action of the registry offered as the button of the notice (its title is the text of the button); empty = none
    bool isOncePerSession = false;  ///< Closed by the user it is not shown again until the tool closes (QE.2: the notice of PDF/A and PDF/UA)

    bool isValid() const { return !id.isEmpty() && !text.isEmpty(); }
    bool operator==(const PDFAppEditContentNoticeRequest&) const = default;
};

/// A level of the editing inside of a group (zadani_gui.md 4.15 "bar of the group", 4.16; the
/// frames -isolation and -group-isolation): the content of a form XObject, of a pattern cell, or
/// of a plain or clipping group of the same container. The levels are the path of the bar from
/// the page to the innermost one. Stage E4 (P43) enters and leaves; the type is declared now, so
/// the canvas, the tree and the cards of E1 read one "edited root" from the start.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentIsolationLevel
{
public:
    PDFContentNodeReference root;       ///< The node, whose children are edited: the root of a form or of a pattern cell, or a Group / MarkedContent node
    PDFContentNodeReference entry;      ///< The node, through which the level was entered (the form use, the object with the pattern); not valid for a group of the same container
    QString title;                      ///< The name of the segment of the bar ("Chart area"; data or a translated kind)

    bool operator==(const PDFAppEditContentIsolationLevel&) const = default;
};

/// The options of the display of the tool, which are no work and no undo step (zadani_gui.md
/// 4.26): what the menu View, the card Grid and units, the card Preview and the left panel set.
/// A value is read from the stored parameter of the tool of the same name, when the tool has it
/// (PDFAppToolParameterModel of the shell, written without an undo step), otherwise it is kept
/// here until the tool closes - so the packages use the names below from the start and the
/// storage begins to work when the core tool declares the parameters (pe1_skeleton_notes.md
/// section 9).
///
/// Threads and ownership: a child of the context, GUI thread. The parameter model is not owned
/// (QPointer; the shell deletes the GUI with the context before the model). changed is emitted
/// last.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentOptions : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Names of the options (= names of the stored parameters). Bool unless said otherwise.
    static constexpr const char* SHOW_RULERS = "showRulers";                    ///< Default false
    static constexpr const char* SHOW_GRID = "showGrid";                        ///< Default false
    static constexpr const char* SHOW_GUIDES = "showGuides";                    ///< Default true
    static constexpr const char* SHOW_CLIPPING_PATHS = "showClippingPaths";     ///< Default false
    static constexpr const char* SHOW_INVISIBLE_TEXT = "showInvisibleText";     ///< Default false
    static constexpr const char* SHOW_ANNOTATIONS = "showAnnotations";          ///< Default true (the dimmed underlay)
    static constexpr const char* SHOW_READING_ORDER = "showReadingOrder";       ///< Default false
    static constexpr const char* SNAP_TO_OBJECTS = "snapToObjects";             ///< Default true
    static constexpr const char* SNAP_TO_GRID = "snapToGrid";                   ///< Default true (grid and guides)
    static constexpr const char* UNITS = "units";                               ///< String "pt", "mm", "in"; default "pt"
    static constexpr const char* GRID_SPACING = "gridSpacing";                  ///< Real, points; default 36
    static constexpr const char* GRID_ORIGIN = "gridOrigin";                    ///< String "bottom-left", "top-left"; default "bottom-left"
    static constexpr const char* PREVIEW_OVERPRINT = "previewOverprint";        ///< Default true
    static constexpr const char* PREVIEW_ANNOTATIONS = "previewAnnotations";    ///< Default true
    static constexpr const char* PREVIEW_PAPER_COLOR = "previewPaperColor";     ///< String "#rrggbb"; default "#ffffff"
    static constexpr const char* LEFT_TAB = "leftTab";                          ///< String, the id of the tab; default "objects"

    explicit PDFAppEditContentOptions(QObject* parent);
    virtual ~PDFAppEditContentOptions() override;

    /// The stored parameters (nullptr = none: every value is kept here). The values kept here
    /// until now are not copied into the model.
    void setModel(PDFAppToolParameterModel* model);

    /// The value of the option: the parameter of the model, when it has one of the name; else the
    /// value kept here; else getDefaultValue. A name of a later stage (the appearance of the
    /// next shape, the options of Find) works the same way without being listed above.
    QVariant getValue(const QString& name) const;
    bool getBool(const QString& name) const;

    /// Sets the value; emits changed(name) last, only when the value changed
    void setValue(const QString& name, const QVariant& value);

    /// The default of a name listed above; an invalid value for another name
    static QVariant getDefaultValue(const QString& name);

Q_SIGNALS:
    /// The value of the option changed (also by the model: Undo of the parameters does not
    /// exist in this tool, but a restore of the stored values does)
    void changed(const QString& name);

private:
    void onModelChanged(const QString& name);
    bool hasModelParameter(const QString& name) const;

    QPointer<PDFAppToolParameterModel> m_model;
    QVariantMap m_values;
};

/// The one object, which the parts of Edit Content share (pe1_skeleton_notes.md section 1): it
/// owns what lives as long as the tool, holds the state the packages read from each other, and
/// is the only way of a command into the model.
///
/// Ownership: a child of the GUI of the tool (PDFAppToolGUIContent creates it in ensureContext,
/// at the latest when the shell asks getOwnUndoTarget - in PDFDocumentToolShell::enter after
/// attach, before the mode is entered). Its children, created in its constructor in this order:
/// the session (core), the options, the selection, the tool controller, the preview, the state
/// object of each tab entry of the registry; the last step of the constructor creates the tools
/// (PDFAppEditContentToolController::createTools), so a tool and a tab state find every other
/// child in their constructors. The destructor of the context deletes them explicitly in the
/// reverse order - the tab states (in the reverse order of the tabs), the preview, the controller
/// with the tools, the selection, the options, the session last - while the context is still
/// whole (Qt alone would delete the children in the order of their creation, the session first):
/// in its destructor a tab state may read the session, the options, the selection, the controller
/// and the preview; a tool (deleted with the controller) the session, the options and the
/// selection; the getters of the children deleted already return nullptr then. No package parents
/// another object to the context (Qt would delete it after the session): an object, which lives as
/// long as the tool, is a tab state or a child of the GUI. Not owned,
/// held by QPointer: the GUI (setGUI), the canvas (setCanvas; a part - it dies with the view of
/// the document, possibly before the context, and is created once per entry of the tool).
///
/// The three orders of destruction (zadani_gui.md 4.14): (1) the shell deletes the parts, then
/// the GUI with this context - the parts never touch the context in their destructors; (2) the
/// view of the document dies first - the parts go, getCanvas / getScene / getView return nullptr
/// and every user checks them; (3) a change of the language - the actions and the panels are
/// built again while this context and its children stay, so a part keeps nothing the user would
/// lose. PDFDeferredDeletion::retire of the shell retires the GUI and this context with it: the
/// hook retire() drops the queued commands and actions, emits nothing; the session cancels its
/// builds in its destructor.
///
/// Threads: GUI thread only (the session too). A worker (the preview, a build) gets plain copies
/// and shared pointers of immutable data and returns by a queued call to its own object.
///
/// Signals: every signal is emitted last; a slot may close the tool (the context is retired or
/// deleted when the emission returns - the functions guard themselves and say so).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentContext : public QObject, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// \param document The document being edited (held by the session)
    /// \param cms Color management (may be nullptr)
    /// \param registry The parts of the tool; nullptr = PDFAppEditContentRegistry::getInstance.
    ///        It must outlive the context (a test passes its own one).
    /// \param parent The GUI of the tool (a test: any object)
    explicit PDFAppEditContentContext(PDFDocumentPointer document, PDFCMSPointer cms, const PDFAppEditContentRegistry* registry, QObject* parent);
    virtual ~PDFAppEditContentContext() override;

    // ------------------------------------------------------------- objects, which live as long as the tool (never nullptr,
    //                                                               also in the constructors of the tools and of the tab states)

    const PDFAppEditContentRegistry* getRegistry() const { return m_registry; }
    PDFContentEditSession* getSession() const { return m_session; }

    /// The document being edited as the shared pointer the context was made with (the document of
    /// getSession; never nullptr). For a job in another thread, which must keep the document alive
    /// (the decoding of an image, the rendering of Preview): the job holds the pointer, the GUI
    /// thread reads the document through the session as before.
    const PDFDocumentPointer& getDocumentPointer() const { return m_document; }
    PDFAppEditContentOptions* getOptions() const { return m_options; }
    PDFAppEditContentSelection* getSelection() const { return m_selection; }
    PDFAppEditContentToolController* getToolController() const { return m_toolController; }
    PDFAppEditContentPreview* getPreview() const { return m_preview; }

    /// The state object of a left tab (PDFAppEditContentTabEntry::createState), nullptr = the tab
    /// has none or is not registered
    QObject* getTabState(const QString& tabId) const;

    // ------------------------------------------------------------- the GUI and the parts (not owned; nullptr = none or gone)

    /// The GUI of the tool: its host (the window of a dialog, the key widget), its shell, its
    /// parameter model. Set by the GUI after it was attached; it also gives the model to the
    /// options. A test may leave it unset.
    void setGUI(PDFDocumentToolGUI* gui);
    PDFDocumentToolGUI* getGUI() const { return m_gui.data(); }

    /// The widget, to whose window the dialogs of the tool belong (the key widget of the host),
    /// nullptr without a GUI
    QWidget* getDialogParent() const;

    /// The canvas registers itself in its constructor; a second canvas replaces the first
    void setCanvas(PDFAppEditContentCanvas* canvas);
    PDFAppEditContentCanvas* getCanvas() const { return m_canvas.data(); }
    PDFAppEditContentScene* getScene() const;
    PDFAppEditContentView* getView() const;

    // ------------------------------------------------------------- page and edited container

    PDFInteger getPageCount() const;

    /// The page shown on the canvas, -1 before the first setPage
    PDFInteger getPageIndex() const { return m_pageIndex; }

    /// The key of the page shown (not valid before the first setPage)
    PDFContentContainerKey getPageKey() const;

    /// The node, whose children are edited now: the root of the page container, or the root of
    /// the innermost level of getIsolationPath. The tab Objects shows its subtree, the canvas
    /// dims the rest. Not valid before the first setPage; its node is not valid, while the
    /// container is not loaded yet (the root identifier is taken from the loaded container).
    PDFContentNodeReference getEditedRoot() const;

    /// The container being edited (of getEditedRoot) and its current version, nullptr while it
    /// loads or after it failed
    PDFContentContainerKey getContainerKey() const;
    PDFContentContainerPointer getContainer() const;

    /// The levels of the editing inside of groups, from the outermost; empty = the page itself
    const std::vector<PDFAppEditContentIsolationLevel>& getIsolationPath() const { return m_isolationPath; }

    /// Asks for another page (the tab Pages, an application command, the status bar): only emits
    /// pageRequested. The GUI of the tool (P14) applies the rules of zadani_gui.md 4.27 (typing
    /// ends, pending values are committed, the group is left, the selection cleared) and calls
    /// setPage. An index out of range is ignored.
    void requestPage(PDFInteger pageIndex);

    /// Shows the page: the state only (the isolation path is cleared, the container requested
    /// from the session when it is not loaded); emits pageChanged last. Called by P14 after its
    /// rules; nothing for the page shown already or an index out of range.
    void setPage(PDFInteger pageIndex);

    /// Replaces the levels (P43, stage E4, after its rules); emits containerChanged last
    void setIsolationPath(std::vector<PDFAppEditContentIsolationLevel> path);

    /// Asks to enter the node (a group, a form use, an object with a pattern cell: a double
    /// click, Enter, Enter Group of a card) / to leave \p levelCount levels (Leave, Esc, a
    /// double click on the dimmed page, a segment of the bar): only emits isolationRequested /
    /// leaveIsolationRequested (P43). Before stage E4 nobody listens: nothing happens.
    void requestIsolation(const PDFContentNodeReference& node);
    void requestLeaveIsolation(int levelCount);

    // ------------------------------------------------------------- commands

    /// The model may be changed now: a page is shown, its container is loaded and not read-only,
    /// the preview is off, no run of the tool is in progress (setRunning) and the session is not
    /// inside of a change. The tools, the cards and the actions are disabled otherwise.
    bool isEditingAllowed() const;

    /// The one rule of the lock (R7-4; QE.8, RE-16): the selected nodes, which a command of the
    /// user may change - a command of a card, a drag of a row of the tree, an action of a menu and
    /// a gesture of the canvas all take their nodes from here, so a locked object is left alone
    /// everywhere. A node passes isNodeChangeable; the nodes keep the order of the selection (the
    /// key object of PDFAppEditContentSelection::getKeyNode is in the list exactly when it passes).
    /// The whole selection is taken (never a part of it: a cap of a card is a cap of what it shows,
    /// never of what its command changes, R7-1). Cost: the selection x the depth of the nodes
    /// (PDFContentContainer::MAXIMUM_DEPTH at most), one lookup of the lock per level.
    std::vector<PDFContentNodeReference> getChangeableSelection() const;

    /// The node may be changed by a command of the user: a node of the edited container
    /// (getContainer), inside of the edited root and not the root itself, not of the kind Opaque
    /// (its content is kept byte for byte), and neither the node nor one of its ancestors up to
    /// the edited root is locked (PDFContentEditSession::isNodeLocked: a locked group locks what
    /// it holds). False while the container is not loaded. The descendants are not asked: a lock
    /// protects an object from a direct selection and a direct command; a command on an unlocked
    /// group changes the group as a whole with a locked object inside of it (owner 2026-11-28).
    bool isNodeChangeable(const PDFContentNodeReference& node) const;

    /// The one policy of the complex page (zadani.md QE.10, 12.1; integration row 27): the
    /// container is complex above PDFAppEditContentScene::COMPLEX_PAGE_OBJECTS nodes or above
    /// PDFAppEditContentScene::COMPLEX_PAGE_PATH_SEGMENTS segments of its paths (the counts of
    /// PDFContentDisplay::getCounts, which the core keeps per version). The canvas (a drag shows
    /// the common outline) and the notice of the tool GUI both ask this.
    static bool isComplexPage(const PDFContentDisplayCounts& counts);

    // ------------------------------------------------------------- a new version of the document (zadani_gui.md 4.21)

    /// Loads the session again over \p document, a new version of the document of the tab, while
    /// the tool has no work ("a session without work is loaded again"; the GUI of the tool calls
    /// it from onDocumentChanged when the undo stack is clean and no value of a card is pending -
    /// with work the stale-document notice stays). Nothing for a retired context, a null document
    /// or the document of the session.
    ///
    /// In this order: the queued commands and actions are dropped, an open macro is closed; the
    /// selection and the node of the card Locked object are cleared, the active tool goes back to
    /// Select (its gesture and mode end); a new session over \p document replaces the old one
    /// (getSession, getDocumentPointer follow; a clean undo stack; the locks and hidden flags of
    /// the old session are gone - they belong to the nodes of the old version); the old session
    /// is disconnected and deleted from the event loop (its builds and their late results die
    /// with it - a build of the old session never reaches a holder of the new one). The page
    /// index stays (clamped to the pages of the new version), the isolation path is cleared, the
    /// container of the page is requested again. Then, each emission guarded (a slot may close the
    /// tool): sessionReplaced (every holder of a connection to the session connects to the new
    /// one), pageChanged (every part reads the page again: the scene, the tabs, the cards, the
    /// preview start over with new request numbers), editingAllowedChanged.
    void reloadDocument(PDFDocumentPointer document);

    /// Executes the command now (PDFContentEditSession::execute: one undo step, or a part of the
    /// open macro). \returns true = applied. A refused command leaves the model unchanged; its
    /// text is kept (getLastRefusalText) and commandRefused is emitted last (P14 shows the
    /// notice). Refused without calling the session: a null command, !isEditingAllowed(). Never
    /// call it from a slot of PDFContentEditSession::modelChanged, of QMenu::triggered, of an item
    /// view or from dropMimeData - use requestExecute there. The slots of the session run inside
    /// of the call: the tool may be closed when it returns (the caller guards itself).
    bool execute(const PDFContentCommandPointer& command);

    /// Executes the command from the event loop (in the order of the requests); a request of a
    /// context retired meanwhile is dropped. The result is told by the signals of the session or
    /// by commandRefused.
    void requestExecute(const PDFContentCommandPointer& command);

    /// The commands executed between the two calls are one undo step of the text (one gesture:
    /// a drag with a copy, a value given to the leaves of a multiple selection). Nested calls
    /// count; a macro left open is closed by retire.
    void beginMacro(const QString& text);
    void endMacro();

    /// The translated reason of the last refused command (empty after a command, which was applied)
    const QString& getLastRefusalText() const { return m_lastRefusalText; }

    /// Commits / discards the value of a field of a card, which the user has typed and not
    /// confirmed yet (zadani_gui.md 4.19): emits pendingEditsCommitRequested /
    /// pendingEditsDiscardRequested; a card with such a value executes its command in the slot
    /// (commit) or forgets the value (discard). Commit is called by P14 before Undo / Redo, a
    /// switch of the page, Apply and in retranslate (before the shell builds the parts again), by
    /// the tool controller before a switch of the tool. Discard is for a package, which drops the
    /// typed values deliberately (none in stage E1); at the end of the tool nobody discards - the
    /// connections of the retired cards are cut before the hooks run, each card forgets its value
    /// in its own retire hook (R3-9). Never called inside of a command.
    void commitPendingEdits();
    void discardPendingEdits();

    // ------------------------------------------------------------- modes

    /// Preview is on (zadani_gui.md 4.15): the editing is paused (isEditingAllowed is false), the
    /// canvas shows the picture of PDFAppEditContentPreview. Set by the action of the toolbar
    /// toggle and of the key P (P14) after commitPendingEdits; emits previewActiveChanged last.
    bool isPreviewActive() const { return m_isPreviewActive; }
    void setPreviewActive(bool isActive);

    /// A run of the tool is in progress (Apply Changes; PDFDocumentToolGUI::setContentEnabled
    /// of P14): the editing is paused; emits editingAllowedChanged last
    bool isRunning() const { return m_isRunning; }
    void setRunning(bool isRunning);

    // ------------------------------------------------------------- cards

    /// A card, which is shown on request (PDFAppEditContentCardIds: Keyboard, Grid and units,
    /// Locked object): the entry of the card reads it in isShown. Emits cardsChanged last.
    bool isCardRequested(const QString& cardId) const { return m_requestedCards.contains(cardId); }
    void setCardRequested(const QString& cardId, bool isRequested);

    /// The expanded state of a card section (not stored, zadani_gui.md 4.26; it survives the
    /// rebuild of the stack and of the panel). \p sectionId is the id of the card, or
    /// "<card>/<section>" of a card of more sections. No signal: the section itself changes it.
    bool isSectionExpanded(const QString& sectionId) const { return m_expandedSections.contains(sectionId); }
    void setSectionExpanded(const QString& sectionId, bool isExpanded);

    /// The node the card Locked object speaks about (a click on a locked object, zadani_gui.md
    /// 4.15 "-locked-click"); not valid = none. Set by the tool Select together with the request
    /// of the card; cleared by the next change of the selection (P11).
    const PDFContentNodeReference& getLockedClickNode() const { return m_lockedClickNode; }
    void setLockedClickNode(const PDFContentNodeReference& node);

    // ------------------------------------------------------------- requests, which another package answers

    /// Shows / hides a notice: only emits noticeRequested / noticeHideRequested (P14)
    void showNotice(const PDFAppEditContentNoticeRequest& request);
    void hideNotice(const QString& noticeId);

    /// Opens the menu \p menuId of the registry (PDFAppEditContentMenuIds: CONTEXT for the
    /// selection on the canvas and in the tab Objects, LAYER_ROW of the tab Layers, TAG_ROW of the
    /// tab Structure) at the global position: only emits contextMenuRequested (P12,
    /// pdfappeditcontentmenus.*, opens it). Nothing for an empty menu id.
    void requestContextMenu(const QString& menuId, const QPoint& globalPosition);

    /// Runs an action of the registry from the event loop: PDFAppEditContentActionEntry::trigger
    /// (or triggerChoice with \p choiceId) is called by a queued slot, when the action is still
    /// visible and enabled then and the context is not retired. \returns false for an unknown
    /// action (nothing is queued). The one way a menu row, a shortcut, a canvas key and a button
    /// of a card start an action.
    bool triggerAction(const QString& actionId, const QString& choiceId = QString());

    // ------------------------------------------------------------- commands of the application routed to the tool (zadani_gui.md 4.20, way 2)
    //
    // While the canvas of the tool is shown, the window asks the tool about its commands (the
    // menu, the shortcuts of the scopes Window and Document, the status bar, MCP):
    // PDFAppDocumentTab -> PDFAppToolDocumentPage -> PDFDocumentToolShell -> the GUI of the tool,
    // whose getDocumentCommandState / handleDocumentCommand only call these two functions. The
    // shortcuts of the scope Canvas (Ctrl+A / C / X / V, Delete) never reach the tool canvas: the
    // view of the tool (P11) turns such a key press into handleApplicationCommand of the same
    // command (pe1_skeleton_notes.md 8.2). Undo, Redo, Save and Save As stay with the shell
    // (PDFAppEditContentKeyTable::getShellCommandIds: NotHandled here).

    /// The state of the command of the application for the tool: the active tool is asked first
    /// (PDFAppEditContentTool::getApplicationCommandState: typing answers Select All, Copy, ...),
    /// then the action entry, which answers the command (PDFAppEditContentRegistry::
    /// findActionByApplicationCommand): Enabled when it is visible and enabled now, Disabled
    /// otherwise (the default isEnabled of an entry is isEditingAllowed, so the command is disabled
    /// during Preview and a run; an entry like the zoom, which acts in Preview, says so in its own
    /// isEnabled). An answer of the tool is Disabled while !isEditingAllowed(). NotHandled when
    /// neither answers - the application acts as without the tool ("hidden, not disabled") - and
    /// for the commands of the shell. Cheap, no side effects (asked about every command of the
    /// application at each change of the state of the shell).
    PDFAppToolCommandState getApplicationCommandState(const QString& commandId) const;

    /// Handles the command, when getApplicationCommandState is Enabled now: it is queued like
    /// triggerAction and runs from the event loop - the state is asked again then, the active tool
    /// takes it (PDFAppEditContentTool::handleApplicationCommand) when it answers Enabled, else the
    /// entry is triggered. \returns the command was taken (queued); false = it is not the tool's
    /// now (nothing is queued).
    bool handleApplicationCommand(const QString& commandId);

Q_SIGNALS:
    /// requestPage
    void pageRequested(pdf::PDFInteger pageIndex);

    /// The shown page changed (setPage); the edited root changed with it (no containerChanged).
    /// Also emitted by reloadDocument for the same page index (the page of a new version).
    void pageChanged(pdf::PDFInteger pageIndex);

    /// reloadDocument has replaced the session (getSession is another object now; the old one is
    /// disconnected and deleted from the event loop): an object, which connected to the signals
    /// of the session (modelChanged, editorStateChanged, containerLoaded, ...), connects to the
    /// new getSession() in its slot; pageChanged follows right after it. A new signal, because no
    /// existing one can tell a holder of a connection to the old session.
    void sessionReplaced();

    /// The edited root changed without a change of the page (setIsolationPath), or the container
    /// of the edited root was loaded or failed to load (getContainer changed between nullptr and a
    /// container). A new version of the loaded container by a command is told by the session
    /// (modelChanged), not here.
    void containerChanged();

    /// requestIsolation / requestLeaveIsolation
    void isolationRequested(const pdf::PDFContentNodeReference& node);
    void leaveIsolationRequested(int levelCount);

    /// A command was refused (execute, requestExecute); the text is translated
    void commandRefused(const QString& text);

    /// isEditingAllowed may have changed (the preview, a run, the container, the page)
    void editingAllowedChanged();

    /// commitPendingEdits / discardPendingEdits (the cards connect by PDFDeferredDeletion::connect)
    void pendingEditsCommitRequested();
    void pendingEditsDiscardRequested();

    /// setPreviewActive
    void previewActiveChanged(bool isActive);

    /// The set of the requested cards or the node of the card Locked object changed
    void cardsChanged();

    /// showNotice / hideNotice
    void noticeRequested(const pdf::PDFAppEditContentNoticeRequest& request);
    void noticeHideRequested(const QString& noticeId);

    /// requestContextMenu: the menu of the registry and the global position
    void contextMenuRequested(const QString& menuId, const QPoint& globalPosition);

protected:
    /// The hook of PDFDeferredDeletion::retire: the queued commands and actions are dropped, an
    /// open macro is closed, the builds of the session are left to its destructor. Emits nothing.
    virtual void retire() override;

private:
    /// A queued action (triggerAction) or a queued command of the application
    /// (handleApplicationCommand: \p actionId is the id of the command then)
    struct QueuedAction
    {
        QString actionId;
        QString choiceId;
        bool isApplicationCommand = false;
    };

    void runApplicationCommand(const QString& commandId);

    void onQueuedCommands();
    void onQueuedActions();
    void onContainerLoaded(const pdf::PDFContentContainerKey& key);
    void onContainerFailed(const pdf::PDFContentContainerKey& key, const QString& errorText);

    /// The connections of the context to the session (the constructor, reloadDocument)
    void connectSession();

    const PDFAppEditContentRegistry* m_registry = nullptr;
    PDFDocumentPointer m_document;                              ///< The document of the session
    PDFCMSPointer m_cms;                                        ///< The color management of the session (reloadDocument)
    PDFContentEditSession* m_session = nullptr;                 ///< A child
    PDFAppEditContentOptions* m_options = nullptr;              ///< A child
    PDFAppEditContentSelection* m_selection = nullptr;          ///< A child
    PDFAppEditContentToolController* m_toolController = nullptr;    ///< A child
    PDFAppEditContentPreview* m_preview = nullptr;              ///< A child
    std::map<QString, QPointer<QObject>> m_tabStates;           ///< Children (the tab entries created them)
    QPointer<PDFDocumentToolGUI> m_gui;
    QPointer<PDFAppEditContentCanvas> m_canvas;
    PDFInteger m_pageIndex = -1;
    std::vector<PDFAppEditContentIsolationLevel> m_isolationPath;
    std::vector<PDFContentCommandPointer> m_queuedCommands;
    std::vector<QueuedAction> m_queuedActions;
    QString m_lastRefusalText;
    QSet<QString> m_requestedCards;
    QSet<QString> m_expandedSections;
    PDFContentNodeReference m_lockedClickNode;
    int m_macroDepth = 0;
    bool m_isPreviewActive = false;
    bool m_isRunning = false;
    bool m_isCommandsQueued = false;    ///< onQueuedCommands is posted
    bool m_isActionsQueued = false;     ///< onQueuedActions is posted
};

/// The geometry of the nodes as the written content draws them now (pe1_skeleton_notes.md 11,
/// R3-2): the one policy, by which the frame of the selection and the hit test of the canvas
/// (P11), the card Position and size and the alignment of Arrange (P13) get the box of a node, so
/// that they never disagree. Real code of the skeleton; a query of the core over the effective
/// state of a node (pe1_skeleton_notes.md 8.4 item 2) replaces the body of getNodeBoundingBox
/// later, the declarations stay.
///
/// The box of the tree builder (PDFContentNode::getBoundingBox) is the box of the node in the
/// default space of its container as it was read: a command, which moves the node (Transform),
/// changes only its override, never that box. So a leaf is mapped by its effective override
/// (PDFContentContainer::getEffectiveOverride: its own override and the overrides of its
/// ancestors, composed by the core): a Path exactly (its path under the matrix of the override and
/// of its state), every other leaf (an image, a shading, a form use = its /BBox under its /Matrix,
/// a text object as a whole, an opaque node with a known box) by mapping its box. A Root, a Group
/// and a MarkedContent are the union of their children. A node without a box (a state run, a
/// marked point, an opaque node the builder could not measure, a path without points) adds
/// nothing. Boxes of the stroke width and of the glyph outlines beyond their advance are not
/// added (the box of the builder neither).
///
/// Bounds (hostile input): the walk over the children of a group is limited by
/// PDFContentContainer::MAXIMUM_DEPTH + 2 levels (deeper content adds the box of the builder of
/// the node at the limit) and visits every node once (a tree); each leaf asks the core for its
/// effective override, which costs the depth of the leaf. The walk over the isolation path visits
/// each level once.
///
/// Threads: any thread for getNodeBoundingBox (the container is immutable); the others read the
/// session and the context, GUI thread.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentGeometryHelper
{
public:
    PDFAppEditContentGeometryHelper() = delete;

    /// The box of the node in the default space of its container, as the content draws it now (see
    /// the class). \returns an empty (null) rectangle for an unknown node and for a node without a
    /// box
    static QRectF getNodeBoundingBox(const PDFContentContainer& container, PDFContentNodeId nodeId);

    /// The matrix from the default space of the container of \p key to the default space of the
    /// page, through the levels of the isolation path from the innermost one (zadani_gui.md 4.16):
    /// a level of a form maps by the /Matrix of the form (PDFContentContainer::getMatrix), then by
    /// the matrix, with which the content draws its use (the effective override and the state of
    /// the entry node); a level of a pattern cell maps by the /Matrix of the pattern only (pattern
    /// space -> the default space of the content, which uses it; ISO 32000-2, 8.7.3.1); a group of
    /// the same container changes nothing. Identity for the page itself. \returns no value, when
    /// the container is not on the path, or a container on the way is not loaded or has no entry
    /// node any more
    static std::optional<QTransform> getContainerToPageMatrix(const PDFContentEditSession& session,
                                                              const PDFContentContainerKey& key,
                                                              const std::vector<PDFAppEditContentIsolationLevel>& isolationPath);

    /// The box of the node in the default space of the page shown by the context (through its
    /// isolation path); empty (null) for no context, a container not loaded, an unknown node, a
    /// node without a box and a container off the path. PDFAppEditContentSelection::getBoundingBox
    /// is the union of these boxes.
    static QRectF getPageBoundingBox(const PDFAppEditContentContext* context, const PDFContentNodeReference& node);

private:
    /// The box of a node of the kind, which has no children of its own to measure (see the class)
    static QRectF getLeafBoundingBox(const PDFContentContainer& container, const PDFContentNode& node);
};

}   // namespace pdf

Q_DECLARE_METATYPE(pdf::PDFAppEditContentNoticeRequest)

#endif // PDFAPPEDITCONTENTCONTEXT_H
