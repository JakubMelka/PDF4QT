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

#include "pdfannotationproperties.h"
#include "pdfdocumentbuilder.h"

#include <cmath>
#include <limits>

namespace pdf
{

/// Bounding of the values read from a document or given by the caller
class PDFAnnotationPropertyEditorHelper
{
public:
    PDFAnnotationPropertyEditorHelper() = delete;

    /// A finite value bounded to the range, a value, which is not finite, is \p defaultValue
    static PDFReal bound(PDFReal value, PDFReal minimum, PDFReal maximum, PDFReal defaultValue)
    {
        return std::isfinite(value) ? qBound(minimum, value, maximum) : defaultValue;
    }

    /// A text without the null characters, at most \p length characters long
    static QString boundText(const QString& text, qsizetype length)
    {
        QString result = text.left(length);
        result.remove(QChar(u'\0'));
        return result;
    }

    /// A known line ending (an invalid value is None)
    static AnnotationLineEnding boundLineEnding(AnnotationLineEnding lineEnding)
    {
        const int value = int(lineEnding);
        return value >= int(AnnotationLineEnding::None) && value <= int(AnnotationLineEnding::Slash) ? lineEnding : AnnotationLineEnding::None;
    }

    /// A color with the components 0 - 1 (an invalid color stays invalid)
    static QColor boundColor(const QColor& color)
    {
        return color.isValid() ? color.toRgb() : QColor();
    }

    /// Horizontal alignment of a text box (Q): left, center or right
    static TextAlignment boundAlignment(TextAlignment alignment)
    {
        if (alignment.testFlag(Qt::AlignHCenter))
        {
            return TextAlignment(Qt::AlignHCenter | Qt::AlignTop);
        }
        if (alignment.testFlag(Qt::AlignRight))
        {
            return TextAlignment(Qt::AlignRight | Qt::AlignTop);
        }
        return TextAlignment(Qt::AlignLeft | Qt::AlignTop);
    }

    /// A coordinate of a destination: NaN stays (the viewer keeps its value), a finite one is bounded
    static PDFReal boundCoordinate(PDFReal value)
    {
        return std::isfinite(value) ? qBound(-PDFAnnotationProperties::MAXIMUM_COORDINATE, value, PDFAnnotationProperties::MAXIMUM_COORDINATE)
                                    : std::numeric_limits<PDFReal>::quiet_NaN();
    }

    /// Two coordinates are equal (two NaN are equal)
    static bool isSameCoordinate(PDFReal first, PDFReal second)
    {
        return (std::isnan(first) && std::isnan(second)) || first == second;
    }

    /// Two destinations are equal
    static bool isSameDestination(const PDFDestination& first, const PDFDestination& second)
    {
        return first.getDestinationType() == second.getDestinationType() && first.getPageReference() == second.getPageReference() &&
               first.getPageIndex() == second.getPageIndex() && first.getName() == second.getName() &&
               isSameCoordinate(first.getLeft(), second.getLeft()) && isSameCoordinate(first.getTop(), second.getTop()) &&
               isSameCoordinate(first.getRight(), second.getRight()) && isSameCoordinate(first.getBottom(), second.getBottom()) &&
               isSameCoordinate(first.getZoom(), second.getZoom());
    }

    /// Name of the border style of BS /S
    static const char* getBorderStyleName(AnnotationBorderStyle style)
    {
        switch (style)
        {
            case AnnotationBorderStyle::Dashed:
                return "D";
            case AnnotationBorderStyle::Beveled:
                return "B";
            case AnnotationBorderStyle::Inset:
                return "I";
            case AnnotationBorderStyle::Underline:
                return "U";
            default:
                break;
        }
        return "S";
    }

