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

// The page Settings of the new application (step 5, wave 0 and package A): routes
// and sections, the generic rows of every definition and their binders, hostile
// stored values, changes of another instance, pending values written when the page
// is left, the dialog Restore defaults, anchors, the filter /settings?q=, the global
// search to a row, deletion, themes and scales, right-to-left, the minimal window,
// keyboard and accessibility, language change. PDF4QT_APP_SCREENSHOTS=<folder>
// writes pictures of the sections.

#include "appsettingsfixture.h"

#include "pdfappcommandline.h"
#include "pdfappcontext.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfappmainwindow.h"
#include "pdfappnavigator.h"
#include "pdfapppagenotfound.h"
#include "pdfapprestoredefaultsdialog.h"
#include "pdfapproute.h"
#include "pdfappdocumentpanel.h"
#include "pdfappdocumentmenus.h"
#include "pdfappdocumentsidebar.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfappdocumentview.h"
#include "pdfapphelpcatalog.h"
#include "pdfapphelppage.h"
#include "pdfapplocale.h"
#include "pdfappsettingssection_general.h"
#include "pdfapptranslator.h"
#include "pdfappwhatsnewpage.h"
#include "pdfdatabasenotifier.h"
#include "pdfrecentrepository.h"
#include "pdftoolrepository.h"
#include "pdfwhatsnew.h"
#include "pdfappsearchengine.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfappsettingspage.h"
#include "pdfappsettingssection.h"
#include "pdfapptab.h"
#include "pdfappworkspace.h"
#include "pdfdatabase.h"
#include "pdfdatabasestatement.h"
#include "pdfdrawspacecontroller.h"
#include "pdfsettingdefinition.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"
#include "pdfshortcutrepository.h"

#include "pdfcomponentbadges.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"

#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLabel>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTranslator>
#include <QtTest>

#include <atomic>
#include <cmath>
#include <limits>
#include <thread>

/// Helpers of the test of the page Settings
class SettingsTestHelper
{
public:
    SettingsTestHelper() = delete;

    /// Stores a raw value (a hostile row of another version) without validation
    static bool storeRaw(pdf::PDFAppContext* context, const QString& key, const QVariant& value)
    {
        pdf::PDFDatabaseConnection* connection = context->getDatabase()->getConnection();
        pdf::PDFDatabaseStatement statement(connection, "INSERT OR REPLACE INTO setting(key, value, modified) VALUES (?1, ?2, 0)");
        statement.bindText(1, key);
        statement.bindVariant(2, value);
        return statement.execute();
    }

    /// A valid value of the definition, which is not its default
    static QVariant otherValue(const pdf::PDFSettingDefinition& definition)
    {
        using Type = pdf::PDFSettingDefinition::Type;
        using Control = pdf::PDFSettingDefinition::Control;

        switch (definition.getType())
        {
            case Type::Bool:
                return QVariant(!definition.getDefaultValue().toBool());

            case Type::Integer:
            {
                const qint64 minimum = definition.getMinimum().toLongLong();
                const qint64 maximum = qMin(definition.getMaximum().toLongLong(), minimum + 1000000);
                qint64 value = minimum + (maximum - minimum) / 2;
                if (value == definition.getDefaultValue().toLongLong())
                {
                    value = value + 1 <= maximum ? value + 1 : value - 1;
                }
                return QVariant(value);
            }

            case Type::Real:
            {
                const double factor = std::pow(10.0, definition.getDecimals());
                double value = std::round((definition.getMinimum().toDouble() + definition.getMaximum().toDouble()) * 0.5 * factor) / factor;
                if (qFuzzyCompare(value, definition.getDefaultValue().toDouble()))
                {
                    value += 1.0 / factor;
                }
                return QVariant(value);
            }

            case Type::Option:
                for (const pdf::PDFSettingOption& option : definition.getOptions())
                {
                    if (option.id != definition.getDefaultValue().toString())
                    {
                        return QVariant(option.id);
                    }
                }
                return QVariant();

            case Type::String:
                if (definition.getControl() == Control::Color)
                {
                    return QVariant(QStringLiteral("#123456"));
                }
                return QVariant(QStringLiteral("abc"));
        }
        return QVariant();
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

    static int historyCount(const pdftest::AppSettingsFixture& fixture)
    {
        return fixture.window->getCurrentTab()->getHistory().getCount();
    }

    static QString currentRoute(const pdftest::AppSettingsFixture& fixture)
    {
        const pdf::PDFAppHistoryEntry* entry = fixture.window->getCurrentTab()->getHistory().getCurrentEntry();
        return entry ? entry->route.toString() : QString();
    }
};

/// Translator of the language test: a few texts of the page and of the definitions
class SettingsTestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray source(sourceText ? sourceText : "");
        const QByteArray contextText(context ? context : "");
        if (contextText == "pdf::PDFAppSettingsSectionRegistry" && source == "Viewing")
        {
            return QStringLiteral("Ansicht");
        }
        if (contextText == "pdf::PDFSettingDefinitions" && source == "Antialiasing")
        {
            return QStringLiteral("Kantenglättung");
        }
        if (contextText == "pdf::PDFAppSettingsPage" && source == "Settings")
        {
            return QStringLiteral("Einstellungen");
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

class AppSettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void routesAndSections();
    void genericRowEachType();
    void hostileStoredValues();
    void otherInstanceChange();
    void pendingValueWrittenOnLeave();
    void restoreDefaultsDialog();
    void anchorHighlight();
    void filterQuery();
    void filterHostileTexts();
    void globalSearchToRow();
    void deletionMatrix();
    void deleteFromOwnSignals();
    void themesAndScales();
    void rightToLeft();
    void minimalWindow();
    void keyboardAndAccessibility();
    void languageChange();
    void documentViewCoalescing();
    void reviewShellGeometry();
    void reviewEchoKeepsTyping();
    void reviewSectionDeletedWithPendingValue();
    void reviewDocumentViewCoalescing();
    void reviewRestoreTopics();
    void reviewAnchorTargets();
    void reviewBlockRowPadding();
    void reviewGroupGrowsAtOnce();
    void reviewDialogsOverLanguageChange();
    void reviewFormatStoredText();
    void generalRowsWriteAndApply();
    void languageSwitchLive();
    void languageSwitchWithWorkers();
    void languageChangeEverywhere();
    void languageFromOtherInstance();
    void helpInUiLanguage();
    void colorSchemeRow();
    void recentLimitTrims();
    void toolHistoryClear();
    void generalHostileStoredValues();
    void generalSearchAndAnchors();
    void generalDeletionMatrix();
    void generalDeleteFromOwnSignals();
    void generalThemesAndScales();
    void generalRightToLeft();
    void generalMinimalWindow();
    void generalKeyboardAndAccessibility();
    void generalLanguageChange();
    void reviewLanguageCommitOnActivation();
    void reviewConfirmationFocus();
    void reviewPageDestroyedWritesSilently();
    void screenshots();
};

void AppSettingsTest::initTestCase()
{
    // Automatic is English in every test (genericRowEachType writes it), whatever
    // the language of the system running the test is (a Czech Windows)
    pdf::PDFAppTranslator::setSystemLanguages({ QStringLiteral("en-US") });
    QApplication::setStyle(new pdf::PDFWidgetStyle());
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppSettingsTest::routesAndSections()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppWorkspaceTab* homeTab = fixture.window->getHomeTab();

    // Every section of the registry (General … Advanced, Storage) has its route,
    // its highlighted nav item and its header; the page has no workspace header
    const std::vector<pdf::PDFAppSettingsSectionInfo>& sections = pdf::PDFAppSettingsSectionRegistry::getSections();
    QCOMPARE(int(sections.size()), 10);
    QCOMPARE(pdf::PDFAppSettingsSectionRegistry::getSectionIds(), QStringList({ "general", "viewing", "color-modes", "editing", "security", "speech",
                                                                                "shortcuts", "color-management", "advanced", "storage" }));
    for (int i = 0; i < int(sections.size()); ++i)
    {
        const pdf::PDFAppSettingsSectionInfo& info = sections[size_t(i)];
        pdf::PDFAppSettingsPage* page = fixture.openSettings(info.id);
        QVERIFY2(page, qPrintable(info.id));
        QCOMPARE(page->getSectionId(), info.id);
        QVERIFY(!page->isFiltering());
        QCOMPARE(page->getNavigation()->getCurrentIndex(), i);
        QCOMPARE(page->getNavigation()->getCount(), 10);
        QCOMPARE(page->getSectionHeader()->getTitleLabel()->text(), info.getTitle());
        QCOMPARE(page->getSectionHeader()->getIconBadge()->getIconName(), info.iconName);
        QVERIFY(page->getSection());
        QVERIFY(!homeTab->getHeader()->isVisible());
        QCOMPARE(fixture.window->getHomeTab()->getNavItem(), pdf::PDFAppNavItem::Settings);

        // Storage has no Restore defaults; the other sections have it (disabled without changes)
        if (info.id == pdf::PDFSettingDefinitions::sectionStorage())
        {
            QVERIFY(!page->getRestoreButton());
        }
        else
        {
            QVERIFY(page->getRestoreButton());
            QVERIFY(!page->getRestoreButton()->isEnabled());
        }

        // The sections with definitions show one row per visible definition (custom
        // rows of a package - Tool history of General - have no definition, the
        // block rows of Color modes show a definition by their cards)
        const std::vector<const pdf::PDFSettingDefinition*> definitions = fixture.context->getSettingRegistry()->getSectionDefinitions(info.id);
        qsizetype definitionRows = 0;
        for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
        {
            definitionRows += row->getDefinition().has_value() ? 1 : 0;
        }
        QVERIFY(definitionRows <= qsizetype(definitions.size()));
        for (const pdf::PDFSettingDefinition* definition : definitions)
        {
            QVERIFY2(page->getSection()->findRow(definition->getAnchor()), qPrintable(definition->getKey()));
        }
    }

    // /settings is General, an unknown section is Page Not Found
    fixture.navigate(QStringLiteral("/settings"));
    QVERIFY(fixture.settingsPage());
    QCOMPARE(fixture.settingsPage()->getSectionId(), QStringLiteral("general"));
    fixture.navigate(QStringLiteral("/settings/foreign"));
    QVERIFY(qobject_cast<pdf::PDFPageNotFoundPage*>(fixture.window->getCurrentTab()->getPage()));
    fixture.navigate(QStringLiteral("/settings/general/extra"));
    QVERIFY(!fixture.settingsPage());

    // A nav item navigates (Push), Back returns
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("general"));
    const int count = SettingsTestHelper::historyCount(fixture);
    QTest::mouseClick(page->getNavigationItem(QStringLiteral("viewing")), Qt::LeftButton);
    fixture.settle();
    QVERIFY(fixture.settingsPage());
    QCOMPARE(fixture.settingsPage()->getSectionId(), QStringLiteral("viewing"));
    QCOMPARE(SettingsTestHelper::historyCount(fixture), count + 1);
    fixture.window->getNavigator()->goBack(fixture.window->getCurrentTab());
    fixture.settle();
    QCOMPARE(fixture.settingsPage()->getSectionId(), QStringLiteral("general"));

    // The groups of General follow 146:2843 (Application, Documents, Tools, Updates)
    QStringList groups;
    for (pdf::PDFAppSettingGroup* group : fixture.settingsPage()->getSection()->getGroups())
    {
        groups << group->getKey();
    }
    QCOMPARE(groups, QStringList({ "Application", "Documents", "Tools", "Updates" }));
    QCOMPARE(fixture.settingsPage()->getSection()->getRows().front()->getAnchor(), QStringLiteral("language"));

    // The callers of the old steps land on the page: the info bar Security Settings,
    // the View panel Rendering settings, the What's New link (step 2)
    QVERIFY(fixture.openSettings(QStringLiteral("security"), QStringLiteral("allow-uri-link-launch")));
    QVERIFY(fixture.findRow(QStringLiteral("allow-uri-link-launch"))->isHighlighted());
    QVERIFY(fixture.openSettings(QStringLiteral("general"), QStringLiteral("check-for-updates")));
    QVERIFY(fixture.findRow(QStringLiteral("check-for-updates")));

    // The footer of the portable mode (941:138326)
    QCOMPARE(fixture.settingsPage()->getFooterNote()->text(), QStringLiteral("Changes apply at once and are kept until PDF4QT closes."));
}

void AppSettingsTest::genericRowEachType()
{
    pdftest::AppSettingsFixture fixture;

    // The dependencies are fulfilled, so every control is enabled
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::annotationsAuthorMode(), pdf::PDFSettingValues::authorModeCustomName()).isError());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::advancedDeveloperMode(), true).isError());

    int rows = 0;
    for (const QString& sectionId : pdf::PDFAppSettingsSectionRegistry::getSectionIds())
    {
        pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
        QVERIFY(page);
        for (const pdf::PDFSettingDefinition* definition : fixture.context->getSettingRegistry()->getSectionDefinitions(sectionId))
        {
            pdf::PDFAppSettingRow* row = fixture.findRow(definition->getAnchor());
            QVERIFY2(row, qPrintable(definition->getKey()));
            if (!row->getControl() || row->getControl()->getKind() != pdf::PDFAppSettingControl::getKind(*definition))
            {
                // A section of a package shows the setting by its own control (the
                // cards of the color modes, the combo boxes of the profiles) - tested
                // by the test of the package
                ++rows;
                continue;
            }
            QCOMPARE(row->getTitle(), definition->getTitle());
            QCOMPARE(row->getDescription(), definition->getDescription());
            QCOMPARE(row->getControl()->getEditor()->accessibleName(), definition->getTitle());
            if (definition->hasEnabledWhen())
            {
                // An earlier row may have changed the setting of the dependency
                QVERIFY(!fixture.settings().setValue(definition->getEnabledWhenKey(), definition->getEnabledWhenValue()).isError());
            }
            QTRY_VERIFY2(row->getControlWrap()->isEnabled(), qPrintable(definition->getKey()));

            // The control shows the stored value, the value of the user is stored and read back
            QCOMPARE(row->getControl()->getShownValue().toString(), fixture.settings().getValue(definition->getKey()).toString());

            const QVariant value = SettingsTestHelper::otherValue(*definition);
            QVERIFY2(value.isValid(), qPrintable(definition->getKey()));
            QVERIFY2(pdftest::AppSettingsFixture::setControlValue(row, value), qPrintable(definition->getKey()));
            QTRY_VERIFY2(!row->getControl()->hasPendingValue(), qPrintable(definition->getKey()));
            const QVariant stored = fixture.settings().getValue(definition->getKey());
            QVERIFY2(stored == *definition->normalize(value), qPrintable(definition->getKey() + QStringLiteral(": ") + stored.toString() + QStringLiteral(" != ") + value.toString()));
            QVERIFY2(sectionId == pdf::PDFSettingDefinitions::sectionStorage() || (page->getRestoreButton() && page->getRestoreButton()->isEnabled()), qPrintable(definition->getKey()));
            ++rows;
        }
    }
    QVERIFY(rows > 60);

    // A value of the slider, of a unit, of decimals: the value text
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("speech"));
    QVERIFY(page);
    pdf::PDFAppSettingRow* volume = fixture.findRow(QStringLiteral("volume"));
    QCOMPARE(volume->getControl()->getKind(), pdf::PDFAppSettingControl::Kind::Slider);
    QCOMPARE(volume->getControl()->getValueLabel()->text(), QStringLiteral("0.50"));
    QVERIFY(fixture.openSettings(QStringLiteral("viewing")));
    QCOMPARE(fixture.findRow(QStringLiteral("magnifier-zoom"))->getControl()->getDoubleSpinBox()->getDisplaySuffix(), QString::fromUtf8("×"));

    // Formatting of the values (Restore defaults)
    const pdf::PDFSettingRegistry* registry = fixture.context->getSettingRegistry();
    QCOMPARE(pdf::PDFAppSettingControl::formatValue(*registry->findDefinition(pdf::PDFSettingKeys::magnifierZoom()), 3.0), QString::fromUtf8("3.0 ×"));
    QCOMPARE(pdf::PDFAppSettingControl::formatValue(*registry->findDefinition(pdf::PDFSettingKeys::viewingWheelSpeedVertical()), qint64(150)), QStringLiteral("150 %"));
    QCOMPARE(pdf::PDFAppSettingControl::formatValue(*registry->findDefinition(pdf::PDFSettingKeys::renderingSmoothImages()), false), QStringLiteral("Off"));
    QCOMPARE(pdf::PDFAppSettingControl::formatValue(*registry->findDefinition(pdf::PDFSettingKeys::colorScheme()), QStringLiteral("dark")), QStringLiteral("Dark"));
    QCOMPARE(pdf::PDFAppSettingControl::formatValue(*registry->findDefinition(pdf::PDFSettingKeys::annotationsCustomAuthorName()), QString()), QStringLiteral("(empty)"));

    // A dependency disables its row: Custom author name only for Custom name
    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    pdf::PDFAppSettingRow* mode = fixture.findRow(QStringLiteral("author-of-new-annotations"));
    pdf::PDFAppSettingRow* name = fixture.findRow(QStringLiteral("custom-author-name"));
    QVERIFY(name->getControlWrap()->isEnabled());
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(mode, pdf::PDFSettingValues::authorModeAnonymous()));
    QTRY_VERIFY(!name->getControlWrap()->isEnabled());
    QVERIFY(!pdftest::AppSettingsFixture::setControlValue(name, QStringLiteral("x")));

    // The developer rows follow the Developer mode, the profiles the system
    QVERIFY(fixture.openSettings(QStringLiteral("advanced")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("developer-mode")), false));
    QTRY_VERIFY(!fixture.findRow(QStringLiteral("display-text-blocks"))->getControlWrap()->isEnabled());
    QVERIFY(fixture.openSettings(QStringLiteral("color-management")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("color-management-system")), QStringLiteral("generic")));
    QTRY_VERIFY(!fixture.findRow(QStringLiteral("additional-profile-directory"))->getControlWrap()->isEnabled());
    QVERIFY(fixture.findRow(QStringLiteral("color-management-system"))->getControlWrap()->isEnabled());
}

