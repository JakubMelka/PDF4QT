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

#include "pdfpagedrawing.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfobjectutils.h"
#include "pdfpage.h"
#include "pdfpainterutils.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRegularExpression>
#include <QSvgRenderer>
#include <QTextOption>
#include <QTransform>
#include <QXmlStreamReader>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the drawing: the geometry, the checks, the painting and the JSON of the elements
class PDFPageDrawingHelper
{
public:
    PDFPageDrawingHelper() = delete;

    using Kind = PDFPageDrawing::Kind;
    using Element = PDFPageDrawing::Element;
    using Alignment = PDFPageDrawing::Alignment;

    /// Widest stroke and largest font of an element in points (an untrusted drawing of MCP)
    static constexpr double MAXIMUM_PEN_WIDTH = 100.0;
    static constexpr double MAXIMUM_FONT_SIZE = 1000.0;

    /// Largest coordinate of an element in points (ISO 32000-2, Annex C: the largest page is
    /// 14 400 units of user space; a drawing far away from every page is an error)
    static constexpr double MAXIMUM_COORDINATE = 1.0e6;

    /// The ids of the kinds in the JSON of MCP (the parameter elements of Sign by Hand)
    static constexpr std::array<std::pair<Kind, const char*>, 9> KIND_IDS = { { { Kind::Text, "text" },
                                                                              { Kind::FreehandCurve, "freehand-curve" },
                                                                              { Kind::Line, "line" },
                                                                              { Kind::Rectangle, "rectangle" },
                                                                              { Kind::RoundedRectangle, "rounded-rectangle" },
                                                                              { Kind::Dot, "dot" },
                                                                              { Kind::AcceptMark, "accept-mark" },
                                                                              { Kind::RejectMark, "reject-mark" },
                                                                              { Kind::SvgImage, "svg-image" } } };

    static QString getKindId(Kind kind)
    {
        for (const auto& [value, id] : KIND_IDS)
        {
            if (value == kind)
            {
                return QString::fromLatin1(id);
            }
        }
        return QString();
    }

    static std::optional<Kind> findKind(const QString& id)
    {
        for (const auto& [value, kindId] : KIND_IDS)
        {
            if (id == QLatin1String(kindId))
            {
                return value;
            }
        }
        return std::nullopt;
    }

    /// The element has a box (text, shapes, marks, image), the other ones points (curve, line)
    static bool hasRectangle(Kind kind)
    {
        return kind != Kind::FreehandCurve && kind != Kind::Line;
    }

    static bool isFinite(const QRectF& rectangle)
    {
        return std::isfinite(rectangle.x()) && std::isfinite(rectangle.y()) && std::isfinite(rectangle.width()) && std::isfinite(rectangle.height()) &&
               std::abs(rectangle.x()) <= MAXIMUM_COORDINATE && std::abs(rectangle.y()) <= MAXIMUM_COORDINATE &&
               std::abs(rectangle.width()) <= MAXIMUM_COORDINATE && std::abs(rectangle.height()) <= MAXIMUM_COORDINATE;
    }

    static bool isFinite(const QPointF& point)
    {
        return std::isfinite(point.x()) && std::isfinite(point.y()) && std::abs(point.x()) <= MAXIMUM_COORDINATE && std::abs(point.y()) <= MAXIMUM_COORDINATE;
    }

    /// The box of the element on its page, with its stroke (the /BBox of an appearance holds all
    /// that is painted)
    static QRectF getElementBoundingBox(const Element& element)
    {
        const double halfPen = element.style.penWidth > 0.0 ? element.style.penWidth * 0.5 : 0.0;
        switch (element.kind)
        {
            case Kind::Text:
            {
                // The text turns about the center of its box (8.3.4: counter clockwise in user space)
                const QRectF rectangle = element.rectangle.normalized();
                QTransform matrix;
                matrix.translate(rectangle.center().x(), rectangle.center().y());
                matrix.rotate(element.style.angle);
                matrix.translate(-rectangle.center().x(), -rectangle.center().y());
                return matrix.map(QPolygonF(rectangle)).boundingRect();
            }

            case Kind::FreehandCurve:
            case Kind::Line:
            {
                QPolygonF polygon;
                for (const QPointF& point : element.points)
                {
                    polygon << point;
                }
                return polygon.boundingRect().adjusted(-halfPen, -halfPen, halfPen, halfPen);
            }

            case Kind::Dot:
            case Kind::SvgImage:
                return element.rectangle.normalized();

            case Kind::Rectangle:
            case Kind::RoundedRectangle:
            case Kind::AcceptMark:
            case Kind::RejectMark:
                return element.rectangle.normalized().adjusted(-halfPen, -halfPen, halfPen, halfPen);
        }
        return QRectF();
    }

