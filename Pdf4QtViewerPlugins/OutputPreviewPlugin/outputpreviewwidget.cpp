//    Copyright (C) 2021 Jakub Melka
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
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "outputpreviewwidget.h"

#include "pdfwidgetutils.h"

#include <QPainter>
#include <QMouseEvent>
#include <QFontMetrics>

namespace pdfplugin
{

OutputPreviewWidget::OutputPreviewWidget(QWidget* parent) :
    BaseClass(parent),
    m_inkMapper(nullptr),
    m_displayMode(Separations),
    m_alarmColor(Qt::red),
    m_inkCoverageLimit(3.0),
    m_richBlackLimit(1.0)
{
    setMouseTracking(true);
}

QSize OutputPreviewWidget::sizeHint() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(500, 300));
}

QSize OutputPreviewWidget::minimumSizeHint() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(400, 300));
}

void OutputPreviewWidget::clear()
{
    m_pageImage = QImage();
    m_originalProcessBitmap = pdf::PDFFloatBitmapWithColorSpace();
    m_pageSizeMM = QSizeF();
    m_infoBoxItems.clear();
    m_imagePointUnderCursor = std::nullopt;
    m_inkCoverageMM.dirty();
    m_alarmCoverageImage.dirty();
    m_alarmRichBlackImage.dirty();
    m_inkCoverageImage.dirty();
    m_shapeMask.dirty();
    m_opacityMask.dirty();
    update();
}

void OutputPreviewWidget::setPageImage(QImage image, pdf::PDFFloatBitmapWithColorSpace originalProcessBitmap, QSizeF pageSizeMM)
{
    m_pageImage = qMove(image);
    m_originalProcessBitmap = qMove(originalProcessBitmap);
    m_pageSizeMM = pageSizeMM;

    if (m_imagePointUnderCursor.has_value())
    {
        QPoint point = m_imagePointUnderCursor.value();
        if (point.x() >= image.width() || point.y() >= image.height())
        {
            m_imagePointUnderCursor = std::nullopt;
        }
    }

    m_inkCoverageMM.dirty();
    m_alarmCoverageImage.dirty();
    m_alarmRichBlackImage.dirty();
    m_inkCoverageImage.dirty();
    m_shapeMask.dirty();
    m_opacityMask.dirty();

    buildInfoBoxItems();
    update();
}

void OutputPreviewWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    Q_UNUSED(event);

    QRect rect = this->rect();
    painter.fillRect(rect, Qt::gray);

    QRect contentRect = getContentRect();
    QRect pageImageRect = getPageImageRect(contentRect);

    if (pageImageRect.isValid())
    {
        painter.save();
        painter.setClipRect(pageImageRect, Qt::IntersectClip);

        switch (m_displayMode)
        {
            case Separations:
            {
                if (!m_pageImage.isNull())
                {
                    painter.translate(0, (pageImageRect.height() - m_pageImage.height()) / 2);
                    painter.drawImage(pageImageRect.topLeft(), m_pageImage);
                }
                break;
            }

            case ColorWarningInkCoverage:
            {
                const AlarmImageInfo& alarmImage = getAlarmCoverageImage();
                if (!alarmImage.image.isNull())
                {
                    painter.translate(0, (pageImageRect.height() - alarmImage.image.height()) / 2);
                    painter.drawImage(pageImageRect.topLeft(), alarmImage.image);
                }
                break;
            }

            case ColorWarningRichBlack:
            {
                const AlarmImageInfo& image = getAlarmRichBlackImage();
                if (!image.image.isNull())
                {
                    painter.translate(0, (pageImageRect.height() - image.image.height()) / 2);
                    painter.drawImage(pageImageRect.topLeft(), image.image);
                }
                break;
            }

            case InkCoverage:
            {
                const InkCoverageInfo& image = getInkCoverageInfo();
                if (!image.image.isNull())
                {
                    painter.translate(0, (pageImageRect.height() - image.image.height()) / 2);
                    painter.drawImage(pageImageRect.topLeft(), image.image);
                }
                break;
            }

            case ShapeChannel:
            {
                const QImage& image = getShapeImage();
                if (!image.isNull())
                {
                    painter.translate(0, (pageImageRect.height() - image.height()) / 2);
                    painter.drawImage(pageImageRect.topLeft(), image);
                }
                break;
            }

            case OpacityChannel:
            {
                const QImage& image = getOpacityImage();
                if (!image.isNull())
                {
                    painter.translate(0, (pageImageRect.height() - image.height()) / 2);
                    painter.drawImage(pageImageRect.topLeft(), image);
                }
                break;
            }

            default:
                Q_ASSERT(false);
        }

        painter.restore();
    }

    if (!m_infoBoxItems.empty())
    {
        painter.save();

        int infoBoxWidth = getInfoBoxWidth();
        int itemHorizontalMargin = getInfoBoxContentHorizontalMargin();

        QRect infoBoxRect = contentRect;
        infoBoxRect.setLeft(infoBoxRect.right() - infoBoxWidth);

        painter.setPen(Qt::black);
        painter.setBrush(QBrush(Qt::white));
        painter.drawRect(infoBoxRect);
        painter.setClipRect(infoBoxRect, Qt::IntersectClip);
        painter.setBrush(Qt::NoBrush);

        QFontMetrics fontMetrics(painter.font(), painter.device());
        QRect rowRect = infoBoxRect;
        rowRect.setHeight(fontMetrics.lineSpacing());

        for (const auto& infoBoxItem : m_infoBoxItems)
        {
            switch (infoBoxItem.style)
            {
                case pdfplugin::OutputPreviewWidget::Header:
                {
                    painter.save();

                    QFont font = painter.font();
                    font.setBold(true);
                    painter.setFont(font);

                    painter.drawText(rowRect, Qt::AlignCenter | Qt::TextSingleLine, infoBoxItem.caption);

                    painter.restore();
                    break;
                }

                case pdfplugin::OutputPreviewWidget::Separator:
                    break;

                case pdfplugin::OutputPreviewWidget::ColoredItem:
                {
                    QRect cellRect = rowRect.marginsRemoved(QMargins(itemHorizontalMargin, 0, itemHorizontalMargin, 0));

                    if (infoBoxItem.color.isValid())
                    {
                        QRect ellipseRect = cellRect;
                        ellipseRect.setWidth(ellipseRect.height());
                        cellRect.setLeft(ellipseRect.right() + 1);

                        painter.save();
                        painter.setPen(Qt::NoPen);
                        painter.setBrush(QBrush(infoBoxItem.color));
                        painter.drawEllipse(ellipseRect);
                        painter.restore();
                    }

                    painter.drawText(cellRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, infoBoxItem.caption);
                    painter.drawText(cellRect, Qt::AlignVCenter | Qt::AlignRight | Qt::TextSingleLine, infoBoxItem.value);
                    break;
                }

                case pdfplugin::OutputPreviewWidget::ColorOnly:
                {
                    QRect cellRect = rowRect.marginsRemoved(QMargins(itemHorizontalMargin, 0, itemHorizontalMargin, 0));
                    QPoint center = cellRect.center();
                    cellRect.setWidth(cellRect.width() / 4);
                    cellRect.moveCenter(center);
                    painter.fillRect(cellRect, infoBoxItem.color);
                    break;
                }

                default:
                    Q_ASSERT(false);
                    break;
            }

            rowRect.translate(0, rowRect.height());
        }

        rowRect.translate(0, rowRect.height());

        painter.restore();

        if (m_displayMode == InkCoverage)
        {
            const InkCoverageInfo& info = getInkCoverageInfo();
            if (info.colorScale.isValid())
            {
                painter.save();

                int rowHeight = rowRect.height();
                QRect colorScaleRect = rowRect;
                colorScaleRect.setBottom(contentRect.bottom());
                const int maxRows = colorScaleRect.height() / rowHeight;
                int rows = maxRows;

                if (rows > 6)
                {
                    painter.save();
                    QFont font = painter.font();
                    font.setBold(true);
                    painter.setFont(font);
                    painter.drawText(rowRect, Qt::AlignCenter | Qt::TextSingleLine, tr("Distribution"));
                    rowRect.translate(0, rowRect.height());
                    colorScaleRect.setTop(rowRect.top());
                    painter.restore();
                    --rows;

                    const pdf::PDFColorScale& colorScale = info.colorScale;
                    pdf::PDFLinearInterpolation<qreal> interpolation(0, rows - 1, colorScale.getMax(), colorScale.getMin());

                    QRect colorRect = colorScaleRect;
                    colorRect.setLeft(colorScaleRect.left() + colorScaleRect.width() / 3);
                    colorRect.setWidth(colorScaleRect.width() / 3);
                    colorRect.setHeight(rowHeight);
                    colorRect.translate(0, rowHeight / 2);

                    QRect textRect = rowRect;
                    textRect.setLeft(colorRect.right() + colorRect.height() / 2);

                    QLocale locale;

                    qreal colorScaleTop = colorRect.top();
                    qreal colorScaleBottom = colorRect.bottom();

                    for (int i = 0; i < rows; ++i)
                    {
                        if (i < rows - 1)
                        {
                            QLinearGradient gradient(0, colorRect.top(), 0, colorRect.bottom());
                            gradient.setColorAt(0, colorScale.map(interpolation(i)));
                            gradient.setColorAt(1, colorScale.map(interpolation(i + 1)));
                            painter.setPen(Qt::NoPen);
                            painter.setBrush(QBrush(gradient));
                            painter.drawRect(colorRect);
                            colorScaleBottom = colorRect.bottom();
                        }

                        pdf::PDFReal value = interpolation(i) * 100.0;

                        QPointF point2 = (textRect.topLeft() + textRect.bottomLeft()) * 0.5;
                        point2.rx() -= rowHeight / 4;
                        QPointF point1 = point2;
                        point1.rx() -= rowHeight / 4;

                        painter.setPen(Qt::black);
                        painter.drawLine(point1, point2);
                        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, QString("%1 %").arg(locale.toString(value, 'f', 2)));

                        colorRect.translate(0, colorRect.height());
                        textRect.translate(0, textRect.height());
                    }

                    if (m_imagePointUnderCursor.has_value())
                    {
                        pdf::PDFLinearInterpolation<qreal> inverseInterpolation(colorScale.getMax(), colorScale.getMin(), colorScaleTop, colorScaleBottom);
                        pdf::PDFColorComponent coverage = m_originalProcessBitmap.getPixelInkCoverage(m_imagePointUnderCursor->x(), m_imagePointUnderCursor->y());
                        qreal yCoordinate = inverseInterpolation(coverage);

                        const int triangleRight = colorRect.left();
                        const int triangleLeft = triangleRight - colorRect.height();
                        const int halfHeight = (triangleRight - triangleLeft) * 0.5;

                        QPolygonF triangle;
                        triangle << QPointF(triangleLeft, yCoordinate - halfHeight);
                        triangle << QPointF(triangleRight, yCoordinate);
                        triangle << QPointF(triangleLeft, yCoordinate + halfHeight);
                        painter.setPen(Qt::NoPen);
                        painter.setBrush(QBrush(Qt::red));
                        painter.drawConvexPolygon(triangle);

                        QString textCoverage = QString("%1 %").arg(locale.toString(coverage * 100.0, 'f', 2));
                        const int textRight = triangleLeft - rowHeight / 4;
                        const int textWidth = painter.fontMetrics().width(textCoverage);
                        const int textStart = textRight - textWidth;

                        QRect textRect(textStart, yCoordinate - halfHeight, textWidth + 1, rowHeight);
                        painter.setPen(Qt::black);
                        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, textCoverage);
                    }
                }

                painter.restore();
            }
        }
    }
}

