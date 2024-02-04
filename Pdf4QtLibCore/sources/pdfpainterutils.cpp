//    Copyright (C) 2021-2024 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "pdfpainterutils.h"

#include <QPainterPath>
#include <QFontMetrics>

#include "pdfdbgheap.h"

namespace pdf
{

QRect PDFPainterHelper::drawBubble(QPainter* painter, QPoint point, QColor color, QString text, Qt::Alignment alignment)
{
    QFontMetrics fontMetrics = painter->fontMetrics();

    const int lineSpacing = fontMetrics.lineSpacing();
    const int bubbleHeight = lineSpacing* 2;
    const int bubbleWidth = lineSpacing + fontMetrics.horizontalAdvance(text);

    QRect rectangle(point, QSize(bubbleWidth, bubbleHeight));

    if (alignment.testFlag(Qt::AlignVCenter))
    {
        rectangle.translate(0, -rectangle.height() / 2);
    }
    else if (alignment.testFlag(Qt::AlignTop))
    {
        rectangle.translate(0, -rectangle.height());
    }

    if (alignment.testFlag(Qt::AlignHCenter))
    {
        rectangle.translate(-rectangle.width() / 2, 0);
    }
    else if (alignment.testFlag(Qt::AlignLeft))
    {
        rectangle.translate(-rectangle.width(), 0);
    }

    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(color));
    painter->drawRoundedRect(rectangle, rectangle.height() / 2, rectangle.height() / 2, Qt::AbsoluteSize);
    painter->setPen(Qt::black);
    painter->drawText(rectangle, Qt::AlignCenter, text);

    return rectangle;
}

BLMatrix2D PDFPainterHelper::getBLMatrix(QTransform transform)
{
    BLMatrix2D matrix;
    matrix.reset(transform.m11(), transform.m12(), transform.m21(), transform.m22(), transform.dx(), transform.dy());
    return matrix;
}

BLRect PDFPainterHelper::getBLRect(QRect rect)
{
    return BLRect(rect.x(), rect.y(), rect.width(), rect.height());
}

BLRect PDFPainterHelper::getBLRect(QRectF rect)
{
    return BLRect(rect.x(), rect.y(), rect.width(), rect.height());
}

BLPath PDFPainterHelper::getBLPath(const QPainterPath& path)
{
    BLPath blPath;

    int elementCount = path.elementCount();
    for (int i = 0; i < elementCount; ++i) {
        const QPainterPath::Element& element = path.elementAt(i);

        switch (element.type)
        {
            case QPainterPath::MoveToElement:
                blPath.moveTo(element.x, element.y);
                break;

            case QPainterPath::LineToElement:
                blPath.lineTo(element.x, element.y);
                break;

            case QPainterPath::CurveToElement:
                if (i + 2 < elementCount)
                {
                    const QPainterPath::Element& ctrlPoint1 = path.elementAt(i++);
                    const QPainterPath::Element& ctrlPoint2 = path.elementAt(i++);
                    const QPainterPath::Element& endPoint = path.elementAt(i);
                    blPath.cubicTo(ctrlPoint1.x, ctrlPoint1.y, ctrlPoint2.x, ctrlPoint2.y, endPoint.x, endPoint.y);
                }
                break;

            case QPainterPath::CurveToDataElement:
                Q_ASSERT(false);
                break;
        }
    }

    return blPath;
}

void PDFPainterHelper::setBLPen(BLContext& context, const QPen& pen)
{
    const Qt::PenCapStyle capStyle = pen.capStyle();
    const Qt::PenJoinStyle joinStyle = pen.joinStyle();
    const QColor color = pen.color();
    const qreal width = pen.widthF();
    const qreal miterLimit = pen.miterLimit();
    const qreal dashOffset = pen.dashOffset();
    const QList<qreal> customDashPattern = pen.dashPattern();
    const Qt::PenStyle penStyle = pen.style();

    context.setStrokeAlpha(pen.color().alphaF());
    context.setStrokeWidth(width);
    context.setStrokeMiterLimit(miterLimit);

    switch (capStyle)
    {
    case Qt::FlatCap:
        context.setStrokeCaps(BL_STROKE_CAP_BUTT);
        break;
    case Qt::SquareCap:
        context.setStrokeCaps(BL_STROKE_CAP_SQUARE);
        break;
    case Qt::RoundCap:
        context.setStrokeCaps(BL_STROKE_CAP_ROUND);
        break;
    }

    BLArray<double> dashArray;

    for (double value : customDashPattern)
    {
        dashArray.append(value);
    }

    context.setStrokeDashOffset(dashOffset);
    context.setStrokeDashArray(dashArray);

    switch (joinStyle)
    {
    case Qt::MiterJoin:
        context.setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
        break;
    case Qt::BevelJoin:
        context.setStrokeJoin(BL_STROKE_JOIN_BEVEL);
        break;
    case Qt::RoundJoin:
        context.setStrokeJoin(BL_STROKE_JOIN_ROUND);
        break;
    case Qt::SvgMiterJoin:
        context.setStrokeJoin(BL_STROKE_JOIN_MITER_CLIP);
        break;
    }

    context.setStrokeStyle(BLRgba32(color.rgba()));

    BLStrokeOptions strokeOptions = context.strokeOptions();

    switch (penStyle)
    {
    case Qt::SolidLine:
        strokeOptions.dashArray.clear();
        strokeOptions.dashOffset = 0.0;
        break;

    case Qt::DashLine:
    {
        constexpr double dashPattern[] = {4, 4};
        strokeOptions.dashArray.assignData(dashPattern, std::size(dashPattern));
        break;
    }

    case Qt::DotLine:
    {
        constexpr double dashPattern[] = {1, 3};
        strokeOptions.dashArray.assignData(dashPattern, std::size(dashPattern));
        break;
    }

    case Qt::DashDotLine:
    {
        constexpr double dashPattern[] = {4, 2, 1, 2};
        strokeOptions.dashArray.assignData(dashPattern, std::size(dashPattern));
        break;
    }

    case Qt::DashDotDotLine:
    {
        constexpr double dashPattern[] = {4, 2, 1, 2, 1, 2};
        strokeOptions.dashArray.assignData(dashPattern, std::size(dashPattern));
        break;
    }

    default:
        break;
    }

    context.setStrokeOptions(strokeOptions);
}