    /// The SVG image keeps the limits (plan 7.2): at most MAXIMUM_SVG_SIZE bytes, well formed XML
    /// without a document type definition (no entities), no reference outside of itself (an
    /// attribute href only data:)
    static bool isSvgSafe(const QByteArray& data, QString* errorMessage)
    {
        if (data.isEmpty() || data.size() > PDFPageDrawing::MAXIMUM_SVG_SIZE)
        {
            *errorMessage = PDFPageDrawing::tr("The SVG image is empty or larger than 4 MB.");
            return false;
        }

        QXmlStreamReader reader(data);
        while (!reader.atEnd())
        {
            const QXmlStreamReader::TokenType token = reader.readNext();
            if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference)
            {
                *errorMessage = PDFPageDrawing::tr("The SVG image has a document type definition, which is not allowed.");
                return false;
            }

            if (token == QXmlStreamReader::StartElement)
            {
                for (const QXmlStreamAttribute& attribute : reader.attributes())
                {
                    if (attribute.name() == QLatin1String("href") && !attribute.value().trimmed().startsWith(QLatin1String("data:"), Qt::CaseInsensitive) &&
                        !attribute.value().trimmed().startsWith(QLatin1Char('#')))
                    {
                        *errorMessage = PDFPageDrawing::tr("The SVG image refers to an external file, which is not allowed.");
                        return false;
                    }
                }
            }
        }

