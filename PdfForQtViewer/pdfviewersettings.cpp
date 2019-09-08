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
    settings.beginGroup("ViewerSettings");
    m_settings.m_directory = settings.value("defaultDirectory", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    m_settings.m_features = static_cast<pdf::PDFRenderer::Features>(settings.value("rendererFeatures", static_cast<int>(pdf::PDFRenderer::getDefaultFeatures())).toInt());
    m_settings.m_rendererEngine = static_cast<pdf::RendererEngine>(settings.value("renderingEngine", static_cast<int>(pdf::RendererEngine::OpenGL)).toInt());
    m_settings.m_multisampleAntialiasing = settings.value("msaa", true).toBool();
    m_settings.m_rendererSamples = settings.value("rendererSamples", 16).toInt();
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

}   // namespace pdfviewer
