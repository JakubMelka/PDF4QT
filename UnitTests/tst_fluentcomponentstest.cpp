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
#include "pdfcomponentcards.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentrows.h"
#include "pdfstylepainter.h"
#include "pdfthemecolors.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include "gallerypages.h"

#include <QtTest>

#include <QAccessible>
#include <QButtonGroup>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFocusFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMetaEnum>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSet>
#include <QTranslator>
#include <QVBoxLayout>

#include <functional>
#include <limits>
#include <type_traits>

using Color = pdf::PDFThemeColors::Color;
using Painter = pdf::PDFComponentPainter;

// A property row cannot be constructed with a flag before its parent: a pointer
// to the parent in the place of the flag was taken as true (review C5)
static_assert(!std::is_constructible_v<pdf::PDFPropertyRow, QString, QString, bool, QWidget*>);
static_assert(!std::is_constructible_v<pdf::PDFPropertyRow, QString, QString, bool>);
static_assert(std::is_constructible_v<pdf::PDFPropertyRow, QString, QString, QWidget*>);
static_assert(std::is_constructible_v<pdf::PDFPropertyRow, QString, QString, pdf::PDFPropertyRow::Editability, QWidget*>);

/// Translator of the test, which translates a few texts of the rows
class TestTranslator : public QTranslator
{
public:
    virtual QString translate(const char* context, const char* sourceText, const char* disambiguation, int n) const override
    {
        Q_UNUSED(context);
        Q_UNUSED(disambiguation);
        Q_UNUSED(n);

        const QByteArrayView source(sourceText);
        if (source == "No longer exists")
        {
            return QStringLiteral("Gone");
        }
        if (source == "Current version")
        {
            return QStringLiteral("Installed");
        }
        return QString();
    }

    virtual bool isEmpty() const override { return false; }
};

/// Row of the test: it draws its background by the state and has a fixed size
class TestRow : public pdf::PDFRowButton
{
public:
    explicit TestRow(QWidget* parent = nullptr) :
        pdf::PDFRowButton(parent)
    {

    }

    void setSubtitle(const QString& subtitle)
    {
        replaceAccessibleDescription(m_subtitle, subtitle);
        m_subtitle = subtitle;
    }

    mutable int paintCount = 0;
    mutable Painter::State lastState;

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const Painter::State& state) const override
    {
        ++paintCount;
        lastState = state;

        if (state.isPressed())
        {
            pdf::PDFStylePainter::fillRect(painter, QRectF(rect), Color::BgPressed);
        }
        else if (state.isHovered())
        {
            pdf::PDFStylePainter::fillRect(painter, QRectF(rect), Color::BgHover);
        }

        if (state.hasFocusRing())
        {
            Painter::paintFocusRingInside(painter, QRectF(rect), pdf::PDFStylePainter::Radii(8.0));
        }
    }

    virtual QSize getContentSizeHint() const override { return QSize(300, 48); }

private:
    QString m_subtitle;
};

/// Row of the test, whose size includes its nested widgets (as the rows of the steps 2 and 3)
class TrailingRow : public TestRow
{
public:
    explicit TrailingRow(QWidget* parent = nullptr) :
        TestRow(parent)
    {

    }

protected:
    virtual QSize getContentSizeHint() const override { return QSize(200 + getTrailingRect().width(), 48); }
};

/// Row of the test, which is not interactive (a text)
class LabelRow : public TestRow
{
public:
    explicit LabelRow(QWidget* parent = nullptr) :
        TestRow(parent)
    {

    }

    virtual bool isInteractive() const override { return false; }
};

/// Free-standing button of the test with the ring outside, which does not set its radius
class BareOutsideButton : public pdf::PDFComponentButton
{
public:
    explicit BareOutsideButton(QWidget* parent = nullptr) :
        pdf::PDFComponentButton(FocusRing::Outside, parent)
    {

    }

protected:
    virtual void paintContent(QPainter*, const QRect&, const Painter::State&) const override { }
    virtual QSize getContentSizeHint() const override { return QSize(40, 24); }
};

/// Card of the test
class TestCard : public pdf::PDFCardButton
{
public:
    explicit TestCard(QWidget* parent = nullptr) :
        pdf::PDFCardButton(parent)
    {

    }

protected:
    virtual void paintContent(QPainter* painter, const QRect& rect, const Painter::State& state) const override
    {
        pdf::PDFStylePainter::fill(painter, QRectF(rect), pdf::PDFStylePainter::Radii(12.0), state.isOn() ? Color::BgAccentSubtle : Color::BgSurface);
    }

    virtual QSize getContentSizeHint() const override { return QSize(154, 48); }
};

class FluentComponentsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void sizes();
    void sizeStability();
    void pixelProbes_data();
    void pixelProbes();
    void widgetPixels();
    void themeChange();
    void rightToLeft();
    void starButton();
    void colorSwatchGroup();
    void tag();
    void baseKeyboard();
    void baseSelection();
    void baseFocusRing();
    void baseAccessibility();
    void baseTrailingWidgets();
    void defensive();
    void mnemonics();
    void selectionByMouseAndKeys();
    void independentGroups();
    void selectionReentrancy();
    void selectionToNone();
    void trailingLifetime();
    void accessibilityEvents();
    void focusFrame();
    void apiDetails();
    void navItem();
    void linkUnderline();
    void swatchGeometry();
    void thumbnailShadow();
    void galleryPages();
    void rowSizes();
    void rowSizeStability();
    void rowPixels_data();
    void rowPixels();
    void rowWidgetsMatchPainter();
    void fileNameElision();
    void rowNestedWidgets();
    void rowKeyboard();
    void rowAccessibility();
    void propertyRowEditing();
    void propertyRowCopy();
    void propertyRowLifetime();
    void rowThemeAndDirection();
    void rowDefensive();
    void propertyRowKeyboard();
    void propertyRowFocusDeletion();
    void moreButtonMenu();
    void rowNestedDeletion();
    void rowRetranslation();
    void rowDetails();

private:
    /// Returns the pixel of the area, which differs most from the background
    static QColor strongestInk(const QImage& image, const QRect& area, const QColor& background);

    /// Returns the distance of two colors (sum of the differences of the channels)
    static int colorDistance(const QColor& first, const QColor& second);

    /// Renders the drawing over the background bg/surface
    static QImage render(const std::function<void(QPainter&)>& paint, QSize size, qreal scale = 1.0);
    static QColor pixel(const QImage& image, qreal x, qreal y);
    static bool isNear(const QColor& first, const QColor& second, int tolerance = 3);
    static QColor blend(const QColor& top, const QColor& bottom);
    static int countLeakPixels(const QImage& image);
    static QColor token(Color color) { return pdf::PDFStylePainter::color(color); }
    static Painter::State state(QStyle::State flags, Qt::LayoutDirection direction = Qt::LeftToRight) { return Painter::State(flags | QStyle::State_Enabled, direction); }
    static int countPixels(const QImage& image, const QRect& area, const QColor& color, int tolerance);

    /// Returns a description of the largest difference of two images of the same size (empty, if they are equal)
    static QString describeDifference(const QImage& first, const QImage& second, int tolerance);

    /// Renders the widget with its children over bg/surface at the scale
    static QImage renderWidget(QWidget* widget, qreal scale);

    /// Collects the events of accessibility
    static void collectAccessibleEvent(QAccessibleEvent* event);
    static int countAccessibleEvents(const QObject* object, QAccessible::Event type);
    static QList<QPair<const QObject*, QAccessible::Event>> s_accessibleEvents;

    pdf::PDFWidgetStyle* m_style = nullptr;
};

QList<QPair<const QObject*, QAccessible::Event>> FluentComponentsTest::s_accessibleEvents;

QString FluentComponentsTest::describeDifference(const QImage& first, const QImage& second, int tolerance)
{
    if (first.size() != second.size())
    {
        return QStringLiteral("size %1x%2 / %3x%4").arg(first.width()).arg(first.height()).arg(second.width()).arg(second.height());
    }

    int count = 0;
    int largest = 0;
    QString description;
    for (int y = 0; y < first.height(); ++y)
    {
        for (int x = 0; x < first.width(); ++x)
        {
            if (!isNear(first.pixelColor(x, y), second.pixelColor(x, y), tolerance))
            {
                if (count == 0)
                {
                    description = QStringLiteral("(%1, %2) %3 / %4").arg(x).arg(y).arg(first.pixelColor(x, y).name(QColor::HexArgb), second.pixelColor(x, y).name(QColor::HexArgb));
                }
                largest = qMax(largest, colorDistance(first.pixelColor(x, y), second.pixelColor(x, y)));
                ++count;
            }
        }
    }

    return count > 0 ? QStringLiteral("%1 pixels, largest distance %2, first %3").arg(count).arg(largest).arg(description) : QString();
}

QImage FluentComponentsTest::renderWidget(QWidget* widget, qreal scale)
{
    QImage image(widget->size() * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(token(Color::BgSurface));

    QPainter painter(&image);
    widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
    painter.end();
    return image;
}

void FluentComponentsTest::collectAccessibleEvent(QAccessibleEvent* event)
{
    s_accessibleEvents.append(qMakePair(static_cast<const QObject*>(event->object()), event->type()));
}

int FluentComponentsTest::countAccessibleEvents(const QObject* object, QAccessible::Event type)
{
    int count = 0;
    for (const auto& event : std::as_const(s_accessibleEvents))
    {
        if (event.first == object && event.second == type)
        {
            ++count;
        }
    }
    return count;
}

QImage FluentComponentsTest::render(const std::function<void(QPainter&)>& paint, QSize size, qreal scale)
{
    QImage image(size * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(token(Color::BgSurface));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paint(painter);
    painter.end();
    return image;
}

QColor FluentComponentsTest::pixel(const QImage& image, qreal x, qreal y)
{
    // Coordinates are logical, the center of the logical pixel is read
    const qreal scale = image.devicePixelRatio();
    return image.pixelColor(qFloor((x + 0.5) * scale), qFloor((y + 0.5) * scale));
}

bool FluentComponentsTest::isNear(const QColor& first, const QColor& second, int tolerance)
{
    return qAbs(first.red() - second.red()) <= tolerance && qAbs(first.green() - second.green()) <= tolerance &&
           qAbs(first.blue() - second.blue()) <= tolerance && qAbs(first.alpha() - second.alpha()) <= tolerance;
}

QColor FluentComponentsTest::blend(const QColor& top, const QColor& bottom)
{
    const qreal alpha = top.alphaF();
    return QColor::fromRgbF(top.redF() * alpha + bottom.redF() * (1.0 - alpha),
                            top.greenF() * alpha + bottom.greenF() * (1.0 - alpha),
                            top.blueF() * alpha + bottom.blueF() * (1.0 - alpha));
}

int FluentComponentsTest::countLeakPixels(const QImage& image)
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

int FluentComponentsTest::countPixels(const QImage& image, const QRect& area, const QColor& color, int tolerance)
{
    // The area is logical
    int count = 0;
    for (int y = area.top(); y <= area.bottom(); ++y)
    {
        for (int x = area.left(); x <= area.right(); ++x)
        {
            if (isNear(pixel(image, x, y), color, tolerance))
            {
                ++count;
            }
        }
    }
    return count;
}

void FluentComponentsTest::initTestCase()
{
    m_style = new pdf::PDFWidgetStyle();
    QApplication::setStyle(m_style);
    QVERIFY(pdf::PDFWidgetStyle::isActive());
}

void FluentComponentsTest::cleanup()
{
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QApplication::setLayoutDirection(Qt::LeftToRight);
}

void FluentComponentsTest::sizes()
{
    // Tool icon: diameter 28 / 32 / 44, icon of half of it
    QCOMPARE(Painter::getBadgeDiameter(Painter::BadgeSize::Small), 28);
    QCOMPARE(Painter::getBadgeDiameter(Painter::BadgeSize::Medium), 32);
    QCOMPARE(Painter::getBadgeDiameter(Painter::BadgeSize::Large), 44);
    QCOMPARE(Painter::getBadgeIconExtent(Painter::BadgeSize::Small), 14);
    QCOMPARE(Painter::getBadgeIconExtent(Painter::BadgeSize::Medium), 16);
    QCOMPARE(Painter::getBadgeIconExtent(Painter::BadgeSize::Large), 22);
    pdf::PDFToolBadge badge(QStringLiteral("grid"));
    QCOMPARE(badge.sizeHint(), QSize(32, 32));
    badge.setBadgeSize(Painter::BadgeSize::Large);
    QCOMPARE(badge.sizeHint(), QSize(44, 44));
    badge.setBadgeSize(Painter::BadgeSize::Small);
    QCOMPARE(badge.sizeHint(), QSize(28, 28));

    // Tag: 21 px high for any character, 10 px from the edges (border 1 + padding 9), "OCR" is 44 x 21
    const QSize ocrTag = Painter::getTagSize(Painter::Tag{ QStringLiteral("OCR") });
    QCOMPARE(ocrTag.height(), 21);
    QVERIFY2(qAbs(ocrTag.width() - 44) <= 1, qPrintable(QString::number(ocrTag.width())));
    QCOMPARE(Painter::getTagSize(Painter::Tag{ QStringLiteral("A") }).height(), 21);
    QCOMPARE(Painter::getTagSize(Painter::Tag{ QStringLiteral("✂") }).height(), 21);
    QCOMPARE(Painter::getTagSize(Painter::Tag{ QStringLiteral("Advanced") }).width(), qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("Advanced"), Painter::getTagFont())) + 20);
    QCOMPARE(pdf::PDFTag(QStringLiteral("OCR")).sizeHint(), ocrTag);

    // Text link: 2 px at the sides, 16 px high, "View all" is 53 x 16
    const QSize linkSize = Painter::getTextLinkSize(Painter::TextLink{ QStringLiteral("View all") });
    QCOMPARE(linkSize.height(), 16);
    QVERIFY2(qAbs(linkSize.width() - 53) <= 1, qPrintable(QString::number(linkSize.width())));
    pdf::PDFTextLink link(QStringLiteral("View all"));
    QCOMPARE(link.sizeHint(), linkSize);
    QCOMPARE(link.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 4.0);
    QCOMPARE(link.focusPolicy(), Qt::TabFocus);

    // Star button: 28 and 24, radius 6
    pdf::PDFStarButton star;
    QCOMPARE(star.sizeHint(), QSize(28, 28));
    star.setStarSize(pdf::PDFStarButton::StarSize::Small);
    QCOMPARE(star.sizeHint(), QSize(24, 24));
    QCOMPARE(star.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 6.0);

    // Color swatch: 28 x 28, round
    pdf::PDFColorSwatch swatch(Qt::yellow, QStringLiteral("Yellow"));
    QCOMPARE(swatch.sizeHint(), QSize(28, 28));
    QCOMPARE(swatch.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 14.0);

    // Page thumbnail: 32 x 42, 56 x 74, 80 x 106 and the margins of the shadow
    QCOMPARE(Painter::getPageThumbnailSize(Painter::ThumbnailSize::Small), QSize(32, 42));
    QCOMPARE(Painter::getPageThumbnailSize(Painter::ThumbnailSize::Medium), QSize(56, 74));
    QCOMPARE(Painter::getPageThumbnailSize(Painter::ThumbnailSize::Large), QSize(80, 106));
    QCOMPARE(Painter::getPageThumbnailShadowMargins(), QMargins(2, 1, 2, 3));
    // The widget has the size of the frame, the shadow takes no space in a layout (F1)
    pdf::PDFPageThumbnail thumbnail(Painter::ThumbnailSize::Medium);
    QCOMPARE(thumbnail.sizeHint(), QSize(56, 74));
    QCOMPARE(thumbnail.minimumSizeHint(), QSize(56, 74));
    thumbnail.setThumbnailSize(Painter::ThumbnailSize::Large);
    QCOMPARE(thumbnail.sizeHint(), QSize(80, 106));
    thumbnail.setThumbnailSize(Painter::ThumbnailSize::Small);
    QCOMPARE(thumbnail.sizeHint(), QSize(32, 42));

    // Bases: the size of the content
    QCOMPARE(TestRow().sizeHint(), QSize(300, 48));
    QCOMPARE(TestCard().sizeHint(), QSize(154, 48));
    QCOMPARE(TestCard().property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 12.0);
    QCOMPARE(Painter::getCardRadius(), 12.0);
    QCOMPARE(TestCard().getTrailingMargins(), Painter::getCardTrailingMargins());
    QCOMPARE(TestCard().getTrailingSpacing(), 8);
    QCOMPARE(TestRow().getTrailingMargins(), QMargins(0, 0, 12, 0));
    QCOMPARE(TestRow().getTrailingSpacing(), Painter::getRowTrailingSpacing());
    QVERIFY(!TestRow().property(pdf::PDFWidgetStyleProperties::RADIUS).isValid());

    // A button with the ring outside has the ring also without its own radius (C8)
    BareOutsideButton bare;
    QVERIFY(bare.property(pdf::PDFWidgetStyleProperties::RADIUS).isValid());
    QCOMPARE(bare.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 0.0);
}

void FluentComponentsTest::sizeStability()
{
    // A widget has the size of the variant Default in each state
    pdf::PDFStarButton star;
    pdf::PDFColorSwatch swatch(Qt::red);
    pdf::PDFTag tag(QStringLiteral("OCR"));
    tag.setClickable(true);
    pdf::PDFTextLink link(QStringLiteral("View all"));
    TestRow row;

    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{ &star, &swatch, &tag, &link, &row })
    {
        const QSize size = button->sizeHint();
        button->setAttribute(Qt::WA_UnderMouse, true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(false);
        button->setCheckable(true);
        button->setChecked(true);
        QCOMPARE(button->sizeHint(), size);
        button->setEnabled(false);
        QCOMPARE(button->sizeHint(), size);
        button->setLayoutDirection(Qt::RightToLeft);
        QCOMPARE(button->sizeHint(), size);
    }

    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(star.sizeHint(), QSize(28, 28));
    QCOMPARE(tag.sizeHint(), Painter::getTagSize(Painter::Tag{ QStringLiteral("OCR") }));
}

void FluentComponentsTest::pixelProbes_data()
{
    QTest::addColumn<QString>("theme");
    QTest::addColumn<qreal>("scale");

    for (const char* theme : { "light", "dark" })
    {
        for (const qreal scale : { 1.0, 1.5, 2.0 })
        {
            QTest::newRow(qPrintable(QStringLiteral("%1@%2").arg(QString::fromLatin1(theme)).arg(scale))) << QString::fromLatin1(theme) << scale;
        }
    }
}

void FluentComponentsTest::pixelProbes()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QColor surface = token(Color::BgSurface);
    const QStyle::State none = QStyle::State_None;

    // Tool icon: the circle of the hue, outside of the icon
    for (const pdf::PDFAccentHue hue : { pdf::PDFAccentHue::Blue, pdf::PDFAccentHue::Amber, pdf::PDFAccentHue::Neutral })
    {
        const Painter::ToolBadge badge{ QStringLiteral("grid"), hue, Painter::BadgeSize::Medium };
        const QImage image = render([&](QPainter& painter) { Painter::paintToolBadge(&painter, QRectF(0, 0, 32, 32), badge, state(none)); }, QSize(32, 32), scale);
        QVERIFY(isNear(pixel(image, 4, 16), token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Background))));
        QVERIFY(isNear(pixel(image, 0, 0), surface));
        QVERIFY(countPixels(image, QRect(8, 8, 16, 16), token(pdf::PDFAccentHueColors::getColor(hue, pdf::PDFAccentHueColors::Role::Icon)), 40) > 0);
    }

    // Tag: fill bg/subtle, hover bg/muted, pressed bg/track, border, focus ring inside
    const Painter::Tag tag{ QStringLiteral("OCR") };
    const QSize tagSize = Painter::getTagSize(tag);
    const qreal tagMiddle = tagSize.width() / 2.0;
    auto renderTag = [&](QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintTag(&painter, QRectF(QPointF(0, 0), tagSize), tag, state(flags)); }, tagSize, scale);
    };
    QImage image = renderTag(none);
    QVERIFY(isNear(pixel(image, 5, 10), token(Color::BgSubtle)));
    QVERIFY(isNear(pixel(image, tagMiddle, 0), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, tagMiddle, 1), token(Color::BgSubtle)));
    image = renderTag(QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 5, 10), token(Color::BgMuted)));
    image = renderTag(QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(isNear(pixel(image, 5, 10), token(Color::BgTrack)));
    image = renderTag(QStyle::State_HasFocus);
    QVERIFY(isNear(pixel(image, tagMiddle, 0), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, tagMiddle, 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, tagMiddle, 2), token(Color::BgSubtle)));

    // Text link: text/accent, pressed text/accent-pressed, focus ring outside
    const Painter::TextLink link{ QStringLiteral("View all") };
    const QSize linkSize = Painter::getTextLinkSize(link);
    auto renderLink = [&](QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(4, 4), linkSize), link, state(flags)); }, linkSize + QSize(8, 8), scale);
    };
    image = renderLink(none);
    QVERIFY(countPixels(image, QRect(4, 4, linkSize.width(), linkSize.height()), token(Color::TextAccent), 8) > 5);
    image = renderLink(QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(countPixels(image, QRect(4, 4, linkSize.width(), linkSize.height()), token(Color::TextAccentPressed), 8) > 5);
    image = renderLink(QStyle::State_HasFocus);
    QVERIFY(isNear(pixel(image, 4 + linkSize.width() / 2.0, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 4 + linkSize.width() / 2.0, 3), token(Color::BorderFocus)));
    image = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(4, 4), linkSize), link, Painter::State(QStyle::State_None)); }, linkSize + QSize(8, 8), scale);
    QCOMPARE(countPixels(image, QRect(4, 4, linkSize.width(), linkSize.height()), token(Color::TextAccent), 8), 0);
    QVERIFY(countPixels(image, QRect(4, 4, linkSize.width(), linkSize.height()), token(Color::TextDisabled), 8) > 5);

    // Star button: hover and press backgrounds, the pinned star is filled amber
    auto renderStar = [&](QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintStar(&painter, QRectF(0, 0, 28, 28), Painter::StarSize::Medium, Painter::State(flags)); }, QSize(28, 28), scale);
    };
    const QStyle::State enabled = QStyle::State_Enabled;
    image = renderStar(enabled);
    QVERIFY(isNear(pixel(image, 2, 14), surface));
    QVERIFY(isNear(pixel(image, 13, 14), surface));
    image = renderStar(enabled | QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 2, 14), blend(token(Color::BgHover), surface)));
    image = renderStar(enabled | QStyle::State_MouseOver | QStyle::State_Sunken);
    QVERIFY(isNear(pixel(image, 2, 14), blend(token(Color::BgPressed), surface)));
    image = renderStar(enabled | QStyle::State_On);
    QVERIFY(isNear(pixel(image, 13, 14), token(Color::IconSun), 12));
    image = renderStar(QStyle::State_On);
    QVERIFY(isNear(pixel(image, 13, 14), token(Color::IconDisabled), 12));
    image = renderStar(QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 2, 14), surface));

    // Color swatch: the color, the selection ring, hover, disabled selection, no color
    const Painter::ColorSwatch swatch{ token(Color::AnnotationYellow), Painter::SwatchKind::Color };
    auto renderSwatch = [&](const Painter::ColorSwatch& content, QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintColorSwatch(&painter, QRectF(4, 4, 28, 28), content, Painter::State(flags)); }, QSize(36, 36), scale);
    };
    image = renderSwatch(swatch, enabled);
    QVERIFY(isNear(pixel(image, 18, 18), token(Color::AnnotationYellow)));
    QVERIFY(isNear(pixel(image, 18, 5), surface));
    image = renderSwatch(swatch, enabled | QStyle::State_On);
    QVERIFY(isNear(pixel(image, 18, 5), token(Color::BgAccent), 12));
    QVERIFY(isNear(pixel(image, 18, 18), token(Color::AnnotationYellow)));
    image = renderSwatch(swatch, enabled | QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 18, 5), blend(token(Color::BgHover), surface), 4));
    image = renderSwatch(swatch, QStyle::State_On | QStyle::State_MouseOver);
    QVERIFY(isNear(pixel(image, 18, 5), token(Color::BorderStrong), 12));
    QVERIFY(isNear(pixel(image, 18, 18), token(Color::AnnotationYellow)));
    image = renderSwatch(swatch, enabled | QStyle::State_On | QStyle::State_HasFocus);
    QVERIFY(isNear(pixel(image, 18, 5), token(Color::BgAccent), 12));
    QVERIFY(isNear(pixel(image, 18, 3), token(Color::BorderFocus), 12));
    const Painter::ColorSwatch noColor{ QColor(), Painter::SwatchKind::None };
    image = renderSwatch(noColor, enabled);
    QVERIFY(isNear(pixel(image, 16, 18), token(Color::IconDanger), 40));
    QVERIFY(isNear(pixel(image, 13, 13), surface));
    image = renderSwatch(noColor, none);
    QVERIFY(isNear(pixel(image, 16, 18), token(Color::IconDisabled), 40));

    // Page thumbnail: border, title line, lines, the paper
    const QRectF frame(4, 4, 32, 42);
    image = render([&](QPainter& painter) { Painter::paintPageThumbnail(&painter, frame, Painter::PageThumbnail(), state(none)); }, QSize(40, 50), scale);
    QVERIFY(isNear(pixel(image, 4 + 16, 4), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 4 + 16, 4 + 6), token(Color::TextMuted)));
    QVERIFY(isNear(pixel(image, 4 + 16, 4 + 10), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 4 + 16, 4 + 30), surface));
    QVERIFY(isNear(pixel(image, 4 + 25, 4 + 16), surface));     // the last line is 14 px long
    QCOMPARE(countLeakPixels(image), 0);

    QPixmap picture(QSize(64, 84));
    picture.fill(token(Color::SampleBlue));
    Painter::PageThumbnail withPicture;
    withPicture.size = Painter::ThumbnailSize::Medium;
    withPicture.pixmap = picture;
    image = render([&](QPainter& painter) { Painter::paintPageThumbnail(&painter, QRectF(4, 4, 56, 74), withPicture, state(none)); }, QSize(64, 82), scale);
    QVERIFY(isNear(pixel(image, 4 + 28, 4 + 37), token(Color::SampleBlue)));
    QVERIFY(isNear(pixel(image, 4 + 28, 4), token(Color::BorderStrong), 6));
}