        if (reader.hasError())
        {
            *errorMessage = PDFPageDrawing::tr("The SVG image is damaged.");
            return false;
        }
        return true;
    }

    /// Checks one element (plan 7.2); the page is checked by the caller
    static bool isElementValid(const Element& element, qsizetype* pointCount, QString* errorMessage)
    {
        const PDFPageDrawing::Style& style = element.style;
        if (!std::isfinite(style.penWidth) || style.penWidth < 0.0 || style.penWidth > MAXIMUM_PEN_WIDTH ||
            !std::isfinite(style.fontSize) || style.fontSize <= 0.0 || style.fontSize > MAXIMUM_FONT_SIZE ||
            !std::isfinite(style.angle) || std::abs(style.angle) > 360.0 || !style.penColor.isValid() ||
            !std::isfinite(element.cornerRadius) || element.cornerRadius < 0.0 || element.cornerRadius > MAXIMUM_COORDINATE)
        {
            *errorMessage = PDFPageDrawing::tr("The style of an element of the drawing is not valid.");
            return false;
        }

        if (hasRectangle(element.kind))
        {
            if (!isFinite(element.rectangle) || element.rectangle.normalized().isEmpty() || !element.points.empty())
            {
                *errorMessage = PDFPageDrawing::tr("An element of the drawing has no valid box.");
                return false;
            }
        }
        else
        {
            const size_t requiredCount = element.kind == Kind::Line ? 2 : 0;
            if ((requiredCount && element.points.size() != requiredCount) || element.points.size() < 2 ||
                !std::all_of(element.points.cbegin(), element.points.cend(), [](const QPointF& point) { return isFinite(point); }))
            {
                *errorMessage = PDFPageDrawing::tr("A line or a curve of the drawing has no valid points.");
                return false;
            }
            *pointCount += qsizetype(element.points.size());
        }

        if (element.kind == Kind::Text && (element.text.trimmed().isEmpty() || element.text.size() > PDFPageDrawing::MAXIMUM_TEXT_LENGTH))
        {
            *errorMessage = PDFPageDrawing::tr("A text of the drawing is empty or longer than %1 characters.").arg(PDFPageDrawing::MAXIMUM_TEXT_LENGTH);
            return false;
        }

        if (element.kind != Kind::Text && !element.text.isEmpty())
        {
            *errorMessage = PDFPageDrawing::tr("Only a text label of the drawing has a text.");
            return false;
        }

        if (element.kind == Kind::SvgImage ? !isSvgSafe(element.svgData, errorMessage) : !element.svgData.isEmpty())
        {
            if (element.kind != Kind::SvgImage)
            {
                *errorMessage = PDFPageDrawing::tr("Only an SVG image of the drawing has the data of an image.");
            }
            return false;
        }
        return true;
    }

    /// A color of a stream: opaque for PDF/A-1 (ISO 19005-1, 6.4: no transparency)
    static QColor getColor(const QColor& color, bool isOpaqueRequired)
    {
        QColor result = color;
        if (isOpaqueRequired)
        {
            result.setAlpha(255);
        }
        return result;
    }

    /// Paints one element in the user space of its page (the painter has the y axis upward)
    static void drawElement(QPainter* painter, const Element& element, bool isOpaqueRequired)
    {
        PDFPainterStateGuard guard(painter);

        const PDFPageDrawing::Style& style = element.style;
        QPen pen(getColor(style.penColor, isOpaqueRequired), style.penWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(style.penWidth > 0.0 ? pen : QPen(Qt::NoPen));
        painter->setBrush(style.fillColor.isValid() ? QBrush(getColor(style.fillColor, isOpaqueRequired)) : QBrush(Qt::NoBrush));

        const QRectF rectangle = element.rectangle.normalized();
        switch (element.kind)
        {
            case Kind::Text:
            {
                QFont font;
                if (!style.fontFamily.isEmpty())
                {
                    font.setFamily(style.fontFamily);
                }
                font.setPointSizeF(style.fontSize);
                font.setHintingPreference(QFont::PreferNoHinting);
                painter->setFont(font);
                painter->setPen(getColor(style.penColor, isOpaqueRequired));
                painter->setBrush(Qt::NoBrush);

                // The text turns about the center of its box counter clockwise, its lines go down
                painter->translate(rectangle.center());
                painter->rotate(style.angle);
                painter->scale(1.0, -1.0);

                Qt::Alignment alignment = Qt::AlignVCenter;
                switch (style.alignment)
                {
                    case Alignment::Left:
                        alignment |= Qt::AlignLeft;
                        break;
                    case Alignment::Center:
                        alignment |= Qt::AlignHCenter;
                        break;
                    case Alignment::Right:
                        alignment |= Qt::AlignRight;
                        break;
                }
                QTextOption option(alignment);
                option.setWrapMode(QTextOption::WordWrap);
                painter->drawText(QRectF(-rectangle.width() * 0.5, -rectangle.height() * 0.5, rectangle.width(), rectangle.height()), element.text, option);
                break;
            }

            case Kind::FreehandCurve:
            {
                QPainterPath path(element.points.front());
                for (size_t i = 1; i < element.points.size(); ++i)
                {
                    path.lineTo(element.points[i]);
                }
                painter->strokePath(path, painter->pen());
                break;
            }

            case Kind::Line:
                painter->drawLine(element.points[0], element.points[1]);
                break;

            case Kind::Rectangle:
                painter->drawRect(rectangle);
                break;

            case Kind::RoundedRectangle:
                painter->drawRoundedRect(rectangle, element.cornerRadius, element.cornerRadius);
                break;

            case Kind::Dot:
                painter->setPen(Qt::NoPen);
                painter->setBrush(getColor(style.penColor, isOpaqueRequired));
                painter->drawEllipse(rectangle);
                break;

            case Kind::AcceptMark:
            {
                // A check mark (the y axis goes up: the bottom of the box is its top() value)
                const QPointF points[3] = { QPointF(rectangle.left() + rectangle.width() * 0.15, rectangle.top() + rectangle.height() * 0.5),
                                            QPointF(rectangle.left() + rectangle.width() * 0.4, rectangle.top() + rectangle.height() * 0.2),
                                            QPointF(rectangle.left() + rectangle.width() * 0.85, rectangle.top() + rectangle.height() * 0.85) };
                painter->setBrush(Qt::NoBrush);
                painter->drawPolyline(points, 3);
                break;
            }

            case Kind::RejectMark:
            {
                const QRectF inner = rectangle.adjusted(rectangle.width() * 0.15, rectangle.height() * 0.15, -rectangle.width() * 0.15, -rectangle.height() * 0.15);
                painter->drawLine(inner.topLeft(), inner.bottomRight());
                painter->drawLine(inner.bottomLeft(), inner.topRight());
                break;
            }

            case Kind::SvgImage:
            {
                QSvgRenderer renderer(element.svgData);
                if (renderer.isValid())
                {
                    painter->translate(rectangle.left(), rectangle.bottom());
                    painter->scale(1.0, -1.0);
                    renderer.render(painter, QRectF(0.0, 0.0, rectangle.width(), rectangle.height()));
                }
                break;
            }
        }
    }

    /// The media box of a page, inherited of the page tree (Table 31) - one walk with a visited
    /// set and a depth limit; an invalid rectangle for none
    static QRectF getMediaBox(const PDFObjectStorage* storage, PDFObjectReference page)
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        PDFObjectWalk walk(PDFObjectWalk::DEFAULT_MAXIMUM_DEPTH);
        walk.enterReference(page, 0);
        const PDFDictionary* node = storage->getDictionaryFromObject(storage->getObjectByReference(page));
        for (int depth = 0; node; ++depth)
        {
            const QRectF mediaBox = loader.readRectangle(node->get("MediaBox"), QRectF());
            if (mediaBox.isValid())
            {
                return mediaBox;
            }

            const PDFObject& parent = node->get("Parent");
            if (!parent.isReference() || !walk.enter(parent, depth + 1))
            {
                break;
            }
            node = storage->getDictionaryFromObject(storage->getObject(parent));
        }
        return QRectF();
    }

    /// The page objects of the document of the builder in the order of the pages: the leaves of the
    /// whole page tree (ISO 32000-2, 7.7.3.2 - an intermediate /Pages node is never a page), one
    /// visited set (PDFPage::parse); the page tree itself is not changed
    static std::vector<PDFObjectReference> getPages(const PDFDocumentBuilder* builder)
    {
        std::vector<PDFObjectReference> pages;
        const PDFObjectStorage* storage = builder->getStorage();
        const PDFDictionary* trailer = storage->getDictionaryFromObject(storage->getTrailerDictionary());
        const PDFDictionary* catalog = trailer ? storage->getDictionaryFromObject(trailer->get("Root")) : nullptr;
        if (catalog)
        {
            for (const PDFPage& page : PDFPage::parse(storage, catalog->get("Pages")))
            {
                pages.push_back(page.getPageReference());
            }
        }
        return pages;
    }

    /// A small content stream of the operators (the brackets q … Q of the appended drawing)
    static PDFObjectReference addOperatorStream(PDFDocumentBuilder* builder, const QByteArray& operators)
    {
        PDFDictionaryBuilder dictionary;
        dictionary.addEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(operators.size()));
        return builder->addObject(PDFObject::createStream(PDFStream(std::move(dictionary), QByteArray(operators))));
    }

    /// A color as "#rrggbb" (the JSON of MCP; alpha is not written)
    static QString getColorText(const QColor& color)
    {
        return color.name(QColor::HexRgb);
    }

    /// A color of the JSON: exactly "#rrggbb"
    static std::optional<QColor> readColor(const QJsonValue& value)
    {
        static const QRegularExpression expression(QStringLiteral("^#[0-9a-fA-F]{6}$"));
        if (!value.isString() || !expression.match(value.toString()).hasMatch())
        {
            return std::nullopt;
        }
        return QColor::fromString(value.toString());
    }

    /// A finite number of the JSON in a range
    static std::optional<double> readNumber(const QJsonValue& value, double minimum, double maximum)
    {
        if (!value.isDouble())
        {
            return std::nullopt;
        }
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < minimum || number > maximum)
        {
            return std::nullopt;
        }
        return number;
    }

    /// A point [x, y] of the JSON
    static std::optional<QPointF> readPoint(const QJsonValue& value)
    {
        const QJsonArray array = value.toArray();
        if (!value.isArray() || array.size() != 2)
        {
            return std::nullopt;
        }
        const std::optional<double> x = readNumber(array.at(0), -MAXIMUM_COORDINATE, MAXIMUM_COORDINATE);
        const std::optional<double> y = readNumber(array.at(1), -MAXIMUM_COORDINATE, MAXIMUM_COORDINATE);
        if (!x || !y)
        {
            return std::nullopt;
        }
        return QPointF(*x, *y);
    }

    /// Reads the style object of the JSON into \p style (strictly: an unknown key is an error)
    static bool readStyle(const QJsonValue& value, PDFPageDrawing::Style* style, QString* errorMessage)
    {
        if (!value.isObject())
        {
            *errorMessage = PDFPageDrawing::tr("The style of an element must be an object.");
            return false;
        }

        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            const QString& key = it.key();
            bool isValid = true;
            if (key == QLatin1String("penColor") || key == QLatin1String("fillColor"))
            {
                const std::optional<QColor> color = readColor(it.value());
                isValid = color.has_value();
                (key == QLatin1String("penColor") ? style->penColor : style->fillColor) = color.value_or(QColor());
            }
            else if (key == QLatin1String("penWidth"))
            {
                const std::optional<double> number = readNumber(it.value(), 0.0, MAXIMUM_PEN_WIDTH);
                isValid = number.has_value();
                style->penWidth = number.value_or(0.0);
            }
            else if (key == QLatin1String("fontSize"))
            {
                const std::optional<double> number = readNumber(it.value(), 1.0, MAXIMUM_FONT_SIZE);
                isValid = number.has_value();
                style->fontSize = number.value_or(12.0);
            }
            else if (key == QLatin1String("angle"))
            {
                const std::optional<double> number = readNumber(it.value(), -360.0, 360.0);
                isValid = number.has_value();
                style->angle = number.value_or(0.0);
            }
            else if (key == QLatin1String("alignment"))
            {
                const QString alignment = it.value().toString();
                isValid = it.value().isString() && (alignment == QLatin1String("left") || alignment == QLatin1String("center") || alignment == QLatin1String("right"));
                style->alignment = alignment == QLatin1String("center") ? Alignment::Center : (alignment == QLatin1String("right") ? Alignment::Right : Alignment::Left);
            }
            else
            {
                *errorMessage = PDFPageDrawing::tr("Unknown key \"%1\" of the style of an element.").arg(key);
                return false;
            }

            if (!isValid)
            {
                *errorMessage = PDFPageDrawing::tr("The value of \"%1\" of the style of an element is not valid.").arg(key);
                return false;
            }
        }
        return true;
    }

    /// Reads one element of the JSON (strictly)
    static std::optional<Element> readElement(const QJsonValue& value, QString* errorMessage)
    {
        if (!value.isObject())
        {
            *errorMessage = PDFPageDrawing::tr("An element of the drawing must be an object.");
            return std::nullopt;
        }

        const QJsonObject object = value.toObject();
        const std::optional<Kind> kind = findKind(object.value(QStringLiteral("kind")).toString());
        if (!object.value(QStringLiteral("kind")).isString() || !kind)
        {
            *errorMessage = PDFPageDrawing::tr("An element of the drawing has no known kind.");
            return std::nullopt;
        }

        // Owner answer Q6.7, R6-20: an assistant never places an image (a signature of another person could be pasted)
        if (*kind == Kind::SvgImage)
        {
            *errorMessage = PDFPageDrawing::tr("Images are not allowed in a drawing of the assistant.");
            return std::nullopt;
        }

        Element element;
        element.kind = *kind;
        bool hasPage = false;
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            const QString& key = it.key();
            if (key == QLatin1String("kind"))
            {
                continue;
            }

            if (key == QLatin1String("page"))
            {
                const std::optional<double> page = readNumber(it.value(), 1.0, double(std::numeric_limits<int>::max()));
                if (!page || std::floor(*page) != *page)
                {
                    *errorMessage = PDFPageDrawing::tr("The page of an element must be a whole number from 1.");
                    return std::nullopt;
                }
                element.pageIndex = PDFInteger(*page) - 1;
                hasPage = true;
            }
            else if (key == QLatin1String("rect") && hasRectangle(element.kind))
            {
                const QJsonArray array = it.value().toArray();
                std::array<std::optional<double>, 4> values;
                for (int i = 0; i < 4 && it.value().isArray() && array.size() == 4; ++i)
                {
                    values[i] = readNumber(array.at(i), -MAXIMUM_COORDINATE, MAXIMUM_COORDINATE);
                }
                if (!values[0] || !values[1] || !values[2] || !values[3] || *values[2] <= 0.0 || *values[3] <= 0.0)
                {
                    *errorMessage = PDFPageDrawing::tr("The box of an element must be [x, y, width, height] with a positive size.");
                    return std::nullopt;
                }
                element.rectangle = QRectF(*values[0], *values[1], *values[2], *values[3]);
            }
            else if (key == QLatin1String("points") && !hasRectangle(element.kind))
            {
                const QJsonArray array = it.value().toArray();
                if (!it.value().isArray() || array.size() > PDFPageDrawing::MAXIMUM_POINT_COUNT)
                {
                    *errorMessage = PDFPageDrawing::tr("The points of an element must be an array of [x, y].");
                    return std::nullopt;
                }
                for (const QJsonValue& pointValue : array)
                {
                    const std::optional<QPointF> point = readPoint(pointValue);
                    if (!point)
                    {
                        *errorMessage = PDFPageDrawing::tr("The points of an element must be an array of [x, y].");
                        return std::nullopt;
                    }
                    element.points.push_back(*point);
                }
            }
            else if (key == QLatin1String("text") && element.kind == Kind::Text)
            {
                if (!it.value().isString())
                {
                    *errorMessage = PDFPageDrawing::tr("The text of an element must be a string.");
                    return std::nullopt;
                }
                element.text = it.value().toString();
            }
            else if (key == QLatin1String("radius") && element.kind == Kind::RoundedRectangle)
            {
                const std::optional<double> radius = readNumber(it.value(), 0.0, MAXIMUM_COORDINATE);
                if (!radius)
                {
                    *errorMessage = PDFPageDrawing::tr("The radius of a rounded rectangle is not valid.");
                    return std::nullopt;
                }
                element.cornerRadius = *radius;
            }
            else if (key == QLatin1String("style"))
            {
                if (!readStyle(it.value(), &element.style, errorMessage))
                {
                    return std::nullopt;
                }
            }
            else
            {
                *errorMessage = PDFPageDrawing::tr("Unknown key \"%1\" of an element of the kind \"%2\".").arg(key, getKindId(element.kind));
                return std::nullopt;
            }
        }

        if (!hasPage)
        {
            *errorMessage = PDFPageDrawing::tr("An element of the drawing has no page.");
            return std::nullopt;
        }

        qsizetype pointCount = 0;
        if (!isElementValid(element, &pointCount, errorMessage))
        {
            return std::nullopt;
        }
        return element;
    }
};

