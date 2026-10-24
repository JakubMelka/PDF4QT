// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#ifndef PDFSETTINGDEFINITIONS_H
#define PDFSETTINGDEFINITIONS_H

#include "pdfappglobal.h"

#include <QString>

namespace pdf
{

class PDFSettingRegistry;
class PDFSettingDefinition;

/// Keys of the settings used by the code (one place, no string literals elsewhere)
class PDF4QTLIBAPPSHARED_EXPORT PDFSettingKeys
{
public:
    PDFSettingKeys() = delete;

    // Settings › General › Application
    static QString colorScheme() { return QStringLiteral("appearance.colorScheme"); }

    // Settings › General › Documents
    static QString recentMaxFiles() { return QStringLiteral("recent.maxFiles"); }

    // Settings › General › Tools
    static QString toolsSuggestNext() { return QStringLiteral("tools.suggestNext"); }
    static QString toolsHistoryRuns() { return QStringLiteral("tools.historyRuns"); }

    // Settings › General › Updates (newapp_whatsnew.md 5)
    static QString updateAutoCheck() { return QStringLiteral("update.autoCheck"); }
    static QString updateLastCheck() { return QStringLiteral("update.lastCheck"); }
    static QString updateLatestVersion() { return QStringLiteral("update.latestVersion"); }
    static QString updateLastError() { return QStringLiteral("update.lastError"); }
    static QString updateLastSuccess() { return QStringLiteral("update.lastSuccess"); }

    // Panel My Bookmarks of the document
    static QString bookmarksAutoGenerate() { return QStringLiteral("bookmarks.autoGenerate"); }

    // Page AI (newapp_mcp.md 1)
    static QString mcpPort() { return QStringLiteral("mcp.port"); }
    static QString mcpAllowRemote() { return QStringLiteral("mcp.allowRemote"); }
    static QString mcpRequireToken() { return QStringLiteral("mcp.requireToken"); }
    static QString mcpStartWithApp() { return QStringLiteral("mcp.startWithApp"); }
    static QString mcpToken() { return QStringLiteral("mcp.token"); }
    static QString mcpPermissionRead() { return QStringLiteral("mcp.permissionRead"); }
    static QString mcpPermissionEdit() { return QStringLiteral("mcp.permissionEdit"); }
    static QString mcpPermissionFiles() { return QStringLiteral("mcp.permissionFiles"); }

    // Document tab (step 3; Settings page = step 5)

    // Settings › General
    static QString viewingShowSidebar() { return QStringLiteral("viewing.showSidebar"); }
    static QString viewingSidebarIconSize() { return QStringLiteral("viewing.sidebarIconSize"); }

    // Settings › Viewing
    static QString renderingAntialiasing() { return QStringLiteral("rendering.antialiasing"); }
    static QString renderingTextAntialiasing() { return QStringLiteral("rendering.textAntialiasing"); }
    static QString renderingSmoothImages() { return QStringLiteral("rendering.smoothImages"); }
    static QString viewingShowAnnotations() { return QStringLiteral("viewing.showAnnotations"); }
    static QString renderingColorAdjustAnnotations() { return QStringLiteral("rendering.colorAdjustAnnotations"); }
    static QString renderingClipToCropBox() { return QStringLiteral("rendering.clipToCropBox"); }
    static QString renderingIgnoreOptionalContent() { return QStringLiteral("rendering.ignoreOptionalContent"); }
    static QString renderingLogicalPixelZoom() { return QStringLiteral("rendering.logicalPixelZoom"); }
    static QString viewingSmoothWheelScrolling() { return QStringLiteral("viewing.smoothWheelScrolling"); }
    static QString viewingWheelSpeedHorizontal() { return QStringLiteral("viewing.wheelSpeedHorizontal"); }
    static QString viewingWheelSpeedVertical() { return QStringLiteral("viewing.wheelSpeedVertical"); }
    static QString magnifierSize() { return QStringLiteral("magnifier.size"); }
    static QString magnifierZoom() { return QStringLiteral("magnifier.zoom"); }
    static QString renderingRealText() { return QStringLiteral("rendering.realText"); }

