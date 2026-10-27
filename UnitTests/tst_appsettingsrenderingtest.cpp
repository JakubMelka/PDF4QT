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

// Settings › Viewing, Color modes, Color management and Advanced (package B of
// step 5): every row applied to an open document, the cards of the color modes
// and of the presets, the preview and the contrast, the lists of the color
// profiles read in a worker with a hostile folder, the engine and the caches of
// two documents, hostile stored values, another instance, Restore defaults,
// search and anchors, deletion, themes and scales, right-to-left, the minimal
// window, keyboard and accessibility, language change.
// PDF4QT_APP_SCREENSHOTS=<folder> writes pictures of the sections.

#include "appsettingsfixture.h"

#include "pdfappcolormodepresets.h"
#include "pdfappcolorpicker.h"
#include "pdfappcolormodepreview.h"
#include "pdfappcolorprofiles.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfappmainwindow.h"
#include "pdfapprestoredefaultsdialog.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfappsettingspage.h"
#include "pdfappsettingssection.h"
#include "pdfappsettingssection_colormanagement.h"
#include "pdfappsettingssection_colormodes.h"
#include "pdfapptab.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"
#include "pdfdatabasenotifier.h"
#include "pdfdatabasestatement.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcms.h"
#include "pdfcolorpickerdialog.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpanels.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfexecutionpolicy.h"
#include "pdfrenderer.h"
#include "pdfwidgettool.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentcards.h"
#include "pdfcomponentpanels.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSlider>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtTest>

#include <cmath>
#include <thread>

/// Helpers of the test of the rendering sections
class RenderingTestHelper
{
public:
    RenderingTestHelper() = delete;

    /// Stores a raw value (a hostile row of another version) without validation
    static bool storeRaw(pdf::PDFAppContext* context, const QString& key, const QVariant& value)
    {
        pdf::PDFDatabaseConnection* connection = context->getDatabase()->getConnection();
        pdf::PDFDatabaseStatement statement(connection, "INSERT OR REPLACE INTO setting(key, value, modified) VALUES (?1, ?2, 0)");
        statement.bindText(1, key);
        statement.bindVariant(2, value);
        return statement.execute();
    }

    /// Picture of the widget at the device pixel ratio
    static QImage render(QWidget* widget, qreal devicePixelRatio)
    {
        QImage image(QSize(qCeil(widget->width() * devicePixelRatio), qCeil(widget->height() * devicePixelRatio)), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(devicePixelRatio);
        image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
        widget->render(&image, QPoint(), QRegion(), QWidget::RenderFlags(QWidget::DrawChildren));
        return image;
    }

    /// Number of the pixels of the leak color (a color of a palette)
    static int countLeakPixels(const QImage& image)
    {
        const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
        int count = 0;
        const QImage converted = image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < converted.height(); ++y)
        {
            const QRgb* line = reinterpret_cast<const QRgb*>(converted.constScanLine(y));
            for (int x = 0; x < converted.width(); ++x)
            {
                count += (line[x] & 0xFFFFFF) == leak ? 1 : 0;
            }
        }
        return count;
    }

    static QScrollArea* scrollArea(QWidget* page)
    {
        for (QWidget* widget = page; widget; widget = widget->parentWidget())
        {
            if (QScrollArea* area = qobject_cast<QScrollArea*>(widget))
            {
                return area;
            }
        }
        return nullptr;
    }

    static pdf::PDFAppColorModesSettingsSection* colorModes(const pdftest::AppSettingsFixture& fixture)
    {
        pdf::PDFAppSettingsPage* page = fixture.settingsPage();
        return page ? qobject_cast<pdf::PDFAppColorModesSettingsSection*>(page->getSection()) : nullptr;
    }

    static pdf::PDFAppColorManagementSettingsSection* colorManagement(const pdftest::AppSettingsFixture& fixture)
    {
        pdf::PDFAppSettingsPage* page = fixture.settingsPage();
        return page ? qobject_cast<pdf::PDFAppColorManagementSettingsSection*>(page->getSection()) : nullptr;
    }

    /// Waits for the lists of the profiles (at least \p count readings)
    static bool waitForScan(pdf::PDFAppColorManagementSettingsSection* section, int count = 1)
    {
        QElapsedTimer timer;
        timer.start();
        while (section && (section->isScanning() || section->getScanCount() < count) && timer.elapsed() < 30000 * pdftest::AppDocumentFixture::getTimeoutFactor())
        {
            QTest::qWait(10);
        }
        return section && !section->isScanning() && section->getScanCount() >= count;
    }

    static void click(QWidget* widget)
    {
        QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, widget->rect().center());
    }

    static pdf::PDFComboBox* comboBox(const pdftest::AppSettingsFixture& fixture, const QString& anchor)
    {
        pdf::PDFAppSettingRow* row = fixture.findRow(anchor);
        return row && row->getControl() ? row->getControl()->getComboBox() : nullptr;
    }

    /// Writes an ICC profile of Qt (sRGB with the description) into the file
    static bool writeProfile(const QString& filePath, const QString& description)
    {
        QColorSpace colorSpace(QColorSpace::SRgb);
        colorSpace.setDescription(description);
        QFile file(filePath);
        return file.open(QFile::WriteOnly) && file.write(colorSpace.iccProfile()) > 0;
    }

    static bool writeFile(const QString& filePath, const QByteArray& data)
    {
        QFile file(filePath);
        return file.open(QFile::WriteOnly) && file.write(data) == data.size();
    }
};

/// Translator of the language test: texts of the section Color modes
class RenderingTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray source(sourceText ? sourceText : "");
        const QByteArray contextText(context ? context : "");
        if (contextText == "pdf::PDFAppColorModePresets" && source == "Sepia")
        {
            return QStringLiteral("Sépia");
        }
        if (contextText == "pdf::PDFAppColorModesSettingsSection" && source == "Contrast")
        {
            return QStringLiteral("Kontrast");
        }
        if (contextText == "pdf::PDFAppColorModePresets" && source == "Excellent")
        {
            return QStringLiteral("Vynikající");
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

class AppSettingsRenderingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void sectionsSmoke();
    void viewingRowsApplyToView();
    void colorModeCardsSameAsViewPanel();
    void presetWritesColorsOnce();
    void customColorSwitchesPreset();
    void contrastVerdicts();
    void colorManagementGenericDisables();
    void profileListsAndDirectory();
    void profileDirectoryHostile();
    void advancedEngineAndCaches();
    void developerRowsEnabledByMode();
    void twoDocumentsOneApply();
    void cmsChangeCost();
    void hostileStoredValues();
    void otherInstanceChange();
    void restoreDefaults();
    void searchAndAnchors();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void languageChange();
    void openLists();
    void previewGeometry();
    void reviewBrowseUntrustedFolder();
    void moreColorsDialog();
    void screenshots();
};

void AppSettingsRenderingTest::initTestCase()
{
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppSettingsRenderingTest::cleanup()
{
    pdf::PDFAppFileDialogs::setFolderHandler(nullptr);
    pdf::PDFAppColorProfiles::clearCache();
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppSettingsRenderingTest::sectionsSmoke()
{
    // Every section of the package opens with its rows of the definitions
    pdftest::AppSettingsFixture fixture;
    for (const QString& sectionId : { QStringLiteral("viewing"), QStringLiteral("color-modes"), QStringLiteral("color-management"), QStringLiteral("advanced") })
    {
        pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
        QVERIFY2(page, qPrintable(sectionId));
        QCOMPARE(page->getSectionId(), sectionId);
        QVERIFY(page->getSection());
        for (const pdf::PDFSettingDefinition* definition : fixture.context->getSettingRegistry()->getSectionDefinitions(sectionId))
        {
            QVERIFY2(fixture.findRow(definition->getAnchor()), qPrintable(definition->getKey()));
        }
    }

    // The default pair of the colors is the preset Amber (160:4427)
    const pdf::PDFAppColorModePreset* amber = pdf::PDFAppColorModePresets::findPreset(QStringLiteral("amber"));
    QVERIFY(amber);
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("amber"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesPageColor()), pdf::PDFAppColorModePresets::formatColor(amber->pageColor));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()), pdf::PDFAppColorModePresets::formatColor(amber->textColor));

    // The presets draw the tokens, whose values are the stored colors
    for (const pdf::PDFAppColorModePreset& preset : pdf::PDFAppColorModePresets::getPresets())
    {
        if (!preset.isCustom())
        {
            const pdf::PDFThemeColors& colors = pdf::PDFThemeManager::getInstance()->getTheme();
            QCOMPARE(colors.getColor(preset.pageToken).rgb(), preset.pageColor.rgb());
            QCOMPARE(colors.getColor(preset.textToken).rgb(), preset.textColor.rgb());
        }
    }
    QCOMPARE(pdf::PDFAppColorModePresets::getPresets().size(), size_t(10));
    QVERIFY(pdf::PDFAppColorModePresets::getPresets().back().isCustom());
}