PDFPageDrawing::PDFPageDrawing() :
    m_data(std::make_shared<const Data>())
{

}

PDFPageDrawing::PDFPageDrawing(std::vector<Element> elements) :
    m_data(std::make_shared<const Data>(Data{ std::move(elements) }))
{

}

const std::vector<PDFPageDrawing::Element>& PDFPageDrawing::getElements() const
{
    return m_data->elements;
}

bool PDFPageDrawing::isEmpty() const
{
    return m_data->elements.empty();
}

std::vector<PDFInteger> PDFPageDrawing::getPageIndices() const
{
    std::vector<PDFInteger> pageIndices;
    for (const Element& element : m_data->elements)
    {
        pageIndices.push_back(element.pageIndex);
    }
    std::sort(pageIndices.begin(), pageIndices.end());
    pageIndices.erase(std::unique(pageIndices.begin(), pageIndices.end()), pageIndices.end());
    return pageIndices;
}

PDFInteger PDFPageDrawing::getFirstPageIndex() const
{
    const std::vector<PDFInteger> pageIndices = getPageIndices();
    return pageIndices.empty() ? -1 : pageIndices.front();
}

QRectF PDFPageDrawing::getBoundingBox(PDFInteger pageIndex) const
{
    QRectF boundingBox;
    for (const Element& element : m_data->elements)
    {
        if (element.pageIndex == pageIndex)
        {
            boundingBox = boundingBox.united(PDFPageDrawingHelper::getElementBoundingBox(element));
        }
    }
    return boundingBox;
}

