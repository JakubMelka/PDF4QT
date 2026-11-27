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

#include "pdfcontentdisplay.h"
#include "pdfcatalog.h"
#include "pdfcms.h"
#include "pdfcolorspaces.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdffunction.h"
#include "pdfimage.h"
#include "pdfmeshqualitysettings.h"
#include "pdfoperationcontrol.h"
#include "pdfoptionalcontent.h"
#include "pdfpainter.h"
#include "pdfpattern.h"
#include "pdfrenderer.h"

#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <limits>
#include <map>
#include <set>

namespace pdf
{

/// The helpers of PDFContentDisplay (static functions only)
class PDFContentDisplayHelper
{
public:
    PDFContentDisplayHelper() = delete;

    using Property = PDFContentStateProperty;

    /// Depth of the walks over a subtree: as deep as the builder and the editor go, with their leaves
    static constexpr int MAXIMUM_WALK_DEPTH = PDFContentContainer::MAXIMUM_DEPTH + 2;

    /// Versions of containers, whose counts are kept (getCounts)
    static constexpr size_t COUNT_CACHE_SIZE = 64;

    /// Computes the lazily evaluated type of a matrix, so that a const copy is plain data for more
    /// threads (QTransform::type writes a mutable member at its first call)
    static void makePlain(QTransform& matrix) { matrix.type(); }

    /// The color as an opaque RGB color (an invalid color stays invalid)
    static QColor toOpaqueRgb(const QColor& color);

    /// The color of the state of the builder
    static PDFContentDisplayColor getBuilderColor(const PDFPageContentProcessorState& state,
                                                  const PDFContentSymbolicState* symbolicState,
                                                  bool isStroke,
                                                  const PDFCMS* cms);

    /// The color of a value of an override (the kind Color)
    static PDFContentDisplayColor getOverrideColor(const PDFContentColorValue& value,
                                                   const PDFContentDisplayContext& context,
                                                   const PDFCMS* cms,
                                                   RenderingIntent intent);

    /// Applies the properties of an effective override to a state
    static void applyOverride(PDFContentDisplayState& state,
                              const PDFContentStateOverride& override,
                              const PDFContentDisplayContext& context,
                              const PDFCMS* cms,
                              RenderingIntent intent);

    /// The display state of a node; the matrix is followed by \p containerToTarget.
    /// \param isOverridePossible false: neither the node nor an ancestor has an override (the walk of
    ///        getBoundingBox knows it), getEffectiveOverride is not asked (its cost is the depth)
    static std::optional<PDFContentDisplayState> computeState(const PDFContentContainer& container,
                                                              const PDFContentNode& node,
                                                              const PDFContentDisplayContext& context,
                                                              const QTransform& containerToTarget,
                                                              bool isColorNeeded,
                                                              bool isOverridePossible);

    /// The matrix T x CTM of a node (no value: unknown, no state, a singular matrix on the way)
    static std::optional<QTransform> getNodeMatrix(const PDFContentContainer& container, const PDFContentNode& node, bool isOverridePossible);

    /// The node or an ancestor has an override (the cost is the depth)
    static bool hasOverrideOnPath(const PDFContentContainer& container, const PDFContentNode& node);

    /// The pattern a node is painted with by its effective state: the pattern object of the color
    static PDFObject getPatternObject(const PDFContentContainer& container, const PDFContentNode& node, bool isStroke);

    /// getContainerMatrix with a context (the colors are not needed)
    static std::optional<QTransform> getContainerMatrix(const PDFContentContainer& container, const PDFContentDisplayUsePath& uses);

    /// The box of a subtree in the target space
    static PDFContentDisplayBox getBoundingBox(const PDFContentContainer& container,
                                               const PDFContentNode& start,
                                               const PDFContentDisplayContext& context,
                                               const QTransform& containerToTarget);

    /// The box of a leaf (and of a TextShow) in the target space
    static PDFContentDisplayBox getLeafBoundingBox(const PDFContentContainer& container,
                                                   const PDFContentNode& node,
                                                   const PDFContentDisplayContext& context,
                                                   const QTransform& containerToTarget,
                                                   bool isOverridePossible);

    /// The box of the solid stroke of a path in the target space
    static QRectF getStrokeBoundingBox(const PDFContentPath& path, bool closeBeforePaint, const PDFContentDisplayState& state);

    /// The resource dictionary of a container: its own resources as the container knows them, else
    /// (a form or a pattern cell without /Resources) the resources of the page it inherits from
    static PDFObject getResourceDictionary(const PDFContentContainer& container, const PDFDocument* document);

    /// A resource object of a container (dereferenced): its own resources, else those of the page,
    /// whose resources it inherits
    static PDFObject getResource(const PDFContentContainer& container,
                                 PDFContentResourceCategory category,
                                 const QByteArray& name,
                                 const PDFDocument* document);

    /// The text of a text object in the target space
    static PDFContentDisplayTextPointer getText(const PDFContentContainer& container,
                                                const PDFContentNode& textObject,
                                                const PDFContentDisplayContext& context,
                                                const QTransform& containerToTarget);

    /// Adds the glyphs of one text show to a text
    static void addTextShow(PDFContentDisplayText& text,
                            std::vector<PDFReal>& cellEnds,
                            const PDFContentContainer& container,
                            const PDFContentNode& show,
                            const PDFContentDisplayContext& context,
                            const QTransform& containerToTarget);

    /// The box of a glyph in the target space (its outline box, else its cell); empty, when it has none
    static QRectF getGlyphBox(const PDFContentDisplayGlyph& glyph);

    /// Splits the glyphs of a text into lines
    static void splitLines(PDFContentDisplayText& text, const std::vector<PDFReal>& cellEnds);

    /// Collects the clips of a node before the node, which reach the nodes after it
    static void collectClips(const PDFContentContainer& container, const PDFContentNode& node, PDFContentDisplayClipChain& chain);

    /// Counts of a container (not cached)
    static PDFContentDisplayCounts computeCounts(const PDFContentContainer& container);

    /// The cache of getCounts: the counts of the last versions
    static QMutex& getCountMutex();
    static std::map<quint64, PDFContentDisplayCounts>& getCountCache();
    static std::deque<quint64>& getCountOrder();

