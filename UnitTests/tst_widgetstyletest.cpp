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

#include "pdfaccenthue.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentpanels.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfmessagebox.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdftooltip.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include "gallerypages.h"

#include <QtTest>

#ifdef PDF4QT_TEST_PLATFORM_THEME
#include "colorschemetesttheme.h"
#endif

#include <QCheckBox>
#include <QContextMenuEvent>
#include <QFocusFrame>
#include <QMenu>
#include <QMetaEnum>
#include <QPainter>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QTabWidget>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QItemEditorFactory>
#include <QScreen>
#include <QScrollBar>
#include <QSlider>
#include <QStandardItemModel>
#include <QTableView>
#include <QTextBlock>
#include <QToolTip>
#include <QStyleOptionHeader>
#include <QLineEdit>
#include <QTimer>
#include <QDir>
#include <QProgressBar>
#include <QRubberBand>
#include <QSplitter>
#include <QStyleFactory>
#include <QStyleHints>
#include <QPushButton>
#include <QScrollArea>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionGroupBox>
#include <QTextLayout>
#include <QStyleOptionMenuItem>
#include <QStyleOptionProgressBar>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QStyleOptionTab>
#include <QStyleOptionToolButton>
#include <QStyleOptionViewItem>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <QPixmapCache>
#include <QProcess>

#include <cstdlib>
#include <limits>
#include <map>
#include <memory>

using Color = pdf::PDFThemeColors::Color;

class WidgetStyleTest : public QObject
{
    Q_OBJECT

public:
    /// Called by QTEST_MAIN before the application is created: the process started
    /// by reviewFontsWithoutGuiApplication loads the fonts with a QCoreApplication only
    static void initMain();

private slots:
    void initTestCase();
    void cleanup();

    void themeTokens();
    void themeTokensChecksum();
    void themeManager();
    void systemColorScheme();
    void leakPalette();
    void fonts();
    void icons();
    void pixelMetrics_data();
    void pixelMetrics();
    void styleHints_data();
    void styleHints();
    void sizesFromContents();
    void pushButtonPixels_data();
    void pushButtonPixels();
    void fieldPixels();
    void indicatorPixels();
    void subControlRects();
    void rightToLeft();
    void nullSafety();
    void busyAnimation();
    void mnemonics();
    void keyboardFocusFrame();
    void themeSwitch();
    void widgets();
    void toolTip();
    void toolTipCompact();
    void galleryHasNoLeak();
    void leakPageShowsLeak();
    void iconStrokes();
    void toolBarPaddings();
    void headerViews();
    void treeView();
    void titleFonts();
    void messageBoxButtons();
    void messageBoxScrimTarget();
    void overlayScrollBars();
    void toolTipOfQt();
    void plainTextLines();
    void dial();
    void tabBarScrolls();
    void progressBarThickness();
    void framePrimitive();
    void itemEditorFactory();
    void itemEditing();
    void chipsAndSegments();
    void pointerTargets();
    void subMenuMargin();
    void popupPlacement();
    void contextMenus();
    void components();
    void disabledAndSelect();
    void rowStyles();

    // R3: members and slots, which replaced the lambdas of the style, and the safe constructs
    void styleToolBarConnections();
    void styleSplitterHover();
    void styleTextPaletteBinder();
    void styleThemeChangeDeletesWidgets();
    void styleSafeConstructs();

    // Fixes of the review of the lambdas (S2, S12): bounded sizes and caches of the painters
    void styleReviewFixes();

    // Fixes of the code review of the library (Pdf4QtLibFluent/CODE_REVIEW.md: F05, F07, F09, F10, F12, F13)
    void reviewFontsWithoutGuiApplication();
    void reviewIconCacheKeys();
    void reviewSegmentRemoval();
    void reviewDialDeletedBySignal();
    void reviewClassFontsRestored();
    void searchFieldFocusIcon();

private:
    static constexpr const char* CORE_FONTS_VARIABLE = "PDF4QT_WIDGET_STYLE_TEST_CORE_FONTS";

    static QImage render(const std::function<void(QPainter&)>& paint, QSize size, qreal scale = 1.0);
    static QColor pixel(const QImage& image, qreal x, qreal y);
    static bool isNear(const QColor& first, const QColor& second, int tolerance = 2);
    static QColor blend(const QColor& top, const QColor& bottom);
    static int countLeakPixels(const QImage& image);
    static QColor token(Color color) { return pdf::PDFStylePainter::color(color); }

    pdf::PDFWidgetStyle* m_style = nullptr;
};

QImage WidgetStyleTest::render(const std::function<void(QPainter&)>& paint, QSize size, qreal scale)
{
    QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    paint(painter);
    painter.end();
    return image;
}

QColor WidgetStyleTest::pixel(const QImage& image, qreal x, qreal y)
{
    // Coordinates are logical, the center of the logical pixel is read
    const qreal scale = image.devicePixelRatio();
    return image.pixelColor(qFloor((x + 0.5) * scale), qFloor((y + 0.5) * scale));
}

bool WidgetStyleTest::isNear(const QColor& first, const QColor& second, int tolerance)
{
    return qAbs(first.red() - second.red()) <= tolerance && qAbs(first.green() - second.green()) <= tolerance &&
           qAbs(first.blue() - second.blue()) <= tolerance && qAbs(first.alpha() - second.alpha()) <= tolerance;
}

QColor WidgetStyleTest::blend(const QColor& top, const QColor& bottom)
{
    const qreal alpha = top.alphaF();
    return QColor::fromRgbF(top.redF() * alpha + bottom.redF() * (1.0 - alpha),
                            top.greenF() * alpha + bottom.greenF() * (1.0 - alpha),
                            top.blueF() * alpha + bottom.blueF() * (1.0 - alpha));
}

int WidgetStyleTest::countLeakPixels(const QImage& image)
{
    const QColor leak = pdf::PDFThemeColors::getLeakColor();
    int count = 0;

    for (int y = 0; y < image.height(); ++y)
    {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            const QRgb value = line[x];
            if (qAlpha(value) == 255 && qAbs(qRed(value) - leak.red()) <= 6 && qAbs(qGreen(value) - leak.green()) <= 6 && qAbs(qBlue(value) - leak.blue()) <= 6)
            {
                ++count;
            }
        }
    }

    return count;
}

void WidgetStyleTest::initTestCase()
{
    m_style = new pdf::PDFWidgetStyle();
    QApplication::setStyle(m_style);
    QVERIFY(pdf::PDFWidgetStyle::isActive());
}

void WidgetStyleTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    pdf::PDFThemeManager::getInstance()->setReducedMotion(false);
    pdf::PDFThemeManager::getInstance()->setFollowingSystemColorScheme(false);
    QApplication::setLayoutDirection(Qt::LeftToRight);
    m_style->setAnimationTime(-1);
}

void WidgetStyleTest::themeTokens()
{
    QCOMPARE(static_cast<int>(pdf::PDFThemeColors::ColorCount), 146);

    // The name of each enumeration value is derived from the name of the variable of the design
    const QMetaEnum metaEnum = QMetaEnum::fromType<pdf::PDFThemeColors::Color>();
    QSet<QByteArray> names;

    for (int i = 0; i < pdf::PDFThemeColors::ColorCount; ++i)
    {
        const Color color = static_cast<Color>(i);
        const QByteArray name = pdf::PDFThemeColors::getTokenName(color);
        QVERIFY(!name.isEmpty());
        QVERIFY2(!names.contains(name), name.constData());
        names.insert(name);

        QByteArray expectedKey;
        bool isUpper = true;
        for (const char character : name)
        {
            if (character == '/' || character == '-')
            {
                isUpper = true;
                continue;
            }
            expectedKey.append(isUpper ? QChar::fromLatin1(character).toUpper().toLatin1() : character);
            isUpper = false;
        }

        QCOMPARE(QByteArray(metaEnum.valueToKey(i)), expectedKey);
        QCOMPARE(pdf::PDFThemeColors::getColorFromTokenName(name), color);

        for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
        {
            const QByteArray value = pdf::PDFThemeColors::getBuiltinTokenValue(color, scheme);
            QVERIFY2(value.size() == 7 || value.size() == 9, name.constData());
            QVERIFY(value.startsWith('#'));
        }
    }

    QCOMPARE(pdf::PDFThemeColors::getColorFromTokenName("no/such-token"), pdf::PDFThemeColors::ColorCount);

    const std::vector<pdf::PDFThemeColors> themes = pdf::PDFThemeColors::getBuiltinThemes();
    QCOMPARE(themes.size(), size_t(2));
    QCOMPARE(themes[0].getId(), QStringLiteral("light"));
    QVERIFY(!themes[0].isDark());
    QCOMPARE(themes[1].getId(), QStringLiteral("dark"));
    QVERIFY(themes[1].isDark());

    // Values of the design: opaque, and translucent with the opacity as the last byte
    QCOMPARE(themes[0].getColor(pdf::PDFThemeColors::BgAccent), QColor(0x1a, 0x73, 0xe8));
    QCOMPARE(themes[1].getColor(pdf::PDFThemeColors::BgAccent), QColor(0x3b, 0x82, 0xf6));
    QCOMPARE(themes[0].getColor(pdf::PDFThemeColors::BgHover), QColor(0x0f, 0x17, 0x2a, 0x0f));
    QCOMPARE(themes[1].getColor(pdf::PDFThemeColors::EffectShadow), QColor(0x00, 0x00, 0x00, 0x73));
}

void WidgetStyleTest::themeTokensChecksum()
{
    // Checksum of "<name> <light> <dark>\n" of all variables of the collection
    // Color, computed in the design file by a script (FNV-1a, 32 bits)
    QByteArray text;
    for (int i = 0; i < pdf::PDFThemeColors::ColorCount; ++i)
    {
        const Color color = static_cast<Color>(i);
        text += pdf::PDFThemeColors::getTokenName(color) + ' ' +
                pdf::PDFThemeColors::getBuiltinTokenValue(color, pdf::PDFThemeColors::Scheme::Light) + ' ' +
                pdf::PDFThemeColors::getBuiltinTokenValue(color, pdf::PDFThemeColors::Scheme::Dark) + '\n';
    }

    quint32 hash = 0x811c9dc5u;
    for (const char character : text)
    {
        hash ^= static_cast<uchar>(character);
        hash *= 0x01000193u;
    }

    QCOMPARE(text.size(), qsizetype(4843));
    QCOMPARE(hash, quint32(0xf09f395du));
}

void WidgetStyleTest::themeManager()
{
    pdf::PDFThemeManager* manager = pdf::PDFThemeManager::getInstance();
    QSignalSpy spy(manager, &pdf::PDFThemeManager::themeChanged);
    const quint64 serial = manager->getSerial();

    QVERIFY(!manager->setTheme(QStringLiteral("no-such-theme")));
    QCOMPARE(spy.count(), 0);

    QVERIFY(manager->setTheme(QStringLiteral("dark")));
    QCOMPARE(spy.count(), 1);
    QVERIFY(manager->getSerial() != serial);
    QVERIFY(manager->getTheme().isDark());

    // The same theme again changes nothing
    QVERIFY(manager->setTheme(QStringLiteral("dark")));
    QCOMPARE(spy.count(), 1);

    // A new theme is a new table of colors
    pdf::PDFThemeColors custom(QStringLiteral("custom-dark"), QStringLiteral("Custom"), pdf::PDFThemeColors::Scheme::Dark);
    custom.setColor(pdf::PDFThemeColors::BgAccent, QColor(1, 2, 3));
    manager->registerTheme(custom);
    QCOMPARE(manager->getThemes().size(), size_t(3));
    QVERIFY(manager->setTheme(QStringLiteral("custom-dark")));
    QCOMPARE(manager->getColor(pdf::PDFThemeColors::BgAccent), QColor(1, 2, 3));

    // Replacing the active theme applies its colors
    custom.setColor(pdf::PDFThemeColors::BgAccent, QColor(4, 5, 6));
    manager->registerTheme(custom);
    QCOMPARE(manager->getColor(pdf::PDFThemeColors::BgAccent), QColor(4, 5, 6));
    QCOMPARE(manager->getThemes().size(), size_t(3));

    manager->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QVERIFY(!manager->getTheme().isDark());
}

void WidgetStyleTest::systemColorScheme()
{
#ifdef PDF4QT_TEST_PLATFORM_THEME
    // A platform theme, which keeps a requested scheme (as QWindowsTheme); the offscreen
    // platform ignores the requests, QStyleHints::colorScheme would be always Unknown
    pdf::PDFThemeManager* themeManager = pdf::PDFThemeManager::getInstance();
    QVERIFY(!themeManager->isFollowingSystemColorScheme());    // default
    ColorSchemeTestTheme platformTheme(Qt::ColorScheme::Dark);

    // The style requests the scheme of the theme (native dialogs, title bars)
    themeManager->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Dark);
    themeManager->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Light);
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Light);

    // A theme following the system leaves the scheme to the system, at once
    QSignalSpy followingSpy(themeManager, &pdf::PDFThemeManager::followingSystemColorSchemeChanged);
    QSignalSpy themeSpy(themeManager, &pdf::PDFThemeManager::themeChanged);
    themeManager->setFollowingSystemColorScheme(true);
    QVERIFY(themeManager->isFollowingSystemColorScheme());
    QCOMPARE(followingSpy.count(), 1);
    QCOMPARE(themeSpy.count(), 0);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Dark);
    const int requestCount = platformTheme.getRequestCount();
    themeManager->setFollowingSystemColorScheme(true);
    QCOMPARE(followingSpy.count(), 1);
    QCOMPARE(platformTheme.getRequestCount(), requestCount);

    // ... also after a change of the theme, and the changes of the system are seen
    themeManager->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    QSignalSpy schemeSpy(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged);
    platformTheme.setSystemScheme(Qt::ColorScheme::Light);
    QCOMPARE(schemeSpy.count(), 1);
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Light);

    // Not following again: the scheme of the theme is requested at once
    themeManager->setFollowingSystemColorScheme(false);
    QCOMPARE(followingSpy.count(), 2);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Dark);
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Dark);
    platformTheme.setSystemScheme(Qt::ColorScheme::Dark);
    platformTheme.setSystemScheme(Qt::ColorScheme::Light);
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Dark);

    // The style released by the application removes its request in both modes,
    // the style preparing the application follows the mode
    m_style->unpolish(qApp);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    themeManager->setFollowingSystemColorScheme(true);
    themeManager->setFollowingSystemColorScheme(false);
    themeManager->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);     // not prepared, no request
    m_style->polish(qApp);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Light);
    m_style->unpolish(qApp);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    themeManager->setFollowingSystemColorScheme(true);
    m_style->polish(qApp);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Light);
    m_style->unpolish(qApp);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    themeManager->setFollowingSystemColorScheme(false);
    m_style->polish(qApp);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Light);

    // The application changes the theme from the signal of the changed scheme, which
    // the request delivers at once (nested request): the requests converge, the leak
    // palette of the style stays
    themeManager->setFollowingSystemColorScheme(true);
    platformTheme.setSystemScheme(Qt::ColorScheme::Dark);
    QObject receiver;
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &receiver, [themeManager](Qt::ColorScheme scheme)
    {
        themeManager->setScheme(scheme == Qt::ColorScheme::Dark ? pdf::PDFThemeColors::Scheme::Dark : pdf::PDFThemeColors::Scheme::Light);
    });
    QCOMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Light);
    themeManager->setFollowingSystemColorScheme(false);     // requests Light: the scheme changes
    themeManager->setFollowingSystemColorScheme(true);      // Dark of the system: the theme follows
    QCOMPARE(themeManager->getTheme().getScheme(), pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(platformTheme.getRequestedScheme(), Qt::ColorScheme::Unknown);
    // The outermost request applies the palettes at once (the nested ones are guarded)
    QVERIFY(QApplication::palette() == pdf::PDFThemeColors::createLeakPalette());
    QCOMPARE(QToolTip::palette().color(QPalette::Active, QPalette::ToolTipBase), themeManager->getTheme().getColor(pdf::PDFThemeColors::BgSurface));
    QCoreApplication::processEvents();
    QVERIFY(QApplication::palette() == pdf::PDFThemeColors::createLeakPalette());
#else
    QSKIP("Needs the private headers of QtGui (Qt6::GuiPrivate)");
#endif
}

void WidgetStyleTest::leakPalette()
{
    const QColor leak = pdf::PDFThemeColors::getLeakColor();

    for (const QPalette& palette : { QApplication::palette(), m_style->standardPalette(), pdf::PDFThemeColors::createLeakPalette() })
    {
        for (int group = 0; group < QPalette::NColorGroups; ++group)
        {
            for (int role = 0; role < QPalette::NColorRoles; ++role)
            {
                if (role == QPalette::NoRole)
                {
                    continue;
                }
                QCOMPARE(palette.color(static_cast<QPalette::ColorGroup>(group), static_cast<QPalette::ColorRole>(role)), leak);
            }
        }
    }

    // The palette can not be replaced, while the style is active
    QApplication::setPalette(QPalette(Qt::white));
    QTRY_COMPARE(QApplication::palette().color(QPalette::Window), leak);
}

void WidgetStyleTest::fonts()
{
    QVERIFY(pdf::PDFThemeFonts::loadFonts());
    QCOMPARE(pdf::PDFThemeFonts::getFamily(), QStringLiteral("Inter"));

    const QFont body = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body);
    QCOMPARE(body.pixelSize(), 13);
    QCOMPARE(body.weight(), QFont::Normal);
    QCOMPARE(QFontInfo(body).family(), QStringLiteral("Inter"));

    QCOMPARE(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold).weight(), QFont::DemiBold);
    QCOMPARE(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Caption).pixelSize(), 11);
    QCOMPARE(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Caption).weight(), QFont::Medium);
    QCOMPARE(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Title).pixelSize(), 15);
    QCOMPARE(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::DialogTitle).pixelSize(), 16);

    // The style sets the font of the application
    QCOMPARE(QApplication::font().family(), QStringLiteral("Inter"));
    QCOMPARE(QApplication::font().pixelSize(), 13);

    // Weights are separate faces of the font, their texts differ in width
    const QString sample = QStringLiteral("Browse Files");
    QVERIFY(pdf::PDFStylePainter::textWidth(sample, pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold)) >
            pdf::PDFStylePainter::textWidth(sample, pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body)));
}

void WidgetStyleTest::icons()
{
    const QStringList names = pdf::PDFThemeIcons::getIconNames();
    QVERIFY(names.size() >= 300);

    for (const char* name : { "check", "minus", "plus", "x", "x-circle", "chevron-up", "chevron-down", "chevron-left", "chevron-right", "calendar", "search", "eye", "eye-off" })
    {
        QVERIFY2(pdf::PDFThemeIcons::hasIcon(QString::fromLatin1(name)), name);
    }

    QVERIFY(!pdf::PDFThemeIcons::hasIcon(QStringLiteral("no-such-icon")));
    QVERIFY(pdf::PDFThemeIcons::getIcon(QStringLiteral("no-such-icon")).isNull());

    const QIcon icon = pdf::PDFThemeIcons::getIcon(QStringLiteral("check"));
    QVERIFY(!icon.isNull());
    QCOMPARE(pdf::PDFThemeIcons::getIconName(icon), QStringLiteral("check"));
    QVERIFY(pdf::PDFThemeIcons::getIconName(QIcon()).isEmpty());

    // The icon is filled by the given color, whatever color its file has
    const QPixmap pixmap = pdf::PDFThemeIcons::getPixmap(QStringLiteral("check"), QSizeF(16, 16), 2.0, QColor(255, 0, 0));
    QCOMPARE(pixmap.size(), QSize(32, 32));
    QCOMPARE(pixmap.devicePixelRatio(), 2.0);

    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    int covered = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            const QColor color = image.pixelColor(x, y);
            if (color.alpha() > 0)
            {
                ++covered;
                QCOMPARE(color.red(), 255);
                QCOMPARE(color.green(), 0);
                QCOMPARE(color.blue(), 0);
            }
        }
    }
    QVERIFY(covered > 20);

    // Modes of the icon are colors of the theme
    QCOMPARE(pdf::PDFThemeIcons::getModeColor(QIcon::Normal), pdf::PDFThemeColors::IconDefault);
    QCOMPARE(pdf::PDFThemeIcons::getModeColor(QIcon::Disabled), pdf::PDFThemeColors::IconDisabled);
    QCOMPARE(pdf::PDFThemeIcons::getModeColor(QIcon::Active), pdf::PDFThemeColors::IconStrong);
    QCOMPARE(pdf::PDFThemeIcons::getModeColor(QIcon::Selected), pdf::PDFThemeColors::IconAccent);

    // Each standard pixmap of the table of the design has its icon
    const QMetaEnum metaEnum = QMetaEnum::fromType<QStyle::StandardPixmap>();
    for (const char* key : { "SP_MessageBoxInformation", "SP_MessageBoxWarning", "SP_MessageBoxCritical", "SP_MessageBoxQuestion", "SP_DialogOkButton", "SP_DialogCancelButton",
                             "SP_DialogSaveButton", "SP_DialogOpenButton", "SP_ArrowUp", "SP_ArrowDown", "SP_ArrowLeft", "SP_ArrowRight", "SP_DirIcon", "SP_FileIcon",
                             "SP_TrashIcon", "SP_BrowserReload", "SP_MediaPlay", "SP_LineEditClearButton", "SP_TitleBarNormalButton", "SP_DockWidgetCloseButton",
                             "SP_ToolBarHorizontalExtensionButton", "SP_ToolBarVerticalExtensionButton", "SP_VistaShield", "SP_DriveHDIcon", "SP_ComputerIcon" })
    {
        const int value = metaEnum.keyToValue(key);
        QVERIFY2(value >= 0, key);
        QVERIFY2(!m_style->standardIcon(static_cast<QStyle::StandardPixmap>(value)).isNull(), key);
        QVERIFY2(!m_style->standardPixmap(static_cast<QStyle::StandardPixmap>(value)).isNull(), key);
    }

    QCOMPARE(m_style->standardPixmap(QStyle::SP_MessageBoxCritical).deviceIndependentSize().toSize(), QSize(36, 36));
}

void WidgetStyleTest::pixelMetrics_data()
{
    QTest::addColumn<int>("metric");
    QTest::addColumn<int>("expected");

    // Table "Pixel metrics" of the design
    QTest::newRow("PM_DefaultFrameWidth") << int(QStyle::PM_DefaultFrameWidth) << 1;
    QTest::newRow("PM_SpinBoxFrameWidth") << int(QStyle::PM_SpinBoxFrameWidth) << 1;
    QTest::newRow("PM_ComboBoxFrameWidth") << int(QStyle::PM_ComboBoxFrameWidth) << 1;
    QTest::newRow("PM_MenuPanelWidth") << int(QStyle::PM_MenuPanelWidth) << 1;
    QTest::newRow("PM_ButtonMargin") << int(QStyle::PM_ButtonMargin) << 16;
    QTest::newRow("PM_ButtonDefaultIndicator") << int(QStyle::PM_ButtonDefaultIndicator) << 0;
    QTest::newRow("PM_ButtonShiftHorizontal") << int(QStyle::PM_ButtonShiftHorizontal) << 0;
    QTest::newRow("PM_ButtonShiftVertical") << int(QStyle::PM_ButtonShiftVertical) << 0;
    QTest::newRow("PM_MenuButtonIndicator") << int(QStyle::PM_MenuButtonIndicator) << 14;
    QTest::newRow("PM_ButtonIconSize") << int(QStyle::PM_ButtonIconSize) << 16;
    QTest::newRow("PM_SmallIconSize") << int(QStyle::PM_SmallIconSize) << 16;
    QTest::newRow("PM_ListViewIconSize") << int(QStyle::PM_ListViewIconSize) << 16;
    QTest::newRow("PM_ToolBarIconSize") << int(QStyle::PM_ToolBarIconSize) << 20;
    QTest::newRow("PM_LargeIconSize") << int(QStyle::PM_LargeIconSize) << 32;
    QTest::newRow("PM_IconViewIconSize") << int(QStyle::PM_IconViewIconSize) << 32;
    QTest::newRow("PM_MessageBoxIconSize") << int(QStyle::PM_MessageBoxIconSize) << 36;
    QTest::newRow("PM_IndicatorWidth") << int(QStyle::PM_IndicatorWidth) << 18;
    QTest::newRow("PM_IndicatorHeight") << int(QStyle::PM_IndicatorHeight) << 18;
    QTest::newRow("PM_ExclusiveIndicatorWidth") << int(QStyle::PM_ExclusiveIndicatorWidth) << 18;
    QTest::newRow("PM_ExclusiveIndicatorHeight") << int(QStyle::PM_ExclusiveIndicatorHeight) << 18;
    QTest::newRow("PM_CheckBoxLabelSpacing") << int(QStyle::PM_CheckBoxLabelSpacing) << 8;
    QTest::newRow("PM_RadioButtonLabelSpacing") << int(QStyle::PM_RadioButtonLabelSpacing) << 8;
    QTest::newRow("PM_MenuHMargin") << int(QStyle::PM_MenuHMargin) << 6;
    QTest::newRow("PM_MenuVMargin") << int(QStyle::PM_MenuVMargin) << 6;
    QTest::newRow("PM_MenuScrollerHeight") << int(QStyle::PM_MenuScrollerHeight) << 20;
    QTest::newRow("PM_MenuTearoffHeight") << int(QStyle::PM_MenuTearoffHeight) << 0;
    QTest::newRow("PM_MenuDesktopFrameWidth") << int(QStyle::PM_MenuDesktopFrameWidth) << 0;
    QTest::newRow("PM_MenuBarItemSpacing") << int(QStyle::PM_MenuBarItemSpacing) << 2;
    QTest::newRow("PM_MenuBarHMargin") << int(QStyle::PM_MenuBarHMargin) << 6;
    QTest::newRow("PM_MenuBarVMargin") << int(QStyle::PM_MenuBarVMargin) << 4;
    QTest::newRow("PM_MenuBarPanelWidth") << int(QStyle::PM_MenuBarPanelWidth) << 0;
    QTest::newRow("PM_ToolBarFrameWidth") << int(QStyle::PM_ToolBarFrameWidth) << 0;
    QTest::newRow("PM_ToolBarItemMargin") << int(QStyle::PM_ToolBarItemMargin) << 0;
    QTest::newRow("PM_ToolBarItemSpacing") << int(QStyle::PM_ToolBarItemSpacing) << 2;
    QTest::newRow("PM_ToolBarHandleExtent") << int(QStyle::PM_ToolBarHandleExtent) << 0;
    QTest::newRow("PM_ToolBarSeparatorExtent") << int(QStyle::PM_ToolBarSeparatorExtent) << 13;
    QTest::newRow("PM_ToolBarExtensionExtent") << int(QStyle::PM_ToolBarExtensionExtent) << 36;
    QTest::newRow("PM_TabBarTabHSpace") << int(QStyle::PM_TabBarTabHSpace) << 28;
    QTest::newRow("PM_TabBarTabOverlap") << int(QStyle::PM_TabBarTabOverlap) << 0;
    QTest::newRow("PM_TabBarTabShiftHorizontal") << int(QStyle::PM_TabBarTabShiftHorizontal) << 0;
    QTest::newRow("PM_TabBarTabShiftVertical") << int(QStyle::PM_TabBarTabShiftVertical) << 0;
    QTest::newRow("PM_TabBarScrollButtonWidth") << int(QStyle::PM_TabBarScrollButtonWidth) << 28;
    QTest::newRow("PM_TabCloseIndicatorWidth") << int(QStyle::PM_TabCloseIndicatorWidth) << 20;
    QTest::newRow("PM_TabCloseIndicatorHeight") << int(QStyle::PM_TabCloseIndicatorHeight) << 20;
    QTest::newRow("PM_ScrollBarExtent") << int(QStyle::PM_ScrollBarExtent) << 12;
    QTest::newRow("PM_ScrollBarSliderMin") << int(QStyle::PM_ScrollBarSliderMin) << 32;
    QTest::newRow("PM_ScrollView_ScrollBarSpacing") << int(QStyle::PM_ScrollView_ScrollBarSpacing) << 0;
    QTest::newRow("PM_ScrollView_ScrollBarOverlap") << int(QStyle::PM_ScrollView_ScrollBarOverlap) << 0;
    QTest::newRow("PM_SliderThickness") << int(QStyle::PM_SliderThickness) << 20;
    QTest::newRow("PM_SliderControlThickness") << int(QStyle::PM_SliderControlThickness) << 14;
    QTest::newRow("PM_SliderLength") << int(QStyle::PM_SliderLength) << 14;
    QTest::newRow("PM_SliderTickmarkOffset") << int(QStyle::PM_SliderTickmarkOffset) << 4;
    QTest::newRow("PM_ProgressBarChunkWidth") << int(QStyle::PM_ProgressBarChunkWidth) << 1;
    QTest::newRow("PM_SplitterWidth") << int(QStyle::PM_SplitterWidth) << 6;
    QTest::newRow("PM_DockWidgetSeparatorExtent") << int(QStyle::PM_DockWidgetSeparatorExtent) << 6;
    QTest::newRow("PM_DockWidgetTitleMargin") << int(QStyle::PM_DockWidgetTitleMargin) << 12;
    QTest::newRow("PM_DockWidgetTitleBarButtonMargin") << int(QStyle::PM_DockWidgetTitleBarButtonMargin) << 4;
    QTest::newRow("PM_DockWidgetFrameWidth") << int(QStyle::PM_DockWidgetFrameWidth) << 0;
    QTest::newRow("PM_HeaderMargin") << int(QStyle::PM_HeaderMargin) << 12;
    QTest::newRow("PM_HeaderMarkSize") << int(QStyle::PM_HeaderMarkSize) << 12;
    QTest::newRow("PM_HeaderGripMargin") << int(QStyle::PM_HeaderGripMargin) << 3;
    QTest::newRow("PM_HeaderDefaultSectionSizeHorizontal") << int(QStyle::PM_HeaderDefaultSectionSizeHorizontal) << 120;
    QTest::newRow("PM_HeaderDefaultSectionSizeVertical") << int(QStyle::PM_HeaderDefaultSectionSizeVertical) << 30;
    QTest::newRow("PM_TreeViewIndentation") << int(QStyle::PM_TreeViewIndentation) << 16;
    QTest::newRow("PM_TextCursorWidth") << int(QStyle::PM_TextCursorWidth) << 1;
    QTest::newRow("PM_SizeGripSize") << int(QStyle::PM_SizeGripSize) << 16;
    QTest::newRow("PM_ToolTipLabelFrameWidth") << int(QStyle::PM_ToolTipLabelFrameWidth) << 1;
    QTest::newRow("PM_LayoutLeftMargin") << int(QStyle::PM_LayoutLeftMargin) << 16;
    QTest::newRow("PM_LayoutHorizontalSpacing") << int(QStyle::PM_LayoutHorizontalSpacing) << 8;
    QTest::newRow("PM_LayoutVerticalSpacing") << int(QStyle::PM_LayoutVerticalSpacing) << 8;
}

