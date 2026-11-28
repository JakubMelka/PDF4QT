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

#include "pdfappeditcontentitems.h"

#include "pdfappeditcontentcontext.h"
#include "pdfappeditcontentregistry.h"
#include "pdfappeditcontentscene.h"
#include "pdfapptoolhost.h"
#include "pdfblendfunction.h"
#include "pdfdocumenttoolgui.h"
#include "pdfdocumenttoolrunner.h"
#include "pdfpainterutils.h"
#include "pdfthemecolors.h"
#include "pdfthememanager.h"

#include <QBrush>
#include <QGradient>
#include <QPaintDevice>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QtGlobal>
#include <QtMath>

#include <cmath>
#include <tuple>

namespace pdf
{

// Stage E1, package P11, fix package FX2: the items of the canvas. Every item draws in the default space of its
// container (the item coordinates; a form item maps the space of its form into the space of its use). Bounds: an item
// draws the data of one node - a path its segments, a text its glyphs (the lines are built at their first paint, only
// the lines exposed), an image one picture of the scene (at most 4 megapixels, shared by the uses of the image); a
// paint or a hit intersects at most PDFAppEditContentClipNode::MAXIMUM_EXACT_CLIPS clip paths exactly and walks up at
// most the nesting of the forms (PDFContentEditSession::MAXIMUM_CONTAINER_NESTING); the tile of a pattern repeats its
// cell at most MAXIMUM_TILE_REPEATS times per axis.

/// Helpers of this file (static functions only)
class PDFAppEditContentItemsHelper
{
public:
    PDFAppEditContentItemsHelper() = delete;

    /// Most copies of a pattern cell per axis drawn into one tile (a cell larger than its steps overlaps its
    /// neighbors); more: the pattern is drawn as the placeholder (Preview draws it)
    static constexpr int MAXIMUM_TILE_REPEATS = 8;

    /// Most pixels of the tile of a pattern
    static constexpr qreal MAXIMUM_TILE_PIXELS = 4000000.0;

    /// A new serial of an item (GUI thread only)
    static quint64 allocateSerial()
    {
        static quint64 serial = 0;
        return ++serial;
    }

    /// The color of a token of the active theme
    static QColor getThemeColor(PDFThemeColors::Color color)
    {
        return PDFThemeManager::getInstance()->getColor(color);
    }

    /// The display context of the scene of the arguments (the default one without a scene)
    static PDFContentDisplayContext getDisplayContext(const PDFAppEditContentItemArguments& arguments)
    {
        return arguments.scene ? arguments.scene->getDisplayContext() : PDFContentDisplayContext();
    }

    /// The display context of the scene of an item
    static PDFContentDisplayContext getDisplayContext(const QGraphicsItem* item)
    {
        const PDFAppEditContentScene* scene = item ? dynamic_cast<const PDFAppEditContentScene*>(item->scene()) : nullptr;
        return scene ? scene->getDisplayContext() : PDFContentDisplayContext();
    }

    /// The box of a node by the display query of the core (empty, when none is known)
    static QRectF getDisplayBox(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context)
    {
        const PDFContentDisplayBox box = PDFContentDisplay::getBoundingBox(container, id, context);
        return box.isValid ? box.box : QRectF();
    }

    /// A small margin of the bounding rectangles in the units of the item (antialiasing, a cosmetic line)
    static constexpr qreal BOUNDING_MARGIN = 2.0;

    /// The pen of the blue dashed outline of a clipping path (cosmetic)
    static QPen createClipOutlinePen()
    {
        QPen pen(getThemeColor(PDFThemeColors::AccentBlueBorderStrong));
        pen.setCosmetic(true);
        pen.setWidthF(1.0);
        pen.setStyle(Qt::DashLine);
        return pen;
    }

    /// The scale of a matrix (the square root of the absolute determinant)
    static qreal getScale(const QTransform& matrix)
    {
        return std::sqrt(std::abs(matrix.determinant()));
    }

    /// The item of a form use around \p item (its parent, when it is one), nullptr else
    static const PDFAppEditContentItem* getFormParent(const QGraphicsItem* item)
    {
        const QGraphicsItem* parent = item->parentItem();
        const PDFAppEditContentItem* parentItem = parent ? qgraphicsitem_cast<const PDFAppEditContentItem*>(parent) : nullptr;
        return parentItem && parentItem->getNodeKind() == PDFContentNodeKind::FormUse ? parentItem : nullptr;
    }

    /// The device pixel ratio of the device of the painter
    static qreal getDevicePixelRatio(const QPainter* painter)
    {
        const QPaintDevice* device = painter ? painter->device() : nullptr;
        return device ? device->devicePixelRatioF() : 1.0;
    }

    /// The gradient of an axial or a radial shading in the shading space
    static QGradient createGradient(const PDFContentDisplayShading& shading)
    {
        QGradient gradient = shading.kind == PDFContentDisplayShading::Kind::Axial ? QGradient(QLinearGradient(shading.start, shading.end))
                                                                                   : QGradient(QRadialGradient(shading.end, shading.endRadius, shading.start, shading.startRadius));
        for (const PDFContentDisplayShading::Stop& stop : shading.stops)
        {
            gradient.setColorAt(qBound(0.0, stop.position, 1.0), stop.color);
        }
        gradient.setSpread(QGradient::PadSpread);
        return gradient;
    }

    /// The transform of the pixels of a picture onto a rectangle (row 0 is its top, the larger y)
    static QTransform getPictureToRectangle(const QImage& picture, const QRectF& rectangle)
    {
        if (picture.isNull())
        {
            return QTransform();
        }
        return QTransform(rectangle.width() / picture.width(), 0.0, 0.0, -rectangle.height() / picture.height(), rectangle.left(), rectangle.bottom());
    }
};

// ---------------------------------------------------------------- PDFAppEditContentClipNode

PDFAppEditContentClipNode::PDFAppEditContentClipNode(std::shared_ptr<const PDFAppEditContentClipNode> parent, QPainterPath path) :
    m_parent(std::move(parent)),
    m_path(std::move(path))
{
    const QRectF bounds = m_path.boundingRect();
    m_bounds = m_parent ? m_parent->m_bounds.intersected(bounds) : bounds;
    m_length = m_parent ? m_parent->m_length + 1 : 1;
}

PDFAppEditContentClipNode::~PDFAppEditContentClipNode()
{
    // A chain of 100 000 clips would be released recursively (one frame per clip): the parents, which only this node
    // holds, are released one by one here instead
    std::shared_ptr<const PDFAppEditContentClipNode> parent = std::move(m_parent);
    while (parent && parent.use_count() == 1)
    {
        std::shared_ptr<const PDFAppEditContentClipNode> next = std::move(parent->m_parent);
        parent = std::move(next);
    }
}

void PDFAppEditContentClipNode::apply(QPainter* painter, int* budget) const
{
    for (const PDFAppEditContentClipNode* node = this; node; node = node->m_parent.get())
    {
        const Qt::ClipOperation operation = painter->hasClipping() ? Qt::IntersectClip : Qt::ReplaceClip;
        if (*budget <= 0)
        {
            // The rest of the chain by the intersection of its bounding rectangles (one rectangle, the chain is cut here)
            painter->setClipRect(node->m_bounds, operation);
            return;
        }
        painter->setClipPath(node->m_path, operation);
        --*budget;
    }
}

bool PDFAppEditContentClipNode::contains(const QPointF& point, int* budget) const
{
    for (const PDFAppEditContentClipNode* node = this; node; node = node->m_parent.get())
    {
        if (*budget <= 0)
        {
            return node->m_bounds.contains(point);
        }
        if (!node->m_path.contains(point))
        {
            return false;
        }
        --*budget;
    }
    return true;
}

// ---------------------------------------------------------------- PDFAppEditContentPictureKey

bool PDFAppEditContentPictureKey::operator==(const PDFAppEditContentPictureKey& other) const
{
    return xobject == other.xobject && container == other.container && node == other.node && maskColor == other.maskColor;
}

bool PDFAppEditContentPictureKey::operator<(const PDFAppEditContentPictureKey& other) const
{
    if (xobject != other.xobject)
    {
        return xobject < other.xobject;
    }
    if (container != other.container)
    {
        return container < other.container;
    }
    if (node != other.node)
    {
        return node < other.node;
    }
    return maskColor < other.maskColor;
}

// ---------------------------------------------------------------- PDFAppEditContentItem

PDFAppEditContentItem::PDFAppEditContentItem(PDFContentNodeId nodeId, PDFContentNodeKind kind, PDFAppEditContentItem* parent) :
    QGraphicsItem(parent),
    m_nodeId(nodeId),
    m_nodeKind(kind),
    m_serial(PDFAppEditContentItemsHelper::allocateSerial())
{
    // No events, no selection, no movement (zadani_gui.md 4.14)
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    setAcceptDrops(false);
}

PDFAppEditContentItem::~PDFAppEditContentItem() = default;

bool PDFAppEditContentItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    Q_UNUSED(node);
    Q_UNUSED(container);
    return false;
}

