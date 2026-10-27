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

#include "pdfappannotationoperations.h"

#include "pdfdocumentbuilder.h"
#include "pdfexception.h"
#include "pdfoperationcontrol.h"

#include <QUrl>

#include <cmath>

namespace pdf
{

/// Validation of the untrusted arguments of the creation operations (package A)
class PDFAppAnnotationOperationsCreateHelper
{
public:
    PDFAppAnnotationOperationsCreateHelper() = delete;

    /// Largest absolute coordinate of a point of an annotation (page space)
    static constexpr PDFReal MAXIMUM_COORDINATE = 1.0e6;

    /// Largest border width (as the styles of the tools)
    static constexpr PDFReal MAXIMUM_PEN_WIDTH = 100.0;

    /// Maximal length of the author of an annotation
    static constexpr qsizetype MAXIMUM_AUTHOR_LENGTH = 256;

    /// Size of the rectangle of a new sticky note (the icon of the viewer is drawn there)
    static constexpr PDFReal STICKY_NOTE_SIZE = 24.0;

    /// Interval of the cancellation checks of Mark as annotations
    static constexpr size_t CANCEL_CHECK_INTERVAL = 64;

    /// Page of the document (nullptr for an index out of the range)
    static const PDFPage* getPage(const PDFDocument* document, PDFInteger pageIndex)
    {
        const PDFCatalog* catalog = document ? document->getCatalog() : nullptr;
        if (!catalog || pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            return nullptr;
        }
        return catalog->getPage(size_t(pageIndex));
    }

    static bool isFinite(PDFReal value)
    {
        return std::isfinite(value) && std::abs(value) <= MAXIMUM_COORDINATE;
    }

    static bool isFinite(const QPointF& point)
    {
        return isFinite(point.x()) && isFinite(point.y());
    }

    /// Normalized rectangle with finite coordinates and a positive area (else nullopt)
    static std::optional<QRectF> getRectangle(const QRectF& rectangle)
    {
        const QRectF normalized = rectangle.normalized();
        if (!isFinite(normalized.topLeft()) || !isFinite(normalized.bottomRight()) || !(normalized.width() > 0.0) || !(normalized.height() > 0.0))
        {
            return std::nullopt;
        }
        return normalized;
    }

    /// Quadrilaterals with finite points, whole pieces of 4 points, at most
    /// MAXIMUM_MARKUP_QUADRILATERALS pieces (empty = invalid)
    static QPolygonF getQuadrilaterals(const QPolygonF& quadrilaterals)
    {
        const qsizetype pieces = qMin<qsizetype>(quadrilaterals.size() / 4, qsizetype(PDFAppAnnotationOperations::MAXIMUM_MARKUP_QUADRILATERALS));
        QPolygonF result;
        result.reserve(pieces * 4);
        for (qsizetype i = 0; i < pieces * 4; ++i)
        {
            if (!isFinite(quadrilaterals[i]))
            {
                return QPolygonF();
            }
            result.push_back(quadrilaterals[i]);
        }
        return result;
    }

    /// Text without control characters (except of the line breaks and the tabs of a
    /// multi-line text), \r\n and \r are \n; at most \p maximumLength characters
    static QString sanitize(const QString& text, qsizetype maximumLength, bool isMultiLine)
    {
        QString normalized = text.left(maximumLength * 2);
        normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));

        QString result;
        result.reserve(qMin(normalized.size(), maximumLength));
        for (const QChar character : normalized)
        {
            if (result.size() >= maximumLength)
            {
                break;
            }

            const bool isLineCharacter = character == QLatin1Char('\n') || character == QLatin1Char('\t');
            if (isLineCharacter && isMultiLine)
            {
                result.append(character);
                continue;
            }

            const QChar::Category category = character.category();
            if (category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Other_NotAssigned)
            {
                if (isLineCharacter)
                {
                    result.append(QLatin1Char(' '));
                }
                continue;
            }
            result.append(character);
        }