void WidgetStyleTest::pixelMetrics()
{
    QFETCH(int, metric);
    QFETCH(int, expected);

    QCOMPARE(m_style->pixelMetric(static_cast<QStyle::PixelMetric>(metric), nullptr, nullptr), expected);
}

void WidgetStyleTest::styleHints_data()
{
    QTest::addColumn<int>("hint");
    QTest::addColumn<int>("expected");

    // Table "Style hints" of the design
    QTest::newRow("SH_UnderlineShortcut") << int(QStyle::SH_UnderlineShortcut) << 0;
    QTest::newRow("SH_DialogButtonLayout") << int(QStyle::SH_DialogButtonLayout) << int(QDialogButtonBox::MacLayout);
    QTest::newRow("SH_DialogButtonBox_ButtonsHaveIcons") << int(QStyle::SH_DialogButtonBox_ButtonsHaveIcons) << 0;
    QTest::newRow("SH_FocusFrame_AboveWidget") << int(QStyle::SH_FocusFrame_AboveWidget) << 1;
    QTest::newRow("SH_FocusFrame_Mask") << int(QStyle::SH_FocusFrame_Mask) << 0;
    QTest::newRow("SH_ItemView_ShowDecorationSelected") << int(QStyle::SH_ItemView_ShowDecorationSelected) << 1;
    QTest::newRow("SH_ItemView_ActivateItemOnSingleClick") << int(QStyle::SH_ItemView_ActivateItemOnSingleClick) << 0;
    QTest::newRow("SH_ItemView_ChangeHighlightOnFocus") << int(QStyle::SH_ItemView_ChangeHighlightOnFocus) << 1;
    QTest::newRow("SH_ItemView_ArrowKeysNavigateIntoChildren") << int(QStyle::SH_ItemView_ArrowKeysNavigateIntoChildren) << 1;
    QTest::newRow("SH_ItemView_EllipsisLocation") << int(QStyle::SH_ItemView_EllipsisLocation) << int(Qt::AlignRight);
    QTest::newRow("SH_ItemView_DrawDelegateFrame") << int(QStyle::SH_ItemView_DrawDelegateFrame) << 0;
    QTest::newRow("SH_ItemView_PaintAlternatingRowColorsForEmptyArea") << int(QStyle::SH_ItemView_PaintAlternatingRowColorsForEmptyArea) << 1;
    QTest::newRow("SH_ItemView_ScrollMode") << int(QStyle::SH_ItemView_ScrollMode) << int(QAbstractItemView::ScrollPerPixel);
    QTest::newRow("SH_ComboBox_Popup") << int(QStyle::SH_ComboBox_Popup) << 0;
    QTest::newRow("SH_ComboBox_ListMouseTracking") << int(QStyle::SH_ComboBox_ListMouseTracking) << 1;
    QTest::newRow("SH_ComboBox_AllowWheelScrolling") << int(QStyle::SH_ComboBox_AllowWheelScrolling) << 0;
    QTest::newRow("SH_SpinBox_AnimateButton") << int(QStyle::SH_SpinBox_AnimateButton) << 1;
    QTest::newRow("SH_SpinBox_KeyPressAutoRepeatRate") << int(QStyle::SH_SpinBox_KeyPressAutoRepeatRate) << 75;
    QTest::newRow("SH_SpinBox_ClickAutoRepeatThreshold") << int(QStyle::SH_SpinBox_ClickAutoRepeatThreshold) << 500;
    QTest::newRow("SH_SpinBox_ButtonsInsideFrame") << int(QStyle::SH_SpinBox_ButtonsInsideFrame) << 1;
    QTest::newRow("SH_Menu_MouseTracking") << int(QStyle::SH_Menu_MouseTracking) << 1;
    QTest::newRow("SH_Menu_SubMenuPopupDelay") << int(QStyle::SH_Menu_SubMenuPopupDelay) << 200;
    QTest::newRow("SH_Menu_FillScreenWithScroll") << int(QStyle::SH_Menu_FillScreenWithScroll) << 0;
    QTest::newRow("SH_Menu_Scrollable") << int(QStyle::SH_Menu_Scrollable) << 1;
    QTest::newRow("SH_Menu_SubMenuUniDirection") << int(QStyle::SH_Menu_SubMenuUniDirection) << 1;
    QTest::newRow("SH_Menu_SubMenuSloppyCloseTimeout") << int(QStyle::SH_Menu_SubMenuSloppyCloseTimeout) << 1000;
    QTest::newRow("SH_Menu_SelectionWrap") << int(QStyle::SH_Menu_SelectionWrap) << 1;
    QTest::newRow("SH_Menu_KeyboardSearch") << int(QStyle::SH_Menu_KeyboardSearch) << 1;
    QTest::newRow("SH_Menu_FadeOutOnHide") << int(QStyle::SH_Menu_FadeOutOnHide) << 0;
    QTest::newRow("SH_Menu_SupportsSections") << int(QStyle::SH_Menu_SupportsSections) << 1;
    QTest::newRow("SH_MenuBar_MouseTracking") << int(QStyle::SH_MenuBar_MouseTracking) << 1;
    QTest::newRow("SH_MenuBar_AltKeyNavigation") << int(QStyle::SH_MenuBar_AltKeyNavigation) << 1;
    QTest::newRow("SH_MainWindow_SpaceBelowMenuBar") << int(QStyle::SH_MainWindow_SpaceBelowMenuBar) << 0;
    QTest::newRow("SH_ScrollBar_Transient") << int(QStyle::SH_ScrollBar_Transient) << 0;
    QTest::newRow("SH_ScrollBar_ContextMenu") << int(QStyle::SH_ScrollBar_ContextMenu) << 0;
    QTest::newRow("SH_ScrollBar_LeftClickAbsolutePosition") << int(QStyle::SH_ScrollBar_LeftClickAbsolutePosition) << 1;
    QTest::newRow("SH_ScrollBar_MiddleClickAbsolutePosition") << int(QStyle::SH_ScrollBar_MiddleClickAbsolutePosition) << 1;
    QTest::newRow("SH_ScrollBar_ScrollWhenPointerLeavesControl") << int(QStyle::SH_ScrollBar_ScrollWhenPointerLeavesControl) << 1;
    QTest::newRow("SH_ScrollBar_RollBetweenButtons") << int(QStyle::SH_ScrollBar_RollBetweenButtons) << 0;
    QTest::newRow("SH_Slider_AbsoluteSetButtons") << int(QStyle::SH_Slider_AbsoluteSetButtons) << int(Qt::LeftButton);
    QTest::newRow("SH_Slider_PageSetButtons") << int(QStyle::SH_Slider_PageSetButtons) << int(Qt::MiddleButton);
    QTest::newRow("SH_Slider_StopMouseOverSlider") << int(QStyle::SH_Slider_StopMouseOverSlider) << 1;
    QTest::newRow("SH_TabBar_Alignment") << int(QStyle::SH_TabBar_Alignment) << int(Qt::AlignLeft);
    QTest::newRow("SH_TabBar_CloseButtonPosition") << int(QStyle::SH_TabBar_CloseButtonPosition) << int(QTabBar::RightSide);
    QTest::newRow("SH_TabBar_ElideMode") << int(QStyle::SH_TabBar_ElideMode) << int(Qt::ElideRight);
    QTest::newRow("SH_TabBar_SelectMouseType") << int(QStyle::SH_TabBar_SelectMouseType) << int(QEvent::MouseButtonPress);
    QTest::newRow("SH_TabBar_AllowWheelScrolling") << int(QStyle::SH_TabBar_AllowWheelScrolling) << 0;
    QTest::newRow("SH_TabWidget_DefaultTabPosition") << int(QStyle::SH_TabWidget_DefaultTabPosition) << int(QTabWidget::North);
    QTest::newRow("SH_GroupBox_TextLabelVerticalAlignment") << int(QStyle::SH_GroupBox_TextLabelVerticalAlignment) << int(Qt::AlignTop);
    QTest::newRow("SH_Header_ArrowAlignment") << int(QStyle::SH_Header_ArrowAlignment) << int(Qt::AlignRight | Qt::AlignVCenter);
    QTest::newRow("SH_ToolTip_WakeUpDelay") << int(QStyle::SH_ToolTip_WakeUpDelay) << 500;
    QTest::newRow("SH_ToolTip_FallAsleepDelay") << int(QStyle::SH_ToolTip_FallAsleepDelay) << 2000;
    QTest::newRow("SH_ToolTipLabel_Opacity") << int(QStyle::SH_ToolTipLabel_Opacity) << 255;
    QTest::newRow("SH_ToolTip_Mask") << int(QStyle::SH_ToolTip_Mask) << 0;
    QTest::newRow("SH_ToolBox_SelectedPageTitleBold") << int(QStyle::SH_ToolBox_SelectedPageTitleBold) << 0;
    QTest::newRow("SH_ToolButton_PopupDelay") << int(QStyle::SH_ToolButton_PopupDelay) << 300;
    QTest::newRow("SH_ToolButtonStyle") << int(QStyle::SH_ToolButtonStyle) << int(Qt::ToolButtonIconOnly);
    QTest::newRow("SH_MessageBox_CenterButtons") << int(QStyle::SH_MessageBox_CenterButtons) << 0;
    QTest::newRow("SH_MessageBox_UseBorderForButtonSpacing") << int(QStyle::SH_MessageBox_UseBorderForButtonSpacing) << 0;
    QTest::newRow("SH_MessageBox_TextInteractionFlags") << int(QStyle::SH_MessageBox_TextInteractionFlags) << int(Qt::TextSelectableByMouse);
    QTest::newRow("SH_ProgressDialog_CenterCancelButton") << int(QStyle::SH_ProgressDialog_CenterCancelButton) << 0;
    QTest::newRow("SH_ProgressDialog_TextLabelAlignment") << int(QStyle::SH_ProgressDialog_TextLabelAlignment) << int(Qt::AlignLeft);
    QTest::newRow("SH_LineEdit_PasswordCharacter") << int(QStyle::SH_LineEdit_PasswordCharacter) << 0x2022;
    QTest::newRow("SH_LineEdit_PasswordMaskDelay") << int(QStyle::SH_LineEdit_PasswordMaskDelay) << 0;
    QTest::newRow("SH_Widget_Animate") << int(QStyle::SH_Widget_Animate) << 1;
    QTest::newRow("SH_Widget_Animation_Duration") << int(QStyle::SH_Widget_Animation_Duration) << 120;
    QTest::newRow("SH_Widget_ShareActivation") << int(QStyle::SH_Widget_ShareActivation) << 0;
    QTest::newRow("SH_Workspace_FillSpaceOnMaximize") << int(QStyle::SH_Workspace_FillSpaceOnMaximize) << 0;
    QTest::newRow("SH_RequestSoftwareInputPanel") << int(QStyle::SH_RequestSoftwareInputPanel) << int(QStyle::RSIP_OnMouseClick);
    QTest::newRow("SH_SpinControls_DisableOnBounds") << int(QStyle::SH_SpinControls_DisableOnBounds) << 1;
    QTest::newRow("SH_DockWidget_ButtonsHaveFrame") << int(QStyle::SH_DockWidget_ButtonsHaveFrame) << 0;
    QTest::newRow("SH_EtchDisabledText") << int(QStyle::SH_EtchDisabledText) << 0;
    QTest::newRow("SH_DitherDisabledText") << int(QStyle::SH_DitherDisabledText) << 0;
    QTest::newRow("SH_PrintDialog_RightAlignButtons") << int(QStyle::SH_PrintDialog_RightAlignButtons) << 1;
    QTest::newRow("SH_FontDialog_SelectAssociatedText") << int(QStyle::SH_FontDialog_SelectAssociatedText) << 1;
    QTest::newRow("SH_Button_FocusPolicy") << int(QStyle::SH_Button_FocusPolicy) << int(Qt::StrongFocus);
}

void WidgetStyleTest::styleHints()
{
    QFETCH(int, hint);
    QFETCH(int, expected);

    QCOMPARE(m_style->styleHint(static_cast<QStyle::StyleHint>(hint), nullptr, nullptr, nullptr), expected);

    if (hint == QStyle::SH_Widget_Animation_Duration)
    {
        // The animations are turned off by the setting of the reduced motion
        pdf::PDFThemeManager::getInstance()->setReducedMotion(true);
        QCOMPARE(m_style->styleHint(QStyle::SH_Widget_Animation_Duration), 0);
    }
}

void WidgetStyleTest::sizesFromContents()
{
    const QFont semiBold = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold);
    const int textWidth = qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("Cancel"), semiBold));

    // Push button: height 36, paddings 16 / 18, border 1 of the secondary button
    QStyleOptionButton button;
    button.text = QStringLiteral("Cancel");
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_PushButton, &button, QSize(), nullptr), QSize(textWidth + 36, 36));

    button.features = QStyleOptionButton::DefaultButton;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_PushButton, &button, QSize(), nullptr), QSize(textWidth + 34, 36));

    button.features = QStyleOptionButton::None;
    button.icon = pdf::PDFThemeIcons::getIcon(QStringLiteral("save"));
    button.iconSize = QSize(16, 16);
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_PushButton, &button, QSize(), nullptr), QSize(textWidth + 36 + 24, 36));

    button.features = QStyleOptionButton::HasMenu;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_PushButton, &button, QSize(), nullptr), QSize(textWidth + 36 + 24 + 16, 36));

    button.features = QStyleOptionButton::None;
    button.text.clear();
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_PushButton, &button, QSize(), nullptr), QSize(36, 36));

    // Tool button of a toolbar: 36 x 36, with a menu indicator + 14, split + 18
    QStyleOptionToolButton toolButton;
    toolButton.icon = pdf::PDFThemeIcons::getIcon(QStringLiteral("save"));
    toolButton.iconSize = QSize(20, 20);
    toolButton.toolButtonStyle = Qt::ToolButtonIconOnly;
    toolButton.subControls = QStyle::SC_ToolButton;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_ToolButton, &toolButton, QSize(), nullptr), QSize(36, 36));

    toolButton.features = QStyleOptionToolButton::HasMenu;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_ToolButton, &toolButton, QSize(), nullptr), QSize(50, 36));

    toolButton.features = QStyleOptionToolButton::HasMenu | QStyleOptionToolButton::MenuButtonPopup;
    toolButton.subControls |= QStyle::SC_ToolButtonMenu;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_ToolButton, &toolButton, QSize(), nullptr), QSize(54, 36));

    toolButton.features = QStyleOptionToolButton::None;
    toolButton.subControls = QStyle::SC_ToolButton;
    toolButton.text = QStringLiteral("Highlight");
    toolButton.toolButtonStyle = Qt::ToolButtonTextUnderIcon;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_ToolButton, &toolButton, QSize(), nullptr), QSize(88, 56));

    // Check box and radio button: indicator 18 + gap 8 + text
    QStyleOptionButton checkBox;
    checkBox.text = QStringLiteral("Enable");
    const int labelWidth = qCeil(pdf::PDFStylePainter::textWidth(checkBox.text, pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body)));
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_CheckBox, &checkBox, QSize(), nullptr), QSize(26 + labelWidth, 18));
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_RadioButton, &checkBox, QSize(), nullptr), QSize(26 + labelWidth, 18));

    // Fields: 34 high
    QStyleOptionFrame frame;
    frame.lineWidth = 1;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_LineEdit, &frame, QSize(100, 16), nullptr).height(), 34);

    QStyleOptionSpinBox spinBox;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_SpinBox, &spinBox, QSize(50, 16), nullptr).height(), 34);

    QStyleOptionComboBox comboBox;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_ComboBox, &comboBox, QSize(120, 16), nullptr), QSize(120 + 13 + 8 + 14 + 11, 34));

    // Menu: item 32, separator 7, title of a section 24, item of the menu bar 28
    QStyleOptionMenuItem menuItem;
    menuItem.menuItemType = QStyleOptionMenuItem::Normal;
    menuItem.text = QStringLiteral("Save");
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_MenuItem, &menuItem, QSize(), nullptr).height(), 32);
    QVERIFY(m_style->sizeFromContents(QStyle::CT_MenuItem, &menuItem, QSize(), nullptr).width() >= 186);

    menuItem.menuItemType = QStyleOptionMenuItem::Separator;
    menuItem.text.clear();
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_MenuItem, &menuItem, QSize(), nullptr).height(), 7);
    menuItem.text = QStringLiteral("Document");
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_MenuItem, &menuItem, QSize(), nullptr).height(), 24);
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_MenuBarItem, &menuItem, QSize(), nullptr).height(), 28);
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_MenuBar, nullptr, QSize(100, 10), nullptr).height(), 36);

    // Tabs: chip 32 with the margin of the focus ring, row of the navigation 240 x 38 with the gap 2
    QStyleOptionTab tab;
    tab.shape = QTabBar::RoundedNorth;
    tab.text = QStringLiteral("Details");
    tab.tabIndex = -1;
    const int chipText = qCeil(pdf::PDFStylePainter::textWidth(tab.text, pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::SmallSemiBold)));
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_TabBarTab, &tab, QSize(), nullptr), QSize(qMax(64, chipText + 28 + 2) + 6, 36));

    // The selected chip keeps the width (padding 15 instead of the border 1 and the padding 14)
    tab.state |= QStyle::State_Selected;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_TabBarTab, &tab, QSize(), nullptr), QSize(qMax(64, chipText + 28 + 2) + 6, 36));

    tab.shape = QTabBar::RoundedWest;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_TabBarTab, &tab, QSize(), nullptr), QSize(240, 40));

    // Slider 20 (28 with ticks), progress bar 16, header 28, item of a view 30
    QStyleOptionSlider slider;
    slider.orientation = Qt::Horizontal;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_Slider, &slider, QSize(100, 10), nullptr), QSize(100, 20));
    slider.tickPosition = QSlider::TicksBelow;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_Slider, &slider, QSize(100, 10), nullptr), QSize(100, 28));
    slider.orientation = Qt::Vertical;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_Slider, &slider, QSize(10, 100), nullptr), QSize(28, 100));

    QStyleOptionProgressBar progressBar;
    progressBar.state |= QStyle::State_Horizontal;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_ProgressBar, &progressBar, QSize(100, 10), nullptr), QSize(100, 16));

    QStyleOptionHeader header;
    header.orientation = Qt::Horizontal;
    header.text = QStringLiteral("Name");
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_HeaderSection, &header, QSize(), nullptr).height(), 28);

    QStyleOptionViewItem viewItem;
    viewItem.features = QStyleOptionViewItem::HasDisplay;
    viewItem.text = QStringLiteral("Contract.pdf");
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_ItemViewItem, &viewItem, QSize(), nullptr).height(), 30);
}

void WidgetStyleTest::pushButtonPixels_data()
{
    QTest::addColumn<QString>("theme");
    QTest::addColumn<qreal>("scale");

    QTest::newRow("light 1x") << QStringLiteral("light") << 1.0;
    QTest::newRow("light 2x") << QStringLiteral("light") << 2.0;
    QTest::newRow("dark 1x") << QStringLiteral("dark") << 1.0;
    QTest::newRow("dark 1.5x") << QStringLiteral("dark") << 1.5;
}

void WidgetStyleTest::pushButtonPixels()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);

    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QSize size(148, 36);
    auto draw = [this, size, scale](QStyleOptionButton option, QStyle::State state)
    {
        option.rect = QRect(QPoint(0, 0), size);
        option.state = state;
        return render([this, &option](QPainter& painter) { m_style->drawControl(QStyle::CE_PushButtonBevel, &option, &painter, nullptr); }, size, scale);
    };

    const QStyle::State enabled = QStyle::State_Enabled | QStyle::State_Active;

    // Primary button: accent fill without a border, radius 8
    QStyleOptionButton primary;
    primary.features = QStyleOptionButton::DefaultButton;
    QImage image = draw(primary, enabled);
    QVERIFY(isNear(pixel(image, 74, 18), token(pdf::PDFThemeColors::BgAccent)));
    QVERIFY(isNear(pixel(image, 0, 18), token(pdf::PDFThemeColors::BgAccent)));
    QCOMPARE(pixel(image, 0, 0).alpha(), 0);
    QCOMPARE(pixel(image, 147, 35).alpha(), 0);
    QCOMPARE(pixel(image, 3, 3).alpha(), 255);
    QCOMPARE(pixel(image, 8, 0).alpha(), 255);

    image = draw(primary, enabled | QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 74, 18), token(pdf::PDFThemeColors::BgAccentHover)));
    image = draw(primary, enabled | QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(isNear(pixel(image, 74, 18), token(pdf::PDFThemeColors::BgAccentPressed)));
    image = draw(primary, QStyle::State_Active);
    QVERIFY(isNear(pixel(image, 74, 18), token(pdf::PDFThemeColors::BgMuted)));

    // Secondary button: surface with a border of 1 px inside; overlays of hover and press
    QStyleOptionButton secondary;
    image = draw(secondary, enabled);
    QVERIFY(isNear(pixel(image, 74, 18), token(pdf::PDFThemeColors::BgSurface)));
    QVERIFY(isNear(pixel(image, 0, 18), token(pdf::PDFThemeColors::BorderDefault)));
    QVERIFY(isNear(pixel(image, 74, 0), token(pdf::PDFThemeColors::BorderDefault)));
    QVERIFY(isNear(pixel(image, 74, 35), token(pdf::PDFThemeColors::BorderDefault)));
    QVERIFY(isNear(pixel(image, 2, 18), token(pdf::PDFThemeColors::BgSurface)));
    QCOMPARE(pixel(image, 0, 0).alpha(), 0);

    image = draw(secondary, enabled | QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 74, 18), blend(token(pdf::PDFThemeColors::BgHover), token(pdf::PDFThemeColors::BgSurface))));
    image = draw(secondary, enabled | QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(isNear(pixel(image, 74, 18), blend(token(pdf::PDFThemeColors::BgPressed), token(pdf::PDFThemeColors::BgSurface))));
    QVERIFY(isNear(pixel(image, 0, 18), token(pdf::PDFThemeColors::BorderStrong)));

    // The focus ring replaces the border of the secondary button
    image = draw(secondary, enabled | QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange);
    QVERIFY(isNear(pixel(image, 0, 18), token(pdf::PDFThemeColors::BgSurface)));

    // Flat button draws nothing at rest
    QStyleOptionButton flat;
    flat.features = QStyleOptionButton::Flat;
    image = draw(flat, enabled);
    QCOMPARE(pixel(image, 74, 18).alpha(), 0);
    image = draw(flat, enabled | QStyle::State_MouseOver);
    QCOMPARE(pixel(image, 74, 18).alpha(), token(pdf::PDFThemeColors::BgHover).alpha());

    // Checked button: accent border
    image = draw(secondary, enabled | QStyle::State_On);
    QVERIFY(isNear(pixel(image, 0, 18), token(pdf::PDFThemeColors::BorderFocus)));
    QVERIFY(isNear(pixel(image, 74, 18), blend(token(pdf::PDFThemeColors::BgAccentSubtle), token(pdf::PDFThemeColors::BgSurface))));
}