void PDFAppEditContentItem::setEditorState(bool isLocked, bool isHidden)
{
    if (m_isLocked == isLocked && m_isHidden == isHidden)
    {
        return;
    }

    m_isLocked = isLocked;
    m_isHidden = isHidden;
    update();
}

void PDFAppEditContentItem::setDimmed(bool isDimmed)
{
    if (m_isDimmed == isDimmed)
    {
        return;
    }

    m_isDimmed = isDimmed;
    updateOpacity();
}

void PDFAppEditContentItem::setBaseOpacity(qreal opacity)
{
    m_baseOpacity = qBound(0.0, opacity, 1.0);
    updateOpacity();
}

void PDFAppEditContentItem::updateOpacity()
{
    // R6-15: the dimming multiplies the constant alpha of the content, it never replaces it
    setOpacity(m_baseOpacity * (m_isDimmed ? 0.35 : 1.0));
}

void PDFAppEditContentItem::clearCache()
{
}

bool PDFAppEditContentItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    // The bounding rectangle under the clips in effect (R6-9: a part cut away by a clip is not hit)
    if (!isVisible())
    {
        return false;
    }

    const QPointF point = mapFromScene(scenePoint);
    const qreal itemTolerance = getItemTolerance(tolerance);
    const QRectF rectangle = boundingRect();
    if (isHiddenInEditor())
    {
        QPainterPath outline;
        outline.addRect(rectangle);
        return isHitByOutline(outline, point, itemTolerance) && isInsideClipPaths(point);
    }
    return rectangle.adjusted(-itemTolerance, -itemTolerance, itemTolerance, itemTolerance).contains(point) && isInsideClipPaths(point);
}

void PDFAppEditContentItem::setClipPaths(PDFAppEditContentClipPathsPointer clipPaths)
{
    if (m_clipPaths == clipPaths)
    {
        return;
    }

    m_clipPaths = std::move(clipPaths);
    update();
}

void PDFAppEditContentItem::setBaseTransform(const QTransform& transform)
{
    m_baseTransform = transform;
    updateTransform();
}

void PDFAppEditContentItem::setGestureTransform(const QTransform& transform)
{
    m_gestureTransform = transform;
    updateTransform();
}

void PDFAppEditContentItem::updateTransform()
{
    // A point of the item -> its container (base) -> moved by the gesture in the space of the parent
    setTransform(m_baseTransform * m_gestureTransform);
}

PDFAppEditContentScene* PDFAppEditContentItem::getContentScene() const
{
    return dynamic_cast<PDFAppEditContentScene*>(scene());
}

void PDFAppEditContentItem::applyClipPaths(QPainter* painter) const
{
    // The chain of an item is in the coordinates of its parent; the chains of the form uses around it apply too (R6-7),
    // each in the coordinates of its own parent - one budget of exact paths for all of them. Bounded by the nesting of
    // the forms (the parent of a form item of the page is the content root, no form).
    const QTransform itemToDevice = painter->worldTransform();
    QTransform toDevice = itemToDevice;
    int budget = PDFAppEditContentClipNode::MAXIMUM_EXACT_CLIPS;
    bool isTransformChanged = false;
    for (const PDFAppEditContentItem* current = this; current; current = PDFAppEditContentItemsHelper::getFormParent(current))
    {
        const QTransform& transform = current->transform();
        if (!transform.isIdentity())
        {
            if (!transform.isInvertible())
            {
                break;
            }
            toDevice = transform.inverted() * toDevice;
        }

        if (current->m_clipPaths)
        {
            painter->setWorldTransform(toDevice);
            isTransformChanged = true;
            current->m_clipPaths->apply(painter, &budget);
        }
    }

    if (isTransformChanged)
    {
        painter->setWorldTransform(itemToDevice);
    }
}

bool PDFAppEditContentItem::isInsideClipPaths(const QPointF& itemPoint) const
{
    QPointF point = itemPoint;
    int budget = PDFAppEditContentClipNode::MAXIMUM_EXACT_CLIPS;
    for (const PDFAppEditContentItem* current = this; current; current = PDFAppEditContentItemsHelper::getFormParent(current))
    {
        // Into the coordinates of the parent, where the chain of the item is
        point = current->transform().map(point);
        if (current->m_clipPaths && !current->m_clipPaths->contains(point, &budget))
        {
            return false;
        }
    }
    return true;
}

qreal PDFAppEditContentItem::getItemTolerance(qreal sceneTolerance) const
{
    const qreal scale = PDFAppEditContentItemsHelper::getScale(sceneTransform());
    return scale > 0.0 ? sceneTolerance / scale : sceneTolerance;
}

void PDFAppEditContentItem::paintHiddenOutline(QPainter* painter, const QPainterPath& outline) const
{
    QPen pen(PDFAppEditContentItemsHelper::getThemeColor(PDFThemeColors::BorderStrong));
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    pen.setStyle(Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(outline);
}

bool PDFAppEditContentItem::isHitByOutline(const QPainterPath& outline, const QPointF& itemPoint, qreal itemTolerance) const
{
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * itemTolerance);
    return stroker.createStroke(outline).contains(itemPoint);
}

// ---------------------------------------------------------------- PDFAppEditContentDisplayAccess

std::optional<PDFContentDisplayState> PDFAppEditContentDisplayAccess::getDisplayState(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context)
{
    return PDFContentDisplay::getDisplayState(container, id, context);
}

PDFContentDisplayTextPointer PDFAppEditContentDisplayAccess::getText(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context)
{
    return PDFContentDisplay::getText(container, id, context);
}

PDFContentDisplayCounts PDFAppEditContentDisplayAccess::getCounts(const PDFContentContainer& container)
{
    return PDFContentDisplay::getCounts(container);
}

QPainterPath PDFAppEditContentDisplayAccess::createPath(const PDFContentPath& path, Qt::FillRule fillRule)
{
    // One element per segment of the core (the number of the segments bounds it)
    QPainterPath result;
    result.setFillRule(fillRule);
    for (const PDFContentPath::Segment& segment : path.getSegments())
    {
        switch (segment.type)
        {
            case PDFContentPath::SegmentType::MoveTo:
                result.moveTo(segment.points[0]);
                break;

            case PDFContentPath::SegmentType::LineTo:
                result.lineTo(segment.points[0]);
                break;

            case PDFContentPath::SegmentType::CurveTo:
                result.cubicTo(segment.points[0], segment.points[1], segment.points[2]);
                break;

            case PDFContentPath::SegmentType::Close:
                result.closeSubpath();
                break;
        }
    }
    return result;
}

QPen PDFAppEditContentDisplayAccess::createPen(const PDFContentDisplayState& state, const QColor& color)
{
    // As the renderer of the project (PDFPainterHelper::createPenFromState): the values of the state in the space of the
    // node; the line width 0 is the thinnest line of the device; the dash array by the core (item 7 of the fix package)
    QPen pen(color);
    pen.setWidthF(state.lineWidth);
    pen.setCosmetic(state.lineWidth <= 0.0);
    pen.setCapStyle(state.lineCap);
    pen.setJoinStyle(state.lineJoin);
    pen.setMiterLimit(state.miterLimit);
    if (state.dashPattern.isSolid() || state.lineWidth <= 0.0)
    {
        pen.setStyle(Qt::SolidLine);
    }
    else
    {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern(state.dashPattern.createForQPen(pen.widthF()));
        pen.setDashOffset(state.dashPattern.getDashOffset());
    }
    return pen;
}

