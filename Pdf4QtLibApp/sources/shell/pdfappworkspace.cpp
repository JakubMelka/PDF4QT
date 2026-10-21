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

#include "pdfappworkspace.h"
#include "pdfappcontext.h"
#include "pdfdatabasenotifier.h"
#include "pdfappsearchwidgets.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentpainter.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QStyle>
#include <QtMath>
#include <QVBoxLayout>
#include <QVersionNumber>

#include <array>
#include <utility>

namespace pdf
{

/// Geometry of the banners of the storage (portable-banner 941:138467, storage-banner-* 958:28036)
class PDFAppStorageBannerHelper
{
public:
    PDFAppStorageBannerHelper() = delete;

    static constexpr qreal RADIUS = 12.0;
    static constexpr int PADDING_LEFT = 14;
    static constexpr int PADDING_RIGHT = 16;
    static constexpr int PADDING_VERTICAL = 12;
    static constexpr int ICON_CIRCLE = 36;
    static constexpr int ICON = 20;
    static constexpr int GAP = 12;

    /// Hue of the banner: portable mode and read only settings are information
    /// (blue, storage-banner-read-only 958:28548), the fallback and the replaced
    /// database warn (amber, 958:28449, 958:28641)
    static PDFAccentHue getHue(PDFAppStorageBanner::Kind kind)
    {
        return kind == PDFAppStorageBanner::Kind::Portable || kind == PDFAppStorageBanner::Kind::ReadOnly ? PDFAccentHue::Blue : PDFAccentHue::Amber;
    }

    static QString getIconName(PDFAppStorageBanner::Kind kind)
    {
        return kind == PDFAppStorageBanner::Kind::Portable ? QStringLiteral("portable") : QStringLiteral("database");
    }

    /// The icon circle is painted in the leading padding (border 1 + 14 + 36 + 12);
    /// the margins of a layout are not mirrored, so they follow the direction here
    static void updateMargins(QWidget* banner)
    {
        QLayout* layout = banner ? banner->layout() : nullptr;
        if (!layout)
        {
            return;
        }

        const int leading = 1 + PADDING_LEFT + ICON_CIRCLE + GAP;
        const int trailing = 1 + PADDING_RIGHT;
        const bool isRightToLeft = banner->layoutDirection() == Qt::RightToLeft;
        layout->setContentsMargins(isRightToLeft ? trailing : leading, 1 + PADDING_VERTICAL, isRightToLeft ? leading : trailing, 1 + PADDING_VERTICAL);
    }
};

/// Logo of a target of the footer (windows-logo 30:102, linux-logo 74:799,
/// apple-logo 30:106 of pdf4qt-home 28:4): a filled icon of 14 px in icon/default
class PDFAppFooterLogo : public QWidget
{
public:
    static constexpr int SIZE = 14;

    explicit PDFAppFooterLogo(const QString& iconName, QWidget* parent) :
        QWidget(parent),
        m_iconName(iconName)
    {
        setObjectName(QStringLiteral("footerLogo"));
        setFixedSize(SIZE, SIZE);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppFooterLogo::update));
    }

    const QString& getIconName() const { return m_iconName; }

protected:
    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        // The logos are filled shapes: the icon engine fills them by the color
        QPainter painter(this);
        PDFStylePainter::icon(&painter, QRectF(0, 0, SIZE, SIZE), m_iconName, PDFThemeColors::IconDefault);
    }

private:
    QString m_iconName;
};

/// Targets of the footer (footer-os-targets 28:219): Windows / Linux / macOS, each
/// a logo of 14 px, 6 px and the text, the targets after a gap of 16. The widget
/// places its parts itself and hides them, when it is narrower than they need (a
/// logo always with its text), so the row of the footer shrinks in a narrow window
/// without overlaps
class PDFAppFooterTargets : public QWidget
{
public:
    static constexpr int GAP = 16;
    static constexpr int LOGO_GAP = 6;

    explicit PDFAppFooterTargets(QWidget* parent) :
        QWidget(parent)
    {
        setObjectName(QStringLiteral("footerTargets"));
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

        for (const QString& iconName : { QStringLiteral("windows"), QStringLiteral("linux"), QStringLiteral("macos") })
        {
            m_logos.append(new PDFAppFooterLogo(iconName, this));
            m_labels.append(new PDFLabel(QString(), PDFThemeFonts::Role::CaptionSemiBold, PDFThemeColors::TextTertiary, this));
        }
        updateTexts();
    }

    virtual QSize sizeHint() const override
    {
        int width = 0;
        int height = PDFAppFooterLogo::SIZE;
        for (const QPointer<PDFLabel>& label : m_labels)
        {
            if (label)
            {
                const QSize labelSize = label->sizeHint();
                width += GAP + PDFAppFooterLogo::SIZE + LOGO_GAP + labelSize.width();
                height = qMax(height, labelSize.height());
            }
        }
        return QSize(width, height);
    }

    virtual QSize minimumSizeHint() const override
    {
        return QSize(0, sizeHint().height());
    }

protected:
    virtual void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        placeTargets();
    }

    virtual void changeEvent(QEvent* event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::LanguageChange)
        {
            updateTexts();
        }
        else if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        {
            placeTargets();
        }
    }

private:
    void updateTexts()
    {
        const std::array<QString, 3> texts = { PDFAppFooter::tr("Windows"), PDFAppFooter::tr("Linux"), PDFAppFooter::tr("macOS") };
        for (qsizetype i = 0; i < m_labels.size() && i < qsizetype(texts.size()); ++i)
        {
            if (m_labels[i])
            {
                m_labels[i]->setText(texts[size_t(i)]);
            }
        }
        updateGeometry();
        placeTargets();
    }

    void placeTargets()
    {
        // All targets or none (a part of the list would say something else)
        const bool isShown = width() >= sizeHint().width();
        int x = 0;
        for (qsizetype i = 0; i < m_labels.size() && i < m_logos.size(); ++i)
        {
            PDFLabel* label = m_labels[i];
            PDFAppFooterLogo* logo = m_logos[i];
            if (!label || !logo)
            {
                continue;
            }

            // The logo and the text are centered on the row (the text 0.5 px below
            // the top of the logo in Figma, 13 of 14)
            x += GAP;
            const QRect logoRect(x, (height() - PDFAppFooterLogo::SIZE) / 2, PDFAppFooterLogo::SIZE, PDFAppFooterLogo::SIZE);
            x += PDFAppFooterLogo::SIZE + LOGO_GAP;
            const QSize labelSize = label->sizeHint();
            const QRect labelRect(x, (height() - labelSize.height()) / 2, labelSize.width(), labelSize.height());
            x += labelSize.width();

            logo->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logoRect));
            label->setGeometry(QStyle::visualRect(layoutDirection(), rect(), labelRect));
            logo->setVisible(isShown);
            label->setVisible(isShown);
        }
    }

    QList<QPointer<PDFAppFooterLogo>> m_logos;
    QList<QPointer<PDFLabel>> m_labels;
};