void WidgetStyleTest::fieldPixels()
{
    const QSize size(200, 34);
    auto draw = [this, size](QStyle::State state, const QWidget* widget)
    {
        QStyleOptionFrame option;
        option.rect = QRect(QPoint(0, 0), size);
        option.state = state;
        option.lineWidth = 1;
        return render([this, &option, widget](QPainter& painter) { m_style->drawPrimitive(QStyle::PE_PanelLineEdit, &option, &painter, widget); }, size);
    };

    const QStyle::State enabled = QStyle::State_Enabled | QStyle::State_Active;

    QImage image = draw(enabled, nullptr);
    QVERIFY(isNear(pixel(image, 100, 17), token(pdf::PDFThemeColors::BgSurface)));
    QVERIFY(isNear(pixel(image, 0, 17), token(pdf::PDFThemeColors::BorderDefault)));
    QVERIFY(isNear(pixel(image, 1, 17), token(pdf::PDFThemeColors::BgSurface)));
    QCOMPARE(pixel(image, 0, 0).alpha(), 0);

    image = draw(enabled | QStyle::State_MouseOver, nullptr);
    QVERIFY(isNear(pixel(image, 0, 17), token(pdf::PDFThemeColors::BorderStrong)));

    // The focus ring of 2 px lies inside of the field and it is shown also after a click
    image = draw(enabled | QStyle::State_HasFocus, nullptr);
    QVERIFY(isNear(pixel(image, 0, 17), token(pdf::PDFThemeColors::BorderFocus)));
    QVERIFY(isNear(pixel(image, 1, 17), token(pdf::PDFThemeColors::BorderFocus)));
    QVERIFY(isNear(pixel(image, 2, 17), token(pdf::PDFThemeColors::BgSurface)));

    image = draw(QStyle::State_Active, nullptr);
    QVERIFY(isNear(pixel(image, 100, 17), token(pdf::PDFThemeColors::BgMuted)));

    // Invalid and read-only fields
    pdf::PDFLineEdit invalidEdit;
    invalidEdit.setInvalid(true);
    image = draw(enabled, &invalidEdit);
    QVERIFY(isNear(pixel(image, 0, 17), token(pdf::PDFThemeColors::TextDanger)));

    pdf::PDFLineEdit readOnlyEdit;
    readOnlyEdit.setReadOnly(true);
    image = draw(enabled, &readOnlyEdit);
    QVERIFY(isNear(pixel(image, 100, 17), token(pdf::PDFThemeColors::BgSubtle)));

    // Frameless field draws nothing
    QStyleOptionFrame frameless;
    frameless.rect = QRect(QPoint(0, 0), size);
    frameless.state = enabled;
    frameless.lineWidth = 0;
    image = render([this, &frameless](QPainter& painter) { m_style->drawPrimitive(QStyle::PE_PanelLineEdit, &frameless, &painter, nullptr); }, size);
    QCOMPARE(pixel(image, 100, 17).alpha(), 0);
}

void WidgetStyleTest::indicatorPixels()
{
    const QSize size(18, 18);
    auto draw = [this, size](QStyle::PrimitiveElement element, QStyle::State state)
    {
        QStyleOptionButton option;
        option.rect = QRect(QPoint(0, 0), size);
        option.state = state;
        return render([this, &option, element](QPainter& painter) { m_style->drawPrimitive(element, &option, &painter, nullptr); }, size);
    };

    const QStyle::State enabled = QStyle::State_Enabled | QStyle::State_Active;

    QImage image = draw(QStyle::PE_IndicatorCheckBox, enabled | QStyle::State_Off);
    QVERIFY(isNear(pixel(image, 9, 9), token(pdf::PDFThemeColors::BgSurface)));
    QVERIFY(isNear(pixel(image, 0, 9), token(pdf::PDFThemeColors::BorderStrong)));

    image = draw(QStyle::PE_IndicatorCheckBox, enabled | QStyle::State_On);
    QVERIFY(isNear(pixel(image, 2, 9), token(pdf::PDFThemeColors::BgAccent)));
    QVERIFY(isNear(pixel(image, 0, 9), token(pdf::PDFThemeColors::BgAccent)));

    image = draw(QStyle::PE_IndicatorCheckBox, enabled | QStyle::State_NoChange);
    QVERIFY(isNear(pixel(image, 2, 3), token(pdf::PDFThemeColors::BgAccent)));
    QVERIFY(pixel(image, 9, 9).red() > token(pdf::PDFThemeColors::BgAccent).red() + 40);

    image = draw(QStyle::PE_IndicatorCheckBox, QStyle::State_Active | QStyle::State_On);
    QVERIFY(isNear(pixel(image, 2, 9), token(pdf::PDFThemeColors::BorderStrong)));

    // Hover and pressed (decision 23 of components.md), both themes and fractional scales:
    // unchecked bg/surface + bg/hover / bg/pressed with a border icon/muted 1.5 px,
    // checked bg/accent-hover without a border, pressed bg/accent-pressed
    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            auto drawScaled = [this, size, scale](QStyle::PrimitiveElement element, QStyle::State state)
            {
                QStyleOptionButton option;
                option.rect = QRect(QPoint(0, 0), size);
                option.state = state;
                return render([this, &option, element](QPainter& painter) { m_style->drawPrimitive(element, &option, &painter, nullptr); }, size, scale);
            };

            const QColor surface = token(pdf::PDFThemeColors::BgSurface);
            for (const QStyle::PrimitiveElement element : { QStyle::PE_IndicatorCheckBox, QStyle::PE_IndicatorRadioButton })
            {
                image = drawScaled(element, enabled | QStyle::State_Off | QStyle::State_MouseOver);
                QVERIFY(isNear(pixel(image, 5, 9), blend(token(pdf::PDFThemeColors::BgHover), surface), 3));
                image = drawScaled(element, enabled | QStyle::State_Off | QStyle::State_MouseOver | QStyle::State_Sunken);
                QVERIFY(isNear(pixel(image, 5, 9), blend(token(pdf::PDFThemeColors::BgPressed), surface), 3));

                image = drawScaled(element, enabled | QStyle::State_On | QStyle::State_MouseOver);
                QVERIFY(isNear(pixel(image, 0.5, 9), token(pdf::PDFThemeColors::BgAccentHover), 3));
                QVERIFY(isNear(pixel(image, 3, 9), token(pdf::PDFThemeColors::BgAccentHover), 3));
                image = drawScaled(element, enabled | QStyle::State_On | QStyle::State_MouseOver | QStyle::State_Sunken);
                QVERIFY(isNear(pixel(image, 3, 9), token(pdf::PDFThemeColors::BgAccentPressed), 3));
            }

            image = drawScaled(QStyle::PE_IndicatorCheckBox, enabled | QStyle::State_Off | QStyle::State_MouseOver);
            QVERIFY(isNear(pixel(image, 0, 9), token(pdf::PDFThemeColors::IconMuted), 3));
            image = drawScaled(QStyle::PE_IndicatorCheckBox, enabled | QStyle::State_Off | QStyle::State_MouseOver | QStyle::State_Sunken);
            QVERIFY(isNear(pixel(image, 0, 9), token(pdf::PDFThemeColors::IconMuted), 3));
        }
    }
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light")));

    // Unchecked disabled indicator: bg/muted with border/default
    image = draw(QStyle::PE_IndicatorCheckBox, QStyle::State_Active | QStyle::State_Off);
    QVERIFY(isNear(pixel(image, 9, 9), token(pdf::PDFThemeColors::BgMuted)));
    QVERIFY(isNear(pixel(image, 0, 9), token(pdf::PDFThemeColors::BorderDefault)));

    // Unchecked indicator with the focus has the ring inside
    image = draw(QStyle::PE_IndicatorCheckBox, enabled | QStyle::State_Off | QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange);
    QVERIFY(isNear(pixel(image, 1, 9), token(pdf::PDFThemeColors::BorderFocus)));

    image = draw(QStyle::PE_IndicatorRadioButton, enabled | QStyle::State_On);
    QVERIFY(isNear(pixel(image, 2, 9), token(pdf::PDFThemeColors::BgAccent)));
    QVERIFY(isNear(pixel(image, 9, 9), token(pdf::PDFThemeColors::IconOnAccent)));
    QCOMPARE(pixel(image, 0, 0).alpha(), 0);
}

void WidgetStyleTest::subControlRects()
{
    // Spin box: stepper of 20 px, which ends 7 px before the right edge
    QStyleOptionSpinBox spinBox;
    spinBox.rect = QRect(0, 0, 140, 34);
    spinBox.buttonSymbols = QAbstractSpinBox::UpDownArrows;
    QCOMPARE(m_style->subControlRect(QStyle::CC_SpinBox, &spinBox, QStyle::SC_SpinBoxUp, nullptr), QRect(113, 1, 20, 16));
    QCOMPARE(m_style->subControlRect(QStyle::CC_SpinBox, &spinBox, QStyle::SC_SpinBoxDown, nullptr), QRect(113, 17, 20, 16));
    QCOMPARE(m_style->subControlRect(QStyle::CC_SpinBox, &spinBox, QStyle::SC_SpinBoxFrame, nullptr), spinBox.rect);
    QCOMPARE(m_style->subControlRect(QStyle::CC_SpinBox, &spinBox, QStyle::SC_SpinBoxEditField, nullptr).right() + 1, 105);

    spinBox.buttonSymbols = QAbstractSpinBox::NoButtons;
    QVERIFY(m_style->subControlRect(QStyle::CC_SpinBox, &spinBox, QStyle::SC_SpinBoxUp, nullptr).isEmpty());

    // Combo box: chevron of 14 px, 11 px from the right edge
    QStyleOptionComboBox comboBox;
    comboBox.rect = QRect(0, 0, 260, 34);
    QCOMPARE(m_style->subControlRect(QStyle::CC_ComboBox, &comboBox, QStyle::SC_ComboBoxArrow, nullptr), QRect(235, 10, 14, 14));
    QCOMPARE(m_style->subControlRect(QStyle::CC_ComboBox, &comboBox, QStyle::SC_ComboBoxEditField, nullptr).left(), 13);

    comboBox.editable = true;
    QCOMPARE(m_style->subControlRect(QStyle::CC_ComboBox, &comboBox, QStyle::SC_ComboBoxArrow, nullptr), QRect(230, 5, 24, 24));

    // Scroll bar: no buttons, the slider is at least 32 px long
    QStyleOptionSlider scrollBar;
    scrollBar.rect = QRect(0, 0, 12, 200);
    scrollBar.orientation = Qt::Vertical;
    scrollBar.minimum = 0;
    scrollBar.maximum = 100000;
    scrollBar.pageStep = 10;
    QVERIFY(m_style->subControlRect(QStyle::CC_ScrollBar, &scrollBar, QStyle::SC_ScrollBarAddLine, nullptr).isEmpty());
    QVERIFY(m_style->subControlRect(QStyle::CC_ScrollBar, &scrollBar, QStyle::SC_ScrollBarSubLine, nullptr).isEmpty());
    QCOMPARE(m_style->subControlRect(QStyle::CC_ScrollBar, &scrollBar, QStyle::SC_ScrollBarGroove, nullptr), scrollBar.rect);
    QCOMPARE(m_style->subControlRect(QStyle::CC_ScrollBar, &scrollBar, QStyle::SC_ScrollBarSlider, nullptr).height(), 32);
    QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_ScrollBar, &scrollBar, QPoint(6, 10), nullptr), QStyle::SC_ScrollBarSlider);
    QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_ScrollBar, &scrollBar, QPoint(6, 150), nullptr), QStyle::SC_ScrollBarAddPage);

    scrollBar.maximum = 0;
    QVERIFY(!m_style->subControlRect(QStyle::CC_ScrollBar, &scrollBar, QStyle::SC_ScrollBarSlider, nullptr).isValid());

    // Slider: handle of 14 px on the axis of the groove
    QStyleOptionSlider slider;
    slider.rect = QRect(0, 0, 180, 20);
    slider.orientation = Qt::Horizontal;
    slider.minimum = 0;
    slider.maximum = 100;
    slider.sliderPosition = 50;
    QCOMPARE(m_style->subControlRect(QStyle::CC_Slider, &slider, QStyle::SC_SliderGroove, nullptr), QRect(0, 8, 180, 4));
    QCOMPARE(m_style->subControlRect(QStyle::CC_Slider, &slider, QStyle::SC_SliderHandle, nullptr), QRect(83, 3, 14, 14));

    // Check box: the indicator is centered vertically, the label follows after 8 px
    QStyleOptionButton checkBox;
    checkBox.rect = QRect(0, 0, 120, 34);
    QCOMPARE(m_style->subElementRect(QStyle::SE_CheckBoxIndicator, &checkBox, nullptr), QRect(0, 8, 18, 18));
    QCOMPARE(m_style->subElementRect(QStyle::SE_CheckBoxContents, &checkBox, nullptr), QRect(26, 0, 94, 34));

    // Split tool button: the menu part of 18 px
    QStyleOptionToolButton toolButton;
    toolButton.rect = QRect(0, 0, 54, 36);
    toolButton.features = QStyleOptionToolButton::MenuButtonPopup | QStyleOptionToolButton::HasMenu;
    toolButton.subControls = QStyle::SC_ToolButton | QStyle::SC_ToolButtonMenu;
    QCOMPARE(m_style->subControlRect(QStyle::CC_ToolButton, &toolButton, QStyle::SC_ToolButton, nullptr), QRect(0, 0, 36, 36));
    QCOMPARE(m_style->subControlRect(QStyle::CC_ToolButton, &toolButton, QStyle::SC_ToolButtonMenu, nullptr), QRect(36, 0, 18, 36));
}

void WidgetStyleTest::rightToLeft()
{
    QStyleOptionButton checkBox;
    checkBox.rect = QRect(0, 0, 120, 18);
    checkBox.direction = Qt::RightToLeft;
    QCOMPARE(m_style->subElementRect(QStyle::SE_CheckBoxIndicator, &checkBox, nullptr), QRect(102, 0, 18, 18));
    QCOMPARE(m_style->subElementRect(QStyle::SE_CheckBoxContents, &checkBox, nullptr), QRect(0, 0, 94, 18));

    QStyleOptionSpinBox spinBox;
    spinBox.rect = QRect(0, 0, 140, 34);
    spinBox.direction = Qt::RightToLeft;
    QCOMPARE(m_style->subControlRect(QStyle::CC_SpinBox, &spinBox, QStyle::SC_SpinBoxUp, nullptr), QRect(7, 1, 20, 16));

    QStyleOptionComboBox comboBox;
    comboBox.rect = QRect(0, 0, 260, 34);
    comboBox.direction = Qt::RightToLeft;
    QCOMPARE(m_style->subControlRect(QStyle::CC_ComboBox, &comboBox, QStyle::SC_ComboBoxArrow, nullptr), QRect(11, 10, 14, 14));

    QStyleOptionToolButton toolButton;
    toolButton.rect = QRect(0, 0, 54, 36);
    toolButton.direction = Qt::RightToLeft;
    toolButton.features = QStyleOptionToolButton::MenuButtonPopup | QStyleOptionToolButton::HasMenu;
    toolButton.subControls = QStyle::SC_ToolButton | QStyle::SC_ToolButtonMenu;
    QCOMPARE(m_style->subControlRect(QStyle::CC_ToolButton, &toolButton, QStyle::SC_ToolButtonMenu, nullptr), QRect(0, 0, 18, 36));
}

void WidgetStyleTest::nullSafety()
{
    // Each element of the style is asked without a widget (and without an option,
    // where the function allows it). Nothing may crash and nothing may leak.
    QImage image(120, 60, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);

    QStyleOption plainOption;
    plainOption.rect = QRect(0, 0, 120, 60);
    plainOption.state = QStyle::State_Enabled | QStyle::State_Active | QStyle::State_MouseOver | QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;

    QStyleOptionComplex complexOption;
    complexOption.rect = plainOption.rect;
    complexOption.state = plainOption.state;
    complexOption.subControls = QStyle::SC_All;
    complexOption.activeSubControls = QStyle::SC_All;

    const QMetaObject& metaObject = QStyle::staticMetaObject;

    const QMetaEnum primitives = metaObject.enumerator(metaObject.indexOfEnumerator("PrimitiveElement"));
    for (int i = 0; i < primitives.keyCount(); ++i)
    {
        m_style->drawPrimitive(static_cast<QStyle::PrimitiveElement>(primitives.value(i)), &plainOption, &painter, nullptr);
        m_style->drawPrimitive(static_cast<QStyle::PrimitiveElement>(primitives.value(i)), nullptr, &painter, nullptr);
        m_style->drawPrimitive(static_cast<QStyle::PrimitiveElement>(primitives.value(i)), &plainOption, nullptr, nullptr);
    }

    const QMetaEnum controls = metaObject.enumerator(metaObject.indexOfEnumerator("ControlElement"));
    for (int i = 0; i < controls.keyCount(); ++i)
    {
        m_style->drawControl(static_cast<QStyle::ControlElement>(controls.value(i)), &plainOption, &painter, nullptr);
        m_style->drawControl(static_cast<QStyle::ControlElement>(controls.value(i)), nullptr, &painter, nullptr);
    }

    const QMetaEnum complexControls = metaObject.enumerator(metaObject.indexOfEnumerator("ComplexControl"));
    for (int i = 0; i < complexControls.keyCount(); ++i)
    {
        const QStyle::ComplexControl control = static_cast<QStyle::ComplexControl>(complexControls.value(i));
        m_style->drawComplexControl(control, &complexOption, &painter, nullptr);
        m_style->drawComplexControl(control, nullptr, &painter, nullptr);
        m_style->hitTestComplexControl(control, &complexOption, QPoint(10, 10), nullptr);
        m_style->hitTestComplexControl(control, nullptr, QPoint(10, 10), nullptr);

        for (int bit = 0; bit < 16; ++bit)
        {
            m_style->subControlRect(control, &complexOption, static_cast<QStyle::SubControl>(1 << bit), nullptr);
            m_style->subControlRect(control, nullptr, static_cast<QStyle::SubControl>(1 << bit), nullptr);
        }
    }

    const QMetaEnum subElements = metaObject.enumerator(metaObject.indexOfEnumerator("SubElement"));
    for (int i = 0; i < subElements.keyCount(); ++i)
    {
        m_style->subElementRect(static_cast<QStyle::SubElement>(subElements.value(i)), &plainOption, nullptr);
        m_style->subElementRect(static_cast<QStyle::SubElement>(subElements.value(i)), nullptr, nullptr);
    }

    const QMetaEnum metrics = metaObject.enumerator(metaObject.indexOfEnumerator("PixelMetric"));
    for (int i = 0; i < metrics.keyCount(); ++i)
    {
        m_style->pixelMetric(static_cast<QStyle::PixelMetric>(metrics.value(i)), nullptr, nullptr);
        m_style->pixelMetric(static_cast<QStyle::PixelMetric>(metrics.value(i)), &plainOption, nullptr);
    }

    const QMetaEnum hints = metaObject.enumerator(metaObject.indexOfEnumerator("StyleHint"));
    for (int i = 0; i < hints.keyCount(); ++i)
    {
        m_style->styleHint(static_cast<QStyle::StyleHint>(hints.value(i)), nullptr, nullptr, nullptr);
        m_style->styleHint(static_cast<QStyle::StyleHint>(hints.value(i)), &plainOption, nullptr, nullptr);
    }

    const QMetaEnum contents = metaObject.enumerator(metaObject.indexOfEnumerator("ContentsType"));
    for (int i = 0; i < contents.keyCount(); ++i)
    {
        m_style->sizeFromContents(static_cast<QStyle::ContentsType>(contents.value(i)), &plainOption, QSize(40, 16), nullptr);
        m_style->sizeFromContents(static_cast<QStyle::ContentsType>(contents.value(i)), nullptr, QSize(40, 16), nullptr);
    }

    const QMetaEnum pixmaps = metaObject.enumerator(metaObject.indexOfEnumerator("StandardPixmap"));
    for (int i = 0; i < pixmaps.keyCount(); ++i)
    {
        m_style->standardIcon(static_cast<QStyle::StandardPixmap>(pixmaps.value(i)), nullptr, nullptr);
        m_style->standardPixmap(static_cast<QStyle::StandardPixmap>(pixmaps.value(i)), nullptr, nullptr);
    }

    m_style->drawItemText(&painter, plainOption.rect, Qt::AlignCenter, QPalette(), true, QStringLiteral("Text"), QPalette::WindowText);
    m_style->drawItemText(nullptr, plainOption.rect, Qt::AlignCenter, QPalette(), true, QStringLiteral("Text"), QPalette::WindowText);
    m_style->polish(static_cast<QWidget*>(nullptr));
    m_style->unpolish(static_cast<QWidget*>(nullptr));

    painter.end();
    QCOMPARE(countLeakPixels(image), 0);
}

void WidgetStyleTest::busyAnimation()
{
    const QSize size(180, 4);
    QProgressBar progressBar;

    auto draw = [this, size, &progressBar](qint64 time)
    {
        m_style->setAnimationTime(time);

        QStyleOptionProgressBar option;
        option.rect = QRect(QPoint(0, 0), size);
        option.state = QStyle::State_Enabled | QStyle::State_Horizontal;
        option.minimum = 0;
        option.maximum = 0;
        return render([this, &option, &progressBar](QPainter& painter)
        {
            m_style->drawControl(QStyle::CE_ProgressBarGroove, &option, &painter, &progressBar);
            m_style->drawControl(QStyle::CE_ProgressBarContents, &option, &painter, &progressBar);
        }, size);
    };

    auto findChunk = [](const QImage& image)
    {
        const QColor accent = pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgAccent);
        int first = -1;
        int last = -1;
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixelColor(x, 2) == accent)
            {
                first = first < 0 ? x : first;
                last = x;
            }
        }
        return QPair<int, int>(first, last);
    };

    // The chunk of 30 % of the groove moves with the time of the animation
    const QPair<int, int> start = findChunk(draw(300));
    const QPair<int, int> middle = findChunk(draw(600));
    const QPair<int, int> end = findChunk(draw(900));

    QVERIFY(start.first >= 0);
    QVERIFY(middle.first > start.first);
    QVERIFY(end.first > middle.first);
    QVERIFY(qAbs((middle.second - middle.first + 1) - 54) <= 2);

    // Without the animation the chunk rests
    pdf::PDFThemeManager::getInstance()->setReducedMotion(true);
    QCOMPARE(findChunk(draw(300)), findChunk(draw(900)));
}

void WidgetStyleTest::mnemonics()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPushButton* button = new QPushButton(QStringLiteral("&Apply"), &window);
    layout->addWidget(button);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    // Mnemonics are underlined after the Alt key has been pressed in the window
    QCOMPARE(m_style->styleHint(QStyle::SH_UnderlineShortcut, nullptr, button), 0);
    QTest::keyPress(button, Qt::Key_Alt);
    QCOMPARE(m_style->styleHint(QStyle::SH_UnderlineShortcut, nullptr, button), 1);
    QTest::keyRelease(button, Qt::Key_Alt);
    QCOMPARE(m_style->styleHint(QStyle::SH_UnderlineShortcut, nullptr, button), 1);

    // Another window is not affected
    QWidget other;
    QCOMPARE(m_style->styleHint(QStyle::SH_UnderlineShortcut, nullptr, &other), 0);
}

void WidgetStyleTest::keyboardFocusFrame()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPushButton* first = new QPushButton(QStringLiteral("First"), &window);
    QPushButton* second = new QPushButton(QStringLiteral("Second"), &window);
    QCheckBox* checkBox = new QCheckBox(QStringLiteral("Check"), &window);
    pdf::PDFLineEdit* lineEdit = new pdf::PDFLineEdit(&window);
    layout->addWidget(first);
    layout->addWidget(second);
    layout->addWidget(checkBox);
    layout->addWidget(lineEdit);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto findFrame = [&window]() -> QFocusFrame*
    {
        const QList<QFocusFrame*> frames = window.findChildren<QFocusFrame*>();
        for (QFocusFrame* frame : frames)
        {
            if (frame->widget())
            {
                return frame;
            }
        }
        return nullptr;
    };

    // The focus set by the mouse shows no ring
    first->setFocus(Qt::MouseFocusReason);
    QTest::mouseClick(first, Qt::LeftButton);
    QTest::qWait(50);
    QVERIFY(!findFrame());

    // The Tab key moves the focus and shows the ring around the button
    QTest::keyClick(first, Qt::Key_Tab);
    QTRY_VERIFY(findFrame());
    QCOMPARE(findFrame()->widget(), second);
    QCOMPARE(findFrame()->geometry(), second->geometry().adjusted(-2, -2, 2, 2));

    QTest::keyClick(second, Qt::Key_Tab);
    QTRY_VERIFY(findFrame() && findFrame()->widget() == checkBox);

    // A field has its ring inside, the frame is not used for it
    QTest::keyClick(checkBox, Qt::Key_Tab);
    QTRY_VERIFY(!findFrame());
    QVERIFY(lineEdit->hasFocus());

    // A click hides the ring
    QTest::keyClick(lineEdit, Qt::Key_Backtab);
    QTRY_VERIFY(findFrame());
    QTest::mouseClick(first, Qt::LeftButton);
    QTRY_VERIFY(!findFrame());
}

void WidgetStyleTest::themeSwitch()
{
    QPushButton button(QStringLiteral("Button"));
    button.setProperty(pdf::PDFWidgetStyleProperties::PRIMARY, true);

    // The pointer of the platform rests at the origin of the screen, the button must not be hovered
    button.move(300, 300);
    button.show();
    QVERIFY(QTest::qWaitForWindowExposed(&button));

    pdf::PDFLineEdit lineEdit;
    QCOMPARE(lineEdit.palette().color(QPalette::Text), pdf::PDFThemeColors().getColor(pdf::PDFThemeColors::TextPrimary));

    const QColor lightAccent = token(pdf::PDFThemeColors::BgAccent);
    QCOMPARE(button.grab().toImage().pixelColor(4, button.height() / 2), lightAccent);

    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    const QColor darkAccent = token(pdf::PDFThemeColors::BgAccent);
    QVERIFY(lightAccent != darkAccent);
    QCOMPARE(button.grab().toImage().pixelColor(4, button.height() / 2), darkAccent);

    // Widgets with their own palette follow the theme, the palette of the application stays
    QCOMPARE(lineEdit.palette().color(QPalette::Text), token(pdf::PDFThemeColors::TextPrimary));
    QCOMPARE(lineEdit.palette().color(QPalette::Window), pdf::PDFThemeColors::getLeakColor());
    QCOMPARE(QApplication::palette().color(QPalette::Text), pdf::PDFThemeColors::getLeakColor());
}