void AppSettingsRenderingTest::viewingRowsApplyToView()
{
    using Feature = pdf::PDFRenderer::Feature;

    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(fixture.openSettings(QStringLiteral("viewing")));

    // Each check box of Page display switches its feature of the renderer
    const std::pair<QString, Feature> features[] =
    {
        { QStringLiteral("antialiasing"), pdf::PDFRenderer::Antialiasing },
        { QStringLiteral("text-antialiasing"), pdf::PDFRenderer::TextAntialiasing },
        { QStringLiteral("smooth-pictures"), pdf::PDFRenderer::SmoothImages },
        { QStringLiteral("display-annotations"), pdf::PDFRenderer::DisplayAnnotations },
        { QStringLiteral("adjust-colors-of-annotations"), pdf::PDFRenderer::ColorAdjust_Annotations },
        { QStringLiteral("clip-to-crop-box"), pdf::PDFRenderer::ClipToCropBox },
        { QStringLiteral("ignore-optional-content"), pdf::PDFRenderer::IgnoreOptionalContent },
        { QStringLiteral("use-logical-pixels-when-zooming"), pdf::PDFRenderer::LogicalSizeZooming }
    };
    for (const auto& [anchor, feature] : features)
    {
        pdf::PDFAppSettingRow* row = fixture.findRow(anchor);
        QVERIFY2(row, qPrintable(anchor));
        const bool value = !row->getControl()->getShownValue().toBool();
        QVERIFY(pdftest::AppSettingsFixture::setControlValue(row, value));
        QTRY_COMPARE(view->getProxy()->getFeatures().testFlag(feature), value);
    }

    // Program default (owner Q5): the content outside of the crop box is hidden
    QVERIFY(fixture.context->getSettingRegistry()->findDefinition(pdf::PDFSettingKeys::renderingClipToCropBox())->getDefaultValue().toBool());

    // The magnifier: the setting is the diameter, the tool takes the radius
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("magnifier-size")), qint64(240)));
    QTRY_COMPARE(view->getToolManager()->getMagnifierTool()->getMagnifierSize(), pdf::PDFAppDocumentView::getMagnifierRadius(240));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("magnifier-zoom")), 3.5));
    QTRY_COMPARE(view->getToolManager()->getMagnifierTool()->getMagnifierZoom(), 3.5);

    // Scrolling and printing are stored (the wheel is applied by the view, the text by the print)
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("smooth-mouse-wheel-scrolling")), false));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("vertical-wheel-speed")), qint64(150)));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("draw-text-as-real-text")), true));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::viewingSmoothWheelScrolling()), false);
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::viewingWheelSpeedVertical()), qint64(150));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::renderingRealText()), true);
    QTRY_VERIFY(!view->isApplySettingsQueued());

    // The wheel speed reaches the canvas (review RT5 - it was checked only in the
    // database): a wheel step of 40 pixels moves the pages by 80 pixels at 200 %
    // and by 40 pixels at 100 % (smooth scrolling is off)
    fixture.window->activateTab(tab);
    fixture.settle();
    QWidget* canvas = view->getPdfWidget()->getDrawWidget()->getWidget();
    QVERIFY(canvas && canvas->isVisible());
    const auto wheelDistance = [&fixture, view, canvas]()
    {
        const int top = view->getProxy()->getPagesIntersectingRectBoundingBox(canvas->rect()).top();
        const QPointF center = QRectF(canvas->rect()).center();
        QWheelEvent event(center, canvas->mapToGlobal(center), QPoint(0, -40), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &event);
        fixture.settle();
        return top - view->getProxy()->getPagesIntersectingRectBoundingBox(canvas->rect()).top();
    };
    // The value is applied, when the notification of the change has reached the
    // view (from the event loop, the polling of the notifier) and the queued
    // application of the batch has run - a fixed number of settle() is not enough
    // on a loaded machine
    const auto setSpeed = [&fixture, view](qint64 percent)
    {
        QSignalSpy spy(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
        if (fixture.settings().setValue(pdf::PDFSettingKeys::viewingWheelSpeedVertical(), percent).isError())
        {
            return false;
        }
        const bool isNotified = QTest::qWaitFor([&spy]()
        {
            for (const QList<QVariant>& arguments : spy)
            {
                if (arguments.value(1).toString() == pdf::PDFSettingKeys::viewingWheelSpeedVertical())
                {
                    return true;
                }
            }
            return false;
        }, 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
        fixture.settle();
        return isNotified && QTest::qWaitFor([view]() { return !view->isApplySettingsQueued(); }, 10000 * pdftest::AppDocumentFixture::getTimeoutFactor());
    };
    QVERIFY(setSpeed(200));
    QCOMPARE(wheelDistance(), 80);
    QVERIFY(setSpeed(100));
    QCOMPARE(wheelDistance(), 40);
}

void AppSettingsRenderingTest::colorModeCardsSameAsViewPanel()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);

    // Six cards in the order of 160:4351, Normal selected (program default)
    QCOMPARE(pdf::PDFAppColorModesSettingsSection::getModeIds().size(), 6);
    for (const QString& mode : pdf::PDFAppColorModesSettingsSection::getModeIds())
    {
        QVERIFY2(section->getModeCard(mode), qPrintable(mode));
        QCOMPARE(section->getModeCard(mode)->isSelected(), mode == QStringLiteral("normal"));
    }
    QCOMPARE(section->getModeCard(QStringLiteral("highContrast"))->getName(), QStringLiteral("High contrast"));
    QCOMPARE(section->getModeCard(QStringLiteral("customColors"))->getPreview(), pdf::PDFPreviewPainter::Preview::ModeCustomColors);

    // A card of Settings = the command of the View panel of the document
    RenderingTestHelper::click(section->getModeCard(QStringLiteral("inverted")));
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("inverted"));
    QTRY_COMPARE(view->getColorMode(), QStringLiteral("inverted"));
    QVERIFY(view->isCommandChecked(pdf::PDFAppCommands::colorModeInverted()));
    QVERIFY(view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::ColorAdjust_Invert));

    // The command of the View panel selects the card of Settings
    view->requestCommand(pdf::PDFAppCommands::colorModeCustom());
    fixture.settle();
    QTRY_VERIFY(section->getModeCard(QStringLiteral("customColors"))->isSelected());
    QVERIFY(!section->getModeCard(QStringLiteral("inverted"))->isSelected());
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("customColors"));
}

void AppSettingsRenderingTest::presetWritesColorsOnce()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    // Another mode is shown: choosing a Reading theme switches it to Custom colors
    // (owner 2026-10-25)
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::viewingColorMode(), QStringLiteral("grayscale")).isError());
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    QTRY_VERIFY(!view->isApplySettingsQueued());
    QTRY_COMPARE(view->getColorMode(), QStringLiteral("grayscale"));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);
    QVERIFY(section->getPresetCard(QStringLiteral("amber"))->isSelected());
    QVERIFY(section->getModeCard(QStringLiteral("grayscale"))->isSelected());

    // The mode, the preset and its two colors in one transaction: the color
    // management of the document changes once (one compilation of the pages)
    QSignalSpy cmsSpy(view->getCMSManager(), &pdf::PDFCMSManager::colorManagementSystemChanged);
    QSignalSpy changeSpy(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
    RenderingTestHelper::click(section->getPresetCard(QStringLiteral("sepia")));
    QVERIFY(section->hasPendingWrite());
    fixture.settle();
    QVERIFY(!section->hasPendingWrite());
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("customColors"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("sepia"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()), QStringLiteral("#5B4636"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesPageColor()), QStringLiteral("#F4ECD8"));
    QCOMPARE(changeSpy.size(), 4);
    QTRY_VERIFY(!view->isApplySettingsQueued());
    QTest::qWait(50);
    QCOMPARE(cmsSpy.size(), 1);
    QCOMPARE(view->getColorMode(), QStringLiteral("customColors"));
    QVERIFY(section->getModeCard(QStringLiteral("customColors"))->isSelected());
    QVERIFY(!section->getModeCard(QStringLiteral("grayscale"))->isSelected());
    QCOMPARE(view->getCMSManager()->getSettings().foregroundColor, QColor(QStringLiteral("#5B4636")));
    QCOMPARE(view->getCMSManager()->getSettings().backgroundColor, QColor(QStringLiteral("#F4ECD8")));

    // The cards, the preview, the custom card and the names of the colors follow
    QVERIFY(section->getPresetCard(QStringLiteral("sepia"))->isSelected());
    QVERIFY(!section->getPresetCard(QStringLiteral("amber"))->isSelected());
    QCOMPARE(section->getPreview()->getPageColor(), QColor(QStringLiteral("#F4ECD8")));
    QCOMPARE(section->getPreview()->getTextColor(), QColor(QStringLiteral("#5B4636")));
    QCOMPARE(section->getPresetCard(QStringLiteral("custom"))->getCustomPageColor(), QColor(QStringLiteral("#F4ECD8")));
    QCOMPARE(fixture.findRow(QStringLiteral("text-color"))->getControl()->getColorButton()->getShownName(), QStringLiteral("Sepia brown"));
    QCOMPARE(fixture.findRow(QStringLiteral("page-color"))->getControl()->getColorButton()->getShownName(), QStringLiteral("Sepia"));

    // Two clicks before the event loop: one batch with the last preset
    cmsSpy.clear();
    RenderingTestHelper::click(section->getPresetCard(QStringLiteral("night")));
    RenderingTestHelper::click(section->getPresetCard(QStringLiteral("terminal")));
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("terminal"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()), QStringLiteral("#00FF7F"));
    QTRY_VERIFY(!view->isApplySettingsQueued());
    QTest::qWait(50);
    QCOMPARE(cmsSpy.size(), 1);
}

void AppSettingsRenderingTest::customColorSwitchesPreset()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);

    // The names of the colors of the default preset (160:4441 / 160:4446)
    pdf::PDFColorButton* textButton = fixture.findRow(QStringLiteral("text-color"))->getControl()->getColorButton();
    pdf::PDFColorButton* pageButton = fixture.findRow(QStringLiteral("page-color"))->getControl()->getColorButton();
    QCOMPARE(textButton->getShownName(), QStringLiteral("Amber"));
    QCOMPARE(pageButton->getShownName(), QStringLiteral("Near black"));

    // A color picked by the user switches the preset to Custom…
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("text-color")), QStringLiteral("#123456")));
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()), QStringLiteral("#123456"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("custom"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesPageColor()), QStringLiteral("#0B0B10"));
    QVERIFY(section->getPresetCard(QStringLiteral("custom"))->isSelected());
    QCOMPARE(section->getPresetCard(QStringLiteral("custom"))->getCustomTextColor(), QColor(QStringLiteral("#123456")));
    QCOMPARE(section->getPreview()->getTextColor(), QColor(QStringLiteral("#123456")));

    // Custom… stays when a color changes again; a preset gives its colors back
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("page-color")), QStringLiteral("#FFFFFF")));
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("custom"));
    RenderingTestHelper::click(section->getPresetCard(QStringLiteral("paper")));
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesPageColor()), QStringLiteral("#FFFFFF"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()), QStringLiteral("#111111"));

    // A color of the preset picked again keeps the preset
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("page-color")), QStringLiteral("#FFFFFF")));
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("paper"));

    // Custom… keeps the colors of the user
    RenderingTestHelper::click(section->getPresetCard(QStringLiteral("custom")));
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("custom"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()), QStringLiteral("#111111"));

    // A chosen Reading theme (also Custom…) shows it: the mode is Custom colors
    // (owner 2026-10-25); a color picked by the user does not change the mode
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("customColors"));
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::viewingColorMode(), QStringLiteral("normal")).isError());
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("page-color")), QStringLiteral("#FEFEFE")));
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("normal"));
}

