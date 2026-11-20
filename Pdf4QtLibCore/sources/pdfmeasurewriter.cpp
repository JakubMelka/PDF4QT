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

#include "pdfmeasurewriter.h"
#include "pdfannotation.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfoperationcontrol.h"
#include "pdfpage.h"

#include <QFont>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <set>

#include "pdfdbgheap.h"

namespace pdf
{

// ------------------------------------------------------- PDFMeasureGeometry

PDFMeasureQuantity PDFMeasureGeometry::getQuantity(PDFMeasureKind kind)
{
    switch (kind)
    {
        case PDFMeasureKind::Distance:
        case PDFMeasureKind::Perimeter:
            return PDFMeasureQuantity::Length;
        case PDFMeasureKind::Area:
            return PDFMeasureQuantity::Area;
        case PDFMeasureKind::Angle:
            return PDFMeasureQuantity::Angle;
    }
    return PDFMeasureQuantity::Length;
}

std::optional<std::vector<QPointF>> PDFMeasureGeometry::normalizePoints(PDFMeasureKind kind, const std::vector<QPointF>& points, int maximumPointCount)
{
    for (const QPointF& point : points)
    {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
        {
            return std::nullopt;
        }
    }

    std::vector<QPointF> result = points;
    switch (kind)
    {
        case PDFMeasureKind::Distance:
            if (result.size() != 2)
            {
                return std::nullopt;
            }
            return result;

        case PDFMeasureKind::Angle:
            if (result.size() != 3 || result[0] == result[1] || result[2] == result[1])
            {
                return std::nullopt;
            }
            return result;

        case PDFMeasureKind::Perimeter:
        case PDFMeasureKind::Area:
        {
            // The shape is closed: its first point repeated at the end is not a new corner
            if (result.size() > 1 && result.front() == result.back())
            {
                result.pop_back();
            }

            std::vector<QPointF> distinct = result;
            std::sort(distinct.begin(), distinct.end(), [](const QPointF& left, const QPointF& right) { return std::make_pair(left.x(), left.y()) < std::make_pair(right.x(), right.y()); });
            distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
            if (distinct.size() < 3 || result.size() > size_t(qMax(0, maximumPointCount)))
            {
                return std::nullopt;
            }

            if (kind == PDFMeasureKind::Perimeter)
            {
                // A perimeter is a closed /PolyLine (Q7.5 b): the last vertex repeats the first
                result.push_back(result.front());
            }
            return result;
        }
    }

    return std::nullopt;
}

bool PDFMeasureGeometry::isComplete(PDFMeasureKind kind, const std::vector<QPointF>& points)
{
    switch (kind)
    {
        case PDFMeasureKind::Distance:
            return points.size() == 2;
        case PDFMeasureKind::Angle:
            return points.size() == 3;
        case PDFMeasureKind::Perimeter:
        case PDFMeasureKind::Area:
            return points.size() > 3 && points.front() == points.back();
    }
    return false;
}

PDFReal PDFMeasureGeometry::getPolylineLength(const std::vector<QPointF>& points)
{
    PDFReal length = 0.0;
    for (size_t i = 1; i < points.size(); ++i)
    {
        length += std::hypot(points[i].x() - points[i - 1].x(), points[i].y() - points[i - 1].y());
    }
    return length;
}

PDFReal PDFMeasureGeometry::getPolygonArea(const std::vector<QPointF>& points)
{
    // The shoelace formula over the closed polygon (the integral along its edges)
    PDFReal area = 0.0;
    const size_t count = points.size();
    for (size_t i = 0; i < count; ++i)
    {
        const QPointF& p1 = points[i];
        const QPointF& p2 = points[(i + 1) % count];
        area += p1.x() * p2.y() - p1.y() * p2.x();
    }
    return qAbs(area) * 0.5;
}

PDFReal PDFMeasureGeometry::getAngle(const QPointF& first, const QPointF& vertex, const QPointF& second)
{
    const QPointF arm1 = first - vertex;
    const QPointF arm2 = second - vertex;
    if (arm1.isNull() || arm2.isNull())
    {
        return 0.0;
    }

    // B181: the angle of the arms is the smaller one (0 to 180°) - atan2 of the cross and the dot product
    // does not depend on the order of the arms
    const PDFReal cross = arm1.x() * arm2.y() - arm1.y() * arm2.x();
    const PDFReal dot = arm1.x() * arm2.x() + arm1.y() * arm2.y();
    return qRadiansToDegrees(std::atan2(qAbs(cross), dot));
}

PDFReal PDFMeasureGeometry::getValue(PDFMeasureKind kind, const std::vector<QPointF>& points, PDFReal userUnit)
{
    switch (kind)
    {
        case PDFMeasureKind::Distance:
        case PDFMeasureKind::Perimeter:
            return getPolylineLength(points) * userUnit;
        case PDFMeasureKind::Area:
            return getPolygonArea(points) * userUnit * userUnit;
        case PDFMeasureKind::Angle:
            return points.size() == 3 ? getAngle(points[0], points[1], points[2]) : 0.0;
    }
    return 0.0;
}

PDFReal PDFMeasureGeometry::convertValue(PDFMeasureKind kind, PDFReal value, const PDFMeasureScale& scale, const PDFMeasureUnit& unit)
{
    const PDFReal scaleFactor = scale.isValid() ? scale.getScaleFactor() : 1.0;
    switch (kind)
    {
        case PDFMeasureKind::Distance:
        case PDFMeasureKind::Perimeter:
            return value * scaleFactor * unit.factor;
        case PDFMeasureKind::Area:
            return value * scaleFactor * scaleFactor * unit.factor;
        case PDFMeasureKind::Angle:
            return value * unit.factor;
    }
    return value;
}

QString PDFMeasureGeometry::formatValue(PDFReal value)
{
    if (!std::isfinite(value))
    {
        return QString();
    }
    QString text = QString::number(value, 'f', VALUE_DECIMALS);
    if (text.startsWith(QLatin1Char('-')) && text.mid(1).toDouble() == 0.0)
    {
        // A value rounded to zero has no sign
        text = text.mid(1);
    }
    return text;
}

QString PDFMeasureGeometry::getValueText(PDFMeasureKind kind, const QString& numberText, const QString& unitSymbol)
{
    switch (kind)
    {
        case PDFMeasureKind::Perimeter:
            return tr("p = %1 %2", "a perimeter").arg(numberText, unitSymbol);
        case PDFMeasureKind::Area:
            return tr("A = %1 %2", "an area").arg(numberText, unitSymbol);
        case PDFMeasureKind::Distance:
        case PDFMeasureKind::Angle:
            break;
    }
    return tr("%1 %2", "a length or an angle").arg(numberText, unitSymbol);
}

QString PDFMeasureGeometry::getKindTitle(PDFMeasureKind kind)
{
    switch (kind)
    {
        case PDFMeasureKind::Distance:
            return tr("Length");
        case PDFMeasureKind::Perimeter:
            return tr("Perimeter");
        case PDFMeasureKind::Area:
            return tr("Area");
        case PDFMeasureKind::Angle:
            return tr("Angle");
    }
    return QString();
}

QString PDFMeasureGeometry::getSubject(PDFMeasureKind kind)
{
    switch (kind)
    {
        case PDFMeasureKind::Distance:
            return QString::fromLatin1(SUBJECT_DISTANCE);
        case PDFMeasureKind::Perimeter:
            return QString::fromLatin1(SUBJECT_PERIMETER);
        case PDFMeasureKind::Area:
            return QString::fromLatin1(SUBJECT_AREA);
        case PDFMeasureKind::Angle:
            return QString::fromLatin1(SUBJECT_ANGLE);
    }
    return QString();
}

const PDFViewport* PDFMeasureGeometry::findScaleViewport(const std::vector<PDFViewport>& viewports, const QPointF& point, PDFReal userUnit, PDFMeasureScale* scale)
{
    // 12.9.2: "starting with the last one and iterating in reverse, the first one whose BBox entry contains the
    // point shall be chosen"; a viewport without a usable rectilinear measure gives no scale and is passed
    for (auto it = viewports.rbegin(); it != viewports.rend(); ++it)
    {
        if (!it->getBoundingBox().normalized().contains(point))
        {
            continue;
        }

        const PDFMeasureScale viewportScale = PDFMeasureScale::createFromMeasure(it->getMeasure(), userUnit);
        if (viewportScale.isValid())
        {
            if (scale)
            {
                *scale = viewportScale;
            }
            return &*it;
        }
    }
    return nullptr;
}

// ------------------------------------------------------- PDFMeasureAnnotationInfo

/// The parts of the writer and the reader of the measurement annotations
class PDFMeasureWriterHelper
{
public:
    PDFMeasureWriterHelper() = delete;

