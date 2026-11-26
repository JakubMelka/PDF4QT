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

// Edit Content, stage E0, package P03 (session, undo, basic commands): the basic commands and the
// rules of writing back they carry. Plan: GUI_overhaul/edit_tool/zadani_jadro.md 4.4 (R-1, R-2,
// R-3, R-5, R-6), 4.5, 4.12 (consistency of the structure).
//
// The one principle behind every rule: a node the user did not touch sees the same graphics
// state after the command as before it. So
//  - a node, which is moved, copied or wrapped, gets the difference between the state it saw and
//    the state of its new place into its override (R-2) and never leaks there (it is wrapped);
//  - whenever the leak of a node stops reaching the nodes behind it (the node is deleted, moved
//    or wrapped), a residual state run sets the leak at the place of its end (R-3);
//  - therefore the state of every other node stays as the builder has set it, and the nodes a
//    command moves or creates get the state of the place, where they are written
//    (PDFContentNode::getState; the stream writer relies on it).
//
// Three parts: PDFContentCommandHelper (functions without a state: values of the graphics state,
// differences, small objects), PDFContentCommandWork (one computation of a command over the
// editors of the containers) and the commands (PDFContentModelCommand keeps the versions of the
// containers before and after, so undo is the old version and redo the new one).

#include "pdfcontentcommands.h"

#include "pdfblendfunction.h"
#include "pdfcolorspaces.h"
#include "pdfcontenteditsession.h"
#include "pdfcontentjson.h"
#include "pdfcontentstreamtext.h"
#include "pdfcontentstructure.h"
#include "pdfdocument.h"
#include "pdfexception.h"
#include "pdfparser.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace pdf
{

PDFContentCommand::~PDFContentCommand() = default;

bool PDFContentCommand::mergeWith(const PDFContentCommand& next)
{
    Q_UNUSED(next);
    return false;
}

/// The two states of one place of a content (a plain value of shared pointers to immutable states)
struct PDFContentCommandState
{
    PDFContentResolvedStatePointer resolved;
    PDFContentSymbolicStatePointer symbolic;

    bool isValid() const { return resolved && symbolic; }
    bool operator==(const PDFContentCommandState&) const = default;
};

/// Functions of the commands, which have no state: every one works with its arguments only
/// (reentrant).
class PDFContentCommandHelper
{
public:
    PDFContentCommandHelper() = delete;

    using Property = PDFContentStateProperty;
    using Kind = PDFContentNodeKind;
    using Operator = PDFContentOperator;

    /// Limits: depth of a property list written into a content; the operations of the pool of
    /// a container a command still adds a node to (the pool only grows: the operations of removed
    /// nodes stay in it. A loaded container has at most PDFContentContainer::
    /// MAXIMUM_OPERATION_COUNT of them, so it always has room for as many again); the forms and
    /// the resource entries one walk over the forms of a copied form use looks at. The limits of
    /// the nodes are PDFContentCommand::MAXIMUM_NODES_OF_COMMAND and MAXIMUM_NODES_OF_CONTAINER.
    static constexpr int MAXIMUM_PROPERTY_LIST_DEPTH = 8;
    static constexpr size_t MAXIMUM_OPERATIONS_OF_CONTAINER = 2 * PDFContentContainer::MAXIMUM_OPERATION_COUNT;
    static constexpr size_t MAXIMUM_WALKED_FORMS = 100000;

    /// Two commands are merged, when the second one comes within this time (milliseconds)
    static constexpr qint64 MERGE_INTERVAL = 1000;

    /// A node of this kind can be moved, copied and grouped as an object
    static bool isObjectKind(Kind kind)
    {
        switch (kind)
        {
            case Kind::Group:
            case Kind::MarkedContent:
            case Kind::MarkedPoint:
            case Kind::TextObject:
            case Kind::Path:
            case Kind::Image:
            case Kind::FormUse:
            case Kind::Shading:
                return true;

            default:
                return false;
        }
    }

    static bool isTextProperty(Property property)
    {
        switch (property)
        {
            case Property::Font:
            case Property::CharacterSpacing:
            case Property::WordSpacing:
            case Property::HorizontalScaling:
            case Property::Leading:
            case Property::TextRenderingMode:
            case Property::TextRise:
            case Property::TextKnockout:
                return true;

            default:
                return false;
        }
    }

    /// The property can change what a node of the kind paints (a path, an image and a shading
    /// do not read the text state; a marked point and a clipping path paint nothing)
    static bool isRelevant(const PDFContentNode::Data& data, Property property)
    {
        switch (data.kind)
        {
            case Kind::MarkedPoint:
                return false;

            case Kind::Path:
            {
                if (data.paint.isClippingPath() || isTextProperty(property))
                {
                    return false;
                }

                // What only stroking reads, and what only filling reads (8.4.1, Table 51)
                switch (property)
                {
                    case Property::StrokeColor:
                    case Property::AlphaStroking:
                    case Property::LineWidth:
                    case Property::LineCap:
                    case Property::LineJoin:
                    case Property::MiterLimit:
                    case Property::DashPattern:
                        return data.paint.stroke;

                    case Property::FillColor:
                    case Property::AlphaFilling:
                        return data.paint.fill;

                    default:
                        return true;
                }
            }

            case Kind::Image:
            case Kind::Shading:
                return !isTextProperty(property);

            default:
                return true;
        }
    }

    /// The state behind a node
    static PDFContentCommandState getStateAfter(const PDFContentNode& node)
    {
        if (node.getLeak().endState && node.getLeak().endSymbolicState)
        {
            return PDFContentCommandState{ node.getLeak().endState, node.getLeak().endSymbolicState };
        }

        return PDFContentCommandState{ node.getState(), node.getSymbolicState() };
    }

    /// The node or something below it is content (a state run is none, an empty group neither)
    static bool hasContent(const PDFContentNode& node, int depth = 0)
    {
        switch (node.getKind())
        {
            case Kind::StateRun:
                return false;

            case Kind::Root:
            case Kind::Group:
            case Kind::MarkedContent:
                break;

            default:
                return true;
        }

        if (depth > PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            return true;
        }

        for (const PDFContentNodePointer& child : node.getChildren())
        {
            if (hasContent(*child, depth + 1))
            {
                return true;
            }
        }

        return false;
    }

    /// Something of a node is content, when the subtree of one node is left out
    static bool hasContentWithout(const PDFContentNode& node, PDFContentNodeId excluded, int depth = 0)
    {
        if (node.getId() == excluded || node.getKind() == Kind::StateRun)
        {
            return false;
        }

        if (node.getKind() != Kind::Root && node.getKind() != Kind::Group && node.getKind() != Kind::MarkedContent)
        {
            return true;
        }

        if (depth > PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            return true;
        }

        for (const PDFContentNodePointer& child : node.getChildren())
        {
            if (hasContentWithout(*child, excluded, depth + 1))
            {
                return true;
            }
        }

        return false;
    }

    /// The nodes of a subtree, the node itself first (a walk without recursion)
    static std::vector<PDFContentNodePointer> getSubtree(const PDFContentNodePointer& node)
    {
        std::vector<PDFContentNodePointer> result;
        std::vector<PDFContentNodePointer> stack = { node };

        while (!stack.empty())
        {
            PDFContentNodePointer current = std::move(stack.back());
            stack.pop_back();

            const std::vector<PDFContentNodePointer>& children = current->getChildren();
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                stack.push_back(*it);
            }

            result.push_back(std::move(current));
        }

        return result;
    }

    /// The numbers of a line cap and of a line join (ISO 32000-2, 8.4.3.3, 8.4.3.4)
    static PDFInteger getLineCap(Qt::PenCapStyle style)
    {
        switch (style)
        {
            case Qt::RoundCap:
                return 1;
            case Qt::SquareCap:
                return 2;
            default:
                return 0;
        }
    }

    static PDFInteger getLineJoin(Qt::PenJoinStyle style)
    {
        switch (style)
        {
            case Qt::RoundJoin:
                return 1;
            case Qt::BevelJoin:
                return 2;
            default:
                return 0;
        }
    }

    static bool isNoneObject(const PDFObject& object)
    {
        return object.isNull() || (object.isName() && object.getString() == "None");
    }

    /// The color of a state as the content has set it
    static bool makeColorValue(bool isStroking,
                               const PDFContentCommandState& state,
                               const PDFContentOperationList& operations,
                               PDFContentStateValue* value,
                               QString* errorText)
    {
        const PDFPageContentProcessorState& processorState = state.resolved->getState();
        const PDFContentPropertySource& source = state.symbolic->getSource(isStroking ? Property::StrokeColor : Property::FillColor);
        const PDFColor& original = isStroking ? processorState.getStrokeColorOriginal() : processorState.getFillColorOriginal();
        const PDFAbstractColorSpace* colorSpace = isStroking ? processorState.getStrokeColorSpace() : processorState.getFillColorSpace();

        value->kind = PDFContentStateValue::Kind::Color;
        PDFContentColorValue& color = value->color;
        color = PDFContentColorValue();

        for (size_t i = 0; i < original.size(); ++i)
        {
            color.components.push_back(PDFReal(original[i]));
        }

        if (source.origin == PDFContentPropertySource::Origin::Operator && !source.object.isNull())
        {
            const QByteArray name = source.object.isName() ? source.object.getString() : QByteArray();
            if (name == "DeviceGray" || name == "DeviceRGB" || name == "DeviceCMYK")
            {
                color.deviceSpace = name;
            }
            else
            {
                // The color space as it is in the resources (a name is the operand itself)
                color.colorSpaceObject = source.object;
            }

            if (!source.patternObject.isNull())
            {
                // The tint components of an uncolored pattern are the numbers in front of the
                // name of the pattern (8.7.3.3); the state of the processor does not keep them
                color.patternObject = source.patternObject;
                color.components.clear();

                if (source.operationIndex < operations.getCount())
                {
                    for (const PDFLexicalAnalyzer::Token& token : operations.getOperation(source.operationIndex).getOperands())
                    {
                        if (token.type == PDFLexicalAnalyzer::TokenType::Integer || token.type == PDFLexicalAnalyzer::TokenType::Real)
                        {
                            color.components.push_back(token.data.toDouble());
                        }
                    }
                }
            }

            return true;
        }

        // Not set by an operator of the content with a known space: only a device space is known
        // without its object (the initial color of a page is black in DeviceGray, 8.4.1)
        const PDFAbstractColorSpace::ColorSpace kind = colorSpace ? colorSpace->getColorSpace() : PDFAbstractColorSpace::ColorSpace::Invalid;
        switch (kind)
        {
            case PDFAbstractColorSpace::ColorSpace::DeviceGray:
                color.deviceSpace = "DeviceGray";
                return true;

            case PDFAbstractColorSpace::ColorSpace::DeviceRGB:
                color.deviceSpace = "DeviceRGB";
                return true;

            case PDFAbstractColorSpace::ColorSpace::DeviceCMYK:
                color.deviceSpace = "DeviceCMYK";
                return true;

            default:
                break;
        }

        *errorText = PDFTranslationContext::tr("The color of the object cannot be kept, because its color space is not known.");
        return false;
    }

    /// The value of a property in a state, as an override sets it (R-2, R-3).
    /// \param isSkipped The property has no value, which can be set (no font, the default
    ///        halftone origin): nothing is written for it
    static bool makeValue(Property property,
                          const PDFContentCommandState& state,
                          const PDFContentOperationList& operations,
                          PDFContentStateValue* value,
                          bool* isSkipped,
                          QString* errorText)
    {
        using ValueKind = PDFContentStateValue::Kind;

        const PDFPageContentProcessorState& processorState = state.resolved->getState();
        const PDFContentPropertySource& source = state.symbolic->getSource(property);
        const bool isFromExtGState = source.origin == PDFContentPropertySource::Origin::ExtGState && !source.object.isNull();

        *value = PDFContentStateValue();
        *isSkipped = false;

        auto setReal = [value](PDFReal real) { value->kind = ValueKind::Real; value->real = real; };
        auto setInteger = [value](PDFInteger integer) { value->kind = ValueKind::Integer; value->integer = integer; };
        auto setBoolean = [value](bool boolean) { value->kind = ValueKind::Boolean; value->boolean = boolean; };
        auto setName = [value](QByteArray name) { value->kind = ValueKind::Name; value->name = std::move(name); };

        // A value written by reference: the object the content has set; the default by its name
        auto setObjectOrDefault = [value, &source, isFromExtGState, &setName]()
        {
            if (isFromExtGState)
            {
                value->kind = ValueKind::Object;
                value->object = source.object;
            }
            else
            {
                setName("Default");
            }
        };

        switch (property)
        {
            case Property::LineWidth:
                setReal(processorState.getLineWidth());
                break;

            case Property::LineCap:
                setInteger(getLineCap(processorState.getLineCapStyle()));
                break;

            case Property::LineJoin:
                setInteger(getLineJoin(processorState.getLineJoinStyle()));
                break;

            case Property::MiterLimit:
                setReal(processorState.getMitterLimit());
                break;

            case Property::DashPattern:
                value->kind = ValueKind::Dash;
                value->dash = processorState.getLineDashPattern();
                break;

            case Property::RenderingIntent:
                // The default intent, 8.6.5.8
                setName(!processorState.getRenderingIntentName().isEmpty() ? processorState.getRenderingIntentName() : QByteArray("RelativeColorimetric"));
                break;

            case Property::Flatness:
                setReal(processorState.getFlatness());
                break;

            case Property::StrokeColor:
                return makeColorValue(true, state, operations, value, errorText);

            case Property::FillColor:
                return makeColorValue(false, state, operations, value, errorText);

            case Property::Font:
                // No font is set at the beginning of a page: there is nothing to go back to
                if (!processorState.getTextFont() || source.object.isNull())
                {
                    *isSkipped = true;
                }
                else if (!source.object.isReference() && !source.object.isDictionary())
                {
                    // The source of a font is the font itself, also of a font set by gs (its
                    // /Font [font size] gives the font, never the array: "/name size Tf" needs
                    // a font dictionary as its resource - ISO 32000-2, 9.3.1, Table 57)
                    *errorText = PDFTranslationContext::tr("The font of the object cannot be kept, because it is not a font of the resources.");
                    return false;
                }
                else
                {
                    value->kind = ValueKind::Font;
                    value->object = source.object;
                    value->real = processorState.getTextFontSize();
                }
                break;

            case Property::CharacterSpacing:
                setReal(processorState.getTextCharacterSpacing());
                break;

            case Property::WordSpacing:
                setReal(processorState.getTextWordSpacing());
                break;

            case Property::HorizontalScaling:
                setReal(processorState.getTextHorizontalScaling());
                break;

            case Property::Leading:
                setReal(processorState.getTextLeading());
                break;

            case Property::TextRenderingMode:
                setInteger(PDFInteger(processorState.getTextRenderingMode()));
                break;

            case Property::TextRise:
                setReal(processorState.getTextRise());
                break;

            case Property::TextKnockout:
                setBoolean(processorState.getTextKnockout());
                break;

            case Property::AlphaStroking:
                setReal(processorState.getAlphaStroking());
                break;

            case Property::AlphaFilling:
                setReal(processorState.getAlphaFilling());
                break;

            case Property::BlendMode:
                if (isFromExtGState && source.object.isArray())
                {
                    value->kind = ValueKind::Object;
                    value->object = source.object;
                }
                else
                {
                    setName(PDFBlendModeInfo::getBlendModeName(processorState.getBlendMode()).toLatin1());
                }
                break;

            case Property::SoftMask:
                if (!state.resolved->hasSoftMask())
                {
                    setName("None");
                }
                else if (isFromExtGState && !isNoneObject(source.object))
                {
                    // The mask by reference, in the space it was set in (R-6)
                    value->kind = ValueKind::Object;
                    value->object = source.object;
                    value->matrix = state.symbolic->getSoftMaskMatrix();
                }
                else
                {
                    *errorText = PDFTranslationContext::tr("The soft mask of the object cannot be kept, because it is not set by the content itself.");
                    return false;
                }
                break;

            case Property::AlphaIsShape:
                setBoolean(processorState.getAlphaIsShape());
                break;

            case Property::StrokeAdjustment:
                setBoolean(processorState.getStrokeAdjustment());
                break;

            case Property::OverprintStroking:
                setBoolean(processorState.getOverprintMode().overprintStroking);
                break;

            case Property::OverprintFilling:
                setBoolean(processorState.getOverprintMode().overprintFilling);
                break;

            case Property::OverprintMode:
                setInteger(processorState.getOverprintMode().overprintMode);
                break;

            case Property::BlackGeneration:
            case Property::UndercolorRemoval:
            case Property::Transfer:
            case Property::Halftone:
                setObjectOrDefault();
                break;

            case Property::HalftoneOrigin:
                if (isFromExtGState)
                {
                    value->kind = ValueKind::Object;
                    value->object = source.object;
                }
                else
                {
                    // The origin of the device has no name
                    *isSkipped = true;
                }
                break;

            case Property::Smoothness:
                setReal(processorState.getSmoothness());
                break;

            case Property::BlackPointCompensation:
                switch (processorState.getBlackPointCompensationMode())
                {
                    case BlackPointCompensationMode::ON:
                        setName("ON");
                        break;
                    case BlackPointCompensationMode::OFF:
                        setName("OFF");
                        break;
                    default:
                        setName("Default");
                        break;
                }
                break;

            default:
                *isSkipped = true;
                break;
        }

        return true;
    }

    /// The soft masks of two states differ: another mask, or the same one set in another space.
    /// A mask is set and removed by gs only, so its source tells every change of it.
    static bool isSoftMaskDifferent(const PDFContentCommandState& left, const PDFContentCommandState& right, bool isSameContainer)
    {
        if (left.resolved->hasSoftMask() != right.resolved->hasSoftMask())
        {
            return true;
        }

        if (!left.resolved->hasSoftMask())
        {
            return false;
        }

        const PDFContentPropertySource& leftSource = left.symbolic->getSource(Property::SoftMask);
        const PDFContentPropertySource& rightSource = right.symbolic->getSource(Property::SoftMask);

        if (leftSource.origin != rightSource.origin || !(leftSource.object == rightSource.object))
        {
            return true;
        }

        if (isSameContainer && leftSource.operationIndex != rightSource.operationIndex)
        {
            return true;
        }

        return left.symbolic->getSoftMaskMatrix() != right.symbolic->getSoftMaskMatrix();
    }

    /// The properties a node, which saw the state \p source, has to set at a place with the
    /// state \p target (R-2). In the container of a form an inherited property has the value of
    /// one use only: it is unknown.
    static bool getDifference(const PDFContentNode::Data& data,
                              const PDFContentCommandState& source,
                              const PDFContentCommandState& target,
                              bool isSourceForm,
                              bool isTargetForm,
                              bool isSameContainer,
                              const PDFContentOperationList& sourceOperations,
                              const PDFContentOperationList& targetOperations,
                              PDFContentStatePropertySet* properties,
                              QString* errorText)
    {
        *properties = PDFContentStatePropertySet();

        PDFContentStatePropertySet different = PDFContentResolvedState::getDifference(*source.resolved, *target.resolved);
        different.remove(Property::SoftMask);
        if (isSoftMaskDifferent(source, target, isSameContainer))
        {
            different.insert(Property::SoftMask);
        }

        if (!isSameContainer)
        {
            // The colors of two states are compared by their color space objects, and two
            // containers never share one: the same color of a device space is the same color
            for (const bool isStroking : { false, true })
            {
                const Property property = isStroking ? Property::StrokeColor : Property::FillColor;
                PDFContentStateValue sourceValue;
                PDFContentStateValue targetValue;
                QString ignoredText;
                if (different.contains(property) &&
                    makeColorValue(isStroking, source, sourceOperations, &sourceValue, &ignoredText) &&
                    makeColorValue(isStroking, target, targetOperations, &targetValue, &ignoredText) &&
                    !sourceValue.color.deviceSpace.isEmpty() &&
                    sourceValue.color.deviceSpace == targetValue.color.deviceSpace &&
                    sourceValue.color.components == targetValue.color.components)
                {
                    different.remove(property);
                }
            }
        }

        for (int i = 0; i < int(Property::Count); ++i)
        {
            const Property property = Property(i);
            if (!isRelevant(data, property))
            {
                continue;
            }

            const bool isSourceUnknown = isSourceForm && source.symbolic->isInherited(property);
            const bool isTargetUnknown = isTargetForm && target.symbolic->isInherited(property);

            if (isSourceUnknown && isTargetUnknown && isSameContainer)
            {
                // The same unknown value
                continue;
            }

            if (isSourceUnknown)
            {
                if (isTargetUnknown || different.contains(property) || !target.symbolic->isInherited(property))
                {
                    *errorText = PDFTranslationContext::tr("The object takes a part of its graphics state from the place, where its form is used; it cannot be placed behind content, which changes that state.");
                    return false;
                }
                continue;
            }

            if (isTargetUnknown || different.contains(property))
            {
                properties->insert(property);
            }
        }

        return true;
    }

    /// The override, which gives a node the state \p source at a place with the state \p target:
    /// the matrix M = CTM of the source x (CTM of the target)^-1 and every differing property (R-2)
    static bool computeBake(const PDFContentNode::Data& data,
                            const PDFContentCommandState& source,
                            const PDFContentCommandState& target,
                            bool isSourceForm,
                            bool isTargetForm,
                            bool isSameContainer,
                            const PDFContentOperationList& sourceOperations,
                            const PDFContentOperationList& targetOperations,
                            PDFContentStateOverride* bake,
                            QString* errorText)
    {
        *bake = PDFContentStateOverride();

        if (!source.isValid() || !target.isValid())
        {
            *errorText = PDFTranslationContext::tr("The graphics state of the object or of its new place is not known.");
            return false;
        }

        if (source == target)
        {
            return true;
        }

        if (data.kind != Kind::MarkedPoint)
        {
            const QTransform& sourceMatrix = source.resolved->getCurrentTransformationMatrix();
            const QTransform& targetMatrix = target.resolved->getCurrentTransformationMatrix();
            if (sourceMatrix != targetMatrix)
            {
                QTransform inverted;
                if (!invertMatrix(targetMatrix, &inverted))
                {
                    *errorText = PDFTranslationContext::tr("The object cannot be placed there, because the transformation matrix of the place cannot be inverted.");
                    return false;
                }

                bake->transform = sourceMatrix * inverted;
            }
        }

        PDFContentStatePropertySet properties;
        if (!getDifference(data, source, target, isSourceForm, isTargetForm, isSameContainer, sourceOperations, targetOperations, &properties, errorText))
        {
            return false;
        }

        for (const Property property : properties.getProperties())
        {
            PDFContentStateValue value;
            bool isSkipped = false;
            if (!makeValue(property, source, sourceOperations, &value, &isSkipped, errorText))
            {
                return false;
            }

            if (!isSkipped)
            {
                bake->properties[property] = std::move(value);
            }
        }

        return true;
    }

    /// An override of a node at a new place: what it had (relative to its old state) over the
    /// bake. Inside of the wrap the matrix is transform x CTM of the place, so the old transform
    /// stays in front.
    static PDFContentStateOverride composeOverride(const std::optional<PDFContentStateOverride>& existing, const PDFContentStateOverride& bake)
    {
        if (!existing)
        {
            return bake;
        }

        PDFContentStateOverride result = bake;
        result.transform = existing->transform * bake.transform;
        for (const auto& item : existing->properties)
        {
            result.properties[item.first] = item.second;
        }
        return result;
    }

    /// What a node changes for the nodes behind it, from the states of its beginning and its end
    static PDFContentStateLeak makeLeak(const PDFContentCommandState& begin, const PDFContentCommandState& end, bool isForm, bool isClipChanged)
    {
        PDFContentStateLeak leak;
        leak.isClipChanged = isClipChanged;

        if (begin.isValid() && end.isValid() && !(begin == end))
        {
            leak.properties = PDFContentResolvedState::getDifference(*begin.resolved, *end.resolved);
            leak.properties.remove(Property::SoftMask);
            if (isSoftMaskDifferent(begin, end, true))
            {
                leak.properties.insert(Property::SoftMask);
            }

            if (isForm)
            {
                for (int i = 0; i < int(Property::Count); ++i)
                {
                    if (begin.symbolic->isInherited(Property(i)) && !end.symbolic->isInherited(Property(i)))
                    {
                        leak.properties.insert(Property(i));
                    }
                }
            }

            leak.isTransformChanged = begin.resolved->getCurrentTransformationMatrix() != end.resolved->getCurrentTransformationMatrix();
        }

        if (!leak.properties.isEmpty() || leak.isTransformChanged)
        {
            leak.endState = end.resolved;
            leak.endSymbolicState = end.symbolic;
        }

        return leak;
    }

    /// The operands of the canonical text of an operation
    static PDFContentOperation::Operands getOperands(const QByteArray& bytes)
    {
        PDFContentOperation::Operands operands;
        PDFLexicalAnalyzer analyzer(bytes.constData(), bytes.constData() + bytes.size());

        try
        {
            while (!analyzer.isAtEnd())
            {
                PDFLexicalAnalyzer::Token token = analyzer.fetch();
                if (token.type == PDFLexicalAnalyzer::TokenType::EndOfFile)
                {
                    break;
                }
                operands.push_back(std::move(token));
            }
        }
        catch (const PDFException&)
        {
            operands.clear();
        }

        // The last token is the operator itself
        if (!operands.empty() && operands.back().type == PDFLexicalAnalyzer::TokenType::Command)
        {
            operands.pop_back();
        }

        return operands;
    }

    static PDFContentOperation createOperation(Operator contentOperator, const QByteArray& bytes, std::vector<PDFContentResourceName> resourceNames = { })
    {
        return PDFContentOperation::createGenerated(contentOperator, bytes, getOperands(bytes), std::move(resourceNames));
    }

    /// A direct object as the text of a content stream (a property list written inline, 14.6.2).
    /// \returns false for an object, which cannot be written inline: it holds a reference or
    ///          a stream, a number, which is not finite, or it is nested too deep
    static bool writeObject(const PDFObject& object, int depth, QByteArray* text)
    {
        if (depth > MAXIMUM_PROPERTY_LIST_DEPTH)
        {
            return false;
        }

        if (object.isNull())
        {
            text->append("null");
        }
        else if (object.isBool())
        {
            text->append(object.getBool() ? "true" : "false");
        }
        else if (object.isInt())
        {
            text->append(QByteArray::number(qlonglong(object.getInteger())));
        }
        else if (object.isReal())
        {
            if (!PDFContentStreamText::isNumberWritable(object.getReal()))
            {
                return false;
            }
            text->append(PDFContentStreamText::formatNumber(object.getReal()));
        }
        else if (object.isName())
        {
            text->append(PDFContentStreamText::formatName(object.getString()));
        }
        else if (object.isString())
        {
            text->append(PDFContentStreamText::formatString(object.getString()));
        }
        else if (object.isArray())
        {
            const PDFArray* array = object.getArray();
            text->append('[');
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                if (i > 0)
                {
                    text->append(' ');
                }
                if (!writeObject(array->getItem(i), depth + 1, text))
                {
                    return false;
                }
            }
            text->append(']');
        }
        else if (object.isDictionary())
        {
            const PDFDictionary* dictionary = object.getDictionary();
            text->append("<<");
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                if (i > 0)
                {
                    text->append(' ');
                }
                text->append(PDFContentStreamText::formatName(dictionary->getKey(i).getString()));
                text->append(' ');
                if (!writeObject(dictionary->getValue(i), depth + 1, text))
                {
                    return false;
                }
            }
            text->append(">>");
        }
        else
        {
            return false;
        }

        return true;
    }

    static PDFObject createMatrixObject(const QTransform& matrix)
    {
        PDFArrayBuilder array;
        for (const PDFReal value : { matrix.m11(), matrix.m12(), matrix.m21(), matrix.m22(), matrix.dx(), matrix.dy() })
        {
            array.appendItem(PDFObject::createReal(value));
        }
        return PDFObject::createArray(std::move(array));
    }

    static bool isFiniteMatrix(const QTransform& matrix)
    {
        for (const PDFReal value : { matrix.m11(), matrix.m12(), matrix.m21(), matrix.m22(), matrix.dx(), matrix.dy() })
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        return true;
    }

    /// The inverse of a matrix of a state; false for a matrix, which has none.
    ///
    /// The test of QTransform is used on purpose (a determinant, which is zero in a fuzzy
    /// comparison): it is the test of the content processor, which refuses the operator cm,
    /// when the new current transformation matrix does not pass it
    /// (PDFPageContentProcessor::operatorAdjustCurrentTransformationMatrix). So the matrix of
    /// every state of a node passes it - the builder never gives a state another one - and
    /// a command must not write a matrix the processor would drop, when the content is read
    /// again.
    static bool invertMatrix(const QTransform& matrix, QTransform* inverse)
    {
        bool isInvertible = false;
        *inverse = matrix.inverted(&isInvertible);
        return isInvertible && isFiniteMatrix(*inverse);
    }

    /// A matrix a command may move objects by: finite numbers and an inverse (invertMatrix)
    static bool isRegularMatrix(const QTransform& matrix)
    {
        return isFiniteMatrix(matrix) && matrix.isInvertible();
    }

    /// A copy of a pattern with /Matrix' = /Matrix x \p displacement as a new object of the
    /// session (R-5: the matrix of a pattern maps to the default space of the content, in which
    /// the pattern is a resource, and cm does not act on it - ISO 32000-2, 8.7.3.1).
    /// \returns the reference to the copy as an object, null with \p errorText on an error
    static PDFObject copyPattern(PDFContentEditSession* session, const PDFObject& pattern, const QTransform& displacement, QString* errorText)
    {
        PDFObject object = pattern;
        if (pattern.isReference())
        {
            object = session->getNewObject(pattern.getReference());
            if (object.isNull() && session->getDocument())
            {
                object = session->getDocument()->getObjectByReference(pattern.getReference());
            }
        }

        const PDFDictionary* dictionary = nullptr;
        if (object.isStream())
        {
            dictionary = object.getStream()->getDictionary();
        }
        else if (object.isDictionary())
        {
            dictionary = object.getDictionary();
        }

        if (!dictionary || !session->getDocument())
        {
            *errorText = PDFTranslationContext::tr("The pattern of the object cannot be read.");
            return PDFObject();
        }

        PDFDocumentDataLoaderDecorator loader(session->getDocument());
        const QTransform matrix = loader.readMatrixFromDictionary(dictionary, "Matrix", QTransform()) * displacement;
        if (!isFiniteMatrix(matrix))
        {
            *errorText = PDFTranslationContext::tr("The matrix of the pattern of the object is not a matrix of finite numbers.");
            return PDFObject();
        }

        PDFDictionaryBuilder builder(*dictionary);
        builder.setEntry(PDFInplaceOrMemoryString("Matrix"), createMatrixObject(matrix));

        PDFObject copy;
        if (object.isStream())
        {
            // The data stay as they are, encoded by the filters of the dictionary
            copy = PDFObject::createStream(PDFStream(std::move(builder), QByteArray(*object.getStream()->getContent())));
        }
        else
        {
            copy = PDFObject::createDictionary(std::move(builder));
        }

        return PDFObject::createReference(session->addNewObject(copy));
    }

    /// The color of a state is a pattern
    static bool isPatternColor(const PDFContentCommandState& state, bool isStroking)
    {
        const PDFPageContentProcessorState& processorState = state.resolved->getState();
        const PDFAbstractColorSpace* colorSpace = isStroking ? processorState.getStrokeColorSpace() : processorState.getFillColorSpace();
        return colorSpace && colorSpace->getColorSpace() == PDFAbstractColorSpace::ColorSpace::Pattern;
    }

    /// The property an operator of a text object sets, when an override of the appearance can
    /// replace it (R-1: such an operation is suppressed); Count for any other operator
    static Property getAppearanceProperty(Operator contentOperator)
    {
        switch (contentOperator)
        {
            case Operator::ColorSetStrokingColorSpace:
            case Operator::ColorSetStrokingColor:
            case Operator::ColorSetStrokingColorN:
            case Operator::ColorSetDeviceGrayStroking:
            case Operator::ColorSetDeviceRGBStroking:
            case Operator::ColorSetDeviceCMYKStroking:
                return Property::StrokeColor;

            case Operator::ColorSetFillingColorSpace:
            case Operator::ColorSetFillingColor:
            case Operator::ColorSetFillingColorN:
            case Operator::ColorSetDeviceGrayFilling:
            case Operator::ColorSetDeviceRGBFilling:
            case Operator::ColorSetDeviceCMYKFilling:
                return Property::FillColor;

            case Operator::SetLineWidth:
                return Property::LineWidth;

            case Operator::SetLineCap:
                return Property::LineCap;

            case Operator::SetLineJoin:
                return Property::LineJoin;

            case Operator::SetMitterLimit:
                return Property::MiterLimit;

            case Operator::SetLineDashPattern:
                return Property::DashPattern;

            default:
                return Property::Count;
        }
    }

    /// The key of an ExtGState, which sets a property an override of the appearance or of the
    /// transparency can carry (ISO 32000-2, Table 57); nullptr for any other property
    static const char* getExtGStateKey(Property property)
    {
        switch (property)
        {
            case Property::LineWidth:
                return "LW";
            case Property::LineCap:
                return "LC";
            case Property::LineJoin:
                return "LJ";
            case Property::MiterLimit:
                return "ML";
            case Property::DashPattern:
                return "D";
            case Property::AlphaStroking:
                return "CA";
            case Property::AlphaFilling:
                return "ca";
            case Property::BlendMode:
                return "BM";
            default:
                return nullptr;
        }
    }

    /// The prefix of a new name of a resource (zadani_jadro.md 4.4 R-4)
    static QByteArray getResourcePrefix(PDFContentResourceCategory category)
    {
        switch (category)
        {
            case PDFContentResourceCategory::Font:
                return "F";
            case PDFContentResourceCategory::XObject:
                return "Im";
            case PDFContentResourceCategory::ExtGState:
                return "GS";
            case PDFContentResourceCategory::ColorSpace:
                return "CS";
            case PDFContentResourceCategory::Pattern:
                return "P";
            case PDFContentResourceCategory::Shading:
                return "Sh";
            default:
                return "MC";
        }
    }

    /// The names of the standard blend modes (ISO 32000-2, 11.3.5, Tables 134 and 135)
    static bool isBlendModeName(const QByteArray& name)
    {
        static constexpr const char* NAMES[] = { "Normal", "Compatible", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "ColorDodge", "ColorBurn",
                                                 "HardLight", "SoftLight", "Difference", "Exclusion", "Hue", "Saturation", "Color", "Luminosity" };
        return std::any_of(std::begin(NAMES), std::end(NAMES), [&name](const char* item) { return name == item; });
    }

    /// The page and the path of the form uses, which lead to a container, through the loaded
    /// containers of a session (the identifier "p3:57/12" of a script): the first page (by its
    /// index), which reaches the form within the depth limit of the session, with the shortest
    /// path from that page (the first one in the order of the content).
    ///
    /// Each page is walked breadth first, so a form is first reached at its smallest depth from
    /// that page. One map for the whole walk keeps the smallest depth a form was entered with:
    /// a form is entered again only from a smaller depth (another page may reach it at a smaller
    /// depth than the one, where an earlier page met the limit of the nesting - it is found from
    /// that page then). So a form is entered at most MAXIMUM_CONTAINER_NESTING times, and the
    /// work is bounded by that number times the nodes of the loaded forms; the result does not
    /// depend on the order, in which the pages reach a form.
    static bool findPath(const PDFContentEditSession* session, const PDFContentContainerKey& key, PDFInteger* pageIndex, std::vector<PDFContentNodeId>* path)
    {
        path->clear();
        *pageIndex = -1;

        if (key.kind == PDFContentContainerKey::Kind::Page)
        {
            *pageIndex = key.pageIndex;
            return true;
        }

        if (key.kind != PDFContentContainerKey::Kind::Form || !session->getDocument())
        {
            return false;
        }

        struct Item
        {
            PDFContentContainerKey key;
            std::vector<PDFContentNodeId> path;
        };

        // The smallest depth (the length of the path) each form was entered with
        std::map<PDFContentContainerKey, size_t> enteredDepths;

        const PDFInteger pageCount = PDFInteger(session->getDocument()->getCatalog()->getPageCount());
        for (PDFInteger i = 0; i < pageCount; ++i)
        {
            const PDFContentContainerKey pageKey = session->getPageKey(i);
            if (!session->getContainer(pageKey))
            {
                continue;
            }

            // Breadth first: the items in the order of their depth
            std::vector<Item> queue = { Item{ pageKey, { } } };
            for (size_t head = 0; head < queue.size(); ++head)
            {
                const Item item = std::move(queue[head]);

                const PDFContentContainerPointer container = session->getContainer(item.key);
                if (!container || !container->getRoot() || item.path.size() >= size_t(PDFContentEditSession::MAXIMUM_CONTAINER_NESTING))
                {
                    continue;
                }

                for (const PDFContentNodePointer& node : getSubtree(container->getRoot()))
                {
                    if (node->getKind() != Kind::FormUse || !node->getData().xobject.isValid())
                    {
                        continue;
                    }

                    const PDFContentContainerKey formKey = PDFContentContainerKey::createForm(node->getData().xobject);
                    const size_t depth = item.path.size() + 1;
                    auto it = enteredDepths.find(formKey);
                    if (it != enteredDepths.end() && it->second <= depth)
                    {
                        continue;
                    }
                    enteredDepths[formKey] = depth;

                    Item next{ formKey, item.path };
                    next.path.push_back(node->getId());

                    if (formKey == key)
                    {
                        *pageIndex = i;
                        *path = std::move(next.path);
                        return true;
                    }

                    queue.push_back(std::move(next));
                }
            }
        }

        return false;
    }

    /// The objects with a content of their own, which a subtree paints, by the resources of its
    /// container: the XObjects of its operations (Do) and the patterns of its colors - of the
    /// states of its nodes (also the state in front of the subtree, which a copy bakes in) and
    /// of their overrides. A pattern set by scn / SCN inside of the subtree is in the state of
    /// every node behind it (also of the parts of a text), so the states hold every pattern,
    /// with which something is painted. A suppressed operation paints nothing. Whether an object
    /// is a form or a tiling pattern is decided by the caller.
    static void addPaintedObjects(const PDFContentContainer& container, const PDFContentNodePointer& top, std::vector<PDFObjectReference>* references)
    {
        const PDFContentOperationList& operations = container.getOperations();

        for (const PDFContentNodePointer& node : getSubtree(top))
        {
            if (node->getKind() == Kind::FormUse && node->getData().xobject.isValid())
            {
                references->push_back(node->getData().xobject);
            }

            for (const Property property : { Property::FillColor, Property::StrokeColor })
            {
                if (node->getSymbolicState())
                {
                    const PDFObject& pattern = node->getSymbolicState()->getSource(property).patternObject;
                    if (pattern.isReference())
                    {
                        references->push_back(pattern.getReference());
                    }
                }

                if (const PDFContentStateOverride* override = node->getOverride())
                {
                    auto it = override->properties.find(property);
                    if (it != override->properties.cend() && it->second.color.patternObject.isReference())
                    {
                        references->push_back(it->second.color.patternObject.getReference());
                    }
                }
            }

            for (const PDFContentOperationSpan& span : { node->getPrefix(), node->getSuffix() })
            {
                for (size_t i = 0; i < span.count && span.first + i < operations.getCount(); ++i)
                {
                    const PDFContentOperation& operation = operations.getOperation(span.first + i);
                    if (operation.getFlags().testFlag(PDFContentOperation::Suppressed))
                    {
                        continue;
                    }

                    if (operation.getOperator() != Operator::PaintXObject)
                    {
                        continue;
                    }

                    for (const PDFContentResourceName& resourceName : operation.getResourceNames())
                    {
                        const PDFObject object = resourceName.category == PDFContentResourceCategory::XObject ? container.getResources().getObject(resourceName.category, resourceName.name) : PDFObject();
                        if (object.isReference())
                        {
                            references->push_back(object.getReference());
                        }
                    }
                }
            }
        }
    }

    /// The references of the entries of the categories XObject and Pattern of a resource
    /// dictionary (every form and pattern a content with these resources may paint)
    static void addResourceObjects(const PDFDocument* document, const PDFObject& resourcesObject, std::vector<PDFObjectReference>* references)
    {
        const PDFObject& resources = document->getObject(resourcesObject);
        if (!resources.isDictionary())
        {
            return;
        }

        for (const char* category : { "XObject", "Pattern" })
        {
            const PDFObject& entries = document->getObject(resources.getDictionary()->get(category));
            if (!entries.isDictionary())
            {
                continue;
            }

            const PDFDictionary* dictionary = entries.getDictionary();
            for (size_t i = 0; i < dictionary->getCount(); ++i)
            {
                if (dictionary->getValue(i).isReference())
                {
                    references->push_back(dictionary->getValue(i).getReference());
                }
            }
        }
    }

    /// A copy of a subtree into the container of a form or of the cell of a tiling pattern would
    /// make that form or pattern paint itself: the subtree paints it, or paints a form or
    /// a pattern, which paints it through other forms and patterns (a form, whose pattern has
    /// a cell, which draws the form). Such content has no end for a reader, which does not guard
    /// itself (the walks of the editor have their visited sets because a document may hold it;
    /// the editor never creates it).
    ///
    /// A walk over the graph of the forms and the tiling patterns: one visited set for the whole
    /// walk, the depth limit of the session and a limit of the walked objects. A loaded form or
    /// cell is read exactly (what its operations and states paint); one, which is not loaded, by
    /// its /Resources (every XObject and pattern it names: it may paint them), without
    /// /Resources of its own by the resources of the source content (a form inherits the
    /// resources of the content, which draws it - ISO 32000-2, 7.8.3). A new object of the
    /// session (a pattern copied by an earlier command) is read like an object of the document.
    /// What the walk cannot finish is taken for a loop: the copy is refused then.
    /// \param target A form or a pattern cell; false for a page
    static bool isPaintingContainer(const PDFContentEditSession* session, const PDFContentContainer& source, const PDFContentNodePointer& top, const PDFContentContainerKey& target)
    {
        const PDFDocument* document = session->getDocument();
        if (!document || !target.isValid() || target.kind == PDFContentContainerKey::Kind::Page)
        {
            return false;
        }

        const PDFObjectReference targetReference = target.reference;

        std::vector<PDFObjectReference> painted;
        addPaintedObjects(source, top, &painted);

        std::set<PDFObjectReference> visited;
        std::vector<std::pair<PDFObjectReference, int>> stack;
        for (const PDFObjectReference& reference : painted)
        {
            if (visited.insert(reference).second)
            {
                stack.emplace_back(reference, 1);
            }
        }

        while (!stack.empty())
        {
            const std::pair<PDFObjectReference, int> current = stack.back();
            stack.pop_back();

            if (current.first == targetReference)
            {
                return true;
            }

            std::vector<PDFObjectReference> next;
            PDFContentContainerPointer container = session->getContainer(PDFContentContainerKey::createForm(current.first));
            if (!container)
            {
                container = session->getContainer(PDFContentContainerKey::createPatternCell(current.first));
            }

            if (container && container->getRoot())
            {
                addPaintedObjects(*container, container->getRoot(), &next);
            }
            else
            {
                const PDFObject newObject = session->getNewObject(current.first);
                const PDFObject& object = newObject.isNull() ? document->getObjectByReference(current.first) : newObject;
                const PDFDictionary* dictionary = object.isStream() ? object.getStream()->getDictionary() : nullptr;
                const PDFObject& subtype = dictionary ? document->getObject(dictionary->get("Subtype")) : object;
                const PDFObject& patternType = dictionary ? document->getObject(dictionary->get("PatternType")) : object;
                const bool isForm = subtype.isName() && subtype.getString() == "Form";
                const bool isTilingPattern = patternType.isInt() && patternType.getInteger() == 1;
                if (!dictionary || (!isForm && !isTilingPattern))
                {
                    // An image, a shading pattern, or nothing a reader paints as a content
                    continue;
                }

                if (dictionary->hasKey("Resources"))
                {
                    addResourceObjects(document, dictionary->get("Resources"), &next);
                }
                else
                {
                    for (const PDFContentResourceCategory category : { PDFContentResourceCategory::XObject, PDFContentResourceCategory::Pattern })
                    {
                        for (const PDFContentResources::Entry& entry : source.getResources().getEntries(category))
                        {
                            if (entry.object.isReference())
                            {
                                next.push_back(entry.object.getReference());
                            }
                        }
                    }
                }
            }

            for (const PDFObjectReference& reference : next)
            {
                if (reference == targetReference)
                {
                    return true;
                }

                if (!visited.insert(reference).second)
                {
                    continue;
                }

                if (current.second >= PDFContentEditSession::MAXIMUM_CONTAINER_NESTING || visited.size() > MAXIMUM_WALKED_FORMS)
                {
                    // Deeper or larger than the walk goes: not known, so not allowed
                    return true;
                }

                stack.emplace_back(reference, current.second + 1);
            }
        }

        return false;
    }
};

