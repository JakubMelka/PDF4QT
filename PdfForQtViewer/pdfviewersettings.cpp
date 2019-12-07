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

#include "pdfviewersettings.h"

namespace pdfviewer
{

void PDFViewerSettings::setSettings(const PDFViewerSettings::Settings& settings)
{
    m_settings = settings;
    emit settingsChanged();
}

void PDFViewerSettings::readSettings(QSettings& settings)
{
    Settings defaultSettings;

    settings.beginGroup("ViewerSettings");
    m_settings.m_directory = settings.value("defaultDirectory", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    m_settings.m_features = static_cast<pdf::PDFRenderer::Features>(settings.value("rendererFeatures", static_cast<int>(pdf::PDFRenderer::getDefaultFeatures())).toInt());
    m_settings.m_rendererEngine = static_cast<pdf::RendererEngine>(settings.value("renderingEngine", static_cast<int>(pdf::RendererEngine::OpenGL)).toInt());
    m_settings.m_multisampleAntialiasing = settings.value("msaa", defaultSettings.m_multisampleAntialiasing).toBool();
    m_settings.m_rendererSamples = settings.value("rendererSamples", defaultSettings.m_rendererSamples).toInt();
    m_settings.m_preferredMeshResolutionRatio = settings.value("preferredMeshResolutionRatio", defaultSettings.m_preferredMeshResolutionRatio).toDouble();
    m_settings.m_minimalMeshResolutionRatio = settings.value("minimalMeshResolutionRatio", defaultSettings.m_minimalMeshResolutionRatio).toDouble();
    m_settings.m_colorTolerance = settings.value("colorTolerance", defaultSettings.m_colorTolerance).toDouble();
    m_settings.m_allowLaunchApplications = settings.value("allowLaunchApplications", defaultSettings.m_allowLaunchApplications).toBool();
    m_settings.m_allowLaunchURI = settings.value("allowLaunchURI", defaultSettings.m_allowLaunchURI).toBool();
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
    settings.setValue("preferredMeshResolutionRatio", m_settings.m_preferredMeshResolutionRatio);
    settings.setValue("minimalMeshResolutionRatio", m_settings.m_minimalMeshResolutionRatio);
    settings.setValue("colorTolerance", m_settings.m_colorTolerance);
    settings.setValue("allowLaunchApplications", m_settings.m_allowLaunchApplications);
    settings.setValue("allowLaunchURI", m_settings.m_allowLaunchURI);
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

}   // namespace pdfviewer
