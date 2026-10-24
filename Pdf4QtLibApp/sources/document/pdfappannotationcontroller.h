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

#ifndef PDFAPPANNOTATIONCONTROLLER_H
#define PDFAPPANNOTATIONCONTROLLER_H

#include "pdfappglobal.h"
#include "pdfappdocument.h"

#include "pdfdocument.h"
#include "pdfwidgetannotation.h"
#include "pdfwidgettool.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <vector>

namespace pdf
{

class PDFAppDocumentView;
class PDFDrawWidgetProxy;
class PDFToolManager;
class PDFWidgetAnnotationManager;
class PDFWidgetFormManager;
class PDFWidgetTool;
struct PDFViewColors;

/// Category of an engine change, which the controller commits (the flag of the
/// change and the default description of its undo step)
enum class PDFAppAnnotationChangeKind
{
    Tool,       ///< A creation tool has created an annotation (flag Annotation)
    Annotation, ///< The annotation manager has changed annotations (flag Annotation)
    Form        ///< The form manager has changed a form field (flag FormField)
};

/// Controller of the annotations of one document view (step 4 plan 3.1, wave 0).
/// It is the only place, where the engine of the page (PDFToolManager,
/// PDFWidgetAnnotationManager, PDFWidgetFormManager of the view) meets the model
/// of the document:
///
/// - **Commit.** Every documentModified of the three managers is committed by
///   PDFAppDocument::commit as one undo step (flags Annotation / FormField), with
///   the pending description (setPendingDescription, one-shot) or a generic one,
///   and with the key of an open merge session (beginMergeSession - a live change
///   of the color or the width of the annotation bar is one step, O20). A refused
///   commit (locked, blocked by an operation, stale version) shows a status
///   message and changes nothing; it never crashes.
/// - **Delegated user interface.** The controller switches the engine to the
///   delegated mode (E3 / E4 / E7): the engine shows no dialog and no menu, it asks
///   for them. The requests are re-emitted as the signals of the controller
///   **from the event loop** (queued), never inside of a mouse handler of the
///   engine - a slot may open a modal dialog or delete the tab.
/// - **Lock and permissions (O8, O20).** A locked document, a document without
///   the permission to modify annotations or to fill forms: the annotation manager
///   is read only, the form is ReadOnlyForm, a creation tool ends.
/// - **Panel API.** The panels (Notes, Annotate, Insert) and the bars select
///   annotations, ask for the properties / comment / reply / geometry and follow
///   the selection (selectionChanged) and the active tool (activeToolChanged).
///
/// The controller is a child of its view, created after the managers and deleted
/// before them (the view deletes it in its destructor). The GUI thread only.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppAnnotationController : public QObject
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    /// Timeout of the status messages of a refused change (milliseconds)
    static constexpr int STATUS_MESSAGE_TIMEOUT = 5000;

    /// Maximal length of a description of an undo step
    static constexpr qsizetype MAXIMUM_DESCRIPTION_LENGTH = 256;

    explicit PDFAppAnnotationController(PDFAppDocumentView* view);
    virtual ~PDFAppAnnotationController() override;

    /// Watches the canvas while a merge session is open: a press of the mouse or
    /// a key on the canvas ends the session (a drag or a nudge is a new step)
    virtual bool eventFilter(QObject* watched, QEvent* event) override;

    PDFAppDocumentView* getView() const;
    PDFAppDocument* getDocument() const;
    PDFToolManager* getToolManager() const;
    PDFWidgetAnnotationManager* getAnnotationManager() const;
    PDFWidgetFormManager* getFormManager() const;

    /// The view has a new document model (PDFAppDocumentView::setDocument)
    void setDocument(PDFAppDocument* document);

    // ------------------------------------------------------------------ commit

    /// Description of the next committed engine change (one-shot; cleared by the
    /// next commit and by a change of the tool). Empty = the generic description.
    void setPendingDescription(const QString& description);
    const QString& getPendingDescription() const { return m_pendingDescription; }

    /// Opens a merge session: the following changes of the annotations made by the
    /// engine are one undo step with the key (O20). An open session of another key
    /// is ended first. Only the engine changes made while the session is armed join
    /// it - beginMergeSession arms it, endEngineChange disarms it; any other commit
    /// (a reply of the comment popup, a change of the Notes panel, a command) is a
    /// new undo step and it ends the session.
    void beginMergeSession(const QString& key);

    /// Ends the merge session (a change of the selection, of the tool, a hide of
    /// the bar, a press of another control, a closed dialog)
    void endMergeSession();

    /// A change of the application made by a synchronous call of the engine (a
    /// command, a menu item, the bar): \p description names its undo step, a not
    /// empty \p mergeKey joins it to the merge session of the key (beginMergeSession).
    /// Call endEngineChange, when the call of the engine has returned.
    void beginEngineChange(const QString& description, const QString& mergeKey = QString());