        // A dangling high surrogate at the cut is removed
        if (!result.isEmpty() && result.back().isHighSurrogate())
        {
            result.chop(1);
        }
        return result;
    }

    static QString getAuthor(const PDFAppNewAnnotation& data)
    {
        return sanitize(data.author, MAXIMUM_AUTHOR_LENGTH, false).trimmed();
    }

    static QString getContents(const PDFAppNewAnnotation& data)
    {
        return sanitize(data.contents, PDFAppAnnotationOperations::MAXIMUM_TEXT_LENGTH, true);
    }

    /// Opaque color of the data (else the default color)
    static QColor getColor(const QColor& color, const QColor& defaultColor)
    {
        if (!color.isValid())
        {
            return defaultColor;
        }
        QColor result = color.toRgb();
        result.setAlpha(255);
        return result;
    }

    static PDFReal getOpacity(PDFReal opacity)
    {
        return std::isfinite(opacity) ? qBound(0.0, opacity, 1.0) : 1.0;
    }

    static PDFReal getPenWidth(PDFReal penWidth)
    {
        return std::isfinite(penWidth) ? qBound(0.0, penWidth, MAXIMUM_PEN_WIDTH) : 1.0;
    }

    /// Default color of a text markup (the defaults of the engine tool)
    static QColor getDefaultMarkupColor(AnnotationType type)
    {
        switch (type)
        {
            case AnnotationType::Highlight:
                return QColor(Qt::yellow);
            case AnnotationType::Underline:
                return QColor(Qt::black);
            case AnnotationType::Squiggly:
                return QColor(Qt::red);
            case AnnotationType::StrikeOut:
                return QColor(Qt::red);
            default:
                break;
        }
        return QColor(Qt::black);
    }

    /// Creates one text markup on the page (the type is a text markup type)
    static PDFObjectReference createTextMarkup(PDFDocumentBuilder* builder,
                                               PDFObjectReference page,
                                               AnnotationType type,
                                               const QPolygonF& quadrilaterals,
                                               const QColor& color,
                                               const QString& author,
                                               const QString& contents,
                                               PDFReal opacity)
    {
        PDFObjectReference annotation;
        switch (type)
        {
            case AnnotationType::Highlight:
                annotation = builder->createAnnotationHighlight(page, quadrilaterals, color);
                break;
            case AnnotationType::Underline:
                annotation = builder->createAnnotationUnderline(page, quadrilaterals, color);
                break;
            case AnnotationType::Squiggly:
                annotation = builder->createAnnotationSquiggly(page, quadrilaterals, color);
                break;
            case AnnotationType::StrikeOut:
                annotation = builder->createAnnotationStrikeout(page, quadrilaterals, color);
                break;
            default:
                return PDFObjectReference();
        }

        if (!author.isEmpty())
        {
            builder->setAnnotationTitle(annotation, author);
        }
        if (!contents.isEmpty())
        {
            builder->setAnnotationContents(annotation, contents);
        }
        if (opacity < 1.0)
        {
            builder->setAnnotationOpacity(annotation, opacity);
        }
        builder->updateAnnotationAppearanceStreams(annotation);
        return annotation;
    }

    /// Finalizes the modifier: the changed document with the flag Annotation, or nullopt
    static PDFAppAnnotationResult finalize(PDFDocumentModifier& modifier)
    {
        modifier.markAnnotationsChanged();
        if (!modifier.finalize() || modifier.getDocument().isNull())
        {
            return std::nullopt;
        }
        return PDFModifiedDocument(modifier.getDocument(), nullptr, modifier.getFlags() | PDFModifiedDocument::Annotation);
    }
};

bool PDFAppAnnotationOperations::isTextMarkupType(AnnotationType type)
{
    switch (type)
    {
        case AnnotationType::Highlight:
        case AnnotationType::Underline:
        case AnnotationType::Squiggly:
        case AnnotationType::StrikeOut:
            return true;

        default:
            break;
    }
    return false;
}

PDFAppAnnotationResult PDFAppAnnotationOperations::createTextMarkups(const PDFDocumentPointer& document,
                                                                     AnnotationType type,
                                                                     const std::vector<PDFAppTextMarkupPart>& parts,
                                                                     const PDFAppNewAnnotation& data,
                                                                     std::vector<PDFObjectReference>* created)
{
    using Helper = PDFAppAnnotationOperationsCreateHelper;

    if (created)
    {
        created->clear();
    }
    if (document.isNull() || !isTextMarkupType(type) || parts.empty())
    {
        return std::nullopt;
    }

    try
    {
        PDFDocumentModifier modifier(document.data());
        PDFDocumentBuilder* builder = modifier.getBuilder();
        const QColor color = Helper::getColor(data.color, Helper::getDefaultMarkupColor(type));
        const QString author = Helper::getAuthor(data);
        const QString contents = Helper::getContents(data);
        const PDFReal opacity = Helper::getOpacity(data.opacity);

        std::vector<PDFObjectReference> references;
        for (const PDFAppTextMarkupPart& part : parts)
        {
            if (references.size() >= MAXIMUM_CREATED_ANNOTATIONS)
            {
                break;
            }

            const PDFPage* page = Helper::getPage(document.data(), part.pageIndex);
            const QPolygonF quadrilaterals = Helper::getQuadrilaterals(part.quadrilaterals);
            if (!page || quadrilaterals.isEmpty())
            {
                continue;
            }

            // The comment of the selection is one comment: only the first markup
            // (the first page of a selection over pages) has the contents
            const QString& partContents = references.empty() ? contents : QString();
            const PDFObjectReference annotation = Helper::createTextMarkup(builder, page->getPageReference(), type, quadrilaterals, color, author, partContents, opacity);
            if (annotation.isValid())
            {
                references.push_back(annotation);
            }
        }

        if (references.empty())
        {
            return std::nullopt;
        }

        PDFAppAnnotationResult result = Helper::finalize(modifier);
        if (result && created)
        {
            *created = std::move(references);
        }
        return result;
    }
    catch (const PDFException&)
    {
        return std::nullopt;
    }
}

