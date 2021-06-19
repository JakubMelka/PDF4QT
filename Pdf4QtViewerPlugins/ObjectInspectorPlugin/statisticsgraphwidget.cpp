//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "statisticsgraphwidget.h"
#include "ui_statisticsgraphwidget.h"

#include "pdfwidgetutils.h"

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

    hint.margins = QMargins(fivePixelsX, fivePixelsY, fivePixelsX, fivePixelsY);
    hint.colorRectangleWidth = pdf::PDFWidgetUtils::scaleDPI_x(this, 40);
    hint.linesWidthLeft = pdf::PDFWidgetUtils::scaleDPI_x(this, 15);
    hint.linesWidthRight = pdf::PDFWidgetUtils::scaleDPI_x(this, 5);

    QFontMetrics fontMetricsTitle(getTitleFont());
    hint.titleWidth = fontMetricsTitle.horizontalAdvance(m_statistics.title);
    hint.titleHeight = fontMetricsTitle.lineSpacing();

    QFontMetrics fontMetricsHeader(getHeaderFont());
    hint.textHeight = fontMetricsHeader.lineSpacing();

    QFontMetrics fontMetricsText(getTextFont());
    hint.textHeight += fontMetricsText.lineSpacing() * int(m_statistics.items.size());

    // Determine text header width
    s

    return hint;
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

}   // namespace pdfplugin
