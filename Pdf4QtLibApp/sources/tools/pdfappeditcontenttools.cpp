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

#include "pdfappeditcontenttools.h"

#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentscene.h"
#include "pdfappeditcontentselection.h"
#include "pdfappeditcontentview.h"
#include "pdfdocumenttoolgui.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QtGlobal>
#include <QtMath>

#include <cmath>
#include <set>

namespace pdf
{

// Stage E1, package P11: the base of the tools, the controller and the tool Select (zadani_gui.md 4.16). A tool
// changes the model only by one command (or one macro) at the end of a gesture; every call of the context, of the
// selection or of the session may close the tool (their slots run inside), so a tool checks isAlive after it.

// ---------------------------------------------------------------- PDFAppEditContentTool

PDFAppEditContentTool::PDFAppEditContentTool(PDFAppEditContentContext* context) :
    m_context(context),
    m_controller(context ? context->getToolController() : nullptr)
{

}

PDFAppToolCommandState PDFAppEditContentTool::getApplicationCommandState(const QString& commandId) const
{
    // Real code of the skeleton: a tool takes no command of the application unless it says so
    Q_UNUSED(commandId);
    return PDFAppToolCommandState::NotHandled;
}

void PDFAppEditContentTool::handleApplicationCommand(const QString& commandId)
{
    Q_UNUSED(commandId);
}

PDFAppEditContentTool::~PDFAppEditContentTool() = default;

void PDFAppEditContentTool::activate(const PDFAppEditContentToolRequest& request)
{
    Q_UNUSED(request);
}

void PDFAppEditContentTool::deactivate()
{
}

void PDFAppEditContentTool::mousePress(QMouseEvent* event, const QPointF& scenePoint)
{
    Q_UNUSED(event);
    Q_UNUSED(scenePoint);
}

void PDFAppEditContentTool::mouseMove(QMouseEvent* event, const QPointF& scenePoint)
{
    Q_UNUSED(event);
    Q_UNUSED(scenePoint);
}

void PDFAppEditContentTool::mouseRelease(QMouseEvent* event, const QPointF& scenePoint)
{
    Q_UNUSED(event);
    Q_UNUSED(scenePoint);
}

void PDFAppEditContentTool::mouseDoubleClick(QMouseEvent* event, const QPointF& scenePoint)
{
    Q_UNUSED(event);
    Q_UNUSED(scenePoint);
}

bool PDFAppEditContentTool::keyPress(QKeyEvent* event)
{
    Q_UNUSED(event);
    return false;
}

void PDFAppEditContentTool::modifiersChanged(Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);
}

bool PDFAppEditContentTool::cancel()
{
    return false;
}

bool PDFAppEditContentTool::isDragActive() const
{
    return false;
}

QString PDFAppEditContentTool::getStatusText() const
{
    return QString();
}

QString PDFAppEditContentTool::getModeLabel() const
{
    return QString();
}

QCursor PDFAppEditContentTool::getCursor(const QPointF& scenePoint) const
{
    Q_UNUSED(scenePoint);
    return QCursor(Qt::ArrowCursor);
}

void PDFAppEditContentTool::paint(QPainter* painter, const PDFAppEditContentToolPaintContext& paintContext) const
{
    Q_UNUSED(painter);
    Q_UNUSED(paintContext);
}

QRectF PDFAppEditContentTool::getPreviewRectangle(qreal pixelSize) const
{
    Q_UNUSED(pixelSize);
    return QRectF();
}

void PDFAppEditContentTool::modelChanged(const PDFContentChange& change)
{
    Q_UNUSED(change);
}

void PDFAppEditContentTool::update()
{
    if (!isAlive())
    {
        return;
    }

    if (PDFAppEditContentScene* scene = m_context->getScene())
    {
        scene->updateToolPreview();
    }
    m_controller->requestStateUpdate();
}

bool PDFAppEditContentTool::isAlive() const
{
    // The controller owns the tool: it exists while the tool does; retired = the tool of the application is closing
    return m_controller && m_context && !PDFDeferredDeletion::isRetired(m_controller);
}

// ---------------------------------------------------------------- PDFAppEditContentToolController

PDFAppEditContentToolController::PDFAppEditContentToolController(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    PDFRetirable(this),
    m_context(context)
{
    // The tools are created by createTools, when the context is whole (the last step of its constructor). The
    // controller follows the context: Preview, a run, a new page return to Select.
    if (context)
    {
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::editingAllowedChanged, this, &PDFAppEditContentToolController::onEditingAllowedChanged);
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::pageChanged, this, &PDFAppEditContentToolController::onPageChanged);
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::sessionReplaced, this, &PDFAppEditContentToolController::onSessionReplaced);
        connectSession();
    }
}

void PDFAppEditContentToolController::connectSession()
{
    // The session is created before the controller (the context creates it first) and replaced by reloadDocument, which
    // cuts every connection of the old one (R6-21: a gesture ends when its nodes change under it)
    if (PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr)
    {
        PDFDeferredDeletion::connect(session, &PDFContentEditSession::modelChanged, this, &PDFAppEditContentToolController::onModelChanged, Qt::UniqueConnection);
    }
}

void PDFAppEditContentToolController::onSessionReplaced()
{
    connectSession();
}

void PDFAppEditContentToolController::onModelChanged(const PDFContentChange& change)
{
    // Only the active tool has a gesture; a retired controller tells nobody
    PDFAppEditContentTool* tool = getActiveTool();
    if (tool && !PDFDeferredDeletion::isRetired(this))
    {
        tool->modelChanged(change);
    }
}

void PDFAppEditContentToolController::createTools()
{
    // Every tool of the registry is created once and lives as long as the controller. No tool is active until the
    // editing becomes possible (onEditingAllowedChanged makes Select the active one then).
    PDFAppEditContentContext* context = m_context.data();
    if (m_isToolsCreated || !context || !context->getRegistry())
    {
        return;
    }

    m_isToolsCreated = true;
    for (const PDFAppEditContentToolEntry* entry : context->getRegistry()->getTools())
    {
        std::unique_ptr<PDFAppEditContentTool> tool = entry->createTool(context);
        if (tool)
        {
            tool->m_controller = this;
            m_tools[entry->getId()] = std::move(tool);
        }
    }
}

