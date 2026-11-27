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

#include "pdfappdocument.h"
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
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>
#include <QtGlobal>

#include <cmath>

namespace pdf
{

// Stage E1, package P11: the items of the canvas. Every item draws in the default space of its container (the item
// coordinates; a form item maps the space of its form into the space of its use). Bounds: an item draws the data of
// one node - a path its segments, a text its glyphs (the lines are built at their first paint, only the lines
// exposed), an image one picture of at most 4 megapixels.

/// Helpers of this file (static functions only)
class PDFAppEditContentItemsHelper
{
public:
    PDFAppEditContentItemsHelper() = delete;

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
};

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
    setOpacity(isDimmed ? 0.35 : 1.0);
}

void PDFAppEditContentItem::clearCache()
{
}

bool PDFAppEditContentItem::isHit(const QPointF& scenePoint, qreal tolerance) const
{
    return isVisible() && sceneBoundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(scenePoint);
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
    if (!m_clipPaths)
    {
        return;
    }

    for (const QPainterPath& path : *m_clipPaths)
    {
        painter->setClipPath(path, painter->hasClipping() ? Qt::IntersectClip : Qt::ReplaceClip);
    }
}

bool PDFAppEditContentItem::isInsideClipPaths(const QPointF& itemPoint) const
{
    if (!m_clipPaths)
    {
        return true;
    }

    for (const QPainterPath& path : *m_clipPaths)
    {
        if (!path.contains(itemPoint))
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
    // node; the line width 0 is the thinnest line of the device
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
        // The dash array of QPen is in the units of the pen width (as PDFLineDashPattern::createForQPen)
        QList<qreal> dashes;
        for (const PDFReal value : state.dashPattern.getDashArray())
        {
            dashes.push_back(value / state.lineWidth);
        }
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern(dashes);
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
    // The separable blend modes QPainter has (as PDFBlendModeInfo::getCompositionModeFromBlendMode of the core, which
    // is not exported); the others are drawn as Normal (Preview shows them)
    switch (blendMode)
    {
        case BlendMode::Multiply:   return QPainter::CompositionMode_Multiply;
        case BlendMode::Screen:     return QPainter::CompositionMode_Screen;
        case BlendMode::Overlay:    return QPainter::CompositionMode_Overlay;
        case BlendMode::Darken:     return QPainter::CompositionMode_Darken;
        case BlendMode::Lighten:    return QPainter::CompositionMode_Lighten;
        case BlendMode::ColorDodge: return QPainter::CompositionMode_ColorDodge;
        case BlendMode::ColorBurn:  return QPainter::CompositionMode_ColorBurn;
        case BlendMode::HardLight:  return QPainter::CompositionMode_HardLight;
        case BlendMode::SoftLight:  return QPainter::CompositionMode_SoftLight;
        case BlendMode::Difference: return QPainter::CompositionMode_Difference;
        case BlendMode::Exclusion:  return QPainter::CompositionMode_Exclusion;
        default:
            break;
    }
    return QPainter::CompositionMode_SourceOver;
}

PDFDocumentPointer PDFAppEditContentDisplayAccess::getDocument(const PDFAppEditContentContext* context)
{
    // The context does not give its shared document (a request of pe1p11_state.md); the GUI has it - only when it is
    // the very document of the session (a document reloaded under the tool is another one)
    const PDFDocumentToolGUI* gui = context ? context->getGUI() : nullptr;
    const PDFAppDocument* document = gui && gui->getShell() ? gui->getDocument() : nullptr;
    PDFDocumentPointer pointer = document ? document->getDocument() : PDFDocumentPointer();
    if (!pointer || pointer.data() != context->getSession()->getDocument())
    {
        return PDFDocumentPointer();
    }
    return pointer;
}

PDFCMSPointer PDFAppEditContentDisplayAccess::getColorManagement(const PDFAppEditContentContext* context)
{
    const PDFDocumentToolGUI* gui = context ? context->getGUI() : nullptr;
    PDFAppToolHost* host = gui ? gui->getHost() : nullptr;
    return host ? host->createEnvironment().cms : PDFCMSPointer();
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
}

QRectF PDFAppEditContentPathItem::boundingRect() const
{
    return m_boundingRect;
}

QPainterPath PDFAppEditContentPathItem::shape() const
{
    return m_containerPath;
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

    if (m_state && (m_paint.fill || m_paint.stroke))
    {
        PDFPainterStateGuard contentGuard(painter);
        painter->setCompositionMode(PDFAppEditContentDisplayAccess::getCompositionMode(m_state->blendMode));
        painter->setWorldTransform(m_matrix, true);
        if (m_paint.fill)
        {
            painter->fillPath(m_path, PDFAppEditContentDisplayAccess::getColor(m_state->fillColor, m_state->alphaFilling));
        }
        if (m_paint.stroke)
        {
            painter->strokePath(m_path, PDFAppEditContentDisplayAccess::createPen(*m_state, PDFAppEditContentDisplayAccess::getColor(m_state->strokeColor, m_state->alphaStroking)));
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
    m_strokeShape = QPainterPath();
    m_strokeShapeWidth = -1.0;
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
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * getItemTolerance(tolerance));
    return stroker.createStroke(m_containerPath).contains(point);
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

    // By the rectangles of its lines (zadani_gui.md 4.15)
    const QPointF point = mapFromScene(scenePoint);
    const qreal itemTolerance = getItemTolerance(tolerance);
    if (!m_boundingRect.adjusted(-itemTolerance, -itemTolerance, itemTolerance, itemTolerance).contains(point) || !isInsideClipPaths(point))
    {
        return false;
    }

    if (!m_text || m_text->lines.empty())
    {
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
    m_maskColor = state ? PDFAppEditContentDisplayAccess::getColor(state->fillColor, 1.0) : QColor(Qt::black);
    setOpacity(state ? qBound(0.0, state->alphaFilling, 1.0) * (isDimmed() ? 0.35 : 1.0) : 1.0);

    // The unit square under the matrix (a degenerate matrix: the box of the builder)
    QRectF box = m_matrix.mapRect(QRectF(0.0, 0.0, 1.0, 1.0));
    if (box.isEmpty())
    {
        box = node.getBoundingBox();
    }
    m_boundingRect = box;

    // A new version: the picture is decoded again at the next paint (the matrix alone does not change the pixels, but
    // the image of a new node may be another one)
    m_image = QImage();
    m_isDecodingRequested = false;
    m_isDecodingFailed = false;
}

QRectF PDFAppEditContentImageItem::boundingRect() const
{
    return m_boundingRect;
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

    // The first paint asks for the picture (a worker decodes it, zadani_gui.md 4.24)
    if (!m_isDecodingRequested && PDFAppEditContentDisplayAccess::IS_IMAGE_READY)
    {
        m_isDecodingRequested = true;
        if (PDFAppEditContentScene* scene = getContentScene())
        {
            scene->requestDecoding(this);
        }
    }

    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setWorldTransform(m_matrix, true);
    if (!m_image.isNull())
    {
        // Row 0 of the picture is the top of the unit square (v = 1)
        const QTransform pixelToUnit(1.0 / m_image.width(), 0.0, 0.0, -1.0 / m_image.height(), 0.0, 1.0);
        painter->setWorldTransform(pixelToUnit, true);
        painter->drawImage(QPointF(0.0, 0.0), m_image);
        return;
    }

    // Until the picture comes (or without one): a light placeholder of the image area
    painter->fillRect(QRectF(0.0, 0.0, 1.0, 1.0), QColor(200, 200, 200, m_isDecodingFailed ? 64 : 128));
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

void PDFAppEditContentImageItem::clearCache()
{
    // The picture does not depend on the theme; a new device pixel ratio does not need another one (at most 4
    // megapixels are decoded whatever the zoom)
    update();
}

void PDFAppEditContentImageItem::setDecodedImage(const PDFContentDisplayImage& image)
{
    if (image.status != PDFContentDisplayImage::Status::Decoded || image.image.isNull())
    {
        m_isDecodingFailed = true;
        update();
        return;
    }

    // A stencil mask comes painted in the fill color of the display state already (PDFContentDisplay::decodeImage)
    m_image = image.image;
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
    m_shading.reset();
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

    if (!m_isDecodingRequested && PDFAppEditContentDisplayAccess::IS_SHADING_READY)
    {
        m_isDecodingRequested = true;
        if (PDFAppEditContentScene* scene = getContentScene())
        {
            scene->requestDecoding(this);
        }
    }

    // The area of the shading: its box (clipped by the clips in effect)
    painter->setClipRect(m_boundingRect, painter->hasClipping() ? Qt::IntersectClip : Qt::ReplaceClip);
    if (m_shading && m_shading->status == PDFContentDisplayShading::Status::Done)
    {
        const PDFContentDisplayShading& shading = *m_shading;
        if (shading.kind == PDFContentDisplayShading::Kind::Picture)
        {
            painter->drawImage(shading.pictureRectangle, shading.picture);
            return;
        }

        if (shading.backgroundColor.isValid())
        {
            painter->fillRect(m_boundingRect, shading.backgroundColor);
        }

        QGradient gradient = shading.kind == PDFContentDisplayShading::Kind::Axial ? QGradient(QLinearGradient(shading.start, shading.end))
                                                                                   : QGradient(QRadialGradient(shading.end, shading.endRadius, shading.start, shading.startRadius));
        for (const PDFContentDisplayShading::Stop& stop : shading.stops)
        {
            gradient.setColorAt(qBound(0.0, stop.position, 1.0), stop.color);
        }
        gradient.setSpread(QGradient::PadSpread);

        // The gradient in the shading space, the area in the default space of the container
        const QTransform& matrix = shading.matrix;
        if (matrix.isInvertible())
        {
            painter->setWorldTransform(matrix, true);
            painter->fillRect(matrix.inverted().mapRect(m_boundingRect), QBrush(gradient));
        }
        return;
    }

    // Until the shading comes (or without one): a light area
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

void PDFAppEditContentShadingItem::clearCache()
{
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
    setOpacity(state ? qBound(0.0, state->alphaFilling, 1.0) : 1.0);

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

    // The children draw the form; without them (not loaded yet, not loadable, nested too deep) a light outline of its box
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
    // By the box of the form (its /BBox under the matrices)
    if (!isVisible())
    {
        return false;
    }

    const QPointF point = mapFromScene(scenePoint);
    const qreal itemTolerance = getItemTolerance(tolerance);
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

// ---------------------------------------------------------------- the entries of the registry

/// The entry of the items of the paths, which paint
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
}

}   // namespace pdf
