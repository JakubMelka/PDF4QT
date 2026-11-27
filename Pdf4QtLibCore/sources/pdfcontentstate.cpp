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

// Edit Content, stage E0, package P02 (tree and state): the resolved and the symbolic state of a
// node (GUI_overhaul/edit_tool/zadani_jadro.md 4.2, 4.3).

#include "pdfcontentstate.h"

#include <QByteArrayView>

#include <memory>
#include <utility>

namespace pdf
{

/// The comparison of the objects of a graphics state (the functions of black generation,
/// undercolor removal and transfer, the halftone), which a state of the processor holds
/// dereferenced. PDFObject::operator== compares arrays, dictionaries and the data of streams
/// down to their last value; the builder compares the state after every operation, so the
/// cost of one comparison must not depend on a size the document chooses.
///
/// Threads and ownership: static functions without a state.
class PDFContentStateHelper
{
public:
    /// The values (items, entries, nested ones included) of two different objects compared at most
    static constexpr int MAXIMUM_COMPARED_VALUES = 64;

    /// Two different strings or names longer than this are not compared
    static constexpr qsizetype MAXIMUM_COMPARED_STRING_LENGTH = 256;

    /// Direct objects nested deeper are not compared
    static constexpr int MAXIMUM_COMPARED_DEPTH = 8;

    /// Two objects have the same value, as far as it is known cheaply: the same object of the
    /// document (shared content) is equal at once; two different objects are compared by value
    /// within the limits above and are different beyond them.
    static bool isObjectEqual(const PDFObject& left, const PDFObject& right)
    {
        int budget = MAXIMUM_COMPARED_VALUES;
        return isObjectEqual(left, right, 0, budget);
    }

    /// Two byte arrays are equal; the same shared bytes are not read (a state and its copy share
    /// the name of the rendering intent, whose length the content chooses)
    static bool isBytesEqual(const QByteArray& left, const QByteArray& right)
    {
        return left.size() == right.size() && (left.constData() == right.constData() || left == right);
    }

private:
    /// The texts of two different strings (or of two keys) are equal; texts longer than the
    /// limit are not compared and count as different
    static bool isTextEqual(QByteArrayView left, QByteArrayView right)
    {
        return left.size() == right.size() && left.size() <= MAXIMUM_COMPARED_STRING_LENGTH && left == right;
    }