    /// Size of the label font, which is drawn and then scaled (QFont has whole pixel sizes)
    static constexpr int LABEL_FONT_PIXELS = 100;

    /// The effective /UserUnit of the page dictionary (Table 31; PDFPage::getEffectiveUserUnit)
    static PDFReal getUserUnit(const PDFObjectStorage* storage, PDFObjectReference page)
    {
        const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page));
        if (!dictionary)
        {
            return 1.0;
        }
        PDFDocumentDataLoaderDecorator loader(storage);
        const PDFReal userUnit = loader.readNumberFromDictionary(dictionary, "UserUnit", 1.0);
        if (std::isfinite(userUnit) && userUnit >= PDFPage::MINIMUM_USER_UNIT && userUnit <= PDFPage::MAXIMUM_USER_UNIT)
        {
            return userUnit;
        }
        return 1.0;
    }

    /// The media box of the page dictionary or its ancestors (inheritable, Table 31; one visited set)
    static QRectF getMediaBox(const PDFObjectStorage* storage, PDFObjectReference page)
    {
        PDFDocumentDataLoaderDecorator loader(storage);
        const PDFDictionary* dictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page));
        std::set<const PDFDictionary*> visited;
        while (dictionary && visited.insert(dictionary).second && visited.size() <= 256)
        {
            const QRectF box = loader.readRectangle(dictionary->get("MediaBox"), QRectF());
            if (box.isValid())
            {
                return box.normalized();
            }
            dictionary = storage->getDictionaryFromObject(dictionary->get("Parent"));
        }
        return QRectF();
    }

    /// A number format dictionary (Table 268): the label, the conversion factor, a decimal display of
    /// hundredths (F D, D 100 - the 2 places of the values of the tool)
    static void writeNumberFormat(PDFObjectFactory& factory, const QString& label, PDFReal conversionFactor)
    {
        factory.beginArray();
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << WrapName("NumberFormat");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("U");
        factory << label;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("C");
        factory << conversionFactor;
        factory.endDictionaryItem();
        factory.beginDictionaryItem("F");
        factory << WrapName("D");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("D");
        factory << PDFInteger(100);
        factory.endDictionaryItem();
        factory.endDictionary();
        factory.endArray();
    }

    /// The rectilinear measure dictionary of a measurement (Table 266, 267): /R, /X, /D, /A, /T always
    /// (R7-12 e); /X converts the units of the default user space of the page - /UserUnit points (B209) - to
    /// the length unit, /D the length unit to itself, /A the square of the length unit to the area unit, /T
    /// degrees to the angle unit. The labels are the untranslated abbreviations (B183). Isotropic: no /Y, /CYX.
    static PDFObject createMeasure(const PDFMeasureScale& scale, const PDFMeasureUnit& lengthUnit, const PDFMeasureUnit& areaUnit, const PDFMeasureUnit& angleUnit, PDFReal userUnit)
    {
        PDFObjectFactory factory;
        factory.beginDictionary();
        factory.beginDictionaryItem("Type");
        factory << WrapName("Measure");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("Subtype");
        factory << WrapName("RL");
        factory.endDictionaryItem();
        factory.beginDictionaryItem("R");
        factory << scale.getDocumentRatioText();
        factory.endDictionaryItem();
        factory.beginDictionaryItem("X");
        writeNumberFormat(factory, lengthUnit.getDocumentSymbol(), scale.getScaleFactor() * lengthUnit.factor * userUnit);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("D");
        writeNumberFormat(factory, lengthUnit.getDocumentSymbol(), 1.0);
        factory.endDictionaryItem();
        factory.beginDictionaryItem("A");
        writeNumberFormat(factory, areaUnit.getDocumentSymbol(), areaUnit.factor / (lengthUnit.factor * lengthUnit.factor));
        factory.endDictionaryItem();
        factory.beginDictionaryItem("T");
        writeNumberFormat(factory, angleUnit.getDocumentSymbol(), angleUnit.factor);
        factory.endDictionaryItem();
        factory.endDictionary();
        return factory.takeObject();
    }

    /// The path of a label: the text in the standard font (sans serif, the size and the style of \p style;
    /// "Saved annotations use the standard annotation font", 471:5490) converted to curves - the appearance
    /// never depends on the fonts of a reader - centered at the origin, y up
    static QPainterPath createTextPath(const QString& text, const PDFMeasureAnnotationStyle& style)
    {
        QFont font(QStringLiteral("Helvetica"));
        font.setStyleHint(QFont::SansSerif);
        font.setPixelSize(LABEL_FONT_PIXELS);
        font.setBold(style.isBold);
        font.setItalic(style.isItalic);

        QPainterPath path;
        path.addText(0.0, 0.0, font, text);
        const PDFReal fontSize = qBound(1.0, std::isfinite(style.fontSize) ? style.fontSize : 10.0, 1000.0);
        const PDFReal factor = fontSize / LABEL_FONT_PIXELS;
        path = QTransform(factor, 0.0, 0.0, -factor, 0.0, 0.0).map(path);
        const QPointF center = path.boundingRect().center();
        path.translate(-center);
        return path;
    }

    /// Draws a label: its background box (the background color and its alpha) and its text, rotated by
    /// \p angle degrees at \p position; returns the drawn area
    static QRectF drawLabel(QPainter& painter, const QString& text, const QPointF& position, PDFReal angle, const PDFMeasureAnnotationStyle& style)
    {
        const QPainterPath textPath = createTextPath(text, style);
        if (textPath.isEmpty())
        {
            return QRectF();
        }

        const PDFReal padding = qMax(1.0, style.fontSize * 0.25);
        QPainterPath boxPath;
        boxPath.addRect(textPath.boundingRect().adjusted(-padding, -padding, padding, padding));

        QTransform transform;
        transform.translate(position.x(), position.y());
        transform.rotate(angle);

        const QPainterPath mappedBox = transform.map(boxPath);
        if (style.backgroundColor.isValid() && style.backgroundColor.alpha() > 0)
        {
            painter.fillPath(mappedBox, PDFMeasureAnnotationStyle::getWrittenColor(style.backgroundColor, style.isOpaqueRequired));
        }
        painter.fillPath(transform.map(textPath), PDFMeasureAnnotationStyle::getWrittenColor(style.textColor.isValid() ? style.textColor : QColor(Qt::black), style.isOpaqueRequired));
        return mappedBox.boundingRect();
    }

    /// The centroid of the corners (the label of a perimeter and of an area, as the old plugin)
    static QPointF getCenter(const std::vector<QPointF>& points, bool isClosed)
    {
        const size_t count = isClosed && points.size() > 1 ? points.size() - 1 : points.size();
        QPointF center(0.0, 0.0);
        for (size_t i = 0; i < count; ++i)
        {
            center += points[i];
        }
        return count > 0 ? center / PDFReal(count) : center;
    }

    /// Draws the appearance of a measurement (12.5.5) and returns its bounding box: the lines in the line
    /// color, a distance with its end ticks, a perimeter dashed, an area filled by the background color (only its
    /// outline, when no transparency is allowed - PDFMeasureAnnotationStyle::isOpaqueRequired), an angle with its
    /// arc, the label of the value
    static QRectF drawAppearance(QPainter& painter, PDFMeasureKind kind, const std::vector<QPointF>& points, const QString& text, const PDFMeasureAnnotationStyle& style)
    {
        painter.setRenderHint(QPainter::Antialiasing, true);
        const PDFReal lineWidth = qBound(0.1, std::isfinite(style.lineWidth) ? style.lineWidth : 1.0, 100.0);
        QPen pen(PDFMeasureAnnotationStyle::getWrittenColor(style.lineColor.isValid() ? style.lineColor : QColor(Qt::black), style.isOpaqueRequired));
        pen.setWidthF(lineWidth);
        pen.setCapStyle(Qt::FlatCap);
        pen.setJoinStyle(Qt::MiterJoin);

        QRectF bounds;
        auto addBounds = [&bounds, lineWidth](const QRectF& rectangle)
        {
            const QRectF adjusted = rectangle.adjusted(-lineWidth, -lineWidth, lineWidth, lineWidth);
            bounds = bounds.isValid() ? bounds.united(adjusted) : adjusted;
        };

        QPolygonF polygon;
        for (const QPointF& point : points)
        {
            polygon << point;
        }

        switch (kind)
        {
            case PDFMeasureKind::Distance:
            {
                const QLineF line(points.front(), points.back());
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(line);

                // The ticks at both ends, perpendicular to the line (the extension lines of 471:5975)
                const QLineF normal = line.normalVector().unitVector();
                const QPointF tick = (normal.p2() - normal.p1()) * qMax(3.0, lineWidth * 4.0);
                painter.drawLine(line.p1() - tick, line.p1() + tick);
                painter.drawLine(line.p2() - tick, line.p2() + tick);
                addBounds(QPolygonF({ line.p1() - tick, line.p1() + tick, line.p2() - tick, line.p2() + tick }).boundingRect());

                // The label along the line, never upside down
                PDFReal angle = qRadiansToDegrees(std::atan2(line.dy(), line.dx()));
                if (angle > 90.0)
                {
                    angle -= 180.0;
                }
                else if (angle < -90.0)
                {
                    angle += 180.0;
                }
                addBounds(drawLabel(painter, text, line.center(), angle, style));
                break;
            }

            case PDFMeasureKind::Perimeter:
            {
                QPen dashedPen = pen;
                dashedPen.setDashPattern({ 4.0, 2.0 });
                painter.setPen(dashedPen);
                painter.setBrush(Qt::NoBrush);
                painter.drawPolyline(polygon);
                addBounds(polygon.boundingRect());
                addBounds(drawLabel(painter, text, getCenter(points, true), 0.0, style));
                break;
            }

            case PDFMeasureKind::Area:
            {
                // Owner 2026-11-19 (PDF/A-1): an area of an opaque appearance is its outline only - an opaque fill
                // would hide the drawing under it
                const bool isFilled = !style.isOpaqueRequired && style.backgroundColor.isValid() && style.backgroundColor.alpha() > 0;
                painter.setPen(pen);
                painter.setBrush(isFilled ? QBrush(style.backgroundColor) : QBrush(Qt::NoBrush));
                painter.drawPolygon(polygon, Qt::OddEvenFill);
                addBounds(polygon.boundingRect());
                addBounds(drawLabel(painter, text, getCenter(points, false), 0.0, style));
                break;
            }

            case PDFMeasureKind::Angle:
            {
                const QPointF vertex = points[1];
                const QLineF arm1(vertex, points[0]);
                const QLineF arm2(vertex, points[2]);
                painter.setPen(pen);
                painter.setBrush(Qt::NoBrush);
                painter.drawLine(arm1);
                painter.drawLine(arm2);
                addBounds(polygon.boundingRect());

                // The arc of the smaller angle (B181) between the arms
                const PDFReal radius = qMin(arm1.length(), arm2.length()) * 0.3;
                const PDFReal startAngle = qRadiansToDegrees(std::atan2(arm1.dy(), arm1.dx()));
                PDFReal span = qRadiansToDegrees(std::atan2(arm2.dy(), arm2.dx())) - startAngle;
                while (span > 180.0)
                {
                    span -= 360.0;
                }
                while (span < -180.0)
                {
                    span += 360.0;
                }
                if (radius > 0.0)
                {
                    QPainterPath arc;
                    const QRectF arcRectangle(vertex.x() - radius, vertex.y() - radius, 2.0 * radius, 2.0 * radius);
                    // QPainterPath angles go counterclockwise in a y-down space; the space here is y up
                    arc.arcMoveTo(arcRectangle, -startAngle);
                    arc.arcTo(arcRectangle, -startAngle, -span);
                    painter.strokePath(arc, pen);
                }

                const PDFReal bisector = qDegreesToRadians(startAngle + span * 0.5);
                const PDFReal labelDistance = qMax(radius * 1.6, style.fontSize * 2.0);
                const QPointF labelPosition = vertex + QPointF(std::cos(bisector), std::sin(bisector)) * labelDistance;
                addBounds(drawLabel(painter, text, labelPosition, 0.0, style));
                break;
            }
        }

        return bounds;
    }

    /// The text of the value of an existing annotation by its number formats (Table 268), else its /Contents
    static void fillValue(PDFMeasureAnnotationInfo& info, const PDFMeasure& measure, const QString& contents)
    {
        const bool isMeasure = measure.isValid() && measure.isRectilinear() && !measure.getXFormat().empty();
        info.hasMeasure = isMeasure;
        info.ratio = measure.getScaleRatio();

        if (!isMeasure)
        {
            // Without a rectilinear measure the geometry is all: points, square points, degrees
            const PDFReal value = PDFMeasureGeometry::getValue(info.kind, info.points, 1.0);
            const PDFMeasureQuantity quantity = PDFMeasureGeometry::getQuantity(info.kind);
            info.unitId = PDFMeasureUnits::getDefaultUnit(quantity).id;
            info.unitLabel = PDFMeasureUnits::getDefaultUnit(quantity).getDocumentSymbol();
            info.value = value;
            info.text = contents.isEmpty() ? PDFMeasureGeometry::getValueText(info.kind, PDFMeasureGeometry::formatValue(value), info.unitLabel) : contents;
            return;
        }

        const PDFReal xFactor = measure.getUnitsPerUserSpaceUnit();
        const PDFReal yFactor = measure.getUnitsPerUserSpaceUnitY() * measure.getFactorYX();
        const std::vector<PDFNumberFormat>* formats = nullptr;
        PDFReal value = 0.0;
        switch (info.kind)
        {
            case PDFMeasureKind::Distance:
            case PDFMeasureKind::Perimeter:
            {
                // Each segment by the factors of both axes (an anisotropic measure, CYX), in the units of X
                for (size_t i = 1; i < info.points.size(); ++i)
                {
                    const QPointF delta = info.points[i] - info.points[i - 1];
                    value += std::hypot(delta.x() * xFactor, delta.y() * yFactor);
                }
                formats = &measure.getDistanceFormat();
                break;
            }
            case PDFMeasureKind::Area:
            {
                value = PDFMeasureGeometry::getPolygonArea(info.points) * xFactor * yFactor;
                formats = &measure.getAreaFormat();
                break;
            }
            case PDFMeasureKind::Angle:
            {
                value = PDFMeasureGeometry::getValue(info.kind, info.points, 1.0);
                formats = &measure.getAngleFormat();
                break;
            }
        }

        if (formats && !formats->empty())
        {
            info.text = PDFNumberFormat::format(*formats, value);
            info.value = value * formats->front().getConversionFactor();
            info.unitLabel = formats->front().getUnitLabel();
        }
        else if (info.kind == PDFMeasureKind::Angle)
        {
            // /T is optional: degrees
            info.value = value;
            info.unitLabel = PDFMeasureUnits::getDefaultUnit(PDFMeasureQuantity::Angle).getDocumentSymbol();
            info.text = PDFMeasureGeometry::getValueText(info.kind, PDFMeasureGeometry::formatValue(value), info.unitLabel);
        }
        else
        {
            // /D, /A are required; a damaged measure has the units of X (a length, its square for an area)
            info.value = value;
            info.unitLabel = measure.getUnitLabel();
            info.text = contents.isEmpty() ? PDFNumberFormat::format(measure.getXFormat(), info.kind == PDFMeasureKind::Area ? value : value / qMax(xFactor, 1e-300)) : contents;
        }

        // The id of a known unit of the label: a length by the aliases, an area or an angle by its symbol
        const PDFMeasureQuantity quantity = PDFMeasureGeometry::getQuantity(info.kind);
        if (quantity == PDFMeasureQuantity::Length)
        {
            info.unitId = PDFMeasureUnits::findLengthUnitByLabel(info.unitLabel).id;
        }
        else
        {
            for (const PDFMeasureUnit& unit : PDFMeasureUnits::getUnits(quantity))
            {
                if (unit.getDocumentSymbol().compare(info.unitLabel.trimmed(), Qt::CaseInsensitive) == 0 || QString::fromLatin1(unit.id) == info.unitLabel.trimmed())
                {
                    info.unitId = unit.id;
                    break;
                }
            }
        }
    }
};