PDFAppAnnotationResult PDFAppAnnotationOperations::createStickyNote(const PDFDocumentPointer& document,
                                                                    PDFInteger pageIndex,
                                                                    const QPointF& position,
                                                                    TextAnnotationIcon icon,
                                                                    const PDFAppNewAnnotation& data,
                                                                    PDFObjectReference* created)
{
    using Helper = PDFAppAnnotationOperationsCreateHelper;

    if (created)
    {
        *created = PDFObjectReference();
    }

    const PDFPage* page = Helper::getPage(document.data(), pageIndex);
    if (!page || !Helper::isFinite(position))
    {
        return std::nullopt;
    }

    switch (icon)
    {
        case TextAnnotationIcon::Comment:
        case TextAnnotationIcon::Help:
        case TextAnnotationIcon::Insert:
        case TextAnnotationIcon::Key:
        case TextAnnotationIcon::NewParagraph:
        case TextAnnotationIcon::Note:
        case TextAnnotationIcon::Paragraph:
            break;

        default:
            return std::nullopt;
    }

    try
    {
        PDFDocumentModifier modifier(document.data());
        PDFDocumentBuilder* builder = modifier.getBuilder();

        // The point is the top-left corner of the icon (the PDF y axis goes up)
        const QRectF rectangle(position.x(), position.y() - Helper::STICKY_NOTE_SIZE, Helper::STICKY_NOTE_SIZE, Helper::STICKY_NOTE_SIZE);
        const PDFObjectReference annotation = builder->createAnnotationText(page->getPageReference(), rectangle, icon, Helper::getAuthor(data), QString(), Helper::getContents(data), false);
        builder->setAnnotationColor(annotation, Helper::getColor(data.color, QColor(Qt::yellow)));
        const PDFReal opacity = Helper::getOpacity(data.opacity);
        if (opacity < 1.0)
        {
            builder->setAnnotationOpacity(annotation, opacity);
        }
        builder->updateAnnotationAppearanceStreams(annotation);

        PDFAppAnnotationResult result = Helper::finalize(modifier);
        if (result && created)
        {
            *created = annotation;
        }
        return result;
    }
    catch (const PDFException&)
    {
        return std::nullopt;
    }
}