    /// Number of the alignment of Q
    static PDFInteger getAlignmentNumber(TextAlignment alignment)
    {
        if (alignment.testFlag(Qt::AlignHCenter))
        {
            return 1;
        }
        return alignment.testFlag(Qt::AlignRight) ? 2 : 0;
    }
};

QColor PDFAnnotationPropertyEditor::readColor(const std::vector<PDFReal>& components)
{
    std::vector<PDFReal> values;
    values.reserve(components.size());
    for (const PDFReal component : components)
    {
        values.push_back(PDFAnnotationPropertyEditorHelper::bound(component, 0.0, 1.0, 0.0));
    }

    switch (values.size())
    {
        case 1:
            return QColor::fromRgbF(float(values[0]), float(values[0]), float(values[0]));
        case 3:
            return QColor::fromRgbF(float(values[0]), float(values[1]), float(values[2]));
        case 4:
            return QColor::fromCmykF(float(values[0]), float(values[1]), float(values[2]), float(values[3])).toRgb();
        default:
            break;
    }
    return QColor();
}

PDFObject PDFAnnotationPropertyEditor::createColor(const QColor& color)
{
    PDFObjectFactory factory;
    factory.beginArray();
    if (color.isValid())
    {
        const QColor rgb = color.toRgb();
        factory << PDFReal(rgb.redF()) << PDFReal(rgb.greenF()) << PDFReal(rgb.blueF());
    }
    factory.endArray();
    return factory.takeObject();
}

bool PDFAnnotationPropertyEditor::isValidName(const QByteArray& name)
{
    // A name of an icon: printable ASCII without the delimiters of PDF
    if (name.isEmpty() || name.size() > 127)
    {
        return false;
    }

    for (const char character : name)
    {
        const bool isPrintable = character > 32 && character < 127;
        if (!isPrintable || QByteArrayView("()<>[]{}/%#").contains(character))
        {
            return false;
        }
    }
    return true;
}

std::vector<QByteArray> PDFAnnotationPropertyEditor::getIconNames(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Text:
            return { "Comment", "Note", "Help", "Key", "Insert", "Paragraph", "NewParagraph" };
        case AnnotationType::Stamp:
            return { "Approved", "AsIs", "Confidential", "Departmental", "Draft", "Experimental", "Expired",
                     "Final", "ForComment", "ForPublicRelease", "NotApproved", "NotForPublicRelease", "Sold", "TopSecret" };
        case AnnotationType::FileAttachment:
            return { "PushPin", "Paperclip", "Graph", "Tag" };
        case AnnotationType::Sound:
            return { "Speaker", "Mic" };
        default:
            break;
    }
    return { };
}

PDFAnnotationProperties::Fields PDFAnnotationPropertyEditor::getSupportedFields(AnnotationType type, bool hasCalloutLine)
{
    using F = PDFAnnotationProperties;

    PDFAnnotationProperties::Fields fields = F::None;
    switch (type)
    {
        case AnnotationType::Text:
            fields = F::Color | F::IconName | F::Open;
            break;
        case AnnotationType::FreeText:
            fields = F::Color | F::InteriorColor | F::Border | F::DefaultAppearance | F::Alignment;
            fields.setFlag(F::CalloutLineEnding, hasCalloutLine);
            break;
        case AnnotationType::Line:
            fields = F::Color | F::InteriorColor | F::Border | F::LineEndings | F::LeaderLines | F::Caption;
            break;
        case AnnotationType::Polyline:
            fields = F::Color | F::InteriorColor | F::Border | F::LineEndings;
            break;
        case AnnotationType::Square:
        case AnnotationType::Circle:
        case AnnotationType::Polygon:
            fields = F::Color | F::InteriorColor | F::Border;
            break;
        case AnnotationType::Ink:
            fields = F::Color | F::Border;
            break;
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::Squiggly:
        case AnnotationType::StrikeOut:
        case AnnotationType::Caret:
            fields = F::Color;
            break;
        case AnnotationType::Stamp:
        case AnnotationType::FileAttachment:
        case AnnotationType::Sound:
            fields = F::Color | F::IconName;
            break;
        case AnnotationType::Redact:
            fields = F::Color | F::InteriorColor;
            break;
        case AnnotationType::Link:
            return F::Contents | F::Flags | F::Color | F::Border | F::HighlightMode;
        case AnnotationType::Invalid:
            return F::None;
        default:
            return F::Contents | F::Flags;
    }

    // Every markup annotation has the comment and the opacity
    return fields | F::Author | F::Subject | F::Contents | F::Opacity | F::Flags;
}