PDFAppScrollArea::PDFAppScrollArea(QWidget* content, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("scrollArea"));
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);

    // The area is no stop of Tab; a focused child is scrolled into the view
    setFocusPolicy(Qt::NoFocus);
    if (QApplication* application = qobject_cast<QApplication*>(QCoreApplication::instance()))
    {
        connect(application, &QApplication::focusChanged, this, &PDFAppScrollArea::onFocusChanged);
    }

    // The viewport shows what is below it (the canvas, the panel of the sidebar)
    viewport()->setAutoFillBackground(false);
    setContent(content);
}

PDFAppScrollArea::~PDFAppScrollArea() = default;

void PDFAppScrollArea::onFocusChanged(QWidget* old, QWidget* now)
{
    Q_UNUSED(old);

    // A child focused by the keyboard is moved into the view, no margin is added
    // (the paddings belong to the content). A click focuses a widget under the
    // pointer, which is visible: the view does not move then (a widget taller than
    // the view would be centered by the press, and the release would hit another
    // place of the content)
    if (QGuiApplication::mouseButtons() != Qt::NoButton)
    {
        return;
    }

    QWidget* content = widget();
    if (!now || !content || !content->isAncestorOf(now) || !now->isVisibleTo(content))
    {
        return;
    }
    ensureWidgetVisible(now, 0, 0);
}

void PDFAppScrollArea::setContent(QWidget* content)
{
    if (!content)
    {
        return;
    }

    // QScrollArea::setWidget fills the content by the palette (the leak color)
    setWidget(content);
    content->setAutoFillBackground(false);
}

PDFAppLogoTile::PDFAppLogoTile(QWidget* parent) :
    QWidget(parent)
{
    setFixedSize(36, 36);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppLogoTile::update));
}

QSize PDFAppLogoTile::sizeHint() const
{
    return QSize(36, 36);
}

void PDFAppLogoTile::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // logo-icon-bg: bg/accent, radius 8, icon file-text 20 in icon/on-accent
    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF rect(0, 0, 36, 36);
    PDFStylePainter::fill(&painter, rect, PDFStylePainter::Radii(8.0), PDFThemeColors::BgAccent);
    PDFStylePainter::icon(&painter, PDFStylePainter::centered(rect, QSizeF(20.0, 20.0)), QStringLiteral("file-text"), PDFThemeColors::IconOnAccent);
}

PDFAppParagraph::PDFAppParagraph(const QFont& font, int lineHeight, PDFThemeColors::Color color, QWidget* parent) :
    BaseClass(QString(), PDFThemeFonts::Role::Body, color, parent),
    m_lineHeight(qMax(1, lineHeight))
{
    setFont(font);
    setTextFormat(Qt::PlainText);
    setWordWrap(true);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

QStringList PDFAppParagraph::getLines(int width) const
{
    const QMargins margins = contentsMargins();
    return PDFComponentPainter::wrapText(text(), font(), width - margins.left() - margins.right());
}

bool PDFAppParagraph::hasHeightForWidth() const
{
    return true;
}

int PDFAppParagraph::heightForWidth(int width) const
{
    // A layout asks with the width of its cell (an item with an alignment gets
    // less), the widget never gets more than its maximum width
    const QMargins margins = contentsMargins();
    const int boundedWidth = qBound(minimumWidth(), width, maximumWidth());
    return int(getLines(boundedWidth).size()) * m_lineHeight + margins.top() + margins.bottom();
}

QSize PDFAppParagraph::sizeHint() const
{
    // A paragraph of a fixed width (Page Not Found) knows its height, otherwise
    // the text on one line (the layout asks heightForWidth for the real width)
    const QMargins margins = contentsMargins();
    int width = maximumWidth();
    if (minimumWidth() != width)
    {
        width = qCeil(PDFStylePainter::textWidth(text(), font())) + margins.left() + margins.right();
    }
    return QSize(width, qMax(heightForWidth(width), m_lineHeight));
}

QSize PDFAppParagraph::minimumSizeHint() const
{
    const QMargins margins = contentsMargins();
    const int width = qCeil(PDFComponentPainter::getLongestWordWidth(text(), font())) + margins.left() + margins.right();
    return QSize(qMin(width, sizeHint().width()), m_lineHeight);
}

void PDFAppParagraph::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const QRect rect = contentsRect();
    const QStringList lines = getLines(width());
    const Qt::Alignment horizontal = QStyle::visualAlignment(layoutDirection(), alignment()) & Qt::AlignHorizontal_Mask;
    const QColor color = PDFStylePainter::color(isEnabled() ? getTextColor() : PDFThemeColors::TextDisabled);

    QPainter painter(this);
    for (qsizetype i = 0; i < lines.size(); ++i)
    {
        const QRectF lineRect(rect.left(), rect.top() + i * m_lineHeight, rect.width(), m_lineHeight);
        PDFStylePainter::text(&painter, lineRect, int(horizontal | Qt::AlignVCenter | Qt::TextSingleLine), lines[i], font(), color);
    }
}

