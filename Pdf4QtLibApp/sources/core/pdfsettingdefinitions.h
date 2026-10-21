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
    static void registerAi(PDFSettingRegistry* registry);
    static void registerState(PDFSettingRegistry* registry);
    static void add(PDFSettingRegistry* registry, const PDFSettingDefinition& definition);
};

}   // namespace pdf

#endif // PDFSETTINGDEFINITIONS_H