QMargins OutputPreviewWidget::getDrawMargins() const
{
    const int horizontalMargin = pdf::PDFWidgetUtils::scaleDPI_x(this, 5);
    const int verticalMargin = pdf::PDFWidgetUtils::scaleDPI_y(this, 5);

    return QMargins(horizontalMargin, verticalMargin, horizontalMargin, verticalMargin);
}

QRect OutputPreviewWidget::getContentRect() const
{
    QRect rect = this->rect();
    QRect contentRect = rect.marginsRemoved(getDrawMargins());
    return contentRect;
}

QRect OutputPreviewWidget::getPageImageRect(QRect contentRect) const
{
    int infoBoxWidth = getInfoBoxWidth();

    if (infoBoxWidth > 0)
    {
        infoBoxWidth += pdf::PDFWidgetUtils::scaleDPI_x(this, 5);
    }

    contentRect.setRight(contentRect.right() - infoBoxWidth);
    return contentRect;
}

int OutputPreviewWidget::getInfoBoxWidth() const
{
    if (m_infoBoxItems.empty())
    {
        return 0;
    }

    return pdf::PDFWidgetUtils::scaleDPI_x(this, 200);
}

int OutputPreviewWidget::getInfoBoxContentHorizontalMargin() const
{
    return pdf::PDFWidgetUtils::scaleDPI_x(this, 5);
}

