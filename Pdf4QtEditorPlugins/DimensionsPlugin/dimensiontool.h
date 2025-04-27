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

#ifndef DIMENSIONTOOL_H
#define DIMENSIONTOOL_H

#include "pdfwidgettool.h"

#include <QAction>
#include <QPolygonF>

struct DimensionUnit;
using DimensionUnits = std::vector<DimensionUnit>;

struct DimensionUnit
{
    explicit inline DimensionUnit() = default;
    explicit inline DimensionUnit(pdf::PDFReal scale, QString symbol) :
        scale(scale),
        symbol(qMove(symbol))
    {

    }

    pdf::PDFReal scale = 1.0;
    QString symbol;

    static DimensionUnits getLengthUnits();
    static DimensionUnits getAreaUnits();
    static DimensionUnits getAngleUnits();
};

class Dimension
{
public:

    enum Type
    {
        Linear,
        Perimeter,
        Area,
        Angular
    };

    inline explicit Dimension() = default;
    inline explicit Dimension(Type type, pdf::PDFInteger pageIndex, pdf::PDFReal measuredValue, std::vector<QPointF> polygon) :
        m_type(type),
        m_pageIndex(pageIndex),
        m_measuredValue(measuredValue),
        m_polygon(qMove(polygon))
    {

    }

    Type getType() const { return m_type; }
    pdf::PDFInteger getPageIndex() const { return m_pageIndex; }
    pdf::PDFReal getMeasuredValue() const { return m_measuredValue; }
    const std::vector<QPointF>& getPolygon() const { return m_polygon; }

    /// Returns true, if definition fo given type is complete
    static bool isComplete(Type type, const std::vector<QPointF>& polygon);

private:
    Type m_type = Linear;
    pdf::PDFInteger m_pageIndex = -1;
    pdf::PDFReal m_measuredValue = 0.0;
    std::vector<QPointF> m_polygon;
};

class DimensionTool : public pdf::PDFWidgetTool
{
    Q_OBJECT

private:
    using BaseClass = pdf::PDFWidgetTool;

public:

    enum Style
    {
        LinearHorizontal,
        LinearVertical,
        Linear,
        Perimeter,
        RectanglePerimeter,
        Area,
        RectangleArea,
        Angle,
        LastStyle
    };


    explicit DimensionTool(Style style, pdf::PDFDrawWidgetProxy* proxy, QAction* action, QObject* parent);

    void drawPage(QPainter* painter,
                  pdf::PDFInteger pageIndex,
                  const pdf::PDFPrecompiledPage* compiledPage,
                  pdf::PDFTextLayoutGetter& layoutGetter,
                  const QTransform& pagePointToDevicePointMatrix,
                  const pdf::PDFColorConvertor& convertor,
                  QList<pdf::PDFRenderError>& errors) const override;

signals:
    void dimensionCreated(Dimension dimension);

private:
    void onPointPicked(pdf::PDFInteger pageIndex, QPointF pagePoint);
    void onRectanglePicked(pdf::PDFInteger pageIndex, QRectF pageRectangle);

    QPointF adjustPagePoint(QPointF pagePoint) const;
    Dimension::Type getDimensionType() const;
    pdf::PDFReal getMeasuredValue(pdf::PDFInteger pageIndex, const std::vector<QPointF>& pickedPoints) const;

    Style m_style;
    int m_previewPointPixelSize;
    pdf::PDFPickTool* m_pickTool;
};

#endif // DIMENSIONTOOL_H