/// One computation of a command: the editors of the containers it changes, the changes of the
/// structure and the list of what was changed. Nothing reaches the session before the command
/// has succeeded (PDFContentModelCommand::apply), except the identifiers and the new objects it
/// has taken.
///
/// Threads and ownership: a local object of one apply, in the thread of the session; it owns
/// its editors.
class PDFContentCommandWork
{
public:
    using Helper = PDFContentCommandHelper;
    using Kind = PDFContentNodeKind;
    using Property = PDFContentStateProperty;
    using State = PDFContentCommandState;

    /// A container being changed
    struct Container
    {
        PDFContentContainerKey key;
        PDFContentContainerPointer source;                      ///< The version before the command
        std::unique_ptr<PDFContentContainerEditor> editor;
        bool isForm = false;                                    ///< A form or a cell of a pattern: an inherited property is unknown
        bool isTouched = false;
        size_t nodeCount = 0;                                   ///< The nodes of the tree being changed
        size_t sourceNodeCount = 0;                             ///< The nodes of the version before the command
        std::optional<PDFInteger> largestMarkedContentId;       ///< Of the content, with the identifiers the command has given
    };

    explicit PDFContentCommandWork(PDFContentEditSession* session) :
        m_session(session)
    {

    }

    PDFContentEditSession* getSession() const { return m_session; }

    /// The container of a key for a change: it must be loaded and not read-only
    Container* getContainer(const PDFContentContainerKey& key, QString* errorText)
    {
        auto it = m_containers.find(key);
        if (it != m_containers.end())
        {
            return it->second.get();
        }

        PDFContentContainerPointer source = m_session->getContainer(key);
        if (!source)
        {
            *errorText = PDFTranslationContext::tr("The content of the object is not loaded.");
            return nullptr;
        }

        if (source->isReadOnly())
        {
            *errorText = PDFTranslationContext::tr("The content of the page cannot be read whole, so it cannot be changed.");
            return nullptr;
        }

        std::unique_ptr<Container> container = std::make_unique<Container>();
        container->key = key;
        container->source = source;
        container->editor = std::make_unique<PDFContentContainerEditor>(source);
        container->isForm = key.kind != PDFContentContainerKey::Kind::Page;
        container->nodeCount = source->getNodeCount();
        container->sourceNodeCount = container->nodeCount;

        Container* result = container.get();
        m_containers[key] = std::move(container);
        return result;
    }

    /// The node of a reference with its container
    PDFContentNodePointer getNode(const PDFContentNodeReference& reference, Container** container, QString* errorText)
    {
        if (!reference.isValid())
        {
            *errorText = PDFTranslationContext::tr("The object is not valid.");
            return nullptr;
        }

        *container = getContainer(reference.container, errorText);
        if (!*container)
        {
            return nullptr;
        }

        PDFContentNodePointer node = (*container)->editor->getNode(reference.node);
        if (!node)
        {
            *errorText = PDFTranslationContext::tr("The object does not exist.");
        }
        return node;
    }

    // ---- queries of the tree being changed ----

    static PDFInteger getIndex(const Container* container, PDFContentNodeId id)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        const PDFContentNodePointer parent = node ? container->editor->getNode(node->getParentId()) : nullptr;
        if (!parent)
        {
            return -1;
        }