void AppSettingsTest::hostileStoredValues()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppContext* context = fixture.context.get();

    // Rows of another version: wrong types, out of the range, unknown options,
    // 64 kB texts, non-finite numbers - the page shows the defaults and writes nothing
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::renderingAntialiasing(), QStringLiteral("yes")));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::renderingTextAntialiasing(), qint64(7)));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::magnifierSize(), qint64(99999)));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::magnifierZoom(), QStringLiteral("nan")));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::viewingWheelSpeedVertical(), 1.0e300));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::colorScheme(), QStringLiteral("sepia")));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::appearanceLanguage(), QString(65536, QLatin1Char('x'))));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::annotationsCustomAuthorName(), QString(65536, QLatin1Char('y'))));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::colorModesTextColor(), QStringLiteral("javascript:alert(1)")));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::colorModesBitonalThreshold(), QByteArray("\x00\x01\x02", 3)));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::speechVolume(), -1.0));
    QVERIFY(SettingsTestHelper::storeRaw(context, pdf::PDFSettingKeys::cmsSystem(), QVariant()));

    QVERIFY(fixture.openSettings(QStringLiteral("viewing")));
    QCOMPARE(fixture.findRow(QStringLiteral("antialiasing"))->getControl()->getShownValue(), QVariant(true));
    QCOMPARE(fixture.findRow(QStringLiteral("text-antialiasing"))->getControl()->getShownValue(), QVariant(true));
    QCOMPARE(fixture.findRow(QStringLiteral("magnifier-size"))->getControl()->getShownValue(), QVariant(qint64(100)));
    QCOMPARE(fixture.findRow(QStringLiteral("magnifier-zoom"))->getControl()->getShownValue().toDouble(), 2.0);
    QCOMPARE(fixture.findRow(QStringLiteral("vertical-wheel-speed"))->getControl()->getShownValue(), QVariant(qint64(100)));
    QVERIFY(!fixture.settingsPage()->getRestoreButton()->isEnabled());

    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    QCOMPARE(fixture.findRow(QStringLiteral("color-scheme"))->getControl()->getShownValue(), QVariant(QStringLiteral("auto")));
    QCOMPARE(fixture.findRow(QStringLiteral("language"))->getControl()->getShownValue(), QVariant(QStringLiteral("en")));
    QVERIFY(!fixture.findRow(QStringLiteral("language"))->getControl()->isNotAvailableShown());

    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    QCOMPARE(fixture.findRow(QStringLiteral("custom-author-name"))->getControl()->getShownValue(), QVariant(QString()));

    QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
    QCOMPARE(fixture.findRow(QStringLiteral("text-color"))->getControl()->getColorButton()->getColor(), QColor(QStringLiteral("#FFB000")));
    QCOMPARE(fixture.findRow(QStringLiteral("black-and-white-threshold"))->getControl()->getShownValue(), QVariant(qint64(128)));

    QVERIFY(fixture.openSettings(QStringLiteral("speech")));
    QCOMPARE(fixture.findRow(QStringLiteral("volume"))->getControl()->getShownValue().toDouble(), 1.0);

    // Nothing has been written back: the hostile rows stay as they are
    QCOMPARE(fixture.settings().getStoredValue(pdf::PDFSettingKeys::colorScheme()), QVariant(QStringLiteral("sepia")));
    QCOMPARE(fixture.settings().getStoredValue(pdf::PDFSettingKeys::magnifierSize()), QVariant(qint64(99999)));

    // A stored option, which an options provider does not offer, is "(not available)"
    class Provider : public pdf::PDFSettingOptionsProvider
    {
    public:
        virtual std::vector<pdf::PDFSettingOption> getOptions(const pdf::PDFSettingDefinition&) const override
        {
            return { { QStringLiteral("voice-a"), QStringLiteral("Voice A"), true }, { QStringLiteral("voice-b"), QStringLiteral("Voice B"), true } };
        }
    };
    const pdf::PDFSettingDefinition voice = pdf::PDFSettingDefinition::createOption(QStringLiteral("test.voice"), QStringLiteral("voice-a"), std::make_shared<Provider>())
                                                .setSection(QStringLiteral("test")).setAnchor(QStringLiteral("voice")).setTitle("Voice");
    {
        QWidget host;
        pdf::PDFAppSettingControl control(nullptr, voice, &host);
        QCOMPARE(control.getComboBox()->count(), 2);
        QVERIFY(!control.isNotAvailableShown());
    }
}

void AppSettingsTest::otherInstanceChange()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page);

    // Another instance changes the values: the controls follow without writing back
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::renderingAntialiasing(), false));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("antialiasing"))->getControl()->getShownValue(), QVariant(false));
    QVERIFY(page->getRestoreButton()->isEnabled());
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::magnifierZoom(), 3.5));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("magnifier-zoom"))->getControl()->getShownValue().toDouble(), 3.5);
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::renderingAntialiasing(), QVariant()));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("antialiasing"))->getControl()->getShownValue(), QVariant(true));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::magnifierZoom(), QVariant()));
    QTRY_VERIFY(!page->getRestoreButton()->isEnabled());

    // A value being typed (focus, pending) is kept over a change of another instance
    pdf::PDFAppSettingControl* size = fixture.findRow(QStringLiteral("magnifier-size"))->getControl();
    QLineEdit* sizeEdit = size->getSpinBox()->findChild<QLineEdit*>();
    size->getSpinBox()->setFocus();
    QTest::keyClick(sizeEdit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(sizeEdit, QStringLiteral("250"));
    QVERIFY(size->hasPendingValue());
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::magnifierSize(), qint64(300)));
    QCOMPARE(size->getSpinBox()->value(), 250);
    QTest::keyClick(sizeEdit, Qt::Key_Return);
    QTRY_COMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(250));

    // A dependency changed by another instance
    QVERIFY(fixture.openSettings(QStringLiteral("advanced")));
    QVERIFY(!fixture.findRow(QStringLiteral("display-text-lines"))->getControlWrap()->isEnabled());
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::advancedDeveloperMode(), true));
    QTRY_VERIFY(fixture.findRow(QStringLiteral("display-text-lines"))->getControlWrap()->isEnabled());
}

void AppSettingsTest::pendingValueWrittenOnLeave()
{
    // Typed, not confirmed: written when the page is left
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("viewing")));
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("horizontal-wheel-speed"))->getControl();
        control->getSpinBox()->setFocus();
        QLineEdit* edit = control->getSpinBox()->findChild<QLineEdit*>();
        QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(edit, QStringLiteral("230"));
        QVERIFY(control->hasPendingValue());
        fixture.navigate(QStringLiteral("/home"));
        QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::viewingWheelSpeedHorizontal()), qint64(230));
    }

    // ... after the rest of WRITE_DELAY
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("editing")));
        QVERIFY(fixture.settings().setValue(pdf::PDFSettingKeys::annotationsAuthorMode(), pdf::PDFSettingValues::authorModeCustomName()).isError() == false);
        fixture.settle();
        QTRY_VERIFY(fixture.findRow(QStringLiteral("custom-author-name"))->getControlWrap()->isEnabled());
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("custom-author-name"))->getControl();
        control->getLineEdit()->setFocus();
        QTest::keyClicks(control->getLineEdit(), QStringLiteral("Jakub"));
        QVERIFY(control->hasPendingValue());
        QTRY_COMPARE(fixture.settings().getString(pdf::PDFSettingKeys::annotationsCustomAuthorName()), QStringLiteral("Jakub"));
        QVERIFY(!control->hasPendingValue());
    }

    // ... when the window is deleted, when the tab is left for a document
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("viewing")));
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("magnifier-size"))->getControl();
        control->getSpinBox()->setFocus();
        QLineEdit* edit = control->getSpinBox()->findChild<QLineEdit*>();
        QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(edit, QStringLiteral("123"));
        QVERIFY(fixture.openDocument());
        QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(123));

        fixture.window->activateTab(fixture.window->getHomeTab());
        fixture.settle();
        control = fixture.findRow(QStringLiteral("magnifier-size"))->getControl();
        control->getSpinBox()->setFocus();
        edit = control->getSpinBox()->findChild<QLineEdit*>();
        QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(edit, QStringLiteral("321"));
        delete fixture.window.data();
        fixture.settle();
        QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(321));
    }

    // ... a slider moved by the keys (no editingFinished, the value waits for
    // WRITE_DELAY): written when the tab is left - by the hide of the page, in the
    // next turn of the event loop (FX5-2: not inside of the hide traversal of Qt,
    // whose slots may delete the widgets of the page), long before WRITE_DELAY -
    // and at once when the page is deleted (review RT5)
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppDocumentTab* tab = fixture.openDocument();
        QVERIFY(tab);
        fixture.window->activateTab(fixture.window->getHomeTab());
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("black-and-white-threshold"))->getControl();
        QCOMPARE(control->getKind(), pdf::PDFAppSettingControl::Kind::Slider);
        control->getSlider()->setFocus();
        QTest::keyClick(control->getSlider(), Qt::Key_Right);
        QVERIFY(control->hasPendingValue());
        fixture.window->activateTab(tab);
        QCoreApplication::processEvents();
        QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::colorModesBitonalThreshold()), qint64(129));
        QVERIFY(!control->hasPendingValue());

        fixture.window->activateTab(fixture.window->getHomeTab());
        fixture.settle();
        control = fixture.findRow(QStringLiteral("black-and-white-threshold"))->getControl();
        control->getSlider()->setFocus();
        QTest::keyClick(control->getSlider(), Qt::Key_Right);
        QVERIFY(control->hasPendingValue());
        delete fixture.settingsPage();
        QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::colorModesBitonalThreshold()), qint64(130));
        fixture.settle();
    }
}

void AppSettingsTest::restoreDefaultsDialog()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFSettingsRepository settings = fixture.settings();
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::viewingWheelSpeedVertical(), 150).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingSmoothImages(), false).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingIgnoreOptionalContent(), true).isError());
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::colorScheme(), QStringLiteral("dark")).isError());

    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page);
    QVERIFY(page->getRestoreButton()->isEnabled());

    // 941:135918: four rows "title: current → default", Cancel is the default
    QTest::mouseClick(page->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QPointer<pdf::PDFAppRestoreDefaultsDialog> dialog = page->getRestoreDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QCOMPARE(dialog->getChangeCount(), 4);
    QCOMPARE(dialog->getFacts()->getRowCount(), 4);
    QStringList values;
    for (int i = 0; i < dialog->getFacts()->getRowCount(); ++i)
    {
        values << dialog->getFacts()->getKey(i) + QStringLiteral(": ") + dialog->getFacts()->getValue(i);
    }
    QVERIFY2(values.contains(QString::fromUtf8("Magnifier zoom: 3.0 × → 2.0 ×")), qPrintable(values.join(QLatin1Char('|'))));
    QVERIFY(values.contains(QString::fromUtf8("Vertical wheel speed: 150 % → 100 %")));
    QVERIFY(values.contains(QString::fromUtf8("Smooth pictures: Off → On")));
    QVERIFY(values.contains(QString::fromUtf8("Ignore optional content: On → Off")));
    QVERIFY(dialog->getRestoreButton()->isEnabled());
    QVERIFY(dialog->getCancelButton()->isDefault());

    // Another instance changes a setting of the section meanwhile: the rows follow
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::magnifierSize(), qint64(150)));
    QTRY_COMPARE(dialog->getChangeCount(), 5);

    // Cancel changes nothing
    QTest::mouseClick(dialog->getCancelButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(settings.getChangedKeys(QStringLiteral("viewing")).size(), 5);

    // Restore Defaults restores the section in one transaction, the other sections stay
    QVERIFY(page->openRestoreDefaultsDialog());
    dialog = page->getRestoreDialog();
    QTest::mouseClick(dialog->getRestoreButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(!dialog);
    QVERIFY(settings.getChangedKeys(QStringLiteral("viewing")).isEmpty());
    QCOMPARE(settings.getString(pdf::PDFSettingKeys::colorScheme()), QStringLiteral("dark"));
    QTRY_VERIFY(!page->getRestoreButton()->isEnabled());
    QTRY_COMPARE(fixture.findRow(QStringLiteral("magnifier-zoom"))->getControl()->getShownValue().toDouble(), 2.0);
    QVERIFY(!page->openRestoreDefaultsDialog());

    // Another instance restores everything while the dialog is open: Restore is disabled
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::magnifierZoom(), 4.0).isError());
    fixture.settle();
    QVERIFY(page->openRestoreDefaultsDialog());
    dialog = page->getRestoreDialog();
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::magnifierZoom(), QVariant()));
    QTRY_COMPARE(dialog->getChangeCount(), 0);
    QVERIFY(!dialog->getRestoreButton()->isEnabled());
    dialog->reject();
    fixture.settle();

    // Storage has no Restore defaults
    page = fixture.openSettings(pdf::PDFSettingDefinitions::sectionStorage());
    QVERIFY(!page->openRestoreDefaultsDialog());
}

void AppSettingsTest::anchorHighlight()
{
    pdftest::AppSettingsFixture fixture(QSize(1440, 600));
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("advanced"), QStringLiteral("display-text-lines"));
    QVERIFY(page);
    pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("display-text-lines"));
    QVERIFY(row->isHighlighted());

    // The row is scrolled into the view
    QScrollArea* area = SettingsTestHelper::scrollArea(page);
    QVERIFY(area);
    QTRY_VERIFY(area->verticalScrollBar()->value() > 0);
    const QRect rowRect(row->mapTo(area->viewport(), QPoint(0, 0)), row->size());
    QVERIFY(area->viewport()->rect().intersects(rowRect));

    // The highlight ends after HIGHLIGHT_DURATION
    QTRY_VERIFY_WITH_TIMEOUT(!row->isHighlighted(), pdf::PDFAppSettingRow::HIGHLIGHT_DURATION * 2 * pdftest::AppDocumentFixture::getTimeoutFactor());

    // An unknown anchor shows the top of the section
    page = fixture.openSettings(QStringLiteral("advanced"), QStringLiteral("no-such-row"));
    QVERIFY(page);
    for (pdf::PDFAppSettingRow* other : page->getSection()->getRows())
    {
        QVERIFY(!other->isHighlighted());
    }

    // A link #anchor of the page scrolls and highlights
    QVERIFY(page->openLink(QStringLiteral("#prefetch-pages")));
    QVERIFY(fixture.findRow(QStringLiteral("prefetch-pages"))->isHighlighted());
}

void AppSettingsTest::filterQuery()
{
    pdftest::AppSettingsFixture fixture;

    // /settings?q=: the matching rows of all sections below their headers
    pdf::PDFAppSettingsPage* page = fixture.openFilter(QStringLiteral("magnifier"));
    QVERIFY(page);
    QVERIFY(page->isFiltering());
    QVERIFY(!page->getRestoreButton());
    QCOMPARE(page->getNavigation()->getCurrentIndex(), -1);
    QVERIFY(!page->findRow(QStringLiteral("magnifier-zoom"))->isHidden());
    QVERIFY(!page->findRow(QStringLiteral("magnifier-size"))->isHidden());
    QVERIFY(page->findRow(QStringLiteral("antialiasing"))->isHidden());
    QVERIFY(!page->getNavigationItem(QStringLiteral("viewing"))->isHidden());
    QVERIFY(page->getNavigationItem(QStringLiteral("general"))->isHidden());
    QCOMPARE(page->getNavigationItem(QStringLiteral("viewing"))->getSubtitle(), QStringLiteral("2 matches"));
    QVERIFY(page->getEmptyState()->isHidden());

    // Option titles, keywords and descriptions are searched; a section title shows the section
    page = fixture.openFilter(QStringLiteral("Blend2D"));
    QVERIFY(!page->findRow(QStringLiteral("rendering-engine"))->isHidden());
    page = fixture.openFilter(QStringLiteral("dark mode"));
    QVERIFY(!page->findRow(QStringLiteral("color-scheme"))->isHidden());
    page = fixture.openFilter(QStringLiteral("shortcuts"));
    QVERIFY(!page->getNavigationItem(QStringLiteral("shortcuts"))->isHidden());

    // Typing replaces the route of the filtered page (no new history entry)
    const int count = SettingsTestHelper::historyCount(fixture);
    page->getSearchField()->setFocus();
    page->getSearchField()->selectAll();
    QTest::keyClicks(page->getSearchField(), QStringLiteral("cache"));
    QTRY_COMPARE(page->getQuery(), QStringLiteral("cache"));
    QCOMPARE(fixture.settingsPage(), page);
    QCOMPARE(SettingsTestHelper::historyCount(fixture), count);
    QCOMPARE(SettingsTestHelper::currentRoute(fixture), QStringLiteral("/settings?q=cache"));
    QVERIFY(!page->findRow(QStringLiteral("compiled-page-cache-size"))->isHidden());

    // No match: the empty state with Clear search, which goes to the sections
    page->getSearchField()->selectAll();
    QTest::keyClicks(page->getSearchField(), QStringLiteral("zzqqxxyy"));
    QTRY_VERIFY(!page->getEmptyState()->isHidden());
    QVERIFY(page->getEmptyState()->findChild<QLabel*>(QStringLiteral("emptyText"))->text().contains(QStringLiteral("zzqqxxyy")));
    QTest::mouseClick(page->getClearSearchButton(), Qt::LeftButton);
    fixture.settle();
    QVERIFY(fixture.settingsPage());
    QVERIFY(!fixture.settingsPage()->isFiltering());
    QCOMPARE(fixture.settingsPage()->getSectionId(), QStringLiteral("general"));

    // Typing on a section page: one Push to the filtered page, the field keeps the focus
    page = fixture.openSettings(QStringLiteral("editing"));
    const int countBefore = SettingsTestHelper::historyCount(fixture);
    page->getSearchField()->setFocus();
    QTest::keyClicks(page->getSearchField(), QStringLiteral("undo"));
    QTRY_VERIFY(fixture.settingsPage() && fixture.settingsPage()->isFiltering());
    page = fixture.settingsPage();
    QCOMPARE(page->getQuery(), QStringLiteral("undo"));
    QCOMPARE(SettingsTestHelper::historyCount(fixture), countBefore + 1);
    QTRY_VERIFY(page->getSearchField()->hasFocus());
    QCOMPARE(page->getSearchField()->text(), QStringLiteral("undo"));

    // Esc clears the filter and shows the sections
    QTest::keyClick(page->getSearchField(), Qt::Key_Escape);
    fixture.settle();
    QVERIFY(fixture.settingsPage());
    QVERIFY(!fixture.settingsPage()->isFiltering());

    // An empty query is General
    fixture.navigate(QStringLiteral("/settings?q=%20%20"));
    QVERIFY(fixture.settingsPage());
    QCOMPARE(fixture.settingsPage()->getSectionId(), QStringLiteral("general"));

    // A nav item of the filtered page goes to its section
    page = fixture.openFilter(QStringLiteral("magnifier"));
    QTest::mouseClick(page->getNavigationItem(QStringLiteral("viewing")), Qt::LeftButton);
    fixture.settle();
    QCOMPARE(fixture.settingsPage()->getSectionId(), QStringLiteral("viewing"));
}

