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

#ifndef PDFVIEWERSETTINGS_H
#define PDFVIEWERSETTINGS_H

#include "pdfviewerglobal.h"
#include "pdfglobal.h"
#include "pdfrenderer.h"
#include "pdfcms.h"
#include "pdfexecutionpolicy.h"
#include "pdfform.h"
#include "pdfapplicationtranslator.h"

#include <QObject>

class QSettings;

namespace pdfviewer
{

class PDF4QTLIBGUILIBSHARED_EXPORT PDFViewerSettings : public QObject
{
    Q_OBJECT

public:
    inline explicit PDFViewerSettings(QObject* parent) :
        QObject(parent)
    {

    }

    enum ColorScheme
    {
        AutoScheme,
        LightScheme,
        DarkScheme
    };

    enum SidebarButtonIconSize
    {
        SidebarButtonIconSizeSmall,
        SidebarButtonIconSizeMedium,
        SidebarButtonIconSizeLarge,
        SidebarButtonIconSizeVeryLarge
    };

    struct Settings
    {
        static constexpr int WHEEL_SCROLL_SPEED_PERCENT_MIN = 10;
        static constexpr int WHEEL_SCROLL_SPEED_PERCENT_MAX = 400;
        static constexpr int WHEEL_SCROLL_SPEED_PERCENT_DEFAULT = 100;

        Settings();

        bool operator==(const Settings&) const = default;
        bool operator!=(const Settings&) const = default;

        pdf::PDFRenderer::Features m_features;
        QString m_directory;
        pdf::RendererEngine m_rendererEngine;
        bool m_prefetchPages;
        pdf::PDFReal m_preferredMeshResolutionRatio;
        pdf::PDFReal m_minimalMeshResolutionRatio;
        pdf::PDFReal m_colorTolerance;
        bool m_allowLaunchApplications;
        bool m_allowLaunchURI;
        bool m_allowDeveloperMode;
        pdf::PDFExecutionPolicy::Strategy m_multithreadingStrategy;

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

        // Undo/redo steps settings
        int m_maximumUndoSteps;
        int m_maximumRedoSteps;

        // Form settings
        pdf::PDFFormManager::FormAppearanceFlags m_formAppearanceFlags;

        // Signature settings
        bool m_signatureVerificationEnabled;
        bool m_signatureTreatWarningsAsErrors;
        bool m_signatureIgnoreCertificateValidityTime;
        bool m_signatureUseSystemStore;

        // Bookmarks settings
        bool m_autoGenerateBookmarks;

        // UI Dark/Light mode settings
        ColorScheme m_colorScheme;
        SidebarButtonIconSize m_sidebarButtonIconSize;

        // UI scrolling settings
        bool m_smoothWheelScrolling;
        int m_wheelScrollHorizontalSpeedPercent;
        int m_wheelScrollVerticalSpeedPercent;

        // Language
        pdf::PDFApplicationTranslator::ELanguage m_language;
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

    bool isPagePrefetchingEnabled() const { return m_settings.m_prefetchPages; }

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

    ColorScheme getColorScheme() const;
    void setColorScheme(ColorScheme colorScheme);

    static ColorScheme getColorSchemeStatic();

signals:
    void settingsChanged();

private:
    Settings m_settings;
    pdf::PDFCMSSettings m_colorManagementSystemSettings;
};

}   // namespace pdfviewer

#endif // PDFVIEWERSETTINGS_H