    // Settings › Color modes
    static QString viewingColorMode() { return QStringLiteral("viewing.colorMode"); }
    static QString colorModesCustomPreset() { return QStringLiteral("colorModes.customPreset"); }
    static QString colorModesTextColor() { return QStringLiteral("colorModes.textColor"); }
    static QString colorModesPageColor() { return QStringLiteral("colorModes.pageColor"); }
    static QString colorModesHighContrastStrength() { return QStringLiteral("colorModes.highContrastStrength"); }
    static QString colorModesBitonalThreshold() { return QStringLiteral("colorModes.bitonalThreshold"); }

    // Settings › Editing
    static QString editingUndoLimit() { return QStringLiteral("editing.undoLimit"); }
    static QString editingRedoLimit() { return QStringLiteral("editing.redoLimit"); }
    static QString formsHighlightFields() { return QStringLiteral("forms.highlightFields"); }
    static QString formsHighlightRequiredFields() { return QStringLiteral("forms.highlightRequiredFields"); }
    static QString formsAllowEditReadOnly() { return QStringLiteral("forms.allowEditReadOnly"); }
    static QString annotationsAuthorMode() { return QStringLiteral("annotations.authorMode"); }
    static QString annotationsCustomAuthorName() { return QStringLiteral("annotations.customAuthorName"); }
    static QString annotationsShowMarkupBar() { return QStringLiteral("annotations.showMarkupBar"); }

    // Settings › Security
    static QString securityOpenLinks() { return QStringLiteral("security.openLinks"); }
    static QString securityLaunchFiles() { return QStringLiteral("security.launchFiles"); }
    static QString signaturesVerify() { return QStringLiteral("signatures.verify"); }
    static QString signaturesStrict() { return QStringLiteral("signatures.strict"); }
    static QString signaturesIgnoreExpiration() { return QStringLiteral("signatures.ignoreExpiration"); }
    static QString signaturesUseSystemStore() { return QStringLiteral("signatures.useSystemStore"); }

    // Settings › Speech
    static QString speechEngine() { return QStringLiteral("speech.engine"); }
    static QString speechLocale() { return QStringLiteral("speech.locale"); }
    static QString speechVoice() { return QStringLiteral("speech.voice"); }
    static QString speechRate() { return QStringLiteral("speech.rate"); }
    static QString speechPitch() { return QStringLiteral("speech.pitch"); }
    static QString speechVolume() { return QStringLiteral("speech.volume"); }

    // Settings › Color management
    static QString cmsSystem() { return QStringLiteral("cms.system"); }
    static QString cmsRenderingIntent() { return QStringLiteral("cms.renderingIntent"); }
    static QString cmsAccuracy() { return QStringLiteral("cms.accuracy"); }
    static QString cmsColorAdaptation() { return QStringLiteral("cms.colorAdaptation"); }
    static QString cmsBlackPointCompensation() { return QStringLiteral("cms.blackPointCompensation"); }
    static QString cmsWhitePaperTransformed() { return QStringLiteral("cms.whitePaperTransformed"); }
    static QString cmsConsiderOutputIntent() { return QStringLiteral("cms.considerOutputIntent"); }
    static QString cmsOutputProfile() { return QStringLiteral("cms.outputProfile"); }
    static QString cmsDeviceGrayProfile() { return QStringLiteral("cms.deviceGrayProfile"); }
    static QString cmsDeviceRgbProfile() { return QStringLiteral("cms.deviceRgbProfile"); }
    static QString cmsDeviceCmykProfile() { return QStringLiteral("cms.deviceCmykProfile"); }
    static QString cmsProfileDirectory() { return QStringLiteral("cms.profileDirectory"); }