PDFAppSidebar::PDFAppSidebar(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    m_context(context)
{
    setObjectName(QStringLiteral("sidebar"));
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::PANEL);
    setFixedWidth(WIDTH);

    // Paddings of the design: sidebar 20 / 16, logo-area 16 / 8 / 16, nav groups 10,
    // bottom-area 14 / 8 (the switch is 182 wide); the switch is 8 px above the
    // bottom of the window (owner decision O11)
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 20, 10, 8);
    layout->setSpacing(0);

    QHBoxLayout* logoLayout = new QHBoxLayout();
    logoLayout->setContentsMargins(6, 8, 6, 16);
    logoLayout->setSpacing(10);
    logoLayout->addWidget(new PDFAppLogoTile(this), 0, Qt::AlignVCenter);
    // logo-text-group: Bold 14 (letter spacing -0.2), 2, Regular 10, the group of
    // 31 px centered on the tile, so 3 px below its top (the labels are not
    // stretched to the height of the tile; the line of Regular 10 is 13 px here)
    QVBoxLayout* logoTexts = new QVBoxLayout();
    logoTexts->setContentsMargins(0, 0, 0, 0);
    logoTexts->setSpacing(2);
    PDFLabel* logoTitle = new PDFLabel(tr("PDF4QT"), PDFThemeFonts::Role::TileTitle, PDFThemeColors::TextPrimary, this);
    logoTitle->setObjectName(QStringLiteral("logoTitle"));
    QFont logoTitleFont = logoTitle->font();
    logoTitleFont.setLetterSpacing(QFont::AbsoluteSpacing, -0.2);
    logoTitle->setFont(logoTitleFont);
    PDFLabel* logoSubtitle = new PDFLabel(tr("Open. Edit. Do more."), PDFThemeFonts::Role::Tiny, PDFThemeColors::TextMuted, this);
    logoSubtitle->setObjectName(QStringLiteral("logoSubtitle"));
    logoTexts->addSpacing(3);
    logoTexts->addWidget(logoTitle);
    logoTexts->addWidget(logoSubtitle);
    logoTexts->addStretch(1);
    logoLayout->addLayout(logoTexts, 1);
    layout->addLayout(logoLayout);

    // Navigation in three groups, in the order of PDFAppNavItem
    m_navigation = new PDFNavList(PDFNavItem::Kind::Home, this);
    m_navigation->setObjectName(QStringLiteral("navigation"));
    m_navigation->setAccessibleName(tr("Navigation"));
    m_navigation->addItem(tr("Home"), QStringLiteral("home"));
    m_navigation->addItem(tr("Open"), QStringLiteral("folder"));
    m_navigation->addItem(tr("Recent"), QStringLiteral("clock"));
    m_navigation->addItem(tr("Tools"), QStringLiteral("tools"));
    m_navigation->addItem(tr("AI"), QStringLiteral("ai"));
    m_navigation->addGroup();
    m_navigation->addItem(tr("What's New"), QStringLiteral("gift"));
    m_navigation->addItem(tr("About"), QStringLiteral("info"));
    m_navigation->addItem(tr("Support"), QStringLiteral("headphones"));
    m_navigation->addItem(tr("Sponsor"), QStringLiteral("heart"));
    m_navigation->addGroup();
    m_navigation->addItem(tr("Settings"), QStringLiteral("settings"));
    m_navigation->setCurrentIndex(-1);

    // The navigation scrolls in a low window, the logo and the switch stay
    m_navigationArea = new PDFAppScrollArea(m_navigation, this);
    m_navigationArea->setObjectName(QStringLiteral("navigationArea"));
    m_navigationArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_navigationArea, 1);
    layout->addSpacing(14);

    // Switch of the color scheme (Segment 47:157, the sun of Light is icon/sun)
    m_themeSwitch = new PDFSegmentedControl(this);
    m_themeSwitch->setObjectName(QStringLiteral("themeSwitch"));
    m_themeSwitch->setAccessibleName(tr("Color scheme"));
    const int lightSegment = m_themeSwitch->addSegment(tr("Light"), QStringLiteral("sun"));
    m_themeSwitch->addSegment(tr("Dark"), QStringLiteral("moon"));
    m_themeSwitch->addSegment(tr("Auto"), QStringLiteral("monitor"));
    m_themeSwitch->setSegmentIconColor(lightSegment, PDFThemeColors::IconSun);
    m_themeSwitch->setFixedWidth(182);
    layout->addWidget(m_themeSwitch, 0, Qt::AlignHCenter);

    updateThemeSwitch();

    connect(m_navigation, &PDFNavList::currentIndexChanged, this, &PDFAppSidebar::onNavigationIndexChanged);
    connect(m_themeSwitch, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppSidebar::onThemeIndexChanged);

    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppSidebar::onSettingChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppSidebar::onEverythingChanged);
    }
}

PDFAppSidebar::~PDFAppSidebar() = default;

PDFAppNavItem PDFAppSidebar::getCurrentItem() const
{
    if (!m_navigation)
    {
        return PDFAppNavItem::None;
    }

    const int index = m_navigation->getCurrentIndex();
    if (index < int(PDFAppNavItem::Home) || index > int(PDFAppNavItem::Settings))
    {
        return PDFAppNavItem::None;
    }
    return static_cast<PDFAppNavItem>(index);
}

void PDFAppSidebar::setCurrentItem(PDFAppNavItem item)
{
    if (!m_navigation || getCurrentItem() == item)
    {
        return;
    }

    // The highlight follows the navigation, it does not navigate
    QPointer<PDFAppSidebar> guard(this);
    m_isUpdating = true;
    const int index = item >= PDFAppNavItem::Home && item <= PDFAppNavItem::Settings ? int(item) : -1;
    m_navigation->setCurrentIndex(index);
    if (guard)
    {
        m_isUpdating = false;
    }
}

PDFAppRoute PDFAppSidebar::getRoute(PDFAppNavItem item)
{
    switch (item)
    {
        case PDFAppNavItem::Home:
            return PDFAppRoutes::home();
        case PDFAppNavItem::Open:
            return PDFAppRoutes::open();
        case PDFAppNavItem::Recent:
            return PDFAppRoutes::recent();
        case PDFAppNavItem::Tools:
            return PDFAppRoutes::tools();
        case PDFAppNavItem::Ai:
            return PDFAppRoutes::ai();
        case PDFAppNavItem::WhatsNew:
            return PDFAppRoutes::whatsNew();
        case PDFAppNavItem::About:
            return PDFAppRoutes::about();
        case PDFAppNavItem::Support:
            return PDFAppRoutes::support();
        case PDFAppNavItem::Sponsor:
            return PDFAppRoutes::sponsor();
        case PDFAppNavItem::Settings:
            return PDFAppRoutes::settings();
        case PDFAppNavItem::None:
            break;
    }
    return PDFAppRoute();
}

int PDFAppSidebar::getSchemeIndex(const QString& colorScheme)
{
    if (colorScheme == PDFSettingValues::colorSchemeLight())
    {
        return 0;
    }
    if (colorScheme == PDFSettingValues::colorSchemeDark())
    {
        return 1;
    }
    return 2;
}

QString PDFAppSidebar::getSchemeValue(int index)
{
    switch (index)
    {
        case 0:
            return PDFSettingValues::colorSchemeLight();
        case 1:
            return PDFSettingValues::colorSchemeDark();
        default:
            return PDFSettingValues::colorSchemeAuto();
    }
}

void PDFAppSidebar::onNavigationIndexChanged(int index)
{
    if (m_isUpdating || index < int(PDFAppNavItem::Home) || index > int(PDFAppNavItem::Settings))
    {
        return;
    }

    // The navigator moves the highlight, when it shows the route (no item is
    // highlighted, when the route ends on Page Not Found)
    const PDFAppRoute route = getRoute(static_cast<PDFAppNavItem>(index));
    Q_EMIT navigationRequested(route, PDFAppNavigationMode::Push);
}