PDFAppEditContentToolController::~PDFAppEditContentToolController() = default;

PDFAppEditContentTool* PDFAppEditContentToolController::getActiveTool() const
{
    return m_activeToolId.isEmpty() ? nullptr : getTool(m_activeToolId);
}

const PDFAppEditContentToolEntry* PDFAppEditContentToolController::getActiveToolEntry() const
{
    if (m_activeToolId.isEmpty() || !m_context || !m_context->getRegistry())
    {
        return nullptr;
    }
    return m_context->getRegistry()->findTool(m_activeToolId);
}

PDFAppEditContentTool* PDFAppEditContentToolController::getTool(const QString& toolId) const
{
    const auto it = m_tools.find(toolId);
    return it != m_tools.cend() ? it->second.get() : nullptr;
}

bool PDFAppEditContentToolController::activateTool(const QString& toolId, const PDFAppEditContentToolRequest& request)
{
    PDFAppEditContentContext* context = m_context.data();
    PDFAppEditContentTool* tool = getTool(toolId);
    if (!context || PDFDeferredDeletion::isRetired(this) || !tool || !context->getRegistry()->findTool(toolId) || !context->isEditingAllowed())
    {
        return false;
    }

    // The values typed into the cards are committed first (their slots may close the tool)
    QPointer<PDFAppEditContentToolController> guard(this);
    context->commitPendingEdits();
    if (!guard || PDFDeferredDeletion::isRetired(this) || !m_context || !m_context->isEditingAllowed())
    {
        return false;
    }

    if (PDFAppEditContentTool* oldTool = getActiveTool())
    {
        oldTool->deactivate();
    }
    m_activeToolId = toolId;
    m_activeRequest = request;
    tool->activate(request);
    if (PDFAppEditContentScene* scene = m_context->getScene())
    {
        scene->updateToolPreview();
    }

    Q_EMIT activeToolChanged(toolId);
    return true;
}

bool PDFAppEditContentToolController::activateEditorTool(const PDFContentNodeReference& node)
{
    const PDFAppEditContentContext* context = m_context.data();
    const PDFContentContainerPointer container = context && node.isValid() ? context->getSession()->getContainer(node.container) : PDFContentContainerPointer();
    const PDFContentNodePointer target = container ? container->getNode(node.node) : PDFContentNodePointer();
    const PDFAppEditContentToolEntry* entry = target ? context->getRegistry()->findEditorTool(*target, *container) : nullptr;
    if (!entry)
    {
        return false;
    }

    PDFAppEditContentToolRequest request;
    request.target = node;
    return activateTool(entry->getId(), request);
}

void PDFAppEditContentToolController::activateSelectTool()
{
    activateTool(QString::fromLatin1(PDFAppEditContentToolIds::SELECT));
}

void PDFAppEditContentToolController::setMultiple(bool isMultiple)
{
    if (m_isMultiple == isMultiple)
    {
        return;
    }

    m_isMultiple = isMultiple;
    Q_EMIT multipleChanged(isMultiple);
}

void PDFAppEditContentToolController::finishGesture()
{
    if (!m_isMultiple && m_activeToolId != QLatin1String(PDFAppEditContentToolIds::SELECT))
    {
        activateSelectTool();
    }
}

bool PDFAppEditContentToolController::cancel()
{
    PDFAppEditContentTool* tool = getActiveTool();
    if (!tool)
    {
        return false;
    }

    // KE.16: the drag, then the mode of the tool, then another tool returns to Select
    if (tool->cancel())
    {
        return true;
    }
    if (m_activeToolId != QLatin1String(PDFAppEditContentToolIds::SELECT) && getTool(QString::fromLatin1(PDFAppEditContentToolIds::SELECT)))
    {
        resetToSelect();
        return true;
    }
    return false;
}

void PDFAppEditContentToolController::mousePress(QMouseEvent* event, const QPointF& scenePoint)
{
    PDFAppEditContentTool* tool = getActiveTool();
    if (tool && m_context && m_context->isEditingAllowed())
    {
        tool->mousePress(event, scenePoint);
    }
}

void PDFAppEditContentToolController::mouseMove(QMouseEvent* event, const QPointF& scenePoint)
{
    PDFAppEditContentTool* tool = getActiveTool();
    if (tool && m_context && m_context->isEditingAllowed())
    {
        tool->mouseMove(event, scenePoint);
    }
}

void PDFAppEditContentToolController::mouseRelease(QMouseEvent* event, const QPointF& scenePoint)
{
    PDFAppEditContentTool* tool = getActiveTool();
    if (tool && m_context && m_context->isEditingAllowed())
    {
        tool->mouseRelease(event, scenePoint);
    }
}

void PDFAppEditContentToolController::mouseDoubleClick(QMouseEvent* event, const QPointF& scenePoint)
{
    PDFAppEditContentTool* tool = getActiveTool();
    if (tool && m_context && m_context->isEditingAllowed())
    {
        tool->mouseDoubleClick(event, scenePoint);
    }
}

bool PDFAppEditContentToolController::keyPress(QKeyEvent* event)
{
    PDFAppEditContentContext* context = m_context.data();
    if (!context || PDFDeferredDeletion::isRetired(this))
    {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const bool isEditingAllowed = context->isEditingAllowed();
    const PDFAppEditContentRegistry* registry = context->getRegistry();

    // The unmodified key of a tool entry (V, T, ...): only while the editing is allowed
    if (isEditingAllowed && modifiers == Qt::NoModifier && !event->isAutoRepeat())
    {
        const PDFAppEditContentToolEntry* entry = registry->findToolByKey(QKeySequence(event->key()));
        if (entry && entry->getPlace() != PDFAppEditContentToolEntry::Place::Mode)
        {
            activateTool(entry->getId());
            return true;
        }
    }

    // The canvas key of an action (P of Preview also during Preview: the action says whether it is enabled)
    if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)))
    {
        const QKeySequence sequence(QKeyCombination(modifiers, Qt::Key(event->key())));
        if (const PDFAppEditContentActionEntry* action = registry->findActionByKey(sequence, PDFAppEditContentKeyScope::Canvas))
        {
            if (!event->isAutoRepeat())
            {
                context->triggerAction(action->getId());
            }
            return true;
        }
    }

    PDFAppEditContentTool* tool = getActiveTool();
    return isEditingAllowed && tool && tool->keyPress(event);
}

