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
#include <QFontMetrics>

namespace pdfplugin
{

OutputPreviewWidget::OutputPreviewWidget(QWidget* parent) :
    BaseClass(parent),
    m_inkMapper(nullptr),
    m_displayMode(Separations)
{

}

QSize OutputPreviewWidget::sizeHint() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(400, 300));
}

QSize OutputPreviewWidget::minimumSizeHint() const
{
    return pdf::PDFWidgetUtils::scaleDPI(this, QSize(200, 200));
}

void OutputPreviewWidget::clear()
{
    m_pageImage = QImage();
    m_originalProcessBitmap = pdf::PDFFloatBitmapWithColorSpace();
    m_pageSizeMM = QSizeF();
    m_infoBoxItems.clear();
    update();
}

void OutputPreviewWidget::setPageImage(QImage image, pdf::PDFFloatBitmapWithColorSpace originalProcessBitmap, QSizeF pageSizeMM)
{
    m_pageImage = qMove(image);
    m_originalProcessBitmap = qMove(originalProcessBitmap);
    m_pageSizeMM = pageSizeMM;
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

                        addInfoBoxColoredItem(Qt::green, colorInfo.textName, QString("100 %"));
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

                        addInfoBoxColoredItem(Qt::blue, colorInfo.textName, QString("100 %"));
                    }
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

}   // pdfplugin