void OutputPreviewWidget::buildInfoBoxItems()
{
    m_infoBoxItems.clear();

    QColor sampleColor;

    switch (m_displayMode)
    {
        case Separations:
        case ColorWarningInkCoverage:
        case ColorWarningRichBlack:
        case InkCoverage:
        {
            if (m_originalProcessBitmap.getWidth() > 0 && m_originalProcessBitmap.getHeight() > 0)
            {
                const pdf::PDFPixelFormat pixelFormat = m_originalProcessBitmap.getPixelFormat();
                std::vector<pdf::PDFInkMapper::ColorInfo> separations = m_inkMapper->getSeparations(pixelFormat.getProcessColorChannelCount(), true);

                QStringList colorValues;
                colorValues.reserve(pixelFormat.getColorChannelCount());
                Q_ASSERT(pixelFormat.getColorChannelCount() == separations.size());

                std::vector<QColor> inkColors;

                if (m_imagePointUnderCursor.has_value())
                {
                    QPoint point = m_imagePointUnderCursor.value();

                    Q_ASSERT(point.x() >= 0);
                    Q_ASSERT(point.x() < m_originalProcessBitmap.getWidth());
                    Q_ASSERT(point.y() >= 0);
                    Q_ASSERT(point.y() < m_originalProcessBitmap.getHeight());

                    pdf::PDFColorBuffer buffer = m_originalProcessBitmap.getPixel(point.x(), point.y());
                    for (int i = 0; i < pixelFormat.getColorChannelCount(); ++i)
                    {
                        const pdf::PDFColorComponent color = buffer[i] * 100.0f;
                        const int percent = qRound(color);
                        colorValues << QString("%1 %").arg(percent);

                        QColor inkColor = separations[i].color;
                        if (inkColor.isValid())
                        {
                            inkColor.setAlphaF(buffer[i]);
                            inkColors.push_back(inkColor);
                        }
                    }

                    Q_ASSERT(point.x() >= 0);
                    Q_ASSERT(point.x() < m_pageImage.width());
                    Q_ASSERT(point.y() >= 0);
                    Q_ASSERT(point.y() < m_pageImage.height());

                    sampleColor = m_pageImage.pixelColor(point);
                }
                else
                {
                    for (int i = 0; i < pixelFormat.getColorChannelCount(); ++i)
                    {
                        colorValues << QString();
                    }
                }

                // Count process/spot inks

                int processInks = 0;
                int spotInks = 0;

                for (const auto& colorInfo : separations)
                {
                    if (!colorInfo.isSpot)
                    {
                        ++processInks;
                    }
                    else
                    {
                        ++spotInks;
                    }
                }

                int colorValueIndex = 0;
                if (processInks > 0)
                {
                    addInfoBoxSeparator();
                    addInfoBoxHeader(tr("Process Inks"));

                    for (const auto& colorInfo : separations)
                    {
                        if (colorInfo.isSpot)
                        {
                            continue;
                        }

                        addInfoBoxColoredItem(colorInfo.color, colorInfo.textName, colorValues[colorValueIndex++]);
                    }
                }

                if (spotInks > 0)
                {
                    addInfoBoxSeparator();
                    addInfoBoxHeader(tr("Spot Inks"));

                    for (const auto& colorInfo : separations)
                    {
                        if (!colorInfo.isSpot)
                        {
                            continue;
                        }

                        addInfoBoxColoredItem(colorInfo.color, colorInfo.textName, colorValues[colorValueIndex++]);
                    }
                }
            }
            break;
        }

        case ShapeChannel:
        case OpacityChannel:
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    if (m_displayMode == ColorWarningInkCoverage)
    {
        addInfoBoxSeparator();
        addInfoBoxHeader(tr("Warning | Ink Coverage"));

        QLocale locale;
        const auto& alarmImage = getAlarmCoverageImage();
        addInfoBoxColoredItem(Qt::green, tr("OK"), QString("%1 mm²").arg(locale.toString(alarmImage.areaValid, 'f', 2)));
        addInfoBoxColoredItem(Qt::red, tr("Failure"), QString("%1 mm²").arg(locale.toString(alarmImage.areaInvalid, 'f', 2)));
    }

    if (m_displayMode == ColorWarningRichBlack)
    {
        addInfoBoxSeparator();
        addInfoBoxHeader(tr("Warning | Rich Black"));

        QLocale locale;
        const auto& alarmImage = getAlarmRichBlackImage();
        addInfoBoxColoredItem(Qt::green, tr("OK"), QString("%1 mm²").arg(locale.toString(alarmImage.areaValid, 'f', 2)));
        addInfoBoxColoredItem(Qt::red, tr("Failure"), QString("%1 mm²").arg(locale.toString(alarmImage.areaInvalid, 'f', 2)));
    }

    if (m_displayMode == Separations || m_displayMode == InkCoverage)
    {
        if (m_originalProcessBitmap.getWidth() > 0 && m_originalProcessBitmap.getHeight() > 0)
        {
            const pdf::PDFPixelFormat pixelFormat = m_originalProcessBitmap.getPixelFormat();
            std::vector<pdf::PDFInkMapper::ColorInfo> separations = m_inkMapper->getSeparations(pixelFormat.getProcessColorChannelCount(), true);
            const std::vector<pdf::PDFColorComponent>& inkCoverage = getInkCoverage();

            if (!inkCoverage.empty() && inkCoverage.size() == separations.size())
            {
                addInfoBoxSeparator();
                addInfoBoxHeader(tr("Ink Coverage"));

                QLocale locale;

                for (size_t i = 0; i < inkCoverage.size(); ++i)
                {
                    const pdf::PDFColorComponent area = inkCoverage[i];
                    const QColor separationColor = separations[i].color;
                    const QString& name = separations[i].textName;

                    addInfoBoxColoredItem(separationColor, name, QString("%1 mm²").arg(locale.toString(area, 'f', 2)));
                }
            }
        }
    }

    if (m_displayMode == ShapeChannel || m_displayMode == OpacityChannel)
    {
        addInfoBoxSeparator();
        addInfoBoxHeader(tr("Shape/Opacity"));
    }

    if (sampleColor.isValid())
    {
        addInfoBoxSeparator();
        addInfoBoxHeader(tr("Sample Color"));
        addInfoBoxColoredRect(sampleColor);
    }
}

void OutputPreviewWidget::addInfoBoxHeader(QString caption)
{
    m_infoBoxItems.push_back(InfoBoxItem(Header, QColor(), caption, QString()));
}

void OutputPreviewWidget::addInfoBoxSeparator()
{
    if (!m_infoBoxItems.empty())
    {
        m_infoBoxItems.push_back(InfoBoxItem(Separator, QColor(), QString(), QString()));
    }
}

void OutputPreviewWidget::addInfoBoxColoredItem(QColor color, QString caption, QString value)
{
    m_infoBoxItems.push_back(InfoBoxItem(ColoredItem, color, caption, value));
}

void OutputPreviewWidget::addInfoBoxColoredRect(QColor color)
{
    m_infoBoxItems.push_back(InfoBoxItem(ColorOnly, color, QString(), QString()));
}

const std::vector<pdf::PDFColorComponent>& OutputPreviewWidget::getInkCoverage() const
{
    return m_inkCoverageMM.get(this, &OutputPreviewWidget::getInkCoverageImpl);
}

const OutputPreviewWidget::AlarmImageInfo& OutputPreviewWidget::getAlarmCoverageImage() const
{
    return m_alarmCoverageImage.get(this, &OutputPreviewWidget::getAlarmCoverageImageImpl);
}

const OutputPreviewWidget::AlarmImageInfo& OutputPreviewWidget::getAlarmRichBlackImage() const
{
    return m_alarmRichBlackImage.get(this, &OutputPreviewWidget::getAlarmRichBlackImageImpl);
}

const OutputPreviewWidget::InkCoverageInfo& OutputPreviewWidget::getInkCoverageInfo() const
{
    return m_inkCoverageImage.get(this, &OutputPreviewWidget::getInkCoverageInfoImpl);
}

const QImage& OutputPreviewWidget::getShapeImage() const
{
    return m_shapeMask.get(this, &OutputPreviewWidget::getShapeImageImpl);
}

const QImage& OutputPreviewWidget::getOpacityImage() const
{
    return m_opacityMask.get(this, &OutputPreviewWidget::getOpacityImageImpl);
}

std::vector<pdf::PDFColorComponent> OutputPreviewWidget::getInkCoverageImpl() const
{
    std::vector<pdf::PDFColorComponent> result;

    if (m_originalProcessBitmap.getWidth() > 0 && m_originalProcessBitmap.getHeight() > 0)
    {
        pdf::PDFPixelFormat pixelFormat = m_originalProcessBitmap.getPixelFormat();
        pdf::PDFColorComponent totalArea = m_pageSizeMM.width() * m_pageSizeMM.height();
        pdf::PDFColorComponent pixelArea = totalArea / pdf::PDFColorComponent(m_originalProcessBitmap.getWidth() * m_originalProcessBitmap.getHeight());

        const uint8_t colorChannelCount = pixelFormat.getColorChannelCount();
        result.resize(colorChannelCount, 0.0f);

        for (size_t y = 0; y < m_originalProcessBitmap.getHeight(); ++y)
        {
            for (size_t x = 0; x < m_originalProcessBitmap.getWidth(); ++x)
            {
                const pdf::PDFConstColorBuffer buffer = m_originalProcessBitmap.getPixel(x, y);
                const pdf::PDFColorComponent alpha = pixelFormat.hasOpacityChannel() ? buffer[pixelFormat.getOpacityChannelIndex()] : 1.0f;

                for (uint8_t i = 0; i < colorChannelCount; ++i)
                {
                    result[i] += buffer[i] * alpha;
                }
            }
        }

        for (uint8_t i = 0; i < colorChannelCount; ++i)
        {
            result[i] *= pixelArea;
        }
    }

    return result;
}

OutputPreviewWidget::AlarmImageInfo OutputPreviewWidget::getAlarmCoverageImageImpl() const
{
    AlarmImageInfo alarmImage;
    alarmImage.image = m_pageImage;
    alarmImage.areaValid = 0.0f;
    alarmImage.areaInvalid = 0.0f;

    const int width = alarmImage.image.width();
    const int height = alarmImage.image.height();

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            pdf::PDFColorComponent inkCoverage = m_originalProcessBitmap.getPixelInkCoverage(x, y);

            if (inkCoverage > m_inkCoverageLimit)
            {
                alarmImage.areaInvalid += 1.0f;
                alarmImage.image.setPixelColor(x, y, m_alarmColor);
            }
            else if (!qFuzzyIsNull(inkCoverage))
            {
                alarmImage.areaValid += 1.0f;
            }
        }
    }

    if (width > 0 && height > 0)
    {
        const pdf::PDFColorComponent factor = m_pageSizeMM.width() * m_pageSizeMM.height() / (pdf::PDFColorComponent(width) * pdf::PDFColorComponent(height));
        alarmImage.areaValid *= factor;
        alarmImage.areaInvalid *= factor;
    }

    return alarmImage;
}