void AppSettingsTest::filterHostileTexts()
{
    pdftest::AppSettingsFixture fixture;

    // Control characters, lone surrogates, 10 kB texts and random input: the route
    // is always valid, the page never crashes
    QStringList texts = { QString(10240, QLatin1Char('a')), QStringLiteral("a\nb\tc\x01"), QString(QChar(0xD800)) + QStringLiteral("x"),
                          QStringLiteral("%00%2F../.."), QString::fromUtf8("😀😀😀"), QStringLiteral("[(*+?\\") };
    QRandomGenerator random(5);
    for (int i = 0; i < 20; ++i)
    {
        QString text;
        const int length = random.bounded(1, 400);
        for (int j = 0; j < length; ++j)
        {
            text += QChar(char16_t(random.bounded(1, 0xFFFF)));
        }
        texts << text;
    }

    for (const QString& text : texts)
    {
        const QString query = pdf::PDFAppSettingsPage::sanitizeQuery(text);
        QVERIFY(query.size() <= pdf::PDFAppSettingsPage::MAXIMUM_QUERY_LENGTH);
        const pdf::PDFAppRoute route = pdf::PDFAppSettingsPage::createFilterRoute(text);
        QVERIFY(route.isValid());

        pdf::PDFAppSettingsPage* page = fixture.openFilter(text);
        QVERIFY(page);
        page->getSearchField()->setText(text);
        QTest::keyClick(page->getSearchField(), Qt::Key_End);
        QTest::keyClicks(page->getSearchField(), QStringLiteral("x"));
        QTRY_VERIFY(fixture.settingsPage());
    }
}

void AppSettingsTest::globalSearchToRow()
{
    pdftest::AppSettingsFixture fixture;

    // A result of the global search opens the section at the row of the setting
    const pdf::PDFAppSearchData data = pdf::PDFAppSearchData::load(fixture.context.get());
    const pdf::PDFAppSearchEngine engine;
    for (const QString& key : { pdf::PDFSettingKeys::magnifierZoom(), pdf::PDFSettingKeys::colorScheme(), pdf::PDFSettingKeys::appearanceLanguage(),
                                pdf::PDFSettingKeys::cmsProfileDirectory() })
    {
        const pdf::PDFSettingDefinition* definition = fixture.context->getSettingRegistry()->findDefinition(key);
        const pdf::PDFAppSearchResults results = engine.search(definition->getTitle(), data);
        const pdf::PDFAppSearchResult* result = results.findResult(pdf::PDFAppSearchGroup::Settings, key);
        QVERIFY2(result, qPrintable(key));
        fixture.window->getNavigator()->navigate(result->route, pdf::PDFAppNavigationMode::Push);
        fixture.settle();
        QVERIFY(fixture.settingsPage());
        QCOMPARE(fixture.settingsPage()->getSectionId(), definition->getSection());
        QVERIFY(fixture.findRow(definition->getAnchor())->isHighlighted());
    }

    // RC5-S: every custom row of Storage is found (also State: portable, memory)
    for (const QString& anchor : { QStringLiteral("location"), QStringLiteral("state"), QStringLiteral("export"), QStringLiteral("import") })
    {
        const QString query = anchor == QLatin1String("state") ? QStringLiteral("portable") : anchor;
        const pdf::PDFAppSearchResults results = engine.search(query, data);
        const pdf::PDFAppSearchResult* result = results.findResult(pdf::PDFAppSearchGroup::Settings, QStringLiteral("storage.") + anchor);
        QVERIFY2(result, qPrintable(anchor));
        fixture.window->getNavigator()->navigate(result->route, pdf::PDFAppNavigationMode::Push);
        fixture.settle();
        QVERIFY2(fixture.findRow(anchor) && fixture.findRow(anchor)->isHighlighted(), qPrintable(anchor));
    }

    // The section titles of the search come from the registry (Storage too)
    QCOMPARE(pdf::PDFAppSearchEngine::getSettingsSectionTitle(QStringLiteral("storage")), QStringLiteral("Storage"));
    QCOMPARE(pdf::PDFAppSearchEngine::getSettingsSectionTitle(QStringLiteral("color-modes")), QStringLiteral("Color modes"));
}

void AppSettingsTest::deletionMatrix()
{
    // The window deleted with the dialog Restore defaults open
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        QVERIFY(page->openRestoreDefaultsDialog());
        QPointer<pdf::PDFAppRestoreDefaultsDialog> dialog = page->getRestoreDialog();
        delete fixture.window.data();
        fixture.settle();
        QVERIFY(!dialog);
    }

    // The page replaced with the dialog open: the dialog goes, nothing is restored
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        QVERIFY(page->openRestoreDefaultsDialog());
        QPointer<pdf::PDFAppRestoreDefaultsDialog> dialog = page->getRestoreDialog();
        fixture.navigate(QStringLiteral("/home"));
        QVERIFY(!dialog);
        QVERIFY(fixture.settings().isChanged(pdf::PDFSettingKeys::magnifierZoom()));
    }

    // The page deleted by the application, the section deleted with the dialog open
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        QVERIFY(page->openRestoreDefaultsDialog());
        QPointer<pdf::PDFAppRestoreDefaultsDialog> dialog = page->getRestoreDialog();
        delete page->getSection();
        fixture.settle();
        QVERIFY(!dialog);
        delete fixture.settingsPage();
        fixture.settle();
        QVERIFY(fixture.settings().isChanged(pdf::PDFSettingKeys::magnifierZoom()));
    }

    // An open combo box popup, a pending value, an open palette: the window goes
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        fixture.findRow(QStringLiteral("color-scheme"))->getControl()->getComboBox()->showPopup();
        fixture.settle();
        delete fixture.window.data();
        fixture.settle();
    }
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("color-modes")));
        QVERIFY(fixture.findRow(QStringLiteral("text-color"))->getControl()->getColorButton()->showPalette());
        fixture.settle();
        fixture.navigate(QStringLiteral("/home"));
        delete fixture.window.data();
        fixture.settle();
    }

    // Rows, groups and controls deleted by the application before the page
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        delete page->findRow(QStringLiteral("antialiasing"));
        delete page->getSection()->getGroups().front();
        delete page->findRow(QStringLiteral("magnifier-size"))->getControl();
        QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::magnifierSize(), qint64(120)));
        page->commitPendingValues();
        delete page->getSectionHeader();
        delete page->getNavigation();
        delete page->getSearchField();
        QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::magnifierZoom(), 3.0));
        fixture.navigate(QStringLiteral("/settings/general"));
        QVERIFY(fixture.settingsPage());
    }

    // The context closed before the page (a binder writes nothing then)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("viewing")));
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("magnifier-size"))->getControl();
        control->getSpinBox()->setFocus();
        QTest::keyClicks(control->getSpinBox()->findChild<QLineEdit*>(), QStringLiteral("7"));
        fixture.context->close();
        delete fixture.window.data();
        fixture.settle();
    }
}

void AppSettingsTest::deleteFromOwnSignals()
{
    // The window deleted synchronously from the signals of a binder, the dialog and
    // the navigation of the page
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("viewing")));
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("antialiasing"))->getControl();
        QObject::connect(control, &pdf::PDFAppSettingControl::valueWritten, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        QTest::mouseClick(control->getCheckBox(), Qt::LeftButton, Qt::NoModifier, QPoint(9, control->getCheckBox()->height() / 2));
        fixture.settle();
        QVERIFY(!fixture.window);
        QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::renderingAntialiasing()), false);
    }
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("viewing")));
        pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("magnifier-size"))->getControl();
        QObject::connect(control, &pdf::PDFAppSettingControl::valueWritten, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("magnifier-size")), qint64(200)));
        QVERIFY(!fixture.window);
        QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(200));
    }
    {
        // A refused write (the context closed): writeFailed deletes the page
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        pdf::PDFAppSettingControl* control = page->findRow(QStringLiteral("antialiasing"))->getControl();
        QObject::connect(control, &pdf::PDFAppSettingControl::writeFailed, fixture.window.data(), [&fixture]() { delete fixture.window.data(); });
        pdf::PDFDatabaseStatement block(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_setting BEFORE INSERT ON setting BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        QVERIFY(block.execute());
        QTest::mouseClick(control->getCheckBox(), Qt::LeftButton, Qt::NoModifier, QPoint(9, control->getCheckBox()->height() / 2));
        fixture.settle();
        QVERIFY(!fixture.window);
    }
    {
        // The status message of a refused write is shown by the page
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        pdf::PDFDatabaseStatement block(fixture.context->getDatabase()->getConnection(), "CREATE TRIGGER block_setting BEFORE INSERT ON setting BEGIN SELECT RAISE(ABORT, 'blocked'); END");
        QVERIFY(block.execute());
        pdf::PDFAppSettingControl* control = page->findRow(QStringLiteral("antialiasing"))->getControl();
        QTest::mouseClick(control->getCheckBox(), Qt::LeftButton, Qt::NoModifier, QPoint(9, control->getCheckBox()->height() / 2));
        fixture.settle();
        QVERIFY(!page->getStatusLabel()->isHidden());
        QVERIFY(page->getStatusLabel()->text().contains(QStringLiteral("could not be saved")));
        QCOMPARE(control->getShownValue(), QVariant(true));
    }
    {
        // The dialog finished deletes the window (later: the dialog is its child, a
        // sender is never deleted synchronously); the page restores before it goes
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        QVERIFY(page->openRestoreDefaultsDialog());
        pdf::PDFAppRestoreDefaultsDialog* dialog = page->getRestoreDialog();
        QObject::connect(dialog, &QDialog::finished, fixture.window.data(), [&fixture]() { fixture.window->deleteLater(); });
        QTest::mouseClick(dialog->getRestoreButton(), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
        QVERIFY(!fixture.settings().isChanged(pdf::PDFSettingKeys::magnifierZoom()));
    }
    {
        // The navigation of a nav item deletes the window
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("general"));
        QObject::connect(page, &pdf::PDFAppPage::navigationRequested, fixture.window.data(), [&fixture]() { fixture.window->deleteLater(); });
        QTest::mouseClick(page->getNavigationItem(QStringLiteral("speech")), Qt::LeftButton);
        fixture.settle();
        QVERIFY(!fixture.window);
    }
}

void AppSettingsTest::themesAndScales()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        for (const QString& sectionId : pdf::PDFAppSettingsSectionRegistry::getSectionIds())
        {
            pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId, sectionId == QStringLiteral("viewing") ? QStringLiteral("magnifier-zoom") : QString());
            QVERIFY(page);
            for (qreal ratio : { 1.0, 1.5, 2.0 })
            {
                const QImage image = SettingsTestHelper::render(page, ratio);
                if (SettingsTestHelper::countLeakPixels(image) > 0 && !qEnvironmentVariable("PDF4QT_SETTINGS_LEAK_DUMP").isEmpty())
                {
                    image.save(qEnvironmentVariable("PDF4QT_SETTINGS_LEAK_DUMP") + QStringLiteral("/leak-%1-%2.png").arg(sectionId).arg(ratio));
                    const QRgb leak = pdf::PDFThemeColors::getLeakColor().rgb() & 0xFFFFFF;
                    const QImage converted = image.convertToFormat(QImage::Format_RGB32);
                    for (int y = 0; y < converted.height(); ++y)
                    {
                        for (int x = 0; x < converted.width(); ++x)
                        {
                            if ((converted.pixel(x, y) & 0xFFFFFF) == leak)
                            {
                                QWidget* child = page->childAt(QPoint(x, y) / ratio);
                                qWarning("leak at %d %d: %s %s", x, y, child ? child->metaObject()->className() : "-", child ? qPrintable(child->objectName()) : "");
                                y = converted.height();
                                break;
                            }
                        }
                    }
                }
                QVERIFY2(SettingsTestHelper::countLeakPixels(image) == 0, qPrintable(QStringLiteral("%1 at %2").arg(sectionId).arg(ratio)));
            }
        }
    }

    // The highlight of an anchor in the colors of the theme
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QVERIFY(fixture.openSettings(QStringLiteral("viewing"), QStringLiteral("magnifier-zoom")));
    pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("magnifier-zoom"));
    const QImage image = SettingsTestHelper::render(row, 1.0);
    const QColor fill = image.pixelColor(row->width() / 2, 4);
    const QColor surface = pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BgSurface);
    QVERIFY2(fill != surface && fill.blue() > fill.red(), qPrintable(fill.name()));
}

void AppSettingsTest::rightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    {
        pdftest::AppSettingsFixture fixture;
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        QVERIFY(page);

        // The control is at the left of the texts, the menu at the right of the content
        pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("magnifier-size"));
        QVERIFY(row->getControlWrap()->x() < row->getTitleLabel()->mapTo(row, QPoint(0, 0)).x());
        QVERIFY(page->getNavigation()->mapTo(page, QPoint(0, 0)).x() > page->getSection()->mapTo(page, QPoint(0, 0)).x());
        QCOMPARE(SettingsTestHelper::countLeakPixels(SettingsTestHelper::render(page, 1.0)), 0);
        fixture.screenshot(QStringLiteral("settings-viewing-rtl"));
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppSettingsTest::minimalWindow()
{
    pdftest::AppSettingsFixture fixture(QSize(720, 480));
    for (const QString& sectionId : pdf::PDFAppSettingsSectionRegistry::getSectionIds())
    {
        pdf::PDFAppSettingsPage* page = fixture.openSettings(sectionId);
        QVERIFY(page);
        fixture.settle();

        // The menu is above the content, no horizontal scroll bar, rows inside the page
        QVERIFY(page->getColumns()->isStacked());
        QScrollArea* area = SettingsTestHelper::scrollArea(page);
        QVERIFY(area);
        QVERIFY2(area->horizontalScrollBar()->maximum() == 0 || !area->horizontalScrollBar()->isVisible(), qPrintable(sectionId));
        for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
        {
            const QRect rect(row->mapTo(page, QPoint(0, 0)), row->size());
            QVERIFY2(rect.right() <= page->width(), qPrintable(row->getAnchor()));
            const QRect control(row->getControlWrap()->mapTo(page, QPoint(0, 0)), row->getControlWrap()->size());
            QVERIFY2(control.right() <= page->width(), qPrintable(row->getAnchor()));
        }
    }
    fixture.screenshot(QStringLiteral("settings-720"));
}

void AppSettingsTest::keyboardAndAccessibility()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("general"));
    QVERIFY(page);
    QCOMPARE(page->getSearchField()->accessibleName(), QStringLiteral("Search settings"));
    QCOMPARE(page->getRestoreButton()->text(), QStringLiteral("Restore defaults"));
    QCOMPARE(page->getNavigationItem(QStringLiteral("viewing"))->getSubtitle(), QStringLiteral("Page display, scrolling, magnifier"));

    // The arrows of the navigation go to the next section and keep the focus there
    pdf::PDFNavItem* item = page->getNavigationItem(QStringLiteral("general"));
    item->setFocus(Qt::TabFocusReason);
    QTest::keyClick(item, Qt::Key_Down);
    fixture.settle();
    QTRY_VERIFY(fixture.settingsPage() && fixture.settingsPage()->getSectionId() == QStringLiteral("viewing"));
    page = fixture.settingsPage();
    QTRY_VERIFY(page->getNavigationItem(QStringLiteral("viewing"))->hasFocus());

    // The editors have their accessible names and descriptions and take the focus by Tab
    for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
    {
        QWidget* editor = row->getControl()->getEditor();
        QVERIFY(!editor->accessibleName().isEmpty());
        QCOMPARE(editor->accessibleDescription(), row->getDescription());
        QVERIFY(editor->focusPolicy() & Qt::TabFocus);
    }

    // Space toggles a check box, Enter writes a spin box
    QCheckBox* checkBox = fixture.findRow(QStringLiteral("antialiasing"))->getControl()->getCheckBox();
    checkBox->setFocus(Qt::TabFocusReason);
    QTest::keyClick(checkBox, Qt::Key_Space);
    fixture.settle();
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::renderingAntialiasing()), false);
    pdf::PDFSpinBox* spinBox = fixture.findRow(QStringLiteral("magnifier-size"))->getControl()->getSpinBox();
    spinBox->setFocus(Qt::TabFocusReason);
    QTest::keyClick(spinBox, Qt::Key_Up);
    QTest::keyClick(spinBox, Qt::Key_Return);
    fixture.settle();
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(101));
}

void AppSettingsTest::languageChange()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page);
    page->getSearchField()->setText(QStringLiteral("abc"));
    pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("magnifier-size"))->getControl();
    control->getSpinBox()->setFocus();
    QTest::keyClick(control->getSpinBox()->findChild<QLineEdit*>(), Qt::Key_Up);
    QVERIFY(control->hasPendingValue());

    // The page is built again in the new language: the section, the text of the
    // search and the pending value are kept
    SettingsTestTranslator translator;
    QCoreApplication::installTranslator(&translator);
    fixture.settle();
    QCOMPARE(fixture.settingsPage(), page);
    QCOMPARE(page->getSectionId(), QStringLiteral("viewing"));
    QCOMPARE(page->getSectionHeader()->getTitleLabel()->text(), QStringLiteral("Ansicht"));
    QCOMPARE(page->findRow(QStringLiteral("antialiasing"))->getTitle(), QString::fromUtf8("Kantenglättung"));
    QCOMPARE(page->getSearchField()->text(), QStringLiteral("abc"));
    QCOMPARE(page->getNavigationItem(QStringLiteral("viewing"))->text(), QStringLiteral("Ansicht"));
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(101));

    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
    QCOMPARE(page->getSectionHeader()->getTitleLabel()->text(), QStringLiteral("Viewing"));

    // The filtered page keeps its query
    page = fixture.openFilter(QStringLiteral("magnifier"));
    QCoreApplication::installTranslator(&translator);
    fixture.settle();
    QVERIFY(page->isFiltering());
    QVERIFY(!page->findRow(QStringLiteral("magnifier-zoom"))->isHidden());
    QVERIFY(page->findRow(QStringLiteral("antialiasing"))->isHidden());
    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
}

