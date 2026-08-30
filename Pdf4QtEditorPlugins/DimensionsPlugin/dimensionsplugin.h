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

#include "dimensionsettings.h"
#include "dimensiontool.h"

#include "pdfplugin.h"

#include <QObject>

class QMenu;

namespace pdf
{
class PDFDocumentBuilder;
}

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
    void onConvertToAnnotationsTriggered();
    void onSettingsTriggered();
    void onExportTriggered();
    void onManagePresetsTriggered();
    void onCustomScaleTriggered();
    void onScaleMenuAboutToShow();
    void onCalibrationLinePicked(pdf::PDFReal measuredLength);
    void onDimensionCreated(Dimension dimension);

    void updateActions();
    void updateGraphics();
    void updateScaleAction();

    const DimensionsPluginSettings& getSettings() const { return m_settingsStorage.getSettings(); }

    /// Sets the scale currently in effect, remembers it for the current document,
    /// if it is requested by the settings, and stores everything to the application
    /// settings
    /// \param scale New scale
    void setScale(const DimensionScale& scale);

    /// Selects the scale for the currently opened document. The scale remembered
    /// for the document has the highest priority, then the scale defined by the
    /// measure dictionary of the document, and finally the default scale from
    /// the settings. The scale of the previously opened document is never reused.
    void applyDocumentScale();

    /// Returns the scale defined by the measure dictionaries of the document.
    /// If the document defines more than one different scale, then a single
    /// scale cannot represent it and an invalid scale is returned - such
    /// measurements are resolved for each measurement separately,
    /// see \p getScaleFromViewport.
    DimensionScale getScaleFromDocument() const;

    /// Returns the scale prescribed by the viewport, in which the point lies,
    /// or an invalid scale, if the point does not lie in a viewport with
    /// a usable measure
    /// \param pageIndex Page index
    /// \param point Point in the default user space of the page
    DimensionScale getScaleFromViewport(pdf::PDFInteger pageIndex, const QPointF& point) const;

    /// Returns the scale, which applies to the dimension - either the scale
    /// prescribed by the document, or the scale currently selected by the user
    /// \param dimension Dimension
    const DimensionScale& getEffectiveScale(const Dimension& dimension) const;

    /// Returns true, if the measurements can be stored in the document
    /// as annotations
    bool canCreateAnnotations() const;

    /// Stores the measurements in the document as measurement annotations.
    /// All of them are written in a single modification of the document, so
    /// the whole operation can be undone at once. Returns true, if at least
    /// one annotation was created and the document was really modified.
    /// \param dimensions Dimensions to be stored
    /// \param notCreated If it is not nullptr, then the dimensions, which were
    ///        not stored in the document, are returned in it
    bool createDimensionAnnotations(const std::vector<Dimension>& dimensions, std::vector<Dimension>* notCreated = nullptr);

    /// Creates a single measurement annotation using the builder. The caller is
    /// responsible for finalizing the modification of the document. Returns true,
    /// if the annotation was really created.
    /// \param builder Document builder
    /// \param dimension Dimension
    bool createDimensionAnnotation(pdf::PDFDocumentBuilder* builder, const Dimension& dimension);

    /// Creates the measure dictionary, which describes the scale and units used
    /// for the dimension, so external applications can interpret the created
    /// annotation
    /// \param dimension Dimension, for which is the measure created
    pdf::PDFObject createMeasureDictionary(const Dimension& dimension) const;

    /// Returns the text displayed for the dimension, for example "12.50 m"
    /// \param dimension Dimension
    QString getDimensionText(const Dimension& dimension) const;

    /// Returns the localized name of the dimension type
    /// \param type Dimension type
    static QString getDimensionTypeName(Dimension::Type type);

    /// Collects the measurements, which are stored in the document
    /// as measurement annotations
    std::vector<QStringList> getAnnotationExportRows() const;

    std::array<DimensionTool*, DimensionTool::LastStyle> m_dimensionTools;
    std::vector<Dimension> m_dimensions;

    QAction* m_showDimensionsAction;
    QAction* m_clearDimensionsAction;
    QAction* m_convertToAnnotationsAction;
    QAction* m_exportAction;
    QAction* m_scaleAction;
    QAction* m_settingsAction;
    QMenu* m_scaleMenu;

    DimensionsSettingsStorage m_settingsStorage;

    /// Identification of the currently opened document, under which its scale
    /// is remembered in the application settings
    DocumentIdentity m_documentIdentity;

    /// Scale currently in effect. It is not a part of the settings, because it
    /// belongs to the opened document and must not leak to another one.
    DimensionScale m_scale;
};

}   // namespace pdfplugin

#endif // DIMENSIONSPLUGIN_H