PDFAppAnnotationResult PDFAppAnnotationOperations::createFreeText(const PDFDocumentPointer& document,
                                                                  PDFInteger pageIndex,
                                                                  const QRectF& rectangle,
                                                                  const QString& text,
                                                                  const PDFFreeTextStyle& style,
                                                                  const PDFAppNewAnnotation& data,
                                                                  PDFObjectReference* created)
{
    using Helper = PDFAppAnnotationOperationsCreateHelper;

    if (created)
    {
        *created = PDFObjectReference();
    }

    const PDFPage* page = Helper::getPage(document.data(), pageIndex);
    const std::optional<QRectF> area = Helper::getRectangle(rectangle);
    const QString contents = Helper::sanitize(text, MAXIMUM_TEXT_LENGTH, true);
    if (!page || !area || contents.trimmed().isEmpty())
    {
        return std::nullopt;
    }

    try
    {
        // Font, size, color and alignment bounded as the engine tool bounds them
        // (the style may come from the database)
        PDFFreeTextStyle boundedStyle = style;
        boundedStyle.fontFamily = boundedStyle.fontFamily.trimmed().left(256);
        if (boundedStyle.fontFamily.isEmpty())
        {
            boundedStyle.fontFamily = QStringLiteral("Helvetica");
        }
        boundedStyle.fontSize = std::isfinite(boundedStyle.fontSize) ? qBound(1.0, boundedStyle.fontSize, 512.0) : 10.0;
        boundedStyle.textColor = Helper::getColor(boundedStyle.textColor, QColor(Qt::black));
        const Qt::Alignment horizontal = boundedStyle.textAlignment & Qt::AlignHorizontal_Mask;
        boundedStyle.textAlignment = TextAlignment((horizontal == Qt::AlignHCenter || horizontal == Qt::AlignRight) ? horizontal : Qt::AlignLeft) | Qt::AlignTop;

        PDFDocumentModifier modifier(document.data());
        PDFDocumentBuilder* builder = modifier.getBuilder();
        const PDFObjectReference annotation = builder->createAnnotationFreeText(page->getPageReference(), *area, Helper::getAuthor(data), QString(), contents, boundedStyle, true);

        // The border (C and the width); no border = width 0
        const PDFReal borderWidth = data.color.isValid() ? Helper::getPenWidth(data.penWidth) : 0.0;
        if (borderWidth > 0.0)
        {
            builder->setAnnotationColor(annotation, Helper::getColor(data.color, QColor(Qt::black)));
        }
        builder->setAnnotationBorder(annotation, 0.0, 0.0, borderWidth);
        const PDFReal opacity = Helper::getOpacity(data.opacity);
        if (opacity < 1.0)
        {
            builder->setAnnotationOpacity(annotation, opacity);
        }
        builder->updateAnnotationAppearanceStreams(annotation);

        PDFAppAnnotationResult result = Helper::finalize(modifier);
        if (result && created)
        {
            *created = annotation;
        }
        return result;
    }
    catch (const PDFException&)
    {
        return std::nullopt;
    }
}

PDFAppAnnotationResult PDFAppAnnotationOperations::createWebLink(const PDFDocumentPointer& document,
                                                                 PDFInteger pageIndex,
                                                                 const QRectF& rectangle,
                                                                 const QString& url,
                                                                 PDFObjectReference* created)
{
    using Helper = PDFAppAnnotationOperationsCreateHelper;

    if (created)
    {
        *created = PDFObjectReference();
    }

    const PDFPage* page = Helper::getPage(document.data(), pageIndex);
    const std::optional<QRectF> area = Helper::getRectangle(rectangle);
    const QString link = url.trimmed();
    if (!page || !area || !isWebLinkAllowed(link))
    {
        return std::nullopt;
    }

    // ISO 32000-2 12.6.4.8: the URI is 7-bit ASCII. An address with other
    // characters (a path in Czech, an internationalized host) is written
    // percent-encoded / in Punycode - a text string of UTF-16 would be no address.
    bool isAscii = true;
    for (const QChar character : link)
    {
        isAscii = isAscii && character.unicode() < 0x80;
    }
    const QString storedLink = isAscii ? link : QString::fromLatin1(QUrl(link, QUrl::StrictMode).toEncoded(QUrl::FullyEncoded));
    if (storedLink.isEmpty())
    {
        return std::nullopt;
    }

    try
    {
        PDFDocumentModifier modifier(document.data());
        const PDFObjectReference annotation = modifier.getBuilder()->createAnnotationLink(page->getPageReference(), *area, storedLink, LinkHighlightMode::Invert);

        PDFAppAnnotationResult result = Helper::finalize(modifier);
        if (result && created)
        {
            *created = annotation;
        }
        return result;
    }
    catch (const PDFException&)
    {
        return std::nullopt;
    }
}