bool PDFPageDrawing::isValid(const PDFDocument* document, QString* errorMessage) const
{
    QString message;
    const std::vector<Element>& elements = m_data->elements;
    const size_t pageCount = document && document->getCatalog() ? document->getCatalog()->getPageCount() : 0;

    qsizetype pointCount = 0;
    bool isValid = true;
    if (elements.size() > size_t(MAXIMUM_ELEMENT_COUNT))
    {
        message = tr("The drawing has more than %1 elements.").arg(MAXIMUM_ELEMENT_COUNT);
        isValid = false;
    }

    for (const Element& element : elements)
    {
        if (!isValid)
        {
            break;
        }

        if (element.pageIndex < 0 || (document && size_t(element.pageIndex) >= pageCount))
        {
            message = tr("An element of the drawing is on a page, which the document does not have.");
            isValid = false;
        }
        else
        {
            isValid = PDFPageDrawingHelper::isElementValid(element, &pointCount, &message);
        }
    }

    if (isValid && pointCount > MAXIMUM_POINT_COUNT)
    {
        message = tr("The curves of the drawing have more than %1 points.").arg(MAXIMUM_POINT_COUNT);
        isValid = false;
    }

    if (errorMessage)
    {
        *errorMessage = message;
    }
    return isValid;
}