void AppSettingsRenderingTest::contrastVerdicts()
{
    using P = pdf::PDFAppColorModePresets;
    using V = P::Verdict;

    // WCAG ratios and the limits 7, 4.5 and 3 (the ratio as it is shown)
    QCOMPARE(P::formatRatio(P::getContrastRatio(QColor(QStringLiteral("#FFB000")), QColor(QStringLiteral("#0B0B10")))), QStringLiteral("10.7 : 1"));
    QCOMPARE(P::getContrastRatio(Qt::black, Qt::white), 21.0);
    QCOMPARE(P::getContrastRatio(Qt::white, Qt::white), 1.0);
    QCOMPARE(P::getContrastRatio(QColor(), Qt::white), 1.0);
    QCOMPARE(P::getVerdict(21.0), V::Excellent);
    QCOMPARE(P::getVerdict(7.0), V::Excellent);
    QCOMPARE(P::getVerdict(6.96), V::Excellent);
    QCOMPARE(P::getVerdict(6.94), V::Good);
    QCOMPARE(P::getVerdict(4.5), V::Good);
    QCOMPARE(P::getVerdict(4.44), V::Low);
    QCOMPARE(P::getVerdict(3.0), V::Low);
    QCOMPARE(P::getVerdict(2.9), V::Poor);
    QCOMPARE(P::getVerdict(1.0), V::Poor);
    QCOMPARE(P::getVerdict(P::getContrastRatio(QColor(QStringLiteral("#767676")), Qt::white)), V::Good);
    QCOMPARE(P::getVerdict(P::getContrastRatio(QColor(QStringLiteral("#949494")), Qt::white)), V::Low);
    QCOMPARE(P::getVerdict(P::getContrastRatio(QColor(QStringLiteral("#AAAAAA")), Qt::white)), V::Poor);
    QCOMPARE(P::getVerdictText(V::Excellent), QStringLiteral("Excellent"));
    QCOMPARE(P::getVerdictText(V::Poor), QStringLiteral("Poor"));
    QVERIFY(!P::parseColor(QStringLiteral("#12345")).isValid());
    QVERIFY(!P::parseColor(QStringLiteral("red")).isValid());
    QCOMPARE(P::findPresetId(QColor(QStringLiteral("#0B0B10")), QColor(QStringLiteral("#FFB000"))), QStringLiteral("amber"));
    QVERIFY(P::findPresetId(QColor(QStringLiteral("#0B0B10")), QColor(QStringLiteral("#FFB001"))).isEmpty());
    QVERIFY(!P::findPreset(QStringLiteral("unknown")));

    // The row Contrast of the page (160:4447): the ratio, the verdict and the badge
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);
    QCOMPARE(section->getContrastRatioLabel()->text(), QStringLiteral("10.7 : 1"));
    QCOMPARE(section->getContrastVerdictLabel()->text(), QStringLiteral("Excellent"));
    QCOMPARE(section->getContrastBadge()->getIconName(), QStringLiteral("check"));
    QCOMPARE(section->getContrastBadge()->getExtent(), 20);
    QCOMPARE(section->getContrastBadge()->getIconExtent(), 12);
    QVERIFY(section->getContrastRow()->getControlWidget()->accessibleName().contains(QStringLiteral("10.7 : 1")));

    const std::tuple<QString, QString, QString, QString> pairs[] =
    {
        { QStringLiteral("#767676"), QStringLiteral("#FFFFFF"), QStringLiteral("Good"), QStringLiteral("check") },
        { QStringLiteral("#949494"), QStringLiteral("#FFFFFF"), QStringLiteral("Low"), QStringLiteral("alert-triangle") },
        { QStringLiteral("#AAAAAA"), QStringLiteral("#FFFFFF"), QStringLiteral("Poor"), QStringLiteral("x") }
    };
    for (const auto& [text, page, verdict, icon] : pairs)
    {
        QVERIFY(!fixture.settings().setValues({ { pdf::PDFSettingKeys::colorModesTextColor(), text }, { pdf::PDFSettingKeys::colorModesPageColor(), page } }).isError());
        fixture.settle();
        QCOMPARE(section->getContrastVerdictLabel()->text(), verdict);
        QCOMPARE(section->getContrastBadge()->getIconName(), icon);
    }
}

void AppSettingsRenderingTest::colorManagementGenericDisables()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));

    // The system with the version of the library (944:9100)
    pdf::PDFComboBox* system = RenderingTestHelper::comboBox(fixture, QStringLiteral("color-management-system"));
    QVERIFY(system);
    QCOMPARE(system->count(), 2);
    QCOMPARE(system->itemText(0), QStringLiteral("Generic"));
    QVERIFY2(system->itemText(1).startsWith(QStringLiteral("Little CMS 2.")), qPrintable(system->itemText(1)));
    QCOMPARE(system->currentData().toString(), QStringLiteral("lcms2"));
    QCOMPARE(RenderingTestHelper::comboBox(fixture, QStringLiteral("rendering-intent"))->currentText(), QStringLiteral("Automatic"));

    // Generic disables every other row of the section, the document follows
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("color-management-system")), QStringLiteral("generic")));
    fixture.settle();
    QTRY_COMPARE(view->getCMSManager()->getSettings().system, pdf::PDFCMSSettings::System::Generic);
    for (pdf::PDFAppSettingRow* row : fixture.settingsPage()->getSection()->getRows())
    {
        QCOMPARE(row->getControlWrap()->isEnabled(), row->getAnchor() == QStringLiteral("color-management-system"));
    }
    QVERIFY(!pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("output-color-profile")), QString()));

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("color-management-system")), QStringLiteral("lcms2")));
    fixture.settle();
    for (pdf::PDFAppSettingRow* row : fixture.settingsPage()->getSection()->getRows())
    {
        QVERIFY2(row->getControlWrap()->isEnabled(), qPrintable(row->getAnchor()));
    }

    // The other rows reach the color management of the document
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("rendering-intent")), QStringLiteral("perceptual")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("black-point-compensation")), false));
    QTRY_COMPARE(view->getCMSManager()->getSettings().intent, pdf::RenderingIntent::Perceptual);
    QTRY_VERIFY(!view->getCMSManager()->getSettings().isBlackPointCompensationActive);
}

void AppSettingsRenderingTest::profileListsAndDirectory()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
    pdf::PDFAppColorManagementSettingsSection* section = RenderingTestHelper::colorManagement(fixture);
    QVERIFY(RenderingTestHelper::waitForScan(section));

    // The built-in lists: the first profile is the default (stored as "")
    pdf::PDFComboBox* output = RenderingTestHelper::comboBox(fixture, QStringLiteral("output-color-profile"));
    pdf::PDFComboBox* gray = RenderingTestHelper::comboBox(fixture, QStringLiteral("device-gray-color-profile"));
    pdf::PDFComboBox* rgb = RenderingTestHelper::comboBox(fixture, QStringLiteral("device-rgb-color-profile"));
    pdf::PDFComboBox* cmyk = RenderingTestHelper::comboBox(fixture, QStringLiteral("device-cmyk-color-profile"));
    QVERIFY(output && gray && rgb && cmyk);
    QVERIFY(output->count() >= 1);
    QCOMPARE(output->itemData(0).toString(), QString());
    QVERIFY(!output->itemText(0).isEmpty());
    QCOMPARE(gray->currentData().toString(), QString());
    QVERIFY(gray->itemText(0).startsWith(QStringLiteral("Gray D65")));
    QVERIFY(gray->findData(QStringLiteral("@GENERIC_Gray_D50_g22")) > 0);
    QVERIFY(rgb->findData(QStringLiteral("@GENERIC_RGB_HDTV")) > 0);
    QCOMPARE(cmyk->itemData(0).toString(), QString());

    // A profile of the list is stored and reaches the document
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("device-gray-color-profile")), QStringLiteral("@GENERIC_Gray_D50_g22")));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::cmsDeviceGrayProfile()), QStringLiteral("@GENERIC_Gray_D50_g22"));
    QTRY_COMPARE(view->getCMSManager()->getSettings().deviceGray, QStringLiteral("@GENERIC_Gray_D50_g22"));

    // A folder of the user (Browse… by the dialog of the application): its profile
    // is listed after the reading in the worker and used by the document
    const QString folder = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("profiles"));
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(RenderingTestHelper::writeProfile(QDir(folder).absoluteFilePath(QStringLiteral("Test.icc")), QStringLiteral("PDF4QT test RGB")));
    QVERIFY(RenderingTestHelper::writeFile(QDir(folder).absoluteFilePath(QStringLiteral("Broken.icc")), QByteArray(300, '\x7F')));
    QVERIFY(RenderingTestHelper::writeFile(QDir(folder).absoluteFilePath(QStringLiteral("readme.txt")), QByteArray("not a profile")));
    pdf::PDFAppFileDialogs::setFolderHandler([folder](QWidget*, const QString&) { return folder; });
    const int scans = section->getScanCount();
    RenderingTestHelper::click(fixture.findRow(QStringLiteral("additional-profile-directory"))->getControl()->getBrowseButton());
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::cmsProfileDirectory()), folder);
    QVERIFY(RenderingTestHelper::waitForScan(section, scans + 1));
    QCOMPARE(section->getProfileList()->directoryState, pdf::PDFAppProfileDirectoryState::Usable);
    QCOMPARE(section->getProfileList()->directory, folder);
    rgb = RenderingTestHelper::comboBox(fixture, QStringLiteral("device-rgb-color-profile"));
    const int index = rgb->findText(QStringLiteral("PDF4QT test RGB"));
    QVERIFY2(index > 0, qPrintable(QStringLiteral("%1 items").arg(rgb->count())));
    QVERIFY(rgb->itemData(index).toString().endsWith(QStringLiteral("Test.icc")));
    QCOMPARE(fixture.findRow(QStringLiteral("additional-profile-directory"))->getDescription(), QStringLiteral("Folder with your own ICC profiles"));
    QTRY_COMPARE(view->getCMSManager()->getSettings().profileDirectory, folder);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("device-rgb-color-profile")), rgb->itemData(index)));
    QTRY_VERIFY(view->getCMSManager()->getSettings().deviceRGB.endsWith(QStringLiteral("Test.icc")));

    // The folder emptied: the stored profile is "(not available)", the document uses the default
    QVERIFY(QFile::remove(QDir(folder).absoluteFilePath(QStringLiteral("Test.icc"))));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::cmsProfileDirectory(), QString()));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::cmsProfileDirectory(), folder));
    QVERIFY(RenderingTestHelper::waitForScan(section, scans + 2));
    QTRY_VERIFY(fixture.findRow(QStringLiteral("device-rgb-color-profile"))->getControl()->isNotAvailableShown());

    // Two folders while the lists are read: the lists of the last folder are shown
    const QString second = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(second));
    QVERIFY(RenderingTestHelper::writeProfile(QDir(second).absoluteFilePath(QStringLiteral("Second.icc")), QStringLiteral("PDF4QT second RGB")));
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::cmsProfileDirectory(), fixture.folder()).isError());
    QTRY_VERIFY(section->isScanning() || section->getProfileList()->directory == fixture.folder());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::cmsProfileDirectory(), second).isError());
    QTRY_VERIFY_WITH_TIMEOUT(!section->isScanning() && section->getProfileList() && section->getProfileList()->directory == second, 30000 * pdftest::AppDocumentFixture::getTimeoutFactor());
    QVERIFY(RenderingTestHelper::comboBox(fixture, QStringLiteral("device-rgb-color-profile"))->findText(QStringLiteral("PDF4QT second RGB")) > 0);

    // While the lists are read, a stored profile is offered as Loading profiles…
    pdf::PDFAppColorProfileOptionsProvider provider;
    const pdf::PDFSettingDefinition* definition = fixture.context->getSettingRegistry()->findDefinition(pdf::PDFSettingKeys::cmsDeviceRgbProfile());
    provider.setLoading({ { pdf::PDFSettingKeys::cmsDeviceRgbProfile(), QStringLiteral("x.icc") } });
    std::vector<pdf::PDFSettingOption> options = provider.getOptions(*definition);
    QCOMPARE(options.size(), size_t(2));
    QCOMPARE(options.back().id, QStringLiteral("x.icc"));
    QCOMPARE(options.back().title, QStringLiteral("Loading profiles…"));
    provider.setList(std::make_shared<const pdf::PDFAppColorProfileList>());
    options = provider.getOptions(*definition);
    QCOMPARE(options.size(), size_t(1));
    QCOMPARE(options.front().title, QStringLiteral("None (simple conversion)"));
}