void PDFAppSidebar::onThemeIndexChanged(int index)
{
    if (m_isUpdating || !m_context)
    {
        return;
    }

    const PDFDatabaseError error = m_context->getSettings().setValue(PDFSettingKeys::colorScheme(), getSchemeValue(index));
    if (error.isError())
    {
        // Another instance holds the database: the switch shows the stored value
        qWarning("The color scheme cannot be stored: %s", qPrintable(error.toString()));
        updateThemeSwitch();
        return;
    }

    // The own commit is applied at once (the notifier would apply it later)
    m_context->applyColorScheme();
}

void PDFAppSidebar::onSettingChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && key == PDFSettingKeys::colorScheme())
    {
        updateThemeSwitch();
    }
}

void PDFAppSidebar::onEverythingChanged()
{
    updateThemeSwitch();
}

void PDFAppSidebar::updateThemeSwitch()
{
    if (!m_themeSwitch)
    {
        return;
    }

    const QString colorScheme = m_context ? m_context->getSettings().getString(PDFSettingKeys::colorScheme()) : PDFSettingValues::colorSchemeAuto();
    const int index = getSchemeIndex(colorScheme);
    if (m_themeSwitch->getCurrentIndex() == index)
    {
        return;
    }

    QPointer<PDFAppSidebar> guard(this);
    m_isUpdating = true;
    m_themeSwitch->setCurrentIndex(index);
    if (guard)
    {
        m_isUpdating = false;
    }
}

PDFAppCrumbLink::PDFAppCrumbLink(const QString& text, QWidget* parent) :
    BaseClass(FocusRing::Outside, parent)
{
    setFocusRingRadius(4.0);
    setFocusPolicy(Qt::TabFocus);
    setCursor(Qt::PointingHandCursor);
    setText(text);
}

QFont PDFAppCrumbLink::getCrumbFont(bool isUnderlined)
{
    QFont font = PDFThemeFonts::getFont(QFont::Medium, 12);
    font.setUnderline(isUnderlined);
    return font;
}

void PDFAppCrumbLink::paintContent(QPainter* painter, const QRect& rect, const PDFComponentPainter::State& state) const
{
    PDFComponentPainter::DirectionGuard directionGuard(painter, state);
    PDFThemeColors::Color color = PDFThemeColors::TextAccent;
    if (!state.isEnabled())
    {
        color = PDFThemeColors::TextDisabled;
    }
    else if (state.isPressed())
    {
        color = PDFThemeColors::TextAccentPressed;
    }

    PDFStylePainter::text(painter, rect, int(Qt::AlignVCenter | Qt::AlignHCenter | Qt::TextSingleLine), text(), getCrumbFont(state.isHovered()), PDFStylePainter::color(color));
}

QSize PDFAppCrumbLink::getContentSizeHint() const
{
    return QSize(int(std::ceil(PDFStylePainter::textWidth(text(), getCrumbFont()))), 15);
}

PDFAppBreadcrumb::PDFAppBreadcrumb(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("breadcrumb"));
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(6);
    m_layout->addStretch(1);
}

void PDFAppBreadcrumb::clear()
{
    if (!m_layout)
    {
        return;
    }

    // A link may be the sender of the navigation, which has replaced the items
    while (m_layout->count() > 1)
    {
        QLayoutItem* item = m_layout->takeAt(0);
        if (QWidget* widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    m_links.clear();
    m_routes.clear();
}

void PDFAppBreadcrumb::setItems(const QList<PDFAppBreadcrumbItem>& items)
{
    clear();
    for (const PDFAppBreadcrumbItem& item : items)
    {
        if (item.route.isValid())
        {
            addLink(item.text, item.route.toString());
        }
        else
        {
            addCurrent(item.text);
        }
    }
}

void PDFAppBreadcrumb::addSeparator()
{
    if (!m_layout || m_layout->count() <= 1)
    {
        return;
    }

    PDFLabel* separator = new PDFLabel(QString(QChar(0x203A)), PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextTertiary, this);
    separator->setFont(PDFThemeFonts::getFont(QFont::Medium, 12));
    m_layout->insertWidget(m_layout->count() - 1, separator);
}

void PDFAppBreadcrumb::addLink(const QString& text, const QString& route)
{
    if (!m_layout)
    {
        return;
    }

    addSeparator();
    PDFAppCrumbLink* link = new PDFAppCrumbLink(text, this);
    m_layout->insertWidget(m_layout->count() - 1, link);
    m_links.append(link);
    m_routes.append(route);
    connect(link, &PDFAppCrumbLink::clicked, this, &PDFAppBreadcrumb::onLinkClicked);
}

void PDFAppBreadcrumb::addCurrent(const QString& text)
{
    if (!m_layout)
    {
        return;
    }

    addSeparator();
    PDFLabel* label = new PDFLabel(QString(), PDFThemeFonts::Role::SmallMedium, PDFThemeColors::TextTertiary, this);
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    label->setFont(PDFThemeFonts::getFont(QFont::Medium, 12));
    m_layout->insertWidget(m_layout->count() - 1, label);
}

QList<PDFAppCrumbLink*> PDFAppBreadcrumb::getLinks() const
{
    QList<PDFAppCrumbLink*> links;
    for (const QPointer<PDFAppCrumbLink>& link : m_links)
    {
        if (link)
        {
            links.append(link);
        }
    }
    return links;
}

QString PDFAppBreadcrumb::getRoute(const PDFAppCrumbLink* link) const
{
    for (qsizetype i = 0; i < m_links.size(); ++i)
    {
        if (m_links.at(i) == link)
        {
            return m_routes.value(i);
        }
    }
    return QString();
}

void PDFAppBreadcrumb::onLinkClicked()
{
    const PDFAppCrumbLink* link = qobject_cast<const PDFAppCrumbLink*>(sender());
    const QString route = getRoute(link);
    if (!route.isEmpty())
    {
        Q_EMIT linkActivated(route);
    }
}

PDFAppWorkspaceHeader::PDFAppWorkspaceHeader(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("workspaceHeader"));

    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(24);

    // welcome-message-group: title Bold 28 (the design has Extra Bold, which is
    // not bundled - review I-35) in a box of 34 px, the subtitle Regular 14
    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(4);
    m_breadcrumb = new PDFAppBreadcrumb(this);
    m_breadcrumb->hide();
    textLayout->addWidget(m_breadcrumb);
    connect(m_breadcrumb, &PDFAppBreadcrumb::linkActivated, this, &PDFAppWorkspaceHeader::onBreadcrumbActivated);
    m_title = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::Bold, 28), PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("headerTitle"));
    m_title->setFixedHeight(34);
    m_title->hide();
    m_subtitle = new PDFAppHeaderText(PDFThemeFonts::getFont(QFont::Normal, 14), PDFThemeColors::TextTertiary, this);
    m_subtitle->setObjectName(QStringLiteral("headerSubtitle"));
    m_subtitle->hide();
    textLayout->addWidget(m_title);
    textLayout->addWidget(m_subtitle);
    layout->addLayout(textLayout, 1);

    m_searchField = new PDFLineEdit(this);
    m_searchField->setObjectName(QStringLiteral("globalSearch"));
    m_searchField->setSearchField();
    // Search field Filled=True (49:143, 941:7123): the clear button x-circle at the right
    m_searchField->setClearButtonEnabled(true);
    m_searchField->setPlaceholderText(tr("Search tools, documents, or help..."));
    m_searchField->setAccessibleName(tr("Search tools, documents, or help"));
    m_searchField->setFixedWidth(320);
    m_searchField->setMaxLength(256);
    layout->addWidget(m_searchField, 0, Qt::AlignVCenter);

    // The popup of the results while typing (a child of the field, PDFAppSearchController)
    new PDFAppSearchController(m_searchField);

    connect(m_searchField, &QLineEdit::returnPressed, this, &PDFAppWorkspaceHeader::onReturnPressed);
}

