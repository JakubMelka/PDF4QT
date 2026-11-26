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

// Edit Content, stage E0, package P01 (operations, resources, stream writer, ExtGState).
// Plan: GUI_overhaul/edit_tool/zadani_jadro.md 4.4 (base rule, R-1 wrap, R-6, R-7, canonical
// writing). Standard: ISO 32000-2, 7.2.3 (white space, delimiters), 7.8.2 (content streams),
// 8.4.4 (graphics state operators), 8.4.5 (ExtGState), 8.5.2 - 8.5.4 (paths), 8.6.8 (color
// operators), 9.3 (text state), 11.6.5.1 (the space of a soft mask).

#include "pdfcontentstreamwriter.h"
#include "pdfcontentextgstate.h"
#include "pdfcontentstreamtext.h"

#include <QtGlobal>

#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include "pdfdbgheap.h"

namespace pdf
{

/// One run of PDFContentStreamWriter::write: the bytes written so far, the resources with the
/// names added for the overrides and the errors of the nodes.
///
/// Threads and ownership: a local object of one call; it reads the immutable container, which
/// its caller keeps alive for the call, and owns its result.
class PDFContentStreamWriterHelper
{
public:
    explicit PDFContentStreamWriterHelper(const PDFContentContainer& container) :
        m_container(container),
        m_operations(container.getOperations())
    {
        m_result.resources = container.getResources();
    }

    PDFContentStreamWriteResult run();

    /// A number of the canonical text; false for a value, which is not finite
    static bool appendNumber(QByteArray* text, PDFReal value)
    {
        if (!PDFContentStreamText::isNumberWritable(value))
        {
            return false;
        }

        if (!text->isEmpty())
        {
            text->append(' ');
        }
        text->append(PDFContentStreamText::formatNumber(value));
        return true;
    }

    /// The six numbers of a matrix "a b c d e f"; false for a matrix, which is not affine or has
    /// a value, which is not finite
    static bool formatMatrix(const QTransform& matrix, QByteArray* text)
    {
        if (!matrix.isAffine())
        {
            return false;
        }

        text->clear();
        return appendNumber(text, matrix.m11()) && appendNumber(text, matrix.m12()) &&
               appendNumber(text, matrix.m21()) && appendNumber(text, matrix.m22()) &&
               appendNumber(text, matrix.dx()) && appendNumber(text, matrix.dy());
    }

    /// The inverse of an affine matrix. QTransform::inverted refuses a matrix with a small
    /// determinant (a fuzzy comparison with zero), which a transformation matrix of a content
    /// may have by right (two scales of 0.000001).
    static bool invertMatrix(const QTransform& matrix, QTransform* inverse)
    {
        const PDFReal determinant = matrix.m11() * matrix.m22() - matrix.m12() * matrix.m21();
        if (!matrix.isAffine() || !std::isfinite(determinant) || determinant == 0.0)
        {
            return false;
        }

        const PDFReal a = matrix.m22() / determinant;
        const PDFReal b = -matrix.m12() / determinant;
        const PDFReal c = -matrix.m21() / determinant;
        const PDFReal d = matrix.m11() / determinant;
        const PDFReal e = -(matrix.dx() * a + matrix.dy() * c);
        const PDFReal f = -(matrix.dx() * b + matrix.dy() * d);

        *inverse = QTransform(a, b, c, d, e, f);
        return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && std::isfinite(d) && std::isfinite(e) && std::isfinite(f);
    }

    /// Two matrices are the same in the precision of the written numbers
    static bool isSameMatrix(const QTransform& left, const QTransform& right)
    {
        return isSameNumber(left.m11(), right.m11()) && isSameNumber(left.m12(), right.m12()) &&
               isSameNumber(left.m21(), right.m21()) && isSameNumber(left.m22(), right.m22()) &&
               isSameNumber(left.dx(), right.dx()) && isSameNumber(left.dy(), right.dy()) &&
               left.isAffine() && right.isAffine();
    }

    static bool isSameNumber(PDFReal left, PDFReal right)
    {
        // Nine significant digits are written (PDFContentStreamText::MAXIMUM_SIGNIFICANT_DIGITS)
        return std::abs(left - right) <= 1e-9 * qMax(PDFReal(1.0), qMax(std::abs(left), std::abs(right)));
    }

