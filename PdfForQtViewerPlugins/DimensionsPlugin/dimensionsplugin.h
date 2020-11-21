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

#ifndef DIMENSIONSPLUGIN_H
#define DIMENSIONSPLUGIN_H

#include "pdfplugin.h"
#include "dimensiontool.h"

#include <QObject>

namespace pdfplugin
{

class DimensionsPlugin : public pdf::PDFPlugin, public pdf::IDocumentDrawInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "PdfForQt.DimensionsPlugin" FILE "DimensionsPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    DimensionsPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;

    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const pdf::PDFPrecompiledPage* compiledPage,
                          pdf::PDFTextLayoutGetter& layoutGetter,
                          const QMatrix& pagePointToDevicePointMatrix,
                          QList<pdf::PDFRenderError>& errors) const override;

private:
    void onShowDimensionsTriggered();
    void onClearDimensionsTriggered();
    void onSettingsTriggered();
    void onDimensionCreated(Dimension dimension);
    void updateActions();

    std::array<DimensionTool*, DimensionTool::LastStyle> m_dimensionTools;
    std::vector<Dimension> m_dimensions;

    QAction* m_showDimensionsAction;
    QAction* m_clearDimensionsAction;
    QAction* m_settingsAction;

    DimensionUnit m_lengthUnit;
    DimensionUnit m_areaUnit;
    DimensionUnit m_angleUnit;
};

}   // namespace pdfplugin

#endif // DIMENSIONSPLUGIN_H
