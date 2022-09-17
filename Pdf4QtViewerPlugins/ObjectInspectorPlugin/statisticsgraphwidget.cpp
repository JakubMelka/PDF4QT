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

#include "statisticsgraphwidget.h"
#include "ui_statisticsgraphwidget.h"

#include "pdfwidgetutils.h"
#include "pdfpainterutils.h"

#include <QPainter>

namespace pdfplugin
{

StatisticsGraphWidget::StatisticsGraphWidget(QWidget* parent) :
    QWidget(parent),
    ui(new Ui::StatisticsGraphWidget)
{
    ui->setupUi(this);
}

StatisticsGraphWidget::~StatisticsGraphWidget()
{
    delete ui;
}

void StatisticsGraphWidget::setStatistics(Statistics statistics)
{
    if (m_statistics != statistics)
    {
        m_statistics = qMove(statistics);
        updateGeometry();
        update();
    }
}

StatisticsGraphWidget::GeometryHint StatisticsGraphWidget::getGeometryHint() const
{
    GeometryHint hint;

    int fivePixelsX = pdf::PDFWidgetUtils::scaleDPI_x(this, 5);
    int fivePixelsY = pdf::PDFWidgetUtils::scaleDPI_y(this, 5);
    int textMargin = fivePixelsX;

    hint.margins = QMargins(fivePixelsX, fivePixelsY, fivePixelsX, fivePixelsY);
    hint.colorRectangleWidth = pdf::PDFWidgetUtils::scaleDPI_x(this, 80);
    hint.linesWidthLeft = pdf::PDFWidgetUtils::scaleDPI_x(this, 15);
    hint.linesWidthRight = pdf::PDFWidgetUtils::scaleDPI_x(this, 5);
    hint.markerSize = fivePixelsX;
    hint.normalLineWidth = pdf::PDFWidgetUtils::scaleDPI_y(this, 2);
    hint.selectedLineWidth = pdf::PDFWidgetUtils::scaleDPI_y(this, 3);
    hint.colorRectangeLeftMargin = pdf::PDFWidgetUtils::scaleDPI_x(this, 40);

    QFontMetrics fontMetricsTitle(getTitleFont());
    hint.titleWidth = fontMetricsTitle.horizontalAdvance(m_statistics.title);
    hint.titleHeight = fontMetricsTitle.lineSpacing();

    QFontMetrics fontMetricsHeader(getHeaderFont());
    hint.textHeight = fontMetricsHeader.lineSpacing();

    QFontMetrics fontMetricsText(getTextFont());
    hint.textHeight += fontMetricsText.lineSpacing() * int(m_statistics.items.size());

    hint.textMargin = textMargin;
    hint.textLineHeight = fontMetricsText.lineSpacing();

    // Determine text header width
    hint.textWidths.resize(m_statistics.headers.size(), 0);

    for (int i = 0; i < m_statistics.headers.size(); ++i)
    {
        hint.textWidths[i] = fontMetricsHeader.horizontalAdvance(m_statistics.headers[i]);
    }

    for (const auto& item : m_statistics.items)
    {
        Q_ASSERT(static_cast<size_t>(item.texts.size()) == hint.textWidths.size());

        for (int i = 0; i < item.texts.size(); ++i)
        {
            hint.textWidths[i] = qMax(hint.textWidths[i], fontMetricsHeader.horizontalAdvance(item.texts[i]));
        }
    }

    int totalTextWidth = textMargin;
    for (int& textWidth : hint.textWidths)
    {
        textWidth += 2 * textMargin;
        totalTextWidth += textWidth;
    }

    const int widthHint = hint.margins.left() + hint.colorRectangeLeftMargin + hint.colorRectangleWidth + hint.linesWidthLeft + hint.linesWidthRight + qMax(totalTextWidth, hint.titleWidth) + hint.margins.right();
    const int heightHint = hint.margins.top() + hint.titleHeight + qMax(pdf::PDFWidgetUtils::scaleDPI_y(this, 100), hint.textHeight) + hint.margins.bottom();

    hint.minimalWidgetSize = QSize(widthHint, heightHint).expandedTo(pdf::PDFWidgetUtils::scaleDPI(this, QSize(400, 300)));

    return hint;
}

void StatisticsGraphWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    QRect rect = this->rect();

    painter.fillRect(rect, Qt::white);

    GeometryHint geometryHint = getGeometryHint();
    rect = rect.marginsRemoved(geometryHint.margins);

    if (!m_statistics.title.isEmpty())
    {
        pdf::PDFPainterStateGuard guard(&painter);

        QRect titleRect = rect;
        titleRect.setHeight(geometryHint.titleHeight);

        painter.setFont(getHeaderFont());
        painter.drawText(titleRect, Qt::AlignCenter, m_statistics.title);

        rect.setTop(titleRect.bottom());
    }

    rect.setLeft(rect.left() + geometryHint.colorRectangeLeftMargin);

    // Color boxes
    m_colorBoxes.clear();
    const int height = rect.height();