    /// A kind, whose children make its box
    static bool isParentKind(PDFContentNodeKind kind);
};

QColor PDFContentDisplayHelper::toOpaqueRgb(const QColor& color)
{
    if (!color.isValid())
    {
        return QColor();
    }

    QColor result = color.toRgb();
    result.setAlphaF(1.0);
    return result;
}

PDFContentDisplayColor PDFContentDisplayHelper::getBuilderColor(const PDFPageContentProcessorState& state,
                                                                const PDFContentSymbolicState* symbolicState,
                                                                bool isStroke,
                                                                const PDFCMS* cms)
{
    PDFContentDisplayColor result;
    const PDFAbstractColorSpace* colorSpace = isStroke ? state.getStrokeColorSpace() : state.getFillColorSpace();
    const PDFPatternColorSpace* patternColorSpace = colorSpace ? colorSpace->asPatternColorSpace() : nullptr;
    if (!patternColorSpace)
    {
        // The processor converted the color when the builder ran (with the color management of the session)
        result.kind = PDFContentDisplayColor::Kind::Color;
        result.color = toOpaqueRgb(isStroke ? state.getStrokeColor() : state.getFillColor());
        return result;
    }

    result.kind = PDFContentDisplayColor::Kind::Pattern;
    if (symbolicState)
    {
        result.patternObject = symbolicState->getSource(isStroke ? Property::StrokeColor : Property::FillColor).patternObject;
    }

    // An uncolored tiling pattern: the color of its components in the underlying space
    PDFColorSpacePointer uncoloredColorSpace = patternColorSpace->getUncoloredPatternColorSpace();
    if (uncoloredColorSpace)
    {
        try
        {
            PDFRenderErrorReporterDummy reporter;
            result.color = toOpaqueRgb(uncoloredColorSpace->getCheckedColor(patternColorSpace->getUncoloredPatternColor(), cms, state.getRenderingIntent(), &reporter));
        }
        catch (const PDFException&)
        {
            result.color = QColor();
        }
        catch (const PDFRendererException&)
        {
            result.color = QColor();
        }
    }
    return result;
}

PDFContentDisplayColor PDFContentDisplayHelper::getOverrideColor(const PDFContentColorValue& value,
                                                                 const PDFContentDisplayContext& context,
                                                                 const PDFCMS* cms,
                                                                 RenderingIntent intent)
{
    PDFContentDisplayColor result;
    result.value = value;
    result.isOverridden = true;
    result.kind = value.isPattern() ? PDFContentDisplayColor::Kind::Pattern : PDFContentDisplayColor::Kind::Unconverted;
    result.patternObject = value.patternObject;

    try
    {
        PDFColorSpacePointer colorSpace;
        if (value.deviceSpace == COLOR_SPACE_NAME_DEVICE_GRAY)
        {
            colorSpace.reset(new PDFDeviceGrayColorSpace());
        }
        else if (value.deviceSpace == COLOR_SPACE_NAME_DEVICE_RGB)
        {
            colorSpace.reset(new PDFDeviceRGBColorSpace());
        }
        else if (value.deviceSpace == COLOR_SPACE_NAME_DEVICE_CMYK)
        {
            colorSpace.reset(new PDFDeviceCMYKColorSpace());
        }
        else if (value.deviceSpace.isEmpty() && context.document && !value.colorSpaceObject.isNull())
        {
            // A space of the document (named by cs / CS): created as the processor creates it
            colorSpace = PDFAbstractColorSpace::createColorSpace(nullptr, context.document, context.iccProfileCache, context.document->getObject(value.colorSpaceObject));
        }

        if (!colorSpace)
        {
            return result;
        }

        if (const PDFPatternColorSpace* patternColorSpace = colorSpace->asPatternColorSpace())
        {
            // The components of an uncolored pattern are of its underlying space
            colorSpace = patternColorSpace->getUncoloredPatternColorSpace();
            if (!colorSpace || value.components.empty())
            {
                return result;
            }
        }
        else if (value.isPattern())
        {
            return result;
        }

        PDFRenderErrorReporterDummy reporter;
        const QColor color = toOpaqueRgb(colorSpace->getCheckedColor(PDFAbstractColorSpace::convertToColor(value.components), cms, intent, &reporter));
        if (color.isValid())
        {
            result.color = color;
            if (!value.isPattern())
            {
                result.kind = PDFContentDisplayColor::Kind::Color;
            }
        }
    }
    catch (const PDFException&)
    {
        // Unconverted (a pattern keeps no color)
    }
    catch (const PDFRendererException&)
    {
        // Unconverted (a pattern keeps no color)
    }

    return result;
}

void PDFContentDisplayHelper::applyOverride(PDFContentDisplayState& state,
                                            const PDFContentStateOverride& override,
                                            const PDFContentDisplayContext& context,
                                            const PDFCMS* cms,
                                            RenderingIntent intent)
{
    // The rendering intent of the override converts its colors (ISO 32000-2, 8.6.5.8)
    auto intentIt = override.properties.find(Property::RenderingIntent);
    if (intentIt != override.properties.cend() && intentIt->second.kind == PDFContentStateValue::Kind::Name)
    {
        const QByteArray& name = intentIt->second.name;
        if (name == "Perceptual")
        {
            intent = RenderingIntent::Perceptual;
        }
        else if (name == "AbsoluteColorimetric")
        {
            intent = RenderingIntent::AbsoluteColorimetric;
        }
        else if (name == "RelativeColorimetric")
        {
            intent = RenderingIntent::RelativeColorimetric;
        }
        else if (name == "Saturation")
        {
            intent = RenderingIntent::Saturation;
        }
    }

    for (const auto& [property, value] : override.properties)
    {
        state.overriddenProperties.insert(property);

        switch (property)
        {
            case Property::LineWidth:
                if (value.kind == PDFContentStateValue::Kind::Real && std::isfinite(value.real))
                {
                    state.lineWidth = qAbs(value.real);
                }
                break;

            case Property::LineCap:
                if (value.kind == PDFContentStateValue::Kind::Integer)
                {
                    state.lineCap = PDFPageContentProcessor::convertLineCapToPenCapStyle(value.integer);
                }
                break;

            case Property::LineJoin:
                if (value.kind == PDFContentStateValue::Kind::Integer)
                {
                    state.lineJoin = PDFPageContentProcessor::convertLineJoinToPenJoinStyle(value.integer);
                }
                break;

            case Property::MiterLimit:
                if (value.kind == PDFContentStateValue::Kind::Real && std::isfinite(value.real))
                {
                    state.miterLimit = value.real;
                }
                break;

            case Property::DashPattern:
                if (value.kind == PDFContentStateValue::Kind::Dash)
                {
                    state.dashPattern = value.dash;
                }
                break;

            case Property::AlphaStroking:
                if (value.kind == PDFContentStateValue::Kind::Real && std::isfinite(value.real))
                {
                    state.alphaStroking = qBound(0.0, value.real, 1.0);
                }
                break;

            case Property::AlphaFilling:
                if (value.kind == PDFContentStateValue::Kind::Real && std::isfinite(value.real))
                {
                    state.alphaFilling = qBound(0.0, value.real, 1.0);
                }
                break;

            case Property::BlendMode:
            {
                // A name, or an array of names: the first one known (ISO 32000-2, 11.6.3), else Normal
                BlendMode blendMode = BlendMode::Invalid;
                if (value.kind == PDFContentStateValue::Kind::Name)
                {
                    blendMode = PDFBlendModeInfo::getBlendMode(value.name);
                }
                else if (value.kind == PDFContentStateValue::Kind::Object && value.object.isArray())
                {
                    const PDFArray* array = value.object.getArray();
                    for (size_t i = 0, count = array->getCount(); i < count && blendMode == BlendMode::Invalid; ++i)
                    {
                        const PDFObject& item = array->getItem(i);
                        if (item.isName())
                        {
                            blendMode = PDFBlendModeInfo::getBlendMode(item.getString());
                        }
                    }
                }
                else if (value.kind == PDFContentStateValue::Kind::Object && value.object.isName())
                {
                    blendMode = PDFBlendModeInfo::getBlendMode(value.object.getString());
                }

                if (value.kind == PDFContentStateValue::Kind::Name || value.kind == PDFContentStateValue::Kind::Object)
                {
                    state.blendMode = (blendMode == BlendMode::Invalid) ? BlendMode::Normal : blendMode;
                }
                break;
            }

            case Property::TextRenderingMode:
                if (value.kind == PDFContentStateValue::Kind::Integer && value.integer >= 0 && value.integer <= 7)
                {
                    state.textRenderingMode = static_cast<TextRenderingMode>(value.integer);
                }
                break;

            case Property::StrokeColor:
                if (value.kind == PDFContentStateValue::Kind::Color)
                {
                    state.strokeColor = getOverrideColor(value.color, context, cms, intent);
                }
                break;

            case Property::FillColor:
                if (value.kind == PDFContentStateValue::Kind::Color)
                {
                    state.fillColor = getOverrideColor(value.color, context, cms, intent);
                }
                break;

            case Property::SoftMask:
                if (value.kind == PDFContentStateValue::Kind::Name)
                {
                    state.hasSoftMask = value.name != "None";
                }
                else if (value.kind == PDFContentStateValue::Kind::Object)
                {
                    state.hasSoftMask = !value.object.isNull() && !(value.object.isName() && value.object.getString() == "None");
                }
                break;

            case Property::OverprintStroking:
                if (value.kind == PDFContentStateValue::Kind::Boolean)
                {
                    state.overprintMode.overprintStroking = value.boolean;
                }
                break;

            case Property::OverprintFilling:
                if (value.kind == PDFContentStateValue::Kind::Boolean)
                {
                    state.overprintMode.overprintFilling = value.boolean;
                }
                break;

            case Property::OverprintMode:
                if (value.kind == PDFContentStateValue::Kind::Integer)
                {
                    state.overprintMode.overprintMode = int(value.integer);
                }
                break;

            default:
                // Not a value of the display state (the text properties are the values the text was
                // laid out with, R-2; the others are not drawn by the canvas)
                break;
        }
    }
}

std::optional<PDFContentDisplayState> PDFContentDisplayHelper::computeState(const PDFContentContainer& container,
                                                                            const PDFContentNode& node,
                                                                            const PDFContentDisplayContext& context,
                                                                            const QTransform& containerToTarget,
                                                                            bool isColorNeeded,
                                                                            bool isOverridePossible)
{
    const PDFContentResolvedStatePointer& resolvedState = node.getState();
    if (!resolvedState)
    {
        return std::nullopt;
    }

    std::optional<PDFContentStateOverride> override;
    if (isOverridePossible || node.getOverride())
    {
        override = container.getEffectiveOverride(node.getId());
        if (!override)
        {
            return std::nullopt;
        }
    }

    const PDFPageContentProcessorState& state = resolvedState->getState();
    PDFContentDisplayState result;
    result.matrix = (override ? override->transform : QTransform()) * state.getCurrentTransformationMatrix() * containerToTarget;
    result.lineWidth = state.getLineWidth();
    result.dashPattern = state.getLineDashPattern();
    result.lineCap = state.getLineCapStyle();
    result.lineJoin = state.getLineJoinStyle();
    result.miterLimit = state.getMitterLimit();
    result.alphaStroking = state.getAlphaStroking();
    result.alphaFilling = state.getAlphaFilling();
    result.blendMode = state.getBlendMode();
    result.textRenderingMode = state.getTextRenderingMode();
    result.hasSoftMask = resolvedState->hasSoftMask();
    result.overprintMode = state.getOverprintMode();

    PDFCMSGeneric genericCms;
    const PDFCMS* cms = context.cms ? context.cms : &genericCms;
    if (isColorNeeded)
    {
        result.fillColor = getBuilderColor(state, node.getSymbolicState().get(), false, cms);
        result.strokeColor = getBuilderColor(state, node.getSymbolicState().get(), true, cms);
    }

    if (override)
    {
        PDFContentDisplayContext overrideContext = context;
        if (!isColorNeeded)
        {
            // The colors are not converted: no document, no space is created
            overrideContext.document = nullptr;
        }
        applyOverride(result, *override, overrideContext, cms, state.getRenderingIntent());

        if (!isColorNeeded)
        {
            result.fillColor = PDFContentDisplayColor();
            result.strokeColor = PDFContentDisplayColor();
        }
    }

    makePlain(result.matrix);
    return result;
}

bool PDFContentDisplayHelper::hasOverrideOnPath(const PDFContentContainer& container, const PDFContentNode& node)
{
    // Bounded by the depth of the tree (a hand-made container with a loop of parents ends too)
    PDFContentNodePointer current = container.getNode(node.getId());
    for (int level = 0; current && level <= MAXIMUM_WALK_DEPTH + 1; ++level)
    {
        if (current->getOverride())
        {
            return true;
        }
        current = container.getParent(current->getId());
    }
    return current != nullptr;
}

std::optional<QTransform> PDFContentDisplayHelper::getNodeMatrix(const PDFContentContainer& container, const PDFContentNode& node, bool isOverridePossible)
{
    if (!node.getState())
    {
        return std::nullopt;
    }

    QTransform matrix = node.getState()->getCurrentTransformationMatrix();
    if (isOverridePossible || node.getOverride())
    {
        const std::optional<PDFContentStateOverride> override = container.getEffectiveOverride(node.getId());
        if (!override)
        {
            return std::nullopt;
        }
        matrix = override->transform * matrix;
    }
    return matrix;
}

PDFObject PDFContentDisplayHelper::getPatternObject(const PDFContentContainer& container, const PDFContentNode& node, bool isStroke)
{
    const Property property = isStroke ? Property::StrokeColor : Property::FillColor;
    if (node.getOverride() || node.getParentId().isValid())
    {
        const std::optional<PDFContentStateOverride> override = container.getEffectiveOverride(node.getId());
        if (override)
        {
            auto it = override->properties.find(property);
            if (it != override->properties.cend() && it->second.kind == PDFContentStateValue::Kind::Color)
            {
                return it->second.color.patternObject;
            }
        }
    }

    const PDFContentResolvedStatePointer& state = node.getState();
    const PDFAbstractColorSpace* colorSpace = state ? (isStroke ? state->getState().getStrokeColorSpace() : state->getState().getFillColorSpace()) : nullptr;
    if (!colorSpace || !colorSpace->asPatternColorSpace() || !node.getSymbolicState())
    {
        return PDFObject();
    }
    return node.getSymbolicState()->getSource(property).patternObject;
}

std::optional<QTransform> PDFContentDisplayHelper::getContainerMatrix(const PDFContentContainer& container, const PDFContentDisplayUsePath& uses)
{
    if (uses.empty())
    {
        return QTransform();
    }

    if (uses.size() > size_t(PDFContentDisplay::MAXIMUM_USE_DEPTH))
    {
        return std::nullopt;
    }

    // One visited set for the whole walk: a container met twice is a cycle
    std::set<PDFContentContainerKey> visited;
    visited.insert(container.getKey());

    QTransform result;
    const PDFContentContainer* inner = &container;
    for (auto it = uses.crbegin(); it != uses.crend(); ++it)
    {
        const PDFContentDisplayUse& use = *it;
        if (!use.container || !visited.insert(use.container->getKey()).second)
        {
            return std::nullopt;
        }

        const PDFContentContainerKey& innerKey = inner->getKey();
        const PDFContentNodePointer node = use.container->getNode(use.node);
        if (!node)
        {
            return std::nullopt;
        }

        QTransform levelMatrix;
        switch (innerKey.kind)
        {
            case PDFContentContainerKey::Kind::Form:
            {
                // Form space -> the space of the use (/Matrix, ISO 32000-2, 8.10.1), then as the content draws the use
                if (node->getKind() != PDFContentNodeKind::FormUse || node->getData().xobject != innerKey.reference)
                {
                    return std::nullopt;
                }

                const std::optional<QTransform> useMatrix = getNodeMatrix(*use.container, *node, true);
                if (!useMatrix)
                {
                    return std::nullopt;
                }
                levelMatrix = inner->getMatrix() * *useMatrix;
                break;
            }

            case PDFContentContainerKey::Kind::PatternCell:
            {
                // Pattern space -> the default space of the content, which uses the pattern (8.7.3.1)
                bool isPainted = false;
                for (const bool isStroke : { false, true })
                {
                    const PDFObject patternObject = getPatternObject(*use.container, *node, isStroke);
                    isPainted = isPainted || (patternObject.isReference() && patternObject.getReference() == innerKey.reference);
                }

                if (!isPainted)
                {
                    return std::nullopt;
                }
                levelMatrix = inner->getMatrix();
                break;
            }

            case PDFContentContainerKey::Kind::Page:
                // A page is never drawn inside of another content
                return std::nullopt;
        }

        result = result * levelMatrix;
        inner = use.container.get();
    }

    makePlain(result);
    return result;
}

bool PDFContentDisplayHelper::isParentKind(PDFContentNodeKind kind)
{
    return kind == PDFContentNodeKind::Root || kind == PDFContentNodeKind::Group || kind == PDFContentNodeKind::MarkedContent || kind == PDFContentNodeKind::TextObject;
}

QRectF PDFContentDisplayHelper::getStrokeBoundingBox(const PDFContentPath& path, bool closeBeforePaint, const PDFContentDisplayState& state)
{
    if (state.lineWidth <= 0.0 || !std::isfinite(state.lineWidth))
    {
        // The thinnest line of the device: nothing in the space of the content
        return QRectF();
    }

    // A local painter path of this thread only (never shared)
    QPainterPath painterPath;
    for (const PDFContentPath::Segment& segment : path.getSegments())
    {
        switch (segment.type)
        {
            case PDFContentPath::SegmentType::MoveTo:
                painterPath.moveTo(segment.points[0]);
                break;

            case PDFContentPath::SegmentType::LineTo:
                painterPath.lineTo(segment.points[0]);
                break;

            case PDFContentPath::SegmentType::CurveTo:
                painterPath.cubicTo(segment.points[0], segment.points[1], segment.points[2]);
                break;

            case PDFContentPath::SegmentType::Close:
                painterPath.closeSubpath();
                break;
        }
    }

    if (closeBeforePaint)
    {
        painterPath.closeSubpath();
    }

    // The solid stroke as the processor strokes it (the miter limit of PDF given to Qt as it is)
    QPainterPathStroker stroker;
    stroker.setWidth(state.lineWidth);
    stroker.setCapStyle(state.lineCap);
    stroker.setJoinStyle(state.lineJoin);
    stroker.setMiterLimit(state.miterLimit);
    const QPainterPath outline = stroker.createStroke(painterPath);
    return state.matrix.map(outline).boundingRect();
}

PDFObject PDFContentDisplayHelper::getResource(const PDFContentContainer& container,
                                               PDFContentResourceCategory category,
                                               const QByteArray& name,
                                               const PDFDocument* document)
{
    PDFObject object = container.getResources().getObject(category, name);
    if (object.isNull() && document && container.getInheritedResourcesPageIndex() >= 0)
    {
        // A form or a pattern cell without /Resources of its own: the resources of the page (7.8.3)
        const PDFCatalog* catalog = document->getCatalog();
        const size_t pageIndex = size_t(container.getInheritedResourcesPageIndex());
        const PDFPage* page = catalog && pageIndex < catalog->getPageCount() ? catalog->getPage(pageIndex) : nullptr;
        const PDFDictionary* resources = page ? document->getDictionaryFromObject(page->getResources()) : nullptr;
        const PDFDictionary* categoryDictionary = resources ? document->getDictionaryFromObject(resources->get(PDFContentResources::getCategoryKey(category))) : nullptr;
        if (categoryDictionary)
        {
            object = categoryDictionary->get(name);
        }
    }
    return document ? document->getObject(object) : object;
}

QRectF PDFContentDisplayHelper::getGlyphBox(const PDFContentDisplayGlyph& glyph)
{
    if (glyph.outline && !glyph.outline->isEmpty())
    {
        return glyph.matrix.mapRect(glyph.outline->getBoundingRect());
    }

    if (!glyph.cell.isEmpty())
    {
        return glyph.matrix.mapRect(glyph.cell);
    }

    return QRectF();
}

PDFContentDisplayBox PDFContentDisplayHelper::getLeafBoundingBox(const PDFContentContainer& container,
                                                                 const PDFContentNode& node,
                                                                 const PDFContentDisplayContext& context,
                                                                 const QTransform& containerToTarget,
                                                                 bool isOverridePossible)
{
    PDFContentDisplayBox result;
    result.isValid = true;

    const PDFContentNodeKind kind = node.getKind();
    switch (kind)
    {
        case PDFContentNodeKind::StateRun:
        case PDFContentNodeKind::MarkedPoint:
            return result;

        case PDFContentNodeKind::TextShow:
        {
            // The glyphs of the show
            PDFContentDisplayText text;
            std::vector<PDFReal> cellEnds;
            addTextShow(text, cellEnds, container, node, context, containerToTarget);
            for (const PDFContentDisplayGlyph& glyph : text.glyphs)
            {
                const QRectF box = getGlyphBox(glyph);
                if (!box.isNull())
                {
                    result.box = result.box.isNull() ? box : result.box.united(box);
                }
            }
            return result;
        }

        default:
            break;
    }

    const std::optional<QTransform> nodeMatrix = getNodeMatrix(container, node, isOverridePossible);
    if (!nodeMatrix)
    {
        // A matrix on the way cannot be inverted: the box is not known
        result.isValid = false;
        return result;
    }
    const QTransform matrix = *nodeMatrix * containerToTarget;

    // The box of the builder moved by the override and the target: D = CTM^-1 x T x CTM x containerToTarget
    auto getMovedBuilderBox = [&node, &nodeMatrix, &containerToTarget]() -> QRectF
    {
        const QRectF& box = node.getBoundingBox();
        if (box.isNull())
        {
            return QRectF();
        }

        const QTransform& currentTransformationMatrix = node.getState()->getCurrentTransformationMatrix();
        if (!currentTransformationMatrix.isInvertible())
        {
            return containerToTarget.mapRect(box);
        }
        return (currentTransformationMatrix.inverted() * *nodeMatrix * containerToTarget).mapRect(box);
    };

    switch (kind)
    {
        case PDFContentNodeKind::Path:
        {
            const PDFContentNode::Data& data = node.getData();
            result.box = data.path.transformed(matrix).getBoundingBox();
            if (data.paint.stroke && !data.path.isEmpty())
            {
                const std::optional<PDFContentDisplayState> state = computeState(container, node, context, containerToTarget, false, isOverridePossible);
                if (state)
                {
                    const QRectF strokeBox = getStrokeBoundingBox(data.path, data.paint.closeBeforePaint, *state);
                    if (!strokeBox.isNull())
                    {
                        result.box = result.box.isNull() ? strokeBox : result.box.united(strokeBox);
                    }
                }
            }
            break;
        }

        case PDFContentNodeKind::Image:
            result.box = matrix.mapRect(QRectF(0.0, 0.0, 1.0, 1.0));
            break;

        case PDFContentNodeKind::FormUse:
        {
            // /BBox of the form under its /Matrix and the matrix of the use (ISO 32000-2, 8.10.1)
            bool isDone = false;
            if (context.document && node.getData().xobject.isValid())
            {
                try
                {
                    const PDFObject formObject = context.document->getObject(PDFObject::createReference(node.getData().xobject));
                    const PDFStream* stream = formObject.isStream() ? formObject.getStream() : nullptr;
                    const PDFDictionary* dictionary = stream ? stream->getDictionary() : nullptr;
                    if (dictionary && dictionary->hasKey("BBox"))
                    {
                        PDFDocumentDataLoaderDecorator loader(context.document);
                        const QRectF formBox = loader.readRectangle(dictionary->get("BBox"), QRectF()).normalized();
                        const QTransform formMatrix = loader.readMatrixFromDictionary(dictionary, "Matrix", QTransform());
                        result.box = (formMatrix * matrix).mapRect(formBox);
                        isDone = true;
                    }
                }
                catch (const PDFException&)
                {
                    isDone = false;
                }
            }

            if (!isDone)
            {
                result.box = getMovedBuilderBox();
                result.isUnknown = result.box.isNull();
            }
            break;
        }

        case PDFContentNodeKind::Shading:
        {
            // sh paints the clip region (ISO 32000-2, 8.7.4.2): /BBox of the shading and the clips in effect
            std::optional<QRectF> box;
            if (context.document)
            {
                try
                {
                    const PDFObject shadingObject = getResource(container, PDFContentResourceCategory::Shading, node.getData().resourceName, context.document);
                    const PDFDictionary* dictionary = shadingObject.isStream() ? shadingObject.getStream()->getDictionary() : (shadingObject.isDictionary() ? shadingObject.getDictionary() : nullptr);
                    if (dictionary && dictionary->hasKey("BBox"))
                    {
                        PDFDocumentDataLoaderDecorator loader(context.document);
                        box = matrix.mapRect(loader.readRectangle(dictionary->get("BBox"), QRectF()).normalized());
                    }
                }
                catch (const PDFException&)
                {
                    box = std::nullopt;
                }
            }

            PDFContentDisplayClipChain chain;
            chain.isValid = true;
            collectClips(container, node, chain);
            for (const PDFContentDisplayClip& clip : chain.clips)
            {
                QRectF clipBox;
                if (clip.isText)
                {
                    const PDFContentNodePointer textObject = container.getNode(clip.node);
                    const PDFContentDisplayTextPointer text = textObject ? getText(container, *textObject, context, containerToTarget) : nullptr;
                    clipBox = text ? text->boundingBox : QRectF();
                }
                else
                {
                    clipBox = clip.path.transformed(containerToTarget).getBoundingBox();
                }
                box = box ? box->intersected(clipBox) : clipBox;
            }

            if (box)
            {
                result.box = *box;
            }
            else
            {
                result.isUnknown = true;
            }
            break;
        }

        case PDFContentNodeKind::Opaque:
            result.box = getMovedBuilderBox();
            result.isUnknown = result.box.isNull() && !node.getPrefix().isEmpty();
            break;

        default:
            // A kind with children is measured by its children (getBoundingBox)
            result.box = getMovedBuilderBox();
            break;
    }

    return result;
}

PDFContentDisplayBox PDFContentDisplayHelper::getBoundingBox(const PDFContentContainer& container,
                                                             const PDFContentNode& start,
                                                             const PDFContentDisplayContext& context,
                                                             const QTransform& containerToTarget)
{
    PDFContentDisplayBox result;
    result.isValid = true;

    // A walk over the subtree (a tree: every node once); bounded by the depth of the tree. The
    // effective override is asked only below a node with an override (its cost is the depth).
    struct Item
    {
        const PDFContentNode* node = nullptr;
        int depth = 0;
        bool isOverrideAbove = false;
    };

    std::vector<Item> stack;
    stack.push_back(Item{ &start, 0, hasOverrideOnPath(container, start) });
    while (!stack.empty())
    {
        const Item item = stack.back();
        stack.pop_back();

        const PDFContentNodeKind kind = item.node->getKind();
        const bool isOverridePossible = item.isOverrideAbove || item.node->getOverride();
        PDFContentDisplayBox box;
        if (isParentKind(kind) && item.depth <= MAXIMUM_WALK_DEPTH && kind != PDFContentNodeKind::TextObject)
        {
            for (const PDFContentNodePointer& child : item.node->getChildren())
            {
                if (child)
                {
                    stack.push_back(Item{ child.get(), item.depth + 1, isOverridePossible });
                }
            }
            continue;
        }
        else if (kind == PDFContentNodeKind::TextObject)
        {
            // The union of its glyph boxes
            box.isValid = true;
            const PDFContentDisplayTextPointer text = getText(container, *item.node, context, containerToTarget);
            box.box = text ? text->boundingBox : QRectF();
        }
        else if (isParentKind(kind))
        {
            // Deeper than the builder goes: the box of the builder of the whole subtree
            box.isValid = true;
            box.box = containerToTarget.mapRect(item.node->getBoundingBox());
        }
        else
        {
            box = getLeafBoundingBox(container, *item.node, context, containerToTarget, isOverridePossible);
        }

        if (!box.isValid && item.node == &start)
        {
            result.isValid = false;
        }
        result.isUnknown = result.isUnknown || box.isUnknown;
        if (!box.box.isNull())
        {
            result.box = result.box.isNull() ? box.box : result.box.united(box.box);
        }
    }

    return result;
}

void PDFContentDisplayHelper::addTextShow(PDFContentDisplayText& text,
                                          std::vector<PDFReal>& cellEnds,
                                          const PDFContentContainer& container,
                                          const PDFContentNode& show,
                                          const PDFContentDisplayContext& context,
                                          const QTransform& containerToTarget)
{
    const std::optional<PDFContentDisplayState> state = computeState(container, show, context, containerToTarget, true, true);
    if (!state)
    {
        return;
    }

    const PDFContentNode::Data& data = show.getData();
    const PDFContentTextState& textState = data.textState;

    PDFContentDisplayTextRun run;
    run.node = show.getId();
    run.state = *state;
    run.firstGlyph = text.glyphs.size();
    run.isType3 = textState.font && textState.font->getFontType() == FontType::Type3;
    run.isVertical = textState.font && textState.font->getCMap() && textState.font->getCMap()->isVertical();
    run.isInvisible = !run.isType3 && state->textRenderingMode == TextRenderingMode::Invisible;
    run.isClipping = !run.isType3 && isTextRenderingModeClipped(state->textRenderingMode);
    run.isComplete = data.isTextComplete;

    if (textState.realizedFont)
    {
        bool isKnown = false;
        for (const PDFRealizedFontPointer& font : text.realizedFonts)
        {
            isKnown = isKnown || font == textState.realizedFont;
        }
        if (!isKnown)
        {
            text.realizedFonts.push_back(textState.realizedFont);
        }
    }

    // The text rendering matrix of 9.4.4 without the font size: the outlines of a realized font are
    // made for the absolute font size (PDFContentTextItem::outline)
    const PDFReal horizontalScaling = textState.horizontalScaling * 0.01;
    const PDFReal sign = textState.fontSize < 0.0 ? -1.0 : 1.0;
    const PDFReal absoluteFontSize = qAbs(textState.fontSize);
    const QTransform outlineMatrix(horizontalScaling * sign, 0.0, 0.0, sign, 0.0, textState.rise);
    const size_t runIndex = text.runs.size();

    for (size_t i = 0; i < data.textItems.size(); ++i)
    {
        const PDFContentTextItem& item = data.textItems[i];
        if (item.kind != PDFContentTextItem::Kind::Glyph)
        {
            continue;
        }

        // The cell along the advance: w0 x |Tfs| in the glyph space (x_text = Th x s x x_glyph), else the move of Tm
        PDFReal cellWidth = 0.0;
        if (item.width.has_value())
        {
            cellWidth = *item.width * absoluteFontSize;
        }
        else if (horizontalScaling != 0.0)
        {
            cellWidth = item.advance / (horizontalScaling * sign);
        }

        PDFContentDisplayGlyph glyph;
        glyph.outline = textState.realizedFont ? item.outline : nullptr;
        glyph.cell = QRectF(0.0, -0.2 * absoluteFontSize, cellWidth, 1.2 * absoluteFontSize).normalized();
        glyph.textMatrix = item.textMatrix * state->matrix;
        glyph.matrix = outlineMatrix * glyph.textMatrix;
        glyph.codeBytes = item.codeBytes;
        glyph.code = item.code;
        glyph.unicode = item.unicode;
        glyph.run = runIndex;
        glyph.itemIndex = i;
        makePlain(glyph.textMatrix);
        makePlain(glyph.matrix);

        const QRectF box = getGlyphBox(glyph);
        if (!box.isNull())
        {
            text.boundingBox = text.boundingBox.isNull() ? box : text.boundingBox.united(box);
        }

        text.glyphs.push_back(std::move(glyph));
        cellEnds.push_back(cellWidth * horizontalScaling * sign);
    }

    run.glyphCount = text.glyphs.size() - run.firstGlyph;
    text.runs.push_back(std::move(run));
}

void PDFContentDisplayHelper::splitLines(PDFContentDisplayText& text, const std::vector<PDFReal>& cellEnds)
{
    // The relative tolerance of a base line: 0.1 % of the font size (zadani_jadro.md 4.7)
    constexpr PDFReal TOLERANCE = 0.001;

    auto isSameLine = [&text](const PDFContentDisplayGlyph& previous, const PDFContentDisplayGlyph& next) -> bool
    {
        const PDFContentDisplayTextRun& previousRun = text.runs[previous.run];
        const PDFContentDisplayTextRun& nextRun = text.runs[next.run];
        if (previousRun.isVertical != nextRun.isVertical || !previous.textMatrix.isInvertible())
        {
            return false;
        }

        // The text space of the next glyph in the text space of the previous one
        const QTransform relative = next.textMatrix * previous.textMatrix.inverted();

        // |Tfs| of the previous glyph: its cell is 1.2 |Tfs| high (addTextShow)
        const PDFReal fontSize = qAbs(previous.cell.height()) / 1.2;
        const PDFReal limit = TOLERANCE * fontSize;
        if (!previousRun.isVertical)
        {
            return qAbs(relative.dy()) <= limit && relative.m11() > 0.0 && qAbs(relative.m12()) <= TOLERANCE * relative.m11();
        }
        return qAbs(relative.dx()) <= limit && relative.m22() > 0.0 && qAbs(relative.m21()) <= TOLERANCE * relative.m22();
    };

    auto finishLine = [&text, &cellEnds](PDFContentDisplayTextLine& line)
    {
        const PDFContentDisplayGlyph& first = text.glyphs[line.firstGlyph];
        const PDFContentDisplayGlyph& last = text.glyphs[line.firstGlyph + line.glyphCount - 1];
        line.isVertical = text.runs[first.run].isVertical;
        line.isInvisible = true;
        line.isClipping = false;

        const bool isLineMatrixInvertible = first.textMatrix.isInvertible();
        line.matrix = isLineMatrixInvertible ? first.textMatrix : QTransform();
        const QTransform targetToLine = isLineMatrixInvertible ? first.textMatrix.inverted() : QTransform();

        for (size_t i = line.firstGlyph; i < line.firstGlyph + line.glyphCount; ++i)
        {
            const PDFContentDisplayGlyph& glyph = text.glyphs[i];
            const PDFContentDisplayTextRun& run = text.runs[glyph.run];
            line.isInvisible = line.isInvisible && run.isInvisible;
            line.isClipping = line.isClipping || run.isClipping;

            const QRectF box = getGlyphBox(glyph);
            if (!box.isNull())
            {
                line.boundingBox = line.boundingBox.isNull() ? box : line.boundingBox.united(box);
            }

            // The rectangle in the line space: the outline box and the cell
            const QTransform glyphToLine = glyph.matrix * targetToLine;
            for (const QRectF& glyphRectangle : { glyph.outline && !glyph.outline->isEmpty() ? glyph.outline->getBoundingRect() : QRectF(), glyph.cell })
            {
                if (!glyphRectangle.isNull())
                {
                    const QRectF lineRectangle = glyphToLine.mapRect(glyphRectangle);
                    line.rectangle = line.rectangle.isNull() ? lineRectangle : line.rectangle.united(lineRectangle);
                }
            }
        }

        if (!isLineMatrixInvertible)
        {
            line.rectangle = line.boundingBox;
        }

        const QPointF start = first.textMatrix.map(QPointF(0.0, 0.0));
        const QPointF end = line.isVertical ? last.textMatrix.map(QPointF(0.0, 0.0))
                                            : last.textMatrix.map(QPointF(cellEnds[line.firstGlyph + line.glyphCount - 1], 0.0));
        line.baseline = QLineF(start, end);
        makePlain(line.matrix);
    };

    PDFContentDisplayTextLine line;
    for (size_t i = 0; i < text.glyphs.size(); ++i)
    {
        if (i > 0 && !isSameLine(text.glyphs[i - 1], text.glyphs[i]))
        {
            finishLine(line);
            text.lines.push_back(std::move(line));
            line = PDFContentDisplayTextLine();
        }

        if (line.glyphCount == 0)
        {
            line.firstGlyph = i;
        }
        ++line.glyphCount;
    }

    if (line.glyphCount > 0)
    {
        finishLine(line);
        text.lines.push_back(std::move(line));
    }
}

PDFContentDisplayTextPointer PDFContentDisplayHelper::getText(const PDFContentContainer& container,
                                                              const PDFContentNode& textObject,
                                                              const PDFContentDisplayContext& context,
                                                              const QTransform& containerToTarget)
{
    if (textObject.getKind() != PDFContentNodeKind::TextObject || !getNodeMatrix(container, textObject, true))
    {
        return nullptr;
    }

    auto text = std::make_shared<PDFContentDisplayText>();
    text->node = textObject.getId();
    std::vector<PDFReal> cellEnds;

    // The text shows in the order of the content (also inside of marked content of the text object);
    // a tree, so every node once, bounded by the depth of the tree
    struct Item
    {
        const PDFContentNode* node = nullptr;
        int depth = 0;
    };

    std::vector<Item> stack;
    stack.push_back(Item{ &textObject, 0 });
    while (!stack.empty())
    {
        const Item item = stack.back();
        stack.pop_back();

        if (item.node->getKind() == PDFContentNodeKind::TextShow)
        {
            addTextShow(*text, cellEnds, container, *item.node, context, containerToTarget);
            continue;
        }

        if (isParentKind(item.node->getKind()) && item.depth <= MAXIMUM_WALK_DEPTH)
        {
            const std::vector<PDFContentNodePointer>& children = item.node->getChildren();
            for (auto it = children.crbegin(); it != children.crend(); ++it)
            {
                if (*it)
                {
                    stack.push_back(Item{ it->get(), item.depth + 1 });
                }
            }
        }
    }

    splitLines(*text, cellEnds);

    bool isAnyGlyphRun = false;
    bool isEveryRunInvisible = true;
    for (const PDFContentDisplayTextRun& run : text->runs)
    {
        text->isClipping = text->isClipping || run.isClipping;
        if (run.glyphCount > 0)
        {
            isAnyGlyphRun = true;
            isEveryRunInvisible = isEveryRunInvisible && run.isInvisible;
        }
    }
    text->isInvisible = isAnyGlyphRun && isEveryRunInvisible;

    return text;
}

void PDFContentDisplayHelper::collectClips(const PDFContentContainer& container, const PDFContentNode& node, PDFContentDisplayClipChain& chain)
{
    // The path from the root to the node (bounded by the depth of the tree)
    std::vector<PDFContentNodePointer> path;
    PDFContentNodePointer current = container.getNode(node.getId());
    while (current && path.size() <= size_t(MAXIMUM_WALK_DEPTH) + 1)
    {
        path.push_back(current);
        current = container.getParent(current->getId());
    }
    std::reverse(path.begin(), path.end());

    struct Item
    {
        const PDFContentNode* node = nullptr;
        int depth = 0;
    };

    for (size_t level = 0; level + 1 < path.size(); ++level)
    {
        const PDFContentNode& ancestor = *path[level];
        if (ancestor.getKind() == PDFContentNodeKind::TextObject)
        {
            // A clip by text of an earlier show becomes real at ET only (ISO 32000-2, 9.3.6)
            continue;
        }

        const PDFInteger index = container.getIndexInParent(path[level + 1]->getId());
        const std::vector<PDFContentNodePointer>& children = ancestor.getChildren();
        for (PDFInteger i = 0; i < index && size_t(i) < children.size(); ++i)
        {
            // The siblings before, in the order of the content; a marked content leaks the clips of
            // its children, a group restores the state at its end (Q)
            std::vector<Item> stack;
            stack.push_back(Item{ children[size_t(i)].get(), int(level) + 1 });
            while (!stack.empty())
            {
                const Item item = stack.back();
                stack.pop_back();
                if (!item.node)
                {
                    continue;
                }

                const PDFContentNode::Data& data = item.node->getData();
                switch (item.node->getKind())
                {
                    case PDFContentNodeKind::Path:
                    {
                        if (data.paint.clip == PDFContentPathPaint::Clip::None)
                        {
                            break;
                        }

                        const std::optional<QTransform> matrix = getNodeMatrix(container, *item.node, true);
                        if (!matrix)
                        {
                            chain.isUnknown = true;
                            break;
                        }

                        PDFContentDisplayClip clip;
                        clip.node = item.node->getId();
                        clip.path = data.path.transformed(*matrix);
                        clip.fillRule = data.paint.clip == PDFContentPathPaint::Clip::EvenOdd ? Qt::OddEvenFill : Qt::WindingFill;
                        chain.clips.push_back(std::move(clip));
                        break;
                    }

                    case PDFContentNodeKind::TextObject:
                        if (data.clipsByText)
                        {
                            PDFContentDisplayClip clip;
                            clip.node = item.node->getId();
                            clip.isText = true;
                            chain.clips.push_back(std::move(clip));
                        }
                        break;

                    case PDFContentNodeKind::MarkedContent:
                        if (item.node->getLeak().isClipChanged && item.depth <= MAXIMUM_WALK_DEPTH)
                        {
                            const std::vector<PDFContentNodePointer>& innerChildren = item.node->getChildren();
                            for (auto it = innerChildren.crbegin(); it != innerChildren.crend(); ++it)
                            {
                                stack.push_back(Item{ it->get(), item.depth + 1 });
                            }
                        }
                        else if (item.node->getLeak().isClipChanged || item.node->getLeak().isUnknown)
                        {
                            chain.isUnknown = true;
                        }
                        break;

                    case PDFContentNodeKind::Opaque:
                        if (item.node->getLeak().isUnknown || item.node->getLeak().isClipChanged)
                        {
                            chain.isUnknown = true;
                        }
                        break;

                    default:
                        break;
                }
            }
        }
    }
}

PDFContentDisplayCounts PDFContentDisplayHelper::computeCounts(const PDFContentContainer& container)
{
    PDFContentDisplayCounts counts;
    std::vector<const PDFContentNode*> stack;
    if (container.getRoot())
    {
        stack.push_back(container.getRoot().get());
    }

    // A tree: every node once (the stack is explicit, the depth does not matter)
    while (!stack.empty())
    {
        const PDFContentNode* node = stack.back();
        stack.pop_back();
        ++counts.nodeCount;

        switch (node->getKind())
        {
            case PDFContentNodeKind::Path:
                ++counts.pathCount;
                counts.pathSegmentCount += node->getData().path.getSegments().size();
                break;

            case PDFContentNodeKind::TextShow:
                for (const PDFContentTextItem& item : node->getData().textItems)
                {
                    if (item.kind == PDFContentTextItem::Kind::Glyph)
                    {
                        ++counts.glyphCount;
                    }
                }
                break;

            case PDFContentNodeKind::Image:
                ++counts.imageCount;
                break;

            default:
                break;
        }

        for (const PDFContentNodePointer& child : node->getChildren())
        {
            if (child)
            {
                stack.push_back(child.get());
            }
        }
    }

    return counts;
}

QMutex& PDFContentDisplayHelper::getCountMutex()
{
    static QMutex mutex;
    return mutex;
}

std::map<quint64, PDFContentDisplayCounts>& PDFContentDisplayHelper::getCountCache()
{
    static std::map<quint64, PDFContentDisplayCounts> cache;
    return cache;
}

std::deque<quint64>& PDFContentDisplayHelper::getCountOrder()
{
    static std::deque<quint64> order;
    return order;
}

/// Decodes the image of one Image node by the image decoding of the processor (item 4): the
/// operation of the node is processed as a content of its own under the resources of its container,
/// the image is taken before the processor paints it. A local object of one call in one thread; it
/// creates and destroys its own font cache and optional content activity.
class PDFContentDisplayImageProcessor : public PDFPageContentProcessor
{
public:
    explicit PDFContentDisplayImageProcessor(const PDFPage* page,
                                             const PDFDocument* document,
                                             const PDFCMS* cms,
                                             const PDFICCProfileCache* iccProfileCache,
                                             const PDFOperationControl* control);
    virtual ~PDFContentDisplayImageProcessor() override = default;