PDFObjectReference PDFPageDrawing::createFormXObject(PDFDocumentBuilder* builder, PDFInteger pageIndex, bool isOpaqueRequired) const
{
    const std::vector<PDFObjectReference> pages = PDFPageDrawingHelper::getPages(builder);
    const QRectF boundingBox = getBoundingBox(pageIndex);
    if (pageIndex < 0 || size_t(pageIndex) >= pages.size() || boundingBox.isEmpty())
    {
        return PDFObjectReference();
    }

    const QRectF mediaBox = PDFPageDrawingHelper::getMediaBox(builder->getStorage(), pages[pageIndex]);
    if (!mediaBox.isValid())
    {
        return PDFObjectReference();
    }

    // The elements use the unrotated user space of the page with the origin of its media box; the
    // page of QPdfWriter starts at zero, the /BBox is in that space (the old appearance of the
    // Signature plugin). The form is painted with a saved graphics state (8.10.1).
    PDFContentStreamBuilder contentBuilder(mediaBox.size(), PDFContentStreamBuilder::CoordinateSystem::PDF);
    QPainter* painter = contentBuilder.begin();
    painter->translate(-mediaBox.topLeft());
    for (const Element& element : m_data->elements)
    {
        if (element.pageIndex == pageIndex)
        {
            PDFPageDrawingHelper::drawElement(painter, element, isOpaqueRequired);
        }
    }
    PDFContentStreamBuilder::ContentStream contentStream = contentBuilder.end(painter);

    const std::vector<PDFObject> copiedObjects = builder->copyFrom({ contentStream.resources, contentStream.contents }, contentStream.document.getStorage(), true);
    if (copiedObjects.size() != 2 || !copiedObjects[0].isReference() || !copiedObjects[1].isReference())
    {
        return PDFObjectReference();
    }

    const PDFObjectReference formReference = copiedObjects[1].getReference();
    PDFObjectFactory factory;
    factory.beginDictionary();
    factory.beginDictionaryItem("Type");
    factory << WrapName("XObject");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Subtype");
    factory << WrapName("Form");
    factory.endDictionaryItem();
    factory.beginDictionaryItem("BBox");
    factory << boundingBox.translated(-mediaBox.topLeft());
    factory.endDictionaryItem();
    factory.beginDictionaryItem("Resources");
    factory << copiedObjects[0].getReference();
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->mergeTo(formReference, factory.takeObject());
    return formReference;
}