void AppSettingsTest::documentViewCoalescing()
{
    // A batch of settings is applied to an open document once (wave 0 item 3)
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    QVERIFY(fixture.openSettings(QStringLiteral("viewing")));
    pdf::PDFSettingsRepository settings = fixture.settings();
    QVERIFY(!settings.setValues({ { pdf::PDFSettingKeys::renderingAntialiasing(), false }, { pdf::PDFSettingKeys::renderingSmoothImages(), false },
                                  { pdf::PDFSettingKeys::magnifierZoom(), 4.0 } }).isError());
    fixture.settle();
    QTRY_COMPARE(fixture.findRow(QStringLiteral("magnifier-zoom"))->getControl()->getShownValue().toDouble(), 4.0);
    QCOMPARE(fixture.findRow(QStringLiteral("antialiasing"))->getControl()->getShownValue(), QVariant(false));
}

void AppSettingsTest::reviewShellGeometry()
{
    // The design review of wave 0 (RW5-0D) against 146:3132 / 147:4565 / 1269:14388
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page);
    fixture.settle();

    // Every title of a wide row is one line - also "Clip to crop box", whose last
    // glyph overhangs its advance (QTextLayout broke it at the advance before)
    for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
    {
        QVERIFY2(!row->isStacked(), qPrintable(row->getAnchor()));
        pdf::PDFAppParagraph* title = row->getTitleLabel();
        QVERIFY2(title->getLines(title->width()).size() == 1, qPrintable(row->getAnchor()));
        QCOMPARE(title->height(), title->getLineHeight());
    }

    // The border-top of a row is above its padding: a row of a spin box is 57 high
    // (146:3655), a row of texts 56 (146:3559), the first row of a card 56 (146:3546)
    pdf::PDFAppSettingRow* first = fixture.findRow(QStringLiteral("antialiasing"));
    pdf::PDFAppSettingRow* texts = fixture.findRow(QStringLiteral("text-antialiasing"));
    pdf::PDFAppSettingRow* spin = fixture.findRow(QStringLiteral("horizontal-wheel-speed"));
    QVERIFY(first && texts && spin);
    QVERIFY(!first->isDividerVisible());
    QVERIFY(texts->isDividerVisible() && spin->isDividerVisible());
    QCOMPARE(first->height(), 56);
    QCOMPARE(texts->height(), 56);
    QCOMPARE(spin->height(), 57);
    QCOMPARE(spin->getControlWrap()->geometry().top(), 1 + 11);
    const QColor divider = SettingsTestHelper::render(spin, 1.0).pixelColor(spin->width() / 2, 0);
    QCOMPARE(divider, pdf::PDFThemeManager::getInstance()->getColor(pdf::PDFThemeColors::BorderDivider));

    // Storage: the texts of 1269:14388
    QCOMPARE(page->getNavigationItem(QStringLiteral("storage"))->getSubtitle(), QStringLiteral("Database, backup, other files"));
    page = fixture.openSettings(QStringLiteral("storage"));
    QVERIFY(page);
    QCOMPARE(page->getSectionHeader()->getSubtitleLabel()->text(), QStringLiteral("Where PDF4QT keeps your settings and history."));
    QVERIFY(!page->getRestoreButton());

    // The count of a single match of the filter is singular
    page = fixture.openFilter(QStringLiteral("antialiasing"));
    QVERIFY(page);
    QCOMPARE(page->getNavigationItem(QStringLiteral("viewing"))->getSubtitle(), QStringLiteral("2 matches"));
    page = fixture.openFilter(QStringLiteral("crop box"));
    QVERIFY(page);
    QCOMPARE(page->getNavigationItem(QStringLiteral("viewing"))->getSubtitle(), QStringLiteral("1 match"));
}

void AppSettingsTest::reviewEchoKeepsTyping()
{
    // RW5-0C: the notification of the own write (after the rest of WRITE_DELAY)
    // reaches the field the user is still typing in - its text is not set again,
    // so the cursor and the undo history stay and the next character lands at the
    // cursor (it went to the end of the text before)
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::annotationsAuthorMode(), pdf::PDFSettingValues::authorModeCustomName()).isError());
    QVERIFY(fixture.openSettings(QStringLiteral("editing")));
    pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("custom-author-name"));
    QTRY_VERIFY(row->getControlWrap()->isEnabled());
    pdf::PDFAppSettingControl* control = row->getControl();
    QLineEdit* edit = control->getLineEdit();
    edit->setFocus();
    QTest::keyClicks(edit, QStringLiteral("Jakub"));
    QTest::keyClick(edit, Qt::Key_Left);
    QTest::keyClick(edit, Qt::Key_Left);
    QCOMPARE(edit->cursorPosition(), 3);
    QVERIFY(control->hasPendingValue());

    QTRY_COMPARE(fixture.settings().getString(pdf::PDFSettingKeys::annotationsCustomAuthorName()), QStringLiteral("Jakub"));
    QVERIFY(!control->hasPendingValue());
    fixture.settle();   // the notification of the commit is queued
    QCOMPARE(edit->cursorPosition(), 3);
    QVERIFY(edit->isUndoAvailable());
    QTest::keyClicks(edit, QStringLiteral("x"));
    QCOMPARE(edit->text(), QStringLiteral("Jakxub"));
    QTRY_COMPARE(fixture.settings().getString(pdf::PDFSettingKeys::annotationsCustomAuthorName()), QStringLiteral("Jakxub"));

    // A change of another instance is still shown (the text differs)
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::annotationsCustomAuthorName(), QStringLiteral("Other")));
    QTRY_COMPARE(edit->text(), QStringLiteral("Other"));
}

void AppSettingsTest::reviewSectionDeletedWithPendingValue()
{
    // RW5-0C: a section deleted by the application while a value is being edited
    // and the dialog Restore defaults is open: the binder writes the value, the
    // queued notification of the write reaches the page and the dialog after the
    // section is gone - the dialog closes, the page goes on without the section
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page);
    QVERIFY(page->openRestoreDefaultsDialog());
    QPointer<pdf::PDFAppRestoreDefaultsDialog> dialog = page->getRestoreDialog();
    QVERIFY(dialog);

    pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("magnifier-size"))->getControl();
    control->getSpinBox()->setValue(123);
    QVERIFY(control->hasPendingValue());
    delete page->getSection();
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(123));
    fixture.settle();
    QVERIFY(!dialog);
    QVERIFY(fixture.settings().isChanged(pdf::PDFSettingKeys::magnifierZoom()));

    // The page goes on without the section
    QVERIFY(!page->openRestoreDefaultsDialog());
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::magnifierZoom(), QVariant()));
    fixture.navigate(QStringLiteral("/settings/viewing"));
    QVERIFY(fixture.findRow(QStringLiteral("magnifier-size")));
    QCOMPARE(fixture.findRow(QStringLiteral("magnifier-size"))->getControl()->getShownValue(), QVariant(qint64(123)));
}

void AppSettingsTest::reviewDocumentViewCoalescing()
{
    // RW5-0C (documentViewCoalescing reads only the rows of the page): an open
    // document applies the notifications of a batch of settings once, from the
    // event loop - a batch of three values costs as many applications as one value
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentView> view = tab->getView();
    QVERIFY(view);
    fixture.settle();
    QVERIFY(!view->isApplySettingsQueued());
    QVERIFY(view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::Antialiasing));

    // Scheduled more times: queued once, nothing is applied before the event loop
    pdf::PDFSettingsRepository settings = fixture.settings();
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingAntialiasing(), false).isError());
    view->scheduleApplySettings();
    view->scheduleApplySettings();
    QVERIFY(view->isApplySettingsQueued());
    QVERIFY(view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::Antialiasing));
    fixture.settle();
    QVERIFY(!view->isApplySettingsQueued());
    QVERIFY(!view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::Antialiasing));

    // Each application ends with viewStateChanged: one value, then a batch of three
    QSignalSpy single(view.data(), &pdf::PDFAppDocumentView::viewStateChanged);
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingSmoothImages(), false).isError());
    fixture.settle();
    const qsizetype perChange = single.size();
    QVERIFY(perChange >= 1);
    QVERIFY(!view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::SmoothImages));

    QSignalSpy batch(view.data(), &pdf::PDFAppDocumentView::viewStateChanged);
    QVERIFY(!settings.setValues({ { pdf::PDFSettingKeys::renderingAntialiasing(), true }, { pdf::PDFSettingKeys::renderingSmoothImages(), true },
                                  { pdf::PDFSettingKeys::renderingTextAntialiasing(), false } }).isError());
    fixture.settle();
    QCOMPARE(batch.size(), perChange);
    QVERIFY(view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::Antialiasing));
    QVERIFY(view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::SmoothImages));
    QVERIFY(!view->getProxy()->getFeatures().testFlag(pdf::PDFRenderer::TextAntialiasing));

    // A view deleted with a queued application
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::renderingAntialiasing(), false).isError());
    view->scheduleApplySettings();
    QVERIFY(view->isApplySettingsQueued());
    delete fixture.window.data();
    fixture.settle();
    QVERIFY(!view);
}

void AppSettingsTest::reviewRestoreTopics()
{
    // RC5-S: the button Restore defaults and the rows of its dialog read the
    // changed values again only for the topics of the section (Recent, the tool
    // history, the thumbnails write often); Shortcuts restores by its shortcuts
    pdftest::AppSettingsFixture fixture;
    pdf::PDFSettingsRepository settings = fixture.settings();
    QVERIFY(!settings.setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page);
    pdf::PDFAppSettingsSection* section = page->getSection();
    QVERIFY(section->isRestoreTopic(QString()));
    QVERIFY(section->isRestoreTopic(pdf::PDFSettingsRepository::getTopic()));
    QVERIFY(!section->isRestoreTopic(pdf::PDFToolRepository::getPairTopic()));
    QVERIFY(!section->isRestoreTopic(QStringLiteral("shortcut")));
    QVERIFY(page->getRestoreButton()->isEnabled());

    pdf::PDFDatabaseNotifier* notifier = fixture.context->getNotifier();
    QVERIFY(notifier);

    // Another topic does not read the changed values (the state set by the test stays)
    page->getRestoreButton()->setEnabled(false);
    Q_EMIT notifier->changed(pdf::PDFToolRepository::getPairTopic(), QString());
    Q_EMIT notifier->changed(QStringLiteral("recent_file"), QString());
    QVERIFY(!page->getRestoreButton()->isEnabled());
    Q_EMIT notifier->changed(pdf::PDFSettingsRepository::getTopic(), pdf::PDFSettingKeys::magnifierZoom());
    QVERIFY(page->getRestoreButton()->isEnabled());
    page->getRestoreButton()->setEnabled(false);
    Q_EMIT notifier->everythingChanged();
    QVERIFY(page->getRestoreButton()->isEnabled());

    // The dialog the same
    QVERIFY(page->openRestoreDefaultsDialog());
    fixture.settle();
    QPointer<pdf::PDFAppRestoreDefaultsDialog> dialog = page->getRestoreDialog();
    QVERIFY(dialog);
    QCOMPARE(dialog->getFacts()->getRowCount(), 1);
    dialog->getFacts()->clear();
    Q_EMIT notifier->changed(pdf::PDFToolRepository::getPairTopic(), QString());
    QCOMPARE(dialog->getFacts()->getRowCount(), 0);
    Q_EMIT notifier->changed(pdf::PDFSettingsRepository::getTopic(), pdf::PDFSettingKeys::magnifierZoom());
    QCOMPARE(dialog->getFacts()->getRowCount(), 1);
    dialog->reject();
    fixture.settle();
    QVERIFY(!dialog);

    // Shortcuts: its own topic
    page = fixture.openSettings(QStringLiteral("shortcuts"));
    QVERIFY(page);
    section = page->getSection();
    QVERIFY(section->isRestoreTopic(QStringLiteral("shortcut")));
    QVERIFY(!section->isRestoreTopic(pdf::PDFSettingsRepository::getTopic()));
    QVERIFY(!page->getRestoreButton()->isEnabled());
    QVERIFY(!fixture.context->getShortcuts().setShortcut(QStringLiteral("file.open"), QStringLiteral("Ctrl+Shift+F12")).isError());
    QTRY_VERIFY(page->getRestoreButton()->isEnabled());

    // Security keeps the certificates: a change of them is not a change of the section
    page = fixture.openSettings(QStringLiteral("security"));
    QVERIFY(page);
    QVERIFY(!page->getSection()->isRestoreTopic(QStringLiteral("trusted_certificate")));
}

void AppSettingsTest::reviewAnchorTargets()
{
    // RC5-S: one path for the anchors of a route and of a link of the page - a row
    // (also a block row) is highlighted, a custom part of a section (the table of
    // the certificates) is only scrolled to, a row hidden by the filter is no target
    pdftest::AppSettingsFixture fixture(QSize(1440, 600));

    // A block row of a package
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("color-modes"), QStringLiteral("preset"));
    QVERIFY(page);
    pdf::PDFAppSettingRow* preset = fixture.findRow(QStringLiteral("preset"));
    QVERIFY(preset && preset->getBlockWidget());
    QVERIFY(preset->isHighlighted());
    QCOMPARE(page->findAnchorTarget(QStringLiteral("preset")), preset);

    // The table of the certificates: a route and a link of the page scroll to it
    page = fixture.openSettings(QStringLiteral("security"), QStringLiteral("trusted-certificates"));
    QVERIFY(page);
    QWidget* table = page->findAnchorTarget(QStringLiteral("trusted-certificates"));
    QVERIFY(table);
    QVERIFY(!qobject_cast<pdf::PDFAppSettingRow*>(table));
    QScrollArea* area = SettingsTestHelper::scrollArea(page);
    QVERIFY(area);
    QTRY_VERIFY(area->verticalScrollBar()->value() > 0);
    area->verticalScrollBar()->setValue(0);
    QVERIFY(page->openLink(QStringLiteral("#trusted-certificates")));
    QVERIFY(area->verticalScrollBar()->value() > 0);
    for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
    {
        QVERIFY2(!row->isHighlighted(), qPrintable(row->getAnchor()));
    }

    // A link to a row of the section highlights it; an unknown anchor does nothing
    QVERIFY(page->openLink(QStringLiteral("#use-system-certificate-store")));
    QVERIFY(fixture.findRow(QStringLiteral("use-system-certificate-store"))->isHighlighted());
    QVERIFY(!page->findAnchorTarget(QStringLiteral("no-such-anchor")));
    QVERIFY(!page->findAnchorTarget(QString()));

    // The filtered page: a matching row is a target (in any section), a filtered one not
    page = fixture.openFilter(QStringLiteral("magnifier"));
    QVERIFY(page);
    QCOMPARE(page->findAnchorTarget(QStringLiteral("magnifier-zoom")), page->findRow(QStringLiteral("magnifier-zoom")));
    QVERIFY(page->findRow(QStringLiteral("antialiasing")));
    QVERIFY(!page->findAnchorTarget(QStringLiteral("antialiasing")));
    QVERIFY(page->openLink(QStringLiteral("#magnifier-zoom")));
    QVERIFY(page->findRow(QStringLiteral("magnifier-zoom"))->isHighlighted());
}

void AppSettingsTest::reviewBlockRowPadding()
{
    // RC5-S: the paddings of a block row are kept, when the divider is shown or
    // hidden (updateRows, the filter); the divider is above the padding
    pdf::PDFAppSettingGroup group(QStringLiteral("Test"), QStringLiteral("Test"), QString());
    pdf::PDFAppSettingRow* first = new pdf::PDFAppSettingRow(QStringLiteral("First"), QString(), QStringLiteral("first"));
    pdf::PDFAppSettingRow* block = new pdf::PDFAppSettingRow(QStringLiteral("Block"), QString(), QStringLiteral("block"));
    block->setBlockWidget(new QWidget(), QMargins(10, 12, 8, 14));
    QCOMPARE(block->getPadding(), QMargins(10, 12, 8, 14));
    QCOMPARE(block->layout()->contentsMargins(), QMargins(10, 12, 8, 14));
    group.addRow(first);
    group.addRow(block);
    QVERIFY(block->isDividerVisible());
    QCOMPARE(block->layout()->contentsMargins(), QMargins(10, 12 + pdf::PDFAppSettingRow::DIVIDER_WIDTH, 8, 14));

    // The first row hidden: the block is the first visible row of the card
    first->hide();
    group.updateRows();
    QVERIFY(!block->isDividerVisible());
    QCOMPARE(block->layout()->contentsMargins(), QMargins(10, 12, 8, 14));
    first->show();
    group.updateRows();
    QCOMPARE(block->layout()->contentsMargins(), QMargins(10, 13, 8, 14));

    // A row of texts keeps the paddings 16 / 11
    QCOMPARE(first->getPadding(), QMargins(16, 11, 16, 11));
    QCOMPARE(first->layout()->contentsMargins(), QMargins(16, 11, 16, 11));

    // The block rows of Color modes in the page, also after a filter
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("color-modes"));
    QVERIFY(page);
    int blocks = 0;
    for (pdf::PDFAppSettingRow* row : page->getSection()->getRows())
    {
        if (row->getBlockWidget())
        {
            ++blocks;
            QVERIFY2(row->getPadding() != QMargins(16, 11, 16, 11), qPrintable(row->getAnchor()));
            QCOMPARE(row->layout()->contentsMargins(), row->getPadding() + QMargins(0, row->isDividerVisible() ? 1 : 0, 0, 0));
        }
    }
    QVERIFY(blocks >= 2);
    page = fixture.openFilter(QStringLiteral("night"));
    QVERIFY(page);
    pdf::PDFAppSettingRow* preset = page->findRow(QStringLiteral("preset"));
    QVERIFY(preset && !preset->isHidden());
    QCOMPARE(preset->layout()->contentsMargins(), preset->getPadding() + QMargins(0, preset->isDividerVisible() ? 1 : 0, 0, 0));
}