    /// The resources of the content as the container knows them (also the entries an editor has
    /// added); null for a container without resources of its own (the processor takes the page's)
    static PDFObject createResources(const PDFContentContainer& container);

    /// Processes the operation of the image
    void decode(const PDFObject& resources, const QByteArray& content, const PDFContentDisplayState& state);

    PDFContentDisplayImage takeResult() { return std::move(m_result); }

protected:
    virtual bool isOriginalImageRequired(const PDFStream* stream, PDFObjectReference reference) override;
    virtual bool performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference) override;
    virtual bool isContentSuppressedByOC(PDFObjectReference ocgOrOcmd) override;
    virtual bool isTextIgnored() const override { return true; }

private:
    /// /Width x /Height of an image stream: 0, when they are not positive numbers
    qint64 getPixelCount(const PDFStream* stream) const;

    const PDFOperationControl* m_control;
    PDFContentDisplayImage m_result;
    QColor m_fillColor;
    bool m_isImageSeen = false;
};

PDFContentDisplayImageProcessor::PDFContentDisplayImageProcessor(const PDFPage* page,
                                                                 const PDFDocument* document,
                                                                 const PDFCMS* cms,
                                                                 const PDFICCProfileCache* iccProfileCache,
                                                                 const PDFOperationControl* control) :
    PDFPageContentProcessor(page, document, nullptr, iccProfileCache, cms, nullptr, QTransform(), PDFMeshQualitySettings()),
    m_control(control)
{
    setOperationControl(control);
}