std::optional<PDFMeasureAnnotationInfo> PDFMeasureAnnotationInfo::read(const PDFDocument* document, PDFInteger pageIndex, PDFObjectReference annotation, int maximumPointCount)
{
    if (!document)
    {
        return std::nullopt;
    }

    const PDFAnnotationPtr parsedAnnotation = PDFAnnotation::parse(&document->getStorage(), annotation);
    if (!parsedAnnotation)
    {
        return std::nullopt;
    }

    PDFMeasureAnnotationInfo info;
    info.annotation = annotation;
    info.pageIndex = pageIndex;

    PDFObject measureObject;
    QString subject;
    if (const PDFMarkupAnnotation* markupAnnotation = parsedAnnotation->asMarkupAnnotation())
    {
        subject = markupAnnotation->getSubject();
        info.author = markupAnnotation->getWindowTitle().left(MAXIMUM_TEXT_LENGTH);
    }

    std::optional<std::vector<QPointF>> points;
    if (const PDFLineAnnotation* lineAnnotation = dynamic_cast<const PDFLineAnnotation*>(parsedAnnotation.data()))
    {
        if (lineAnnotation->getIntent() != PDFLineAnnotation::Intent::Dimension)
        {
            return std::nullopt;
        }
        info.kind = PDFMeasureKind::Distance;
        measureObject = lineAnnotation->getMeasureDictionary();
        points = PDFMeasureGeometry::normalizePoints(info.kind, { lineAnnotation->getLine().p1(), lineAnnotation->getLine().p2() }, maximumPointCount);
    }
    else if (const PDFPolygonalGeometryAnnotation* polygonalAnnotation = dynamic_cast<const PDFPolygonalGeometryAnnotation*>(parsedAnnotation.data()))
    {
        const std::vector<QPointF>& vertices = polygonalAnnotation->getVertices();
        if (vertices.size() > size_t(qMax(0, maximumPointCount)) + 1)
        {
            return std::nullopt;
        }

        const bool isDimension = polygonalAnnotation->getIntent() == PDFPolygonalGeometryAnnotation::Intent::Dimension;
        measureObject = polygonalAnnotation->getMeasure();
        const bool hasMeasure = !document->getObject(measureObject).isNull();
        const bool isAngleSubject = subject == QLatin1String(PDFMeasureGeometry::SUBJECT_ANGLE);

        if (polygonalAnnotation->getType() == AnnotationType::Polygon)
        {
            if (!isDimension)
            {
                return std::nullopt;
            }

            // The old plugin wrote a perimeter as a /Polygon /PolygonDimension (B171): its /Subj says so
            info.kind = subject == QLatin1String(PDFMeasureGeometry::SUBJECT_PERIMETER) ? PDFMeasureKind::Perimeter : PDFMeasureKind::Area;
        }
        else if (vertices.size() == 3 && isAngleSubject && (isDimension || hasMeasure))
        {
            // Q7.5 (b): an angle is a /PolyLine of 3 vertices with /Measure and /Subj "Angle" (the old plugin
            // wrote /IT /PolyLineDimension)
            info.kind = PDFMeasureKind::Angle;
        }
        else if (isDimension)
        {
            // A closed /PolyLine /PolyLineDimension is a perimeter (Q7.5 b), an open one a length along the line
            info.kind = (vertices.size() > 3 && vertices.front() == vertices.back()) ? PDFMeasureKind::Perimeter : PDFMeasureKind::Distance;
        }
        else
        {
            return std::nullopt;
        }

        if (info.kind == PDFMeasureKind::Distance)
        {
            // A length along an open polyline: the points as they are
            if (vertices.size() < 2)
            {
                return std::nullopt;
            }
            for (const QPointF& point : vertices)
            {
                if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                {
                    return std::nullopt;
                }
            }
            points = vertices;
        }
        else
        {
            points = PDFMeasureGeometry::normalizePoints(info.kind, vertices, maximumPointCount);
        }
    }
    else
    {
        return std::nullopt;
    }

    if (!points)
    {
        return std::nullopt;
    }
    info.points = std::move(*points);
    const PDFMeasure measure = PDFMeasure::parse(&document->getStorage(), measureObject);
    PDFMeasureWriterHelper::fillValue(info, measure, parsedAnnotation->getContents());
    info.text.truncate(MAXIMUM_TEXT_LENGTH);

    const PDFCatalog* catalog = document->getCatalog();
    const PDFReal userUnit = (pageIndex >= 0 && size_t(pageIndex) < catalog->getPageCount()) ? catalog->getPage(size_t(pageIndex))->getEffectiveUserUnit() : 1.0;
    info.scale = PDFMeasureScale::createFromMeasure(measure, userUnit);
    return info;
}

