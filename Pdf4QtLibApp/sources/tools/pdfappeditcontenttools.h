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

#ifndef PDFAPPEDITCONTENTTOOLS_H
#define PDFAPPEDITCONTENTTOOLS_H

#include "pdfappglobal.h"
#include "pdfcontentcommands.h"
#include "pdfdeferreddeletion.h"

#include <QCoreApplication>
#include <QCursor>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVariant>

#include <map>
#include <memory>
#include <vector>

class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace pdf
{

class PDFAppEditContentContext;
class PDFAppEditContentRegistry;
class PDFAppEditContentScene;
class PDFAppEditContentSelection;
class PDFAppEditContentToolController;
class PDFAppEditContentView;
class PDFAppEditContentToolEntry;
enum class PDFAppToolCommandState;  // pdfdocumenttoolgui.h

// Edit Content, stage E1, package P11 (canvas). Plan: zadani_gui.md 4.16 (tools and their state
// machines), 4.20 (keys of the canvas), KE.16 (order of Esc). This file: the base of the tools,
// the controller and the tool Select; every other tool is a file of its package
// (pdfappeditcontenttools_<area>.*) with its entry of the registry.

/// What a tool is started with (PDFAppEditContentToolController::activateTool)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentToolRequest
{
public:
    PDFContentNodeReference target;     ///< The node the tool edits (typing, nodes, crop, shading); not valid for a tool of the toolbar
    QVariant argument;                  ///< A value of the tool (the eyedropper: "fill" / "stroke", the row its button is in)
};

/// What a tool gets to paint its preview with
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentToolPaintContext
{
public:
    qreal pixelSize = 1.0;      ///< The size of a pixel of the view in the units of the scene (a line of 1 px on the screen has this width)
    bool isFocused = false;     ///< The canvas has the keyboard focus
};

/// A tool of the canvas: a state machine over the events of the view (zadani_gui.md 4.16).
///
/// Rules of every tool:
/// - It never changes the model directly. At the end of a gesture it executes one command, or
///   the commands of one macro (PDFAppEditContentContext::execute, beginMacro / endMacro).
/// - It holds identifiers of nodes (PDFContentNodeReference), never pointers to nodes or to
///   scene items; it finds what it needs in the session and in the scene at the moment of use
///   and checks the scene and the view for nullptr (the parts may be gone).
/// - It gets its events from the controller (the view forwards them), never from an item. The
///   points are in the default space of the page (= the scene), not rotated.
/// - It creates no scene items: its preview (a rubber band, a ghost of a shape, the pen path,
///   handles of nodes) is painted by paint, which the one permanent item of the layer
///   ToolPreview calls; update() asks for a repaint.
/// - A call of the context or of the session may close the tool (their slots run inside): after
///   it the tool touches itself only when its controller is not retired (isAlive).
///
/// Threads and ownership: a plain class (not a QObject), GUI thread. Owned by the controller,
/// which creates every tool of the registry in PDFAppEditContentToolController::createTools - the
/// last step of the constructor of the context - and deletes it in its destructor; a tool is never
/// deleted while one of its functions runs. In the constructor of a tool every object of the
/// context, which is documented "never nullptr" (the session, the options, the selection, the
/// controller, the preview, the states of the tabs), exists already; the GUI and the canvas do not
/// (they come later and go earlier - check them). The context and the controller are valid for
/// the whole life of the tool, also in its destructor: the context deletes its children in the
/// reverse order of their creation, so the controller with the tools goes before the selection,
/// the options and the session (pdfappeditcontentcontext.h).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentTool
{
public:
    /// \param context The context; its controller is the controller of the tool from now on
    explicit PDFAppEditContentTool(PDFAppEditContentContext* context);
    virtual ~PDFAppEditContentTool();

    PDFAppEditContentTool(const PDFAppEditContentTool&) = delete;
    PDFAppEditContentTool& operator=(const PDFAppEditContentTool&) = delete;

    PDFAppEditContentContext* getContext() const { return m_context; }

    /// The controller of the tool (the controller of the context; valid in the constructor too)
    PDFAppEditContentToolController* getController() const { return m_controller; }

    /// The tool becomes the active one / stops being it (another tool, Preview, the end of the
    /// tool). deactivate ends a gesture in progress without a command and forgets the state; both
    /// may be called without a canvas.
    virtual void activate(const PDFAppEditContentToolRequest& request);
    virtual void deactivate();

    /// Events of the view; \p scenePoint is the point of the default space of the page. The
    /// event is accepted by the view when the tool is active; the tool does not call accept or
    /// ignore.
    virtual void mousePress(QMouseEvent* event, const QPointF& scenePoint);
    virtual void mouseMove(QMouseEvent* event, const QPointF& scenePoint);
    virtual void mouseRelease(QMouseEvent* event, const QPointF& scenePoint);
    virtual void mouseDoubleClick(QMouseEvent* event, const QPointF& scenePoint);

    /// A key of the canvas, which is no key of a tool entry or of an action (the arrows, Enter,
    /// Delete, Backspace, ...). \returns true = the tool has taken the key. The default takes
    /// nothing.
    virtual bool keyPress(QKeyEvent* event);

    /// The modifiers changed during a gesture (Shift, Ctrl, Alt pressed or released without a
    /// move of the pointer): the tool evaluates its gesture again. The default does nothing.
    virtual void modifiersChanged(Qt::KeyboardModifiers modifiers);

    /// Esc (KE.16: after a menu, before the selection): the tool cancels its drag or leaves its
    /// mode. \returns true = Esc was used; false = the tool has nothing to cancel (the controller
    /// then returns to Select, or says false itself). The default returns false.
    virtual bool cancel();

    /// A gesture with a pressed button is in progress (the order of Esc; the view keeps the
    /// mouse). The default returns false.
    virtual bool isDragActive() const;

    /// Translated text of the status bar for the state of the tool ("Drag to move - Shift keeps
    /// the direction"); empty = the description of the selection is shown instead. Default: empty.
    virtual QString getStatusText() const;

    /// Translated label of the mode at the top left of the canvas for the modes the toolbar does
    /// not show ("Typing in the text", "Editing nodes"; zadani_gui.md 4.15), also the name
    /// PDFDocumentToolGUI::getEscapeModeName gives. Default: empty.
    virtual QString getModeLabel() const;

    /// The cursor over the canvas for the state of the tool at the point. Default: the arrow.
    virtual QCursor getCursor(const QPointF& scenePoint) const;

    /// Paints the preview of the tool in the coordinates of the scene (see the class). The
    /// painter state is saved and restored by the caller. Default: nothing.
    virtual void paint(QPainter* painter, const PDFAppEditContentToolPaintContext& paintContext) const;

    /// A command of the application routed to the tool (zadani_gui.md 4.20, way 2; the ids of
    /// PDFAppCommands): the active tool is asked before the action entries
    /// (PDFAppEditContentContext::getApplicationCommandState) - typing (E2, P22) answers Select
    /// All, Copy, Cut, Paste and Delete for its characters. NotHandled = the action entry of the
    /// command answers (or nobody). Cheap and without side effects (read at every refresh of the
    /// actions of the window). The context makes the answer Disabled while the editing is not
    /// allowed. Default (real code of the skeleton): NotHandled.
    virtual PDFAppToolCommandState getApplicationCommandState(const QString& commandId) const;

    /// Runs the command, for which getApplicationCommandState is Enabled. Called from the event
    /// loop by the context (PDFAppEditContentContext::handleApplicationCommand queues it), after
    /// the state was asked again; the rules of the other events hold (a command, isAlive).
    /// Default (real code of the skeleton): nothing.
    virtual void handleApplicationCommand(const QString& commandId);

protected:
    /// Asks the scene to repaint the preview of the tool and the controller to tell the changed
    /// status text, mode label and cursor (PDFAppEditContentToolController::stateChanged, from
    /// the event loop, once for more calls)
    void update();

    /// The tool may go on after a call, which ran foreign code: its controller is not retired
    bool isAlive() const;

private:
    friend class PDFAppEditContentToolController;

    PDFAppEditContentContext* m_context = nullptr;
    PDFAppEditContentToolController* m_controller = nullptr;
};

/// The active tool of the canvas and the switching between the tools (zadani_gui.md 4.16).
///
/// - The controller creates every tool of the registry of its context in createTools, which the
///   context calls as the last step of its constructor (so the tools find every other child of
///   the context); the active tool is Select (when it is registered), else none.
/// - activateTool commits the pending values of the cards (PDFAppEditContentContext::
///   commitPendingEdits), deactivates the old tool, activates the new one and emits
///   activeToolChanged last. A tool, which is not registered, or a request while the editing is
///   not allowed (Preview, a run) is refused.
/// - Multiple (the toggle of the toolbar): finishGesture keeps a creation tool active;
///   otherwise it returns to Select.
/// - Preview on, a run started, the page switched: the active tool is deactivated and Select
///   becomes the active one (the controller follows the signals of the context).
/// - The view forwards its mouse and key events here. A key of the canvas goes: the key of a
///   tool entry (activateTool) -> the canvas key of an action entry (PDFAppEditContentContext::
///   triggerAction) -> keyPress of the active tool.
///
/// Threads and ownership: a child of the context (it lives as long as the tool), GUI thread. It
/// owns the tools. It holds no pointer to the view or to the scene (it asks the context). Its
/// hook retire() deactivates the active tool (a gesture ends without a command, the mouse grab
/// and the cursor of the view are released by the view itself) and emits nothing.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentToolController : public QObject, public PDFRetirable
{
    Q_OBJECT

private:
    using BaseClass = QObject;

public:
    explicit PDFAppEditContentToolController(PDFAppEditContentContext* context, QObject* parent);
    virtual ~PDFAppEditContentToolController() override;

    /// Creates every tool of the registry of the context (PDFAppEditContentToolEntry::createTool);
    /// a second call does nothing. Called by the context as the last step of its constructor
    /// (real code of the skeleton). A tool is created in the order of PDFAppEditContentRegistry::
    /// getTools and finds the whole context in its constructor.
    void createTools();

    /// The identifier of the active tool (PDFAppEditContentToolIds); empty = none
    const QString& getActiveToolId() const { return m_activeToolId; }

    /// The active tool and its entry, nullptr = none
    PDFAppEditContentTool* getActiveTool() const;
    const PDFAppEditContentToolEntry* getActiveToolEntry() const;

    /// The node the active tool edits (PDFAppEditContentToolRequest::target); not valid = none
    const PDFContentNodeReference& getActiveToolTarget() const { return m_activeRequest.target; }

    /// The tool of the identifier, nullptr = not registered (tests)
    PDFAppEditContentTool* getTool(const QString& toolId) const;

    /// Makes the tool the active one (see the class). \returns false, when it is refused. The
    /// slots of the commit and of activeToolChanged may close the tool of the application: the
    /// caller guards itself.
    bool activateTool(const QString& toolId, const PDFAppEditContentToolRequest& request = PDFAppEditContentToolRequest());

    /// Starts the tool, which edits the node (a double click, Enter; PDFAppEditContentRegistry::
    /// findEditorTool). \returns false, when no tool edits it.
    bool activateEditorTool(const PDFContentNodeReference& node);

    /// Returns to the tool Select
    void activateSelectTool();

    /// The toggle Multiple of the toolbar (a creation tool stays active after its gesture)
    bool isMultiple() const { return m_isMultiple; }
    void setMultiple(bool isMultiple);

    /// A creation tool has finished its gesture (it has executed its command): Select becomes the
    /// active tool, unless Multiple is on
    void finishGesture();

    /// Esc of the tool (PDFDocumentToolGUI::handleEscape of P14; KE.16): the drag of the active
    /// tool, then its mode (a tool other than Select returns to Select), then - by the caller -
    /// the group and the selection. \returns true = Esc was used here.
    bool cancel();

    /// Events of the view (PDFAppEditContentView forwards them; nothing while the editing is not
    /// allowed). keyPress returns true, when the key was used.
    void mousePress(QMouseEvent* event, const QPointF& scenePoint);
    void mouseMove(QMouseEvent* event, const QPointF& scenePoint);
    void mouseRelease(QMouseEvent* event, const QPointF& scenePoint);
    void mouseDoubleClick(QMouseEvent* event, const QPointF& scenePoint);
    bool keyPress(QKeyEvent* event);
    void modifiersChanged(Qt::KeyboardModifiers modifiers);

    /// Of the active tool (empty / the arrow without one)
    QString getStatusText() const;
    QString getModeLabel() const;
    QCursor getCursor(const QPointF& scenePoint) const;
    bool isDragActive() const;

    /// PDFAppEditContentTool::update of a tool: stateChanged from the event loop (once)
    void requestStateUpdate();

Q_SIGNALS:
    /// The active tool or its target changed
    void activeToolChanged(const QString& toolId);

    /// The status text, the mode label, the cursor or the preview of the active tool may have changed
    void stateChanged();

    /// setMultiple
    void multipleChanged(bool isMultiple);

protected:
    virtual void retire() override;

private:
    void onQueuedStateUpdate();
    void onEditingAllowedChanged();
    void onPageChanged();

    /// Select becomes the active tool without a commit and without a check of the editing (Preview, a run, a new
    /// page: the controller follows the context); emits activeToolChanged last, when the tool changed
    void resetToSelect();

    QPointer<PDFAppEditContentContext> m_context;
    std::map<QString, std::unique_ptr<PDFAppEditContentTool>> m_tools;
    QString m_activeToolId;
    PDFAppEditContentToolRequest m_activeRequest;
    bool m_isMultiple = false;
    bool m_isStateUpdateQueued = false;
    bool m_isToolsCreated = false;      ///< createTools has run
};

/// The tool Select (V; zadani_gui.md 4.16, the first row of the table): a click selects, Shift /
/// Ctrl + click adds, a drag over nothing selects by a rectangle, a drag of an object moves it
/// (Ctrl = a copy, Shift = one direction, Alt = no snapping), the handles resize and rotate, the
/// arrows move by 1 pt (Shift: 10 pt), Delete calls PDFAppEditContentContext::triggerAction(
/// PDFAppEditContentActionIds::DELETE_SELECTION) (the action of P12 owns what is deleted; the view
/// turns the shortcut of the application command edit.delete into the same action), Alt + click
/// walks through the objects below each other, a double click and Enter start the editor tool of
/// the object, a click on a locked object shows the card Locked object. Commands: Transform, Copy.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentSelectTool : public PDFAppEditContentTool
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentSelectTool)