void AppSettingsTest::reviewGroupGrowsAtOnce()
{
    // RC5-S: a widget of a group, whose height changes after it was shown (the
    // table of the certificates gets its rows from a worker), is laid out with all
    // the levels of the page at once (Qt propagates a size hint one level per pass
    // of the event loop - about ten passes; the groups below would cover it)
    pdftest::AppSettingsFixture fixture(QSize(1440, 700));
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page);
    const QList<pdf::PDFAppSettingGroup*> groups = page->getSection()->getGroups();
    QVERIFY(groups.size() >= 2);
    pdf::PDFAppSettingGroup* group = groups.front();
    pdf::PDFAppSettingGroup* next = groups.at(1);

    QWidget* extra = new QWidget();
    extra->setObjectName(QStringLiteral("growingWidget"));
    extra->setFixedHeight(20);
    group->insertWidget(extra);
    fixture.settle();
    for (int i = 0; i < 10; ++i)
    {
        QCoreApplication::processEvents();
    }
    QCOMPARE(extra->height(), 20);

    extra->setFixedHeight(400);
    pdf::PDFAppSettingGroup::updateAncestorLayouts(extra);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(extra->height(), 400);
    QVERIFY2(group->height() >= group->minimumSizeHint().height(), qPrintable(QStringLiteral("%1 %2").arg(group->height()).arg(group->minimumSizeHint().height())));
    QVERIFY(next->mapTo(page, QPoint(0, 0)).y() >= extra->mapTo(page, QPoint(0, extra->height())).y());

    // Hostile arguments
    pdf::PDFAppSettingGroup::updateAncestorLayouts(nullptr);
    QWidget lonely;
    pdf::PDFAppSettingGroup::updateAncestorLayouts(&lonely);
}

void AppSettingsTest::reviewDialogsOverLanguageChange()
{
    // RC5-S: a language change builds the page again while QEvent::LanguageChange is
    // sent to a copy of the list of the children of the window; the dialogs of the
    // page and of General (children of the window) are hidden at once and deleted
    // later, their scrims too (PDFMessageBox::hideScrim) - nothing is deleted under
    // the delivery (ASan / valgrind)
    pdftest::AppSettingsFixture fixture;
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page && page->openRestoreDefaultsDialog());
    fixture.settle();
    QPointer<pdf::PDFAppRestoreDefaultsDialog> dialog = page->getRestoreDialog();
    QVERIFY(dialog && dialog->isVisible());
    QPointer<QWidget> scrim = dialog->getScrim();
    QVERIFY(scrim);

    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(fixture.window.data(), &languageChange);
    QVERIFY(!page->getRestoreDialog());
    QVERIFY(!dialog || dialog->isHidden());
    QVERIFY(!scrim || scrim->isHidden());
    fixture.settle();
    QVERIFY(!dialog);
    QVERIFY(!scrim);
    QCOMPARE(page->getSectionId(), QStringLiteral("viewing"));
    QVERIFY(page->getRestoreButton()->isEnabled());
}

void AppSettingsTest::reviewFormatStoredText()
{
    // RC5-S / RC5-BC: a stored text of another version or of a damaged file shown by
    // formatValue (the rows of Restore defaults) is one bounded line without control
    // and bidirectional characters; a path is elided in its middle (the file stays)
    pdftest::AppSettingsFixture fixture;
    const pdf::PDFSettingRegistry* registry = fixture.context->getSettingRegistry();
    const pdf::PDFSettingDefinition* name = registry->findDefinition(pdf::PDFSettingKeys::annotationsCustomAuthorName());
    const pdf::PDFSettingDefinition* folder = registry->findDefinition(pdf::PDFSettingKeys::cmsProfileDirectory());
    QVERIFY(name && folder);
    QCOMPARE(folder->getControl(), pdf::PDFSettingDefinition::Control::Path);

    using Control = pdf::PDFAppSettingControl;
    const QString hostile = QStringLiteral("A") + QChar(0x202E) + QStringLiteral("B\nC\r") + QChar(0x0007) + QChar(0x200F) + QStringLiteral("D");
    QCOMPARE(Control::formatValue(*name, hostile), QStringLiteral("AB C D"));
    QCOMPARE(Control::formatValue(*name, QStringLiteral("Jane")), QStringLiteral("Jane"));
    QCOMPARE(Control::formatValue(*name, QString(QChar(0x202E))), QStringLiteral("(empty)"));

    const QString longName = QString(5000, QLatin1Char('n'));
    const QString shownName = Control::formatValue(*name, longName);
    QCOMPARE(shownName.size(), qsizetype(Control::MAXIMUM_STRING_TEXT));
    QVERIFY(shownName.endsWith(QChar(0x2026)));

    const QString path = QStringLiteral("C:/") + QString(300, QLatin1Char('d')) + QStringLiteral("/profiles");
    const QString shownPath = Control::formatValue(*folder, path);
    QCOMPARE(shownPath.size(), qsizetype(Control::MAXIMUM_STRING_TEXT));
    QVERIFY(shownPath.startsWith(QStringLiteral("C:/ddd")));
    QVERIFY(shownPath.endsWith(QStringLiteral("dd/profiles")));
    QVERIFY(shownPath.contains(QChar(0x2026)));
    QCOMPARE(Control::formatValue(*folder, QStringLiteral("C:/Profiles")), QStringLiteral("C:/Profiles"));

    // Surrogate pairs are never split
    QString emoji;
    for (int i = 0; i < 200; ++i)
    {
        emoji += QString::fromUtf8("\xF0\x9F\x98\x80");
    }
    for (const pdf::PDFSettingDefinition* definition : { name, folder })
    {
        const QString shown = Control::formatValue(*definition, emoji);
        QVERIFY(shown.size() <= Control::MAXIMUM_STRING_TEXT);
        for (qsizetype i = 0; i < shown.size(); ++i)
        {
            if (shown.at(i).isHighSurrogate())
            {
                QVERIFY(i + 1 < shown.size() && shown.at(i + 1).isLowSurrogate());
            }
            if (shown.at(i).isLowSurrogate())
            {
                QVERIFY(i > 0 && shown.at(i - 1).isHighSurrogate());
            }
        }
    }

    // The dialog Restore defaults shows the bounded text of a hostile stored row
    QVERIFY(SettingsTestHelper::storeRaw(fixture.context.get(), pdf::PDFSettingKeys::annotationsCustomAuthorName(), longName + QChar(0x202E)));
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("editing"));
    QVERIFY(page);
    if (page->openRestoreDefaultsDialog())
    {
        fixture.settle();
        pdf::PDFAppRestoreDefaultsDialog* dialog = page->getRestoreDialog();
        QVERIFY(dialog);
        for (int i = 0; i < dialog->getFacts()->getRowCount(); ++i)
        {
            QVERIFY(dialog->getFacts()->getValue(i).size() < 2 * Control::MAXIMUM_STRING_TEXT + 8);
            QVERIFY(!dialog->getFacts()->getValue(i).contains(QChar(0x202E)));
        }
        dialog->reject();
        fixture.settle();
    }
}

void AppSettingsTest::screenshots()
{
    // PDF4QT_APP_SCREENSHOTS=<folder>: every section in both themes, the dialog
    // Restore defaults and the filtered page (compare with 146:2843 … 147:4565)
    if (qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS").isEmpty())
    {
        QSKIP("PDF4QT_APP_SCREENSHOTS is not set");
    }

    pdftest::AppSettingsFixture fixture(QSize(1440, 1100));
    fixture.window->getHomeTab()->getBanner()->hide();
    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        for (const QString& sectionId : pdf::PDFAppSettingsSectionRegistry::getSectionIds())
        {
            QVERIFY(fixture.openSettings(sectionId));
            fixture.screenshot(QStringLiteral("settings-%1-%2").arg(sectionId, suffix));
        }

        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
        QVERIFY(page->openRestoreDefaultsDialog());
        fixture.settle();
        fixture.screenshot(QStringLiteral("settings-restore-%1").arg(suffix));
        // The dialog is a window of its own (941:136108)
        page->getRestoreDialog()->grab().save(QDir(qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS")).absoluteFilePath(QStringLiteral("settings-restore-dialog-%1.png").arg(suffix)));
        page->getRestoreDialog()->reject();
        fixture.settle();

        QVERIFY(fixture.openFilter(QStringLiteral("color")));
        fixture.screenshot(QStringLiteral("settings-filter-%1").arg(suffix));
    }
}

/// Helpers of the tests of the General section (package A)
class GeneralTestHelper
{
public:
    GeneralTestHelper() = delete;

    /// Folder of the real translation files next to the test (empty = not built)
    static QString realTranslationFile(const QString& language)
    {
        const QString file = pdf::PDFAppTranslator::getTranslationFile(language);
        return QFileInfo(file).isFile() ? file : QString();
    }

    /// A text of PDF4QT_cs.qm (an old context of the core library)
    static QString czechParserText()
    {
        return QCoreApplication::translate("pdf::PDFParser", "Dictionary key must be a name.");
    }

    static pdf::PDFAppGeneralSettingsSection* section(const pdftest::AppSettingsFixture& fixture)
    {
        pdf::PDFAppSettingsPage* page = fixture.settingsPage();
        return page ? qobject_cast<pdf::PDFAppGeneralSettingsSection*>(page->getSection()) : nullptr;
    }

    /// Records the runs of the tool history (rows of tool_pair and the counter)
    static bool recordRuns(pdf::PDFAppContext* context, qint64 runs)
    {
        pdf::PDFDatabaseConnection* connection = context->getDatabase()->getConnection();
        pdf::PDFDatabaseStatement statement(connection, "INSERT OR REPLACE INTO tool_pair(from_tool, to_tool, count, last_used) VALUES ('merge', 'compress', ?1, 1)");
        statement.bindInt64(1, qMax<qint64>(1, runs));
        return statement.execute() && !context->getSettings().setValue(pdf::PDFSettingKeys::toolsHistoryRuns(), runs).isError();
    }

    static qint64 pairCount(pdf::PDFAppContext* context)
    {
        pdf::PDFDatabaseStatement statement(context->getDatabase()->getConnection(), "SELECT COUNT(*) FROM tool_pair");
        return statement.next() ? statement.getInt64(0) : -1;
    }

    /// Texts of the labels of a widget joined (the message box draws its texts by labels)
    static QString labelTexts(QWidget* widget)
    {
        QStringList texts;
        for (QLabel* label : widget->findChildren<QLabel*>())
        {
            texts << label->text();
        }
        return texts.join(QLatin1Char('\n'));
    }

    /// Changes the language by the combo box of the row Language as the user does
    static bool chooseLanguage(pdftest::AppSettingsFixture& fixture, const QString& language)
    {
        pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("language"));
        if (!row || !pdftest::AppSettingsFixture::setControlValue(row, language))
        {
            return false;
        }
        fixture.settle();
        return fixture.settings().getString(pdf::PDFSettingKeys::appearanceLanguage()) == language;
    }
};

/// Pseudo translation of every text of PDF4QT: "[text]" (the contexts of Qt are
/// left alone - key names, layout direction)
class PseudoTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);
        const QByteArray contextText(context ? context : "");
        if (!contextText.startsWith("pdf") || !sourceText || !*sourceText)
        {
            return QString();
        }
        return QStringLiteral("[%1]").arg(QString::fromUtf8(sourceText));
    }

    virtual bool isEmpty() const override { return false; }
};

/// Restores the overrides of the translator of a test
class TranslatorOverrideGuard
{
public:
    TranslatorOverrideGuard() :
        m_systemLanguages(pdf::PDFAppTranslator::getSystemLanguages())
    {
    }

    ~TranslatorOverrideGuard()
    {
        pdf::PDFAppTranslator::setTranslationDirectory(QString());
        pdf::PDFAppTranslator::setSystemLanguages(m_systemLanguages);
    }

    TranslatorOverrideGuard(const TranslatorOverrideGuard&) = delete;
    TranslatorOverrideGuard& operator=(const TranslatorOverrideGuard&) = delete;

private:
    QStringList m_systemLanguages;
};

void AppSettingsTest::generalRowsWriteAndApply()
{
    pdftest::AppSettingsFixture fixture;
    pdf::PDFAppDocumentTab* tab = fixture.openDocument();
    QVERIFY(tab);
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    pdf::PDFAppGeneralSettingsSection* section = GeneralTestHelper::section(fixture);
    QVERIFY(section);

    // The rows of 146:2843 in the groups Application, Documents, Tools, Updates
    QStringList anchors;
    for (pdf::PDFAppSettingRow* row : section->getRows())
    {
        anchors << row->getAnchor();
    }
    QCOMPARE(anchors, QStringList({ QStringLiteral("language"), QStringLiteral("color-scheme"), QStringLiteral("recent-files"),
                                    QStringLiteral("show-sidebar-when-opening-documents"), QStringLiteral("sidebar-icon-size"),
                                    QStringLiteral("suggest-next-tools"), QStringLiteral("tool-history"), QStringLiteral("check-for-updates") }));
    QVERIFY(fixture.findRow(QStringLiteral("language"))->getTagText().isEmpty());   // Q2: no "Restart required"
    QCOMPARE(fixture.findRow(QStringLiteral("language"))->getControl()->getComboBox()->count(), 11);

    // Color scheme: the theme follows at once
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("color-scheme")), pdf::PDFSettingValues::colorSchemeDark()));
    QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("color-scheme")), pdf::PDFSettingValues::colorSchemeLight()));
    QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);

    // Recent files, Suggest tools, Check for updates are written
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("recent-files")), 25));
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::recentMaxFiles()), qint64(25));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("suggest-next-tools")), false));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::toolsSuggestNext()), false);
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("check-for-updates")), false));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::updateAutoCheck()), false);

    // Sidebar icon size: the open document follows at once
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("sidebar-icon-size")), QStringLiteral("veryLarge")));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::viewingSidebarIconSize()), QStringLiteral("veryLarge"));
    int largestTab = 0;
    for (QToolButton* button : tab->getView()->getSidebar()->findChildren<QToolButton*>())
    {
        largestTab = qMax(largestTab, button->iconSize().width());
    }
    QVERIFY(largestTab > 18);

    // Show sidebar: a newly opened document has no sidebar
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("show-sidebar-when-opening-documents")), false));
    QCOMPARE(fixture.settings().getBool(pdf::PDFSettingKeys::viewingShowSidebar()), false);
    pdf::PDFAppDocumentTab* secondTab = fixture.openDocument();
    QVERIFY(secondTab);
    QVERIFY(!secondTab->getView()->isSidebarVisible());
    QVERIFY(fixture.openSettings(QStringLiteral("general")));

    // Language: the translator of the context applies it (English is the source text)
    QVERIFY(GeneralTestHelper::chooseLanguage(fixture, pdf::PDFSettingValues::languageAuto()));
    QCOMPARE(fixture.context->getTranslator()->getLanguage(), pdf::PDFSettingValues::languageAuto());
    QVERIFY(GeneralTestHelper::chooseLanguage(fixture, pdf::PDFSettingValues::languageEnglish()));
    QCOMPARE(fixture.context->getTranslator()->getActiveLanguage(), QString());
}