    static bool isEndOfLine(char character)
    {
        return character == '\n' || character == '\r';
    }

private:
    /// A node being written. The walk over the tree keeps its own stack: the builder and the
    /// container editor limit the depth (PDFContentContainer::MAXIMUM_DEPTH), a tree made
    /// through the constructors of the nodes does not have to keep it.
    struct Frame
    {
        const PDFContentNode* node = nullptr;
        size_t nextChild = 0;
        bool isEntered = false;
        bool isWrapped = false;     ///< "Q" of a wrap follows the node
        QByteArray trailer;         ///< Canonical text written after the node and its wrap (the clipping path of a split path)
    };

    enum class LastPiece
    {
        None,       ///< Nothing written yet
        Original,   ///< An operation at its place of the original content
        Text        ///< Canonical text, or an operation, which is at no place of the original content
    };

    void addError(const PDFContentNode* node, const QString& text);

    void appendOriginalOperation(size_t index, const PDFContentOperation& operation);
    void appendText(const QByteArray& text);
    void appendTail();
    void appendSpan(const PDFContentNode* node, const PDFContentOperationSpan& span);

    void enterNode(Frame* frame);
    void leaveNode(const Frame& frame);

    /// The body of a leaf: its canonical text, when it has one (a modified path), else its operations
    void appendBody(const PDFContentNode* node);
    void appendPath(const PDFContentNode* node, const PDFContentPath& path, const PDFContentPathPaint& paint);

    /// The operators of an override: the matrix, the soft mask, the ExtGState, the colors, the
    /// line parameters, the text state
    void appendOverride(const PDFContentNode* node, const PDFContentStateOverride& override);
    void appendSoftMask(const PDFContentNode* node, const PDFContentStateOverride& override, const PDFContentStateValue& value);
    void appendExtGState(const PDFContentNode* node, const PDFObject& dictionary);
    void appendColor(const PDFContentNode* node, const PDFContentStateValue& value, bool isStroking);
    void appendOperatorProperty(const PDFContentNode* node, PDFContentStateProperty property, const PDFContentStateValue& value);

    /// The current transformation matrix at the beginning of a node (identity without a state)
    static QTransform getNodeMatrix(const PDFContentNode* node)
    {
        return node->getState() ? node->getState()->getCurrentTransformationMatrix() : QTransform();
    }

    const PDFContentContainer& m_container;
    const PDFContentOperationList& m_operations;
    PDFContentStreamWriteResult m_result;
    QByteArray m_content;
    LastPiece m_lastPiece = LastPiece::None;
    size_t m_lastOriginalIndex = 0;
    bool m_isLastInlineImage = false;