void WidgetStyleTest::widgets()
{
    // Line edit
    pdf::PDFLineEdit lineEdit(QStringLiteral("Text"));
    QVERIFY(!lineEdit.isInvalid());
    lineEdit.setInvalid(true);
    QVERIFY(lineEdit.isInvalid());
    QCOMPARE(lineEdit.sizeHint().height(), 34);
    lineEdit.setPill(true);
    QCOMPARE(lineEdit.sizeHint().height(), 36);
    lineEdit.setReadOnly(true);
    QCOMPARE(lineEdit.palette().color(QPalette::Text), token(pdf::PDFThemeColors::TextSecondary));

    pdf::PDFLineEdit passwordEdit;
    passwordEdit.setPasswordField();
    QCOMPARE(passwordEdit.echoMode(), QLineEdit::Password);
    QVERIFY(passwordEdit.getPasswordButton());
    passwordEdit.getPasswordButton()->toggle();
    QCOMPARE(passwordEdit.echoMode(), QLineEdit::Normal);
    QVERIFY(!passwordEdit.getPasswordButton()->accessibleName().isEmpty());

    // Spin boxes reserve the space of the prefix and of the suffix
    pdf::PDFSpinBox spinBox;
    const int plainWidth = spinBox.sizeHint().width();
    QCOMPARE(spinBox.sizeHint().height(), 34);
    spinBox.setDisplaySuffix(QStringLiteral("px"));
    QVERIFY(spinBox.sizeHint().width() > plainWidth);
    QCOMPARE(spinBox.getDisplaySuffix(), QStringLiteral("px"));
    QVERIFY(spinBox.suffix().isEmpty());
    spinBox.setDisplaySuffix(QString());
    QCOMPARE(spinBox.sizeHint().width(), plainWidth);

    pdf::PDFDoubleSpinBox doubleSpinBox;
    doubleSpinBox.setDisplayPrefix(QStringLiteral("x"));
    QCOMPARE(doubleSpinBox.getDisplayPrefix(), QStringLiteral("x"));

    // Date edit with the calendar of the library
    pdf::PDFDateTimeEdit dateEdit(QDate(2026, 10, 14));
    dateEdit.setCalendarPopupEnabled(true);
    QVERIFY(qobject_cast<pdf::PDFCalendarWidget*>(dateEdit.calendarWidget()));
    QCOMPARE(dateEdit.sizeHint().height(), 34);

    pdf::PDFCalendarWidget calendar;
    QCOMPARE(calendar.sizeHint(), QSize(210, 240));

    // Navigation of 32 px with buttons of 28 px
    QWidget* navigationBar = calendar.findChild<QWidget*>(QStringLiteral("qt_calendar_navigationbar"));
    QVERIFY(navigationBar);
    QCOMPARE(navigationBar->minimumHeight(), 32);
    QCOMPARE(navigationBar->maximumHeight(), 32);
    QVERIFY(!calendar.isGridVisible());

    // Combo box variants
    pdf::PDFComboBox comboBox;
    comboBox.addItems({ QStringLiteral("First"), QStringLiteral("Second") });
    QCOMPARE(comboBox.sizeHint().height(), 34);
    comboBox.setCompact(true);
    QCOMPARE(comboBox.sizeHint().height(), 28);
    comboBox.setCompact(false);
    comboBox.setSelectPrefix(QStringLiteral("Sort:"));
    QCOMPARE(comboBox.sizeHint().height(), 32);

    pdf::PDFComboBox editableComboBox;
    editableComboBox.setEditable(true);
    editableComboBox.show();
    QVERIFY(QTest::qWaitForWindowExposed(&editableComboBox));
    QCOMPARE(editableComboBox.lineEdit()->palette().color(QPalette::Text), token(pdf::PDFThemeColors::TextPrimary));

    // Label
    pdf::PDFLabel label(QStringLiteral("Label"), pdf::PDFThemeFonts::Role::Title, pdf::PDFThemeColors::TextMuted);
    QCOMPARE(label.font().pixelSize(), 15);
    QCOMPARE(label.getTextColor(), pdf::PDFThemeColors::TextMuted);
    label.resize(label.sizeHint());
    QCOMPARE(countLeakPixels(label.grab().toImage()), 0);

    // Segmented control
    pdf::PDFSegmentedControl segmentedControl;
    QSignalSpy spy(&segmentedControl, &pdf::PDFSegmentedControl::currentIndexChanged);
    QCOMPARE(segmentedControl.addSegment(QStringLiteral("Light")), 0);
    QCOMPARE(segmentedControl.addSegment(QStringLiteral("Dark")), 1);
    QCOMPARE(segmentedControl.getCount(), 2);
    QCOMPARE(segmentedControl.getCurrentIndex(), 0);
    segmentedControl.setCurrentIndex(1);
    QCOMPARE(segmentedControl.getCurrentIndex(), 1);
    QVERIFY(spy.count() >= 1);
    QCOMPARE(spy.last().at(0).toInt(), 1);
    QCOMPARE(segmentedControl.sizeHint().height(), 32);

    // Editors of items
    pdf::PDFItemEditorFactory factory;
    QWidget parent;
    QVERIFY(qobject_cast<pdf::PDFSpinBox*>(factory.createEditor(QMetaType::Int, &parent)));
    QVERIFY(qobject_cast<pdf::PDFDoubleSpinBox*>(factory.createEditor(QMetaType::Double, &parent)));
    QVERIFY(qobject_cast<pdf::PDFLineEdit*>(factory.createEditor(QMetaType::QString, &parent)));
    QVERIFY(qobject_cast<pdf::PDFDateTimeEdit*>(factory.createEditor(QMetaType::QDate, &parent)));
    QCOMPARE(factory.valuePropertyName(QMetaType::QString), QByteArray("text"));
    QCOMPARE(factory.valuePropertyName(QMetaType::Int), QByteArray("value"));

    // Message box
    pdf::PDFMessageBox messageBox(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    messageBox.setDetailedText(QStringLiteral("Details"));
    messageBox.setCheckBoxText(QStringLiteral("Do not show again"));
    messageBox.setDefaultButton(QDialogButtonBox::Ok);
    QCOMPARE(messageBox.accessibleName(), QStringLiteral("Title"));
    QCOMPARE(messageBox.accessibleDescription(), QStringLiteral("Text"));
    QVERIFY(messageBox.getButtonBox()->button(QDialogButtonBox::Ok)->isDefault());

    QTimer::singleShot(0, &messageBox, [&messageBox]() { messageBox.getButtonBox()->button(QDialogButtonBox::Cancel)->click(); });
    QCOMPARE(messageBox.exec(), int(QDialog::Rejected));
    QCOMPARE(messageBox.getClickedStandardButton(), QDialogButtonBox::Cancel);

    // Status bar
    pdf::PDFStatusBar statusBar;
    QToolButton* item = statusBar.createItem(QStringLiteral("151 %"), QString(), true);
    QVERIFY(pdf::PDFWidgetStyleProperties::isSet(item, pdf::PDFWidgetStyleProperties::STATUS_ITEM));
    statusBar.showMessage(QStringLiteral("Ready"));
    statusBar.resize(400, statusBar.sizeHint().height());
    QCOMPARE(countLeakPixels(statusBar.grab().toImage()), 0);
    QVERIFY(statusBar.minimumHeight() <= 28);
}

void WidgetStyleTest::toolTip()
{
    QPushButton button(QStringLiteral("Rotate"));
    button.setShortcut(QKeySequence(Qt::Key_F4));
    button.setToolTip(QStringLiteral("Rotate right\nRotates the selected pages by 90 degrees."));
    button.show();
    QVERIFY(QTest::qWaitForWindowExposed(&button));

    QVERIFY(!pdf::PDFToolTip::isVisible());
    QVERIFY(pdf::PDFToolTip::showForWidget(&button, button.mapToGlobal(QPoint(5, 5)), QPoint(5, 5)));
    QVERIFY(pdf::PDFToolTip::isVisible());
    QVERIFY(pdf::PDFToolTip::getWindow());
    QVERIFY(pdf::PDFToolTip::getWindow()->width() >= 260);
    QCOMPARE(countLeakPixels(pdf::PDFToolTip::getWindow()->grab().toImage()), 0);

    // A key hides the tooltip
    QTest::keyClick(&button, Qt::Key_A);
    QVERIFY(!pdf::PDFToolTip::isVisible());

    // A widget without a tooltip, and a rich text, are not handled
    QPushButton plain(QStringLiteral("Plain"));
    QVERIFY(!pdf::PDFToolTip::showForWidget(&plain, QPoint(), QPoint()));
    plain.setToolTip(QStringLiteral("<b>Rich</b> text"));
    QVERIFY(!pdf::PDFToolTip::showForWidget(&plain, QPoint(), QPoint()));

    pdf::PDFToolTip::showText(QPoint(10, 10), QStringLiteral("Title"));
    QVERIFY(pdf::PDFToolTip::isVisible());
    pdf::PDFToolTip::hideText();
    QVERIFY(!pdf::PDFToolTip::isVisible());
}

void WidgetStyleTest::toolTipCompact()
{
    // The default tooltip of a name only: the panel is 15 + 2 * 7 high; the rest
    // of the window is the margin of the shadow
    pdf::PDFToolTip::showText(QPoint(10, 10), QStringLiteral("Title"));
    QVERIFY(pdf::PDFToolTip::isVisible());
    QCOMPARE(pdf::PDFToolTip::getStyle(), pdf::PDFToolTip::Style::Default);
    QWidget* window = pdf::PDFToolTip::getWindow();
    QVERIFY(window);
    const int verticalMargins = window->height() - 29;
    const int horizontalMargins = window->width() - (qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("Title"), pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::SmallMedium))) + 22);

    // Link tooltip 1028:22472: the address and its kind Regular 11 in lines of 13 px,
    // gap 2, paddings 6 / 10 inside of the border: 300 x 42 for the address of the design
    const QString address = QStringLiteral("https://market-insight.example/reports/2026?ref=pdf");
    const QString kind = QStringLiteral("Web link \u00B7 opens in your web browser");
    const QFont font = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::CaptionRegular);
    pdf::PDFToolTip::showText(QPoint(10, 10), address, QString(), kind, nullptr, pdf::PDFToolTip::Style::Compact);
    QVERIFY(pdf::PDFToolTip::isVisible());
    QCOMPARE(pdf::PDFToolTip::getStyle(), pdf::PDFToolTip::Style::Compact);
    QCOMPARE(pdf::PDFToolTip::getTitle(), address);
    QCOMPARE(window->height() - verticalMargins, 42);
    const int expectedWidth = qCeil(qMax(pdf::PDFStylePainter::textWidth(address, font), pdf::PDFStylePainter::textWidth(kind, font))) + 22;
    QCOMPARE(window->width() - horizontalMargins, expectedWidth);
    QVERIFY2(qAbs(expectedWidth - 300) <= 3, qPrintable(QString::number(expectedWidth)));

    for (pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Dark, pdf::PDFThemeColors::Scheme::Light })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);
        const QImage image = window->grab().toImage();
        QCOMPARE(countLeakPixels(image), 0);

        // The name is drawn by text/primary, the kind by text/muted: the ink of the
        // name is stronger (darker in the light theme, lighter in the dark theme)
        // The panel starts at the first opaque pixel (its border) from the top / left
        const qreal ratio = image.devicePixelRatio();
        int panelTop = 0;
        while (panelTop < image.height() - 1 && qAlpha(image.pixel(image.width() / 2, panelTop)) < 255)
        {
            ++panelTop;
        }
        int panelLeft = 0;
        while (panelLeft < image.width() - 1 && qAlpha(image.pixel(panelLeft, panelTop + qRound(20 * ratio))) < 255)
        {
            ++panelLeft;
        }
        const QRect panel(qRound(panelLeft / ratio), qRound(panelTop / ratio), window->width() - horizontalMargins, window->height() - verticalMargins);
        auto strongestInk = [&image, &panel, scheme, ratio](int top, int bottom)
        {
            int strongest = scheme == pdf::PDFThemeColors::Scheme::Light ? 255 : 0;
            for (int y = qRound((panel.top() + top) * ratio); y < qRound((panel.top() + bottom) * ratio); ++y)
            {
                for (int x = qRound((panel.left() + 2) * ratio); x < qRound((panel.right() - 2) * ratio); ++x)
                {
                    const int lightness = image.pixelColor(x, y).lightness();
                    strongest = scheme == pdf::PDFThemeColors::Scheme::Light ? qMin(strongest, lightness) : qMax(strongest, lightness);
                }
            }
            return strongest;
        };
        const int titleInk = strongestInk(7, 20);
        const int kindInk = strongestInk(22, 35);
        const QString context = QString("%1 %2").arg(titleInk).arg(kindInk);
        QVERIFY2(scheme == pdf::PDFThemeColors::Scheme::Light ? titleInk < kindInk : titleInk > kindInk, qPrintable(context));
    }

    // Hostile texts: the lines are not wrapped, the width is bounded, at most 8 lines
    pdf::PDFToolTip::showText(QPoint(10, 10), QString(5000, QLatin1Char('w')), QStringLiteral("Ctrl+K"), QString(5000, QLatin1Char('\n')), nullptr, pdf::PDFToolTip::Style::Compact);
    QVERIFY(pdf::PDFToolTip::isVisible());
    QCOMPARE(window->width() - horizontalMargins, 480 + 22);
    QCOMPARE(window->height() - verticalMargins, 13 + 2 + 8 * 13 + 14);
    window->grab();
    pdf::PDFToolTip::showText(QPoint(10, 10), QStringLiteral("Title"), QString(), QString(), nullptr, static_cast<pdf::PDFToolTip::Style>(77));
    QCOMPARE(pdf::PDFToolTip::getStyle(), pdf::PDFToolTip::Style::Default);

    pdf::PDFToolTip::hideText();
    QCOMPARE(pdf::PDFToolTip::getStyle(), pdf::PDFToolTip::Style::Default);
}

void WidgetStyleTest::galleryHasNoLeak()
{
    // Pages of the gallery show all controls of the style and all widgets of the
    // library. No pixel of them may have the leak color of the palette.
    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

        QWidget window;
        QVBoxLayout* layout = new QVBoxLayout(&window);
        QScrollArea* scrollArea = new QScrollArea(&window);
        layout->addWidget(scrollArea);
        window.resize(1400, 900);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        const std::vector<pdffluentgallery::GalleryPages::Page> pages = pdffluentgallery::GalleryPages::createPages(nullptr);
        QVERIFY(pages.size() >= 18);

        for (const pdffluentgallery::GalleryPages::Page& page : pages)
        {
            // The scroll area would delete its previous widget
            scrollArea->takeWidget();
            scrollArea->setWidget(page.widget);
            scrollArea->setWidgetResizable(true);
            QApplication::processEvents();

            QImage image(page.widget->size(), QImage::Format_ARGB32_Premultiplied);
            image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
            {
                QPainter painter(&image);
                page.widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
            }

            const int leakPixels = countLeakPixels(image);
            if (leakPixels > 0)
            {
                image.save(QDir::temp().filePath(QStringLiteral("pdf4qt_leak_%1_%2.png").arg(page.id, theme)));
            }
            QVERIFY2(leakPixels == 0, qPrintable(QStringLiteral("%1 pixels of the leak color on the page %2 (%3)").arg(leakPixels).arg(page.id, theme)));
        }

        // The pages are deleted with the last scroll area content
        scrollArea->takeWidget();
        for (const pdffluentgallery::GalleryPages::Page& page : pages)
        {
            delete page.widget;
        }
    }
}

void WidgetStyleTest::leakPageShowsLeak()
{
    // The detector of the leaks works: widgets of Qt drawn from the palette are violet
    const pdffluentgallery::GalleryPages::Page page = pdffluentgallery::GalleryPages::createLeakPage(nullptr);

    QScrollArea scrollArea;
    scrollArea.setWidget(page.widget);
    scrollArea.setWidgetResizable(true);
    scrollArea.resize(1200, 700);
    scrollArea.show();
    QVERIFY(QTest::qWaitForWindowExposed(&scrollArea));

    QImage image(page.widget->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(pdf::PDFStylePainter::color(pdf::PDFThemeColors::BgCanvas));
    {
        QPainter painter(&image);
        page.widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    }

    QVERIFY(countLeakPixels(image) > 50);
}

void WidgetStyleTest::iconStrokes()
{
    // An icon of the design keeps its strokes of 2 px, when it is resized
    auto getCoverage = [](const QString& name, int size, qreal scale)
    {
        const QImage image = pdf::PDFThemeIcons::getPixmap(name, QSizeF(size, size), scale, Qt::black).toImage();
        qreal coverage = 0.0;
        for (int y = 0; y < image.height(); ++y)
        {
            for (int x = 0; x < image.width(); ++x)
            {
                coverage += image.pixelColor(x, y).alphaF();
            }
        }
        return coverage / (scale * scale);
    };

    // Two diagonals of the cross: length 2 * 8 * sqrt(2) at 16 px, width 2, round caps
    const qreal cross16 = getCoverage(QStringLiteral("x"), 16, 1.0);
    QVERIFY2(cross16 > 44.0 && cross16 < 56.0, qPrintable(QString::number(cross16)));

    const qreal cross24 = getCoverage(QStringLiteral("x"), 24, 1.0);
    QVERIFY2(cross24 > 66.0 && cross24 < 80.0, qPrintable(QString::number(cross24)));

    // The same icon on a screen with the scale 2 has the same logical coverage
    const qreal cross16Scaled = getCoverage(QStringLiteral("x"), 16, 2.0);
    QVERIFY2(qAbs(cross16Scaled - cross16) < 3.0, qPrintable(QString::number(cross16Scaled)));

    // Minus of 12 px is a line of two full rows
    const QImage minus = pdf::PDFThemeIcons::getPixmap(QStringLiteral("minus"), QSizeF(12, 12), 1.0, Qt::black).toImage();
    QCOMPARE(minus.size(), QSize(12, 12));
    QCOMPARE(minus.pixelColor(6, 5).alpha(), 255);
    QCOMPARE(minus.pixelColor(6, 6).alpha(), 255);
    QCOMPARE(minus.pixelColor(6, 4).alpha(), 0);
    QCOMPARE(minus.pixelColor(6, 7).alpha(), 0);
}

void WidgetStyleTest::toolBarPaddings()
{
    // Paddings of the panel: 12 along the bar and 8 across it
    QToolBar toolBar;
    QAction* action = toolBar.addAction(pdf::PDFThemeIcons::getIcon(QStringLiteral("save")), QStringLiteral("Save"));
    toolBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&toolBar));

    QWidget* button = toolBar.widgetForAction(action);
    QVERIFY(button);
    QCOMPARE(toolBar.sizeHint().height(), 52);
    QCOMPARE(button->geometry().topLeft(), QPoint(12, 8));
    QCOMPARE(button->size(), QSize(36, 36));

    // The paddings survive a change of the style of the toolbar
    QEvent styleChange(QEvent::StyleChange);
    QCoreApplication::sendEvent(&toolBar, &styleChange);
    QTRY_COMPARE(toolBar.sizeHint().height(), 52);
    QTRY_COMPARE(button->geometry().topLeft(), QPoint(12, 8));

    // A vertical toolbar has the paddings 8 / 12 (a window can not be so narrow,
    // the toolbar lies in a host widget)
    QWidget host;
    host.resize(300, 300);
    QToolBar verticalToolBar(&host);
    QAction* verticalAction = verticalToolBar.addAction(pdf::PDFThemeIcons::getIcon(QStringLiteral("save")), QStringLiteral("Save"));
    verticalToolBar.setOrientation(Qt::Vertical);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QCOMPARE(verticalToolBar.sizeHint().width(), 52);
    verticalToolBar.setGeometry(0, 0, 52, 200);
    QTRY_COMPARE(verticalToolBar.widgetForAction(verticalAction)->geometry(), QRect(8, 12, 36, 36));

    // The paddings follow a change of the orientation
    verticalToolBar.setOrientation(Qt::Horizontal);
    verticalToolBar.setGeometry(0, 0, 200, 52);
    QTRY_COMPARE(verticalToolBar.widgetForAction(verticalAction)->geometry(), QRect(12, 8, 36, 36));

    // The extension button of a narrow toolbar ends at the padding of the panel
    for (int i = 0; i < 8; ++i)
    {
        verticalToolBar.addAction(pdf::PDFThemeIcons::getIcon(QStringLiteral("save")), QStringLiteral("Action %1").arg(i));
    }
    verticalToolBar.setGeometry(0, 0, 200, 52);
    QWidget* extension = verticalToolBar.findChild<QWidget*>(QStringLiteral("qt_toolbar_ext_button"));
    QVERIFY(extension);
    QTRY_VERIFY(extension->isVisible());
    QTRY_COMPARE(extension->geometry().right(), 200 - 12 - 1);
    QCOMPARE(extension->geometry().top(), 8);
    QCOMPARE(extension->geometry().height(), 36);

    // Icon tabs of a sidebar: paddings 10 / 12 / 6 / 12 around buttons of 32 px
    QToolBar sidebarTabs;
    sidebarTabs.setProperty(pdf::PDFWidgetStyleProperties::SIDEBAR_TABS, true);
    QAction* tabAction = sidebarTabs.addAction(pdf::PDFThemeIcons::getIcon(QStringLiteral("bookmark")), QStringLiteral("Bookmarks"));
    sidebarTabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&sidebarTabs));
    QCOMPARE(sidebarTabs.sizeHint().height(), 48);
    QCOMPARE(sidebarTabs.widgetForAction(tabAction)->geometry(), QRect(12, 10, 32, 32));

    // The menu button of a toolbar (an icon with a text) has no chevron: 10 + 18 + 8 + text + 12
    QToolButton menuButton;
    QMenu buttonMenu;
    buttonMenu.addAction(QStringLiteral("Open"));
    menuButton.setIcon(pdf::PDFThemeIcons::getIcon(QStringLiteral("file")));
    menuButton.setText(QStringLiteral("File"));
    menuButton.setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    menuButton.setPopupMode(QToolButton::InstantPopup);
    menuButton.setMenu(&buttonMenu);
    const int menuButtonTextWidth = menuButton.sizeHint().width() - 10 - 18 - 8 - 12;
    QVERIFY2(menuButtonTextWidth >= 20 && menuButtonTextWidth <= 24, qPrintable(QString::number(menuButton.sizeHint().width())));

    // An icon button with a menu has the chevron: 8 + 20 + 2 + 12 + 8
    QToolBar menuToolBar;
    QToolButton* iconMenuButton = new QToolButton(&menuToolBar);
    iconMenuButton->setIcon(pdf::PDFThemeIcons::getIcon(QStringLiteral("file")));
    iconMenuButton->setPopupMode(QToolButton::InstantPopup);
    iconMenuButton->setMenu(&buttonMenu);
    menuToolBar.addWidget(iconMenuButton);
    menuToolBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&menuToolBar));
    QCOMPARE(iconMenuButton->sizeHint(), QSize(50, 36));

    // The size of the icons can be chosen by the user of the application
    QToolBar largeToolBar;
    largeToolBar.setIconSize(QSize(32, 32));
    QAction* largeAction = largeToolBar.addAction(pdf::PDFThemeIcons::getIcon(QStringLiteral("save")), QStringLiteral("Save"));
    largeToolBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&largeToolBar));
    QCOMPARE(largeToolBar.widgetForAction(largeAction)->size(), QSize(48, 48));
}

void WidgetStyleTest::headerViews()
{
    QStandardItemModel model(3, 2);
    model.setHorizontalHeaderLabels({ QStringLiteral("Key"), QStringLiteral("Value") });

    QTableView tableView;
    tableView.setModel(&model);
    tableView.resize(400, 200);
    tableView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tableView));

    // Rows of 30 px, the band of the header 28 px, labels start at the padding
    QCOMPARE(tableView.verticalHeader()->defaultSectionSize(), 30);
    QCOMPARE(tableView.rowHeight(0), 30);
    QCOMPARE(tableView.horizontalHeader()->height(), 28);
    QCOMPARE(tableView.horizontalHeader()->defaultAlignment(), Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter));

    QStyleOptionHeader option;
    option.rect = QRect(0, 0, 120, 28);
    option.text = QStringLiteral("Key");
    option.textAlignment = tableView.horizontalHeader()->defaultAlignment();
    option.orientation = Qt::Horizontal;
    QCOMPARE(m_style->subElementRect(QStyle::SE_HeaderLabel, &option, tableView.horizontalHeader()).left(), 12);

    // The alignment chosen by the application is kept
    QTableView centeredView;
    centeredView.horizontalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    centeredView.setModel(&model);
    centeredView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&centeredView));
    QCOMPARE(centeredView.horizontalHeader()->defaultAlignment(), Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter));
}

void WidgetStyleTest::treeView()
{
    QStandardItemModel model;
    for (int i = 0; i < 40; ++i)
    {
        QStandardItem* item = new QStandardItem(QStringLiteral("Item %1").arg(i));
        item->appendRow(new QStandardItem(QStringLiteral("Child")));
        model.appendRow(item);
    }

    pdf::PDFTreeView treeView;
    treeView.setHeaderHidden(true);
    treeView.setModel(&model);
    treeView.resize(300, 224);
    treeView.show();
    QVERIFY(QTest::qWaitForWindowExposed(&treeView));

    // Border 1 and the margin 6 around the rows
    QTRY_COMPARE(treeView.viewport()->geometry().topLeft(), QPoint(7, 7));
    QCOMPARE(treeView.viewport()->geometry().bottom(), treeView.height() - 8);
    QCOMPARE(treeView.visualRect(model.index(0, 0)).height(), 30);

    // The slot of the branch lies 8 px inside of the row, the text follows after 8 px
    QStyleOptionViewItem option;
    option.initFrom(&treeView);
    option.rect = treeView.visualRect(model.index(0, 0));
    option.index = model.index(0, 0);
    option.features = QStyleOptionViewItem::HasDisplay;
    option.text = QStringLiteral("Item 0");
    QCOMPARE(option.rect.left(), 16);
    QCOMPARE(m_style->subElementRect(QStyle::SE_ItemViewItemText, &option, &treeView).left(), 32);

    QStyleOption branchOption;
    branchOption.rect = QRect(0, 0, 16, 30);
    QCOMPARE(m_style->subElementRect(QStyle::SE_TreeViewDisclosureItem, &branchOption, &treeView), QRect(8, 0, 16, 30));

    // Branches are expanded without the animation, which Qt draws from the palette
    QVERIFY(!treeView.isAnimated());

    // A click on the chevron (also on its half, which lies in the item) expands the row
    QVERIFY(!treeView.isExpanded(model.index(0, 0)));
    QTest::mouseClick(treeView.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 15));
    QVERIFY(treeView.isExpanded(model.index(0, 0)));

    // The scroll bar is computed for the viewport with the margins: the last
    // row can be scrolled whole into the viewport
    treeView.scrollToBottom();
    QCoreApplication::processEvents();
    const QRect lastRect = treeView.visualRect(model.index(model.rowCount() - 1, 0));
    QVERIFY2(lastRect.bottom() < treeView.viewport()->height(), qPrintable(QStringLiteral("%1 / %2").arg(lastRect.bottom()).arg(treeView.viewport()->height())));

    // A tree with a header has the margins of the header only
    treeView.setHeaderHidden(false);
    QCoreApplication::processEvents();
    QTRY_COMPARE(treeView.viewport()->geometry().left(), 1);
    QCOMPARE(treeView.viewport()->geometry().top(), 1 + treeView.header()->height());

    treeView.setHeaderHidden(true);
    QTRY_COMPARE(treeView.viewport()->geometry().topLeft(), QPoint(7, 7));
}