QColor PDFAppEditContentDisplayAccess::getColor(const PDFContentDisplayColor& color, PDFReal alpha)
{
    // A color, which the canvas cannot convert or a pattern without a color of its own: a neutral gray of the document
    // (the cards say so; Preview shows the exact result)
    QColor result = color.color.isValid() ? color.color : QColor(128, 128, 128);
    result.setAlphaF(qBound(0.0, alpha, 1.0));
    return result;
}

QPainter::CompositionMode PDFAppEditContentDisplayAccess::getCompositionMode(BlendMode blendMode)
{
    // The separable blend modes QPainter has (PDFBlendModeInfo of the core, exported since the fix package FX1); the
    // others are drawn as Normal (Preview shows them)
    if (!PDFBlendModeInfo::isSupportedByQt(blendMode))
    {
        return QPainter::CompositionMode_SourceOver;
    }
    return PDFBlendModeInfo::getCompositionModeFromBlendMode(blendMode);
}

PDFDocumentPointer PDFAppEditContentDisplayAccess::getDocument(const PDFAppEditContentContext* context)
{
    // The shared document of the context (item 6 of the fix package): a worker holds it, also without a GUI
    return context ? context->getDocumentPointer() : PDFDocumentPointer();
}

PDFCMSPointer PDFAppEditContentDisplayAccess::getColorManagement(const PDFAppEditContentContext* context)
{
    const PDFDocumentToolGUI* gui = context ? context->getGUI() : nullptr;
    PDFAppToolHost* host = gui ? gui->getHost() : nullptr;
    return host ? host->createEnvironment().cms : PDFCMSPointer();
}

qint64 PDFAppEditContentDisplayAccess::getPixelStep(qreal pixels)
{
    // Powers of 4 (the double of the width and of the height), so a zoom changes the step seldom
    const qint64 maximum = PDFContentDisplayImageOptions().maximumPixels;
    qint64 step = 4096;
    while (step < maximum && qreal(step) < pixels)
    {
        step *= 4;
    }
    return qMin(step, maximum);
}

qreal PDFAppEditContentDisplayAccess::getDevicePixels(const QRectF& rectangle, const QTransform& transform)
{
    const qreal pixels = std::abs(transform.determinant()) * std::abs(rectangle.width() * rectangle.height());
    return std::isfinite(pixels) ? pixels : 0.0;
}

PDFAppEditContentPatternPaint PDFAppEditContentDisplayAccess::createPatternPaint(const PDFContentDisplayTilingCell& cell, const PDFContentDisplayShading& shading, qint64 pixels)
{
    using Helper = PDFAppEditContentItemsHelper;

    PDFAppEditContentPatternPaint paint;
    paint.isDone = true;
    paint.pixels = pixels;

    if (cell.status == PDFContentDisplayTilingCell::Status::Done && !cell.picture.isNull())
    {
        // A tiling pattern (ISO 32000-2, 8.7.3.1): the cell /BBox repeated by /XStep and /YStep in the pattern space. One
        // tile of the size of the steps gets every copy of the cell, which reaches into it (a cell larger than its steps
        // overlaps its neighbors); the texture brush repeats the tile.
        const QRectF cellRectangle = cell.cellRectangle;
        const qreal xStep = std::abs(cell.xStep);
        const qreal yStep = std::abs(cell.yStep);
        if (!(xStep > 0.0) || !(yStep > 0.0) || cellRectangle.isEmpty())
        {
            return paint;
        }

        const qreal unitsX = cell.picture.width() / cellRectangle.width();
        const qreal unitsY = cell.picture.height() / cellRectangle.height();
        const qreal tileWidth = std::ceil(xStep * unitsX);
        const qreal tileHeight = std::ceil(yStep * unitsY);
        const qreal repeatsX = std::ceil(cellRectangle.width() / xStep) + 1.0;
        const qreal repeatsY = std::ceil(cellRectangle.height() / yStep) + 1.0;
        if (!(tileWidth >= 1.0) || !(tileHeight >= 1.0) || tileWidth * tileHeight > Helper::MAXIMUM_TILE_PIXELS ||
            repeatsX > Helper::MAXIMUM_TILE_REPEATS || repeatsY > Helper::MAXIMUM_TILE_REPEATS)
        {
            return paint;
        }

        QImage tile(int(tileWidth), int(tileHeight), QImage::Format_ARGB32_Premultiplied);
        tile.fill(Qt::transparent);
        const qreal scaleX = tileWidth / xStep;
        const qreal scaleY = tileHeight / yStep;
        const qreal left = cellRectangle.left();
        const qreal bottom = cellRectangle.top();      // the smaller y of the pattern space
        {
            QPainter tilePainter(&tile);
            tilePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            const int countX = int(repeatsX);
            const int countY = int(repeatsY);
            for (int i = -countX; i <= countX; ++i)
            {
                for (int j = -countY; j <= countY; ++j)
                {
                    // The copy (i, j) of the cell in the pixels of the tile (row 0 = the larger y of the tile)
                    const qreal x = (cellRectangle.left() + i * xStep - left) * scaleX;
                    const qreal y = (bottom + yStep - (cellRectangle.bottom() + j * yStep)) * scaleY;
                    const QRectF target(x, y, cellRectangle.width() * scaleX, cellRectangle.height() * scaleY);
                    if (target.intersects(QRectF(0.0, 0.0, tileWidth, tileHeight)))
                    {
                        tilePainter.drawImage(target, cell.picture);
                    }
                }
            }
        }

        // The pixels of the tile -> the pattern space -> the default space of the container (/Matrix)
        const QTransform tileToPattern(1.0 / scaleX, 0.0, 0.0, -1.0 / scaleY, left, bottom + yStep);
        paint.brush = QBrush(tile);
        paint.brush.setTransform(tileToPattern * cell.matrix);
        paint.isPainted = true;
        return paint;
    }

    if (shading.status == PDFContentDisplayShading::Status::Done)
    {
        // A shading pattern (8.7.4): a gradient in the shading space, or the picture of the other types over its rectangle
        if (shading.kind == PDFContentDisplayShading::Kind::Picture)
        {
            if (shading.picture.isNull())
            {
                return paint;
            }
            paint.brush = QBrush(shading.picture);
            paint.brush.setTransform(Helper::getPictureToRectangle(shading.picture, shading.pictureRectangle));
        }
        else
        {
            paint.brush = QBrush(Helper::createGradient(shading));
            paint.brush.setTransform(shading.matrix);
            paint.backgroundColor = shading.backgroundColor;
        }
        paint.isPainted = true;
    }
    return paint;
}

// ---------------------------------------------------------------- PDFAppEditContentPathItem

PDFAppEditContentPathItem::PDFAppEditContentPathItem(const PDFAppEditContentItemArguments& arguments) :
    PDFAppEditContentItem(arguments.node->getId(), arguments.node->getKind(), arguments.parent)
{
    setData(*arguments.node, *arguments.container, PDFAppEditContentItemsHelper::getDisplayContext(arguments));
}

PDFAppEditContentPathItem::~PDFAppEditContentPathItem() = default;

void PDFAppEditContentPathItem::setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context)
{
    prepareGeometryChange();

    const PDFContentNode::Data& data = node.getData();
    m_containerKey = container.getKey();
    m_paint = data.paint;
    m_state = PDFAppEditContentDisplayAccess::getDisplayState(container, node.getId(), context);
    m_matrix = m_state ? m_state->matrix : QTransform();
    m_path = PDFAppEditContentDisplayAccess::createPath(data.path, m_paint.fillEvenOdd ? Qt::OddEvenFill : Qt::WindingFill);
    if (m_paint.closeBeforePaint)
    {
        m_path.closeSubpath();
    }
    m_containerPath = m_matrix.map(m_path);
    m_containerPath.setFillRule(m_path.fillRule());

    // The box of the core (with the stroke); the points of the path without it
    QRectF box = PDFAppEditContentItemsHelper::getDisplayBox(container, node.getId(), context);
    if (box.isNull())
    {
        box = m_containerPath.controlPointRect();
    }
    constexpr qreal margin = PDFAppEditContentItemsHelper::BOUNDING_MARGIN;
    m_boundingRect = box.adjusted(-margin, -margin, margin, margin);
    m_strokeShape = QPainterPath();
    m_strokeShapeWidth = -1.0;

    // A new version: the paints of its patterns are made again (the pattern or the matrix may be another one)
    m_fillPattern = PDFAppEditContentPatternPaint();
    m_strokePattern = PDFAppEditContentPatternPaint();
    m_requestedPixels[0] = 0;
    m_requestedPixels[1] = 0;
}