bool PDFPageDrawing::appendToPages(PDFDocumentBuilder* builder, QString* errorMessage) const
{
    const std::vector<PDFObjectReference> pages = PDFPageDrawingHelper::getPages(builder);
    for (const PDFInteger pageIndex : getPageIndices())
    {
        if (pageIndex < 0 || size_t(pageIndex) >= pages.size())
        {
            *errorMessage = tr("An element of the drawing is on a page, which the document does not have.");
            return false;
        }

        // R6-17: a page without a valid media box (also an inherited one, Table 31) is an error
        const PDFObjectReference page = pages[pageIndex];
        const QRectF mediaBox = PDFPageDrawingHelper::getMediaBox(builder->getStorage(), page);
        if (!mediaBox.isValid())
        {
            *errorMessage = tr("Page %1 has no valid media box.").arg(pageIndex + 1);
            return false;
        }

        // The page content builder reads the media box of the page itself: an inherited one is
        // written to the page (the same value)
        const PDFDictionary* pageDictionary = builder->getDictionaryFromObject(builder->getObjectByReference(page));
        if (!pageDictionary || !pageDictionary->get("MediaBox").isArray())
        {
            PDFObjectFactory factory;
            factory.beginDictionary();
            factory.beginDictionaryItem("MediaBox");
            factory << mediaBox;
            factory.endDictionaryItem();
            factory.endDictionary();
            builder->mergeTo(page, factory.takeObject());
        }

        PDFPageContentStreamBuilder pageBuilder(builder, PDFContentStreamBuilder::CoordinateSystem::PDF, PDFPageContentStreamBuilder::Mode::PlaceAfter);
        QPainter* painter = pageBuilder.begin(page);
        if (!painter)
        {
            *errorMessage = tr("Page %1 has no valid media box.").arg(pageIndex + 1);
            return false;
        }

        painter->translate(-mediaBox.topLeft());
        for (const Element& element : m_data->elements)
        {
            if (element.pageIndex == pageIndex)
            {
                PDFPageDrawingHelper::drawElement(painter, element, false);
            }
        }
        pageBuilder.end(painter);

        // 5.5: the old content keeps its graphics state to itself - [q, old content, Q, the drawing
        // in q … Q moved to the origin of the media box]; the state of the page does not change
        const PDFDictionary* updatedPage = builder->getDictionaryFromObject(builder->getObjectByReference(page));
        const PDFObject contentsObject = updatedPage ? builder->getObject(updatedPage->get("Contents")) : PDFObject();
        if (!contentsObject.isArray() || contentsObject.getArray()->getCount() == 0)
        {
            *errorMessage = tr("The drawing could not be added to page %1.").arg(pageIndex + 1);
            return false;
        }

        const PDFArray* contents = contentsObject.getArray();
        std::vector<PDFObject> items;
        items.push_back(PDFObject::createReference(PDFPageDrawingHelper::addOperatorStream(builder, "q\n")));
        for (size_t i = 0; i + 1 < contents->getCount(); ++i)
        {
            items.push_back(contents->getItem(i));
        }

        const QByteArray shift = "Q\nq\n1 0 0 1 " + PDFDocumentBuilder::formatPDFReal(mediaBox.left()) + " " + PDFDocumentBuilder::formatPDFReal(mediaBox.top()) + " cm\n";
        items.push_back(PDFObject::createReference(PDFPageDrawingHelper::addOperatorStream(builder, shift)));
        items.push_back(contents->getItem(contents->getCount() - 1));
        items.push_back(PDFObject::createReference(PDFPageDrawingHelper::addOperatorStream(builder, "Q\n")));

        PDFDictionaryBuilder pageUpdate(*builder->getDictionaryFromObject(builder->getObjectByReference(page)));
        pageUpdate.setEntry(PDFInplaceOrMemoryString("Contents"), PDFObject::createArray(PDFArrayBuilder(std::move(items))));
        builder->setObject(page, PDFObject::createDictionary(std::move(pageUpdate)));
    }
    return true;
}