void FluentComponentsTest::widgetPixels()
{
    const QColor surface = token(Color::BgSurface);

    // The widgets draw the same as the painter
    pdf::PDFStarButton star;
    star.resize(star.sizeHint());
    QImage image = star.grab().toImage();
    const QColor base = image.pixelColor(2, 14);
    star.setAttribute(Qt::WA_UnderMouse, true);
    image = star.grab().toImage();
    QVERIFY(isNear(image.pixelColor(2, 14), blend(token(Color::BgHover), base), 4));
    star.setEnabled(false);
    image = star.grab().toImage();
    QCOMPARE(image.pixelColor(2, 14), base);

    // A disabled swatch keeps its color and has no hover
    pdf::PDFColorSwatch swatch(token(Color::AnnotationRed), QStringLiteral("Red"));
    swatch.resize(swatch.sizeHint());
    swatch.setAttribute(Qt::WA_UnderMouse, true);
    image = swatch.grab().toImage();
    QVERIFY(isNear(image.pixelColor(14, 1), blend(token(Color::BgHover), base), 4));
    swatch.setEnabled(false);
    image = swatch.grab().toImage();
    QCOMPARE(image.pixelColor(14, 1), base);
    QVERIFY(isNear(image.pixelColor(14, 14), token(Color::AnnotationRed)));

    // A tag, which is only a label, does not react to the pointer
    pdf::PDFTag tag(QStringLiteral("OCR"));
    tag.resize(tag.sizeHint());
    tag.setAttribute(Qt::WA_UnderMouse, true);
    image = tag.grab().toImage();
    QVERIFY(isNear(image.pixelColor(5, 10), token(Color::BgSubtle)));
    tag.setClickable(true);
    image = tag.grab().toImage();
    QVERIFY(isNear(image.pixelColor(5, 10), token(Color::BgMuted)));

    // Badge and thumbnail
    pdf::PDFToolBadge badge(QStringLiteral("lock"), pdf::PDFAccentHue::Red);
    badge.resize(badge.sizeHint());
    image = badge.grab().toImage();
    QVERIFY(isNear(image.pixelColor(4, 16), token(Color::AccentRedBg)));
    pdf::PDFPageThumbnail thumbnail;
    thumbnail.resize(thumbnail.sizeHint());
    image = thumbnail.grab().toImage();
    QVERIFY(isNear(image.pixelColor(16, 0), token(Color::BorderStrong)));
    QVERIFY(isNear(image.pixelColor(16, 30), surface));

    for (QWidget* widget : std::initializer_list<QWidget*>{ &star, &swatch, &tag, &badge, &thumbnail })
    {
        QCOMPARE(countLeakPixels(widget->grab().toImage()), 0);
    }

    // The widgets at the scales 1.5 and 2 draw the same as the painter (C18)
    QWidget thumbnailHolder;
    thumbnailHolder.resize(40, 52);
    pdf::PDFPageThumbnail* childThumbnail = new pdf::PDFPageThumbnail(&thumbnailHolder);
    childThumbnail->setGeometry(QRect(QPoint(4, 4), childThumbnail->sizeHint()));
    swatch.setEnabled(true);
    swatch.setSelected(true);
    swatch.setAttribute(Qt::WA_UnderMouse, false);
    star.setEnabled(true);
    for (const qreal scale : { 1.0, 1.5, 2.0 })
    {
        const QImage starImage = renderWidget(&star, scale);
        const QImage starPainted = render([&](QPainter& painter) { Painter::paintStar(&painter, QRectF(0, 0, 28, 28), Painter::StarSize::Medium, star.getComponentState()); }, QSize(28, 28), scale);
        QVERIFY(isNear(pixel(starImage, 2, 14), blend(token(Color::BgHover), surface), 4));
        if (qFuzzyCompare(scale, 1.0))
        {
            // At other scales the widget, which is not on a screen of the scale, draws its icon from a pixmap of its own scale
            const QString starDifference = describeDifference(starImage, starPainted, 3);
            QVERIFY2(starDifference.isEmpty(), qPrintable(starDifference));
        }

        const QImage swatchImage = renderWidget(&swatch, scale);
        QVERIFY(isNear(pixel(swatchImage, 14, 1), token(Color::BgAccent), 12));
        QVERIFY(isNear(pixel(swatchImage, 14, 14), token(Color::AnnotationRed)));

        // The thumbnail is a child widget (its shadow lies in the parent)
        const QImage thumbnailImage = renderWidget(&thumbnailHolder, scale);
        QVERIFY(isNear(pixel(thumbnailImage, 4 + 16, 4), token(Color::BorderStrong), 6));
        QVERIFY(isNear(pixel(thumbnailImage, 4 + 16, 4 + 6), token(Color::TextMuted), 6));
        QCOMPARE(countLeakPixels(thumbnailImage), 0);
    }
}

void FluentComponentsTest::themeChange()
{
    pdf::PDFToolBadge badge(QStringLiteral("grid"), pdf::PDFAccentHue::Teal);
    badge.resize(badge.sizeHint());
    pdf::PDFTag tag(QStringLiteral("OCR"));
    tag.resize(tag.sizeHint());
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    TestRow* row = new TestRow(&window);
    layout->addWidget(row);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    const QColor lightBadge = badge.grab().toImage().pixelColor(4, 16);
    const QColor lightTag = tag.grab().toImage().pixelColor(5, 10);
    QVERIFY(isNear(lightBadge, token(Color::AccentTealBg)));

    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    const QColor darkBadge = badge.grab().toImage().pixelColor(4, 16);
    const QColor darkTag = tag.grab().toImage().pixelColor(5, 10);
    QVERIFY(isNear(darkBadge, token(Color::AccentTealBg)));
    QVERIFY(isNear(darkTag, token(Color::BgSubtle)));
    QVERIFY(lightBadge != darkBadge);
    QVERIFY(lightTag != darkTag);

    // The row repaints itself with the colors of the new theme and asks its layout for a new size
    row->paintCount = 0;
    int layoutRequests = 0;
    struct Filter : public QObject
    {
        int* count = nullptr;
        virtual bool eventFilter(QObject*, QEvent* event) override
        {
            if (event->type() == QEvent::LayoutRequest)
            {
                ++*count;
            }
            return false;
        }
    } filter;
    filter.count = &layoutRequests;
    window.installEventFilter(&filter);
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);
    QTRY_VERIFY(row->paintCount > 0);
    QTRY_VERIFY(layoutRequests > 0);
    window.removeEventFilter(&filter);
}

void FluentComponentsTest::rightToLeft()
{
    // The label of a link starts at the trailing edge in a wider rectangle
    const Painter::TextLink link{ QStringLiteral("View all") };
    const QSize linkSize = Painter::getTextLinkSize(link);
    const QRect area(0, 0, linkSize.width() + 80, linkSize.height());
    const QColor accent = token(Color::TextAccent);

    QImage image = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(area), link, state(QStyle::State_None)); }, area.size());
    QVERIFY(countPixels(image, QRect(0, 0, 40, area.height()), accent, 8) > 0);
    QCOMPARE(countPixels(image, QRect(area.width() - 40, 0, 40, area.height()), accent, 8), 0);
    image = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(area), link, state(QStyle::State_None, Qt::RightToLeft)); }, area.size());
    QCOMPARE(countPixels(image, QRect(0, 0, 40, area.height()), accent, 8), 0);
    QVERIFY(countPixels(image, QRect(area.width() - 40, 0, 40, area.height()), accent, 8) > 0);

    // Mirroring of the logical rectangles
    const Painter::State rtl(QStyle::State_Enabled, Qt::RightToLeft);
    QCOMPARE(rtl.visualRect(QRectF(10, 0, 100, 20), QRectF(10, 0, 30, 20)), QRectF(80, 0, 30, 20));
    QCOMPARE(Painter::State().visualRect(QRectF(10, 0, 100, 20), QRectF(10, 0, 30, 20)), QRectF(10, 0, 30, 20));
    QCOMPARE(rtl.leadingAlignment(), Qt::AlignRight);
    QCOMPARE(Painter::State(QStyle::State_Enabled, Qt::LayoutDirectionAuto).isRightToLeft(), false);

    // The state of a widget carries its direction
    pdf::PDFTextLink linkWidget(QStringLiteral("View all"));
    linkWidget.setLayoutDirection(Qt::RightToLeft);
    QVERIFY(linkWidget.getComponentState().isRightToLeft());

    // Trailing widgets of a row are mirrored
    TestRow row;
    row.resize(300, 48);
    pdf::PDFStarButton* star = new pdf::PDFStarButton(pdf::PDFStarButton::StarSize::Small);
    row.addTrailingWidget(star);
    QCOMPARE(star->geometry(), QRect(300 - 12 - 24, 12, 24, 24));
    row.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(star->geometry(), QRect(12, 12, 24, 24));
}

void FluentComponentsTest::starButton()
{
    pdf::PDFStarButton star;
    QSignalSpy spy(&star, &pdf::PDFStarButton::pinnedChanged);
    QVERIFY(!star.isPinned());
    QCOMPARE(star.toolTip(), QStringLiteral("Pin file"));
    QCOMPARE(star.getSelectionBehavior(), pdf::PDFComponentButton::SelectionBehavior::Toggle);

    // The signal is emitted only on a real change, after the change of the state
    bool pinnedInSlot = false;
    connect(&star, &pdf::PDFStarButton::pinnedChanged, &star, [&](bool) { pinnedInSlot = star.isPinned(); });
    star.setPinned(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.back().front().toBool(), true);
    QVERIFY(pinnedInSlot);
    star.setPinned(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(star.toolTip(), QStringLiteral("Unpin"));
    QVERIFY(star.getComponentState().isOn());

    // A click toggles it
    star.click();
    QVERIFY(!star.isPinned());
    QCOMPARE(spy.count(), 2);
    QCOMPARE(star.toolTip(), QStringLiteral("Pin file"));

    // A tooltip of the application is kept
    star.setToolTip(QStringLiteral("Pin the tool"));
    star.setPinned(true);
    QCOMPARE(star.toolTip(), QStringLiteral("Pin the tool"));

    // Accessibility: a checkable button named by its state
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&star);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Unpin"));
    QVERIFY(accessible->state().checkable);
    QVERIFY(accessible->state().checked);

    // Invalid size
    star.setStarSize(static_cast<pdf::PDFStarButton::StarSize>(5));
    QCOMPARE(star.getStarSize(), pdf::PDFStarButton::StarSize::Medium);
}

void FluentComponentsTest::colorSwatchGroup()
{
    // Swatches of an exclusive button group
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QButtonGroup palette;
    pdf::PDFColorSwatch* yellow = new pdf::PDFColorSwatch(token(Color::AnnotationYellow), QStringLiteral("Yellow"), &window);
    pdf::PDFColorSwatch* red = new pdf::PDFColorSwatch(token(Color::AnnotationRed), QStringLiteral("Red"), &window);
    pdf::PDFColorSwatch* none = new pdf::PDFColorSwatch(&window);
    none->setKind(pdf::PDFColorSwatch::Kind::None);
    palette.addButton(yellow);
    palette.addButton(red);
    palette.addButton(none);
    layout->addWidget(yellow);
    layout->addWidget(red);
    layout->addWidget(none);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    yellow->setSelected(true);
    QVERIFY(yellow->isSelected());
    red->click();
    QVERIFY(red->isSelected());
    QVERIFY(!yellow->isSelected());

    // A click on the selected swatch keeps it
    red->click();
    QVERIFY(red->isSelected());

    // The selected swatch reacts to the pointer (the design has its Hover variant)
    red->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(red->getComponentState().isHovered());
    red->setAttribute(Qt::WA_UnderMouse, false);

    // The arrows move the selection
    red->setFocus(Qt::TabFocusReason);
    QTest::keyClick(red, Qt::Key_Right);
    QVERIFY(none->isSelected());
    QVERIFY(none->hasFocus());

    // The selected swatch can be unselected by the program: the current color is none of them
    none->setSelected(false);
    QVERIFY(!none->isSelected());
    QVERIFY(!palette.checkedButton());
    QVERIFY(palette.exclusive());
    QCOMPARE(none->property("selected").toBool(), false);
    red->setProperty("selected", true);
    QVERIFY(red->isSelected());

    // Accessibility: radio buttons named by their colors
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(red);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::RadioButton);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Red"));
    QCOMPARE(red->toolTip(), QStringLiteral("Red"));
    QCOMPARE(QAccessible::queryAccessibleInterface(none)->text(QAccessible::Name), QStringLiteral("No color"));
    pdf::PDFColorSwatch unnamed(QColor(0x12, 0x34, 0x56));
    QCOMPARE(QAccessible::queryAccessibleInterface(&unnamed)->text(QAccessible::Name), QStringLiteral("#123456"));
    pdf::PDFColorSwatch invalid((QColor()));
    QCOMPARE(QAccessible::queryAccessibleInterface(&invalid)->text(QAccessible::Name), QStringLiteral("Color"));
    unnamed.setAccessibleName(QStringLiteral("Navy"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&unnamed)->text(QAccessible::Name), QStringLiteral("Navy"));

    // A button group of the application takes the place of the auto exclusivity
    QWidget other;
    QButtonGroup group;
    pdf::PDFColorSwatch first(Qt::green, QString(), &other);
    pdf::PDFColorSwatch second(Qt::blue, QString(), &other);
    group.addButton(&first);
    group.addButton(&second);
    first.click();
    second.click();
    QVERIFY(second.isSelected());
    QVERIFY(!first.isSelected());
    second.click();
    QVERIFY(second.isSelected());
    QCOMPARE(group.checkedButton(), &second);

    // A disabled swatch does not react
    second.setEnabled(false);
    second.setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(!second.getComponentState().isHovered());
    QVERIFY(second.getComponentState().isOn());

    // Setters
    first.setColor(Qt::cyan);
    QCOMPARE(first.getColor(), QColor(Qt::cyan));
    first.setKind(static_cast<pdf::PDFColorSwatch::Kind>(42));
    QCOMPARE(first.getKind(), pdf::PDFColorSwatch::Kind::Color);
}

void FluentComponentsTest::tag()
{
    pdf::PDFTag tag(QStringLiteral("Advanced"));
    QVERIFY(!tag.isClickable());
    QCOMPARE(tag.focusPolicy(), Qt::NoFocus);
    QVERIFY(tag.testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->role(), QAccessible::StaticText);
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->text(QAccessible::Name), QStringLiteral("Advanced"));

    // A label does not activate
    QSignalSpy spy(&tag, &QAbstractButton::clicked);
    tag.resize(tag.sizeHint());
    QTest::keyClick(&tag, Qt::Key_Return);
    QCOMPARE(spy.count(), 0);

    tag.setClickable(true);
    QCOMPARE(tag.focusPolicy(), Qt::StrongFocus);
    QVERIFY(!tag.testAttribute(Qt::WA_TransparentForMouseEvents));
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->role(), QAccessible::Button);
    QTest::keyClick(&tag, Qt::Key_Return);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(tag.getFocusRing(), pdf::PDFComponentButton::FocusRing::Inside);

    // The mouse of a label passes to the row below it
    TestRow row;
    row.resize(300, 48);
    pdf::PDFTag* rowTag = new pdf::PDFTag(QStringLiteral("Advanced"));
    row.addTrailingWidget(rowTag);
    QSignalSpy rowSpy(&row, &QAbstractButton::clicked);
    row.show();
    QVERIFY(QTest::qWaitForWindowExposed(&row));
    QVERIFY(!row.childAt(rowTag->geometry().center()));
    QTest::mouseClick(&row, Qt::LeftButton, Qt::NoModifier, rowTag->geometry().center());
    QCOMPARE(rowSpy.count(), 1);
}

void FluentComponentsTest::baseKeyboard()
{
    TestRow row;
    row.setText(QStringLiteral("Merge PDF"));
    row.resize(row.sizeHint());
    row.show();
    row.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&row));
    row.setFocus(Qt::TabFocusReason);

    QSignalSpy spy(&row, &QAbstractButton::clicked);
    QTest::keyClick(&row, Qt::Key_Space);
    QCOMPARE(spy.count(), 1);
    QTest::keyClick(&row, Qt::Key_Return);
    QCOMPARE(spy.count(), 2);
    QTest::keyClick(&row, Qt::Key_Enter, Qt::KeypadModifier);
    QCOMPARE(spy.count(), 3);

    // Enter with a modifier and keys of other meaning do nothing
    QTest::keyClick(&row, Qt::Key_Return, Qt::ControlModifier);
    QTest::keyClick(&row, Qt::Key_A);
    QCOMPARE(spy.count(), 3);

    // A disabled row does not activate
    row.setEnabled(false);
    QTest::keyClick(&row, Qt::Key_Return);
    QCOMPARE(spy.count(), 3);

    // A card as well
    TestCard card;
    card.resize(card.sizeHint());
    QSignalSpy cardSpy(&card, &QAbstractButton::clicked);
    QTest::keyClick(&card, Qt::Key_Return);
    QCOMPARE(cardSpy.count(), 1);
}

void FluentComponentsTest::baseSelection()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // Static exclusive group: the selected card does not react to the pointer and a click keeps it
    QWidget parent;
    QButtonGroup staticGroup;
    TestCard* first = new TestCard(&parent);
    TestCard* second = new TestCard(&parent);
    first->setSelectionBehavior(Behavior::ExclusiveStatic);
    second->setSelectionBehavior(Behavior::ExclusiveStatic);
    staticGroup.addButton(first);
    staticGroup.addButton(second);
    QVERIFY(first->isCheckable());
    QVERIFY(!first->autoExclusive());
    first->click();
    QVERIFY(first->isChecked());
    first->click();
    QVERIFY(first->isChecked());
    second->click();
    QVERIFY(second->isChecked());
    QVERIFY(!first->isChecked());
    second->setAttribute(Qt::WA_UnderMouse, true);
    second->setDown(true);
    QVERIFY(!second->getComponentState().isHovered());
    QVERIFY(!second->getComponentState().isPressed());
    first->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(first->getComponentState().isHovered());
    second->setDown(false);

    // Deselectable group: a click on the selected card unselects it, also in a button group
    QWidget other;
    QButtonGroup group;
    TestCard* third = new TestCard(&other);
    TestCard* fourth = new TestCard(&other);
    third->setSelectionBehavior(Behavior::Deselectable);
    fourth->setSelectionBehavior(Behavior::Deselectable);
    group.addButton(third);
    group.addButton(fourth);
    third->click();
    QVERIFY(third->isChecked());
    fourth->click();
    QVERIFY(fourth->isChecked());
    QVERIFY(!third->isChecked());
    fourth->click();
    QVERIFY(!fourth->isChecked());
    QVERIFY(!third->isChecked());
    QVERIFY(group.exclusive());
    QVERIFY(!group.checkedButton());
    third->click();
    QVERIFY(third->isChecked());

    // Deselectable without a group
    TestCard single;
    single.setSelectionBehavior(Behavior::Deselectable);
    single.click();
    QVERIFY(single.isChecked());
    single.click();
    QVERIFY(!single.isChecked());

    // Back to a plain button, an invalid value is a plain button
    single.click();
    single.setSelectionBehavior(Behavior::None);
    QVERIFY(!single.isCheckable());
    QVERIFY(!single.isChecked());
    single.setSelectionBehavior(static_cast<Behavior>(77));
    QCOMPARE(single.getSelectionBehavior(), Behavior::None);
    QCOMPARE(single.getComponentState().flags & (QStyle::State_On | QStyle::State_Off), QStyle::State(QStyle::State_None));
}

void FluentComponentsTest::baseFocusRing()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    TestRow* row = new TestRow(&window);
    TestCard* card = new TestCard(&window);
    layout->addWidget(row);
    layout->addWidget(card);
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

    // A click shows no ring
    QTest::mouseClick(row, Qt::LeftButton);
    QVERIFY(row->hasFocus());
    QVERIFY(!row->getComponentState().hasFocusRing());

    // The keyboard: the row draws the ring inside, the focus frame is not used
    QTest::keyClick(row, Qt::Key_Tab);
    QTest::keyClick(card, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(row->hasFocus());
    QVERIFY(row->getComponentState().hasFocusRing());
    QVERIFY(!findFrame());
    row->paintCount = 0;
    row->repaint();
    QVERIFY(row->paintCount > 0);
    QVERIFY(row->lastState.hasFocusRing());

    // The card leaves its ring to the focus frame of the style, it has the size of the card
    QTest::keyClick(row, Qt::Key_Tab);
    QTRY_VERIFY(card->hasFocus());
    QVERIFY(!card->getComponentState().hasFocusRing());
    QTRY_VERIFY(findFrame());
    QCOMPARE(findFrame()->widget(), card);
    QCOMPARE(findFrame()->geometry(), card->geometry().adjusted(-2, -2, 2, 2));

    // A disabled row has no ring
    QTest::keyClick(card, Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(row->hasFocus());
    row->setEnabled(false);
    QVERIFY(!row->getComponentState().hasFocusRing());
}

void FluentComponentsTest::baseAccessibility()
{
    TestRow row;
    row.setText(QStringLiteral("Merge PDF"));
    row.setSubtitle(QStringLiteral("Combine multiple PDFs into one file"));

    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&row);
    QVERIFY(accessible);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Merge PDF"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Combine multiple PDFs into one file"));
    QVERIFY(accessible->actionInterface());
    QVERIFY(accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::pressAction()));

    // The press action clicks the row
    QSignalSpy spy(&row, &QAbstractButton::clicked);
    accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(spy.count(), 1);

    // A description of the application is kept, a new subtitle replaces the previous one
    row.setSubtitle(QStringLiteral("Other"));
    QCOMPARE(row.accessibleDescription(), QStringLiteral("Other"));
    row.setAccessibleDescription(QStringLiteral("Own"));
    row.setSubtitle(QStringLiteral("Third"));
    QCOMPARE(row.accessibleDescription(), QStringLiteral("Own"));

    // The name with an ampersand is the text as it is drawn
    row.setText(QStringLiteral("Save & close"));
    QCOMPARE(row.getAccessibleText(), QStringLiteral("Save & close"));
    row.setAccessibleName(QStringLiteral("Own name"));
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Own name"));

    // A choice is a radio button with the checked state
    TestCard card;
    card.setText(QStringLiteral("Add Page Numbers"));
    card.setSelectionBehavior(pdf::PDFComponentButton::SelectionBehavior::Deselectable);
    QAccessibleInterface* cardAccessible = QAccessible::queryAccessibleInterface(&card);
    QCOMPARE(cardAccessible->role(), QAccessible::RadioButton);
    QVERIFY(!cardAccessible->state().checked);
    cardAccessible->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    QVERIFY(card.isChecked());
    QVERIFY(cardAccessible->state().checked);
    QCOMPARE(cardAccessible->text(QAccessible::Name), QStringLiteral("Add Page Numbers"));
}