QRectF PDFAppEditContentPathItem::boundingRect() const
{
    return m_boundingRect;
}

QPainterPath PDFAppEditContentPathItem::shape() const
{
    return m_containerPath;
}

bool PDFAppEditContentPathItem::hasPattern(bool isStroke) const
{
    if (!m_state)
    {
        return false;
    }
    const PDFContentDisplayColor& color = isStroke ? m_state->strokeColor : m_state->fillColor;
    return color.kind == PDFContentDisplayColor::Kind::Pattern && (isStroke ? m_paint.stroke : m_paint.fill);
}

void PDFAppEditContentPathItem::setPatternPaint(bool isStroke, const PDFAppEditContentPatternPaint& paint)
{
    (isStroke ? m_strokePattern : m_fillPattern) = paint;
    update();
}

void PDFAppEditContentPathItem::requestPattern(bool isStroke, qreal pixels)
{
    // One request per zoom step: a larger picture is asked for when the zoom needs it, the old one is drawn meanwhile
    const qint64 step = PDFAppEditContentDisplayAccess::getPixelStep(pixels);
    qint64& requested = m_requestedPixels[isStroke ? 1 : 0];
    if (requested >= step)
    {
        return;
    }

    requested = step;
    if (PDFAppEditContentScene* scene = getContentScene())
    {
        scene->requestDecoding(this, isStroke ? PDFAppEditContentDecodeKind::StrokePattern : PDFAppEditContentDecodeKind::FillPattern, step);
    }
}

void PDFAppEditContentPathItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    PDFPainterStateGuard guard(painter);
    applyClipPaths(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (isHiddenInEditor())
    {
        paintHiddenOutline(painter, m_containerPath);
        return;
    }

    if (m_state && isPainted())
    {
        PDFPainterStateGuard contentGuard(painter);
        painter->setCompositionMode(PDFAppEditContentDisplayAccess::getCompositionMode(m_state->blendMode));
        const qreal devicePixelRatio = PDFAppEditContentItemsHelper::getDevicePixelRatio(painter);
        const qreal pixels = PDFAppEditContentDisplayAccess::getDevicePixels(m_boundingRect, painter->worldTransform()) * devicePixelRatio * devicePixelRatio;
        const QTransform containerToDevice = painter->worldTransform();

        if (m_paint.fill)
        {
            if (hasPattern(false))
            {
                // A pattern fill: its brush is in the default space of the container (item 2 of the fix package)
                requestPattern(false, pixels);
                if (m_fillPattern.isPainted)
                {
                    if (m_fillPattern.backgroundColor.isValid())
                    {
                        painter->fillPath(m_containerPath, m_fillPattern.backgroundColor);
                    }
                    painter->setOpacity(painter->opacity() * qBound(0.0, m_state->alphaFilling, 1.0));
                    painter->fillPath(m_containerPath, m_fillPattern.brush);
                    painter->setOpacity(1.0);
                }
                else
                {
                    painter->fillPath(m_containerPath, PDFAppEditContentDisplayAccess::getColor(m_state->fillColor, m_state->alphaFilling));
                }
            }
            else
            {
                painter->setWorldTransform(m_matrix * containerToDevice);
                painter->fillPath(m_path, PDFAppEditContentDisplayAccess::getColor(m_state->fillColor, m_state->alphaFilling));
            }
        }
        if (m_paint.stroke)
        {
            painter->setWorldTransform(m_matrix * containerToDevice);
            QPen pen = PDFAppEditContentDisplayAccess::createPen(*m_state, PDFAppEditContentDisplayAccess::getColor(m_state->strokeColor, m_state->alphaStroking));
            if (hasPattern(true))
            {
                requestPattern(true, pixels);
                if (m_strokePattern.isPainted && m_matrix.isInvertible())
                {
                    // The brush of the pen in the space of the node (the painter draws the stroke there)
                    QBrush brush = m_strokePattern.brush;
                    brush.setTransform(brush.transform() * m_matrix.inverted());
                    pen.setBrush(brush);
                    painter->setOpacity(painter->opacity() * qBound(0.0, m_state->alphaStroking, 1.0));
                }
            }
            painter->strokePath(m_path, pen);
        }
    }

    // The path also clips: the blue dashed outline with View > Clipping paths
    const PDFAppEditContentScene* scene = getContentScene();
    if (m_paint.clip != PDFContentPathPaint::Clip::None && scene && scene->isClippingPathShown())
    {
        painter->setPen(PDFAppEditContentItemsHelper::createClipOutlinePen());
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(m_containerPath);
    }
}

bool PDFAppEditContentPathItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    if (node.getKind() != PDFContentNodeKind::Path || node.getData().paint.isClippingPath())
    {
        return false;
    }

    setData(node, container, PDFAppEditContentItemsHelper::getDisplayContext(this));
    update();
    return true;
}

bool PDFAppEditContentPathItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    if (!isVisible())
    {
        return false;
    }

    const QPointF point = mapFromScene(scenePoint);
    const qreal itemTolerance = getItemTolerance(tolerance);
    if (!m_boundingRect.adjusted(-itemTolerance, -itemTolerance, itemTolerance, itemTolerance).contains(point) || !isInsideClipPaths(point))
    {
        return false;
    }

    // Hidden: only a band around its outline (R6-17); painting switched off: no shape (item 8 - the tree selects it)
    if (isHiddenInEditor())
    {
        return isHitByOutline(m_containerPath, point, itemTolerance);
    }
    if (!isPainted())
    {
        return false;
    }

    // By the fill, or by a band around the outline at least as wide as the tolerance (zadani_gui.md 4.15)
    if (m_paint.fill && m_containerPath.contains(point))
    {
        return true;
    }

    qreal strokeWidth = 2.0 * itemTolerance;
    if (m_paint.stroke && m_state)
    {
        strokeWidth = qMax(strokeWidth, m_state->lineWidth * PDFAppEditContentItemsHelper::getScale(m_matrix));
    }
    if (!qFuzzyCompare(m_strokeShapeWidth, strokeWidth))
    {
        QPainterPathStroker stroker;
        stroker.setWidth(strokeWidth);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        m_strokeShape = stroker.createStroke(m_containerPath);
        m_strokeShapeWidth = strokeWidth;
    }
    return m_strokeShape.contains(point);
}

void PDFAppEditContentPathItem::clearCache()
{
    // The theme or the device pixel ratio changed: the paths of the hit and the pictures of the patterns are made again
    m_strokeShape = QPainterPath();
    m_strokeShapeWidth = -1.0;
    m_fillPattern = PDFAppEditContentPatternPaint();
    m_strokePattern = PDFAppEditContentPatternPaint();
    m_requestedPixels[0] = 0;
    m_requestedPixels[1] = 0;
    update();
}

// ---------------------------------------------------------------- PDFAppEditContentClipPathItem

PDFAppEditContentClipPathItem::PDFAppEditContentClipPathItem(const PDFAppEditContentItemArguments& arguments) :
    PDFAppEditContentItem(arguments.node->getId(), arguments.node->getKind(), arguments.parent)
{
    setData(*arguments.node, *arguments.container, PDFAppEditContentItemsHelper::getDisplayContext(arguments));
}

PDFAppEditContentClipPathItem::~PDFAppEditContentClipPathItem() = default;

void PDFAppEditContentClipPathItem::setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context)
{
    prepareGeometryChange();

    const std::optional<PDFContentDisplayState> state = PDFAppEditContentDisplayAccess::getDisplayState(container, node.getId(), context);
    const PDFContentNode::Data& data = node.getData();
    const QPainterPath path = PDFAppEditContentDisplayAccess::createPath(data.path, data.paint.clip == PDFContentPathPaint::Clip::EvenOdd ? Qt::OddEvenFill : Qt::WindingFill);
    m_containerPath = state ? state->matrix.map(path) : path;
    m_containerPath.setFillRule(path.fillRule());

    constexpr qreal margin = PDFAppEditContentItemsHelper::BOUNDING_MARGIN;
    m_boundingRect = m_containerPath.controlPointRect().adjusted(-margin, -margin, margin, margin);
}

QRectF PDFAppEditContentClipPathItem::boundingRect() const
{
    return m_boundingRect;
}

void PDFAppEditContentClipPathItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    // Only with View > Clipping paths: the one blue dashed outline of the tool (zadani_gui.md 4.15)
    const PDFAppEditContentScene* scene = getContentScene();
    if (!scene || !scene->isClippingPathShown())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    applyClipPaths(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(PDFAppEditContentItemsHelper::createClipOutlinePen());
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(m_containerPath);
}

bool PDFAppEditContentClipPathItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    if (node.getKind() != PDFContentNodeKind::Path || !node.getData().paint.isClippingPath())
    {
        return false;
    }

    setData(node, container, PDFAppEditContentItemsHelper::getDisplayContext(this));
    update();
    return true;
}

bool PDFAppEditContentClipPathItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    // A clipping path is hit only while it is shown, by a band around its outline
    const PDFAppEditContentScene* scene = getContentScene();
    if (!isVisible() || !scene || !scene->isClippingPathShown())
    {
        return false;
    }

    const QPointF point = mapFromScene(scenePoint);
    return isHitByOutline(m_containerPath, point, getItemTolerance(tolerance));
}

// ---------------------------------------------------------------- PDFAppEditContentTextItem

PDFAppEditContentTextItem::PDFAppEditContentTextItem(const PDFAppEditContentItemArguments& arguments) :
    PDFAppEditContentItem(arguments.node->getId(), arguments.node->getKind(), arguments.parent)
{
    // The lines are drawn only where the view exposes them
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    setData(*arguments.node, *arguments.container, PDFAppEditContentItemsHelper::getDisplayContext(arguments));
}

PDFAppEditContentTextItem::~PDFAppEditContentTextItem() = default;

void PDFAppEditContentTextItem::setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context)
{
    prepareGeometryChange();

    // The text of the core: the glyphs (their outlines co-owned by the text) and the lines; the paths of the lines are
    // built at their first paint (zadani_gui.md 4.24)
    m_containerKey = container.getKey();
    m_builderBox = node.getBoundingBox();
    m_text = PDFAppEditContentDisplayAccess::getText(container, node.getId(), context);
    m_isTextComputed = true;
    m_linePaths.clear();
    m_linePaths.resize(m_text ? m_text->lines.size() : 0);

    QRectF box = m_text && !m_text->boundingBox.isNull() ? m_text->boundingBox : m_builderBox;
    constexpr qreal margin = PDFAppEditContentItemsHelper::BOUNDING_MARGIN;
    m_boundingRect = box.adjusted(-margin, -margin, margin, margin);
}

const PDFContentDisplayTextPointer& PDFAppEditContentTextItem::getText() const
{
    return m_text;
}

bool PDFAppEditContentTextItem::isInvisible() const
{
    return m_text && m_text->isInvisible;
}

std::optional<QPainterPath> PDFAppEditContentTextItem::getTextClip() const
{
    // ISO 32000-2, 9.3.6: the glyphs of the modes 4 - 7 are added to the clip, which takes effect at ET (one loop over
    // the glyphs of the text object; a glyph without an outline adds nothing)
    if (!m_text || !m_text->isClipping)
    {
        return std::nullopt;
    }

    QPainterPath clip;
    clip.setFillRule(Qt::WindingFill);
    for (const PDFContentDisplayGlyph& glyph : m_text->glyphs)
    {
        if (glyph.run < m_text->runs.size() && m_text->runs[glyph.run].isClipping && glyph.outline)
        {
            clip.addPath(glyph.outline->createPath(glyph.matrix));
        }
    }
    clip.setFillRule(Qt::WindingFill);
    return clip;
}

QRectF PDFAppEditContentTextItem::boundingRect() const
{
    return m_boundingRect;
}

void PDFAppEditContentTextItem::buildLine(size_t lineIndex) const
{
    LinePaths& paths = m_linePaths[lineIndex];
    paths.isBuilt = true;
    paths.runPaths.clear();
    paths.runIndices.clear();
    paths.cellPath = QPainterPath();

    // One path of the glyphs of each run of the line (the glyphs of the line, each once)
    const PDFContentDisplayTextLine& line = m_text->lines[lineIndex];
    const size_t end = qMin(line.firstGlyph + line.glyphCount, m_text->glyphs.size());
    for (size_t i = line.firstGlyph; i < end; ++i)
    {
        const PDFContentDisplayGlyph& glyph = m_text->glyphs[i];
        if (paths.runIndices.empty() || paths.runIndices.back() != glyph.run)
        {
            paths.runIndices.push_back(glyph.run);
            paths.runPaths.emplace_back();
        }

        if (glyph.outline)
        {
            paths.runPaths.back().addPath(glyph.outline->createPath(glyph.matrix));
        }
        else if (!glyph.cell.isEmpty())
        {
            // No outline (Type 3, a missing glyph): its advance cell
            paths.cellPath.addPolygon(glyph.matrix.map(QPolygonF(glyph.cell)));
            paths.cellPath.closeSubpath();
        }
    }
}

void PDFAppEditContentTextItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(widget);

    if (!m_text)
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    applyClipPaths(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const PDFAppEditContentScene* scene = getContentScene();
    const bool isInvisibleShown = scene && scene->isInvisibleTextShown();
    const QRectF exposed = option ? option->exposedRect : QRectF();

    for (size_t lineIndex = 0; lineIndex < m_text->lines.size(); ++lineIndex)
    {
        const PDFContentDisplayTextLine& line = m_text->lines[lineIndex];
        if (!exposed.isNull() && !line.boundingBox.isNull() && !exposed.intersects(line.boundingBox))
        {
            continue;
        }

        if (line.isInvisible)
        {
            // Invisible text: nothing, or the gray dotted base line (zadani_gui.md 4.15)
            if (isInvisibleShown)
            {
                QPen pen(PDFAppEditContentItemsHelper::getThemeColor(PDFThemeColors::IconMuted));
                pen.setCosmetic(true);
                pen.setWidthF(1.0);
                pen.setStyle(Qt::DotLine);
                painter->setPen(pen);
                painter->drawLine(line.baseline);
            }
            continue;
        }

        if (!m_linePaths[lineIndex].isBuilt)
        {
            buildLine(lineIndex);
        }

        const LinePaths& paths = m_linePaths[lineIndex];
        if (isHiddenInEditor())
        {
            QPainterPath outline;
            outline.addRect(line.boundingBox);
            paintHiddenOutline(painter, outline);
            continue;
        }

        for (size_t i = 0; i < paths.runPaths.size(); ++i)
        {
            const size_t runIndex = paths.runIndices[i];
            if (runIndex >= m_text->runs.size())
            {
                continue;
            }

            const PDFContentDisplayTextRun& run = m_text->runs[runIndex];
            const PDFContentDisplayState& state = run.state;
            if (run.isInvisible)
            {
                continue;
            }

            painter->setCompositionMode(PDFAppEditContentDisplayAccess::getCompositionMode(state.blendMode));
            const TextRenderingMode mode = run.isType3 ? TextRenderingMode::Fill : state.textRenderingMode;
            if (isTextRenderingModeFilled(mode))
            {
                painter->fillPath(paths.runPaths[i], PDFAppEditContentDisplayAccess::getColor(state.fillColor, state.alphaFilling));
            }
            if (isTextRenderingModeStroked(mode))
            {
                // The line width of the state in the default space of the container
                PDFContentDisplayState scaled = state;
                scaled.lineWidth = state.lineWidth * PDFAppEditContentItemsHelper::getScale(state.matrix);
                painter->strokePath(paths.runPaths[i], PDFAppEditContentDisplayAccess::createPen(scaled, PDFAppEditContentDisplayAccess::getColor(state.strokeColor, state.alphaStroking)));
            }
        }

        if (!paths.cellPath.isEmpty())
        {
            // The cells of the glyphs without an outline: a light fill of the color of the first run
            const PDFContentDisplayState* state = m_text->runs.empty() ? nullptr : &m_text->runs.front().state;
            QColor color = state ? PDFAppEditContentDisplayAccess::getColor(state->fillColor, 0.25) : QColor(128, 128, 128, 64);
            painter->fillPath(paths.cellPath, color);
        }
    }
}

bool PDFAppEditContentTextItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    if (node.getKind() != PDFContentNodeKind::TextObject)
    {
        return false;
    }

    setData(node, container, PDFAppEditContentItemsHelper::getDisplayContext(this));
    update();
    return true;
}

