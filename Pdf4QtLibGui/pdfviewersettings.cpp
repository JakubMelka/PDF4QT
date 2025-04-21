//    Copyright (C) 2019-2025 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfviewersettings.h"
#include "pdfconstants.h"

#include <QSettings>
#include <QPixmapCache>
#include <QStandardPaths>

#include "pdfdbgheap.h"

namespace pdfviewer
{

void PDFViewerSettings::setSettings(const PDFViewerSettings::Settings& settings)
{
    if (m_settings != settings)
    {
        m_settings = settings;
        Q_EMIT settingsChanged();
    }
}

void PDFViewerSettings::readSettings(QSettings& settings, const pdf::PDFCMSSettings& defaultCMSSettings)
{
    Settings defaultSettings;

    settings.beginGroup("ViewerSettings");
    m_settings.m_directory = settings.value("defaultDirectory", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    m_settings.m_features = static_cast<pdf::PDFRenderer::Features>(settings.value("rendererFeaturesv2", static_cast<int>(pdf::PDFRenderer::getDefaultFeatures())).toInt());
    m_settings.m_rendererEngine = static_cast<pdf::RendererEngine>(settings.value("renderingEngine", static_cast<int>(pdf::RendererEngine::Blend2D_MultiThread)).toInt());
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
    m_settings.m_allowDeveloperMode = settings.value("allowDeveloperMode", defaultSettings.m_allowDeveloperMode).toBool();
    m_settings.m_multithreadingStrategy = static_cast<pdf::PDFExecutionPolicy::Strategy>(settings.value("multithreadingStrategy", static_cast<int>(defaultSettings.m_multithreadingStrategy)).toInt());
    m_settings.m_magnifierSize = settings.value("magnifierSize", defaultSettings.m_magnifierSize).toInt();
    m_settings.m_magnifierZoom = settings.value("magnifierZoom", defaultSettings.m_magnifierZoom).toDouble();
    m_settings.m_maximumUndoSteps = settings.value("maximumUndoSteps", defaultSettings.m_maximumUndoSteps).toInt();
    m_settings.m_maximumRedoSteps = settings.value("maximumRedoSteps", defaultSettings.m_maximumRedoSteps).toInt();
    settings.endGroup();

    settings.beginGroup("ColorManagementSystemSettings");
    m_colorManagementSystemSettings.system = static_cast<pdf::PDFCMSSettings::System>(settings.value("system", int(defaultCMSSettings.system)).toInt());
    m_colorManagementSystemSettings.accuracy = static_cast<pdf::PDFCMSSettings::Accuracy>(settings.value("accuracy", int(defaultCMSSettings.accuracy)).toInt());
    m_colorManagementSystemSettings.intent = static_cast<pdf::RenderingIntent>(settings.value("intent", int(defaultCMSSettings.intent)).toInt());
    m_colorManagementSystemSettings.colorAdaptationXYZ = static_cast<pdf::PDFCMSSettings::ColorAdaptationXYZ>(settings.value("colorAdaptationXYZ"), int(defaultCMSSettings.colorAdaptationXYZ));
    m_colorManagementSystemSettings.isBlackPointCompensationActive = settings.value("isBlackPointCompensationActive", defaultCMSSettings.isBlackPointCompensationActive).toBool();
    m_colorManagementSystemSettings.isWhitePaperColorTransformed = settings.value("isWhitePaperColorTransformed", defaultCMSSettings.isWhitePaperColorTransformed).toBool();
    m_colorManagementSystemSettings.isConsiderOutputIntent = settings.value("isConsiderOutputIntent", defaultCMSSettings.isConsiderOutputIntent).toBool();
    m_colorManagementSystemSettings.outputCS = settings.value("outputCS", defaultCMSSettings.outputCS).toString();
    m_colorManagementSystemSettings.deviceGray = settings.value("deviceGray", defaultCMSSettings.deviceGray).toString();
    m_colorManagementSystemSettings.deviceRGB = settings.value("deviceRGB", defaultCMSSettings.deviceRGB).toString();
    m_colorManagementSystemSettings.deviceCMYK = settings.value("deviceCMYK", defaultCMSSettings.deviceCMYK).toString();
    m_colorManagementSystemSettings.softProofingProfile = settings.value("softProofingProfile", defaultCMSSettings.softProofingProfile).toString();
    m_colorManagementSystemSettings.proofingIntent = static_cast<pdf::RenderingIntent>(settings.value("proofingIntent", int(defaultCMSSettings.proofingIntent)).toInt());
    m_colorManagementSystemSettings.outOfGamutColor = settings.value("outOfGamutColor", defaultCMSSettings.outOfGamutColor).value<QColor>();
    m_colorManagementSystemSettings.profileDirectory = settings.value("profileDirectory", defaultCMSSettings.profileDirectory).toString();
    m_colorManagementSystemSettings.foregroundColor = settings.value("foregroundColor", defaultCMSSettings.foregroundColor).value<QColor>();
    m_colorManagementSystemSettings.backgroundColor = settings.value("backgroundColor", defaultCMSSettings.backgroundColor).value<QColor>();
    m_colorManagementSystemSettings.sigmoidSlopeFactor = settings.value("sigmoidSlopeFactor", defaultCMSSettings.sigmoidSlopeFactor).toDouble();
    m_colorManagementSystemSettings.bitonalThreshold = settings.value("bitonalThreshold",defaultCMSSettings.bitonalThreshold).toInt();
    settings.endGroup();

    settings.beginGroup("SpeechSettings");
    m_settings.m_speechEngine = settings.value("speechEngine", defaultSettings.m_speechEngine).toString();
    m_settings.m_speechLocale = settings.value("speechLocale", defaultSettings.m_speechLocale).toString();
    m_settings.m_speechVoice = settings.value("speechVoice", defaultSettings.m_speechVoice).toString();
    m_settings.m_speechRate = settings.value("speechRate", defaultSettings.m_speechRate).toDouble();
    m_settings.m_speechPitch = settings.value("speechPitch", defaultSettings.m_speechPitch).toDouble();
    m_settings.m_speechVolume = settings.value("speechVolume", defaultSettings.m_speechVolume).toDouble();
    settings.endGroup();

    settings.beginGroup("Forms");
    m_settings.m_formAppearanceFlags = static_cast<pdf::PDFFormManager::FormAppearanceFlags>(settings.value("formAppearance", int(pdf::PDFFormManager::getDefaultApperanceFlags())).toInt());
    settings.endGroup();

    settings.beginGroup("Signature");
    m_settings.m_signatureVerificationEnabled = settings.value("signatureVerificationEnabled", defaultSettings.m_signatureVerificationEnabled).toBool();
    m_settings.m_signatureTreatWarningsAsErrors = settings.value("signatureTreatWarningsAsErrors", defaultSettings.m_signatureTreatWarningsAsErrors).toBool();
    m_settings.m_signatureIgnoreCertificateValidityTime = settings.value("signatureIgnoreCertificateValidityTime", defaultSettings.m_signatureIgnoreCertificateValidityTime).toBool();
    m_settings.m_signatureUseSystemStore = settings.value("signatureUseSystemStore", defaultSettings.m_signatureUseSystemStore).toBool();
    settings.endGroup();

    settings.beginGroup("Bookmarks");
    m_settings.m_autoGenerateBookmarks = settings.value("autoGenerateBookmarks", defaultSettings.m_autoGenerateBookmarks).toBool();
    settings.endGroup();

    settings.beginGroup("ColorScheme");
    m_settings.m_colorScheme = static_cast<ColorScheme>(settings.value("colorScheme", int(defaultSettings.m_colorScheme)).toInt());
    settings.endGroup();

    // Language
    m_settings.m_language = pdf::PDFApplicationTranslator::loadSettings(settings);

    Q_EMIT settingsChanged();
}

void PDFViewerSettings::writeSettings(QSettings& settings)
{
    settings.beginGroup("ViewerSettings");
    settings.setValue("defaultDirectory", m_settings.m_directory);
    settings.setValue("rendererFeaturesv2", static_cast<int>(m_settings.m_features));
    settings.setValue("renderingEngine", static_cast<int>(m_settings.m_rendererEngine));
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
    settings.setValue("allowDeveloperMode", m_settings.m_allowDeveloperMode);
    settings.setValue("multithreadingStrategy", static_cast<int>(m_settings.m_multithreadingStrategy));
    settings.setValue("magnifierSize", m_settings.m_magnifierSize);
    settings.setValue("magnifierZoom", m_settings.m_magnifierZoom);
    settings.setValue("maximumUndoSteps", m_settings.m_maximumUndoSteps);
    settings.setValue("maximumRedoSteps", m_settings.m_maximumRedoSteps);
    settings.endGroup();

    settings.beginGroup("ColorManagementSystemSettings");
    settings.setValue("system", int(m_colorManagementSystemSettings.system));
    settings.setValue("accuracy", int(m_colorManagementSystemSettings.accuracy));
    settings.setValue("intent", int(m_colorManagementSystemSettings.intent));
    settings.setValue("colorAdaptationXYZ", int(m_colorManagementSystemSettings.colorAdaptationXYZ));
    settings.setValue("isBlackPointCompensationActive", m_colorManagementSystemSettings.isBlackPointCompensationActive);
    settings.setValue("isWhitePaperColorTransformed", m_colorManagementSystemSettings.isWhitePaperColorTransformed);
    settings.setValue("isConsiderOutputIntent", m_colorManagementSystemSettings.isConsiderOutputIntent);
    settings.setValue("outputCS", m_colorManagementSystemSettings.outputCS);
    settings.setValue("deviceGray", m_colorManagementSystemSettings.deviceGray);
    settings.setValue("deviceRGB", m_colorManagementSystemSettings.deviceRGB);
    settings.setValue("deviceCMYK", m_colorManagementSystemSettings.deviceCMYK);
    settings.setValue("softProofingProfile", m_colorManagementSystemSettings.softProofingProfile);
    settings.setValue("proofingIntent", int(m_colorManagementSystemSettings.proofingIntent));
    settings.setValue("outOfGamutColor", m_colorManagementSystemSettings.outOfGamutColor);
    settings.setValue("profileDirectory", m_colorManagementSystemSettings.profileDirectory);
    settings.setValue("foregroundColor", m_colorManagementSystemSettings.foregroundColor);
    settings.setValue("backgroundColor", m_colorManagementSystemSettings.backgroundColor);
    settings.setValue("sigmoidSlopeFactor", m_colorManagementSystemSettings.sigmoidSlopeFactor);
    settings.setValue("bitonalThreshold", m_colorManagementSystemSettings.bitonalThreshold);
    settings.endGroup();

    settings.beginGroup("SpeechSettings");
    settings.setValue("speechEngine", m_settings.m_speechEngine);
    settings.setValue("speechLocale", m_settings.m_speechLocale);
    settings.setValue("speechVoice", m_settings.m_speechVoice);
    settings.setValue("speechRate", m_settings.m_speechRate);
    settings.setValue("speechPitch", m_settings.m_speechPitch);
    settings.setValue("speechVolume", m_settings.m_speechVolume);
    settings.endGroup();

    settings.beginGroup("Forms");
    settings.setValue("formAppearance", int(m_settings.m_formAppearanceFlags));
    settings.endGroup();

    settings.beginGroup("Signature");
    settings.setValue("signatureVerificationEnabled", m_settings.m_signatureVerificationEnabled);
    settings.setValue("signatureTreatWarningsAsErrors", m_settings.m_signatureTreatWarningsAsErrors);
    settings.setValue("signatureIgnoreCertificateValidityTime", m_settings.m_signatureIgnoreCertificateValidityTime);
    settings.setValue("signatureUseSystemStore", m_settings.m_signatureUseSystemStore);
    settings.endGroup();

    settings.beginGroup("Bookmarks");
    settings.setValue("autoGenerateBookmarks", m_settings.m_autoGenerateBookmarks);
    settings.endGroup();

    settings.beginGroup("ColorScheme");
    settings.setValue("colorScheme", int(m_settings.m_colorScheme));
    settings.endGroup();

    pdf::PDFApplicationTranslator::saveSettings(settings, m_settings.m_language);
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
        Q_EMIT settingsChanged();
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
        Q_EMIT settingsChanged();
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
        Q_EMIT settingsChanged();
    }
}

void PDFViewerSettings::setPreferredMeshResolutionRatio(pdf::PDFReal preferredMeshResolutionRatio)
{
    if (m_settings.m_preferredMeshResolutionRatio != preferredMeshResolutionRatio)
    {
        m_settings.m_preferredMeshResolutionRatio = preferredMeshResolutionRatio;
        Q_EMIT settingsChanged();
    }
}

void PDFViewerSettings::setMinimalMeshResolutionRatio(pdf::PDFReal minimalMeshResolutionRatio)
{
    if (m_settings.m_minimalMeshResolutionRatio != minimalMeshResolutionRatio)
    {
        m_settings.m_minimalMeshResolutionRatio = minimalMeshResolutionRatio;
        Q_EMIT settingsChanged();
    }
}

void PDFViewerSettings::setColorTolerance(pdf::PDFReal colorTolerance)
{
    if (m_settings.m_colorTolerance != colorTolerance)
    {
        m_settings.m_colorTolerance = colorTolerance;
        Q_EMIT settingsChanged();
    }
}

void PDFViewerSettings::setColorScheme(ColorScheme colorScheme)
{
    if (m_settings.m_colorScheme != colorScheme)
    {
        m_settings.m_colorScheme = colorScheme;
        Q_EMIT settingsChanged();
    }
}

PDFViewerSettings::ColorScheme PDFViewerSettings::getColorSchemeStatic()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("ColorScheme");
    const ColorScheme colorScheme = static_cast<ColorScheme>(settings.value("colorScheme", int(AutoScheme)).toInt());
    settings.endGroup();