void WidgetStyleTest::titleFonts()
{
    // Titles of group boxes are drawn Semi Bold, their children keep the font of the application
    QGroupBox groupBox(QStringLiteral("Document"));
    QVBoxLayout* layout = new QVBoxLayout(&groupBox);
    pdf::PDFLineEdit* lineEdit = new pdf::PDFLineEdit(QStringLiteral("DRAFT"), &groupBox);
    QCheckBox* checkBox = new QCheckBox(QStringLiteral("Check"), &groupBox);
    layout->addWidget(lineEdit);
    layout->addWidget(checkBox);
    groupBox.show();
    QVERIFY(QTest::qWaitForWindowExposed(&groupBox));

    QCOMPARE(lineEdit->font().weight(), QFont::Normal);
    QCOMPARE(checkBox->font().weight(), QFont::Normal);
    QCOMPARE(groupBox.font().weight(), QFont::Normal);

    // The title fits into the minimal size of the group box
    QStyleOptionGroupBox option;
    option.initFrom(&groupBox);
    option.text = groupBox.title();
    option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxLabel;
    const QRect label = m_style->subControlRect(QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxLabel, &groupBox);
    QVERIFY(label.width() >= QFontMetrics(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold)).horizontalAdvance(groupBox.title()));
    QVERIFY(groupBox.minimumSizeHint().width() >= label.width());
}

void WidgetStyleTest::messageBoxButtons()
{
    using Button = QDialogButtonBox::StandardButton;

    auto run = [](QDialogButtonBox::StandardButtons buttons, Qt::Key key, Button& defaultButton) -> Button
    {
        pdf::PDFMessageBox messageBox(pdf::PDFThemeIcons::Badge::Question, QStringLiteral("Title"), QStringLiteral("Save the changes?"), buttons);
        defaultButton = QDialogButtonBox::NoButton;

        QTimer::singleShot(0, &messageBox, [&messageBox, &defaultButton, key]()
        {
            const QList<QAbstractButton*> boxButtons = messageBox.getButtonBox()->buttons();
            for (QAbstractButton* boxButton : boxButtons)
            {
                QPushButton* pushButton = qobject_cast<QPushButton*>(boxButton);
                if (pushButton && pushButton->isDefault())
                {
                    defaultButton = messageBox.getButtonBox()->standardButton(pushButton);
                }
            }

            QTest::keyClick(&messageBox, key);
        });

        messageBox.exec();
        return messageBox.getClickedStandardButton();
    };

    // The default button is the button, which accepts the message - never the
    // destructive one, which is the first button of the layout
    Button defaultButton = QDialogButtonBox::NoButton;
    QCOMPARE(run(QDialogButtonBox::Save | QDialogButtonBox::Discard | QDialogButtonBox::Cancel, Qt::Key_Enter, defaultButton), QDialogButtonBox::Save);
    QCOMPARE(defaultButton, QDialogButtonBox::Save);

    // Escape activates the button, which rejects the message
    QCOMPARE(run(QDialogButtonBox::Save | QDialogButtonBox::Discard | QDialogButtonBox::Cancel, Qt::Key_Escape, defaultButton), QDialogButtonBox::Cancel);
    QCOMPARE(run(QDialogButtonBox::Yes | QDialogButtonBox::No, Qt::Key_Escape, defaultButton), QDialogButtonBox::No);
    QCOMPARE(defaultButton, QDialogButtonBox::Yes);
    QCOMPARE(run(QDialogButtonBox::Ok, Qt::Key_Escape, defaultButton), QDialogButtonBox::Ok);
    QCOMPARE(run(QDialogButtonBox::Abort | QDialogButtonBox::Cancel, Qt::Key_Escape, defaultButton), QDialogButtonBox::Cancel);

    // Each button closes the message box
    pdf::PDFMessageBox messageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok);
    QAbstractButton* actionButton = messageBox.addButton(QStringLiteral("Show details"), QDialogButtonBox::ActionRole);
    QTimer::singleShot(0, &messageBox, [actionButton]() { actionButton->click(); });
    messageBox.exec();
    QCOMPARE(messageBox.getClickedButton(), actionButton);

    // Long details are scrolled, the message box stays small
    pdf::PDFMessageBox longBox(pdf::PDFThemeIcons::Badge::Critical, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok);
    longBox.setDetailedText(QString(QStringLiteral("Line of a trace\n")).repeated(400));
    longBox.show();
    QVERIFY(QTest::qWaitForWindowExposed(&longBox));
    QVERIFY2(longBox.height() - longBox.getShadowMargins().top() - longBox.getShadowMargins().bottom() < 420, qPrintable(QString::number(longBox.height())));
    QCOMPARE(countLeakPixels(longBox.grab().toImage()), 0);
    longBox.hide();

    // The message box is a card without the title bar of the system, centered
    // over its parent, which is covered by a scrim while the message is shown
    QWidget parent;
    parent.resize(800, 600);
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));

    pdf::PDFMessageBox cardBox(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Some fonts are not embedded"), QStringLiteral("The document may look different on another computer."), QDialogButtonBox::Ok, &parent);
    QVERIFY(cardBox.windowFlags().testFlag(Qt::FramelessWindowHint));

    auto findScrims = [&parent]()
    {
        QList<QWidget*> scrims;
        for (QWidget* child : parent.findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        {
            if (child->isVisible() && child->geometry() == parent.rect())
            {
                scrims << child;
            }
        }
        return scrims;
    };

    bool hasScrim = false;
    QPoint cardCenter;
    QTimer::singleShot(0, &cardBox, [&]()
    {
        hasScrim = findScrims().size() == 1;
        cardCenter = cardBox.mapToGlobal(cardBox.rect().marginsRemoved(cardBox.getShadowMargins()).center());
        cardBox.getButtonBox()->button(QDialogButtonBox::Ok)->click();
    });
    cardBox.exec();

    QVERIFY(hasScrim);
    QVERIFY(findScrims().isEmpty());
    const QPoint parentCenter = parent.mapToGlobal(parent.rect().center());
    QVERIFY2((cardCenter - parentCenter).manhattanLength() <= 2, qPrintable(QStringLiteral("%1 %2 / %3 %4").arg(cardCenter.x()).arg(cardCenter.y()).arg(parentCenter.x()).arg(parentCenter.y())));

    // Inside of another widget the card has no shadow
    pdf::PDFMessageBox embeddedBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok, &parent);
    embeddedBox.setWindowFlags(Qt::Widget);
    embeddedBox.show();
    QVERIFY(embeddedBox.getShadowMargins().isNull());
    QCOMPARE(embeddedBox.layout()->contentsMargins(), QMargins(23, 21, 23, 21));
    QCOMPARE(countLeakPixels(embeddedBox.grab().toImage()), 0);
}

/// Widget filled by a token (the content below an overlay scroll bar, the parts of a window)
class WidgetStyleTestFill : public QWidget
{
public:
    explicit WidgetStyleTestFill(pdf::PDFThemeColors::Color color, QWidget* parent = nullptr) :
        QWidget(parent),
        m_color(color)
    {

    }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), pdf::PDFStylePainter::color(m_color));
    }

private:
    pdf::PDFThemeColors::Color m_color;
};

void WidgetStyleTest::messageBoxScrimTarget()
{
    // An application with a top bar of its own covers only its body (the scrim of
    // pdf4qt-recent-clear-history 941:135742 starts below app-top-bar): the scrim
    // covers the target, the card is centered over the target, the top bar stays
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);

        QWidget window;
        window.resize(800, 600);
        WidgetStyleTestFill* topBar = new WidgetStyleTestFill(Color::BgTitlebar, &window);
        topBar->setGeometry(0, 0, 800, 38);
        WidgetStyleTestFill* body = new WidgetStyleTestFill(Color::BgCanvas, &window);
        body->setGeometry(0, 38, 800, 562);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        pdf::PDFMessageBox box(pdf::PDFThemeIcons::Badge::Warning, QStringLiteral("Clear history?"), QStringLiteral("The list of recent files is emptied."), QDialogButtonBox::Ok, &window);
        QCOMPARE(box.getScrimTarget(), &window);
        box.setScrimTarget(body);
        QCOMPARE(box.getScrimTarget(), body);
        QVERIFY(!box.getScrim());

        box.show();
        QVERIFY(QTest::qWaitForWindowExposed(&box));
        QVERIFY(box.getScrim());
        QCOMPARE(box.getScrim()->parentWidget(), body);
        QCOMPARE(box.getScrim()->geometry(), body->rect());

        const QPoint cardCenter = box.mapToGlobal(box.rect().marginsRemoved(box.getShadowMargins()).center());
        const QPoint bodyCenter = body->mapToGlobal(body->rect().center());
        QVERIFY2((cardCenter - bodyCenter).manhattanLength() <= 2, qPrintable(QStringLiteral("%1 %2 / %3 %4").arg(cardCenter.x()).arg(cardCenter.y()).arg(bodyCenter.x()).arg(bodyCenter.y())));

        // Pixels: the top bar keeps its color, the body is below the scrim
        const QImage image = window.grab().toImage();
        QVERIFY2(isNear(pixel(image, 400, 19), token(Color::BgTitlebar)), qPrintable(pixel(image, 400, 19).name()));
        const QColor scrimmed = blend(token(Color::BgScrim), token(Color::BgCanvas));
        QVERIFY2(isNear(pixel(image, 20, 300), scrimmed, 3), qPrintable(pixel(image, 20, 300).name() + QStringLiteral(" / ") + scrimmed.name()));
        QCOMPARE(countLeakPixels(image), 0);

        // A change while the box is shown moves the scrim and the card (null = the parent)
        box.setScrimTarget(nullptr);
        QCOMPARE(box.getScrimTarget(), &window);
        QVERIFY(box.getScrim());
        QCOMPARE(box.getScrim()->parentWidget(), &window);
        QCOMPARE(box.getScrim()->geometry(), window.rect());
        QCOMPARE(body->findChildren<QWidget*>(Qt::FindDirectChildrenOnly).size(), 0);
        const QPoint movedCenter = box.mapToGlobal(box.rect().marginsRemoved(box.getShadowMargins()).center());
        QVERIFY((movedCenter - window.mapToGlobal(window.rect().center())).manhattanLength() <= 2);

        // The target deleted while the box is shown: its scrim goes with it, the
        // parent is the target again; hiding and showing again do not crash
        box.setScrimTarget(body);
        QCOMPARE(box.getScrim()->parentWidget(), body);
        delete body;
        QVERIFY(!box.getScrim());
        QCOMPARE(box.getScrimTarget(), &window);
        box.hide();
        box.show();
        QVERIFY(box.getScrim());
        QCOMPARE(box.getScrim()->parentWidget(), &window);
        box.hide();
        QVERIFY(!box.getScrim());

        // The box deleted while it is shown removes the scrim of its target
        WidgetStyleTestFill* secondBody = new WidgetStyleTestFill(Color::BgCanvas, &window);
        secondBody->setGeometry(0, 38, 800, 562);
        secondBody->show();
        pdf::PDFMessageBox* deletedBox = new pdf::PDFMessageBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok, &window);
        deletedBox->setScrimTarget(secondBody);
        deletedBox->show();
        QCOMPARE(secondBody->findChildren<QWidget*>(Qt::FindDirectChildrenOnly).size(), 1);
        delete deletedBox;
        QCOMPARE(secondBody->findChildren<QWidget*>(Qt::FindDirectChildrenOnly).size(), 0);

        // A target deleted before the box is shown: the parent
        QWidget* lostTarget = new QWidget(&window);
        pdf::PDFMessageBox lateBox(pdf::PDFThemeIcons::Badge::Information, QStringLiteral("Title"), QStringLiteral("Text"), QDialogButtonBox::Ok, &window);
        lateBox.setScrimTarget(lostTarget);
        delete lostTarget;
        lateBox.show();
        QVERIFY(lateBox.getScrim());
        QCOMPARE(lateBox.getScrim()->parentWidget(), &window);
        lateBox.hide();
    }

    // The title and the text are plain texts (a name of a folder with tags given by
    // an application is never interpreted as a rich text)
    const QString title = QStringLiteral("<b>Reports</b>");
    const QString text = QStringLiteral("The folder <i>2026</i> & <a href=\"https://example.com\">link</a><br>is pinned.");
    pdf::PDFMessageBox plainBox(pdf::PDFThemeIcons::Badge::Information, title, text, QDialogButtonBox::Ok);
    plainBox.setDetailedText(QStringLiteral("<table><tr><td>cell</td></tr></table>"));
    int labelCount = 0;
    for (QLabel* label : plainBox.findChildren<QLabel*>())
    {
        QCOMPARE(label->textFormat(), Qt::PlainText);
        ++labelCount;
    }
    QVERIFY(labelCount >= 3);
    QLabel* titleLabel = nullptr;
    for (QLabel* label : plainBox.findChildren<QLabel*>())
    {
        titleLabel = label->text() == title ? label : titleLabel;
    }
    QVERIFY(titleLabel);

    // The tags are drawn as characters: the label needs the width of the whole text
    QVERIFY2(titleLabel->sizeHint().width() >= QFontMetrics(titleLabel->font()).horizontalAdvance(title) - 1,
             qPrintable(QString::number(titleLabel->sizeHint().width())));
}

void WidgetStyleTest::overlayScrollBars()
{
    // QAbstractScrollArea with the property overlayScrollBars: the viewport takes the
    // whole area, the scroll bars lie over its edge (PM_ScrollView_ScrollBarOverlap)
    // and draw only the groove and the slider, the content is seen in their gutters.
    // Without the property the bars take their extent of 12 px.
    for (const pdf::PDFThemeColors::Scheme scheme : { pdf::PDFThemeColors::Scheme::Light, pdf::PDFThemeColors::Scheme::Dark })
    {
        pdf::PDFThemeManager::getInstance()->setScheme(scheme);

        for (const bool isOverlay : { false, true })
        {
            QScrollArea area;
            area.setFrameShape(QFrame::NoFrame);
            area.setWidgetResizable(true);
            WidgetStyleTestFill* content = new WidgetStyleTestFill(Color::BgSubtle);
            content->setMinimumSize(500, 900);
            area.setWidget(content);
            area.resize(300, 200);
            if (isOverlay)
            {
                pdf::PDFWidgetStyleProperties::set(&area, pdf::PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
            }
            area.show();
            QVERIFY(QTest::qWaitForWindowExposed(&area));

            QScrollBar* bar = area.verticalScrollBar();
            QVERIFY(bar->isVisible());
            QVERIFY(area.horizontalScrollBar()->isVisible());
            QStyleOptionSlider option;
            option.initFrom(bar);
            QCOMPARE(m_style->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, &option, bar), isOverlay ? 12 : 0);
            QCOMPARE(m_style->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, &option, area.horizontalScrollBar()), isOverlay ? 12 : 0);
            QCOMPARE(area.viewport()->size(), isOverlay ? QSize(300, 200) : QSize(288, 188));

            // The bar lies at the edge of the area in both cases
            const QRect barRect(bar->mapTo(&area, QPoint(0, 0)), bar->size());
            QCOMPARE(barRect.left(), 288);
            QCOMPARE(barRect.width(), 12);

            if (isOverlay)
            {
                // The gutter of the bar shows the content, the slider (6 px, centered) is drawn over it
                const QImage image = area.grab().toImage();
                QVERIFY2(isNear(pixel(image, 289, 4), token(Color::BgSubtle)), qPrintable(pixel(image, 289, 4).name()));
                QVERIFY2(isNear(pixel(image, 298, 4), token(Color::BgSubtle)), qPrintable(pixel(image, 298, 4).name()));
                QVERIFY2(isNear(pixel(image, 294, 6), token(Color::BorderStrong), 3), qPrintable(pixel(image, 294, 6).name()));
                QCOMPARE(countLeakPixels(image), 0);
            }

            // The wheel, the keyboard and the drag of the slider scroll the content
            QWheelEvent wheel(QPointF(100, 100), area.viewport()->mapToGlobal(QPointF(100, 100)), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(area.viewport(), &wheel);
            QVERIFY(bar->value() > 0);
            bar->setValue(0);
            QTest::keyClick(&area, Qt::Key_PageDown);
            QVERIFY(bar->value() > 0);
            bar->setValue(0);
            QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, QPoint(6, 6));
            QTest::mouseMove(bar, QPoint(6, 60));
            QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, QPoint(6, 60));
            QVERIFY(bar->value() > 0);

            // The property can be changed later: the area lays its viewport out again
            pdf::PDFWidgetStyleProperties::set(&area, pdf::PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS, !isOverlay);
            QCOMPARE(area.viewport()->size(), !isOverlay ? QSize(300, 200) : QSize(288, 188));
        }
    }

    // A scroll bar of the application, which is not a bar of such area, never overlaps
    QScrollArea area;
    pdf::PDFWidgetStyleProperties::set(&area, pdf::PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    QWidget* content = new QWidget();
    area.setWidget(content);
    QScrollBar foreign(Qt::Vertical, content);
    QScrollBar alone(Qt::Vertical);
    pdf::PDFWidgetStyleProperties::set(&alone, pdf::PDFWidgetStyleProperties::OVERLAY_SCROLL_BARS);
    QCOMPARE(m_style->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, &foreign), 0);
    QCOMPARE(m_style->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, &alone), 0);
    QCOMPARE(m_style->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, &area), 0);
    QCOMPARE(m_style->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, nullptr), 0);
    QCOMPARE(m_style->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, area.verticalScrollBar()), 12);
}

void WidgetStyleTest::toolTipOfQt()
{
    // Tooltips shown by Qt (a rich text, a tooltip shown by the application)
    // are an opaque panel with the colors of the theme
    QWidget window;
    window.resize(200, 100);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto findTipLabel = []() -> QWidget*
    {
        const QWidgetList widgets = QApplication::topLevelWidgets();
        for (QWidget* widget : widgets)
        {
            if (widget->inherits("QTipLabel") && widget->isVisible())
            {
                return widget;
            }
        }
        return nullptr;
    };

    QToolTip::showText(window.mapToGlobal(QPoint(20, 20)), QStringLiteral("<b>Rich</b> tooltip of the application"), &window);
    QTRY_VERIFY(findTipLabel());

    QWidget* tipLabel = findTipLabel();
    const QImage image = tipLabel->grab().toImage();
    QCOMPARE(countLeakPixels(image), 0);
    QVERIFY(isNear(pixel(image, 0, image.height() / 2), token(pdf::PDFThemeColors::BorderStrong)));
    QVERIFY(isNear(pixel(image, 3, 3), token(pdf::PDFThemeColors::BgSurface)));
    QVERIFY(!tipLabel->testAttribute(Qt::WA_TranslucentBackground));

    // The text has the color of the theme
    bool hasText = false;
    for (int x = 0; x < image.width() && !hasText; ++x)
    {
        for (int y = 0; y < image.height() && !hasText; ++y)
        {
            hasText = isNear(image.pixelColor(x, y), token(pdf::PDFThemeColors::TextPrimary), 8);
        }
    }
    QVERIFY(hasText);

    QToolTip::hideText();
    QTRY_VERIFY(!findTipLabel());
}

void WidgetStyleTest::plainTextLines()
{
    pdf::PDFPlainTextEdit edit(QStringLiteral("First\nSecond\nThird"));
    edit.resize(260, 120);
    edit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&edit));
    QCOMPARE(edit.toPlainText(), QStringLiteral("First\nSecond\nThird"));

    // Lines of 18 px
    auto getLineTop = [&edit](int blockNumber)
    {
        QTextCursor cursor(edit.document()->findBlockByNumber(blockNumber));
        return edit.cursorRect(cursor).top();
    };

    QCOMPARE(getLineTop(1) - getLineTop(0), 18);
    QCOMPARE(getLineTop(2) - getLineTop(1), 18);

    // A click into the gap between two lines of the font belongs to the line,
    // in whose line of the design it lies - the caret does not jump to an end of a line
    const QRect secondLine = edit.cursorRect(QTextCursor(edit.document()->findBlockByNumber(1)));
    const int secondLineStart = edit.document()->findBlockByNumber(1).position();
    for (int y = secondLine.top() - 1; y <= secondLine.top() + 16; ++y)
    {
        QTest::mouseClick(edit.viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(secondLine.left() + 16, y));
        const int position = edit.textCursor().position();
        QVERIFY2(position > secondLineStart && position < secondLineStart + 6, qPrintable(QStringLiteral("y=%1 position=%2").arg(y).arg(position)));
    }

    // The selection fills the whole lines of the design
    QTextCursor selection(edit.document());
    selection.select(QTextCursor::Document);
    edit.setTextCursor(selection);
    {
        const QImage image = edit.viewport()->grab().toImage();
        const QColor highlight = edit.palette().color(QPalette::Active, QPalette::Highlight);

        // Rows above and below the line of the font, and the row between two lines
        for (int x = secondLine.left() + 1; x < secondLine.left() + 30; x += 4)
        {
            QVERIFY2(isNear(image.pixelColor(x, secondLine.top() - 1), highlight), qPrintable(QString::number(x)));
            QVERIFY2(isNear(image.pixelColor(x, secondLine.top() + 16), highlight), qPrintable(QString::number(x)));
            QVERIFY2(isNear(image.pixelColor(x, secondLine.top() + 17), highlight), qPrintable(QString::number(x)));
        }
    }

    // A wrapped paragraph has the same lines
    edit.setPlainText(QString(QStringLiteral("word ")).repeated(60));
    QCoreApplication::processEvents();
    const QTextBlock block = edit.document()->firstBlock();
    QVERIFY(edit.document()->documentLayout()->blockBoundingRect(block).height() > 0);
    QVERIFY(block.layout()->lineCount() > 1);
    QCOMPARE(block.layout()->lineAt(1).position().y() - block.layout()->lineAt(0).position().y(), 18.0);
    QCOMPARE(countLeakPixels(edit.grab().toImage()), 0);
}

void WidgetStyleTest::dial()
{
    // The value follows the pointer along the arc of 270 degrees, which the style draws
    pdf::PDFDial dial;
    dial.setRange(0, 100);
    dial.resize(64, 64);
    dial.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dial));

    auto getPoint = [](qreal clockwiseAngle)
    {
        const qreal angle = qDegreesToRadians(clockwiseAngle);
        return QPointF(32.0 + 26.0 * qSin(angle), 32.0 - 26.0 * qCos(angle)).toPoint();
    };

    QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, getPoint(225.0));
    QCOMPARE(dial.value(), 0);
    QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, getPoint(135.0));
    QCOMPARE(dial.value(), 100);
    QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, getPoint(0.0));
    QVERIFY(qAbs(dial.value() - 50) <= 1);
    QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, getPoint(27.0));
    QVERIFY(qAbs(dial.value() - 60) <= 1);

    // The gap at the bottom belongs to the nearer end of the arc
    QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, getPoint(190.0));
    QCOMPARE(dial.value(), 0);
    QTest::mouseClick(&dial, Qt::LeftButton, Qt::NoModifier, getPoint(170.0));
    QCOMPARE(dial.value(), 100);

    // The track is the whole ring
    const QImage image = dial.grab().toImage();
    QVERIFY(isNear(pixel(image, 32, 58), token(pdf::PDFThemeColors::BgTrack), 6));
}

void WidgetStyleTest::tabBarScrolls()
{
    // Chips have no scroll buttons by the design, their labels are elided down
    // to the minimal width of a chip (64 px in the tab rectangle of 70 px)
    QTabBar chips;
    for (int i = 0; i < 30; ++i)
    {
        chips.addTab(QStringLiteral("Tab with a long label %1").arg(i));
    }
    chips.show();
    QVERIFY(QTest::qWaitForWindowExposed(&chips));
    QVERIFY(!chips.usesScrollButtons());
    QVERIFY(!chips.expanding());
    QVERIFY2(chips.minimumSizeHint().width() <= 30 * 70 + 8, qPrintable(QString::number(chips.minimumSizeHint().width())));

    // An application with many tabs turns the scroll buttons on, the style keeps them
    QTabBar tabBar;
    tabBar.setUsesScrollButtons(true);
    for (int i = 0; i < 30; ++i)
    {
        tabBar.addTab(QStringLiteral("Tab with a long label %1").arg(i));
    }
    tabBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabBar));
    QVERIFY(tabBar.usesScrollButtons());
    QVERIFY2(tabBar.minimumSizeHint().width() < 400, qPrintable(QString::number(tabBar.minimumSizeHint().width())));

    // Tabs of the documents keep their labels and scroll, the top bar of the
    // application holding them has the background of the strip
    QWidget topBar;
    topBar.setProperty(pdf::PDFWidgetStyleProperties::DOCUMENT_TABS, true);
    QHBoxLayout* topBarLayout = new QHBoxLayout(&topBar);
    topBarLayout->setContentsMargins(16, 6, 16, 0);
    QTabBar* documentTabs = new QTabBar(&topBar);
    documentTabs->setProperty(pdf::PDFWidgetStyleProperties::DOCUMENT_TABS, true);
    documentTabs->setTabsClosable(true);
    const QString label = QStringLiteral("Contract_final_signed.pdf");
    for (int i = 0; i < 10; ++i)
    {
        documentTabs->addTab(label);
    }
    topBarLayout->addWidget(documentTabs);
    topBar.resize(500, 38);
    topBar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&topBar));
    QVERIFY(documentTabs->usesScrollButtons());
    QCOMPARE(documentTabs->elideMode(), Qt::ElideNone);
    const int labelWidth = QFontMetrics(pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold)).horizontalAdvance(label);
    QVERIFY2(documentTabs->tabRect(0).width() > labelWidth, qPrintable(QStringLiteral("%1 / %2").arg(documentTabs->tabRect(0).width()).arg(labelWidth)));

    const QImage image = topBar.grab().toImage();
    QVERIFY(isNear(pixel(image, 4, 20), token(Color::BgTitlebar)));
    QCOMPARE(countLeakPixels(image), 0);
}