std::vector<PDFMeasureAnnotationInfo> PDFMeasureAnnotationInfo::readPages(const PDFDocument* document, const std::vector<PDFInteger>& pageIndices, size_t maximumCount, int maximumPointCount, bool* isTruncated,
                                                                         const PDFOperationControl* control)
{
    std::vector<PDFMeasureAnnotationInfo> result;
    if (isTruncated)
    {
        *isTruncated = false;
    }
    if (!document)
    {
        return result;
    }

    const PDFCatalog* catalog = document->getCatalog();
    size_t pointCount = 0;
    for (const PDFInteger pageIndex : pageIndices)
    {
        if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            continue;
        }

        const PDFPage* page = catalog->getPage(size_t(pageIndex));
        for (const PDFObjectReference& reference : page->getAnnotations())
        {
            // Every annotation is parsed: a page may have very many of them
            if (PDFOperationControl::isOperationCanceled(control))
            {
                return result;
            }

            std::optional<PDFMeasureAnnotationInfo> info = read(document, pageIndex, reference, maximumPointCount);
            if (!info)
            {
                continue;
            }
            if (result.size() >= maximumCount || pointCount + info->points.size() > MAXIMUM_TOTAL_POINT_COUNT)
            {
                if (isTruncated)
                {
                    *isTruncated = true;
                }
                return result;
            }
            pointCount += info->points.size();
            result.push_back(std::move(*info));
        }
    }
    return result;
}