PDFObject PDFContentDisplayImageProcessor::createResources(const PDFContentContainer& container)
{
    const PDFContentResources& resources = container.getResources();
    PDFDictionaryBuilder dictionary;
    bool isEmpty = true;
    for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
    {
        const std::vector<PDFContentResources::Entry>& entries = resources.getEntries(category);
        if (entries.empty())
        {
            continue;
        }

        PDFDictionaryBuilder categoryDictionary;
        for (const PDFContentResources::Entry& entry : entries)
        {
            categoryDictionary.addEntry(PDFInplaceOrMemoryString(entry.name), PDFObject(entry.object));
        }
        dictionary.addEntry(PDFInplaceOrMemoryString(PDFContentResources::getCategoryKey(category)), PDFObject::createDictionary(std::move(categoryDictionary)));
        isEmpty = false;
    }

    if (isEmpty && container.getKey().kind != PDFContentContainerKey::Kind::Page)
    {
        return PDFObject();
    }
    return PDFObject::createDictionary(std::move(dictionary));
}

void PDFContentDisplayImageProcessor::decode(const PDFObject& resources, const QByteArray& content, const PDFContentDisplayState& state)
{
    m_fillColor = state.fillColor.color.isValid() ? state.fillColor.color : QColor(Qt::black);
    initializeProcessor();

    try
    {
        processForm(QTransform(), QRectF(), resources, PDFObject(), content, -1);
    }
    catch (const PDFException& exception)
    {
        m_result.status = PDFContentDisplayImage::Status::Failed;
        m_result.errorText = exception.getMessage();
    }
    catch (const PDFRendererException& exception)
    {
        m_result.status = PDFContentDisplayImage::Status::Failed;
        m_result.errorText = exception.getError().message;
    }

    if (PDFOperationControl::isOperationCanceled(m_control) && m_result.status != PDFContentDisplayImage::Status::TooLarge)
    {
        m_result.status = PDFContentDisplayImage::Status::Canceled;
        m_result.image = QImage();
        return;
    }

    if (m_result.status == PDFContentDisplayImage::Status::NotImage)
    {
        // The processor refused the operation before the image (a missing resource, a wrong
        // inline image): the reason is its error
        m_result.status = PDFContentDisplayImage::Status::Failed;
        const QList<PDFRenderError>& errors = getRenderErrors();
        m_result.errorText = !errors.isEmpty() ? errors.front().message : PDFTranslationContext::tr("The image cannot be decoded.");
    }
}