void FluentComponentsTest::baseTrailingWidgets()
{
    TestRow row;
    row.resize(380, 48);
    row.show();
    QVERIFY(QTest::qWaitForWindowExposed(&row));
    QCOMPARE(row.getTrailingRect(), QRect());
    QCOMPARE(row.getContentRect(QMargins(8, 0, 6, 0), 10), QRect(8, 0, 366, 48));

    // A row of widgets 12 px from the trailing edge with gaps of 12 px
    pdf::PDFStarButton* star = new pdf::PDFStarButton();
    pdf::PDFIconButton* more = new pdf::PDFIconButton(QStringLiteral("more-vertical"), pdf::PDFIconButton::Size::Small);
    row.addTrailingWidget(star);
    row.addTrailingWidget(more);
    row.addTrailingWidget(more);
    row.addTrailingWidget(nullptr);
    row.addTrailingWidget(&row);
    QCOMPARE(row.getTrailingWidgets().size(), 2);
    QCOMPARE(star->parentWidget(), &row);
    QCOMPARE(star->geometry(), QRect(380 - 12 - 24 - 12 - 28, 10, 28, 28));
    QCOMPARE(more->geometry(), QRect(380 - 12 - 24, 12, 24, 24));
    QCOMPARE(row.getTrailingRect(), QRect(380 - 12 - 64, 10, 64, 28));
    QCOMPARE(row.getContentRect(QMargins(8, 0, 6, 0), 10).right(), 380 - 12 - 64 - 1 - 10);

    // A hidden widget leaves its place, a resize moves the widgets
    star->hide();
    QApplication::processEvents();
    QCOMPARE(row.getTrailingRect(), QRect(380 - 12 - 24, 12, 24, 24));
    star->show();
    row.resize(400, 48);
    QCOMPARE(more->geometry(), QRect(400 - 12 - 24, 12, 24, 24));

    // A change of the size of a nested widget
    star->setStarSize(pdf::PDFStarButton::StarSize::Small);
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(star->geometry(), QRect(400 - 12 - 24 - 12 - 24, 12, 24, 24));

    // A column at the top (actions of Pinned card)
    row.setTrailingOrientation(Qt::Vertical);
    row.setTrailingSpacing(4);
    row.setTrailingMargins(QMargins(0, 12, 8, 0));
    QCOMPARE(star->geometry(), QRect(400 - 8 - 24, 12, 24, 24));
    QCOMPARE(more->geometry(), QRect(400 - 8 - 24, 40, 24, 24));
    row.setTrailingSpacing(-5);
    QCOMPARE(row.getTrailingSpacing(), 0);

    // A removed widget is not placed any more, a deleted one is forgotten
    row.removeTrailingWidget(more);
    row.removeTrailingWidget(more);
    QCOMPARE(row.getTrailingWidgets().size(), 1);
    QVERIFY(!more->testAttribute(Qt::WA_NoMousePropagation));
    delete star;
    QCOMPARE(row.getTrailingWidgets().size(), 0);
    QCOMPARE(row.getTrailingRect(), QRect());
    row.resize(200, 48);
}

void FluentComponentsTest::defensive()
{
    // Null painter
    const Painter::State normal;
    Painter::paintToolBadge(nullptr, QRectF(0, 0, 32, 32), Painter::ToolBadge(), normal);
    Painter::paintTag(nullptr, QRectF(0, 0, 40, 21), Painter::Tag{ QStringLiteral("OCR") }, normal);
    Painter::paintTextLink(nullptr, QRectF(0, 0, 40, 16), Painter::TextLink{ QStringLiteral("Link") }, normal);
    Painter::paintStar(nullptr, QRectF(0, 0, 28, 28), Painter::StarSize::Medium, normal);
    Painter::paintColorSwatch(nullptr, QRectF(0, 0, 28, 28), Painter::ColorSwatch(), normal);
    Painter::paintPageThumbnail(nullptr, QRectF(0, 0, 32, 42), Painter::PageThumbnail(), normal);
    Painter::paintPageThumbnailShadow(nullptr, QRectF(0, 0, 32, 42));
    Painter::paintPageThumbnailFrame(nullptr, QRectF(0, 0, 32, 42), Painter::PageThumbnail(), normal);
    Painter::paintFocusRingOutside(nullptr, QRectF(0, 0, 10, 10), pdf::PDFStylePainter::Radii(2.0));
    Painter::paintFocusRingInside(nullptr, QRectF(0, 0, 10, 10), pdf::PDFStylePainter::Radii(2.0));

    // Empty and negative rectangles draw nothing, the state of the painter is restored
    const QImage empty = render([](QPainter&) { }, QSize(40, 40));
    for (const QRectF& rect : { QRectF(), QRectF(10, 10, 0, 20), QRectF(10, 10, -20, -20), QRectF(10, 10, 20, -5) })
    {
        const QImage image = render([&](QPainter& painter)
        {
            const QTransform transform = painter.transform();
            Painter::paintToolBadge(&painter, rect, Painter::ToolBadge{ QStringLiteral("grid"), pdf::PDFAccentHue::Red, Painter::BadgeSize::Large }, normal);
            Painter::paintTag(&painter, rect, Painter::Tag{ QStringLiteral("OCR") }, normal);
            Painter::paintTextLink(&painter, rect, Painter::TextLink{ QStringLiteral("Link") }, Painter::State(QStyle::State_Enabled | QStyle::State_HasFocus));
            Painter::paintStar(&painter, rect, Painter::StarSize::Medium, Painter::State(QStyle::State_Enabled | QStyle::State_On | QStyle::State_MouseOver));
            Painter::paintColorSwatch(&painter, rect, Painter::ColorSwatch{ Qt::red, Painter::SwatchKind::None }, Painter::State(QStyle::State_Enabled | QStyle::State_On | QStyle::State_HasFocus));
            Painter::paintPageThumbnail(&painter, rect, Painter::PageThumbnail(), normal);
            Painter::paintPageThumbnailShadow(&painter, rect);
            Painter::paintPageThumbnailFrame(&painter, rect, Painter::PageThumbnail(), normal);
            QCOMPARE(painter.transform(), transform);
            QCOMPARE(painter.hasClipping(), false);
        }, QSize(40, 40));
        QCOMPARE(image, empty);
    }

    // Small rectangles: the components shrink, nothing is drawn outside
    for (const QSizeF& size : { QSizeF(3, 3), QSizeF(1, 40), QSizeF(12, 5) })
    {
        const QRectF rect(QPointF(10, 10), size);
        const QImage image = render([&](QPainter& painter)
        {
            Painter::paintToolBadge(&painter, rect, Painter::ToolBadge{ QStringLiteral("grid"), pdf::PDFAccentHue::Red, Painter::BadgeSize::Large }, normal);
            Painter::paintTag(&painter, rect, Painter::Tag{ QStringLiteral("A long label") }, normal);
            Painter::paintStar(&painter, rect, Painter::StarSize::Medium, Painter::State(QStyle::State_Enabled | QStyle::State_On | QStyle::State_MouseOver));
            Painter::paintColorSwatch(&painter, rect, Painter::ColorSwatch{ Qt::red, Painter::SwatchKind::None }, Painter::State(QStyle::State_Enabled | QStyle::State_On));
            Painter::paintPageThumbnail(&painter, rect, Painter::PageThumbnail{ Painter::ThumbnailSize::Large, QPixmap() }, normal);
        }, QSize(60, 60));
        const QRect outside = rect.toAlignedRect().adjusted(-4, -4, 4, 4);
        for (int y = 0; y < 60; ++y)
        {
            for (int x = 0; x < 60; ++x)
            {
                if (!outside.contains(x, y))
                {
                    QVERIFY2(image.pixelColor(x, y) == empty.pixelColor(0, 0), qPrintable(QStringLiteral("%1 %2 (%3)").arg(x).arg(y).arg(size.width())));
                }
            }
        }
    }

    // Empty texts, unknown and empty icons, invalid colors
    const QImage image = render([&](QPainter& painter)
    {
        Painter::paintToolBadge(&painter, QRectF(0, 0, 32, 32), Painter::ToolBadge{ QStringLiteral("no-such-icon"), pdf::PDFAccentHue::Blue, Painter::BadgeSize::Medium }, normal);
        Painter::paintToolBadge(&painter, QRectF(0, 0, 32, 32), Painter::ToolBadge{ QString(), pdf::PDFAccentHue::Blue, Painter::BadgeSize::Medium }, normal);
        Painter::paintTag(&painter, QRectF(0, 0, 20, 21), Painter::Tag(), normal);
        Painter::paintTextLink(&painter, QRectF(0, 0, 4, 16), Painter::TextLink(), normal);
        Painter::paintColorSwatch(&painter, QRectF(0, 0, 28, 28), Painter::ColorSwatch{ QColor(), Painter::SwatchKind::Color }, normal);
    }, QSize(40, 40));
    QCOMPARE(countLeakPixels(image), 0);
    QCOMPARE(Painter::getTagSize(Painter::Tag()), QSize(20, 21));
    QCOMPARE(Painter::getTextLinkSize(Painter::TextLink()), QSize(4, 16));
    QCOMPARE(Painter::elidedText(QStringLiteral("Text"), Painter::getTagFont(), 0.0), QString());
    QCOMPARE(Painter::elidedText(QStringLiteral("Text"), Painter::getTagFont(), -5.0), QString());
    QCOMPARE(Painter::elidedText(QString(), Painter::getTagFont(), 50.0), QString());

    // Invalid values of the enumerations are replaced by the defaults
    QCOMPARE(Painter::normalized(static_cast<Painter::BadgeSize>(31)), Painter::BadgeSize::Medium);
    QCOMPARE(Painter::normalized(static_cast<pdf::PDFAccentHue>(-1)), pdf::PDFAccentHue::Neutral);
    QCOMPARE(Painter::normalized(static_cast<Painter::StarSize>(0)), Painter::StarSize::Medium);
    QCOMPARE(Painter::normalized(static_cast<Painter::SwatchKind>(9)), Painter::SwatchKind::Color);
    QCOMPARE(Painter::normalized(static_cast<Painter::ThumbnailSize>(9)), Painter::ThumbnailSize::Small);
    QCOMPARE(Painter::getBadgeDiameter(static_cast<Painter::BadgeSize>(1000)), 32);
    QCOMPARE(Painter::getPageThumbnailSize(static_cast<Painter::ThumbnailSize>(-3)), QSize(32, 42));
    QCOMPARE(Painter::getStarSize(static_cast<Painter::StarSize>(99)), QSize(28, 28));

    pdf::PDFToolBadge badge;
    badge.setBadgeSize(static_cast<Painter::BadgeSize>(7));
    QCOMPARE(badge.getBadgeSize(), Painter::BadgeSize::Medium);
    badge.setHue(static_cast<pdf::PDFAccentHue>(100));
    QCOMPARE(badge.getHue(), pdf::PDFAccentHue::Neutral);
    badge.setIconName(QStringLiteral("no-such-icon"));
    badge.resize(badge.sizeHint());
    QCOMPARE(countLeakPixels(badge.grab().toImage()), 0);
    pdf::PDFPageThumbnail thumbnail;
    thumbnail.setThumbnailSize(static_cast<Painter::ThumbnailSize>(12));
    QCOMPARE(thumbnail.getThumbnailSize(), Painter::ThumbnailSize::Small);
    thumbnail.setPixmap(QPixmap());
    thumbnail.resize(1, 1);
    thumbnail.grab();
    thumbnail.resize(0, 0);
    thumbnail.grab();

    // Widgets with zero size
    pdf::PDFTag tag;
    tag.resize(0, 0);
    tag.grab();
    TestCard card;
    card.setFocusRingRadius(-3.0);
    QCOMPARE(card.getFocusRingRadius(), 0.0);
    QCOMPARE(card.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 0.0);
    card.setFocusRingRadius(qQNaN());
    QCOMPARE(card.getFocusRingRadius(), 0.0);
    card.setTrailingOrientation(static_cast<Qt::Orientation>(17));
    QCOMPARE(card.getTrailingOrientation(), Qt::Horizontal);
    card.resize(0, 0);
    card.addTrailingWidget(new pdf::PDFStarButton());
    card.grab();
    QVERIFY(card.getContentRect(QMargins(20, 20, 20, 20), 8).width() >= 0);
    QVERIFY(card.getContentRect(QMargins(20, 20, 20, 20), 8).height() >= 0);
}

void FluentComponentsTest::mnemonics()
{
    // The text is data: an ampersand is drawn, measured and announced, it creates no shortcut (C2)
    TestRow row;
    row.setText(QStringLiteral("Protect & Sign"));
    QCOMPARE(row.shortcut(), QKeySequence());
    QCOMPARE(row.getAccessibleText(), QStringLiteral("Protect & Sign"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&row)->text(QAccessible::Name), QStringLiteral("Protect & Sign"));

    // A shortcut of the application is kept
    const QKeySequence own(Qt::CTRL | Qt::Key_P);
    row.setShortcut(own);
    row.setText(QStringLiteral("Review & Inspect"));
    QCOMPARE(row.shortcut(), own);

    pdf::PDFTag tag(QStringLiteral("R&D"));
    QCOMPARE(tag.shortcut(), QKeySequence());
    QCOMPARE(tag.sizeHint().width(), qCeil(pdf::PDFStylePainter::textWidth(QStringLiteral("R&D"), Painter::getTagFont())) + 20);
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->text(QAccessible::Name), QStringLiteral("R&D"));
    pdf::PDFTextLink link(QStringLiteral("Terms & Conditions"));
    QCOMPARE(link.shortcut(), QKeySequence());
    QCOMPARE(link.sizeHint(), Painter::getTextLinkSize(Painter::TextLink{ QStringLiteral("Terms & Conditions") }));

    // The text set through QAbstractButton loses its mnemonic, when the button is polished
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    TestRow* polished = new TestRow(&window);
    static_cast<QAbstractButton*>(polished)->setText(QStringLiteral("Merge &PDF"));
    QVERIFY(!polished->shortcut().isEmpty());
    TestRow* pressed = new TestRow(&window);
    layout->addWidget(polished);
    layout->addWidget(pressed);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    QCOMPARE(polished->shortcut(), QKeySequence());

    // ... or when its mnemonic is pressed; the button is not activated by it
    static_cast<QAbstractButton*>(pressed)->setText(QStringLiteral("&Open"));
    QCOMPARE(pressed->shortcut(), QKeySequence(Qt::ALT | Qt::Key_O));
    QSignalSpy spy(pressed, &QAbstractButton::clicked);
    QTest::keyClick(&window, Qt::Key_O, Qt::AltModifier);
    QTest::qWait(250);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(pressed->shortcut(), QKeySequence());
}

void FluentComponentsTest::selectionByMouseAndKeys()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QButtonGroup choices;
    QButtonGroup pages;
    QButtonGroup swatches;
    swatches.setExclusive(false);

    TestCard* first = new TestCard(&window);
    TestCard* second = new TestCard(&window);
    TestCard* current = new TestCard(&window);
    TestCard* other = new TestCard(&window);
    TestCard* kept = new TestCard(&window);
    for (TestCard* card : { first, second })
    {
        card->setSelectionBehavior(Behavior::Deselectable);
        choices.addButton(card);
    }
    for (TestCard* card : { current, other })
    {
        card->setSelectionBehavior(Behavior::ExclusiveStatic);
        pages.addButton(card);
    }
    kept->setSelectionBehavior(Behavior::Exclusive);
    swatches.addButton(kept);
    for (TestCard* card : { first, second, current, other, kept })
    {
        layout->addWidget(card);
    }
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // Deselectable: the mouse, Space and Enter unselect the selected item, also
    // in an exclusive group, which stays exclusive (C1)
    QSignalSpy firstClicked(first, &QAbstractButton::clicked);
    bool isExclusiveInSlot = false;
    connect(first, &QAbstractButton::clicked, first, [&]() { isExclusiveInSlot = choices.exclusive(); });
    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(first->isChecked());
    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(!first->isChecked());
    QVERIFY(!choices.checkedButton());
    QVERIFY(choices.exclusive());
    QVERIFY(isExclusiveInSlot);
    QCOMPARE(firstClicked.count(), 2);
    QTest::mouseClick(second, Qt::LeftButton);
    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(first->isChecked());
    QVERIFY(!second->isChecked());

    first->setFocus(Qt::TabFocusReason);
    QTest::keyClick(first, Qt::Key_Space);
    QVERIFY(!first->isChecked());
    QTest::keyClick(first, Qt::Key_Space);
    QVERIFY(first->isChecked());
    QTest::keyClick(first, Qt::Key_Return);
    QVERIFY(!first->isChecked());
    QVERIFY(choices.exclusive());

    // A press released outside of the button does not unselect it, the group stays exclusive
    QTest::mouseClick(first, Qt::LeftButton);
    QVERIFY(first->isChecked());
    QTest::mousePress(first, Qt::LeftButton);
    QTest::mouseRelease(first, Qt::LeftButton, Qt::NoModifier, QPoint(-20, -20));
    QVERIFY(first->isChecked());
    QVERIFY(choices.exclusive());
    QCOMPARE(choices.checkedButton(), first);

    // ExclusiveStatic: nothing reacts on the selected item, no clicked signal (C5)
    current->click();
    QVERIFY(current->isChecked());
    QVERIFY(!current->isActivatable());
    QSignalSpy currentClicked(current, &QAbstractButton::clicked);
    QSignalSpy currentPressed(current, &QAbstractButton::pressed);
    QTest::mouseClick(current, Qt::LeftButton);
    current->setFocus(Qt::TabFocusReason);
    QTest::keyClick(current, Qt::Key_Space);
    QTest::keyClick(current, Qt::Key_Return);
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(current);
    QVERIFY(accessible && accessible->actionInterface());
    QVERIFY(!accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::toggleAction()));
    QVERIFY(!accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::pressAction()));
    accessible->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    QCOMPARE(currentClicked.count(), 0);
    QCOMPARE(currentPressed.count(), 0);
    QVERIFY(current->isChecked());
    QVERIFY(!current->isDown());

    // The other item is clicked as a button
    QSignalSpy otherClicked(other, &QAbstractButton::clicked);
    QTest::mouseClick(other, Qt::LeftButton);
    QCOMPARE(otherClicked.count(), 1);
    QVERIFY(other->isChecked());
    QVERIFY(!current->isChecked());
    QVERIFY(current->isActivatable());
    QVERIFY(accessible->actionInterface()->actionNames().contains(QAccessibleActionInterface::toggleAction()));

    // Exclusive in a group, which is not exclusive: a click keeps the selected item (C10)
    QTest::mouseClick(kept, Qt::LeftButton);
    QVERIFY(kept->isChecked());
    QTest::mouseClick(kept, Qt::LeftButton);
    QVERIFY(kept->isChecked());
    kept->click();
    QVERIFY(kept->isChecked());
}

void FluentComponentsTest::independentGroups()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // Two choices and a radio button laid out on one page do not exclude each other (C3)
    QWidget page;
    QVBoxLayout* layout = new QVBoxLayout(&page);
    QButtonGroup themes;
    QButtonGroup modes;
    TestCard* light = new TestCard(&page);
    TestCard* dark = new TestCard(&page);
    TestCard* normal = new TestCard(&page);
    TestCard* sepia = new TestCard(&page);
    QRadioButton* radio = new QRadioButton(QStringLiteral("Option"), &page);
    for (TestCard* card : { light, dark })
    {
        card->setSelectionBehavior(Behavior::ExclusiveStatic);
        themes.addButton(card);
        layout->addWidget(card);
    }
    for (TestCard* card : { normal, sepia })
    {
        card->setSelectionBehavior(Behavior::ExclusiveStatic);
        modes.addButton(card);
        layout->addWidget(card);
    }
    layout->addWidget(radio);

    radio->setChecked(true);
    light->click();
    normal->click();
    QVERIFY(light->isChecked());
    QVERIFY(normal->isChecked());
    QVERIFY(radio->isChecked());
    dark->click();
    QVERIFY(dark->isChecked());
    QVERIFY(!light->isChecked());
    QVERIFY(normal->isChecked());
    QVERIFY(radio->isChecked());

    // Without a group each exclusive button is a choice of its own
    TestCard* single = new TestCard(&page);
    TestCard* another = new TestCard(&page);
    single->setSelectionBehavior(Behavior::Exclusive);
    another->setSelectionBehavior(Behavior::Deselectable);
    QVERIFY(!single->autoExclusive());
    QVERIFY(!another->autoExclusive());
    single->click();
    another->click();
    QVERIFY(single->isChecked());
    QVERIFY(another->isChecked());
    QVERIFY(radio->isChecked());
    QVERIFY(dark->isChecked());
    another->click();
    QVERIFY(!another->isChecked());
}

void FluentComponentsTest::selectionReentrancy()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // A slot, which deletes the group while the item is unselected, breaks nothing (C4)
    QWidget parent;
    QPointer<QButtonGroup> group = new QButtonGroup(&parent);
    TestCard* card = new TestCard(&parent);
    card->setSelectionBehavior(Behavior::Deselectable);
    group->addButton(card);
    card->click();
    QVERIFY(card->isChecked());
    connect(card, &QAbstractButton::toggled, &parent, [&](bool isChecked) { if (!isChecked) { delete group.data(); } });
    card->click();
    QVERIFY(!group);
    QVERIFY(!card->isChecked());
    QVERIFY(!card->group());

    // ... or the button itself
    QButtonGroup other;
    QPointer<TestCard> doomed = new TestCard(&parent);
    doomed->setSelectionBehavior(Behavior::Deselectable);
    other.addButton(doomed);
    doomed->click();
    connect(doomed.data(), &QAbstractButton::toggled, &parent, [&](bool isChecked) { if (!isChecked) { delete doomed.data(); } });
    doomed->click();
    QVERIFY(!doomed);
    QVERIFY(other.exclusive());
    QVERIFY(other.buttons().isEmpty());

    // ... also during a click by the mouse, whose group was made non-exclusive for the click
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    QPointer<QButtonGroup> mouseGroup = new QButtonGroup(&window);
    TestCard* mouseCard = new TestCard(&window);
    TestCard* mouseOther = new TestCard(&window);
    for (TestCard* item : { mouseCard, mouseOther })
    {
        item->setSelectionBehavior(Behavior::Deselectable);
        mouseGroup->addButton(item);
        layout->addWidget(item);
    }
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTest::mouseClick(mouseCard, Qt::LeftButton);
    QVERIFY(mouseCard->isChecked());
    connect(mouseCard, &QAbstractButton::toggled, &window, [&](bool isChecked) { if (!isChecked) { delete mouseGroup.data(); } });
    QTest::mouseClick(mouseCard, Qt::LeftButton);
    QVERIFY(!mouseGroup);
    QVERIFY(!mouseCard->isChecked());
    QTest::mouseClick(mouseOther, Qt::LeftButton);
    QTest::mouseClick(mouseCard, Qt::LeftButton);
    QVERIFY(mouseOther->isChecked());
    QVERIFY(mouseCard->isChecked());
}

void FluentComponentsTest::selectionToNone()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // The checked button of an exclusive group becomes a plain button: unchecked with the signal (C9)
    QWidget parent;
    QButtonGroup group;
    TestCard* first = new TestCard(&parent);
    TestCard* second = new TestCard(&parent);
    for (TestCard* card : { first, second })
    {
        card->setSelectionBehavior(Behavior::Exclusive);
        group.addButton(card);
    }
    first->click();
    QCOMPARE(group.checkedButton(), first);

    QSignalSpy toggled(first, &QAbstractButton::toggled);
    first->setSelectionBehavior(Behavior::None);
    QCOMPARE(toggled.count(), 1);
    QCOMPARE(toggled.front().front().toBool(), false);
    QVERIFY(!first->isChecked());
    QVERIFY(!first->isCheckable());
    QVERIFY(!group.checkedButton());
    QVERIFY(group.exclusive());
    second->click();
    QCOMPARE(group.checkedButton(), second);
    first->click();
    QCOMPARE(group.checkedButton(), second);
    QVERIFY(!first->isChecked());
}

void FluentComponentsTest::trailingLifetime()
{
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    TrailingRow* row = new TrailingRow(&window);
    layout->addWidget(row);
    pdf::PDFStarButton* star = new pdf::PDFStarButton();
    pdf::PDFIconButton* more = new pdf::PDFIconButton(QStringLiteral("more-vertical"), pdf::PDFIconButton::Size::Small);
    row->addTrailingWidget(star);
    row->addTrailingWidget(more);
    window.resize(400, 48);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QCOMPARE(row->width(), 400);
    QCOMPARE(star->geometry(), QRect(400 - 12 - 24 - 12 - 28, 10, 28, 28));
    QCOMPARE(layout->sizeHint().width(), 200 + 28 + 12 + 24);

    // The size hint follows the nested widgets, the layout gets the new size (C6)
    star->hide();
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(layout->sizeHint().width(), 200 + 24);
    star->show();
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(layout->sizeHint().width(), 200 + 28 + 12 + 24);

    // A deleted widget leaves no gap (C7)
    delete more;
    QApplication::processEvents();
    QCOMPARE(row->getTrailingWidgets().size(), 1);
    QCOMPARE(star->geometry(), QRect(400 - 12 - 28, 10, 28, 28));
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(layout->sizeHint().width(), 200 + 28);

    // A widget moved to another parent is forgotten and not placed any more (C7)
    QWidget other;
    star->setParent(&other);
    QCOMPARE(row->getTrailingWidgets().size(), 0);
    QCOMPARE(row->getTrailingRect(), QRect());
    QVERIFY(!star->testAttribute(Qt::WA_NoMousePropagation));
    const QRect starGeometry = star->geometry();
    window.resize(300, 48);
    QApplication::sendPostedEvents();
    QApplication::processEvents();
    QCOMPARE(star->geometry(), starGeometry);
    QCOMPARE(row->getContentRect(QMargins(), 0), QRect(0, 0, row->width(), 48));

    // A widget hidden by the application stays hidden, the others are shown (C7)
    pdf::PDFIconButton* hidden = new pdf::PDFIconButton(QStringLiteral("x"), pdf::PDFIconButton::Size::Small);
    hidden->hide();
    row->addTrailingWidget(hidden);
    QVERIFY(hidden->isHidden());
    pdf::PDFIconButton* child = new pdf::PDFIconButton(QStringLiteral("copy"), pdf::PDFIconButton::Size::Small, row);
    row->addTrailingWidget(child);
    QVERIFY(child->isVisible());
    QVERIFY(child->testAttribute(Qt::WA_NoMousePropagation));
    row->addTrailingWidget(&window);
    QCOMPARE(row->getTrailingWidgets().size(), 2);

    // A click on a disabled nested widget does not activate the row (C27)
    QSignalSpy rowClicked(row, &QAbstractButton::clicked);
    child->setEnabled(false);
    QTest::mouseClick(child, Qt::LeftButton);
    QCOMPARE(rowClicked.count(), 0);
    QTest::mouseClick(row, Qt::LeftButton, Qt::NoModifier, QPoint(20, 24));
    QCOMPARE(rowClicked.count(), 1);
}