void PDFAppEditContentToolController::modifiersChanged(Qt::KeyboardModifiers modifiers)
{
    PDFAppEditContentTool* tool = getActiveTool();
    if (tool && m_context && m_context->isEditingAllowed())
    {
        tool->modifiersChanged(modifiers);
    }
}

QString PDFAppEditContentToolController::getStatusText() const
{
    const PDFAppEditContentTool* tool = getActiveTool();
    return tool ? tool->getStatusText() : QString();
}

QString PDFAppEditContentToolController::getModeLabel() const
{
    const PDFAppEditContentTool* tool = getActiveTool();
    return tool ? tool->getModeLabel() : QString();
}

QCursor PDFAppEditContentToolController::getCursor(const QPointF& scenePoint) const
{
    const PDFAppEditContentTool* tool = getActiveTool();
    if (!tool || !m_context || !m_context->isEditingAllowed())
    {
        return QCursor(Qt::ArrowCursor);
    }
    return tool->getCursor(scenePoint);
}

bool PDFAppEditContentToolController::isDragActive() const
{
    const PDFAppEditContentTool* tool = getActiveTool();
    return tool && tool->isDragActive();
}

void PDFAppEditContentToolController::requestStateUpdate()
{
    if (m_isStateUpdateQueued || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    m_isStateUpdateQueued = true;
    QMetaObject::invokeMethod(this, &PDFAppEditContentToolController::onQueuedStateUpdate, Qt::QueuedConnection);
}

void PDFAppEditContentToolController::retire()
{
    // A gesture ends without a command; nothing is emitted
    m_isStateUpdateQueued = false;
    if (PDFAppEditContentTool* tool = getActiveTool())
    {
        tool->deactivate();
    }
}

void PDFAppEditContentToolController::onQueuedStateUpdate()
{
    m_isStateUpdateQueued = false;
    Q_EMIT stateChanged();
}

void PDFAppEditContentToolController::resetToSelect()
{
    const QString selectId = QString::fromLatin1(PDFAppEditContentToolIds::SELECT);
    PDFAppEditContentTool* select = getTool(selectId);
    if (!select || PDFDeferredDeletion::isRetired(this))
    {
        return;
    }

    PDFAppEditContentTool* oldTool = getActiveTool();
    if (oldTool == select && m_activeRequest.target.isValid() == false)
    {
        // Select stays: only its gesture ends
        select->deactivate();
        select->activate(PDFAppEditContentToolRequest());
        return;
    }

    if (oldTool)
    {
        oldTool->deactivate();
    }
    m_activeToolId = selectId;
    m_activeRequest = PDFAppEditContentToolRequest();
    select->activate(m_activeRequest);
    if (m_context)
    {
        if (PDFAppEditContentScene* scene = m_context->getScene())
        {
            scene->updateToolPreview();
        }
    }
    Q_EMIT activeToolChanged(selectId);
}

void PDFAppEditContentToolController::onEditingAllowedChanged()
{
    // The editing stopped (Preview, a run, a read-only page): the active tool ends, Select is the one; the editing
    // started with no tool: Select (zadani_gui.md 4.16)
    const bool isEditingAllowed = m_context && m_context->isEditingAllowed();
    const bool isSelectActive = m_activeToolId == QLatin1String(PDFAppEditContentToolIds::SELECT);
    if (!isEditingAllowed)
    {
        if (PDFAppEditContentTool* tool = getActiveTool())
        {
            if (!isSelectActive)
            {
                resetToSelect();
            }
            else
            {
                tool->deactivate();
            }
        }
        return;
    }

    if (m_activeToolId.isEmpty())
    {
        resetToSelect();
    }
}

void PDFAppEditContentToolController::onPageChanged()
{
    // Another page: the gesture and the mode of the old page end
    if (getActiveTool())
    {
        resetToSelect();
    }
}

// ---------------------------------------------------------------- PDFAppEditContentSelectTool

/// Helpers of the tool Select (static functions only)
class PDFAppEditContentSelectToolHelper
{
public:
    PDFAppEditContentSelectToolHelper() = delete;

    using Handle = PDFAppEditContentSelectionItem::Handle;

    /// The tolerance of a hit: 6 px of the screen (zadani_gui.md 4.15)
    static constexpr qreal HIT_TOLERANCE = 6.0;

    /// The smallest size of a box after a resize, in pixels of the screen
    static constexpr qreal MINIMUM_SIZE = 1.0;

    /// A direction of the screen in the scene (\p screenVector in pixels), as a unit vector; the default without a view
    static QPointF getSceneDirection(const PDFAppEditContentView* view, const QPointF& screenVector, const QPointF& fallback)
    {
        if (!view || !view->transform().isInvertible())
        {
            return fallback;
        }
        const QTransform inverted = view->transform().inverted();
        const QPointF direction = inverted.map(screenVector) - inverted.map(QPointF(0.0, 0.0));
        const qreal length = std::hypot(direction.x(), direction.y());
        return length > 0.0 ? direction / length : fallback;
    }

    /// The node draws an image only: an Image, or a group (q cm /Im Do Q, as an image is placed) or a marked content,
    /// whose content is images and state only. Bounded by the size of the subtree (each node once).
    static bool isImage(const PDFContentNode& node)
    {
        bool hasImage = false;
        std::vector<const PDFContentNode*> stack{ &node };
        while (!stack.empty())
        {
            const PDFContentNode* current = stack.back();
            stack.pop_back();
            switch (current->getKind())
            {
                case PDFContentNodeKind::Image:
                    hasImage = true;
                    break;

                case PDFContentNodeKind::Group:
                case PDFContentNodeKind::MarkedContent:
                    for (const PDFContentNodePointer& child : current->getChildren())
                    {
                        stack.push_back(child.get());
                    }
                    break;

                case PDFContentNodeKind::StateRun:
                case PDFContentNodeKind::MarkedPoint:
                    break;

                default:
                    return false;
            }
        }
        return hasImage;
    }

    /// The cursor of a handle by its direction on the screen
    static Qt::CursorShape getHandleCursor(const PDFAppEditContentView* view, const QRectF& box, Handle handle)
    {
        if (handle == Handle::Rotation)
        {
            return Qt::CrossCursor;
        }

        const QPointF point = PDFAppEditContentSelectionItem::getHandlePoint(box, handle);
        QPointF vector = point - box.center();
        if (view)
        {
            vector = view->transform().map(point) - view->transform().map(box.center());
        }
        const qreal angle = std::fmod(std::atan2(vector.y(), vector.x()) * 180.0 / M_PI + 360.0, 180.0);
        if (angle < 22.5 || angle >= 157.5)
        {
            return Qt::SizeHorCursor;
        }
        if (angle < 67.5)
        {
            return Qt::SizeFDiagCursor;
        }
        if (angle < 112.5)
        {
            return Qt::SizeVerCursor;
        }
        return Qt::SizeBDiagCursor;
    }
};

PDFAppEditContentSelectTool::PDFAppEditContentSelectTool(PDFAppEditContentContext* context) :
    PDFAppEditContentTool(context)
{

}

PDFAppEditContentSelectTool::~PDFAppEditContentSelectTool() = default;

PDFAppEditContentScene* PDFAppEditContentSelectTool::getScene() const
{
    return getContext() ? getContext()->getScene() : nullptr;
}

PDFAppEditContentView* PDFAppEditContentSelectTool::getView() const
{
    return getContext() ? getContext()->getView() : nullptr;
}

PDFAppEditContentSelection* PDFAppEditContentSelectTool::getSelection() const
{
    return getContext() ? getContext()->getSelection() : nullptr;
}

qreal PDFAppEditContentSelectTool::getPixelSize() const
{
    const PDFAppEditContentScene* scene = getScene();
    return scene ? scene->getPixelSize() : 1.0;
}

void PDFAppEditContentSelectTool::deactivate()
{
    abortGesture();
}

std::vector<PDFContentNodeReference> PDFAppEditContentSelectTool::getMovableSelection() const
{
    // R7-4: the one rule of the lock of the context (a locked ancestor locks its children too)
    const PDFAppEditContentContext* context = getContext();
    return context ? context->getChangeableSelection() : std::vector<PDFContentNodeReference>();
}

bool PDFAppEditContentSelectTool::isGestureTouched(const PDFContentChange& change) const
{
    // The nodes of the gesture (the node under the press of a gesture, which has not started yet, too)
    std::set<PDFContentNodeReference> gestureNodes(m_nodes.cbegin(), m_nodes.cend());
    if (m_pressNode.isValid())
    {
        gestureNodes.insert(m_pressNode);
    }
    const PDFAppEditContentContext* context = getContext();
    if (gestureNodes.empty() || !context)
    {
        return false;
    }

    std::set<PDFContentNodeReference> touched(change.changedNodes.cbegin(), change.changedNodes.cend());
    touched.insert(change.removedNodes.cbegin(), change.removedNodes.cend());

    // A node of the gesture or one of its ancestors changed (a moved group moves its children), or the node is gone. A
    // walk up is bounded by the depth of a tree of the core.
    const PDFContentEditSession* session = context->getSession();
    for (const PDFContentNodeReference& node : gestureNodes)
    {
        const PDFContentContainerPointer container = session ? session->getContainer(node.container) : PDFContentContainerPointer();
        if (!container || !container->getNode(node.node))
        {
            return true;
        }

        PDFContentNodeId current = node.node;
        for (int depth = 0; depth <= PDFContentContainer::MAXIMUM_DEPTH + 2 && current.isValid(); ++depth)
        {
            if (touched.count(PDFContentNodeReference{ node.container, current }))
            {
                return true;
            }
            const PDFContentNodePointer parent = container->getParent(current);
            current = parent ? parent->getId() : PDFContentNodeId();
        }
    }

    // A descendant of a node of the gesture changed or was added (its box is another one now)
    std::vector<PDFContentNodeReference> inside(change.changedNodes.cbegin(), change.changedNodes.cend());
    inside.insert(inside.end(), change.addedNodes.cbegin(), change.addedNodes.cend());
    for (const PDFContentNodeReference& node : inside)
    {
        const PDFContentContainerPointer container = session ? session->getContainer(node.container) : PDFContentContainerPointer();
        const PDFContentNodePointer target = container ? container->getNode(node.node) : PDFContentNodePointer();
        PDFContentNodeId current = target ? target->getParentId() : PDFContentNodeId();
        for (int depth = 0; depth <= PDFContentContainer::MAXIMUM_DEPTH + 2 && current.isValid(); ++depth)
        {
            if (gestureNodes.count(PDFContentNodeReference{ node.container, current }))
            {
                return true;
            }
            const PDFContentNodePointer parent = container->getParent(current);
            current = parent ? parent->getId() : PDFContentNodeId();
        }
    }
    return false;
}

void PDFAppEditContentSelectTool::modelChanged(const PDFContentChange& change)
{
    // R6-21: a gesture, whose nodes changed under it (an undo by Ctrl+Z with the button held, a command of a key), ends
    // without a command: the release executes nothing, the scene drops the gesture transform
    switch (m_gesture)
    {
        case Gesture::Pending:
        case Gesture::Move:
        case Gesture::Resize:
        case Gesture::Rotate:
            if (isGestureTouched(change))
            {
                abortGesture();
            }
            break;

        case Gesture::None:
        case Gesture::RubberBand:
            break;
    }
}

QTransform PDFAppEditContentSelectTool::getResizeTransform(const QRectF& box, int handleValue, const QPointF& point, bool isAspectKept)
{
    using Handle = PDFAppEditContentSelectionItem::Handle;

    const Handle handle = Handle(handleValue);
    const QPointF anchor = PDFAppEditContentSelectionItem::getHandlePoint(box, PDFAppEditContentSelectionItem::getOppositeHandle(handle));
    const QPointF start = PDFAppEditContentSelectionItem::getHandlePoint(box, handle);
    const bool isHorizontal = handle != Handle::Top && handle != Handle::Bottom;
    const bool isVertical = handle != Handle::Left && handle != Handle::Right;

    // The scale of each axis: the distance of the point from the anchor against the distance of the handle; an axis
    // without an extent stays
    const qreal startX = start.x() - anchor.x();
    const qreal startY = start.y() - anchor.y();
    qreal scaleX = isHorizontal && !qFuzzyIsNull(startX) ? (point.x() - anchor.x()) / startX : 1.0;
    qreal scaleY = isVertical && !qFuzzyIsNull(startY) ? (point.y() - anchor.y()) / startY : 1.0;
    if (isAspectKept)
    {
        // One scale: the larger one of a corner, the moved axis of an edge (the signs of each axis stay)
        qreal scale = 1.0;
        if (isHorizontal && isVertical)
        {
            scale = qMax(qAbs(scaleX), qAbs(scaleY));
            scaleX = std::copysign(scale, scaleX);
            scaleY = std::copysign(scale, scaleY);
        }
        else
        {
            scale = isHorizontal ? qAbs(scaleX) : qAbs(scaleY);
            scaleX = isHorizontal ? scaleX : scale;
            scaleY = isVertical ? scaleY : scale;
        }
    }
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY))
    {
        return QTransform();
    }

    // Around the anchor: to the origin, the scale, back
    return QTransform::fromTranslate(-anchor.x(), -anchor.y()) * QTransform::fromScale(scaleX, scaleY) * QTransform::fromTranslate(anchor.x(), anchor.y());
}