PDFAnnotationProperties PDFAnnotationPropertyEditor::read(const PDFObjectStorage* storage, PDFObjectReference annotation)
{
    PDFAnnotationProperties properties;
    const PDFAnnotationPtr parsed = storage && annotation.isValid() ? PDFAnnotation::parse(storage, annotation) : PDFAnnotationPtr();
    const PDFDictionary* dictionary = parsed ? storage->getDictionaryFromObject(storage->getObjectByReference(annotation)) : nullptr;
    if (!dictionary || !storage->getObject(dictionary->get("Subtype")).isName())
    {
        return properties;
    }

    PDFDocumentDataLoaderDecorator loader(storage);
    properties.type = parsed->getType();
    properties.hasCalloutLine = properties.type == AnnotationType::FreeText && loader.readNumberArrayFromDictionary(dictionary, "CL").size() >= 4;
    properties.supportedFields = getSupportedFields(properties.type, properties.hasCalloutLine);

    properties.author = loader.readTextStringFromDictionary(dictionary, "T", QString());
    properties.subject = loader.readTextStringFromDictionary(dictionary, "Subj", QString());
    properties.contents = loader.readTextStringFromDictionary(dictionary, "Contents", QString());
    properties.color = readColor(loader.readNumberArrayFromDictionary(dictionary, "C"));
    properties.interiorColor = readColor(loader.readNumberArrayFromDictionary(dictionary, "IC"));
    properties.opacity = loader.readNumberFromDictionary(dictionary, "CA", 1.0);
    properties.flags = parsed->getFlags();
    properties.modifiedDate = parsed->getLastModifiedDateTime();
    if (const PDFMarkupAnnotation* markup = parsed->asMarkupAnnotation())
    {
        properties.creationDate = markup->getCreationDate();
    }

    // The border style dictionary BS wins over the older Border array
    const PDFAnnotationBorder& border = parsed->getBorder();
    if (border.isValid())
    {
        properties.borderWidth = border.getWidth();
        properties.borderStyle = border.getStyle();
        properties.dashPattern = border.getDashPattern();
    }
    if (const PDFDictionary* borderStyle = storage->getDictionaryFromObject(dictionary->get("BS")))
    {
        // PDFAnnotationBorder::parseBS does not read the dash pattern
        properties.dashPattern = loader.readNumberArrayFromDictionary(borderStyle, "D");
    }

    const std::vector<QByteArray> lineEndings = loader.readNameArrayFromDictionary(dictionary, "LE");
    if (lineEndings.size() == 2)
    {
        properties.startLineEnding = PDFAnnotation::convertNameToLineEnding(lineEndings[0]);
        properties.endLineEnding = PDFAnnotation::convertNameToLineEnding(lineEndings[1]);
    }
    properties.calloutLineEnding = PDFAnnotation::convertNameToLineEnding(loader.readNameFromDictionary(dictionary, "LE"));

    properties.iconName = loader.readNameFromDictionary(dictionary, "Name");
    properties.isOpen = loader.readBooleanFromDictionary(dictionary, "Open", false);

    // Text box: the font, its size and the text color of DA, the alignment of Q
    const PDFAnnotationDefaultAppearance defaultAppearance = PDFDocumentBuilder::getDefaultFreeTextAppearance(dictionary);
    properties.freeTextStyle.fontFamily = PDFDocumentBuilder::decodeFreeTextFontName(defaultAppearance.getFontName());
    properties.freeTextStyle.fontSize = defaultAppearance.getFontSize();
    properties.freeTextStyle.textColor = defaultAppearance.getFontColor();
    const PDFInteger alignment = loader.readIntegerFromDictionary(dictionary, "Q", 0);
    properties.freeTextStyle.textAlignment = alignment == 1 ? Qt::AlignHCenter : (alignment == 2 ? Qt::AlignRight : Qt::AlignLeft);

    // Link: the highlight mode, the destination of a GoTo action (or of Dest), the address of a web link
    if (const PDFLinkAnnotation* link = dynamic_cast<const PDFLinkAnnotation*>(parsed.data()))
    {
        properties.highlightMode = link->getHighlightMode();
        if (const PDFActionGoTo* goTo = dynamic_cast<const PDFActionGoTo*>(link->getAction()))
        {
            properties.destination = goTo->getDestination();
            properties.isDestinationEditable = true;
            properties.supportedFields |= PDFAnnotationProperties::Destination;
        }
        else if (const PDFActionURI* uri = dynamic_cast<const PDFActionURI*>(link->getAction()))
        {
            properties.uri = QString::fromLatin1(uri->getURI());
        }
    }

    properties.leaderLineLength = loader.readNumberFromDictionary(dictionary, "LL", 0.0);
    properties.leaderLineExtension = loader.readNumberFromDictionary(dictionary, "LLE", 0.0);
    properties.isCaptionShown = loader.readBooleanFromDictionary(dictionary, "Cap", false);
    properties.isCaptionAbove = loader.readNameFromDictionary(dictionary, "CP") == "Top";

    return bound(std::move(properties));
}