    QColor selectedColor = Qt::darkYellow;

    int top = rect.top();
    for (size_t i = 0; i < m_statistics.items.size(); ++i)
    {
        const StatisticsItem& item = m_statistics.items[i];
        QRect currentRect(rect.left(), top, geometryHint.colorRectangleWidth, height * item.portion);
        top = currentRect.bottom();

        QColor color = (m_selectedColorBox == i) ? selectedColor: item.color;
        color.setAlphaF(0.8f);
        painter.fillRect(currentRect, color);
        m_colorBoxes.push_back(currentRect);
    }

    QPoint minLinePoint;
    QPoint maxLinePoint;

    // Marked lines
    {
        pdf::PDFPainterStateGuard guard(&painter);
        for (size_t i = 0; i < m_statistics.items.size(); ++i)
        {
            QPen pen = painter.pen();
            pen.setColor((i == m_selectedColorBox) ? selectedColor : Qt::black);
            pen.setWidth(geometryHint.markerSize);
            pen.setCapStyle(Qt::RoundCap);
            painter.setPen(pen);

            QRect colorBox = m_colorBoxes[i];
            QPoint marker = colorBox.center();
            marker.setX(colorBox.right());

            painter.drawPoint(marker);

            pen.setWidth((i == m_selectedColorBox) ? geometryHint.selectedLineWidth : geometryHint.normalLineWidth);
            painter.setPen(pen);

            QPoint lineEnd = marker + QPoint(geometryHint.linesWidthLeft, 0);
            painter.drawLine(marker, lineEnd);

            if (i == 0)
            {
                minLinePoint = lineEnd;
            }
            maxLinePoint = lineEnd;
        }
    }

    // Texts
    {
        pdf::PDFPainterStateGuard guard(&painter);

        QFont headerFont = getHeaderFont();
        QFont textFont = getTextFont();

        QRect textRect = rect;
        textRect.setLeft(maxLinePoint.x() + geometryHint.textMargin);
        textRect.setHeight(geometryHint.textLineHeight);

        std::vector<QLine> horizontalLines;
        horizontalLines.reserve(m_statistics.items.size());

        auto drawTexts = [&](const QStringList& texts, bool isHeader)
        {
            pdf::PDFPainterStateGuard guard(&painter);
            painter.setFont(isHeader ? headerFont : textFont);

            const int y = textRect.center().y();
            minLinePoint.ry() = qMin(minLinePoint.ry(), y);
            maxLinePoint.ry() = qMax(maxLinePoint.ry(), y);

            QRect cellRect = textRect;
            for (int i = 0; i < texts.size(); ++i)
            {
                cellRect.setWidth(geometryHint.textWidths[i]);
                QRect finalCellRect = cellRect.marginsRemoved(QMargins(geometryHint.textMargin, 0, geometryHint.textMargin, 0));
                QString text = texts[i];
                Qt::Alignment alignment = (i == 0) ? Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter) : Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter);
                painter.drawText(finalCellRect, alignment, text);
                cellRect.translate(cellRect.width(), 0);
            }

            if (!isHeader)
            {
                QPoint startPoint = textRect.center();
                startPoint.setX(textRect.left());

                QPoint endPoint(maxLinePoint.x(), startPoint.y());
                horizontalLines.emplace_back(startPoint, endPoint);
            }

            textRect.translate(0, textRect.height());
        };

        if (!m_statistics.headers.isEmpty())
        {
            drawTexts(m_statistics.headers, true);
        }

        for (size_t i = 0; i < m_statistics.items.size(); ++i)
        {
            const StatisticsItem& item = m_statistics.items[i];
            drawTexts(item.texts, i == m_selectedColorBox);
        }

        QPen pen = painter.pen();
        pen.setColor(Qt::black);
        pen.setWidth(geometryHint.normalLineWidth);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);

        painter.drawLine(minLinePoint, maxLinePoint);

        for (const QLine& line : horizontalLines)
        {
            painter.drawLine(line);
        }

        pen = painter.pen();
        pen.setColor(Qt::black);
        pen.setWidth(geometryHint.markerSize);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);

        for (const QLine& line : horizontalLines)
        {
            painter.drawPoint(line.p1());
        }
    }
}

QFont StatisticsGraphWidget::getTitleFont() const
{
    QFont font = this->font();
    font.setPointSize(font.pointSize() * 2);
    font.setBold(true);
    return font;
}

QFont StatisticsGraphWidget::getHeaderFont() const
{
    QFont font = this->font();
    font.setBold(true);
    return font;
}

QFont StatisticsGraphWidget::getTextFont() const
{
    return this->font();
}

QSize StatisticsGraphWidget::sizeHint() const
{
    return getGeometryHint().minimalWidgetSize + pdf::PDFWidgetUtils::scaleDPI(this, QSize(100, 100));
}

QSize StatisticsGraphWidget::minimumSizeHint() const
{
    return getGeometryHint().minimalWidgetSize;
}

}   // namespace pdfplugin