    /// The call of the engine announced by beginEngineChange has returned: a
    /// description, which no commit has taken (the engine has changed nothing),
    /// is dropped - the pending description before beginEngineChange (of an active
    /// creation tool) is given back - and the merge session takes no further change
    void endEngineChange();

    /// Key of the open merge session (empty = none)
    const QString& getMergeSessionKey() const { return m_mergeKey; }
    bool isMergeSessionOpen() const { return !m_mergeKey.isEmpty(); }

    /// Key of a merge session of a live change of \p property of the annotations
    /// \p references; \p counter distinguishes two sessions of the same selection
    /// (a new press of the control)
    static QString createMergeKey(const QString& property, const std::vector<PDFObjectReference>& references, quint64 counter);

    /// Commits a change made by the application (an operation of
    /// PDFAppAnnotationOperations, a dialog): the flags of \p modified are joined
    /// with \p flags, the description is \p description (else the pending one,
    /// else the generic one of \p kind); the key of the open merge session is used.
    /// \returns false, when the document refuses the change (a status message is
    /// shown).
    bool commit(const PDFModifiedDocument& modified, PDFAppAnnotationChangeKind kind, const QString& description = QString());

    /// Generic description of an undo step of the kind ("Add annotation", "Edit
    /// annotations", "Fill form field")
    static QString getDefaultDescription(PDFAppAnnotationChangeKind kind);

    // ------------------------------------------------------------------ state

    /// Annotations can be created and changed: the document is shown, it is not
    /// locked and it allows the modification of annotations (ModifyInteractiveItems)
    bool isAnnotationEditingAllowed() const;

    /// The form can be filled: the document is shown, not locked, it allows the
    /// filling of the form (ModifyFormFields or ModifyInteractiveItems) and its
    /// form is not an XFA form (shown only, PDFAppFormController::isXFAForm)
    bool isFormFillingAllowed() const;

    /// Reason, why annotations cannot be changed, for a tooltip or a status
    /// message (O8: a locked document and a document without the permission have
    /// different texts); empty when the editing is allowed
    QString getEditingRefusalReason() const;

    /// Reads the lock and the permissions again and applies them to the engine
    /// (also called by the controller itself on every change of the document)
    void updateReadOnlyState();

    // ------------------------------------------------------------------ tools

    /// Active tool of the tool manager (nullptr = none)
    PDFWidgetTool* getActiveTool() const;

    /// Adds the tool to the tool manager (a creation tool of package A, created
    /// with the view or the controller as its parent)
    void addTool(PDFWidgetTool* tool);

    /// Activates the tool (refused, when it is an annotation tool and the editing
    /// is not allowed); nullptr ends the active tool
    bool activateTool(PDFWidgetTool* tool);

    /// Ends the active tool: the text selection tool becomes active (the default
    /// tool of the document view)
    void endActiveTool();

    /// The tool creates or deletes annotations (not one of the predefined tools of
    /// the tool manager - selection, magnifier, screenshot, ...)
    bool isCreationTool(const PDFWidgetTool* tool) const;

    /// Colors of the annotation editing of the theme (E6): the selection frame,
    /// the handles, the points, the rubber band, the snap marker, the lock badge
    static void applyThemeColors(PDFViewColors& colors);

    // ------------------------------------------------------------------ selection

    std::vector<PDFObjectReference> getSelectedAnnotations() const;
    bool hasAnnotationSelection() const;
    void selectAnnotations(const std::vector<PDFObjectReference>& references);
    void clearAnnotationSelection();

    /// Clipboard and deletion of the selected annotations (commands edit.* of
    /// package B and the routing of edit.copy / edit.selectAll / edit.deselect)
    void copySelection();
    void cutSelection();
    bool canPaste() const;
    void paste(bool keepPosition);
    void deleteSelection();

    /// Selects all selectable annotations of the displayed pages
    void selectAll();

    // ------------------------------------------------------------------ requests

    /// Requests of the panels and the commands; the signals come from the event loop
    void requestProperties(PDFObjectReference annotation);
    void requestComment(PDFObjectReference annotation, bool compose);
    void requestReply(PDFObjectReference annotation);
    void requestGeometry(PDFObjectReference annotation);
    void requestCopyToPages();
    void requestMenu(const PDFAnnotationMenuRequest& request);

Q_SIGNALS:
    /// A tool asks for the user interface of a new annotation (E3)
    void creationRequested(const pdf::PDFAnnotationCreationRequest& request);

    /// The context menu of the annotations (E4)
    void menuRequested(const pdf::PDFAnnotationMenuRequest& request);

    /// The popup of a markup annotation (E4, double click on the page)
    void popupRequested(pdf::PDFObjectReference annotation, pdf::PDFObjectReference page, QPoint globalPosition);

    /// The properties dialog of the annotation
    void propertiesRequested(pdf::PDFObjectReference annotation);