PDFAnnotationProperties PDFAnnotationPropertyEditor::bound(PDFAnnotationProperties properties)
{
    using H = PDFAnnotationPropertyEditorHelper;
    using P = PDFAnnotationProperties;

    properties.author = H::boundText(properties.author, P::MAXIMUM_TITLE_LENGTH);
    properties.subject = H::boundText(properties.subject, P::MAXIMUM_TITLE_LENGTH);
    properties.contents = H::boundText(properties.contents, P::MAXIMUM_TEXT_LENGTH);
    properties.color = H::boundColor(properties.color);
    properties.interiorColor = H::boundColor(properties.interiorColor);
    properties.opacity = H::bound(properties.opacity, 0.0, 1.0, 1.0);
    properties.borderWidth = H::bound(properties.borderWidth, 0.0, P::MAXIMUM_BORDER_WIDTH, 1.0);

    const int borderStyle = int(properties.borderStyle);
    if (borderStyle < int(AnnotationBorderStyle::Solid) || borderStyle > int(AnnotationBorderStyle::Underline))
    {
        properties.borderStyle = AnnotationBorderStyle::Solid;
    }

    // A dash pattern of finite positive lengths, not all of them zero
    std::vector<PDFReal> dashPattern;
    PDFReal dashSum = 0.0;
    for (const PDFReal length : properties.dashPattern)
    {
        if (dashPattern.size() < P::MAXIMUM_DASH_ITEMS)
        {
            dashPattern.push_back(H::bound(length, 0.0, P::MAXIMUM_DASH_LENGTH, 0.0));
            dashSum += dashPattern.back();
        }
    }
    if (dashSum <= 0.0)
    {
        dashPattern = { 3.0 };
    }
    properties.dashPattern = std::move(dashPattern);

    properties.flags = PDFAnnotation::Flags(properties.flags.toInt() & 0x3FF);
    properties.startLineEnding = H::boundLineEnding(properties.startLineEnding);
    properties.endLineEnding = H::boundLineEnding(properties.endLineEnding);
    properties.calloutLineEnding = H::boundLineEnding(properties.calloutLineEnding);
    if (!isValidName(properties.iconName))
    {
        properties.iconName.clear();
    }

    PDFFreeTextStyle& style = properties.freeTextStyle;
    style.fontFamily = H::boundText(style.fontFamily, 127).trimmed();
    if (style.fontFamily.isEmpty())
    {
        style.fontFamily = QStringLiteral("Helvetica");
    }
    style.fontSize = H::bound(style.fontSize, P::MINIMUM_FONT_SIZE, P::MAXIMUM_FONT_SIZE, 10.0);
    style.textColor = style.textColor.isValid() ? style.textColor.toRgb() : QColor(Qt::black);
    style.textAlignment = H::boundAlignment(style.textAlignment);

    properties.leaderLineLength = H::bound(properties.leaderLineLength, -P::MAXIMUM_LEADER_LINE, P::MAXIMUM_LEADER_LINE, 0.0);
    properties.leaderLineExtension = H::bound(properties.leaderLineExtension, 0.0, P::MAXIMUM_LEADER_LINE, 0.0);

    const int highlightMode = int(properties.highlightMode);
    if (highlightMode < int(LinkHighlightMode::None) || highlightMode > int(LinkHighlightMode::Push))
    {
        properties.highlightMode = LinkHighlightMode::Invert;
    }

    // A destination: a known type, finite bounded coordinates (NaN = kept by the viewer)
    PDFDestination& destination = properties.destination;
    const int destinationType = int(destination.getDestinationType());
    if (destinationType < int(DestinationType::Invalid) || destinationType > int(DestinationType::FitBV))
    {
        destination = PDFDestination();
    }
    destination.setLeft(H::boundCoordinate(destination.getLeft()));
    destination.setTop(H::boundCoordinate(destination.getTop()));
    destination.setRight(H::boundCoordinate(destination.getRight()));
    destination.setBottom(H::boundCoordinate(destination.getBottom()));
    destination.setZoom(std::isfinite(destination.getZoom()) ? qBound(0.0, destination.getZoom(), P::MAXIMUM_ZOOM) : std::numeric_limits<PDFReal>::quiet_NaN());
    destination.setPageIndex(qMax<PDFInteger>(0, destination.getPageIndex()));
    return properties;
}