OutputPreviewWidget::AlarmImageInfo OutputPreviewWidget::getAlarmRichBlackImageImpl() const
{
    AlarmImageInfo alarmImage;
    alarmImage.image = m_pageImage;
    alarmImage.areaValid = 0.0f;
    alarmImage.areaInvalid = 0.0f;

    const pdf::PDFPixelFormat pixelFormat = m_originalProcessBitmap.getPixelFormat();
    if (pixelFormat.getProcessColorChannelCount() == 4)
    {
        const int width = alarmImage.image.width();
        const int height = alarmImage.image.height();

        const uint8_t blackChannelIndex = pixelFormat.getProcessColorChannelIndexStart() + 3;

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                pdf::PDFConstColorBuffer buffer = m_originalProcessBitmap.getPixel(x, y);
                pdf::PDFColorComponent blackInk = buffer[blackChannelIndex];
                pdf::PDFColorComponent inkCoverage = m_originalProcessBitmap.getPixelInkCoverage(x, y);
                pdf::PDFColorComponent inkCoverageWithoutBlack = inkCoverage - blackInk;

                if (blackInk > m_richBlackLimit && !qFuzzyIsNull(inkCoverageWithoutBlack))
                {
                    alarmImage.areaInvalid += 1.0f;
                    alarmImage.image.setPixelColor(x, y, m_alarmColor);
                }
                else if (!qFuzzyIsNull(inkCoverage))
                {
                    alarmImage.areaValid += 1.0f;
                }
            }
        }

        if (width > 0 && height > 0)
        {
            const pdf::PDFColorComponent factor = m_pageSizeMM.width() * m_pageSizeMM.height() / (pdf::PDFColorComponent(width) * pdf::PDFColorComponent(height));
            alarmImage.areaValid *= factor;
            alarmImage.areaInvalid *= factor;
        }
    }

    return alarmImage;
}