qint64 PDFContentDisplayImageProcessor::getPixelCount(const PDFStream* stream) const
{
    const PDFDictionary* dictionary = stream ? stream->getDictionary() : nullptr;
    if (!dictionary)
    {
        return 0;
    }

    PDFDocumentDataLoaderDecorator loader(getDocument());
    const PDFInteger width = loader.readIntegerFromDictionary(dictionary, "Width", 0);
    const PDFInteger height = loader.readIntegerFromDictionary(dictionary, "Height", 0);
    if (width <= 0 || height <= 0 || width > PDFContentDisplay::MAXIMUM_DECODED_PIXELS || height > PDFContentDisplay::MAXIMUM_DECODED_PIXELS)
    {
        // Not positive, or so large that the product would not fit
        return (width <= 0 || height <= 0) ? 0 : PDFContentDisplay::MAXIMUM_DECODED_PIXELS + 1;
    }
    return qint64(width) * qint64(height);
}

bool PDFContentDisplayImageProcessor::isOriginalImageRequired(const PDFStream* stream, PDFObjectReference reference)
{
    Q_UNUSED(reference);

    if (m_isImageSeen)
    {
        // One image only (the operation of the node)
        return false;
    }
    m_isImageSeen = true;

    // The image and its masks are refused before any decoding (zadani_overeni.md 6.2)
    const PDFDictionary* dictionary = stream ? stream->getDictionary() : nullptr;
    std::vector<qint64> pixelCounts = { getPixelCount(stream) };
    for (const char* key : { "SMask", "Mask" })
    {
        const PDFObject maskObject = dictionary ? getDocument()->getObject(dictionary->get(key)) : PDFObject();
        if (maskObject.isStream())
        {
            pixelCounts.push_back(getPixelCount(maskObject.getStream()));
        }
    }

    for (const qint64 pixelCount : pixelCounts)
    {
        if (pixelCount <= 0 || pixelCount > PDFContentDisplay::MAXIMUM_DECODED_PIXELS)
        {
            m_result.status = PDFContentDisplayImage::Status::TooLarge;
            m_result.errorText = PDFTranslationContext::tr("The image is too large to be shown.");
            break;
        }
    }

    if (dictionary)
    {
        PDFDocumentDataLoaderDecorator loader(getDocument());
        const PDFInteger maximumSide = std::numeric_limits<int>::max();
        const PDFInteger width = std::clamp<PDFInteger>(loader.readIntegerFromDictionary(dictionary, "Width", 0), 0, maximumSide);
        const PDFInteger height = std::clamp<PDFInteger>(loader.readIntegerFromDictionary(dictionary, "Height", 0), 0, maximumSide);
        m_result.sourceSize = QSize(int(width), int(height));
    }

    return m_result.status != PDFContentDisplayImage::Status::TooLarge;
}