void WidgetStyleTest::progressBarThickness()
{
    // The progress bar is as thick as the bar of the design, also in a layout
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QProgressBar* progressBar = new QProgressBar(&window);
    QProgressBar* thinBar = new QProgressBar(&window);
    thinBar->setProperty(pdf::PDFWidgetStyleProperties::THIN, true);
    thinBar->setTextVisible(false);
    QProgressBar* ownBar = new QProgressBar(&window);
    ownBar->setMinimumHeight(24);
    layout->addWidget(progressBar);
    layout->addWidget(thinBar);
    layout->addWidget(ownBar);
    layout->addStretch(1);
    window.resize(300, 200);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTRY_COMPARE(progressBar->height(), 16);
    QCOMPARE(thinBar->height(), 2);

    // Sizes set by the application are kept
    QCOMPARE(ownBar->height(), 24);
}

void WidgetStyleTest::framePrimitive()
{
    // The option of the primitive has no shape of the frame, the frame is drawn
    QStyleOptionFrame option;
    option.rect = QRect(0, 0, 40, 30);
    option.state = QStyle::State_Enabled;

    const QImage image = render([this, &option](QPainter& painter)
    {
        painter.fillRect(option.rect, token(pdf::PDFThemeColors::BgCanvas));
        m_style->drawPrimitive(QStyle::PE_Frame, &option, &painter, nullptr);
    }, option.rect.size());

    QVERIFY(isNear(pixel(image, 20, 0), token(pdf::PDFThemeColors::BorderDefault)));
    QVERIFY(isNear(pixel(image, 0, 15), token(pdf::PDFThemeColors::BorderDefault)));
    QVERIFY(isNear(pixel(image, 20, 15), token(pdf::PDFThemeColors::BgCanvas)));
}

void WidgetStyleTest::itemEditorFactory()
{
    QWidget parent;
    pdf::PDFItemEditorFactory factory;

    // The style has installed the factory as the default factory of the application
    // (default delegates of the views create the editors of the library)
    QVERIFY(dynamic_cast<const pdf::PDFItemEditorFactory*>(QItemEditorFactory::defaultFactory()));

    // Editors of all basic types are widgets of the library
    QWidget* boolEditor = factory.createEditor(QMetaType::Bool, &parent);
    QVERIFY(qobject_cast<pdf::PDFComboBox*>(boolEditor));
    QCOMPARE(factory.valuePropertyName(QMetaType::Bool), QByteArray("value"));
    QVERIFY(boolEditor->setProperty("value", true));
    QCOMPARE(boolEditor->property("value"), QVariant(true));

    // An unsigned integer keeps its whole range
    QWidget* uintEditor = factory.createEditor(QMetaType::UInt, &parent);
    QVERIFY(uintEditor);
    const QByteArray uintProperty = factory.valuePropertyName(QMetaType::UInt);
    QVERIFY(uintEditor->setProperty(uintProperty.constData(), 4000000000u));
    QCOMPARE(uintEditor->property(uintProperty.constData()).toUInt(), 4000000000u);

    // The factory can be the default factory of the application: it does not ask itself
    QItemEditorFactory::setDefaultFactory(new pdf::PDFItemEditorFactory());
    QVERIFY(qobject_cast<pdf::PDFComboBox*>(QItemEditorFactory::defaultFactory()->createEditor(QMetaType::Bool, &parent)));

    // Values of other types are edited as a text, as the factory of Qt edits them
    QVERIFY(qobject_cast<pdf::PDFLineEdit*>(QItemEditorFactory::defaultFactory()->createEditor(QMetaType::LongLong, &parent)));
    QVERIFY(qobject_cast<pdf::PDFLineEdit*>(factory.createEditor(QMetaType::QUrl, &parent)));
    QCOMPARE(QItemEditorFactory::defaultFactory()->valuePropertyName(QMetaType::LongLong), QByteArray("text"));
    QItemEditorFactory::setDefaultFactory(nullptr);
    QVERIFY(factory.createEditor(QMetaType::LongLong, &parent));

    // The factory of Qt is replaced, when the style prepares the application, and it is
    // restored, when the style releases the application
    m_style->unpolish(qApp);
    m_style->polish(qApp);
    QVERIFY(dynamic_cast<const pdf::PDFItemEditorFactory*>(QItemEditorFactory::defaultFactory()));
    m_style->unpolish(qApp);
    QVERIFY(!dynamic_cast<const pdf::PDFItemEditorFactory*>(QItemEditorFactory::defaultFactory()));

    // A factory of the application is kept
    QItemEditorFactory* applicationFactory = new QItemEditorFactory();
    QItemEditorFactory::setDefaultFactory(applicationFactory);
    m_style->polish(qApp);
    QCOMPARE(QItemEditorFactory::defaultFactory(), applicationFactory);
    m_style->unpolish(qApp);
    QCOMPARE(QItemEditorFactory::defaultFactory(), applicationFactory);

    // A factory, which the application sets after the style has prepared it, is kept too
    QItemEditorFactory::setDefaultFactory(nullptr);
    m_style->polish(qApp);
    QItemEditorFactory* laterFactory = new QItemEditorFactory();
    QItemEditorFactory::setDefaultFactory(laterFactory);
    m_style->unpolish(qApp);
    QCOMPARE(QItemEditorFactory::defaultFactory(), laterFactory);

    // The state of the other tests: the factory of Qt replaced by the style
    QItemEditorFactory::setDefaultFactory(nullptr);
    m_style->polish(qApp);
    QVERIFY(dynamic_cast<const pdf::PDFItemEditorFactory*>(QItemEditorFactory::defaultFactory()));
}

void WidgetStyleTest::itemEditing()
{
    // A cell of a table edited by the delegate of the library and by the default delegate
    // of Qt: the view paints the edited cell (bg/surface and one focus ring), the frameless
    // editor paints only its text; text and selection in the colors of the theme
    for (const bool isLibraryDelegate : { true, false })
    {
        QStandardItemModel model(3, 2);
        for (int row = 0; row < 3; ++row)
        {
            model.setItem(row, 0, new QStandardItem(QStringLiteral("Annual report %1.pdf").arg(row + 1)));
            model.setItem(row, 1, new QStandardItem(QStringLiteral("2.4 MB")));
        }

        QTableView view;
        view.setModel(&model);
        if (isLibraryDelegate)
        {
            view.setItemDelegate(new pdf::PDFItemDelegate(&view));
        }
        view.setColumnWidth(0, 220);
        view.resize(420, 160);
        view.show();
        view.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&view));

        // The style never replaces the delegate of a view
        QCOMPARE(qobject_cast<pdf::PDFItemDelegate*>(view.itemDelegate()) != nullptr, isLibraryDelegate);

        const QModelIndex index = model.index(1, 0);
        view.setCurrentIndex(index);
        view.edit(index);
        QLineEdit* editor = view.viewport()->findChild<QLineEdit*>();
        QVERIFY(editor);
        editor->setFocus();
        editor->selectAll();
        QCoreApplication::processEvents();

        QImage image = view.grab().toImage();
        const QByteArray dumpDirectory = qgetenv("PDF4QT_TEST_DUMP");
        if (!dumpDirectory.isEmpty())
        {
            image.save(QDir(QString::fromLocal8Bit(dumpDirectory)).filePath(QStringLiteral("itemEditing_%1.png").arg(isLibraryDelegate ? QStringLiteral("library") : QStringLiteral("qt"))));
        }

        QCOMPARE(countLeakPixels(image), 0);
        QCOMPARE(editor->palette().color(QPalette::Text), token(pdf::PDFThemeColors::TextPrimary));
        QVERIFY(editor->palette().color(QPalette::Highlight) != pdf::PDFThemeColors::getLeakColor());
        QVERIFY(editor->palette().color(QPalette::HighlightedText) != pdf::PDFThemeColors::getLeakColor());

        // One ring: along the middle line of the edited cell the color of the ring
        // is found only at its two edges
        // (a read-only editor has no text cursor, which is drawn in the color of the ring)
        editor->deselect();
        editor->setReadOnly(true);
        QCoreApplication::processEvents();
        image = view.grab().toImage();
        const QRect cell = view.visualRect(index).translated(view.viewport()->geometry().topLeft());
        int runs = 0;
        bool isInRun = false;
        for (int x = cell.left(); x <= cell.right(); ++x)
        {
            const bool isRing = isNear(pixel(image, x, cell.center().y()), token(Color::BorderFocus), 24);
            runs += isRing && !isInRun ? 1 : 0;
            isInRun = isRing;
        }
        QCOMPARE(runs, 2);
        QVERIFY(isNear(pixel(image, cell.left() + 4, cell.top() + 4), token(Color::BgSurface), 3));
    }

    // A persistent editor opened before the view is shown saves its value, also after
    // a change of the theme of the platform (the view is polished again)
    QStandardItemModel model(1, 1);
    model.setItem(0, 0, new QStandardItem(QStringLiteral("Draft")));
    QTableView view;
    view.setModel(&model);
    QAbstractItemDelegate* delegate = view.itemDelegate();
    view.openPersistentEditor(model.index(0, 0));
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QEvent themeChange(QEvent::ThemeChange);
    QCoreApplication::sendEvent(&view, &themeChange);
    QCOMPARE(view.itemDelegate(), delegate);
    QLineEdit* editor = view.viewport()->findChild<QLineEdit*>();
    QVERIFY(editor);
    editor->setFocus();
    editor->setText(QStringLiteral("Final"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(model.data(model.index(0, 0)).toString(), QStringLiteral("Final"));
}

void WidgetStyleTest::chipsAndSegments()
{
    // Selected chip of a single choice: bg/surface + bg/accent-subtle, no border, text/accent
    // (decision of Q-9, the full accent belongs only to the primary button)
    QWidget row;
    QHBoxLayout* rowLayout = new QHBoxLayout(&row);
    QButtonGroup group;
    QPushButton* selected = new QPushButton(QStringLiteral("Recognition"), &row);
    QPushButton* other = new QPushButton(QStringLiteral("Output"), &row);
    for (QPushButton* chip : { selected, other })
    {
        chip->setProperty(pdf::PDFWidgetStyleProperties::CHIP, true);
        chip->setCheckable(true);
        chip->setFocusPolicy(Qt::NoFocus);
        group.addButton(chip);
        rowLayout->addWidget(chip);
    }
    selected->setChecked(true);
    row.show();
    QVERIFY(QTest::qWaitForWindowExposed(&row));
    QCOMPARE(countLeakPixels(row.grab().toImage()), 0);
    QImage image = row.grab(selected->geometry()).toImage();
    const QColor subtle = blend(token(Color::BgAccentSubtle), token(Color::BgSurface));
    const int middle = selected->height() / 2;
    QVERIFY(isNear(pixel(image, 3, middle), subtle, 3));

    // No border: the first column of the chip, which is not the background, is the fill
    const QColor background = row.grab().toImage().pixelColor(0, 0);
    int firstColumn = 0;
    while (firstColumn < selected->width() && isNear(pixel(image, firstColumn, middle), background, 2))
    {
        ++firstColumn;
    }
    QVERIFY(firstColumn < 6);

    // The antialiased first column is a mix of the background and of the fill; a border
    // (border/default) would not lie on the line between them
    const QColor edge = pixel(image, firstColumn, middle);
    const qreal coverage = qreal(background.red() - edge.red()) / qreal(background.red() - subtle.red());
    const QColor mixed = QColor::fromRgbF(background.redF() + coverage * (subtle.redF() - background.redF()),
                                          background.greenF() + coverage * (subtle.greenF() - background.greenF()),
                                          background.blueF() + coverage * (subtle.blueF() - background.blueF()));
    QVERIFY2(isNear(edge, mixed, 4), qPrintable(edge.name() + QStringLiteral(" ") + mixed.name()));
    QVERIFY(isNear(pixel(image, firstColumn + 1, middle), subtle, 3));
    int accentPixels = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            accentPixels += isNear(image.pixelColor(x, y), token(Color::TextAccent), 24) ? 1 : 0;
            QVERIFY(!isNear(image.pixelColor(x, y), token(Color::BgAccent), 2));
        }
    }
    QVERIFY(accentPixels > 10);

    // Selected tab of a chip tab bar has the same look
    QTabBar tabBar;
    tabBar.addTab(QStringLiteral("Settings"));
    tabBar.addTab(QStringLiteral("Review"));
    tabBar.resize(tabBar.sizeHint());
    image = tabBar.grab().toImage();
    QVERIFY(isNear(pixel(image, tabBar.tabRect(0).center().x(), tabBar.tabRect(0).center().y() + 10), subtle, 3));

    // On / off option (chip with the property "option"): the check mark replaces its icon
    // when it is on, it has always the place of the icon, so it keeps its width
    auto makeChip = [](const QString& text, bool isOption, bool isChecked, const QIcon& icon)
    {
        std::unique_ptr<QPushButton> chip = std::make_unique<QPushButton>(icon, text);
        chip->setProperty(pdf::PDFWidgetStyleProperties::CHIP, true);
        if (isOption)
        {
            chip->setProperty(pdf::PDFWidgetStyleProperties::OPTION, true);
        }
        chip->setCheckable(true);
        chip->setChecked(isChecked);
        chip->resize(chip->sizeHint());
        return chip;
    };
    const QIcon matchCase = pdf::PDFThemeIcons::getIcon(QStringLiteral("match-case"));
    const std::unique_ptr<QPushButton> plain = makeChip(QStringLiteral("Whole words"), false, false, QIcon());
    const std::unique_ptr<QPushButton> optionOff = makeChip(QStringLiteral("Whole words"), true, false, QIcon());
    const std::unique_ptr<QPushButton> optionOn = makeChip(QStringLiteral("Whole words"), true, true, QIcon());
    const std::unique_ptr<QPushButton> optionOnWithIcon = makeChip(QStringLiteral("Whole words"), true, true, matchCase);
    QCOMPARE(optionOff->sizeHint().width(), plain->sizeHint().width() + 20);
    QCOMPARE(optionOn->sizeHint().width(), optionOff->sizeHint().width());
    QCOMPARE(optionOnWithIcon->sizeHint().width(), optionOn->sizeHint().width());
    const QImage withoutIcon = optionOn->grab().toImage();
    const QImage withIcon = optionOnWithIcon->grab().toImage();
    const int iconColumns = qRound(34 * withIcon.devicePixelRatio());
    QCOMPARE(withIcon.copy(0, 0, iconColumns, withIcon.height()), withoutIcon.copy(0, 0, iconColumns, withoutIcon.height()));
    int checkPixels = 0;
    for (int y = 0; y < withIcon.height(); ++y)
    {
        for (int x = 0; x < iconColumns; ++x)
        {
            checkPixels += isNear(withIcon.pixelColor(x, y), token(Color::IconAccent), 24) ? 1 : 0;
        }
    }
    QVERIFY(checkPixels > 6);

    // A chip of a single choice (filter, tab) shows no check mark and has no place for it
    const std::unique_ptr<QPushButton> filterOn = makeChip(QStringLiteral("Whole words"), false, true, QIcon());
    QCOMPARE(filterOn->sizeHint().width(), plain->sizeHint().width());
    const QImage filterImage = filterOn->grab().toImage();
    int filterAccentPixels = 0;
    for (int y = 0; y < filterImage.height(); ++y)
    {
        for (int x = 0; x < qRound(14 * filterImage.devicePixelRatio()); ++x)
        {
            filterAccentPixels += isNear(filterImage.pixelColor(x, y), token(Color::IconAccent), 24) ? 1 : 0;
        }
    }
    QCOMPARE(filterAccentPixels, 0);

    // Segmented control (Q-16): bg/track without padding, all segments as wide as the widest
    pdf::PDFSegmentedControl presets;
    for (const char* name : { "Smaller file", "Balanced", "Best quality", "Custom\xE2\x80\xA6" })
    {
        presets.addSegment(QString::fromUtf8(name));
    }
    presets.setCurrentIndex(1);
    const QSize hint = presets.sizeHint();
    QCOMPARE(hint.height(), 32);
    QCOMPARE(hint.width() % 4, 0);
    int widest = 0;
    for (const QToolButton* segment : presets.findChildren<QToolButton*>())
    {
        widest = qMax(widest, segment->sizeHint().width());
    }
    QVERIFY(qAbs(widest - qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("Best quality"), pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::CaptionSemiBold))) - 28) <= 1);
    QCOMPARE(hint.width(), 4 * widest);
    presets.resize(hint);
    presets.show();
    QVERIFY(QTest::qWaitForWindowExposed(&presets));
    const QList<QToolButton*> segments = presets.findChildren<QToolButton*>();
    QCOMPARE(segments.size(), 4);
    QCOMPARE(segments.first()->geometry(), QRect(0, 0, widest, 32));
    for (const QToolButton* segment : segments)
    {
        QCOMPARE(segment->width(), widest);
    }
    image = presets.grab().toImage();
    QVERIFY(isNear(pixel(image, 2 * widest + widest / 2, 2), token(Color::BgTrack), 3));

    // The plate of the selected segment has the radius of the track (16): its corner
    // does not reach out of the corner of the track
    // (dark theme: the background of the window differs from the plate; the bottom
    // corners are covered by the shadow of the plate, as in the design)
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("dark")));
    const QImage unselectedCorner = presets.grab().toImage();
    presets.setCurrentIndex(0);
    image = presets.grab().toImage();
    QVERIFY(isNear(pixel(image, 3, 16), token(Color::BgSurface), 3));
    for (const QPoint& point : { QPoint(5, 3), QPoint(3, 5), QPoint(7, 1) })
    {
        QVERIFY2(isNear(pixel(image, point.x(), point.y()), pixel(unselectedCorner, point.x(), point.y()), 6), qPrintable(QStringLiteral("%1,%2").arg(point.x()).arg(point.y())));
    }
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(QStringLiteral("light")));
    QCOMPARE(countLeakPixels(image), 0);

    // A control of a given width (the theme switch of the sidebar, 182) divides it equally
    pdf::PDFSegmentedControl theme;
    theme.addSegment(QStringLiteral("Light"), QStringLiteral("sun"));
    theme.addSegment(QStringLiteral("Dark"), QStringLiteral("moon"));
    theme.addSegment(QStringLiteral("Auto"), QStringLiteral("monitor"));
    theme.setFixedWidth(182);
    theme.show();
    QVERIFY(QTest::qWaitForWindowExposed(&theme));
    for (const QToolButton* segment : theme.findChildren<QToolButton*>())
    {
        QVERIFY2(qAbs(segment->width() - 61) <= 1, qPrintable(QString::number(segment->width())));
        QCOMPARE(segment->height(), 32);
    }
}

void WidgetStyleTest::pointerTargets()
{
    // The strip between the stepper and the edge of the field belongs to the stepper
    QStyleOptionSpinBox spinBox;
    spinBox.rect = QRect(0, 0, 140, 34);
    spinBox.state = QStyle::State_Enabled;
    spinBox.subControls = QStyle::SC_All;
    spinBox.buttonSymbols = QAbstractSpinBox::UpDownArrows;
    spinBox.stepEnabled = QAbstractSpinBox::StepUpEnabled | QAbstractSpinBox::StepDownEnabled;

    QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_SpinBox, &spinBox, QPoint(138, 5), nullptr), QStyle::SC_SpinBoxUp);
    QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_SpinBox, &spinBox, QPoint(138, 28), nullptr), QStyle::SC_SpinBoxDown);
    QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_SpinBox, &spinBox, QPoint(125, 0), nullptr), QStyle::SC_SpinBoxUp);
    QVERIFY(m_style->hitTestComplexControl(QStyle::CC_SpinBox, &spinBox, QPoint(40, 17), nullptr) != QStyle::SC_SpinBoxUp);
    QVERIFY(m_style->hitTestComplexControl(QStyle::CC_SpinBox, &spinBox, QPoint(40, 17), nullptr) != QStyle::SC_SpinBoxDown);

    spinBox.direction = Qt::RightToLeft;
    QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_SpinBox, &spinBox, QPoint(1, 5), nullptr), QStyle::SC_SpinBoxUp);

    QStyleOptionComboBox comboBox;
    comboBox.rect = QRect(0, 0, 200, 34);
    comboBox.state = QStyle::State_Enabled;
    comboBox.subControls = QStyle::SC_All;
    comboBox.editable = true;
    QCOMPARE(m_style->hitTestComplexControl(QStyle::CC_ComboBox, &comboBox, QPoint(198, 2), nullptr), QStyle::SC_ComboBoxArrow);
    QVERIFY(m_style->hitTestComplexControl(QStyle::CC_ComboBox, &comboBox, QPoint(40, 17), nullptr) != QStyle::SC_ComboBoxArrow);
}

void WidgetStyleTest::subMenuMargin()
{
    // The transparent margin of a submenu covers a strip of its parent menu.
    // A click into this strip belongs to the item of the parent menu.
    QWidget window;
    window.resize(300, 200);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QMenu menu(&window);
    QMenu* subMenu = menu.addMenu(QStringLiteral("Open recent"));
    subMenu->addAction(QStringLiteral("First document"));
    subMenu->addAction(QStringLiteral("Second document"));
    QAction* closeAction = menu.addAction(QStringLiteral("Close the document"));
    menu.addAction(QStringLiteral("Print the document"));
    QSignalSpy spy(closeAction, &QAction::triggered);

    menu.popup(window.mapToGlobal(QPoint(20, 20)));
    QVERIFY(QTest::qWaitForWindowExposed(&menu));
    menu.setActiveAction(subMenu->menuAction());
    QTest::keyClick(&menu, Qt::Key_Right);
    QTRY_VERIFY(subMenu->isVisible());

    if (subMenu->contentsMargins().left() < 16)
    {
        QSKIP("Popups are not translucent on this platform, they have no margins.");
    }

    // A point of the item below the submenu item, near the right edge of the panel of the parent menu
    const QRect actionRect = menu.actionGeometry(closeAction);
    const QPoint globalPoint = menu.mapToGlobal(QPoint(actionRect.right() - 3, actionRect.center().y()));
    const QPoint subMenuPoint = subMenu->mapFromGlobal(globalPoint);

    if (!subMenu->rect().contains(subMenuPoint))
    {
        QSKIP("The margin of the submenu does not cover the parent menu.");
    }

    QVERIFY(subMenuPoint.x() < subMenu->contentsMargins().left());
    QTest::mouseClick(subMenu, Qt::LeftButton, Qt::NoModifier, subMenuPoint);
    QTRY_COMPARE(spy.count(), 1);
    QTRY_VERIFY(!menu.isVisible());
}

void WidgetStyleTest::popupPlacement()
{
    // The panel of a menu takes the place, which Qt has given to its window.
    // The window has transparent margins with the shadow of the panel.
    QWidget window;
    window.resize(200, 100);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QRect available = screen->availableGeometry();

    auto popup = [&window](const QPoint& position, QMargins& margins) -> QRect
    {
        QCursor::setPos(position);

        QMenu menu(&window);
        menu.addAction(QStringLiteral("First action"));
        menu.addAction(QStringLiteral("Second action"));
        menu.addAction(QStringLiteral("Third action"));
        menu.popup(position);

        if (!QTest::qWaitForWindowExposed(&menu))
        {
            return QRect();
        }

        QCoreApplication::processEvents();
        margins = menu.contentsMargins();
        const QRect panel = menu.geometry().marginsRemoved(margins);
        menu.hide();
        return panel;
    };

    QMargins margins;
    const QPoint topLeft = available.topLeft() + QPoint(120, 120);
    QRect panel = popup(topLeft, margins);
    QVERIFY(panel.isValid());

    if (margins.left() < 16)
    {
        QSKIP("Popups are not translucent on this platform, they have no margins.");
    }

    // Menu below the pointer
    QCOMPARE(panel.topLeft(), topLeft);

    // Menu, which does not fit below the pointer, lies above it or at the edge of the screen
    const QPoint bottom(available.left() + 120, available.bottom() - 10);
    panel = popup(bottom, margins);
    QVERIFY2(panel.bottom() <= available.bottom() && panel.bottom() >= bottom.y() - 3, qPrintable(QStringLiteral("%1 / %2").arg(panel.bottom()).arg(bottom.y())));

    // Menu at the right edge of the screen ends at the edge
    const QPoint right(available.right() - 5, available.top() + 120);
    panel = popup(right, margins);
    QVERIFY2(panel.right() <= available.right() && panel.right() >= available.right() - 1, qPrintable(QStringLiteral("%1 / %2").arg(panel.right()).arg(available.right())));
}

void WidgetStyleTest::contextMenus()
{
    // The standard context menus of the text widgets (some of them are shown
    // by QMenu::exec) are drawn in the colors of the theme, also their icons
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFLineEdit* lineEdit = new pdf::PDFLineEdit(&window);
    lineEdit->setText(QStringLiteral("Project_Report.pdf"));
    lineEdit->selectAll();
    pdf::PDFPlainTextEdit* plainTextEdit = new pdf::PDFPlainTextEdit(&window);
    plainTextEdit->setPlainText(QStringLiteral("Keywords of the document"));
    plainTextEdit->selectAll();
    pdf::PDFSpinBox* spinBox = new pdf::PDFSpinBox(&window);
    spinBox->setValue(12);
    layout->addWidget(lineEdit);
    layout->addWidget(plainTextEdit);
    layout->addWidget(spinBox);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    for (QWidget* target : { static_cast<QWidget*>(lineEdit), static_cast<QWidget*>(plainTextEdit->viewport()), static_cast<QWidget*>(spinBox) })
    {
        QImage image;
        int actionCount = 0;
        int iconCount = 0;

        QTimer timer;
        timer.setInterval(20);
        QObject::connect(&timer, &QTimer::timeout, &timer, [&]()
        {
            QMenu* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (!menu || !menu->isVisible())
            {
                return;
            }

            timer.stop();
            QCoreApplication::processEvents();
            image = menu->grab().toImage();
            for (const QAction* action : menu->actions())
            {
                if (!action->isSeparator())
                {
                    ++actionCount;
                    iconCount += action->icon().isNull() ? 0 : 1;
                }
            }
            menu->close();
        });
        timer.start();

        QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(10, 10), target->mapToGlobal(QPoint(10, 10)));
        QCoreApplication::sendEvent(target, &event);
        QTRY_VERIFY(!image.isNull());
        QTRY_VERIFY(!QApplication::activePopupWidget());

        const QString name = QString::fromLatin1(target->metaObject()->className());
        QVERIFY2(actionCount > 0, qPrintable(name));
        QVERIFY2(countLeakPixels(image) == 0, qPrintable(QStringLiteral("%1: %2 leak pixels, %3 icons").arg(name).arg(countLeakPixels(image)).arg(iconCount)));
    }
}