    /// The geometry dialog of the annotation
    void geometryRequested(pdf::PDFObjectReference annotation);

    /// The Copy to Pages dialog of the selection
    void copyToPagesRequested();

    /// The comment popup of the annotation (\p compose = a new comment)
    void commentRequested(pdf::PDFObjectReference annotation, bool compose);

    /// The reply field of the comment popup of the annotation
    void replyRequested(pdf::PDFObjectReference annotation);

    /// The active tool has changed (nullptr = none)
    void activeToolChanged(pdf::PDFWidgetTool* tool);

    /// The selection of the annotations has changed
    void selectionChanged();

    /// The editing state has changed (isAnnotationEditingAllowed, isFormFillingAllowed)
    void editingAllowedChanged();

    /// An engine change has been committed (one undo step or a merged change)
    void changeCommitted(pdf::PDFAppAnnotationChangeKind kind);

    /// An engine change has been refused by the document (\p reason is shown in
    /// the status bar)
    void changeRefused(const QString& reason);

private:
    void connectEngine();
    void onToolDocumentModified(pdf::PDFModifiedDocument document);
    void onAnnotationDocumentModified(pdf::PDFModifiedDocument document);
    void onFormDocumentModified(pdf::PDFModifiedDocument document);
    void onCreationRequested(const pdf::PDFAnnotationCreationRequest& request);
    void onMenuRequested(const pdf::PDFAnnotationMenuRequest& request);
    void onPopupRequested(pdf::PDFObjectReference annotation, pdf::PDFObjectReference page, QPoint globalPosition);
    void onPropertiesRequested(pdf::PDFObjectReference annotation);
    void onGeometryRequested(pdf::PDFObjectReference annotation);
    void onCopyToPagesRequested();
    void onCreationRequestedQueued(const pdf::PDFAnnotationCreationRequest& request);
    void onMenuRequestedQueued(const pdf::PDFAnnotationMenuRequest& request);
    void onPopupRequestedQueued(pdf::PDFObjectReference annotation, pdf::PDFObjectReference page, QPoint globalPosition);
    void onPropertiesRequestedQueued(pdf::PDFObjectReference annotation);
    void onGeometryRequestedQueued(pdf::PDFObjectReference annotation);
    void onCopyToPagesRequestedQueued();
    void onCommentRequestedQueued(pdf::PDFObjectReference annotation, bool compose);
    void onReplyRequestedQueued(pdf::PDFObjectReference annotation);
    void onEngineSelectionChanged();
    void onSelectionChangedQueued();
    void onActiveToolChanged(pdf::PDFWidgetTool* tool);
    void onDocumentChanged(const pdf::PDFModifiedDocument& modified);
    void onDocumentStateChanged();
    void onDatabaseChanged(const QString& topic, const QString& key);
    void onEverythingChanged();
    void reloadToolStyles();
    void restoreSelection();
    bool commitEngineChange(const PDFModifiedDocument& modified, PDFAppAnnotationChangeKind kind, const QString& description, bool isFromEngine);
    bool isEngineDocumentCurrent() const;
    QWidget* getCanvasWidget() const;
    void showStatusMessage(const QString& text);

    QPointer<PDFAppDocumentView> m_view;
    QPointer<PDFAppDocument> m_document;
    QPointer<PDFToolManager> m_toolManager;
    QPointer<PDFWidgetAnnotationManager> m_annotationManager;
    QPointer<PDFWidgetFormManager> m_formManager;
    QString m_pendingDescription;
    QString m_mergeKey;
    QString m_savedDescription;                        ///< Pending description before beginEngineChange
    quint64 m_commitCounter = 0;                       ///< Number of the commits of the controller (taken or refused)
    quint64 m_engineChangeCommitCounter = 0;           ///< m_commitCounter at beginEngineChange
    bool m_isEngineChangeOpen = false;                 ///< beginEngineChange without endEngineChange yet
    bool m_isMergeArmed = false;                       ///< The engine changes join the merge session (beginMergeSession ... endEngineChange)
    QPointer<QWidget> m_mergeSessionCanvas;           ///< Canvas watched while a merge session is open (a press on it ends the session)
    quint64 m_knownVersion = 0;                        ///< Version of the last documentChanged handled by the controller
    std::vector<PDFObjectReference> m_lastSelection;   ///< Selection before the last change of the document (restored after undo / redo)
    bool m_isCommitting = false;                       ///< A commit of the controller runs (its documentChanged is not an undo / redo)
    bool m_isSelectionChangePending = false;           ///< selectionChanged is queued (the engine has changed its selection)
    bool m_isAnnotationEditingAllowed = false;
    bool m_isFormFillingAllowed = false;
    bool m_isStateKnown = false;
};

}   // namespace pdf

#endif // PDFAPPANNOTATIONCONTROLLER_H
