//    Copyright (C) 2019-2020 Jakub Melka
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
#include "pdfcms.h"
#include "pdfexecutionpolicy.h"
#include "pdfform.h"

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
        Settings();

        bool operator==(const Settings&) const = default;
        bool operator!=(const Settings&) const = default;

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
        pdf::PDFExecutionPolicy::Strategy m_multithreadingStrategy = pdf::PDFExecutionPolicy::Strategy::PageMultithreaded;

        // Cache settings
        int m_compiledPageCacheLimit;
        int m_thumbnailsCacheLimit;
        int m_fontCacheLimit;
        int m_instancedFontCacheLimit;

        // Speech settings
        QString m_speechEngine;
        QString m_speechLocale;
        QString m_speechVoice;
        double m_speechRate;
        double m_speechPitch;
        double m_speechVolume;

        // Magnifier tool settings
        int m_magnifierSize;
        double m_magnifierZoom;

        // Form settings
        pdf::PDFFormManager::FormAppearanceFlags m_formAppearanceFlags;
    };

    const Settings& getSettings() const { return m_settings; }
    void setSettings(const Settings& settings);

    void readSettings(QSettings& settings, const pdf::PDFCMSSettings& defaultCMSSettings);
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

    int getCompiledPageCacheLimit() const { return m_settings.m_compiledPageCacheLimit; }
    int getThumbnailsCacheLimit() const { return m_settings.m_thumbnailsCacheLimit; }
    int getFontCacheLimit() const { return m_settings.m_fontCacheLimit; }
    int getInstancedFontCacheLimit() const { return m_settings.m_instancedFontCacheLimit; }

    const pdf::PDFCMSSettings& getColorManagementSystemSettings() const { return m_colorManagementSystemSettings; }
    void setColorManagementSystemSettings(const pdf::PDFCMSSettings& settings) { m_colorManagementSystemSettings = settings; }

    pdf::PDFExecutionPolicy::Strategy getMultithreadingStrategy() const { return m_settings.m_multithreadingStrategy; }

signals:
    void settingsChanged();

private:
    Settings m_settings;
    pdf::PDFCMSSettings m_colorManagementSystemSettings;
};

}   // namespace pdfviewer

#endif // PDFVIEWERSETTINGS_H