OutputPreviewWidget::InkCoverageInfo OutputPreviewWidget::getInkCoverageInfoImpl() const
{
    InkCoverageInfo coverageInfo;
    coverageInfo.minValue = 0.0f;
    coverageInfo.maxValue = 1.0f;

    pdf::PDFFloatBitmap inkCoverageBitmap = m_originalProcessBitmap.getInkCoverageBitmap();

    int width = int(inkCoverageBitmap.getWidth());
    int height = int(inkCoverageBitmap.getHeight());

    if (width > 0 && height > 0)
    {
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                pdf::PDFColorBuffer buffer = inkCoverageBitmap.getPixel(x, y);
                const pdf::PDFColorComponent coverage = buffer[0];
                coverageInfo.maxValue = qMax(coverage, coverageInfo.maxValue);
            }
        }

        coverageInfo.colorScale = pdf::PDFColorScale(coverageInfo.minValue, coverageInfo.maxValue);
        coverageInfo.image = QImage(width, height, QImage::Format_RGBX8888);

        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                pdf::PDFColorBuffer buffer = inkCoverageBitmap.getPixel(x, y);
                const pdf::PDFColorComponent coverage = buffer[0];

                coverageInfo.image.setPixelColor(x, y, coverageInfo.colorScale.map(coverage));
            }
        }
    }

    return coverageInfo;
}