        const std::vector<PDFContentNodePointer>& children = parent->getChildren();
        for (size_t i = 0; i < children.size(); ++i)
        {
            if (children[i]->getId() == id)
            {
                return PDFInteger(i);
            }
        }
        return -1;
    }

    static PDFInteger getChildCount(const Container* container, PDFContentNodeId id)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        return node ? PDFInteger(node->getChildren().size()) : 0;
    }

    /// \p ancestor is the node \p id or one of the nodes above it
    static bool isAncestorOrSelf(const Container* container, PDFContentNodeId ancestor, PDFContentNodeId id)
    {
        int depth = 0;
        PDFContentNodePointer node = container->editor->getNode(id);
        while (node && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            if (node->getId() == ancestor)
            {
                return true;
            }
            node = container->editor->getNode(node->getParentId());
        }
        return false;
    }

    /// The node \p id or a node above it is a text object (q, cm, Do and paths are not allowed
    /// between BT and ET - ISO 32000-2, 8.2)
    static bool isInTextObject(const Container* container, PDFContentNodeId id)
    {
        int depth = 0;
        PDFContentNodePointer node = container->editor->getNode(id);
        while (node && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            if (node->getKind() == Kind::TextObject)
            {
                return true;
            }
            node = container->editor->getNode(node->getParentId());
        }
        return false;
    }

    /// The nearest sequence of the structure (a marked content with an MCID) at or above a node
    static PDFContentNodeId getSequence(const Container* container, PDFContentNodeId id)
    {
        int depth = 0;
        PDFContentNodePointer node = container->editor->getNode(id);
        while (node && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            if (node->getKind() == Kind::MarkedContent && node->getData().mcid)
            {
                return node->getId();
            }
            node = container->editor->getNode(node->getParentId());
        }
        return PDFContentNodeId();
    }

    /// The nodes above a node in the version of the container before the command, the nearest
    /// one first. The checks a command makes before its first change read the tree so: nothing
    /// is built for them (a node the command has created has none).
    static std::vector<PDFContentNodePointer> getSourceAncestors(const Container* container, PDFContentNodeId id)
    {
        std::vector<PDFContentNodePointer> result;
        PDFContentNodePointer node = container->source->getParent(id);
        while (node && result.size() <= size_t(PDFContentContainer::MAXIMUM_DEPTH) + 2)
        {
            result.push_back(node);
            node = container->source->getParent(node->getId());
        }
        return result;
    }

    /// A node above the node \p id (in the version before the command) is a text object
    static bool isInSourceTextObject(const Container* container, PDFContentNodeId id)
    {
        const std::vector<PDFContentNodePointer> ancestors = getSourceAncestors(container, id);
        return std::any_of(ancestors.cbegin(), ancestors.cend(), [](const PDFContentNodePointer& ancestor) { return ancestor->getKind() == Kind::TextObject; });
    }

    /// The node \p id or a node above it is not closed by the content (a q without its Q, a BT
    /// without its ET, a BDC without its EMC: PDFContentContainer::isUnclosed). Such a node
    /// reaches to the end of the content with everything inside of it: neither it nor a node
    /// inside of it is moved, transformed or reordered.
    static bool isInUnclosedNode(const Container* container, PDFContentNodeId id)
    {
        if (container->source->isUnclosed(id))
        {
            return true;
        }

        const std::vector<PDFContentNodePointer> ancestors = getSourceAncestors(container, id);
        return std::any_of(ancestors.cbegin(), ancestors.cend(), [container](const PDFContentNodePointer& ancestor) { return container->source->isUnclosed(ancestor->getId()); });
    }

    /// The first child of a parent, which must stay behind every other one (the tail of the
    /// content, the rest behind an inline image without an end, a node, which ends with such
    /// a rest; there may be two of them): new content goes in front of it
    /// (PDFContentContainer::getAppendIndex). Not valid, when there is none.
    static PDFContentNodeId getPinnedChild(const Container* container, PDFContentNodeId parentId)
    {
        const PDFContentNodePointer sourceParent = container->source->getNode(parentId);
        const PDFInteger appendIndex = container->source->getAppendIndex(parentId);
        if (!sourceParent || appendIndex < 0)
        {
            return PDFContentNodeId();
        }

        // A pinned node may be gone by the command already (Clear Page, a delete of its parent)
        const std::vector<PDFContentNodePointer>& children = sourceParent->getChildren();
        for (size_t i = size_t(appendIndex); i < children.size(); ++i)
        {
            const PDFContentNodeId pinnedId = children[i]->getId();
            const PDFContentNodePointer pinned = container->editor->getNode(pinnedId);
            if (pinned && pinned->getParentId() == parentId)
            {
                return pinnedId;
            }
        }

        return PDFContentNodeId();
    }

    /// The node is a child, which must stay behind every other child of its parent
    static bool isPinned(const Container* container, PDFContentNodeId id)
    {
        const PDFContentNodePointer node = container->source->getNode(id);
        if (!node || !node->getParentId().isValid())
        {
            return false;
        }

        const PDFInteger appendIndex = container->source->getAppendIndex(node->getParentId());
        return appendIndex >= 0 && container->source->getIndexInParent(id) >= appendIndex;
    }

    /// The state of the place in front of the child \p index of a parent (behind its last child
    /// for the number of the children)
    static State getStateAt(Container* container, PDFContentNodeId parentId, PDFInteger index)
    {
        const PDFContentNodePointer parent = container->editor->getNode(parentId);
        if (!parent)
        {
            return State();
        }

        const std::vector<PDFContentNodePointer>& children = parent->getChildren();
        if (index >= 0 && index < PDFInteger(children.size()))
        {
            const State state{ children[size_t(index)]->getState(), children[size_t(index)]->getSymbolicState() };
            if (state.isValid())
            {
                return state;
            }
        }

        if (index > 0 && index <= PDFInteger(children.size()))
        {
            return Helper::getStateAfter(*children[size_t(index - 1)]);
        }

        if (parent->getKind() == Kind::Root)
        {
            const PDFContentContainer::Data& data = container->editor->getContainerData();
            return State{ data.initialState, data.initialSymbolicState };
        }

        return State{ parent->getState(), parent->getSymbolicState() };
    }

    // ---- changes ----

    void setNodeData(Container* container, PDFContentNodeId id, PDFContentNode::Data data)
    {
        container->editor->setNodeData(id, std::move(data));
        container->isTouched = true;
        m_changed.insert(PDFContentNodeReference{ container->key, id });
    }

    size_t appendOperation(Container* container, PDFContentOperation operation)
    {
        container->isTouched = true;
        return container->editor->getOperations().append(std::move(operation));
    }

    void addAddedNode(Container* container, PDFContentNodeId id)
    {
        const PDFContentNodeReference reference{ container->key, id };
        if (m_addedSet.insert(reference).second)
        {
            m_added.push_back(reference);
        }
    }

    /// Inserts a new node; false, when the content would be nested too deep, and at the limits
    /// against growth: every node a command creates comes through here (a copy, a state run,
    /// a group, a wrap), so one count bounds them all. The nodes of a container are counted
    /// here and checked once at the end of the command (checkContainerLimits): a command may
    /// add a node before it removes others.
    bool insertNode(Container* container, PDFContentNode::Data data, PDFContentNodeId parentId, PDFInteger index, QString* errorText)
    {
        if (m_createdCount >= PDFContentCommand::MAXIMUM_NODES_OF_COMMAND)
        {
            *errorText = PDFTranslationContext::tr("The change would create more than %Ln object(s) at once.", nullptr, int(PDFContentCommand::MAXIMUM_NODES_OF_COMMAND));
            return false;
        }

        if (container->editor->getOperations().getCount() > Helper::MAXIMUM_OPERATIONS_OF_CONTAINER)
        {
            *errorText = PDFTranslationContext::tr("The content would have more than %Ln object(s).", nullptr, int(PDFContentCommand::MAXIMUM_NODES_OF_CONTAINER));
            return false;
        }

        const PDFContentNodeId id = data.id;
        if (!container->editor->insertNode(std::move(data), parentId, index))
        {
            *errorText = PDFTranslationContext::tr("The object cannot be placed there: the content would be nested too deep.");
            return false;
        }

        ++m_createdCount;
        ++container->nodeCount;
        container->isTouched = true;
        addAddedNode(container, id);
        return true;
    }

    bool moveNode(Container* container, PDFContentNodeId id, PDFContentNodeId parentId, PDFInteger index, QString* errorText)
    {
        if (!container->editor->moveNode(id, parentId, index))
        {
            *errorText = PDFTranslationContext::tr("The object cannot be placed there: the content would be nested too deep, or the place is inside of the object itself.");
            return false;
        }

        container->isTouched = true;
        m_changed.insert(PDFContentNodeReference{ container->key, id });
        return true;
    }

    /// Removes a node with its subtree, without anything else (see removeNode)
    void eraseNode(Container* container, PDFContentNodeId id)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        if (!node)
        {
            return;
        }

        size_t count = 0;
        for (const PDFContentNodePointer& item : Helper::getSubtree(node))
        {
            m_removed.insert(PDFContentNodeReference{ container->key, item->getId() });
            ++count;
        }

        container->editor->removeNode(id);
        container->nodeCount -= qMin(count, container->nodeCount);
        container->isTouched = true;
    }

    /// A new state run (a node without operations, which sets what its override says)
    bool insertStateRun(Container* container,
                        PDFContentNodeId parentId,
                        PDFInteger index,
                        const State& begin,
                        PDFContentStateOverride override,
                        PDFContentStateLeak leak,
                        QString* errorText)
    {
        PDFContentNode::Data data;
        data.kind = Kind::StateRun;
        data.id = m_session->allocateNodeId();
        data.state = begin.resolved;
        data.symbolicState = begin.symbolic;
        data.leak = std::move(leak);
        data.override = std::move(override);
        return insertNode(container, std::move(data), parentId, index, errorText);
    }

    /// A leak has something a residual state run sets again (the clipping path is none of it)
    static bool hasResidual(const PDFContentStateLeak& leak)
    {
        return !leak.properties.isEmpty() || leak.isTransformChanged;
    }

    /// R-3: the leak of a node stops reaching the nodes behind it - a state run at the place of
    /// its end sets it. The clipping path is never set again (a deleted clip is gone).
    /// \param begin The state in front of the run (the state the node began with)
    /// \param stateOperations The operations the states of the leak refer to; nullptr: those of
    ///        the container (the states of a subtree copied from another container refer to
    ///        the operations of their source)
    bool insertResidual(Container* container,
                        PDFContentNodeId parentId,
                        PDFInteger index,
                        const State& begin,
                        const PDFContentStateLeak& leak,
                        QString* errorText,
                        const PDFContentOperationList* stateOperations = nullptr)
    {
        if (!hasResidual(leak))
        {
            return true;
        }

        const State end{ leak.endState, leak.endSymbolicState };
        if (!begin.isValid() || !end.isValid())
        {
            *errorText = PDFTranslationContext::tr("The graphics state behind the object is not known.");
            return false;
        }

        if (index < 0)
        {
            *errorText = PDFTranslationContext::tr("The place of the object in the content is not known.");
            return false;
        }

        PDFContentStateOverride override;

        if (leak.isTransformChanged)
        {
            QTransform inverted;
            if (!Helper::invertMatrix(begin.resolved->getCurrentTransformationMatrix(), &inverted))
            {
                *errorText = PDFTranslationContext::tr("The transformation matrix in front of the object cannot be inverted.");
                return false;
            }

            // M x CTM of the beginning = CTM of the end
            override.transform = end.resolved->getCurrentTransformationMatrix() * inverted;
        }

        for (const Property property : leak.properties.getProperties())
        {
            PDFContentStateValue value;
            bool isSkipped = false;
            if (!Helper::makeValue(property, end, stateOperations ? *stateOperations : container->editor->getOperations(), &value, &isSkipped, errorText))
            {
                return false;
            }

            if (!isSkipped)
            {
                override.properties[property] = std::move(value);
            }
        }

        if (override.isEmpty())
        {
            return true;
        }

        PDFContentStateLeak runLeak = leak;
        runLeak.isClipChanged = false;
        runLeak.isUnknown = false;
        return insertStateRun(container, parentId, index, begin, std::move(override), std::move(runLeak), errorText);
    }

    /// Removes a node with its subtree; its leak is kept for the nodes behind it (R-3)
    bool removeNode(Container* container, PDFContentNodeId id, QString* errorText)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        if (!node)
        {
            return true;
        }

        const PDFContentNodeId parentId = node->getParentId();
        const PDFInteger index = getIndex(container, id);
        const State begin{ node->getState(), node->getSymbolicState() };
        const PDFContentStateLeak leak = node->getLeak();

        eraseNode(container, id);
        return insertResidual(container, parentId, index, begin, leak, errorText);
    }

    /// Gives a node new data with another override (R-1). The first wrap of a node closes its
    /// leak inside: a residual state run behind it keeps the leak for the following nodes (R-3).
    /// \param stateOperations See insertResidual
    bool setOverride(Container* container, PDFContentNode::Data data, QString* errorText, const PDFContentOperationList* stateOperations = nullptr)
    {
        const PDFContentNodePointer node = container->editor->getNode(data.id);
        const PDFContentNode::Data& old = node->getData();

        const bool isWrap = PDFContentNode::isWrappableKind(old.kind) && !old.hasClipEffect;
        if (isWrap && !old.override && data.override)
        {
            // The place is looked up only for a node, which leaks (a path, an image and a shading
            // never do): the lookup reads the children of the parent
            if (hasResidual(old.leak) &&
                !insertResidual(container, old.parentId, getIndex(container, data.id) + 1, State{ old.state, old.symbolicState }, old.leak, errorText, stateOperations))
            {
                return false;
            }
            data.leak = PDFContentStateLeak();
        }

        const PDFContentNodeId id = data.id;
        setNodeData(container, id, std::move(data));
        return true;
    }

    /// The leak of a marked content is the leak of its children: computed again for the marked
    /// contents from a node upwards, after their children changed
    void refreshLeaks(Container* container, PDFContentNodeId id)
    {
        int depth = 0;
        PDFContentNodePointer node = container->editor->getNode(id);
        while (node && node->getKind() == Kind::MarkedContent && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            const State begin{ node->getState(), node->getSymbolicState() };
            const std::vector<PDFContentNodePointer>& children = node->getChildren();
            const State end = children.empty() ? begin : Helper::getStateAfter(*children.back());
            const bool isClipChanged = std::any_of(children.cbegin(), children.cend(), [](const PDFContentNodePointer& child) { return child->hasClipEffect() || child->getLeak().isClipChanged; });

            const PDFContentStateLeak leak = Helper::makeLeak(begin, end, container->isForm, isClipChanged);
            const PDFContentStateLeak& oldLeak = node->getLeak();
            if (!(leak.properties == oldLeak.properties) || leak.isTransformChanged != oldLeak.isTransformChanged || leak.isClipChanged != oldLeak.isClipChanged ||
                leak.endState != oldLeak.endState || leak.endSymbolicState != oldLeak.endSymbolicState)
            {
                PDFContentNode::Data data = node->getData();
                data.leak = leak;
                const PDFContentNodeId nodeId = data.id;
                container->editor->setNodeData(nodeId, std::move(data));
                container->isTouched = true;
            }

            node = container->editor->getNode(node->getParentId());
        }
    }

    /// A new group q ... Q without children
    bool insertGroup(Container* container, PDFContentNodeId parentId, PDFInteger index, const State& state, const QRectF& boundingBox, PDFContentNodeId* groupId, QString* errorText)
    {
        PDFContentNode::Data data;
        data.kind = Kind::Group;
        data.id = m_session->allocateNodeId();
        data.saveDepth = 1;
        data.state = state.resolved;
        data.symbolicState = state.symbolic;
        data.boundingBox = boundingBox;
        data.prefix = PDFContentOperationSpan{ appendOperation(container, Helper::createOperation(PDFContentOperator::SaveGraphicState, "q")), 1 };
        data.suffix = PDFContentOperationSpan{ appendOperation(container, Helper::createOperation(PDFContentOperator::RestoreGraphicState, "Q")), 1 };

        *groupId = data.id;
        return insertNode(container, std::move(data), parentId, index, errorText);
    }

    /// Gives a node, which is at a new place already, the state of the place and the override,
    /// which keeps what it saw at its old place (R-2). A node, which leaks, never leaks at its new
    /// place: a leaf gets a wrap, a marked content a new group around it. The caller has put the
    /// residual state run to the old place.
    /// \param source The state the node saw (its container may be another one: a copy)
    /// \param target The state of the new place
    /// \param topId The node, which stands at the place now (the node, or its new group)
    bool placeNode(Container* container,
                   PDFContentNodeId id,
                   const State& source,
                   const State& target,
                   bool isSourceForm,
                   bool isSameContainer,
                   const PDFContentOperationList& sourceOperations,
                   PDFContentNodeId* topId,
                   QString* errorText)
    {
        *topId = id;

        const PDFContentNodePointer node = container->editor->getNode(id);
        PDFContentNode::Data data = node->getData();

        PDFContentStateOverride bake;
        if (!Helper::computeBake(data, source, target, isSourceForm, container->isForm, isSameContainer, sourceOperations, container->editor->getOperations(), &bake, errorText))
        {
            return false;
        }

        switch (data.kind)
        {
            case Kind::Path:
            case Kind::Image:
            case Kind::Shading:
            case Kind::FormUse:
            case Kind::TextObject:
            {
                if (data.hasClipEffect && data.kind == Kind::TextObject)
                {
                    // A text, which adds to the clipping path, has no wrap (the wrap would end
                    // the clip) and its override is written from stage E2 only
                    if (!bake.isEmpty() || data.override)
                    {
                        *errorText = PDFTranslationContext::tr("A text, which adds to the clipping path, cannot be placed into another graphics state yet.");
                        return false;
                    }

                    // It leaks at its new place: a state run behind it sets the state of the place again
                    PDFContentStateOverride restore;
                    for (const Property property : data.leak.properties.getProperties())
                    {
                        if (container->isForm && target.symbolic->isInherited(property))
                        {
                            *errorText = PDFTranslationContext::tr("A text, which adds to the clipping path, cannot be placed there: the graphics state of the place comes from the use of the form.");
                            return false;
                        }

                        PDFContentStateValue value;
                        bool isSkipped = false;
                        if (!Helper::makeValue(property, target, container->editor->getOperations(), &value, &isSkipped, errorText))
                        {
                            return false;
                        }
                        if (!isSkipped)
                        {
                            restore.properties[property] = std::move(value);
                        }
                    }

                    const State end = Helper::getStateAfter(*node);
                    data.state = target.resolved;
                    data.symbolicState = target.symbolic;
                    setNodeData(container, id, std::move(data));

                    if (!restore.isEmpty())
                    {
                        PDFContentStateLeak restoreLeak;
                        restoreLeak.properties = node->getLeak().properties;
                        restoreLeak.endState = target.resolved;
                        restoreLeak.endSymbolicState = target.symbolic;
                        return insertStateRun(container, node->getParentId(), getIndex(container, id) + 1, end, std::move(restore), std::move(restoreLeak), errorText);
                    }
                    return true;
                }

                if (data.hasClipEffect)
                {
                    // A path with a clipping effect: its override is written without a wrap around
                    // the clip (the stream writer); a clipping path paints nothing
                    if (!bake.isEmpty() || data.override)
                    {
                        data.override = Helper::composeOverride(data.override, bake);
                    }
                }
                else if (!bake.isEmpty() || data.override || !data.leak.isEmpty())
                {
                    // The wrap, also an empty one for a node, which leaks: nothing leaks at the new place
                    data.override = Helper::composeOverride(data.override, bake);
                    data.leak = PDFContentStateLeak();
                }

                data.state = target.resolved;
                data.symbolicState = target.symbolic;
                setNodeData(container, id, std::move(data));
                return true;
            }

            case Kind::Group:
            {
                // q ... Q keeps everything inside: the bake is a state run at its beginning
                data.state = target.resolved;
                data.symbolicState = target.symbolic;
                setNodeData(container, id, std::move(data));

                if (!bake.isEmpty())
                {
                    PDFContentStateLeak leak;
                    for (const auto& item : bake.properties)
                    {
                        leak.properties.insert(item.first);
                    }
                    leak.isTransformChanged = !bake.transform.isIdentity();
                    leak.endState = source.resolved;
                    leak.endSymbolicState = source.symbolic;
                    return insertStateRun(container, id, 0, target, std::move(bake), std::move(leak), errorText);
                }
                return true;
            }

            case Kind::MarkedContent:
            {
                if (bake.isEmpty() && data.leak.isEmpty())
                {
                    data.state = target.resolved;
                    data.symbolicState = target.symbolic;
                    setNodeData(container, id, std::move(data));
                    return true;
                }

                // BDC ... EMC has no wrap of its own: a new group around it carries the bake and
                // keeps the leak of the marked content from the new place
                const PDFContentNodeId parentId = data.parentId;
                const PDFInteger index = getIndex(container, id);

                PDFContentNodeId groupId;
                if (!insertGroup(container, parentId, index, target, data.boundingBox, &groupId, errorText) ||
                    !moveNode(container, id, groupId, -1, errorText))
                {
                    return false;
                }

                *topId = groupId;

                if (!bake.isEmpty())
                {
                    PDFContentStateLeak leak;
                    for (const auto& item : bake.properties)
                    {
                        leak.properties.insert(item.first);
                    }
                    leak.isTransformChanged = !bake.transform.isIdentity();
                    leak.endState = source.resolved;
                    leak.endSymbolicState = source.symbolic;
                    return insertStateRun(container, groupId, 0, target, std::move(bake), std::move(leak), errorText);
                }
                return true;
            }

            case Kind::MarkedPoint:
                data.state = target.resolved;
                data.symbolicState = target.symbolic;
                setNodeData(container, id, std::move(data));
                return true;

            default:
                break;
        }

        *errorText = PDFTranslationContext::tr("An object of this kind cannot be moved.");
        return false;
    }

    /// A text object sets a pattern as a color of one of its parts inside (between BT and ET).
    /// The wrap moves the glyphs, but cm does not move a pattern (R-5; ISO 32000-2, 8.7.3.1), and
    /// the operator, which sets it, stands inside of the text, where the wrap cannot replace it by
    /// a moved copy without changing the rest of the text: such a text is not moved before the
    /// canonical rewrite of texts (stage E2). A soft mask set inside needs nothing: its space is
    /// the matrix of the moment of its gs (ISO 32000-2, 11.6.5.1), which is inside of the wrap,
    /// so the mask moves with the text.
    /// The operations of the text are read in their order (those of the container: a copy has
    /// its own). An operation, which is suppressed, sets nothing (R-1: an override of the color
    /// of the whole text suppresses the color operators of the text); a pattern set behind the
    /// last glyphs paints nothing of the text.
    static bool isSettingPatternInside(Container* container, const PDFContentNodePointer& text)
    {
        const PDFContentOperationList& operations = container->editor->getOperations();
        bool isPatternSet[2] = { false, false };    // Filling, stroking

        for (const PDFContentNodePointer& item : Helper::getSubtree(text))
        {
            if (item->getKind() == Kind::TextShow)
            {
                if (isPatternSet[0] || isPatternSet[1])
                {
                    return true;
                }
                continue;
            }

            const PDFContentOperationSpan& span = item->getPrefix();
            for (size_t i = 0; i < span.count && span.first + i < operations.getCount(); ++i)
            {
                const PDFContentOperation& operation = operations.getOperation(span.first + i);
                const Property property = Helper::getAppearanceProperty(operation.getOperator());
                if (operation.getFlags().testFlag(PDFContentOperation::Suppressed) || (property != Property::FillColor && property != Property::StrokeColor))
                {
                    continue;
                }

                // scn / SCN with a name as its last operand selects a pattern (8.7.3.2, 8.7.3.3);
                // every other color operator ends it
                const bool isColorN = operation.getOperator() == PDFContentOperator::ColorSetFillingColorN || operation.getOperator() == PDFContentOperator::ColorSetStrokingColorN;
                const PDFContentOperation::Operands& operands = operation.getOperands();
                isPatternSet[property == Property::StrokeColor ? 1 : 0] = isColorN && !operands.empty() && operands.back().type == PDFLexicalAnalyzer::TokenType::Name;
            }
        }

        return false;
    }

    /// Moves a leaf by a matrix of the default space of its container (R-1; R-5 for its
    /// patterns, R-6 for its soft mask)
    /// \param stateOperations See insertResidual
    bool transformLeaf(Container* container, PDFContentNodeId id, const QTransform& displacement, QString* errorText, const PDFContentOperationList* stateOperations = nullptr)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        PDFContentNode::Data data = node->getData();
        const State state{ data.state, data.symbolicState };
        const PDFContentOperationList& operations = stateOperations ? *stateOperations : container->editor->getOperations();

        if (!state.isValid())
        {
            *errorText = PDFTranslationContext::tr("The graphics state of the object is not known.");
            return false;
        }

        if (data.kind == Kind::TextObject && data.hasClipEffect)
        {
            *errorText = PDFTranslationContext::tr("A text, which adds to the clipping path, cannot be moved yet.");
            return false;
        }

        if (data.kind == Kind::TextObject && isSettingPatternInside(container, node))
        {
            *errorText = PDFTranslationContext::tr("The text sets a pattern as its color inside; it cannot be moved yet, because the pattern would stay where it is.");
            return false;
        }

        const QTransform& matrix = state.resolved->getCurrentTransformationMatrix();
        QTransform inverted;
        if (!Helper::invertMatrix(matrix, &inverted))
        {
            *errorText = PDFTranslationContext::tr("The object cannot be moved, because its transformation matrix cannot be inverted.");
            return false;
        }

        // Inside of the wrap the matrix is T x CTM. The object moves by D in the space of the
        // container: T' x CTM = T x CTM x D, so T' = T x CTM x D x CTM^-1.
        PDFContentStateOverride override = data.override ? *data.override : PDFContentStateOverride();
        override.transform = override.transform * matrix * displacement * inverted;

        const bool isClippingPath = data.kind == Kind::Path && data.paint.isClippingPath();
        if (!isClippingPath)
        {
            // R-5: cm does not move a pattern, a copy with /Matrix x D does
            for (const bool isStroking : { false, true })
            {
                const Property property = isStroking ? Property::StrokeColor : Property::FillColor;
                const bool isUsed = data.kind == Kind::Path ? (isStroking ? data.paint.stroke : data.paint.fill) : data.kind != Kind::Shading;
                if (!isUsed)
                {
                    continue;
                }

                PDFContentStateValue value;
                bool isPattern = false;

                auto it = override.properties.find(property);
                if (it != override.properties.end())
                {
                    value = it->second;
                    isPattern = value.kind == PDFContentStateValue::Kind::Color && value.color.isPattern();
                }
                else if (Helper::isPatternColor(state, isStroking))
                {
                    bool isSkipped = false;
                    if (!Helper::makeValue(property, state, operations, &value, &isSkipped, errorText))
                    {
                        return false;
                    }

                    if (!value.color.isPattern())
                    {
                        *errorText = PDFTranslationContext::tr("The object cannot be moved, because its pattern is not set by the content itself.");
                        return false;
                    }
                    isPattern = true;
                }

                if (isPattern)
                {
                    value.color.patternObject = Helper::copyPattern(m_session, value.color.patternObject, displacement, errorText);
                    if (value.color.patternObject.isNull())
                    {
                        return false;
                    }
                    override.properties[property] = std::move(value);
                }
            }

            // R-6: the mask moves with the object - it is set again in its space moved by D
            auto it = override.properties.find(Property::SoftMask);
            if (it != override.properties.end())
            {
                if (it->second.kind == PDFContentStateValue::Kind::Object && !Helper::isNoneObject(it->second.object))
                {
                    it->second.matrix = it->second.matrix * displacement;
                }
            }
            else if (state.resolved->hasSoftMask())
            {
                PDFContentStateValue value;
                bool isSkipped = false;
                if (!Helper::makeValue(Property::SoftMask, state, operations, &value, &isSkipped, errorText))
                {
                    return false;
                }

                value.matrix = value.matrix * displacement;
                override.properties[Property::SoftMask] = std::move(value);
            }
        }

        data.override = std::move(override);
        return setOverride(container, std::move(data), errorText, stateOperations);
    }

    /// Moves a node by a matrix of the default space of its container: a leaf itself, a group
    /// or a marked content through its leaves (a container has no wrap, R-1)
    /// \param isNamed The command names the node itself (a node, which cannot be moved, is an
    ///        error then; below a container a state run and a marked point are skipped)
    /// \param count Number of the moved leaves
    /// \param nestedOperations The operations the states of the nodes below the first node refer
    ///        to; nullptr: those of the container. A subtree copied from another container keeps
    ///        the states of its source below its top (the top has the state of its new place), and
    ///        a state names the operation, which set a color, by its index in its own container:
    ///        the tint components of a pattern are read from there.
    bool transformNode(Container* container,
                       PDFContentNodeId id,
                       const QTransform& displacement,
                       bool isNamed,
                       int depth,
                       size_t* count,
                       QString* errorText,
                       const PDFContentOperationList* nestedOperations = nullptr)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        if (!node)
        {
            return true;
        }

        switch (node->getKind())
        {
            case Kind::Path:
            case Kind::Image:
            case Kind::Shading:
            case Kind::FormUse:
            case Kind::TextObject:
                ++*count;
                return transformLeaf(container, id, displacement, errorText, depth > 0 ? nestedOperations : nullptr);

            case Kind::Group:
            case Kind::MarkedContent:
            {
                if (depth > PDFContentContainer::MAXIMUM_DEPTH + 2)
                {
                    break;
                }

                // The identifiers first: a wrap inserts state runs among the children
                std::vector<PDFContentNodeId> children;
                for (const PDFContentNodePointer& child : node->getChildren())
                {
                    children.push_back(child->getId());
                }

                for (const PDFContentNodeId childId : children)
                {
                    if (!transformNode(container, childId, displacement, false, depth + 1, count, errorText, nestedOperations))
                    {
                        return false;
                    }
                }
                return true;
            }

            case Kind::StateRun:
            case Kind::MarkedPoint:
                if (!isNamed)
                {
                    return true;
                }
                break;

            default:
                break;
        }

        *errorText = node->getKind() == Kind::Opaque ? PDFTranslationContext::tr("The content has a part, which cannot be read, so it cannot be moved.")
                                                     : PDFTranslationContext::tr("An object of this kind cannot be moved.");
        return false;
    }

    /// The operations of a subtree (prefix and suffix of every node)
    static std::vector<size_t> getOperationIndices(const PDFContentNodePointer& node)
    {
        std::vector<size_t> result;
        for (const PDFContentNodePointer& item : Helper::getSubtree(node))
        {
            for (const PDFContentOperationSpan& span : { item->getPrefix(), item->getSuffix() })
            {
                for (size_t i = 0; i < span.count; ++i)
                {
                    result.push_back(span.first + i);
                }
            }
        }
        return result;
    }

    /// R-1 for a text object: its own operators, which set an overridden property, would cancel
    /// the wrap, so they are suppressed. A gs of the text, which sets such a property together
    /// with others, cannot be suppressed: the change is refused.
    bool suppressOperations(Container* container, const PDFContentNodePointer& node, const std::set<Property>& properties, QString* errorText)
    {
        PDFContentOperationList& operations = container->editor->getOperations();

        for (const size_t index : getOperationIndices(node))
        {
            if (index >= operations.getCount())
            {
                continue;
            }

            const PDFContentOperation& operation = operations.getOperation(index);
            if (operation.getFlags().testFlag(PDFContentOperation::Suppressed))
            {
                continue;
            }

            if (operation.getOperator() == PDFContentOperator::SetGraphicState)
            {
                PDFObject dictionaryObject;
                if (!operation.getResourceNames().empty())
                {
                    dictionaryObject = container->editor->getResources().getObject(PDFContentResourceCategory::ExtGState, operation.getResourceNames().front().name);
                }
                if (dictionaryObject.isReference() && m_session->getDocument())
                {
                    dictionaryObject = m_session->getDocument()->getObjectByReference(dictionaryObject.getReference());
                }

                const PDFDictionary* dictionary = dictionaryObject.isDictionary() ? dictionaryObject.getDictionary() : nullptr;
                for (const Property property : properties)
                {
                    const char* key = Helper::getExtGStateKey(property);
                    if (dictionary && key && dictionary->hasKey(key))
                    {
                        *errorText = PDFTranslationContext::tr("The text sets this property by a graphics state dictionary inside; it cannot be changed for the whole text yet.");
                        return false;
                    }
                }
                continue;
            }

            if (properties.count(Helper::getAppearanceProperty(operation.getOperator())))
            {
                PDFContentOperation::Flags flags = operation.getFlags();
                flags.setFlag(PDFContentOperation::Suppressed, true);
                operations.setFlags(index, flags);
                container->isTouched = true;
            }
        }

        return true;
    }

    /// Sets properties of the appearance or of the transparency of a node: a leaf gets them into
    /// its override, a group or a marked content gives them to its leaves (R-1).
    /// \param isAppearance Colors and line parameters (paths and texts only); else the alphas
    ///        and the blend mode (every leaf, which paints)
    bool setProperties(Container* container,
                       PDFContentNodeId id,
                       const std::map<Property, PDFContentStateValue>& properties,
                       bool isAppearance,
                       bool isNamed,
                       int depth,
                       size_t* count,
                       QString* errorText)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        if (!node)
        {
            return true;
        }

        const Kind kind = node->getKind();
        const bool isLeaf = isAppearance ? (kind == Kind::Path || kind == Kind::TextObject)
                                         : (kind == Kind::Path || kind == Kind::TextObject || kind == Kind::Image || kind == Kind::Shading || kind == Kind::FormUse);

        if (kind == Kind::Group || kind == Kind::MarkedContent)
        {
            if (depth > PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                return true;
            }

            std::vector<PDFContentNodeId> children;
            for (const PDFContentNodePointer& child : node->getChildren())
            {
                children.push_back(child->getId());
            }

            for (const PDFContentNodeId childId : children)
            {
                if (!setProperties(container, childId, properties, isAppearance, false, depth + 1, count, errorText))
                {
                    return false;
                }
            }
            return true;
        }

        PDFContentNode::Data data = node->getData();
        const bool isClippingPath = kind == Kind::Path && data.paint.isClippingPath();

        if (!isLeaf || isClippingPath)
        {
            if (isNamed)
            {
                *errorText = isAppearance ? PDFTranslationContext::tr("The appearance of an object of this kind cannot be changed.")
                                          : PDFTranslationContext::tr("The transparency of an object of this kind cannot be changed.");
                return false;
            }
            return true;
        }

        if (kind == Kind::TextObject)
        {
            if (data.hasClipEffect)
            {
                *errorText = PDFTranslationContext::tr("A text, which adds to the clipping path, cannot be restyled yet.");
                return false;
            }

            std::set<Property> keys;
            for (const auto& item : properties)
            {
                keys.insert(item.first);
            }
            if (!suppressOperations(container, node, keys, errorText))
            {
                return false;
            }
        }

        PDFContentStateOverride override = data.override ? *data.override : PDFContentStateOverride();
        for (const auto& item : properties)
        {
            override.properties[item.first] = item.second;
        }
        data.override = std::move(override);

        ++*count;
        return setOverride(container, std::move(data), errorText);
    }

    // ---- structure (zadani_jadro.md 4.12, the table of the consistency) ----

    /// The structure model (read in this thread, when the session does not have it yet)
    PDFContentStructureModelPointer getStructure()
    {
        if (!m_structure)
        {
            if (!m_session->getStructure())
            {
                m_session->loadStructure(nullptr);
            }
            m_structure = m_session->getStructure();
        }
        return m_structure;
    }

    /// The largest marked content identifier of a content, with those the command has given
    PDFInteger getLargestMarkedContentId(Container* container)
    {
        if (!container->largestMarkedContentId)
        {
            PDFInteger largest = -1;
            if (container->source->getRoot())
            {
                for (const PDFContentNodePointer& node : Helper::getSubtree(container->source->getRoot()))
                {
                    if (node->getData().mcid)
                    {
                        largest = qMax(largest, *node->getData().mcid);
                    }
                }
            }
            container->largestMarkedContentId = largest;
        }
        return *container->largestMarkedContentId;
    }

    /// Takes a change of the structure the model answered with
    bool addStructureChange(Container* container, const PDFContentStructureChange& change, QString* errorText)
    {
        switch (change.action)
        {
            case PDFContentStructureChange::Action::Refused:
                *errorText = !change.text.isEmpty() ? change.text : PDFTranslationContext::tr("The structure of the document does not allow the change.");
                return false;

            case PDFContentStructureChange::Action::SplitSequence:
                container->largestMarkedContentId = qMax(getLargestMarkedContentId(container), change.newMcid);
                m_structureChanges.push_back(change);
                break;

            case PDFContentStructureChange::Action::RemoveSequence:
                m_structureChanges.push_back(change);
                break;

            default:
                break;
        }
        return true;
    }

    /// Every sequence of a subtree is gone with it
    bool removeSequences(Container* container, const PDFContentNodePointer& node, QString* errorText)
    {
        for (const PDFContentNodePointer& item : Helper::getSubtree(node))
        {
            if (item->getKind() == Kind::MarkedContent && item->getData().mcid)
            {
                const PDFContentStructureModelPointer structure = getStructure();
                if (structure && !addStructureChange(container, structure->getChangeForDelete(container->source->getStructuralParentKey(), *item->getData().mcid, true), errorText))
                {
                    return false;
                }
            }
        }
        return true;
    }

    /// Removes the wrap of a marked content: its children (the state runs left in it) go to its
    /// place in its parent (R-3)
    bool dissolveMarkedContent(Container* container, PDFContentNodeId id, QString* errorText)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        const PDFContentNodeId parentId = node->getParentId();
        PDFInteger index = getIndex(container, id);

        std::vector<PDFContentNodeId> children;
        for (const PDFContentNodePointer& child : node->getChildren())
        {
            children.push_back(child->getId());
        }

        for (const PDFContentNodeId childId : children)
        {
            if (!moveNode(container, childId, parentId, index++, errorText))
            {
                return false;
            }
        }

        eraseNode(container, id);
        refreshLeaks(container, parentId);
        return true;
    }

    /// A node has left a sequence (it was deleted or moved out): a sequence without content
    /// loses its wrap and its place in the structure
    bool finishSequence(Container* container, PDFContentNodeId sequenceId, QString* errorText)
    {
        const PDFContentNodePointer sequence = container->editor->getNode(sequenceId);
        if (!sequence || !sequence->getData().mcid)
        {
            return true;
        }

        const bool isEmpty = !Helper::hasContent(*sequence);
        const PDFContentStructureModelPointer structure = getStructure();
        if (structure && !addStructureChange(container, structure->getChangeForDelete(container->source->getStructuralParentKey(), *sequence->getData().mcid, isEmpty), errorText))
        {
            return false;
        }

        return !isEmpty || dissolveMarkedContent(container, sequenceId, errorText);
    }

    /// A marked content like another one, without children: with another marked content
    /// identifier (a new property list), or with a copy of its operators
    bool insertMarkedContentLike(Container* container,
                                 const PDFContentNode::Data& original,
                                 std::optional<PDFInteger> mcid,
                                 PDFContentNodeId parentId,
                                 PDFInteger index,
                                 const State& state,
                                 PDFContentNodeId* newId,
                                 QString* errorText)
    {
        PDFContentOperationList& operations = container->editor->getOperations();

        PDFContentNode::Data data;
        data.kind = Kind::MarkedContent;
        data.id = m_session->allocateNodeId();
        data.state = state.resolved;
        data.symbolicState = state.symbolic;
        data.tag = original.tag;
        data.properties = original.properties;
        data.propertiesName = original.propertiesName;
        data.optionalContent = original.optionalContent;
        data.isArtifact = original.isArtifact;
        data.actualText = original.actualText;
        data.mcid = original.mcid;

        if (mcid)
        {
            // The property list with the new identifier: inline, when it has direct values only
            // (ISO 32000-2, 14.6.2), else a new named resource
            PDFObject properties = original.properties;
            if (properties.isReference() && m_session->getDocument())
            {
                properties = m_session->getDocument()->getObjectByReference(properties.getReference());
            }

            PDFDictionaryBuilder builder;
            if (properties.isDictionary())
            {
                builder = PDFDictionaryBuilder(*properties.getDictionary());
            }
            builder.setEntry(PDFInplaceOrMemoryString("MCID"), PDFObject::createInteger(*mcid));
            const PDFObject newProperties = PDFObject::createDictionary(std::move(builder));

            QByteArray bytes = PDFContentStreamText::formatName(original.tag);
            bytes.append(' ');

            QByteArray inlineText;
            std::vector<PDFContentResourceName> resourceNames;
            if (Helper::writeObject(newProperties, 0, &inlineText))
            {
                bytes.append(inlineText);
                data.propertiesName = QByteArray();
            }
            else
            {
                const QByteArray name = container->editor->getResources().addObject(PDFContentResourceCategory::Properties, "MC", newProperties);
                if (name.isEmpty())
                {
                    *errorText = PDFTranslationContext::tr("The property list of the tagged content cannot be written.");
                    return false;
                }
                bytes.append(PDFContentStreamText::formatName(name));
                data.propertiesName = name;
                resourceNames.push_back(PDFContentResourceName{ PDFContentResourceCategory::Properties, name });
            }
            bytes.append(" BDC");

            data.properties = newProperties;
            data.mcid = mcid;
            data.prefix = PDFContentOperationSpan{ appendOperation(container, Helper::createOperation(PDFContentOperator::MarkedContentBeginWithProperties, bytes, std::move(resourceNames))), 1 };
        }
        else
        {
            size_t first = 0;
            for (size_t i = 0; i < original.prefix.count; ++i)
            {
                const size_t newIndex = appendOperation(container, PDFContentOperation::createCopy(operations.getOperation(original.prefix.first + i)));
                first = i == 0 ? newIndex : first;
            }
            data.prefix = PDFContentOperationSpan{ first, original.prefix.count };
        }

        data.suffix = PDFContentOperationSpan{ appendOperation(container, Helper::createOperation(PDFContentOperator::MarkedContentEnd, "EMC")), 1 };

        *newId = data.id;
        return insertNode(container, std::move(data), parentId, index, errorText);
    }

    // ---- result ----

    struct Result
    {
        std::map<PDFContentContainerKey, std::pair<PDFContentContainerPointer, PDFContentContainerPointer>> containers;    ///< Before and after
        PDFContentStructureModelPointer structureBefore;
        PDFContentStructureModelPointer structureAfter;
        bool isStructureChanged = false;
        PDFContentChange change;
    };

    /// PDFContentCommand::MAXIMUM_NODES_OF_CONTAINER after the command: a container, which would
    /// have more nodes than the limit and more than before the command, refuses it. A container
    /// loaded above the limit can still be edited, as long as it does not grow.
    bool checkContainerLimits(QString* errorText) const
    {
        for (const auto& item : m_containers)
        {
            const Container* container = item.second.get();
            if (container->nodeCount > PDFContentCommand::MAXIMUM_NODES_OF_CONTAINER && container->nodeCount > container->sourceNodeCount)
            {
                *errorText = PDFTranslationContext::tr("The content would have more than %Ln object(s).", nullptr, int(PDFContentCommand::MAXIMUM_NODES_OF_CONTAINER));
                return false;
            }
        }
        return true;
    }

    /// Builds the new versions; the work must not be used afterwards
    Result finish()
    {
        Result result;

        for (auto& item : m_containers)
        {
            Container* container = item.second.get();
            if (!container->isTouched)
            {
                continue;
            }

            result.containers[item.first] = std::make_pair(container->source, container->editor->build());
            result.change.containers.push_back(item.first);
        }

        if (!m_structureChanges.empty() && m_structure)
        {
            result.structureBefore = m_structure;
            result.structureAfter = m_structure->applied(m_structureChanges);
            result.isStructureChanged = true;
            result.change.isStructureChanged = true;
        }

        // A node the command has added and removed again was never there
        for (const PDFContentNodeReference& reference : m_added)
        {
            if (!m_removed.count(reference))
            {
                result.change.addedNodes.push_back(reference);
            }
        }
        for (const PDFContentNodeReference& reference : m_removed)
        {
            if (!m_addedSet.count(reference))
            {
                result.change.removedNodes.push_back(reference);
            }
        }
        for (const PDFContentNodeReference& reference : m_changed)
        {
            if (!m_addedSet.count(reference) && !m_removed.count(reference))
            {
                result.change.changedNodes.push_back(reference);
            }
        }

        return result;
    }