    return colorScheme;
}

PDFViewerSettings::ColorScheme PDFViewerSettings::getColorScheme() const
{
    return m_settings.m_colorScheme;
}

PDFViewerSettings::Settings::Settings() :
    m_features(pdf::PDFRenderer::getDefaultFeatures()),
    m_rendererEngine(pdf::RendererEngine::Blend2D_MultiThread),
    m_prefetchPages(true),
    m_preferredMeshResolutionRatio(0.02),
    m_minimalMeshResolutionRatio(0.005),
    m_colorTolerance(0.01),
    m_allowLaunchApplications(true),
    m_allowLaunchURI(true),
    m_allowDeveloperMode(false),
    m_multithreadingStrategy(pdf::PDFExecutionPolicy::Strategy::AlwaysMultithreaded),
    m_compiledPageCacheLimit(512 * 1024),
    m_thumbnailsCacheLimit(64 * 1024),
    m_fontCacheLimit(pdf::DEFAULT_FONT_CACHE_LIMIT),
    m_instancedFontCacheLimit(pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT),
    m_speechRate(0.0),
    m_speechPitch(0.0),
    m_speechVolume(1.0),
    m_magnifierSize(100),
    m_magnifierZoom(2.0),
    m_maximumUndoSteps(5),
    m_maximumRedoSteps(5),
    m_formAppearanceFlags(pdf::PDFFormManager::getDefaultApperanceFlags()),
    m_signatureVerificationEnabled(true),
    m_signatureTreatWarningsAsErrors(false),
    m_signatureIgnoreCertificateValidityTime(false),
    m_signatureUseSystemStore(true),
    m_autoGenerateBookmarks(true),
    m_colorScheme(AutoScheme),
    m_language(pdf::PDFApplicationTranslator::E_LANGUAGE_AUTOMATIC_SELECTION)
{

}

}   // namespace pdfviewer
