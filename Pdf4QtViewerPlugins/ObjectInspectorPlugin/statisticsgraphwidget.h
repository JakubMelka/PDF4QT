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

#ifndef STATISTICSGRAPHWIDGET_H
#define STATISTICSGRAPHWIDGET_H

#include <QWidget>

#include <vector>

namespace Ui
{
class StatisticsGraphWidget;
}

namespace pdfplugin
{

class StatisticsGraphWidget : public QWidget
{
    Q_OBJECT

public:

    struct StatisticsItem
    {
        bool operator==(const StatisticsItem&) const = default;

        qreal portion;
        QColor color;
        QStringList texts;
    };

    struct Statistics
    {
        bool operator==(const Statistics&) const = default;

        QString title;
        QStringList headers;
        std::vector<StatisticsItem> items;
    };

    explicit StatisticsGraphWidget(QWidget* parent);
    virtual ~StatisticsGraphWidget() override;

    virtual QSize sizeHint() const override;
    virtual QSize minimumSizeHint() const override;

    void setStatistics(Statistics statistics);

protected:
    virtual void paintEvent(QPaintEvent* event) override;

private:
    Ui::StatisticsGraphWidget* ui;

    struct GeometryHint
    {
        QMargins margins;
        int colorRectangleWidth = 0;
        int linesWidthLeft = 0;
        int linesWidthRight = 0;
        int markerSize = 0;
        int normalLineWidth = 0;
        int selectedLineWidth = 0;
        int colorRectangeLeftMargin = 0;

        int titleWidth = 0;
        int titleHeight = 0;
        int textHeight = 0;
        int textMargin = 0;
        int textLineHeight = 0;

        std::vector<int> textWidths;

        QSize minimalWidgetSize;
    };

    GeometryHint getGeometryHint() const;

    QFont getTitleFont() const;
    QFont getHeaderFont() const;
    QFont getTextFont() const;

    Statistics m_statistics;
    std::vector<QRect> m_colorBoxes;
    size_t m_selectedColorBox = -1;
};

}   // namespace pdfplugin

#endif // STATISTICSGRAPHWIDGET_H