PDFAppWorkspaceHeader::~PDFAppWorkspaceHeader() = default;

void PDFAppWorkspaceHeader::setTexts(const QString& title, const QString& subtitle, const QString& subtitleAccent)
{
    if (m_title)
    {
        m_title->setHeaderText(title);
        m_title->setVisible(!title.isEmpty());
    }

    if (m_subtitle)
    {
        m_subtitle->setHeaderText(subtitle, subtitleAccent);
        m_subtitle->setVisible(!subtitle.isEmpty());
    }
}

PDFAppHeaderText::PDFAppHeaderText(const QFont& font, PDFThemeColors::Color color, QWidget* parent) :
    BaseClass(parent)
{
    setFont(font);
    setTextColor(color);
    setTextFormat(Qt::PlainText);

    // The layout gives the width (the search field keeps its width), the text is elided
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

PDFAppHeaderText::~PDFAppHeaderText() = default;

void PDFAppHeaderText::setHeaderText(const QString& text, const QString& accent)
{
    // A text of a page is never a rich text, the tooltip shows the whole text of
    // an elided text only (event)
    setText(text);
    setToolTip(text);
    if (m_accent != accent)
    {
        m_accent = accent;
        updateGeometry();
        update();
    }
}

qsizetype PDFAppHeaderText::getAccentIndex() const
{
    return m_accent.isEmpty() ? -1 : text().indexOf(m_accent);
}

QList<QTextLayout::FormatRange> PDFAppHeaderText::getFormats() const
{
    QList<QTextLayout::FormatRange> formats;
    const qsizetype index = getAccentIndex();
    if (index >= 0)
    {
        QTextLayout::FormatRange range;
        range.start = int(index);
        range.length = int(m_accent.size());
        range.format.setFontWeight(QFont::DemiBold);
        range.format.setForeground(PDFThemeManager::getInstance()->getColor(PDFThemeColors::TextAccent));
        formats << range;
    }
    return formats;
}

qreal PDFAppHeaderText::getTextWidth() const
{
    if (text().isEmpty())
    {
        return 0.0;
    }

    if (getAccentIndex() < 0)
    {
        return PDFStylePainter::textWidth(text(), font());
    }

    QTextLayout layout(text(), font());
    layout.setFormats(getFormats());
    layout.beginLayout();
    QTextLine line = layout.createLine();
    layout.endLayout();
    return line.isValid() ? line.naturalTextWidth() : 0.0;
}

bool PDFAppHeaderText::isElided() const
{
    return getTextWidth() > contentsRect().width();
}

QSize PDFAppHeaderText::sizeHint() const
{
    return QSize(qCeil(getTextWidth()), QFontMetrics(font()).height());
}

QSize PDFAppHeaderText::minimumSizeHint() const
{
    return QSize(1, QFontMetrics(font()).height());
}

void PDFAppHeaderText::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (text().isEmpty())
    {
        return;
    }

    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    const QRectF box(contentsRect());
    const qsizetype accentIndex = getAccentIndex();
    if (accentIndex < 0 || isElided())
    {
        const PDFComponentPainter::State state(QStyle::State_Enabled, layoutDirection());
        PDFComponentPainter::drawElidedText(&painter, state, box, text(), font(), getTextColor(), PDFComponentPainter::TextKind::Interface);
        return;
    }

    // The whole text with its stressed part: one line at the leading edge,
    // centered vertically (the text fits, isElided)
    QTextLayout layout(text(), font());
    QTextOption option;
    option.setTextDirection(layoutDirection());
    option.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(option);
    layout.setFormats(getFormats());
    layout.beginLayout();
    QTextLine line = layout.createLine();
    layout.endLayout();
    if (!line.isValid())
    {
        return;
    }

    const qreal width = line.naturalTextWidth();
    const qreal x = layoutDirection() == Qt::RightToLeft ? box.right() + 1.0 - width : box.left();
    const qreal y = box.top() + (box.height() - line.height()) / 2.0;
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(PDFThemeManager::getInstance()->getColor(getTextColor()));
    layout.draw(&painter, QPointF(x - line.x(), y - line.y()));
}

bool PDFAppHeaderText::event(QEvent* event)
{
    // The whole text is the tooltip of an elided text only
    if (event->type() == QEvent::ToolTip && !isElided())
    {
        event->ignore();
        return true;
    }
    return BaseClass::event(event);
}

void PDFAppWorkspaceHeader::setBreadcrumbs(const QList<PDFAppBreadcrumbItem>& items)
{
    if (m_breadcrumb)
    {
        m_breadcrumb->setItems(items);
        m_breadcrumb->setVisible(!items.isEmpty());
    }
}

void PDFAppWorkspaceHeader::onBreadcrumbActivated(const QString& route)
{
    const PDFAppRoute parsedRoute = PDFAppRoute::parse(route);
    if (parsedRoute.isValid())
    {
        Q_EMIT breadcrumbActivated(parsedRoute);
    }
}

void PDFAppWorkspaceHeader::onReturnPressed()
{
    if (!m_searchField)
    {
        return;
    }

    const QString text = m_searchField->text().simplified();
    if (!text.isEmpty())
    {
        Q_EMIT searchRequested(text);
    }
}