void AppSettingsRenderingTest::profileDirectoryHostile()
{
    using State = pdf::PDFAppProfileDirectoryState;
    using Profiles = pdf::PDFAppColorProfiles;

    pdftest::AppSettingsFixture fixture;
    const QDir root(fixture.folder());

    // Too many entries (10 000 files of another application)
    const QString many = root.absoluteFilePath(QStringLiteral("many"));
    QVERIFY(QDir().mkpath(many));
    for (int i = 0; i < 10000; ++i)
    {
        QFile file(QDir(many).absoluteFilePath(QStringLiteral("file%1.dat").arg(i)));
        QVERIFY(file.open(QFile::WriteOnly));
    }
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(Profiles::checkDirectory(many), State::TooManyFiles);
    qDebug("check of 10 000 files: %lld ms", qint64(timer.elapsed()));

    // Too many profiles, a huge profile, profiles too large together
    const QString profiles = root.absoluteFilePath(QStringLiteral("profiles"));
    QVERIFY(QDir().mkpath(profiles));
    for (int i = 0; i <= Profiles::MAXIMUM_PROFILES; ++i)
    {
        QVERIFY(RenderingTestHelper::writeFile(QDir(profiles).absoluteFilePath(QStringLiteral("p%1.ICC").arg(i)), QByteArray(64, '\x01')));
    }
    QCOMPARE(Profiles::checkDirectory(profiles), State::TooManyFiles);

    const QString huge = root.absoluteFilePath(QStringLiteral("huge"));
    QVERIFY(QDir().mkpath(huge));
    {
        QFile file(QDir(huge).absoluteFilePath(QStringLiteral("huge.icc")));
        QVERIFY(file.open(QFile::WriteOnly));
        QVERIFY(file.resize(Profiles::MAXIMUM_PROFILE_SIZE + 1));
    }
    QCOMPARE(Profiles::checkDirectory(huge), State::TooLarge);

    // Damaged profiles within the limits are read without a harm
    const QString damaged = root.absoluteFilePath(QStringLiteral("damaged"));
    QVERIFY(QDir().mkpath(damaged));
    QByteArray header = QColorSpace(QColorSpace::SRgb).iccProfile();
    QVERIFY(RenderingTestHelper::writeFile(QDir(damaged).absoluteFilePath(QStringLiteral("truncated.icc")), header.left(header.size() / 2)));
    header[20] = 'X';
    QVERIFY(RenderingTestHelper::writeFile(QDir(damaged).absoluteFilePath(QStringLiteral("header.icc")), header));
    QVERIFY(RenderingTestHelper::writeFile(QDir(damaged).absoluteFilePath(QStringLiteral("empty.icc")), QByteArray()));
    QCOMPARE(Profiles::checkDirectory(damaged), State::Usable);
    const pdf::PDFAppColorProfileList list = Profiles::scan(damaged);
    QCOMPARE(list.directoryState, State::Usable);
    QVERIFY(!list.rgbProfiles.empty());

    // A network folder, a missing folder, a file: not read, the check never waits
    timer.restart();
    QCOMPARE(Profiles::checkDirectory(QStringLiteral("//pdf4qt-unknown-server/share/profiles")), State::Network);
    QCOMPARE(Profiles::checkDirectory(QStringLiteral("\\\\pdf4qt-unknown-server\\share")), State::Network);
    // Device paths of Windows reaching a server or a device (a stored hostile value,
    // an imported backup) are never touched either
    QCOMPARE(Profiles::checkDirectory(QStringLiteral("\\\\.\\UNC\\pdf4qt-unknown-server\\share")), State::Network);
    QCOMPARE(Profiles::checkDirectory(QStringLiteral("\\\\?\\GLOBALROOT\\Device\\Mup\\pdf4qt-unknown-server\\share")), State::NotFolder);
    QCOMPARE(Profiles::checkDirectory(QStringLiteral("//./pipe/pdf4qt-profiles")), State::NotFolder);
    QVERIFY(!Profiles::isDirectoryUsable(QStringLiteral("\\\\.\\GLOBALROOT\\Device\\LanmanRedirector\\pdf4qt-unknown-server\\share")));
    QVERIFY(timer.elapsed() < 2000 * pdftest::AppDocumentFixture::getTimeoutFactor());
    QCOMPARE(Profiles::checkDirectory(root.absoluteFilePath(QStringLiteral("missing"))), State::Missing);
    QCOMPARE(Profiles::checkDirectory(QDir(damaged).absoluteFilePath(QStringLiteral("empty.icc"))), State::NotFolder);
    QCOMPARE(Profiles::checkDirectory(QString()), State::None);
    QVERIFY(Profiles::isDirectoryUsable(QString()));
    QVERIFY(!Profiles::isDirectoryUsable(many));
    QVERIFY(!Profiles::getStateText(State::Network).isEmpty());
    QVERIFY(Profiles::getStateText(State::Usable).isEmpty());

    // The names of the profiles are data: control and bidirectional characters removed, bounded
    QCOMPARE(Profiles::sanitizeName(QStringLiteral("A\u202EB\nC\u2028D")), QStringLiteral("ABC D"));
    QCOMPARE(Profiles::sanitizeName(QString(1000, QLatin1Char('x'))).size(), qsizetype(Profiles::MAXIMUM_NAME_LENGTH));

    // The page tells, why the folder is not read; the document does not use it
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
    pdf::PDFAppColorManagementSettingsSection* section = RenderingTestHelper::colorManagement(fixture);
    QVERIFY(RenderingTestHelper::waitForScan(section));
    const std::pair<QString, State> folders[] =
    {
        { many, State::TooManyFiles },
        { huge, State::TooLarge },
        { QStringLiteral("//pdf4qt-unknown-server/share/profiles"), State::Network }
    };
    for (const auto& [path, state] : folders)
    {
        const int scans = section->getScanCount();
        QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("additional-profile-directory")), path));
        QVERIFY(RenderingTestHelper::waitForScan(section, scans + 1));
        QCOMPARE(section->getProfileList()->directoryState, state);
        QCOMPARE(fixture.findRow(QStringLiteral("additional-profile-directory"))->getDescription(), Profiles::getStateText(state));
        QTRY_VERIFY(!view->isApplySettingsQueued());
        QVERIFY(view->getCMSManager()->getSettings().profileDirectory.isEmpty());
    }
}

void AppSettingsRenderingTest::advancedEngineAndCaches()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* first = fixture.openDocument();
    pdf::PDFAppDocumentTab* second = fixture.openDocument();
    QVERIFY(first && second);
    QPointer<pdf::PDFAppDocumentView> views[] = { first->getView(), second->getView() };
    QVERIFY(fixture.openSettings(QStringLiteral("advanced")));

    // The engine of both documents is switched
    pdf::PDFComboBox* engine = RenderingTestHelper::comboBox(fixture, QStringLiteral("rendering-engine"));
    QCOMPARE(engine->itemText(0), QStringLiteral("QPainter"));
    QCOMPARE(engine->currentData().toString(), QStringLiteral("blend2dMultiThread"));
    const std::pair<QString, pdf::RendererEngine> engines[] =
    {
        { QStringLiteral("qpainter"), pdf::RendererEngine::QPainter },
        { QStringLiteral("blend2dSingleThread"), pdf::RendererEngine::Blend2D_SingleThread },
        { QStringLiteral("blend2dMultiThread"), pdf::RendererEngine::Blend2D_MultiThread }
    };
    for (const auto& [id, rendererEngine] : engines)
    {
        QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("rendering-engine")), id));
        for (const QPointer<pdf::PDFAppDocumentView>& view : views)
        {
            QTRY_COMPARE(view->getProxy()->getRendererEngine(), rendererEngine);
        }
    }

    // The strategy of the threads is the one of the process
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("multithreading-strategy")), QStringLiteral("singleThread")));
    QTRY_VERIFY(!pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("multithreading-strategy")), QStringLiteral("loadBalanced")));
    QTRY_VERIFY(pdf::PDFExecutionPolicy::isParallelizing(pdf::PDFExecutionPolicy::Scope::Page));

    // Caches and shading (3 decimals)
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("compiled-page-cache-size")), qint64(131072)));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("cached-font-limit")), qint64(64)));
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::cacheCompiledPages()), qint64(131072));
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::cacheFontLimit()), qint64(64));
    QCOMPARE(fixture.findRow(QStringLiteral("compiled-page-cache-size"))->getControl()->getSpinBox()->getDisplaySuffix(), QStringLiteral("kB"));
    QCOMPARE(fixture.findRow(QStringLiteral("color-tolerance"))->getControl()->getDoubleSpinBox()->decimals(), 3);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("preferred-mesh-resolution-ratio")), 0.05));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("color-tolerance")), 0.123));
    for (const QPointer<pdf::PDFAppDocumentView>& view : views)
    {
        QTRY_COMPARE(view->getProxy()->getMeshQualitySettings().preferredMeshResolutionRatio, 0.05);
        QTRY_COMPARE(view->getProxy()->getMeshQualitySettings().tolerance, 0.123);
    }

    // Prefetch is stored (the view reads it at the navigation)
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("prefetch-pages")), false));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::renderingPrefetchPages()), false);
}

void AppSettingsRenderingTest::developerRowsEnabledByMode()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    pdf::PDFAppDocumentView* view = tab->getView();
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("advanced"));
    QVERIFY(page);

    // Developer 147:4951 and the rows of the old Editor below the time (not in Figma)
    pdf::PDFAppSettingGroup* group = page->getSection()->findGroup(QStringLiteral("Developer"));
    QVERIFY(group);
    QStringList anchors;
    for (pdf::PDFAppSettingRow* row : group->getRows())
    {
        anchors << row->getAnchor();
    }
    QCOMPARE(anchors, QStringList({ QStringLiteral("developer-mode"), QStringLiteral("display-page-compile-draw-time"), QStringLiteral("display-text-blocks"), QStringLiteral("display-text-lines") }));

    for (const QString& anchor : anchors.mid(1))
    {
        QVERIFY2(!fixture.findRow(anchor)->getControlWrap()->isEnabled(), qPrintable(anchor));
        QVERIFY(!pdftest::AppSettingsFixture::setControlValue(fixture.findRow(anchor), true));
    }

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("developer-mode")), true));
    for (const QString& anchor : anchors.mid(1))
    {
        QTRY_VERIFY2(fixture.findRow(anchor)->getControlWrap()->isEnabled(), qPrintable(anchor));
    }

    // The overlays reach the document
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("display-text-blocks")), true));
    QTRY_VERIFY(view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::DebugTextBlocks));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("developer-mode")), false));
    QTRY_VERIFY(!view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::DebugTextBlocks));
    QTRY_VERIFY(!fixture.findRow(QStringLiteral("display-text-lines"))->getControlWrap()->isEnabled());
}

void AppSettingsRenderingTest::twoDocumentsOneApply()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* first = fixture.openDocument();
    pdf::PDFAppDocumentTab* second = fixture.openDocument();
    QVERIFY(first && second);
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::viewingColorMode(), QStringLiteral("customColors")).isError());
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("color-modes"));
    QVERIFY(page);
    QTRY_VERIFY(!first->getView()->isApplySettingsQueued() && !second->getView()->isApplySettingsQueued());

    // A preset (three values) compiles each document once
    QSignalSpy firstSpy(first->getView()->getCMSManager(), &pdf::PDFCMSManager::colorManagementSystemChanged);
    QSignalSpy secondSpy(second->getView()->getCMSManager(), &pdf::PDFCMSManager::colorManagementSystemChanged);
    RenderingTestHelper::click(RenderingTestHelper::colorModes(fixture)->getPresetCard(QStringLiteral("mint")));
    fixture.settle();
    QTest::qWait(50);
    QCOMPARE(firstSpy.size(), 1);
    QCOMPARE(secondSpy.size(), 1);

    // Restore defaults of the section (four values) - once again
    QVERIFY(page->openRestoreDefaultsDialog());
    QVERIFY(page->getRestoreDialog()->getChangeCount() >= 4);
    QTest::mouseClick(page->getRestoreDialog()->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QTest::qWait(50);
    QCOMPARE(firstSpy.size(), 2);
    QCOMPARE(secondSpy.size(), 2);
    QCOMPARE(first->getView()->getCMSManager()->getSettings().backgroundColor, QColor(QStringLiteral("#0B0B10")));
}