PDFAnnotationProperties::Fields PDFAnnotationPropertyEditor::getChangedFields(const PDFAnnotationProperties& first, const PDFAnnotationProperties& second)
{
    using F = PDFAnnotationProperties;

    PDFAnnotationProperties::Fields fields = F::None;
    fields.setFlag(F::Author, first.author != second.author);
    fields.setFlag(F::Subject, first.subject != second.subject);
    fields.setFlag(F::Contents, first.contents != second.contents);
    fields.setFlag(F::Color, first.color != second.color);
    fields.setFlag(F::InteriorColor, first.interiorColor != second.interiorColor);
    fields.setFlag(F::Opacity, !qFuzzyCompare(1.0 + first.opacity, 1.0 + second.opacity));
    fields.setFlag(F::Border, !qFuzzyCompare(1.0 + first.borderWidth, 1.0 + second.borderWidth) || first.borderStyle != second.borderStyle ||
                                  (first.borderStyle == AnnotationBorderStyle::Dashed && first.dashPattern != second.dashPattern));
    fields.setFlag(F::Flags, first.flags != second.flags);
    fields.setFlag(F::LineEndings, first.startLineEnding != second.startLineEnding || first.endLineEnding != second.endLineEnding);
    fields.setFlag(F::IconName, first.iconName != second.iconName);
    fields.setFlag(F::Open, first.isOpen != second.isOpen);
    fields.setFlag(F::DefaultAppearance, first.freeTextStyle.fontFamily != second.freeTextStyle.fontFamily ||
                                             !qFuzzyCompare(first.freeTextStyle.fontSize, second.freeTextStyle.fontSize) ||
                                             first.freeTextStyle.textColor != second.freeTextStyle.textColor);
    fields.setFlag(F::Alignment, PDFAnnotationPropertyEditorHelper::getAlignmentNumber(first.freeTextStyle.textAlignment) !=
                                     PDFAnnotationPropertyEditorHelper::getAlignmentNumber(second.freeTextStyle.textAlignment));
    fields.setFlag(F::CalloutLineEnding, first.calloutLineEnding != second.calloutLineEnding);
    fields.setFlag(F::LeaderLines, !qFuzzyCompare(1.0 + first.leaderLineLength, 1.0 + second.leaderLineLength) ||
                                       !qFuzzyCompare(1.0 + first.leaderLineExtension, 1.0 + second.leaderLineExtension));
    fields.setFlag(F::Caption, first.isCaptionShown != second.isCaptionShown || first.isCaptionAbove != second.isCaptionAbove);
    fields.setFlag(F::HighlightMode, first.highlightMode != second.highlightMode);
    fields.setFlag(F::Destination, !PDFAnnotationPropertyEditorHelper::isSameDestination(first.destination, second.destination));
    return fields & first.supportedFields;
}