bool PDFAppEditContentTextItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    if (!isVisible())
    {
        return false;
    }

    // By the rectangles of its lines (zadani_gui.md 4.15); hidden: by the outlines of the lines (R6-17)
    const QPointF point = mapFromScene(scenePoint);
    const qreal itemTolerance = getItemTolerance(tolerance);
    if (!m_boundingRect.adjusted(-itemTolerance, -itemTolerance, itemTolerance, itemTolerance).contains(point) || !isInsideClipPaths(point))
    {
        return false;
    }

    const bool isHidden = isHiddenInEditor();
    if (!m_text || m_text->lines.empty())
    {
        if (isHidden)
        {
            QPainterPath outline;
            outline.addRect(m_builderBox);
            return isHitByOutline(outline, point, itemTolerance);
        }
        return m_builderBox.adjusted(-itemTolerance, -itemTolerance, itemTolerance, itemTolerance).contains(point);
    }

    const PDFAppEditContentScene* scene = getContentScene();
    const bool isInvisibleShown = scene && scene->isInvisibleTextShown();
    for (const PDFContentDisplayTextLine& line : m_text->lines)
    {
        if (line.isInvisible && !isInvisibleShown)
        {
            continue;
        }
        if (isHidden)
        {
            QPainterPath outline;
            outline.addRect(line.boundingBox);
            if (isHitByOutline(outline, point, itemTolerance))
            {
                return true;
            }
            continue;
        }
        if (line.boundingBox.adjusted(-itemTolerance, -itemTolerance, itemTolerance, itemTolerance).contains(point))
        {
            return true;
        }
    }
    return false;
}

void PDFAppEditContentTextItem::clearCache()
{
    for (LinePaths& paths : m_linePaths)
    {
        paths = LinePaths();
    }
    update();
}

// ---------------------------------------------------------------- PDFAppEditContentImageItem

PDFAppEditContentImageItem::PDFAppEditContentImageItem(const PDFAppEditContentItemArguments& arguments) :
    PDFAppEditContentItem(arguments.node->getId(), arguments.node->getKind(), arguments.parent)
{
    setData(*arguments.node, *arguments.container, PDFAppEditContentItemsHelper::getDisplayContext(arguments));
}

PDFAppEditContentImageItem::~PDFAppEditContentImageItem() = default;

void PDFAppEditContentImageItem::setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context)
{
    prepareGeometryChange();

    const std::optional<PDFContentDisplayState> state = PDFAppEditContentDisplayAccess::getDisplayState(container, node.getId(), context);
    m_containerKey = container.getKey();
    m_matrix = state ? state->matrix : QTransform();
    m_blendMode = state ? state->blendMode : BlendMode::Normal;
    setBaseOpacity(state ? state->alphaFilling : 1.0);

    // The picture of the scene: one per image XObject, an inline image per its node; a stencil mask per its color. A new
    // version with the same key (a move, a resize) keeps its picture (R6-14).
    const PDFContentNode::Data& data = node.getData();
    PDFAppEditContentPictureKey key;
    if (data.xobject.isValid())
    {
        key.xobject = data.xobject;
    }
    else
    {
        key.container = m_containerKey;
        key.node = node.getId();
    }
    key.maskColor = state ? PDFAppEditContentDisplayAccess::getColor(state->fillColor, 1.0).rgba() : qRgba(0, 0, 0, 255);
    m_pictureKey = key;

    // The unit square under the matrix (a degenerate matrix: the box of the builder)
    QRectF box = m_matrix.mapRect(QRectF(0.0, 0.0, 1.0, 1.0));
    if (box.isEmpty())
    {
        box = node.getBoundingBox();
    }
    m_boundingRect = box;
}

QRectF PDFAppEditContentImageItem::boundingRect() const
{
    return m_boundingRect;
}

bool PDFAppEditContentImageItem::hasPicture() const
{
    const PDFAppEditContentScene* scene = getContentScene();
    return scene && scene->hasPicture(m_pictureKey);
}

void PDFAppEditContentImageItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    PDFPainterStateGuard guard(painter);
    applyClipPaths(painter);

    if (isHiddenInEditor())
    {
        QPainterPath outline;
        outline.addPolygon(m_matrix.map(QPolygonF(QRectF(0.0, 0.0, 1.0, 1.0))));
        outline.closeSubpath();
        paintHiddenOutline(painter, outline);
        return;
    }

    // The picture of the scene for the size on the device (the scene decodes a larger one in a worker when the zoom
    // needs it; R6-5, zadani_gui.md 4.24)
    painter->setCompositionMode(PDFAppEditContentDisplayAccess::getCompositionMode(m_blendMode));
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setWorldTransform(m_matrix, true);
    const qreal devicePixelRatio = PDFAppEditContentItemsHelper::getDevicePixelRatio(painter);
    const qreal pixels = PDFAppEditContentDisplayAccess::getDevicePixels(QRectF(0.0, 0.0, 1.0, 1.0), painter->worldTransform()) * devicePixelRatio * devicePixelRatio;
    bool isFailed = false;
    PDFAppEditContentScene* scene = getContentScene();
    const QImage* picture = scene ? scene->getPicture(this, pixels, &isFailed) : nullptr;
    if (picture && !picture->isNull())
    {
        // Row 0 of the picture is the top of the unit square (v = 1)
        const QTransform pixelToUnit(1.0 / picture->width(), 0.0, 0.0, -1.0 / picture->height(), 0.0, 1.0);
        painter->setWorldTransform(pixelToUnit, true);
        painter->drawImage(QPointF(0.0, 0.0), *picture);
        return;
    }

    // Until the picture comes (or without one): a light placeholder of the image area
    painter->fillRect(QRectF(0.0, 0.0, 1.0, 1.0), QColor(200, 200, 200, isFailed ? 64 : 128));
}

bool PDFAppEditContentImageItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    if (node.getKind() != PDFContentNodeKind::Image)
    {
        return false;
    }

    setData(node, container, PDFAppEditContentItemsHelper::getDisplayContext(this));
    update();
    return true;
}

bool PDFAppEditContentImageItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    // R6-9: by the unit square under the matrix of the image (a rotated image not in the corners of its bounding
    // rectangle) and by the clips in effect
    if (!isVisible())
    {
        return false;
    }

    const QPointF point = mapFromScene(scenePoint);
    const qreal itemTolerance = getItemTolerance(tolerance);
    if (!isInsideClipPaths(point))
    {
        return false;
    }

    QPainterPath outline;
    outline.addPolygon(m_matrix.map(QPolygonF(QRectF(0.0, 0.0, 1.0, 1.0))));
    outline.closeSubpath();
    if (isHiddenInEditor())
    {
        return isHitByOutline(outline, point, itemTolerance);
    }
    return outline.contains(point) || isHitByOutline(outline, point, itemTolerance);
}

void PDFAppEditContentImageItem::clearCache()
{
    // The pictures are the scene's (dropped there on a change of the theme)
    update();
}

// ---------------------------------------------------------------- PDFAppEditContentShadingItem

PDFAppEditContentShadingItem::PDFAppEditContentShadingItem(const PDFAppEditContentItemArguments& arguments) :
    PDFAppEditContentItem(arguments.node->getId(), arguments.node->getKind(), arguments.parent)
{
    setData(*arguments.node, *arguments.container, PDFAppEditContentItemsHelper::getDisplayContext(arguments));
}

PDFAppEditContentShadingItem::~PDFAppEditContentShadingItem() = default;

void PDFAppEditContentShadingItem::setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context)
{
    prepareGeometryChange();

    // The box of the core: /BBox of the shading and the clips in effect; unknown = the box of the builder
    m_containerKey = container.getKey();
    QRectF box = PDFAppEditContentItemsHelper::getDisplayBox(container, node.getId(), context);
    if (box.isNull())
    {
        box = node.getBoundingBox();
    }
    m_boundingRect = box;

    // R6-16: the constant alpha and the blend mode of the state (sh paints with the fill alpha, ISO 32000-2, 11.6.4.4)
    const std::optional<PDFContentDisplayState> state = PDFAppEditContentDisplayAccess::getDisplayState(container, node.getId(), context);
    m_alpha = state ? qBound(0.0, state->alphaFilling, 1.0) : 1.0;
    m_blendMode = state ? state->blendMode : BlendMode::Normal;

    // A new version: the shading of the old one is drawn until the new one comes (no flicker); asked again
    m_isDecodingRequested = false;
}

