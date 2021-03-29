//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

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
    m_alarmColor(Qt::red)
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

    if (pageImageRect.isValid() && !m_pageImage.isNull())
    {
        painter.save();
        painter.setClipRect(pageImageRect, Qt::IntersectClip);
        painter.translate(0, (pageImageRect.height() - m_pageImage.height()) / 2);
        painter.drawImage(pageImageRect.topLeft(), m_pageImage);
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

        painter.restore();
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

    return pdf::PDFWidgetUtils::scaleDPI_x(this, 150);
}

int OutputPreviewWidget::getInfoBoxContentHorizontalMargin() const
{
    return pdf::PDFWidgetUtils::scaleDPI_x(this, 5);
}

void OutputPreviewWidget::buildInfoBoxItems()
{
    m_infoBoxItems.clear();

    switch (m_displayMode)
    {
        case pdfplugin::OutputPreviewWidget::Separations:
        case pdfplugin::OutputPreviewWidget::ColorWarningInkCoverage:
        case pdfplugin::OutputPreviewWidget::ColorWarningRichBlack:
        {
            if (m_originalProcessBitmap.getWidth() > 0 && m_originalProcessBitmap.getHeight() > 0)
            {
                const pdf::PDFPixelFormat pixelFormat = m_originalProcessBitmap.getPixelFormat();
                std::vector<pdf::PDFInkMapper::ColorInfo> separations = m_inkMapper->getSeparations(pixelFormat.getProcessColorChannelCount(), true);

                QStringList colorValues;
                colorValues.reserve(pixelFormat.getColorChannelCount());
                Q_ASSERT(pixelFormat.getColorChannelCount() == separations.size());

                QColor sampleColor;
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

                if (sampleColor.isValid())
                {
                    addInfoBoxSeparator();
                    addInfoBoxHeader(tr("Sample Color"));
                    addInfoBoxColoredRect(sampleColor);
                }
            }
            break;
        }

        case pdfplugin::OutputPreviewWidget::InkCoverage:
            break;

        default:
            Q_ASSERT(false);
            break;
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

QColor OutputPreviewWidget::getAlarmColor() const
{
    return m_alarmColor;
}

void OutputPreviewWidget::setAlarmColor(const QColor& alarmColor)
{
    m_alarmColor = alarmColor;
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
