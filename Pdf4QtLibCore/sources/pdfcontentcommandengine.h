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

#ifndef PDFCONTENTCOMMANDENGINE_H
#define PDFCONTENTCOMMANDENGINE_H

// Edit Content: the engine of the commands (internal header of Pdf4QtLibCore - not installed,
// not a public API, not exported). The commands of pdfcontentcommands.cpp and of the later
// command files (pdfcontentcommands_<area>.cpp) use it: a command derives from
// PDFContentModelCommand (or PDFContentPlaceCommand, when it puts nodes to a place), implements
// compute, getText, getType and toJson, and registers its JSON type by
// PDFContentCommandFactory::registerType. The principle and the rules: pdfcontentcommands.cpp.

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
#include <QPointer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace pdf
{

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

    /// Limits of the walk, which finds the new objects of the session a command names (the
    /// values of the overrides and of the resources it keeps and the new objects they refer to):
    /// the nesting of direct values and of new objects, and the number of the values looked at.
    /// A walk over a limit drops nothing.
    static constexpr int MAXIMUM_NAMED_OBJECT_DEPTH = 64;
    static constexpr size_t MAXIMUM_NAMED_OBJECT_VALUES = 1000000;

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

            case Operator::TextSetRenderMode:
                // The mode a switch of the painting of the whole text sets (set_appearance)
                return Property::TextRenderingMode;

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
    /// The object of the XObject resources is a form XObject: a stream with /Subtype /Form
    /// (ISO 32000-2, 8.10.2, Table 93). A reference is followed once: to a new object of the
    /// session, or to an object of the document.
    static bool isFormXObject(const PDFContentEditSession* session, const PDFObject& object)
    {
        const PDFDocument* document = session ? session->getDocument() : nullptr;

        PDFObject value = object;
        if (value.isReference())
        {
            const PDFObject newObject = session ? session->getNewObject(value.getReference()) : PDFObject();
            value = !newObject.isNull() ? newObject : (document ? document->getObjectByReference(value.getReference()) : PDFObject());
        }

        if (!value.isStream())
        {
            return false;
        }

        PDFObject subtype = value.getStream()->getDictionary()->get("Subtype");
        if (subtype.isReference() && document)
        {
            subtype = document->getObjectByReference(subtype.getReference());
        }

        return subtype.isName() && subtype.getString() == "Form";
    }

    /// The prefix of the name of a new resource (zadani_jadro.md 4.4, R-4;
    /// PDFContentResources::allocateName): F, Im (an image and every XObject, which is no
    /// form), Fm (a form XObject), GS, CS, P, Sh, MC
    /// \param isForm XObject only: the object is a form XObject
    static QByteArray getResourcePrefix(PDFContentResourceCategory category, bool isForm)
    {
        switch (category)
        {
            case PDFContentResourceCategory::Font:
                return "F";
            case PDFContentResourceCategory::XObject:
                return isForm ? "Fm" : "Im";
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
    //
    // The tree being changed is read through the queries of the editor, which make no node
    // (PDFContentContainerEditor::getParentId and the functions beside it): the node of a parent
    // costs all its children again after every change below it, so a command over many children
    // of one parent (Select All and Delete on a flat page) must never ask for the node of the
    // parent once per child. The node of a leaf, and of the node a command names, is cheap.

    static PDFInteger getIndex(const Container* container, PDFContentNodeId id)
    {
        return container->editor->getIndexInParent(id);
    }

    static PDFInteger getChildCount(const Container* container, PDFContentNodeId id)
    {
        return container->editor->getChildCount(id);
    }

    /// \p ancestor is the node \p id or one of the nodes above it
    static bool isAncestorOrSelf(const Container* container, PDFContentNodeId ancestor, PDFContentNodeId id)
    {
        int depth = 0;
        PDFContentNodeId current = id;
        while (container->editor->getNodeData(current) && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            if (current == ancestor)
            {
                return true;
            }
            current = container->editor->getParentId(current);
        }
        return false;
    }

    /// The node \p id or a node above it is a text object (q, cm, Do and paths are not allowed
    /// between BT and ET - ISO 32000-2, 8.2)
    static bool isInTextObject(const Container* container, PDFContentNodeId id)
    {
        int depth = 0;
        PDFContentNodeId current = id;
        const PDFContentNode::Data* data = container->editor->getNodeData(current);
        while (data && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            if (data->kind == Kind::TextObject)
            {
                return true;
            }
            current = container->editor->getParentId(current);
            data = container->editor->getNodeData(current);
        }
        return false;
    }

    /// The nearest sequence of the structure (a marked content with an MCID) at or above a node
    static PDFContentNodeId getSequence(const Container* container, PDFContentNodeId id)
    {
        int depth = 0;
        PDFContentNodeId current = id;
        const PDFContentNode::Data* data = container->editor->getNodeData(current);
        while (data && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            if (data->kind == Kind::MarkedContent && data->mcid)
            {
                return current;
            }
            current = container->editor->getParentId(current);
            data = container->editor->getNodeData(current);
        }
        return PDFContentNodeId();
    }

    /// The node \p id or something below it is content (Helper::hasContent for the tree being
    /// changed, read child by child: the walk ends at the first content it finds, and the node
    /// of a parent with many children is never made)
    static bool hasContent(const Container* container, PDFContentNodeId id, int depth = 0)
    {
        const PDFContentNode::Data* data = container->editor->getNodeData(id);
        if (!data)
        {
            return false;
        }

        switch (data->kind)
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

        const PDFInteger count = container->editor->getChildCount(id);
        for (PDFInteger i = 0; i < count; ++i)
        {
            if (hasContent(container, container->editor->getChildId(id, i), depth + 1))
            {
                return true;
            }
        }

        return false;
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

    // A node the content does not close (a q without its Q, a BT without its ET, a BDC without
    // its EMC: PDFContentContainer::isUnclosed) reaches to the end of the content with everything
    // inside of it. What the commands refuse because of it:
    //  - the unclosed node itself is not moved, copied, grouped, ungrouped or transformed (R-7:
    //    the model adds a missing closing operator only where something is written behind the
    //    node; these commands never give such a node another place or a wrap) - also not as the
    //    wrap an order command would take along with its only content. A change of the state
    //    (set_appearance, set_transparency, the paint switches) is not refused: an unclosed text
    //    object gets its wrap with the missing ET written inside of it ("q 0.5 g BT ... ET Q"),
    //    as since stage E0;
    //  - nothing is placed behind an unclosed text, which adds to the clipping path
    //    (isClipPendingBehind): the ET the model would write makes that clip real. The builder
    //    marks such a text as an open end, so the end of its parent is in front of it
    //    (getPinnedChild); the check covers a text without the mark (a tree made by an editor).
    // A closed node inside of an unclosed one is a node as any other: the wraps, the state runs
    // and the groups a command makes stand inside of the unclosed node, which stays unclosed,
    // and a node, which leaves it for a place behind it, gets the state it saw by R-2 (the
    // model closes the unclosed node in front of it).

    /// Closing the unclosed node (or the chain of its last unclosed children) would make
    /// a pending clipping path real for what is written behind it: a text object with a text
    /// rendering mode 4 to 7 without its ET. The glyphs of such a text are added to the clipping
    /// path at the end of the text object (ISO 32000-2, 9.3.6) - in the content they never are,
    /// because the text object does not end; an ET written by the model in front of new content
    /// would clip that content. A group above such a text takes the clip away again with its Q;
    /// a marked content does not.
    static bool isClipPendingBehind(const PDFContentContainer& source, PDFContentNodePointer node)
    {
        int depth = 0;
        while (node && source.isUnclosed(node->getId()) && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            switch (node->getKind())
            {
                case Kind::TextObject:
                    return node->hasClipEffect() || node->getData().clipsByText;

                case Kind::MarkedContent:
                    node = node->getChildren().empty() ? PDFContentNodePointer() : node->getChildren().back();
                    break;

                default:
                    return false;
            }
        }
        return false;
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
        if (!container->editor->getNodeData(parentId))
        {
            return State();
        }

        // The two children around the place are asked for, never the node of the parent
        const PDFInteger count = container->editor->getChildCount(parentId);
        if (index >= 0 && index < count)
        {
            const PDFContentNodePointer child = container->editor->getNode(container->editor->getChildId(parentId, index));
            const State state{ child->getState(), child->getSymbolicState() };
            if (state.isValid())
            {
                return state;
            }
        }

        if (index > 0 && index <= count)
        {
            return Helper::getStateAfter(*container->editor->getNode(container->editor->getChildId(parentId, index - 1)));
        }

        const PDFContentNode::Data* parent = container->editor->getNodeData(parentId);
        if (parent->kind == Kind::Root)
        {
            const PDFContentContainer::Data& data = container->editor->getContainerData();
            return State{ data.initialState, data.initialSymbolicState };
        }

        return State{ parent->state, parent->symbolicState };
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

    /// An added node is an object of the command (a copy): it stands in front of the nodes the
    /// command has added on its way (the piece of a sequence split for the copy, a state run),
    /// behind the objects named so before it. PDFContentChange::addedNodes begins with the
    /// objects of the command: the views select them, a script names the first one.
    void setAddedNodeAsObject(Container* container, PDFContentNodeId id)
    {
        const PDFContentNodeReference reference{ container->key, id };
        auto it = std::find(m_added.begin(), m_added.end(), reference);
        if (it == m_added.end() || size_t(std::distance(m_added.begin(), it)) < m_objectCount)
        {
            return;
        }

        std::rotate(m_added.begin() + std::ptrdiff_t(m_objectCount), it, it + 1);
        ++m_objectCount;
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
        // Called after every change of the children of a node, so it must not cost the number
        // of the children: the last child and the count of the children, which change the
        // clipping path, are asked of the editor (the node of a marked content with all its
        // children is never made here; of any other parent not even its data are read twice)
        int depth = 0;
        PDFContentNodeId current = id;
        const PDFContentNode::Data* node = container->editor->getNodeData(current);
        while (node && node->kind == Kind::MarkedContent && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
        {
            const State begin{ node->state, node->symbolicState };
            const PDFInteger count = container->editor->getChildCount(current);
            const State end = count == 0 ? begin : Helper::getStateAfter(*container->editor->getNode(container->editor->getChildId(current, count - 1)));
            const bool isClipChanged = container->editor->isClipChangedByChildren(current);

            const PDFContentStateLeak leak = Helper::makeLeak(begin, end, container->isForm, isClipChanged);
            const PDFContentStateLeak& oldLeak = node->leak;
            if (!(leak.properties == oldLeak.properties) || leak.isTransformChanged != oldLeak.isTransformChanged || leak.isClipChanged != oldLeak.isClipChanged ||
                leak.endState != oldLeak.endState || leak.endSymbolicState != oldLeak.endSymbolicState)
            {
                PDFContentNode::Data data = *node;
                data.leak = leak;
                container->editor->setNodeData(current, std::move(data));  // "node" is not valid after this line
                container->isTouched = true;
            }

            current = container->editor->getParentId(current);
            node = container->editor->getNodeData(current);
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

        // A move by the identity matrix moves nothing: the object passed every check a move
        // makes before it changes anything, and it is left as it is (no wrap, no copy of
        // a pattern). A command, which changes no object so, is refused as unchanged.
        if (displacement.isIdentity())
        {
            return true;
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

    /// The text rendering mode a switch of the painting gives to a show of the mode \p mode
    /// (ISO 32000-2, 9.3.6, Table 104: 0 fill, 1 stroke, 2 fill and stroke, 3 neither; 4 - 7 the
    /// same, which also clip): the switch, which is not given, keeps its part, the clip is kept
    static PDFInteger getSwitchedTextRenderingMode(PDFInteger mode, const PDFContentPaintSwitches& switches)
    {
        const PDFInteger clip = mode >= 4 ? 4 : 0;
        const PDFInteger paint = mode - clip;
        const bool isFilled = switches.fill.value_or(paint == 0 || paint == 2);
        const bool isStroked = switches.stroke.value_or(paint == 1 || paint == 2);
        const PDFInteger switched = isFilled ? (isStroked ? 2 : 0) : (isStroked ? 1 : 3);
        return clip + switched;
    }

    /// The text rendering mode a text object gives its shows by a switch of the painting: the
    /// property TextRenderingMode of its wrap. No value: nothing changes (every show has its mode
    /// already); an error: its shows would get different modes.
    /// \param mode The mode for the wrap
    /// \returns false, when the switch cannot be made for the whole text
    static bool getSwitchedTextRenderingMode(const PDFContentNodePointer& text, const PDFContentPaintSwitches& switches, std::optional<PDFInteger>* mode, QString* errorText)
    {
        *mode = std::nullopt;

        // The mode a show is painted with: the one of the wrap of the text (its own Tr operators
        // are suppressed then, R-1), else the one of its state
        std::optional<PDFInteger> wrapMode;
        if (const PDFContentStateOverride* override = text->getOverride())
        {
            auto it = override->properties.find(Property::TextRenderingMode);
            if (it != override->properties.cend() && it->second.kind == PDFContentStateValue::Kind::Integer)
            {
                wrapMode = it->second.integer;
            }
        }

        bool isChanged = false;
        std::optional<PDFInteger> switchedMode;
        for (const PDFContentNodePointer& item : Helper::getSubtree(text))
        {
            if (item->getKind() != Kind::TextShow)
            {
                continue;
            }

            PDFInteger current = 0;
            if (wrapMode)
            {
                current = *wrapMode;
            }
            else if (item->getState())
            {
                current = PDFInteger(item->getState()->getState().getTextRenderingMode());
            }

            const PDFInteger switched = getSwitchedTextRenderingMode(current, switches);
            isChanged = isChanged || switched != current;
            if (switchedMode && *switchedMode != switched)
            {
                *errorText = PDFTranslationContext::tr("The parts of the text are painted in different ways; the painting cannot be switched for the whole text yet.");
                return false;
            }
            switchedMode = switched;
        }

        if (isChanged)
        {
            *mode = switchedMode;
        }
        return true;
    }

    /// Sets properties of the appearance or of the transparency of a node: a leaf gets them into
    /// its override, a group or a marked content gives them to its leaves (R-1). The switches of
    /// the painting (the appearance only, PDFContentCommandFactory::createSetAppearance) change
    /// the painting operator of a path and the text rendering mode of a text.
    /// \param isAppearance Colors and line parameters (paths and texts only); else the alphas
    ///        and the blend mode (every leaf, which paints)
    bool setProperties(Container* container,
                       PDFContentNodeId id,
                       const std::map<Property, PDFContentStateValue>& nodeProperties,
                       const PDFContentPaintSwitches& switches,
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
                if (!setProperties(container, childId, nodeProperties, switches, isAppearance, false, depth + 1, count, errorText))
                {
                    return false;
                }
            }
            return true;
        }

        PDFContentNode::Data data = node->getData();

        // A clipping path paints nothing: the properties do not apply to it. A switch, which
        // paints it, does, when the command names the path itself (a group leaves it alone).
        const bool isClippingPath = kind == Kind::Path && data.paint.isClippingPath() && !(isNamed && !switches.isEmpty());

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

        if (kind == Kind::TextObject && data.hasClipEffect)
        {
            *errorText = PDFTranslationContext::tr("A text, which adds to the clipping path, cannot be restyled yet.");
            return false;
        }

        // The switches of the painting: a path gets its new painting operator, a text the mode of
        // its wrap (a property as the others, so the operators Tr of the text are suppressed)
        std::map<Property, PDFContentStateValue> textProperties;
        const std::map<Property, PDFContentStateValue>* propertiesPointer = &nodeProperties;
        PDFContentPathPaint paint = data.paint;
        if (!switches.isEmpty() && kind == Kind::Path)
        {
            paint.fill = switches.fill.value_or(paint.fill);
            paint.stroke = switches.stroke.value_or(paint.stroke);
        }
        else if (!switches.isEmpty() && kind == Kind::TextObject)
        {
            std::optional<PDFInteger> mode;
            if (!getSwitchedTextRenderingMode(node, switches, &mode, errorText))
            {
                return false;
            }

            if (mode)
            {
                textProperties = nodeProperties;
                PDFContentStateValue value;
                value.kind = PDFContentStateValue::Kind::Integer;
                value.integer = *mode;
                textProperties[Property::TextRenderingMode] = value;
                propertiesPointer = &textProperties;
            }
        }
        const std::map<Property, PDFContentStateValue>& properties = *propertiesPointer;
        const bool isPaintChanged = paint.fill != data.paint.fill || paint.stroke != data.paint.stroke;

        // The object has every value already: nothing is set (no wrap, no state run, no
        // suppressed operator), and a command, which changes no object at all, is refused as
        // unchanged. An object has a value, when its override sets it; an object without that
        // value in its override has the value of the state at its place - but not a text,
        // whose own operators may set the property again for a part of it (the override of
        // a text suppresses them, which is a change), and not a property a form or a pattern
        // cell inherits (Container::isForm): the state of the form is the one of its first use,
        // another use draws the object with another value (ISO 32000-2, 8.10.1). A path, whose
        // painting is switched, changes in any case.
        bool isSetAlready = true;
        {
            const State state{ data.state, data.symbolicState };

            for (const auto& item : properties)
            {
                PDFContentStateValue stateValue;
                const PDFContentStateValue* current = nullptr;

                if (data.override)
                {
                    auto it = data.override->properties.find(item.first);
                    if (it != data.override->properties.cend())
                    {
                        current = &it->second;
                    }
                }

                if (!current && kind != Kind::TextObject && state.isValid() && !(container->isForm && state.symbolic->isInherited(item.first)))
                {
                    // A value, which cannot be read from the state, is no value the object has
                    bool isSkipped = false;
                    QString ignoredText;
                    if (Helper::makeValue(item.first, state, container->editor->getOperations(), &stateValue, &isSkipped, &ignoredText) && !isSkipped)
                    {
                        current = &stateValue;
                    }
                }

                if (!current || !PDFContentStateValue::isEqual(*current, item.second))
                {
                    isSetAlready = false;
                    break;
                }
            }

            if (isSetAlready && !isPaintChanged)
            {
                ++*count;
                return true;
            }
        }

        if (!isSetAlready)
        {
            if (kind == Kind::TextObject)
            {
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
        }

        if (isPaintChanged)
        {
            // The painting operator of the path changes: the path is written by its geometry
            // (PDFContentStreamWriter::writePathPaint keeps the fill rule, the closing and the clip)
            data.paint = paint;
            data.isModified = true;
        }

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
        // The sequence is read through the editor: its node would cost all its children after
        // every node deleted from it (a tagged figure of many paths)
        const PDFContentNode::Data* sequence = container->editor->getNodeData(sequenceId);
        if (!sequence || !sequence->mcid)
        {
            return true;
        }

        const PDFInteger mcid = *sequence->mcid;
        const bool isEmpty = !hasContent(container, sequenceId);
        const PDFContentStructureModelPointer structure = getStructure();
        if (structure && !addStructureChange(container, structure->getChangeForDelete(container->source->getStructuralParentKey(), mcid, isEmpty), errorText))
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
        PDFContentStructureDifference structureDifference;  ///< What the changes touched in the model, before and after
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
            result.structureAfter = m_structure->applied(m_structureChanges, &result.structureDifference);
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
    size_t m_objectCount = 0;                           ///< The added nodes at the beginning of m_added, which are objects of the command (setAddedNodeAsObject)
};

/// A command, which changes the containers and the structure model: it computes the new versions
/// once (compute) and keeps the difference of each container (PDFContentContainerDifference:
/// the nodes it changed, never a version with its index), so revert makes the old versions again
/// and an apply after a revert the new ones - with the same nodes and the same identifiers.
///
/// A version somebody still holds (a test, a snapshot, a view) is set as the object it is: the
/// command remembers both versions by weak pointers, so undo and redo give the same objects
/// whenever that can be seen, and keep no version alive. A version nobody holds is made from the
/// current one by the difference; it has the number of the version it stands for
/// (PDFContentContainer::getVersion), by which the commands know the model they were applied to.
///
/// Threads and ownership: as PDFContentCommand; it co-owns what its differences keep (shared
/// pointers to immutable nodes) and the versions of the structure model.
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

        for (auto& item : m_containers)
        {
            const PDFContentContainerPointer before = getVersion(session, item.first, &item.second, false);
            Q_ASSERT(before);
            if (before)
            {
                session->setContainer(before);
            }
        }
        if (m_isStructureChanged)
        {
            const PDFContentStructureModelPointer before = getStructure(session, false);
            Q_ASSERT(before);
            if (before)
            {
                session->setStructure(before);
            }
        }
        session->setNodeIdCounter(m_counterBefore);
        setChange(m_revertChange);
    }

    virtual PDFInteger getPageIndex() const override { return m_pageIndex; }

    virtual size_t getStoredItemCount() const override
    {
        // The elements and the items of the parent tree the change touched, and what the
        // differences of the containers keep
        size_t count = m_structureDifference.getElementCount() + m_structureDifference.getParentTreeItemCount();
        for (const auto& item : m_containers)
        {
            count += item.second.difference.getStoredItemCount();
        }
        return count;
    }

    virtual std::vector<PDFObjectReference> getNewObjects() const override { return m_newObjects; }

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

        // The merged command leads from the first version of this command to the last version
        // of the next one. Its difference is made from those two versions: the last one is the
        // current version of the session, the one between is held by the session until the push
        // is over, the first one is made again from it (or is still held by somebody). Nothing
        // of this command changes, before every difference is made.
        std::map<PDFContentContainerKey, ContainerStep> merged;
        for (const auto& item : next.m_containers)
        {
            auto it = m_containers.find(item.first);
            if (it == m_containers.end() || it->second.difference.getVersionAfter() != item.second.difference.getVersionBefore())
            {
                return false;
            }

            const PDFContentContainerPointer last = item.second.after.lock();
            if (!last)
            {
                return false;
            }

            PDFContentContainerPointer middle = item.second.before.lock();
            if (!middle)
            {
                middle = item.second.difference.getBefore(last);
            }

            PDFContentContainerPointer first = it->second.before.lock();
            if (!first && middle)
            {
                first = it->second.difference.getBefore(middle);
            }

            if (!first)
            {
                return false;
            }

            ContainerStep step;
            step.difference = PDFContentContainerDifference::create(first, last);
            step.before = first;
            step.after = last;
            if (!step.difference.isValid())
            {
                return false;
            }

            merged[item.first] = std::move(step);
        }

        m_containers = std::move(merged);
        m_counterAfter = next.m_counterAfter;
        m_time = next.m_time;

        // The new objects of the two commands, which the merged command does not name, are the
        // objects of the version between (a move by the arrow keys copies the pattern of the
        // object with every press): nothing refers to them any more
        std::vector<PDFObjectReference> created = m_newObjects;
        created.insert(created.end(), next.m_newObjects.cbegin(), next.m_newObjects.cend());
        m_newObjects.clear();

        if (!created.empty())
        {
            std::set<PDFObjectReference> named;
            const bool isComplete = getNamedNewObjects(&named);
            for (const PDFObjectReference& reference : created)
            {
                if (!isComplete || named.count(reference))
                {
                    m_newObjects.push_back(reference);
                }
                else
                {
                    m_session->removeNewObject(reference);
                }
            }
        }

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
            // new versions are the ones computed then. A version is known by its number: the
            // object may be another one (made again by an undo), the content is the same - for
            // the containers and for the structure model (PDFContentStructureModel::getVersion,
            // kept by reverted / reapplied).
            const PDFContentStructureModelPointer currentStructure = session->getStructure();
            bool isSameModel = !m_isStructureChanged || (currentStructure && currentStructure->getVersion() == m_structureDifference.getVersionBefore());
            for (const auto& item : m_containers)
            {
                const PDFContentContainerPointer current = session->getContainer(item.first);
                isSameModel = isSameModel && current && current->getVersion() == item.second.difference.getVersionBefore();
            }

            // The versions after, all of them before the first one is set
            std::vector<PDFContentContainerPointer> versions;
            PDFContentStructureModelPointer structureAfter;
            if (isSameModel)
            {
                for (auto& item : m_containers)
                {
                    PDFContentContainerPointer after = getVersion(session, item.first, &item.second, true);
                    isSameModel = isSameModel && after;
                    versions.push_back(std::move(after));
                }

                if (m_isStructureChanged)
                {
                    structureAfter = getStructure(session, true);
                    isSameModel = isSameModel && structureAfter;
                }
            }

            if (isSameModel)
            {
                for (const PDFContentContainerPointer& after : versions)
                {
                    session->setContainer(after);
                }
                if (m_isStructureChanged)
                {
                    session->setStructure(structureAfter);
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
        const PDFInteger objectNumberBefore = session->getNextNewObjectNumber();

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

        // The command keeps the difference of every container, not its two versions: the new
        // version lives in the session, the old one as long as somebody else holds it
        m_containers.clear();
        for (const auto& item : result.containers)
        {
            ContainerStep step;
            step.difference = PDFContentContainerDifference::create(item.second.first, item.second.second);
            step.before = item.second.first;
            step.after = item.second.second;
            m_containers[item.first] = std::move(step);
        }

        // Of the structure the command keeps what the change touched (A3L1-4: never the two
        // models); the model after lives in the session
        m_structureBefore = result.structureBefore;
        m_structureAfter = result.structureAfter;
        m_structureDifference = std::move(result.structureDifference);
        m_isStructureChanged = result.isStructureChanged;
        m_counterBefore = counterBefore;
        m_counterAfter = session->getNodeIdCounter();
        m_applyChange = std::move(result.change);

        m_revertChange = m_applyChange;
        std::swap(m_revertChange.addedNodes, m_revertChange.removedNodes);

        // The new objects of the session the command has created (the session drops those of
        // a refused command itself)
        m_session = session;
        m_newObjects.clear();
        for (PDFInteger objectNumber = objectNumberBefore; objectNumber < session->getNextNewObjectNumber(); ++objectNumber)
        {
            m_newObjects.push_back(PDFObjectReference(objectNumber, PDFContentEditSession::NEW_OBJECT_GENERATION));
        }

        for (const auto& item : result.containers)
        {
            setPlace(session, item.first);
            session->setContainer(item.second.second);
        }
        if (m_isStructureChanged)
        {
            session->setStructure(result.structureAfter);
        }

        m_time = std::chrono::steady_clock::now();
        m_isComputed = true;
        setChange(m_applyChange);
        return true;
    }

    /// What the command keeps of one container
    struct ContainerStep
    {
        PDFContentContainerDifference difference;
        std::weak_ptr<const PDFContentContainer> before;    ///< The version before, as long as somebody holds it
        std::weak_ptr<const PDFContentContainer> after;     ///< The version after, as long as somebody holds it
    };

    /// The version of a container before or after the command: the object it was, when
    /// somebody still holds it, else made from the current version of the session by the
    /// difference (and remembered, as long as it is held)
    /// \returns nullptr, when the current version of the session is not the other version
    static PDFContentContainerPointer getVersion(const PDFContentEditSession* session, const PDFContentContainerKey& key, ContainerStep* step, bool isAfter)
    {
        std::weak_ptr<const PDFContentContainer>& version = isAfter ? step->after : step->before;

        PDFContentContainerPointer result = version.lock();
        if (!result)
        {
            const PDFContentContainerPointer current = session->getContainer(key);
            result = isAfter ? step->difference.getAfter(current) : step->difference.getBefore(current);
            version = result;
        }
        return result;
    }

    /// The structure model before or after the command: the object it was, when somebody still
    /// holds it, else made from the current model of the session by the difference (a copy of
    /// the model with the touched parts put back)
    /// \returns nullptr, when the session has no model, or one the difference is not of
    PDFContentStructureModelPointer getStructure(const PDFContentEditSession* session, bool isAfter)
    {
        std::weak_ptr<const PDFContentStructureModel>& version = isAfter ? m_structureAfter : m_structureBefore;

        PDFContentStructureModelPointer result = version.lock();
        if (!result)
        {
            const PDFContentStructureModelPointer current = session->getStructure();
            if (current)
            {
                result = isAfter ? current->reapplied(m_structureDifference) : current->reverted(m_structureDifference);
            }
            version = result;
        }
        return result;
    }

    /// The new objects of the session, which the differences of the command name: by a node or
    /// a resource entry they keep, or by another new object, which is named (a copy of
    /// a pattern may name another new object in its resources). One set of visited objects for
    /// the whole walk (a new object is entered once), a limit of the depth and of the number of
    /// the values looked at.
    /// \returns false, when the walk met a limit: the set is not complete then, and nothing must
    ///          be dropped by it
    bool getNamedNewObjects(std::set<PDFObjectReference>* result) const
    {
        std::set<PDFObjectReference>& named = *result;
        if (!m_session)
        {
            return false;
        }

        std::vector<PDFObject> objects;
        for (const auto& item : m_containers)
        {
            item.second.difference.addObjects(&objects);
        }

        std::vector<std::pair<PDFObject, int>> stack;
        for (PDFObject& object : objects)
        {
            stack.emplace_back(std::move(object), 0);
        }

        size_t valueCount = 0;
        while (!stack.empty())
        {
            const PDFObject object = std::move(stack.back().first);
            const int depth = stack.back().second;
            stack.pop_back();

            if (depth > Helper::MAXIMUM_NAMED_OBJECT_DEPTH || ++valueCount > Helper::MAXIMUM_NAMED_OBJECT_VALUES)
            {
                return false;
            }

            if (object.isReference())
            {
                const PDFObjectReference reference = object.getReference();
                if (reference.generation == PDFContentEditSession::NEW_OBJECT_GENERATION && named.insert(reference).second)
                {
                    const PDFObject newObject = m_session->getNewObject(reference);
                    if (!newObject.isNull())
                    {
                        stack.emplace_back(newObject, depth + 1);
                    }
                    else
                    {
                        // A reference of the document with the reserved generation: no new object
                        named.erase(reference);
                    }
                }
            }
            else if (object.isArray())
            {
                const PDFArray* array = object.getArray();
                for (size_t i = 0; i < array->getCount(); ++i)
                {
                    stack.emplace_back(array->getItem(i), depth + 1);
                }
            }
            else if (object.isDictionary() || object.isStream())
            {
                const PDFDictionary* dictionary = object.isDictionary() ? object.getDictionary() : object.getStream()->getDictionary();
                for (size_t i = 0; i < dictionary->getCount(); ++i)
                {
                    stack.emplace_back(dictionary->getValue(i), depth + 1);
                }
            }
        }

        return true;
    }

    std::map<PDFContentContainerKey, ContainerStep> m_containers;   ///< The difference of each changed container
    QPointer<PDFContentEditSession> m_session;                     ///< The session the command was applied to (for the new objects of a merge)
    std::vector<PDFObjectReference> m_newObjects;                   ///< The new objects of the session the command has created and still names
    std::weak_ptr<const PDFContentStructureModel> m_structureBefore;   ///< The model before, as long as somebody holds it
    std::weak_ptr<const PDFContentStructureModel> m_structureAfter;    ///< The model after, as long as somebody holds it
    PDFContentStructureDifference m_structureDifference;               ///< What the change touched in the model
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
    static constexpr const char* KEY_FILL_ENABLED = "fillEnabled";
    static constexpr const char* KEY_STROKE_ENABLED = "strokeEnabled";
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

    /// The place in front of an anchor of a parent takes content. Refused: the place behind
    /// an unclosed text, which adds to the clipping path (Work::isClipPendingBehind) - the model
    /// would close the text in front of the new content, and the clip of the text, which the
    /// content never makes real, would clip it. The tree is read as it is before the command
    /// (an unclosed node never moves).
    ///
    /// The tree builder marks such a text as an open end (PDFContentNode::isOpenEnd), which
    /// getAnchor never puts anything behind; the check is reached by a container, in which the
    /// text is not marked so (made by an editor, PDFContentContainerEditor::setNodeData /
    /// insertNode, and set into the session by a command).
    static bool checkPlace(const Work::Container* container, PDFContentNodeId parentId, PDFContentNodeId anchor, QString* errorText)
    {
        const PDFContentNodePointer parent = container->source->getNode(parentId);
        if (!parent)
        {
            return true;
        }

        const std::vector<PDFContentNodePointer>& children = parent->getChildren();
        PDFInteger index = PDFInteger(children.size());
        if (anchor.isValid())
        {
            const PDFContentNodePointer anchorNode = container->source->getNode(anchor);
            if (!anchorNode || anchorNode->getParentId() != parentId)
            {
                return true;
            }
            index = container->source->getIndexInParent(anchor);
        }

        if (index > 0 && index <= PDFInteger(children.size()) && Work::isClipPendingBehind(*container->source, children[size_t(index - 1)]))
        {
            *errorText = PDFTranslationContext::tr("The place is behind a text, which is not closed in the content and adds to the clipping path: an object placed there would be clipped by it.");
            return false;
        }

        return true;
    }

    /// The rest of a marked content from a child on gets a wrap of its own, right behind the
    /// marked content: a sequence of the structure with a new identifier, any other marked
    /// content with a copy of its operators
    /// \param mcid The identifier of the new piece of a sequence; no value for a marked content
    ///        without one
    /// \param pieceId The new wrap
    static bool splitSequence(Work& work, Work::Container* container, PDFContentNodeId sequenceId, PDFInteger firstIndex, std::optional<PDFInteger> mcid, PDFContentNodeId* pieceId, QString* errorText)
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

        // The object itself only: an object inside of an unclosed one is moved as any other
        if (container->source->isUnclosed(node->getId()))
        {
            *errorText = PDFTranslationContext::tr("The object is not closed in the content, so it cannot be moved.");
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
        return container->editor->getParentId(id) == parentId && Work::getIndex(container, id) + 1 == getAnchorIndex(container, parentId, anchor);
    }

    /// The node is painted at the place in front of the anchor already, though it does not
    /// stand there itself: it is the last content of the child right in front of the place, or
    /// (\p isInAnchorAllowed) the first content of the anchor. An order command has nothing to do
    /// with the node then.
    static bool isPaintedAtPlace(const Work::Container* container, PDFContentNodeId id, PDFContentNodeId parentId, PDFContentNodeId anchor, bool isInAnchorAllowed)
    {
        // The parent is read through the editor (its node would cost all its children for every
        // node of the command)
        const PDFInteger anchorIndex = getAnchorIndex(container, parentId, anchor);
        if (!container->editor->getNodeData(parentId) || anchorIndex < 0)
        {
            return false;
        }

        // The child of the parent, which holds the node
        PDFContentNodeId holderId;
        {
            int depth = 0;
            PDFContentNodeId current = id;
            while (container->editor->getNodeData(current) && container->editor->getParentId(current) != parentId && depth++ <= PDFContentContainer::MAXIMUM_DEPTH + 2)
            {
                current = container->editor->getParentId(current);
            }

            if (!container->editor->getNodeData(current) || container->editor->getParentId(current) != parentId || current == id)
            {
                return false;
            }
            holderId = current;
        }

        const PDFInteger count = Work::getChildCount(container, parentId);
        if (anchorIndex > 0 && anchorIndex <= count && container->editor->getChildId(parentId, anchorIndex - 1) == holderId)
        {
            return !hasContentBehind(container->editor->getNode(holderId), id);
        }

        if (isInAnchorAllowed && anchorIndex < count && container->editor->getChildId(parentId, anchorIndex) == holderId)
        {
            return !hasContentInFront(container->editor->getNode(holderId), id);
        }

        return false;
    }
};

}   // namespace pdf

#endif // PDFCONTENTCOMMANDENGINE_H