PDFAppAnnotationResult PDFAppAnnotationOperations::createPageLink(const PDFDocumentPointer& document,
                                                                  PDFInteger pageIndex,
                                                                  const QRectF& rectangle,
                                                                  const PDFDestination& destination,
                                                                  PDFObjectReference* created)
{
    using Helper = PDFAppAnnotationOperationsCreateHelper;

    if (created)
    {
        *created = PDFObjectReference();
    }

    const PDFPage* page = Helper::getPage(document.data(), pageIndex);
    const PDFPage* targetPage = Helper::getPage(document.data(), destination.getPageIndex());
    const std::optional<QRectF> area = Helper::getRectangle(rectangle);
    if (!page || !targetPage || !area)
    {
        return std::nullopt;
    }

    // Only the destinations, which a picked target gives; the coordinates of the
    // type are finite (the zoom of XYZ may be NaN - the reader keeps their zoom)
    PDFDestination target = destination;
    target.setPageReference(targetPage->getPageReference());
    switch (destination.getDestinationType())
    {
        case DestinationType::Fit:
        case DestinationType::FitB:
            break;

        case DestinationType::FitH:
        case DestinationType::FitBH:
            if (!Helper::isFinite(destination.getTop()))
            {
                return std::nullopt;
            }
            break;

        case DestinationType::FitV:
        case DestinationType::FitBV:
            if (!Helper::isFinite(destination.getLeft()))
            {
                return std::nullopt;
            }
            break;

        case DestinationType::FitR:
            if (!Helper::isFinite(QPointF(destination.getLeft(), destination.getTop())) || !Helper::isFinite(QPointF(destination.getRight(), destination.getBottom())))
            {
                return std::nullopt;
            }
            break;

        case DestinationType::XYZ:
        {
            if (!Helper::isFinite(QPointF(destination.getLeft(), destination.getTop())))
            {
                return std::nullopt;
            }
            const PDFReal zoom = destination.getZoom();
            if (!std::isnan(zoom) && (!std::isfinite(zoom) || zoom <= 0.0 || zoom > 1000.0))
            {
                return std::nullopt;
            }
            break;
        }

        case DestinationType::Invalid:
        case DestinationType::Named:
            return std::nullopt;
    }

    try
    {
        PDFDocumentModifier modifier(document.data());
        PDFDocumentBuilder* builder = modifier.getBuilder();
        const PDFObjectReference action = builder->createActionGoTo(target);
        const PDFObjectReference annotation = builder->createAnnotationLink(page->getPageReference(), *area, action, LinkHighlightMode::Invert);

        PDFAppAnnotationResult result = Helper::finalize(modifier);
        if (result && created)
        {
            *created = annotation;
        }
        return result;
    }
    catch (const PDFException&)
    {
        return std::nullopt;
    }
}

PDFAppAnnotationResult PDFAppAnnotationOperations::markSearchResults(const PDFDocumentPointer& document,
                                                                     const PDFDocumentTextSearchMatches& matches,
                                                                     AnnotationType type,
                                                                     const PDFAppNewAnnotation& data,
                                                                     const PDFOperationControl* control,
                                                                     std::atomic<size_t>* processed,
                                                                     size_t* createdCount)
{
    using Helper = PDFAppAnnotationOperationsCreateHelper;

    if (createdCount)
    {
        *createdCount = 0;
    }
    if (document.isNull() || !isTextMarkupType(type) || matches.empty())
    {
        return std::nullopt;
    }

    try
    {
        PDFDocumentModifier modifier(document.data());
        PDFDocumentBuilder* builder = modifier.getBuilder();
        const QColor color = Helper::getColor(data.color, Helper::getDefaultMarkupColor(type));
        const QString author = Helper::getAuthor(data);
        const QString contents = Helper::getContents(data);
        const PDFReal opacity = Helper::getOpacity(data.opacity);

        size_t count = 0;
        for (size_t i = 0; i < matches.size() && count < MAXIMUM_CREATED_ANNOTATIONS; ++i)
        {
            if (i % Helper::CANCEL_CHECK_INTERVAL == 0)
            {
                if (PDFOperationControl::isOperationCancelled(control))
                {
                    return std::nullopt;
                }
                if (processed)
                {
                    processed->store(i, std::memory_order_relaxed);
                }
            }

            const PDFDocumentTextSearchMatch& match = matches[i];
            const PDFPage* page = Helper::getPage(document.data(), match.pageIndex);
            const QPolygonF quadrilaterals = Helper::getQuadrilaterals(match.quadrilaterals);
            if (!page || quadrilaterals.isEmpty())
            {
                continue;
            }

            if (Helper::createTextMarkup(builder, page->getPageReference(), type, quadrilaterals, color, author, contents, opacity).isValid())
            {
                ++count;
            }
        }

        if (count == 0 || PDFOperationControl::isOperationCancelled(control))
        {
            return std::nullopt;
        }

        PDFAppAnnotationResult result = Helper::finalize(modifier);
        if (result && createdCount)
        {
            *createdCount = count;
        }
        return result;
    }
    catch (const PDFException&)
    {
        return std::nullopt;
    }
}

