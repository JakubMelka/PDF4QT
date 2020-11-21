//    Copyright (C) 2020 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

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

    inline explicit Dimension() = default;
    inline explicit Dimension(pdf::PDFInteger pageIndex, pdf::PDFReal measuredValue, std::vector<QPointF> polygon) :
        m_pageIndex(pageIndex),
        m_measuredValue(measuredValue),
        m_polygon(qMove(polygon))
    {

    }

    enum Type
    {
        Linear,
        Perimeter,
        Area
    };

    /// Returns true, if definition fo given type is complete
    static bool isComplete(Type type, const std::vector<QPointF>& polygon);

private:
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
        Area,
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
    QPointF adjustPagePoint(QPointF pagePoint) const;
    Dimension::Type getDimensionType() const;
    pdf::PDFReal getMeasuredValue() const;

    Style m_style;
    int m_previewPointPixelSize;
    pdf::PDFPickTool* m_pickTool;
};

#endif // DIMENSIONTOOL_H