private:
    PDFContentEditSession* m_session = nullptr;
    std::map<PDFContentContainerKey, std::unique_ptr<Container>> m_containers;
    PDFContentStructureModelPointer m_structure;
    std::vector<PDFContentStructureChange> m_structureChanges;
    std::vector<PDFContentNodeReference> m_added;       ///< In the order they were added (the objects first, see the commands)
    std::set<PDFContentNodeReference> m_addedSet;
    std::set<PDFContentNodeReference> m_removed;
    std::set<PDFContentNodeReference> m_changed;
    size_t m_createdCount = 0;                          ///< The nodes the command has created (PDFContentCommand::MAXIMUM_NODES_OF_COMMAND)
};

/// A command, which changes the containers and the structure model: it computes the new versions
/// once (compute) and keeps the versions before and after, so revert sets the old ones and an
/// apply after a revert the new ones again - the same objects, with the same identifiers.
///
/// Threads and ownership: as PDFContentCommand; it co-owns the versions of the containers and of
/// the structure model it keeps (shared pointers to immutable data).
class PDFContentModelCommand : public PDFContentCommand
{
public:
    using Helper = PDFContentCommandHelper;
    using Work = PDFContentCommandWork;
    using Kind = PDFContentNodeKind;
    using Property = PDFContentStateProperty;
    using State = PDFContentCommandState;

    virtual bool apply(PDFContentEditSession* session, QString* errorText) override final
    {
        // Told again by every apply: true only for the one refusal "nothing to change"
        setRefusedAsUnchanged(false);

        QString text;
        const bool result = applyImpl(session, &text);
        if (!result && errorText)
        {
            *errorText = text;
        }
        return result;
    }

    virtual void revert(PDFContentEditSession* session) override final
    {
        if (!session || !m_isComputed)
        {
            return;
        }

        for (const auto& item : m_containers)
        {
            session->setContainer(item.second.first);
        }
        if (m_isStructureChanged)
        {
            session->setStructure(m_structureBefore);
        }
        session->setNodeIdCounter(m_counterBefore);
        setChange(m_revertChange);
    }

    virtual PDFInteger getPageIndex() const override { return m_pageIndex; }

protected:
    /// Computes the change in the editors of \p work; false with the reason, when it is refused
    virtual bool compute(Work& work, QString* errorText) = 0;

    /// The objects a command is given: at least one, not more than the limit, each one once
    /// (the selection of the GUI is a set; in a script a repeated identifier is a mistake of its
    /// author, and "the same object twice" has no single meaning for a copy)
    /// \param emptyText The refusal of a command without an object
    static bool checkNodes(const std::vector<PDFContentNodeReference>& nodes, const QString& emptyText, QString* errorText)
    {
        if (nodes.empty())
        {
            *errorText = emptyText;
            return false;
        }

        if (nodes.size() > PDFContentCommand::MAXIMUM_NODES_OF_COMMAND)
        {
            *errorText = PDFTranslationContext::tr("The change names more than %Ln object(s).", nullptr, int(PDFContentCommand::MAXIMUM_NODES_OF_COMMAND));
            return false;
        }

        std::set<PDFContentNodeReference> distinct;
        for (const PDFContentNodeReference& reference : nodes)
        {
            if (!distinct.insert(reference).second)
            {
                *errorText = PDFTranslationContext::tr("The change names an object twice.");
                return false;
            }
        }

        return true;
    }

    /// The place of the command: the page of a page container; of a form, when its use is known
    void setPlace(const PDFContentEditSession* session, const PDFContentContainerKey& key)
    {
        if (m_paths.count(key))
        {
            return;
        }

        PDFInteger pageIndex = -1;
        std::vector<PDFContentNodeId> path;
        if (key.kind == PDFContentContainerKey::Kind::Page)
        {
            pageIndex = key.pageIndex;
        }
        else if (!session || !Helper::findPath(session, key, &pageIndex, &path))
        {
            return;
        }

        m_paths[key] = std::make_pair(pageIndex, std::move(path));
        if (m_pageIndex < 0)
        {
            m_pageIndex = pageIndex;
        }
    }

    /// The identifier of a node for a script (empty for a node of a form, whose use is not known)
    QString writeId(const PDFContentNodeReference& reference) const
    {
        PDFInteger pageIndex = -1;
        std::vector<PDFContentNodeId> path;

        auto it = m_paths.find(reference.container);
        if (it != m_paths.end())
        {
            pageIndex = it->second.first;
            path = it->second.second;
        }
        else if (reference.container.kind == PDFContentContainerKey::Kind::Page)
        {
            pageIndex = reference.container.pageIndex;
        }
        else
        {
            return QString();
        }

        path.push_back(reference.node);
        return PDFContentJson::writeNodeId(pageIndex, path);
    }

    QJsonArray writeIds(const std::vector<PDFContentNodeReference>& nodes) const
    {
        QJsonArray result;
        for (const PDFContentNodeReference& reference : nodes)
        {
            result.append(writeId(reference));
        }
        return result;
    }

    /// Takes the result of the following command over (mergeWith of a descendant): this command
    /// then leads from its own old versions to the new versions of \p next. Only for a command
    /// applied right after this one (its old versions are the new ones of this command) and
    /// within the merge interval.
    bool mergeResult(const PDFContentModelCommand& next)
    {
        if (!m_isComputed || !next.m_isComputed || m_isStructureChanged || next.m_isStructureChanged)
        {
            return false;
        }

        const qint64 elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(next.m_time - m_time).count();
        if (elapsed < 0 || elapsed > Helper::MERGE_INTERVAL)
        {
            return false;
        }

        if (m_containers.size() != next.m_containers.size() || m_counterAfter != next.m_counterBefore)
        {
            return false;
        }

        for (const auto& item : next.m_containers)
        {
            auto it = m_containers.find(item.first);
            if (it == m_containers.end() || it->second.second != item.second.first)
            {
                return false;
            }
        }

        for (const auto& item : next.m_containers)
        {
            m_containers[item.first].second = item.second.second;
        }
        m_counterAfter = next.m_counterAfter;
        m_time = next.m_time;

        // The state runs of the first wrap were added by this command; the next one adds none
        // to the same nodes, but it is merged in for safety
        for (PDFContentChange* change : { &m_applyChange, &m_revertChange })
        {
            const PDFContentChange& other = change == &m_applyChange ? next.m_applyChange : next.m_revertChange;
            change->addedNodes.insert(change->addedNodes.end(), other.addedNodes.cbegin(), other.addedNodes.cend());
            change->removedNodes.insert(change->removedNodes.end(), other.removedNodes.cbegin(), other.removedNodes.cend());
            for (const PDFContentNodeReference& reference : other.changedNodes)
            {
                if (std::find(change->changedNodes.cbegin(), change->changedNodes.cend(), reference) == change->changedNodes.cend())
                {
                    change->changedNodes.push_back(reference);
                }
            }
        }
        setChange(m_applyChange);
        return true;
    }

private:
    bool applyImpl(PDFContentEditSession* session, QString* errorText)
    {
        if (!session)
        {
            *errorText = PDFTranslationContext::tr("There is no session to change.");
            return false;
        }

        if (m_isComputed)
        {
            // Apply after revert: the model is in the state of the first apply again, so the
            // new versions are the ones computed then
            bool isSameModel = !m_isStructureChanged || session->getStructure() == m_structureBefore;
            for (const auto& item : m_containers)
            {
                isSameModel = isSameModel && session->getContainer(item.first) == item.second.first;
            }

            if (isSameModel)
            {
                for (const auto& item : m_containers)
                {
                    session->setContainer(item.second.second);
                }
                if (m_isStructureChanged)
                {
                    session->setStructure(m_structureAfter);
                }
                session->setNodeIdCounter(m_counterAfter);
                setChange(m_applyChange);
                return true;
            }

            // The command was executed already and the model is not the one it was applied to:
            // a second computation would replace the versions the undo stack still needs of the
            // first one (the same object would stand in two steps, and the first step could
            // never be undone). A command object is executed once.
            *errorText = PDFTranslationContext::tr("The change was applied already; it cannot be applied a second time.");
            return false;
        }

        const quint64 counterBefore = session->getNodeIdCounter();

        Work work(session);
        if (!compute(work, errorText) || !work.checkContainerLimits(errorText))
        {
            session->setNodeIdCounter(counterBefore);
            return false;
        }

        Work::Result result = work.finish();
        if (result.containers.empty() && !result.isStructureChanged)
        {
            // The one refusal a replay of a script takes for a success without an effect
            session->setNodeIdCounter(counterBefore);
            *errorText = PDFTranslationContext::tr("There is nothing to change.");
            setRefusedAsUnchanged(true);
            return false;
        }

        m_containers = std::move(result.containers);
        m_structureBefore = std::move(result.structureBefore);
        m_structureAfter = std::move(result.structureAfter);
        m_isStructureChanged = result.isStructureChanged;
        m_counterBefore = counterBefore;
        m_counterAfter = session->getNodeIdCounter();
        m_applyChange = std::move(result.change);

        m_revertChange = m_applyChange;
        std::swap(m_revertChange.addedNodes, m_revertChange.removedNodes);

        for (const auto& item : m_containers)
        {
            setPlace(session, item.first);
            session->setContainer(item.second.second);
        }
        if (m_isStructureChanged)
        {
            session->setStructure(m_structureAfter);
        }

        m_time = std::chrono::steady_clock::now();
        m_isComputed = true;
        setChange(m_applyChange);
        return true;
    }