PDFAppStorageBanner::PDFAppStorageBanner(QWidget* parent) :
    BaseClass(parent)
{
    using Helper = PDFAppStorageBannerHelper;

    setObjectName(QStringLiteral("storageBanner"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    // The icon circle is painted in the leading padding (border 1 + 14 + 36 + 12)
    QHBoxLayout* layout = new QHBoxLayout(this);
    m_mainLayout = layout;
    Helper::updateMargins(this);
    layout->setSpacing(Helper::GAP);

    QVBoxLayout* textLayout = new QVBoxLayout();
    m_textLayout = textLayout;
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    m_titleLabel = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, this);
    m_titleLabel->setObjectName(QStringLiteral("bannerTitle"));
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setWordWrap(true);
    // The text Regular 12 with the line height 16 (portable-banner 941:138473)
    m_textLabel = new PDFAppParagraph(PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 16, PDFThemeColors::TextSecondary, this);
    m_textLabel->setObjectName(QStringLiteral("bannerText"));
    textLayout->addWidget(m_titleLabel);
    textLayout->addWidget(m_textLabel);
    layout->addLayout(textLayout, 1);

    // The buttons are created in their container (beside or below the text)
    m_actions = new QWidget(this);
    m_actions->setObjectName(QStringLiteral("bannerActions"));
    m_actions->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    QHBoxLayout* actions = new QHBoxLayout(m_actions);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);

    m_tryAgainButton = createButton(tr("Try Again"));
    m_tryAgainButton->setObjectName(QStringLiteral("bannerTryAgain"));
    m_showInFolderButton = createButton(tr("Show in Folder"));
    m_showInFolderButton->setObjectName(QStringLiteral("bannerShowInFolder"));
    m_checkForUpdatesButton = createButton(tr("Check for Updates"));
    m_checkForUpdatesButton->setObjectName(QStringLiteral("bannerCheckForUpdates"));
    m_dismissButton = createButton(tr("Dismiss"));
    m_dismissButton->setObjectName(QStringLiteral("bannerDismiss"));

    actions->addWidget(m_tryAgainButton);
    actions->addWidget(m_showInFolderButton);
    actions->addWidget(m_checkForUpdatesButton);
    actions->addWidget(m_dismissButton);
    layout->addWidget(m_actions, 0, Qt::AlignVCenter);

    connect(m_tryAgainButton, &QPushButton::clicked, this, &PDFAppStorageBanner::tryAgainRequested);
    connect(m_showInFolderButton, &QPushButton::clicked, this, &PDFAppStorageBanner::showInFolderRequested);
    connect(m_checkForUpdatesButton, &QPushButton::clicked, this, &PDFAppStorageBanner::checkForUpdatesRequested);
    connect(m_dismissButton, &QPushButton::clicked, this, &PDFAppStorageBanner::onDismissClicked);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppStorageBanner::update));

    updateContent();
}

PDFAppStorageBanner::~PDFAppStorageBanner() = default;

QPushButton* PDFAppStorageBanner::createButton(const QString& text)
{
    QPushButton* button = new QPushButton(text, m_actions ? m_actions.data() : static_cast<QWidget*>(this));
    button->setAutoDefault(false);
    return button;
}

PDFAppStorageBanner::Kind PDFAppStorageBanner::getKind(const PDFDatabaseState& state)
{
    switch (state.mode)
    {
        case PDFDatabaseStorageMode::Portable:
            return Kind::Portable;
        case PDFDatabaseStorageMode::MemoryFallback:
            return Kind::Fallback;
        case PDFDatabaseStorageMode::ReadOnlyNewerSchema:
            return Kind::ReadOnly;
        case PDFDatabaseStorageMode::Disk:
            return state.damagedFilePath.isEmpty() ? Kind::None : Kind::Replaced;
    }
    return Kind::None;
}

void PDFAppStorageBanner::setState(const PDFDatabaseState& state)
{
    m_state = state;
    updateContent();
}

PDFAppStorageBanner::Kind PDFAppStorageBanner::getKind() const
{
    return m_kind;
}

QString PDFAppStorageBanner::getTitle() const
{
    return m_titleLabel ? m_titleLabel->text() : QString();
}

QString PDFAppStorageBanner::getText() const
{
    return m_textLabel ? m_textLabel->text() : QString();
}

void PDFAppStorageBanner::onDismissClicked()
{
    // The banner of the replaced database is hidden for the rest of the run
    m_dismissedFile = m_state.damagedFilePath;
    updateContent();
}

