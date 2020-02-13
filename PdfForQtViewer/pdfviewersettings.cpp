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

#include "pdfviewersettings.h"
#include "pdfconstants.h"

#include <QPixmapCache>

namespace pdfviewer
{
const int PIXMAP_CACHE_LIMIT = QPixmapCache::cacheLimit();

void PDFViewerSettings::setSettings(const PDFViewerSettings::Settings& settings)
{
    if (m_settings != settings)
    {
        m_settings = settings;
        emit settingsChanged();
    }
}

void PDFViewerSettings::readSettings(QSettings& settings, const pdf::PDFCMSSettings& defaultCMSSettings)
{
    Settings defaultSettings;

    settings.beginGroup("ViewerSettings");
    m_settings.m_directory = settings.value("defaultDirectory", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    m_settings.m_features = static_cast<pdf::PDFRenderer::Features>(settings.value("rendererFeatures", static_cast<int>(pdf::PDFRenderer::getDefaultFeatures())).toInt());
    m_settings.m_rendererEngine = static_cast<pdf::RendererEngine>(settings.value("renderingEngine", static_cast<int>(pdf::RendererEngine::OpenGL)).toInt());
    m_settings.m_multisampleAntialiasing = settings.value("msaa", defaultSettings.m_multisampleAntialiasing).toBool();
    m_settings.m_rendererSamples = settings.value("rendererSamples", defaultSettings.m_rendererSamples).toInt();
    m_settings.m_prefetchPages = settings.value("prefetchPages", defaultSettings.m_prefetchPages).toBool();
    m_settings.m_preferredMeshResolutionRatio = settings.value("preferredMeshResolutionRatio", defaultSettings.m_preferredMeshResolutionRatio).toDouble();
    m_settings.m_minimalMeshResolutionRatio = settings.value("minimalMeshResolutionRatio", defaultSettings.m_minimalMeshResolutionRatio).toDouble();
    m_settings.m_colorTolerance = settings.value("colorTolerance", defaultSettings.m_colorTolerance).toDouble();
    m_settings.m_compiledPageCacheLimit = settings.value("compiledPageCacheLimit", defaultSettings.m_compiledPageCacheLimit).toInt();
    m_settings.m_thumbnailsCacheLimit = settings.value("thumbnailsCacheLimit", defaultSettings.m_thumbnailsCacheLimit).toInt();
    m_settings.m_fontCacheLimit = settings.value("fontCacheLimit", defaultSettings.m_fontCacheLimit).toInt();
    m_settings.m_instancedFontCacheLimit = settings.value("instancedFontCacheLimit", defaultSettings.m_instancedFontCacheLimit).toInt();
    m_settings.m_allowLaunchApplications = settings.value("allowLaunchApplications", defaultSettings.m_allowLaunchApplications).toBool();
    m_settings.m_allowLaunchURI = settings.value("allowLaunchURI", defaultSettings.m_allowLaunchURI).toBool();
    m_settings.m_multithreadingStrategy = static_cast<pdf::PDFExecutionPolicy::Strategy>(settings.value("mutlithreadingStrategy", static_cast<int>(defaultSettings.m_multithreadingStrategy)).toInt());
    settings.endGroup();

    settings.beginGroup("ColorManagementSystemSettings");
    m_colorManagementSystemSettings.system = static_cast<pdf::PDFCMSSettings::System>(settings.value("system", int(defaultCMSSettings.system)).toInt());
    m_colorManagementSystemSettings.accuracy = static_cast<pdf::PDFCMSSettings::Accuracy>(settings.value("accuracy", int(defaultCMSSettings.accuracy)).toInt());
    m_colorManagementSystemSettings.intent = static_cast<pdf::RenderingIntent>(settings.value("intent", int(defaultCMSSettings.intent)).toInt());
    m_colorManagementSystemSettings.isBlackPointCompensationActive = settings.value("isBlackPointCompensationActive", defaultCMSSettings.isBlackPointCompensationActive).toBool();
    m_colorManagementSystemSettings.isWhitePaperColorTransformed = settings.value("isWhitePaperColorTransformed", defaultCMSSettings.isWhitePaperColorTransformed).toBool();
    m_colorManagementSystemSettings.outputCS = settings.value("outputCS", defaultCMSSettings.outputCS).toString();
    m_colorManagementSystemSettings.deviceGray = settings.value("deviceGray", defaultCMSSettings.deviceGray).toString();
    m_colorManagementSystemSettings.deviceRGB = settings.value("deviceRGB", defaultCMSSettings.deviceRGB).toString();
    m_colorManagementSystemSettings.deviceCMYK = settings.value("deviceCMYK", defaultCMSSettings.deviceCMYK).toString();
    m_colorManagementSystemSettings.profileDirectory = settings.value("profileDirectory", defaultCMSSettings.profileDirectory).toString();
    settings.endGroup();

    settings.beginGroup("SpeechSettings");
    m_settings.m_speechEngine = settings.value("speechEngine", defaultSettings.m_speechEngine).toString();
    m_settings.m_speechLocale = settings.value("speechLocale", defaultSettings.m_speechLocale).toString();
    m_settings.m_speechVoice = settings.value("speechVoice", defaultSettings.m_speechVoice).toString();
    m_settings.m_speechRate = settings.value("speechRate", defaultSettings.m_speechRate).toDouble();
    m_settings.m_speechPitch = settings.value("speechPitch", defaultSettings.m_speechPitch).toDouble();
    m_settings.m_speechVolume = settings.value("speechVolume", defaultSettings.m_speechVolume).toDouble();
    settings.endGroup();

    emit settingsChanged();
}

void PDFViewerSettings::writeSettings(QSettings& settings)
{
    settings.beginGroup("ViewerSettings");
    settings.setValue("defaultDirectory", m_settings.m_directory);
    settings.setValue("rendererFeatures", static_cast<int>(m_settings.m_features));
    settings.setValue("renderingEngine", static_cast<int>(m_settings.m_rendererEngine));
    settings.setValue("msaa", m_settings.m_multisampleAntialiasing);
    settings.setValue("rendererSamples", m_settings.m_rendererSamples);
    settings.setValue("prefetchPages", m_settings.m_prefetchPages);
    settings.setValue("preferredMeshResolutionRatio", m_settings.m_preferredMeshResolutionRatio);
    settings.setValue("minimalMeshResolutionRatio", m_settings.m_minimalMeshResolutionRatio);
    settings.setValue("colorTolerance", m_settings.m_colorTolerance);
    settings.setValue("compiledPageCacheLimit", m_settings.m_compiledPageCacheLimit);
    settings.setValue("thumbnailsCacheLimit", m_settings.m_thumbnailsCacheLimit);
    settings.setValue("fontCacheLimit", m_settings.m_fontCacheLimit);
    settings.setValue("instancedFontCacheLimit", m_settings.m_instancedFontCacheLimit);
    settings.setValue("allowLaunchApplications", m_settings.m_allowLaunchApplications);
    settings.setValue("allowLaunchURI", m_settings.m_allowLaunchURI);
    settings.setValue("mutlithreadingStrategy", static_cast<int>(m_settings.m_multithreadingStrategy));
    settings.endGroup();

    settings.beginGroup("ColorManagementSystemSettings");
    settings.setValue("system", int(m_colorManagementSystemSettings.system));
    settings.setValue("accuracy", int(m_colorManagementSystemSettings.accuracy));
    settings.setValue("intent", int(m_colorManagementSystemSettings.intent));
    settings.setValue("isBlackPointCompensationActive", m_colorManagementSystemSettings.isBlackPointCompensationActive);
    settings.setValue("isWhitePaperColorTransformed", m_colorManagementSystemSettings.isWhitePaperColorTransformed);
    settings.setValue("outputCS", m_colorManagementSystemSettings.outputCS);
    settings.setValue("deviceGray", m_colorManagementSystemSettings.deviceGray);
    settings.setValue("deviceRGB", m_colorManagementSystemSettings.deviceRGB);
    settings.setValue("deviceCMYK", m_colorManagementSystemSettings.deviceCMYK);
    settings.setValue("profileDirectory", m_colorManagementSystemSettings.profileDirectory);
    settings.endGroup();

    settings.beginGroup("SpeechSettings");
    settings.setValue("speechEngine", m_settings.m_speechEngine);
    settings.setValue("speechLocale", m_settings.m_speechLocale);
    settings.setValue("speechVoice", m_settings.m_speechVoice);
    settings.setValue("speechRate", m_settings.m_speechRate);
    settings.setValue("speechPitch", m_settings.m_speechPitch);
    settings.setValue("speechVolume", m_settings.m_speechVolume);
    settings.endGroup();
}

QString PDFViewerSettings::getDirectory() const
{
    return m_settings.m_directory;
}

void PDFViewerSettings::setDirectory(const QString& directory)
{
    if (m_settings.m_directory != directory)
    {
        m_settings.m_directory = directory;
        emit settingsChanged();
    }
}

pdf::PDFRenderer::Features PDFViewerSettings::getFeatures() const
{
    return m_settings.m_features;
}

void PDFViewerSettings::setFeatures(const pdf::PDFRenderer::Features& features)
{
    if (m_settings.m_features != features)
    {
        m_settings.m_features = features;
        emit settingsChanged();
    }
}

pdf::RendererEngine PDFViewerSettings::getRendererEngine() const
{
    return m_settings.m_rendererEngine;
}

void PDFViewerSettings::setRendererEngine(pdf::RendererEngine rendererEngine)
{
    if (m_settings.m_rendererEngine != rendererEngine)
    {
        m_settings.m_rendererEngine = rendererEngine;
        emit settingsChanged();
    }
}

int PDFViewerSettings::getRendererSamples() const
{
    return m_settings.m_rendererSamples;
}

void PDFViewerSettings::setRendererSamples(int rendererSamples)
{
    if (m_settings.m_rendererSamples != rendererSamples)
    {
        m_settings.m_rendererSamples = rendererSamples;
        emit settingsChanged();
    }
}

void PDFViewerSettings::setPreferredMeshResolutionRatio(pdf::PDFReal preferredMeshResolutionRatio)
{
    if (m_settings.m_preferredMeshResolutionRatio != preferredMeshResolutionRatio)
    {
        m_settings.m_preferredMeshResolutionRatio = preferredMeshResolutionRatio;
        emit settingsChanged();
    }
}

void PDFViewerSettings::setMinimalMeshResolutionRatio(pdf::PDFReal minimalMeshResolutionRatio)
{
    if (m_settings.m_minimalMeshResolutionRatio != minimalMeshResolutionRatio)
    {
        m_settings.m_minimalMeshResolutionRatio = minimalMeshResolutionRatio;
        emit settingsChanged();
    }
}

void PDFViewerSettings::setColorTolerance(pdf::PDFReal colorTolerance)
{
    if (m_settings.m_colorTolerance != colorTolerance)
    {
        m_settings.m_colorTolerance = colorTolerance;
        emit settingsChanged();
    }
}

PDFViewerSettings::Settings::Settings() :
    m_features(pdf::PDFRenderer::getDefaultFeatures()),
    m_rendererEngine(pdf::RendererEngine::OpenGL),
    m_multisampleAntialiasing(true),
    m_rendererSamples(16),
    m_prefetchPages(true),
    m_preferredMeshResolutionRatio(0.02),
    m_minimalMeshResolutionRatio(0.005),
    m_colorTolerance(0.01),
    m_allowLaunchApplications(true),
    m_allowLaunchURI(true),
    m_compiledPageCacheLimit(128 * 1024),
    m_thumbnailsCacheLimit(PIXMAP_CACHE_LIMIT),
    m_fontCacheLimit(pdf::DEFAULT_FONT_CACHE_LIMIT),
    m_instancedFontCacheLimit(pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT),
    m_multithreadingStrategy(pdf::PDFExecutionPolicy::Strategy::PageMultithreaded),
    m_speechRate(0.0),
    m_speechPitch(0.0),
    m_speechVolume(1.0)
{

}

}   // namespace pdfviewer