void FluentComponentsTest::accessibilityEvents()
{
    const QAccessible::UpdateHandler previousHandler = QAccessible::installUpdateHandler(&FluentComponentsTest::collectAccessibleEvent);
    s_accessibleEvents.clear();

    // The name of the star is its state: a change is announced (C12)
    pdf::PDFStarButton star;
    star.setPinned(true);
    QCOMPARE(countAccessibleEvents(&star, QAccessible::NameChanged), 1);
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&star);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Unpin"));
    QCOMPARE(accessible->text(QAccessible::Description), QString());
    star.setToolTip(QStringLiteral("Pin the tool to Home"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Pin the tool to Home"));
    star.setToolTip(QString());
    star.setAccessibleName(QStringLiteral("Pinned tool"));
    s_accessibleEvents.clear();
    star.setPinned(false);
    QCOMPARE(countAccessibleEvents(&star, QAccessible::NameChanged), 0);

    // A new language takes the texts of the state again
    star.setToolTip(QString());
    QEvent languageChange(QEvent::LanguageChange);
    QApplication::sendEvent(&star, &languageChange);
    QCOMPARE(star.toolTip(), QStringLiteral("Pin file"));

    // The name of a swatch follows its color, its kind and its name
    pdf::PDFColorSwatch swatch(QColor(0x12, 0x34, 0x56));
    s_accessibleEvents.clear();
    swatch.setColor(QColor(0x65, 0x43, 0x21));
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 1);
    swatch.setColorName(QStringLiteral("Brown"));
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 2);
    swatch.setColor(Qt::red);
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 2);
    swatch.setColorName(QString());
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 3);
    swatch.setKind(pdf::PDFColorSwatch::Kind::None);
    QCOMPARE(countAccessibleEvents(&swatch, QAccessible::NameChanged), 4);
    QCOMPARE(QAccessible::queryAccessibleInterface(&swatch)->text(QAccessible::Name), QStringLiteral("No color"));

    // The role of a tag changes with its clickability
    pdf::PDFTag tag(QStringLiteral("OCR"));
    s_accessibleEvents.clear();
    tag.setClickable(true);
    QCOMPARE(countAccessibleEvents(&tag, QAccessible::RoleChanged), 1);
    QCOMPARE(QAccessible::queryAccessibleInterface(&tag)->role(), QAccessible::Button);

    QAccessible::installUpdateHandler(previousHandler);
}

void FluentComponentsTest::focusFrame()
{
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    TestRow* row = new TestRow(&window);
    BareOutsideButton* bare = new BareOutsideButton(&window);
    TestCard* card = new TestCard(&window);
    layout->addWidget(row);
    layout->addWidget(bare);
    layout->addWidget(card);
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

    // A button with the ring outside, which has no radius of its own, has the ring (C8)
    QTest::mouseClick(row, Qt::LeftButton);
    QTest::keyClick(row, Qt::Key_Tab);
    QTRY_VERIFY(bare->hasFocus());
    QTRY_VERIFY(findFrame());
    QCOMPARE(findFrame()->widget(), bare);

    // A new radius repaints the whole ring, which is shown (C20)
    QTest::keyClick(bare, Qt::Key_Tab);
    QTRY_VERIFY(card->hasFocus());
    QTRY_VERIFY(findFrame() && findFrame()->widget() == card);
    QFocusFrame* frame = findFrame();
    QApplication::processEvents();

    struct PaintFilter : public QObject
    {
        bool isRingRepainted = false;
        virtual bool eventFilter(QObject*, QEvent* event) override
        {
            // The corner of the frame lies outside of the card
            if (event->type() == QEvent::Paint && static_cast<QPaintEvent*>(event)->region().contains(QPoint(0, 0)))
            {
                isRingRepainted = true;
            }
            return false;
        }
    } filter;
    frame->installEventFilter(&filter);
    card->setFocusRingRadius(4.0);
    QCOMPARE(card->property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 4.0);
    QTRY_VERIFY(filter.isRingRepainted);
    frame->removeEventFilter(&filter);
}

void FluentComponentsTest::apiDetails()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;
    const QColor surface = token(Color::BgSurface);

    // The state is not converted from the flags implicitly; flags without State_Enabled are disabled (C21)
    static_assert(!std::is_convertible_v<QStyle::State, Painter::State>);
    static_assert(std::is_constructible_v<Painter::State, QStyle::State>);
    QVERIFY(!Painter::State(QStyle::State_None).isEnabled());
    QVERIFY(Painter::State().isEnabled());

    // Checked and selected are told apart (C22)
    const Painter::State checked(QStyle::State_Enabled | QStyle::State_On);
    QVERIFY(checked.isChecked());
    QVERIFY(!checked.isSelected());
    QVERIFY(checked.isOn());
    const Painter::State selected(QStyle::State_Enabled | QStyle::State_Selected);
    QVERIFY(!selected.isChecked());
    QVERIFY(selected.isSelected());
    QVERIFY(selected.isOn());

    // Enumerations and properties for the meta-object system (C24)
    QCOMPARE(QByteArray(QMetaEnum::fromType<Behavior>().valueToKey(int(Behavior::Deselectable))), QByteArray("Deselectable"));
    QCOMPARE(QByteArray(QMetaEnum::fromType<pdf::PDFComponentButton::FocusRing>().valueToKey(int(pdf::PDFComponentButton::FocusRing::Outside))), QByteArray("Outside"));
    pdf::PDFStarButton star;
    QSignalSpy pinnedSpy(&star, &pdf::PDFStarButton::pinnedChanged);
    QVERIFY(star.setProperty("pinned", true));
    QVERIFY(star.isPinned());
    QCOMPARE(pinnedSpy.count(), 1);
    QCOMPARE(star.property("pinned").toBool(), true);
    QVERIFY(star.metaObject()->property(star.metaObject()->indexOfProperty("pinned")).hasNotifySignal());
    pdf::PDFColorSwatch swatch;
    QVERIFY(swatch.setProperty("color", QColor(Qt::green)));
    QCOMPARE(swatch.getColor(), QColor(Qt::green));

    // A tag, which stops being clickable, gives its focus away and a text is not pressed by Space (C11, C19)
    QWidget window;
    QHBoxLayout* layout = new QHBoxLayout(&window);
    pdf::PDFTag* tag = new pdf::PDFTag(QStringLiteral("Forms"), &window);
    tag->setClickable(true);
    LabelRow* label = new LabelRow(&window);
    layout->addWidget(tag);
    layout->addWidget(label);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    tag->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(tag->hasFocus());
    QVERIFY(tag->testAttribute(Qt::WA_SetCursor));
    tag->setClickable(false);
    QVERIFY(!tag->hasFocus());
    QVERIFY(!tag->testAttribute(Qt::WA_SetCursor));
    label->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(label->hasFocus());
    QSignalSpy labelPressed(label, &QAbstractButton::pressed);
    QSignalSpy labelClicked(label, &QAbstractButton::clicked);
    QTest::keyClick(label, Qt::Key_Space);
    QTest::keyClick(label, Qt::Key_Return);
    QTest::mouseClick(label, Qt::LeftButton);
    QCOMPARE(labelPressed.count(), 0);
    QCOMPARE(labelClicked.count(), 0);
    QVERIFY(!label->isActivatable());

    // The placeholder of a thumbnail is mirrored for right-to-left layouts (C16): the shorter
    // last line lies at the leading edge
    const QRectF frame(4, 4, 32, 42);
    const QImage ltr = render([&](QPainter& painter) { Painter::paintPageThumbnailFrame(&painter, frame, Painter::PageThumbnail(), state(QStyle::State_None)); }, QSize(40, 50));
    const QImage rtl = render([&](QPainter& painter) { Painter::paintPageThumbnailFrame(&painter, frame, Painter::PageThumbnail(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(40, 50));
    QVERIFY(isNear(pixel(ltr, 10, 20), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(ltr, 29, 20), surface));
    QVERIFY(isNear(pixel(rtl, 10, 20), surface));
    QVERIFY(isNear(pixel(rtl, 29, 20), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(rtl, 10, 14), token(Color::BorderStrong)));
}

void FluentComponentsTest::navItem()
{
    using Behavior = pdf::PDFComponentButton::SelectionBehavior;

    // The item of a navigation is a row with the static exclusive behaviour (C17)
    pdf::PDFNavList list;
    list.addItem(QStringLiteral("Home"), QStringLiteral("home"));
    list.addItem(QStringLiteral("Open"), QStringLiteral("folder"));
    list.show();
    list.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&list));
    pdf::PDFNavItem* home = list.getItem(0);
    pdf::PDFNavItem* open = list.getItem(1);
    QVERIFY(qobject_cast<pdf::PDFRowButton*>(home));
    QCOMPARE(home->getSelectionBehavior(), Behavior::ExclusiveStatic);
    QCOMPARE(home->getFocusRing(), pdf::PDFComponentButton::FocusRing::Inside);
    QCOMPARE(QAccessible::queryAccessibleInterface(home)->role(), QAccessible::RadioButton);
    QCOMPARE(home->sizeHint(), QSize(190, 38));

    // A click on the current item does nothing
    QSignalSpy homeClicked(home, &QAbstractButton::clicked);
    QSignalSpy changed(&list, &pdf::PDFNavList::currentIndexChanged);
    QTest::mouseClick(home, Qt::LeftButton);
    home->setFocus(Qt::TabFocusReason);
    QTest::keyClick(home, Qt::Key_Return);
    QTest::keyClick(home, Qt::Key_Space);
    QCOMPARE(homeClicked.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(list.getCurrentIndex(), 0);
    home->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(!home->getComponentState().isHovered());

    // Another item becomes the current one by the mouse and by Enter
    QTest::mouseClick(open, Qt::LeftButton);
    QCOMPARE(list.getCurrentIndex(), 1);
    QCOMPARE(changed.count(), 1);
    home->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(home->getComponentState().isHovered());
    home->setFocus(Qt::TabFocusReason);
    QTest::keyClick(home, Qt::Key_Return);
    QCOMPARE(list.getCurrentIndex(), 0);
    QCOMPARE(changed.count(), 2);

    // Invalid kind
    home->setKind(static_cast<pdf::PDFNavItem::Kind>(7));
    QCOMPARE(home->getKind(), pdf::PDFNavItem::Kind::Home);
    home->setKind(pdf::PDFNavItem::Kind::Settings);
    QCOMPARE(home->sizeHint(), QSize(300, 52));
}

void FluentComponentsTest::linkUnderline()
{
    // The underline is a whole pixel at the bottom of the line box of 16 px (F3); the
    // drawing does not depend on the antialiasing of the caller (samples of the gallery)
    const Painter::TextLink link{ QStringLiteral("View all") };
    const QSize size = Painter::getTextLinkSize(link);
    const int textWidth = qFloor(pdf::PDFStylePainter::textWidth(link.text, Painter::getTextLinkFont()));
    const QColor accent = token(Color::TextAccent);

    for (const qreal scale : { 1.0, 1.5, 2.0 })
    {
        auto paintHovered = [&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(4, 4), size), link, state(QStyle::State_MouseOver)); };
        const QImage image = render(paintHovered, size + QSize(8, 8), scale);

        QImage plain(image.size(), QImage::Format_ARGB32_Premultiplied);
        plain.setDevicePixelRatio(scale);
        plain.fill(token(Color::BgSurface));
        {
            QPainter painter(&plain);
            paintHovered(painter);
        }
        const QString difference = describeDifference(image, plain, 0);
        QVERIFY2(difference.isEmpty(), qPrintable(difference));

        for (int x = 4 + 2; x < 4 + 2 + textWidth; ++x)
        {
            QVERIFY2(isNear(pixel(image, x, 4 + 15), accent, 3), qPrintable(QStringLiteral("%1 at %2").arg(pixel(image, x, 4 + 15).name()).arg(x)));
        }
        QCOMPARE(countPixels(image, QRect(4, 4 + 14, size.width(), 1), accent, 40), 0);
        QCOMPARE(countPixels(image, QRect(4, 4 + 16, size.width(), 1), accent, 40), 0);

        // Without the pointer there is no underline
        const QImage normal = render([&](QPainter& painter) { Painter::paintTextLink(&painter, QRectF(QPointF(4, 4), size), link, state(QStyle::State_None)); }, size + QSize(8, 8), scale);
        QCOMPARE(countPixels(normal, QRect(4, 4 + 15, size.width(), 1), accent, 40), 0);
    }

    // The widget under the pointer draws the same underline
    pdf::PDFTextLink widget(link.text);
    widget.resize(widget.sizeHint());
    widget.setAttribute(Qt::WA_UnderMouse, true);
    const QImage image = renderWidget(&widget, 1.0);
    QVERIFY(countPixels(image, QRect(2, 15, textWidth, 1), accent, 3) >= textWidth - 1);
}

void FluentComponentsTest::swatchGeometry()
{
    // The fill of the color does not bleed into its border of 1 px at 1x (F4)
    const Painter::ColorSwatch yellow{ token(Color::AnnotationYellow), Painter::SwatchKind::Color };
    QImage image = render([&](QPainter& painter) { Painter::paintColorSwatch(&painter, QRectF(0, 0, 28, 28), yellow, state(QStyle::State_None)); }, QSize(28, 28));
    QVERIFY2(isNear(pixel(image, 4, 14), token(Color::BorderStrong), 4), qPrintable(pixel(image, 4, 14).name()));
    QVERIFY2(isNear(pixel(image, 23, 14), token(Color::BorderStrong), 4), qPrintable(pixel(image, 23, 14).name()));
    QVERIFY2(isNear(pixel(image, 14, 4), token(Color::BorderStrong), 4), qPrintable(pixel(image, 14, 4).name()));
    QVERIFY(isNear(pixel(image, 14, 14), token(Color::AnnotationYellow)));
    QVERIFY(isNear(pixel(image, 6, 14), token(Color::AnnotationYellow)));

    // A translucent color is not tinted by the border
    QColor translucent = token(Color::AnnotationBlue);
    translucent.setAlpha(128);
    image = render([&](QPainter& painter) { Painter::paintColorSwatch(&painter, QRectF(0, 0, 28, 28), Painter::ColorSwatch{ translucent, Painter::SwatchKind::Color }, state(QStyle::State_None)); }, QSize(28, 28));
    QVERIFY(isNear(pixel(image, 14, 14), blend(translucent, token(Color::BgSurface)), 3));

    // The slash of no color lies on the center line drawn by Figma (F2): in the row 14 its
    // strongest pixel is x = 12 (the origin of the card without the shift would give 13)
    const Painter::ColorSwatch none{ QColor(), Painter::SwatchKind::None };
    image = render([&](QPainter& painter) { Painter::paintColorSwatch(&painter, QRectF(0, 0, 28, 28), none, state(QStyle::State_None)); }, QSize(28, 28));
    const QColor danger = token(Color::IconDanger);
    int bestX = -1;
    int bestDistance = std::numeric_limits<int>::max();
    for (int x = 8; x <= 18; ++x)
    {
        const QColor color = pixel(image, x, 14);
        const int distance = qAbs(color.red() - danger.red()) + qAbs(color.green() - danger.green()) + qAbs(color.blue() - danger.blue());
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestX = x;
        }
    }
    QCOMPARE(bestX, 12);
    QVERIFY2(isNear(pixel(image, 4, 14), token(Color::BorderStrong), 4), qPrintable(pixel(image, 4, 14).name()));
}

void FluentComponentsTest::thumbnailShadow()
{
    // The widget is the frame, its shadow is drawn into the parent around it (F1)
    QWidget parent;
    parent.resize(100, 140);
    pdf::PDFPageThumbnail* thumbnail = new pdf::PDFPageThumbnail(Painter::ThumbnailSize::Medium, &parent);
    thumbnail->move(10, 10);
    thumbnail->resize(thumbnail->sizeHint());
    QCOMPARE(thumbnail->geometry(), QRect(10, 10, 56, 74));
    parent.show();
    QVERIFY(QTest::qWaitForWindowExposed(&parent));

    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));
        const QColor surface = token(Color::BgSurface);
        for (const qreal scale : { 1.0, 2.0 })
        {
            const QImage image = renderWidget(&parent, scale);
            QVERIFY2(isNear(pixel(image, 10 + 28, 10), token(Color::BorderStrong), 6), qPrintable(pixel(image, 10 + 28, 10).name()));
            QVERIFY(isNear(pixel(image, 10 + 28, 10 + 65), surface, 3));

            // Below the frame lies the shadow, it is darker than the background of the
            // parent and it ends within the margins of the shadow
            const QColor background = pixel(image, 90, 120);
            const QColor below = pixel(image, 10 + 28, 10 + 74);
            QVERIFY2(below.lightness() < background.lightness(), qPrintable(QStringLiteral("%1 / %2").arg(below.name(), background.name())));
            QVERIFY(isNear(pixel(image, 10 + 28, 10 + 74 + 4), background, 1));
            QVERIFY(isNear(pixel(image, 10 + 56 + 3, 10 + 37), background, 1));
            QCOMPARE(countLeakPixels(image), 0);
        }

        // Rendered into a device (QWidget::grab, as on the screen): the frame lies at its place and the shadow below it
        const QImage grabbed = parent.grab().toImage();
        QVERIFY2(isNear(grabbed.pixelColor(10 + 28, 10), token(Color::BorderStrong), 6), qPrintable(grabbed.pixelColor(10 + 28, 10).name()));
        QVERIFY(isNear(grabbed.pixelColor(10 + 28, 10 + 65), surface, 3));
        QVERIFY(grabbed.pixelColor(10 + 28, 10 + 74) != grabbed.pixelColor(10 + 28, 10 + 74 + 8));
    }

    // The application may remove the shadow; the widget still draws the frame
    thumbnail->setGraphicsEffect(nullptr);
    const QImage image = renderWidget(&parent, 1.0);
    QVERIFY(isNear(pixel(image, 10 + 28, 10 + 74), pixel(image, 90, 120), 1));
    QVERIFY(isNear(pixel(image, 10 + 28, 10), token(Color::BorderStrong), 6));
}

void FluentComponentsTest::galleryPages()
{
    // The pages of the components exist, each component has its samples with
    // the nodes of the design, no page shows the leak color
    const QStringList expected = { QStringLiteral("19-shell-home"), QStringLiteral("20-catalog"), QStringLiteral("21-forms"), QStringLiteral("22-document"), QStringLiteral("23-panels") };

    for (const QString& theme : { QStringLiteral("light"), QStringLiteral("dark") })
    {
        QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

        QScrollArea scrollArea;
        scrollArea.resize(1400, 900);
        scrollArea.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scrollArea));

        const std::vector<pdffluentgallery::GalleryPages::Page> pages = pdffluentgallery::GalleryPages::createPages(nullptr);
        QStringList ids;
        QSet<QString> nodes;
        for (const pdffluentgallery::GalleryPages::Page& page : pages)
        {
            ids << page.id;
            if (!expected.contains(page.id))
            {
                continue;
            }

            scrollArea.takeWidget();
            scrollArea.setWidget(page.widget);
            scrollArea.setWidgetResizable(true);
            QApplication::processEvents();

            for (const pdffluentgallery::GallerySpecimen* specimen : page.widget->findChildren<pdffluentgallery::GallerySpecimen*>())
            {
                nodes.insert(specimen->getFigmaNode());
            }

            for (const qreal scale : { 1.0, 2.0 })
            {
                const QImage image = pdffluentgallery::GalleryPages::renderWidget(page.widget, scale);
                const int leakPixels = countLeakPixels(image);
                QVERIFY2(leakPixels == 0, qPrintable(QStringLiteral("%1 pixels of the leak color on the page %2 (%3, %4)").arg(leakPixels).arg(page.id, theme).arg(scale)));
            }
        }

        // The pages follow the page of the finished components
        const qsizetype componentsIndex = ids.indexOf(QStringLiteral("18-components"));
        QVERIFY(componentsIndex >= 0);
        QCOMPARE(ids.mid(componentsIndex + 1, expected.size()), expected);

        // Variants of the design: Text link, Star button, Page thumbnail, Tool icon, Tag, Color swatch
        for (const char* node : { "49:144", "49:150", "64:545", "64:566", "78:869", "78:882", "89:1268", "706:146711", "116:2351", "206:779", "773:147235",
                                  "49:160", "49:181", "49:198", "49:225", "49:236", "49:269", "50:243", "50:270", "64:508", "64:535",
                                  "89:1290", "89:1320", "89:1331", "89:1358", "192:816", "192:831", "103:1895", "103:1922", "103:1932", "103:1941",
                                  "206:818", "206:822", "206:829", "206:837", "206:844", "206:854", "206:862" })
        {
            QVERIFY2(nodes.contains(QString::fromLatin1(node)), node);
        }

        scrollArea.takeWidget();
        for (const pdffluentgallery::GalleryPages::Page& page : pages)
        {
            delete page.widget;
        }
    }
}

QColor FluentComponentsTest::strongestInk(const QImage& image, const QRect& area, const QColor& background)
{
    QColor best = background;
    int bestDistance = -1;
    for (int y = area.top(); y <= area.bottom(); ++y)
    {
        for (int x = area.left(); x <= area.right(); ++x)
        {
            const QColor color = pixel(image, x, y);
            const int distance = colorDistance(color, background);
            if (distance > bestDistance)
            {
                bestDistance = distance;
                best = color;
            }
        }
    }
    return best;
}

int FluentComponentsTest::colorDistance(const QColor& first, const QColor& second)
{
    return qAbs(first.red() - second.red()) + qAbs(first.green() - second.green()) + qAbs(first.blue() - second.blue());
}

/// Contents of the rows of the step 2 as the default variants of the design
class RowSamples
{
public:
    RowSamples() = delete;