    std::map<PDFContentContainerKey, std::pair<PDFContentContainerPointer, PDFContentContainerPointer>> m_containers;  ///< Before and after
    PDFContentStructureModelPointer m_structureBefore;
    PDFContentStructureModelPointer m_structureAfter;
    bool m_isStructureChanged = false;
    quint64 m_counterBefore = 0;
    quint64 m_counterAfter = 0;
    PDFContentChange m_applyChange;
    PDFContentChange m_revertChange;
    bool m_isComputed = false;
    std::chrono::steady_clock::time_point m_time;
    PDFInteger m_pageIndex = -1;
    std::map<PDFContentContainerKey, std::pair<PDFInteger, std::vector<PDFContentNodeId>>> m_paths;    ///< Container -> its page and the form uses leading to it
};

/// The keys of the operations of a script and the reading of their fields
class PDFContentCommandJson
{
public:
    PDFContentCommandJson() = delete;

    using Helper = PDFContentCommandHelper;

    static constexpr const char* KEY_IDS = "ids";
    static constexpr const char* KEY_MATRIX = "matrix";
    static constexpr const char* KEY_PARENT = "parent";
    static constexpr const char* KEY_INDEX = "index";
    static constexpr const char* KEY_MODE = "mode";
    static constexpr const char* KEY_PAGE = "page";
    static constexpr const char* KEY_FILL = "fill";
    static constexpr const char* KEY_STROKE = "stroke";
    static constexpr const char* KEY_LINE_WIDTH = "lineWidth";
    static constexpr const char* KEY_LINE_CAP = "lineCap";
    static constexpr const char* KEY_LINE_JOIN = "lineJoin";
    static constexpr const char* KEY_MITER_LIMIT = "miterLimit";
    static constexpr const char* KEY_DASH = "dash";
    static constexpr const char* KEY_DASH_ARRAY = "array";
    static constexpr const char* KEY_DASH_PHASE = "phase";
    static constexpr const char* KEY_STROKE_ALPHA = "strokeAlpha";
    static constexpr const char* KEY_FILL_ALPHA = "fillAlpha";
    static constexpr const char* KEY_BLEND_MODE = "blendMode";
    static constexpr const char* MODE_FOLLOW_PARENT = "follow_parent";
    static constexpr const char* MODE_KEEP_LAYER_AND_TAG = "keep_layer_and_tag";

    static void addError(std::vector<QString>* errors, const QString& path, const char* key, const QString& text)
    {
        if (errors)
        {
            errors->push_back(key ? QStringLiteral("%1.%2: %3").arg(path, QString::fromLatin1(key), text) : QStringLiteral("%1: %2").arg(path, text));
        }
    }

    /// Every key of an operation is known (the type and the temporary name are allowed everywhere)
    static bool checkKeys(const QJsonObject& object, std::initializer_list<const char*> keys, const QString& path, std::vector<QString>* errors)
    {
        bool result = true;
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        {
            const QString key = it.key();
            if (key == QLatin1String(PDFContentJson::KEY_TYPE) || key == QLatin1String(PDFContentJson::KEY_AS))
            {
                continue;
            }

            if (std::none_of(keys.begin(), keys.end(), [&key](const char* item) { return key == QLatin1String(item); }))
            {
                addError(errors, path, nullptr, PDFTranslationContext::tr("The operation has an unknown key '%1'.").arg(key));
                result = false;
            }
        }
        return result;
    }

    static bool readNode(const QJsonValue& value, const PDFContentEditSession* session, const QString& path, const char* key, std::vector<QString>* errors, PDFContentNodeReference* node)
    {
        if (!value.isString())
        {
            addError(errors, path, key, PDFTranslationContext::tr("An identifier of an object is expected."));
            return false;
        }

        QString errorText;
        *node = PDFContentJson::readNodeId(value.toString(), session, &errorText);
        if (!node->isValid())
        {
            addError(errors, path, key, !errorText.isEmpty() ? errorText : PDFTranslationContext::tr("The object does not exist."));
            return false;
        }
        return true;
    }

    /// The objects of an operation: "ids" (an array), or "id" (one object)
    static bool readNodes(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors, std::vector<PDFContentNodeReference>* nodes)
    {
        const bool hasId = object.contains(QLatin1String(PDFContentJson::KEY_ID));
        const bool hasIds = object.contains(QLatin1String(KEY_IDS));

        if (hasId == hasIds)
        {
            addError(errors, path, nullptr, PDFTranslationContext::tr("The operation needs exactly one of 'id' and 'ids'."));
            return false;
        }

        if (hasId)
        {
            PDFContentNodeReference node;
            if (!readNode(object.value(QLatin1String(PDFContentJson::KEY_ID)), session, path, PDFContentJson::KEY_ID, errors, &node))
            {
                return false;
            }
            nodes->push_back(node);
            return true;
        }

        const QJsonValue value = object.value(QLatin1String(KEY_IDS));
        const QJsonArray array = value.toArray();
        if (!value.isArray() || array.isEmpty())
        {
            addError(errors, path, KEY_IDS, PDFTranslationContext::tr("A list of identifiers of objects is expected."));
            return false;
        }

        if (size_t(array.size()) > PDFContentCommand::MAXIMUM_NODES_OF_COMMAND)
        {
            addError(errors, path, KEY_IDS, PDFTranslationContext::tr("The operation names more than %Ln object(s).", nullptr, int(PDFContentCommand::MAXIMUM_NODES_OF_COMMAND)));
            return false;
        }

        // Each object once: the error names the first repeat (the command refuses a repeat too,
        // without the place in the script)
        bool result = true;
        bool isRepeatReported = false;
        std::set<PDFContentNodeReference> distinct;
        for (qsizetype i = 0; i < array.size(); ++i)
        {
            PDFContentNodeReference node;
            const QByteArray key = QByteArray(KEY_IDS) + '[' + QByteArray::number(qlonglong(i)) + ']';
            if (!readNode(array[i], session, path, key.constData(), errors, &node))
            {
                result = false;
            }
            else if (!distinct.insert(node).second)
            {
                if (!isRepeatReported)
                {
                    addError(errors, path, key.constData(), PDFTranslationContext::tr("The object is named twice."));
                    isRepeatReported = true;
                }
                result = false;
            }
            else
            {
                nodes->push_back(node);
            }
        }
        return result;
    }

    static bool readMatrix(const QJsonObject& object, bool isRequired, const QString& path, std::vector<QString>* errors, QTransform* matrix)
    {
        if (!object.contains(QLatin1String(KEY_MATRIX)))
        {
            if (isRequired)
            {
                addError(errors, path, KEY_MATRIX, PDFTranslationContext::tr("A matrix of six numbers is expected."));
            }
            return !isRequired;
        }

        const std::optional<QTransform> value = PDFContentJson::readMatrix(object.value(QLatin1String(KEY_MATRIX)));
        if (!value)
        {
            addError(errors, path, KEY_MATRIX, PDFTranslationContext::tr("A matrix of six numbers is expected."));
            return false;
        }

        *matrix = *value;
        return true;
    }

    /// A number, which is an integer in a range
    static bool readInteger(const QJsonValue& value, PDFInteger minimum, PDFInteger maximum, PDFInteger* result)
    {
        const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!value.isDouble() || !std::isfinite(number) || number != std::floor(number) || number < double(minimum) || number > double(maximum))
        {
            return false;
        }

        *result = PDFInteger(number);
        return true;
    }

    static bool readReal(const QJsonValue& value, PDFReal* result)
    {
        const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!value.isDouble() || !std::isfinite(number))
        {
            return false;
        }

        *result = number;
        return true;
    }

    /// The place of the operations reorder and copy
    static bool readTarget(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors, PDFContentNodeReference* parent, PDFInteger* index)
    {
        bool result = readNode(object.value(QLatin1String(KEY_PARENT)), session, path, KEY_PARENT, errors, parent);

        *index = -1;
        if (object.contains(QLatin1String(KEY_INDEX)) &&
            !readInteger(object.value(QLatin1String(KEY_INDEX)), -1, PDFInteger(PDFContentContainer::MAXIMUM_OPERATION_COUNT), index))
        {
            addError(errors, path, KEY_INDEX, PDFTranslationContext::tr("An index among the children, or -1 for the end, is expected."));
            result = false;
        }
        return result;
    }
};

// ---------------------------------------------------------------------------------------------
//  Delete
// ---------------------------------------------------------------------------------------------

/// Removes nodes (R-3: a residual state run keeps their leak; 4.12: a sequence, which loses its
/// last content, loses its wrap and its place in the structure)
class PDFContentDeleteCommand : public PDFContentModelCommand
{
public:
    explicit PDFContentDeleteCommand(std::vector<PDFContentNodeReference> nodes) :
        m_nodes(std::move(nodes))
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Delete %Ln Object(s)", nullptr, int(m_nodes.size())); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_DELETE; }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_DELETE));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_IDS), writeIds(m_nodes));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        std::vector<PDFContentNodeReference> nodes;
        bool result = PDFContentCommandJson::checkKeys(object, { PDFContentJson::KEY_ID, PDFContentCommandJson::KEY_IDS }, path, errors);
        result = PDFContentCommandJson::readNodes(object, session, path, errors, &nodes) && result;
        return result ? PDFContentCommandFactory::createDelete(std::move(nodes)) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to delete."), errorText))
        {
            return false;
        }

        std::set<PDFContentNodeReference> selected(m_nodes.cbegin(), m_nodes.cend());
        std::set<PDFContentContainerKey> placed;

        // All the objects are checked, before one is removed
        for (const PDFContentNodeReference& reference : m_nodes)
        {
            Work::Container* container = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node)
            {
                return false;
            }

            if (placed.insert(reference.container).second)
            {
                setPlace(work.getSession(), reference.container);
            }

            switch (node->getKind())
            {
                case Kind::Root:
                case Kind::StateRun:
                case Kind::TextShow:
                    *errorText = PDFTranslationContext::tr("An object of this kind cannot be deleted.");
                    return false;

                case Kind::Opaque:
                    *errorText = PDFTranslationContext::tr("A part of the content, which cannot be read, cannot be deleted alone.");
                    return false;

                default:
                    break;
            }

            if (Work::isInSourceTextObject(container, reference.node))
            {
                *errorText = PDFTranslationContext::tr("A part of a text cannot be deleted yet.");
                return false;
            }
        }

        for (const PDFContentNodeReference& reference : selected)
        {
            Work::Container* container = work.getContainer(reference.container, errorText);
            const PDFContentNodePointer node = container ? container->editor->getNode(reference.node) : nullptr;
            if (!node)
            {
                // Deleted with a node above it
                continue;
            }

            const PDFContentNodeId parentId = node->getParentId();
            const PDFContentNodeId sequenceId = Work::getSequence(container, parentId);

            if (!work.removeSequences(container, node, errorText) ||
                !work.removeNode(container, reference.node, errorText))
            {
                return false;
            }

            work.refreshLeaks(container, parentId);

            if (sequenceId.isValid() && !work.finishSequence(container, sequenceId, errorText))
            {
                return false;
            }
        }

        return true;
    }

private:
    std::vector<PDFContentNodeReference> m_nodes;
};

// ---------------------------------------------------------------------------------------------
//  Transform
// ---------------------------------------------------------------------------------------------

/// Moves nodes by a matrix of the default space of their container (R-1, R-5, R-6)
class PDFContentTransformCommand : public PDFContentModelCommand
{
public:
    explicit PDFContentTransformCommand(std::vector<PDFContentNodeReference> nodes, const QTransform& matrix) :
        m_nodes(std::move(nodes)),
        m_matrix(matrix)
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Move %Ln Object(s)", nullptr, int(m_nodes.size())); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_TRANSFORM; }

    virtual bool mergeWith(const PDFContentCommand& next) override
    {
        // Moves by the arrow keys: the same objects again within the merge interval
        const PDFContentTransformCommand* other = dynamic_cast<const PDFContentTransformCommand*>(&next);
        if (!other || other->m_nodes != m_nodes || !mergeResult(*other))
        {
            return false;
        }

        m_matrix = m_matrix * other->m_matrix;
        return true;
    }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_TRANSFORM));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_IDS), writeIds(m_nodes));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_MATRIX), PDFContentJson::writeMatrix(m_matrix));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        std::vector<PDFContentNodeReference> nodes;
        QTransform matrix;
        bool result = PDFContentCommandJson::checkKeys(object, { PDFContentJson::KEY_ID, PDFContentCommandJson::KEY_IDS, PDFContentCommandJson::KEY_MATRIX }, path, errors);
        result = PDFContentCommandJson::readNodes(object, session, path, errors, &nodes) && result;
        result = PDFContentCommandJson::readMatrix(object, true, path, errors, &matrix) && result;
        return result ? PDFContentCommandFactory::createTransform(std::move(nodes), matrix) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to move."), errorText))
        {
            return false;
        }

        // A singular or infinite matrix is an error of the command (zadani_overeni.md 6.2)
        if (!Helper::isRegularMatrix(m_matrix))
        {
            *errorText = PDFTranslationContext::tr("The transformation is not a regular matrix of finite numbers.");
            return false;
        }

        // All the objects are checked in the tree as it is before the command, then they move:
        // the set of the named objects is made once, and each object walks up its tree once
        const std::set<PDFContentNodeReference> named(m_nodes.cbegin(), m_nodes.cend());
        std::set<PDFContentContainerKey> placed;
        std::vector<std::pair<Work::Container*, PDFContentNodeId>> moved;

        for (const PDFContentNodeReference& reference : m_nodes)
        {
            Work::Container* container = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node)
            {
                return false;
            }

            if (placed.insert(reference.container).second)
            {
                setPlace(work.getSession(), reference.container);
            }

            bool isCovered = false;
            for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, reference.node))
            {
                if (ancestor->getKind() == Kind::TextObject)
                {
                    *errorText = PDFTranslationContext::tr("A part of a text cannot be moved yet.");
                    return false;
                }

                // An object below another named one moves with it, once
                isCovered = isCovered || named.count(PDFContentNodeReference{ reference.container, ancestor->getId() }) > 0;
            }

            if (Work::isInUnclosedNode(container, reference.node))
            {
                *errorText = PDFTranslationContext::tr("The object is not closed in the content, or it is inside of such an object, so it cannot be moved.");
                return false;
            }

            if (!isCovered)
            {
                moved.emplace_back(container, reference.node);
            }
        }

        size_t count = 0;
        for (const auto& item : moved)
        {
            if (!work.transformNode(item.first, item.second, m_matrix, true, 0, &count, errorText))
            {
                return false;
            }
        }

        if (count == 0)
        {
            // The objects exist, but none of them holds anything, which moves (an empty group,
            // a marked content with a marked point only): the model is what the command asks
            // for already
            *errorText = PDFTranslationContext::tr("The objects hold nothing, which could be moved.");
            setRefusedAsUnchanged(true);
            return false;
        }

        return true;
    }

private:
    std::vector<PDFContentNodeReference> m_nodes;
    QTransform m_matrix;
};

// ---------------------------------------------------------------------------------------------
//  Set Appearance, Set Transparency
// ---------------------------------------------------------------------------------------------

/// Sets properties of the override of nodes (R-1: on leaves only): the appearance (colors and
/// line parameters) or the transparency (alphas and blend mode)
class PDFContentSetPropertiesCommand : public PDFContentModelCommand
{
public:
    explicit PDFContentSetPropertiesCommand(std::vector<PDFContentNodeReference> nodes, std::map<Property, PDFContentStateValue> properties, bool isAppearance) :
        m_nodes(std::move(nodes)),
        m_properties(std::move(properties)),
        m_isAppearance(isAppearance)
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override
    {
        return m_isAppearance ? PDFContentCommandFactory::tr("Change Appearance") : PDFContentCommandFactory::tr("Change Transparency");
    }

    virtual QByteArray getType() const override
    {
        return m_isAppearance ? PDFContentCommandFactory::TYPE_SET_APPEARANCE : PDFContentCommandFactory::TYPE_SET_TRANSPARENCY;
    }

    virtual bool mergeWith(const PDFContentCommand& next) override
    {
        // A dragged value: the same properties of the same objects again within the merge interval
        const PDFContentSetPropertiesCommand* other = dynamic_cast<const PDFContentSetPropertiesCommand*>(&next);
        if (!other || other->m_isAppearance != m_isAppearance || other->m_nodes != m_nodes || other->m_properties.size() != m_properties.size())
        {
            return false;
        }

        for (const auto& item : other->m_properties)
        {
            if (!m_properties.count(item.first))
            {
                return false;
            }
        }

        if (!mergeResult(*other))
        {
            return false;
        }

        m_properties = other->m_properties;
        return true;
    }

    virtual QJsonObject toJson() const override
    {
        using Json = PDFContentCommandJson;

        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(getType()));
        object.insert(QLatin1String(Json::KEY_IDS), writeIds(m_nodes));

        for (const auto& item : m_properties)
        {
            const PDFContentStateValue& value = item.second;
            switch (item.first)
            {
                case Property::FillColor:
                    object.insert(QLatin1String(Json::KEY_FILL), PDFContentJson::writeColor(value.color));
                    break;
                case Property::StrokeColor:
                    object.insert(QLatin1String(Json::KEY_STROKE), PDFContentJson::writeColor(value.color));
                    break;
                case Property::LineWidth:
                    object.insert(QLatin1String(Json::KEY_LINE_WIDTH), value.real);
                    break;
                case Property::LineCap:
                    object.insert(QLatin1String(Json::KEY_LINE_CAP), qint64(value.integer));
                    break;
                case Property::LineJoin:
                    object.insert(QLatin1String(Json::KEY_LINE_JOIN), qint64(value.integer));
                    break;
                case Property::MiterLimit:
                    object.insert(QLatin1String(Json::KEY_MITER_LIMIT), value.real);
                    break;
                case Property::DashPattern:
                {
                    QJsonArray array;
                    for (const PDFReal item2 : value.dash.getDashArray())
                    {
                        array.append(item2);
                    }
                    QJsonObject dash;
                    dash.insert(QLatin1String(Json::KEY_DASH_ARRAY), array);
                    dash.insert(QLatin1String(Json::KEY_DASH_PHASE), value.dash.getDashOffset());
                    object.insert(QLatin1String(Json::KEY_DASH), dash);
                    break;
                }
                case Property::AlphaStroking:
                    object.insert(QLatin1String(Json::KEY_STROKE_ALPHA), value.real);
                    break;
                case Property::AlphaFilling:
                    object.insert(QLatin1String(Json::KEY_FILL_ALPHA), value.real);
                    break;
                case Property::BlendMode:
                    object.insert(QLatin1String(Json::KEY_BLEND_MODE), QString::fromLatin1(value.name));
                    break;
                default:
                    break;
            }
        }
        return object;
    }

    static PDFContentCommandPointer appearanceFromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;
        using ValueKind = PDFContentStateValue::Kind;

        std::vector<PDFContentNodeReference> nodes;
        std::map<Property, PDFContentStateValue> properties;

        bool result = Json::checkKeys(object, { PDFContentJson::KEY_ID, Json::KEY_IDS, Json::KEY_FILL, Json::KEY_STROKE, Json::KEY_LINE_WIDTH, Json::KEY_LINE_CAP,
                                                Json::KEY_LINE_JOIN, Json::KEY_MITER_LIMIT, Json::KEY_DASH }, path, errors);
        result = Json::readNodes(object, session, path, errors, &nodes) && result;

        for (const auto& item : { std::make_pair(Json::KEY_FILL, Property::FillColor), std::make_pair(Json::KEY_STROKE, Property::StrokeColor) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                QString errorText;
                const std::optional<PDFContentColorValue> color = PDFContentJson::readColor(object.value(QLatin1String(item.first)), &errorText);
                if (color)
                {
                    PDFContentStateValue value;
                    value.kind = ValueKind::Color;
                    value.color = *color;
                    properties[item.second] = std::move(value);
                }
                else
                {
                    Json::addError(errors, path, item.first, !errorText.isEmpty() ? errorText : PDFTranslationContext::tr("A color is expected."));
                    result = false;
                }
            }
        }

        for (const auto& item : { std::make_pair(Json::KEY_LINE_WIDTH, Property::LineWidth), std::make_pair(Json::KEY_MITER_LIMIT, Property::MiterLimit) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                PDFContentStateValue value;
                value.kind = ValueKind::Real;
                if (Json::readReal(object.value(QLatin1String(item.first)), &value.real))
                {
                    properties[item.second] = std::move(value);
                }
                else
                {
                    Json::addError(errors, path, item.first, PDFTranslationContext::tr("A number is expected."));
                    result = false;
                }
            }
        }

        for (const auto& item : { std::make_pair(Json::KEY_LINE_CAP, Property::LineCap), std::make_pair(Json::KEY_LINE_JOIN, Property::LineJoin) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                PDFContentStateValue value;
                value.kind = ValueKind::Integer;
                if (Json::readInteger(object.value(QLatin1String(item.first)), 0, 2, &value.integer))
                {
                    properties[item.second] = std::move(value);
                }
                else
                {
                    Json::addError(errors, path, item.first, PDFTranslationContext::tr("One of the numbers 0, 1, 2 is expected."));
                    result = false;
                }
            }
        }

        if (object.contains(QLatin1String(Json::KEY_DASH)))
        {
            const QJsonValue dashValue = object.value(QLatin1String(Json::KEY_DASH));
            const QJsonObject dash = dashValue.toObject();
            const QJsonValue arrayValue = dash.value(QLatin1String(Json::KEY_DASH_ARRAY));

            std::vector<PDFReal> dashArray;
            PDFReal phase = 0.0;
            bool isDashValid = dashValue.isObject() && arrayValue.isArray() && arrayValue.toArray().size() <= MAXIMUM_DASH_COUNT;

            if (isDashValid)
            {
                for (const QJsonValue& number : arrayValue.toArray())
                {
                    PDFReal real = 0.0;
                    isDashValid = isDashValid && Json::readReal(number, &real);
                    dashArray.push_back(real);
                }

                for (auto it = dash.constBegin(); it != dash.constEnd(); ++it)
                {
                    isDashValid = isDashValid && (it.key() == QLatin1String(Json::KEY_DASH_ARRAY) || it.key() == QLatin1String(Json::KEY_DASH_PHASE));
                }

                if (dash.contains(QLatin1String(Json::KEY_DASH_PHASE)))
                {
                    isDashValid = isDashValid && Json::readReal(dash.value(QLatin1String(Json::KEY_DASH_PHASE)), &phase);
                }
            }

            if (isDashValid)
            {
                PDFContentStateValue value;
                value.kind = ValueKind::Dash;
                value.dash = PDFLineDashPattern(dashArray, phase);
                properties[Property::DashPattern] = std::move(value);
            }
            else
            {
                Json::addError(errors, path, Json::KEY_DASH, PDFTranslationContext::tr("A dash pattern { \"array\": [...], \"phase\": number } is expected."));
                result = false;
            }
        }

        return result ? PDFContentCommandFactory::createSetAppearance(std::move(nodes), std::move(properties)) : nullptr;
    }

    static PDFContentCommandPointer transparencyFromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        std::vector<PDFContentNodeReference> nodes;
        std::optional<PDFReal> alphaStroking;
        std::optional<PDFReal> alphaFilling;
        std::optional<QByteArray> blendMode;

        bool result = Json::checkKeys(object, { PDFContentJson::KEY_ID, Json::KEY_IDS, Json::KEY_STROKE_ALPHA, Json::KEY_FILL_ALPHA, Json::KEY_BLEND_MODE }, path, errors);
        result = Json::readNodes(object, session, path, errors, &nodes) && result;

        for (const auto& item : { std::make_pair(Json::KEY_STROKE_ALPHA, &alphaStroking), std::make_pair(Json::KEY_FILL_ALPHA, &alphaFilling) })
        {
            if (object.contains(QLatin1String(item.first)))
            {
                PDFReal real = 0.0;
                if (Json::readReal(object.value(QLatin1String(item.first)), &real))
                {
                    *item.second = real;
                }
                else
                {
                    Json::addError(errors, path, item.first, PDFTranslationContext::tr("A number from 0 to 1 is expected."));
                    result = false;
                }
            }
        }

        if (object.contains(QLatin1String(Json::KEY_BLEND_MODE)))
        {
            const QJsonValue value = object.value(QLatin1String(Json::KEY_BLEND_MODE));
            if (value.isString())
            {
                blendMode = value.toString().toLatin1();
            }
            else
            {
                Json::addError(errors, path, Json::KEY_BLEND_MODE, PDFTranslationContext::tr("The name of a blend mode is expected."));
                result = false;
            }
        }

        return result ? PDFContentCommandFactory::createSetTransparency(std::move(nodes), alphaStroking, alphaFilling, blendMode) : nullptr;
    }