QTransform PDFAppEditContentSelectTool::getRotationTransform(const QPointF& center, const QPointF& start, const QPointF& point, bool isSnapped)
{
    const qreal startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
    const qreal angle = std::atan2(point.y() - center.y(), point.x() - center.x());
    qreal degrees = qRadiansToDegrees(angle - startAngle);
    if (isSnapped)
    {
        degrees = std::round(degrees / 15.0) * 15.0;
    }

    QTransform rotation;
    rotation.rotate(degrees);
    return QTransform::fromTranslate(-center.x(), -center.y()) * rotation * QTransform::fromTranslate(center.x(), center.y());
}

void PDFAppEditContentSelectTool::mousePress(QMouseEvent* event, const QPointF& scenePoint)
{
    using Helper = PDFAppEditContentSelectToolHelper;
    using Handle = PDFAppEditContentSelectionItem::Handle;

    PDFAppEditContentContext* context = getContext();
    PDFAppEditContentScene* scene = getScene();
    PDFAppEditContentSelection* selection = getSelection();
    if (!context || !scene || !selection)
    {
        return;
    }

    abortGesture();
    m_pressPoint = scenePoint;
    m_currentPoint = scenePoint;
    m_modifiers = event->modifiers();
    m_isToggleDeferred = false;
    m_pressNode = PDFContentNodeReference();
    m_pendingGesture = Gesture::None;
    const qreal pixelSize = getPixelSize();
    const std::vector<PDFContentNodeReference> nodes = scene->getNodesAt(scenePoint, Helper::HIT_TOLERANCE * pixelSize, true);

    // The right button: an object, which is not selected, becomes the selection (the context menu follows from the view)
    if (event->button() == Qt::RightButton)
    {
        for (const PDFContentNodeReference& node : nodes)
        {
            if (!context->getSession()->isNodeLocked(node))
            {
                if (!selection->contains(node))
                {
                    selection->setNode(node);
                }
                break;
            }
        }
        return;
    }
    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    // A handle of the selection: a resize or a rotation, behind the distance of a drag of the system (R6-10: a click on
    // a handle with a jitter of the hand changes nothing)
    const Handle handle = scene->getSelectionItem()->getHandleAt(scenePoint, pixelSize);
    if (handle != Handle::None)
    {
        m_nodes = getMovableSelection();
        if (!m_nodes.empty())
        {
            m_gesture = Gesture::Pending;
            m_pendingGesture = handle == Handle::Rotation ? Gesture::Rotate : Gesture::Resize;
            m_handle = int(handle);
            m_startBox = scene->getSelectionItem()->getHandleBox();
            m_isAspectDefault = true;
            const PDFContentContainerPointer container = context->getContainer();
            for (const PDFContentNodeReference& node : m_nodes)
            {
                const PDFContentNodePointer target = container && node.container == container->getKey() ? container->getNode(node.node) : PDFContentNodePointer();
                m_isAspectDefault = m_isAspectDefault && target && PDFAppEditContentSelectToolHelper::isImage(*target);
            }
            m_transform = QTransform();
            update();
            return;
        }
    }

    // The objects under the point from the top: the first one, which is not locked (a click goes through a locked
    // object, whose frame and card are shown, -locked-click); Alt walks down through them
    const bool isAlt = m_modifiers.testFlag(Qt::AltModifier);
    PDFContentNodeReference lockedNode;
    std::vector<PDFContentNodeReference> unlocked;
    for (const PDFContentNodeReference& node : nodes)
    {
        if (context->getSession()->isNodeLocked(node))
        {
            if (!lockedNode.isValid() && unlocked.empty())
            {
                lockedNode = node;
            }
        }
        else
        {
            unlocked.push_back(node);
        }
    }

    PDFContentNodeReference target;
    if (isAlt && !unlocked.empty())
    {
        m_cycleIndex = unlocked == m_cycleNodes ? (m_cycleIndex + 1) % unlocked.size() : 0;
        m_cycleNodes = unlocked;
        target = unlocked[m_cycleIndex];
    }
    else if (!unlocked.empty())
    {
        m_cycleNodes.clear();
        target = unlocked.front();
    }
    m_pressNode = target;

    // The selection changes (its slots may close the tool)
    const bool isAdding = m_modifiers.testFlag(Qt::ShiftModifier) || m_modifiers.testFlag(Qt::ControlModifier);
    if (!target.isValid())
    {
        if (!isAdding)
        {
            selection->clear();
        }
    }
    else if (m_modifiers.testFlag(Qt::ControlModifier) && selection->contains(target))
    {
        // Ctrl + click on a selected object removes it at the release, unless it is dragged (a copy)
        m_isToggleDeferred = true;
    }
    else if (isAdding)
    {
        selection->addNode(target);
    }
    else if (selection->contains(target))
    {
        selection->setKeyNode(target);
    }
    else
    {
        selection->setNode(target);
    }
    if (!isAlive())
    {
        return;
    }

    if (lockedNode.isValid())
    {
        // The frame with the name of the locked object and its card, until the selection changes
        QPointer<PDFAppEditContentContext> guard(context);
        context->setLockedClickNode(lockedNode);
        if (guard && isAlive())
        {
            context->setCardRequested(QString::fromLatin1(PDFAppEditContentCardIds::LOCKED_OBJECT), true);
        }
        if (!isAlive())
        {
            return;
        }
    }

    m_gesture = target.isValid() ? Gesture::Pending : Gesture::RubberBand;
    m_pendingGesture = target.isValid() ? Gesture::Move : Gesture::None;
    m_nodes = getMovableSelection();
    update();
}