QRectF PDFAppEditContentShadingItem::boundingRect() const
{
    return m_boundingRect;
}

void PDFAppEditContentShadingItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    PDFPainterStateGuard guard(painter);
    applyClipPaths(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    if (isHiddenInEditor())
    {
        QPainterPath outline;
        outline.addRect(m_boundingRect);
        paintHiddenOutline(painter, outline);
        return;
    }

    if (!m_isDecodingRequested)
    {
        m_isDecodingRequested = true;
        if (PDFAppEditContentScene* scene = getContentScene())
        {
            const qreal devicePixelRatio = PDFAppEditContentItemsHelper::getDevicePixelRatio(painter);
            const qreal pixels = PDFAppEditContentDisplayAccess::getDevicePixels(m_boundingRect, painter->worldTransform()) * devicePixelRatio * devicePixelRatio;
            scene->requestDecoding(this, PDFAppEditContentDecodeKind::Shading, PDFAppEditContentDisplayAccess::getPixelStep(pixels));
        }
    }

    // The area of the shading: its box (clipped by the clips in effect)
    painter->setClipRect(m_boundingRect, painter->hasClipping() ? Qt::IntersectClip : Qt::ReplaceClip);
    painter->setOpacity(painter->opacity() * m_alpha);
    painter->setCompositionMode(PDFAppEditContentDisplayAccess::getCompositionMode(m_blendMode));
    if (m_shading && m_shading->status == PDFContentDisplayShading::Status::Done)
    {
        const PDFContentDisplayShading& shading = *m_shading;
        if (shading.kind == PDFContentDisplayShading::Kind::Picture)
        {
            // The types 1, 4 - 7 (the meshes since FX1): the picture of the core over its rectangle
            painter->drawImage(shading.pictureRectangle, shading.picture);
            return;
        }

        if (shading.backgroundColor.isValid())
        {
            painter->fillRect(m_boundingRect, shading.backgroundColor);
        }

        // The gradient in the shading space, the area in the default space of the container
        const QTransform& matrix = shading.matrix;
        if (matrix.isInvertible())
        {
            painter->setWorldTransform(matrix, true);
            painter->fillRect(matrix.inverted().mapRect(m_boundingRect), QBrush(PDFAppEditContentItemsHelper::createGradient(shading)));
        }
        return;
    }

    // Until the shading comes, and for TooLarge / Failed: a light area
    painter->fillRect(m_boundingRect, QColor(160, 160, 160, 64));
}

bool PDFAppEditContentShadingItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    if (node.getKind() != PDFContentNodeKind::Shading)
    {
        return false;
    }

    setData(node, container, PDFAppEditContentItemsHelper::getDisplayContext(this));
    update();
    return true;
}

bool PDFAppEditContentShadingItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    // The box of the core (/BBox and the clips) and the clip paths themselves
    return PDFAppEditContentItem::isHit(scenePoint, tolerance);
}

void PDFAppEditContentShadingItem::clearCache()
{
    // The theme changed: the shading is computed again (the colors of the display)
    m_shading.reset();
    m_isDecodingRequested = false;
    update();
}

void PDFAppEditContentShadingItem::setShading(const PDFContentDisplayShading& shading)
{
    m_shading = shading;
    update();
}

// ---------------------------------------------------------------- PDFAppEditContentFormItem

PDFAppEditContentFormItem::PDFAppEditContentFormItem(const PDFAppEditContentItemArguments& arguments) :
    PDFAppEditContentItem(arguments.node->getId(), arguments.node->getKind(), arguments.parent)
{
    setData(*arguments.node, *arguments.container, PDFAppEditContentItemsHelper::getDisplayContext(arguments));
}

PDFAppEditContentFormItem::~PDFAppEditContentFormItem() = default;

void PDFAppEditContentFormItem::setData(const PDFContentNode& node, const PDFContentContainer& container, const PDFContentDisplayContext& context)
{
    prepareGeometryChange();

    const std::optional<PDFContentDisplayState> state = PDFAppEditContentDisplayAccess::getDisplayState(container, node.getId(), context);
    const PDFObjectReference reference = node.getData().xobject;
    m_formKey = reference.isValid() ? PDFContentContainerKey::createForm(reference) : PDFContentContainerKey();
    m_useMatrix = state ? state->matrix : QTransform();
    m_builderBox = PDFAppEditContentItemsHelper::getDisplayBox(container, node.getId(), context);
    if (m_builderBox.isNull())
    {
        m_builderBox = node.getBoundingBox();
    }
    setBaseOpacity(state ? state->alphaFilling : 1.0);

    // The children (of the old version of the form) stay until the scene builds the new ones; a new use matrix moves
    // them at once
    if (m_formVersion != 0)
    {
        if (const PDFAppEditContentScene* scene = getContentScene())
        {
            if (const PDFContentContainerPointer form = scene->getContainer(m_formKey))
            {
                setBaseTransform(form->getMatrix() * m_useMatrix);
            }
        }
    }
}

void PDFAppEditContentFormItem::setForm(const PDFContentContainer& form)
{
    prepareGeometryChange();
    m_formBox = form.getBoundingBox().normalized();
    m_formVersion = form.getVersion();

    // The form space -> the space of the use by /Matrix, then as the content draws the use; the children are clipped
    // to /BBox (ISO 32000-2, 8.10.1)
    setBaseTransform(form.getMatrix() * m_useMatrix);
    setFlag(QGraphicsItem::ItemClipsChildrenToShape, true);
    setFlag(QGraphicsItem::ItemHasNoContents, false);
}

void PDFAppEditContentFormItem::resetForm()
{
    // Without children: the outline of the box of the builder in the space of the use
    prepareGeometryChange();
    m_formVersion = 0;
    m_formBox = QRectF();
    setBaseTransform(QTransform());
    setFlag(QGraphicsItem::ItemClipsChildrenToShape, false);
}

void PDFAppEditContentFormItem::updateChildrenVisibility()
{
    // R6-8: a hidden use shows its outline only, its content is hidden (a nested use with it)
    const bool isShown = !isHiddenInEditor();
    for (QGraphicsItem* child : childItems())
    {
        child->setVisible(isShown);
    }
}

void PDFAppEditContentFormItem::setEditorState(bool isLocked, bool isHidden)
{
    PDFAppEditContentItem::setEditorState(isLocked, isHidden);
    updateChildrenVisibility();
}

QRectF PDFAppEditContentFormItem::boundingRect() const
{
    return m_formVersion != 0 ? m_formBox : m_builderBox;
}

QPainterPath PDFAppEditContentFormItem::shape() const
{
    QPainterPath path;
    path.addRect(boundingRect());
    return path;
}

void PDFAppEditContentFormItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    // The children draw the form; without them (not loaded yet, not loadable, nested too deep, beyond the budget) and
    // when hidden: a light outline of its box
    if (m_formVersion != 0 && !isHiddenInEditor())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    applyClipPaths(painter);
    QPainterPath outline;
    outline.addRect(boundingRect());
    paintHiddenOutline(painter, outline);
}

bool PDFAppEditContentFormItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    if (node.getKind() != PDFContentNodeKind::FormUse)
    {
        return false;
    }

    const PDFContentContainerKey formKey = m_formKey;
    setData(node, container, PDFAppEditContentItemsHelper::getDisplayContext(this));
    update();

    // Another form: the scene replaces the item (its children are of the old form)
    return formKey == m_formKey;
}

bool PDFAppEditContentFormItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    // By the box of the form (its /BBox under the matrices) and by the clips around the use, each in its own space (R6-7)
    if (!isVisible())
    {
        return false;
    }

    const QPointF point = mapFromScene(scenePoint);
    const qreal itemTolerance = getItemTolerance(tolerance);
    if (isHiddenInEditor())
    {
        QPainterPath outline;
        outline.addRect(boundingRect());
        return isHitByOutline(outline, point, itemTolerance) && isInsideClipPaths(point);
    }
    return boundingRect().adjusted(-itemTolerance, -itemTolerance, itemTolerance, itemTolerance).contains(point) && isInsideClipPaths(point);
}

// ---------------------------------------------------------------- PDFAppEditContentGroupItem