    static bool isObjectEqual(const PDFObject& left, const PDFObject& right, int depth, int& budget)
    {
        const PDFObject::Type type = left.getType();
        if (type != right.getType())
        {
            return false;
        }

        if (--budget < 0)
        {
            return false;
        }

        switch (type)
        {
            case PDFObject::Type::Null:
                return true;

            case PDFObject::Type::Bool:
            case PDFObject::Type::Int:
            case PDFObject::Type::Real:
            case PDFObject::Type::Reference:
                // Plain values
                return left == right;

            case PDFObject::Type::String:
            case PDFObject::Type::Name:
            {
                // A short string is stored in the object itself, a long one in the heap (and
                // a string of one kind never equals a string of the other, PDFInplaceOrMemoryString)
                const PDFStringRef leftString = left.getStringObject();
                const PDFStringRef rightString = right.getStringObject();
                if (leftString.memoryString && rightString.memoryString)
                {
                    return leftString.memoryString == rightString.memoryString ||
                           isTextEqual(QByteArrayView(leftString.memoryString->getString()), QByteArrayView(rightString.memoryString->getString()));
                }

                return !leftString.memoryString && !rightString.memoryString && left == right;
            }

            case PDFObject::Type::Array:
            {
                const PDFArray* leftArray = left.getArray();
                const PDFArray* rightArray = right.getArray();
                if (leftArray == rightArray)
                {
                    return true;
                }

                const size_t count = leftArray->getCount();
                if (count != rightArray->getCount() || depth >= MAXIMUM_COMPARED_DEPTH || count > size_t(budget))
                {
                    return false;
                }

                for (size_t i = 0; i < count; ++i)
                {
                    if (!isObjectEqual(leftArray->getItem(i), rightArray->getItem(i), depth + 1, budget))
                    {
                        return false;
                    }
                }

                return true;
            }

            case PDFObject::Type::Dictionary:
            {
                const PDFDictionary* leftDictionary = left.getDictionary();
                const PDFDictionary* rightDictionary = right.getDictionary();
                if (leftDictionary == rightDictionary)
                {
                    return true;
                }

                // The entries in the same order: two dictionaries, which differ by their order
                // only, count as different
                const size_t count = leftDictionary->getCount();
                if (count != rightDictionary->getCount() || depth >= MAXIMUM_COMPARED_DEPTH || count > size_t(budget))
                {
                    return false;
                }

                for (size_t i = 0; i < count; ++i)
                {
                    if (!isTextEqual(leftDictionary->getKey(i).getView(), rightDictionary->getKey(i).getView()) ||
                        !isObjectEqual(leftDictionary->getValue(i), rightDictionary->getValue(i), depth + 1, budget))
                    {
                        return false;
                    }
                }

                return true;
            }

            case PDFObject::Type::Stream:
                // The data of two streams are never compared
                return left.getStream() == right.getStream();

            default:
                return false;
        }
    }
};

bool PDFContentStateValue::isEqual(const PDFContentStateValue& left, const PDFContentStateValue& right)
{
    if (left.kind != right.kind)
    {
        return false;
    }

    switch (left.kind)
    {
        case Kind::None:
            return true;

        case Kind::Real:
            return left.real == right.real;

        case Kind::Integer:
            return left.integer == right.integer;

        case Kind::Boolean:
            return left.boolean == right.boolean;

        case Kind::Name:
            return left.name == right.name;

        case Kind::Dash:
            return left.dash == right.dash;

        case Kind::Color:
            return left.color.deviceSpace == right.color.deviceSpace &&
                   left.color.components == right.color.components &&
                   PDFContentStateHelper::isObjectEqual(left.color.colorSpaceObject, right.color.colorSpaceObject) &&
                   PDFContentStateHelper::isObjectEqual(left.color.patternObject, right.color.patternObject);

        case Kind::Font:
            return left.real == right.real && PDFContentStateHelper::isObjectEqual(left.object, right.object);

        case Kind::Object:
            return left.matrix == right.matrix && PDFContentStateHelper::isObjectEqual(left.object, right.object);
    }

    return false;
}

PDFContentResolvedState::PDFContentResolvedState(const PDFPageContentProcessorState& state) :
    m_state(state),
    m_hasSoftMask(state.getSoftMask() != nullptr)
{
    // The soft mask dictionary of the processor state is a raw pointer into the document: the
    // resolved state must not point there (the mask is an owned object of the symbolic state)
    m_state.setSoftMask(nullptr);

    // A QTransform computes its type lazily, also in a const object: it is computed here once,
    // so the matrices of the shared state are plain data for the threads, which read them
    m_state.getCurrentTransformationMatrix().type();
    m_state.getTextMatrix().type();
    m_state.getTextLineMatrix().type();
}

PDFContentStatePropertySet PDFContentResolvedState::getDifference(const PDFContentResolvedState& left, const PDFContentResolvedState& right)
{
    PDFContentStatePropertySet result = getDifference(left.getState(), right.getState());

    // A resolved state knows only, whether a soft mask is set
    if (left.hasSoftMask() != right.hasSoftMask())
    {
        result.insert(PDFContentStateProperty::SoftMask);
    }

    return result;
}

PDFContentStatePropertySet PDFContentResolvedState::getDifference(const PDFPageContentProcessorState& left, const PDFPageContentProcessorState& right)
{
    using Property = PDFContentStateProperty;

    PDFContentStatePropertySet result;

    auto compare = [&result](Property property, bool isSame)
    {
        if (!isSame)
        {
            result.insert(property);
        }
    };

    compare(Property::LineWidth, left.getLineWidth() == right.getLineWidth());
    compare(Property::LineCap, left.getLineCapStyle() == right.getLineCapStyle());
    compare(Property::LineJoin, left.getLineJoinStyle() == right.getLineJoinStyle());
    compare(Property::MiterLimit, left.getMitterLimit() == right.getMitterLimit());
    compare(Property::DashPattern, left.getLineDashPattern() == right.getLineDashPattern());
    compare(Property::RenderingIntent, PDFContentStateHelper::isBytesEqual(left.getRenderingIntentName(), right.getRenderingIntentName()) && left.getRenderingIntent() == right.getRenderingIntent());
    compare(Property::Flatness, left.getFlatness() == right.getFlatness());
    compare(Property::StrokeColor, left.getStrokeColorSpace() == right.getStrokeColorSpace() &&
                                   left.getStrokeColorOriginal() == right.getStrokeColorOriginal() &&
                                   left.getStrokeColor() == right.getStrokeColor());
    compare(Property::FillColor, left.getFillColorSpace() == right.getFillColorSpace() &&
                                 left.getFillColorOriginal() == right.getFillColorOriginal() &&
                                 left.getFillColor() == right.getFillColor());
    compare(Property::Font, left.getTextFont() == right.getTextFont() && left.getTextFontSize() == right.getTextFontSize());
    compare(Property::CharacterSpacing, left.getTextCharacterSpacing() == right.getTextCharacterSpacing());
    compare(Property::WordSpacing, left.getTextWordSpacing() == right.getTextWordSpacing());
    compare(Property::HorizontalScaling, left.getTextHorizontalScaling() == right.getTextHorizontalScaling());
    compare(Property::Leading, left.getTextLeading() == right.getTextLeading());
    compare(Property::TextRenderingMode, left.getTextRenderingMode() == right.getTextRenderingMode());
    compare(Property::TextRise, left.getTextRise() == right.getTextRise());
    compare(Property::TextKnockout, left.getTextKnockout() == right.getTextKnockout());
    compare(Property::AlphaStroking, left.getAlphaStroking() == right.getAlphaStroking());
    compare(Property::AlphaFilling, left.getAlphaFilling() == right.getAlphaFilling());
    compare(Property::BlendMode, left.getBlendMode() == right.getBlendMode());
    compare(Property::AlphaIsShape, left.getAlphaIsShape() == right.getAlphaIsShape());
    compare(Property::StrokeAdjustment, left.getStrokeAdjustment() == right.getStrokeAdjustment());
    compare(Property::OverprintStroking, left.getOverprintMode().overprintStroking == right.getOverprintMode().overprintStroking);
    compare(Property::OverprintFilling, left.getOverprintMode().overprintFilling == right.getOverprintMode().overprintFilling);
    compare(Property::OverprintMode, left.getOverprintMode().overprintMode == right.getOverprintMode().overprintMode);
    compare(Property::BlackGeneration, PDFContentStateHelper::isObjectEqual(left.getBlackGenerationFunction(), right.getBlackGenerationFunction()));
    compare(Property::UndercolorRemoval, PDFContentStateHelper::isObjectEqual(left.getUndercolorRemovalFunction(), right.getUndercolorRemovalFunction()));
    compare(Property::Transfer, PDFContentStateHelper::isObjectEqual(left.getTransferFunction(), right.getTransferFunction()));
    compare(Property::Halftone, PDFContentStateHelper::isObjectEqual(left.getHalftone(), right.getHalftone()));
    compare(Property::HalftoneOrigin, left.getHalftoneOrigin() == right.getHalftoneOrigin());
    compare(Property::Smoothness, left.getSmoothness() == right.getSmoothness());
    compare(Property::BlackPointCompensation, left.getBlackPointCompensationMode() == right.getBlackPointCompensationMode());

    return result;
}

const PDFContentPropertySource& PDFContentSymbolicState::getSource(PDFContentStateProperty property) const
{
    // The source of every property, which was never set: one immutable value for all states
    // (initialized once, by the rules of a local static also when more threads come first)
    static const PDFContentPropertySource inheritedSource;

    const size_t index = size_t(property);
    if (index < m_sources.size() && m_sources[index])
    {
        return *m_sources[index];
    }

    return inheritedSource;
}

PDFContentSymbolicState PDFContentSymbolicState::withSource(PDFContentStateProperty property, PDFContentPropertySource source) const
{
    PDFContentSymbolicState result = *this;

    const size_t index = size_t(property);
    if (index < result.m_sources.size())
    {
        if (source.origin == PDFContentPropertySource::Origin::Inherited)
        {
            // An inherited property has no source of its own: getSource gives the default one,
            // whatever else the caller has filled in
            result.m_sources[index].reset();
        }
        else
        {
            result.m_sources[index] = std::make_shared<const PDFContentPropertySource>(std::move(source));
        }
    }

    return result;
}

PDFContentSymbolicState PDFContentSymbolicState::withSoftMaskMatrix(const QTransform& matrix) const
{
    PDFContentSymbolicState result = *this;
    result.m_softMaskMatrix = matrix;
    result.m_softMaskMatrix.type();     // Computed once, see PDFContentResolvedState
    return result;
}

}   // namespace pdf
