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
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyleproperties.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

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

    /// Hue of the banner: portable mode is information (blue), the others warn (amber)
    static PDFAccentHue getHue(PDFAppStorageBanner::Kind kind)
    {
        return kind == PDFAppStorageBanner::Kind::Portable ? PDFAccentHue::Blue : PDFAccentHue::Amber;
    }

    static QString getIconName(PDFAppStorageBanner::Kind kind)
    {
        return kind == PDFAppStorageBanner::Kind::Portable ? QStringLiteral("portable") : QStringLiteral("database");
    }
};

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

PDFAppSidebar::PDFAppSidebar(PDFAppContext* context, QWidget* parent) :
    BaseClass(parent),
    m_context(context)
{
    setObjectName(QStringLiteral("sidebar"));
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::PANEL);
    setFixedWidth(WIDTH);

    // Paddings of the design: sidebar 20 / 16, logo-area 16 / 8 / 16, nav groups 10,
    // bottom-area 14 / 8 (the switch is 182 wide)
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 20, 10, 24);
    layout->setSpacing(0);

    QHBoxLayout* logoLayout = new QHBoxLayout();
    logoLayout->setContentsMargins(6, 8, 6, 16);
    logoLayout->setSpacing(10);
    logoLayout->addWidget(new PDFAppLogoTile(this), 0, Qt::AlignVCenter);
    QVBoxLayout* logoTexts = new QVBoxLayout();
    logoTexts->setContentsMargins(0, 0, 0, 0);
    logoTexts->setSpacing(2);
    logoTexts->addWidget(new PDFLabel(tr("PDF4QT"), PDFThemeFonts::Role::TileTitle, PDFThemeColors::TextPrimary, this));
    logoTexts->addWidget(new PDFLabel(tr("Open. Edit. Do more."), PDFThemeFonts::Role::Tiny, PDFThemeColors::TextMuted, this));
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
    layout->addWidget(m_navigation);
    layout->addStretch(1);

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
    m_title = new PDFLabel(QString(), PDFThemeFonts::Role::Title, PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("headerTitle"));
    m_title->setFont(PDFThemeFonts::getFont(QFont::Bold, 28));
    m_title->setFixedHeight(34);
    m_title->setTextFormat(Qt::PlainText);
    m_title->hide();
    m_subtitle = new PDFLabel(QString(), PDFThemeFonts::Role::Body, PDFThemeColors::TextTertiary, this);
    m_subtitle->setObjectName(QStringLiteral("headerSubtitle"));
    m_subtitle->setFont(PDFThemeFonts::getFont(QFont::Normal, 14));
    m_subtitle->setTextFormat(Qt::PlainText);
    m_subtitle->hide();
    textLayout->addWidget(m_title);
    textLayout->addWidget(m_subtitle);
    layout->addLayout(textLayout, 1);

    m_searchField = new PDFLineEdit(this);
    m_searchField->setObjectName(QStringLiteral("globalSearch"));
    m_searchField->setSearchField();
    m_searchField->setPlaceholderText(tr("Search tools, documents, or help..."));
    m_searchField->setAccessibleName(tr("Search tools, documents, or help"));
    m_searchField->setFixedWidth(320);
    m_searchField->setMaxLength(256);
    layout->addWidget(m_searchField, 0, Qt::AlignVCenter);

    connect(m_searchField, &QLineEdit::returnPressed, this, &PDFAppWorkspaceHeader::onReturnPressed);
}

PDFAppWorkspaceHeader::~PDFAppWorkspaceHeader() = default;