bool PDFAnnotationPropertyEditor::write(PDFDocumentBuilder* builder, PDFObjectReference annotation, const PDFAnnotationProperties& properties, PDFAnnotationProperties::Fields fields)
{
    using F = PDFAnnotationProperties;
    using H = PDFAnnotationPropertyEditorHelper;

    if (!builder)
    {
        return false;
    }

    // The fields, which the annotation of the document has (not the type given by the caller)
    const PDFAnnotationProperties current = read(builder->getStorage(), annotation);
    fields &= current.supportedFields;
    if (fields == F::None)
    {
        return false;
    }

    const PDFAnnotationProperties value = bound(properties);
    const PDFDictionary* dictionary = builder->getStorage()->getDictionaryFromObject(builder->getStorage()->getObjectByReference(annotation));

    PDFObjectFactory factory;
    factory.beginDictionary();

    if (fields.testFlag(F::Author))
    {
        factory.beginDictionaryItem("T");
        factory << value.author;
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Subject))
    {
        factory.beginDictionaryItem("Subj");
        factory << value.subject;
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Contents))
    {
        factory.beginDictionaryItem("Contents");
        factory << value.contents;
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Color))
    {
        factory.beginDictionaryItem("C");
        factory << createColor(value.color);
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::InteriorColor))
    {
        // No fill = no entry (an empty array is allowed too, a missing entry is the clearest)
        factory.beginDictionaryItem("IC");
        factory << (value.interiorColor.isValid() ? createColor(value.interiorColor) : PDFObject());
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Opacity))
    {
        factory.beginDictionaryItem("CA");
        factory << value.opacity;
        factory.endDictionaryItem();
        if (dictionary->hasKey("ca"))
        {
            factory.beginDictionaryItem("ca");
            factory << value.opacity;
            factory.endDictionaryItem();
        }
    }
    if (fields.testFlag(F::Border))
    {
        // BS replaces Border: the width, the style and the dash pattern of a dashed border
        factory.beginDictionaryItem("Border");
        factory << PDFObject();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("BS");
        factory.beginDictionary();
        factory.beginDictionaryItem("W");
        factory << value.borderWidth;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("S");
        factory << WrapName(H::getBorderStyleName(value.borderStyle));
        factory.endDictionaryItem();
        factory.beginDictionaryItem("D");
        if (value.borderStyle == AnnotationBorderStyle::Dashed)
        {
            factory.beginArray();
            for (const PDFReal length : value.dashPattern)
            {
                factory << length;
            }
            factory.endArray();
        }
        else
        {
            factory << PDFObject();
        }
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Flags))
    {
        factory.beginDictionaryItem("F");
        factory << PDFInteger(value.flags.toInt());
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::LineEndings))
    {
        factory.beginDictionaryItem("LE");
        factory.beginArray();
        factory << value.startLineEnding << value.endLineEnding;
        factory.endArray();
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::CalloutLineEnding))
    {
        factory.beginDictionaryItem("LE");
        factory << value.calloutLineEnding;
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::IconName) && !value.iconName.isEmpty())
    {
        factory.beginDictionaryItem("Name");
        factory << WrapName(value.iconName);
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Open))
    {
        factory.beginDictionaryItem("Open");
        factory << value.isOpen;
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::DefaultAppearance))
    {
        factory.beginDictionaryItem("DA");
        factory << WrapString(PDFDocumentBuilder::createFreeTextDefaultAppearance(value.freeTextStyle));
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Alignment))
    {
        factory.beginDictionaryItem("Q");
        factory << H::getAlignmentNumber(value.freeTextStyle.textAlignment);
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::LeaderLines))
    {
        factory.beginDictionaryItem("LL");
        factory << value.leaderLineLength;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("LLE");
        factory << value.leaderLineExtension;
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Caption))
    {
        factory.beginDictionaryItem("Cap");
        factory << value.isCaptionShown;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("CP");
        factory << WrapName(value.isCaptionAbove ? "Top" : "Inline");
        factory.endDictionaryItem();
    }

    if (fields.testFlag(F::HighlightMode))
    {
        factory.beginDictionaryItem("H");
        factory << value.highlightMode;
        factory.endDictionaryItem();
    }
    if (fields.testFlag(F::Destination))
    {
        // The GoTo action is replaced by the destination (an invalid one removes it)
        factory.beginDictionaryItem("A");
        factory << PDFObject();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Dest");
        factory << value.destination;
        factory.endDictionaryItem();
    }

    factory.beginDictionaryItem("M");
    factory << WrapCurrentDateTime();
    factory.endDictionaryItem();
    factory.endDictionary();

    builder->mergeTo(annotation, factory.takeObject());
    builder->updateAnnotationAppearanceStreams(annotation);
    return true;
}

}   // namespace pdf