void AppSettingsRenderingTest::cmsChangeCost()
{
    // A change of a setting of the color management (a color of the color modes, a
    // slider) clears the lists of the manager of each open document: the next
    // color management lists the installed profiles of the system again in the
    // thread of the user interface (PDFCMSManager::getExternalProfilesImpl, more
    // than 100 ms on Windows). The application lists them once per process (the
    // opt-in cache of Core, review RC5-BC, item 2 of step5_followups.md); the time
    // of one change is printed without and with the cache.
    const bool wasEnabled = pdf::PDFCMSManager::isExternalProfilesCacheEnabled();
    {
        // The context turns the cache on once at the start; a view does not
        // (FX5-2: it was turned on in every constructor of PDFAppDocumentView)
        pdf::PDFCMSManager::setExternalProfilesCacheEnabled(false);
        pdftest::AppSettingsFixture fixture;
        QVERIFY(pdf::PDFCMSManager::isExternalProfilesCacheEnabled());
        pdf::PDFCMSManager::setExternalProfilesCacheEnabled(false);
        QVERIFY(fixture.openDocument());
        QVERIFY(!pdf::PDFCMSManager::isExternalProfilesCacheEnabled());
    }

    struct Measurement
    {
        double listing = 0.0;
        double creation = 0.0;
        int profiles = 0;
    };

    constexpr int CHANGES = 10;
    auto measure = [](bool isCached) -> Measurement
    {
        pdf::PDFCMSManager::setExternalProfilesCacheEnabled(isCached);
        pdf::PDFCMSManager manager(nullptr);
        pdf::PDFCMSSettings settings = manager.getDefaultSettings();
        manager.setSettings(settings);
        manager.getCurrentCMS();

        Measurement result;
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < CHANGES; ++i)
        {
            settings.foregroundColor = QColor::fromRgb(0x10 + i, 0x20, 0x30);
            manager.setSettings(settings);
            result.profiles = int(manager.getRGBProfiles().size());
        }
        result.listing = double(timer.nsecsElapsed()) / CHANGES / 1e6;

        timer.restart();
        for (int i = 0; i < CHANGES; ++i)
        {
            settings.foregroundColor = QColor::fromRgb(0x30 + i, 0x20, 0x10);
            manager.setSettings(settings);
            manager.getCurrentCMS();
        }
        result.creation = double(timer.nsecsElapsed()) / CHANGES / 1e6;
        return result;
    };

    const Measurement uncached = measure(false);
    const Measurement cached = measure(true);
    QCOMPARE(cached.profiles, uncached.profiles);
    qInfo("one change of the color management (%d RGB profiles): lists %.3f ms, color management %.3f ms; cached: lists %.3f ms, color management %.3f ms",
          uncached.profiles, uncached.listing, uncached.creation, cached.listing, cached.creation);

    // The cache: a profile added to the folder is listed after the cache is
    // cleared (Settings › Color management reads the lists again), managers of
    // other threads share the lists
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    QVERIFY(RenderingTestHelper::writeProfile(QDir(folder.path()).absoluteFilePath(QStringLiteral("First.icc")), QStringLiteral("PDF4QT first RGB")));
    auto findProfile = [](const pdf::PDFCMSManager& manager, const QString& name)
    {
        for (const pdf::PDFColorProfileIdentifier& identifier : manager.getRGBProfiles())
        {
            if (identifier.name == name)
            {
                return true;
            }
        }
        return false;
    };

    pdf::PDFCMSManager::clearExternalProfilesCache();
    pdf::PDFCMSManager manager(nullptr);
    pdf::PDFCMSSettings settings = manager.getDefaultSettings();
    settings.profileDirectory = folder.path();
    manager.setSettings(settings);
    QVERIFY(findProfile(manager, QStringLiteral("PDF4QT first RGB")));

    QVERIFY(RenderingTestHelper::writeProfile(QDir(folder.path()).absoluteFilePath(QStringLiteral("Second.icc")), QStringLiteral("PDF4QT second RGB")));
    settings.foregroundColor = Qt::red;
    manager.setSettings(settings);
    QVERIFY(findProfile(manager, QStringLiteral("PDF4QT first RGB")));
    QVERIFY(!findProfile(manager, QStringLiteral("PDF4QT second RGB")));

    bool isSharedByThread = false;
    std::thread thread([&isSharedByThread, &settings, &findProfile]()
    {
        pdf::PDFCMSManager threadManager(nullptr);
        threadManager.setSettings(settings);
        isSharedByThread = findProfile(threadManager, QStringLiteral("PDF4QT first RGB")) && !findProfile(threadManager, QStringLiteral("PDF4QT second RGB"));
    });
    thread.join();
    QVERIFY(isSharedByThread);

    // The lists of Settings are current and the documents take them at their next change
    const pdf::PDFAppColorProfileList list = pdf::PDFAppColorProfiles::scan(folder.path());
    bool isListed = false;
    for (const pdf::PDFAppColorProfileList::Profile& profile : list.rgbProfiles)
    {
        isListed = isListed || profile.name == QStringLiteral("PDF4QT second RGB");
    }
    QVERIFY(isListed);
    settings.foregroundColor = Qt::green;
    manager.setSettings(settings);
    QVERIFY(findProfile(manager, QStringLiteral("PDF4QT second RGB")));

    // Off: every change lists the profiles again
    pdf::PDFCMSManager::setExternalProfilesCacheEnabled(false);
    QVERIFY(QFile::remove(QDir(folder.path()).absoluteFilePath(QStringLiteral("Second.icc"))));
    settings.foregroundColor = Qt::blue;
    manager.setSettings(settings);
    QVERIFY(!findProfile(manager, QStringLiteral("PDF4QT second RGB")));
    pdf::PDFCMSManager::setExternalProfilesCacheEnabled(wasEnabled);
}

void AppSettingsRenderingTest::hostileStoredValues()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppContext* context = fixture.context.get();

    // Rows of another version: unknown options, scripts, wrong types, 64 kB texts
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::viewingColorMode(), QStringLiteral("sepia")));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::colorModesCustomPreset(), QStringLiteral("<script>")));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::colorModesTextColor(), QStringLiteral("javascript:")));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::colorModesPageColor(), QStringLiteral("#GGGGGG")));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::colorModesHighContrastStrength(), std::nan("")));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::cmsOutputProfile(), QString(65536, QLatin1Char('o'))));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::cmsDeviceRgbProfile(), QStringLiteral("C:/pdf4qt-missing/profile.icc")));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::cmsDeviceCmykProfile(), qint64(42)));
    const QString bidiProfile = QStringLiteral("evil") + QChar(0x202E) + QStringLiteral("cci.exe\n") + QChar(0x0001) + QChar(0x2028) + QStringLiteral("x");
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::cmsDeviceGrayProfile(), bidiProfile));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::cmsProfileDirectory(), QString(65536, QLatin1Char('d'))));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::cmsSystem(), QStringLiteral("lcms3")));
    QVERIFY(RenderingTestHelper::storeRaw(context, pdf::PDFSettingKeys::renderingEngine(), QByteArray("\x00\xFF", 2)));

    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);
    QVERIFY(section->getModeCard(QStringLiteral("normal"))->isSelected());
    QVERIFY(section->getPresetCard(QStringLiteral("amber"))->isSelected());
    QCOMPARE(section->getTextColor(), QColor(QStringLiteral("#FFB000")));
    QCOMPARE(section->getPageColor(), QColor(QStringLiteral("#0B0B10")));
    QCOMPARE(section->getPreview()->getTextColor(), QColor(QStringLiteral("#FFB000")));
    QCOMPARE(section->getContrastRatioLabel()->text(), QStringLiteral("10.7 : 1"));
    QCOMPARE(fixture.findRow(QStringLiteral("high-contrast-strength"))->getControl()->getShownValue().toDouble(), 10.0);

    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
    QVERIFY(RenderingTestHelper::waitForScan(RenderingTestHelper::colorManagement(fixture)));
    // A system of another version is "(not available)" (the documents use Little CMS)
    QVERIFY(fixture.findRow(QStringLiteral("color-management-system"))->getControl()->isNotAvailableShown());
    QCOMPARE(fixture.findRow(QStringLiteral("color-management-system"))->getControl()->getShownValue(), QVariant());
    // A stored profile, which is not offered (also a hostile text), is "(not available)"
    // with at most MAXIMUM_OPTION_TEXT characters, a value of another type is the default
    QVERIFY(fixture.findRow(QStringLiteral("output-color-profile"))->getControl()->isNotAvailableShown());
    QVERIFY(RenderingTestHelper::comboBox(fixture, QStringLiteral("output-color-profile"))->currentText().size() < 100);
    QVERIFY(fixture.findRow(QStringLiteral("device-rgb-color-profile"))->getControl()->isNotAvailableShown());
    QVERIFY(!fixture.findRow(QStringLiteral("device-cmyk-color-profile"))->getControl()->isNotAvailableShown());

    // ... without control, format (bidirectional) and line separator characters - in
    // the combo box and in the texts of the values (Restore defaults, review RC5-BC)
    QVERIFY(fixture.findRow(QStringLiteral("device-gray-color-profile"))->getControl()->isNotAvailableShown());
    QString restoreText;
    QString restoreDefaultText;
    for (const pdf::PDFAppSettingChange& change : RenderingTestHelper::colorManagement(fixture)->getChangedValues())
    {
        if (change.key == pdf::PDFSettingKeys::cmsDeviceGrayProfile())
        {
            restoreText = change.currentValue;
            restoreDefaultText = change.defaultValue;
        }
    }
    QVERIFY(restoreDefaultText.startsWith(QStringLiteral("Gray D65")));
    const pdf::PDFSettingDefinition* systemDefinition = context->getSettingRegistry()->findDefinition(pdf::PDFSettingKeys::cmsSystem());
    QVERIFY(systemDefinition);
    for (const QString& shown : { RenderingTestHelper::comboBox(fixture, QStringLiteral("device-gray-color-profile"))->currentText(), restoreText,
                                  pdf::PDFAppSettingControl::formatValue(*systemDefinition, bidiProfile) })
    {
        QVERIFY2(shown.contains(QStringLiteral("evilcci.exe")), qPrintable(shown));
        for (const QChar character : { QChar(0x202E), QChar(QLatin1Char('\n')), QChar(0x0001), QChar(0x2028) })
        {
            QVERIFY2(!shown.contains(character), qPrintable(shown));
        }
    }
    QCOMPARE(RenderingTestHelper::colorManagement(fixture)->getProfileList()->directoryState, pdf::PDFAppProfileDirectoryState::None);

    QVERIFY(fixture.openSettings(QStringLiteral("advanced")));
    QCOMPARE(RenderingTestHelper::comboBox(fixture, QStringLiteral("rendering-engine"))->currentData().toString(), QStringLiteral("blend2dMultiThread"));

    // Nothing has been written back: the hostile rows stay as they are
    QCOMPARE(fixture.settings().getStoredValue(pdf::PDFSettingKeys::colorModesCustomPreset()), QVariant(QStringLiteral("<script>")));
    QCOMPARE(fixture.settings().getStoredValue(pdf::PDFSettingKeys::cmsDeviceRgbProfile()), QVariant(QStringLiteral("C:/pdf4qt-missing/profile.icc")));

    // The document opens with the hostile rows
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    QTRY_VERIFY(!tab->getView()->isApplySettingsQueued());
    QCOMPARE(tab->getView()->getCMSManager()->getSettings().backgroundColor, QColor(QStringLiteral("#0B0B10")));
    QVERIFY(tab->getView()->getCMSManager()->getSettings().profileDirectory.isEmpty());
    QCOMPARE(tab->getView()->getCMSManager()->getSettings().system, pdf::PDFCMSSettings::System::LittleCMS2);
    QVERIFY(!tab->getView()->getCMSManager()->getSettings().outputCS.startsWith(QStringLiteral("ooo")));
}