bool PDFContentDisplayImageProcessor::performOriginalImagePainting(const PDFImage& image, const PDFStream* stream, PDFObjectReference reference)
{
    Q_UNUSED(stream);
    Q_UNUSED(reference);

    try
    {
        QImage decoded = image.getImage(getCMS(), this, m_control);
        if (decoded.isNull() || PDFOperationControl::isOperationCanceled(m_control))
        {
            m_result.status = decoded.isNull() ? PDFContentDisplayImage::Status::Failed : PDFContentDisplayImage::Status::Canceled;
            m_result.errorText = PDFTranslationContext::tr("The image cannot be decoded.");
            return true;
        }

        if (decoded.format() == QImage::Format_Alpha8)
        {
            // A stencil mask (ISO 32000-2, 8.9.6.2): painted with the fill color
            QImage painted(decoded.size(), QImage::Format_ARGB32_Premultiplied);
            PDFAbstractColorSpace::checkImageAllocated(painted);
            painted.fill(m_fillColor);
            painted.setAlphaChannel(decoded);
            decoded = std::move(painted);
            m_result.isImageMask = true;
        }

        m_result.image = decoded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        m_result.status = m_result.image.isNull() ? PDFContentDisplayImage::Status::Failed : PDFContentDisplayImage::Status::Decoded;
    }
    catch (const PDFException& exception)
    {
        m_result.status = PDFContentDisplayImage::Status::Failed;
        m_result.errorText = exception.getMessage();
    }
    catch (const PDFRendererException& exception)
    {
        m_result.status = PDFContentDisplayImage::Status::Failed;
        m_result.errorText = exception.getError().message;
    }

    // The processor does not convert the image again
    return true;
}

bool PDFContentDisplayImageProcessor::isContentSuppressedByOC(PDFObjectReference ocgOrOcmd)
{
    // The canvas shows hidden content too (the layers are its own state)
    Q_UNUSED(ocgOrOcmd);
    return false;
}

PDFObject PDFContentDisplayHelper::getResourceDictionary(const PDFContentContainer& container, const PDFDocument* document)
{
    PDFObject resources = PDFContentDisplayImageProcessor::createResources(container);
    if (resources.isNull() && document && container.getInheritedResourcesPageIndex() >= 0)
    {
        const PDFCatalog* catalog = document->getCatalog();
        const size_t pageIndex = size_t(container.getInheritedResourcesPageIndex());
        const PDFPage* page = catalog && pageIndex < catalog->getPageCount() ? catalog->getPage(pageIndex) : nullptr;
        resources = page ? document->getObject(page->getResources()) : PDFObject();
    }
    return resources;
}

/// Paints one cell of a tiling pattern into an image by the painter of the renderer (item 7). A local
/// object of one call in one thread.
class PDFContentDisplayCellPainter : public PDFPainter
{
public:
    explicit PDFContentDisplayCellPainter(QPainter* painter,
                                          const QTransform& patternToImage,
                                          const PDFPage* page,
                                          const PDFDocument* document,
                                          const PDFFontCache* fontCache,
                                          const PDFICCProfileCache* iccProfileCache,
                                          const PDFCMS* cms,
                                          const PDFOptionalContentActivity* activity,
                                          const PDFOperationControl* control);

    /// Paints the cell; an uncolored cell (/PaintType 2) in \p color (ISO 32000-2, 8.7.3.3)
    void paintCell(const PDFTilingPattern& pattern, const QColor& color);
};

PDFContentDisplayCellPainter::PDFContentDisplayCellPainter(QPainter* painter,
                                                           const QTransform& patternToImage,
                                                           const PDFPage* page,
                                                           const PDFDocument* document,
                                                           const PDFFontCache* fontCache,
                                                           const PDFICCProfileCache* iccProfileCache,
                                                           const PDFCMS* cms,
                                                           const PDFOptionalContentActivity* activity,
                                                           const PDFOperationControl* control) :
    PDFPainter(painter, PDFRenderer::Features(PDFRenderer::Antialiasing | PDFRenderer::TextAntialiasing | PDFRenderer::IgnoreOptionalContent),
               patternToImage, page, document, fontCache, iccProfileCache, cms, activity, PDFMeshQualitySettings())
{
    setOperationControl(control);
}

void PDFContentDisplayCellPainter::paintCell(const PDFTilingPattern& pattern, const QColor& color)
{
    initializeProcessor();

    const bool isUncolored = pattern.getPaintingType() == PDFTilingPattern::PaintType::Uncolored;
    if (isUncolored)
    {
        PDFPageContentProcessorState state = *getGraphicState();
        const QColor paintColor = color.isValid() ? color : QColor(Qt::black);
        state.setFillColor(paintColor, PDFColor());
        state.setStrokeColor(paintColor, PDFColor());
        setGraphicsState(state);
    }

    // The cell of an uncolored pattern specifies no color: its color operators are ignored
    PDFUncoloredTilingPatternGuard guard(this, isUncolored);
    processForm(QTransform(), pattern.getBoundingBox(), pattern.getResources(), PDFObject(), pattern.getContent(), -1);
}

// ---------------------------------------------------------------- PDFContentDisplay

std::optional<PDFContentDisplayState> PDFContentDisplay::getDisplayState(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context)
{
    const PDFContentNodePointer node = container.getNode(id);
    if (!node)
    {
        return std::nullopt;
    }
    return PDFContentDisplayHelper::computeState(container, *node, context, QTransform(), true, true);
}

std::optional<PDFContentDisplayState> PDFContentDisplay::getDisplayState(const PDFContentContainer& container,
                                                                         PDFContentNodeId id,
                                                                         const PDFContentDisplayContext& context,
                                                                         const PDFContentDisplayUsePath& uses)
{
    const PDFContentNodePointer node = container.getNode(id);
    const std::optional<QTransform> containerToTarget = node ? PDFContentDisplayHelper::getContainerMatrix(container, uses) : std::nullopt;
    if (!containerToTarget)
    {
        return std::nullopt;
    }
    return PDFContentDisplayHelper::computeState(container, *node, context, *containerToTarget, true, true);
}

std::optional<QTransform> PDFContentDisplay::getContainerMatrix(const PDFContentContainer& container, const PDFContentDisplayUsePath& uses)
{
    return PDFContentDisplayHelper::getContainerMatrix(container, uses);
}

PDFContentDisplayBox PDFContentDisplay::getBoundingBox(const PDFContentContainer& container, PDFContentNodeId id, const PDFContentDisplayContext& context)
{
    return getBoundingBox(container, id, context, PDFContentDisplayUsePath());
}