void PDFAppStorageBanner::updateContent()
{
    Kind kind = getKind(m_state);
    if (kind == Kind::Replaced && !m_dismissedFile.isEmpty() && m_dismissedFile == m_state.damagedFilePath)
    {
        kind = Kind::None;
    }
    m_kind = kind;

    QString title;
    QString text;
    switch (kind)
    {
        case Kind::None:
            break;

        case Kind::Portable:
            title = tr("Portable mode – settings and history are kept only until PDF4QT closes");
            text = tr("Nothing is written to this computer. Changes of the settings, the Recent history, saved workspaces and the access token of the AI server (a new one at each start) are forgotten when you close PDF4QT.");
            break;

        case Kind::Fallback:
            title = tr("Settings could not be opened – changes are kept only until PDF4QT closes");
            text = tr("The settings database is used by another program or cannot be read. PDF4QT started with the default settings; changes of the settings, the Recent history and saved workspaces are forgotten when you close it.");
            if (!m_state.error.getMessage().isEmpty())
            {
                text += QLatin1Char(' ') + tr("Reason: %1").arg(m_state.error.getMessage().left(256));
            }
            break;

        case Kind::ReadOnly:
            title = tr("Settings are read only – they were saved by a newer PDF4QT");
            text = tr("PDF4QT %1 reads them but does not change them, so the newer version keeps working. Your changes apply until you close PDF4QT; the Recent history is not updated.").arg(PDFAppFooter::getDisplayVersion());
            break;

        case Kind::Replaced:
            title = tr("The settings were damaged and have been reset");
            text = tr("PDF4QT could not read the settings database, kept it as %1 and created a new one with the default settings. Recent history, pinned items and saved workspaces start empty. Your documents are not affected.").arg(QFileInfo(m_state.damagedFilePath).fileName());
            break;
    }

    if (m_titleLabel)
    {
        m_titleLabel->setText(title);
    }
    if (m_textLabel)
    {
        m_textLabel->setText(text);
    }

    // The buttons in the current language (a change of the language comes here)
    const std::array<std::pair<QPushButton*, QString>, 4> buttonTexts = { std::make_pair(m_tryAgainButton.data(), tr("Try Again")),
                                                                          std::make_pair(m_showInFolderButton.data(), tr("Show in Folder")),
                                                                          std::make_pair(m_checkForUpdatesButton.data(), tr("Check for Updates")),
                                                                          std::make_pair(m_dismissButton.data(), tr("Dismiss")) };
    for (const std::pair<QPushButton*, QString>& buttonText : buttonTexts)
    {
        if (buttonText.first)
        {
            buttonText.first->setText(buttonText.second);
        }
    }

    const bool hasFile = !m_state.filePath.isEmpty();
    if (m_tryAgainButton)
    {
        m_tryAgainButton->setVisible(kind == Kind::Fallback && m_state.isRetryPossible());
    }
    if (m_showInFolderButton)
    {
        m_showInFolderButton->setVisible((kind == Kind::Fallback || kind == Kind::Replaced) && hasFile);
    }
    if (m_checkForUpdatesButton)
    {
        m_checkForUpdatesButton->setVisible(kind == Kind::ReadOnly);
    }
    if (m_dismissButton)
    {
        m_dismissButton->setVisible(kind == Kind::Replaced);
    }
    if (m_actions)
    {
        // An empty container would keep the gap of 12 beside the text
        bool hasButtons = false;
        for (const QPushButton* button : m_actions->findChildren<QPushButton*>(Qt::FindDirectChildrenOnly))
        {
            hasButtons = hasButtons || !button->isHidden();
        }
        m_actions->setVisible(hasButtons);

        // The size of the buttons is needed now (the placement and the layout of the
        // banner), not after the posted layout request of the container
        if (QLayout* actionsLayout = m_actions->layout())
        {
            actionsLayout->invalidate();
        }
        m_actions->updateGeometry();
    }
    updateActionsPlacement();

    setAccessibleName(title);
    setAccessibleDescription(text);
    setVisible(kind != Kind::None);
    update();
}

void PDFAppStorageBanner::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    using Helper = PDFAppStorageBannerHelper;
    if (m_kind == Kind::None)
    {
        return;
    }

    // bg and border of the hue, radius 12; the icon circle bg/surface 36 with the icon 20
    QPainter painter(this);
    PDFStylePainterGuard guard(&painter);
    painter.setRenderHint(QPainter::Antialiasing);

    const PDFAccentHue hue = Helper::getHue(m_kind);
    const QRectF bannerRect = QRectF(rect());
    PDFStylePainter::fill(&painter, bannerRect, PDFStylePainter::Radii(Helper::RADIUS), PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background));
    PDFStylePainter::strokeInside(&painter, bannerRect, PDFStylePainter::Radii(Helper::RADIUS), PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Border));

    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const qreal circleLeft = isRightToLeft ? bannerRect.right() - 1 - Helper::PADDING_LEFT - Helper::ICON_CIRCLE : bannerRect.left() + 1 + Helper::PADDING_LEFT;
    const QRectF circleRect(circleLeft, bannerRect.center().y() - Helper::ICON_CIRCLE / 2.0, Helper::ICON_CIRCLE, Helper::ICON_CIRCLE);
    PDFStylePainter::fill(&painter, circleRect, PDFStylePainter::Radii(Helper::ICON_CIRCLE / 2.0), PDFThemeColors::BgSurface);
    PDFStylePainter::icon(&painter, PDFStylePainter::centered(circleRect, QSizeF(Helper::ICON, Helper::ICON)), Helper::getIconName(m_kind),
                          PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon));
}

void PDFAppStorageBanner::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::LanguageChange)
    {
        updateContent();
    }
    else if (event->type() == QEvent::LayoutDirectionChange)
    {
        PDFAppStorageBannerHelper::updateMargins(this);
    }
    else if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
    {
        updateActionsPlacement();
    }
}

void PDFAppStorageBanner::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateActionsPlacement();
}

void PDFAppStorageBanner::updateActionsPlacement()
{
    using Helper = PDFAppStorageBannerHelper;
    if (!m_actions || !m_mainLayout || !m_textLayout)
    {
        return;
    }

    // The width of the text beside the buttons decides (the placement depends only
    // on the width, so the height for the width of the layout stays consistent)
    const int leading = 1 + Helper::PADDING_LEFT + Helper::ICON_CIRCLE + Helper::GAP;
    const int trailing = 1 + Helper::PADDING_RIGHT;
    const int textWidth = width() - leading - trailing - Helper::GAP - m_actions->sizeHint().width();
    const bool isBelowText = !m_actions->isHidden() && textWidth < MIN_TEXT_WIDTH;
    const bool isPlaced = m_mainLayout->indexOf(m_actions) >= 0 || m_textLayout->indexOf(m_actions) >= 0;
    if (isPlaced && isBelowText == m_isActionsBelowText)
    {
        return;
    }

    m_isActionsBelowText = isBelowText;
    m_mainLayout->removeWidget(m_actions);
    m_textLayout->removeWidget(m_actions);
    if (QLayout* actionsLayout = m_actions->layout())
    {
        // Below the text: 12 px under it (the spacing of the text layout is 2)
        actionsLayout->setContentsMargins(0, isBelowText ? Helper::GAP - 2 : 0, 0, 0);
    }

    if (isBelowText)
    {
        m_textLayout->addWidget(m_actions, 0, Qt::AlignLeading);
    }
    else
    {
        m_mainLayout->addWidget(m_actions, 0, Qt::AlignVCenter);
    }
}