void PDFAppEditContentSelectTool::mouseMove(QMouseEvent* event, const QPointF& scenePoint)
{
    m_currentPoint = scenePoint;
    m_modifiers = event->modifiers();
    if (m_gesture == Gesture::None)
    {
        return;
    }

    if (m_gesture == Gesture::Pending)
    {
        // A move, a resize or a rotation starts behind the distance of a drag of the system; the gesture then follows
        // the pointer from the point of the press (no jump)
        const qreal distance = QLineF(m_pressPoint, scenePoint).length() / getPixelSize();
        if (distance < QApplication::startDragDistance())
        {
            return;
        }
        m_nodes = getMovableSelection();
        if (m_nodes.empty() || m_pendingGesture == Gesture::None)
        {
            m_gesture = Gesture::None;
            return;
        }
        m_gesture = m_pendingGesture;
        m_isToggleDeferred = false;
    }

    updateGesture();
}

void PDFAppEditContentSelectTool::updateGesture()
{
    PDFAppEditContentScene* scene = getScene();
    if (!scene)
    {
        return;
    }

    const bool isShift = m_modifiers.testFlag(Qt::ShiftModifier);
    switch (m_gesture)
    {
        case Gesture::Move:
        {
            // Shift: only along the axis of the larger move
            QPointF delta = m_currentPoint - m_pressPoint;
            if (isShift)
            {
                if (qAbs(delta.x()) >= qAbs(delta.y()))
                {
                    delta.setY(0.0);
                }
                else
                {
                    delta.setX(0.0);
                }
            }
            m_transform = QTransform::fromTranslate(delta.x(), delta.y());
            break;
        }

        case Gesture::Resize:
        {
            // An image keeps its proportions (Shift frees them), the other objects the reverse (zadani_gui.md 4.16)
            // The handle moves by the move of the pointer (R6-10: the grab point keeps its distance from the edge)
            const bool isAspectKept = m_isAspectDefault != isShift;
            const QPointF handlePoint = PDFAppEditContentSelectionItem::getHandlePoint(m_startBox, PDFAppEditContentSelectionItem::Handle(m_handle)) + (m_currentPoint - m_pressPoint);
            m_transform = getResizeTransform(m_startBox, m_handle, handlePoint, isAspectKept);

            // Not smaller than a pixel: the scale is not degenerate
            const QRectF result = m_transform.mapRect(m_startBox);
            const qreal minimum = PDFAppEditContentSelectToolHelper::MINIMUM_SIZE * getPixelSize();
            const bool isFlat = (m_startBox.width() > 0.0 && result.width() < minimum) || (m_startBox.height() > 0.0 && result.height() < minimum);
            if (isFlat || !m_transform.isInvertible())
            {
                m_transform = QTransform();
            }
            break;
        }

        case Gesture::Rotate:
            // Around the center of the box at the press; the angle from the point of the press (on the rotation
            // handle, which itself moves with the frame)
            m_transform = getRotationTransform(m_startBox.center(), m_pressPoint, m_currentPoint, isShift);
            break;

        case Gesture::RubberBand:
            update();
            return;

        case Gesture::None:
        case Gesture::Pending:
            return;
    }

    // The items follow the gesture (a complex page: only the frames)
    scene->setGestureTransform(m_nodes, m_transform);
    update();
}