    // Settings › Advanced
    static QString renderingEngine() { return QStringLiteral("rendering.engine"); }
    static QString renderingMultithreading() { return QStringLiteral("rendering.multithreading"); }
    static QString renderingPrefetchPages() { return QStringLiteral("rendering.prefetchPages"); }
    static QString cacheCompiledPages() { return QStringLiteral("cache.compiledPagesKB"); }
    static QString cacheThumbnails() { return QStringLiteral("cache.thumbnailsKB"); }
    static QString cacheFontLimit() { return QStringLiteral("cache.fontLimit"); }
    static QString cacheInstancedFontLimit() { return QStringLiteral("cache.instancedFontLimit"); }
    static QString renderingMeshPreferredRatio() { return QStringLiteral("rendering.meshPreferredRatio"); }
    static QString renderingMeshMinimalRatio() { return QStringLiteral("rendering.meshMinimalRatio"); }
    static QString renderingColorTolerance() { return QStringLiteral("rendering.colorTolerance"); }
    static QString advancedDeveloperMode() { return QStringLiteral("advanced.developerMode"); }
    static QString advancedShowRenderTimes() { return QStringLiteral("advanced.showRenderTimes"); }
    static QString advancedShowTextBlocks() { return QStringLiteral("advanced.showTextBlocks"); }
    static QString advancedShowTextLines() { return QStringLiteral("advanced.showTextLines"); }

    // Defaults of a new document (no row in Settings)
    static QString viewingPageLayout() { return QStringLiteral("viewing.pageLayout"); }
    static QString viewingZoom() { return QStringLiteral("viewing.zoom"); }
    static QString viewingFirstPageRight() { return QStringLiteral("viewing.firstPageRight"); }
    static QString documentAutoRefresh() { return QStringLiteral("document.autoRefresh"); }
};

/// Values of the options of the settings
class PDF4QTLIBAPPSHARED_EXPORT PDFSettingValues
{
public:
    PDFSettingValues() = delete;

    // appearance.colorScheme
    static QString colorSchemeLight() { return QStringLiteral("light"); }
    static QString colorSchemeDark() { return QStringLiteral("dark"); }
    static QString colorSchemeAuto() { return QStringLiteral("auto"); }

    // annotations.authorMode (step 4, owner question Q1: Anonymous by default)
    static QString authorModeAnonymous() { return QStringLiteral("anonymous"); }
    static QString authorModeSystemUserName() { return QStringLiteral("systemUserName"); }
    static QString authorModeCustomName() { return QStringLiteral("customName"); }

    // mcp.permission*
    static QString permissionAllow() { return QStringLiteral("allow"); }
    static QString permissionAsk() { return QStringLiteral("ask"); }
    static QString permissionDeny() { return QStringLiteral("deny"); }
};

/// Built-in definitions of the settings. One function per section of Settings
/// (and per area, which is not a section): adding a setting = one statement in
/// the function of its section.
class PDF4QTLIBAPPSHARED_EXPORT PDFSettingDefinitions
{
public:
    PDFSettingDefinitions() = delete;

    /// Registers all built-in definitions
    static void registerBuiltinDefinitions(PDFSettingRegistry* registry);

    /// Section ids of Settings (routes /settings/{section}, newapp_navigation.md 3)
    static QString sectionGeneral() { return QStringLiteral("general"); }
    static QString sectionViewing() { return QStringLiteral("viewing"); }
    static QString sectionColorModes() { return QStringLiteral("color-modes"); }
    static QString sectionEditing() { return QStringLiteral("editing"); }
    static QString sectionSecurity() { return QStringLiteral("security"); }
    static QString sectionSpeech() { return QStringLiteral("speech"); }
    static QString sectionShortcuts() { return QStringLiteral("shortcuts"); }
    static QString sectionColorManagement() { return QStringLiteral("color-management"); }
    static QString sectionAdvanced() { return QStringLiteral("advanced"); }

private:
    static void registerGeneral(PDFSettingRegistry* registry);
    static void registerDocumentPanels(PDFSettingRegistry* registry);
    static void registerDocument(PDFSettingRegistry* registry);
    static void registerAi(PDFSettingRegistry* registry);
    static void registerState(PDFSettingRegistry* registry);
    static void add(PDFSettingRegistry* registry, const PDFSettingDefinition& definition);
};

}   // namespace pdf

#endif // PDFSETTINGDEFINITIONS_H