    static Painter::LinkRow linkRow() { return Painter::LinkRow{ QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"), std::nullopt }; }

    static Painter::RecentFileRow recentFileRow()
    {
        Painter::RecentFileRow row;
        row.fileName = QStringLiteral("Project_Report.pdf");
        row.path = QStringLiteral("C:\\Users\\Jakub\\Documents");
        row.time = QStringLiteral("Today 10:24");
        return row;
    }

    static Painter::RecentToolRow recentToolRow() { return Painter::RecentToolRow{ QStringLiteral("Compress"), QStringLiteral("Today 11:02"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Amber }; }

    static Painter::WorkspaceRow workspaceRow()
    {
        Painter::WorkspaceRow row;
        row.name = QStringLiteral("Thesis review");
        row.meta = QStringLiteral("4 documents · Opened Yesterday 16:15");
        return row;
    }

    static Painter::ToolRow toolRow()
    {
        Painter::ToolRow row;
        row.name = QStringLiteral("Merge PDF");
        row.description = QStringLiteral("Combine multiple PDFs into one file");
        row.iconName = QStringLiteral("grid");
        return row;
    }

    static Painter::CollectionRow collectionRow()
    {
        Painter::CollectionRow row;
        row.name = QStringLiteral("Prepare for sharing");
        row.meta = QStringLiteral("3 tools · Compress, Sanitize, Sign");
        return row;
    }

    static Painter::ToolShortcut toolShortcut() { return Painter::ToolShortcut{ QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue }; }

    static Painter::IssueRow issueRow()
    {
        Painter::IssueRow row;
        row.number = QStringLiteral("#612");
        row.title = QStringLiteral("Crash when opening some encrypted documents");
        row.description = QStringLiteral("Documents protected with AES-256 and an empty user password now open correctly.");
        row.date = QStringLiteral("Sep 12, 2026");
        return row;
    }

    static Painter::VersionRow versionRow()
    {
        Painter::VersionRow row;
        row.version = QStringLiteral("v1.5.3");
        row.date = QStringLiteral("Sep 14, 2026");
        row.summary = QStringLiteral("One unified app, faster loading, timestamped signatures.");
        return row;
    }

    static Painter::PropertyRow propertyRow(bool editable, Painter::PropertyState state)
    {
        Painter::PropertyRow row;
        row.key = QStringLiteral("Author");
        row.value = QStringLiteral("Jane Novak");
        row.editable = editable;
        row.state = state;
        return row;
    }
};

void FluentComponentsTest::rowSizes()
{
    // Sizes of the default variants of the cards
    QCOMPARE(Painter::getLinkRowSize(RowSamples::linkRow()), QSize(328, 50));
    QCOMPARE(Painter::getRecentFileRowSize(RowSamples::recentFileRow()), QSize(381, 51));
    QCOMPARE(Painter::getRecentToolRowSize(RowSamples::recentToolRow()), QSize(342, 48));
    QCOMPARE(Painter::getWorkspaceRowSize(RowSamples::workspaceRow()), QSize(344, 52));
    QCOMPARE(Painter::getToolRowSize(RowSamples::toolRow()), QSize(380, 48));
    QCOMPARE(Painter::getCollectionRowSize(RowSamples::collectionRow()), QSize(324, 52));
    QCOMPARE(Painter::getToolShortcutSize(RowSamples::toolShortcut()), QSize(180, 48));
    QCOMPARE(Painter::getIssueRowSize(RowSamples::issueRow()), QSize(786, 52));
    QCOMPARE(Painter::getVersionRowSize(RowSamples::versionRow()), QSize(324, 60));
    QCOMPARE(Painter::getPropertyRowSize(RowSamples::propertyRow(false, Painter::PropertyState::Default)), QSize(326, 30));
    QCOMPARE(Painter::getToolShortcutRadius(), 10.0);
    QCOMPARE(Painter::getMoreButtonSize(), QSize(16, 16));

    // The texts of a link row are not elided: a long title widens the row
    Painter::LinkRow longLink = RowSamples::linkRow();
    longLink.title = QStringLiteral("Report an issue of the application on the page of the project at GitHub");
    const int titleWidth = qCeil(pdf::PDFStylePainter::textWidth(longLink.title, pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold)));
    QCOMPARE(Painter::getLinkRowMinimumSize(longLink).width(), 10 + 16 + 12 + titleWidth + 12 + 14 + 10);
    QCOMPARE(Painter::getLinkRowSize(longLink).width(), Painter::getLinkRowMinimumSize(longLink).width());

    // The summary of a version row wraps, the row grows; without a summary it has the top line only
    Painter::VersionRow version = RowSamples::versionRow();
    QCOMPARE(Painter::getVersionRowHeight(version, 324), 60);
    QVERIFY(Painter::getVersionRowHeight(version, 200) > 60);
    QCOMPARE(Painter::getVersionRowHeight(version, 2000), 46);
    version.summary.clear();
    QCOMPARE(Painter::getVersionRowHeight(version, 324), 30);

    // Widgets have the sizes of the painter and place the nested widgets as the cards
    pdf::PDFLinkRow linkRow(QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"));
    QCOMPARE(linkRow.sizeHint(), QSize(328, 50));
    linkRow.setTitle(longLink.title);
    QCOMPARE(linkRow.sizeHint(), Painter::getLinkRowSize(longLink));
    QCOMPARE(linkRow.minimumSizeHint(), Painter::getLinkRowMinimumSize(longLink));

    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents"), QStringLiteral("Today 10:24"));
    QCOMPARE(fileRow.sizeHint(), QSize(381, 51));
    fileRow.resize(fileRow.sizeHint());
    fileRow.show();
    QCOMPARE(fileRow.getStarButton()->geometry(), QRect(317, 13, 24, 24));
    QCOMPARE(fileRow.getMenuButton()->geometry(), QRect(353, 17, 16, 16));
    QCOMPARE(fileRow.getMenuButton()->property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 4.0);

    pdf::PDFRecentToolRow recentToolRow(QStringLiteral("Compress"), QStringLiteral("Today 11:02"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Amber);
    QCOMPARE(recentToolRow.sizeHint(), QSize(342, 48));

    pdf::PDFWorkspaceRow workspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents"));
    QCOMPARE(workspaceRow.sizeHint(), QSize(344, 52));
    workspaceRow.resize(workspaceRow.sizeHint());
    workspaceRow.show();
    QCOMPARE(workspaceRow.getMenuButton()->geometry(), QRect(320, 18, 16, 16));
    QCOMPARE(workspaceRow.getHue(), pdf::PDFAccentHue::Violet);
    QCOMPARE(workspaceRow.getIconName(), QStringLiteral("layers"));

    pdf::PDFToolRow toolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine multiple PDFs into one file"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(toolRow.sizeHint(), QSize(380, 48));
    toolRow.resize(toolRow.sizeHint());
    toolRow.show();
    QCOMPARE(toolRow.getStarButton()->geometry(), QRect(346, 10, 28, 28));
    QVERIFY(toolRow.getTagWidget()->isHidden());
    toolRow.setTag(QStringLiteral("Advanced"));
    QVERIFY(!toolRow.getTagWidget()->isHidden());
    const int tagWidth = Painter::getTagSize(Painter::Tag{ QStringLiteral("Advanced") }).width();
    QCOMPARE(toolRow.getTagWidget()->geometry(), QRect(346 - 10 - tagWidth, 13, tagWidth, 21));
    QCOMPARE(toolRow.getStarButton()->geometry(), QRect(346, 10, 28, 28));
    toolRow.setTag(QString());
    QVERIFY(toolRow.getTagWidget()->isHidden());

    pdf::PDFCollectionRow collectionRow(QStringLiteral("Prepare for sharing"), QStringLiteral("3 tools"));
    QCOMPARE(collectionRow.sizeHint(), QSize(324, 52));

    pdf::PDFToolShortcut shortcut(QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(shortcut.sizeHint(), QSize(180, 48));
    QCOMPARE(shortcut.getFocusRing(), pdf::PDFComponentButton::FocusRing::Outside);
    QCOMPARE(shortcut.property(pdf::PDFWidgetStyleProperties::RADIUS).toReal(), 10.0);

    pdf::PDFIssueRow issueRow(QStringLiteral("#612"), QStringLiteral("Crash"), QStringLiteral("Fixed"), QStringLiteral("Sep 12, 2026"));
    QCOMPARE(issueRow.sizeHint(), QSize(786, 52));
    QCOMPARE(issueRow.getStatusIconName(), QStringLiteral("check"));

    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14, 2026"), RowSamples::versionRow().summary);
    QCOMPARE(versionRow.sizeHint(), QSize(324, 60));
    QVERIFY(versionRow.hasHeightForWidth());
    QVERIFY(versionRow.sizePolicy().hasHeightForWidth());
    QCOMPARE(versionRow.heightForWidth(324), 60);
    QVERIFY(versionRow.heightForWidth(150) > 60);

    // Property row: copy in the reserved place, editable edit + copy, editing confirm + cancel
    pdf::PDFPropertyRow propertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"));
    QCOMPARE(propertyRow.sizeHint(), QSize(326, 30));
    propertyRow.resize(propertyRow.sizeHint());
    propertyRow.show();
    QCOMPARE(propertyRow.getCopyButton()->geometry(), QRect(298, 3, 24, 24));
    propertyRow.setEditable(true);
    QCOMPARE(propertyRow.getEditButton()->geometry(), QRect(274, 3, 24, 24));
    QCOMPARE(propertyRow.getCopyButton()->geometry(), QRect(298, 3, 24, 24));
    QVERIFY(propertyRow.startEditing());
    QCOMPARE(propertyRow.getConfirmButton()->geometry(), QRect(274, 3, 24, 24));
    QCOMPARE(propertyRow.getCancelButton()->geometry(), QRect(298, 3, 24, 24));
    QCOMPARE(propertyRow.getEditor()->geometry(), QRect(150, 3, 116, 24));
    QCOMPARE(propertyRow.getConfirmButton()->getIconColor(), std::optional<Color>(Color::IconAccent));
}

void FluentComponentsTest::rowSizeStability()
{
    // Each row has the size of its variant Default in each state
    pdf::PDFLinkRow linkRow(QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"));
    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today 10:24"));
    pdf::PDFRecentToolRow recentToolRow(QStringLiteral("Compress"), QStringLiteral("Today"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Amber);
    pdf::PDFWorkspaceRow workspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents"));
    pdf::PDFToolRow toolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFCollectionRow collectionRow(QStringLiteral("Prepare"), QStringLiteral("3 tools"));
    pdf::PDFToolShortcut shortcut(QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    pdf::PDFIssueRow issueRow(QStringLiteral("#612"), QStringLiteral("Crash"), QStringLiteral("Fixed"), QStringLiteral("Sep 12"));
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"));
    pdf::PDFPropertyRow propertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable);

    for (QAbstractButton* button : std::initializer_list<QAbstractButton*>{ &linkRow, &fileRow, &recentToolRow, &workspaceRow, &toolRow, &collectionRow, &shortcut, &issueRow, &versionRow, &propertyRow })
    {
        const QSize size = button->sizeHint();
        button->setAttribute(Qt::WA_UnderMouse, true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(true);
        QCOMPARE(button->sizeHint(), size);
        button->setDown(false);
        button->setLayoutDirection(Qt::RightToLeft);
        QCOMPARE(button->sizeHint(), size);
        button->setEnabled(false);
        QCOMPARE(button->sizeHint(), size);
        button->setEnabled(true);
    }

    // Star, status and the states of the property row do not change the size
    const QSize fileSize = fileRow.sizeHint();
    fileRow.setPinned(true);
    fileRow.setStatus(pdf::PDFRecentFileRow::Status::Missing);
    QCOMPARE(fileRow.sizeHint(), fileSize);
    fileRow.setStarVisible(false);
    QCOMPARE(fileRow.sizeHint(), fileSize);

    const QSize propertySize = propertyRow.sizeHint();
    propertyRow.copy();
    QCOMPARE(propertyRow.sizeHint(), propertySize);
    propertyRow.startEditing();
    QCOMPARE(propertyRow.sizeHint(), propertySize);
    propertyRow.setEditable(false);
    QCOMPARE(propertyRow.sizeHint(), propertySize);

    // A change of the theme keeps the sizes
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    QCOMPARE(fileRow.sizeHint(), fileSize);
    QCOMPARE(shortcut.sizeHint(), QSize(180, 48));
}

void FluentComponentsTest::rowPixels_data()
{
    pixelProbes_data();
}

void FluentComponentsTest::rowPixels()
{
    QFETCH(QString, theme);
    QFETCH(qreal, scale);
    QVERIFY(pdf::PDFThemeManager::getInstance()->setTheme(theme));

    const QColor surface = token(Color::BgSurface);
    const QColor hover = blend(token(Color::BgHover), surface);
    const QColor pressed = blend(token(Color::BgPressed), surface);
    const QStyle::State none = QStyle::State_None;
    const QStyle::State over = QStyle::State_MouseOver;
    const QStyle::State down = QStyle::State_MouseOver | QStyle::State_Sunken;
    const QStyle::State focus = QStyle::State_HasFocus;

    // Nearer to the first token than to the second one
    auto isCloser = [](const QColor& color, const QColor& expected, const QColor& other)
    {
        return colorDistance(color, expected) < colorDistance(color, other);
    };

    // Link row: hover and press backgrounds, the focus ring inside, the chevron darker under the pointer
    const Painter::LinkRow link = RowSamples::linkRow();
    auto renderLink = [&](QStyle::State flags) { return render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), link, state(flags)); }, QSize(328, 50), scale); };
    QImage image = renderLink(none);
    QVERIFY(isNear(pixel(image, 20, 4), surface));
    QVERIFY(isCloser(strongestInk(image, QRect(304, 18, 14, 14), surface), token(Color::IconMuted), token(Color::IconDefault)));
    QVERIFY(isCloser(strongestInk(image, QRect(10, 17, 16, 16), surface), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderLink(over);
    QVERIFY(isNear(pixel(image, 20, 4), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(304, 18, 14, 14), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderLink(down);
    QVERIFY(isNear(pixel(image, 20, 4), pressed));
    image = renderLink(focus);
    QVERIFY(isNear(pixel(image, 160, 0), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 160, 1), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 160, 2), surface));
    image = render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), link, Painter::State(QStyle::State_MouseOver)); }, QSize(328, 50), scale);
    QVERIFY(isNear(pixel(image, 20, 4), surface));
    QCOMPARE(countPixels(image, QRect(38, 10, 120, 30), token(Color::TextPrimary), 8), 0);
    QVERIFY(countPixels(image, QRect(38, 10, 120, 30), token(Color::TextDisabled), 8) > 0);

    // Recent file row: border, hover with the strong border, focus ring, the badge of a missing file
    const Painter::RecentFileRow file = RowSamples::recentFileRow();
    auto renderFile = [&](const Painter::RecentFileRow& content, QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintRecentFileRow(&painter, QRectF(0, 0, 381, 51), content, state(flags)); }, QSize(381, 51), scale);
    };
    image = renderFile(file, none);
    QVERIFY(isNear(pixel(image, 190, 0), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 190, 1), surface));
    QVERIFY(isCloser(strongestInk(image, QRect(12, 14, 18, 22), surface), token(Color::IconPdf), token(Color::IconMuted)));
    QVERIFY(countPixels(image, QRect(235, 18, 70, 15), token(Color::TextTertiary), 12) > 0);
    image = renderFile(file, over);
    QVERIFY(isNear(pixel(image, 190, 0), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 190, 4), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(353, 17, 16, 16), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderFile(file, down);
    QVERIFY(isNear(pixel(image, 190, 4), pressed));
    image = renderFile(file, focus);
    QVERIFY(isNear(pixel(image, 190, 0), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 190, 1), token(Color::BorderFocus)));
    QVERIFY(isCloser(strongestInk(image, QRect(353, 17, 16, 16), surface), token(Color::IconMuted), token(Color::IconDefault)));

    Painter::RecentFileRow missing = file;
    missing.status = Painter::FileStatus::Missing;
    image = renderFile(missing, none);
    const int badgeWidth = qCeil(pdf::PDFStylePainter::textWidth(Painter::getMissingFileText(), Painter::getTagFont())) + 16;
    QVERIFY(isNear(pixel(image, 305 - badgeWidth / 2.0, 17), token(Color::AccentRedBg)));
    QVERIFY(countPixels(image, QRect(305 - badgeWidth, 16, badgeWidth, 19), token(Color::TextDanger), 12) > 0);
    QVERIFY(isCloser(strongestInk(image, QRect(12, 14, 18, 22), surface), token(Color::IconMuted), token(Color::IconPdf)));
    QCOMPARE(countPixels(image, QRect(42, 10, 100, 16), token(Color::TextPrimary), 6), 0);

    Painter::RecentFileRow pinned = file;
    pinned.pinned = true;
    image = renderFile(pinned, none);
    QVERIFY(isNear(pixel(image, 317 + 12, 13 + 12), token(Color::IconSun), 12));
    pinned.paintNested = false;
    image = renderFile(pinned, none);
    QVERIFY(isNear(pixel(image, 317 + 12, 13 + 12), surface));

    // Recent tool row and Workspace row: badge, chevron / menu under the pointer
    image = render([&](QPainter& painter) { Painter::paintRecentToolRow(&painter, QRectF(0, 0, 342, 48), RowSamples::recentToolRow(), state(over)); }, QSize(342, 48), scale);
    QVERIFY(isNear(pixel(image, 13, 24), token(Color::AccentAmberBg)));
    QVERIFY(isNear(pixel(image, 100, 3), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(316, 17, 14, 14), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = render([&](QPainter& painter) { Painter::paintWorkspaceRow(&painter, QRectF(0, 0, 344, 52), RowSamples::workspaceRow(), state(none)); }, QSize(344, 52), scale);
    QVERIFY(isNear(pixel(image, 13, 26), token(Color::AccentVioletBg)));
    QVERIFY(isCloser(strongestInk(image, QRect(320, 18, 16, 16), surface), token(Color::IconMuted), token(Color::IconDefault)));

    // Tool row: the star 28 at the trailing edge, the tag before it
    Painter::ToolRow tool = RowSamples::toolRow();
    tool.tag = QStringLiteral("Advanced");
    image = render([&](QPainter& painter) { Painter::paintToolRow(&painter, QRectF(0, 0, 380, 48), tool, state(down)); }, QSize(380, 48), scale);
    QVERIFY(isNear(pixel(image, 11, 24), token(Color::AccentBlueBg)));
    QVERIFY(isNear(pixel(image, 200, 3), pressed));
    const int toolTagWidth = Painter::getTagSize(Painter::Tag{ tool.tag }).width();
    QVERIFY(isNear(pixel(image, 336 - toolTagWidth / 2.0, 13), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 336 - toolTagWidth / 2.0, 15), token(Color::BgSubtle)));

    // Collection row: the circle of the fixed colors, the chevron keeps its color
    image = render([&](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), RowSamples::collectionRow(), state(over)); }, QSize(324, 52), scale);
    const QColor circle = blend(token(Color::BgAccentSubtle), hover);
    QVERIFY(isNear(pixel(image, 13, 26), circle));
    QVERIFY(isCloser(strongestInk(image, QRect(18, 18, 16, 16), circle), token(Color::IconAccent), token(Color::IconDefault)));
    QVERIFY(isCloser(strongestInk(image, QRect(302, 19, 14, 14), hover), token(Color::IconMuted), token(Color::IconDefault)));

    // Tool shortcut: surface and border, the neutral steps under the pointer, the focus ring outside without the border
    const Painter::ToolShortcut shortcut = RowSamples::toolShortcut();
    auto renderShortcut = [&](QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintToolShortcut(&painter, QRectF(4, 4, 180, 48), shortcut, state(flags)); }, QSize(188, 56), scale);
    };
    image = renderShortcut(none);
    QVERIFY(isNear(pixel(image, 94, 4), token(Color::BorderDefault)));
    QVERIFY(isNear(pixel(image, 94, 6), surface));
    image = renderShortcut(over);
    QVERIFY(isNear(pixel(image, 94, 4), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 94, 7), token(Color::AccentNeutralBgHover)));
    image = renderShortcut(down);
    QVERIFY(isNear(pixel(image, 94, 7), token(Color::AccentNeutralBgPressed)));
    image = renderShortcut(focus);
    QVERIFY(isNear(pixel(image, 94, 2), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 94, 3), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 94, 4), surface));
    image = render([&](QPainter& painter) { Painter::paintToolShortcut(&painter, QRectF(4, 4, 180, 48), shortcut, state(focus), false); }, QSize(188, 56), scale);
    QVERIFY(isNear(pixel(image, 94, 3), surface));
    QVERIFY(isNear(pixel(image, 94, 4), surface));

    // Issue row: the green status circle, the number text/accent
    image = render([&](QPainter& painter) { Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), RowSamples::issueRow(), state(none)); }, QSize(786, 52), scale);
    QVERIFY(isNear(pixel(image, 12, 26), token(Color::AccentGreenBg)));
    QVERIFY(countPixels(image, QRect(46, 18, 44, 16), token(Color::TextAccent), 12) > 0);
    QVERIFY(countPixels(image, QRect(690, 19, 86, 13), token(Color::TextMuted), 12) > 0);

    // Version row: the dot of the current version, the line, the hidden line
    Painter::VersionRow version = RowSamples::versionRow();
    auto renderVersion = [&](const Painter::VersionRow& content) { return render([&](QPainter& painter) { Painter::paintVersionRow(&painter, QRectF(0, 0, 324, 60), content, state(none)); }, QSize(324, 60), scale); };
    image = renderVersion(version);
    QVERIFY(isNear(pixel(image, 13, 9), token(Color::BgAccent)));
    QVERIFY(isNear(pixel(image, 13, 40), token(Color::BgTrack)));
    QVERIFY(countPixels(image, QRect(32, 0, 60, 16), token(Color::TextAccent), 12) > 0);
    QVERIFY(countPixels(image, QRect(32, 18, 284, 28), token(Color::TextTertiary), 12) > 0);
    version.current = false;
    version.lineVisible = false;
    image = renderVersion(version);
    QVERIFY(isNear(pixel(image, 13, 9), token(Color::BorderStrong)));
    QVERIFY(isNear(pixel(image, 13, 40), surface));
    QCOMPARE(countPixels(image, QRect(32, 0, 60, 16), token(Color::TextAccent), 12), 0);

    // Property row: the states of the design
    auto renderProperty = [&](const Painter::PropertyRow& content, QStyle::State flags)
    {
        return render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), content, state(flags)); }, QSize(326, 30), scale);
    };
    image = renderProperty(RowSamples::propertyRow(false, Painter::PropertyState::Default), none);
    QVERIFY(isNear(pixel(image, 5, 15), surface));
    QVERIFY(isNear(strongestInk(image, QRect(298, 3, 24, 24), surface), surface));
    image = renderProperty(RowSamples::propertyRow(false, Painter::PropertyState::Default), over);
    QVERIFY(isNear(pixel(image, 5, 15), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(303, 8, 14, 14), hover), token(Color::IconDefault), token(Color::IconMuted)));
    image = renderProperty(RowSamples::propertyRow(true, Painter::PropertyState::Default), none);
    QVERIFY(isCloser(strongestInk(image, QRect(279, 8, 14, 14), surface), token(Color::IconMuted), token(Color::IconDefault)));
    image = renderProperty(RowSamples::propertyRow(true, Painter::PropertyState::Copied), none);
    QVERIFY(isNear(pixel(image, 5, 15), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(303, 8, 14, 14), hover), token(Color::AccentGreenIcon), token(Color::IconDefault)));
    image = renderProperty(RowSamples::propertyRow(true, Painter::PropertyState::Editing), none);
    QVERIFY(isNear(pixel(image, 200, 3), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 200, 4), token(Color::BorderFocus)));
    QVERIFY(isNear(pixel(image, 262, 15), surface));
    QVERIFY(isNear(pixel(image, 146, 15), hover));
    QVERIFY(isCloser(strongestInk(image, QRect(279, 8, 14, 14), hover), token(Color::IconAccent), token(Color::IconMuted)));

    // No leak color anywhere
    QCOMPARE(countLeakPixels(image), 0);
}

void FluentComponentsTest::rowWidgetsMatchPainter()
{
    // The widgets with real nested widgets draw the same as the painter with the nested elements
    QWidget window;
    window.resize(820, 300);
    pdf::PDFRecentFileRow* fileRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users\\Jakub\\Documents"), QStringLiteral("Today 10:24"), &window);
    fileRow->setGeometry(0, 0, 381, 51);
    fileRow->setPinned(true);
    pdf::PDFToolRow* toolRow = new pdf::PDFToolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine multiple PDFs into one file"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    toolRow->setGeometry(0, 60, 380, 48);
    toolRow->setTag(QStringLiteral("Advanced"));
    pdf::PDFWorkspaceRow* workspaceRow = new pdf::PDFWorkspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents · Opened Yesterday 16:15"), &window);
    workspaceRow->setGeometry(0, 120, 344, 52);
    pdf::PDFPropertyRow* propertyRow = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable, &window);
    propertyRow->setGeometry(0, 180, 326, 30);
    window.setFocusPolicy(Qt::StrongFocus);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.setFocus(Qt::OtherFocusReason);
    QVERIFY(!fileRow->hasFocus());

    for (const qreal scale : { 1.0, 1.5, 2.0 })
    {
        // At the scale 1.5 a nested widget at an odd logical position starts in
        // the middle of a device pixel, Qt clips it to whole pixels: the
        // antialiased edges of the nested widgets differ slightly
        const int tolerance = qFuzzyCompare(scale, 1.5) ? 24 : 3;

        for (const bool hovered : { false, true })
        {
            for (QWidget* row : std::initializer_list<QWidget*>{ fileRow, toolRow, workspaceRow, propertyRow })
            {
                row->setAttribute(Qt::WA_UnderMouse, hovered);
            }

            const QStyle::State flags = hovered ? QStyle::State_MouseOver : QStyle::State_None;
            const QString prefix = QStringLiteral("scale %1, hovered %2").arg(scale).arg(hovered);

            Painter::RecentFileRow file = fileRow->getContent();
            file.paintNested = true;
            QString difference = describeDifference(renderWidget(fileRow, scale), render([&](QPainter& painter) { Painter::paintRecentFileRow(&painter, QRectF(0, 0, 381, 51), file, state(flags)); }, QSize(381, 51), scale), tolerance);
            QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Recent file row (%1): %2").arg(prefix, difference)));

            Painter::ToolRow tool = toolRow->getContent();
            tool.paintNested = true;
            difference = describeDifference(renderWidget(toolRow, scale), render([&](QPainter& painter) { Painter::paintToolRow(&painter, QRectF(0, 0, 380, 48), tool, state(flags)); }, QSize(380, 48), scale), tolerance);
            QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Tool row (%1): %2").arg(prefix, difference)));

            Painter::WorkspaceRow workspace = workspaceRow->getContent();
            workspace.paintNested = true;
            difference = describeDifference(renderWidget(workspaceRow, scale), render([&](QPainter& painter) { Painter::paintWorkspaceRow(&painter, QRectF(0, 0, 344, 52), workspace, state(flags)); }, QSize(344, 52), scale), tolerance);
            QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Workspace row (%1): %2").arg(prefix, difference)));

            Painter::PropertyRow property = propertyRow->getContent();
            QCOMPARE(property.state, hovered ? Painter::PropertyState::Hover : Painter::PropertyState::Default);
            property.paintNested = true;
            difference = describeDifference(renderWidget(propertyRow, scale), render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), property, state(QStyle::State_None)); }, QSize(326, 30), scale), tolerance);
            QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Property row (%1): %2").arg(prefix, difference)));
        }

        // Copied: the check in the place of the copy button
        propertyRow->setAttribute(Qt::WA_UnderMouse, false);
        propertyRow->setCopiedDuration(60000);
        propertyRow->copy();
        Painter::PropertyRow copied = propertyRow->getContent();
        QCOMPARE(copied.state, Painter::PropertyState::Copied);
        copied.paintNested = true;
        QString difference = describeDifference(renderWidget(propertyRow, scale), render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), copied, state(QStyle::State_None)); }, QSize(326, 30), scale), tolerance);
        QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Property row copied (scale %1): %2").arg(scale).arg(difference)));
        propertyRow->hide();
        propertyRow->show();
        QVERIFY(!propertyRow->isCopied());

        // Editing: the frame and the actions confirm and cancel (the text is the field)
        QVERIFY(propertyRow->startEditing());
        Painter::PropertyRow editing = propertyRow->getContent();
        QCOMPARE(editing.state, Painter::PropertyState::Editing);
        editing.paintNested = true;
        editing.value.clear();
        QImage widgetImage = renderWidget(propertyRow, scale);
        const QImage painterImage = render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), editing, state(QStyle::State_None)); }, QSize(326, 30), scale);
        difference = describeDifference(widgetImage.copy(QRect(0, 0, qRound(140 * scale), qRound(30 * scale))), painterImage.copy(QRect(0, 0, qRound(140 * scale), qRound(30 * scale))), tolerance);
        QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Property row editing, key (scale %1): %2").arg(scale).arg(difference)));
        difference = describeDifference(widgetImage.copy(QRect(qRound(270 * scale), 0, qRound(56 * scale), qRound(30 * scale))), painterImage.copy(QRect(qRound(270 * scale), 0, qRound(56 * scale), qRound(30 * scale))), tolerance);
        QVERIFY2(difference.isEmpty(), qPrintable(QStringLiteral("Property row editing, actions (scale %1): %2").arg(scale).arg(difference)));
        propertyRow->cancelEditing();

        // No leak color anywhere
        QCOMPARE(countLeakPixels(renderWidget(&window, scale)), 0);
    }
}