void AppSettingsRenderingTest::otherInstanceChange()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);

    // Another instance chooses a mode, a preset with its colors
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::viewingColorMode(), QStringLiteral("bitonal")));
    QVERIFY(section->getModeCard(QStringLiteral("bitonal"))->isSelected());
    QVERIFY(!section->getModeCard(QStringLiteral("normal"))->isSelected());
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::colorModesCustomPreset(), QStringLiteral("night")));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::colorModesTextColor(), QStringLiteral("#D6D9DE")));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::colorModesPageColor(), QStringLiteral("#1E2228")));
    QVERIFY(section->getPresetCard(QStringLiteral("night"))->isSelected());
    QCOMPARE(section->getPreview()->getPageColor(), QColor(QStringLiteral("#1E2228")));
    QCOMPARE(fixture.findRow(QStringLiteral("page-color"))->getControl()->getColorButton()->getShownName(), QStringLiteral("Night blue"));

    // A change of another instance never switches the preset (only a color of the user does)
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::colorModesTextColor(), QStringLiteral("#ABCDEF")));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("night"));

    // The folder of the profiles of another instance gives new lists
    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
    pdf::PDFAppColorManagementSettingsSection* management = RenderingTestHelper::colorManagement(fixture);
    QVERIFY(RenderingTestHelper::waitForScan(management));
    const int scans = management->getScanCount();
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::cmsProfileDirectory(), fixture.folder()));
    QVERIFY(RenderingTestHelper::waitForScan(management, scans + 1));
    QCOMPARE(management->getProfileList()->directory, fixture.folder());
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::cmsSystem(), QStringLiteral("generic")));
    QVERIFY(!fixture.findRow(QStringLiteral("device-gray-color-profile"))->getControlWrap()->isEnabled());
}

void AppSettingsRenderingTest::restoreDefaults()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("color-modes"));
    QVERIFY(page);
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(!page->getRestoreButton()->isEnabled());

    RenderingTestHelper::click(section->getModeCard(QStringLiteral("grayscale")));
    RenderingTestHelper::click(section->getPresetCard(QStringLiteral("highContrast")));
    fixture.settle();
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("black-and-white-threshold")), qint64(200)));
    QTRY_VERIFY(page->getRestoreButton()->isEnabled());

    // The dialog lists the five changed values, Restore Defaults restores them in one step
    QVERIFY(page->openRestoreDefaultsDialog());
    QCOMPARE(page->getRestoreDialog()->getChangeCount(), 5);
    QTest::mouseClick(page->getRestoreDialog()->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(fixture.settings().getChangedKeys(pdf::PDFSettingDefinitions::sectionColorModes()).isEmpty());
    QVERIFY(section->getModeCard(QStringLiteral("normal"))->isSelected());
    QVERIFY(section->getPresetCard(QStringLiteral("amber"))->isSelected());
    QCOMPARE(section->getPreview()->getPageColor(), QColor(QStringLiteral("#0B0B10")));
    QVERIFY(!page->getRestoreButton()->isEnabled());

    // Color management: the profiles and the folder go back, the lists are read again
    page = fixture.openSettings(QStringLiteral("color-management"));
    pdf::PDFAppColorManagementSettingsSection* management = RenderingTestHelper::colorManagement(fixture);
    QVERIFY(RenderingTestHelper::waitForScan(management));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("device-gray-color-profile")), QStringLiteral("@GENERIC_Gray_D93_g10")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("additional-profile-directory")), fixture.folder()));
    QVERIFY(RenderingTestHelper::waitForScan(management, 2));
    const int scans = management->getScanCount();
    QVERIFY(page->openRestoreDefaultsDialog());
    QCOMPARE(page->getRestoreDialog()->getChangeCount(), 2);
    QTest::mouseClick(page->getRestoreDialog()->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(RenderingTestHelper::waitForScan(management, scans + 1));
    QCOMPARE(RenderingTestHelper::comboBox(fixture, QStringLiteral("device-gray-color-profile"))->currentData().toString(), QString());
    QVERIFY(management->getProfileList()->directory.isEmpty());

    // Viewing and Advanced are generic sections
    page = fixture.openSettings(QStringLiteral("advanced"));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("rendering-engine")), QStringLiteral("qpainter")));
    QVERIFY(page->openRestoreDefaultsDialog());
    QCOMPARE(page->getRestoreDialog()->getChangeCount(), 1);
    QTest::mouseClick(page->getRestoreDialog()->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::renderingEngine()), QStringLiteral("blend2dMultiThread"));
}

void AppSettingsRenderingTest::searchAndAnchors()
{
    pdftest::AppSettingsFixture fixture;

    // The anchors of the block rows and of the custom rows are highlighted
    for (const QString& anchor : { QStringLiteral("color-mode"), QStringLiteral("preset"), QStringLiteral("contrast"), QStringLiteral("color-preview") })
    {
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes"), anchor));
        pdf::PDFAppSettingRow* row = fixture.findRow(anchor);
        QVERIFY2(row, qPrintable(anchor));
        QVERIFY2(row->isHighlighted(), qPrintable(anchor));
    }
    QVERIFY(fixture.findRow(QStringLiteral("preset"))->getBlockWidget());
    QVERIFY(fixture.findRow(QStringLiteral("preset"))->getControlWrap()->isHidden());
    QVERIFY(fixture.openSettings(QStringLiteral("color-management"), QStringLiteral("output-color-profile")));
    QVERIFY(fixture.findRow(QStringLiteral("output-color-profile"))->isHighlighted());

    // The filter finds the cards by the names of the modes and of the presets
    const std::tuple<QString, QString, QString> queries[] =
    {
        { QStringLiteral("terminal"), QStringLiteral("preset"), QStringLiteral("color-mode") },
        { QStringLiteral("bitonal"), QStringLiteral("color-mode"), QStringLiteral("preset") },
        { QStringLiteral("contrast"), QStringLiteral("contrast"), QStringLiteral("text-color") },
        { QStringLiteral("little cms"), QStringLiteral("color-management-system"), QStringLiteral("accuracy") },
        { QStringLiteral("blend2d"), QStringLiteral("rendering-engine"), QStringLiteral("prefetch-pages") },
        { QStringLiteral("loupe"), QStringLiteral("magnifier-size"), QStringLiteral("antialiasing") }
    };
    for (const auto& [query, shown, hidden] : queries)
    {
        pdf::PDFAppSettingsPage* page = fixture.openFilter(query);
        QVERIFY(page && page->isFiltering());
        QVERIFY2(page->findRow(shown) && !page->findRow(shown)->isHidden(), qPrintable(query));
        QVERIFY2(!page->findRow(hidden) || page->findRow(hidden)->isHidden(), qPrintable(query));
    }
}

void AppSettingsRenderingTest::deletionMatrix()
{
    // A click of a card is written when the window goes before the event loop
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        RenderingTestHelper::click(RenderingTestHelper::colorModes(fixture)->getPresetCard(QStringLiteral("sepia")));
        RenderingTestHelper::click(RenderingTestHelper::colorModes(fixture)->getModeCard(QStringLiteral("inverted")));
        delete fixture.window.data();
        fixture.settle();
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("sepia"));
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesPageColor()), QStringLiteral("#F4ECD8"));
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("inverted"));
    }

    // ... and when the page is left (hidden) before the event loop
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        RenderingTestHelper::click(RenderingTestHelper::colorModes(fixture)->getPresetCard(QStringLiteral("mint")));
        fixture.settingsPage()->commitPendingValues();
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("mint"));
        fixture.navigate(QStringLiteral("/home"));
    }

    // The context closed before the section (a pending click writes nothing)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        RenderingTestHelper::click(RenderingTestHelper::colorModes(fixture)->getPresetCard(QStringLiteral("night")));
        fixture.context->close();
        delete fixture.window.data();
        fixture.settle();
    }

    // The window, the page and the section deleted while the lists of the profiles are read
    for (int variant = 0; variant < 3; ++variant)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
        pdf::PDFAppColorManagementSettingsSection* section = RenderingTestHelper::colorManagement(fixture);
        QVERIFY(section);
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::cmsProfileDirectory(), fixture.folder()).isError());
        switch (variant)
        {
            case 0:
                delete fixture.window.data();
                break;
            case 1:
                fixture.navigate(QStringLiteral("/home"));
                break;
            default:
                delete section;
                break;
        }
        fixture.settle();
        QTest::qWait(200);
        fixture.settle();
    }

    // Parts of the section deleted by the application, then changes arrive
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
        delete section->getModeCard(QStringLiteral("normal"));
        delete section->getPresetCard(QStringLiteral("custom"));
        delete section->getPreview();
        delete section->getContrastBadge();
        delete section->getContrastRow();
        delete fixture.findRow(QStringLiteral("text-color"));
        QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::viewingColorMode(), QStringLiteral("grayscale")));
        QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::colorModesTextColor(), QStringLiteral("#010203")));
        RenderingTestHelper::click(section->getPresetCard(QStringLiteral("paper")));
        fixture.settle();
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("paper"));
        // The preset switches the mode to Custom colors (owner 2026-10-25)
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("customColors"));
        QVERIFY(section->getModeCard(QStringLiteral("customColors"))->isSelected());
        QVERIFY(!section->getModeCard(QStringLiteral("grayscale"))->isSelected());
    }

    // The palette of a color open, then the window goes
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        QVERIFY(fixture.findRow(QStringLiteral("page-color"))->getControl()->getColorButton()->showPalette());
        fixture.settle();
        delete fixture.window.data();
        fixture.settle();
    }
}