QImage OutputPreviewWidget::getShapeImageImpl() const
{
    if (!m_originalProcessBitmap.getPixelFormat().hasShapeChannel())
    {
        return QImage();
    }

    return m_originalProcessBitmap.getChannelImage(m_originalProcessBitmap.getPixelFormat().getShapeChannelIndex());
}

QImage OutputPreviewWidget::getOpacityImageImpl() const
{
    if (!m_originalProcessBitmap.getPixelFormat().hasOpacityChannel())
    {
        return QImage();
    }

    return m_originalProcessBitmap.getChannelImage(m_originalProcessBitmap.getPixelFormat().getOpacityChannelIndex());
}

pdf::PDFColorComponent OutputPreviewWidget::getRichBlackLimit() const
{
    return m_richBlackLimit;
}

void OutputPreviewWidget::setRichBlackLimit(pdf::PDFColorComponent richBlackLimit)
{
    if (m_richBlackLimit != richBlackLimit)
    {
        m_richBlackLimit = richBlackLimit;
        m_alarmRichBlackImage.dirty();
        buildInfoBoxItems();
        update();
    }
}

pdf::PDFColorComponent OutputPreviewWidget::getInkCoverageLimit() const
{
    return m_inkCoverageLimit;
}

void OutputPreviewWidget::setInkCoverageLimit(pdf::PDFColorComponent inkCoverageLimit)
{
    if (m_inkCoverageLimit != inkCoverageLimit)
    {
        m_inkCoverageLimit = inkCoverageLimit;
        m_alarmCoverageImage.dirty();
        buildInfoBoxItems();
        update();
    }
}