void FluentComponentsTest::fileNameElision()
{
    const QFont font = pdf::PDFThemeFonts::getFont(pdf::PDFThemeFonts::Role::BodySemiBold);
    const QString name = QStringLiteral("Technical_Documentation_v2.pdf");
    const qreal fullWidth = pdf::PDFStylePainter::textWidth(name, font);
    const QChar ellipsis(0x2026);

    // A name, which fits, is kept
    QCOMPARE(Painter::elidedFileName(name, font, fullWidth + 1.0), name);

    // A longer name is elided in the middle and keeps its extension
    for (const qreal width : { fullWidth - 10.0, fullWidth / 2.0, fullWidth / 3.0 })
    {
        const QString elided = Painter::elidedFileName(name, font, width);
        QVERIFY2(elided.endsWith(QStringLiteral(".pdf")), qPrintable(elided));
        QVERIFY2(elided.contains(ellipsis), qPrintable(elided));
        QVERIFY2(elided.startsWith(QStringLiteral("Te")), qPrintable(elided));
        QVERIFY2(pdf::PDFStylePainter::textWidth(elided, font) <= width + 1.0, qPrintable(elided));
    }
    QVERIFY(Painter::elidedFileName(name, font, fullWidth / 2.0).contains(QStringLiteral("v2.pdf")));

    // Not even the extension fits: the name is elided at its end, it never overflows
    const qreal tinyWidth = pdf::PDFStylePainter::textWidth(QStringLiteral(".pdf"), font);
    QVERIFY(pdf::PDFStylePainter::textWidth(Painter::elidedFileName(name, font, tinyWidth), font) <= tinyWidth + 1.0);

    // Names without an extension are elided at their end
    const QFontMetricsF metrics(font);
    for (const QString& plain : { QStringLiteral("README_OF_THE_PROJECT_WITHOUT_EXTENSION"), QStringLiteral(".profile_of_the_user_of_the_application"), QStringLiteral("folder.v2\\file_of_the_application_long"), QStringLiteral("name_ending_with_a_dot_of_the_file.") })
    {
        QCOMPARE(Painter::elidedFileName(plain, font, 80.0), metrics.elidedText(plain, Qt::ElideRight, 80.0));
    }

    // Defensive inputs
    QCOMPARE(Painter::elidedFileName(QString(), font, 100.0), QString());
    QCOMPARE(Painter::elidedFileName(name, font, 0.0), QString());
    QCOMPARE(Painter::elidedFileName(name, font, -5.0), QString());
    QCOMPARE(Painter::elidedFileName(name, font, qQNaN()), QString());

    // The widget keeps the full name (accessible name), only the drawing is elided
    pdf::PDFRecentFileRow row(name, QStringLiteral("C:\\Users"), QStringLiteral("Today"));
    row.resize(200, 51);
    QCOMPARE(row.getFileName(), name);
    QCOMPARE(QAccessible::queryAccessibleInterface(&row)->text(QAccessible::Name), name);
    QCOMPARE(countLeakPixels(row.grab().toImage()), 0);
}

void FluentComponentsTest::rowNestedWidgets()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFRecentFileRow* fileRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    pdf::PDFToolRow* toolRow = new pdf::PDFToolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    pdf::PDFWorkspaceRow* workspaceRow = new pdf::PDFWorkspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents"), &window);
    layout->addWidget(fileRow);
    layout->addWidget(toolRow);
    layout->addWidget(workspaceRow);
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));
    fileRow->setMenu(&menu);
    QCOMPARE(fileRow->getMenu(), &menu);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QSignalSpy fileClicked(fileRow, &QAbstractButton::clicked);
    QSignalSpy filePinned(fileRow, &pdf::PDFRecentFileRow::pinnedChanged);
    QSignalSpy fileMenu(fileRow, &pdf::PDFRecentFileRow::menuRequested);

    // The star pins the file, the row is not activated
    QTest::mouseClick(fileRow->getStarButton(), Qt::LeftButton);
    QCOMPARE(filePinned.count(), 1);
    QVERIFY(fileRow->isPinned());
    QCOMPARE(fileClicked.count(), 0);

    // The signal only on a real change
    fileRow->setPinned(true);
    QCOMPARE(filePinned.count(), 1);
    fileRow->setPinned(false);
    QCOMPARE(filePinned.count(), 2);
    QCOMPARE(filePinned.back().front().toBool(), false);

    // The menu button shows the menu (after the signal, whose slot may fill it), the row is not activated
    bool isMenuVisibleInSlot = true;
    QMetaObject::Connection connection = connect(fileRow, &pdf::PDFRecentFileRow::menuRequested, fileRow, [&]() { isMenuVisibleInSlot = menu.isVisible(); });
    QTest::mouseClick(fileRow->getMenuButton(), Qt::LeftButton);
    QCOMPARE(fileMenu.count(), 1);
    QVERIFY(!isMenuVisibleInSlot);
    disconnect(connection);
    QCOMPARE(fileClicked.count(), 0);
    QTRY_VERIFY(menu.isVisible());
    menu.hide();

    // A right click shows the same menu
    const QPoint inside(100, 25);
    QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, inside, fileRow->mapToGlobal(inside));
    QApplication::sendEvent(fileRow, &contextEvent);
    QCOMPARE(fileMenu.count(), 2);
    QTRY_VERIFY(menu.isVisible());
    menu.hide();

    // A deleted menu is forgotten
    {
        QMenu* temporaryMenu = new QMenu();
        workspaceRow->setMenu(temporaryMenu);
        delete temporaryMenu;
        QCOMPARE(workspaceRow->getMenu(), nullptr);
        QVERIFY(!workspaceRow->getMenuButton()->showMenu());
    }

    // A disabled nested button does not activate the row
    fileRow->getStarButton()->setEnabled(false);
    QTest::mouseClick(fileRow->getStarButton(), Qt::LeftButton);
    QCOMPARE(fileClicked.count(), 0);
    QCOMPARE(filePinned.count(), 2);
    fileRow->getStarButton()->setEnabled(true);

    // The row itself is activated elsewhere
    QTest::mouseClick(fileRow, Qt::LeftButton, Qt::NoModifier, QPoint(100, 25));
    QCOMPARE(fileClicked.count(), 1);

    // Hidden star: the row does not place it
    fileRow->setStarVisible(false);
    QVERIFY(fileRow->getStarButton()->isHidden());
    fileRow->setStarVisible(true);
    QVERIFY(!fileRow->getStarButton()->isHidden());

    // Tool row: the star, the tag is a label (the mouse passes to the row)
    QSignalSpy toolClicked(toolRow, &QAbstractButton::clicked);
    QSignalSpy toolPinned(toolRow, &pdf::PDFToolRow::pinnedChanged);
    QTest::mouseClick(toolRow->getStarButton(), Qt::LeftButton);
    QCOMPARE(toolPinned.count(), 1);
    QCOMPARE(toolClicked.count(), 0);
    toolRow->setTag(QStringLiteral("Advanced"));
    QApplication::processEvents();
    QTest::mouseClick(toolRow, Qt::LeftButton, Qt::NoModifier, toolRow->getTagWidget()->geometry().center());
    QCOMPARE(toolClicked.count(), 1);

    // Workspace row: the menu button
    QSignalSpy workspaceClicked(workspaceRow, &QAbstractButton::clicked);
    QSignalSpy workspaceMenu(workspaceRow, &pdf::PDFWorkspaceRow::menuRequested);
    QTest::mouseClick(workspaceRow->getMenuButton(), Qt::LeftButton);
    QCOMPARE(workspaceMenu.count(), 1);
    QCOMPARE(workspaceClicked.count(), 0);

    // The menu button is darker under the pointer of its row
    fileRow->setAttribute(Qt::WA_UnderMouse, true);
    QVERIFY(fileRow->getComponentState().isHovered());
    const QImage hovered = fileRow->getMenuButton()->grab().toImage();
    fileRow->setAttribute(Qt::WA_UnderMouse, false);
    const QImage normal = fileRow->getMenuButton()->grab().toImage();
    QVERIFY(hovered != normal);
}

void FluentComponentsTest::rowKeyboard()
{
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    pdf::PDFRecentFileRow* fileRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    pdf::PDFToolShortcut* shortcut = new pdf::PDFToolShortcut(QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    pdf::PDFVersionRow* versionRow = new pdf::PDFVersionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"), &window);
    pdf::PDFPropertyRow* propertyRow = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable, &window);
    layout->addWidget(fileRow);
    layout->addWidget(shortcut);
    layout->addWidget(versionRow);
    layout->addWidget(propertyRow);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QSignalSpy fileClicked(fileRow, &QAbstractButton::clicked);
    QSignalSpy filePinned(fileRow, &pdf::PDFRecentFileRow::pinnedChanged);
    QSignalSpy fileMenu(fileRow, &pdf::PDFRecentFileRow::menuRequested);

    // The row is activated by Enter and Space
    fileRow->setFocus(Qt::TabFocusReason);
    QTest::keyClick(fileRow, Qt::Key_Return);
    QTest::keyClick(fileRow, Qt::Key_Space);
    QCOMPARE(fileClicked.count(), 2);

    // Tab reaches the star and the menu button, which do not activate the row
    QTest::keyClick(fileRow, Qt::Key_Tab);
    QTRY_VERIFY(fileRow->getStarButton()->hasFocus());
    QTest::keyClick(fileRow->getStarButton(), Qt::Key_Space);
    QCOMPARE(filePinned.count(), 1);
    QTest::keyClick(fileRow->getStarButton(), Qt::Key_Tab);
    QTRY_VERIFY(fileRow->getMenuButton()->hasFocus());
    QTest::keyClick(fileRow->getMenuButton(), Qt::Key_Return);
    QCOMPARE(fileMenu.count(), 1);
    QCOMPARE(fileClicked.count(), 2);

    // Back to the row by Shift+Tab: its focus ring is inside
    QTest::keyClick(fileRow->getMenuButton(), Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(fileRow->getStarButton()->hasFocus());
    QTest::keyClick(fileRow->getStarButton(), Qt::Key_Tab, Qt::ShiftModifier);
    QTRY_VERIFY(fileRow->hasFocus());
    QVERIFY(fileRow->getComponentState().hasFocusRing());

    // The shortcut is activated by Enter, its ring lies outside (drawn by the style)
    QSignalSpy shortcutClicked(shortcut, &QAbstractButton::clicked);
    shortcut->setFocus(Qt::TabFocusReason);
    QTest::keyClick(shortcut, Qt::Key_Enter, Qt::KeypadModifier);
    QCOMPARE(shortcutClicked.count(), 1);
    QVERIFY(!shortcut->getComponentState().hasFocusRing());

    // The version row and the property row are texts without the focus; the actions of the property row take it
    QCOMPARE(versionRow->focusPolicy(), Qt::NoFocus);
    QCOMPARE(propertyRow->focusPolicy(), Qt::NoFocus);
    QCOMPARE(propertyRow->getCopyButton()->focusPolicy(), Qt::TabFocus);
    QCOMPARE(propertyRow->getEditButton()->focusPolicy(), Qt::TabFocus);
    QSignalSpy versionClicked(versionRow, &QAbstractButton::clicked);
    QTest::mouseClick(versionRow, Qt::LeftButton);
    QCOMPARE(versionClicked.count(), 0);

    // The copy button is shown, when it has the keyboard focus
    const QColor surface = token(Color::BgSurface);
    auto renderCopy = [&]()
    {
        QImage image(QSize(24, 24), QImage::Format_ARGB32_Premultiplied);
        image.fill(surface);
        QPainter painter(&image);
        propertyRow->getCopyButton()->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
        painter.end();
        return image;
    };
    QCOMPARE(colorDistance(strongestInk(renderCopy(), QRect(0, 0, 24, 24), surface), surface), 0);
    propertyRow->getCopyButton()->setFocus(Qt::TabFocusReason);
    QVERIFY(propertyRow->getCopyButton()->hasFocus());
    QVERIFY(colorDistance(strongestInk(renderCopy(), QRect(5, 5, 14, 14), surface), surface) > 100);
}

void FluentComponentsTest::rowAccessibility()
{
    // Rows are buttons named by their titles and described by their subtitles
    pdf::PDFLinkRow linkRow(QStringLiteral("Report Issue"), QStringLiteral("Tell us about a problem"), QStringLiteral("alert-circle"));
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(&linkRow);
    QCOMPARE(accessible->role(), QAccessible::Button);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Report Issue"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Tell us about a problem"));
    linkRow.setSubtitle(QStringLiteral("Other"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Other"));

    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Docs"), QStringLiteral("Today"));
    accessible = QAccessible::queryAccessibleInterface(&fileRow);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Project_Report.pdf"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Today"));
    fileRow.setStatus(pdf::PDFRecentFileRow::Status::Missing);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("No longer exists, C:\\Docs"));
    fileRow.setStatus(pdf::PDFRecentFileRow::Status::Available);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Today"));
    fileRow.setTime(QString());
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs"));
    fileRow.setTime(QStringLiteral("Yesterday"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("C:\\Docs, Yesterday"));
    fileRow.setAccessibleDescription(QStringLiteral("Application description"));
    fileRow.setPath(QStringLiteral("D:\\"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Application description"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fileRow.getStarButton())->text(QAccessible::Name), QStringLiteral("Pin file"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fileRow.getMenuButton())->text(QAccessible::Name), QStringLiteral("More actions"));
    QCOMPARE(QAccessible::queryAccessibleInterface(fileRow.getMenuButton())->role(), QAccessible::Button);
    QCOMPARE(fileRow.getMenuButton()->toolTip(), QStringLiteral("More actions"));

    // The menu button opens a popup, when it has a menu
    QVERIFY(!QAccessible::queryAccessibleInterface(fileRow.getMenuButton())->state().hasPopup);
    QMenu menu;
    fileRow.setMenu(&menu);
    QVERIFY(QAccessible::queryAccessibleInterface(fileRow.getMenuButton())->state().hasPopup);
    fileRow.setMenu(nullptr);

    // The ampersand of a name is data
    pdf::PDFToolRow toolRow(QStringLiteral("Protect & Sign"), QStringLiteral("Passwords"), QStringLiteral("lock"), pdf::PDFAccentHue::Red);
    QCOMPARE(toolRow.shortcut(), QKeySequence());
    QCOMPARE(QAccessible::queryAccessibleInterface(&toolRow)->text(QAccessible::Name), QStringLiteral("Protect & Sign"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&toolRow)->text(QAccessible::Description), QStringLiteral("Passwords"));
    toolRow.setTag(QStringLiteral("Advanced"));
    QCOMPARE(QAccessible::queryAccessibleInterface(toolRow.getTagWidget())->role(), QAccessible::StaticText);

    pdf::PDFRecentToolRow recentToolRow(QStringLiteral("Compress"), QStringLiteral("Today 11:02"), QStringLiteral("minimize-2"), pdf::PDFAccentHue::Violet);
    QCOMPARE(QAccessible::queryAccessibleInterface(&recentToolRow)->text(QAccessible::Description), QStringLiteral("Today 11:02"));
    pdf::PDFWorkspaceRow workspaceRow(QStringLiteral("Thesis"), QStringLiteral("4 documents"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&workspaceRow)->text(QAccessible::Description), QStringLiteral("4 documents"));
    pdf::PDFCollectionRow collectionRow(QStringLiteral("Prepare"), QStringLiteral("3 tools"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&collectionRow)->text(QAccessible::Description), QStringLiteral("3 tools"));
    pdf::PDFToolShortcut shortcut(QStringLiteral("Merge PDF"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    QCOMPARE(QAccessible::queryAccessibleInterface(&shortcut)->text(QAccessible::Name), QStringLiteral("Merge PDF"));

    // Issue row: the number and the title
    pdf::PDFIssueRow issueRow(QStringLiteral("#612"), QStringLiteral("Crash when opening"), QStringLiteral("Fixed"), QStringLiteral("Sep 12"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&issueRow)->text(QAccessible::Name), QStringLiteral("#612 Crash when opening"));
    QCOMPARE(QAccessible::queryAccessibleInterface(&issueRow)->text(QAccessible::Description), QStringLiteral("Fixed, Sep 12"));
    issueRow.setDate(QString());
    QCOMPARE(QAccessible::queryAccessibleInterface(&issueRow)->text(QAccessible::Description), QStringLiteral("Fixed"));
    issueRow.setNumber(QString());
    QCOMPARE(QAccessible::queryAccessibleInterface(&issueRow)->text(QAccessible::Name), QStringLiteral("Crash when opening"));

    // Version row: a text with the date and the summary as its description, without actions
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14, 2026"), QStringLiteral("One unified app."));
    accessible = QAccessible::queryAccessibleInterface(&versionRow);
    QCOMPARE(accessible->role(), QAccessible::StaticText);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("v1.5.3"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Current version. Sep 14, 2026. One unified app."));
    versionRow.setCurrent(false);
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Sep 14, 2026. One unified app."));
    if (QAccessibleActionInterface* actions = accessible->actionInterface())
    {
        QVERIFY(!actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    }

    // Property row: a text, the buttons named by their actions
    pdf::PDFPropertyRow propertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable);
    accessible = QAccessible::queryAccessibleInterface(&propertyRow);
    QCOMPARE(accessible->role(), QAccessible::StaticText);
    QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Author"));
    QCOMPARE(accessible->text(QAccessible::Description), QStringLiteral("Jane Novak"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getCopyButton())->text(QAccessible::Name), QStringLiteral("Copy"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getEditButton())->text(QAccessible::Name), QStringLiteral("Edit"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getConfirmButton())->text(QAccessible::Name), QStringLiteral("Confirm"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getCancelButton())->text(QAccessible::Name), QStringLiteral("Cancel"));
    QCOMPARE(propertyRow.getEditButton()->toolTip(), QStringLiteral("Edit"));
    propertyRow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&propertyRow));
    propertyRow.setCopiedDuration(60000);
    propertyRow.copy();
    QCOMPARE(propertyRow.getCopyButton()->toolTip(), QStringLiteral("Copied"));
    QCOMPARE(QAccessible::queryAccessibleInterface(propertyRow.getCopyButton())->text(QAccessible::Name), QStringLiteral("Copied"));
    QCOMPARE(propertyRow.getEditor()->accessibleName(), QStringLiteral("Author"));
}

void FluentComponentsTest::propertyRowEditing()
{
    // In a dialog: Enter and Esc of the field do not go to the dialog
    QDialog dialog;
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    pdf::PDFPropertyRow* row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable, &dialog);
    QPushButton* okButton = new QPushButton(QStringLiteral("OK"), &dialog);
    okButton->setDefault(true);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(row);
    layout->addWidget(okButton);
    dialog.show();
    dialog.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&dialog));

    QSignalSpy edited(row, &pdf::PDFPropertyRow::edited);
    QSignalSpy editingChanged(row, &pdf::PDFPropertyRow::editingChanged);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QSignalSpy rejected(&dialog, &QDialog::rejected);

    QVERIFY(row->getEditButton()->isVisibleTo(row));
    QVERIFY(row->getCopyButton()->isVisibleTo(row));
    QVERIFY(!row->getConfirmButton()->isVisibleTo(row));
    QVERIFY(!row->getEditor()->isVisibleTo(row));
    QCOMPARE(row->getPropertyState(), Painter::PropertyState::Default);

    // The edit button starts the editing: the field has the value and the focus
    QTest::mouseClick(row->getEditButton(), Qt::LeftButton);
    QVERIFY(row->isEditing());
    QCOMPARE(row->getPropertyState(), Painter::PropertyState::Editing);
    QCOMPARE(editingChanged.count(), 1);
    QCOMPARE(editingChanged.back().front().toBool(), true);
    QVERIFY(row->getEditor()->isVisibleTo(row));
    QCOMPARE(row->getEditor()->text(), QStringLiteral("Jane Novak"));
    QTRY_VERIFY(row->getEditor()->hasFocus());
    QVERIFY(row->getConfirmButton()->isVisibleTo(row));
    QVERIFY(row->getCancelButton()->isVisibleTo(row));
    QVERIFY(!row->getEditButton()->isVisibleTo(row));
    QVERIFY(!row->getCopyButton()->isVisibleTo(row));
    QVERIFY(row->startEditing());
    QCOMPARE(editingChanged.count(), 1);

    // Enter with an unchanged value: no signal edited, the dialog is not accepted
    QTest::keyClick(row->getEditor(), Qt::Key_Return);
    QVERIFY(!row->isEditing());
    QCOMPARE(edited.count(), 0);
    QCOMPARE(editingChanged.count(), 2);
    QCOMPARE(accepted.count(), 0);
    QVERIFY(dialog.isVisible());
    QTRY_VERIFY(row->getEditButton()->hasFocus());

    // Enter of the keypad confirms a changed value, the signal follows the change
    QString valueInSlot;
    QMetaObject::Connection connection = connect(row, &pdf::PDFPropertyRow::edited, row, [&]() { valueInSlot = row->getValue(); });
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("John Doe"));
    QTest::keyClick(row->getEditor(), Qt::Key_Enter, Qt::KeypadModifier);
    QCOMPARE(edited.count(), 1);
    QCOMPARE(edited.back().front().toString(), QStringLiteral("John Doe"));
    QCOMPARE(row->getValue(), QStringLiteral("John Doe"));
    QCOMPARE(valueInSlot, QStringLiteral("John Doe"));
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(QAccessible::queryAccessibleInterface(row)->text(QAccessible::Description), QStringLiteral("John Doe"));
    disconnect(connection);

    // Esc cancels, the dialog is not rejected
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("Discarded"));
    QTest::keyClick(row->getEditor(), Qt::Key_Escape);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("John Doe"));
    QCOMPARE(edited.count(), 1);
    QCOMPARE(rejected.count(), 0);
    QVERIFY(dialog.isVisible());

    // The confirm and cancel buttons
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("By button"));
    QTest::mouseClick(row->getConfirmButton(), Qt::LeftButton);
    QCOMPARE(row->getValue(), QStringLiteral("By button"));
    QCOMPARE(edited.count(), 2);
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("Cancelled"));
    QTest::mouseClick(row->getCancelButton(), Qt::LeftButton);
    QCOMPARE(row->getValue(), QStringLiteral("By button"));
    QCOMPARE(edited.count(), 2);

    // A value set while editing does not change the field; the confirmation takes the field
    QVERIFY(row->startEditing());
    row->setValue(QStringLiteral("From the application"));
    QCOMPARE(row->getEditor()->text(), QStringLiteral("By button"));
    row->confirmEditing();
    QCOMPARE(row->getValue(), QStringLiteral("By button"));
    QCOMPARE(edited.count(), 3);

    // A row, which stops being editable, cancels the editing without the signal edited
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("Lost"));
    const qsizetype changes = editingChanged.count();
    row->setEditable(false);
    QVERIFY(!row->isEditing());
    QCOMPARE(editingChanged.count(), changes + 1);
    QCOMPARE(edited.count(), 3);
    QCOMPARE(row->getValue(), QStringLiteral("By button"));
    QVERIFY(!row->getEditButton()->isVisibleTo(row));
    QVERIFY(!row->startEditing());
    row->confirmEditing();
    row->cancelEditing();
    QCOMPARE(edited.count(), 3);

    // Re-entrant: the slot of the signal edited makes the row not editable
    row->setEditable(true);
    connection = connect(row, &pdf::PDFPropertyRow::edited, row, [row]() { row->setEditable(false); });
    QVERIFY(row->startEditing());
    row->getEditor()->setText(QStringLiteral("Reentrant"));
    row->confirmEditing();
    QCOMPARE(row->getValue(), QStringLiteral("Reentrant"));
    QVERIFY(!row->isEditable());
    QVERIFY(!row->isEditing());
    disconnect(connection);

    // Re-entrant: the slot of editingChanged cancels the editing again
    row->setEditable(true);
    connection = connect(row, &pdf::PDFPropertyRow::editingChanged, row, [row](bool isEditing) { if (isEditing) { row->setEditable(false); } });
    QVERIFY(!row->startEditing());
    QVERIFY(!row->isEditing());
    QVERIFY(!row->getEditor()->isVisibleTo(row));
    disconnect(connection);

    // A disabled row is not edited
    row->setEditable(true);
    row->setEnabled(false);
    QVERIFY(!row->startEditing());
    row->setEnabled(true);
    QVERIFY(row->startEditing());
    row->setEnabled(false);
    QVERIFY(!row->isEditing());
    row->setEnabled(true);

    // The field lies in the place of the value, mirrored for right-to-left
    layout->removeWidget(row);
    row->resize(326, 30);
    QVERIFY(row->startEditing());
    QCOMPARE(row->getEditor()->geometry(), QRect(150, 3, 116, 24));
    row->setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(row->getEditor()->geometry(), QRect(326 - 150 - 116, 3, 116, 24));
    QCOMPARE(row->getConfirmButton()->geometry(), QRect(28, 3, 24, 24));
    row->cancelEditing();
}

