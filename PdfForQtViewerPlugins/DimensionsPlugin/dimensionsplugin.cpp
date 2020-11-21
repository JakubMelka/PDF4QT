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
#include "settingsdialog.h"

namespace pdfplugin
{

DimensionsPlugin::DimensionsPlugin() :
    pdf::PDFPlugin(nullptr),
    m_dimensionTools(),
    m_showDimensionsAction(nullptr),
    m_clearDimensionsAction(nullptr),
    m_settingsAction(nullptr)
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

    horizontalDimensionAction->setCheckable(true);
    verticalDimensionAction->setCheckable(true);
    linearDimensionAction->setCheckable(true);
    perimeterDimensionAction->setCheckable(true);
    areaDimensionAction->setCheckable(true);

    m_dimensionTools[DimensionTool::LinearHorizontal] = new DimensionTool(DimensionTool::LinearHorizontal, widget->getDrawWidgetProxy(), horizontalDimensionAction, this);
    m_dimensionTools[DimensionTool::LinearVertical] = new DimensionTool(DimensionTool::LinearVertical, widget->getDrawWidgetProxy(), verticalDimensionAction, this);
    m_dimensionTools[DimensionTool::Linear] = new DimensionTool(DimensionTool::Linear, widget->getDrawWidgetProxy(), linearDimensionAction, this);
    m_dimensionTools[DimensionTool::Perimeter] = new DimensionTool(DimensionTool::Perimeter, widget->getDrawWidgetProxy(), perimeterDimensionAction, this);
    m_dimensionTools[DimensionTool::Area] = new DimensionTool(DimensionTool::Area, widget->getDrawWidgetProxy(), areaDimensionAction, this);

    pdf::PDFToolManager* toolManager = widget->getToolManager();
    for (DimensionTool* tool : m_dimensionTools)
    {
        toolManager->addTool(tool);
        connect(tool, &DimensionTool::dimensionCreated, this, &DimensionsPlugin::onDimensionCreated);
    }

    m_showDimensionsAction = new QAction(QIcon(":/pdfplugins/dimensiontool/show-dimensions.svg"), tr("Show Dimensions"), this);
    m_clearDimensionsAction = new QAction(QIcon(":/pdfplugins/dimensiontool/clear-dimensions.svg"), tr("Clear Dimensions"), this);
    m_settingsAction = new QAction(QIcon(":/pdfplugins/dimensiontool/settings.svg"), tr("Settings"), this);

    m_showDimensionsAction->setCheckable(true);
    m_showDimensionsAction->setChecked(true);

    connect(m_showDimensionsAction, &QAction::triggered, this, &DimensionsPlugin::onShowDimensionsTriggered);
    connect(m_clearDimensionsAction, &QAction::triggered, this, &DimensionsPlugin::onClearDimensionsTriggered);
    connect(m_settingsAction, &QAction::triggered, this, &DimensionsPlugin::onSettingsTriggered);

    m_lengthUnit = DimensionUnit::getLengthUnits().front();
    m_areaUnit = DimensionUnit::getAreaUnits().front();
    m_angleUnit = DimensionUnit::getAngleUnits().front();

    m_widget->getDrawWidgetProxy()->registerDrawInterface(this);

    updateActions();
}

void DimensionsPlugin::setDocument(const pdf::PDFModifiedDocument& document)
{
    BaseClass::setDocument(document);

    if (document.hasReset())
    {
        m_dimensions.clear();
        updateActions();
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

    if (!result.empty())
    {
        result.push_back(nullptr);
        result.push_back(m_showDimensionsAction);
        result.push_back(m_clearDimensionsAction);
        result.push_back(m_settingsAction);
    }

    return result;
}


void DimensionsPlugin::drawPage(QPainter* painter,
                                pdf::PDFInteger pageIndex,
                                const pdf::PDFPrecompiledPage* compiledPage,
                                pdf::PDFTextLayoutGetter& layoutGetter,
                                const QMatrix& pagePointToDevicePointMatrix,
                                QList<pdf::PDFRenderError>& errors) const
{
    if (!m_showDimensionsAction || !m_showDimensionsAction->isChecked())
    {
        // Nothing to draw
        return;
    }
}

void DimensionsPlugin::onShowDimensionsTriggered()
{
    if (m_widget)
    {
        m_widget->update();
    }
}

void DimensionsPlugin::onClearDimensionsTriggered()
{
    m_dimensions.clear();
    if (m_widget)
    {
        m_widget->update();
    }
    updateActions();
}

void DimensionsPlugin::onSettingsTriggered()
{
    SettingsDialog dialog(m_widget, m_lengthUnit, m_areaUnit, m_angleUnit);
    dialog.exec();
    m_widget->update();
}

void DimensionsPlugin::onDimensionCreated(Dimension dimension)
{
    m_dimensions.emplace_back(qMove(dimension));
    updateActions();
}

void DimensionsPlugin::updateActions()
{
    if (m_showDimensionsAction)
    {
        m_showDimensionsAction->setEnabled(!m_dimensions.empty());
    }
    if (m_clearDimensionsAction)
    {
        m_clearDimensionsAction->setEnabled(!m_dimensions.empty());
    }
}

}