protected:
    /// The values are checked against their tables (ISO 32000-2, 8.4.3 and Table 57)
    bool checkValues(QString* errorText) const
    {
        using ValueKind = PDFContentStateValue::Kind;

        if (m_properties.empty())
        {
            *errorText = PDFTranslationContext::tr("There is no property to set.");
            return false;
        }

        for (const auto& item : m_properties)
        {
            const PDFContentStateValue& value = item.second;
            bool isValid = false;

            switch (item.first)
            {
                case Property::FillColor:
                case Property::StrokeColor:
                {
                    // Stage E0 sets colors of the device spaces only (zadani_jadro.md 4.5): the
                    // number of the components of another space is known by its object alone
                    // (an ICC profile, a Separation), and a pattern needs its object - the
                    // commands of stage E4 set those and check them (ISO 32000-2, 8.6.8: the
                    // operands of scn are the components of the current space)
                    const size_t count = value.color.deviceSpace == "DeviceGray" ? 1 : value.color.deviceSpace == "DeviceRGB" ? 3 : value.color.deviceSpace == "DeviceCMYK" ? 4 : 0;
                    isValid = m_isAppearance && value.kind == ValueKind::Color && count > 0 && count == value.color.components.size() &&
                              value.color.colorSpaceObject.isNull() && !value.color.isPattern() &&
                              std::all_of(value.color.components.cbegin(), value.color.components.cend(), [](PDFReal component) { return std::isfinite(component) && component >= 0.0 && component <= 1.0; });
                    break;
                }

                case Property::LineWidth:
                    isValid = m_isAppearance && value.kind == ValueKind::Real && std::isfinite(value.real) && value.real >= 0.0;
                    break;

                case Property::MiterLimit:
                    isValid = m_isAppearance && value.kind == ValueKind::Real && std::isfinite(value.real) && value.real > 0.0;
                    break;

                case Property::LineCap:
                case Property::LineJoin:
                    isValid = m_isAppearance && value.kind == ValueKind::Integer && value.integer >= 0 && value.integer <= 2;
                    break;

                case Property::DashPattern:
                {
                    // 8.4.3.6: no negative number, not all of them zero
                    const std::vector<PDFReal>& dashArray = value.dash.getDashArray();
                    isValid = m_isAppearance && value.kind == ValueKind::Dash && dashArray.size() <= size_t(MAXIMUM_DASH_COUNT) && std::isfinite(value.dash.getDashOffset()) &&
                              std::all_of(dashArray.cbegin(), dashArray.cend(), [](PDFReal item2) { return std::isfinite(item2) && item2 >= 0.0; }) &&
                              (dashArray.empty() || std::any_of(dashArray.cbegin(), dashArray.cend(), [](PDFReal item2) { return item2 > 0.0; }));
                    break;
                }

                case Property::AlphaStroking:
                case Property::AlphaFilling:
                    isValid = !m_isAppearance && value.kind == ValueKind::Real && std::isfinite(value.real) && value.real >= 0.0 && value.real <= 1.0;
                    break;

                case Property::BlendMode:
                    isValid = !m_isAppearance && value.kind == ValueKind::Name && Helper::isBlendModeName(value.name);
                    break;

                default:
                    break;
            }

            if (!isValid)
            {
                *errorText = PDFTranslationContext::tr("A value of the change is not valid.");
                return false;
            }
        }

        return true;
    }

    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to change."), errorText))
        {
            return false;
        }

        if (!checkValues(errorText))
        {
            return false;
        }

        // As a transform: the checks in the tree before the command, then the changes
        const std::set<PDFContentNodeReference> named(m_nodes.cbegin(), m_nodes.cend());
        std::set<PDFContentContainerKey> placed;
        std::vector<std::pair<Work::Container*, PDFContentNodeId>> changed;

        for (const PDFContentNodeReference& reference : m_nodes)
        {
            Work::Container* container = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node)
            {
                return false;
            }

            if (placed.insert(reference.container).second)
            {
                setPlace(work.getSession(), reference.container);
            }

            bool isCovered = false;
            for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, reference.node))
            {
                if (ancestor->getKind() == Kind::TextObject)
                {
                    *errorText = PDFTranslationContext::tr("A part of a text cannot be restyled yet.");
                    return false;
                }

                // An object below another named one is changed with it, once
                isCovered = isCovered || named.count(PDFContentNodeReference{ reference.container, ancestor->getId() }) > 0;
            }

            if (!isCovered)
            {
                changed.emplace_back(container, reference.node);
            }
        }

        size_t count = 0;
        for (const auto& item : changed)
        {
            if (!work.setProperties(item.first, item.second, m_properties, m_isAppearance, true, 0, &count, errorText))
            {
                return false;
            }
        }

        if (count == 0)
        {
            // The objects exist, but none of them holds anything, which this command changes
            // (an empty group, a group of images for the appearance): nothing to change
            *errorText = PDFTranslationContext::tr("The objects hold nothing, which this change could change.");
            setRefusedAsUnchanged(true);
            return false;
        }

        return true;
    }

private:
    static constexpr qsizetype MAXIMUM_DASH_COUNT = 1024;

    std::vector<PDFContentNodeReference> m_nodes;
    std::map<Property, PDFContentStateValue> m_properties;
    bool m_isAppearance = true;
};

// ---------------------------------------------------------------------------------------------
//  Reorder, Copy, Group, Ungroup: the commands, which put nodes to another place (R-2)
// ---------------------------------------------------------------------------------------------

/// What the commands, which put nodes to a place of a tree, share
class PDFContentPlaceCommand : public PDFContentModelCommand
{
protected:
    /// The child of the parent new nodes go in front of; not valid: behind the last child.
    /// \param index Index among the children of the parent as they are before the command, -1 =
    ///        at the end (in front of the content, which must stay the last)
    /// \param moved The nodes the command takes away (the place skips them)
    static bool getAnchor(Work::Container* container,
                          PDFContentNodeId parentId,
                          PDFInteger index,
                          const std::set<PDFContentNodeId>& moved,
                          PDFContentNodeId* anchor,
                          QString* errorText)
    {
        *anchor = PDFContentNodeId();

        const PDFContentNodePointer parent = container->editor->getNode(parentId);
        const std::vector<PDFContentNodePointer>& children = parent->getChildren();
        const PDFContentNodeId pinnedId = Work::getPinnedChild(container, parentId);

        if (index < -1 || index > PDFInteger(children.size()))
        {
            *errorText = PDFTranslationContext::tr("The place of the object does not exist.");
            return false;
        }

        size_t position = index < 0 ? children.size() : size_t(index);
        for (; position < children.size(); ++position)
        {
            const PDFContentNodeId childId = children[position]->getId();
            if (childId == pinnedId || !moved.count(childId))
            {
                *anchor = childId;
                break;
            }
        }

        if (!anchor->isValid())
        {
            *anchor = pinnedId;
        }

        // The place in front of the first pinned child, when the index points behind it
        if (pinnedId.isValid() && anchor->isValid() && *anchor != pinnedId)
        {
            for (size_t i = 0; i < position && i < children.size(); ++i)
            {
                if (children[i]->getId() == pinnedId)
                {
                    *anchor = pinnedId;
                    break;
                }
            }
        }

        // The place behind a last child, which the content does not close (a q without its Q),
        // is a place as any other: the new version of the container gives that child its
        // closing operators, when something is written behind it
        // (PDFContentContainerEditor::build), and the state of the place is the state behind it.
        return true;
    }

    /// The index of the place in front of an anchor; -1 for an anchor, which is not there
    static PDFInteger getAnchorIndex(const Work::Container* container, PDFContentNodeId parentId, PDFContentNodeId anchor)
    {
        return anchor.isValid() ? Work::getIndex(container, anchor) : Work::getChildCount(container, parentId);
    }

    /// A node paints something itself (a state run sets a state, a container holds nodes)
    static bool isContentLeaf(const PDFContentNode& node)
    {
        switch (node.getKind())
        {
            case Kind::Root:
            case Kind::Group:
            case Kind::MarkedContent:
            case Kind::StateRun:
                return false;

            default:
                return true;
        }
    }

    /// Something of a subtree is painted behind a node of it (in the order of the content)
    /// \returns false also, when the node is not in the subtree
    static bool hasContentBehind(const PDFContentNodePointer& top, PDFContentNodeId id)
    {
        const std::vector<PDFContentNodePointer> nodes = Helper::getSubtree(top);
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            if (nodes[i]->getId() == id)
            {
                // The subtree of the node follows it in the list
                const size_t first = i + Helper::getSubtree(nodes[i]).size();
                return std::any_of(nodes.cbegin() + first, nodes.cend(), [](const PDFContentNodePointer& node) { return isContentLeaf(*node); });
            }
        }
        return false;
    }

    /// Something of a subtree is painted in front of a node of it
    /// \returns false also, when the node is not in the subtree
    static bool hasContentInFront(const PDFContentNodePointer& top, PDFContentNodeId id)
    {
        for (const PDFContentNodePointer& node : Helper::getSubtree(top))
        {
            if (node->getId() == id)
            {
                return false;
            }

            if (isContentLeaf(*node))
            {
                return true;
            }
        }
        return false;
    }

    /// The parent of new or moved objects: it must take children, outside of a text object
    static bool checkParent(Work::Container* container, const PDFContentNodePointer& parent, QString* errorText)
    {
        switch (parent->getKind())
        {
            case Kind::Root:
            case Kind::Group:
            case Kind::MarkedContent:
                break;

            default:
                *errorText = PDFTranslationContext::tr("Objects cannot be placed into an object of this kind.");
                return false;
        }

        if (Work::isInTextObject(container, parent->getId()))
        {
            *errorText = PDFTranslationContext::tr("Objects cannot be placed into a text.");
            return false;
        }

        return true;
    }

    /// A node, which a command may take to another place
    static bool checkMovable(Work::Container* container, const PDFContentNodePointer& node, QString* errorText)
    {
        if (!Helper::isObjectKind(node->getKind()))
        {
            *errorText = node->getKind() == Kind::Opaque ? PDFTranslationContext::tr("A part of the content, which cannot be read, cannot be moved.")
                                                         : PDFTranslationContext::tr("An object of this kind cannot be moved.");
            return false;
        }

        // The checks read the tree as it is before the command: a command checks all its
        // objects before it moves the first one
        if (Work::isInSourceTextObject(container, node->getId()))
        {
            *errorText = PDFTranslationContext::tr("A part of a text cannot be moved yet.");
            return false;
        }

        if (Work::isPinned(container, node->getId()))
        {
            *errorText = PDFTranslationContext::tr("The object is not closed in the content, so it cannot be moved.");
            return false;
        }

        if (Work::isInUnclosedNode(container, node->getId()))
        {
            *errorText = PDFTranslationContext::tr("The object is not closed in the content, or it is inside of such an object, so it cannot be moved.");
            return false;
        }

        for (const PDFContentNodePointer& item : Helper::getSubtree(node))
        {
            if (item->getKind() == Kind::Opaque)
            {
                *errorText = PDFTranslationContext::tr("The object has a part, which cannot be read, so it cannot be moved.");
                return false;
            }
        }

        return true;
    }

    /// Takes a node from its place to the place in front of an anchor of a parent of the same
    /// container: the residual state run stays at the old place (R-3), the node gets the state
    /// of the new place and the bake (R-2).
    /// \param topId The node standing at the new place (the node or its new group)
    static bool moveAndPlace(Work& work,
                             Work::Container* container,
                             PDFContentNodeId id,
                             PDFContentNodeId parentId,
                             PDFContentNodeId anchor,
                             PDFContentNodeId* topId,
                             QString* errorText)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        const PDFContentNodeId oldParentId = node->getParentId();
        const State source{ node->getState(), node->getSymbolicState() };

        // The leak stays at the old place
        if (Work::hasResidual(node->getLeak()) &&
            !work.insertResidual(container, oldParentId, Work::getIndex(container, id) + 1, source, node->getLeak(), errorText))
        {
            return false;
        }

        // An anchor, which is gone, is an error - never "the end of the parent" with the state
        // of its beginning
        PDFInteger index = getAnchorIndex(container, parentId, anchor);
        if (index < 0)
        {
            *errorText = PDFTranslationContext::tr("The place of the object does not exist any more.");
            return false;
        }

        const State target = Work::getStateAt(container, parentId, index);

        // The index of a move counts the children without the moved node
        if (oldParentId == parentId && Work::getIndex(container, id) < index)
        {
            --index;
        }

        if (!work.moveNode(container, id, parentId, index, errorText) ||
            !work.placeNode(container, id, source, target, container->isForm, true, container->editor->getOperations(), topId, errorText))
        {
            return false;
        }

        work.refreshLeaks(container, oldParentId);
        work.refreshLeaks(container, parentId);
        return true;
    }

    /// The node stands right in front of the anchor already
    static bool isAtPlace(const Work::Container* container, PDFContentNodeId id, PDFContentNodeId parentId, PDFContentNodeId anchor)
    {
        const PDFContentNodePointer node = container->editor->getNode(id);
        return node->getParentId() == parentId && Work::getIndex(container, id) + 1 == getAnchorIndex(container, parentId, anchor);
    }

    /// The node is painted at the place in front of the anchor already, though it does not
    /// stand there itself: it is the last content of the child right in front of the place, or
    /// (\p isInAnchorAllowed) the first content of the anchor. An order command has nothing to do
    /// with the node then.
    static bool isPaintedAtPlace(const Work::Container* container, PDFContentNodeId id, PDFContentNodeId parentId, PDFContentNodeId anchor, bool isInAnchorAllowed)
    {
        const PDFContentNodePointer parent = container->editor->getNode(parentId);
        const PDFInteger anchorIndex = getAnchorIndex(container, parentId, anchor);
        if (!parent || anchorIndex < 0)
        {
            return false;
        }

        // The child of the parent, which holds the node
        PDFContentNodeId holderId;
        {
            int depth = 0;
            PDFContentNodePointer node = container->editor->getNode(id);
            while (node && node->getParentId() != parentId && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                node = container->editor->getNode(node->getParentId());
            }

            if (!node || node->getParentId() != parentId || node->getId() == id)
            {
                return false;
            }
            holderId = node->getId();
        }

        const std::vector<PDFContentNodePointer>& children = parent->getChildren();
        if (anchorIndex > 0 && anchorIndex <= PDFInteger(children.size()) && children[size_t(anchorIndex - 1)]->getId() == holderId)
        {
            return !hasContentBehind(children[size_t(anchorIndex - 1)], id);
        }

        if (isInAnchorAllowed && anchorIndex < PDFInteger(children.size()) && children[size_t(anchorIndex)]->getId() == holderId)
        {
            return !hasContentInFront(children[size_t(anchorIndex)], id);
        }

        return false;
    }
};

/// Moves nodes of one container to another place of its tree (R-2; 4.12 for the tags)
class PDFContentReorderCommand : public PDFContentPlaceCommand
{
public:
    using ReorderMode = PDFContentCommandFactory::ReorderMode;

    explicit PDFContentReorderCommand(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, ReorderMode mode) :
        m_nodes(std::move(nodes)),
        m_parent(parent),
        m_index(index),
        m_mode(mode)
    {
        setPlace(nullptr, m_parent.container);
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Reorder %Ln Object(s)", nullptr, int(m_nodes.size())); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_REORDER; }

    virtual QJsonObject toJson() const override
    {
        using Json = PDFContentCommandJson;

        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_REORDER));
        object.insert(QLatin1String(Json::KEY_IDS), writeIds(m_nodes));
        object.insert(QLatin1String(Json::KEY_PARENT), writeId(m_parent));
        object.insert(QLatin1String(Json::KEY_INDEX), qint64(m_index));
        object.insert(QLatin1String(Json::KEY_MODE), QString::fromLatin1(m_mode == ReorderMode::KeepLayerAndTag ? Json::MODE_KEEP_LAYER_AND_TAG : Json::MODE_FOLLOW_PARENT));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        std::vector<PDFContentNodeReference> nodes;
        PDFContentNodeReference parent;
        PDFInteger index = -1;
        ReorderMode mode = ReorderMode::FollowParent;

        bool result = Json::checkKeys(object, { PDFContentJson::KEY_ID, Json::KEY_IDS, Json::KEY_PARENT, Json::KEY_INDEX, Json::KEY_MODE }, path, errors);
        result = Json::readNodes(object, session, path, errors, &nodes) && result;
        result = Json::readTarget(object, session, path, errors, &parent, &index) && result;

        if (object.contains(QLatin1String(Json::KEY_MODE)))
        {
            const QJsonValue value = object.value(QLatin1String(Json::KEY_MODE));
            if (value.isString() && value.toString() == QLatin1String(Json::MODE_KEEP_LAYER_AND_TAG))
            {
                mode = ReorderMode::KeepLayerAndTag;
            }
            else if (!value.isString() || value.toString() != QLatin1String(Json::MODE_FOLLOW_PARENT))
            {
                Json::addError(errors, path, Json::KEY_MODE, PDFTranslationContext::tr("One of 'follow_parent' and 'keep_layer_and_tag' is expected."));
                result = false;
            }
        }

        return result ? PDFContentCommandFactory::createReorder(std::move(nodes), parent, index, mode) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to move."), errorText))
        {
            return false;
        }

        Work::Container* container = nullptr;
        const PDFContentNodePointer parent = work.getNode(m_parent, &container, errorText);
        if (!parent || !checkParent(container, parent, errorText))
        {
            return false;
        }

        setPlace(work.getSession(), m_parent.container);

        // The new parent and the nodes above it: an object cannot go into itself
        std::set<PDFContentNodeId> parentChain = { m_parent.node };
        for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, m_parent.node))
        {
            parentChain.insert(ancestor->getId());
        }

        std::vector<PDFContentNodeId> nodes;
        std::set<PDFContentNodeId> moved;

        for (const PDFContentNodeReference& reference : m_nodes)
        {
            if (reference.container != m_parent.container)
            {
                *errorText = PDFTranslationContext::tr("The objects are not of the content of their new place.");
                return false;
            }

            Work::Container* nodeContainer = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &nodeContainer, errorText);
            if (!node || !checkMovable(container, node, errorText))
            {
                return false;
            }

            if (parentChain.count(reference.node))
            {
                *errorText = PDFTranslationContext::tr("An object cannot be placed into itself.");
                return false;
            }

            moved.insert(reference.node);
            nodes.push_back(reference.node);
        }

        // An object below another moved one stays inside of it: each object walks up once
        // against the set of the moved ones
        for (const PDFContentNodeId id : nodes)
        {
            for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, id))
            {
                if (moved.count(ancestor->getId()))
                {
                    *errorText = PDFTranslationContext::tr("An object and an object inside of it cannot be moved together.");
                    return false;
                }
            }
        }

        // The sequences of the structure do not nest (zadani_jadro.md 4.12)
        const PDFContentNodeId targetSequence = Work::getSequence(container, m_parent.node);

        PDFContentNodeId anchor;
        if (!getAnchor(container, m_parent.node, m_index, moved, &anchor, errorText))
        {
            return false;
        }

        // The nodes stand right in front of the place already, in the order of the command:
        // nothing moves (the command changes nothing then and is refused)
        {
            const PDFContentNodePointer currentParent = container->editor->getNode(m_parent.node);
            const std::vector<PDFContentNodePointer>& children = currentParent->getChildren();
            const size_t anchorIndex = size_t(getAnchorIndex(container, m_parent.node, anchor));

            bool isEveryNodeAtPlace = anchorIndex >= nodes.size() && anchorIndex <= children.size();
            for (size_t i = 0; isEveryNodeAtPlace && i < nodes.size(); ++i)
            {
                isEveryNodeAtPlace = children[anchorIndex - nodes.size() + i]->getId() == nodes[i];
            }

            if (isEveryNodeAtPlace)
            {
                return true;
            }
        }

        // An order command changes the painting order only: when the nodes are painted at the
        // place already, in the order of the command, through the groups and the marked
        // contents around them, it changes nothing - no sequence is split, no node leaves its
        // group (a check per node cannot see it: the first node is not at the place, while the
        // others are still in front of the place)
        if (m_mode == ReorderMode::KeepLayerAndTag && isPaintedInOrderAtPlace(container, m_parent.node, anchor, nodes, moved))
        {
            return true;
        }

        std::vector<PDFContentNodeId> emptied;

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const PDFContentNodeId id = nodes[i];
            const PDFContentNodePointer node = container->editor->getNode(id);
            const PDFContentNodeId oldParentId = node->getParentId();

            if (targetSequence.isValid() && !Work::isAncestorOrSelf(container, targetSequence, id))
            {
                for (const PDFContentNodePointer& item : Helper::getSubtree(node))
                {
                    if (item->getKind() == Kind::MarkedContent && item->getData().mcid)
                    {
                        *errorText = PDFTranslationContext::tr("Tagged content cannot be placed into other tagged content.");
                        return false;
                    }
                }
            }

            const bool isSuccess = m_mode == ReorderMode::KeepLayerAndTag ? moveWithWraps(work, container, id, i + 1 == nodes.size(), &anchor, errorText)
                                                                          : moveToParent(work, container, id, oldParentId, anchor, &emptied, errorText);
            if (!isSuccess)
            {
                return false;
            }
        }

        // The sequences, which lost their last content, lose their wraps now, when every object
        // stands at its place: a wrap may be the anchor of the place of the later objects
        for (const PDFContentNodeId sequenceId : emptied)
        {
            if (container->editor->getNode(sequenceId) && !work.dissolveMarkedContent(container, sequenceId, errorText))
            {
                return false;
            }
        }

        return true;
    }