void PDFAppEditContentSelectTool::mouseRelease(QMouseEvent* event, const QPointF& scenePoint)
{
    m_currentPoint = scenePoint;
    m_modifiers = event->modifiers();
    if (event->button() != Qt::LeftButton)
    {
        return;
    }

    switch (m_gesture)
    {
        case Gesture::Pending:
        {
            // A click without a drag: Ctrl + click on a selected object removes it
            const bool isToggle = m_isToggleDeferred;
            const PDFContentNodeReference node = m_pressNode;
            m_gesture = Gesture::None;
            if (isToggle && getSelection())
            {
                getSelection()->removeNode(node);
            }
            break;
        }

        case Gesture::RubberBand:
        {
            // The objects inside of the rectangle (Shift / Ctrl add them)
            m_gesture = Gesture::None;
            PDFAppEditContentScene* scene = getScene();
            PDFAppEditContentSelection* selection = getSelection();
            if (scene && selection)
            {
                const qreal minimum = PDFAppEditContentSelectToolHelper::MINIMUM_SIZE * getPixelSize();
                const QRectF rectangle = QRectF(m_pressPoint, m_currentPoint).normalized();
                if (rectangle.width() > minimum || rectangle.height() > minimum)
                {
                    std::vector<PDFContentNodeReference> nodes = scene->getNodesIn(rectangle);
                    if (m_modifiers.testFlag(Qt::ShiftModifier) || m_modifiers.testFlag(Qt::ControlModifier))
                    {
                        std::vector<PDFContentNodeReference> united = selection->getNodes();
                        united.insert(united.end(), nodes.begin(), nodes.end());
                        nodes = std::move(united);
                    }
                    selection->setNodes(nodes);
                }
            }
            break;
        }

        case Gesture::Move:
        case Gesture::Resize:
        case Gesture::Rotate:
            finishGesture();
            return;

        case Gesture::None:
            break;
    }

    if (isAlive())
    {
        update();
    }
}