    /// The ExtGState entries of m_result.resources by their key of equality
    /// (PDFContentExtGStateWriter::getEqualityKey): the name of the first entry of each key. Made
    /// at the first ExtGState of the writing, then each added entry is added here too.
    std::map<QByteArray, QByteArray> m_extGStateNames;
    bool m_isExtGStateIndexed = false;
};

void PDFContentStreamWriterHelper::addError(const PDFContentNode* node, const QString& text)
{
    PDFContentWriteError error;
    if (node)
    {
        error.nodeId = node->getId();
    }
    error.text = text;
    m_result.errors.push_back(std::move(error));
}

void PDFContentStreamWriterHelper::appendOriginalOperation(size_t index, const PDFContentOperation& operation)
{
    const QByteArray& bytes = operation.getRawBytes();

    switch (m_lastPiece)
    {
        case LastPiece::None:
            break;

        case LastPiece::Original:
        {
            if (index == m_lastOriginalIndex + 1)
            {
                // Neighbors of the original content: joined as they were
                break;
            }

            // R-7: two regular characters must not meet. The end of an inline image is found
            // by readers as EI followed by a white space, so it gets one too.
            if (PDFContentStreamText::isSeparatorNeeded(m_content, bytes) ||
                (m_isLastInlineImage && !bytes.isEmpty() && !PDFLexicalAnalyzer::isWhitespace(bytes.front())))
            {
                m_content.append('\n');
            }
            break;
        }

        case LastPiece::Text:
        {
            // Canonical text is one operator a line
            if (!bytes.isEmpty() && !isEndOfLine(bytes.front()))
            {
                m_content.append('\n');
            }
            break;
        }
    }

    m_content.append(bytes);
    m_lastPiece = LastPiece::Original;
    m_lastOriginalIndex = index;
    m_isLastInlineImage = operation.getOperator() == PDFContentOperator::InlineImage;
}

void PDFContentStreamWriterHelper::appendText(const QByteArray& text)
{
    if (text.isEmpty())
    {
        return;
    }

    if (!m_content.isEmpty() && !isEndOfLine(m_content.back()) && !isEndOfLine(text.front()))
    {
        m_content.append('\n');
    }

    m_content.append(text);
    m_lastPiece = LastPiece::Text;
    m_isLastInlineImage = false;
}

void PDFContentStreamWriterHelper::appendTail()
{
    const QByteArray& tail = m_operations.getTail();
    if (tail.isEmpty())
    {
        return;
    }

    switch (m_lastPiece)
    {
        case LastPiece::None:
            break;

        case LastPiece::Original:
        {
            if (m_lastOriginalIndex + 1 == m_operations.getOriginalCount())
            {
                // The tail follows the last original operation as it did
                break;
            }

            if (PDFContentStreamText::isSeparatorNeeded(m_content, tail) ||
                (m_isLastInlineImage && !PDFLexicalAnalyzer::isWhitespace(tail.front())))
            {
                m_content.append('\n');
            }
            break;
        }

        case LastPiece::Text:
        {
            if (!isEndOfLine(m_content.back()) && !isEndOfLine(tail.front()))
            {
                m_content.append('\n');
            }
            break;
        }
    }

    // Nothing is written after the tail: it may end inside of a comment or of a string
    m_content.append(tail);
}

void PDFContentStreamWriterHelper::appendSpan(const PDFContentNode* node, const PDFContentOperationSpan& span)
{
    const size_t count = m_operations.getCount();
    if (span.first > count || span.count > count - span.first)
    {
        addError(node, PDFTranslationContext::tr("The object refers to operations, which the content does not have."));
        return;
    }

    const size_t originalCount = m_operations.getOriginalCount();
    for (size_t index = span.first; index < span.first + span.count; ++index)
    {
        const PDFContentOperation& operation = m_operations.getOperation(index);
        const PDFContentOperation::Flags flags = operation.getFlags();

        if (flags.testFlag(PDFContentOperation::Suppressed))
        {
            // R-1: the editor overrides the property the operation sets
            continue;
        }

        if (index < originalCount && !flags.testFlag(PDFContentOperation::Generated))
        {
            appendOriginalOperation(index, operation);
        }
        else
        {
            appendText(operation.getRawBytes());
        }
    }
}

void PDFContentStreamWriterHelper::appendPath(const PDFContentNode* node, const PDFContentPath& path, const PDFContentPathPaint& paint)
{
    QString errorText;
    const QByteArray pathText = PDFContentStreamWriter::writePath(path, &errorText);
    if (!errorText.isEmpty())
    {
        addError(node, errorText);
        return;
    }

    appendText(pathText);
    appendText(PDFContentStreamWriter::writePathPaint(paint));
}

void PDFContentStreamWriterHelper::appendBody(const PDFContentNode* node)
{
    const PDFContentNode::Data& data = node->getData();
    if (data.kind == PDFContentNodeKind::Path && data.isModified)
    {
        // The geometry of the node was changed (or the node is new): its operations, if it has
        // any, are replaced by the canonical text of its path
        appendPath(node, data.path, data.paint);
        return;
    }

    appendSpan(node, data.prefix);
}

void PDFContentStreamWriterHelper::appendExtGState(const PDFContentNode* node, const PDFObject& dictionary)
{
    if (!dictionary.isDictionary())
    {
        addError(node, PDFTranslationContext::tr("A value of the graphics state of the object cannot be written."));
        return;
    }

    // An equal dictionary is shared inside of the container (the order of its keys plays no role):
    // the first entry, which isEqual calls equal, in the order of the entries. The entries are
    // indexed by their key of equality once per writing; an entry, which has no key, is equal
    // to nothing (a reference, a NaN).
    if (!m_isExtGStateIndexed)
    {
        m_isExtGStateIndexed = true;
        for (const PDFContentResources::Entry& entry : m_result.resources.getEntries(PDFContentResourceCategory::ExtGState))
        {
            QByteArray key = PDFContentExtGStateWriter::getEqualityKey(entry.object);
            if (!key.isEmpty())
            {
                // The first entry of a key stays
                m_extGStateNames.emplace(std::move(key), entry.name);
            }
        }
    }

    QByteArray key = PDFContentExtGStateWriter::getEqualityKey(dictionary);
    if (!key.isEmpty())
    {
        auto it = m_extGStateNames.find(key);
        if (it != m_extGStateNames.cend())
        {
            appendText(PDFContentStreamText::formatName(it->second) + " gs");
            return;
        }
    }

    // A new entry is the last one: of its key, there was none before it
    const QByteArray name = m_result.resources.addObject(PDFContentResourceCategory::ExtGState, QByteArrayLiteral("GS"), dictionary);
    if (!key.isEmpty())
    {
        m_extGStateNames.emplace(std::move(key), name);
    }
    appendText(PDFContentStreamText::formatName(name) + " gs");
}

void PDFContentStreamWriterHelper::appendSoftMask(const PDFContentNode* node, const PDFContentStateOverride& override, const PDFContentStateValue& value)
{
    const bool isNone = (value.kind == PDFContentStateValue::Kind::Name && value.name == "None") ||
                        (value.kind == PDFContentStateValue::Kind::Object && (value.object.isNull() || (value.object.isName() && value.object.getString() == "None")));
    if (isNone)
    {
        // No mask has no space
        appendExtGState(node, PDFContentExtGStateWriter::createSoftMaskDictionary(PDFObject()));
        return;
    }

    if (value.kind != PDFContentStateValue::Kind::Object)
    {
        addError(node, PDFTranslationContext::tr("The soft mask of the object cannot be written."));
        return;
    }

    // R-6, ISO 32000-2, 11.6.5.1: the space of a mask is the matrix of its group x the current
    // transformation matrix of the moment gs sets it. The value says, which matrix that has to
    // be (value.matrix); at this place of the wrap the matrix is override.transform x the
    // matrix of the node. So "X cm gs X^-1 cm" with X x (transform x matrix) = value.matrix.
    const QTransform currentMatrix = override.transform * getNodeMatrix(node);
    const PDFObject dictionary = PDFContentExtGStateWriter::createSoftMaskDictionary(value.object);

    if (isSameMatrix(value.matrix, currentMatrix))
    {
        appendExtGState(node, dictionary);
        return;
    }

    QTransform inverseCurrentMatrix;
    QTransform maskMatrix;
    QTransform inverseMaskMatrix;
    QByteArray maskMatrixText;
    QByteArray inverseMaskMatrixText;

    bool isPlaced = invertMatrix(currentMatrix, &inverseCurrentMatrix);
    if (isPlaced)
    {
        maskMatrix = value.matrix * inverseCurrentMatrix;
        isPlaced = invertMatrix(maskMatrix, &inverseMaskMatrix) &&
                   formatMatrix(maskMatrix, &maskMatrixText) &&
                   formatMatrix(inverseMaskMatrix, &inverseMaskMatrixText);
    }

    if (!isPlaced)
    {
        addError(node, PDFTranslationContext::tr("The soft mask of the object cannot be placed, because a transformation matrix cannot be inverted."));
        return;
    }

    appendText(maskMatrixText + " cm");
    appendExtGState(node, dictionary);
    appendText(inverseMaskMatrixText + " cm");
}

void PDFContentStreamWriterHelper::appendColor(const PDFContentNode* node, const PDFContentStateValue& value, bool isStroking)
{
    if (value.kind != PDFContentStateValue::Kind::Color)
    {
        addError(node, PDFTranslationContext::tr("The color of the object cannot be written."));
        return;
    }

    const PDFContentColorValue& color = value.color;

    QByteArray components;
    for (const PDFReal component : color.components)
    {
        if (!appendNumber(&components, component))
        {
            addError(node, PDFTranslationContext::tr("A component of the color of the object is not a finite number."));
            return;
        }
    }

    if (!color.deviceSpace.isEmpty())
    {
        // ISO 32000-2, 8.6.8: g, rg, k set the device space and the color at once
        size_t componentCount = 0;
        const char* colorOperator = nullptr;
        if (color.deviceSpace == "DeviceGray")
        {
            componentCount = 1;
            colorOperator = isStroking ? "G" : "g";
        }
        else if (color.deviceSpace == "DeviceRGB")
        {
            componentCount = 3;
            colorOperator = isStroking ? "RG" : "rg";
        }
        else if (color.deviceSpace == "DeviceCMYK")
        {
            componentCount = 4;
            colorOperator = isStroking ? "K" : "k";
        }

        if (!colorOperator || color.components.size() != componentCount || color.isPattern())
        {
            addError(node, PDFTranslationContext::tr("The color of the object does not fit its color space."));
            return;
        }

        appendText(components + ' ' + colorOperator);
        return;
    }

    QByteArray colorSpaceName;
    if (color.colorSpaceObject.isNull())
    {
        if (!color.isPattern())
        {
            addError(node, PDFTranslationContext::tr("The color of the object has no color space."));
            return;
        }

        // A colored pattern: the Pattern space without an underlying space
        colorSpaceName = QByteArrayLiteral("Pattern");
    }
    else if (color.colorSpaceObject.isName())
    {
        // The name of a space without parameters (DeviceGray, DeviceRGB, DeviceCMYK, Pattern) is
        // an operand of cs itself; these four names never name a resource (8.6.8)
        colorSpaceName = color.colorSpaceObject.getString();
    }
    else
    {
        // The space of the document by its reference (or an equal direct object) under a name
        // of the container; the editor creates no color space objects
        colorSpaceName = m_result.resources.addObject(PDFContentResourceCategory::ColorSpace, QByteArrayLiteral("CS"), color.colorSpaceObject);
    }

    appendText(PDFContentStreamText::formatName(colorSpaceName) + (isStroking ? " CS" : " cs"));

    if (color.isPattern())
    {
        const QByteArray patternName = m_result.resources.addObject(PDFContentResourceCategory::Pattern, QByteArrayLiteral("P"), color.patternObject);
        if (!components.isEmpty())
        {
            components.append(' ');
        }
        components.append(PDFContentStreamText::formatName(patternName));
    }

    if (!components.isEmpty())
    {
        // scn / SCN take every space sc / SC take, and the patterns (8.6.8)
        appendText(components + (isStroking ? " SCN" : " scn"));
    }
}

void PDFContentStreamWriterHelper::appendOperatorProperty(const PDFContentNode* node, PDFContentStateProperty property, const PDFContentStateValue& value)
{
    using Kind = PDFContentStateValue::Kind;

    // ISO 32000-2, 8.4.4 (w J j M d ri i), 9.3 (Tc Tw Tz TL Tf Tr Ts)
    const char* realOperator = nullptr;
    const char* integerOperator = nullptr;

    switch (property)
    {
        case PDFContentStateProperty::LineWidth:
            realOperator = "w";
            break;

        case PDFContentStateProperty::LineCap:
            integerOperator = "J";
            break;

        case PDFContentStateProperty::LineJoin:
            integerOperator = "j";
            break;

        case PDFContentStateProperty::MiterLimit:
            realOperator = "M";
            break;

        case PDFContentStateProperty::Flatness:
            realOperator = "i";
            break;

        case PDFContentStateProperty::CharacterSpacing:
            realOperator = "Tc";
            break;

        case PDFContentStateProperty::WordSpacing:
            realOperator = "Tw";
            break;

        case PDFContentStateProperty::HorizontalScaling:
            realOperator = "Tz";
            break;

        case PDFContentStateProperty::Leading:
            realOperator = "TL";
            break;

        case PDFContentStateProperty::TextRenderingMode:
            integerOperator = "Tr";
            break;

        case PDFContentStateProperty::TextRise:
            realOperator = "Ts";
            break;

        default:
            break;
    }

    QByteArray text;
    bool isWritten = false;

    if (realOperator)
    {
        isWritten = value.kind == Kind::Real && appendNumber(&text, value.real);
        text.append(' ').append(realOperator);
    }
    else if (integerOperator)
    {
        isWritten = value.kind == Kind::Integer;
        text = QByteArray::number(value.integer) + ' ' + integerOperator;
    }
    else if (property == PDFContentStateProperty::DashPattern)
    {
        isWritten = value.kind == Kind::Dash;

        QByteArray dashArray;
        for (const PDFReal dash : value.dash.getDashArray())
        {
            isWritten = isWritten && appendNumber(&dashArray, dash);
        }

        QByteArray dashOffset;
        isWritten = isWritten && appendNumber(&dashOffset, value.dash.getDashOffset());
        text = '[' + dashArray + "] " + dashOffset + " d";
    }
    else if (property == PDFContentStateProperty::RenderingIntent)
    {
        isWritten = value.kind == Kind::Name && !value.name.isEmpty();
        text = PDFContentStreamText::formatName(value.name) + " ri";
    }
    else if (property == PDFContentStateProperty::Font)
    {
        QByteArray size;
        isWritten = value.kind == Kind::Font && !value.object.isNull() && appendNumber(&size, value.real);
        if (isWritten)
        {
            const QByteArray fontName = m_result.resources.addObject(PDFContentResourceCategory::Font, QByteArrayLiteral("F"), value.object);
            text = PDFContentStreamText::formatName(fontName) + ' ' + size + " Tf";
        }
    }

    if (!isWritten)
    {
        addError(node, PDFTranslationContext::tr("A value of the graphics state of the object cannot be written."));
        return;
    }

    appendText(text);
}

void PDFContentStreamWriterHelper::appendOverride(const PDFContentNode* node, const PDFContentStateOverride& override)
{
    // 1. The matrix
    if (!override.transform.isIdentity())
    {
        QTransform inverse;
        if (!invertMatrix(override.transform, &inverse))
        {
            addError(node, PDFTranslationContext::tr("The transformation of the object is not a regular matrix of finite numbers."));
            return;
        }

        QString errorText;
        const QByteArray matrixText = PDFContentStreamWriter::writeMatrix(override.transform, &errorText);
        if (!errorText.isEmpty())
        {
            addError(node, errorText);
            return;
        }

        appendText(matrixText);
    }

    // 2. The soft mask, in its own space and by its own dictionary (R-6)
    auto softMaskIt = override.properties.find(PDFContentStateProperty::SoftMask);
    if (softMaskIt != override.properties.cend())
    {
        appendSoftMask(node, override, softMaskIt->second);
    }

    // 3. Everything else without an operator of its own: one ExtGState with exactly these keys
    std::map<PDFContentStateProperty, PDFContentStateValue> extGStateProperties;
    for (const auto& [property, value] : override.properties)
    {
        if (property != PDFContentStateProperty::SoftMask && PDFContentExtGStateWriter::isExtGStateProperty(property))
        {
            extGStateProperties[property] = value;
        }
    }

    if (extGStateProperties.find(PDFContentStateProperty::OverprintStroking) != extGStateProperties.cend() &&
        extGStateProperties.find(PDFContentStateProperty::OverprintFilling) == extGStateProperties.cend())
    {
        // ISO 32000-2, Table 57: /OP without /op sets the overprint of filling too. The override
        // changes the one of stroking only, so the other one is written with its current value.
        if (!node->getState())
        {
            addError(node, PDFTranslationContext::tr("The overprint of the object cannot be written, because its graphics state is not known."));
            return;
        }

        PDFContentStateValue overprintFilling;
        overprintFilling.kind = PDFContentStateValue::Kind::Boolean;
        overprintFilling.boolean = node->getState()->getState().getOverprintMode().overprintFilling;
        extGStateProperties[PDFContentStateProperty::OverprintFilling] = overprintFilling;
    }

    if (!extGStateProperties.empty())
    {
        appendExtGState(node, PDFContentExtGStateWriter::createDictionary(extGStateProperties));
    }

    // 4. The colors
    for (const PDFContentStateProperty property : { PDFContentStateProperty::StrokeColor, PDFContentStateProperty::FillColor })
    {
        auto it = override.properties.find(property);
        if (it != override.properties.cend())
        {
            appendColor(node, it->second, property == PDFContentStateProperty::StrokeColor);
        }
    }

    // 5. The line parameters and the text state, in the order of the properties
    for (const auto& [property, value] : override.properties)
    {
        if (property == PDFContentStateProperty::StrokeColor || property == PDFContentStateProperty::FillColor ||
            PDFContentExtGStateWriter::isExtGStateProperty(property))
        {
            continue;
        }

        appendOperatorProperty(node, property, value);
    }
}

void PDFContentStreamWriterHelper::enterNode(Frame* frame)
{
    const PDFContentNode* node = frame->node;
    const PDFContentNode::Data& data = node->getData();
    const PDFContentStateOverride* override = node->getOverride();

    if (!override)
    {
        appendBody(node);
        return;
    }

    if (PDFContentNode::isWrappableKind(data.kind) && !data.hasClipEffect)
    {
        // R-1: the wrap. An empty override still isolates the node (R-2: a node with a leak
        // is always wrapped at its new place).
        appendText(QByteArrayLiteral("q"));
        appendOverride(node, *override);
        appendBody(node);
        frame->isWrapped = true;
        return;
    }

    if (data.kind == PDFContentNodeKind::StateRun)
    {
        // A state run of the editor (the residual state of R-3): the operators of the override
        // are written in place, after the operations of the run, and stay in effect
        appendBody(node);
        appendOverride(node, *override);
        return;
    }

    if (!PDFContentNode::isWrappableKind(data.kind))
    {
        addError(node, PDFTranslationContext::tr("An object of this kind cannot be moved or restyled as a whole."));
        return;
    }

    // A node with a clipping effect is never wrapped: Q would end its clip for the nodes after it
    if (override->isEmpty())
    {
        appendBody(node);
        return;
    }

    if (data.kind != PDFContentNodeKind::Path)
    {
        // A text with a rendering mode 4 to 7: its canonical rewrite is a part of stage E2
        addError(node, PDFTranslationContext::tr("A text, which adds to the clipping path, cannot be moved or restyled."));
        return;
    }

    QTransform inverse;
    if (!invertMatrix(override->transform, &inverse))
    {
        addError(node, PDFTranslationContext::tr("The transformation of the object is not a regular matrix of finite numbers."));
        return;
    }

    // The geometry is rewritten in place: a point p of the wrap "M cm" is the point p x M here
    const PDFContentPath movedPath = data.path.transformed(override->transform);

    PDFContentPathPaint clipPaint;
    clipPaint.clip = data.paint.clip;

    if (data.paint.isClippingPath())
    {
        // A clipping path paints nothing: the properties of the override have no effect on it
        appendPath(node, movedPath, clipPaint);
        return;
    }

    // A path, which paints and clips: the painting goes into a wrap, the clip follows it in place
    PDFContentPathPaint paint = data.paint;
    paint.clip = PDFContentPathPaint::Clip::None;

    appendText(QByteArrayLiteral("q"));
    appendOverride(node, *override);
    appendPath(node, data.path, paint);
    frame->isWrapped = true;

    QString errorText;
    frame->trailer = PDFContentStreamWriter::writePath(movedPath, &errorText);
    if (!errorText.isEmpty())
    {
        addError(node, errorText);
        return;
    }

    if (!frame->trailer.isEmpty())
    {
        frame->trailer.append('\n');
    }
    frame->trailer.append(PDFContentStreamWriter::writePathPaint(clipPaint));
}

void PDFContentStreamWriterHelper::leaveNode(const Frame& frame)
{
    // A missing Q, ET or EMC is not added: the suffix is empty then (as it is for every leaf).
    // When something is written behind an unclosed node, PDFContentContainerEditor::build
    // (pdfcontentnode.cpp) has already put the generated closing operators into its suffix.
    appendSpan(frame.node, frame.node->getSuffix());

    if (frame.isWrapped)
    {
        appendText(QByteArrayLiteral("Q"));
    }

    appendText(frame.trailer);
}

PDFContentStreamWriteResult PDFContentStreamWriterHelper::run()
{
    if (m_container.isReadOnly())
    {
        addError(nullptr, PDFTranslationContext::tr("The content cannot be written, because a part of it cannot be read."));
    }
    else if (!m_container.getRoot())
    {
        addError(nullptr, PDFTranslationContext::tr("The content has no objects."));
    }
    else
    {
        std::vector<Frame> stack;
        stack.emplace_back();
        stack.back().node = m_container.getRoot().get();

        while (!stack.empty())
        {
            Frame& frame = stack.back();

            if (!frame.isEntered)
            {
                frame.isEntered = true;
                enterNode(&frame);
            }

            const std::vector<PDFContentNodePointer>& children = frame.node->getChildren();
            if (frame.nextChild < children.size())
            {
                const PDFContentNode* child = children[frame.nextChild++].get();
                if (child)
                {
                    // The reference to the frame is not used after this line
                    stack.emplace_back();
                    stack.back().node = child;
                }
                continue;
            }

            leaveNode(frame);
            stack.pop_back();
        }

        appendTail();
    }

    if (m_result.isSuccess())
    {
        m_result.content = std::move(m_content);
    }
    else
    {
        // A container with an error is not written at all: neither bytes nor new names
        m_result.resources = m_container.getResources();
    }

    return std::move(m_result);
}

PDFContentStreamWriteResult PDFContentStreamWriter::write(const PDFContentContainer& container)
{
    PDFContentStreamWriterHelper helper(container);
    return helper.run();
}

QByteArray PDFContentStreamWriter::writePath(const PDFContentPath& path, QString* errorText)
{
    if (errorText)
    {
        errorText->clear();
    }

    QByteArray text;
    bool isWritable = true;

    QRectF rectangle;
    if (path.isRectangle(&rectangle))
    {
        // ISO 32000-2, 8.5.2.1: x y width height re; the rectangle is written as it was added
        isWritable = PDFContentStreamWriterHelper::appendNumber(&text, rectangle.x()) &&
                     PDFContentStreamWriterHelper::appendNumber(&text, rectangle.y()) &&
                     PDFContentStreamWriterHelper::appendNumber(&text, rectangle.width()) &&
                     PDFContentStreamWriterHelper::appendNumber(&text, rectangle.height());
        text.append(" re");
    }
    else
    {
        for (const PDFContentPath::Segment& segment : path.getSegments())
        {
            QByteArray line;
            int pointCount = 0;
            const char* segmentOperator = "h";

            switch (segment.type)
            {
                case PDFContentPath::SegmentType::MoveTo:
                    pointCount = 1;
                    segmentOperator = "m";
                    break;

                case PDFContentPath::SegmentType::LineTo:
                    pointCount = 1;
                    segmentOperator = "l";
                    break;

                case PDFContentPath::SegmentType::CurveTo:
                    pointCount = 3;
                    segmentOperator = "c";
                    break;

                case PDFContentPath::SegmentType::Close:
                    break;
            }

            for (int i = 0; i < pointCount; ++i)
            {
                isWritable = isWritable &&
                             PDFContentStreamWriterHelper::appendNumber(&line, segment.points[i].x()) &&
                             PDFContentStreamWriterHelper::appendNumber(&line, segment.points[i].y());
            }

            if (!line.isEmpty())
            {
                line.append(' ');
            }
            line.append(segmentOperator);

            if (!text.isEmpty())
            {
                text.append('\n');
            }
            text.append(line);
        }
    }

    if (!isWritable)
    {
        if (errorText)
        {
            *errorText = PDFTranslationContext::tr("A coordinate of the path is not a finite number.");
        }
        return QByteArray();
    }

    return text;
}

QByteArray PDFContentStreamWriter::writePathPaint(const PDFContentPathPaint& paint)
{
    // ISO 32000-2, 8.5.3.1 (path-painting operators), 8.5.4 (clipping path operators: W or W*
    // stands between the construction of the path and its painting operator)
    QByteArray text;

    switch (paint.clip)
    {
        case PDFContentPathPaint::Clip::None:
            break;

        case PDFContentPathPaint::Clip::NonZero:
            text = QByteArrayLiteral("W ");
            break;

        case PDFContentPathPaint::Clip::EvenOdd:
            text = QByteArrayLiteral("W* ");
            break;
    }

    if (paint.stroke && paint.fill)
    {
        text.append(paint.closeBeforePaint ? 'b' : 'B');
        if (paint.fillEvenOdd)
        {
            text.append('*');
        }
    }
    else if (paint.fill)
    {
        // Filling closes every open subpath by itself
        text.append(paint.fillEvenOdd ? "f*" : "f");
    }
    else if (paint.stroke)
    {
        text.append(paint.closeBeforePaint ? 's' : 'S');
    }
    else
    {
        text.append('n');
    }

    return text;
}

QByteArray PDFContentStreamWriter::writeMatrix(const QTransform& matrix, QString* errorText)
{
    if (errorText)
    {
        errorText->clear();
    }

    QByteArray text;
    if (!PDFContentStreamWriterHelper::formatMatrix(matrix, &text))
    {
        if (errorText)
        {
            *errorText = PDFTranslationContext::tr("A value of the transformation matrix is not a finite number.");
        }
        return QByteArray();
    }

    text.append(" cm");
    return text;
}

}   // namespace pdf