bool PDFAppAnnotationOperations::isWebLinkAllowed(const QString& url)
{
    if (url.isEmpty() || url.size() > MAXIMUM_URL_LENGTH)
    {
        return false;
    }

    // No control, format (bidirectional overrides, the invisible tag characters
    // U+E0000 - U+E007F) or unassigned characters, no lone surrogates and no white
    // space - a link, which shows something else than it opens, is refused. The
    // characters are checked as code points (a format character outside of the
    // BMP is a surrogate pair).
    for (qsizetype i = 0; i < url.size(); ++i)
    {
        char32_t codePoint = url[i].unicode();
        if (url[i].isHighSurrogate() && i + 1 < url.size() && url[i + 1].isLowSurrogate())
        {
            codePoint = QChar::surrogateToUcs4(url[i], url[i + 1]);
            ++i;
        }
        else if (url[i].isSurrogate())
        {
            return false;
        }

        const QChar::Category category = QChar::category(codePoint);
        if (category == QChar::Other_Control || category == QChar::Other_Format || category == QChar::Other_NotAssigned || QChar::isSpace(codePoint))
        {
            return false;
        }
    }

    const QUrl parsed(url, QUrl::StrictMode);
    if (!parsed.isValid() || parsed.isRelative())
    {
        return false;
    }

    const QString scheme = parsed.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
    {
        return !parsed.host().isEmpty();
    }
    if (scheme == QLatin1String("mailto"))
    {
        const QString address = parsed.path();
        return address.contains(QLatin1Char('@')) && !address.startsWith(QLatin1Char('@')) && !address.endsWith(QLatin1Char('@'));
    }
    return false;
}

// ------------------------------------------------- MCP (step 7, package C)

/// Creation of the annotations of the assistants (annotations.add): every kind
/// in one modifier, every geometry bounded as the creation tools bound it
class PDFAppAnnotationOperationsMcpHelper
{
public:
    PDFAppAnnotationOperationsMcpHelper() = delete;

    using Helper = PDFAppAnnotationOperationsCreateHelper;

    /// Interval of the cancellation checks
    static constexpr size_t CANCEL_CHECK_INTERVAL = 16;

    /// Smallest and largest font size of a text box
    static constexpr PDFReal MINIMUM_FONT_SIZE = 4.0;
    static constexpr PDFReal MAXIMUM_FONT_SIZE = 144.0;

    /// Points with finite coordinates, \p minimum to \p maximum of them, with an
    /// extent (else an empty polygon)
    static QPolygonF getPoints(const QPolygonF& points, int minimum, int maximum)
    {
        if (points.size() < minimum || points.size() > maximum)
        {
            return QPolygonF();
        }
        for (const QPointF& point : points)
        {
            if (!Helper::isFinite(point))
            {
                return QPolygonF();
            }
        }

        // A shape of one point repeated has no extent
        const QRectF bounds = points.boundingRect();
        if (bounds.width() <= 0.0 && bounds.height() <= 0.0)
        {
            return QPolygonF();
        }
        return points;
    }

    /// Bounding rectangle of a line shape with the room of its border and endings
    static QRectF getShapeRect(const QPolygonF& points, PDFReal penWidth)
    {
        const PDFReal margin = penWidth * 2.0 + 8.0;
        return points.boundingRect().adjusted(-margin, -margin, margin, margin);
    }

    static void setCommonData(PDFDocumentBuilder* builder, PDFObjectReference annotation, const PDFAppAnnotationItem& item)
    {
        const PDFReal opacity = Helper::getOpacity(item.data.opacity);
        if (opacity < 1.0)
        {
            builder->setAnnotationOpacity(annotation, opacity);
        }

        if (!item.name.isEmpty())
        {
            // The unique name of the annotation (NM, ISO 32000-2 Table 166)
            PDFObjectFactory factory;
            factory.beginDictionary();
            factory.beginDictionaryItem("NM");
            factory << item.name.left(128);
            factory.endDictionaryItem();
            factory.endDictionary();
            builder->mergeTo(annotation, factory.takeObject());
        }
    }