void AppSettingsRenderingTest::deleteFromOwnSignals()
{
    // The window deleted synchronously from the click of a card (Qt guards the
    // clicked of a button), the click is written by the section going away
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        pdf::PDFReadingThemeCard* card = RenderingTestHelper::colorModes(fixture)->getPresetCard(QStringLiteral("sepia"));
        QObject::connect(card, &QAbstractButton::clicked, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        RenderingTestHelper::click(card);
        fixture.settle();
        QVERIFY(!fixture.window);
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("sepia"));
    }

    // ... from the notification of the written preset (inside of the write of the section)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        QObject::connect(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        RenderingTestHelper::click(RenderingTestHelper::colorModes(fixture)->getPresetCard(QStringLiteral("black")));
        fixture.settle();
        QVERIFY(!fixture.window);
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("black"));
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()), QStringLiteral("#F2F2F2"));
    }

    // ... from the status message of a refused write
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
        pdf::PDFDatabaseStatement block(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_setting BEFORE INSERT ON setting BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        QVERIFY(block.execute());
        QObject::connect(section, &pdf::PDFAppSettingsSection::statusMessageRequested, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        RenderingTestHelper::click(section->getModeCard(QStringLiteral("bitonal")));
        fixture.settle();
        QVERIFY(!fixture.window);
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("normal"));
    }

    // A refused write: the page shows the status, the cards the stored values
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("color-modes"));
        pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
        pdf::PDFDatabaseStatement block(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_setting BEFORE INSERT ON setting BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        QVERIFY(block.execute());
        RenderingTestHelper::click(section->getPresetCard(QStringLiteral("mint")));
        fixture.settle();
        QVERIFY(!page->getStatusLabel()->isHidden());
        QVERIFY(page->getStatusLabel()->text().contains(QStringLiteral("could not be saved")));
        QVERIFY(section->getPresetCard(QStringLiteral("amber"))->isSelected());
        QVERIFY(!section->getPresetCard(QStringLiteral("mint"))->isSelected());
    }

    // The window deleted from the end of the reading of the profiles
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
        pdf::PDFAppColorManagementSettingsSection* section = RenderingTestHelper::colorManagement(fixture);
        QVERIFY(RenderingTestHelper::waitForScan(section));
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("additional-profile-directory"))->getControl();
        QObject::connect(control, &pdf::PDFAppSettingControl::valueWritten, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("additional-profile-directory")), fixture.folder()));
        QTest::qWait(200);
        fixture.settle();
        QVERIFY(!fixture.window);
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::cmsProfileDirectory()), fixture.folder());
    }
}

void AppSettingsRenderingTest::themesAndScales()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::viewingColorMode(), QStringLiteral("customColors")).isError());
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        for (const QString& sectionId : { QStringLiteral("viewing"), QStringLiteral("color-modes"), QStringLiteral("color-management"), QStringLiteral("advanced") })
        {
            pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId, sectionId == QStringLiteral("color-modes") ? QStringLiteral("preset") : QString());
            QVERIFY(page);
            if (pdf::PDFAppColorManagementSettingsSection* section = RenderingTestHelper::colorManagement(fixture))
            {
                QVERIFY(RenderingTestHelper::waitForScan(section));
            }
            for (qreal ratio : { 1.0, 1.5, 2.0 })
            {
                const QImage image = RenderingTestHelper::render(page, ratio);
                QVERIFY2(RenderingTestHelper::countLeakPixels(image) == 0, qPrintable(QStringLiteral("%1 at %2").arg(sectionId).arg(ratio)));
            }
        }

        // The preview draws the data colors in any theme, its border is of the theme
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        pdf::PDFAppColorModePreview* preview = RenderingTestHelper::colorModes(fixture)->getPreview();
        const QImage image = RenderingTestHelper::render(preview, 2.0);
        QCOMPARE(image.pixelColor(QPoint(preview->width(), 6)).rgb(), QColor(QStringLiteral("#0B0B10")).rgb());
        const QColor border = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BorderStrong);
        const QColor edge = image.pixelColor(QPoint(preview->width(), 0));
        QVERIFY2(qAbs(edge.red() - border.red()) < 40 && qAbs(edge.blue() - border.blue()) < 40, qPrintable(edge.name()));

        // The badge of the contrast in the green of the theme
        pdf::PDFIconBadge* badge = RenderingTestHelper::colorModes(fixture)->getContrastBadge();
        const QImage badgeImage = RenderingTestHelper::render(badge, 1.0);
        const QColor fill = badgeImage.pixelColor(QPoint(10, 2));
        const QColor green = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::AccentGreenBg);
        QVERIFY2(qAbs(fill.green() - green.green()) < 30, qPrintable(fill.name() + QStringLiteral(" ") + green.name()));
    }
}

void AppSettingsRenderingTest::rightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("color-modes"));
        QVERIFY(page);
        pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);

        // The first card is at the right side, the control of the contrast at the left
        QWidget* normal = section->getModeCard(QStringLiteral("normal"));
        QWidget* inverted = section->getModeCard(QStringLiteral("inverted"));
        QVERIFY(normal->mapTo(page, QPoint(0, 0)).x() > inverted->mapTo(page, QPoint(0, 0)).x());
        QWidget* paper = section->getPresetCard(QStringLiteral("paper"));
        QWidget* sepia = section->getPresetCard(QStringLiteral("sepia"));
        QVERIFY(paper->mapTo(page, QPoint(0, 0)).x() > sepia->mapTo(page, QPoint(0, 0)).x());
        pdf::PDFAppSettingRow* contrast = section->getContrastRow();
        QVERIFY(contrast->getControlWrap()->x() < contrast->getTitleLabel()->mapTo(contrast, QPoint(0, 0)).x());
        QCOMPARE(RenderingTestHelper::countLeakPixels(RenderingTestHelper::render(page, 1.0)), 0);
        fixture.screenshot(QStringLiteral("settings-color-modes-rtl"));

        QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
        QVERIFY(RenderingTestHelper::waitForScan(RenderingTestHelper::colorManagement(fixture)));
        QCOMPARE(RenderingTestHelper::countLeakPixels(RenderingTestHelper::render(fixture.settingsPage(), 1.0)), 0);
        fixture.screenshot(QStringLiteral("settings-color-management-rtl"));
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppSettingsRenderingTest::minimalWindow()
{
    pdftest::AppSettingsFixture fixture(QSize(720, 480));
    for (const QString& sectionId : { QStringLiteral("viewing"), QStringLiteral("color-modes"), QStringLiteral("color-management"), QStringLiteral("advanced") })
    {
        pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
        QVERIFY(page);
        fixture.settle();

        // No horizontal scroll bar, every row and every card inside of the page
        QScrollArea* area = RenderingTestHelper::scrollArea(page);
        QVERIFY(area);
        QVERIFY2(area->horizontalScrollBar()->maximum() == 0 || !area->horizontalScrollBar()->isVisible(), qPrintable(sectionId));
        for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
        {
            const QRect rect(row->mapTo(page, QPoint(0, 0)), row->size());
            QVERIFY2(rect.right() <= page->width(), qPrintable(row->getAnchor()));
        }
        fixture.screenshot(QStringLiteral("settings-%1-720").arg(sectionId));
    }

    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("color-modes"), QStringLiteral("color-mode"));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    fixture.screenshot(QStringLiteral("settings-color-modes-720-cards"));
    for (const QString& mode : pdf::PDFAppColorModesSettingsSection::getModeIds())
    {
        QWidget* card = section->getModeCard(mode);
        QVERIFY2(card->mapTo(page, QPoint(card->width(), 0)).x() <= page->width(), qPrintable(mode));
        QVERIFY(card->width() >= pdf::PDFAppColorModesSettingsSection::MODE_CARD_MINIMUM_WIDTH);
    }
    QVERIFY(section->getPreview()->height() >= section->getPreview()->heightForWidth(section->getPreview()->width()));
}

void AppSettingsRenderingTest::keyboardAndAccessibility()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);

    // The cards have their names, take the focus by Tab and choose by Space
    pdf::PDFPreviewCard* inverted = section->getModeCard(QStringLiteral("inverted"));
    QCOMPARE(inverted->accessibleName().isEmpty() ? inverted->getName() : inverted->accessibleName(), QStringLiteral("Inverted"));
    QVERIFY(inverted->focusPolicy() & Qt::TabFocus);
    inverted->setFocus(Qt::TabFocusReason);
    QTest::keyClick(inverted, Qt::Key_Space);
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingColorMode()), QStringLiteral("inverted"));

    pdf::PDFReadingThemeCard* night = section->getPresetCard(QStringLiteral("night"));
    QVERIFY(night->focusPolicy() & Qt::TabFocus);
    night->setFocus(Qt::TabFocusReason);
    QTest::keyClick(night, Qt::Key_Return);
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("night"));

    // The contrast and the preview have accessible texts
    QVERIFY(section->getContrastRow()->getControlWidget()->accessibleName().startsWith(QStringLiteral("Contrast ")));
    QVERIFY(!section->getPreview()->accessibleName().isEmpty());
    QCOMPARE(section->getPreview()->getTexts().size(), 3);

    // The combo boxes of the profiles have the names of their rows
    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
    QVERIFY(RenderingTestHelper::waitForScan(RenderingTestHelper::colorManagement(fixture)));
    for (const QString& anchor : { QStringLiteral("output-color-profile"), QStringLiteral("device-cmyk-color-profile"), QStringLiteral("color-management-system") })
    {
        pdf::PDFComboBox* comboBox = RenderingTestHelper::comboBox(fixture, anchor);
        QVERIFY(comboBox);
        QCOMPARE(comboBox->accessibleName(), fixture.findRow(anchor)->getTitle());
    }
}

void AppSettingsRenderingTest::languageChange()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    RenderingTestHelper::click(RenderingTestHelper::colorModes(fixture)->getPresetCard(QStringLiteral("sepia")));

    // The page is built again in the new language, the click is written first
    RenderingTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    fixture.settle();
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("sepia"));
    QCOMPARE(section->getPresetCard(QStringLiteral("sepia"))->getName(), QString::fromUtf8("Sépia"));
    QVERIFY(section->getPresetCard(QStringLiteral("sepia"))->isSelected());
    QCOMPARE(section->getContrastRow()->getTitle(), QStringLiteral("Kontrast"));
    QCOMPARE(section->getContrastVerdictLabel()->text(), QString::fromUtf8("Vynikající"));

    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
    section = RenderingTestHelper::colorModes(fixture);
    QCOMPARE(section->getPresetCard(QStringLiteral("sepia"))->getName(), QStringLiteral("Sepia"));

    // The color management keeps its lists over the rebuild
    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
    QVERIFY(RenderingTestHelper::waitForScan(RenderingTestHelper::colorManagement(fixture)));
    QCoreApplication::installTranslator(&translator);
    fixture.settle();
    QVERIFY(RenderingTestHelper::waitForScan(RenderingTestHelper::colorManagement(fixture)));
    QVERIFY(RenderingTestHelper::comboBox(fixture, QStringLiteral("device-gray-color-profile"))->count() >= 6);
    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
}

