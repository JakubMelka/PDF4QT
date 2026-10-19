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

#include "pdfsettingdefinitions.h"
#include "pdfsettingdefinition.h"

#include <QLoggingCategory>

#include <limits>

namespace pdf
{

void PDFSettingDefinitions::registerBuiltinDefinitions(PDFSettingRegistry* registry)
{
    if (!registry)
    {
        return;
    }

    registerGeneral(registry);
    registerDocumentPanels(registry);
    registerAi(registry);
    registerState(registry);
}

void PDFSettingDefinitions::add(PDFSettingRegistry* registry, const PDFSettingDefinition& definition)
{
    if (!registry->registerDefinition(definition))
    {
        qWarning("Invalid or duplicate definition of the setting %s", qPrintable(definition.getKey()));
    }
}

void PDFSettingDefinitions::registerGeneral(PDFSettingRegistry* registry)
{
    // Settings › General (Figma pdf4qt-settings-general 146:2843). The full set of
    // the section (Language, sidebar) comes with the Settings page (step 5).
    const QString section = sectionGeneral();

    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::colorScheme(), PDFSettingValues::colorSchemeAuto(),
                                                     { { PDFSettingValues::colorSchemeLight(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Light")), false },
                                                       { PDFSettingValues::colorSchemeDark(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Dark")), false },
                                                       { PDFSettingValues::colorSchemeAuto(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Auto")), false } })
                      .setSection(section, QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Application")))
                      .setAnchor(QStringLiteral("color-scheme"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color scheme"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Light, dark, or follow the system"))
                      .setKeywords({ QStringLiteral("theme"), QStringLiteral("dark mode"), QStringLiteral("light mode"), QStringLiteral("appearance"), QStringLiteral("colors") }));

    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::recentMaxFiles(), 10, 1, 100)
                      .setSection(section, QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Documents")))
                      .setAnchor(QStringLiteral("recent-files"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Recent files"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Number of files kept in the history"))
                      .setKeywords({ QStringLiteral("history"), QStringLiteral("recent"), QStringLiteral("files") }));

    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::toolsSuggestNext(), true)
                      .setSection(section, QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Tools")))
                      .setAnchor(QStringLiteral("suggest-next-tools"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Suggest tools I often use next"))
                      .setKeywords({ QStringLiteral("then continue with"), QStringLiteral("suggestions"), QStringLiteral("tool history") }));

    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::updateAutoCheck(), true)
                      .setSection(section, QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Updates")))
                      .setAnchor(QStringLiteral("check-for-updates"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Check for updates automatically"))
                      .setKeywords({ QStringLiteral("update"), QStringLiteral("new version"), QStringLiteral("release") }));
}

void PDFSettingDefinitions::registerDocumentPanels(PDFSettingRegistry* registry)
{
    // Controls of the panels of a document, which are settings (newapp_storage.md 4).
    // They have no section of Settings yet; step 5 decides, where they are shown.
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::bookmarksAutoGenerate(), false)
                      .setAnchor(QStringLiteral("bookmarks-generate-automatically"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Generate automatically"))
                      .setKeywords({ QStringLiteral("bookmarks"), QStringLiteral("my bookmarks") }));
}

void PDFSettingDefinitions::registerAi(PDFSettingRegistry* registry)
{
    // Page AI (newapp_mcp.md 1): token required, this computer only, port 8765,
    // permissions Read = Allow, Edit = Ask, Files = Ask, not started with PDF4QT
    const std::vector<PDFSettingOption> permissions =
    {
        { PDFSettingValues::permissionAllow(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Allow")), false },
        { PDFSettingValues::permissionAsk(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Ask")), false },
        { PDFSettingValues::permissionDeny(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Deny")), false }
    };

    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::mcpPort(), 8765, 1024, 65535)
                      .setAnchor(QStringLiteral("port"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Port")));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::mcpAllowRemote(), false)
                      .setAnchor(QStringLiteral("allow-remote-connections"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Allow remote connections")));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::mcpRequireToken(), true)
                      .setAnchor(QStringLiteral("require-access-token"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Require access token")));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::mcpStartWithApp(), false)
                      .setAnchor(QStringLiteral("start-with-pdf4qt"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Start with PDF4QT")));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::mcpToken(), QString(), 256)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Access token")));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::mcpPermissionRead(), PDFSettingValues::permissionAllow(), permissions)
                      .setAnchor(QStringLiteral("read-documents"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Read documents")));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::mcpPermissionEdit(), PDFSettingValues::permissionAsk(), permissions)
                      .setAnchor(QStringLiteral("edit-documents"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Edit documents")));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::mcpPermissionFiles(), PDFSettingValues::permissionAsk(), permissions)
                      .setAnchor(QStringLiteral("open-and-save-files"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Open and save files")));
}

void PDFSettingDefinitions::registerState(PDFSettingRegistry* registry)
{
    // State of the application stored as settings: not shown in Settings, not
    // searched and not restored by Restore defaults
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::toolsHistoryRuns(), 0, 0, std::numeric_limits<qint64>::max()));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::updateLastCheck(), 0, 0, std::numeric_limits<qint64>::max()));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::updateLatestVersion(), QString(), 64));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::updateLastError(), QString(), 4096));
}

}   // namespace pdf
