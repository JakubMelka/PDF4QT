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

#include <QCheckBox>
#include <QFocusFrame>
#include <QMenu>
#include <QMetaEnum>
#include <QPainter>
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

using Color = pdf::PDFThemeColors::Color;

class WidgetStyleTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void themeTokens();
    void themeTokensChecksum();
    void themeManager();
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
    void galleryHasNoLeak();
    void leakPageShowsLeak();
    void iconStrokes();
    void toolBarPaddings();
    void headerViews();
    void treeView();
    void titleFonts();
    void messageBoxButtons();
    void toolTipOfQt();
    void plainTextLines();
    void dial();
    void tabBarScrolls();
    void progressBarThickness();
    void framePrimitive();
    void itemEditorFactory();
    void pointerTargets();
    void subMenuMargin();
    void popupPlacement();

private:
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
    QApplication::setLayoutDirection(Qt::LeftToRight);
    m_style->setAnimationTime(-1);
}

void WidgetStyleTest::themeTokens()
{
    QCOMPARE(static_cast<int>(pdf::PDFThemeColors::ColorCount), 133);

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

    QCOMPARE(text.size(), qsizetype(4410));
    QCOMPARE(hash, quint32(0x5dffe938u));
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
    QCOMPARE(pdf::PDFThemeIcons::getModeColor(QIcon::Disabled), pdf::PDFThemeColors::IconMuted);
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

    tab.state |= QStyle::State_Selected;
    QCOMPARE(m_style->sizeFromContents(QStyle::CT_TabBarTab, &tab, QSize(), nullptr), QSize(qMax(64, chipText + 28) + 6, 36));

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
    QVERIFY(isNear(pixel(image, 74, 18), token(pdf::PDFThemeColors::AccentBlueIcon)));
    image = draw(primary, enabled | QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(isNear(pixel(image, 74, 18), token(pdf::PDFThemeColors::TextAccentPressed)));
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
    QCOMPARE(passwordEdit.actions().size(), qsizetype(1));
    passwordEdit.actions().front()->toggle();
    QCOMPARE(passwordEdit.echoMode(), QLineEdit::Normal);
    QVERIFY(!passwordEdit.actions().front()->text().isEmpty());

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
    QCOMPARE(segmentedControl.sizeHint().height(), 36);

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
    QVERIFY2(longBox.height() < 420, qPrintable(QString::number(longBox.height())));
    QCOMPARE(countLeakPixels(longBox.grab().toImage()), 0);
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
    QVERIFY(!QItemEditorFactory::defaultFactory()->createEditor(QMetaType::QBitArray, &parent));
    QVERIFY(QItemEditorFactory::defaultFactory()->valuePropertyName(QMetaType::QBitArray).isEmpty());
    QVERIFY(!factory.createEditor(QMetaType::QBitArray, &parent));
    QItemEditorFactory::setDefaultFactory(nullptr);
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

QTEST_MAIN(WidgetStyleTest)

#include "tst_widgetstyletest.moc"