private:
    /// The items of a subtree in the order they are painted, at the grain of an order command:
    /// a named node is one item (it moves as a whole), every other node, which paints, is one
    /// item; a state run and a container are none (their children are). A container deeper than
    /// the limit of the nesting is one item.
    static void collectPaintedItems(const PDFContentNodePointer& node, const std::set<PDFContentNodeId>& named, int depth, std::vector<PDFContentNodeId>* items)
    {
        if (named.count(node->getId()))
        {
            items->push_back(node->getId());
            return;
        }

        if (node->getKind() == Kind::StateRun)
        {
            return;
        }

        if (isContentLeaf(*node) || depth > PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            items->push_back(node->getId());
            return;
        }

        for (const PDFContentNodePointer& child : node->getChildren())
        {
            collectPaintedItems(child, named, depth + 1, items);
        }
    }

    /// The nodes are painted at the place in front of the anchor already, in the order of the
    /// command: the painting order of the subtree of the parent after the command (what is in
    /// front of the place without the nodes, the nodes, what is behind the place without the
    /// nodes) is the order it has. A node outside of the subtree of the parent is never at the
    /// place.
    static bool isPaintedInOrderAtPlace(const Work::Container* container,
                                        PDFContentNodeId parentId,
                                        PDFContentNodeId anchor,
                                        const std::vector<PDFContentNodeId>& nodes,
                                        const std::set<PDFContentNodeId>& named)
    {
        const PDFContentNodePointer parent = container->editor->getNode(parentId);
        const PDFInteger anchorIndex = getAnchorIndex(container, parentId, anchor);
        if (!parent || anchorIndex < 0 || anchorIndex > PDFInteger(parent->getChildren().size()))
        {
            return false;
        }

        // The items of the subtree, and the index of the place among them
        std::vector<PDFContentNodeId> items;
        size_t place = 0;
        const std::vector<PDFContentNodePointer>& children = parent->getChildren();
        for (size_t i = 0; i < children.size(); ++i)
        {
            if (PDFInteger(i) == anchorIndex)
            {
                place = items.size();
            }
            collectPaintedItems(children[i], named, 1, &items);
        }
        if (anchorIndex == PDFInteger(children.size()))
        {
            place = items.size();
        }

        const auto middle = items.cbegin() + std::ptrdiff_t(place);
        std::vector<PDFContentNodeId> after;
        after.reserve(items.size() + nodes.size());
        std::copy_if(items.cbegin(), middle, std::back_inserter(after), [&named](PDFContentNodeId id) { return !named.count(id); });
        after.insert(after.end(), nodes.cbegin(), nodes.cend());
        std::copy_if(middle, items.cend(), std::back_inserter(after), [&named](PDFContentNodeId id) { return !named.count(id); });
        return after == items;
    }

    /// A drag of a row of the tree: the node belongs to its new parent. It leaves its sequence
    /// (in the middle the sequence is split, the last content takes the wrap away).
    /// \param emptied The sequences without content, whose wraps the command removes at its end
    bool moveToParent(Work& work,
                      Work::Container* container,
                      PDFContentNodeId id,
                      PDFContentNodeId oldParentId,
                      PDFContentNodeId anchor,
                      std::vector<PDFContentNodeId>* emptied,
                      QString* errorText)
    {
        if (isAtPlace(container, id, m_parent.node, anchor))
        {
            return true;
        }

        const PDFInteger structuralParentKey = container->source->getStructuralParentKey();
        const PDFContentNodeId sequenceId = Work::getSequence(container, oldParentId);
        const bool isLeavingSequence = sequenceId.isValid() && !Work::isAncestorOrSelf(container, sequenceId, m_parent.node);

        PDFContentStructureChange change;
        PDFInteger splitIndex = -1;

        if (isLeavingSequence)
        {
            const PDFContentNodePointer sequence = container->editor->getNode(sequenceId);
            const std::vector<PDFContentNodePointer>& children = sequence->getChildren();

            // The child of the sequence, which is the node or holds it. A node deeper in the
            // sequence leaves what holds it there: the sequence stays one piece then.
            const bool isOtherContent = Helper::hasContentWithout(*sequence, id);
            bool isContentBefore = false;
            bool isContentBehind = false;
            bool isChildFound = false;
            for (size_t i = 0; i < children.size(); ++i)
            {
                if (children[i]->getId() == id)
                {
                    isChildFound = true;
                    splitIndex = PDFInteger(i);
                }
                else if (Work::isAncestorOrSelf(container, children[i]->getId(), id))
                {
                    isChildFound = true;
                }
                else if (Helper::hasContent(*children[i]))
                {
                    (isChildFound ? isContentBehind : isContentBefore) = true;
                }
            }

            const bool isInMiddle = splitIndex >= 0 && isContentBefore && isContentBehind;
            const PDFContentStructureModelPointer structure = work.getStructure();
            if (structure)
            {
                change = structure->getChangeForMoveOut(structuralParentKey, *sequence->getData().mcid, !isOtherContent, isInMiddle, work.getLargestMarkedContentId(container));
                if (!work.addStructureChange(container, change, errorText))
                {
                    return false;
                }
            }
        }

        PDFContentNodeId topId;
        if (!moveAndPlace(work, container, id, m_parent.node, anchor, &topId, errorText))
        {
            return false;
        }

        if (change.action == PDFContentStructureChange::Action::RemoveSequence)
        {
            emptied->push_back(sequenceId);
            return true;
        }

        if (change.action == PDFContentStructureChange::Action::SplitSequence && splitIndex >= 0)
        {
            PDFContentNodeId pieceId;
            return splitSequence(work, container, sequenceId, splitIndex, change.newMcid, &pieceId, errorText);
        }

        return true;
    }

    /// The rest of a sequence from a child on gets a wrap of its own with a new identifier,
    /// right behind the sequence
    /// \param pieceId The new wrap
    static bool splitSequence(Work& work, Work::Container* container, PDFContentNodeId sequenceId, PDFInteger firstIndex, PDFInteger mcid, PDFContentNodeId* pieceId, QString* errorText)
    {
        const PDFContentNodePointer sequence = container->editor->getNode(sequenceId);
        const PDFContentNodeId parentId = sequence->getParentId();

        std::vector<PDFContentNodeId> rest;
        for (size_t i = size_t(firstIndex); i < sequence->getChildren().size(); ++i)
        {
            rest.push_back(sequence->getChildren()[i]->getId());
        }

        const State state = Work::getStateAt(container, sequenceId, firstIndex);

        PDFContentNodeId newId;
        if (!work.insertMarkedContentLike(container, sequence->getData(), mcid, parentId, Work::getIndex(container, sequenceId) + 1, state, &newId, errorText))
        {
            return false;
        }

        *pieceId = newId;

        for (const PDFContentNodeId childId : rest)
        {
            if (!work.moveNode(container, childId, newId, -1, errorText))
            {
                return false;
            }
        }

        // The leaks of the two halves; the new one first, the walk of the old one goes on upwards
        work.refreshLeaks(container, newId);
        work.refreshLeaks(container, sequenceId);
        return true;
    }

    /// The node is the only content of a parent (a state run and an empty container are none)
    static bool isOnlyContent(const PDFContentNodePointer& parent, PDFContentNodeId id)
    {
        return std::none_of(parent->getChildren().cbegin(), parent->getChildren().cend(),
                            [id](const PDFContentNodePointer& child) { return child->getId() != id && Helper::hasContent(*child); });
    }

    /// Makes a child of a sequence of the structure its only content, so that it can move with
    /// the wrap and keep its tag: the sequence is split in front of the child and behind it.
    /// A piece, which is cut off, gets a new identifier of the same element, and its child of
    /// the element stands right behind the child of the piece it was cut from. So the children
    /// of the element keep the order of the content - the reading order never changes by an
    /// order command (zadani_jadro.md 4.12; ISO 32000-2, 14.7.2: the order of /K is the logical
    /// order). For "A B C" with B: [m: A] [m1: B] [m2: C] and /K m, m1, m2. The first piece
    /// keeps the identifier and the wrap of the sequence.
    /// \param sequenceId The sequence; on return the piece, whose only content the child is
    static bool isolateInSequence(Work& work, Work::Container* container, PDFContentNodeId* sequenceId, PDFContentNodeId id, QString* errorText)
    {
        const PDFInteger structuralParentKey = container->source->getStructuralParentKey();
        const PDFContentStructureModelPointer structure = work.getStructure();
        if (!structure)
        {
            *errorText = PDFTranslationContext::tr("The structure of the document cannot be read, so the tag of the object cannot be kept.");
            return false;
        }

        // First the cut in front of the child, then the cut behind it: the identifiers grow in
        // the order of the content, and each new child of the element goes behind the last one
        for (const bool isCutInFront : { true, false })
        {
            const PDFContentNodePointer sequence = container->editor->getNode(*sequenceId);
            const std::vector<PDFContentNodePointer>& children = sequence->getChildren();

            PDFInteger index = -1;
            bool isContentInFront = false;
            bool isContentBehind = false;
            for (size_t i = 0; i < children.size(); ++i)
            {
                if (children[i]->getId() == id)
                {
                    index = PDFInteger(i);
                }
                else if (Helper::hasContent(*children[i]))
                {
                    (index < 0 ? isContentInFront : isContentBehind) = true;
                }
            }

            if (index < 0 || !sequence->getData().mcid)
            {
                *errorText = PDFTranslationContext::tr("The object is not in the tagged content it should be taken from.");
                return false;
            }

            if (!(isCutInFront ? isContentInFront : isContentBehind))
            {
                continue;
            }

            const PDFContentStructureChange change = structure->getChangeForReorder(structuralParentKey, *sequence->getData().mcid, false, work.getLargestMarkedContentId(container));
            if (!work.addStructureChange(container, change, errorText))
            {
                return false;
            }

            if (change.action != PDFContentStructureChange::Action::SplitSequence)
            {
                *errorText = PDFTranslationContext::tr("The structure of the document does not allow the change.");
                return false;
            }

            PDFContentNodeId pieceId;
            if (!splitSequence(work, container, *sequenceId, isCutInFront ? index : index + 1, change.newMcid, &pieceId, errorText))
            {
                return false;
            }

            if (isCutInFront)
            {
                // The child is the first content of the new piece now
                *sequenceId = pieceId;
            }
        }

        return true;
    }

    /// An order command: the layer and the tag stay, and the reading order of the tagged content
    /// with them. The node moves with the groups and the marked contents around it, whose only
    /// content it is (a sequence of the structure is split first, so that the node is its only
    /// content: isolateInSequence); of the other marked contents it gets a new wrap at its new
    /// place.
    /// \param isLast The node is the last one of the command: no node is placed in front of the
    ///        anchor after it
    /// \param anchor The place: the nodes go in front of it. A node, which stands at the place as
    ///        the first content of the anchor, gives the place behind itself to the nodes after it.
    bool moveWithWraps(Work& work, Work::Container* container, PDFContentNodeId id, bool isLast, PDFContentNodeId* anchor, QString* errorText)
    {
        const PDFInteger structuralParentKey = container->source->getStructuralParentKey();

        // The node is painted at the place already: nothing moves. As the first content of the
        // anchor only, when it is the last node: a later node would go in front of the anchor,
        // so in front of this one, against the order of the command.
        if (isAtPlace(container, id, m_parent.node, *anchor) || isPaintedAtPlace(container, id, m_parent.node, *anchor, isLast))
        {
            return true;
        }

        // The new parent and the nodes above it, as the tree is now
        std::set<PDFContentNodeId> parentChain;
        {
            int depth = 0;
            PDFContentNodePointer node = container->editor->getNode(m_parent.node);
            while (node && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                parentChain.insert(node->getId());
                node = container->editor->getNode(node->getParentId());
            }
        }

        // The groups and the marked contents above the node, which the node leaves (the nearest
        // first), up to the outermost marked content: a group above that one carries neither
        // a layer nor a tag. A group between the node and its marked content is on the way to
        // it (q BT ... ET Q inside of a tagged paragraph is the common case).
        std::vector<PDFContentNodeId> wraps;
        {
            int depth = 0;
            size_t count = 0;
            const PDFContentNodePointer node = container->editor->getNode(id);
            PDFContentNodePointer wrap = container->editor->getNode(node->getParentId());
            while (wrap && (wrap->getKind() == Kind::MarkedContent || wrap->getKind() == Kind::Group) &&
                   depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2 && !parentChain.count(wrap->getId()))
            {
                wraps.push_back(wrap->getId());
                if (wrap->getKind() == Kind::MarkedContent)
                {
                    count = wraps.size();
                }
                wrap = container->editor->getNode(wrap->getParentId());
            }
            wraps.resize(count);
        }

        // A tag cannot be kept inside of another sequence of the structure: the sequences do
        // not nest (zadani_jadro.md 4.12)
        if (Work::getSequence(container, m_parent.node).isValid())
        {
            for (const PDFContentNodeId wrapId : wraps)
            {
                if (container->editor->getNode(wrapId)->getData().mcid)
                {
                    *errorText = PDFTranslationContext::tr("The tag of the object cannot be kept there: tagged content cannot be placed into other tagged content.");
                    return false;
                }
            }
        }

        // The wraps, which go with the node: it is their only content. A sequence of the
        // structure with other content is split around the node first.
        PDFContentNodeId movedId = id;
        size_t wrapIndex = 0;
        for (; wrapIndex < wraps.size(); ++wrapIndex)
        {
            PDFContentNodeId wrapId = wraps[wrapIndex];
            const PDFContentNodePointer wrap = container->editor->getNode(wrapId);
            const bool isSequence = wrap->getKind() == Kind::MarkedContent && wrap->getData().mcid.has_value();

            if (!isOnlyContent(wrap, movedId))
            {
                if (!isSequence)
                {
                    break;
                }

                if (!isolateInSequence(work, container, &wrapId, movedId, errorText))
                {
                    return false;
                }
            }
            else if (isSequence)
            {
                // The sequence moves as it is: its identifier stays
                const PDFContentStructureModelPointer structure = work.getStructure();
                if (structure && !work.addStructureChange(container, structure->getChangeForReorder(structuralParentKey, *wrap->getData().mcid, true, work.getLargestMarkedContentId(container)), errorText))
                {
                    return false;
                }
            }

            movedId = wrapId;
        }

        if (isAtPlace(container, movedId, m_parent.node, *anchor))
        {
            return true;
        }

        if (movedId == *anchor)
        {
            // The node with its wraps is the anchor itself (it was the first content of the
            // anchor and is cut off from the rest of its sequence now): it stands at the place,
            // and the nodes after it go behind it
            const PDFContentNodePointer parent = container->editor->getNode(m_parent.node);
            const size_t next = size_t(Work::getIndex(container, movedId) + 1);
            *anchor = next < parent->getChildren().size() ? parent->getChildren()[next]->getId() : PDFContentNodeId();
            return true;
        }

        // The data of the marked contents left behind, before anything moves: the node gets
        // a wrap like each of them at its new place. A group left behind needs none: the state
        // it gave the node is in the bake (R-2).
        std::vector<std::pair<PDFContentNode::Data, std::optional<PDFInteger>>> newWraps;
        for (; wrapIndex < wraps.size(); ++wrapIndex)
        {
            const PDFContentNodePointer wrap = container->editor->getNode(wraps[wrapIndex]);
            if (wrap->getKind() != Kind::MarkedContent)
            {
                continue;
            }

            std::optional<PDFInteger> mcid;

            if (wrap->getData().mcid)
            {
                // The node stands deeper in the sequence, in a group or a marked content with
                // other content, so the sequence cannot be split around it. The node gets a new
                // sequence of the same element, whose child stands behind the child of the old
                // one: that is the reading order only when nothing of the old sequence is read
                // behind the node.
                if (hasContentBehind(wrap, movedId))
                {
                    *errorText = PDFTranslationContext::tr("The object stands in a group inside of tagged content, in front of other content of the tag: its order cannot be changed without changing the reading order yet.");
                    return false;
                }

                const PDFContentStructureModelPointer structure = work.getStructure();
                if (!structure)
                {
                    *errorText = PDFTranslationContext::tr("The structure of the document cannot be read, so the tag of the object cannot be kept.");
                    return false;
                }

                const PDFContentStructureChange change = structure->getChangeForReorder(structuralParentKey, *wrap->getData().mcid, false, work.getLargestMarkedContentId(container));
                if (!work.addStructureChange(container, change, errorText))
                {
                    return false;
                }

                if (change.action != PDFContentStructureChange::Action::SplitSequence)
                {
                    *errorText = PDFTranslationContext::tr("The structure of the document does not allow the change.");
                    return false;
                }
                mcid = change.newMcid;
            }

            newWraps.emplace_back(wrap->getData(), mcid);
        }

        PDFContentNodeId topId;
        if (!moveAndPlace(work, container, movedId, m_parent.node, *anchor, &topId, errorText))
        {
            return false;
        }

        for (const auto& item : newWraps)
        {
            const PDFInteger index = Work::getIndex(container, topId);
            const State state = Work::getStateAt(container, m_parent.node, index);

            PDFContentNodeId wrapId;
            if (!work.insertMarkedContentLike(container, item.first, item.second, m_parent.node, index, state, &wrapId, errorText) ||
                !work.moveNode(container, topId, wrapId, -1, errorText))
            {
                return false;
            }

            work.refreshLeaks(container, wrapId);
            topId = wrapId;
        }

        return true;
    }

    std::vector<PDFContentNodeReference> m_nodes;
    PDFContentNodeReference m_parent;
    PDFInteger m_index = -1;
    ReorderMode m_mode = ReorderMode::FollowParent;
};

/// Copies subtrees, also to another container (R-2, R-4, R-5; the copy has no structure tags)
class PDFContentCopyCommand : public PDFContentPlaceCommand
{
public:
    explicit PDFContentCopyCommand(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, const QTransform& matrix) :
        m_nodes(std::move(nodes)),
        m_parent(parent),
        m_index(index),
        m_matrix(matrix)
    {
        setPlace(nullptr, m_parent.container);
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Copy %Ln Object(s)", nullptr, int(m_nodes.size())); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_COPY; }

    virtual QJsonObject toJson() const override
    {
        using Json = PDFContentCommandJson;

        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_COPY));
        object.insert(QLatin1String(Json::KEY_IDS), writeIds(m_nodes));
        object.insert(QLatin1String(Json::KEY_PARENT), writeId(m_parent));
        object.insert(QLatin1String(Json::KEY_INDEX), qint64(m_index));
        object.insert(QLatin1String(Json::KEY_MATRIX), PDFContentJson::writeMatrix(m_matrix));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        std::vector<PDFContentNodeReference> nodes;
        PDFContentNodeReference parent;
        PDFInteger index = -1;
        QTransform matrix;

        bool result = Json::checkKeys(object, { PDFContentJson::KEY_ID, Json::KEY_IDS, Json::KEY_PARENT, Json::KEY_INDEX, Json::KEY_MATRIX }, path, errors);
        result = Json::readNodes(object, session, path, errors, &nodes) && result;
        result = Json::readTarget(object, session, path, errors, &parent, &index) && result;
        result = Json::readMatrix(object, false, path, errors, &matrix) && result;
        return result ? PDFContentCommandFactory::createCopy(std::move(nodes), parent, index, matrix) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to copy."), errorText))
        {
            return false;
        }

        if (!Helper::isRegularMatrix(m_matrix))
        {
            *errorText = PDFTranslationContext::tr("The transformation is not a regular matrix of finite numbers.");
            return false;
        }

        Work::Container* target = nullptr;
        const PDFContentNodePointer parent = work.getNode(m_parent, &target, errorText);
        if (!parent || !checkParent(target, parent, errorText))
        {
            return false;
        }

        // The page of the command is the page of the new place; the source needs its place too
        // (the identifiers of the nodes of a form are written with the path of its uses)
        setPlace(work.getSession(), m_parent.container);
        setPlace(work.getSession(), m_nodes.front().container);

        // A copy has no tag (zadani_jadro.md 4.12: "the copy is without an MCID"). Inside of
        // a sequence of the structure it would be content of that sequence - the element would
        // read it twice. The plan does not say, whether the sequence is to be split around the
        // copy or the place refused: it is refused, the copy goes beside the tagged content.
        if (Work::getSequence(target, m_parent.node).isValid())
        {
            *errorText = PDFTranslationContext::tr("A copy cannot be placed into tagged content: it would become a part of the tag. Place it beside the tagged content.");
            return false;
        }

        // The source is read as it was before the command: the copies never copy each other
        const PDFContentContainerPointer source = work.getSession()->getContainer(m_nodes.front().container);
        if (!source)
        {
            *errorText = PDFTranslationContext::tr("The content of the object is not loaded.");
            return false;
        }

        const bool isSameContainer = source->getKey() == target->key;
        const bool isSourceForm = source->getKey().kind != PDFContentContainerKey::Kind::Page;

        std::vector<PDFContentNodePointer> nodes;
        for (const PDFContentNodeReference& reference : m_nodes)
        {
            if (reference.container != source->getKey())
            {
                *errorText = PDFTranslationContext::tr("The objects to copy are not of one content.");
                return false;
            }

            const PDFContentNodePointer node = source->getNode(reference.node);
            if (!node)
            {
                *errorText = PDFTranslationContext::tr("The object does not exist.");
                return false;
            }

            if (!Helper::isObjectKind(node->getKind()) || isInSourceText(*source, *node))
            {
                *errorText = PDFTranslationContext::tr("An object of this kind cannot be copied.");
                return false;
            }

            // A form or the cell of a pattern never paints itself, also not through other forms
            // and patterns
            if (Helper::isPaintingContainer(work.getSession(), *source, node, target->key))
            {
                *errorText = target->key.kind == PDFContentContainerKey::Kind::Form ? PDFTranslationContext::tr("The copy cannot be placed into this form: the form would draw itself.")
                                                                                    : PDFTranslationContext::tr("The copy cannot be placed into this pattern: the pattern would draw itself.");
                return false;
            }

            nodes.push_back(node);
        }

        PDFContentNodeId anchor;
        if (!getAnchor(target, m_parent.node, m_index, { }, &anchor, errorText))
        {
            return false;
        }

        // The layers of the new place: the marked contents of optional content around it
        std::set<PDFContentNodeId> targetWraps;
        std::set<PDFObjectReference> targetLayers;
        {
            int depth = 0;
            PDFContentNodePointer wrap = target->editor->getNode(m_parent.node);
            while (wrap && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                if (isLayerWrap(*wrap))
                {
                    targetWraps.insert(wrap->getId());
                    if (wrap->getData().optionalContent.isValid())
                    {
                        targetLayers.insert(wrap->getData().optionalContent);
                    }
                }
                wrap = target->editor->getNode(wrap->getParentId());
            }
        }

        size_t count = 0;
        for (const PDFContentNodePointer& node : nodes)
        {
            // The copy keeps its layer (zadani_jadro.md 4.12: inside of the same document, and
            // a session has one document): the marked contents of optional content around the
            // source, the nearest first, without those the new place is inside of already
            std::vector<PDFContentNodePointer> layers;
            {
                int depth = 0;
                PDFContentNodePointer wrap = source->getParent(node->getId());
                while (wrap && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
                {
                    const bool isAroundPlace = (isSameContainer && targetWraps.count(wrap->getId())) ||
                                               (wrap->getData().optionalContent.isValid() && targetLayers.count(wrap->getData().optionalContent));
                    if (isLayerWrap(*wrap) && !isAroundPlace)
                    {
                        layers.push_back(wrap);
                    }
                    wrap = source->getParent(wrap->getId());
                }
            }

            // The objects to place: the node, or its content, when it is a sequence of the
            // structure (the copy has no tag; the state runs of the lost wrap are in the bake)
            std::vector<PDFContentNodePointer> tops;
            if (node->getKind() == Kind::MarkedContent && node->getData().mcid)
            {
                for (const PDFContentNodePointer& child : node->getChildren())
                {
                    if (child->getKind() != Kind::StateRun)
                    {
                        tops.push_back(child);
                    }
                }
            }
            else
            {
                tops.push_back(node);
            }

            for (const PDFContentNodePointer& top : tops)
            {
                if (!Helper::isObjectKind(top->getKind()))
                {
                    *errorText = PDFTranslationContext::tr("The object has a part, which cannot be read, so it cannot be copied.");
                    return false;
                }

                const PDFInteger index = getAnchorIndex(target, m_parent.node, anchor);
                if (index < 0)
                {
                    *errorText = PDFTranslationContext::tr("The place of the object does not exist any more.");
                    return false;
                }

                const State targetState = Work::getStateAt(target, m_parent.node, index);

                std::vector<PDFContentNodeId> created;
                if (!copyNode(work, *source, target, top, m_parent.node, index, isSameContainer, 0, &created, errorText))
                {
                    return false;
                }

                // A sequence inside gives its content instead of itself: every piece is placed
                for (const PDFContentNodeId createdId : created)
                {
                    const PDFContentNodePointer copy = target->editor->getNode(createdId);
                    const State sourceState{ copy->getState(), copy->getSymbolicState() };

                    PDFContentNodeId topId;
                    if (!work.placeNode(target, createdId, sourceState, targetState, isSourceForm, isSameContainer, source->getOperations(), &topId, errorText))
                    {
                        return false;
                    }

                    // The move of the copy in the space of the target container. Below its top
                    // the copy has the states of its source, which name the operations of the
                    // source container.
                    if (!m_matrix.isIdentity() &&
                        !work.transformNode(target, createdId, m_matrix, false, 0, &count, errorText, isSameContainer ? nullptr : &source->getOperations()))
                    {
                        return false;
                    }

                    for (const PDFContentNodePointer& layer : layers)
                    {
                        if (!wrapInLayer(work, *source, target, *layer, m_parent.node, isSameContainer, &topId, errorText))
                        {
                            return false;
                        }
                    }
                }
            }
        }

        work.refreshLeaks(target, m_parent.node);
        return true;
    }

private:
    /// A marked content, which puts its content into a layer: /OC with the name of an optional
    /// content group or membership dictionary of /Properties (ISO 32000-2, 8.11.3.2)
    static bool isLayerWrap(const PDFContentNode& node)
    {
        return node.getKind() == Kind::MarkedContent && node.getData().tag == "OC";
    }

    /// Puts a copy, which stands at its place, into a copy of a marked content of optional
    /// content of its source: BDC with the same property list (R-4 names it in the target
    /// container) and a new EMC
    /// \param topId The node standing at the place: the copy, then the new wrap
    static bool wrapInLayer(Work& work,
                            const PDFContentContainer& source,
                            Work::Container* target,
                            const PDFContentNode& layer,
                            PDFContentNodeId parentId,
                            bool isSameContainer,
                            PDFContentNodeId* topId,
                            QString* errorText)
    {
        const PDFInteger index = Work::getIndex(target, *topId);
        if (index < 0)
        {
            *errorText = PDFTranslationContext::tr("The place of the object does not exist any more.");
            return false;
        }

        const State state = Work::getStateAt(target, parentId, index);

        PDFContentNode::Data data;
        data.kind = Kind::MarkedContent;
        data.id = work.getSession()->allocateNodeId();
        data.state = state.resolved;
        data.symbolicState = state.symbolic;
        data.tag = layer.getData().tag;
        data.properties = layer.getData().properties;
        data.propertiesName = layer.getData().propertiesName;
        data.optionalContent = layer.getData().optionalContent;

        if (!copyOperations(work, source, target, layer.getPrefix(), isSameContainer, &data.prefix, errorText))
        {
            return false;
        }

        // The name of the property list in the target container
        if (!data.prefix.isEmpty())
        {
            for (const PDFContentResourceName& resourceName : target->editor->getOperations().getOperation(data.prefix.first).getResourceNames())
            {
                if (resourceName.category == PDFContentResourceCategory::Properties)
                {
                    data.propertiesName = resourceName.name;
                }
            }
        }

        data.suffix = PDFContentOperationSpan{ work.appendOperation(target, Helper::createOperation(PDFContentOperator::MarkedContentEnd, "EMC")), 1 };

        const PDFContentNodeId wrapId = data.id;
        if (!work.insertNode(target, std::move(data), parentId, index, errorText) ||
            !work.moveNode(target, *topId, wrapId, -1, errorText))
        {
            return false;
        }

        work.refreshLeaks(target, wrapId);
        *topId = wrapId;
        return true;
    }