void AppSettingsTest::languageSwitchLive()
{
    if (GeneralTestHelper::realTranslationFile(QStringLiteral("cs")).isEmpty())
    {
        QSKIP("PDF4QT_cs.qm is not built (target release_translations)");
    }

    TranslatorOverrideGuard overrideGuard;
    const QString realCzechFile = GeneralTestHelper::realTranslationFile(QStringLiteral("cs"));
    pdftest::AppSettingsFixture fixture;
    const QString englishText = GeneralTestHelper::czechParserText();
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    QCOMPARE(pdf::PDFAppLocale::getUiLanguage(), QString());

    // Czech at once: the translation (also of Qt), the locale of the dates (O17),
    // the default locale of the process, no "Restart required"
    QVERIFY(GeneralTestHelper::chooseLanguage(fixture, QStringLiteral("cs")));
    pdf::PDFAppTranslator* translator = fixture.context->getTranslator();
    QCOMPARE(translator->getActiveLanguage(), QStringLiteral("cs"));
    QVERIFY(!translator->hasError());
    QCOMPARE(pdf::PDFAppLocale::getUiLanguage(), QStringLiteral("cs"));
    QCOMPARE(pdf::PDFAppLocale::getUiLocale().language(), QLocale::Czech);
    QCOMPARE(QLocale().language(), QLocale::Czech);
    QVERIFY(GeneralTestHelper::czechParserText() != englishText);
    QVERIFY(pdf::PDFAppLocale::formatDate(QDate(2026, 10, 23)) != QStringLiteral("Oct 23, 2026"));
    QCOMPARE(fixture.window->locale().language(), QLocale::Czech);
    QVERIFY(fixture.settingsPage());
    QVERIFY(fixture.findRow(QStringLiteral("language"))->getDescription() == fixture.findRow(QStringLiteral("language"))->getDefinition()->getDescription());

    // The page was built again in the new language and stays at its section
    QCOMPARE(fixture.settingsPage()->getSectionId(), QStringLiteral("general"));
    QCOMPARE(fixture.findRow(QStringLiteral("language"))->getControl()->getShownValue().toString(), QStringLiteral("cs"));

    // The other pages are shown in the new language (About formats its dates by PDFAppLocale)
    fixture.navigate(QStringLiteral("/about"));
    QVERIFY(fixture.window->getCurrentTab()->getPage());
    fixture.navigate(QStringLiteral("/recent"));
    QVERIFY(fixture.window->getCurrentTab()->getPage());

    // Back to English
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    QVERIFY(GeneralTestHelper::chooseLanguage(fixture, pdf::PDFSettingValues::languageEnglish()));
    QCOMPARE(translator->getActiveLanguage(), QString());
    QCOMPARE(GeneralTestHelper::czechParserText(), englishText);
    QCOMPARE(pdf::PDFAppLocale::getUiLocale(), pdf::PDFAppLocale::getDefaultUiLocale());
    QCOMPARE(pdf::PDFAppLocale::formatDate(QDate(2026, 10, 23)), QStringLiteral("Oct 23, 2026"));
    QCOMPARE(pdf::PDFAppLocale::getUiLanguage(), QString());

    // Automatic: the first language of the system with a translation, English
    // first when the system prefers it, English for a system without a translation
    pdf::PDFAppTranslator::setSystemLanguages({ QStringLiteral("cs-CZ"), QStringLiteral("en-US") });
    QVERIFY(GeneralTestHelper::chooseLanguage(fixture, pdf::PDFSettingValues::languageAuto()));
    QCOMPARE(translator->getActiveLanguage(), QStringLiteral("cs"));
    QCOMPARE(pdf::PDFAppLocale::getUiLocale().language(), QLocale::Czech);
    pdf::PDFAppTranslator::setSystemLanguages({ QStringLiteral("ja-JP"), QStringLiteral("en-US"), QStringLiteral("cs-CZ") });
    translator->setLanguage(pdf::PDFSettingValues::languageAuto());
    fixture.settle();
    QCOMPARE(translator->getActiveLanguage(), QString());
    QCOMPARE(pdf::PDFAppLocale::getUiLocale(), pdf::PDFAppLocale::getDefaultUiLocale());
    pdf::PDFAppTranslator::setSystemLanguages({ QStringLiteral("ja-JP"), QStringLiteral("pt-BR") });
    translator->setLanguage(pdf::PDFSettingValues::languageAuto());
    QCOMPARE(translator->getActiveLanguage(), QString());
    QVERIFY(!translator->hasError());

    // The ids of the languages of the system
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("zh-Hant-TW")), QStringLiteral("zh_TW"));
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("zh-HK")), QStringLiteral("zh_TW"));
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("zh-Hans-HK")), QStringLiteral("zh_CN"));
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("zh-CN")), QStringLiteral("zh_CN"));
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("zh")), QStringLiteral("zh_CN"));
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("en-GB")), QStringLiteral("en"));
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("de_AT")), QStringLiteral("de"));
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("ja")), QString());
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QString(1000, QChar('c'))), QString());
    QCOMPARE(pdf::PDFAppTranslator::getLanguageId(QStringLiteral("../../cs")), QString());
    QCOMPARE(pdf::PDFAppTranslator::resolveAutomaticLanguage(QStringList(1000, QStringLiteral("ja"))), pdf::PDFSettingValues::languageEnglish());

    // A missing translation file: English, the row says it
    QTemporaryDir emptyFolder;
    pdf::PDFAppTranslator::setTranslationDirectory(emptyFolder.path());
    QVERIFY(GeneralTestHelper::chooseLanguage(fixture, QStringLiteral("fr")));
    QVERIFY(translator->hasError());
    QCOMPARE(translator->getActiveLanguage(), QString());
    QCOMPARE(pdf::PDFAppLocale::getUiLocale(), pdf::PDFAppLocale::getDefaultUiLocale());
    pdf::PDFAppSettingRow* languageRow = fixture.findRow(QStringLiteral("language"));
    QVERIFY(languageRow->getDescription().contains(translator->getErrorMessage()));
    QCOMPARE(languageRow->getControl()->getEditor()->accessibleDescription(), languageRow->getDescription());

    // A damaged file (and a file too large to be a translation is not read)
    QFile damaged(QDir(emptyFolder.path()).filePath(QStringLiteral("PDF4QT_de.qm")));
    QVERIFY(damaged.open(QFile::WriteOnly));
    damaged.write(QByteArray(4096, 'x'));
    damaged.close();
    QVERIFY(GeneralTestHelper::chooseLanguage(fixture, QStringLiteral("de")));
    QVERIFY(translator->hasError());
    QVERIFY(translator->getErrorMessage().contains(QStringLiteral("damaged")));
    QVERIFY(fixture.findRow(QStringLiteral("language"))->getDescription().contains(QStringLiteral("damaged")));

    // A working translation again clears the message
    QVERIFY(QFile::copy(realCzechFile, QDir(emptyFolder.path()).filePath(QStringLiteral("PDF4QT_cs.qm"))));
    QVERIFY(GeneralTestHelper::chooseLanguage(fixture, QStringLiteral("cs")));
    QVERIFY(!translator->hasError());
    QCOMPARE(fixture.findRow(QStringLiteral("language"))->getDescription(), fixture.findRow(QStringLiteral("language"))->getDefinition()->getDescription());

    // RC5-S: the message is in the language shown now (not in the language, which
    // was active, when the file failed), and a failed language is tried again by
    // the same value - its file may have been restored
    translator->setLanguage(QStringLiteral("fr"));
    QVERIFY(translator->hasError());
    QVERIFY(translator->getErrorMessage().contains(QStringLiteral("not found")));
    {
        PseudoTranslator pseudo;
        QCoreApplication::installTranslator(&pseudo);
        QVERIFY2(translator->getErrorMessage().startsWith(QLatin1Char('[')), qPrintable(translator->getErrorMessage()));
        QCoreApplication::removeTranslator(&pseudo);
    }
    QVERIFY(QFile::copy(realCzechFile, QDir(emptyFolder.path()).filePath(QStringLiteral("PDF4QT_fr.qm"))));
    translator->setLanguage(QStringLiteral("fr"));
    QVERIFY(!translator->hasError());
    QCOMPARE(translator->getActiveLanguage(), QStringLiteral("fr"));
    QVERIFY(translator->getErrorMessage().isEmpty());
    translator->setLanguage(QStringLiteral("cs"));
    QCOMPARE(translator->getActiveLanguage(), QStringLiteral("cs"));

    // A hostile stored value is English
    QVERIFY(SettingsTestHelper::storeRaw(fixture.context.get(), pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("../../evil")));
    fixture.context->applyLanguage();
    fixture.settle();
    QCOMPARE(translator->getLanguage(), pdf::PDFSettingValues::languageEnglish());
    QCOMPARE(translator->getActiveLanguage(), QString());
}

void AppSettingsTest::languageSwitchWithWorkers()
{
    // QLocale::setDefault is not thread safe: worker threads constructing QLocale()
    // (tr() of "%Ln", the color management, Qt) while the language changes never
    // read freed data (PDFAppLocale keeps the replaced default locales alive) - run
    // it under the sanitizers with --repeat
    std::atomic_bool isRunning = true;
    std::atomic_int iterations = 0;
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i)
    {
        workers.emplace_back([&isRunning, &iterations]()
        {
            while (isRunning)
            {
                const QLocale locale;
                const QString text = locale.toString(1234567) + QCoreApplication::translate("AppSettingsTest", "%Ln pages", nullptr, 1234) +
                                     pdf::PDFAppLocale::formatDate(QDate(2026, 10, 23));
                if (!text.isEmpty())
                {
                    ++iterations;
                }
            }
        });
    }

    const QStringList languages = { QStringLiteral("cs"), QString(), QStringLiteral("de"), QStringLiteral("zh_TW"), QStringLiteral("en"), QStringLiteral("ru") };
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 300 || iterations < 1000; ++i)
    {
        if (timer.elapsed() > 10000 * pdftest::AppSettingsFixture::getTimeoutFactor())
        {
            break;
        }
        pdf::PDFAppLocale::setUiLanguage(languages[i % languages.size()]);
        if (i % 50 == 0)
        {
            pdf::PDFAppLocale::setUiLocale(QLocale(QLocale::French, QLocale::France));
        }
    }

    isRunning = false;
    for (std::thread& worker : workers)
    {
        worker.join();
    }
    QVERIFY(iterations > 0);

    pdf::PDFAppLocale::setUiLanguage(QString());
    QCOMPARE(pdf::PDFAppLocale::getUiLanguage(), QString());
    QCOMPARE(QLocale(), pdf::PDFAppLocale::getDefaultUiLocale());
}

void AppSettingsTest::languageChangeEverywhere()
{
    // A pseudo translation of every text of PDF4QT over every route of the route
    // table and a document tab with each panel and a floating bar: nothing crashes,
    // the texts are translated
    pdftest::AppSettingsFixture fixture;
    PseudoTranslator translator;

    const QStringList routes = { QStringLiteral("/home"), QStringLiteral("/open"), QStringLiteral("/recent"), QStringLiteral("/tools"),
                                 QStringLiteral("/tools/collections"), QStringLiteral("/ai"), QStringLiteral("/whats-new"), QStringLiteral("/about"),
                                 QStringLiteral("/support"), QStringLiteral("/sponsor"), QStringLiteral("/help/settings"), QStringLiteral("/settings/general"),
                                 QStringLiteral("/settings/storage"), QStringLiteral("/settings?q=color"), QStringLiteral("/search?q=merge"),
                                 QStringLiteral("/unknown/route") };
    for (const QString& route : routes)
    {
        fixture.navigate(route);
        QVERIFY2(fixture.window, qPrintable(route));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        fixture.settle();
        QVERIFY2(fixture.window && fixture.window->getCurrentTab() && fixture.window->getCurrentTab()->getPage(), qPrintable(route));
        QVERIFY2(GeneralTestHelper::labelTexts(fixture.window).contains(QLatin1Char('[')), qPrintable(route));
        QCoreApplication::removeTranslator(&translator);
        fixture.settle();
        QVERIFY2(fixture.window, qPrintable(route));
    }

    // The Settings page itself: the title of the section and the rows of General
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    QVERIFY(QCoreApplication::installTranslator(&translator));
    fixture.settle();
    QCOMPARE(fixture.findRow(QStringLiteral("tool-history"))->getTitle(), QStringLiteral("[Tool history]"));
    QCOMPARE(fixture.findRow(QStringLiteral("language"))->getTitle(), QStringLiteral("[Language]"));
    QCoreApplication::removeTranslator(&translator);
    fixture.settle();

    // A document tab with each panel of the sidebar, each panel of the toolbar and
    // the floating bar of a tool
    pdf::PDFAppDocumentTab* tab = fixture.open(pdftest::AppDocumentSamples::annotations(fixture.folder()));
    QVERIFY(tab);
    QPointer<pdf::PDFAppDocumentTab> tabGuard(tab);
    const pdf::PDFAppDocumentPanelRegistry registry = pdf::PDFAppDocumentPanelRegistry::createDefault();
    for (const pdf::PDFAppDocumentPanelFactory* factory : registry.getFactories())
    {
        tab->getView()->showPanel(factory->getId());
        fixture.settle();
        QVERIFY(QCoreApplication::installTranslator(&translator));
        fixture.settle();
        QVERIFY2(tabGuard, qPrintable(factory->getId()));
        QCoreApplication::removeTranslator(&translator);
        fixture.settle();
    }

    using Panel = pdf::PDFAppDocumentToolbar::Panel;
    for (Panel panel : { Panel::File, Panel::View, Panel::GoTo, Panel::Annotate, Panel::Insert })
    {
        QPointer<pdf::PDFAppDocumentPanelPopup> popup = tab->getView()->getToolbar()->openPanel(panel);
        fixture.settle();
        QVERIFY(QCoreApplication::installTranslator(&translator));
        fixture.settle();
        QVERIFY(tabGuard);
        QCoreApplication::removeTranslator(&translator);
        fixture.settle();
        if (popup)
        {
            popup->close();
            fixture.settle();
        }
    }

    QVERIFY(tab->executeCommand(pdf::PDFAppCommands::annotateHighlight()));
    fixture.settle();
    QVERIFY(QCoreApplication::installTranslator(&translator));
    fixture.settle();
    QVERIFY(tabGuard);
    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
    QVERIFY(tabGuard);
}

void AppSettingsTest::languageFromOtherInstance()
{
    // Another instance changes the language: this instance follows at once
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    pdf::PDFAppTranslator* translator = fixture.context->getTranslator();

    if (!GeneralTestHelper::realTranslationFile(QStringLiteral("cs")).isEmpty())
    {
        QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("cs")));
        QTRY_COMPARE(translator->getActiveLanguage(), QStringLiteral("cs"));
        QTRY_COMPARE(fixture.findRow(QStringLiteral("language"))->getControl()->getShownValue().toString(), QStringLiteral("cs"));
        QCOMPARE(QLocale().language(), QLocale::Czech);
    }

    // A missing translation of another instance: the row says it
    TranslatorOverrideGuard overrideGuard;
    QTemporaryDir emptyFolder;
    pdf::PDFAppTranslator::setTranslationDirectory(emptyFolder.path());
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("ko")));
    QTRY_VERIFY(translator->hasError());
    QTRY_VERIFY(fixture.findRow(QStringLiteral("language"))->getDescription().contains(translator->getErrorMessage()));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("language"))->getControl()->getShownValue().toString(), QStringLiteral("ko"));

    // A reset of another instance: English, the message is gone
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::appearanceLanguage(), QVariant()));
    QTRY_VERIFY(!translator->hasError());
    QCOMPARE(translator->getActiveLanguage(), QString());
    QTRY_COMPARE(fixture.findRow(QStringLiteral("language"))->getDescription(), fixture.findRow(QStringLiteral("language"))->getDefinition()->getDescription());

    // Everything changed (an import of the settings): the language is applied again
    QVERIFY(SettingsTestHelper::storeRaw(fixture.context.get(), pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("es")));
    Q_EMIT fixture.context->getNotifier()->everythingChanged();
    fixture.settle();
    QCOMPARE(translator->getLanguage(), QStringLiteral("es"));
    QVERIFY(translator->hasError());
}

void AppSettingsTest::helpInUiLanguage()
{
    // The help topics, What's New and the release notes in the language of the
    // user interface (a translation of a test catalog), English without one
    QTemporaryDir helpFolder;
    for (const QFileInfo& info : QDir(pdf::PDFAppHelpCatalog::getResourceDirectory()).entryInfoList({ QStringLiteral("*.md") }, QDir::Files))
    {
        QVERIFY(QFile::copy(info.absoluteFilePath(), QDir(helpFolder.path()).filePath(info.fileName())));
    }
    QFile english(QDir(helpFolder.path()).filePath(QStringLiteral("settings.md")));
    QVERIFY(english.open(QFile::ReadOnly));
    QString czechText = QString::fromUtf8(english.readAll());
    english.close();
    czechText.replace(QStringLiteral("title: Change settings"), QStringLiteral("title: Změna nastavení"));
    QFile czech(QDir(helpFolder.path()).filePath(QStringLiteral("settings.cs.md")));
    QVERIFY(czech.open(QFile::WriteOnly));
    czech.write(czechText.toUtf8());
    czech.close();
    const pdf::PDFAppHelpCatalog helpCatalog = pdf::PDFAppHelpCatalog::load(helpFolder.path());

    QTemporaryDir whatsNewFolder;
    for (const QFileInfo& info : QDir(QStringLiteral(":/whatsnew")).entryInfoList({ QStringLiteral("*.md") }, QDir::Files))
    {
        QVERIFY(QFile::copy(info.absoluteFilePath(), QDir(whatsNewFolder.path()).filePath(info.fileName())));
    }
    QFile release(QDir(whatsNewFolder.path()).filePath(QStringLiteral("1.3.5.md")));
    QVERIFY(release.open(QFile::ReadOnly));
    QString releaseText = QString::fromUtf8(release.readAll());
    release.close();
    QVERIFY(releaseText.contains(QStringLiteral("PDF4QT 1.3.5 resolves 6 issues")));
    releaseText.replace(QStringLiteral("PDF4QT 1.3.5 resolves 6 issues"), QStringLiteral("PDF4QT 1.3.5 řeší 6 chyb"));
    QFile releaseCzech(QDir(whatsNewFolder.path()).filePath(QStringLiteral("1.3.5.cs.md")));
    QVERIFY(releaseCzech.open(QFile::WriteOnly));
    releaseCzech.write(releaseText.toUtf8());
    releaseCzech.close();
    const pdf::PDFWhatsNewCatalog whatsNewCatalog = pdf::PDFWhatsNewCatalog::load(whatsNewFolder.path(), pdf::PDFWhatsNewCatalog::createDefaultOptions());

    pdftest::AppSettingsFixture fixture;
    fixture.navigate(QStringLiteral("/help/settings"));
    pdf::PDFAppHelpPage* helpPage = qobject_cast<pdf::PDFAppHelpPage*>(fixture.window->getCurrentTab()->getPage());
    QVERIFY(helpPage);
    helpPage->setCatalog(helpCatalog);
    QCOMPARE(helpPage->getTitle(), QStringLiteral("Change settings"));

    // The language changes: the open topic is shown in it at once
    pdf::PDFAppLocale::setUiLanguage(QStringLiteral("cs"));
    QCoreApplication::postEvent(QCoreApplication::instance(), new QEvent(QEvent::LanguageChange));
    fixture.settle();
    QCOMPARE(helpPage->getTitle(), QString::fromUtf8("Změna nastavení"));

    // A topic without its translation is English
    fixture.navigate(QStringLiteral("/help/data-storage"));
    helpPage = qobject_cast<pdf::PDFAppHelpPage*>(fixture.window->getCurrentTab()->getPage());
    QVERIFY(helpPage);
    helpPage->setCatalog(helpCatalog);
    QCOMPARE(helpPage->getTitle(), helpCatalog.findTopic(QStringLiteral("data-storage"))->title);

    // What's New and the release notes of the version with a translation
    fixture.navigate(QStringLiteral("/whats-new/1.3.5"));
    pdf::PDFAppWhatsNewPage* whatsNewPage = qobject_cast<pdf::PDFAppWhatsNewPage*>(fixture.window->getCurrentTab()->getPage());
    QVERIFY(whatsNewPage);
    whatsNewPage->setCatalog(whatsNewCatalog);
    QVERIFY(GeneralTestHelper::labelTexts(whatsNewPage).contains(QString::fromUtf8("řeší 6 chyb")));
    fixture.navigate(QStringLiteral("/whats-new/1.3.5/notes"));
    pdf::PDFAppReleaseNotesPage* notesPage = qobject_cast<pdf::PDFAppReleaseNotesPage*>(fixture.window->getCurrentTab()->getPage());
    QVERIFY(notesPage);
    notesPage->setCatalog(whatsNewCatalog);
    fixture.settle();

    // English again: the pages follow at once
    pdf::PDFAppLocale::setUiLanguage(QString());
    QCoreApplication::postEvent(QCoreApplication::instance(), new QEvent(QEvent::LanguageChange));
    fixture.settle();
    fixture.navigate(QStringLiteral("/whats-new/1.3.5"));
    whatsNewPage = qobject_cast<pdf::PDFAppWhatsNewPage*>(fixture.window->getCurrentTab()->getPage());
    QVERIFY(whatsNewPage);
    whatsNewPage->setCatalog(whatsNewCatalog);
    QVERIFY(!GeneralTestHelper::labelTexts(whatsNewPage).contains(QString::fromUtf8("řeší 6 chyb")));
    QVERIFY(GeneralTestHelper::labelTexts(whatsNewPage).contains(QStringLiteral("resolves 6 issues")));
}