public:
    explicit PDFAppEditContentSelectTool(PDFAppEditContentContext* context);
    virtual ~PDFAppEditContentSelectTool() override;

    virtual void deactivate() override;
    virtual void mousePress(QMouseEvent* event, const QPointF& scenePoint) override;
    virtual void mouseMove(QMouseEvent* event, const QPointF& scenePoint) override;
    virtual void mouseRelease(QMouseEvent* event, const QPointF& scenePoint) override;
    virtual void mouseDoubleClick(QMouseEvent* event, const QPointF& scenePoint) override;
    virtual bool keyPress(QKeyEvent* event) override;
    virtual void modifiersChanged(Qt::KeyboardModifiers modifiers) override;
    virtual bool cancel() override;
    virtual bool isDragActive() const override;
    virtual QString getStatusText() const override;
    virtual QCursor getCursor(const QPointF& scenePoint) const override;
    virtual void paint(QPainter* painter, const PDFAppEditContentToolPaintContext& paintContext) const override;

    // ---- P11 (tests of the canvas) ----

    /// The gesture in progress
    enum class Gesture
    {
        None,
        Pending,        ///< The button is down on an object, the pointer has not moved enough yet
        Move,
        RubberBand,
        Resize,
        Rotate
    };

    Gesture getGesture() const { return m_gesture; }

    /// The matrix of the gesture in progress (the default space of the page)
    const QTransform& getGestureTransform() const { return m_transform; }

    /// The matrix of a resize of \p box by the handle dragged to \p point around the opposite handle
    /// (\p isAspectKept: one scale for both axes); the identity for a box without an extent
    static QTransform getResizeTransform(const QRectF& box, int handle, const QPointF& point, bool isAspectKept);

    /// The matrix of a rotation around \p center by the angle from \p start to \p point (degrees,
    /// counterclockwise in the page space; \p isSnapped: by 15 degrees)
    static QTransform getRotationTransform(const QPointF& center, const QPointF& start, const QPointF& point, bool isSnapped);