    static bool isInSourceText(const PDFContentContainer& source, const PDFContentNode& node)
    {
        int depth = 0;
        PDFContentNodePointer parent = source.getParent(node.getId());
        while (parent && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            if (parent->getKind() == Kind::TextObject)
            {
                return true;
            }
            parent = source.getParent(parent->getId());
        }
        return false;
    }

    /// Copies the operations of a span into the pool of the target; false for an operation,
    /// which cannot be copied
    static bool copyOperations(Work& work,
                               const PDFContentContainer& source,
                               Work::Container* target,
                               const PDFContentOperationSpan& span,
                               bool isSameContainer,
                               PDFContentOperationSpan* result,
                               QString* errorText)
    {
        *result = PDFContentOperationSpan();

        for (size_t i = 0; i < span.count; ++i)
        {
            if (span.first + i >= source.getOperations().getCount())
            {
                *errorText = PDFTranslationContext::tr("The object refers to operations, which the content does not have.");
                return false;
            }

            const PDFContentOperation& operation = source.getOperations().getOperation(span.first + i);
            PDFContentOperation copy = PDFContentOperation::createCopy(operation);

            if (!isSameContainer && !operation.getResourceNames().empty())
            {
                // R-4: the resources of the copy are named in the target container; the object,
                // which is a resource there already, keeps its name
                std::map<QByteArray, QByteArray> renamed;
                std::vector<PDFContentResourceName> resourceNames;
                bool isRenamed = false;

                for (const PDFContentResourceName& resourceName : operation.getResourceNames())
                {
                    const PDFObject object = source.getResources().getObject(resourceName.category, resourceName.name);
                    if (object.isNull())
                    {
                        *errorText = PDFTranslationContext::tr("The object uses a resource, which its content does not define, so it cannot be copied to another content.");
                        return false;
                    }

                    const QByteArray newName = target->editor->getResources().addObject(resourceName.category, Helper::getResourcePrefix(resourceName.category), object);
                    if (newName.isEmpty())
                    {
                        *errorText = PDFTranslationContext::tr("A resource of the object cannot be added to the other content.");
                        return false;
                    }

                    target->isTouched = true;
                    renamed[resourceName.name] = newName;
                    resourceNames.push_back(PDFContentResourceName{ resourceName.category, newName });
                    isRenamed = isRenamed || newName != resourceName.name;
                }

                if (isRenamed)
                {
                    // The operators, which name a resource, have numbers and names as their
                    // operands only (Do, Tf, gs, cs, scn, sh, BDC with a named property list):
                    // the last name is the resource. An inline image is not written again.
                    QByteArray bytes;
                    bool isWritable = operation.getOperator() != PDFContentOperator::InlineImage && !operation.getOperands().empty();
                    qsizetype lastName = -1;
                    for (size_t j = 0; j < operation.getOperands().size(); ++j)
                    {
                        if (operation.getOperands()[j].type == PDFLexicalAnalyzer::TokenType::Name)
                        {
                            lastName = qsizetype(j);
                        }
                    }

                    for (size_t j = 0; j < operation.getOperands().size() && isWritable; ++j)
                    {
                        const PDFLexicalAnalyzer::Token& token = operation.getOperands()[j];
                        switch (token.type)
                        {
                            case PDFLexicalAnalyzer::TokenType::Integer:
                            case PDFLexicalAnalyzer::TokenType::Real:
                                bytes.append(PDFContentStreamText::formatNumber(token.data.toDouble()));
                                break;

                            case PDFLexicalAnalyzer::TokenType::Name:
                            {
                                const QByteArray name = token.data.toByteArray();
                                auto it = renamed.find(name);
                                bytes.append(PDFContentStreamText::formatName(qsizetype(j) == lastName && it != renamed.end() ? it->second : name));
                                break;
                            }

                            default:
                                isWritable = false;
                                break;
                        }
                        bytes.append(' ');
                    }

                    // The text of the operator: the last token of the bytes of the operation
                    const QByteArray raw = operation.getRawBytes().trimmed();
                    qsizetype operatorStart = raw.size();
                    while (operatorStart > 0 && !QByteArray(" \t\r\n\f/()<>[]").contains(raw[operatorStart - 1]))
                    {
                        --operatorStart;
                    }
                    const QByteArray operatorText = raw.mid(operatorStart);

                    if (!isWritable || lastName < 0 || operatorText.isEmpty())
                    {
                        *errorText = PDFTranslationContext::tr("The object uses a named resource in a way, which cannot be copied to another content yet.");
                        return false;
                    }

                    bytes.append(operatorText);
                    copy = PDFContentOperation::createGenerated(operation.getOperator(), bytes, Helper::getOperands(bytes), std::move(resourceNames));
                }
            }

            const size_t index = work.appendOperation(target, std::move(copy));

            // A copy is suppressed on its own: what the override of the copied node replaces
            // stays replaced
            if (operation.getFlags().testFlag(PDFContentOperation::Suppressed))
            {
                PDFContentOperation::Flags flags = target->editor->getOperations().getOperation(index).getFlags();
                flags.setFlag(PDFContentOperation::Suppressed, true);
                target->editor->getOperations().setFlags(index, flags);
            }

            if (i == 0)
            {
                result->first = index;
            }
            result->count = i + 1;
        }

        return true;
    }

    /// Copies a subtree under a parent of the target. A sequence of the structure inside gives
    /// its children instead of itself (the copy has no marked content identifier).
    /// \param index The index of the copy among the children, -1 = at the end
    /// \param created The nodes created right under the parent
    static bool copyNode(Work& work,
                         const PDFContentContainer& source,
                         Work::Container* target,
                         const PDFContentNodePointer& node,
                         PDFContentNodeId parentId,
                         PDFInteger index,
                         bool isSameContainer,
                         int depth,
                         std::vector<PDFContentNodeId>* created,
                         QString* errorText)
    {
        if (depth > PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            *errorText = PDFTranslationContext::tr("The object cannot be placed there: the content would be nested too deep.");
            return false;
        }

        if (node->getKind() == Kind::Opaque || node->getKind() == Kind::Root)
        {
            *errorText = PDFTranslationContext::tr("The object has a part, which cannot be read, so it cannot be copied.");
            return false;
        }

        const bool isOpen = source.isUnclosed(node->getId()) || (!PDFContentNode::isLeafKind(node->getKind()) && node->getSuffix().isEmpty());
        if (isOpen)
        {
            *errorText = PDFTranslationContext::tr("The object is not closed in the content, so it cannot be copied.");
            return false;
        }

        if (node->getKind() == Kind::MarkedContent && node->getData().mcid && depth > 0)
        {
            for (const PDFContentNodePointer& child : node->getChildren())
            {
                std::vector<PDFContentNodeId> ignored;
                if (!copyNode(work, source, target, child, parentId, index, isSameContainer, depth + 1, &ignored, errorText))
                {
                    return false;
                }
                if (index >= 0)
                {
                    index += PDFInteger(ignored.size());
                }
                created->insert(created->end(), ignored.cbegin(), ignored.cend());
            }
            return true;
        }

        PDFContentNode::Data data = node->getData();
        data.id = work.getSession()->allocateNodeId();

        if (!copyOperations(work, source, target, node->getPrefix(), isSameContainer, &data.prefix, errorText) ||
            !copyOperations(work, source, target, node->getSuffix(), isSameContainer, &data.suffix, errorText))
        {
            return false;
        }

        const PDFContentNodeId id = data.id;
        if (!work.insertNode(target, std::move(data), parentId, index, errorText))
        {
            return false;
        }
        created->push_back(id);

        for (const PDFContentNodePointer& child : node->getChildren())
        {
            std::vector<PDFContentNodeId> ignored;
            if (!copyNode(work, source, target, child, id, -1, isSameContainer, depth + 1, &ignored, errorText))
            {
                return false;
            }
        }

        return true;
    }

    std::vector<PDFContentNodeReference> m_nodes;
    PDFContentNodeReference m_parent;
    PDFInteger m_index = -1;
    QTransform m_matrix;
};

/// Wraps nodes of one parent into a new group q ... Q (the state runs between them stay in place)
class PDFContentGroupCommand : public PDFContentPlaceCommand
{
public:
    explicit PDFContentGroupCommand(std::vector<PDFContentNodeReference> nodes) :
        m_nodes(std::move(nodes))
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Group"); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_GROUP; }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_GROUP));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_IDS), writeIds(m_nodes));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        std::vector<PDFContentNodeReference> nodes;
        bool result = PDFContentCommandJson::checkKeys(object, { PDFContentJson::KEY_ID, PDFContentCommandJson::KEY_IDS }, path, errors);
        result = PDFContentCommandJson::readNodes(object, session, path, errors, &nodes) && result;
        return result ? PDFContentCommandFactory::createGroup(std::move(nodes)) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFTranslationContext::tr("There is no object to group."), errorText))
        {
            return false;
        }

        Work::Container* container = nullptr;
        PDFContentNodeId parentId;

        // The objects in the order of the tree
        std::map<PDFInteger, PDFContentNodeId> ordered;
        for (const PDFContentNodeReference& reference : m_nodes)
        {
            if (reference.container != m_nodes.front().container)
            {
                *errorText = PDFTranslationContext::tr("The objects to group are not of one content.");
                return false;
            }

            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node || !checkMovable(container, node, errorText))
            {
                return false;
            }

            if (parentId.isValid() && node->getParentId() != parentId)
            {
                *errorText = PDFTranslationContext::tr("Only objects of one parent can be grouped.");
                return false;
            }

            parentId = node->getParentId();
            ordered[Work::getIndex(container, reference.node)] = reference.node;
        }

        setPlace(work.getSession(), m_nodes.front().container);

        const PDFContentNodePointer parent = container->editor->getNode(parentId);
        if (!parent || !checkParent(container, parent, errorText))
        {
            return false;
        }

        // q ... Q ends a clipping path set inside. An object, which sets one (a clipping path,
        // a path with W, a text with a rendering mode 4 to 7, a marked content holding one),
        // clips everything behind it in the parent: in a group it would stop clipping the
        // content, which stays outside (R-1: a node with a clipping effect is never wrapped,
        // "the following siblings would lose the clip"). Refused, unless everything painted
        // behind it in the parent comes into the group too - as Ungroup refuses a group with
        // a clip.
        {
            std::set<PDFContentNodeId> grouped;
            for (const auto& item : ordered)
            {
                grouped.insert(item.second);
            }

            bool isClipSet = false;
            bool isClipLost = false;
            for (const PDFContentNodePointer& child : parent->getChildren())
            {
                if (grouped.count(child->getId()))
                {
                    isClipSet = isClipSet || child->hasClipEffect() || child->getLeak().isClipChanged;
                }
                else
                {
                    isClipLost = isClipLost || (isClipSet && Helper::hasContent(*child) && !isTail(*child));
                }
            }

            // A marked content does not end a clipping path (only Q does): the clip reaches
            // the content behind the marked contents the objects stand in
            int depth = 0;
            PDFContentNodePointer wrap = parent;
            while (isClipSet && !isClipLost && wrap && wrap->getKind() == Kind::MarkedContent && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                const PDFContentNodePointer outer = container->editor->getNode(wrap->getParentId());
                bool isBehind = false;
                for (const PDFContentNodePointer& child : outer ? outer->getChildren() : std::vector<PDFContentNodePointer>())
                {
                    isClipLost = isClipLost || (isBehind && Helper::hasContent(*child) && !isTail(*child));
                    isBehind = isBehind || child->getId() == wrap->getId();
                }
                wrap = outer;
            }

            if (isClipLost)
            {
                *errorText = PDFTranslationContext::tr("An object to group sets a clipping path for the objects behind it: in a group it would not clip them any more. Group it together with them.");
                return false;
            }
        }

        const PDFContentNodePointer first = container->editor->getNode(ordered.cbegin()->second);
        const State groupState{ first->getState(), first->getSymbolicState() };

        QRectF boundingBox;
        for (const auto& item : ordered)
        {
            boundingBox = boundingBox.united(container->editor->getNode(item.second)->getBoundingBox());
        }

        // The group stands at the place of the first object; it is the first added node of the change
        PDFContentNodeId groupId;
        if (!work.insertGroup(container, parentId, ordered.cbegin()->first, groupState, boundingBox, &groupId, errorText))
        {
            return false;
        }

        for (const auto& item : ordered)
        {
            PDFContentNodeId topId;
            if (!moveAndPlace(work, container, item.second, groupId, PDFContentNodeId(), &topId, errorText))
            {
                return false;
            }
        }

        return true;
    }

private:
    /// The tail of a content: bytes, which are no operations (an opaque node without any)
    static bool isTail(const PDFContentNode& node)
    {
        return node.getKind() == Kind::Opaque && node.getPrefix().isEmpty() && node.getSuffix().isEmpty() && node.getChildren().empty();
    }

    std::vector<PDFContentNodeReference> m_nodes;
};

/// Removes a group and moves its children out by R-2 (refused for a group with a clip)
class PDFContentUngroupCommand : public PDFContentPlaceCommand
{
public:
    explicit PDFContentUngroupCommand(PDFContentNodeReference group) :
        m_group(group)
    {
        setPlace(nullptr, m_group.container);
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Ungroup"); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_UNGROUP; }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_UNGROUP));
        object.insert(QLatin1String(PDFContentJson::KEY_ID), writeId(m_group));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        PDFContentNodeReference group;
        bool result = PDFContentCommandJson::checkKeys(object, { PDFContentJson::KEY_ID }, path, errors);
        result = PDFContentCommandJson::readNode(object.value(QLatin1String(PDFContentJson::KEY_ID)), session, path, PDFContentJson::KEY_ID, errors, &group) && result;
        return result ? PDFContentCommandFactory::createUngroup(group) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        Work::Container* container = nullptr;
        const PDFContentNodePointer group = work.getNode(m_group, &container, errorText);
        if (!group)
        {
            return false;
        }

        setPlace(work.getSession(), m_group.container);

        if (group->getKind() != Kind::Group)
        {
            *errorText = PDFTranslationContext::tr("The object is not a group.");
            return false;
        }

        if (Work::isPinned(container, m_group.node) || group->getSuffix().isEmpty() || Work::isInUnclosedNode(container, m_group.node))
        {
            *errorText = PDFTranslationContext::tr("The group is not closed in the content, or it is inside of an object, which is not closed, so it cannot be removed.");
            return false;
        }

        std::vector<PDFContentNodeId> children;
        for (const PDFContentNodePointer& child : group->getChildren())
        {
            if (child->getKind() == Kind::Opaque)
            {
                *errorText = PDFTranslationContext::tr("The group has a part, which cannot be read, so it cannot be removed.");
                return false;
            }

            // The clip ends with the group: outside it would reach the rest of the parent
            if (child->hasClipEffect() || child->getLeak().isClipChanged)
            {
                *errorText = PDFTranslationContext::tr("The group sets a clipping path: release the clipping first.");
                return false;
            }

            if (child->getKind() != Kind::StateRun)
            {
                children.push_back(child->getId());
            }
        }

        // The children go in front of the group, each with the difference between the state
        // it saw inside and the state outside (R-2); the state runs die with the group
        const PDFContentNodeId parentId = group->getParentId();
        for (const PDFContentNodeId childId : children)
        {
            const PDFContentNodePointer child = container->editor->getNode(childId);
            const State source{ child->getState(), child->getSymbolicState() };

            const PDFInteger index = Work::getIndex(container, m_group.node);
            const State target = Work::getStateAt(container, parentId, index);

            PDFContentNodeId topId;
            if (!work.moveNode(container, childId, parentId, index, errorText) ||
                !work.placeNode(container, childId, source, target, container->isForm, true, container->editor->getOperations(), &topId, errorText))
            {
                return false;
            }
        }

        work.eraseNode(container, m_group.node);
        work.refreshLeaks(container, parentId);
        return true;
    }

private:
    PDFContentNodeReference m_group;
};

// ---------------------------------------------------------------------------------------------
//  Clear Page
// ---------------------------------------------------------------------------------------------

/// Removes every node of a page (every sequence of the page leaves the structure)
class PDFContentClearPageCommand : public PDFContentModelCommand
{
public:
    explicit PDFContentClearPageCommand(PDFContentContainerKey page) :
        m_page(page)
    {
        setPlace(nullptr, m_page);
    }

    virtual QString getText() const override { return PDFContentCommandFactory::tr("Clear Page"); }
    virtual QByteArray getType() const override { return PDFContentCommandFactory::TYPE_CLEAR_PAGE; }

    virtual QJsonObject toJson() const override
    {
        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(PDFContentCommandFactory::TYPE_CLEAR_PAGE));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_PAGE), qint64(m_page.pageIndex + 1));
        return object;
    }

    static PDFContentCommandPointer fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        bool result = Json::checkKeys(object, { Json::KEY_PAGE }, path, errors);

        PDFInteger pageNumber = 0;
        PDFContentContainerKey key;
        if (Json::readInteger(object.value(QLatin1String(Json::KEY_PAGE)), 1, std::numeric_limits<int>::max(), &pageNumber) && session)
        {
            key = session->getPageKey(pageNumber - 1);
        }

        if (!key.isValid())
        {
            Json::addError(errors, path, Json::KEY_PAGE, PDFTranslationContext::tr("The number of a page of the document is expected."));
            result = false;
        }

        return result ? PDFContentCommandFactory::createClearPage(key) : nullptr;
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!m_page.isValid() || m_page.kind != PDFContentContainerKey::Kind::Page)
        {
            *errorText = PDFTranslationContext::tr("Only the content of a page can be cleared.");
            return false;
        }

        Work::Container* container = work.getContainer(m_page, errorText);
        if (!container)
        {
            return false;
        }

        // A page without content is cleared already: the command changes nothing
        const PDFContentNodePointer root = container->editor->getNode(container->source->getRoot() ? container->source->getRoot()->getId() : PDFContentNodeId());
        if (!root)
        {
            *errorText = PDFTranslationContext::tr("The page has no content.");
            setRefusedAsUnchanged(true);
            return false;
        }

        size_t count = 0;
        for (const PDFContentNodePointer& child : root->getChildren())
        {
            // The tail of the content (bytes, which are no operations) has no operations to
            // remove: its node stays, the stream writer writes the bytes last
            if (child->getKind() == Kind::Opaque && child->getPrefix().isEmpty() && child->getSuffix().isEmpty() && child->getChildren().empty())
            {
                continue;
            }

            if (!work.removeSequences(container, child, errorText))
            {
                return false;
            }

            work.eraseNode(container, child->getId());
            ++count;
        }

        if (count == 0)
        {
            *errorText = PDFTranslationContext::tr("The page has no content.");
            setRefusedAsUnchanged(true);
            return false;
        }

        return true;
    }

private:
    PDFContentContainerKey m_page;
};

// ---------------------------------------------------------------------------------------------
//  Factory
// ---------------------------------------------------------------------------------------------

/// The registry of the JSON types. Ownership: one object for the process, a function-local
/// static of getInstance (so a static registration of a command file works in any order of the
/// initialization); the basic types are in it from its creation. Guarded by a mutex: the types
/// are registered before the factory is used, but nothing breaks, when a late one comes.
class PDFContentCommandRegistry
{
public:
    static PDFContentCommandRegistry& getInstance()
    {
        static PDFContentCommandRegistry instance;
        return instance;
    }

    bool registerType(const char* type, PDFContentCommandFactory::Creator creator)
    {
        if (!type || !*type || !creator)
        {
            return false;
        }

        QMutexLocker locker(&m_mutex);
        return m_creators.emplace(QByteArray(type), creator).second;
    }

    PDFContentCommandFactory::Creator getCreator(const QByteArray& type)
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_creators.find(type);
        return it != m_creators.end() ? it->second : nullptr;
    }

    std::vector<QByteArray> getTypes()
    {
        QMutexLocker locker(&m_mutex);
        std::vector<QByteArray> result;
        for (const auto& item : m_creators)
        {
            result.push_back(item.first);
        }
        return result;
    }

private:
    PDFContentCommandRegistry()
    {
        using Factory = PDFContentCommandFactory;

        m_creators.emplace(QByteArray(Factory::TYPE_DELETE), &PDFContentDeleteCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_TRANSFORM), &PDFContentTransformCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_REORDER), &PDFContentReorderCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_COPY), &PDFContentCopyCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_SET_APPEARANCE), &PDFContentSetPropertiesCommand::appearanceFromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_SET_TRANSPARENCY), &PDFContentSetPropertiesCommand::transparencyFromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_GROUP), &PDFContentGroupCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_UNGROUP), &PDFContentUngroupCommand::fromJson);
        m_creators.emplace(QByteArray(Factory::TYPE_CLEAR_PAGE), &PDFContentClearPageCommand::fromJson);
    }

    QMutex m_mutex;
    std::map<QByteArray, PDFContentCommandFactory::Creator> m_creators;     ///< Sorted by the type
};

PDFContentCommandPointer PDFContentCommandFactory::createDelete(std::vector<PDFContentNodeReference> nodes)
{
    return std::make_shared<PDFContentDeleteCommand>(std::move(nodes));
}

PDFContentCommandPointer PDFContentCommandFactory::createTransform(std::vector<PDFContentNodeReference> nodes, const QTransform& matrix)
{
    return std::make_shared<PDFContentTransformCommand>(std::move(nodes), matrix);
}

PDFContentCommandPointer PDFContentCommandFactory::createReorder(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, ReorderMode mode)
{
    return std::make_shared<PDFContentReorderCommand>(std::move(nodes), parent, index, mode);
}

PDFContentCommandPointer PDFContentCommandFactory::createCopy(std::vector<PDFContentNodeReference> nodes, PDFContentNodeReference parent, PDFInteger index, const QTransform& matrix)
{
    return std::make_shared<PDFContentCopyCommand>(std::move(nodes), parent, index, matrix);
}

PDFContentCommandPointer PDFContentCommandFactory::createSetAppearance(std::vector<PDFContentNodeReference> nodes, std::map<PDFContentStateProperty, PDFContentStateValue> properties)
{
    return std::make_shared<PDFContentSetPropertiesCommand>(std::move(nodes), std::move(properties), true);
}

PDFContentCommandPointer PDFContentCommandFactory::createSetTransparency(std::vector<PDFContentNodeReference> nodes, std::optional<PDFReal> alphaStroking, std::optional<PDFReal> alphaFilling, std::optional<QByteArray> blendMode)
{
    std::map<PDFContentStateProperty, PDFContentStateValue> properties;

    if (alphaStroking)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Real;
        value.real = *alphaStroking;
        properties[PDFContentStateProperty::AlphaStroking] = std::move(value);
    }

    if (alphaFilling)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Real;
        value.real = *alphaFilling;
        properties[PDFContentStateProperty::AlphaFilling] = std::move(value);
    }

    if (blendMode)
    {
        PDFContentStateValue value;
        value.kind = PDFContentStateValue::Kind::Name;
        value.name = *blendMode;
        properties[PDFContentStateProperty::BlendMode] = std::move(value);
    }

    return std::make_shared<PDFContentSetPropertiesCommand>(std::move(nodes), std::move(properties), false);
}

PDFContentCommandPointer PDFContentCommandFactory::createGroup(std::vector<PDFContentNodeReference> nodes)
{
    return std::make_shared<PDFContentGroupCommand>(std::move(nodes));
}

PDFContentCommandPointer PDFContentCommandFactory::createUngroup(PDFContentNodeReference group)
{
    return std::make_shared<PDFContentUngroupCommand>(group);
}

PDFContentCommandPointer PDFContentCommandFactory::createClearPage(PDFContentContainerKey page)
{
    return std::make_shared<PDFContentClearPageCommand>(page);
}

PDFContentCommandPointer PDFContentCommandFactory::fromJson(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
{
    const QJsonValue typeValue = object.value(QLatin1String(PDFContentJson::KEY_TYPE));
    if (!typeValue.isString())
    {
        PDFContentCommandJson::addError(errors, path, PDFContentJson::KEY_TYPE, tr("The type of the operation is expected."));
        return nullptr;
    }

    const Creator creator = PDFContentCommandRegistry::getInstance().getCreator(typeValue.toString().toLatin1());
    if (!creator)
    {
        PDFContentCommandJson::addError(errors, path, PDFContentJson::KEY_TYPE, tr("The operation '%1' is not known.").arg(typeValue.toString()));
        return nullptr;
    }

    if (!session)
    {
        PDFContentCommandJson::addError(errors, path, nullptr, tr("There is no session to read the operation for."));
        return nullptr;
    }

    const size_t errorCount = errors ? errors->size() : 0;
    PDFContentCommandPointer command = creator(object, session, path, errors);
    if (!command && errors && errors->size() == errorCount)
    {
        // A creator, which refuses without a word, still gives an error
        PDFContentCommandJson::addError(errors, path, nullptr, tr("The operation cannot be read."));
    }
    return command;
}

bool PDFContentCommandFactory::registerType(const char* type, Creator creator)
{
    return PDFContentCommandRegistry::getInstance().registerType(type, creator);
}

std::vector<QByteArray> PDFContentCommandFactory::getTypes()
{
    return PDFContentCommandRegistry::getInstance().getTypes();
}

}   // namespace pdf
