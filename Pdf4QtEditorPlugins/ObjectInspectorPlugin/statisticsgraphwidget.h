// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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
    size_t m_selectedColorBox = std::numeric_limits<size_t>::max();
};

}   // namespace pdfplugin

#endif // STATISTICSGRAPHWIDGET_H