void FluentComponentsTest::propertyRowCopy()
{
    pdf::PDFPropertyRow row(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable);
    row.resize(row.sizeHint());
    row.show();
    QVERIFY(QTest::qWaitForWindowExposed(&row));
    QSignalSpy copied(&row, &pdf::PDFPropertyRow::copied);

    // The copy button is visible only under the pointer (the pointer of the platform is not used)
    row.setAttribute(Qt::WA_UnderMouse, false);
    row.getCopyButton()->setAttribute(Qt::WA_UnderMouse, false);
    row.getEditButton()->clearFocus();
    QVERIFY(!row.getCopyButton()->hasFocus());
    const QColor surface = token(Color::BgSurface);
    QCOMPARE(colorDistance(strongestInk(renderWidget(&row, 1.0), QRect(298, 3, 24, 24), surface), surface), 0);
    row.setAttribute(Qt::WA_UnderMouse, true);
    QCOMPARE(row.getPropertyState(), Painter::PropertyState::Hover);
    QVERIFY(colorDistance(strongestInk(renderWidget(&row, 1.0), QRect(303, 8, 14, 14), blend(token(Color::BgHover), surface)), surface) > 100);
    row.setAttribute(Qt::WA_UnderMouse, false);

    // The copy: clipboard, signal, the state Copied for the time
    row.setCopiedDuration(40);
    QCOMPARE(row.getCopiedDuration(), 40);
    QTest::mouseClick(row.getCopyButton(), Qt::LeftButton);
    QCOMPARE(copied.count(), 1);
    QCOMPARE(copied.back().front().toString(), QStringLiteral("Jane Novak"));
    QVERIFY(row.isCopied());
    QCOMPARE(row.getPropertyState(), Painter::PropertyState::Copied);
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        QCOMPARE(clipboard->text(), QStringLiteral("Jane Novak"));
    }
    row.setAttribute(Qt::WA_UnderMouse, false);
    const QImage copiedImage = renderWidget(&row, 1.0);
    const QColor hover = blend(token(Color::BgHover), surface);
    QVERIFY(isNear(pixel(copiedImage, 5, 15), hover));
    QVERIFY(colorDistance(strongestInk(copiedImage, QRect(303, 8, 14, 14), hover), token(Color::AccentGreenIcon)) < colorDistance(strongestInk(copiedImage, QRect(303, 8, 14, 14), hover), token(Color::IconDefault)));
    QTRY_VERIFY(!row.isCopied());
    QCOMPARE(row.getCopyButton()->toolTip(), QStringLiteral("Copy"));

    // Nothing is copied by a disabled row or during the editing
    row.setEnabled(false);
    row.copy();
    QCOMPARE(copied.count(), 1);
    row.setEnabled(true);
    QVERIFY(row.startEditing());
    row.copy();
    QCOMPARE(copied.count(), 1);
    QVERIFY(!row.isCopied());

    // The editing ends the state Copied
    row.cancelEditing();
    row.setCopiedDuration(60000);
    row.copy();
    QVERIFY(row.isCopied());
    QVERIFY(row.startEditing());
    QVERIFY(!row.isCopied());
    row.cancelEditing();

    // A hidden row stops the timer and leaves the state
    row.copy();
    QVERIFY(row.isCopied());
    row.hide();
    QVERIFY(!row.isCopied());

    // A hidden row copies (clipboard and signal) without the state Copied
    const qsizetype copiedCount = copied.count();
    row.setValue(QStringLiteral("Hidden value"));
    row.copy();
    QCOMPARE(copied.count(), copiedCount + 1);
    QVERIFY(!row.isCopied());
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        QCOMPARE(clipboard->text(), QStringLiteral("Hidden value"));
    }

    // Defensive durations
    row.setCopiedDuration(-5);
    QCOMPARE(row.getCopiedDuration(), 0);
    row.setCopiedDuration(std::numeric_limits<int>::min());
    QCOMPARE(row.getCopiedDuration(), 0);
    row.show();
    row.copy();
    QTRY_VERIFY(!row.isCopied());
}

void FluentComponentsTest::propertyRowLifetime()
{
    // Deleted while the timer of the state Copied runs
    {
        pdf::PDFPropertyRow* row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"));
        row->setCopiedDuration(20);
        row->show();
        row->copy();
        delete row;
        QTest::qWait(60);
    }

    // Deleted while editing (with the focus in the field)
    {
        QWidget window;
        QVBoxLayout* layout = new QVBoxLayout(&window);
        pdf::PDFPropertyRow* row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable, &window);
        layout->addWidget(row);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(row->startEditing());
        delete row;
        QApplication::processEvents();
    }

    // Deleted by the slots of its signals
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable);
        connect(row.data(), &pdf::PDFPropertyRow::edited, row.data(), [&row]() { delete row.data(); });
        QVERIFY(row->startEditing());
        row->getEditor()->setText(QStringLiteral("Other"));
        row->confirmEditing();
        QVERIFY(!row);
    }
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable);
        connect(row.data(), &pdf::PDFPropertyRow::editingChanged, row.data(), [&row](bool isEditing) { if (!isEditing) { delete row.data(); } });
        QVERIFY(row->startEditing());
        row->setEditable(false);
        QVERIFY(!row);
    }
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable);
        row->show();
        QVERIFY(QTest::qWaitForWindowExposed(row.data()));
        connect(row.data(), &pdf::PDFPropertyRow::editingChanged, row.data(), [&row](bool isEditing) { if (isEditing) { delete row.data(); } });
        QTest::mouseClick(row->getEditButton(), Qt::LeftButton);
        QVERIFY(!row);
    }
    {
        QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"));
        row->show();
        QVERIFY(QTest::qWaitForWindowExposed(row.data()));
        connect(row.data(), &pdf::PDFPropertyRow::copied, row.data(), [&row]() { delete row.data(); });
        QTest::mouseClick(row->getCopyButton(), Qt::LeftButton);
        QVERIFY(!row);
        QTest::qWait(10);
    }

    // A row deleted by the slot of its star
    {
        QPointer<pdf::PDFRecentFileRow> row = new pdf::PDFRecentFileRow(QStringLiteral("a.pdf"), QString(), QString());
        row->show();
        QVERIFY(QTest::qWaitForWindowExposed(row.data()));
        connect(row.data(), &pdf::PDFRecentFileRow::pinnedChanged, row.data(), [&row]() { delete row.data(); });
        QTest::mouseClick(row->getStarButton(), Qt::LeftButton);
        QVERIFY(!row);
    }
}

void FluentComponentsTest::rowThemeAndDirection()
{
    // A change of the theme: the colors are taken at the time of drawing
    QWidget holder;
    pdf::PDFCollectionRow& collectionRow = *new pdf::PDFCollectionRow(QStringLiteral("Prepare for sharing"), QStringLiteral("3 tools"), &holder);
    collectionRow.resize(collectionRow.sizeHint());
    const QColor lightCircle = pixel(renderWidget(&collectionRow, 1.0), 13, 26);
    QVERIFY(isNear(lightCircle, blend(token(Color::BgAccentSubtle), token(Color::BgSurface))));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Dark);
    const QColor darkCircle = pixel(renderWidget(&collectionRow, 1.0), 13, 26);
    QVERIFY2(isNear(darkCircle, blend(token(Color::BgAccentSubtle), token(Color::BgSurface))), qPrintable(QStringLiteral("%1 / %2").arg(darkCircle.name(), blend(token(Color::BgAccentSubtle), token(Color::BgSurface)).name())));
    QVERIFY(lightCircle != darkCircle);
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"));
    versionRow.resize(versionRow.sizeHint());
    QVERIFY(isNear(pixel(renderWidget(&versionRow, 1.0), 13, 9), token(Color::BgAccent)));
    pdf::PDFThemeManager::getInstance()->setScheme(pdf::PDFThemeColors::Scheme::Light);

    // Right-to-left: the icon of a link row at the right edge, the chevron at the left one
    const QColor surface = token(Color::BgSurface);
    const QImage ltr = render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), RowSamples::linkRow(), state(QStyle::State_None)); }, QSize(328, 50));
    const QImage rtl = render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), RowSamples::linkRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(328, 50));
    QVERIFY(colorDistance(strongestInk(ltr, QRect(10, 17, 16, 16), surface), surface) > 100);
    QVERIFY(colorDistance(strongestInk(rtl, QRect(328 - 26, 17, 16, 16), surface), surface) > 100);
    QVERIFY(colorDistance(strongestInk(rtl, QRect(10, 18, 14, 14), surface), surface) > 50);
    QCOMPARE(colorDistance(strongestInk(rtl, QRect(40, 0, 100, 50), surface), surface), 0);

    // The version row has the rail at the right edge
    const QImage rtlVersion = render([&](QPainter& painter) { Painter::paintVersionRow(&painter, QRectF(0, 0, 324, 60), RowSamples::versionRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(324, 60));
    QVERIFY(isNear(pixel(rtlVersion, 324 - 14, 9), token(Color::BgAccent)));
    QVERIFY(isNear(pixel(rtlVersion, 310, 40), token(Color::BgTrack)));
    QVERIFY(isNear(pixel(rtlVersion, 13, 40), surface));

    // Widgets mirror their nested widgets
    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"));
    fileRow.resize(381, 51);
    fileRow.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(fileRow.getMenuButton()->geometry(), QRect(12, 17, 16, 16));
    QCOMPARE(fileRow.getStarButton()->geometry(), QRect(40, 13, 24, 24));
    QVERIFY(fileRow.getComponentState().isRightToLeft());

    pdf::PDFToolRow toolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    toolRow.resize(380, 48);
    toolRow.setLayoutDirection(Qt::RightToLeft);
    QCOMPARE(toolRow.getStarButton()->geometry(), QRect(6, 10, 28, 28));
}

void FluentComponentsTest::rowDefensive()
{
    // Null painter
    const Painter::State normal;
    Painter::paintIconButton(nullptr, QRectF(0, 0, 24, 24), Painter::IconButton(), normal);
    Painter::paintMoreButton(nullptr, QRectF(0, 0, 16, 16), normal);
    Painter::paintLinkRow(nullptr, QRectF(0, 0, 328, 50), RowSamples::linkRow(), normal);
    Painter::paintRecentFileRow(nullptr, QRectF(0, 0, 381, 51), RowSamples::recentFileRow(), normal);
    Painter::paintRecentToolRow(nullptr, QRectF(0, 0, 342, 48), RowSamples::recentToolRow(), normal);
    Painter::paintWorkspaceRow(nullptr, QRectF(0, 0, 344, 52), RowSamples::workspaceRow(), normal);
    Painter::paintToolRow(nullptr, QRectF(0, 0, 380, 48), RowSamples::toolRow(), normal);
    Painter::paintCollectionRow(nullptr, QRectF(0, 0, 324, 52), RowSamples::collectionRow(), normal);
    Painter::paintToolShortcut(nullptr, QRectF(0, 0, 180, 48), RowSamples::toolShortcut(), normal);
    Painter::paintIssueRow(nullptr, QRectF(0, 0, 786, 52), RowSamples::issueRow(), normal);
    Painter::paintVersionRow(nullptr, QRectF(0, 0, 324, 60), RowSamples::versionRow(), normal);
    Painter::paintPropertyRow(nullptr, QRectF(0, 0, 326, 30), RowSamples::propertyRow(true, Painter::PropertyState::Editing), normal);

    const QStyle::State all = QStyle::State_Enabled | QStyle::State_MouseOver | QStyle::State_Sunken | QStyle::State_HasFocus;
    auto paintAll = [&](QPainter& painter, const QRectF& rect)
    {
        const Painter::State full(all);
        Painter::IconButton button;
        button.iconName = QStringLiteral("copy");
        Painter::paintIconButton(&painter, rect, button, full);
        Painter::paintMoreButton(&painter, rect, full);
        Painter::paintLinkRow(&painter, rect, RowSamples::linkRow(), full);
        Painter::RecentFileRow missing = RowSamples::recentFileRow();
        missing.status = Painter::FileStatus::Missing;
        Painter::paintRecentFileRow(&painter, rect, missing, full);
        Painter::paintRecentToolRow(&painter, rect, RowSamples::recentToolRow(), full);
        Painter::paintWorkspaceRow(&painter, rect, RowSamples::workspaceRow(), full);
        Painter::ToolRow tool = RowSamples::toolRow();
        tool.tag = QStringLiteral("Advanced");
        Painter::paintToolRow(&painter, rect, tool, full);
        Painter::paintCollectionRow(&painter, rect, RowSamples::collectionRow(), full);
        Painter::paintToolShortcut(&painter, rect, RowSamples::toolShortcut(), full);
        Painter::paintIssueRow(&painter, rect, RowSamples::issueRow(), full);
        Painter::paintVersionRow(&painter, rect, RowSamples::versionRow(), full);
        Painter::paintPropertyRow(&painter, rect, RowSamples::propertyRow(true, Painter::PropertyState::Editing), full);
        Painter::paintPropertyRow(&painter, rect, RowSamples::propertyRow(false, Painter::PropertyState::Copied), full);
    };

    // Empty and negative rectangles draw nothing, the state of the painter is restored
    const QImage empty = render([](QPainter&) { }, QSize(40, 40));
    for (const QRectF& rect : { QRectF(), QRectF(10, 10, 0, 20), QRectF(10, 10, -20, -20), QRectF(10, 10, 20, -5) })
    {
        const QImage image = render([&](QPainter& painter)
        {
            const QTransform transform = painter.transform();
            const QPen pen = painter.pen();
            paintAll(painter, rect);
            QCOMPARE(painter.transform(), transform);
            QCOMPARE(painter.hasClipping(), false);
            QCOMPARE(painter.pen(), pen);
        }, QSize(40, 40));
        QCOMPARE(image, empty);
    }

    // Small rectangles: nothing is drawn outside (the focus rings outside reach 2 px)
    for (const QSizeF& size : { QSizeF(3, 3), QSizeF(1, 40), QSizeF(12, 5), QSizeF(60, 8) })
    {
        const QRectF rect(QPointF(10, 10), size);
        const QImage image = render([&](QPainter& painter)
        {
            const QPen pen = painter.pen();
            paintAll(painter, rect);
            QCOMPARE(painter.pen(), pen);
            QCOMPARE(painter.hasClipping(), false);
        }, QSize(90, 70));
        const QRect outside = rect.toAlignedRect().adjusted(-3, -3, 3, 3);
        for (int y = 0; y < 70; ++y)
        {
            for (int x = 0; x < 90; ++x)
            {
                if (!outside.contains(x, y))
                {
                    QVERIFY2(image.pixelColor(x, y) == empty.pixelColor(0, 0), qPrintable(QStringLiteral("%1 %2 (%3 x %4)").arg(x).arg(y).arg(size.width()).arg(size.height())));
                }
            }
        }
    }

    // Empty texts, unknown icons
    const QImage image = render([&](QPainter& painter)
    {
        Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), Painter::LinkRow{ QString(), QString(), QStringLiteral("no-such-icon"), std::nullopt }, normal);
        Painter::paintRecentFileRow(&painter, QRectF(0, 0, 381, 51), Painter::RecentFileRow(), normal);
        Painter::paintRecentToolRow(&painter, QRectF(0, 0, 342, 48), Painter::RecentToolRow(), normal);
        Painter::paintWorkspaceRow(&painter, QRectF(0, 0, 344, 52), Painter::WorkspaceRow{ QString(), QString(), QStringLiteral("no-such-icon"), pdf::PDFAccentHue::Teal, true }, normal);
        Painter::paintToolRow(&painter, QRectF(0, 0, 380, 48), Painter::ToolRow(), normal);
        Painter::paintCollectionRow(&painter, QRectF(0, 0, 324, 52), Painter::CollectionRow{ QString(), QString(), QString() }, normal);
        Painter::paintToolShortcut(&painter, QRectF(0, 0, 180, 48), Painter::ToolShortcut(), normal);
        Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), Painter::IssueRow{ QString(), QString(), QString(), QString(), QStringLiteral("no-such-icon") }, normal);
        Painter::paintVersionRow(&painter, QRectF(0, 0, 324, 60), Painter::VersionRow(), normal);
        Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), Painter::PropertyRow(), normal);
        Painter::paintIconButton(&painter, QRectF(0, 0, 24, 24), Painter::IconButton{ QString(), -5, -3, -1.0, std::nullopt }, normal);
    }, QSize(800, 70));
    QCOMPARE(countLeakPixels(image), 0);
    QCOMPARE(Painter::getVersionRowHeight(Painter::VersionRow(), -10), 30);
    QCOMPARE(Painter::getVersionRowHeight(RowSamples::versionRow(), 0), 30);
    QCOMPARE(Painter::getVersionRowHeight(RowSamples::versionRow(), std::numeric_limits<int>::min()), 30);
    QCOMPARE(Painter::getPropertyRowEditorRect(QRectF(), RowSamples::propertyRow(true, Painter::PropertyState::Editing), normal), QRectF(18, 0, 0, 0));
    QCOMPARE(Painter::getPropertyRowEditorRect(QRectF(0, 0, 326, 30), RowSamples::propertyRow(true, Painter::PropertyState::Editing), Painter::State(QStyle::State_Enabled, Qt::RightToLeft)), QRectF(60, 3, 116, 24));

    // Invalid values of the enumerations
    QCOMPARE(Painter::normalized(static_cast<Painter::FileStatus>(7)), Painter::FileStatus::Available);
    QCOMPARE(Painter::normalized(static_cast<Painter::PropertyState>(9), true), Painter::PropertyState::Default);
    QCOMPARE(Painter::normalized(Painter::PropertyState::Editing, false), Painter::PropertyState::Default);
    QCOMPARE(Painter::normalized(Painter::PropertyState::Editing, true), Painter::PropertyState::Editing);
    const Painter::PropertyRow invalidProperty = RowSamples::propertyRow(false, static_cast<Painter::PropertyState>(42));
    QCOMPARE(countLeakPixels(render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), invalidProperty, normal); }, QSize(326, 30))), 0);

    pdf::PDFRecentFileRow fileRow;
    fileRow.setStatus(static_cast<pdf::PDFRecentFileRow::Status>(5));
    QCOMPARE(fileRow.getStatus(), pdf::PDFRecentFileRow::Status::Available);
    pdf::PDFRecentToolRow recentToolRow;
    recentToolRow.setHue(static_cast<pdf::PDFAccentHue>(77));
    QCOMPARE(recentToolRow.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFWorkspaceRow workspaceRow;
    workspaceRow.setHue(static_cast<pdf::PDFAccentHue>(-4));
    QCOMPARE(workspaceRow.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFToolRow toolRow;
    toolRow.setHue(static_cast<pdf::PDFAccentHue>(12));
    QCOMPARE(toolRow.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFToolShortcut shortcut;
    shortcut.setHue(static_cast<pdf::PDFAccentHue>(12));
    QCOMPARE(shortcut.getHue(), pdf::PDFAccentHue::Neutral);
    pdf::PDFLinkRow linkRow;
    linkRow.setIconColor(static_cast<Color>(-1));
    QVERIFY(!linkRow.getIconColor().has_value());
    linkRow.setIconColor(static_cast<Color>(100000));
    QVERIFY(!linkRow.getIconColor().has_value());
    linkRow.setIconColor(Color::IconAccent);
    QCOMPARE(linkRow.getIconColor(), std::optional<Color>(Color::IconAccent));
    linkRow.resetIconColor();
    QVERIFY(!linkRow.getIconColor().has_value());
    pdf::PDFIssueRow issueRow;
    issueRow.setStatusIconName(QStringLiteral("no-such-icon"));
    pdf::PDFVersionRow versionRow;
    QCOMPARE(versionRow.heightForWidth(-50), 30);
    pdf::PDFPropertyRow propertyRow(QStringLiteral("Key"), QString(), pdf::PDFPropertyRow::Editability::Editable);
    pdf::PDFCollectionRow collectionRow;

    // Widgets of zero and of tiny sizes, empty contents
    for (QWidget* widget : std::initializer_list<QWidget*>{ &fileRow, &recentToolRow, &workspaceRow, &toolRow, &shortcut, &linkRow, &issueRow, &versionRow, &propertyRow, &collectionRow })
    {
        for (const QSize& size : { QSize(0, 0), QSize(1, 1), QSize(30, 10) })
        {
            widget->resize(size);
            QCOMPARE(countLeakPixels(widget->grab().toImage()), 0);
        }
    }
    QVERIFY(propertyRow.startEditing());
    QCOMPARE(propertyRow.getEditor()->geometry(), QRect(18, 0, 0, 10));
    propertyRow.confirmEditing();
    QCOMPARE(propertyRow.getValue(), QString());
}

void FluentComponentsTest::propertyRowKeyboard()
{
    using Editability = pdf::PDFPropertyRow::Editability;

    // The rows are created without a parent and added to the dialog later (the
    // focus chain of their children is moved with them)
    QDialog dialog;
    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    QLineEdit* before = new QLineEdit(&dialog);
    pdf::PDFPropertyRow* row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), Editability::Editable);
    QLineEdit* after = new QLineEdit(&dialog);
    QPushButton* okButton = new QPushButton(QStringLiteral("OK"), &dialog);
    okButton->setDefault(true);
    QObject::connect(okButton, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(before);
    layout->addWidget(row);
    layout->addWidget(after);
    layout->addWidget(okButton);
    dialog.show();
    dialog.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&dialog));

    QSignalSpy edited(row, &pdf::PDFPropertyRow::edited);
    QSignalSpy copied(row, &pdf::PDFPropertyRow::copied);
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    QSignalSpy rejected(&dialog, &QDialog::rejected);
    QLineEdit* editor = row->getEditor();
    QVERIFY(editor);

    // Tab goes from the field to the confirm and to the cancel button (review C1)
    QVERIFY(row->startEditing());
    QTRY_VERIFY(editor->hasFocus());
    editor->setText(QStringLiteral("Keyboard"));
    QTest::keyClick(editor, Qt::Key_Tab);
    QTRY_VERIFY(row->getConfirmButton()->hasFocus());
    QVERIFY(row->isEditing());
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Tab);
    QTRY_VERIFY(row->getCancelButton()->hasFocus());
    QVERIFY(row->isEditing());
    QTest::keyClick(row->getCancelButton(), Qt::Key_Backtab, Qt::ShiftModifier);
    QTRY_VERIFY(row->getConfirmButton()->hasFocus());

    // Space on the confirm button confirms, the focus stays in the row (review C2)
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Space);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Keyboard"));
    QCOMPARE(edited.count(), 1);
    QTRY_VERIFY(row->getEditButton()->hasFocus());

    // Space on the cancel button cancels, the focus stays in the row
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Cancelled"));
    row->getCancelButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getCancelButton()->hasFocus());
    QTest::keyClick(row->getCancelButton(), Qt::Key_Space);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Keyboard"));
    QTRY_VERIFY(row->getEditButton()->hasFocus());

    // Enter on the confirm button confirms, not the default button of the dialog (review C3)
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Enter"));
    row->getConfirmButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getConfirmButton()->hasFocus());
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Return);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Enter"));
    QCOMPARE(accepted.count(), 0);
    QTRY_VERIFY(row->getEditButton()->hasFocus());

    // Enter on the cancel button cancels, Esc on the confirm button cancels, the dialog stays
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Lost"));
    row->getCancelButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getCancelButton()->hasFocus());
    QTest::keyClick(row->getCancelButton(), Qt::Key_Enter, Qt::KeypadModifier);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Enter"));
    QVERIFY(row->startEditing());
    row->getConfirmButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getConfirmButton()->hasFocus());
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Escape);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Enter"));
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(rejected.count(), 0);
    QVERIFY(dialog.isVisible());

    // Enter on the edit and the copy button presses them (as Space)
    row->getEditButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getEditButton()->hasFocus());
    QTest::keyClick(row->getEditButton(), Qt::Key_Return);
    QVERIFY(row->isEditing());
    QCOMPARE(accepted.count(), 0);
    row->cancelEditing();
    row->getCopyButton()->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(row->getCopyButton()->hasFocus());
    QTest::keyClick(row->getCopyButton(), Qt::Key_Return);
    QCOMPARE(copied.count(), 1);
    QCOMPARE(accepted.count(), 0);

    // The arrows and the page keys stay in the field (review C4)
    QVERIFY(row->startEditing());
    QTRY_VERIFY(editor->hasFocus());
    for (const Qt::Key key : { Qt::Key_Down, Qt::Key_Up, Qt::Key_PageDown, Qt::Key_PageUp, Qt::Key_Down })
    {
        QTest::keyClick(editor, key);
        QApplication::processEvents();
        QVERIFY(editor->hasFocus());
        QVERIFY(row->isEditing());
    }

    // A row, which stops being editable, keeps the focus in its copy button
    row->setEditable(false);
    QVERIFY(!row->isEditing());
    QTRY_VERIFY(row->getCopyButton()->hasFocus());
    row->setEditable(true);

    // The focus leaving the row confirms the editing (review C11)
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Outside"));
    after->setFocus(Qt::MouseFocusReason);
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Outside"));
    QCOMPARE(edited.count(), 3);
    QVERIFY(after->hasFocus());

    // ... also by Tab from the cancel button
    QVERIFY(row->startEditing());
    editor->setText(QStringLiteral("Tabbed"));
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(editor, Qt::Key_Tab);
    QTest::keyClick(row->getConfirmButton(), Qt::Key_Tab);
    QTest::keyClick(row->getCancelButton(), Qt::Key_Tab);
    QTRY_VERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Tabbed"));

    // A popup of the field and another window keep the editing
    QVERIFY(row->startEditing());
    QTRY_VERIFY(editor->hasFocus());
    before->setFocus(Qt::PopupFocusReason);
    QVERIFY(row->isEditing());
    editor->setFocus(Qt::OtherFocusReason);
    before->setFocus(Qt::ActiveWindowFocusReason);
    QVERIFY(row->isEditing());

    // A hidden row cancels the editing and is not edited, when it is shown again
    editor->setText(QStringLiteral("Hidden"));
    row->hide();
    QVERIFY(!row->isEditing());
    QCOMPARE(row->getValue(), QStringLiteral("Tabbed"));
    row->show();
    QVERIFY(!row->isEditing());
    QVERIFY(!editor->isVisibleTo(row));

    // A click of the OK button of the dialog confirms the editing before the dialog closes
    QVERIFY(row->startEditing());
    QTRY_VERIFY(editor->hasFocus());
    editor->setText(QStringLiteral("By OK"));
    QTest::mouseClick(okButton, Qt::LeftButton);
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(row->getValue(), QStringLiteral("By OK"));
    QVERIFY(!row->isEditing());
}