PDFAppFooter::PDFAppFooter(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("footer"));

    // app-footer (31 px: the padding 16 and the text 15): the padding 16 above the
    // texts, the line border/default of 1 px lies inside of the padding (the stroke
    // is not a part of the layout); the row is 16 px high (the link), its texts end
    // at its bottom, so the link reaches 1 px into the padding above them. The
    // padding below the row is the bottom padding of the workspace (16, owner
    // decision O11), so the tag can reach into it.
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, PADDING - LINK_OVERHANG, 0, PADDING);
    layout->setSpacing(0);

    // The texts (15 px) end at the bottom of the row of the link (16 px): 16 px above
    // the bottom of the window, their centers on the axis of the switch within 0.5 px.
    // The separators " • " are labels of their own between the parts, so they stay
    // between the parts also right-to-left (a separator at the end of the text of
    // a part would move to its other side)
    m_freeLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, this);
    m_freeLabel->setObjectName(QStringLiteral("freeLabel"));
    layout->addWidget(m_freeLabel, 0, Qt::AlignBottom);
    layout->addWidget(createSeparator(), 0, Qt::AlignBottom);
    m_licenseLink = new PDFTextLink(QString(), this);
    m_licenseLink->setObjectName(QStringLiteral("licenseLink"));
    m_licenseLink->setToolTip(getLicenseUrl().toString());
    layout->addWidget(m_licenseLink, 0, Qt::AlignVCenter);
    layout->addWidget(createSeparator(), 0, Qt::AlignBottom);
    m_versionLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, this);
    m_versionLabel->setObjectName(QStringLiteral("versionLabel"));
    layout->addWidget(m_versionLabel, 0, Qt::AlignBottom);

    // The tag (21 px) does not change the height of the row: it is centered on the
    // row and reaches into the paddings (pdf4qt-settings-general-portable 941:138507);
    // the layout keeps only its place (8 + the width of the tag)
    m_tag = new PDFTag(QString(), this);
    m_tag->setObjectName(QStringLiteral("storageTag"));
    m_tag->hide();
    m_tagSpacer = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    layout->addSpacerItem(m_tagSpacer);
    layout->addStretch(1);

    m_targets = new PDFAppFooterTargets(this);
    layout->addWidget(m_targets, 0, Qt::AlignVCenter);

    connect(m_licenseLink, &QAbstractButton::clicked, this, &PDFAppFooter::onLicenseClicked);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppFooter::update));

    updateTexts();
}

PDFAppFooter::~PDFAppFooter() = default;

PDFLabel* PDFAppFooter::createSeparator()
{
    PDFLabel* separator = new PDFLabel(QStringLiteral(" \u2022 "), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, this);
    separator->setObjectName(QStringLiteral("footerSeparator"));
    return separator;
}

QUrl PDFAppFooter::getLicenseUrl()
{
    return QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/blob/master/LICENSE"));
}

QString PDFAppFooter::getDisplayVersion(const QString& version)
{
    qsizetype suffixIndex = -1;
    const QVersionNumber number = QVersionNumber::fromString(version, &suffixIndex);
    if (number.isNull() || suffixIndex != version.size())
    {
        // A text of one line (control and format characters, separators of lines
        // are left out) of at most 32 characters, a surrogate pair is never cut
        QString text;
        for (const QChar character : version.trimmed())
        {
            const QChar::Category category = character.category();
            if (category != QChar::Other_Control && category != QChar::Other_Format &&
                category != QChar::Separator_Line && category != QChar::Separator_Paragraph)
            {
                text += character;
            }
        }
        text.truncate(32);
        if (!text.isEmpty() && text.back().isHighSurrogate())
        {
            text.chop(1);
        }
        return text;
    }
    return QStringLiteral("%1.%2.%3").arg(number.majorVersion()).arg(number.minorVersion()).arg(number.microVersion());
}

QString PDFAppFooter::getDisplayVersion()
{
    return getDisplayVersion(QStringLiteral(PDF4QT_PROJECT_VERSION));
}

void PDFAppFooter::updateTagSpace()
{
    QLayout* layout = this->layout();
    if (!layout || !m_tagSpacer || layout->indexOf(m_tagSpacer) < 0)
    {
        return;
    }

    const int width = m_tag && !m_tag->isHidden() ? TAG_GAP + m_tag->sizeHint().width() : 0;
    if (m_tagSpacer->sizeHint().width() != width)
    {
        m_tagSpacer->changeSize(width, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        layout->invalidate();
    }
    placeTag();
}

void PDFAppFooter::placeTag()
{
    QLayout* layout = this->layout();
    if (!m_tag || m_tag->isHidden() || !layout || !m_tagSpacer || layout->indexOf(m_tagSpacer) < 0)
    {
        return;
    }

    // The place of the layout (mirrored in right-to-left by the layout), the tag
    // is centered vertically on the text of the version (21 on 15: 3 px above and
    // below it, Figma 941:138507)
    const QRect place = m_tagSpacer->geometry();
    const QRect text = m_versionLabel ? m_versionLabel->geometry() : layout->contentsRect();
    const QSize tagSize = m_tag->sizeHint();
    const int x = layoutDirection() == Qt::RightToLeft ? place.left() : place.left() + TAG_GAP;
    const int y = text.top() + (text.height() - tagSize.height()) / 2;
    m_tag->setGeometry(x, y, tagSize.width(), tagSize.height());
}

bool PDFAppFooter::event(QEvent* event)
{
    // The layout sets its geometry before the event reaches the widget
    const bool result = BaseClass::event(event);

    switch (event->type())
    {
        case QEvent::LayoutRequest:
        case QEvent::Resize:
        case QEvent::LayoutDirectionChange:
            placeTag();
            break;

        case QEvent::FontChange:
        case QEvent::StyleChange:
            updateTagSpace();
            break;

        case QEvent::LanguageChange:
            updateTexts();
            break;

        default:
            break;
    }
    return result;
}

void PDFAppFooter::updateTexts()
{
    // Texts of the current language (also the tag of the stored state and the
    // place of the tag, whose width follows its text)
    if (m_freeLabel)
    {
        m_freeLabel->setText(tr("Free • Open Source"));
    }
    if (m_licenseLink)
    {
        m_licenseLink->setText(tr("MIT License"));
    }
    if (m_versionLabel)
    {
        m_versionLabel->setText(tr("Version %1").arg(getDisplayVersion()));
    }
    updateTag();
}

void PDFAppFooter::updateTag()
{
    if (!m_tag)
    {
        return;
    }

    QString text;
    switch (m_storageMode)
    {
        case PDFDatabaseStorageMode::Portable:
            text = tr("Portable");
            break;
        case PDFDatabaseStorageMode::MemoryFallback:
            text = tr("Not saved");
            break;
        case PDFDatabaseStorageMode::ReadOnlyNewerSchema:
            text = tr("Read only");
            break;
        case PDFDatabaseStorageMode::Disk:
            break;
    }

    m_tag->setText(text);
    m_tag->setVisible(!text.isEmpty());
    updateTagSpace();
}

void PDFAppFooter::setState(const PDFDatabaseState& state)
{
    m_storageMode = state.mode;
    updateTag();
}

QString PDFAppFooter::getTagText() const
{
    return m_tag && !m_tag->isHidden() ? m_tag->text() : QString();
}

void PDFAppFooter::onLicenseClicked()
{
    Q_EMIT externalUrlRequested(getLicenseUrl());
}

void PDFAppFooter::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFStylePainter::fillRect(&painter, QRectF(0, 0, width(), 1), PDFThemeColors::BorderDefault);
}

}   // namespace pdf
