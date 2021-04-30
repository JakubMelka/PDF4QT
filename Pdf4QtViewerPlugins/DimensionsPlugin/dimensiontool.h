//    Copyright (C) 2020-2021 Jakub Melka
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
                  const QMatrix& pagePointToDevicePointMatrix,
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