    /// Creates one annotation, an invalid reference for an invalid item
    static PDFObjectReference create(PDFDocumentBuilder* builder, const PDFDocument* document, const PDFAppAnnotationItem& item)
    {
        const PDFPage* page = Helper::getPage(document, item.pageIndex);
        if (!page)
        {
            return PDFObjectReference();
        }

        const PDFObjectReference pageReference = page->getPageReference();
        const QString author = Helper::getAuthor(item.data);
        const QString contents = Helper::getContents(item.data);
        const PDFReal penWidth = Helper::getPenWidth(item.data.penWidth);
        const QColor fillColor = item.data.fillColor.isValid() ? Helper::getColor(item.data.fillColor, QColor()) : QColor();

        PDFObjectReference annotation;
        switch (item.kind)
        {
            case PDFAppAnnotationItem::Kind::TextMarkup:
            {
                const QPolygonF quadrilaterals = Helper::getQuadrilaterals(item.quadrilaterals);
                if (!PDFAppAnnotationOperations::isTextMarkupType(item.markupType) || quadrilaterals.isEmpty())
                {
                    return PDFObjectReference();
                }
                annotation = Helper::createTextMarkup(builder, pageReference, item.markupType, quadrilaterals,
                                                      Helper::getColor(item.data.color, Helper::getDefaultMarkupColor(item.markupType)),
                                                      author, contents, 1.0);
                break;
            }

            case PDFAppAnnotationItem::Kind::StickyNote:
            {
                if (!Helper::isFinite(item.point))
                {
                    return PDFObjectReference();
                }
                const QRectF rectangle(item.point.x(), item.point.y() - Helper::STICKY_NOTE_SIZE, Helper::STICKY_NOTE_SIZE, Helper::STICKY_NOTE_SIZE);
                annotation = builder->createAnnotationText(pageReference, rectangle, item.icon, author, QString(), contents, false);
                builder->setAnnotationColor(annotation, Helper::getColor(item.data.color, QColor(Qt::yellow)));
                break;
            }

            case PDFAppAnnotationItem::Kind::FreeText:
            {
                const std::optional<QRectF> area = Helper::getRectangle(item.rectangle);
                if (!area || contents.trimmed().isEmpty())
                {
                    return PDFObjectReference();
                }
                PDFFreeTextStyle style;
                style.fontSize = std::isfinite(item.fontSize) ? qBound(MINIMUM_FONT_SIZE, item.fontSize, MAXIMUM_FONT_SIZE) : 12.0;
                style.textColor = QColor(Qt::black);
                annotation = builder->createAnnotationFreeText(pageReference, *area, author, QString(), contents, style, false);
                const PDFReal borderWidth = item.data.color.isValid() ? penWidth : 0.0;
                if (borderWidth > 0.0)
                {
                    builder->setAnnotationColor(annotation, Helper::getColor(item.data.color, QColor(Qt::black)));
                }
                builder->setAnnotationBorder(annotation, 0.0, 0.0, borderWidth);
                break;
            }

            case PDFAppAnnotationItem::Kind::Square:
            case PDFAppAnnotationItem::Kind::Circle:
            {
                const std::optional<QRectF> area = Helper::getRectangle(item.rectangle);
                if (!area)
                {
                    return PDFObjectReference();
                }
                const QColor strokeColor = Helper::getColor(item.data.color, QColor(Qt::red));
                annotation = item.kind == PDFAppAnnotationItem::Kind::Square ? builder->createAnnotationSquare(pageReference, *area, penWidth, fillColor, strokeColor, author, QString(), contents)
                                                                             : builder->createAnnotationCircle(pageReference, *area, penWidth, fillColor, strokeColor, author, QString(), contents);
                break;
            }

            case PDFAppAnnotationItem::Kind::Line:
            case PDFAppAnnotationItem::Kind::Arrow:
            {
                const QPolygonF points = getPoints(item.points, 2, 2);
                if (points.isEmpty())
                {
                    return PDFObjectReference();
                }
                const AnnotationLineEnding ending = item.kind == PDFAppAnnotationItem::Kind::Arrow ? AnnotationLineEnding::OpenArrow : AnnotationLineEnding::None;
                annotation = builder->createAnnotationLine(pageReference, getShapeRect(points, penWidth), points[0], points[1], penWidth, fillColor,
                                                           Helper::getColor(item.data.color, QColor(Qt::red)), author, QString(), contents,
                                                           AnnotationLineEnding::None, ending);
                break;
            }

            case PDFAppAnnotationItem::Kind::Polygon:
            case PDFAppAnnotationItem::Kind::Polyline:
            {
                const bool isPolygon = item.kind == PDFAppAnnotationItem::Kind::Polygon;
                const QPolygonF points = getPoints(item.points, isPolygon ? 3 : 2, PDFAppAnnotationOperations::MAXIMUM_SHAPE_POINTS);
                if (points.isEmpty())
                {
                    return PDFObjectReference();
                }
                const QColor strokeColor = Helper::getColor(item.data.color, QColor(Qt::red));
                annotation = isPolygon ? builder->createAnnotationPolygon(pageReference, points, penWidth, fillColor, strokeColor, author, QString(), contents)
                                       : builder->createAnnotationPolyline(pageReference, points, penWidth, fillColor, strokeColor, author, QString(), contents,
                                                                           AnnotationLineEnding::None, AnnotationLineEnding::None);
                break;
            }

            case PDFAppAnnotationItem::Kind::Ink:
            {
                if (item.strokes.empty() || int(item.strokes.size()) > PDFAppAnnotationOperations::MAXIMUM_INK_STROKES)
                {
                    return PDFObjectReference();
                }
                Polygons strokes;
                for (const QPolygonF& stroke : item.strokes)
                {
                    const QPolygonF points = getPoints(stroke, 2, PDFAppAnnotationOperations::MAXIMUM_SHAPE_POINTS);
                    if (points.isEmpty())
                    {
                        return PDFObjectReference();
                    }
                    strokes.push_back(points);
                }
                annotation = builder->createAnnotationInk(pageReference, strokes, qMax(penWidth, 0.5), Helper::getColor(item.data.color, QColor(Qt::red)), author, QString(), contents);
                break;
            }

            case PDFAppAnnotationItem::Kind::Stamp:
            {
                const std::optional<QRectF> area = Helper::getRectangle(item.rectangle);
                if (!area)
                {
                    return PDFObjectReference();
                }
                annotation = builder->createAnnotationStamp(pageReference, *area, item.stamp, author, QString(), contents);
                if (item.data.color.isValid())
                {
                    builder->setAnnotationColor(annotation, Helper::getColor(item.data.color, QColor(Qt::red)));
                }
                break;
            }

            case PDFAppAnnotationItem::Kind::WebLink:
            {
                const std::optional<QRectF> area = Helper::getRectangle(item.rectangle);
                const QString link = item.url.trimmed();
                if (!area || !PDFAppAnnotationOperations::isWebLinkAllowed(link))
                {
                    return PDFObjectReference();
                }

                // ISO 32000-2 12.6.4.8: the URI is 7-bit ASCII (as createWebLink)
                bool isAscii = true;
                for (const QChar character : link)
                {
                    isAscii = isAscii && character.unicode() < 0x80;
                }
                const QString storedLink = isAscii ? link : QString::fromLatin1(QUrl(link, QUrl::StrictMode).toEncoded(QUrl::FullyEncoded));
                if (storedLink.isEmpty())
                {
                    return PDFObjectReference();
                }
                annotation = builder->createAnnotationLink(pageReference, *area, storedLink, LinkHighlightMode::Invert);
                break;
            }

            case PDFAppAnnotationItem::Kind::PageLink:
            {
                const std::optional<QRectF> area = Helper::getRectangle(item.rectangle);
                const PDFPage* targetPage = Helper::getPage(document, item.targetPageIndex);
                if (!area || !targetPage)
                {
                    return PDFObjectReference();
                }
                const PDFObjectReference action = builder->createActionGoTo(PDFDestination::createFit(targetPage->getPageReference()));
                annotation = builder->createAnnotationLink(pageReference, *area, action, LinkHighlightMode::Invert);
                break;
            }
        }

        if (!annotation.isValid())
        {
            return PDFObjectReference();
        }

        setCommonData(builder, annotation, item);
        builder->updateAnnotationAppearanceStreams(annotation);
        return annotation;
    }
};