PDFContentDisplayBox PDFContentDisplay::getBoundingBox(const PDFContentContainer& container,
                                                       PDFContentNodeId id,
                                                       const PDFContentDisplayContext& context,
                                                       const PDFContentDisplayUsePath& uses)
{
    const PDFContentNodePointer node = container.getNode(id);
    const std::optional<QTransform> containerToTarget = node ? PDFContentDisplayHelper::getContainerMatrix(container, uses) : std::nullopt;
    if (!containerToTarget)
    {
        return PDFContentDisplayBox();
    }

    PDFContentDisplayBox result = PDFContentDisplayHelper::getBoundingBox(container, *node, context, *containerToTarget);
    if (!result.isValid)
    {
        result.box = QRectF();
    }
    return result;
}

PDFContentDisplayTextPointer PDFContentDisplay::getText(const PDFContentContainer& container, PDFContentNodeId textObjectId, const PDFContentDisplayContext& context)
{
    return getText(container, textObjectId, context, PDFContentDisplayUsePath());
}

PDFContentDisplayTextPointer PDFContentDisplay::getText(const PDFContentContainer& container,
                                                        PDFContentNodeId textObjectId,
                                                        const PDFContentDisplayContext& context,
                                                        const PDFContentDisplayUsePath& uses)
{
    const PDFContentNodePointer node = container.getNode(textObjectId);
    const std::optional<QTransform> containerToTarget = node ? PDFContentDisplayHelper::getContainerMatrix(container, uses) : std::nullopt;
    if (!containerToTarget)
    {
        return nullptr;
    }
    return PDFContentDisplayHelper::getText(container, *node, context, *containerToTarget);
}

PDFContentDisplayImage PDFContentDisplay::decodeImage(const PDFDocument* document,
                                                      const PDFContentContainerPointer& container,
                                                      PDFContentNodeId id,
                                                      const PDFContentDisplayImageOptions& options,
                                                      const PDFOperationControl* control)
{
    PDFContentDisplayImage result;
    const PDFContentNodePointer node = container ? container->getNode(id) : PDFContentNodePointer();
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!node || node->getKind() != PDFContentNodeKind::Image || !catalog || catalog->getPageCount() == 0)
    {
        return result;
    }

    const std::optional<PDFContentDisplayState> state = PDFContentDisplayHelper::computeState(*container, *node, PDFContentDisplayContext{ document, options.cms, options.iccProfileCache }, QTransform(), true, true);
    if (!state)
    {
        result.status = PDFContentDisplayImage::Status::Failed;
        result.errorText = PDFTranslationContext::tr("The placement of the image cannot be computed.");
        return result;
    }
    result.matrix = state->matrix;

    if (PDFOperationControl::isOperationCanceled(control))
    {
        result.status = PDFContentDisplayImage::Status::Canceled;
        return result;
    }

    // The page the processor is made for: its resources are those a container without /Resources
    // of its own inherits (ISO 32000-2, 7.8.3)
    PDFInteger pageIndex = options.pageIndex;
    if (container->getKey().kind == PDFContentContainerKey::Kind::Page)
    {
        pageIndex = container->getKey().pageIndex;
    }
    else if (pageIndex < 0)
    {
        pageIndex = container->getInheritedResourcesPageIndex();
    }
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        pageIndex = 0;
    }

    // The operation of the image as it is written: "/Name Do" or the whole inline image
    QByteArray content;
    const PDFContentOperationSpan& prefix = node->getPrefix();
    const PDFContentOperationList& operations = container->getOperations();
    for (size_t i = prefix.first; i < prefix.first + prefix.count && i < operations.getCount(); ++i)
    {
        content += operations.getOperation(i).getRawBytes();
        content += ' ';
    }

    PDFCMSGeneric genericCms;
    PDFContentDisplayImageProcessor processor(catalog->getPage(size_t(pageIndex)), document, options.cms ? options.cms : &genericCms, options.iccProfileCache, control);
    processor.decode(PDFContentDisplayImageProcessor::createResources(*container), content, *state);
    result = processor.takeResult();
    result.matrix = state->matrix;

    if (result.status == PDFContentDisplayImage::Status::Decoded)
    {
        if (PDFOperationControl::isOperationCanceled(control))
        {
            result.status = PDFContentDisplayImage::Status::Canceled;
            result.image = QImage();
            return result;
        }

        // At most maximumPixels: scaled down, the aspect ratio kept
        const qint64 maximumPixels = qMax<qint64>(1, options.maximumPixels);
        const qint64 width = result.image.width();
        const qint64 height = result.image.height();
        if (width * height > maximumPixels)
        {
            const PDFReal scale = std::sqrt(PDFReal(maximumPixels) / PDFReal(width * height));
            qint64 scaledWidth = qMax<qint64>(1, qint64(std::floor(PDFReal(width) * scale)));
            qint64 scaledHeight = qMax<qint64>(1, qint64(std::floor(PDFReal(height) * scale)));
            while (scaledWidth * scaledHeight > maximumPixels && (scaledWidth > 1 || scaledHeight > 1))
            {
                // A thin image: the side of one pixel cannot be scaled, the other one is
                if (scaledWidth >= scaledHeight)
                {
                    --scaledWidth;
                }
                else
                {
                    --scaledHeight;
                }
            }
            result.image = result.image.scaled(int(scaledWidth), int(scaledHeight), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }

        if (result.image.isNull())
        {
            result.status = PDFContentDisplayImage::Status::Failed;
            result.errorText = PDFTranslationContext::tr("The image cannot be decoded.");
        }
    }

    return result;
}

PDFContentDisplayClipChain PDFContentDisplay::getClipChain(const PDFContentContainer& container, PDFContentNodeId id)
{
    PDFContentDisplayClipChain chain;
    const PDFContentNodePointer node = container.getNode(id);
    if (!node)
    {
        return chain;
    }

    chain.isValid = true;
    PDFContentDisplayHelper::collectClips(container, *node, chain);
    return chain;
}

std::vector<PDFContentNodeId> PDFContentDisplay::getClippedNodes(const PDFContentContainer& container, PDFContentNodeId clipNodeId)
{
    std::vector<PDFContentNodeId> result;
    PDFContentNodePointer current = container.getNode(clipNodeId);
    if (!current || !current->hasClipEffect())
    {
        return result;
    }

    if (current->getKind() == PDFContentNodeKind::TextShow)
    {
        // A clip by text becomes real at the end of its text object (ISO 32000-2, 9.3.6)
        current = container.getParent(current->getId());
    }

    // Up to the end of the enclosing q ... Q (or of the content); bounded by the depth of the tree
    for (int level = 0; current && level <= PDFContentDisplayHelper::MAXIMUM_WALK_DEPTH; ++level)
    {
        const PDFContentNodePointer parent = container.getParent(current->getId());
        if (!parent)
        {
            break;
        }

        const PDFInteger index = container.getIndexInParent(current->getId());
        const std::vector<PDFContentNodePointer>& children = parent->getChildren();
        for (size_t i = size_t(index) + 1; index >= 0 && i < children.size(); ++i)
        {
            result.push_back(children[i]->getId());
        }

        if (parent->getKind() == PDFContentNodeKind::Group || parent->getKind() == PDFContentNodeKind::Root)
        {
            break;
        }
        current = parent;
    }

    return result;
}

PDFContentDisplayCounts PDFContentDisplay::getCounts(const PDFContentContainer& container)
{
    const quint64 version = container.getVersion();
    {
        QMutexLocker locker(&PDFContentDisplayHelper::getCountMutex());
        auto it = PDFContentDisplayHelper::getCountCache().find(version);
        if (it != PDFContentDisplayHelper::getCountCache().cend())
        {
            return it->second;
        }
    }

    // Computed without the lock (two threads may compute one version twice, with the same result)
    const PDFContentDisplayCounts counts = PDFContentDisplayHelper::computeCounts(container);

    QMutexLocker locker(&PDFContentDisplayHelper::getCountMutex());
    std::map<quint64, PDFContentDisplayCounts>& cache = PDFContentDisplayHelper::getCountCache();
    std::deque<quint64>& order = PDFContentDisplayHelper::getCountOrder();
    if (cache.emplace(version, counts).second)
    {
        order.push_back(version);
        while (order.size() > PDFContentDisplayHelper::COUNT_CACHE_SIZE)
        {
            cache.erase(order.front());
            order.pop_front();
        }
    }
    return counts;
}

