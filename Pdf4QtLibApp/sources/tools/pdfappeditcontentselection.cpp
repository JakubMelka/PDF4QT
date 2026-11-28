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

#include "pdfappeditcontentselection.h"

#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentscene.h"
#include "pdfappeditcontentview.h"
#include "pdfpainterutils.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QCoreApplication>
#include <QLineF>
#include <QPainter>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf
{

// Stage E1 skeleton. Real code: the plain state of PDFAppEditContentSelection (pe1_skeleton_notes.md decision 2) and
// getBoundingBox (pe1_skeleton_notes.md 11, R3-2). Stubs of package P11: PDFAppEditContentSelectionItem.

PDFAppEditContentSelection::PDFAppEditContentSelection(PDFAppEditContentContext* context, QObject* parent) :
    BaseClass(parent),
    m_context(context)
{
    // P11: the pruning after a change of the model, the answer to the requests of the session, the end of the node of
    // the card Locked object (the first receiver of changed, connected before every other one)
    connect(this, &PDFAppEditContentSelection::changed, this, &PDFAppEditContentSelection::onChanged);
    if (context)
    {
        // A new session of reloadDocument is connected again (FX3 request 1)
        PDFDeferredDeletion::connect(context, &PDFAppEditContentContext::sessionReplaced, this, &PDFAppEditContentSelection::connectSession);
    }
    connectSession();
}

void PDFAppEditContentSelection::connectSession()
{
    // The signals of the session of the context; the old session of a reload is disconnected here before it is deleted
    PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    if (session == m_session.data())
    {
        return;
    }
    if (m_session)
    {
        disconnect(m_session.data(), nullptr, this, nullptr);
    }

    m_session = session;
    if (session)
    {
        PDFDeferredDeletion::connect(session, &PDFContentEditSession::modelChanged, this, &PDFAppEditContentSelection::onModelChanged);
        PDFDeferredDeletion::connect(session, &PDFContentEditSession::selectionChangeRequested, this, &PDFAppEditContentSelection::onSelectionChangeRequested);
    }
}

void PDFAppEditContentSelection::onModelChanged(const PDFContentChange& change)
{
    // The removed nodes and the nodes, whose container does not have them any more (one change of the selection)
    const PDFContentEditSession* session = m_context ? m_context->getSession() : nullptr;
    if (!session || m_nodes.empty())
    {
        return;
    }

    // R6-4, R7-3: the removed nodes as a set (selected x removed would be quadratic); every selected node is of one
    // container, which is looked up once
    const std::set<PDFContentNodeReference> removed(change.removedNodes.cbegin(), change.removedNodes.cend());
    const PDFContentContainerPointer container = session->getContainer(m_nodes.front().container);
    std::vector<PDFContentNodeReference> nodes;
    nodes.reserve(m_nodes.size());
    bool isKeyKept = false;
    for (const PDFContentNodeReference& node : m_nodes)
    {
        if (!removed.count(node) && container && container->getNode(node.node))
        {
            nodes.push_back(node);
            isKeyKept = isKeyKept || node == m_keyNode;
        }
    }

    if (nodes.size() != m_nodes.size())
    {
        // The key object stays, when it stays selected
        const PDFContentNodeReference keyNode = isKeyKept ? m_keyNode : (nodes.empty() ? PDFContentNodeReference() : nodes.back());
        assign(std::move(nodes), keyNode);
    }
}

void PDFAppEditContentSelection::assign(std::vector<PDFContentNodeReference> nodes, const PDFContentNodeReference& keyNode)
{
    m_nodes = std::move(nodes);
    m_nodeSet = std::set<PDFContentNodeReference>(m_nodes.cbegin(), m_nodes.cend());
    m_keyNode = keyNode;
    Q_EMIT changed();
}

void PDFAppEditContentSelection::onSelectionChangeRequested(const QList<PDFContentNodeReference>& nodes)
{
    setNodes(std::vector<PDFContentNodeReference>(nodes.cbegin(), nodes.cend()));
}

void PDFAppEditContentSelection::onChanged()
{
    // zadani_gui.md 4.15 "-locked-click": the frame and the card of a locked object stay until the selection changes
    QPointer<PDFAppEditContentContext> context = m_context;
    if (context && context->getLockedClickNode().isValid() && !PDFDeferredDeletion::isRetired(context))
    {
        // The slots of cardsChanged may close the tool
        context->setLockedClickNode(PDFContentNodeReference());
        if (context && !PDFDeferredDeletion::isRetired(context))
        {
            context->setCardRequested(QString::fromLatin1(PDFAppEditContentCardIds::LOCKED_OBJECT), false);
        }
    }
}

PDFAppEditContentSelection::~PDFAppEditContentSelection() = default;

bool PDFAppEditContentSelection::contains(const PDFContentNodeReference& node) const
{
    return m_nodeSet.count(node) > 0;
}

void PDFAppEditContentSelection::setNodes(const std::vector<PDFContentNodeReference>& nodes)
{
    // References, which are not valid, duplicates and the nodes of another container than the first one are dropped;
    // the duplicates by a set (R6-4, R7-3: O(n log n) for Select All of a page of 100 000 objects)
    std::vector<PDFContentNodeReference> selected;
    std::set<PDFContentNodeReference> seen;
    selected.reserve(nodes.size());
    for (const PDFContentNodeReference& node : nodes)
    {
        if (!node.isValid() || (!selected.empty() && node.container != selected.front().container))
        {
            continue;
        }
        if (seen.insert(node).second)
        {
            selected.push_back(node);
        }
    }

    const PDFContentNodeReference keyNode = selected.empty() ? PDFContentNodeReference() : selected.back();
    if (selected == m_nodes && keyNode == m_keyNode)
    {
        return;
    }

    m_nodes = std::move(selected);
    m_nodeSet = std::move(seen);
    m_keyNode = keyNode;
    Q_EMIT changed();
}

void PDFAppEditContentSelection::setNode(const PDFContentNodeReference& node)
{
    setNodes({ node });
}

void PDFAppEditContentSelection::clear()
{
    setNodes({ });
}

void PDFAppEditContentSelection::addNode(const PDFContentNodeReference& node)
{
    if (!node.isValid())
    {
        return;
    }
    if (!m_nodes.empty() && node.container != m_nodes.front().container)
    {
        // A node of another container replaces the selection
        setNodes({ node });
        return;
    }
    if (contains(node))
    {
        // Selected already: it becomes the key object
        setKeyNode(node);
        return;
    }

    m_nodes.push_back(node);
    m_nodeSet.insert(node);
    m_keyNode = node;
    Q_EMIT changed();
}

void PDFAppEditContentSelection::removeNode(const PDFContentNodeReference& node)
{
    if (!contains(node))
    {
        return;
    }

    m_nodes.erase(std::find(m_nodes.begin(), m_nodes.end(), node));
    m_nodeSet.erase(node);
    if (m_keyNode == node)
    {
        m_keyNode = m_nodes.empty() ? PDFContentNodeReference() : m_nodes.back();
    }
    Q_EMIT changed();
}

void PDFAppEditContentSelection::toggleNode(const PDFContentNodeReference& node)
{
    if (contains(node))
    {
        removeNode(node);
    }
    else
    {
        addNode(node);
    }
}

void PDFAppEditContentSelection::setKeyNode(const PDFContentNodeReference& node)
{
    if (m_keyNode == node || !contains(node))
    {
        return;
    }

    m_keyNode = node;
    Q_EMIT changed();
}

QRectF PDFAppEditContentSelection::getBoundingBox() const
{
    // Real code of the skeleton (R3-2): the one policy of the box of a node, PDFAppEditContentGeometryHelper
    QRectF box;
    for (const PDFContentNodeReference& node : m_nodes)
    {
        const QRectF nodeBox = PDFAppEditContentGeometryHelper::getPageBoundingBox(m_context.data(), node);
        if (!nodeBox.isNull())
        {
            box = box.isNull() ? nodeBox : box.united(nodeBox);
        }
    }
    return box;
}

// ---------------------------------------------------------------- PDFAppEditContentSelectionItem

/// Helpers of the selection item (static functions only)
class PDFAppEditContentSelectionItemHelper
{
public:
    PDFAppEditContentSelectionItemHelper() = delete;

    using Handle = PDFAppEditContentSelectionItem::Handle;

    static constexpr Handle HANDLES[] = { Handle::TopLeft, Handle::Top, Handle::TopRight, Handle::Right, Handle::BottomRight, Handle::Bottom, Handle::BottomLeft, Handle::Left };

    static QColor getColor(PDFThemeColors::Color color)
    {
        return PDFThemeManager::getInstance()->getColor(color);
    }

    static QPen createPen(PDFThemeColors::Color color, qreal width, Qt::PenStyle style = Qt::SolidLine)
    {
        QPen pen(getColor(color));
        pen.setCosmetic(true);
        pen.setWidthF(width);
        pen.setStyle(style);
        return pen;
    }

    /// The offset of the handles from the box: the frame of one object, the common frame of more objects is wider
    static qreal getHandleOffset(bool isMultiple, qreal pixelSize)
    {
        return (isMultiple ? 2.0 : 1.0) * PDFAppEditContentSelectionItem::FRAME_OFFSET * pixelSize;
    }

    /// The name of a kind of object for the label of a locked object (the names of the rows of the tab Objects)
    static QString getKindName(PDFContentNodeKind kind)
    {
        const char* context = "pdf::PDFAppEditContentSelectionItem";
        switch (kind)
        {
            case PDFContentNodeKind::Path:          return QCoreApplication::translate(context, "Path");
            case PDFContentNodeKind::TextObject:    return QCoreApplication::translate(context, "Text");
            case PDFContentNodeKind::Image:         return QCoreApplication::translate(context, "Image");
            case PDFContentNodeKind::Shading:       return QCoreApplication::translate(context, "Shading");
            case PDFContentNodeKind::FormUse:       return QCoreApplication::translate(context, "Form XObject");
            case PDFContentNodeKind::Group:         return QCoreApplication::translate(context, "Group");
            case PDFContentNodeKind::MarkedContent: return QCoreApplication::translate(context, "Marked content");
            default:
                break;
        }
        return QCoreApplication::translate(context, "Unknown content");
    }

    /// The box grown by a margin on all sides (a degenerate box keeps its position)
    static QRectF grow(const QRectF& box, qreal margin)
    {
        return box.adjusted(-margin, -margin, margin, margin);
    }
};

PDFAppEditContentSelectionItem::PDFAppEditContentSelectionItem(PDFAppEditContentContext* context) :
    m_context(context)
{
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
}

PDFAppEditContentSelectionItem::~PDFAppEditContentSelectionItem() = default;

QRectF PDFAppEditContentSelectionItem::boundingRect() const
{
    return m_boundingRect;
}

QPointF PDFAppEditContentSelectionItem::getHandlePoint(const QRectF& box, Handle handle)
{
    // The page space is y up: "top" is the edge of the larger y (QRectF::bottom)
    const QPointF center = box.center();
    switch (handle)
    {
        case Handle::TopLeft:       return QPointF(box.left(), box.bottom());
        case Handle::Top:           return QPointF(center.x(), box.bottom());
        case Handle::TopRight:      return QPointF(box.right(), box.bottom());
        case Handle::Right:         return QPointF(box.right(), center.y());
        case Handle::BottomRight:   return QPointF(box.right(), box.top());
        case Handle::Bottom:        return QPointF(center.x(), box.top());
        case Handle::BottomLeft:    return QPointF(box.left(), box.top());
        case Handle::Left:          return QPointF(box.left(), center.y());
        case Handle::None:
        case Handle::Rotation:
            break;
    }
    return center;
}

PDFAppEditContentSelectionItem::Handle PDFAppEditContentSelectionItem::getOppositeHandle(Handle handle)
{
    switch (handle)
    {
        case Handle::TopLeft:       return Handle::BottomRight;
        case Handle::Top:           return Handle::Bottom;
        case Handle::TopRight:      return Handle::BottomLeft;
        case Handle::Right:         return Handle::Left;
        case Handle::BottomRight:   return Handle::TopLeft;
        case Handle::Bottom:        return Handle::Top;
        case Handle::BottomLeft:    return Handle::TopRight;
        case Handle::Left:          return Handle::Right;
        case Handle::None:
        case Handle::Rotation:
            break;
    }
    return handle;
}

bool PDFAppEditContentSelectionItem::isHandleShown(Handle handle) const
{
    if (m_handleBox.isNull() || handle == Handle::None)
    {
        return false;
    }
    if (handle == Handle::Rotation)
    {
        return true;
    }

    // A frame smaller than 24 px on the screen: the corner handles only (zadani_gui.md 4.15)
    const bool isCorner = handle == Handle::TopLeft || handle == Handle::TopRight || handle == Handle::BottomLeft || handle == Handle::BottomRight;
    const qreal pixels = qMin(m_handleBox.width(), m_handleBox.height()) / m_pixelSize;
    return isCorner || pixels >= SMALL_FRAME;
}

QPointF PDFAppEditContentSelectionItem::getRotationHandlePoint() const
{
    using Helper = PDFAppEditContentSelectionItemHelper;

    // The middle of the edge, which is the top one on the screen, then up by the distance of the handle
    const QRectF box = Helper::grow(m_handleBox, Helper::getHandleOffset(m_isMultiple, m_pixelSize));
    const QPointF center = box.center();
    const QPointF edges[] = { QPointF(center.x(), box.bottom()), QPointF(center.x(), box.top()), QPointF(box.left(), center.y()), QPointF(box.right(), center.y()) };
    QPointF best = edges[0];
    qreal bestDot = -std::numeric_limits<qreal>::infinity();
    for (const QPointF& edge : edges)
    {
        const QPointF direction = edge - center;
        const qreal dot = QPointF::dotProduct(direction, m_screenUp);
        if (dot > bestDot)
        {
            bestDot = dot;
            best = edge;
        }
    }
    return best + m_screenUp * (ROTATION_HANDLE_DISTANCE * m_pixelSize);
}

PDFAppEditContentSelectionItem::Handle PDFAppEditContentSelectionItem::getHandleAt(const QPointF& scenePoint, qreal pixelSize) const
{
    using Helper = PDFAppEditContentSelectionItemHelper;

    if (m_handleBox.isNull() || !isVisible())
    {
        return Handle::None;
    }

    // The handles have a constant size on the screen; a hit has 3 px more around them
    const qreal reach = (HANDLE_SIZE * 0.5 + 3.0) * pixelSize;
    if (QLineF(scenePoint, getRotationHandlePoint()).length() <= reach)
    {
        return Handle::Rotation;
    }

    const QRectF box = Helper::grow(m_handleBox, Helper::getHandleOffset(m_isMultiple, pixelSize));
    for (const Handle handle : Helper::HANDLES)
    {
        const QPointF point = getHandlePoint(box, handle);
        if (isHandleShown(handle) && qAbs(point.x() - scenePoint.x()) <= reach && qAbs(point.y() - scenePoint.y()) <= reach)
        {
            return handle;
        }
    }
    return Handle::None;
}

int PDFAppEditContentSelectionItem::getFrameCount() const
{
    // The frames of the nodes and the common frame of more nodes (paint)
    return int(m_frames.size()) + (m_isMultiple ? 1 : 0);
}

void PDFAppEditContentSelectionItem::updateBoxes(const PDFAppEditContentContext* context)
{
    // The key of the cache: the selection (the nodes and the key object), the versions of the containers the boxes are
    // read from (the container of the nodes, the page and the containers of the isolation path; a command makes a new
    // version of every container it changes, undo and redo set another one again) and the isolation path. The key is
    // compared in O(n) (no box is computed); the boxes cost the walks of PDFAppEditContentGeometryHelper.
    const PDFAppEditContentSelection* selection = context->getSelection();
    const PDFContentEditSession* session = context->getSession();
    const std::vector<PDFContentNodeReference>& nodes = selection->getNodes();

    std::vector<PDFContentNodeReference> isolationPath;
    for (const PDFAppEditContentIsolationLevel& level : context->getIsolationPath())
    {
        isolationPath.push_back(level.root);
        isolationPath.push_back(level.entry);
    }

    std::vector<PDFContentContainerPointer> containers;
    containers.push_back(nodes.empty() ? PDFContentContainerPointer() : session->getContainer(nodes.front().container));
    containers.push_back(session->getContainer(context->getPageKey()));
    for (const PDFContentNodeReference& node : isolationPath)
    {
        containers.push_back(node.isValid() ? session->getContainer(node.container) : PDFContentContainerPointer());
    }

    bool isSameVersions = m_boxContainers.size() == containers.size();
    for (size_t i = 0; isSameVersions && i < containers.size(); ++i)
    {
        // The owners are compared (an expired version and a new one at the same address differ)
        const std::weak_ptr<const PDFContentContainer>& cached = m_boxContainers[i];
        isSameVersions = !cached.owner_before(containers[i]) && !containers[i].owner_before(cached) && cached.expired() == !containers[i];
    }

    if (m_isBoxesValid && isSameVersions && m_boxKeyNode == selection->getKeyNode() && m_boxIsolationPath == isolationPath && m_boxNodes == nodes)
    {
        return;
    }

    ++m_boxComputationCount;
    m_isBoxesValid = true;
    m_boxNodes = nodes;
    m_boxKeyNode = selection->getKeyNode();
    m_boxIsolationPath = std::move(isolationPath);
    m_boxContainers.assign(containers.cbegin(), containers.cend());
    m_boxes.clear();
    m_boxesUnion = QRectF();
    for (const PDFContentNodeReference& node : nodes)
    {
        const QRectF box = PDFAppEditContentGeometryHelper::getPageBoundingBox(context, node);
        if (!box.isNull())
        {
            m_boxes.push_back(Frame{ box, node == m_boxKeyNode && nodes.size() > 1 });
            m_boxesUnion = m_boxesUnion.isNull() ? box : m_boxesUnion.united(box);
        }
    }
}

void PDFAppEditContentSelectionItem::updateGeometry()
{
    using Helper = PDFAppEditContentSelectionItemHelper;

    prepareGeometryChange();
    m_frames.clear();
    m_handleBox = QRectF();
    m_lockedBox = QRectF();
    m_focusBox = QRectF();
    m_boundingRect = QRectF();

    PDFAppEditContentContext* context = m_context.data();
    const PDFAppEditContentScene* scene = dynamic_cast<const PDFAppEditContentScene*>(this->scene());
    if (!context || !scene)
    {
        update();
        return;
    }

    m_pixelSize = scene->getPixelSize();

    // "Up" of the screen in the scene (the view flips y and rotates by /Rotate)
    m_screenUp = QPointF(0.0, 1.0);
    const PDFAppEditContentView* view = context->getView();
    if (view && view->transform().isInvertible())
    {
        const QTransform inverted = view->transform().inverted();
        const QPointF up = inverted.map(QPointF(0.0, -1.0)) - inverted.map(QPointF(0.0, 0.0));
        const qreal length = std::hypot(up.x(), up.y());
        if (length > 0.0)
        {
            m_screenUp = up / length;
        }
    }

    // The boxes of the one policy (PDFAppEditContentGeometryHelper) from the cache (R6-13: computed once per change of
    // the selection or of the model), moved by a gesture in progress. Above the limit of the items of a gesture only the
    // common frame is painted (zadani_gui.md 4.24): the boxes are not even mapped one by one then.
    updateBoxes(context);
    const QTransform gesture = scene->isGestureActive() ? scene->getGestureTransform() : QTransform();
    m_isMultiple = m_boxes.size() > 1;
    if (m_boxes.size() > size_t(PDFAppEditContentScene::GESTURE_ITEM_LIMIT))
    {
        m_handleBox = gesture.mapRect(m_boxesUnion);
    }
    else
    {
        m_frames.reserve(m_boxes.size());
        for (const Frame& box : m_boxes)
        {
            m_frames.push_back(Frame{ gesture.mapRect(box.box), box.isKey });
            m_handleBox = m_handleBox.isNull() ? m_frames.back().box : m_handleBox.united(m_frames.back().box);
        }
    }

    m_lockedLabel.clear();
    if (context->getLockedClickNode().isValid())
    {
        const PDFContentNodeReference& locked = context->getLockedClickNode();
        m_lockedBox = PDFAppEditContentGeometryHelper::getPageBoundingBox(context, locked);
        const PDFContentContainerPointer container = context->getSession()->getContainer(locked.container);
        const PDFContentNodePointer node = container ? container->getNode(locked.node) : PDFContentNodePointer();
        m_lockedLabel = QCoreApplication::translate("pdf::PDFAppEditContentSelectionItem", "Locked · %1").arg(Helper::getKindName(node ? node->getKind() : PDFContentNodeKind::Opaque));
    }

    // The focus ring of the canvas (zadani_gui.md 4.23): around the selection, or around the paper
    if (view && view->hasFocus() && PDFWidgetStyle::isFocusRingVisible(view))
    {
        m_focusBox = !m_handleBox.isNull() ? m_handleBox : scene->getPaperRectangle();
    }

    const qreal margin = (FRAME_OFFSET * 2.0 + HANDLE_SIZE + ROTATION_HANDLE_DISTANCE + 8.0) * m_pixelSize;
    for (const QRectF& box : { m_handleBox, m_lockedBox, m_focusBox })
    {
        if (!box.isNull())
        {
            const QRectF grown = Helper::grow(box, margin);
            m_boundingRect = m_boundingRect.isNull() ? grown : m_boundingRect.united(grown);
        }
    }
    if (!m_lockedBox.isNull())
    {
        // The label of the locked object above its frame (its width on the screen in any direction of /Rotate)
        const QFontMetricsF metrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption));
        const qreal labelSize = qMax(40.0, metrics.horizontalAdvance(m_lockedLabel) + 20.0);
        m_boundingRect = m_boundingRect.united(Helper::grow(m_lockedBox, labelSize * m_pixelSize));
    }
    update();
}

void PDFAppEditContentSelectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    using Helper = PDFAppEditContentSelectionItemHelper;

    Q_UNUSED(option);
    Q_UNUSED(widget);

    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);

    // The frame of each object 3 px outside, the key object of more objects thicker (zadani_gui.md 4.15)
    const qreal offset = FRAME_OFFSET * m_pixelSize;
    for (const Frame& frame : m_frames)
    {
        painter->setPen(Helper::createPen(PDFThemeColors::BorderFocus, frame.isKey ? 2.0 : 1.0));
        painter->drawRect(Helper::grow(frame.box, offset));
    }

    // The common frame of more objects
    if (m_isMultiple)
    {
        painter->setPen(Helper::createPen(PDFThemeColors::BorderFocus, 1.0, Qt::DashLine));
        painter->drawRect(Helper::grow(m_handleBox, Helper::getHandleOffset(true, m_pixelSize)));
    }

    // The handles (a constant size on the screen) and the round rotation handle
    if (!m_handleBox.isNull())
    {
        const QRectF box = Helper::grow(m_handleBox, Helper::getHandleOffset(m_isMultiple, m_pixelSize));
        const qreal half = HANDLE_SIZE * 0.5 * m_pixelSize;
        const QPointF rotation = getRotationHandlePoint();

        painter->setPen(Helper::createPen(PDFThemeColors::BorderFocus, 1.0));
        QPointF rotationBase = getHandlePoint(box, Handle::Top);
        qreal bestDistance = std::numeric_limits<qreal>::infinity();
        for (const Handle handle : { Handle::Top, Handle::Bottom, Handle::Left, Handle::Right })
        {
            const QPointF point = getHandlePoint(box, handle);
            const qreal distance = QLineF(point, rotation).length();
            if (distance < bestDistance)
            {
                bestDistance = distance;
                rotationBase = point;
            }
        }
        painter->drawLine(rotationBase, rotation);

        painter->setBrush(Helper::getColor(PDFThemeColors::BgSurface));
        for (const Handle handle : Helper::HANDLES)
        {
            if (isHandleShown(handle))
            {
                const QPointF point = getHandlePoint(box, handle);
                painter->drawRect(QRectF(point.x() - half, point.y() - half, 2.0 * half, 2.0 * half));
            }
        }
        painter->drawEllipse(rotation, half, half);
        painter->setBrush(Qt::NoBrush);
    }

    // A click on a locked object: a gray frame with its label (-locked-click)
    if (!m_lockedBox.isNull())
    {
        const QRectF frame = Helper::grow(m_lockedBox, offset);
        painter->setPen(Helper::createPen(PDFThemeColors::BorderStrong, 1.0));
        painter->drawRect(frame);

        // The label on a tab above the frame, in the coordinates of the device (the view flips and turns the scene)
        const QRectF device = painter->worldTransform().mapRect(frame);
        PDFPainterStateGuard labelGuard(painter);
        painter->resetTransform();
        const QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption);
        const QFontMetricsF metrics(font);
        const QRectF label(device.left(), device.top() - metrics.height() - 4.0, metrics.horizontalAdvance(m_lockedLabel) + 12.0, metrics.height() + 4.0);
        painter->setPen(Helper::createPen(PDFThemeColors::BorderStrong, 1.0));
        painter->setBrush(Helper::getColor(PDFThemeColors::BgSurface));
        painter->drawRect(label);
        painter->setFont(font);
        painter->setPen(Helper::getColor(PDFThemeColors::TextSecondary));
        painter->drawText(label, Qt::AlignCenter, m_lockedLabel);
    }

    // The focus ring of the canvas, when the keyboard has moved the focus here
    if (!m_focusBox.isNull())
    {
        painter->setPen(Helper::createPen(PDFThemeColors::BorderFocus, 2.0));
        painter->drawRect(Helper::grow(m_focusBox, 6.0 * m_pixelSize));
    }
}

}   // namespace pdf

#include "moc_pdfappeditcontentselection.cpp"