PDFAppAnnotationResult PDFAppAnnotationOperations::createAnnotations(const PDFDocumentPointer& document,
                                                                     const std::vector<PDFAppAnnotationItem>& items,
                                                                     const PDFOperationControl* control,
                                                                     std::vector<PDFObjectReference>* created,
                                                                     int* invalidIndex)
{
    using Helper = PDFAppAnnotationOperationsCreateHelper;
    using McpHelper = PDFAppAnnotationOperationsMcpHelper;

    if (created)
    {
        created->clear();
    }
    if (invalidIndex)
    {
        *invalidIndex = -1;
    }
    if (document.isNull() || items.empty() || items.size() > MAXIMUM_CREATED_ANNOTATIONS)
    {
        return std::nullopt;
    }

    try
    {
        PDFDocumentModifier modifier(document.data());
        PDFDocumentBuilder* builder = modifier.getBuilder();

        std::vector<PDFObjectReference> references;
        references.reserve(items.size());
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i % McpHelper::CANCEL_CHECK_INTERVAL == 0 && PDFOperationControl::isOperationCancelled(control))
            {
                return std::nullopt;
            }

            const PDFObjectReference annotation = McpHelper::create(builder, document.data(), items[i]);
            if (!annotation.isValid())
            {
                if (invalidIndex)
                {
                    *invalidIndex = int(i);
                }
                return std::nullopt;
            }
            references.push_back(annotation);
        }

        PDFAppAnnotationResult result = Helper::finalize(modifier);
        if (result && created)
        {
            *created = std::move(references);
        }
        return result;
    }
    catch (const PDFException&)
    {
        return std::nullopt;
    }
}

}   // namespace pdf