void PDFPainterHelper::setBLBrush(BLContext& context, const QBrush& brush)
{
    auto setGradientStops = [](BLGradient& blGradient, const auto& qGradient)
    {
        QVector<BLGradientStop> stops;
        for (const auto& stop : qGradient.stops())
        {
            stops.append(BLGradientStop(stop.first, BLRgba32(stop.second.red(), stop.second.green(), stop.second.blue(), stop.second.alpha())));
        }
        blGradient.assignStops(stops.constData(), stops.size());
    };

    switch (brush.style())
    {
        default:
        case Qt::SolidPattern:
        {
            QColor color = brush.color();
            BLRgba32 blColor = BLRgba32(color.red(), color.green(), color.blue(), color.alpha());
            context.setFillStyle(blColor);
            break;
        }
        case Qt::LinearGradientPattern:
        {
            const QGradient* gradient = brush.gradient();
            if (gradient && gradient->type() == QGradient::LinearGradient)
            {
                const QLinearGradient* linearGradient = static_cast<const QLinearGradient*>(gradient);
                BLLinearGradientValues blLinearGradient;
                blLinearGradient.x0 = linearGradient->start().x();
                blLinearGradient.y0 = linearGradient->start().y();
                blLinearGradient.x1 = linearGradient->finalStop().x();
                blLinearGradient.y1 = linearGradient->finalStop().y();
                BLGradient blGradient(blLinearGradient);
                setGradientStops(blGradient, *gradient);
                context.setFillStyle(blGradient);
            }
            break;
        }
        case Qt::RadialGradientPattern:
        {
            const QGradient* gradient = brush.gradient();
            if (gradient && gradient->type() == QGradient::RadialGradient)
            {
                const QRadialGradient* radialGradient = static_cast<const QRadialGradient*>(gradient);
                BLRadialGradientValues blRadialGradientValues;
                blRadialGradientValues.x0 = radialGradient->center().x();
                blRadialGradientValues.y0 = radialGradient->center().y();
                blRadialGradientValues.x1 = radialGradient->focalPoint().x();
                blRadialGradientValues.y1 = radialGradient->focalPoint().y();
                blRadialGradientValues.r0 = radialGradient->radius();
                BLGradient blGradient(blRadialGradientValues);
                setGradientStops(blGradient, *gradient);
                context.setFillStyle(blGradient);
            }
            break;
        }
        }
}

BLCompOp PDFPainterHelper::getBLCompOp(QPainter::CompositionMode mode)
{
    switch (mode)
    {
        case QPainter::CompositionMode_SourceOver:
            return BL_COMP_OP_SRC_OVER;
        case QPainter::CompositionMode_DestinationOver:
            return BL_COMP_OP_DST_OVER;
        case QPainter::CompositionMode_Clear:
            return BL_COMP_OP_CLEAR;
        case QPainter::CompositionMode_Source:
            return BL_COMP_OP_SRC_COPY;
        case QPainter::CompositionMode_Destination:
            return BL_COMP_OP_DST_COPY;
        case QPainter::CompositionMode_SourceIn:
            return BL_COMP_OP_SRC_IN;
        case QPainter::CompositionMode_DestinationIn:
            return BL_COMP_OP_DST_IN;
        case QPainter::CompositionMode_SourceOut:
            return BL_COMP_OP_SRC_OUT;
        case QPainter::CompositionMode_DestinationOut:
            return BL_COMP_OP_DST_OUT;
        case QPainter::CompositionMode_SourceAtop:
            return BL_COMP_OP_SRC_ATOP;
        case QPainter::CompositionMode_DestinationAtop:
            return BL_COMP_OP_DST_ATOP;
        case QPainter::CompositionMode_Xor:
            return BL_COMP_OP_XOR;
        case QPainter::CompositionMode_Plus:
            return BL_COMP_OP_PLUS;
        case QPainter::CompositionMode_Multiply:
            return BL_COMP_OP_MULTIPLY;
        case QPainter::CompositionMode_Screen:
            return BL_COMP_OP_SCREEN;
        case QPainter::CompositionMode_Overlay:
            return BL_COMP_OP_OVERLAY;
        case QPainter::CompositionMode_Darken:
            return BL_COMP_OP_DARKEN;
        case QPainter::CompositionMode_Lighten:
            return BL_COMP_OP_LIGHTEN;
        case QPainter::CompositionMode_ColorDodge:
            return BL_COMP_OP_COLOR_DODGE;
        case QPainter::CompositionMode_ColorBurn:
            return BL_COMP_OP_COLOR_BURN;
        case QPainter::CompositionMode_HardLight:
            return BL_COMP_OP_HARD_LIGHT;
        case QPainter::CompositionMode_SoftLight:
            return BL_COMP_OP_SOFT_LIGHT;
        case QPainter::CompositionMode_Difference:
            return BL_COMP_OP_DIFFERENCE;
        case QPainter::CompositionMode_Exclusion:
            return BL_COMP_OP_EXCLUSION;
        default:
            break;
    }

    return BL_COMP_OP_SRC_OVER;
}

}   // namespace pdf