// ------------------------------------------------------- PDFMeasureAnnotationStyle

QColor PDFMeasureAnnotationStyle::getWrittenColor(const QColor& color, bool isOpaqueRequired)
{
    if (!isOpaqueRequired || !color.isValid() || color.alpha() == 255)
    {
        return color;
    }

    // ISO 19005-1, 6.4: the color as it looks over the white paper, without its opacity
    const QColor rgb = color.toRgb();
    const PDFReal alpha = rgb.alphaF();
    const PDFReal white = 1.0 - alpha;
    return QColor::fromRgbF(float(rgb.redF() * alpha + white), float(rgb.greenF() * alpha + white), float(rgb.blueF() * alpha + white), 1.0f);
}

// ------------------------------------------------------- PDFMeasureWriter

PDFObjectReference PDFMeasureWriter::createAnnotation(PDFDocumentBuilder* builder,
                                                      PDFObjectReference page,
                                                      PDFMeasureKind kind,
                                                      const std::vector<QPointF>& points,
                                                      const PDFMeasureScale& scale,
                                                      const PDFMeasureDisplayUnits& units,
                                                      const PDFMeasureAnnotationStyle& style,
                                                      QString* errorMessage)
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
        {
            *errorMessage = message;
        }
        return PDFObjectReference();
    };

    if (!builder)
    {
        return fail(tr("There is no document."));
    }

    const PDFObjectStorage* storage = builder->getStorage();
    const PDFDictionary* pageDictionary = storage->getDictionaryFromObject(storage->getObjectByReference(page));
    const PDFObject& pageType = pageDictionary ? pageDictionary->get("Type") : PDFObject();
    if (!pageDictionary || !pageType.isName() || pageType.getString() != "Page")
    {
        return fail(tr("The page of the measurement does not exist."));
    }

    // At most the points of the tool (the points are checked by the tool too)
    const std::optional<std::vector<QPointF>> normalizedPoints = PDFMeasureGeometry::normalizePoints(kind, points, 200);
    if (!normalizedPoints)
    {
        return fail(tr("The points of the measurement are not valid for its kind."));
    }

    if (!scale.isValid())
    {
        return fail(tr("The scale of the measurement is not valid."));
    }

    const PDFMeasureUnit lengthUnit = PDFMeasureUnits::findUnitOrDefault(PDFMeasureQuantity::Length, units.length);
    const PDFMeasureUnit areaUnit = PDFMeasureUnits::findUnitOrDefault(PDFMeasureQuantity::Area, units.area);
    const PDFMeasureUnit angleUnit = PDFMeasureUnits::findUnitOrDefault(PDFMeasureQuantity::Angle, units.angle);

    // The value of the measurement in the unit of its quantity (× the effective /UserUnit, B209)
    const PDFReal userUnit = PDFMeasureWriterHelper::getUserUnit(storage, page);
    const PDFMeasureQuantity quantity = PDFMeasureGeometry::getQuantity(kind);
    const PDFMeasureUnit& unit = (quantity == PDFMeasureQuantity::Length) ? lengthUnit : ((quantity == PDFMeasureQuantity::Area) ? areaUnit : angleUnit);
    const PDFReal value = PDFMeasureGeometry::convertValue(kind, PDFMeasureGeometry::getValue(kind, *normalizedPoints, userUnit), scale, unit);
    if (!std::isfinite(value))
    {
        return fail(tr("The value of the measurement cannot be computed."));
    }

    // /Contents: the alternate description of the annotation (ISO 14289-1, 7.18.1), the value as shown
    const QString text = PDFMeasureGeometry::getValueText(kind, PDFMeasureGeometry::formatValue(value), unit.getSymbol());
    const QString subject = PDFMeasureGeometry::getSubject(kind);
    // /C has no alpha (Table 166): the color of the lines without its opacity, composited over white when no
    // transparency is allowed (the appearance draws it so)
    const QColor lineColor = PDFMeasureAnnotationStyle::getWrittenColor(style.lineColor.isValid() ? style.lineColor : QColor(Qt::black), style.isOpaqueRequired);
    const QColor strokeColor(lineColor.red(), lineColor.green(), lineColor.blue());
    const PDFReal lineWidth = qBound(0.1, std::isfinite(style.lineWidth) ? style.lineWidth : 1.0, 100.0);

    QPolygonF polygon;
    for (const QPointF& point : *normalizedPoints)
    {
        polygon << point;
    }

    PDFObjectReference annotation;
    const char* intent = nullptr;
    switch (kind)
    {
        case PDFMeasureKind::Distance:
        {
            // Table 178: /Line /IT /LineDimension
            const QRectF boundingRect = QRectF(normalizedPoints->front(), normalizedPoints->back()).normalized().adjusted(-lineWidth, -lineWidth, lineWidth, lineWidth);
            annotation = builder->createAnnotationLine(page, boundingRect, normalizedPoints->front(), normalizedPoints->back(), lineWidth, strokeColor, strokeColor,
                                                       style.author, subject, text, AnnotationLineEnding::Butt, AnnotationLineEnding::Butt);
            intent = "LineDimension";
            break;
        }

        case PDFMeasureKind::Perimeter:
            // Q7.5 (b): a closed /PolyLine /IT /PolyLineDimension (Table 181; the convention of the readers, R7-11)
            annotation = builder->createAnnotationPolyline(page, polygon, lineWidth, QColor(), strokeColor, style.author, subject, text, AnnotationLineEnding::None, AnnotationLineEnding::None);
            intent = "PolyLineDimension";
            break;

        case PDFMeasureKind::Area:
        {
            // Table 181: /Polygon /IT /PolygonDimension; the interior color is the background without its alpha
            // (none for an outline of an opaque appearance: a reader, which draws /IC, would hide the drawing)
            QColor interiorColor;
            if (!style.isOpaqueRequired && style.backgroundColor.isValid() && style.backgroundColor.alpha() > 0)
            {
                interiorColor = QColor(style.backgroundColor.red(), style.backgroundColor.green(), style.backgroundColor.blue());
            }
            annotation = builder->createAnnotationPolygon(page, polygon, lineWidth, interiorColor, strokeColor, style.author, subject, text);
            intent = "PolygonDimension";
            break;
        }

        case PDFMeasureKind::Angle:
            // Q7.5 (b): a /PolyLine without /IT (Table 181 has no intent of an angle), /Measure with /T
            annotation = builder->createAnnotationPolyline(page, polygon, lineWidth, QColor(), strokeColor, style.author, subject, text, AnnotationLineEnding::None, AnnotationLineEnding::None);
            break;
    }

    if (!annotation.isValid())
    {
        return fail(tr("The measurement annotation cannot be created."));
    }

    PDFObjectFactory factory;
    factory.beginDictionary();
    if (intent)
    {
        factory.beginDictionaryItem("IT");
        factory << WrapName(intent);
        factory.endDictionaryItem();
    }
    factory.beginDictionaryItem("Measure");
    factory << PDFMeasureWriterHelper::createMeasure(scale, lengthUnit, areaUnit, angleUnit, userUnit);
    factory.endDictionaryItem();
    factory.endDictionary();
    builder->mergeTo(annotation, factory.takeObject());

    // The appearance of the tool (12.5.5; every annotation has /AP - PDF/A): the label with its colors in the
    // standard font as curves. The appearance the builder generated is replaced and its objects freed.
    const QRectF mediaBox = PDFMeasureWriterHelper::getMediaBox(storage, page);
    QRectF drawingArea = mediaBox.isValid() ? mediaBox : polygon.boundingRect();
    drawingArea = drawingArea.united(polygon.boundingRect());
    PDFContentStreamBuilder contentBuilder(QSizeF(qMax(1.0, drawingArea.right()), qMax(1.0, drawingArea.bottom())), PDFContentStreamBuilder::CoordinateSystem::PDF);
    QPainter* painter = contentBuilder.begin();
    const QRectF appearanceBox = PDFMeasureWriterHelper::drawAppearance(*painter, kind, *normalizedPoints, text, style);
    PDFContentStreamBuilder::ContentStream contentStream = contentBuilder.end(painter);
    const PDFObjectReference appearance = builder->createAppearanceStream(contentStream, appearanceBox);

    if (appearance.isValid())
    {
        const PDFDictionary* annotationDictionary = builder->getDictionaryFromObject(builder->getObjectByReference(annotation));
        if (annotationDictionary)
        {
            // The generated normal appearance of the builder and its resources are freed
            const PDFDictionary* oldAppearances = builder->getDictionaryFromObject(annotationDictionary->get("AP"));
            const PDFObject oldNormal = oldAppearances ? oldAppearances->get("N") : PDFObject();

            PDFDictionaryBuilder updatedAnnotation(*annotationDictionary);
            PDFDictionaryBuilder appearances;
            appearances.addEntry(PDFInplaceOrMemoryString("N"), PDFObject::createReference(appearance));
            updatedAnnotation.setEntry(PDFInplaceOrMemoryString("AP"), PDFObject::createDictionary(std::move(appearances)));
            const QRectF rectangle = appearanceBox.united(polygon.boundingRect());
            PDFObjectFactory rectangleFactory;
            rectangleFactory << rectangle;
            updatedAnnotation.setEntry(PDFInplaceOrMemoryString("Rect"), rectangleFactory.takeObject());
            builder->setObject(annotation, PDFObject::createDictionary(std::move(updatedAnnotation)));

            if (oldNormal.isReference() && oldNormal.getReference() != appearance)
            {
                const PDFDictionary* oldForm = builder->getDictionaryFromObject(builder->getObjectByReference(oldNormal.getReference()));
                const PDFObject oldResources = oldForm ? oldForm->get("Resources") : PDFObject();
                if (oldResources.isReference())
                {
                    builder->setObject(oldResources.getReference(), PDFObject());
                }
                builder->setObject(oldNormal.getReference(), PDFObject());
            }
        }
    }

    // ISO 14289-1, 7.18.1: in a tagged document the annotation is the content of an /Annot element
    builder->addObjectStructureElement(annotation, page, "Annot");
    return annotation;
}

}   // namespace pdf