private:
    /// The matrix of the gesture for the current point and modifiers
    void updateGesture();

    /// Executes the gesture as one command (Transform, or Copy for a move with Ctrl)
    void finishGesture();

    /// Ends the gesture without a command
    void abortGesture();

    /// A selected node can be moved (not locked, no opaque content)
    std::vector<PDFContentNodeReference> getMovableSelection() const;

    PDFAppEditContentScene* getScene() const;
    PDFAppEditContentView* getView() const;
    PDFAppEditContentSelection* getSelection() const;

    /// The size of a pixel of the view in the scene
    qreal getPixelSize() const;

    Gesture m_gesture = Gesture::None;
    QPointF m_pressPoint;
    QPointF m_currentPoint;
    Qt::KeyboardModifiers m_modifiers = Qt::NoModifier;
    int m_handle = 0;                               ///< PDFAppEditContentSelectionItem::Handle of a resize
    QRectF m_startBox;                              ///< The box of the handles at the press
    std::vector<PDFContentNodeReference> m_nodes;   ///< The nodes of the gesture
    PDFContentNodeReference m_pressNode;            ///< The node under the press
    bool m_isToggleDeferred = false;                ///< Ctrl + click on a selected node: removed at the release, unless it is dragged
    bool m_isAspectDefault = false;                 ///< A resize keeps the proportions without Shift (images only)
    QTransform m_transform;
    std::vector<PDFContentNodeReference> m_cycleNodes; ///< Alt + click: the nodes under the point of the last cycle
    size_t m_cycleIndex = 0;
};

/// The entries of this file for the registry (the tool Select)
class PDF4QTLIBAPPSHARED_EXPORT PDFAppEditContentTools
{
public:
    PDFAppEditContentTools() = delete;

    static void registerParts(PDFAppEditContentRegistry* registry);
};

}   // namespace pdf

#endif // PDFAPPEDITCONTENTTOOLS_H