PDFContentDisplayShading PDFContentDisplay::getShading(const PDFDocument* document,
                                                       const PDFContentContainerPointer& container,
                                                       PDFContentNodeId id,
                                                       bool isStroke,
                                                       const PDFContentDisplayImageOptions& options,
                                                       const PDFOperationControl* control)
{
    PDFContentDisplayShading result;
    const PDFContentNodePointer node = container ? container->getNode(id) : PDFContentNodePointer();
    if (!node || !document)
    {
        return result;
    }

    PDFCMSGeneric genericCms;
    const PDFCMS* cms = options.cms ? options.cms : &genericCms;
    const PDFContentDisplayContext context{ document, cms, options.iccProfileCache };
    const std::optional<PDFContentDisplayState> state = PDFContentDisplayHelper::computeState(*container, *node, context, QTransform(), true, true);
    if (!state)
    {
        result.status = PDFContentDisplayShading::Status::Failed;
        result.errorText = PDFTranslationContext::tr("The placement of the shading cannot be computed.");
        return result;
    }

    const RenderingIntent intent = node->getState()->getState().getRenderingIntent();
    const PDFObject resources = PDFContentDisplayHelper::getResourceDictionary(*container, document);
    const PDFDictionary* resourceDictionary = document->getDictionaryFromObject(resources);
    const PDFDictionary* colorSpaceDictionary = resourceDictionary ? document->getDictionaryFromObject(resourceDictionary->get(COLOR_SPACE_DICTIONARY)) : nullptr;

    PDFPatternPtr pattern;
    PDFRenderErrorReporterDummy reporter;
    try
    {
        if (node->getKind() == PDFContentNodeKind::Shading)
        {
            // sh: the shading space is the space of the node (ISO 32000-2, 8.7.4.2); no background
            const PDFObject shadingObject = PDFContentDisplayHelper::getResource(*container, PDFContentResourceCategory::Shading, node->getData().resourceName, document);
            pattern = PDFPattern::createShadingPattern(colorSpaceDictionary, document, options.iccProfileCache, shadingObject, QTransform(), PDFObject(), cms, intent, &reporter, true);
            result.matrix = state->matrix;
        }
        else
        {
            // A shading pattern: the pattern space -> the default space of the container (8.7.3.1)
            const PDFObject patternObject = PDFContentDisplayHelper::getPatternObject(*container, *node, isStroke);
            if (patternObject.isNull())
            {
                return result;
            }
            pattern = PDFPattern::createPattern(colorSpaceDictionary, document, options.iccProfileCache, patternObject, cms, intent, &reporter);
            if (pattern && pattern->getType() == PatternType::Shading)
            {
                result.matrix = pattern->getMatrix();
            }
        }
    }
    catch (const PDFException& exception)
    {
        result.status = PDFContentDisplayShading::Status::Failed;
        result.errorText = exception.getMessage();
        return result;
    }
    catch (const PDFRendererException& exception)
    {
        result.status = PDFContentDisplayShading::Status::Failed;
        result.errorText = exception.getError().message;
        return result;
    }

    const PDFShadingPattern* shading = pattern ? pattern->getShadingPattern() : nullptr;
    if (!shading)
    {
        return result;
    }
    PDFContentDisplayHelper::makePlain(result.matrix);

    if (PDFOperationControl::isOperationCanceled(control))
    {
        result.status = PDFContentDisplayShading::Status::Canceled;
        return result;
    }

    result.backgroundColor = PDFContentDisplayHelper::toOpaqueRgb(shading->getBackgroundColor());
    const PDFSingleDimensionShading* singleDimensionShading = nullptr;
    switch (shading->getShadingType())
    {
        case ShadingType::Axial:
            result.kind = PDFContentDisplayShading::Kind::Axial;
            singleDimensionShading = static_cast<const PDFSingleDimensionShading*>(shading);
            break;

        case ShadingType::Radial:
        {
            result.kind = PDFContentDisplayShading::Kind::Radial;
            const PDFRadialShading* radialShading = static_cast<const PDFRadialShading*>(shading);
            result.startRadius = radialShading->getR0();
            result.endRadius = radialShading->getR1();
            singleDimensionShading = radialShading;
            break;
        }

        default:
            // Types 1, 4 - 7 (a function of two variables, meshes): not drawn by this package
            result.kind = PDFContentDisplayShading::Kind::Picture;
            result.status = PDFContentDisplayShading::Status::NotImplemented;
            return result;
    }

    result.start = singleDimensionShading->getStartPoint();
    result.end = singleDimensionShading->getEndPoint();
    result.extendStart = singleDimensionShading->isExtendStart();
    result.extendEnd = singleDimensionShading->isExtendEnd();

    // The stops: the functions sampled at MAXIMUM_GRADIENT_STOPS points of the domain [t0, t1]
    // (8.7.4.5.3: t = t0 + (t1 - t0) s), the colors through the color space of the shading
    const std::vector<PDFFunctionPtr>& functions = singleDimensionShading->getFunctions();
    const PDFAbstractColorSpace* colorSpace = shading->getColorSpace();
    if (functions.empty() || !colorSpace)
    {
        result.status = PDFContentDisplayShading::Status::Failed;
        result.errorText = PDFTranslationContext::tr("The shading has no function.");
        return result;
    }

    const size_t componentCount = colorSpace->getColorComponentCount();
    const PDFReal domainStart = singleDimensionShading->getDomainStart();
    const PDFReal domainEnd = singleDimensionShading->getDomainEnd();
    std::vector<PDFReal> outputs(qMax<size_t>(componentCount, 1), 0.0);
    for (int i = 0; i < MAXIMUM_GRADIENT_STOPS; ++i)
    {
        const PDFReal position = PDFReal(i) / PDFReal(MAXIMUM_GRADIENT_STOPS - 1);
        const PDFReal t = domainStart + (domainEnd - domainStart) * position;

        bool isEvaluated = true;
        if (functions.size() == 1)
        {
            isEvaluated = bool(functions.front()->apply(&t, &t + 1, outputs.data(), outputs.data() + componentCount));
        }
        else
        {
            for (size_t j = 0; j < functions.size() && j < componentCount; ++j)
            {
                isEvaluated = isEvaluated && bool(functions[j]->apply(&t, &t + 1, outputs.data() + j, outputs.data() + j + 1));
            }
        }

        if (!isEvaluated)
        {
            result.status = PDFContentDisplayShading::Status::Failed;
            result.errorText = PDFTranslationContext::tr("The function of the shading cannot be evaluated.");
            result.stops.clear();
            return result;
        }

        try
        {
            std::vector<PDFReal> components(outputs.begin(), outputs.begin() + componentCount);
            PDFContentDisplayShading::Stop stop;
            stop.position = position;
            stop.color = PDFContentDisplayHelper::toOpaqueRgb(colorSpace->getCheckedColor(PDFAbstractColorSpace::convertToColor(components), cms, intent, &reporter));
            result.stops.push_back(stop);
        }
        catch (const PDFException& exception)
        {
            result.status = PDFContentDisplayShading::Status::Failed;
            result.errorText = exception.getMessage();
            result.stops.clear();
            return result;
        }
    }

    result.status = PDFContentDisplayShading::Status::Done;
    return result;
}

PDFContentDisplayTilingCell PDFContentDisplay::getTilingCell(const PDFDocument* document,
                                                             const PDFContentContainerPointer& container,
                                                             PDFContentNodeId id,
                                                             bool isStroke,
                                                             const PDFContentDisplayImageOptions& options,
                                                             const PDFOperationControl* control)
{
    PDFContentDisplayTilingCell result;
    const PDFContentNodePointer node = container ? container->getNode(id) : PDFContentNodePointer();
    const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
    if (!node || !catalog || catalog->getPageCount() == 0)
    {
        return result;
    }

    PDFCMSGeneric genericCms;
    const PDFCMS* cms = options.cms ? options.cms : &genericCms;
    const PDFContentDisplayContext context{ document, cms, options.iccProfileCache };
    const std::optional<PDFContentDisplayState> state = PDFContentDisplayHelper::computeState(*container, *node, context, QTransform(), true, true);
    const PDFObject patternObject = PDFContentDisplayHelper::getPatternObject(*container, *node, isStroke);
    if (!state || patternObject.isNull())
    {
        return result;
    }

    const PDFContentDisplayColor& color = isStroke ? state->strokeColor : state->fillColor;
    const RenderingIntent intent = node->getState()->getState().getRenderingIntent();
    const PDFObject resources = PDFContentDisplayHelper::getResourceDictionary(*container, document);
    const PDFDictionary* resourceDictionary = document->getDictionaryFromObject(resources);
    const PDFDictionary* colorSpaceDictionary = resourceDictionary ? document->getDictionaryFromObject(resourceDictionary->get(COLOR_SPACE_DICTIONARY)) : nullptr;

    PDFPatternPtr pattern;
    PDFRenderErrorReporterDummy reporter;
    try
    {
        pattern = PDFPattern::createPattern(colorSpaceDictionary, document, options.iccProfileCache, patternObject, cms, intent, &reporter);
    }
    catch (const PDFException& exception)
    {
        result.status = PDFContentDisplayTilingCell::Status::Failed;
        result.errorText = exception.getMessage();
        return result;
    }
    catch (const PDFRendererException& exception)
    {
        result.status = PDFContentDisplayTilingCell::Status::Failed;
        result.errorText = exception.getError().message;
        return result;
    }

    const PDFTilingPattern* tilingPattern = pattern ? pattern->getTilingPattern() : nullptr;
    if (!tilingPattern)
    {
        return result;
    }

    result.cellRectangle = tilingPattern->getBoundingBox().normalized();
    result.matrix = tilingPattern->getMatrix();
    result.xStep = tilingPattern->getXStep();
    result.yStep = tilingPattern->getYStep();
    PDFContentDisplayHelper::makePlain(result.matrix);

    // The picture fills the budget of pixels with the aspect ratio of /BBox (the caller gives the
    // budget of its zoom), checked before the image is allocated
    const PDFReal cellWidth = result.cellRectangle.width();
    const PDFReal cellHeight = result.cellRectangle.height();
    const qint64 maximumPixels = qMax<qint64>(1, options.maximumPixels);
    if (!(cellWidth > 0.0) || !(cellHeight > 0.0) || !std::isfinite(cellWidth * cellHeight))
    {
        result.status = PDFContentDisplayTilingCell::Status::Failed;
        result.errorText = PDFTranslationContext::tr("The cell of the pattern is empty.");
        return result;
    }

    const PDFReal scale = std::sqrt(PDFReal(maximumPixels) / (cellWidth * cellHeight));
    const qint64 width = qBound<qint64>(1, qint64(std::floor(cellWidth * scale)), maximumPixels);
    const qint64 height = qBound<qint64>(1, qint64(std::floor(cellHeight * scale)), maximumPixels);
    if (width * height > maximumPixels || width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
    {
        result.status = PDFContentDisplayTilingCell::Status::TooLarge;
        return result;
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        result.status = PDFContentDisplayTilingCell::Status::Canceled;
        return result;
    }

    QImage picture(int(width), int(height), QImage::Format_ARGB32_Premultiplied);
    PDFAbstractColorSpace::checkImageAllocated(picture);
    picture.fill(Qt::transparent);

    // Pattern space -> the pixels: /BBox onto the whole picture, y upward in the pattern space
    const PDFReal scaleX = PDFReal(width) / cellWidth;
    const PDFReal scaleY = PDFReal(height) / cellHeight;
    const QTransform patternToImage(scaleX, 0.0, 0.0, -scaleY, -result.cellRectangle.left() * scaleX, result.cellRectangle.bottom() * scaleY);

    PDFInteger pageIndex = options.pageIndex;
    if (container->getKey().kind == PDFContentContainerKey::Kind::Page)
    {
        pageIndex = container->getKey().pageIndex;
    }
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        pageIndex = 0;
    }

    {
        PDFOptionalContentActivity activity(document, OCUsage::View, nullptr);
        PDFFontCache fontCache(16, 16);
        fontCache.setDocument(PDFModifiedDocument(const_cast<PDFDocument*>(document), &activity));

        QPainter painter(&picture);
        PDFContentDisplayCellPainter cellPainter(&painter, patternToImage, catalog->getPage(size_t(pageIndex)), document, &fontCache, options.iccProfileCache, cms, &activity, control);
        try
        {
            cellPainter.paintCell(*tilingPattern, color.color);
        }
        catch (const PDFException& exception)
        {
            result.errorText = exception.getMessage();
        }
        catch (const PDFRendererException& exception)
        {
            result.errorText = exception.getError().message;
        }
        painter.end();
    }

    if (PDFOperationControl::isOperationCanceled(control))
    {
        result.status = PDFContentDisplayTilingCell::Status::Canceled;
        return result;
    }

    result.picture = std::move(picture);
    result.status = PDFContentDisplayTilingCell::Status::Done;
    return result;
}

}   // namespace pdf
