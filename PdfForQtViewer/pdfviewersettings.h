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
            m_rendererSamples(16)
        {

        }

        pdf::PDFRenderer::Features m_features;
        QString m_directory;
        pdf::RendererEngine m_rendererEngine;
        bool m_multisampleAntialiasing;
        int m_rendererSamples;
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

    bool isMultisampleAntialiasingEnabled() const { return m_settings.m_multisampleAntialiasing; }

signals:
    void settingsChanged();

private:
    Settings m_settings;
};


}   // namespace pdfviewer

#endif // PDFVIEWERSETTINGS_H