void PDFAppEditContentSelectTool::finishGesture()
{
    PDFAppEditContentContext* context = getContext();
    const Gesture gesture = m_gesture;
    const QTransform transform = m_transform;
    const std::vector<PDFContentNodeReference> nodes = m_nodes;
    const bool isCopy = gesture == Gesture::Move && m_modifiers.testFlag(Qt::ControlModifier);
    abortGesture();
    if (!context || nodes.empty() || (transform.isIdentity() && !isCopy))
    {
        update();
        return;
    }

    // One command of the gesture (Copy with the move for Ctrl, else Transform); its slots may close the tool
    PDFContentCommandPointer command = isCopy ? PDFContentCommandFactory::createCopy(nodes, context->getEditedRoot(), -1, transform)
                                              : PDFContentCommandFactory::createTransform(nodes, transform);
    context->execute(command);
    if (isAlive())
    {
        update();
    }
}

void PDFAppEditContentSelectTool::abortGesture()
{
    const bool wasActive = m_gesture != Gesture::None;
    m_gesture = Gesture::None;
    m_pendingGesture = Gesture::None;
    m_transform = QTransform();
    m_nodes.clear();
    m_isToggleDeferred = false;
    if (PDFAppEditContentScene* scene = getScene())
    {
        scene->clearGestureTransform();
    }
    if (wasActive && isAlive())
    {
        update();
    }
}

void PDFAppEditContentSelectTool::mouseDoubleClick(QMouseEvent* event, const QPointF& scenePoint)
{
    using Helper = PDFAppEditContentSelectToolHelper;

    PDFAppEditContentContext* context = getContext();
    PDFAppEditContentScene* scene = getScene();
    if (!context || !scene || event->button() != Qt::LeftButton)
    {
        return;
    }

    abortGesture();
    const std::vector<PDFContentNodeReference> nodes = scene->getNodesAt(scenePoint, Helper::HIT_TOLERANCE * getPixelSize(), false);
    if (nodes.empty())
    {
        // The dimmed page inside of a group: the group is left (zadani_gui.md 4.27)
        if (!context->getIsolationPath().empty())
        {
            context->requestLeaveIsolation(1);
        }
        return;
    }

    // The editor tool of the object (typing, nodes, crop), else the inside of a group or a form (stage E4)
    const PDFContentNodeReference node = nodes.front();
    if (!context->getToolController()->activateEditorTool(node) && isAlive())
    {
        const PDFContentContainerPointer container = context->getContainer();
        const PDFContentNodePointer target = container ? container->getNode(node.node) : PDFContentNodePointer();
        if (target && (target->getKind() == PDFContentNodeKind::Group || target->getKind() == PDFContentNodeKind::MarkedContent || target->getKind() == PDFContentNodeKind::FormUse))
        {
            context->requestIsolation(node);
        }
    }
}