OutputPreviewWidget::DisplayMode OutputPreviewWidget::getDisplayMode() const
{
    return m_displayMode;
}

void OutputPreviewWidget::setDisplayMode(const DisplayMode& displayMode)
{
    if (m_displayMode != displayMode)
    {
        m_displayMode = displayMode;
        buildInfoBoxItems();
        update();
    }
}

QColor OutputPreviewWidget::getAlarmColor() const
{
    return m_alarmColor;
}

void OutputPreviewWidget::setAlarmColor(const QColor& alarmColor)
{
    if (m_alarmColor != alarmColor)
    {
        m_alarmColor = alarmColor;
        m_alarmCoverageImage.dirty();
        m_alarmRichBlackImage.dirty();
        update();
    }
}

const pdf::PDFInkMapper* OutputPreviewWidget::getInkMapper() const
{
    return m_inkMapper;
}

void OutputPreviewWidget::setInkMapper(const pdf::PDFInkMapper* inkMapper)
{
    m_inkMapper = inkMapper;
}

QSize OutputPreviewWidget::getPageImageSizeHint() const
{
    return getPageImageRect(getContentRect()).size();
}

void OutputPreviewWidget::mouseMoveEvent(QMouseEvent* event)
{
    m_imagePointUnderCursor = std::nullopt;

    if (m_pageImage.isNull())
    {
        // Nothing to do...
        return;
    }

    QPoint position = event->pos();
    QRect rect = getPageImageRect(getContentRect());

    if (rect.contains(position))
    {
        int verticalImageOffset = (rect.height() - m_pageImage.height()) / 2;
        QPoint imagePoint = position - rect.topLeft() - QPoint(0, verticalImageOffset);

        if (imagePoint.x() >= 0 && imagePoint.x() < m_pageImage.width() &&
            imagePoint.y() >= 0 && imagePoint.y() < m_pageImage.height())
        {
            m_imagePointUnderCursor = imagePoint;
        }
    }

    buildInfoBoxItems();
    update();
}

}   // pdfplugin