void FluentComponentsTest::propertyRowFocusDeletion()
{
    // A row deleted by the slot of the signal edited, which the focus leaving the row emits
    QWidget window;
    QVBoxLayout* layout = new QVBoxLayout(&window);
    QPointer<pdf::PDFPropertyRow> row = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable, &window);
    QLineEdit* other = new QLineEdit(&window);
    layout->addWidget(row.data());
    layout->addWidget(other);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    connect(row.data(), &pdf::PDFPropertyRow::edited, &window, [&row]() { delete row.data(); });
    QVERIFY(row->startEditing());
    QTRY_VERIFY(row->getEditor()->hasFocus());
    row->getEditor()->setText(QStringLiteral("Other"));
    other->setFocus(Qt::MouseFocusReason);
    QVERIFY(!row);
    QApplication::processEvents();
    QVERIFY(other->hasFocus());
}

void FluentComponentsTest::moreButtonMenu()
{
    QWidget window;
    window.move(100, 100);
    window.resize(420, 140);
    pdf::PDFRecentFileRow* row = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    row->setGeometry(0, 0, 381, 51);

    // The clicks go through the window system, as the clicks of the user (a
    // popup receives the press outside of it and closes, Qt may replay it)
    auto clickButton = [&window](QWidget* widget) { QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, widget->mapTo(&window, widget->rect().center())); };
    pdf::PDFRecentFileRow* rightToLeftRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    rightToLeftRow->setLayoutDirection(Qt::RightToLeft);
    rightToLeftRow->setGeometry(0, 60, 381, 51);
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));
    menu.addAction(QStringLiteral("Remove from the list"));
    row->setMenu(&menu);
    rightToLeftRow->setMenu(&menu);
    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto globalRect = [](const QWidget* widget) { return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()); };

    // Below the button, at its leading edge; the button stays pressed, while its menu is shown
    QSignalSpy requested(row, &pdf::PDFRecentFileRow::menuRequested);
    pdf::PDFMoreButton* button = row->getMenuButton();
    clickButton(button);
    QTRY_VERIFY(menu.isVisible());
    QVERIFY(button->isDown());
    QRect buttonRect = globalRect(button);
    QVERIFY2(qAbs(menu.geometry().left() - buttonRect.left()) < 40, qPrintable(QStringLiteral("menu %1, button %2").arg(menu.geometry().left()).arg(buttonRect.left())));
    QVERIFY(menu.geometry().top() >= buttonRect.top());

    // A second click of the button closes the menu and the button is released
    // (review C8; the replay of the press, which QMenu::setNoReplayFor prevents,
    // needs the mouse grab of a real platform, the offscreen platform has none)
    clickButton(button);
    QTRY_VERIFY(!menu.isVisible());
    QCOMPARE(requested.count(), 1);
    QTRY_VERIFY(!button->isDown());

    // Right-to-left: the menu is aligned with the right (leading) edge of the button (review C7)
    pdf::PDFMoreButton* rightToLeftButton = rightToLeftRow->getMenuButton();
    QVERIFY(rightToLeftButton->showMenu());
    QTRY_VERIFY(menu.isVisible());
    buttonRect = globalRect(rightToLeftButton);
    const QRect menuRect = menu.geometry();
    QVERIFY2(menuRect.left() < buttonRect.left() && menuRect.right() - buttonRect.right() < menuRect.width() / 2,
             qPrintable(QStringLiteral("menu %1..%2, button %3..%4").arg(menuRect.left()).arg(menuRect.right()).arg(buttonRect.left()).arg(buttonRect.right())));
    menu.hide();
    QTRY_VERIFY(!rightToLeftButton->isDown());

    // The context menu key shows the menu below the menu button, not in the middle of the row (review C17)
    row->setFocus(Qt::TabFocusReason);
    QContextMenuEvent keyboardEvent(QContextMenuEvent::Keyboard, row->rect().center(), row->mapToGlobal(row->rect().center()));
    QApplication::sendEvent(row, &keyboardEvent);
    QVERIFY(keyboardEvent.isAccepted());
    QTRY_VERIFY(menu.isVisible());
    buttonRect = globalRect(button);
    QVERIFY2(qAbs(menu.geometry().left() - buttonRect.left()) < 40, qPrintable(QStringLiteral("menu %1, button %2").arg(menu.geometry().left()).arg(buttonRect.left())));
    QCOMPARE(requested.count(), 2);
    menu.hide();
    QTRY_VERIFY(!button->isDown());

    // A disabled button shows nothing
    button->setEnabled(false);
    QVERIFY(!button->showMenu());
    QVERIFY(!button->showMenu(QPoint(10, 10)));
    QVERIFY(!button->isDown());
}

void FluentComponentsTest::rowNestedDeletion()
{
    // The nested widgets are owned by the row; a nested widget deleted by the
    // application is forgotten, the row draws and works without it (review C6)
    QWidget window;
    window.resize(820, 300);
    pdf::PDFRecentFileRow* fileRow = new pdf::PDFRecentFileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"), &window);
    fileRow->setGeometry(0, 0, 381, 51);
    pdf::PDFWorkspaceRow* workspaceRow = new pdf::PDFWorkspaceRow(QStringLiteral("Thesis review"), QStringLiteral("4 documents"), &window);
    workspaceRow->setGeometry(0, 60, 344, 52);
    pdf::PDFToolRow* toolRow = new pdf::PDFToolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue, &window);
    toolRow->setGeometry(0, 120, 380, 48);
    toolRow->setTag(QStringLiteral("Advanced"));
    pdf::PDFPropertyRow* propertyRow = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane"), pdf::PDFPropertyRow::Editability::Editable, &window);
    propertyRow->setGeometry(0, 180, 326, 30);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QMenu menu;
    menu.addAction(QStringLiteral("Open"));

    // Recent file row without the star and the menu button
    fileRow->setPinned(true);
    delete fileRow->getStarButton();
    QCOMPARE(fileRow->getStarButton(), nullptr);
    QVERIFY(!fileRow->isPinned());
    QVERIFY(!fileRow->isStarVisible());
    fileRow->setPinned(true);
    fileRow->setStarVisible(true);
    QVERIFY(!fileRow->getContent().starVisible);
    QCOMPARE(countLeakPixels(renderWidget(fileRow, 1.0)), 0);
    delete fileRow->getMenuButton();
    QCOMPARE(fileRow->getMenuButton(), nullptr);
    fileRow->setMenu(&menu);
    QCOMPARE(fileRow->getMenu(), nullptr);
    for (const QContextMenuEvent::Reason reason : { QContextMenuEvent::Mouse, QContextMenuEvent::Keyboard })
    {
        QContextMenuEvent event(reason, QPoint(100, 25), fileRow->mapToGlobal(QPoint(100, 25)));
        QApplication::sendEvent(fileRow, &event);
        QVERIFY(!event.isAccepted());
    }
    QVERIFY(!menu.isVisible());
    QCOMPARE(countLeakPixels(renderWidget(fileRow, 1.0)), 0);

    // The slot of menuRequested deletes the menu button
    workspaceRow->setMenu(&menu);
    connect(workspaceRow, &pdf::PDFWorkspaceRow::menuRequested, workspaceRow, [workspaceRow]() { delete workspaceRow->getMenuButton(); });
    QContextMenuEvent workspaceEvent(QContextMenuEvent::Mouse, QPoint(100, 25), workspaceRow->mapToGlobal(QPoint(100, 25)));
    QApplication::sendEvent(workspaceRow, &workspaceEvent);
    QVERIFY(!workspaceEvent.isAccepted());
    QCOMPARE(workspaceRow->getMenuButton(), nullptr);
    QCOMPARE(workspaceRow->getMenu(), nullptr);
    QVERIFY(!menu.isVisible());
    QCOMPARE(countLeakPixels(renderWidget(workspaceRow, 1.0)), 0);

    // Tool row without the tag and the star: the texts take their place
    const int textWidthWithTag = toolRow->minimumSizeHint().width();
    delete toolRow->getTagWidget();
    QCOMPARE(toolRow->getTagWidget(), nullptr);
    QVERIFY(toolRow->getContent().tag.isEmpty());
    toolRow->setTag(QStringLiteral("Other"));
    QCOMPARE(toolRow->getTag(), QStringLiteral("Other"));
    QVERIFY(toolRow->minimumSizeHint().width() < textWidthWithTag);
    delete toolRow->getStarButton();
    toolRow->setPinned(true);
    QVERIFY(!toolRow->isPinned());
    QCOMPARE(countLeakPixels(renderWidget(toolRow, 1.0)), 0);

    // Property row: deleted buttons, the field deleted while editing ends the editing
    delete propertyRow->getCopyButton();
    delete propertyRow->getEditButton();
    for (const QEvent::Type type : { QEvent::Enter, QEvent::Leave, QEvent::HoverEnter, QEvent::HoverLeave })
    {
        QEvent event(type);
        QApplication::sendEvent(propertyRow, &event);
    }
    propertyRow->copy();
    QVERIFY(propertyRow->startEditing());
    delete propertyRow->getConfirmButton();
    delete propertyRow->getCancelButton();
    QSignalSpy editingChanged(propertyRow, &pdf::PDFPropertyRow::editingChanged);
    delete propertyRow->getEditor();
    QCOMPARE(propertyRow->getEditor(), nullptr);
    QVERIFY(!propertyRow->isEditing());
    QCOMPARE(editingChanged.count(), 1);
    QVERIFY(!propertyRow->startEditing());
    propertyRow->setEditable(false);
    propertyRow->setKey(QStringLiteral("Key"));
    propertyRow->setLayoutDirection(Qt::RightToLeft);
    propertyRow->resize(200, 30);
    QCOMPARE(countLeakPixels(renderWidget(propertyRow, 1.0)), 0);
    QCOMPARE(countLeakPixels(renderWidget(&window, 2.0)), 0);
}

void FluentComponentsTest::rowRetranslation()
{
    // Texts of the descriptions are translated again after a change of the language (review C15)
    pdf::PDFRecentFileRow fileRow(QStringLiteral("a.pdf"), QStringLiteral("C:\\Docs"), QStringLiteral("Today"));
    fileRow.setStatus(pdf::PDFRecentFileRow::Status::Missing);
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Summary"));
    QAccessibleInterface* fileAccessible = QAccessible::queryAccessibleInterface(&fileRow);
    QAccessibleInterface* versionAccessible = QAccessible::queryAccessibleInterface(&versionRow);
    QCOMPARE(fileAccessible->text(QAccessible::Description), QStringLiteral("No longer exists, C:\\Docs"));
    QCOMPARE(versionAccessible->text(QAccessible::Description), QStringLiteral("Current version. Sep 14. Summary"));

    TestTranslator translator;
    QVERIFY(QCoreApplication::installTranslator(&translator));
    QTRY_COMPARE(fileAccessible->text(QAccessible::Description), QStringLiteral("Gone, C:\\Docs"));
    QTRY_COMPARE(versionAccessible->text(QAccessible::Description), QStringLiteral("Installed. Sep 14. Summary"));
    QCOMPARE(Painter::getMissingFileText(), QStringLiteral("Gone"));

    QVERIFY(QCoreApplication::removeTranslator(&translator));
    QTRY_COMPARE(fileAccessible->text(QAccessible::Description), QStringLiteral("No longer exists, C:\\Docs"));
    QTRY_COMPARE(versionAccessible->text(QAccessible::Description), QStringLiteral("Current version. Sep 14. Summary"));
}

void FluentComponentsTest::rowDetails()
{
    const QColor surface = token(Color::BgSurface);
    auto isCloser = [](const QColor& color, const QColor& expected, const QColor& other) { return colorDistance(color, expected) < colorDistance(color, other); };

    // The press of the edit button of a hovered property row is icon/strong (review C12)
    const QStyle::State pressed = QStyle::State_MouseOver | QStyle::State_Sunken;
    const QColor pressedBackground = blend(token(Color::BgPressed), surface);
    for (const Painter::PropertyAction action : { Painter::PropertyAction::Edit, Painter::PropertyAction::Copy })
    {
        const QImage image = render([&](QPainter& painter) { Painter::paintPropertyRowAction(&painter, QRectF(0, 0, 24, 24), action, Painter::PropertyState::Hover, state(pressed)); }, QSize(24, 24));
        QVERIFY(isNear(pixel(image, 2, 12), pressedBackground));
        QVERIFY(isCloser(strongestInk(image, QRect(5, 5, 14, 14), pressedBackground), token(Color::IconStrong), token(Color::IconDefault)));
    }

    QWidget window;
    pdf::PDFPropertyRow* propertyRow = new pdf::PDFPropertyRow(QStringLiteral("Author"), QStringLiteral("Jane Novak"), pdf::PDFPropertyRow::Editability::Editable, &window);
    propertyRow->setGeometry(0, 0, 326, 30);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    propertyRow->setAttribute(Qt::WA_UnderMouse, true);
    pdf::PDFIconButton* editButton = propertyRow->getEditButton();
    editButton->setAttribute(Qt::WA_UnderMouse, true);
    editButton->setDown(true);
    QImage editImage = renderWidget(editButton, 1.0);
    QVERIFY(isCloser(strongestInk(editImage, QRect(5, 5, 14, 14), pressedBackground), token(Color::IconStrong), token(Color::IconDefault)));
    editButton->setDown(false);
    editButton->setAttribute(Qt::WA_UnderMouse, false);
    editImage = renderWidget(editButton, 1.0);
    QVERIFY(isCloser(strongestInk(editImage, QRect(5, 5, 14, 14), surface), token(Color::IconDefault), token(Color::IconMuted)));
    propertyRow->setAttribute(Qt::WA_UnderMouse, false);

    // The text of the field starts 10 px from its edge: ring 2 + padding 8 (review F1)
    QCOMPARE(Painter::getPropertyRowEditorTextInset(), 10);
    QCOMPARE(propertyRow->getEditor()->property(pdf::PDFWidgetStyleProperties::FIELD_TEXT_INSETS).value<QMargins>(), QMargins(10, 0, 10, 0));
    const QImage editing = render([&](QPainter& painter) { Painter::paintPropertyRow(&painter, QRectF(0, 0, 326, 30), RowSamples::propertyRow(true, Painter::PropertyState::Editing), state(QStyle::State_None)); }, QSize(326, 30));
    QCOMPARE(countPixels(editing, QRect(152, 6, 8, 18), token(Color::TextPrimary), 40), 0);
    QVERIFY(countPixels(editing, QRect(160, 6, 60, 18), token(Color::TextPrimary), 40) > 0);

    // Icon colors: the widget and the painter have one rule, an invalid color is not drawn (review C13, C14)
    pdf::PDFIconButton iconButton(QStringLiteral("copy"));
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconMuted);
    iconButton.setAttribute(Qt::WA_UnderMouse, true);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconDefault);
    iconButton.setDown(true);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconStrong);
    iconButton.setIconColor(Color::IconAccent);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconAccent);
    iconButton.setIconColor(static_cast<Color>(99999));
    QVERIFY(!iconButton.getIconColor().has_value());
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconStrong);
    iconButton.setEnabled(false);
    QCOMPARE(iconButton.getCurrentIconColor(), Color::IconDisabled);
    Painter::IconButton invalidButton;
    invalidButton.iconName = QStringLiteral("copy");
    invalidButton.iconColor = static_cast<Color>(-3);
    QCOMPARE(Painter::getIconButtonColor(invalidButton, Painter::State()), Color::IconMuted);
    Painter::LinkRow invalidLink = RowSamples::linkRow();
    invalidLink.iconColor = static_cast<Color>(100000);
    const QImage linkImage = render([&](QPainter& painter)
    {
        Painter::paintIconButton(&painter, QRectF(0, 0, 24, 24), invalidButton, Painter::State());
        Painter::paintLinkRow(&painter, QRectF(0, 30, 328, 50), invalidLink, Painter::State());
    }, QSize(328, 80));
    QCOMPARE(countLeakPixels(linkImage), 0);
    QVERIFY(isCloser(strongestInk(linkImage, QRect(10, 47, 16, 16), surface), token(Color::IconDefault), token(Color::IconMuted)));

    // A star hidden by the application gives its place to the texts (review C16)
    pdf::PDFRecentFileRow fileRow(QStringLiteral("Project_Report.pdf"), QStringLiteral("C:\\Users"), QStringLiteral("Today"));
    const int minimumWithStar = fileRow.minimumSizeHint().width();
    fileRow.getStarButton()->hide();
    QVERIFY(!fileRow.isStarVisible());
    QVERIFY(!fileRow.getContent().starVisible);
    QCOMPARE(fileRow.minimumSizeHint().width(), minimumWithStar - 24 - 12);
    fileRow.setStarVisible(true);
    QVERIFY(fileRow.isStarVisible());
    QCOMPARE(fileRow.minimumSizeHint().width(), minimumWithStar);

    // A narrow tool row drops its tag as the painter (review N1)
    pdf::PDFToolRow toolRow(QStringLiteral("Merge PDF"), QStringLiteral("Combine"), QStringLiteral("grid"), pdf::PDFAccentHue::Blue);
    toolRow.setTag(QStringLiteral("Advanced"));
    const int tagWidth = Painter::getTagSize(Painter::Tag{ QStringLiteral("Advanced") }).width();
    toolRow.resize(380, 48);
    toolRow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&toolRow));
    QCOMPARE(toolRow.getTagWidget()->geometry().width(), tagWidth);
    QVERIFY(Painter::isToolRowTagShown(QRectF(0, 0, 380, 48), toolRow.getContent()));
    toolRow.resize(120, 48);
    QVERIFY(!Painter::isToolRowTagShown(QRectF(0, 0, 120, 48), toolRow.getContent()));
    QVERIFY(toolRow.getTagWidget()->geometry().isEmpty());
    QVERIFY(!toolRow.getTagWidget()->isHidden());
    toolRow.resize(380, 48);
    QCOMPARE(toolRow.getTagWidget()->geometry().width(), tagWidth);
    QVERIFY(!Painter::isToolRowTagShown(QRectF(0, 0, 380, 48), Painter::ToolRow()));

    // Right-to-left chevrons point to the left (review F2)
    const QImage rightToLeftLink = render([&](QPainter& painter) { Painter::paintLinkRow(&painter, QRectF(0, 0, 328, 50), RowSamples::linkRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QSize(328, 50));
    const QImage chevronLeft = render([&](QPainter& painter) { pdf::PDFStylePainter::icon(&painter, QRectF(10, 18, 14, 14), QStringLiteral("chevron-left"), Color::IconMuted); }, QSize(328, 50));
    const QString chevronDifference = describeDifference(rightToLeftLink.copy(QRect(8, 16, 18, 18)), chevronLeft.copy(QRect(8, 16, 18, 18)), 3);
    QVERIFY2(chevronDifference.isEmpty(), qPrintable(chevronDifference));
    struct ChevronRow
    {
        std::function<void(QPainter&)> paint;
        QRectF chevron;
    };
    const ChevronRow chevronRows[] =
    {
        { [](QPainter& painter) { Painter::paintRecentToolRow(&painter, QRectF(0, 0, 342, 48), RowSamples::recentToolRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QRectF(12, 17, 14, 14) },
        { [](QPainter& painter) { Painter::paintCollectionRow(&painter, QRectF(0, 0, 342, 48), RowSamples::collectionRow(), state(QStyle::State_None, Qt::RightToLeft)); }, QRectF(8, 17, 14, 14) }
    };
    for (const ChevronRow& chevronRow : chevronRows)
    {
        const QImage rowImage = render(chevronRow.paint, QSize(342, 48));
        const QRectF chevron = chevronRow.chevron;
        const QImage expected = render([&](QPainter& painter) { pdf::PDFStylePainter::icon(&painter, chevron, QStringLiteral("chevron-left"), Color::IconMuted); }, QSize(342, 48));
        const QRect area = chevron.toRect().adjusted(-2, -2, 2, 2);
        const QString difference = describeDifference(rowImage.copy(area), expected.copy(area), 3);
        QVERIFY2(difference.isEmpty(), qPrintable(difference));
    }

    // A long number of an issue wraps in its column of 44 px (review F5)
    Painter::IssueRow longIssue = RowSamples::issueRow();
    longIssue.number = QStringLiteral("#1234567");
    const QImage longIssueImage = render([&](QPainter& painter) { Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), longIssue, state(QStyle::State_None)); }, QSize(786, 52));
    const QImage shortIssueImage = render([&](QPainter& painter) { Painter::paintIssueRow(&painter, QRectF(0, 0, 786, 52), RowSamples::issueRow(), state(QStyle::State_None)); }, QSize(786, 52));
    QVERIFY(countPixels(longIssueImage, QRect(46, 8, 44, 9), token(Color::TextAccent), 40) > 0);
    QCOMPARE(countPixels(shortIssueImage, QRect(46, 8, 44, 9), token(Color::TextAccent), 40), 0);
    QCOMPARE(countPixels(longIssueImage, QRect(91, 0, 10, 52), token(Color::TextAccent), 40), 0);

    // The height of a version row follows the summary also with the cached height (review N8)
    pdf::PDFVersionRow versionRow(QStringLiteral("v1.5.3"), QStringLiteral("Sep 14"), QStringLiteral("Short"));
    const int shortHeight = versionRow.heightForWidth(200);
    QCOMPARE(versionRow.heightForWidth(200), shortHeight);
    versionRow.setSummary(RowSamples::versionRow().summary + QStringLiteral(" ") + RowSamples::versionRow().summary);
    QVERIFY(versionRow.heightForWidth(200) > shortHeight);
    QCOMPARE(versionRow.heightForWidth(200), Painter::getVersionRowHeight(versionRow.getContent(), 200));
}

QTEST_MAIN(FluentComponentsTest)

#include "tst_fluentcomponentstest.moc"