PDFAppEditContentGroupItem::PDFAppEditContentGroupItem(const PDFAppEditContentItemArguments& arguments) :
    PDFAppEditContentItem(arguments.node->getId(), arguments.node->getKind(), arguments.parent)
{
    setFlag(QGraphicsItem::ItemHasNoContents, true);
}

PDFAppEditContentGroupItem::~PDFAppEditContentGroupItem() = default;

QRectF PDFAppEditContentGroupItem::boundingRect() const
{
    return QRectF();
}

void PDFAppEditContentGroupItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(painter);
    Q_UNUSED(option);
    Q_UNUSED(widget);
}

bool PDFAppEditContentGroupItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    Q_UNUSED(container);
    return node.getKind() == getNodeKind();
}

bool PDFAppEditContentGroupItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    // The children are hit, the group never itself
    Q_UNUSED(scenePoint);
    Q_UNUSED(tolerance);
    return false;
}

// ---------------------------------------------------------------- PDFAppEditContentOpaqueItem

PDFAppEditContentOpaqueItem::PDFAppEditContentOpaqueItem(const PDFAppEditContentItemArguments& arguments) :
    PDFAppEditContentItem(arguments.node->getId(), arguments.node->getKind(), arguments.parent)
{
    updateNode(*arguments.node, *arguments.container);
}

PDFAppEditContentOpaqueItem::~PDFAppEditContentOpaqueItem() = default;

QRectF PDFAppEditContentOpaqueItem::boundingRect() const
{
    return m_box;
}

void PDFAppEditContentOpaqueItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (m_box.isEmpty())
    {
        return;
    }

    // The hatched rectangle of the content the editor does not understand (zadani_gui.md 4.15)
    PDFPainterStateGuard guard(painter);
    applyClipPaths(painter);
    const QColor color = PDFAppEditContentItemsHelper::getThemeColor(PDFThemeColors::BorderStrong);
    QPen pen(color);
    pen.setCosmetic(true);
    pen.setWidthF(1.0);
    painter->setPen(pen);
    painter->setBrush(QBrush(color, Qt::BDiagPattern));
    painter->drawRect(m_box);
}

bool PDFAppEditContentOpaqueItem::updateNode(const PDFContentNode& node, const PDFContentContainer& container)
{
    if (node.getKind() != PDFContentNodeKind::Opaque)
    {
        return false;
    }

    prepareGeometryChange();
    m_box = PDFAppEditContentItemsHelper::getDisplayBox(container, node.getId(), PDFAppEditContentItemsHelper::getDisplayContext(this));
    if (m_box.isNull())
    {
        m_box = node.getBoundingBox();
    }
    update();
    return true;
}

// ---------------------------------------------------------------- PDFAppEditContentUnderlayItem

PDFAppEditContentUnderlayItem::PDFAppEditContentUnderlayItem()
{
    // Never selectable, no events (zadani_gui.md 4.15: the annotations are not edited by the tool)
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    setAcceptDrops(false);
}

PDFAppEditContentUnderlayItem::~PDFAppEditContentUnderlayItem() = default;

QRectF PDFAppEditContentUnderlayItem::boundingRect() const
{
    return m_picture.isNull() ? QRectF() : m_rectangle;
}

void PDFAppEditContentUnderlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (m_picture.isNull() || m_rectangle.isEmpty())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setOpacity(painter->opacity() * DIMMED_OPACITY);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setWorldTransform(PDFAppEditContentItemsHelper::getPictureToRectangle(m_picture, m_rectangle), true);
    painter->drawImage(QPointF(0.0, 0.0), m_picture);
}

void PDFAppEditContentUnderlayItem::setPicture(const QImage& picture, const QRectF& pageRectangle, PDFInteger pageIndex)
{
    prepareGeometryChange();
    m_picture = picture;
    m_rectangle = pageRectangle;
    m_pageIndex = pageIndex;
    update();
}

// ---------------------------------------------------------------- the entries of the registry

/// The entry of the items of the paths, which paint (or whose painting is switched off)
class PDFAppEditContentPathItemEntry : public PDFAppEditContentItemEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("path"); }
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer&) const override { return node.getKind() == PDFContentNodeKind::Path && !node.getData().paint.isClippingPath(); }
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const override { return new PDFAppEditContentPathItem(arguments); }
};

/// The entry of the items of the clipping paths
class PDFAppEditContentClipPathItemEntry : public PDFAppEditContentItemEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("clipping-path"); }
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer&) const override { return node.getKind() == PDFContentNodeKind::Path && node.getData().paint.isClippingPath(); }
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const override { return new PDFAppEditContentClipPathItem(arguments); }
};

/// The entry of the items of the texts (one item of a text object draws its lines)
class PDFAppEditContentTextItemEntry : public PDFAppEditContentItemEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("text-line"); }
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer&) const override { return node.getKind() == PDFContentNodeKind::TextObject; }
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const override { return new PDFAppEditContentTextItem(arguments); }
};

/// The entry of the items of the images
class PDFAppEditContentImageItemEntry : public PDFAppEditContentItemEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("image"); }
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer&) const override { return node.getKind() == PDFContentNodeKind::Image; }
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const override { return new PDFAppEditContentImageItem(arguments); }
};

/// The entry of the items of the shadings (a picture or a gradient; P43 replaces it by a higher priority)
class PDFAppEditContentShadingItemEntry : public PDFAppEditContentItemEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("shading"); }
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer&) const override { return node.getKind() == PDFContentNodeKind::Shading; }
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const override { return new PDFAppEditContentShadingItem(arguments); }
};

/// The entry of the items of the uses of the forms
class PDFAppEditContentFormItemEntry : public PDFAppEditContentItemEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("form-use"); }
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer&) const override { return node.getKind() == PDFContentNodeKind::FormUse; }
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const override { return new PDFAppEditContentFormItem(arguments); }
};

/// The entry of the items of the groups and the marked contents
class PDFAppEditContentGroupItemEntry : public PDFAppEditContentItemEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("group"); }
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer&) const override { return node.getKind() == PDFContentNodeKind::Group || node.getKind() == PDFContentNodeKind::MarkedContent; }
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const override { return new PDFAppEditContentGroupItem(arguments); }
};

/// The entry of the items of the opaque nodes
class PDFAppEditContentOpaqueItemEntry : public PDFAppEditContentItemEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("opaque"); }
    virtual bool isItemOf(const PDFContentNode& node, const PDFContentContainer&) const override { return node.getKind() == PDFContentNodeKind::Opaque; }
    virtual PDFAppEditContentItem* createItem(const PDFAppEditContentItemArguments& arguments) const override { return new PDFAppEditContentOpaqueItem(arguments); }
};

/// The entry of the overlay of the annotations and fields (zadani_gui.md 4.15; layer Underlay)
class PDFAppEditContentUnderlayEntry : public PDFAppEditContentOverlayEntry
{
public:
    virtual QString getId() const override { return QStringLiteral("annotation-underlay"); }
    virtual PDFAppEditContentSceneLayer getLayer() const override { return PDFAppEditContentSceneLayer::Underlay; }

    virtual QGraphicsItem* createOverlay(PDFAppEditContentContext* context, PDFAppEditContentScene* scene) const override
    {
        // The scene fills it (it finds it among its overlays); the item keeps nothing of the context
        Q_UNUSED(context);
        Q_UNUSED(scene);
        return new PDFAppEditContentUnderlayItem();
    }
};

void PDFAppEditContentItems::registerParts(PDFAppEditContentRegistry* registry)
{
    registry->addItem(std::make_unique<PDFAppEditContentPathItemEntry>());
    registry->addItem(std::make_unique<PDFAppEditContentClipPathItemEntry>());
    registry->addItem(std::make_unique<PDFAppEditContentTextItemEntry>());
    registry->addItem(std::make_unique<PDFAppEditContentImageItemEntry>());
    registry->addItem(std::make_unique<PDFAppEditContentShadingItemEntry>());
    registry->addItem(std::make_unique<PDFAppEditContentFormItemEntry>());
    registry->addItem(std::make_unique<PDFAppEditContentGroupItemEntry>());
    registry->addItem(std::make_unique<PDFAppEditContentOpaqueItemEntry>());
    registry->addOverlay(std::make_unique<PDFAppEditContentUnderlayEntry>());
}

}   // namespace pdf