QJsonArray PDFPageDrawing::toJson() const
{
    QJsonArray array;
    for (const Element& element : m_data->elements)
    {
        // Q6.7: an image is never in the JSON of MCP
        if (element.kind == Kind::SvgImage)
        {
            continue;
        }

        QJsonObject object;
        object.insert(QStringLiteral("kind"), PDFPageDrawingHelper::getKindId(element.kind));
        object.insert(QStringLiteral("page"), qint64(element.pageIndex + 1));
        if (PDFPageDrawingHelper::hasRectangle(element.kind))
        {
            const QRectF rectangle = element.rectangle.normalized();
            object.insert(QStringLiteral("rect"), QJsonArray({ rectangle.x(), rectangle.y(), rectangle.width(), rectangle.height() }));
        }
        else
        {
            QJsonArray points;
            for (const QPointF& point : element.points)
            {
                points.append(QJsonArray({ point.x(), point.y() }));
            }
            object.insert(QStringLiteral("points"), points);
        }

        if (element.kind == Kind::Text)
        {
            object.insert(QStringLiteral("text"), element.text);
        }
        if (element.kind == Kind::RoundedRectangle)
        {
            object.insert(QStringLiteral("radius"), element.cornerRadius);
        }

        QJsonObject style;
        style.insert(QStringLiteral("penColor"), PDFPageDrawingHelper::getColorText(element.style.penColor));
        style.insert(QStringLiteral("penWidth"), element.style.penWidth);
        if (element.style.fillColor.isValid())
        {
            style.insert(QStringLiteral("fillColor"), PDFPageDrawingHelper::getColorText(element.style.fillColor));
        }
        style.insert(QStringLiteral("fontSize"), element.style.fontSize);
        style.insert(QStringLiteral("alignment"), element.style.alignment == Alignment::Center ? QStringLiteral("center")
                                                                                              : (element.style.alignment == Alignment::Right ? QStringLiteral("right") : QStringLiteral("left")));
        style.insert(QStringLiteral("angle"), element.style.angle);
        object.insert(QStringLiteral("style"), style);
        array.append(object);
    }
    return array;
}

std::optional<PDFPageDrawing> PDFPageDrawing::fromJson(const QJsonArray& json, QString* errorMessage)
{
    QString message;
    if (json.size() > MAXIMUM_ELEMENT_COUNT)
    {
        message = tr("The drawing has more than %1 elements.").arg(MAXIMUM_ELEMENT_COUNT);
    }

    std::vector<Element> elements;
    qsizetype pointCount = 0;
    for (qsizetype i = 0; message.isEmpty() && i < json.size(); ++i)
    {
        std::optional<Element> element = PDFPageDrawingHelper::readElement(json.at(i), &message);
        if (!element)
        {
            message = tr("Element %1: %2").arg(i + 1).arg(message);
            break;
        }

        pointCount += qsizetype(element->points.size());
        if (pointCount > MAXIMUM_POINT_COUNT)
        {
            message = tr("The curves of the drawing have more than %1 points.").arg(MAXIMUM_POINT_COUNT);
            break;
        }
        elements.push_back(std::move(*element));
    }

    if (!message.isEmpty())
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return std::nullopt;
    }
    return PDFPageDrawing(std::move(elements));
}

}   // namespace pdf
