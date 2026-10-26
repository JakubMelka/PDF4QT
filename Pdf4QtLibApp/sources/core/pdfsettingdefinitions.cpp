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
    registerDocument(registry);
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

    // Language of the user interface (step 5, owner decision O12: English by
    // default; the options of 944:8916 - the native name first, the English one
    // after it). The tag "Restart required" of Figma is not set: the language
    // switches at once (owner question Q2).
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::appearanceLanguage(), PDFSettingValues::languageEnglish(),
                                                     { { PDFSettingValues::languageAuto(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Automatic")), false },
                                                       { PDFSettingValues::languageEnglish(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "English")), false },
                                                       { QStringLiteral("de"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Deutsch – German")), false },
                                                       { QStringLiteral("ko"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "한국어 – Korean")), false },
                                                       { QStringLiteral("es"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Español – Spanish")), false },
                                                       { QStringLiteral("cs"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Čeština – Czech")), false },
                                                       { QStringLiteral("zh_CN"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "简体中文 – Chinese (Simplified)")), false },
                                                       { QStringLiteral("zh_TW"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "繁體中文 – Chinese (Traditional)")), false },
                                                       { QStringLiteral("fr"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Français – French")), false },
                                                       { QStringLiteral("tr"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Türkçe – Turkish")), false },
                                                       { QStringLiteral("ru"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Русский – Russian")), false } })
                      .setSection(section, QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Application")))
                      .setAnchor(QStringLiteral("language"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Language"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Language of the user interface"))
                      .setKeywords({ QStringLiteral("language"), QStringLiteral("translation"), QStringLiteral("locale"), QStringLiteral("localization") })
                      // The switch rebuilds the whole user interface: only a chosen
                      // language is applied, not every arrow key (owner 2026-10-25)
                      .setCommitOnActivation());

    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::colorScheme(), PDFSettingValues::colorSchemeAuto(),
                                                     { { PDFSettingValues::colorSchemeAuto(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Automatic")), false },
                                                       { PDFSettingValues::colorSchemeLight(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Light")), false },
                                                       { PDFSettingValues::colorSchemeDark(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Dark")), false } })
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
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "“Then continue with” in the result dialog offers the tools you usually run after this one. When off, only the default suggestions are shown and nothing is counted. Only tool identifiers and counts are kept on this computer, never file names."))
                      .setKeywords({ QStringLiteral("then continue with"), QStringLiteral("suggestions"), QStringLiteral("tool history") }));

    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::updateAutoCheck(), true)
                      .setSection(section, QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Updates")))
                      .setAnchor(QStringLiteral("check-for-updates"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Check for updates automatically"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Once a day PDF4QT asks GitHub whether a newer version is available and shows the answer on What’s New. Nothing is downloaded or installed. When off, use Check for updates on What’s New."))
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

void PDFSettingDefinitions::registerDocument(PDFSettingRegistry* registry)
{
    // Settings read by the document tab (step 3). Names, groups, defaults and ranges
    // come from the Figma Settings frames 146:2843 … 147:4565 and from the old
    // applications; the Settings page shows them in step 5. Settings without a
    // section are the defaults of a newly opened document (View and File panels).

    // Settings › General
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::viewingShowSidebar(), true)
                      .setSection(sectionGeneral(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Documents")))
                      .setAnchor(QStringLiteral("show-sidebar-when-opening-documents"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Show sidebar when opening documents"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Open the document sidebar automatically"))
                      .setKeywords({ QStringLiteral("sidebar"), QStringLiteral("panel"), QStringLiteral("thumbnails") }));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::viewingSidebarIconSize(), QStringLiteral("medium"),
                                                   { { QStringLiteral("small"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Small")), false },
                                                     { QStringLiteral("medium"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Medium")), false },
                                                     { QStringLiteral("large"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Large")), false },
                                                     { QStringLiteral("veryLarge"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Very large")), false } })
                      .setSection(sectionGeneral(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Documents")))
                      .setAnchor(QStringLiteral("sidebar-icon-size"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Sidebar icon size"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Size of the buttons in the document sidebar"))
                      .setKeywords({ QStringLiteral("sidebar"), QStringLiteral("icons") }));

    // Settings › Viewing
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingAntialiasing(), true)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page display")))
                      .setAnchor(QStringLiteral("antialiasing"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Antialiasing"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Smooth edges of vector graphics"))
                      .setKeywords({ QStringLiteral("smooth"), QStringLiteral("rendering"), QStringLiteral("quality") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingTextAntialiasing(), true)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page display")))
                      .setAnchor(QStringLiteral("text-antialiasing"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Text antialiasing"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Smooth edges of text"))
                      .setKeywords({ QStringLiteral("smooth"), QStringLiteral("rendering"), QStringLiteral("font") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingSmoothImages(), true)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page display")))
                      .setAnchor(QStringLiteral("smooth-pictures"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Smooth pictures"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Interpolate images when they are scaled"))
                      .setKeywords({ QStringLiteral("images"), QStringLiteral("interpolation"), QStringLiteral("rendering") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::viewingShowAnnotations(), true)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page display")))
                      .setAnchor(QStringLiteral("display-annotations"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Display annotations"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Show comments, highlights and form widgets"))
                      .setKeywords({ QStringLiteral("comments"), QStringLiteral("markup"), QStringLiteral("forms") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingColorAdjustAnnotations(), false)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page display")))
                      .setAnchor(QStringLiteral("adjust-colors-of-annotations"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Adjust colors of annotations"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Apply the color mode to annotations too"))
                      .setKeywords({ QStringLiteral("color mode"), QStringLiteral("annotations") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingClipToCropBox(), true)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page display")))
                      .setAnchor(QStringLiteral("clip-to-crop-box"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Clip to crop box"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Hide content outside the crop box"))
                      .setKeywords({ QStringLiteral("crop"), QStringLiteral("page boxes") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingIgnoreOptionalContent(), false)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page display")))
                      .setAnchor(QStringLiteral("ignore-optional-content"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Ignore optional content"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Show all layers regardless of their state"))
                      .setKeywords({ QStringLiteral("layers"), QStringLiteral("optional content") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingLogicalPixelZoom(), true)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page display")))
                      .setAnchor(QStringLiteral("use-logical-pixels-when-zooming"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Use logical pixels when zooming"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "100 % follows the display scaling"))
                      .setKeywords({ QStringLiteral("zoom"), QStringLiteral("scaling"), QStringLiteral("dpi") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::viewingSmoothWheelScrolling(), true)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Scrolling")))
                      .setAnchor(QStringLiteral("smooth-mouse-wheel-scrolling"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Smooth mouse wheel scrolling"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Animate scrolling with the wheel"))
                      .setKeywords({ QStringLiteral("scroll"), QStringLiteral("wheel"), QStringLiteral("mouse") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::viewingWheelSpeedHorizontal(), 100, 10, 400)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Scrolling")))
                      .setAnchor(QStringLiteral("horizontal-wheel-speed"))
                      .setUnit(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "%"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Horizontal wheel speed"))
                      .setKeywords({ QStringLiteral("scroll"), QStringLiteral("wheel"), QStringLiteral("mouse") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::viewingWheelSpeedVertical(), 100, 10, 400)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Scrolling")))
                      .setAnchor(QStringLiteral("vertical-wheel-speed"))
                      .setUnit(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "%"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Vertical wheel speed"))
                      .setKeywords({ QStringLiteral("scroll"), QStringLiteral("wheel"), QStringLiteral("mouse") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::magnifierSize(), 100, 50, 400)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Magnifier")))
                      .setAnchor(QStringLiteral("magnifier-size"))
                      .setUnit(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "px"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Magnifier size"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Diameter of the magnifier"))
                      .setKeywords({ QStringLiteral("loupe"), QStringLiteral("zoom") }));
    add(registry, PDFSettingDefinition::createReal(PDFSettingKeys::magnifierZoom(), 2.0, 1.0, 10.0)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Magnifier")))
                      .setAnchor(QStringLiteral("magnifier-zoom"))
                      .setUnit(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "×")).setDecimals(1)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Magnifier zoom"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Magnification factor"))
                      .setKeywords({ QStringLiteral("loupe"), QStringLiteral("zoom") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingRealText(), false)
                      .setSection(sectionViewing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Printing and export")))
                      .setAnchor(QStringLiteral("draw-text-as-real-text"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Draw text as real text"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Keeps text selectable in printed and exported files"))
                      .setKeywords({ QStringLiteral("print"), QStringLiteral("export"), QStringLiteral("text") }));

    // Settings › Color modes
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::viewingColorMode(), QStringLiteral("normal"),
                                                   { { QStringLiteral("normal"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Normal")), false },
                                                     { QStringLiteral("inverted"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Inverted")), false },
                                                     { QStringLiteral("grayscale"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Grayscale")), false },
                                                     { QStringLiteral("highContrast"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "High contrast")), false },
                                                     { QStringLiteral("bitonal"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Bitonal")), false },
                                                     { QStringLiteral("customColors"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Custom colors")), false } })
                      .setSection(sectionColorModes(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color modes")))
                      .setAnchor(QStringLiteral("color-mode"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color mode"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "How a page looks in each mode"))
                      .setKeywords({ QStringLiteral("page colors"), QStringLiteral("inverted"), QStringLiteral("night"), QStringLiteral("sepia") }));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::colorModesCustomPreset(), QStringLiteral("amber"),
                                                   { { QStringLiteral("paper"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Paper")), false },
                                                     { QStringLiteral("sepia"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Sepia")), false },
                                                     { QStringLiteral("softGray"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Soft gray")), false },
                                                     { QStringLiteral("mint"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Mint")), false },
                                                     { QStringLiteral("night"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Night")), false },
                                                     { QStringLiteral("black"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Black")), false },
                                                     { QStringLiteral("terminal"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Terminal")), false },
                                                     { QStringLiteral("amber"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Amber")), false },
                                                     { QStringLiteral("highContrast"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "High contrast")), false },
                                                     { QStringLiteral("custom"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Custom")), false } })
                      .setSection(sectionColorModes(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Custom colors")))
                      .setAnchor(QStringLiteral("preset"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Preset"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page and text color of the Custom colors mode"))
                      .setKeywords({ QStringLiteral("custom colors"), QStringLiteral("preset") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::colorModesTextColor(), QStringLiteral("#FFB000"), 16)
                      .setSection(sectionColorModes(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Custom colors")))
                      .setAnchor(QStringLiteral("text-color"))
                      .setControl(PDFSettingDefinition::Control::Color)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Text color"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Choosing a color switches the preset to Custom"))
                      .setKeywords({ QStringLiteral("custom colors"), QStringLiteral("foreground") }));
    // The default pair is the preset Amber (Reading theme card 160:4427)
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::colorModesPageColor(), QStringLiteral("#0B0B10"), 16)
                      .setSection(sectionColorModes(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Custom colors")))
                      .setAnchor(QStringLiteral("page-color"))
                      .setControl(PDFSettingDefinition::Control::Color)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page color"))
                      .setKeywords({ QStringLiteral("custom colors"), QStringLiteral("background"), QStringLiteral("paper") }));
    add(registry, PDFSettingDefinition::createReal(PDFSettingKeys::colorModesHighContrastStrength(), 10.0, 1.0, 100.0)
                      .setSection(sectionColorModes(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Mode adjustments")))
                      .setAnchor(QStringLiteral("high-contrast-strength"))
                      .setControl(PDFSettingDefinition::Control::Slider).setDecimals(0)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "High contrast strength"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Used by the High contrast mode"))
                      .setKeywords({ QStringLiteral("high contrast"), QStringLiteral("sigmoid") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::colorModesBitonalThreshold(), 128, 0, 255)
                      .setSection(sectionColorModes(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Mode adjustments")))
                      .setAnchor(QStringLiteral("black-and-white-threshold"))
                      .setControl(PDFSettingDefinition::Control::Slider)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Black and white threshold"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Used by the Bitonal mode"))
                      .setKeywords({ QStringLiteral("bitonal"), QStringLiteral("threshold") }));

    // Settings › Editing
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::editingUndoLimit(), 100, 0, 1000)
                      .setSection(sectionEditing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Undo history")))
                      .setAnchor(QStringLiteral("maximum-undo-steps"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Maximum undo steps"))
                      .setKeywords({ QStringLiteral("undo"), QStringLiteral("history") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::editingRedoLimit(), 100, 0, 1000)
                      .setSection(sectionEditing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Undo history")))
                      .setAnchor(QStringLiteral("maximum-redo-steps"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Maximum redo steps"))
                      .setKeywords({ QStringLiteral("redo"), QStringLiteral("history") }));

    // Settings › Editing, author of the annotations (step 4; Anonymous by default -
    // owner question Q1, the name is stored in the document); the group Author
    // identity is above Forms (146:3196)
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::annotationsAuthorMode(), PDFSettingValues::authorModeAnonymous(),
                                                   { { PDFSettingValues::authorModeAnonymous(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Anonymous (do not disclose the user name)")), false },
                                                     { PDFSettingValues::authorModeSystemUserName(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "System user name")), false },
                                                     { PDFSettingValues::authorModeCustomName(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Custom name")), false } })
                      .setSection(sectionEditing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Author identity")))
                      .setAnchor(QStringLiteral("author-of-new-annotations"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Author of new annotations"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Name written into comments and markups"))
                      .setKeywords({ QStringLiteral("author"), QStringLiteral("name"), QStringLiteral("annotations"), QStringLiteral("privacy") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::annotationsCustomAuthorName(), QString(), 256)
                      .setSection(sectionEditing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Author identity")))
                      .setAnchor(QStringLiteral("custom-author-name"))
                      .setEnabledWhen(PDFSettingKeys::annotationsAuthorMode(), PDFSettingValues::authorModeCustomName())
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Custom author name"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Used when “Custom name” is selected"))
                      .setKeywords({ QStringLiteral("author"), QStringLiteral("name"), QStringLiteral("annotations") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::formsHighlightFields(), true)
                      .setSection(sectionEditing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Forms")))
                      .setAnchor(QStringLiteral("highlight-form-fields"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Highlight form fields"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Tint all fillable fields"))
                      .setKeywords({ QStringLiteral("forms"), QStringLiteral("fields"), QStringLiteral("acroform") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::formsHighlightRequiredFields(), true)
                      .setSection(sectionEditing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Forms")))
                      .setAnchor(QStringLiteral("highlight-required-form-fields"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Highlight required form fields"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Mark the fields that must be filled"))
                      .setKeywords({ QStringLiteral("forms"), QStringLiteral("required") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::formsAllowEditReadOnly(), false)
                      .setSection(sectionEditing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Forms")))
                      .setAnchor(QStringLiteral("allow-editing-of-read-only-fields"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Allow editing of read-only fields"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Ignore the read-only flag of form fields"))
                      .setKeywords({ QStringLiteral("forms"), QStringLiteral("read only") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::annotationsShowMarkupBar(), true)
                      .setSection(sectionEditing(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Annotations")))
                      .setAnchor(QStringLiteral("show-markup-bar-when-text-is-selected"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Show markup bar when text is selected"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Highlight, underline, strike out or comment the selected text from a small bar above it"))
                      .setKeywords({ QStringLiteral("markup"), QStringLiteral("highlight"), QStringLiteral("selection"), QStringLiteral("bar") }));

    // Settings › Security
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::securityOpenLinks(), true)
                      .setSection(sectionSecurity(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Links and actions")))
                      .setAnchor(QStringLiteral("allow-uri-link-launch"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Allow URI link launch"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Let documents open web links"))
                      .setKeywords({ QStringLiteral("links"), QStringLiteral("web"), QStringLiteral("url"), QStringLiteral("browser") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::securityLaunchFiles(), false)
                      .setSection(sectionSecurity(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Links and actions")))
                      .setAnchor(QStringLiteral("allow-application-launch"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Allow application launch"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Let documents start external programs"))
                      .setKeywords({ QStringLiteral("launch"), QStringLiteral("programs"), QStringLiteral("run") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::signaturesVerify(), true)
                      .setSection(sectionSecurity(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Signature verification")))
                      .setAnchor(QStringLiteral("signature-verification"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Signature verification"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Verify digital signatures when a document opens"))
                      .setKeywords({ QStringLiteral("signatures"), QStringLiteral("certificates") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::signaturesStrict(), false)
                      .setSection(sectionSecurity(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Signature verification")))
                      .setAnchor(QStringLiteral("strict-mode"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Strict mode"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Treat warnings as errors"))
                      .setKeywords({ QStringLiteral("signatures"), QStringLiteral("strict") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::signaturesIgnoreExpiration(), false)
                      .setSection(sectionSecurity(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Signature verification")))
                      .setAnchor(QStringLiteral("ignore-expired-certificates"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Ignore expired certificates"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Do not check the validity period"))
                      .setKeywords({ QStringLiteral("signatures"), QStringLiteral("expired"), QStringLiteral("certificates") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::signaturesUseSystemStore(), true)
                      .setSection(sectionSecurity(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Signature verification")))
                      .setAnchor(QStringLiteral("use-system-certificate-store"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Use system certificate store"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Trust the certificates of the operating system"))
                      .setKeywords({ QStringLiteral("signatures"), QStringLiteral("trusted"), QStringLiteral("certificates") }));

    // Settings › Speech
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::speechEngine(), QStringLiteral(""), 256)
                      .setSection(sectionSpeech(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Voice")))
                      .setAnchor(QStringLiteral("voice-synthesizer"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Voice synthesizer"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Speech engine provided by the system"))
                      .setKeywords({ QStringLiteral("speech"), QStringLiteral("read aloud"), QStringLiteral("engine") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::speechLocale(), QStringLiteral(""), 64)
                      .setSection(sectionSpeech(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Voice")))
                      .setAnchor(QStringLiteral("locale"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Locale"))
                      .setKeywords({ QStringLiteral("speech"), QStringLiteral("language") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::speechVoice(), QStringLiteral(""), 256)
                      .setSection(sectionSpeech(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Voice")))
                      .setAnchor(QStringLiteral("voice"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Voice"))
                      .setKeywords({ QStringLiteral("speech"), QStringLiteral("voice") }));
    add(registry, PDFSettingDefinition::createReal(PDFSettingKeys::speechRate(), 0.0, -1.0, 1.0)
                      .setSection(sectionSpeech(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Sound")))
                      .setAnchor(QStringLiteral("rate"))
                      .setControl(PDFSettingDefinition::Control::Slider).setDecimals(1)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Rate"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Slower to faster"))
                      .setKeywords({ QStringLiteral("speech"), QStringLiteral("speed") }));
    add(registry, PDFSettingDefinition::createReal(PDFSettingKeys::speechPitch(), 0.0, -1.0, 1.0)
                      .setSection(sectionSpeech(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Sound")))
                      .setAnchor(QStringLiteral("pitch"))
                      .setControl(PDFSettingDefinition::Control::Slider).setDecimals(1)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Pitch"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Lower to higher"))
                      .setKeywords({ QStringLiteral("speech") }));
    add(registry, PDFSettingDefinition::createReal(PDFSettingKeys::speechVolume(), 1.0, 0.0, 1.0)
                      .setSection(sectionSpeech(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Sound")))
                      .setAnchor(QStringLiteral("volume"))
                      .setControl(PDFSettingDefinition::Control::Slider).setDecimals(2)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Volume"))
                      .setKeywords({ QStringLiteral("speech"), QStringLiteral("loudness") }));

    // Settings › Color management
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"),
                                                   { { QStringLiteral("generic"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Generic")), false },
                                                     { QStringLiteral("lcms2"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Little CMS 2")), false } })
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color management")))
                      .setAnchor(QStringLiteral("color-management-system"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color management system"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Engine that converts colors"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("icc"), QStringLiteral("little cms") }));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::cmsRenderingIntent(), QStringLiteral("auto"),
                                                   { { QStringLiteral("auto"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Automatic")), false },
                                                     { QStringLiteral("perceptual"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Perceptual")), false },
                                                     { QStringLiteral("relativeColorimetric"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Relative colorimetric")), false },
                                                     { QStringLiteral("absoluteColorimetric"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Absolute colorimetric")), false },
                                                     { QStringLiteral("saturation"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Saturation")), false } })
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color management")))
                      .setAnchor(QStringLiteral("rendering-intent"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Rendering intent"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("intent") }));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::cmsAccuracy(), QStringLiteral("medium"),
                                                   { { QStringLiteral("low"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Low")), false },
                                                     { QStringLiteral("medium"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Medium")), false },
                                                     { QStringLiteral("high"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "High")), false } })
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color management")))
                      .setAnchor(QStringLiteral("accuracy"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Accuracy"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("precision") }));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::cmsColorAdaptation(), QStringLiteral("bradford"),
                                                   { { QStringLiteral("none"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "None")), false },
                                                     { QStringLiteral("xyzScaling"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "XYZ scaling")), false },
                                                     { QStringLiteral("cat97"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "CAT97 matrix")), false },
                                                     { QStringLiteral("cat02"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "CAT02 matrix")), false },
                                                     { QStringLiteral("bradford"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Bradford method")), false } })
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color management")))
                      .setAnchor(QStringLiteral("color-adaptation-method"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color adaptation method"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "XYZ white point"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("white point") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::cmsBlackPointCompensation(), true)
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color management")))
                      .setAnchor(QStringLiteral("black-point-compensation"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Black point compensation"))
                      .setKeywords({ QStringLiteral("cms") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::cmsWhitePaperTransformed(), false)
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color management")))
                      .setAnchor(QStringLiteral("white-paper-color-transformed"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "White paper color transformed"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("paper") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::cmsConsiderOutputIntent(), true)
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color management")))
                      .setAnchor(QStringLiteral("consider-document-output-intents"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Consider document output intents"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("output intent") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::cmsOutputProfile(), QStringLiteral(""), 1024)
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color profiles")))
                      .setAnchor(QStringLiteral("output-color-profile"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Output color profile"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("icc"), QStringLiteral("profile") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::cmsDeviceGrayProfile(), QStringLiteral(""), 1024)
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color profiles")))
                      .setAnchor(QStringLiteral("device-gray-color-profile"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Device gray color profile"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("icc"), QStringLiteral("gray") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::cmsDeviceRgbProfile(), QStringLiteral(""), 1024)
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color profiles")))
                      .setAnchor(QStringLiteral("device-rgb-color-profile"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Device RGB color profile"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("icc"), QStringLiteral("rgb") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::cmsDeviceCmykProfile(), QStringLiteral(""), 1024)
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color profiles")))
                      .setAnchor(QStringLiteral("device-cmyk-color-profile"))
                      .setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Device CMYK color profile"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("icc"), QStringLiteral("cmyk") }));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::cmsProfileDirectory(), QStringLiteral(""), 4096)
                      .setSection(sectionColorManagement(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color profiles")))
                      .setAnchor(QStringLiteral("additional-profile-directory"))
                      .setControl(PDFSettingDefinition::Control::Path).setEnabledWhen(PDFSettingKeys::cmsSystem(), QStringLiteral("lcms2"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Additional profile directory"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Folder with your own ICC profiles"))
                      .setKeywords({ QStringLiteral("cms"), QStringLiteral("icc"), QStringLiteral("folder") }));

    // Settings › Advanced
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::renderingEngine(), QStringLiteral("blend2dMultiThread"),
                                                   { { QStringLiteral("qpainter"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "QPainter")), false },
                                                     { QStringLiteral("blend2dMultiThread"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Blend2D – multithreaded")), false },
                                                     { QStringLiteral("blend2dSingleThread"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Blend2D – single thread")), false } })
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Rendering engine")))
                      .setAnchor(QStringLiteral("rendering-engine"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Rendering engine"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Software renderer used to draw pages"))
                      .setKeywords({ QStringLiteral("renderer"), QStringLiteral("blend2d") }));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::renderingMultithreading(), QStringLiteral("loadBalanced"),
                                                   { { QStringLiteral("singleThread"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Single thread")), false },
                                                     { QStringLiteral("loadBalanced"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Multithreading (load balanced)")), false },
                                                     { QStringLiteral("maximumThreads"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Multithreading (maximum threads)")), false } })
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Rendering engine")))
                      .setAnchor(QStringLiteral("multithreading-strategy"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Multithreading strategy"))
                      .setKeywords({ QStringLiteral("threads"), QStringLiteral("performance") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::renderingPrefetchPages(), true)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Rendering engine")))
                      .setAnchor(QStringLiteral("prefetch-pages"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Prefetch pages"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Prepare neighboring pages in advance"))
                      .setKeywords({ QStringLiteral("performance"), QStringLiteral("cache") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::cacheCompiledPages(), 524288, 65536, 16777216)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Cache")))
                      .setAnchor(QStringLiteral("compiled-page-cache-size"))
                      .setUnit(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "kB"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Compiled page cache size"))
                      .setKeywords({ QStringLiteral("memory"), QStringLiteral("cache") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::cacheThumbnails(), 65536, 1024, 1048576)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Cache")))
                      .setAnchor(QStringLiteral("thumbnail-image-cache-size"))
                      .setUnit(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "kB"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Thumbnail image cache size"))
                      .setKeywords({ QStringLiteral("memory"), QStringLiteral("cache"), QStringLiteral("thumbnails") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::cacheFontLimit(), 32, 8, 1024)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Cache")))
                      .setAnchor(QStringLiteral("cached-font-limit"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Cached font limit"))
                      .setKeywords({ QStringLiteral("memory"), QStringLiteral("cache"), QStringLiteral("fonts") }));
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::cacheInstancedFontLimit(), 128, 16, 4096)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Cache")))
                      .setAnchor(QStringLiteral("cached-instanced-font-limit"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Cached instanced font limit"))
                      .setKeywords({ QStringLiteral("memory"), QStringLiteral("cache"), QStringLiteral("fonts") }));
    add(registry, PDFSettingDefinition::createReal(PDFSettingKeys::renderingMeshPreferredRatio(), 0.02, 0.0001, 1.0)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Shading")))
                      .setAnchor(QStringLiteral("preferred-mesh-resolution-ratio"))
                      .setDecimals(3)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Preferred mesh resolution ratio"))
                      .setKeywords({ QStringLiteral("shading"), QStringLiteral("mesh") }));
    add(registry, PDFSettingDefinition::createReal(PDFSettingKeys::renderingMeshMinimalRatio(), 0.005, 0.0001, 1.0)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Shading")))
                      .setAnchor(QStringLiteral("minimal-mesh-resolution-ratio"))
                      .setDecimals(3)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Minimal mesh resolution ratio"))
                      .setKeywords({ QStringLiteral("shading"), QStringLiteral("mesh") }));
    add(registry, PDFSettingDefinition::createReal(PDFSettingKeys::renderingColorTolerance(), 0.01, 0.0, 1.0)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Shading")))
                      .setAnchor(QStringLiteral("color-tolerance"))
                      .setDecimals(3)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Color tolerance"))
                      .setKeywords({ QStringLiteral("shading"), QStringLiteral("mesh") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::advancedDeveloperMode(), false)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Developer")))
                      .setAnchor(QStringLiteral("developer-mode"))
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Developer mode"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Enable the diagnostic overlays of the pages below"))
                      .setKeywords({ QStringLiteral("developer"), QStringLiteral("debug"), QStringLiteral("diagnostics") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::advancedShowRenderTimes(), false)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Developer")))
                      .setAnchor(QStringLiteral("display-page-compile-draw-time"))
                      .setEnabledWhen(PDFSettingKeys::advancedDeveloperMode(), true)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Display page compile/draw time"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Overlay timing on each page"))
                      .setKeywords({ QStringLiteral("developer"), QStringLiteral("performance"), QStringLiteral("timing") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::advancedShowTextBlocks(), false)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Developer")))
                      .setAnchor(QStringLiteral("display-text-blocks"))
                      .setEnabledWhen(PDFSettingKeys::advancedDeveloperMode(), true)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Display text blocks"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Frame the text blocks found by the text layout"))
                      .setKeywords({ QStringLiteral("developer"), QStringLiteral("text layout") }));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::advancedShowTextLines(), false)
                      .setSection(sectionAdvanced(), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Developer")))
                      .setAnchor(QStringLiteral("display-text-lines"))
                      .setEnabledWhen(PDFSettingKeys::advancedDeveloperMode(), true)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Display text lines"))
                      .setDescription(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Frame the text lines found by the text layout"))
                      .setKeywords({ QStringLiteral("developer"), QStringLiteral("text layout") }));

    // Defaults of a new document
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::viewingPageLayout(), QStringLiteral("continuous"),
                                                   { { QStringLiteral("singlePage"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Single page")), false },
                                                     { QStringLiteral("continuous"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Continuous")), false },
                                                     { QStringLiteral("twoPages"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Two pages")), false },
                                                     { QStringLiteral("twoColumns"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Two columns")), false } })
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Page layout")));
    add(registry, PDFSettingDefinition::createOption(PDFSettingKeys::viewingZoom(), QStringLiteral("fitWidth"),
                                                   { { QStringLiteral("fitPage"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Fit page")), false },
                                                     { QStringLiteral("fitWidth"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Fit width")), false },
                                                     { QStringLiteral("actualSize"), QStringLiteral(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Actual size")), false } })
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Zoom")));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::viewingFirstPageRight(), false)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "First page on the right side")));
    add(registry, PDFSettingDefinition::createBool(PDFSettingKeys::documentAutoRefresh(), true)
                      .setTitle(QT_TRANSLATE_NOOP("pdf::PDFSettingDefinitions", "Automatic refresh")));
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
    add(registry, PDFSettingDefinition::createInteger(PDFSettingKeys::updateLastSuccess(), 0, 0, std::numeric_limits<qint64>::max()));
    add(registry, PDFSettingDefinition::createString(PDFSettingKeys::storageBackupFolder(), QString(), 32767));
}

}   // namespace pdf