void WidgetStyleTest::components()
{
    const QStyle::State enabled = QStyle::State_Enabled | QStyle::State_Active;
    Q_UNUSED(enabled);

    // Icon button: sizes, radius of the focus ring, colors of the icon in the states
    pdf::PDFIconButton iconButton(QStringLiteral("copy"), pdf::PDFIconButton::Size::Small);
    QCOMPARE(iconButton.sizeHint(), QSize(24, 24));
    QCOMPARE(iconButton.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 6.0);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconMuted);
    iconButton.setAttribute(Qt::WA_UnderMouse, true);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconDefault);
    const QColor iconBase = pixel(pdf::PDFIconButton(QStringLiteral("copy")).grab().toImage(), 2, 12);
    QImage image = iconButton.grab().toImage();
    QVERIFY(isNear(pixel(image, 2, 12), blend(token(Color::BgHover), iconBase)));
    iconButton.setDown(true);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconStrong);
    image = iconButton.grab().toImage();
    QVERIFY(isNear(pixel(image, 2, 12), blend(token(Color::BgPressed), iconBase)));
    iconButton.setDown(false);
    iconButton.setIconColor(Color::IconAccent);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconAccent);
    iconButton.setEnabled(false);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconDisabled);
    iconButton.setButtonSize(pdf::PDFIconButton::Size::Medium);
    QCOMPARE(iconButton.sizeHint(), QSize(28, 28));
    iconButton.setButtonSize(pdf::PDFIconButton::Size::TabClose);
    QCOMPARE(iconButton.sizeHint(), QSize(20, 20));
    QCOMPARE(iconButton.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 4.0);
    QCOMPARE(pdf::PDFIconButton::getIconExtent(pdf::PDFIconButton::Size::Small), 14);
    QCOMPARE(pdf::PDFIconButton::getIconExtent(pdf::PDFIconButton::Size::Medium), 16);
    QCOMPARE(pdf::PDFIconButton::getIconExtent(pdf::PDFIconButton::Size::TabClose), 12);

    // Stamp chip: height 26, the label 11 px from the edges, colors of the hue
    pdf::PDFStampChip stamp(QStringLiteral("Approved"), pdf::PDFAccentHue::Green);
    const qreal labelWidth = pdf::PDFStylePainter::textWidth(QStringLiteral("APPROVED"), pdf::PDFStampChip::getLabelFont());
    QCOMPARE(stamp.sizeHint(), QSize(qRound(labelWidth) + 22, 26));
    stamp.resize(stamp.sizeHint());
    image = stamp.grab().toImage();
    QVERIFY(isNear(pixel(image, 3, 13), token(Color::AccentGreenBg)));
    QVERIFY(isNear(pixel(image, 0, 13), token(Color::AccentGreenBorderStrong)));
    stamp.setHue(pdf::PDFAccentHue::Red);
    stamp.setAttribute(Qt::WA_UnderMouse, true);
    image = stamp.grab().toImage();
    QVERIFY(isNear(pixel(image, 3, 13), token(Color::AccentRedBgHover)));
    QCOMPARE(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Neutral, pdf::PDFAccentHueColors::Role::Icon), Color::AccentNeutralIcon);
    QCOMPARE(pdf::PDFAccentHueColors::getColor(pdf::PDFAccentHue::Amber, pdf::PDFAccentHueColors::Role::BackgroundPressed), Color::AccentAmberBgPressed);

    // Navigation: sizes of the two kinds, the selected item does not react to the pointer
    pdf::PDFNavItem navItem(QStringLiteral("Open"), QStringLiteral("folder"));
    QCOMPARE(navItem.sizeHint(), QSize(190, 38));
    navItem.resize(190, 38);
    image = navItem.grab().toImage();
    const QColor navBase = pixel(image, 4, 19);
    navItem.setAttribute(Qt::WA_UnderMouse, true);
    image = navItem.grab().toImage();
    QVERIFY(isNear(pixel(image, 4, 19), blend(token(Color::BgHover), navBase)));
    navItem.setChecked(true);
    image = navItem.grab().toImage();
    QVERIFY(isNear(pixel(image, 4, 19), blend(token(Color::BgAccentSubtle), navBase)));
    navItem.click();
    QVERIFY(navItem.isChecked());

    pdf::PDFNavItem settingsItem(QStringLiteral("General"), QStringLiteral("settings"), pdf::PDFNavItem::Kind::Settings);
    settingsItem.setSubtitle(QStringLiteral("Language, theme, documents"));
    QCOMPARE(settingsItem.sizeHint(), QSize(300, 52));
    QCOMPARE(settingsItem.accessibleDescription(), QStringLiteral("Language, theme, documents"));

    // The arrows move the selection over the border of the groups
    pdf::PDFNavList navList;
    navList.addItem(QStringLiteral("Home"), QStringLiteral("home"));
    navList.addItem(QStringLiteral("Open"), QStringLiteral("folder"));
    navList.addGroup();
    navList.addItem(QStringLiteral("About"), QStringLiteral("info"));
    navList.show();
    QCOMPARE(navList.getCount(), 3);
    QCOMPARE(navList.getCurrentIndex(), 0);
    QSignalSpy navSpy(&navList, &pdf::PDFNavList::currentIndexChanged);
    QTest::keyClick(navList.getItem(0), Qt::Key_Down);
    QTest::keyClick(navList.getItem(1), Qt::Key_Down);
    QCOMPARE(navList.getCurrentIndex(), 2);
    QCOMPARE(navSpy.count(), 2);
    QTest::keyClick(navList.getItem(2), Qt::Key_Home);
    QCOMPARE(navList.getCurrentIndex(), 0);
    QCOMPARE(navList.getItem(2)->y() - navList.getItem(1)->geometry().bottom() - 1, 21);

    // Toolbar field: height 32, at least 64 wide, value and suffix 11 px from the edges
    pdf::PDFToolBarField field(QStringLiteral("308"), QStringLiteral("/ 1592"));
    QCOMPARE(field.sizeHint().height(), 32);
    const QFont valueFont = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold);
    const QFont suffixFont = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Body);
    const int expectedWidth = qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("308"), valueFont) + 4 + pdf::PDFStylePainter::textWidth(QStringLiteral("/ 1592"), suffixFont) + 1) + 22;
    QCOMPARE(field.sizeHint().width(), expectedWidth);
    QCOMPARE(field.accessibleDescription(), QStringLiteral("/ 1592"));
    pdf::PDFToolBarField shortField(QStringLiteral("1"));
    QCOMPARE(shortField.sizeHint(), QSize(64, 32));
    field.resize(field.sizeHint());
    QCOMPARE(countLeakPixels(field.grab().toImage()), 0);
    QCOMPARE(field.property(pdf::PDFWidgetStyleProperties::FIELD_TEXT_INSETS).value<QMargins>().left(), 11);

    // Password field: height 40, the button of 28 px 7 px from the right edge, text at 37
    pdf::PDFLineEdit password(QStringLiteral("secret"));
    password.setPasswordField();
    QVERIFY(password.isPasswordField());
    QCOMPARE(password.sizeHint().height(), 40);
    password.resize(320, 40);
    QCOMPARE(countLeakPixels(password.grab().toImage()), 0);
    QCOMPARE(password.getPasswordButton()->geometry(), QRect(285, 6, 28, 28));
    QCOMPARE(password.property(pdf::PDFWidgetStyleProperties::FIELD_TEXT_INSETS).value<QMargins>(), QMargins(37, 0, 43, 0));
    // The toggle is a plain icon button also in a filled field (decision 22 of components.md)
    QCOMPARE(password.getPasswordButton()->getCurrentIconColor(), Color::IconMuted);
    password.getPasswordButton()->click();
    QCOMPARE(password.echoMode(), QLineEdit::Normal);
    QCOMPARE(password.getPasswordButton()->getIconName(), QStringLiteral("eye-off"));
    QCOMPARE(password.getPasswordButton()->accessibleName(), QStringLiteral("Hide password"));
    password.clear();
    QCOMPARE(password.getPasswordButton()->getCurrentIconColor(), Color::IconMuted);
    password.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(password.getPasswordButton()->geometry(), QRect(7, 6, 28, 28));
    QCOMPARE(countLeakPixels(password.grab().toImage()), 0);

    // Document tabs: 32 px high also without the close button, the lock and the dot widen the tab
    pdf::PDFDocumentTabBar tabBar;
    const int home = tabBar.addTab(QStringLiteral("Home"));
    const int document = tabBar.addTab(QStringLiteral("Project_Report.pdf"));
    tabBar.setTabClosable(document, true);
    QVERIFY(tabBar.isTabClosable(document));
    QVERIFY(!tabBar.isTabClosable(home));
    const int plainWidth = tabBar.tabRect(document).width();
    tabBar.setTabLocked(document, true);
    tabBar.setTabUnsaved(document, true);
    tabBar.setTabProgress(document, 0.5);
    QCOMPARE(tabBar.tabRect(document).width(), plainWidth + 8 + 12 + 8 + 8);
    QCOMPARE(tabBar.tabRect(home).height(), tabBar.tabRect(document).height());
    QCOMPARE(tabBar.getTabProgress(document), 0.5);
    QSignalSpy closeSpy(&tabBar, &QTabBar::tabCloseRequested);
    qobject_cast<QAbstractButton*>(tabBar.tabButton(document, QTabBar::RightSide))->click();
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(closeSpy.front().front().toInt(), document);
    tabBar.moveTab(document, home);
    QVERIFY(tabBar.isTabLocked(0));
    QVERIFY(!tabBar.isTabLocked(1));
    tabBar.resize(tabBar.sizeHint());
    QCOMPARE(countLeakPixels(tabBar.grab().toImage()), 0);

    // Key value rows: 28 px each, label column 120, the value is elided
    pdf::PDFKeyValueList keyValues;
    keyValues.addRow(QStringLiteral("Input"), QStringLiteral("One open PDF"));
    keyValues.addRow(QStringLiteral("Result"), QStringLiteral("A smaller PDF"));
    QCOMPARE(keyValues.getRowCount(), 2);
    keyValues.resize(360, keyValues.heightForWidth(360));
    QCOMPARE(keyValues.height(), 56);
    QCOMPARE(keyValues.getRowRect(1), QRect(0, 28, 360, 28));
    QCOMPARE(keyValues.getRowAt(QPoint(10, 40)), 1);
    QVERIFY(keyValues.accessibleDescription().contains(QStringLiteral("Result: A smaller PDF")));
    QCOMPARE(countLeakPixels(keyValues.grab().toImage()), 0);
    keyValues.clear();
    QCOMPARE(keyValues.getRowCount(), 0);

    // Filter chip, which is a button, shows its count
    QPushButton chip(QStringLiteral("Workspaces"));
    chip.setProperty(pdf::PDFWidgetStyleProperties::CHIP, true);
    const int chipWidth = chip.sizeHint().width();
    pdf::PDFWidgetStyleProperties::set(&chip, pdf::PDFWidgetStyleProperties::CHIP_COUNT, 12);
    QVERIFY(chipWidth > 64);
    QCOMPARE(chip.sizeHint().width(), qMax(64, chipWidth + 6 + qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("12"), pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::Caption)))));
}

void WidgetStyleTest::disabledAndSelect()
{
    // Disabled controls have text/disabled and icon/disabled, without opacity
    QCOMPARE(pdf::PDFThemeIcons::getModeColor(QIcon::Disabled), Color::IconDisabled);
    QCOMPARE(token(Color::TextDisabled), QColor(0x94, 0xa3, 0xb8));

    pdf::PDFLabel label(QStringLiteral("Disabled"));
    QCOMPARE(label.palette().color(QPalette::Disabled, QPalette::Text), token(Color::TextDisabled));

    // Select: hover is the fill bg/hover, the open select bg/pressed with border/strong
    pdf::PDFComboBox select;
    select.addItems({ QStringLiteral("Last opened"), QStringLiteral("Name") });
    select.setSelectPrefix(QStringLiteral("Sort:"));
    select.resize(select.sizeHint());
    QImage image = select.grab().toImage();
    QVERIFY(isNear(pixel(image, 0, 16), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 6, 16), token(Color::BgSurface)));
    select.setAttribute(Qt::WA_UnderMouse, true);
    image = select.grab().toImage();
    QVERIFY(isNear(pixel(image, 6, 16), blend(token(Color::BgHover), token(Color::BgSurface))));
    QVERIFY(isNear(pixel(image, 0, 16), token(Color::BorderDefault)));
    select.setEnabled(false);
    image = select.grab().toImage();
    QVERIFY(isNear(pixel(image, 6, 16), token(Color::BgMuted)));

    // Segment with its own color of the icon
    pdf::PDFSegmentedControl segmented;
    const int light = segmented.addSegment(QStringLiteral("Light"), QStringLiteral("sun"));
    segmented.addSegment(QStringLiteral("Auto"), QStringLiteral("monitor"));
    segmented.setSegmentIconColor(light, Color::IconSun);
    segmented.setCurrentIndex(1);
    segmented.resize(182, 32);
    image = segmented.grab().toImage();
    int sunPixels = 0;
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width() / 2; ++x)
        {
            sunPixels += isNear(image.pixelColor(x, y), token(Color::IconSun), 8) ? 1 : 0;
        }
    }
    QVERIFY(sunPixels > 10);
}

void WidgetStyleTest::rowStyles()
{
    QStandardItemModel model;
    QStandardItem* chapter = new QStandardItem(QStringLiteral("Market Analysis"));
    chapter->appendRow(new QStandardItem(QStringLiteral("4.1 Regional revenue")));
    model.appendRow(chapter);

    // Outline row: indentation 18, chevron slot of 16 at 6 px, the title at 6 + 16 + 4
    pdf::PDFTreeView tree;
    tree.setModel(&model);
    tree.setHeaderHidden(true);
    tree.setRowStyle(pdf::PDFTreeView::RowStyle::Outline);
    tree.expandAll();
    tree.resize(300, 200);
    tree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tree));
    QCOMPARE(tree.indentation(), 18);

    const QModelIndex index = model.index(0, 0);
    QStyleOptionViewItem option;
    option.initFrom(&tree);
    option.rect = tree.visualRect(index);
    option.index = index;
    option.text = index.data().toString();
    option.features = QStyleOptionViewItem::HasDisplay;
    QCOMPARE(option.rect.left(), 18);
    QCOMPARE(tree.style()->subElementRect(QStyle::SE_ItemViewItemText, &option, &tree).left(), 26);
    QCOMPARE(option.rect.height(), 30);

    QStyleOption branch;
    branch.initFrom(&tree);
    branch.rect = QRect(0, option.rect.top(), 18, option.rect.height());
    QCOMPARE(tree.style()->subElementRect(QStyle::SE_TreeViewDisclosureItem, &branch, &tree), QRect(6, option.rect.top(), 16, option.rect.height()));

    // Contents of the manual (help-contents 955:152873): rows 28, indentation 24,
    // the chevron 14 at 8, the title of a chapter at 8 + 14 + 8, the press tracked
    tree.setRowStyle(pdf::PDFTreeView::RowStyle::Contents);
    QCOMPARE(tree.getRowStyle(), pdf::PDFTreeView::RowStyle::Contents);
    QCOMPARE(tree.property(pdf::PDFWidgetStyleProperties::ROW_STYLE).toString(), QStringLiteral("contents"));
    QCOMPARE(tree.indentation(), 24);
    QCOMPARE(pdf::PDFTreeView::getRowRadius(pdf::PDFTreeView::RowStyle::Contents), 6.0);
    tree.doItemsLayout();
    option.rect = tree.visualRect(index);
    QCOMPARE(option.rect.left(), 24);
    QCOMPARE(option.rect.height(), 28);
    QCOMPARE(tree.style()->subElementRect(QStyle::SE_ItemViewItemText, &option, &tree).left(), 30);
    branch.rect = QRect(0, option.rect.top(), 24, option.rect.height());
    QCOMPARE(tree.style()->subElementRect(QStyle::SE_TreeViewDisclosureItem, &branch, &tree), QRect(8, option.rect.top(), 14, option.rect.height()));
    const QModelIndex topic = model.index(0, 0, index);
    QCOMPARE(tree.visualRect(topic).height(), 28);
    QCOMPARE(tree.visualRect(topic).left(), 48);

    // A topic has no chevron: its text starts at 24 per level (toc-Protect with
    // Password at 24, the chapter toc-Protect & Sign at 30), mirrored in
    // right-to-left (review R2-D2)
    QStyleOptionViewItem topicOption = option;
    topicOption.rect = tree.visualRect(topic);
    topicOption.index = topic;
    topicOption.text = topic.data().toString();
    QCOMPARE(tree.style()->subElementRect(QStyle::SE_ItemViewItemText, &topicOption, &tree).left(), 24);
    QCOMPARE(tree.style()->subElementRect(QStyle::SE_ItemViewItemText, &option, &tree).left(), 30);
    tree.setLayoutDirection(Qt::RightToLeft);
    tree.doItemsLayout();
    topicOption.rect = tree.visualRect(topic);
    topicOption.direction = Qt::RightToLeft;
    QCOMPARE(tree.style()->subElementRect(QStyle::SE_ItemViewItemText, &topicOption, &tree).right() + 1, tree.viewport()->width() - 24);
    tree.setLayoutDirection(Qt::LeftToRight);
    tree.doItemsLayout();
    QTest::mousePress(tree.viewport(), Qt::LeftButton, Qt::NoModifier, tree.visualRect(topic).center());
    QCOMPARE(tree.getPressedRow(), topic);
    QTest::mouseRelease(tree.viewport(), Qt::LeftButton, Qt::NoModifier, tree.visualRect(topic).center());
    QVERIFY(!tree.getPressedRow().isValid());

    // Certificate row: height 34
    tree.setRowStyle(pdf::PDFTreeView::RowStyle::Certificate);
    tree.setRootIsDecorated(false);
    tree.doItemsLayout();
    QCOMPARE(tree.visualRect(index).height(), 34);
    QCOMPARE(tree.indentation(), 16);

    // Library row: height 30, back to the default
    tree.setRowStyle(pdf::PDFTreeView::RowStyle::Library);
    tree.doItemsLayout();
    QCOMPARE(tree.visualRect(index).height(), 30);
    tree.setRowStyle(pdf::PDFTreeView::RowStyle::Default);
    QVERIFY(!tree.property(pdf::PDFWidgetStyleProperties::ROW_STYLE).isValid());
}

/// R3: deletes its victim, when the watched widget gets the change of the style
class StyleChangeDeleter : public QObject
{
public:
    explicit StyleChangeDeleter(QObject* parent) : QObject(parent) { }

    QPointer<QWidget> victim;
    int deleted = 0;

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::StyleChange && victim)
        {
            // The watched widget itself is deleted only by a filter, which is not its child
            const bool isSelf = victim == watched;
            if (isSelf && parent() == watched)
            {
                return QObject::eventFilter(watched, event);
            }

            delete victim.data();
            ++deleted;

            if (isSelf)
            {
                // The receiver is gone, the event must not be delivered
                return true;
            }
        }

        return QObject::eventFilter(watched, event);
    }
};

/// Counts the queued calls (QEvent::MetaCall) delivered to the implementation of the style
class StyleMetaCallCounter : public QObject
{
public:
    int count = 0;

    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::MetaCall && watched && watched->inherits("pdf::PDFWidgetStyleImpl"))
        {
            ++count;
        }
        return false;
    }
};

void WidgetStyleTest::styleToolBarConnections()
{
    // R3: the margins of a toolbar follow its orientation (a member slot of the
    // style, the sender is the toolbar) and its change of the style (a queued
    // member slot over a list of the toolbars)
    const QMargins horizontal(8, 12, 16, 4);
    const QMargins vertical(12, 8, 4, 16);

    QWidget host;
    host.resize(300, 300);
    QToolBar* toolBar = new QToolBar(&host);
    toolBar->addAction(pdf::PDFThemeIcons::getIcon(QStringLiteral("save")), QStringLiteral("Save"));
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    QVERIFY(toolBar->layout());
    QCOMPARE(toolBar->layout()->contentsMargins(), horizontal);

    toolBar->setOrientation(Qt::Vertical);
    QCOMPARE(toolBar->layout()->contentsMargins(), vertical);
    toolBar->setOrientation(Qt::Horizontal);
    QCOMPARE(toolBar->layout()->contentsMargins(), horizontal);

    // More changes of the style are applied once from the event loop: one
    // queued call of the style for all of them (T2)
    StyleMetaCallCounter metaCalls;
    qApp->installEventFilter(&metaCalls);
    toolBar->layout()->setContentsMargins(1, 1, 1, 1);
    QEvent styleChange(QEvent::StyleChange);
    QCoreApplication::sendEvent(toolBar, &styleChange);
    QCoreApplication::sendEvent(toolBar, &styleChange);
    QCoreApplication::sendEvent(toolBar, &styleChange);
    // The toolbar itself has set the same margin to all sides
    QVERIFY(toolBar->layout()->contentsMargins() != horizontal);
    QTRY_COMPARE(toolBar->layout()->contentsMargins(), horizontal);
    QCoreApplication::processEvents();
    qApp->removeEventFilter(&metaCalls);
    QCOMPARE(metaCalls.count, 1);

    // A toolbar deleted before the event loop runs is skipped, the next one is served
    QToolBar* deletedToolBar = new QToolBar(&host);
    deletedToolBar->addAction(QStringLiteral("Open"));
    deletedToolBar->ensurePolished();
    QCoreApplication::sendEvent(deletedToolBar, &styleChange);
    delete deletedToolBar;
    toolBar->layout()->setContentsMargins(1, 1, 1, 1);
    QCoreApplication::sendEvent(toolBar, &styleChange);
    QTRY_COMPARE(toolBar->layout()->contentsMargins(), horizontal);

    // A toolbar with another style is not followed by this style any more
    std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
    QVERIFY(fusion);
    QToolBar* fusionToolBar = new QToolBar(&host);
    fusionToolBar->addAction(QStringLiteral("Print"));
    fusionToolBar->ensurePolished();
    QCOMPARE(fusionToolBar->layout()->contentsMargins(), horizontal);
    fusionToolBar->setStyle(fusion.get());
    QCoreApplication::processEvents();
    const QMargins fusionMargins = fusionToolBar->layout()->contentsMargins();
    QVERIFY(fusionMargins != horizontal);
    fusionToolBar->setOrientation(Qt::Vertical);
    QCoreApplication::processEvents();
    QVERIFY(fusionToolBar->layout()->contentsMargins() != vertical);
    delete fusionToolBar;

    // S7: a toolbar scheduled by a change of the style, which gets its own style
    // before the event loop runs, keeps the margins of its own style
    QToolBar* lateToolBar = new QToolBar(&host);
    lateToolBar->addAction(QStringLiteral("Export"));
    lateToolBar->ensurePolished();
    QCOMPARE(lateToolBar->layout()->contentsMargins(), horizontal);
    QCoreApplication::sendEvent(lateToolBar, &styleChange);
    lateToolBar->setStyle(fusion.get());
    const QMargins lateFusionMargins = lateToolBar->layout()->contentsMargins();
    QVERIFY(lateFusionMargins != horizontal);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(lateToolBar->layout()->contentsMargins(), lateFusionMargins);
    delete lateToolBar;

    // A tab bar is repainted by the style, whenever its current tab changes; the
    // tab bar deleted first and a tab bar of another style are left alone
    QTabBar* tabBar = new QTabBar(&host);
    tabBar->addTab(QStringLiteral("One"));
    tabBar->addTab(QStringLiteral("Two"));
    tabBar->show();
    tabBar->setCurrentIndex(1);
    tabBar->setStyle(fusion.get());
    tabBar->setCurrentIndex(0);
    QCOMPARE(tabBar->currentIndex(), 0);
    tabBar->setStyle(nullptr);
    tabBar->setCurrentIndex(1);
    delete tabBar;
    QCoreApplication::processEvents();
}

void WidgetStyleTest::styleSplitterHover()
{
    // R3: the line of a splitter handle appears, when the pointer rests over it
    // (one timer of the style, which waits for the last entered handle). T1:
    // deterministic - no window is shown (the filter of the application sees
    // the events sent to hidden widgets, the style reads only underMouse()),
    // every wait is for the decision of the timer (isSplitterHoverPending) with
    // a long timeout, the negative checks follow the decision.
    static constexpr const char* HOVER = "_pdf4qt_splitterHover";
    pdf::PDFThemeManager* manager = pdf::PDFThemeManager::getInstance();
    const bool wasReducedMotion = manager->isReducedMotion();
    manager->setReducedMotion(true);
    pdf::PDFWidgetStyle* style = m_style;
    auto waitForDecision = [style]() { return QTest::qWaitFor([style]() { return !style->isSplitterHoverPending(); }, 30000); };
    QVERIFY(waitForDecision());

    QSplitter* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(new QWidget());
    splitter->addWidget(new QWidget());
    splitter->addWidget(new QWidget());
    splitter->resize(300, 100);
    splitter->ensurePolished();

    QPointer<QSplitterHandle> first = splitter->handle(1);
    QPointer<QSplitterHandle> second = splitter->handle(2);
    QVERIFY(first && second);

    QEvent enter(QEvent::Enter);
    QEvent leave(QEvent::Leave);

    // The line appears after the delay (0 ms with reduced motion: the timer is
    // pending until the event loop runs)
    first->setAttribute(Qt::WA_UnderMouse, true);
    QCoreApplication::sendEvent(first, &enter);
    QVERIFY(style->isSplitterHoverPending());
    QVERIFY(!first->property(HOVER).toBool());
    QVERIFY(waitForDecision());
    QVERIFY(first->property(HOVER).toBool());

    first->setAttribute(Qt::WA_UnderMouse, false);
    QCoreApplication::sendEvent(first, &leave);
    QVERIFY(!first->property(HOVER).toBool());

    // One timer for the last entered handle: both handles are under the pointer
    // (the old timers of each handle would give both the line)
    first->setAttribute(Qt::WA_UnderMouse, true);
    second->setAttribute(Qt::WA_UnderMouse, true);
    QCoreApplication::sendEvent(first, &enter);
    QCoreApplication::sendEvent(second, &enter);
    QVERIFY(style->isSplitterHoverPending());
    QVERIFY(waitForDecision());
    QVERIFY(second->property(HOVER).toBool());
    QVERIFY(!first->property(HOVER).toBool());
    first->setAttribute(Qt::WA_UnderMouse, false);
    second->setAttribute(Qt::WA_UnderMouse, false);
    QCoreApplication::sendEvent(second, &leave);
    QVERIFY(!second->property(HOVER).toBool());

    // A handle not under the pointer when the time runs out gets no line (the
    // timer has run and decided; QApplication sets WA_UnderMouse on the Enter
    // after the filter of the style, the pointer leaves without a Leave)
    QCoreApplication::sendEvent(second, &enter);
    QVERIFY(second->underMouse());
    second->setAttribute(Qt::WA_UnderMouse, false);
    QVERIFY(style->isSplitterHoverPending());
    QVERIFY(waitForDecision());
    QVERIFY(!second->property(HOVER).toBool());

    // The animated delay of 300 ms: pending after the Enter, the line after it
    manager->setReducedMotion(false);
    second->setAttribute(Qt::WA_UnderMouse, true);
    QCoreApplication::sendEvent(second, &enter);
    QVERIFY(style->isSplitterHoverPending());
    QVERIFY(!second->property(HOVER).toBool());
    QVERIFY(waitForDecision());
    QVERIFY(second->property(HOVER).toBool());
    second->setAttribute(Qt::WA_UnderMouse, false);
    QCoreApplication::sendEvent(second, &leave);

    // The handle is deleted, while the animated timer runs: the timer decides
    // for no handle
    second->setAttribute(Qt::WA_UnderMouse, true);
    QCoreApplication::sendEvent(second, &enter);
    QVERIFY(style->isSplitterHoverPending());
    delete splitter;
    QVERIFY(!first && !second);
    QVERIFY(waitForDecision());
    QVERIFY(!style->isSplitterHoverPending());

    manager->setReducedMotion(wasReducedMotion);
}