void PDFAppWorkspaceHeader::setTexts(const QString& title, const QString& subtitle)
{
    if (m_title)
    {
        m_title->setText(title);
        m_title->setVisible(!title.isEmpty());
    }

    if (m_subtitle)
    {
        m_subtitle->setText(subtitle);
        m_subtitle->setVisible(!subtitle.isEmpty());
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

    // The icon circle is painted in the left padding (border 1 + 14 + 36 + 12)
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(1 + Helper::PADDING_LEFT + Helper::ICON_CIRCLE + Helper::GAP, 1 + Helper::PADDING_VERTICAL,
                               1 + Helper::PADDING_RIGHT, 1 + Helper::PADDING_VERTICAL);
    layout->setSpacing(Helper::GAP);

    QVBoxLayout* textLayout = new QVBoxLayout();
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);
    m_titleLabel = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, this);
    m_titleLabel->setObjectName(QStringLiteral("bannerTitle"));
    m_titleLabel->setTextFormat(Qt::PlainText);
    m_titleLabel->setWordWrap(true);
    m_textLabel = new PDFLabel(QString(), PDFThemeFonts::Role::Small, PDFThemeColors::TextSecondary, this);
    m_textLabel->setObjectName(QStringLiteral("bannerText"));
    m_textLabel->setTextFormat(Qt::PlainText);
    m_textLabel->setWordWrap(true);
    textLayout->addWidget(m_titleLabel);
    textLayout->addWidget(m_textLabel);
    layout->addLayout(textLayout, 1);

    m_tryAgainButton = createButton(tr("Try Again"));
    m_tryAgainButton->setObjectName(QStringLiteral("bannerTryAgain"));
    m_showInFolderButton = createButton(tr("Show in Folder"));
    m_showInFolderButton->setObjectName(QStringLiteral("bannerShowInFolder"));
    m_checkForUpdatesButton = createButton(tr("Check for Updates"));
    m_checkForUpdatesButton->setObjectName(QStringLiteral("bannerCheckForUpdates"));
    m_dismissButton = createButton(tr("Dismiss"));
    m_dismissButton->setObjectName(QStringLiteral("bannerDismiss"));

    QHBoxLayout* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(8);
    actions->addWidget(m_tryAgainButton);
    actions->addWidget(m_showInFolderButton);
    actions->addWidget(m_checkForUpdatesButton);
    actions->addWidget(m_dismissButton);
    layout->addLayout(actions);

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
    QPushButton* button = new QPushButton(text, this);
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
            text = tr("PDF4QT %1 reads them but does not change them, so the newer version keeps working. Your changes apply until you close PDF4QT; the Recent history is not updated.").arg(QStringLiteral(PDF4QT_PROJECT_VERSION));
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
}

PDFAppFooter::PDFAppFooter(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("footer"));

    // app-footer: the line border/default and the padding 16 above the line of texts
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 17, 0, 0);
    layout->setSpacing(0);

    layout->addWidget(new PDFLabel(tr("Free • Open Source • "), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, this), 0, Qt::AlignVCenter);
    m_licenseLink = new PDFTextLink(tr("MIT License"), this);
    m_licenseLink->setObjectName(QStringLiteral("licenseLink"));
    m_licenseLink->setToolTip(getLicenseUrl().toString());
    layout->addWidget(m_licenseLink, 0, Qt::AlignVCenter);
    m_versionLabel = new PDFLabel(tr(" • Version %1").arg(QStringLiteral(PDF4QT_PROJECT_VERSION)), PDFThemeFonts::Role::Small, PDFThemeColors::TextTertiary, this);
    m_versionLabel->setObjectName(QStringLiteral("versionLabel"));
    layout->addWidget(m_versionLabel, 0, Qt::AlignVCenter);
    layout->addSpacing(8);
    m_tag = new PDFTag(QString(), this);
    m_tag->setObjectName(QStringLiteral("storageTag"));
    m_tag->hide();
    layout->addWidget(m_tag, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    // The targets (the logos of Windows and macOS are drawings of the frame, not
    // icons of the library - review I-35 -, they are left out with the one of Linux)
    for (const QString& target : { tr("Windows"), tr("Linux"), tr("macOS") })
    {
        layout->addSpacing(16);
        layout->addWidget(new PDFLabel(target, PDFThemeFonts::Role::CaptionSemiBold, PDFThemeColors::TextTertiary, this), 0, Qt::AlignVCenter);
    }

    connect(m_licenseLink, &QAbstractButton::clicked, this, &PDFAppFooter::onLicenseClicked);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppFooter::update));
}

PDFAppFooter::~PDFAppFooter() = default;

QUrl PDFAppFooter::getLicenseUrl()
{
    return QUrl(QStringLiteral("https://github.com/JakubMelka/PDF4QT/blob/master/LICENSE"));
}

void PDFAppFooter::setState(const PDFDatabaseState& state)
{
    if (!m_tag)
    {
        return;
    }

    QString text;
    switch (state.mode)
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