bool PDFAppEditContentSelectTool::keyPress(QKeyEvent* event)
{
    using Helper = PDFAppEditContentSelectToolHelper;

    PDFAppEditContentContext* context = getContext();
    PDFAppEditContentSelection* selection = getSelection();
    if (!context || !selection)
    {
        return false;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    switch (event->key())
    {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        {
            if (modifiers & ~Qt::ShiftModifier)
            {
                return false;
            }

            // By 1 pt (Shift: 10 pt) in the directions of the screen (zadani_gui.md 4.15)
            const std::vector<PDFContentNodeReference> nodes = getMovableSelection();
            if (nodes.empty() || m_gesture != Gesture::None)
            {
                return !nodes.empty();
            }

            const qreal step = modifiers.testFlag(Qt::ShiftModifier) ? 10.0 : 1.0;
            const PDFAppEditContentView* view = getView();
            const QPointF right = Helper::getSceneDirection(view, QPointF(1.0, 0.0), QPointF(1.0, 0.0));
            const QPointF up = Helper::getSceneDirection(view, QPointF(0.0, -1.0), QPointF(0.0, 1.0));
            QPointF delta;
            switch (event->key())
            {
                case Qt::Key_Left:  delta = -right; break;
                case Qt::Key_Right: delta = right; break;
                case Qt::Key_Up:    delta = up; break;
                default:            delta = -up; break;
            }
            delta *= step;

            // Rounded to the axes of the page (the rotations of /Rotate are by 90 degrees)
            delta = QPointF(std::round(delta.x() * 1000.0) / 1000.0, std::round(delta.y() * 1000.0) / 1000.0);
            context->execute(PDFContentCommandFactory::createTransform(nodes, QTransform::fromTranslate(delta.x(), delta.y())));
            return true;
        }

        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            // The action of P12 owns what is deleted (pe1_skeleton_notes.md 11, R3-7)
            if (modifiers == Qt::NoModifier && !selection->isEmpty())
            {
                return context->triggerAction(QString::fromLatin1(PDFAppEditContentActionIds::DELETE_SELECTION));
            }
            return false;

        case Qt::Key_Return:
        case Qt::Key_Enter:
        {
            // The contextual transition of the one selected object (zadani_gui.md 4.16)
            if (modifiers != Qt::NoModifier || selection->getCount() != 1)
            {
                return false;
            }

            const PDFContentNodeReference node = selection->getKeyNode();
            if (!context->getToolController()->activateEditorTool(node) && isAlive())
            {
                const PDFContentContainerPointer container = context->getContainer();
                const PDFContentNodePointer target = container ? container->getNode(node.node) : PDFContentNodePointer();
                if (target && (target->getKind() == PDFContentNodeKind::Group || target->getKind() == PDFContentNodeKind::MarkedContent || target->getKind() == PDFContentNodeKind::FormUse))
                {
                    context->requestIsolation(node);
                }
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

void PDFAppEditContentSelectTool::modifiersChanged(Qt::KeyboardModifiers modifiers)
{
    m_modifiers = modifiers;
    if (m_gesture == Gesture::Move || m_gesture == Gesture::Resize || m_gesture == Gesture::Rotate)
    {
        updateGesture();
    }
}

bool PDFAppEditContentSelectTool::cancel()
{
    // Esc ends a drag without a command (KE.16); without a drag Select has nothing to cancel
    if (m_gesture == Gesture::None)
    {
        return false;
    }

    abortGesture();
    return true;
}

bool PDFAppEditContentSelectTool::isDragActive() const
{
    return m_gesture != Gesture::None;
}

QString PDFAppEditContentSelectTool::getStatusText() const
{
    switch (m_gesture)
    {
        case Gesture::Move:
            return tr("Drag to move - Shift keeps the direction, Ctrl makes a copy");

        case Gesture::Resize:
            return m_isAspectDefault ? tr("Drag to resize - Shift frees the proportions") : tr("Drag to resize - Shift keeps the proportions");

        case Gesture::Rotate:
            return tr("Drag to rotate - Shift turns by 15°");

        case Gesture::RubberBand:
            return tr("Drag to select the objects inside of the rectangle");

        case Gesture::None:
        case Gesture::Pending:
            break;
    }
    return QString();
}

QCursor PDFAppEditContentSelectTool::getCursor(const QPointF& scenePoint) const
{
    using Helper = PDFAppEditContentSelectToolHelper;
    using Handle = PDFAppEditContentSelectionItem::Handle;

    const PDFAppEditContentScene* scene = getScene();
    if (!scene)
    {
        return QCursor(Qt::ArrowCursor);
    }

    switch (m_gesture)
    {
        case Gesture::Move:
            return QCursor(Qt::SizeAllCursor);

        case Gesture::Resize:
        case Gesture::Rotate:
            return QCursor(Helper::getHandleCursor(getView(), m_startBox, Handle(m_handle)));

        case Gesture::Pending:
            if (m_pendingGesture == Gesture::Resize || m_pendingGesture == Gesture::Rotate)
            {
                return QCursor(Helper::getHandleCursor(getView(), m_startBox, Handle(m_handle)));
            }
            break;

        default:
            break;
    }

    const Handle handle = scene->getSelectionItem()->getHandleAt(scenePoint, scene->getPixelSize());
    if (handle != Handle::None)
    {
        return QCursor(Helper::getHandleCursor(getView(), scene->getSelectionItem()->getHandleBox(), handle));
    }
    return QCursor(Qt::ArrowCursor);
}

void PDFAppEditContentSelectTool::paint(QPainter* painter, const PDFAppEditContentToolPaintContext& paintContext) const
{
    Q_UNUSED(paintContext);

    if (m_gesture != Gesture::RubberBand)
    {
        return;
    }

    // The rectangle of the selection (the painter state is the caller's)
    const QRectF rectangle = QRectF(m_pressPoint, m_currentPoint).normalized();
    QColor fill = PDFThemeManager::getInstance()->getColor(PDFThemeColors::BorderFocus);
    fill.setAlphaF(0.12f);
    QPen pen(PDFThemeManager::getInstance()->getColor(PDFThemeColors::BorderFocus));
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    painter->setPen(pen);
    painter->setBrush(fill);
    painter->drawRect(rectangle);
}

QRectF PDFAppEditContentSelectTool::getPreviewRectangle(qreal pixelSize) const
{
    // R6-11: only the rubber band is painted; its pen is cosmetic, 1 px wide and antialiased (a pixel on each side)
    if (m_gesture != Gesture::RubberBand)
    {
        return QRectF();
    }

    const qreal margin = 2.0 * pixelSize;
    return QRectF(m_pressPoint, m_currentPoint).normalized().adjusted(-margin, -margin, margin, margin);
}

// ---------------------------------------------------------------- registration

/// The entry of the tool Select
class PDFAppEditContentSelectToolEntry : public PDFAppEditContentToolEntry
{
    Q_DECLARE_TR_FUNCTIONS(pdf::PDFAppEditContentSelectToolEntry)

public:
    virtual QString getId() const override { return QString::fromLatin1(PDFAppEditContentToolIds::SELECT); }
    virtual Place getPlace() const override { return Place::Toolbar; }
    virtual int getOrder() const override { return 100; }
    virtual QString getTitle() const override { return tr("Select"); }
    virtual QString getDescription() const override { return tr("Select, move, resize and rotate objects"); }
    virtual QString getIconName() const override { return QStringLiteral("cursor"); }
    virtual QKeySequence getKey() const override { return QKeySequence(Qt::Key_V); }
    virtual std::unique_ptr<PDFAppEditContentTool> createTool(PDFAppEditContentContext* context) const override { return std::make_unique<PDFAppEditContentSelectTool>(context); }
};

void PDFAppEditContentTools::registerParts(PDFAppEditContentRegistry* registry)
{
    registry->addTool(std::make_unique<PDFAppEditContentSelectToolEntry>());
}

}   // namespace pdf

#include "moc_pdfappeditcontenttools.cpp"