void WidgetStyleTest::styleTextPaletteBinder()
{
    // R3: the text palette bound to a widget follows the theme by a child object
    // of the widget, which lives as long as the widget
    pdf::PDFThemeManager* manager = pdf::PDFThemeManager::getInstance();
    QLineEdit edit;
    pdf::PDFThemeWidgetHelper::bindTextPalette(&edit, Color::TextMuted);
    pdf::PDFThemeWidgetHelper::bindTextPalette(&edit, Color::TextMuted);
    QCOMPARE(edit.findChildren<QObject*>(QStringLiteral("_pdf4qt_textPaletteBinder"), Qt::FindDirectChildrenOnly).size(), qsizetype(1));
    QCOMPARE(edit.palette().color(QPalette::Active, QPalette::Text), token(Color::TextMuted));
    QCOMPARE(edit.palette().color(QPalette::Disabled, QPalette::Text), token(Color::TextDisabled));

    manager->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(edit.palette().color(QPalette::Active, QPalette::Text), token(Color::TextMuted));

    // A new color of the text is taken by the same binding
    pdf::PDFThemeWidgetHelper::bindTextPalette(&edit, Color::TextAccent);
    QCOMPARE(edit.findChildren<QObject*>(QStringLiteral("_pdf4qt_textPaletteBinder"), Qt::FindDirectChildrenOnly).size(), qsizetype(1));
    manager->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(edit.palette().color(QPalette::Active, QPalette::Text), token(Color::TextAccent));

    // Widgets deleted after the binding, in both orders of the parent and the child
    QWidget* parent = new QWidget();
    QLineEdit* child = new QLineEdit(parent);
    pdf::PDFThemeWidgetHelper::bindTextPalette(parent);
    pdf::PDFThemeWidgetHelper::bindTextPalette(child);
    delete child;
    manager->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    delete parent;
    manager->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // S6: the binding is found by its binder, not by a flag: a binder deleted
    // by the application is created again by the next binding
    QLineEdit rebound;
    pdf::PDFThemeWidgetHelper::bindTextPalette(&rebound, Color::TextMuted);
    qDeleteAll(rebound.findChildren<QObject*>(QStringLiteral("_pdf4qt_textPaletteBinder"), Qt::FindDirectChildrenOnly));
    QVERIFY(rebound.findChildren<QObject*>(QStringLiteral("_pdf4qt_textPaletteBinder"), Qt::FindDirectChildrenOnly).isEmpty());
    pdf::PDFThemeWidgetHelper::bindTextPalette(&rebound, Color::TextMuted);
    QCOMPARE(rebound.findChildren<QObject*>(QStringLiteral("_pdf4qt_textPaletteBinder"), Qt::FindDirectChildrenOnly).size(), qsizetype(1));
    manager->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(rebound.palette().color(QPalette::Active, QPalette::Text), token(Color::TextMuted));
    manager->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QCOMPARE(rebound.palette().color(QPalette::Active, QPalette::Text), token(Color::TextMuted));

    // The editor of an editable combo box gets its binder again after a polish
    pdf::PDFComboBox combo;
    combo.setEditable(true);
    combo.ensurePolished();
    QVERIFY(combo.lineEdit());
    qDeleteAll(combo.lineEdit()->findChildren<QObject*>(QStringLiteral("_pdf4qt_textPaletteBinder"), Qt::FindDirectChildrenOnly));
    QEvent polish(QEvent::Polish);
    QCoreApplication::sendEvent(&combo, &polish);
    QCOMPARE(combo.lineEdit()->findChildren<QObject*>(QStringLiteral("_pdf4qt_textPaletteBinder"), Qt::FindDirectChildrenOnly).size(), qsizetype(1));

    // A null widget is ignored
    pdf::PDFThemeWidgetHelper::bindTextPalette(nullptr);

    // The button of a password field: the toggle (a member slot of the field)
    // shows the password, the deleted button is not returned
    pdf::PDFLineEdit password;
    password.setPasswordField();
    password.setText(QStringLiteral("secret"));
    QPointer<pdf::PDFIconButton> button = password.getPasswordButton();
    QVERIFY(button);
    button->click();
    QCOMPARE(password.echoMode(), QLineEdit::Normal);
    QCOMPARE(button->accessibleName(), QStringLiteral("Hide password"));
    button->click();
    QCOMPARE(password.echoMode(), QLineEdit::Password);
    QCOMPARE(button->accessibleName(), QStringLiteral("Show password"));
    delete button.data();
    QVERIFY(!password.getPasswordButton());
    password.setText(QStringLiteral("other"));

    // Segments of a segmented control (a member slot of the control)
    pdf::PDFSegmentedControl segments;
    segments.addSegment(QStringLiteral("One"));
    segments.addSegment(QStringLiteral("Two"));
    QSignalSpy spy(&segments, &pdf::PDFSegmentedControl::currentIndexChanged);
    segments.setCurrentIndex(1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    segments.setCurrentIndex(1);
    QCOMPARE(spy.count(), 1);
}

void WidgetStyleTest::styleThemeChangeDeletesWidgets()
{
    // R3: a change of the theme sends the change of the style to all widgets; a
    // handler of the application, which deletes another widget, does not leave
    // a dangling widget in the list of the style
    QWidget* first = new QWidget();
    QWidget* second = new QWidget();
    QToolBar* toolBar = new QToolBar();
    toolBar->addAction(QStringLiteral("Save"));
    toolBar->ensurePolished();

    StyleChangeDeleter* firstDeleter = new StyleChangeDeleter(first);
    StyleChangeDeleter* secondDeleter = new StyleChangeDeleter(second);
    StyleChangeDeleter* toolBarDeleter = new StyleChangeDeleter(toolBar);
    firstDeleter->victim = second;
    secondDeleter->victim = toolBar;
    toolBarDeleter->victim = first;
    first->installEventFilter(firstDeleter);
    second->installEventFilter(secondDeleter);
    toolBar->installEventFilter(toolBarDeleter);

    QPointer<QWidget> firstGuard(first);
    QPointer<QWidget> secondGuard(second);
    QPointer<QWidget> toolBarGuard(toolBar);

    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);

    // The first widget to get the event deletes the next one in the ring, the
    // survivor of the pair deletes nothing more after its victim is gone
    const int alive = (firstGuard ? 1 : 0) + (secondGuard ? 1 : 0) + (toolBarGuard ? 1 : 0);
    QVERIFY2(alive >= 1 && alive <= 2, qPrintable(QString::number(alive)));

    delete firstGuard.data();
    delete secondGuard.data();
    delete toolBarGuard.data();
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
}

void WidgetStyleTest::styleSafeConstructs()
{
    // R3: a scroll bar option with a negative page step (QAbstractSlider keeps it
    // positive, an option of the application can have any value) divided by zero
    QStyleOptionSlider option;
    option.rect = QRect(0, 0, 12, 200);
    option.orientation = Qt::Vertical;
    option.state = QStyle::State_Enabled;
    option.minimum = 0;
    option.maximum = 10;
    option.pageStep = -10;
    option.sliderPosition = 5;
    const QRect slider = m_style->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, nullptr);
    QCOMPARE(slider.height(), 32);
    QVERIFY(QRect(0, 0, 12, 200).contains(slider));

    option.pageStep = 0;
    QCOMPARE(m_style->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, nullptr).height(), 32);
    option.pageStep = 10;
    QCOMPARE(m_style->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, nullptr).height(), 100);

    QImage image(QSize(12, 200), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    option.pageStep = -10;
    m_style->drawComplexControl(QStyle::CC_ScrollBar, &option, &painter, nullptr);
    painter.end();

    // A property of the style set to a widget, which the application deletes,
    // when the widget gets the change of the style
    QWidget* doomed = new QWidget();
    doomed->ensurePolished();
    QPointer<QWidget> doomedGuard(doomed);
    StyleChangeDeleter selfDeleter(nullptr);
    selfDeleter.victim = doomed;
    doomed->installEventFilter(&selfDeleter);
    pdf::PDFWidgetStyleProperties::set(doomed, pdf::PDFWidgetStyleProperties::CARD, true);
    QVERIFY(!doomedGuard);
    QCOMPARE(selfDeleter.deleted, 1);
    QCoreApplication::processEvents();

    // The rubber band gets its translucent window from the style and the
    // attributes back, when the style leaves it
    QRubberBand band(QRubberBand::Rectangle);
    const bool wasTranslucent = band.testAttribute(Qt::WA_TranslucentBackground);
    const bool hadNoBackground = band.testAttribute(Qt::WA_NoSystemBackground);
    band.ensurePolished();
    QVERIFY(band.testAttribute(Qt::WA_TranslucentBackground));

    std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("Fusion")));
    QVERIFY(fusion);
    band.setStyle(fusion.get());
    QCOMPARE(band.testAttribute(Qt::WA_TranslucentBackground), wasTranslucent);
    QCOMPARE(band.testAttribute(Qt::WA_NoSystemBackground), hadNoBackground);

    // And again, when it returns to the style
    band.setStyle(nullptr);
    QVERIFY(band.testAttribute(Qt::WA_TranslucentBackground));
    band.setStyle(fusion.get());
    QCOMPARE(band.testAttribute(Qt::WA_TranslucentBackground), wasTranslucent);
    band.setStyle(nullptr);

    // The tooltip hidden by another way removes its filter of the application
    // with the next hide, a new tooltip works
    QWidget owner;
    owner.resize(100, 100);
    owner.show();
    QVERIFY(QTest::qWaitForWindowExposed(&owner));

    // T4: the filter of the application is seen by its effect, a click hides
    // the tooltip. The window shown again directly (without showText, which
    // installs the filter): a click hides it while the filter of the tooltip
    // hidden by another way is still installed...
    pdf::PDFToolTip::showText(owner.mapToGlobal(QPoint(10, 10)), QStringLiteral("Probe"), QString(), QString(), &owner);
    QVERIFY(pdf::PDFToolTip::isVisible());
    QPointer<QWidget> tipWindow = pdf::PDFToolTip::getWindow();
    QVERIFY(tipWindow);
    tipWindow->hide();
    tipWindow->show();
    QTest::mouseClick(&owner, Qt::LeftButton);
    QVERIFY(!pdf::PDFToolTip::isVisible());
    // ...and does not hide it after hideText has removed the filter
    pdf::PDFToolTip::showText(owner.mapToGlobal(QPoint(10, 10)), QStringLiteral("Probe"), QString(), QString(), &owner);
    QVERIFY(pdf::PDFToolTip::isVisible());
    tipWindow->hide();
    pdf::PDFToolTip::hideText();
    tipWindow->show();
    QTest::mouseClick(&owner, Qt::LeftButton);
    QVERIFY(pdf::PDFToolTip::isVisible());
    tipWindow->hide();
    pdf::PDFToolTip::hideText();
    QVERIFY(!pdf::PDFToolTip::isVisible());

    pdf::PDFToolTip::showText(owner.mapToGlobal(QPoint(10, 10)), QStringLiteral("Title"), QString(), QStringLiteral("Description\nSecond paragraph"), &owner);
    QVERIFY(pdf::PDFToolTip::isVisible());
    QVERIFY(pdf::PDFToolTip::getWindow());
    pdf::PDFToolTip::getWindow()->repaint();
    pdf::PDFToolTip::getWindow()->hide();
    pdf::PDFToolTip::hideText();
    QVERIFY(!pdf::PDFToolTip::isVisible());
    pdf::PDFToolTip::showText(owner.mapToGlobal(QPoint(10, 10)), QStringLiteral("Title"), QStringLiteral("Ctrl+T"), QString(), &owner);
    QVERIFY(pdf::PDFToolTip::isVisible());
    QCOMPARE(pdf::PDFToolTip::getTitle(), QStringLiteral("Title"));
    QTest::mouseClick(&owner, Qt::LeftButton);
    QVERIFY(!pdf::PDFToolTip::isVisible());
}

void WidgetStyleTest::styleReviewFixes()
{
    // S2: a huge or non-finite size is bounded before it is converted to an
    // integer (UBSan sees the overflow, MSVC gives INT_MIN): no pixmap, no shadow
    const QColor black(Qt::black);
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    QVERIFY(!pdf::PDFThemeIcons::getPixmap(QStringLiteral("save"), QSizeF(16.0, 16.0), 1.0, black).isNull());
    QVERIFY(!pdf::PDFThemeIcons::getPixmap(QStringLiteral("save"), QSizeF(0.0001, 0.0001), 1.0, black).isNull());
    for (const QSizeF& size : { QSizeF(nan, 16.0), QSizeF(16.0, infinity), QSizeF(1e12, 1e12), QSizeF(16384.0, 20000.0), QSizeF(-1.0, 16.0) })
    {
        QVERIFY(pdf::PDFThemeIcons::getPixmap(QStringLiteral("save"), size, 1.0, black).isNull());
        QVERIFY(pdf::PDFThemeIcons::getFillPixmap(QStringLiteral("bookmark"), size, 1.0, black).isNull());
    }
    for (qreal ratio : { nan, infinity, 1e12, 0.0, -2.0 })
    {
        QVERIFY(pdf::PDFThemeIcons::getPixmap(QStringLiteral("save"), QSizeF(16.0, 16.0), ratio, black).isNull());
    }

    QImage blank(64, 64, QImage::Format_ARGB32_Premultiplied);
    blank.fill(Qt::transparent);
    QImage image = render([](QPainter& painter)
    {
        const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
        pdf::PDFStylePainter::shadow(&painter, QRectF(8, 8, 40, 40), 8.0, QPointF(0, 2), nan, Qt::black);
        pdf::PDFStylePainter::shadow(&painter, QRectF(8, 8, 40, 40), 8.0, QPointF(0, 2), 1e300, Qt::black);
        pdf::PDFStylePainter::shadow(&painter, QRectF(8, 8, 1e12, 40), 8.0, QPointF(0, 2), 8.0, Qt::black);
        pdf::PDFStylePainter::shadow(&painter, QRectF(8, 8, std::numeric_limits<qreal>::infinity(), 40), 8.0, QPointF(0, 2), 8.0, Qt::black);
    }, QSize(64, 64));
    QCOMPARE(image, blank);
    image = render([](QPainter& painter) { pdf::PDFStylePainter::shadow(&painter, QRectF(8, 8, 40, 40), 8.0, QPointF(0, 2), 8.0, Qt::black); }, QSize(64, 64));
    QVERIFY(image != blank);

    // A huge scale of the painter draws the picture itself (no scaled copy)
    QPixmap picture(200, 100);
    picture.fill(Qt::red);
    for (qreal scale : { 1e-12, 1e12 })
    {
        render([&picture, scale](QPainter& painter)
        {
            painter.scale(scale, scale);
            pdf::PDFComponentPainter::drawScaledPixmap(&painter, QRectF(0, 0, 20, 10), picture);
        }, QSize(64, 64));
    }
    image = render([&picture](QPainter& painter) { pdf::PDFComponentPainter::drawScaledPixmap(&painter, QRectF(0, 0, 20, 10), picture); }, QSize(64, 64));
    QVERIFY(isNear(pixel(image, 10, 5), QColor(Qt::red)));

    // S12: the caches of the icons are bounded: the names of missing icons keep
    // no renderer, the other widths of the strokes at most 256 renderers
    const qsizetype before = pdf::PDFThemeIcons::getCachedRendererCount();
    for (int i = 0; i < 1000; ++i)
    {
        QVERIFY(!pdf::PDFThemeIcons::hasIcon(QStringLiteral("no-such-icon-%1").arg(i)));
    }
    QCOMPARE(pdf::PDFThemeIcons::getCachedRendererCount(), before);
    const qsizetype designRenderers = pdf::PDFThemeIcons::getIconNames().size() * 2;
    for (int i = 1; i <= 800; ++i)
    {
        // Each size has another width of the strokes (2 px at each size)
        const QPixmap pixmap = pdf::PDFThemeIcons::getPixmap(QStringLiteral("save"), QSizeF(i * 0.1 + 0.05, i * 0.1 + 0.05), 1.0, black);
        QVERIFY(!pixmap.isNull());
        QVERIFY(pdf::PDFThemeIcons::getCachedRendererCount() <= designRenderers + 256);
    }
    QVERIFY(pdf::PDFThemeIcons::hasIcon(QStringLiteral("save")));
    QVERIFY(!pdf::PDFThemeIcons::getPixmap(QStringLiteral("save"), QSizeF(24.0, 24.0), 2.0, black).isNull());
}

void WidgetStyleTest::initMain()
{
    if (!qEnvironmentVariableIsSet(CORE_FONTS_VARIABLE))
    {
        return;
    }

    int exitCode = 0;
    {
        int argc = 1;
        char name[] = "UnitTestsWidgetStyle";
        char* argv[] = { name, nullptr };
        QCoreApplication application(argc, argv);

        // The font database needs a QGuiApplication (Qt ends the process by qFatal
        // without it): the fonts are not loaded and they can be loaded later
        exitCode = pdf::PDFThemeFonts::loadFonts() ? 1 : 0;
    }
    std::exit(exitCode);
}

void WidgetStyleTest::reviewFontsWithoutGuiApplication()
{
    // F10: the fonts are not loaded into the font database without a QGuiApplication
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(CORE_FONTS_VARIABLE), QStringLiteral("1"));
    QProcess process;
    process.setProcessEnvironment(environment);
    process.start(QCoreApplication::applicationFilePath(), QStringList());
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
}

void WidgetStyleTest::reviewIconCacheKeys()
{
    // F12: the same size in pixels of another ratio of the device is another pixmap
    QPixmapCache::clear();
    const QColor color(Qt::red);
    const QPixmap first = pdf::PDFThemeIcons::getFillPixmap(QStringLiteral("lock"), QSizeF(16, 16), 2.0, color);
    const QPixmap second = pdf::PDFThemeIcons::getFillPixmap(QStringLiteral("lock"), QSizeF(32, 32), 1.0, color);
    QVERIFY(!first.isNull());
    QVERIFY(!second.isNull());
    QCOMPARE(first.size(), second.size());
    QCOMPARE(first.devicePixelRatio(), 2.0);
    QCOMPARE(second.devicePixelRatio(), 1.0);

    // Two close ratios collided after the rounding to hundredths
    const QPixmap normal = pdf::PDFThemeIcons::getPixmap(QStringLiteral("lock"), QSizeF(16, 16), 1.0, color);
    const QPixmap fractional = pdf::PDFThemeIcons::getPixmap(QStringLiteral("lock"), QSizeF(16.0 / 1.001, 16.0 / 1.001), 1.001, color);
    QCOMPARE(normal.size(), fractional.size());
    QCOMPARE(fractional.devicePixelRatio(), 1.001);

    // F07: an alias of the name of an icon ("./lock") is no icon, the caches do not grow
    QVERIFY(pdf::PDFThemeIcons::hasIcon(QStringLiteral("lock")));
    const qsizetype count = pdf::PDFThemeIcons::getCachedRendererCount();
    for (int i = 1; i <= 300; ++i)
    {
        const QString alias = QStringLiteral("./").repeated(i) + QStringLiteral("lock");
        QVERIFY(!pdf::PDFThemeIcons::hasIcon(alias));
    }
    QCOMPARE(pdf::PDFThemeIcons::getCachedRendererCount(), count);
}

void WidgetStyleTest::reviewSegmentRemoval()
{
    // F09: the indices are the positions of the segments, also after the application
    // has deleted a segment (the identifiers of the button group collided)
    pdf::PDFSegmentedControl control;
    control.addSegment(QStringLiteral("A"));
    control.addSegment(QStringLiteral("B"));
    control.addSegment(QStringLiteral("C"));
    const QList<QAbstractButton*> buttons = control.findChildren<QAbstractButton*>(Qt::FindDirectChildrenOnly);
    QCOMPARE(buttons.size(), 3);
    delete buttons.at(1);
    QCOMPARE(control.addSegment(QStringLiteral("D")), 2);

    QSignalSpy changed(&control, &pdf::PDFSegmentedControl::currentIndexChanged);
    control.setCurrentIndex(1);
    QVERIFY(buttons.at(2)->isChecked());
    QCOMPARE(control.getCurrentIndex(), 1);
    QCOMPARE(changed.last().at(0).toInt(), 1);
    control.setCurrentIndex(2);
    QCOMPARE(control.getCurrentIndex(), 2);
    QVERIFY(!buttons.at(2)->isChecked());
}

/// Gives the test the mouse handlers of the dial
class WidgetStyleTestDial : public pdf::PDFDial
{
public:
    using pdf::PDFDial::mousePressEvent;
    using pdf::PDFDial::mouseReleaseEvent;
};

void WidgetStyleTest::reviewDialDeletedBySignal()
{
    // F05: a slot of valueChanged deletes the dial during the press and the release
    for (const bool isPress : { true, false })
    {
        QPointer<WidgetStyleTestDial> dial = new WidgetStyleTestDial();
        dial->resize(64, 64);
        dial->setRange(0, 100);
        connect(dial, &QDial::valueChanged, this, [dial]() { delete dial.data(); });
        QMouseEvent event(isPress ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease, QPointF(32, 0), QPointF(32, 0),
                          Qt::LeftButton, isPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
        if (isPress)
        {
            dial->mousePressEvent(&event);
        }
        else
        {
            dial->mouseReleaseEvent(&event);
        }
        QVERIFY(dial.isNull());
    }
}

void WidgetStyleTest::reviewClassFontsRestored()
{
    // F13: unpolish gives the classes the fonts, which the application has set before
    // polish, not the font of the application. The style is unpolished first, so the
    // fonts of the application without the style are restored at the end.
    static constexpr const char* classNames[] = { "QPushButton", "QToolButton", "QMenu", "QMenuBar", "QHeaderView", "QStatusBar", "QTipLabel", "QMessageBox", "QTabBar" };
    m_style->unpolish(qApp);
    const QFont originalFont = QApplication::font();
    QList<QFont> originalClassFonts;
    for (const char* className : classNames)
    {
        originalClassFonts.push_back(QApplication::font(className));
    }

    QApplication::setFont(QFont(QStringLiteral("Arial"), 17));
    QApplication::setFont(QFont(QStringLiteral("Courier New"), 19), "QPushButton");
    QApplication::setFont(QFont(QStringLiteral("Arial"), 11), "QHeaderView");
    const QFont general = QApplication::font();
    const QFont button = QApplication::font("QPushButton");
    const QFont header = QApplication::font("QHeaderView");

    m_style->polish(qApp);
    QVERIFY(QApplication::font("QPushButton") != button);
    m_style->unpolish(qApp);
    QCOMPARE(QApplication::font(), general);
    QCOMPARE(QApplication::font("QPushButton"), button);
    QCOMPARE(QApplication::font("QHeaderView"), header);

    // The font of the application without a class removes the fonts of the classes
    QApplication::setFont(originalFont);
    for (qsizetype i = 0; i < originalClassFonts.size(); ++i)
    {
        QApplication::setFont(originalClassFonts[i], classNames[i]);
    }
    m_style->polish(qApp);
    QVERIFY(pdf::PDFWidgetStyle::isActive());
}

void WidgetStyleTest::searchFieldFocusIcon()
{
    // Search field (49:143): the magnifier is icon/muted, icon/accent in the state
    // Focus; the clear button of Filled=True stays icon/muted
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPushButton* other = new QPushButton(QStringLiteral("Other"), &window);
    pdf::PDFLineEdit* field = new pdf::PDFLineEdit(QStringLiteral("sign"), &window);
    field->setSearchField();
    field->setClearButtonEnabled(true);
    field->setFixedWidth(320);
    layout->addWidget(other);
    layout->addWidget(field);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto iconColors = [field]()
    {
        // The darkest pixel of each icon button (the strokes)
        std::map<bool, QColor> colors;
        for (QWidget* child : field->findChildren<QWidget*>(Qt::FindDirectChildrenOnly))
        {
            if (!child->inherits("QLineEditIconButton") || !child->isVisibleTo(field))
            {
                continue;
            }
            // The field with its buttons (a button alone has no background)
            const QImage image = field->grab().toImage();
            const QRect area = child->geometry();
            QColor darkest = Qt::white;
            for (int y = area.top(); y <= area.bottom(); ++y)
            {
                for (int x = area.left(); x <= area.right(); ++x)
                {
                    const QColor color = image.pixelColor(x, y);
                    if (color.alpha() == 255 && color.lightness() < darkest.lightness())
                    {
                        darkest = color;
                    }
                }
            }
            colors[child->geometry().center().x() < field->width() / 2] = darkest;
        }
        return colors;
    };

    other->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY(other->hasFocus());
    std::map<bool, QColor> colors = iconColors();
    QCOMPARE(colors.size(), size_t(2));
    QVERIFY2(isNear(colors[true], token(pdf::PDFThemeColors::IconMuted), 24), qPrintable(colors[true].name()));
    QVERIFY2(isNear(colors[false], token(pdf::PDFThemeColors::IconMuted), 24), qPrintable(colors[false].name()));

    field->setFocus(Qt::MouseFocusReason);
    QTRY_VERIFY(field->hasFocus());
    colors = iconColors();
    QVERIFY2(isNear(colors[true], token(pdf::PDFThemeColors::IconAccent), 24), qPrintable(colors[true].name()));
    QVERIFY2(isNear(colors[false], token(pdf::PDFThemeColors::IconMuted), 24), qPrintable(colors[false].name()));
}

QTEST_MAIN(WidgetStyleTest)

#include "tst_widgetstyletest.moc"