void AppSettingsTest::colorSchemeRow()
{
    // The row and the switch of the sidebar of the window write the same key
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("color-scheme"));
    QCOMPARE(row->getControl()->getComboBox()->itemText(0), QStringLiteral("Automatic"));
    QCOMPARE(row->getControl()->getComboBox()->itemText(1), QStringLiteral("Light"));
    QCOMPARE(row->getControl()->getComboBox()->itemText(2), QStringLiteral("Dark"));

    QVERIFY(pdftest::AppSettingsFixture::setControlValue(row, pdf::PDFSettingValues::colorSchemeDark()));
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorScheme()), pdf::PDFSettingValues::colorSchemeDark());
    QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);

    // A change elsewhere (the switch of the window writes the setting) shows in the row
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::colorScheme(), pdf::PDFSettingValues::colorSchemeLight()).isError());
    QTRY_COMPARE(fixture.findRow(QStringLiteral("color-scheme"))->getControl()->getShownValue().toString(), pdf::PDFSettingValues::colorSchemeLight());
    QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);

    // Another instance
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::colorScheme(), pdf::PDFSettingValues::colorSchemeDark()));
    QTRY_COMPARE(fixture.findRow(QStringLiteral("color-scheme"))->getControl()->getShownValue().toString(), pdf::PDFSettingValues::colorSchemeDark());
    QCOMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::colorScheme(), pdf::PDFSettingValues::colorSchemeLight()));
    QTRY_COMPARE(pdf::PDFThemeManager::getInstance()->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
}

void AppSettingsTest::recentLimitTrims()
{
    // A lowered limit trims the unpinned history at once, the pinned files stay
    pdftest::AppSettingsFixture fixture;
    pdf::PDFRecentRepository recent = fixture.context->getRecent();
    QStringList paths;
    for (int i = 0; i < 6; ++i)
    {
        const QString path = pdftest::AppDocumentSamples::simple(fixture.folder(), QStringLiteral("limit%1.pdf").arg(i), 1);
        QString storedPath;
        QVERIFY(!recent.recordOpened(path, &storedPath).isError());
        paths << storedPath;
    }
    QVERIFY(!recent.setPinned(paths[0], true).isError());
    fixture.settle();
    QCOMPARE(recent.getFiles().size(), size_t(6));

    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("recent-files")), 2));
    QTRY_COMPARE(recent.getFiles().size(), size_t(3));
    QVERIFY(recent.getFile(paths[0]).has_value());

    // A raised limit brings nothing back; a limit of another instance trims too
    QVERIFY(pdftest::AppSettingsFixture::setControlValue(fixture.findRow(QStringLiteral("recent-files")), 50));
    fixture.settle();
    QCOMPARE(recent.getFiles().size(), size_t(3));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::recentMaxFiles(), 1));
    QTRY_COMPARE(recent.getFiles().size(), size_t(2));
    QVERIFY(recent.getFile(paths[0]).has_value());

    // Restore defaults (10) keeps the trimmed history
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::recentMaxFiles(), QVariant()));
    fixture.settle();
    QCOMPARE(recent.getFiles().size(), size_t(2));
}

void AppSettingsTest::toolHistoryClear()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 37));
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    pdf::PDFAppGeneralSettingsSection* section = GeneralTestHelper::section(fixture);
    QVERIFY(section);

    // 505:5219: the description with the count, Clear history… with the icon trash
    pdf::PDFAppSettingRow* row = section->getToolHistoryRow();
    QCOMPARE(row, fixture.findRow(QStringLiteral("tool-history")));
    QCOMPARE(row->getTitle(), QStringLiteral("Tool history"));
    QCOMPARE(row->getDescription(), QString::fromUtf8("Counts of tools run one after another, used for the suggestions · 37 runs recorded"));
    QCOMPARE(section->getClearHistoryButton()->text(), QString::fromUtf8("Clear history…"));
    QVERIFY(section->getClearHistoryButton()->isEnabled());
    QVERIFY(!section->getClearHistoryButton()->icon().isNull());

    // 941:136741: the dialog with the count, Cancel keeps everything
    QTest::mouseClick(section->getClearHistoryButton(), Qt::LeftButton);
    fixture.settle();
    QPointer<pdf::PDFAppClearToolHistoryDialog> dialog = section->getClearHistoryDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->isVisible());
    QVERIFY(GeneralTestHelper::labelTexts(dialog).contains(QStringLiteral("Clear the tool history?")));
    QVERIFY(GeneralTestHelper::labelTexts(dialog).contains(QStringLiteral("(37 runs)")));
    QCOMPARE(dialog->getFacts()->getRowCount(), 2);
    QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("Deleted"));
    QCOMPARE(dialog->getFacts()->getValue(0), QStringLiteral("37 recorded runs"));
    QCOMPARE(dialog->getFacts()->getKey(1), QStringLiteral("Kept"));
    QCOMPARE(dialog->getFacts()->getValue(1), QStringLiteral("Pinned tools, collections, list of recently used tools"));
    QCOMPARE(dialog->getClearButton()->text(), QStringLiteral("Clear Tool History"));
    QCOMPARE(dialog->getCancelButton()->text(), QStringLiteral("Cancel"));
    QVERIFY(dialog->getClearButton()->x() < dialog->getCancelButton()->x());
    QVERIFY(section->openClearHistoryDialog());
    QCOMPARE(section->getClearHistoryDialog(), dialog.data());

    // Another instance records runs meanwhile: the dialog follows
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::toolsHistoryRuns(), 1));
    QTRY_COMPARE(dialog->getFacts()->getValue(0), QStringLiteral("1 recorded run"));
    QVERIFY(GeneralTestHelper::labelTexts(dialog).contains(QStringLiteral("(1 run)")));
    QTRY_COMPARE(row->getDescription(), QString::fromUtf8("Counts of tools run one after another, used for the suggestions · 1 run recorded"));
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::toolsHistoryRuns(), 37));

    dialog->getCancelButton()->click();
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(37));
    QCOMPARE(GeneralTestHelper::pairCount(fixture.context.get()), qint64(1));

    // Escape is Cancel
    QVERIFY(section->openClearHistoryDialog());
    dialog = section->getClearHistoryDialog();
    QTest::keyClick(dialog, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(37));

    // Clear Tool History: the pairs and the counter go, the pinned tools stay (955:143792)
    QVERIFY(!fixture.context->getTools().setPinned(QStringLiteral("pages.merge"), true).isError());
    QVERIFY(section->openClearHistoryDialog());
    dialog = section->getClearHistoryDialog();
    dialog->getClearButton()->click();
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(0));
    QCOMPARE(GeneralTestHelper::pairCount(fixture.context.get()), qint64(0));
    QVERIFY(fixture.context->getTools().isPinned(QStringLiteral("pages.merge")));
    QTRY_COMPARE(row->getDescription(), QString::fromUtf8("Counts of tools run one after another, used for the suggestions · no runs recorded"));
    QVERIFY(!section->getClearHistoryButton()->isEnabled());
    QVERIFY(!section->openClearHistoryDialog());
    fixture.screenshot(QStringLiteral("settings-general-cleared"));

    // Another instance records runs: the button is enabled again
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::toolsHistoryRuns(), 5));
    QTRY_VERIFY(section->getClearHistoryButton()->isEnabled());
    QCOMPARE(section->getShownRunCount(), qint64(5));

    // Another instance clears while the dialog is open: Clear Tool History is disabled
    QVERIFY(section->openClearHistoryDialog());
    dialog = section->getClearHistoryDialog();
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::toolsHistoryRuns(), 0));
    QTRY_VERIFY(!dialog->getClearButton()->isEnabled());
    dialog->reject();
    fixture.settle();

    // Restore defaults of General does not clear the history (state, not a setting)
    QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 9));
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::recentMaxFiles(), 20).isError());
    fixture.settle();
    pdf::PDFAppSettingsPage* page = fixture.settingsPage();
    QVERIFY(page->openRestoreDefaultsDialog());
    QCOMPARE(page->getRestoreDialog()->getChangeCount(), 1);
    page->getRestoreDialog()->getRestoreButton()->click();
    fixture.settle();
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::recentMaxFiles()), qint64(10));
    QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(9));
}

void AppSettingsTest::generalHostileStoredValues()
{
    pdftest::AppSettingsFixture fixture;

    // Hostile counters of the tool history (another version, a damaged row)
    const QList<QVariant> values = { QVariant(-5), QVariant(QStringLiteral("abc")), QVariant(QByteArray(70000, 'x')), QVariant(std::numeric_limits<qint64>::max()),
                                     QVariant(std::nan("")) };
    for (const QVariant& value : values)
    {
        QVERIFY(SettingsTestHelper::storeRaw(fixture.context.get(), pdf::PDFSettingKeys::toolsHistoryRuns(), value));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        pdf::PDFAppGeneralSettingsSection* section = GeneralTestHelper::section(fixture);
        QVERIFY(section);
        QVERIFY(section->getShownRunCount() >= 0);
        QCOMPARE(section->getClearHistoryButton()->isEnabled(), section->getShownRunCount() > 0);
        QVERIFY(!section->getToolHistoryRow()->getDescription().isEmpty());
        if (section->getShownRunCount() > 0)
        {
            QVERIFY(section->openClearHistoryDialog());
            QVERIFY(section->getClearHistoryDialog()->getFacts()->getValue(0).contains(QStringLiteral("recorded runs")));
            section->getClearHistoryDialog()->reject();
            fixture.settle();
        }
    }
    QCOMPARE(pdf::PDFAppGeneralSettingsSection::getRunCountText(-1), QStringLiteral("no runs recorded"));
    QCOMPARE(pdf::PDFAppGeneralSettingsSection::getRunCountText(std::numeric_limits<qint64>::max()), QStringLiteral("2,147,483,647 runs recorded"));

    // Hostile languages are English (no file is read for them)
    const QList<QVariant> languages = { QVariant(QStringLiteral("../../../etc/passwd")), QVariant(QString(65536, QChar('c'))), QVariant(42),
                                        QVariant(QStringLiteral("cs\nde")), QVariant(QStringLiteral("CS")), QVariant(QByteArray("\xff\xfe", 2)) };
    for (const QVariant& value : languages)
    {
        QVERIFY(SettingsTestHelper::storeRaw(fixture.context.get(), pdf::PDFSettingKeys::appearanceLanguage(), value));
        fixture.context->applyLanguage();
        fixture.settle();
        QCOMPARE(fixture.context->getTranslator()->getLanguage(), pdf::PDFSettingValues::languageEnglish());
        QCOMPARE(fixture.context->getTranslator()->getActiveLanguage(), QString());
        QVERIFY(!fixture.context->getTranslator()->hasError());
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        QCOMPARE(fixture.findRow(QStringLiteral("language"))->getControl()->getShownValue().toString(), pdf::PDFSettingValues::languageEnglish());
    }
    QVERIFY(pdf::PDFAppTranslator::getTranslationFile(QStringLiteral("../cs")).isEmpty());
    QVERIFY(pdf::PDFAppTranslator::getTranslationFile(QStringLiteral("en")).isEmpty());
    QVERIFY(pdf::PDFAppTranslator::getTranslationFile(QStringLiteral("cs")).endsWith(QStringLiteral("PDF4QT_cs.qm")));

    // A translation file larger than a translation is not read - also a working
    // translation padded over the limit (QTranslator would load it), review RT5
    const QString realCzechFile = GeneralTestHelper::realTranslationFile(QStringLiteral("cs"));
    TranslatorOverrideGuard overrideGuard;
    QTemporaryDir folder;
    pdf::PDFAppTranslator::setTranslationDirectory(folder.path());
    const QString hugePath = QDir(folder.path()).filePath(QStringLiteral("PDF4QT_tr.qm"));
    if (!realCzechFile.isEmpty())
    {
        QVERIFY(QFile::copy(realCzechFile, hugePath));
        QFile::setPermissions(hugePath, QFile::ReadOwner | QFile::WriteOwner);
    }
    QFile huge(hugePath);
    QVERIFY(huge.open(QFile::ReadWrite));
    QVERIFY(huge.resize(pdf::PDFAppTranslator::MAXIMUM_FILE_SIZE + 1));
    huge.close();
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("tr")).isError());
    fixture.settle();
    QVERIFY(fixture.context->getTranslator()->hasError());
    QCOMPARE(fixture.context->getTranslator()->getActiveLanguage(), QString());
    QVERIFY2(fixture.context->getTranslator()->getErrorMessage().contains(QStringLiteral("not found")), qPrintable(fixture.context->getTranslator()->getErrorMessage()));

    // Only the checked file is loaded (security review RS5): QTranslator::load of a
    // path tries "<path>.qm" before the path (and shorter names after it), and a
    // file with a block of dependencies loads every file it names (any path)
    if (!realCzechFile.isEmpty())
    {
        QFile realFile(realCzechFile);
        QVERIFY(realFile.open(QFile::ReadOnly));
        const QByteArray czech = realFile.readAll();
        realFile.close();

        const QDir translations(folder.path());
        auto writeFile = [](const QString& filePath, const QByteArray& data)
        {
            QFile file(filePath);
            return file.open(QFile::WriteOnly) && file.write(data) == data.size();
        };

        // A damaged PDF4QT_de.qm next to a valid PDF4QT_de.qm.qm
        QVERIFY(writeFile(translations.filePath(QStringLiteral("PDF4QT_de.qm")), QByteArray(4096, 'x')));
        QVERIFY(writeFile(translations.filePath(QStringLiteral("PDF4QT_de.qm.qm")), czech));
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("de")).isError());
        fixture.settle();
        QVERIFY(fixture.context->getTranslator()->hasError());
        QCOMPARE(fixture.context->getTranslator()->getActiveLanguage(), QString());

        // A valid translation with a block of dependencies (tag 0x96), which names an
        // existing translation of the folder
        QByteArray dependencies;
        {
            QDataStream stream(&dependencies, QIODevice::WriteOnly);
            stream << QStringLiteral("dependency");
        }
        QByteArray withDependencies = czech;
        withDependencies.append(QByteArray::fromHex("96"));
        const quint32 length = quint32(dependencies.size());
        withDependencies.append(char((length >> 24) & 0xFF)).append(char((length >> 16) & 0xFF)).append(char((length >> 8) & 0xFF)).append(char(length & 0xFF));
        withDependencies.append(dependencies);
        QVERIFY(writeFile(translations.filePath(QStringLiteral("dependency.qm")), czech));
        QVERIFY(writeFile(translations.filePath(QStringLiteral("PDF4QT_fr.qm")), withDependencies));
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("fr")).isError());
        fixture.settle();
        QVERIFY(fixture.context->getTranslator()->hasError());
        QCOMPARE(fixture.context->getTranslator()->getActiveLanguage(), QString());

        // The same file without the block is a translation
        QVERIFY(writeFile(translations.filePath(QStringLiteral("PDF4QT_es.qm")), czech));
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("es")).isError());
        fixture.settle();
        QVERIFY(!fixture.context->getTranslator()->hasError());
        QCOMPARE(fixture.context->getTranslator()->getActiveLanguage(), QStringLiteral("es"));
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), pdf::PDFSettingValues::languageEnglish()).isError());
        fixture.settle();
    }
}

void AppSettingsTest::generalSearchAndAnchors()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 3));

    // The anchor of the custom row scrolls to it and highlights it
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("general"), QStringLiteral("tool-history"));
    QVERIFY(page);
    QTRY_VERIFY(fixture.findRow(QStringLiteral("tool-history"))->isHighlighted());
    QVERIFY(fixture.openSettings(QStringLiteral("general"), QStringLiteral("language")));
    QTRY_VERIFY(fixture.findRow(QStringLiteral("language"))->isHighlighted());

    // The filter finds the custom row by its title and its keywords, and the language
    for (const QString& query : { QStringLiteral("tool history"), QStringLiteral("then continue"), QStringLiteral("clear history") })
    {
        page = fixture.openFilter(query);
        QVERIFY(page);
        QVERIFY2(page->isFiltering(), qPrintable(query));
        pdf::PDFAppSettingRow* row = page->findRow(QStringLiteral("tool-history"));
        QVERIFY2(row && row->isVisibleTo(page), qPrintable(query));
    }
    page = fixture.openFilter(QStringLiteral("translation"));
    QVERIFY(page->findRow(QStringLiteral("language")) && page->findRow(QStringLiteral("language"))->isVisibleTo(page));
    QVERIFY(!page->findRow(QStringLiteral("tool-history")) || !page->findRow(QStringLiteral("tool-history"))->isVisibleTo(page));
}

void AppSettingsTest::generalDeletionMatrix()
{
    // The window deleted with the dialog Clear the tool history? open
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 4));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
        QPointer<pdf::PDFAppClearToolHistoryDialog> dialog = GeneralTestHelper::section(fixture)->getClearHistoryDialog();
        delete fixture.window.data();
        fixture.settle();
        QVERIFY(!dialog);
        QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(4));
    }

    // The page replaced with the dialog open: the dialog goes, nothing is cleared
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 4));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
        QPointer<pdf::PDFAppClearToolHistoryDialog> dialog = GeneralTestHelper::section(fixture)->getClearHistoryDialog();
        fixture.navigate(QStringLiteral("/home"));
        QVERIFY(!dialog);
        QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(4));
    }

    // The section deleted by the application, then the dialog accepted (nothing happens)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 4));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        pdf::PDFAppGeneralSettingsSection* section = GeneralTestHelper::section(fixture);
        QVERIFY(section->openClearHistoryDialog());
        QPointer<pdf::PDFAppClearToolHistoryDialog> dialog = section->getClearHistoryDialog();
        delete section;
        fixture.settle();
        QVERIFY(!dialog);
        QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(4));
    }

    // The dialog deleted by the application
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 4));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        pdf::PDFAppGeneralSettingsSection* section = GeneralTestHelper::section(fixture);
        QVERIFY(section->openClearHistoryDialog());
        delete section->getClearHistoryDialog();
        fixture.settle();
        QVERIFY(!section->getClearHistoryDialog());
        QVERIFY(section->openClearHistoryDialog());
        section->getClearHistoryDialog()->reject();
        fixture.settle();
    }

    // A language change with the dialog open (the page is built again)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 4));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
        PseudoTranslator translator;
        QCoreApplication::installTranslator(&translator);
        fixture.settle();
        QCoreApplication::removeTranslator(&translator);
        fixture.settle();
        QVERIFY(GeneralTestHelper::section(fixture));
        QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(4));
    }

    // A language change with the dialog Restore defaults open (the page is built
    // again while LanguageChange is sent to the children of the window)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::recentMaxFiles(), 20).isError());
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("general"));
        QVERIFY(page->openRestoreDefaultsDialog());
        fixture.settle();
        PseudoTranslator translator;
        QCoreApplication::installTranslator(&translator);
        fixture.settle();
        QCoreApplication::removeTranslator(&translator);
        fixture.settle();
        QVERIFY(fixture.settingsPage());
        QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::recentMaxFiles()), qint64(20));
    }

    // The window deleted with the popup of the language combo box open and a
    // language change pending
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        fixture.findRow(QStringLiteral("language"))->getControl()->getComboBox()->showPopup();
        fixture.settle();
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("cs")).isError());
        delete fixture.window.data();
        fixture.settle();
    }

    // The context (and its translator) destroyed with a translation installed: the
    // locale of the user interface is English again
    {
        auto fixture = std::make_unique<pdftest::AppSettingsFixture>();
        if (!GeneralTestHelper::realTranslationFile(QStringLiteral("cs")).isEmpty())
        {
            QVERIFY(!fixture->settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("cs")).isError());
            fixture->settle();
            QCOMPARE(fixture->context->getTranslator()->getActiveLanguage(), QStringLiteral("cs"));
        }
        fixture.reset();
        QCoreApplication::processEvents();
        QCOMPARE(pdf::PDFAppLocale::getUiLanguage(), QString());
        QCOMPARE(pdf::PDFAppLocale::getUiLocale(), pdf::PDFAppLocale::getDefaultUiLocale());
        QCOMPARE(GeneralTestHelper::czechParserText(), QStringLiteral("Dictionary key must be a name."));
    }
}

