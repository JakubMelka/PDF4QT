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

#include "dimensionsplugin.h"
#include "pdfdrawwidget.h"

namespace pdfplugin
{

DimensionsPlugin::DimensionsPlugin() :
    pdf::PDFPlugin(nullptr),
    m_dimensionTools()
{

}

void DimensionsPlugin::setWidget(pdf::PDFWidget* widget)
{
    Q_ASSERT(!m_widget);

    BaseClass::setWidget(widget);

    QAction* horizontalDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/linear-horizontal.svg"), tr("Horizontal Dimension"), this);
    QAction* verticalDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/linear-vertical.svg"), tr("Vertical Dimension"), this);
    QAction* linearDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/linear.svg"), tr("Linear Dimension"), this);
    QAction* perimeterDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/perimeter.svg"), tr("Perimeter"), this);
    QAction* areaDimensionAction = new QAction(QIcon(":/pdfplugins/dimensiontool/area.svg"), tr("Area"), this);

    horizontalDimensionAction->setObjectName("dimensiontool_LinearHorizontalAction");
    verticalDimensionAction->setObjectName("dimensiontool_LinearVerticalAction");
    linearDimensionAction->setObjectName("dimensiontool_LinearAction");
    perimeterDimensionAction->setObjectName("dimensiontool_PerimeterAction");
    areaDimensionAction->setObjectName("dimensiontool_AreaAction");

    m_dimensionTools[DimensionTool::LinearHorizontal] = new DimensionTool(DimensionTool::LinearHorizontal, widget->getDrawWidgetProxy(), horizontalDimensionAction, this);
    m_dimensionTools[DimensionTool::LinearVertical] = new DimensionTool(DimensionTool::LinearVertical, widget->getDrawWidgetProxy(), verticalDimensionAction, this);
    m_dimensionTools[DimensionTool::Linear] = new DimensionTool(DimensionTool::Linear, widget->getDrawWidgetProxy(), linearDimensionAction, this);
    m_dimensionTools[DimensionTool::Perimeter] = new DimensionTool(DimensionTool::Perimeter, widget->getDrawWidgetProxy(), perimeterDimensionAction, this);
    m_dimensionTools[DimensionTool::Area] = new DimensionTool(DimensionTool::Area, widget->getDrawWidgetProxy(), areaDimensionAction, this);
}

void DimensionsPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    for (DimensionTool* tool : m_dimensionTools)
    {
        if (tool)
        {
            tool->setDocument(document);
        }
    }
}

std::vector<QAction*> DimensionsPlugin::getActions() const
{
    std::vector<QAction*> result;

    for (DimensionTool* tool : m_dimensionTools)
    {
        if (tool)
        {
            result.push_back(tool->getAction());
        }
    }

    return result;
}

}
