//    Copyright (C) 2019 Jakub Melka
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

#ifndef PDFVIEWERSETTINGS_H
#define PDFVIEWERSETTINGS_H

#include "pdfrenderer.h"

#include <QObject>

namespace pdfviewer
{

class PDFViewerSettings : public QObject
{
    Q_OBJECT

public:
    inline explicit PDFViewerSettings(QObject* parent) :
        QObject(parent)
    {

    }

    struct Settings
    {
        Settings() :
            m_features(pdf::PDFRenderer::getDefaultFeatures()),
            m_rendererEngine(pdf::RendererEngine::OpenGL),
            m_multisampleAntialiasing(true),
            m_rendererSamples(16),
            m_prefetchPages(true),
            m_preferredMeshResolutionRatio(0.02),
            m_minimalMeshResolutionRatio(0.005),
            m_colorTolerance(0.01),
            m_allowLaunchApplications(true),
            m_allowLaunchURI(true)
        {

        }

        pdf::PDFRenderer::Features m_features;
        QString m_directory;
        pdf::RendererEngine m_rendererEngine;
        bool m_multisampleAntialiasing;
        int m_rendererSamples;
        bool m_prefetchPages;
        pdf::PDFReal m_preferredMeshResolutionRatio;
        pdf::PDFReal m_minimalMeshResolutionRatio;
        pdf::PDFReal m_colorTolerance;
        bool m_allowLaunchApplications;
        bool m_allowLaunchURI;
    };

    const Settings& getSettings() const { return m_settings; }
    void setSettings(const Settings& settings);

    void readSettings(QSettings& settings);
    void writeSettings(QSettings& settings);

    QString getDirectory() const;
    void setDirectory(const QString& directory);

    pdf::PDFRenderer::Features getFeatures() const;
    void setFeatures(const pdf::PDFRenderer::Features& features);

    pdf::RendererEngine getRendererEngine() const;
    void setRendererEngine(pdf::RendererEngine rendererEngine);

    int getRendererSamples() const;
    void setRendererSamples(int rendererSamples);

    bool isPagePrefetchingEnabled() const { return m_settings.m_prefetchPages; }
    bool isMultisampleAntialiasingEnabled() const { return m_settings.m_multisampleAntialiasing; }

    pdf::PDFReal getPreferredMeshResolutionRatio() const { return m_settings.m_preferredMeshResolutionRatio; }
    void setPreferredMeshResolutionRatio(pdf::PDFReal preferredMeshResolutionRatio);

    pdf::PDFReal getMinimalMeshResolutionRatio() const { return m_settings.m_minimalMeshResolutionRatio; }
    void setMinimalMeshResolutionRatio(pdf::PDFReal minimalMeshResolutionRatio);

    pdf::PDFReal getColorTolerance() const { return m_settings.m_colorTolerance; }
    void setColorTolerance(pdf::PDFReal colorTolerance);

signals:
    void settingsChanged();

private:
    Settings m_settings;
};


}   // namespace pdfviewer

#endif // PDFVIEWERSETTINGS_H