void AppSettingsTest::generalDeleteFromOwnSignals()
{
    // The window deleted from the signal of the translator (a language change)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        QObject::connect(fixture.context->getTranslator(), &pdf::PDFAppTranslator::languageChanged, fixture.context.get(), [window]() { delete window.data(); });
        QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("fr")).isError());
        fixture.settle();
        QVERIFY(!window);
    }

    // The window deleted from the notification of the cleared history
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 4));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        QObject::connect(fixture.context->getNotifier(), &pdf::PDFDatabaseNotifier::changed, fixture.context.get(), [window](const QString& topic)
        {
            if (topic == pdf::PDFToolRepository::getPairTopic())
            {
                delete window.data();
            }
        });
        QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
        GeneralTestHelper::section(fixture)->getClearHistoryDialog()->getClearButton()->click();
        fixture.settle();
        QVERIFY(!window);
        QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(0));
    }

    // The window deleted from the finished signal of the dialog (a deleteLater of
    // the application, Qt does not survive the deletion of the sender itself)
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 4));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        QPointer<pdf::PDFAppMainWindow> window = fixture.window;
        QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
        pdf::PDFAppClearToolHistoryDialog* dialog = GeneralTestHelper::section(fixture)->getClearHistoryDialog();
        QObject::connect(dialog, &QDialog::finished, fixture.context.get(), [window]() { if (window) { window->deleteLater(); } });
        dialog->getClearButton()->click();
        fixture.settle();
        QVERIFY(!window);
        QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(0));
    }
}

void AppSettingsTest::generalThemesAndScales()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 37));
    TranslatorOverrideGuard overrideGuard;
    QTemporaryDir emptyFolder;
    pdf::PDFAppTranslator::setTranslationDirectory(emptyFolder.path());
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::appearanceLanguage(), QStringLiteral("cs")).isError());
    fixture.settle();

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("general"));
        QVERIFY(page);
        pdf::PDFAppGeneralSettingsSection* section = GeneralTestHelper::section(fixture);
        QVERIFY(section->openClearHistoryDialog());
        fixture.settle();
        pdf::PDFAppClearToolHistoryDialog* dialog = section->getClearHistoryDialog();
        for (qreal ratio : { 1.0, 1.5, 2.0 })
        {
            QCOMPARE(SettingsTestHelper::countLeakPixels(SettingsTestHelper::render(page, ratio)), 0);
            QCOMPARE(SettingsTestHelper::countLeakPixels(SettingsTestHelper::render(dialog, ratio)), 0);
        }

        const QString suffix = scheme == pdf::PDFThemeColors::Scheme::Light ? QStringLiteral("light") : QStringLiteral("dark");
        fixture.screenshot(QStringLiteral("settings-general-clear-%1").arg(suffix));
        if (!qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS").isEmpty())
        {
            dialog->grab().save(QDir(qEnvironmentVariable("PDF4QT_APP_SCREENSHOTS")).absoluteFilePath(QStringLiteral("settings-clear-history-dialog-%1.png").arg(suffix)));
        }
        dialog->reject();
        fixture.settle();
        fixture.screenshot(QStringLiteral("settings-general-language-error-%1").arg(suffix));
    }
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void AppSettingsTest::generalRightToLeft()
{
    QApplication::setLayoutDirection(Qt::RightToLeft);
    {
        pdftest::AppSettingsFixture fixture;
        QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 2));
        QVERIFY(fixture.openSettings(QStringLiteral("general")));
        pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("tool-history"));
        QVERIFY(row->getControlWrap()->x() < row->getTitleLabel()->mapTo(row, QPoint(0, 0)).x());
        QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
        fixture.settle();
        pdf::PDFAppClearToolHistoryDialog* dialog = GeneralTestHelper::section(fixture)->getClearHistoryDialog();
        QVERIFY(dialog->getClearButton()->x() > dialog->getCancelButton()->x());
        QCOMPARE(SettingsTestHelper::countLeakPixels(SettingsTestHelper::render(dialog, 1.0)), 0);
        fixture.screenshot(QStringLiteral("settings-general-rtl"));
        dialog->reject();
        fixture.settle();
    }
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void AppSettingsTest::generalMinimalWindow()
{
    pdftest::AppSettingsFixture fixture(QSize(720, 480));
    QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 37));
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("general"), QStringLiteral("tool-history"));
    QVERIFY(page);
    fixture.settle();
    pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("tool-history"));
    QVERIFY(row->isStacked());
    QPushButton* button = GeneralTestHelper::section(fixture)->getClearHistoryButton();
    const QRect buttonRect(button->mapTo(page, QPoint(0, 0)), button->size());
    QVERIFY(buttonRect.right() <= page->width());
    QVERIFY(button->width() >= button->sizeHint().width());
    fixture.screenshot(QStringLiteral("settings-general-720"));

    // The dialog fits into the window
    QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
    fixture.settle();
    pdf::PDFAppClearToolHistoryDialog* dialog = GeneralTestHelper::section(fixture)->getClearHistoryDialog();
    QVERIFY(dialog->getClearButton()->isVisible());
    QVERIFY(dialog->getCancelButton()->isVisible());
    fixture.screenshot(QStringLiteral("settings-general-720-dialog"));
    dialog->reject();
    fixture.settle();
}

void AppSettingsTest::generalKeyboardAndAccessibility()
{
    pdftest::AppSettingsFixture fixture;
    QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 3));
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    pdf::PDFAppGeneralSettingsSection* section = GeneralTestHelper::section(fixture);
    QPushButton* button = section->getClearHistoryButton();
    QCOMPARE(button->accessibleName(), QStringLiteral("Clear the tool history"));
    QVERIFY(button->focusPolicy() & Qt::TabFocus);

    // Tab from the check box before it reaches the button, Space opens the dialog,
    // Escape closes it, Enter is Cancel (the Primary button is the default)
    QCheckBox* checkBox = fixture.findRow(QStringLiteral("suggest-next-tools"))->getControl()->getCheckBox();
    checkBox->setFocus(Qt::TabFocusReason);
    QTest::keyClick(checkBox, Qt::Key_Tab);
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(button));
    QTest::keyClick(button, Qt::Key_Space);
    fixture.settle();
    QPointer<pdf::PDFAppClearToolHistoryDialog> dialog = section->getClearHistoryDialog();
    QVERIFY(dialog);
    QVERIFY(dialog->getCancelButton()->isDefault());
    QTest::keyClick(dialog, Qt::Key_Escape);
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(3));
    QVERIFY(section->openClearHistoryDialog());
    dialog = section->getClearHistoryDialog();
    QTest::keyClick(dialog, Qt::Key_Return);
    fixture.settle();
    QVERIFY(!dialog);
    QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(3));
}

void AppSettingsTest::generalLanguageChange()
{
    // A test translator: the custom row, the description with the count, the dialog
    pdftest::AppSettingsFixture fixture;
    QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 3));
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
    QPointer<pdf::PDFAppClearToolHistoryDialog> dialog = GeneralTestHelper::section(fixture)->getClearHistoryDialog();

    // The dialog retranslates itself
    PseudoTranslator translator;
    QCoreApplication::installTranslator(&translator);
    QEvent languageChange(QEvent::LanguageChange);
    QCoreApplication::sendEvent(dialog, &languageChange);
    QCOMPARE(dialog->getClearButton()->text(), QStringLiteral("[Clear Tool History]"));
    QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("[Deleted]"));

    // The page is built again: the custom row with the count; the dialog of the old
    // section is closed with it (as Restore defaults)
    fixture.settle();
    QVERIFY(!dialog);
    pdf::PDFAppSettingRow* row = fixture.findRow(QStringLiteral("tool-history"));
    QCOMPARE(row->getTitle(), QStringLiteral("[Tool history]"));
    QVERIFY(row->getDescription().contains(QStringLiteral("[Counts of tools run one after another, used for the suggestions]")));
    QVERIFY(row->getDescription().contains(QStringLiteral("[3 runs recorded]")));
    QCOMPARE(GeneralTestHelper::section(fixture)->getClearHistoryButton()->text(), QString::fromUtf8("[Clear history…]"));

    QCoreApplication::removeTranslator(&translator);
    fixture.settle();
    QCOMPARE(fixture.findRow(QStringLiteral("tool-history"))->getTitle(), QStringLiteral("Tool history"));
    QVERIFY(GeneralTestHelper::section(fixture)->openClearHistoryDialog());
    dialog = GeneralTestHelper::section(fixture)->getClearHistoryDialog();
    QCOMPARE(dialog->getClearButton()->text(), QStringLiteral("Clear Tool History"));
    QCOMPARE(dialog->getFacts()->getKey(0), QStringLiteral("Deleted"));
    dialog->reject();
    fixture.settle();
}

void AppSettingsTest::reviewLanguageCommitOnActivation()
{
    // Owner 2026-10-25 (FX5-2): the Language combo box applies only an activated
    // choice - a click or Enter in the open list, Enter on the closed combo box -
    // not every arrow key or typed letter of the closed combo box (each switch
    // rebuilds the whole user interface). Escape or leaving the combo box without
    // Enter shows the stored value again. The other combo boxes write at once.
    pdftest::AppSettingsFixture fixture;
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    QSignalSpy languageSpy(fixture.context->getTranslator(), &pdf::PDFAppTranslator::languageChanged);
    const QString english = pdf::PDFSettingValues::languageEnglish();
    const QString automatic = pdf::PDFSettingValues::languageAuto();
    auto stored = [&fixture]() { return fixture.settings().getString(pdf::PDFSettingKeys::appearanceLanguage()); };
    auto control = [&fixture]() { return fixture.findRow(QStringLiteral("language"))->getControl(); };
    QCOMPARE(stored(), english);
    QVERIFY(control()->getDefinition().isCommittedOnActivation());
    QVERIFY(!fixture.findRow(QStringLiteral("color-scheme"))->getControl()->getDefinition().isCommittedOnActivation());
    QVERIFY(qobject_cast<pdf::PDFAppSettingComboBox*>(control()->getComboBox()));

    pdf::PDFComboBox* comboBox = control()->getComboBox();
    comboBox->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(comboBox->hasFocus());

    // The arrows only show the items: nothing is written, the language stays
    QTest::keyClick(comboBox, Qt::Key_Down);
    QTest::keyClick(comboBox, Qt::Key_Down);
    fixture.settle();
    QCOMPARE(control()->getShownValue().toString(), QStringLiteral("ko"));
    QVERIFY(control()->isBrowsing());
    QVERIFY(!control()->hasPendingValue());
    QCOMPARE(stored(), english);
    QCOMPARE(languageSpy.count(), 0);

    // A notification of the settings keeps the browsed item
    QVERIFY(fixture.writeFromOtherConnection(pdf::PDFSettingKeys::recentMaxFiles(), qint64(31)));
    QCOMPARE(control()->getShownValue().toString(), QStringLiteral("ko"));

    // Escape shows the stored value again
    QTest::keyClick(comboBox, Qt::Key_Escape);
    QVERIFY(!control()->isBrowsing());
    QCOMPARE(control()->getShownValue().toString(), english);

    // A typed letter searches the items, it writes nothing either
    QTest::keyClick(comboBox, Qt::Key_D);
    fixture.settle();
    QCOMPARE(control()->getShownValue().toString(), QStringLiteral("de"));
    QCOMPARE(stored(), english);

    // Leaving the combo box without Enter shows the stored value again
    QWidget* other = fixture.findRow(QStringLiteral("color-scheme"))->getControl()->getComboBox();
    other->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(other->hasFocus());
    QVERIFY(!control()->isBrowsing());
    QCOMPARE(control()->getShownValue().toString(), english);
    fixture.settle();
    QCOMPARE(stored(), english);
    QCOMPARE(languageSpy.count(), 0);

    // Enter writes the browsed item (once)
    comboBox->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(comboBox->hasFocus());
    QTest::keyClick(comboBox, Qt::Key_Up);
    QCOMPARE(control()->getShownValue().toString(), automatic);
    QCOMPARE(stored(), english);
    QTest::keyClick(comboBox, Qt::Key_Return);
    fixture.settle();
    QCOMPARE(stored(), automatic);
    QVERIFY(!control()->isBrowsing());

    // The open list: its arrows write nothing, a list closed without a choice
    // writes nothing, a choice clicked in the list is written
    comboBox = control()->getComboBox();
    comboBox->showPopup();
    QTRY_VERIFY(comboBox->view()->isVisible());
    QTest::keyClick(comboBox->view(), Qt::Key_Down);
    fixture.settle();
    QCOMPARE(stored(), automatic);
    comboBox->hidePopup();
    QTRY_VERIFY(!comboBox->view()->isVisible());
    fixture.settle();
    QCOMPARE(stored(), automatic);
    QCOMPARE(control()->getShownValue().toString(), automatic);

    comboBox->showPopup();
    QTRY_VERIFY(comboBox->view()->isVisible());
    const QModelIndex englishIndex = comboBox->model()->index(comboBox->findData(english), 0);
    comboBox->view()->setCurrentIndex(englishIndex);
    QTest::mouseClick(comboBox->view()->viewport(), Qt::LeftButton, Qt::NoModifier, comboBox->view()->visualRect(englishIndex).center());
    fixture.settle();
    QCOMPARE(stored(), english);

    // A browsed item is never written when the page goes
    comboBox = control()->getComboBox();
    comboBox->setFocus(Qt::TabFocusReason);
    QTest::keyClick(comboBox, Qt::Key_End);
    QVERIFY(control()->isBrowsing());
    fixture.navigate(QStringLiteral("/home"));
    QCOMPARE(stored(), english);

    // The other combo boxes write at once (the color scheme)
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    pdf::PDFComboBox* scheme = fixture.findRow(QStringLiteral("color-scheme"))->getControl()->getComboBox();
    scheme->setFocus(Qt::TabFocusReason);
    QTest::keyClick(scheme, Qt::Key_End);
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorScheme()), pdf::PDFSettingValues::colorSchemeDark());
    QTest::keyClick(scheme, Qt::Key_Home);
    fixture.settle();
    QCOMPARE(fixture.settings().getString(pdf::PDFSettingKeys::colorScheme()), pdf::PDFSettingValues::colorSchemeAuto());
}

void AppSettingsTest::reviewConfirmationFocus()
{
    // The first keyboard focus of a confirmation is on its Primary button Cancel,
    // not on the leading destructive button, which Space would take (FX5-2)
    pdftest::AppSettingsFixture fixture;
    QVERIFY(GeneralTestHelper::recordRuns(fixture.context.get(), 3));
    QVERIFY(fixture.openSettings(QStringLiteral("general")));
    pdf::PDFAppGeneralSettingsSection* section = GeneralTestHelper::section(fixture);
    QVERIFY(section->openClearHistoryDialog());
    QPointer<pdf::PDFAppClearToolHistoryDialog> clearDialog = section->getClearHistoryDialog();
    QVERIFY(clearDialog);
    fixture.settle();
    QCOMPARE(clearDialog->focusWidget(), static_cast<QWidget*>(clearDialog->getCancelButton()));
    QTest::keyClick(clearDialog->focusWidget(), Qt::Key_Space);
    fixture.settle();
    QVERIFY(!clearDialog);
    QCOMPARE(fixture.context->getTools().getHistoryRuns(), qint64(3));

    // Restore defaults
    QVERIFY(!fixture.settings().setValue(pdf::PDFSettingKeys::magnifierZoom(), 3.0).isError());
    pdf::PDFAppSettingsPage* page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page && page->openRestoreDefaultsDialog());
    QPointer<pdf::PDFAppRestoreDefaultsDialog> restoreDialog = page->getRestoreDialog();
    QVERIFY(restoreDialog);
    fixture.settle();
    QCOMPARE(restoreDialog->focusWidget(), static_cast<QWidget*>(restoreDialog->getCancelButton()));
    QTest::keyClick(restoreDialog->focusWidget(), Qt::Key_Space);
    fixture.settle();
    QVERIFY(!restoreDialog);
    QCOMPARE(fixture.settings().getChangedKeys(QStringLiteral("viewing")).size(), 1);
}

void AppSettingsTest::reviewPageDestroyedWritesSilently()
{
    // RFX5: a page deleted directly (its window closes) writes a value being edited
    // by the destructor of its binder without the notifications - no slot of a
    // section (a new engine of the speech) runs from the destructor of the page
    pdftest::AppSettingsFixture fixture;
    QPointer<pdf::PDFAppSettingsPage> page = fixture.openSettings(QStringLiteral("viewing"));
    QVERIFY(page);
    pdf::PDFAppSettingControl* control = fixture.findRow(QStringLiteral("magnifier-size"))->getControl();
    control->getSpinBox()->setValue(123);
    QVERIFY(control->hasPendingValue());
    QSignalSpy written(control, &pdf::PDFAppSettingControl::valueWritten);
    QSignalSpy failed(control, &pdf::PDFAppSettingControl::writeFailed);
    delete page.data();
    QVERIFY(!page);
    QCOMPARE(fixture.settings().getInteger(pdf::PDFSettingKeys::magnifierSize()), qint64(123));
    QCOMPARE(written.size(), 0);
    QCOMPARE(failed.size(), 0);
    fixture.settle();
}

QTEST_MAIN(AppSettingsTest)
#include "tst_appsettingstest.moc"