void AppSettingsRenderingTest::reviewBrowseUntrustedFolder()
{
    // RFX5: Browse… of a folder never starts the dialog in a network or a device
    // path of the stored value (another computer, a backup) - a server, which does
    // not answer, would block the user interface; a local folder is the start
    pdftest::AppSettingsFixture fixture;
    QString start = QStringLiteral("not called");
    pdf::PDFAppFileDialogs::setFolderHandler([&start](QWidget*, const QString& folder) { start = folder; return QString(); });

    const QStringList hostile = { QStringLiteral("\\\\server-that-is-gone\\icc"), QStringLiteral("//server-that-is-gone/icc"),
                                  QStringLiteral("\\\\.\\PhysicalDrive0"), QStringLiteral("relative/folder") };
    for (const QString& value : hostile)
    {
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::cmsProfileDirectory(), value).isError());
        QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("additional-profile-directory"))->getControl();
        QCOMPARE(control->getLineEdit()->text(), value);
        start = QStringLiteral("not called");
        RenderingTestHelper::click(control->getBrowseButton());
        fixture.settle();
        QVERIFY2(start.isEmpty(), qPrintable(value));
        QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::cmsProfileDirectory()), value);
    }

    const QString folder = QDir(fixture.folder()).absoluteFilePath(QStringLiteral("profiles"));
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::cmsProfileDirectory(), folder).isError());
    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
    RenderingTestHelper::click(fixture.findRow(QStringLiteral("additional-profile-directory"))->getControl()->getBrowseButton());
    fixture.settle();
    QCOMPARE(start, folder);
}

void AppSettingsRenderingTest::moreColorsDialog()
{
    using Dialog = pdf::PDFColorPickerDialog;

    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);
    pdf::PDFColorButton* textButton = fixture.findRow(QStringLiteral("text-color"))->getControl()->getColorButton();
    QVERIFY(textButton);
    QVERIFY(pdf::PDFAppColorPicker::find(textButton));
    QVERIFY(pdf::PDFAppColorPicker::find(fixture.findRow(QStringLiteral("page-color"))->getControl()->getColorButton()));

    // The button → its palette → More colors… opens the dialog with the color of the setting
    auto open = [&]() -> Dialog*
    {
        if (!textButton->showPalette())
        {
            return nullptr;
        }
        fixture.settle();
        textButton->getPalette()->getMoreColorsLink()->click();
        fixture.settle();
        return pdf::PDFAppColorPicker::find(textButton)->getDialog();
    };
    QPointer<Dialog> dialog = open();
    QVERIFY(dialog);
    QVERIFY(!textButton->isPaletteVisible());
    QCOMPARE(Dialog::getHexName(dialog->getCurrentColor()), fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()));
    QCOMPARE(dialog->getScrimTarget(), fixture.window->getDialogHost());

    // Cancel writes nothing
    QSignalSpy changeSpy(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed);
    QTest::keyClick(dialog->getHexField(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(dialog->getHexField(), QStringLiteral("#ABCDEF"));
    dialog->getCancelButton()->click();
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(changeSpy.size(), 0);

    // OK writes the color as a color of the palette (the preset becomes Custom…), the
    // button shows it and the palette has it among the recent colors
    dialog = open();
    QVERIFY(dialog);
    QTest::keyClick(dialog->getHexField(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(dialog->getHexField(), QStringLiteral("#123456"));
    dialog->getOkButton()->click();
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesTextColor()), QStringLiteral("#123456"));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorModesCustomPreset()), QStringLiteral("custom"));
    QCOMPARE(textButton->getColor().rgb(), qRgb(0x12, 0x34, 0x56));
    QCOMPARE(textButton->getPalette()->getRecentColors().front().color.rgb(), qRgb(0x12, 0x34, 0x56));
    QCOMPARE(section->getPreview()->getTextColor(), QColor(QStringLiteral("#123456")));
    const qsizetype writes = changeSpy.size();
    QVERIFY(writes > 0);

    // The same color again writes nothing new
    changeSpy.clear();
    dialog = open();
    QVERIFY(dialog);
    QCOMPARE(dialog->getCurrentColor().rgb(), qRgb(0x12, 0x34, 0x56));
    dialog->getOkButton()->click();
    fixture.settle();
    QCOMPARE(changeSpy.size(), 0);

    // A disabled button opens nothing; the window deleted with the dialog open
    textButton->setEnabled(false);
    QVERIFY(!pdf::PDFAppColorPicker::find(textButton)->openDialog());
    textButton->setEnabled(true);
    dialog = open();
    QVERIFY(dialog);
    delete fixture.window.data();
    fixture.settle();
    QVERIFY(!dialog);
}

void AppSettingsRenderingTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: the sections of the package in both themes
    // (compare with 146:3132, 160:4203, 146:3260, 147:4565)
    if (qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS").isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    pdftest::AppSettingsFixture fixture(QSize(1440, 1500));
    fixture.window->getHomeTab()->getBanner()->hide();
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::viewingColorMode(), QStringLiteral("customColors")).isError());
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        for (const QString& sectionId : { QStringLiteral("viewing"), QStringLiteral("color-modes"), QStringLiteral("color-management"), QStringLiteral("advanced") })
        {
            QVERIFY(fixture.openSettings(sectionId));
            if (pdf::PDFAppColorManagementSettingsSection* section = RenderingTestHelper::colorManagement(fixture))
            {
                QVERIFY(RenderingTestHelper::waitForScan(section));
            }
            fixture.settle();
            fixture.screenshot(QStringLiteral("settings-%1-%2").arg(sectionId, suffix));
        }
    }
}

void AppSettingsRenderingTest::previewGeometry()
{
    // text-preview 160:4430 at the width of the frame (panel 808): the caption 13 high
    // 14 below the divider, the panel 160:4432 8 below it and 117 high (paddings
    // 18 / 16 and its border, two lines of the paragraph 40), the row ends 16 below
    // the panel; the lines start at the leading edge also in a right-to-left interface
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    fixture.settle();
    pdf::PDFAppColorModesSettingsSection* section = RenderingTestHelper::colorModes(fixture);
    QVERIFY(section);
    pdf::PDFAppColorModePreview* preview = section->getPreview();
    pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("color-preview"));
    QWidget* caption = section->findChild<QWidget*>(QStringLiteral("previewCaption"));
    QVERIFY(preview && row && caption);
    QCOMPARE(preview->width(), 808);
    QCOMPARE(preview->heightForWidth(808), 117);
    QCOMPARE(preview->height(), 117);
    QCOMPARE(caption->height(), 13);
    QCOMPARE(caption->mapTo(row, QPoint(0, 0)).y(), 1 + 14);
    QCOMPARE(preview->mapTo(row, QPoint(0, 0)).y(), 1 + 14 + 13 + 8);
    QCOMPARE(row->height() - preview->mapTo(row, QPoint(0, 0)).y() - preview->height(), 16);

    // The heading in the text color (#FFB000 by default): from the leading edge
    auto headingInk = [preview]()
    {
        const QImage image = preview->grab().toImage().convertToFormat(QImage::Format_RGB32);
        int left = image.width();
        int right = -1;
        for (int y = pdf::PDFAppColorModePreview::PADDING_VERTICAL; y < pdf::PDFAppColorModePreview::PADDING_VERTICAL + 18; ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                const QColor color(image.pixel(x, y));
                if (color.red() > 200 && color.green() > 120 && color.blue() < 80)
                {
                    left = qMin(left, x);
                    right = qMax(right, x);
                }
            }
        }
        return std::make_pair(left, right);
    };
    const std::pair<int, int> leftToRight = headingInk();
    QVERIFY(leftToRight.first >= pdf::PDFAppColorModePreview::PADDING_HORIZONTAL && leftToRight.first <= pdf::PDFAppColorModePreview::PADDING_HORIZONTAL + 2);
    QVERIFY(leftToRight.second < preview->width() / 2);
    preview->setLayoutDirection(Qt::RightToLeft);
    const std::pair<int, int> rightToLeft = headingInk();
    preview->setLayoutDirection(Qt::LeftToRight);
    QVERIFY(rightToLeft.second < preview->width() - pdf::PDFAppColorModePreview::PADDING_HORIZONTAL);
    QVERIFY(rightToLeft.second >= preview->width() - pdf::PDFAppColorModePreview::PADDING_HORIZONTAL - 3);
    QVERIFY(rightToLeft.first > preview->width() / 2);
}

void AppSettingsRenderingTest::openLists()
{
    // Open lists of 944:8912 (General 944:8916, Color management 944:9100, Advanced
    // 944:9281): the panel 4 px below (or above) its box, at least as wide as the box,
    // the items of the definitions; PDF4QT_APP_SCREENSHOTS writes each open list.
    // The window fits into the screen of offscreen (800 high), each box is scrolled
    // near the top of the page, so every list opens below its box.
    pdftest::AppSettingsFixture fixture(QSize(1440, 760));
    const QString folder = qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS");
    int listCount = 0;
    for (const QString& sectionId : { QStringLiteral("general"), QStringLiteral("color-management"), QStringLiteral("advanced") })
    {
        QVERIFY(fixture.openSettings(sectionId));
        if (pdf::PDFAppColorManagementSettingsSection* section = RenderingTestHelper::colorManagement(fixture))
        {
            QVERIFY(RenderingTestHelper::waitForScan(section));
        }
        fixture.settle();

        for (pdf::PDFAppSettingRow* row : fixture.settingsPage()->getSection()->getRows())
        {
            pdf::PDFComboBox* combo = row->getControl() ? row->getControl()->getComboBox() : nullptr;
            if (!combo || !combo->isEnabled())
            {
                continue;
            }

            if (QScrollArea* area = RenderingTestHelper::scrollArea(combo))
            {
                area->verticalScrollBar()->setValue(combo->mapTo(area->widget(), QPoint(0, 0)).y() - 100);
            }
            fixture.settle();
            combo->showPopup();
            fixture.settle();
            QWidget* popup = combo->view()->window();
            QVERIFY(popup->isVisible());

            const QRect panel = QRect(popup->mapToGlobal(QPoint(0, 0)), popup->size()).marginsRemoved(pdf::PDFWidgetStyle::getPopoverMargins(popup));
            const QRect box(combo->mapToGlobal(QPoint(0, 0)), combo->size());
            // A long list at the end of the page opens above its box: QComboBox moves the
            // bottom of the window to the top of the popup rectangle of the style, so the
            // panel ends 3 px above the box (1 px closer than below - a finding for the
            // Fluent style, SC_ComboBoxListBoxPopup)
            QVERIFY2(panel.top() == box.bottom() + 1 + 4 || (box.top() - panel.bottom() - 1 >= 3 && box.top() - panel.bottom() - 1 <= 4),
                     qPrintable(QStringLiteral("%1: panel %2 / %3, box %4 / %5, screen %6").arg(row->getAnchor()).arg(panel.top()).arg(panel.bottom()).arg(box.top()).arg(box.bottom()).arg(combo->screen()->availableGeometry().height())));
            QVERIFY(panel.width() >= box.width());
            QVERIFY(combo->maxVisibleItems() <= 10);
            QCOMPARE(RenderingTestHelper::countLeakPixels(popup->grab().toImage()), 0);
            if (!folder.isEmpty())
            {
                popup->grab().save(QDir(folder).absoluteFilePath(QStringLiteral("settings-list-%1.png").arg(row->getAnchor())));
            }

            combo->hidePopup();
            fixture.settle();
            ++listCount;
        }
    }
    QCOMPARE(listCount, 3 + 8 + 2);
}

QTEST_MAIN(AppSettingsRenderingTest)
#include "tst_appsettingsrenderingtest.moc"
