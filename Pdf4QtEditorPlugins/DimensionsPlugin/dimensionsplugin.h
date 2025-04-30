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
    Q_PLUGIN_METADATA(IID "PDF4QT.DimensionsPlugin" FILE "DimensionsPlugin.json")

private:
    using BaseClass = pdf::PDFPlugin;

public:
    DimensionsPlugin();

    virtual void setWidget(pdf::PDFWidget* widget) override;
    virtual void setDocument(const pdf::PDFModifiedDocument& document) override;
    virtual std::vector<QAction*> getActions() const override;
    virtual QString getPluginMenuName() const override;

    virtual void drawPage(QPainter* painter,
                          pdf::PDFInteger pageIndex,
                          const pdf::PDFPrecompiledPage* compiledPage,
                          pdf::PDFTextLayoutGetter& layoutGetter,
                          const QTransform& pagePointToDevicePointMatrix,
                          const pdf::PDFColorConvertor& convertor,
                          QList<pdf::PDFRenderError>& errors) const override;

private:
    void onShowDimensionsTriggered();
    void onClearDimensionsTriggered();
    void onSettingsTriggered();
    void onDimensionCreated(Dimension dimension);
    void updateActions();
    void updateGraphics();

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
