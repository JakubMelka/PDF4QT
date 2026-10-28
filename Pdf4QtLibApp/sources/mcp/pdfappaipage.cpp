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

#include "pdfappaipage.h"
#include "pdfappannotationtoolbar.h"
#include "pdfappcontext.h"
#include "pdfapphomewidgets.h"
#include "pdfappinfowidgets.h"
#include "pdfapplocale.h"
#include "pdfappmainwindow.h"
#include "pdfappmcpactivity.h"
#include "pdfappmcpclientnames.h"
#include "pdfappmcpconfirmationui.h"
#include "pdfappmcpcontroller.h"
#include "pdfappmcphost.h"
#include "pdfapproute.h"
#include "pdfappsettingcontrol.h"
#include "pdfappsettingrow.h"
#include "pdfappworkspace.h"
#include "pdfdatabasenotifier.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfaccenthue.h"
#include "pdfcomponentassistants.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthemeiconengine.h"
#include "pdfthememanager.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace pdf
{

/// Helpers of the page AI: the texts of the states and the geometry of 653:5231
class PDFAppAiPageHelper
{
public:
    PDFAppAiPageHelper() = delete;

    // server-card 653:5446: padding 20 (+ the border 1), gap 20, logo 56 r14 with the icon 28
    static constexpr int SERVER_PADDING = 21;
    static constexpr int SERVER_GAP = 20;
    static constexpr int LOGO_EXTENT = 56;
    static constexpr int LOGO_ICON = 28;
    static constexpr int LOGO_RADIUS = 14;

    // address 653:5459: the text 12 inside of the border - the frameless field
    // has the text padding of the fields of the style, the box adds its border;
    // the row stacks below this width of the address
    static constexpr int ADDRESS_TEXT_INSET = 1;
    static constexpr int ADDRESS_MINIMUM_WIDTH = 240;

    // Paragraphs: 13 / 20 (line-height 1.5), 12 / 16, 12 / 17 (1.4), 11 / 15
    static constexpr int BODY_LINE = 20;
    static constexpr int SMALL_LINE = 16;
    static constexpr int NOTE_LINE = 17;
    static constexpr int CAPTION_LINE = 15;
    static constexpr int STEP_LINE = 18;

    // The control column of the rows of Connection / Permissions (653:5475)
    static constexpr int CONTROL_COLUMN = 320;
    static constexpr int TOKEN_FIELD_WIDTH = 232;

    // Tiles 184 x 66, gap 12, at most six in a row
    static constexpr int TILE_MINIMUM_WIDTH = 166;
    static constexpr int TILE_COLUMNS = 6;
    static constexpr int TILE_GAP = 12;

    static PDFAppParagraph* createText(const QString& text, QFont::Weight weight, int pixelSize, int lineHeight, PDFThemeColors::Color color, QWidget* parent)
    {
        PDFAppParagraph* paragraph = new PDFAppParagraph(PDFThemeFonts::getFont(weight, pixelSize), lineHeight, color, parent);
        paragraph->setText(text);
        return paragraph;
    }

    static QHBoxLayout* createRow(QWidget* widget, int spacing)
    {
        QHBoxLayout* layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(spacing);
        return layout;
    }

    static QString getTime(qint64 milliseconds)
    {
        if (milliseconds <= 0)
        {
            return QString();
        }
        return PDFAppLocale::formatClock(QDateTime::fromMSecsSinceEpoch(milliseconds).toLocalTime().time());
    }

    /// Icon, hue and tone of the status circle of an activity row (653:5612, 955:150874)
    static void setStatusLook(PDFMcpActivityStatus status, PDFComponentPainter::ActivityRow& row)
    {
        using Tone = PDFComponentPainter::ActivityStatusTone;
        row.hue = PDFAccentHue::Neutral;
        row.statusTone = Tone::Muted;
        switch (status)
        {
            case PDFMcpActivityStatus::Ok:
            case PDFMcpActivityStatus::Allowed:
                row.iconName = QStringLiteral("check");
                row.hue = PDFAccentHue::Green;
                break;
            case PDFMcpActivityStatus::Waiting:
                row.iconName = QStringLiteral("hourglass");
                row.hue = PDFAccentHue::Amber;
                row.statusTone = Tone::Secondary;
                break;
            case PDFMcpActivityStatus::DeniedUser:
                row.iconName = QStringLiteral("user-x");
                break;
            case PDFMcpActivityStatus::DeniedTimeout:
                row.iconName = QStringLiteral("timer-off");
                break;
            case PDFMcpActivityStatus::DeniedLocked:
                row.iconName = QStringLiteral("lock");
                break;
            case PDFMcpActivityStatus::DeniedDisconnected:
                row.iconName = QStringLiteral("unplug");
                break;
            case PDFMcpActivityStatus::DeniedStopped:
                row.iconName = QStringLiteral("power");
                break;
            case PDFMcpActivityStatus::DeniedClosed:
            case PDFMcpActivityStatus::DeniedBusy:
            case PDFMcpActivityStatus::DeniedDialog:
            case PDFMcpActivityStatus::DeniedPermission:
                row.iconName = QStringLiteral("ban");
                break;
            case PDFMcpActivityStatus::Canceled:
                row.iconName = QStringLiteral("x");
                break;
            case PDFMcpActivityStatus::Failed:
                row.iconName = QStringLiteral("alert-circle");
                row.hue = PDFAccentHue::Red;
                row.statusTone = Tone::Danger;
                break;
        }
    }

    /// Monogram, hue and the CLI badge of an assistant of its display name
    static PDFComponentPainter::AssistantAvatar createAvatar(const QString& displayName, int extent, int fontPixelSize)
    {
        PDFComponentPainter::AssistantAvatar avatar;

        // The rows mcp_client hold the display names: a known client by its
        // display name, else by the rules of the self-reported names
        PDFAppMcpClientInfo info = PDFAppMcpClientNames::describe(displayName);
        for (const PDFAppMcpKnownClient& client : PDFAppMcpClientNames::getKnownClients())
        {
            if (client.displayName.compare(displayName, Qt::CaseInsensitive) == 0)
            {
                info.monogram = client.monogram;
                info.hue = client.hue;
                info.isCommandLine = client.isCommandLine;
                break;
            }
        }
        avatar.monogram = info.monogram;
        avatar.hue = info.hue;
        avatar.commandLine = info.isCommandLine;
        avatar.extent = extent;
        avatar.fontPixelSize = fontPixelSize;
        return avatar;
    }
};

/// Painted box of the page AI, which has no component: the address of the server
/// card (653:5459 bg/surface, 653:6042 bg/subtle - border/default, radius 8; the
/// focus ring of the keyboard of its field inside) and the flat amber warning row
/// of the remote mode (653:6658 - no border, no radius)
class PDFAppAiFillBox : public QWidget
{
private:
    using BaseClass = QWidget;

public:
    explicit PDFAppAiFillBox(PDFThemeColors::Color fill, std::optional<PDFThemeColors::Color> border, qreal radius, QWidget* parent) :
        BaseClass(parent),
        m_fill(fill),
        m_border(border),
        m_radius(radius)
    {
        setAttribute(Qt::WA_NoSystemBackground, true);
        connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
    }

    PDFThemeColors::Color getFill() const { return m_fill; }
    void setFill(PDFThemeColors::Color fill)
    {
        if (m_fill != fill)
        {
            m_fill = fill;
            update();
        }
    }

    /// The field, whose focus ring of the keyboard the box draws
    void setFocusWidget(QWidget* widget)
    {
        if (m_focusWidget)
        {
            m_focusWidget->removeEventFilter(this);
        }
        m_focusWidget = widget;
        if (widget)
        {
            widget->installEventFilter(this);
        }
    }

protected:
    virtual bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_focusWidget.data() && (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut))
        {
            update();
        }
        return BaseClass::eventFilter(watched, event);
    }

    virtual void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF rect(this->rect());
        const PDFStylePainter::Radii radii(m_radius);
        PDFStylePainter::fill(&painter, rect, radii, m_fill);
        if (m_border.has_value())
        {
            PDFStylePainter::strokeInside(&painter, rect, radii, *m_border, 1.0);
        }
        if (m_focusWidget && m_focusWidget->hasFocus() && PDFWidgetStyle::isFocusRingVisible(m_focusWidget))
        {
            PDFStylePainter::strokeInside(&painter, rect, radii, PDFThemeColors::BorderFocus, 2.0);
        }
    }

private:
    PDFThemeColors::Color m_fill;
    std::optional<PDFThemeColors::Color> m_border;
    qreal m_radius = 0.0;
    QPointer<QWidget> m_focusWidget;
};

// ------------------------------------------------------------------ New token

PDFAppAiNewTokenDialog::PDFAppAiNewTokenDialog(QWidget* parent) :
    BaseClass(PDFThemeIcons::Badge::Warning, true, tr("Create a new access token?"), QString(), CARD_WIDTH, parent)
{
    setObjectName(QStringLiteral("aiNewTokenDialog"));

    // 941:7796: the badge at the top of the head (items-start)
    setBadgeAlignment(Qt::AlignTop);

    // 941:7803: Create New Token (Secondary) at the leading edge, Cancel (Primary) at the trailing edge
    m_createButton = addActionButton(tr("Create New Token"), false, true);
    m_createButton->setObjectName(QStringLiteral("createTokenButton"));
    m_cancelButton = addActionButton(tr("Cancel"), true, false);
    m_cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(m_createButton.data(), &QPushButton::clicked, this, &PDFAppAiNewTokenDialog::accept);
    connect(m_cancelButton.data(), &QPushButton::clicked, this, &PDFAppAiNewTokenDialog::reject);
    updateTexts();
}

PDFAppAiNewTokenDialog::~PDFAppAiNewTokenDialog() = default;

void PDFAppAiNewTokenDialog::setClientNames(const QStringList& names)
{
    if (m_clientNames != names)
    {
        m_clientNames = names;
        updateTexts();
    }
}

void PDFAppAiNewTokenDialog::setTokenRequired(bool isTokenRequired)
{
    if (m_isTokenRequired != isTokenRequired)
    {
        m_isTokenRequired = isTokenRequired;
        updateTexts();
    }
}

QString PDFAppAiNewTokenDialog::getBodyText(const QStringList& names, bool isTokenRequired)
{
    if (!isTokenRequired)
    {
        // Require access token is off (the default of the local mode, owner
        // 2026-10-28): the assistants connect without the token, the new one is
        // used only after the option is turned on
        if (names.isEmpty())
        {
            return tr("Require access token is off, so assistants connect without a token. The new token is used once you turn the option on – the commands on this page are updated then.");
        }

        return tr("Require access token is off, so assistants connect without a token: all connected assistants are disconnected (%1) and can connect again at once. The new token is used once you turn the option on.")
                .arg(names.join(QStringLiteral(", ")));
    }

    if (names.isEmpty())
    {
        // 955:150878: the variant without connected assistants
        return tr("The current token stops working at once. No assistant is connected now. Set up your assistants with the new token – the commands on this page are updated.");
    }

    return tr("The current token stops working at once and all connected assistants are disconnected (%1). To connect an assistant again, run its setup command with the new token – the commands on this page are updated.")
            .arg(names.join(QStringLiteral(", ")));
}

void PDFAppAiNewTokenDialog::updateTexts()
{
    setTitle(tr("Create a new access token?"));
    setText(getBodyText(m_clientNames, m_isTokenRequired));
    if (m_createButton)
    {
        m_createButton->setText(tr("Create New Token"));
    }
    if (m_cancelButton)
    {
        m_cancelButton->setText(tr("Cancel"));
    }
}

void PDFAppAiNewTokenDialog::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

// ------------------------------------------------------------------ Page

PDFAppAiPage::PDFAppAiPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent),
    m_controller(context ? context->getMcpController() : nullptr)
{
    setObjectName(QStringLiteral("aiPage"));

    // A missing token is generated, also when it is not required, so the token
    // row and the setup commands show it at once when Require access token is
    // turned on (the controller does the same at the start of the server)
    if (context && context->getDatabase() && context->getSettings().getString(PDFSettingKeys::mcpToken()).isEmpty())
    {
        context->getSettings().setValue(PDFSettingKeys::mcpToken(), PDFAppMcpController::generateToken());
    }

    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(this, PDFAppColumns::GAP);
    layout->addStretch(1);
    buildContent();

    if (m_controller)
    {
        connect(m_controller, &PDFAppMcpController::stateChanged, this, &PDFAppAiPage::onStateChanged);
        connect(m_controller, &PDFAppMcpController::assistantsChanged, this, &PDFAppAiPage::onAssistantsChanged);
        connect(m_controller, &PDFAppMcpController::activityChanged, this, &PDFAppAiPage::onActivityChanged);
        if (PDFAppMcpHost* host = m_controller->getHost())
        {
            connect(host, &PDFAppMcpHost::confirmationsChanged, this, &PDFAppAiPage::onActivityChanged);
        }
    }
    if (context && context->getNotifier())
    {
        connect(context->getNotifier(), &PDFDatabaseNotifier::changed, this, &PDFAppAiPage::onDatabaseChanged);
        connect(context->getNotifier(), &PDFDatabaseNotifier::everythingChanged, this, &PDFAppAiPage::onEverythingChanged);
    }
}

PDFAppAiPage::~PDFAppAiPage()
{
    // The dialog belongs to the window: it goes with the page, hidden at once and
    // deleted later (the page may go inside of QEvent::LanguageChange sent to a
    // copy of the children of the window)
    if (m_newTokenDialog)
    {
        PDFAppAiNewTokenDialog* dialog = m_newTokenDialog;
        m_newTokenDialog = nullptr;
        dialog->disconnect(this);
        dialog->hide();
        dialog->deleteLater();
    }
}

void PDFAppAiPage::buildContent()
{
    m_columns = PDFAppColumns::createMainAndSide(this);
    m_columns->setObjectName(QStringLiteral("aiGrid"));

    QBoxLayout* mainLayout = m_columns->getMainLayout();
    QBoxLayout* sideLayout = m_columns->getSideLayout();
    QWidget* main = m_columns->getMainColumn();
    QWidget* side = m_columns->getSideColumn();

    mainLayout->addWidget(createServerCard(main));
    mainLayout->addWidget(createConnectionGroup(main));
    mainLayout->addWidget(createPermissionsGroup(main));
    mainLayout->addStretch(1);

    sideLayout->addWidget(createAssistantsSection(side));
    sideLayout->addWidget(createActivitySection(side));
    sideLayout->addWidget(createLinksSection(side));
    sideLayout->addWidget(createNote(side));
    sideLayout->addStretch(1);

    m_connectSection = createConnectSection(this);
    if (QBoxLayout* layout = qobject_cast<QBoxLayout*>(this->layout()))
    {
        layout->insertWidget(0, m_columns);
        layout->insertWidget(1, m_connectSection);
    }

    m_hasShownClients = false;
    m_setupSignature.clear();
    updateAll();
}

void PDFAppAiPage::rebuild()
{
    const int scrollPosition = isVisible() ? getScrollPosition() : -1;
    const QString focusName = PDFAppPageRebuild::getFocusedName(this);

    m_settingRows.clear();
    m_assistantRows.clear();
    m_activityRows.clear();
    m_tiles.clear();
    m_variantChips.clear();
    m_codeBoxes.clear();
    delete m_columns.data();
    delete m_connectSection.data();
    buildContent();
    m_columns->show();
    m_connectSection->show();

    if (scrollPosition >= 0)
    {
        PDFAppPageRebuild::finishLayout();
        setScrollPosition(scrollPosition);
        requestDeferredScroll(scrollPosition, QString());
    }
    PDFAppPageRebuild::restoreFocus(this, focusName);
}

void PDFAppAiPage::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_columns)
    {
        rebuild();
    }
}

void PDFAppAiPage::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    // The times and the rows of another instance may have changed while hidden
    updateAll();
}

bool PDFAppAiPage::setRoute(const PDFAppRoute& route)
{
    if (!route.getQueryItems().isEmpty())
    {
        return false;
    }

    // A fragment is the anchor of a row of Connection / Permissions (the global search)
    const QString fragment = route.getFragment();
    PDFAppSettingRow* row = fragment.isEmpty() ? nullptr : findSettingRow(fragment);
    if (!fragment.isEmpty() && !row)
    {
        return false;
    }

    setAnchorWidget(row);
    if (!BaseClass::setRoute(route))
    {
        return false;
    }
    if (row)
    {
        row->highlight();
    }
    return true;
}

QString PDFAppAiPage::getTitle() const
{
    return tr("AI");
}

QString PDFAppAiPage::getHeaderTitle() const
{
    return tr("AI");
}

QString PDFAppAiPage::getHeaderSubtitle() const
{
    return tr("Let AI assistants work with your documents through the built-in MCP server.");
}

QString PDFAppAiPage::getStateText() const
{
    switch (m_shownState)
    {
        case ShownState::Stopped:
            break;
        case ShownState::Starting:
            return tr("The MCP server is starting.");
        case ShownState::Running:
        case ShownState::Remote:
            return tr("The MCP server is running at %1.").arg(getShownAddress());
        case ShownState::OtherInstance:
            return tr("The MCP server runs in another PDF4QT window.");
        case ShownState::PortInUse:
            return tr("Port %1 is used by another program.").arg(m_controller ? m_controller->getPort() : 0);
        case ShownState::Failed:
            return tr("The MCP server could not start.");
    }
    return tr("The MCP server is stopped.");
}

// ------------------------------------------------------------------ Content

QWidget* PDFAppAiPage::createServerCard(QWidget* parent)
{
    using Helper = PDFAppAiPageHelper;

    // server-card 653:5446 (the hue and the logo follow the state)
    m_serverCard = new PDFTintedCard(PDFAccentHue::Blue, parent);
    m_serverCard->setObjectName(QStringLiteral("serverCard"));
    QHBoxLayout* layout = new QHBoxLayout(m_serverCard);
    layout->setContentsMargins(Helper::SERVER_PADDING, Helper::SERVER_PADDING, Helper::SERVER_PADDING, Helper::SERVER_PADDING);
    layout->setSpacing(Helper::SERVER_GAP);

    m_serverLogo = new PDFIconBadge(QStringLiteral("mcp"), Helper::LOGO_EXTENT, Helper::LOGO_ICON, m_serverCard);
    m_serverLogo->setObjectName(QStringLiteral("serverLogo"));
    m_serverLogo->setRadius(Helper::LOGO_RADIUS);
    layout->addWidget(m_serverLogo, 0, Qt::AlignTop);

    QWidget* content = new QWidget(m_serverCard);
    QVBoxLayout* contentLayout = PDFAppPageWidgets::createColumn(content, 6);
    layout->addWidget(content, 1);

    // title-row: "MCP server" Bold 22 and the badges (gap 10)
    QWidget* titleRow = new QWidget(content);
    QHBoxLayout* titleLayout = Helper::createRow(titleRow, 10);
    titleLayout->addWidget(Helper::createText(tr("MCP server"), QFont::Bold, 22, 27, PDFThemeColors::TextPrimary, titleRow), 0, Qt::AlignVCenter);
    m_stateBadge = new PDFStatusBadge(titleRow);
    m_stateBadge->setObjectName(QStringLiteral("stateBadge"));
    m_stateBadge->setDotVisible(true);
    titleLayout->addWidget(m_stateBadge, 0, Qt::AlignVCenter);
    m_remoteBadge = new PDFStatusBadge(titleRow);
    m_remoteBadge->setObjectName(QStringLiteral("remoteBadge"));
    m_remoteBadge->setDotVisible(true);
    m_remoteBadge->setBadge(tr("Remote access"), QString(), PDFAccentHue::Amber);
    titleLayout->addWidget(m_remoteBadge, 0, Qt::AlignVCenter);
    titleLayout->addStretch(1);
    contentLayout->addWidget(titleRow);

    m_serverMeta = Helper::createText(QString(), QFont::Normal, 13, 16, PDFThemeColors::TextTertiary, content);
    m_serverMeta->setObjectName(QStringLiteral("serverMeta"));
    contentLayout->addWidget(m_serverMeta);
    contentLayout->addWidget(Helper::createText(tr("The server lets AI assistants such as Claude read, search and edit the documents you have open in PDF4QT. "
                                                   "It runs on this computer and only while PDF4QT is open."),
                                                QFont::Normal, 13, Helper::BODY_LINE, PDFThemeColors::TextSecondary, content));

    // Callouts of another instance (940:136797) and of a port in use (955:148979)
    m_callout = new PDFTintedCard(PDFAccentHue::Blue, content);
    m_callout->setObjectName(QStringLiteral("serverCallout"));
    m_callout->setRadius(8.0);
    QHBoxLayout* calloutLayout = new QHBoxLayout(m_callout);
    calloutLayout->setContentsMargins(13, 11, 13, 11);
    calloutLayout->setSpacing(10);
    m_calloutIcon = new PDFIconBadge(QStringLiteral("info"), 16, 16, m_callout);
    m_calloutIcon->setColors(std::nullopt, PDFThemeColors::IconAccent);
    m_calloutIcon->setContentsMargins(0, 1, 0, 0);
    calloutLayout->addWidget(m_calloutIcon, 0, Qt::AlignVCenter);
    QWidget* calloutText = new QWidget(m_callout);
    QVBoxLayout* calloutTextLayout = PDFAppPageWidgets::createColumn(calloutText, 2);
    m_calloutTitle = Helper::createText(QString(), QFont::DemiBold, 13, 16, PDFThemeColors::TextPrimary, calloutText);
    m_calloutTitle->setObjectName(QStringLiteral("calloutTitle"));
    m_calloutText = Helper::createText(QString(), QFont::Normal, 12, Helper::SMALL_LINE, PDFThemeColors::TextSecondary, calloutText);
    m_calloutText->setObjectName(QStringLiteral("calloutText"));
    calloutTextLayout->addWidget(m_calloutTitle);
    calloutTextLayout->addWidget(m_calloutText);
    calloutLayout->addWidget(calloutText, 1);
    m_calloutButton = PDFAppPageWidgets::createButton(QString(), QString(), false, m_callout);
    m_calloutButton->setIcon(QIcon());
    m_calloutButton->setObjectName(QStringLiteral("calloutButton"));
    connect(m_calloutButton.data(), &QPushButton::clicked, this, &PDFAppAiPage::onCalloutClicked);
    calloutLayout->addWidget(m_calloutButton, 0, Qt::AlignVCenter);
    contentLayout->addWidget(m_callout);

    // actions 653:5458: top padding 8, gap 10 - the address (flex, JetBrains Mono 13)
    // and the buttons at the trailing end; the buttons go below the address in a
    // narrow card (the minimal window)
    QWidget* actions = new QWidget(content);
    QVBoxLayout* actionsLayout = PDFAppPageWidgets::createColumn(actions, 0);
    actionsLayout->setContentsMargins(0, 8, 0, 0);

    // The address is a plain box (653:5459 bg/surface + text/primary of a live
    // server, 653:6042 bg/subtle + text/muted of a stopped one), not a read-only
    // field of the style (bg/subtle, text/secondary); its frameless field keeps
    // the selection and the copy of the text
    PDFAppAiFillBox* addressBox = new PDFAppAiFillBox(PDFThemeColors::BgSurface, PDFThemeColors::BorderDefault, 8.0, nullptr);
    addressBox->setObjectName(QStringLiteral("addressBox"));
    addressBox->setFixedHeight(36);
    addressBox->setMinimumWidth(Helper::ADDRESS_MINIMUM_WIDTH);
    m_addressBox = addressBox;
    QHBoxLayout* addressLayout = Helper::createRow(addressBox, 0);
    addressLayout->setContentsMargins(Helper::ADDRESS_TEXT_INSET, 1, Helper::ADDRESS_TEXT_INSET, 1);
    m_addressField = new PDFLineEdit(addressBox);
    m_addressField->setObjectName(QStringLiteral("addressField"));
    m_addressField->setFrame(false);
    m_addressField->setReadOnly(true);
    PDFThemeWidgetHelper::bindTextPalette(m_addressField, PDFThemeColors::TextPrimary, PDFThemeColors::TextMuted);
    m_addressField->setFont(PDFThemeFonts::getMonospaceFont(QFont::Normal, 13));
    m_addressField->setAccessibleName(tr("Server address"));
    addressLayout->addWidget(m_addressField, 1, Qt::AlignVCenter);
    addressBox->setFocusWidget(m_addressField);

    QWidget* buttons = new QWidget(actions);
    QHBoxLayout* buttonsLayout = Helper::createRow(buttons, 10);
    m_copyAddressButton = PDFAppPageWidgets::createButton(tr("Copy Address"), QStringLiteral("copy"), false, buttons);
    m_copyAddressButton->setObjectName(QStringLiteral("copyAddressButton"));
    connect(m_copyAddressButton.data(), &QPushButton::clicked, this, &PDFAppAiPage::onCopyAddressClicked);
    buttonsLayout->addWidget(m_copyAddressButton);
    m_startStopButton = PDFAppPageWidgets::createButton(tr("Stop Server"), QStringLiteral("power"), false, buttons);
    m_startStopButton->setObjectName(QStringLiteral("startStopButton"));
    connect(m_startStopButton.data(), &QPushButton::clicked, this, &PDFAppAiPage::onStartStopClicked);
    buttonsLayout->addWidget(m_startStopButton);
    buttonsLayout->addStretch(1);

    // The wider text of the start / stop button decides, when the row is stacked
    const QString stopText = m_startStopButton->text();
    m_startStopButton->setText(tr("Start Server"));
    const int startWidth = m_startStopButton->sizeHint().width();
    m_startStopButton->setText(stopText);
    const int buttonsWidth = m_copyAddressButton->sizeHint().width() + 10 + qMax(startWidth, m_startStopButton->sizeHint().width());
    actionsLayout->addWidget(new PDFAppResponsiveRow(addressBox, buttons, Helper::ADDRESS_MINIMUM_WIDTH + 10 + buttonsWidth, 10, actions));
    contentLayout->addWidget(actions);

    return m_serverCard;
}

PDFAppSettingRow* PDFAppAiPage::createDefinitionRow(const QString& key, const QString& description, QWidget* parent)
{
    PDFAppContext* context = getContext();
    const PDFSettingDefinition* definition = context ? context->getSettingRegistry()->findDefinition(key) : nullptr;
    if (!definition)
    {
        return nullptr;
    }

    PDFAppSettingRow* row = new PDFAppSettingRow(context, *definition, parent);
    row->setDescription(description);
    row->setControlWrapWidth(PDFAppAiPageHelper::CONTROL_COLUMN);
    m_settingRows.append(row);
    return row;
}

QToolButton* PDFAppAiPage::createToolbarButton(const QString& iconName, const QString& toolTip, QWidget* parent)
{
    // Toolbar button 190:538: 36, icon 20 (the style draws the radius and the states)
    QToolButton* button = new QToolButton(parent);
    button->setAutoRaise(true);
    button->setIcon(PDFThemeIcons::getIcon(iconName));
    button->setIconSize(QSize(20, 20));
    button->setFixedSize(36, 36);
    button->setFocusPolicy(Qt::TabFocus);
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    return button;
}

QWidget* PDFAppAiPage::createConnectionGroup(QWidget* parent)
{
    using Helper = PDFAppAiPageHelper;

    // group-Connection 653:5472
    m_connectionGroup = new PDFAppSettingGroup(QStringLiteral("Connection"), tr("Connection"), tr("The server restarts when you change these settings"), parent);
    m_connectionGroup->setObjectName(QStringLiteral("connectionGroup"));

    m_portRow = createDefinitionRow(PDFSettingKeys::mcpPort(), QString(), m_connectionGroup);
    if (m_portRow)
    {
        if (PDFSpinBox* spinBox = m_portRow->getControl() ? m_portRow->getControl()->getSpinBox() : nullptr)
        {
            spinBox->setGroupSeparatorShown(false);
        }
        m_connectionGroup->addRow(m_portRow);
    }

    // Network access: two radios of mcp.allowRemote (653:5494, V gap 8)
    PDFAppSettingRow* networkRow = new PDFAppSettingRow(tr("Network access"), tr("Who can reach the server"), QStringLiteral("network-access"), m_connectionGroup);
    networkRow->setControlWrapWidth(Helper::CONTROL_COLUMN);
    QWidget* radios = new QWidget(networkRow);
    QVBoxLayout* radiosLayout = PDFAppPageWidgets::createColumn(radios, 8);
    m_localRadio = new QRadioButton(tr("This computer only"), radios);
    m_localRadio->setObjectName(QStringLiteral("localRadio"));
    m_remoteRadio = new QRadioButton(tr("Allow remote connections"), radios);
    m_remoteRadio->setObjectName(QStringLiteral("remoteRadio"));
    QButtonGroup* radioGroup = new QButtonGroup(radios);
    radioGroup->setExclusive(true);
    radioGroup->addButton(m_localRadio);
    radioGroup->addButton(m_remoteRadio);
    connect(m_localRadio.data(), &QRadioButton::clicked, this, &PDFAppAiPage::onLocalClicked);
    connect(m_remoteRadio.data(), &QRadioButton::clicked, this, &PDFAppAiPage::onRemoteClicked);
    radiosLayout->addWidget(m_localRadio);
    radiosLayout->addWidget(m_remoteRadio);
    networkRow->setControlWidget(radios);
    m_settingRows.append(networkRow);
    m_connectionGroup->addRow(networkRow);

    // warning-remote 653:6658: amber, after Network access
    PDFAppSettingRow* warningRow = new PDFAppSettingRow(tr("Remote connections"), QString(), QStringLiteral("remote-warning"), m_connectionGroup);
    PDFAppAiFillBox* warning = new PDFAppAiFillBox(PDFAccentHueColors::getColor(PDFAccentHue::Amber, PDFAccentHueColors::Role::Background), std::nullopt, 0.0, warningRow);
    warning->setObjectName(QStringLiteral("remoteWarningBox"));
    QHBoxLayout* warningLayout = new QHBoxLayout(warning);
    warningLayout->setContentsMargins(16, 10, 16, 10);
    warningLayout->setSpacing(10);
    PDFIconBadge* warningIcon = new PDFIconBadge(QStringLiteral("alert-triangle"), 16, 16, warning);
    warningIcon->setColors(std::nullopt, PDFAccentHueColors::getColor(PDFAccentHue::Amber, PDFAccentHueColors::Role::Icon));
    warningLayout->addWidget(warningIcon, 0, Qt::AlignVCenter);
    warningLayout->addWidget(Helper::createText(tr("Anyone on your network who knows the address and the access token can work with your open documents. "
                                                   "Traffic is not encrypted – allow remote connections only on a network you trust."),
                                                QFont::Normal, 12, Helper::NOTE_LINE, PDFThemeColors::TextSecondary, warning), 1);
    warningRow->setBlockWidget(warning, QMargins(0, 0, 0, 0));

    // A flat row without the divider above it (653:6658, 54 high - not the minimal
    // height of a row of texts); the next row has its divider
    warningRow->setDividerSuppressed(true);
    warningRow->setMinimumHeight(0);
    m_remoteWarning = warningRow;
    m_remoteWarning->setObjectName(QStringLiteral("remoteWarning"));
    m_connectionGroup->addRow(warningRow);

    m_requireTokenRow = createDefinitionRow(PDFSettingKeys::mcpRequireToken(), QString(), m_connectionGroup);
    if (m_requireTokenRow)
    {
        m_connectionGroup->addRow(m_requireTokenRow);
    }

    // Access token: the password field 232 and the toolbar buttons copy / new token
    PDFAppSettingRow* tokenRow = new PDFAppSettingRow(tr("Access token"), tr("Keep it secret. A new token disconnects all assistants"), QStringLiteral("access-token"), m_connectionGroup);
    tokenRow->setControlWrapWidth(Helper::CONTROL_COLUMN);
    QWidget* tokenWrap = new QWidget(tokenRow);
    QHBoxLayout* tokenLayout = Helper::createRow(tokenWrap, 8);
    m_tokenField = new PDFLineEdit(tokenWrap);
    m_tokenField->setObjectName(QStringLiteral("tokenField"));
    m_tokenField->setPasswordField();
    m_tokenField->setReadOnly(true);
    m_tokenField->setFixedWidth(Helper::TOKEN_FIELD_WIDTH);
    m_tokenField->setAccessibleName(tr("Access token"));
    tokenLayout->addWidget(m_tokenField);
    m_copyTokenButton = createToolbarButton(QStringLiteral("copy"), tr("Copy Token"), tokenWrap);
    m_copyTokenButton->setObjectName(QStringLiteral("copyTokenButton"));
    connect(m_copyTokenButton.data(), &QToolButton::clicked, this, &PDFAppAiPage::onCopyTokenClicked);
    tokenLayout->addWidget(m_copyTokenButton);
    m_newTokenButton = createToolbarButton(QStringLiteral("refresh-cw"), tr("Create New Token…"), tokenWrap);
    m_newTokenButton->setObjectName(QStringLiteral("newTokenButton"));
    connect(m_newTokenButton.data(), &QToolButton::clicked, this, &PDFAppAiPage::onNewTokenClicked);
    tokenLayout->addWidget(m_newTokenButton);
    tokenLayout->addStretch(1);
    tokenRow->setControlWidget(tokenWrap);
    m_settingRows.append(tokenRow);
    m_connectionGroup->addRow(tokenRow);

    if (PDFAppSettingRow* startRow = createDefinitionRow(PDFSettingKeys::mcpStartWithApp(), tr("Start the server when the application starts"), m_connectionGroup))
    {
        m_connectionGroup->addRow(startRow);
    }

    return m_connectionGroup;
}

QWidget* PDFAppAiPage::createPermissionsGroup(QWidget* parent)
{
    // group-Permissions 653:5543: three rows of the segmented control Allow / Ask / Deny
    m_permissionsGroup = new PDFAppSettingGroup(QStringLiteral("Permissions"), tr("Permissions"),
                                                tr("Ask shows a confirmation for every request · Deny hides the action from assistants"), parent);
    m_permissionsGroup->setObjectName(QStringLiteral("permissionsGroup"));

    const std::pair<QString, QString> rows[] =
    {
        { PDFSettingKeys::mcpPermissionRead(), tr("Text, properties, outline, annotations and page images of open documents; switching between them and closing them") },
        { PDFSettingKeys::mcpPermissionEdit(), tr("Annotations, form fields, page operations and PDF4QT tools") },
        { PDFSettingKeys::mcpPermissionFiles(), tr("Let assistants open documents from the disk and save results") }
    };
    for (const auto& rowInfo : rows)
    {
        if (PDFAppSettingRow* row = createDefinitionRow(rowInfo.first, rowInfo.second, m_permissionsGroup))
        {
            m_permissionsGroup->addRow(row);
        }
    }
    return m_permissionsGroup;
}

QWidget* PDFAppAiPage::createAssistantsSection(QWidget* parent)
{
    using Helper = PDFAppAiPageHelper;

    // connected-assistants 653:5587
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("assistantsSection"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    m_disconnectAllLink = new PDFTextLink(tr("Disconnect All"));
    m_disconnectAllLink->setObjectName(QStringLiteral("disconnectAllLink"));
    connect(m_disconnectAllLink.data(), &PDFTextLink::clicked, this, &PDFAppAiPage::onDisconnectAllClicked);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Connected assistants"), m_disconnectAllLink, section));

    // clients-list: padding 8, gap 2
    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 2, section);
    card->setObjectName(QStringLiteral("assistantsCard"));
    m_assistantsCard = card;
    m_assistantsEmpty = new PDFAppEmptyState(PDFAppEmptyState::Size::Small, QStringLiteral("mcp"), card);
    m_assistantsEmpty->setObjectName(QStringLiteral("assistantsEmpty"));
    card->getLayout()->addWidget(m_assistantsEmpty);
    layout->addWidget(card);

    m_disconnectHint = Helper::createText(QString(), QFont::Normal, 11, Helper::CAPTION_LINE, PDFThemeColors::TextMuted, section);
    m_disconnectHint->setObjectName(QStringLiteral("disconnectHint"));
    layout->addWidget(m_disconnectHint);
    return section;
}

QWidget* PDFAppAiPage::createActivitySection(QWidget* parent)
{
    // recent-activity 653:5609
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("activitySection"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    m_clearLink = new PDFTextLink(tr("Clear"));
    m_clearLink->setObjectName(QStringLiteral("clearLink"));
    connect(m_clearLink.data(), &PDFTextLink::clicked, this, &PDFAppAiPage::onClearClicked);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Recent activity"), m_clearLink, section));

    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 2, section);
    card->setObjectName(QStringLiteral("activityCard"));
    m_activityCard = card;
    // No frame in Figma: the small empty state of the cards of Home
    m_activityEmpty = new PDFAppEmptyState(PDFAppEmptyState::Size::Small, QStringLiteral("hourglass"), card);
    m_activityEmpty->setObjectName(QStringLiteral("activityEmpty"));
    m_activityEmpty->setTexts(tr("No activity yet"), tr("Requests of the assistants appear here."));
    card->getLayout()->addWidget(m_activityEmpty);
    layout->addWidget(card);
    return section;
}

QWidget* PDFAppAiPage::createLinksSection(QWidget* parent)
{
    // links 653:5645: three Link rows
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("learnMoreSection"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Learn more"), nullptr, section));

    PDFAppCard* card = PDFAppPageWidgets::createCard(8, 2, section);
    card->setObjectName(QStringLiteral("learnMoreCard"));
    layout->addWidget(card);

    PDFLinkRow* guide = new PDFLinkRow(tr("AI guide"), tr("What assistants can do with your documents"), QStringLiteral("book-open"), card);
    guide->setObjectName(QStringLiteral("linkGuide"));
    withLink(guide, PDFAppRoutes::help(QStringLiteral("ai-tools")).toString());
    PDFLinkRow* protocol = new PDFLinkRow(tr("Model Context Protocol"), tr("The open standard behind the server"), QStringLiteral("mcp"), card);
    protocol->setObjectName(QStringLiteral("linkProtocol"));
    withLink(protocol, QStringLiteral("https://modelcontextprotocol.io"));
    PDFLinkRow* issue = new PDFLinkRow(tr("Report an issue"), tr("Tell us about a problem"), QStringLiteral("alert-circle"), card);
    issue->setObjectName(QStringLiteral("linkIssue"));
    withLink(issue, PDFAppProjectLinks::getNewIssue().toString());
    card->getLayout()->addWidget(guide);
    card->getLayout()->addWidget(protocol);
    card->getLayout()->addWidget(issue);
    return section;
}

QWidget* PDFAppAiPage::createNote(QWidget* parent)
{
    using Helper = PDFAppAiPageHelper;

    // privacy-note 653:5679 / remote-note 653:6849: p14 (+ border), gap 12, circle 36 bg/surface, icon 18
    m_note = new PDFTintedCard(PDFAccentHue::Green, parent);
    m_note->setObjectName(QStringLiteral("aiNote"));
    QHBoxLayout* layout = new QHBoxLayout(m_note);
    layout->setContentsMargins(15, 15, 15, 15);
    layout->setSpacing(12);
    m_noteIcon = new PDFIconBadge(QStringLiteral("shield"), 36, 18, m_note);
    layout->addWidget(m_noteIcon, 0, Qt::AlignTop);
    QWidget* text = new QWidget(m_note);
    QVBoxLayout* textLayout = PDFAppPageWidgets::createColumn(text, 2);
    m_noteTitle = Helper::createText(QString(), QFont::Bold, 14, 17, PDFThemeColors::TextPrimary, text);
    m_noteText = Helper::createText(QString(), QFont::Normal, 12, Helper::NOTE_LINE, PDFThemeColors::TextSecondary, text);
    textLayout->addWidget(m_noteTitle);
    textLayout->addWidget(m_noteText);
    layout->addWidget(text, 1);
    return m_note;
}

QWidget* PDFAppAiPage::createConnectSection(QWidget* parent)
{
    using Helper = PDFAppAiPageHelper;

    // assistants 653:5686: the header, six tiles and the setup card (V gap 12)
    QWidget* section = new QWidget(parent);
    section->setObjectName(QStringLiteral("connectSection"));
    QVBoxLayout* layout = PDFAppPageWidgets::createColumn(section, 12);

    m_connectHint = Helper::createText(QString(), QFont::Normal, 12, 15, PDFThemeColors::TextMuted, section);
    layout->addWidget(PDFAppPageWidgets::createSectionHeader(tr("Connect an assistant"), m_connectHint, section));

    PDFAppGrid* tiles = new PDFAppGrid(Helper::TILE_MINIMUM_WIDTH, Helper::TILE_COLUMNS, Helper::TILE_GAP, Helper::TILE_GAP, section);
    tiles->setObjectName(QStringLiteral("assistantTiles"));
    m_tileGroup = new QButtonGroup(tiles);
    m_tileGroup->setExclusive(true);
    for (PDFAppMcpSetupClient client : PDFAppMcpClientSetup::getTileClients())
    {
        // assistant-tiles 653:5690: the logo 36 with the monogram Bold 13 (Other client: the mcp icon in teal)
        PDFComponentPainter::AssistantTile tileData;
        tileData.name = PDFAppMcpClientSetup::getTileName(client);
        tileData.vendor = PDFAppMcpClientSetup::getVendor(client);
        const QString clientInfoName = PDFAppMcpClientSetup::getClientInfoName(client);
        if (!clientInfoName.isEmpty())
        {
            const PDFAppMcpClientInfo info = PDFAppMcpClientNames::describe(clientInfoName);
            tileData.logo.monogram = info.monogram;
            tileData.logo.hue = info.hue;
        }
        tileData.logo.extent = 36;
        tileData.logo.fontPixelSize = 13;

        PDFAssistantTile* tile = new PDFAssistantTile(tileData, tiles);
        tile->setObjectName(QStringLiteral("tile-%1").arg(int(client)));
        m_tileGroup->addButton(tile, int(client));
        tiles->addWidget(tile);
        m_tiles.append(tile);
    }
    connect(m_tileGroup.data(), &QButtonGroup::idToggled, this, &PDFAppAiPage::onTileToggled);
    layout->addWidget(tiles);

    m_setupCard = PDFAppPageWidgets::createCard(16, 16, section);
    m_setupCard->setObjectName(QStringLiteral("setupCard"));
    layout->addWidget(m_setupCard);
    return section;
}

// ------------------------------------------------------------------ State

PDFAppAiPage::ShownState PDFAppAiPage::computeShownState() const
{
    if (!m_controller)
    {
        return ShownState::Stopped;
    }

    switch (m_controller->getState())
    {
        case PDFAppMcpServerState::Stopped:
            return ShownState::Stopped;
        case PDFAppMcpServerState::Starting:
            return ShownState::Starting;
        case PDFAppMcpServerState::Running:
        {
            const PDFMcpServer* server = m_controller->getServer();
            const bool isRemote = server ? server->getConfig().bindMode == PDFMcpBindMode::Remote : isRemoteAllowed();
            return isRemote ? ShownState::Remote : ShownState::Running;
        }
        case PDFAppMcpServerState::OtherInstance:
            return ShownState::OtherInstance;
        case PDFAppMcpServerState::PortInUse:
            return ShownState::PortInUse;
        case PDFAppMcpServerState::Failed:
            return ShownState::Failed;
    }
    return ShownState::Stopped;
}

QString PDFAppAiPage::getShownAddress() const
{
    if (!m_controller)
    {
        return QString();
    }

    // The address of another instance comes from its row (sanitized by the repository)
    if (m_shownState == ShownState::OtherInstance)
    {
        const PDFMcpServerRow row = m_controller->getServerRow();
        if (row.exists && PDFAppMcpClientSetup::isSafeAddress(row.address))
        {
            return row.address;
        }
    }
    return m_controller->getAddress();
}

bool PDFAppAiPage::isRemoteAllowed() const
{
    PDFAppContext* context = getContext();
    return context && context->getDatabase() && context->getSettings().getBool(PDFSettingKeys::mcpAllowRemote());
}

bool PDFAppAiPage::isTokenRequired() const
{
    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase())
    {
        return true;
    }
    const PDFSettingsRepository settings = context->getSettings();
    return settings.getBool(PDFSettingKeys::mcpRequireToken()) || settings.getBool(PDFSettingKeys::mcpAllowRemote());
}

QString PDFAppAiPage::getShownToken() const
{
    return isTokenRequired() && m_controller ? m_controller->getToken() : QString();
}

// ------------------------------------------------------------------ Updates

void PDFAppAiPage::updateAll()
{
    updateServer();
    updateConnection();
    updateAssistants();
    updateActivity();
    updateSetup();
}

void PDFAppAiPage::updateServer()
{
    m_shownState = computeShownState();
    if (!m_serverCard)
    {
        return;
    }

    const bool isRunning = m_shownState == ShownState::Running || m_shownState == ShownState::Remote;
    const bool isProblem = m_shownState == ShownState::PortInUse || m_shownState == ShownState::Failed;
    const quint16 port = m_controller ? m_controller->getPort() : 0;

    // Blue while running (also remote - map 5.2), plain otherwise; the logo neutral
    m_serverCard->setHue(isRunning ? std::optional<PDFAccentHue>(PDFAccentHue::Blue) : std::nullopt);
    m_serverCard->setAccessibleDescription(getStateText());
    if (m_serverLogo)
    {
        if (isRunning)
        {
            // 653:5447: the icon mcp in accent/blue/icon
            m_serverLogo->setColors(PDFThemeColors::BgSurface, PDFAccentHueColors::getColor(PDFAccentHue::Blue, PDFAccentHueColors::Role::Icon),
                                    PDFAccentHueColors::getColor(PDFAccentHue::Blue, PDFAccentHueColors::Role::Border));
        }
        else
        {
            // 653:6030: accent/neutral/bg with the icon accent/neutral/icon
            m_serverLogo->setColors(PDFAccentHueColors::getColor(PDFAccentHue::Neutral, PDFAccentHueColors::Role::Background),
                                    PDFAccentHueColors::getColor(PDFAccentHue::Neutral, PDFAccentHueColors::Role::Icon));
        }
    }

    if (m_stateBadge)
    {
        m_stateBadge->setTextColor(std::nullopt);
        switch (m_shownState)
        {
            case ShownState::Stopped:
                m_stateBadge->setBadge(tr("Stopped"), QString(), PDFAccentHue::Neutral);
                break;
            case ShownState::Starting:
                m_stateBadge->setBadge(tr("Starting…"), QString(), PDFAccentHue::Neutral);
                break;
            case ShownState::Running:
            case ShownState::Remote:
                m_stateBadge->setBadge(tr("Running"), QString(), PDFAccentHue::Green);
                break;
            case ShownState::OtherInstance:
                m_stateBadge->setBadge(tr("In another PDF4QT window"), QString(), PDFAccentHue::Blue);
                break;
            case ShownState::PortInUse:
            case ShownState::Failed:
                m_stateBadge->setBadge(tr("Could not start"), QString(), PDFAccentHue::Red);
                m_stateBadge->setTextColor(PDFThemeColors::TextDanger);
                break;
        }
    }
    if (m_remoteBadge)
    {
        m_remoteBadge->setBadge(tr("Remote access"), QString(), PDFAccentHue::Amber);
        m_remoteBadge->setVisible(m_shownState == ShownState::Remote);
    }

    if (m_serverMeta)
    {
        QString where = tr("not running");
        switch (m_shownState)
        {
            case ShownState::Running:
                where = tr("this computer only");
                break;
            case ShownState::Remote:
                where = tr("all network interfaces");
                break;
            case ShownState::OtherInstance:
                where = tr("runs in another PDF4QT window");
                break;
            case ShownState::Starting:
                where = tr("starting");
                break;
            default:
                break;
        }
        m_serverMeta->setText(tr("Model Context Protocol · Streamable HTTP · %1").arg(where));
    }

    // Callouts of another instance (blue, Show Other Window) and of a port in use (red, Change Port)
    if (m_callout)
    {
        const bool isOther = m_shownState == ShownState::OtherInstance;
        m_callout->setVisible(isOther || isProblem);
        m_callout->setHue(isOther ? PDFAccentHue::Blue : PDFAccentHue::Red);
        if (m_calloutIcon)
        {
            m_calloutIcon->setIconName(isOther ? QStringLiteral("info") : QStringLiteral("alert-circle"));
            m_calloutIcon->setColors(std::nullopt, PDFAccentHueColors::getColor(isOther ? PDFAccentHue::Blue : PDFAccentHue::Red, PDFAccentHueColors::Role::Icon));
        }
        if (isOther)
        {
            m_calloutTitle->setText(tr("The server runs in another PDF4QT window"));
            m_calloutText->setText(tr("Assistants work only with the documents open in that window. Settings and permissions on this page are shared and apply to the running server. "
                                      "To run the server in this window, stop it in the other PDF4QT window first."));
            m_calloutButton->setText(tr("Show Other Window"));

            // A portable instance found the other PDF4QT by the probe of the port
            // only: it cannot reach that window (plan 2.6)
            const PDFMcpServerRow row = m_controller ? m_controller->getServerRow() : PDFMcpServerRow();
            m_calloutButton->setVisible(row.exists && row.state != PDFMcpServerRowState::Stopped);
        }
        else if (m_shownState == ShownState::PortInUse)
        {
            m_calloutTitle->setText(tr("Port %1 is used by another program").arg(port));
            m_calloutText->setText(tr("PDF4QT could not start the server because another program on this computer already uses port %1. "
                                      "Choose a free port and start the server again – assistants set up for port %1 must then be set up again.").arg(port));
            m_calloutButton->setText(tr("Change Port"));
            m_calloutButton->setVisible(true);
        }
        else if (m_shownState == ShownState::Failed)
        {
            // No frame: the pattern of the port in use with the message of the server
            const QString message = m_controller ? PDFAppMcpClientNames::sanitizeName(m_controller->getErrorMessage()) : QString();
            m_calloutTitle->setText(tr("The server could not start"));
            m_calloutText->setText(message.isEmpty() ? tr("Check the port and the network settings and start the server again.") : message);
            m_calloutButton->setText(tr("Change Port"));
            m_calloutButton->setVisible(true);
        }
    }

    // The address: bg/subtle of a stopped server (653:6042), the address of the other window
    if (m_addressField)
    {
        const QString address = getShownAddress();
        if (m_addressField->text() != address)
        {
            m_addressField->setText(address);
        }
        const bool isLive = isRunning || m_shownState == ShownState::OtherInstance;
        m_addressField->setEnabled(isLive);
        if (PDFAppAiFillBox* addressBox = dynamic_cast<PDFAppAiFillBox*>(m_addressBox.data()))
        {
            addressBox->setFill(isLive ? PDFThemeColors::BgSurface : PDFThemeColors::BgSubtle);
        }
    }

    if (m_startStopButton)
    {
        // Stop Server (Secondary) while running, Start Server (Primary) otherwise,
        // disabled for another window and while starting (940:135381)
        m_startStopButton->setText(isRunning ? tr("Stop Server") : tr("Start Server"));
        m_startStopButton->setEnabled(m_shownState != ShownState::OtherInstance && m_shownState != ShownState::Starting && !m_controller.isNull());
        const bool isPrimary = !isRunning && m_shownState != ShownState::OtherInstance && m_shownState != ShownState::Starting;
        if (m_startStopButton->property(PDFWidgetStyleProperties::PRIMARY).toBool() != isPrimary)
        {
            PDFWidgetStyleProperties::set(m_startStopButton, PDFWidgetStyleProperties::PRIMARY, isPrimary);
        }
    }

    // The note of the side column: privacy (green) or remote (amber)
    if (m_note)
    {
        const bool isRemoteNote = m_shownState == ShownState::Remote;
        m_note->setHue(isRemoteNote ? PDFAccentHue::Amber : PDFAccentHue::Green);
        m_noteIcon->setIconName(isRemoteNote ? QStringLiteral("network") : QStringLiteral("shield"));
        m_noteIcon->setColors(PDFThemeColors::BgSurface, isRemoteNote ? PDFAccentHueColors::getColor(PDFAccentHue::Amber, PDFAccentHueColors::Role::Icon) : PDFThemeColors::IconDefault);
        m_noteTitle->setText(isRemoteNote ? tr("Remote access is on") : tr("Your documents stay with you"));
        m_noteText->setText(isRemoteNote ? tr("Assistants on other computers get what they ask for, within the permissions you allow. The connection is not encrypted.")
                                         : tr("PDF4QT sends nothing on its own. An assistant gets only what it asks for, within the permissions you allow."));
    }

    if (m_connectHint)
    {
        m_connectHint->setText(isRunning || m_shownState == ShownState::OtherInstance ? tr("Pick your assistant – the commands are prepared for this server")
                                                                                       : tr("You can set up an assistant now and start the server later"));
    }
}

void PDFAppAiPage::updateConnection()
{
    PDFAppContext* context = getContext();
    const bool hasDatabase = context && context->getDatabase();

    if (m_connectionGroup)
    {
        QString hint = tr("The server restarts when you change these settings");
        if (m_shownState == ShownState::Stopped || m_shownState == ShownState::Starting)
        {
            hint = tr("Used the next time the server starts");
        }
        else if (m_shownState == ShownState::OtherInstance)
        {
            hint = tr("The server in the other PDF4QT window restarts when you change these settings");
        }
        else if (m_shownState == ShownState::PortInUse || m_shownState == ShownState::Failed)
        {
            hint = tr("Change the port, then start the server again");
        }
        m_connectionGroup->setTexts(tr("Connection"), hint);
    }

    for (const QPointer<PDFAppSettingRow>& row : std::as_const(m_settingRows))
    {
        if (row && row->getControl())
        {
            row->updateFromSettings(QString());
        }
    }

    // Port: the description in text/danger and the red border of a port in use (955:148654)
    if (m_portRow)
    {
        const bool isPortProblem = m_shownState == ShownState::PortInUse;
        const quint16 port = m_controller ? m_controller->getPort() : 0;
        m_portRow->setDescription(isPortProblem ? tr("Port %1 is used by another program – choose a free port (1024–65535)").arg(port)
                                                : tr("TCP port of the local HTTP server (1024–65535). After a change, set up the assistants again"));
        if (PDFAppParagraph* description = m_portRow->getDescriptionLabel())
        {
            description->setTextColor(isPortProblem ? PDFThemeColors::TextDanger : PDFThemeColors::TextMuted);
        }
        if (QWidget* editor = m_portRow->getControl() ? m_portRow->getControl()->getEditor() : nullptr)
        {
            if (editor->property(PDFWidgetStyleProperties::INVALID).toBool() != isPortProblem)
            {
                PDFWidgetStyleProperties::set(editor, PDFWidgetStyleProperties::INVALID, isPortProblem);
                editor->update();
            }
        }
    }

    const bool isRemote = isRemoteAllowed();
    if (m_localRadio && m_remoteRadio)
    {
        m_localRadio->setChecked(!isRemote);
        m_remoteRadio->setChecked(isRemote);
        m_localRadio->setEnabled(hasDatabase);
        m_remoteRadio->setEnabled(hasDatabase);
    }
    if (m_remoteWarning)
    {
        m_remoteWarning->setVisible(isRemote);
    }

    // Require access token: checked and disabled while remote connections are allowed (653:6662)
    if (m_requireTokenRow)
    {
        m_requireTokenRow->setDescription(isRemote ? tr("Always required while remote connections are allowed") : tr("Assistants must send the token with every request"));
        if (QCheckBox* checkBox = m_requireTokenRow->getControl() ? m_requireTokenRow->getControl()->getCheckBox() : nullptr)
        {
            if (isRemote)
            {
                const QSignalBlocker blocker(checkBox);
                checkBox->setChecked(true);
            }
            checkBox->setEnabled(!isRemote && hasDatabase);
        }
    }

    if (m_tokenField)
    {
        const QString token = m_controller ? m_controller->getToken() : QString();
        if (m_tokenField->text() != token)
        {
            m_tokenField->setText(token);
        }
    }
    if (m_copyTokenButton)
    {
        m_copyTokenButton->setEnabled(m_tokenField && !m_tokenField->text().isEmpty());
    }
    if (m_newTokenButton)
    {
        m_newTokenButton->setEnabled(hasDatabase && m_controller);
    }

    // An open box Create a new access token? follows Require access token
    if (m_newTokenDialog)
    {
        m_newTokenDialog->setTokenRequired(isTokenRequired());
    }
}

void PDFAppAiPage::updateAssistants()
{
    using Helper = PDFAppAiPageHelper;

    PDFAppContext* context = getContext();
    const bool isLive = m_shownState == ShownState::Running || m_shownState == ShownState::Remote || m_shownState == ShownState::OtherInstance;

    // The rows mcp_client (written by the owner, bounded and sanitized by the repository)
    std::vector<PDFMcpClientRow> clients;
    if (isLive && context && context->getDatabase())
    {
        clients = PDFMcpRepository(context->getDatabase()).getClients();
    }

    if (m_disconnectAllLink)
    {
        m_disconnectAllLink->setVisible(isLive);
        m_disconnectAllLink->setEnabled(!clients.empty());
    }
    if (m_disconnectHint)
    {
        m_disconnectHint->setVisible(isLive);
        m_disconnectHint->setText(m_shownState == ShownState::OtherInstance
                                      ? tr("Disconnect and Clear act on the server in the other PDF4QT window. To block an assistant, create a new access token.")
                                      : tr("Disconnecting ends the session. To block an assistant, create a new access token."));
    }
    if (m_assistantsEmpty)
    {
        // 653:6172; while the server runs (no frame, map 4.9) the hint of the setup
        m_assistantsEmpty->setTexts(tr("No assistant is connected"), isLive ? tr("Set up an assistant below to connect it.")
                                                                            : tr("Start the server to accept connections."));
        m_assistantsEmpty->setVisible(clients.empty());
    }

    // The rows are built again only for other data (the focus of a row stays)
    bool isSame = m_hasShownClients && clients.size() == m_shownClients.size();
    for (size_t i = 0; isSame && i < clients.size(); ++i)
    {
        isSame = clients[i].key == m_shownClients[i].key && clients[i].name == m_shownClients[i].name && clients[i].address == m_shownClients[i].address &&
                 clients[i].connected == m_shownClients[i].connected;
    }
    if (isSame && m_assistantRows.size() == qsizetype(clients.size()))
    {
        // The texts of the language
        for (qsizetype i = 0; i < m_assistantRows.size(); ++i)
        {
            if (m_assistantRows[i])
            {
                m_assistantRows[i]->setDisconnectToolTip(tr("Disconnect"));
            }
        }
        return;
    }

    for (const QPointer<PDFAssistantRow>& row : std::as_const(m_assistantRows))
    {
        if (row)
        {
            row->hide();
            row->deleteLater();
        }
    }
    m_assistantRows.clear();
    m_shownClients = clients;
    m_hasShownClients = true;

    PDFAppCard* card = qobject_cast<PDFAppCard*>(m_assistantsCard.data());
    if (!card)
    {
        return;
    }

    for (const PDFMcpClientRow& client : clients)
    {
        // client row 653:5591: "This computer · connected at 14:02" / "192.168.1.57 · connected at 16:05" + Tag Remote
        PDFComponentPainter::AssistantRow rowData;
        rowData.name = PDFAppMcpClientNames::sanitizeName(client.name);
        rowData.avatar = Helper::createAvatar(client.name, 32, 12);
        const QString where = client.address.isEmpty() ? tr("This computer") : PDFAppMcpClientNames::sanitizeName(client.address);
        const QString time = Helper::getTime(client.connected);
        rowData.meta = time.isEmpty() ? where : tr("%1 · connected at %2").arg(where, time);
        rowData.tag = client.address.isEmpty() ? QString() : tr("Remote");

        PDFAssistantRow* row = new PDFAssistantRow(rowData, card);
        row->setObjectName(QStringLiteral("assistantRow"));
        row->setProperty("assistantKey", client.key);
        row->setDisconnectToolTip(tr("Disconnect"));
        connect(row, &PDFAssistantRow::disconnectClicked, this, &PDFAppAiPage::onDisconnectClicked);
        card->getLayout()->addWidget(row);
        m_assistantRows.append(row);
    }
}

void PDFAppAiPage::updateActivity()
{
    using Helper = PDFAppAiPageHelper;

    PDFAppContext* context = getContext();
    std::vector<PDFMcpActivityRow> rows;
    if (context && context->getDatabase())
    {
        rows = PDFMcpRepository(context->getDatabase()).getActivity(ACTIVITY_ROW_COUNT);
    }

    // The waiting confirmations of this instance (Show reopens them)
    std::vector<std::shared_ptr<PDFAppMcpConfirmation>> confirmations;
    if (PDFAppMcpHost* host = m_controller ? m_controller->getHost() : nullptr)
    {
        confirmations = host->getPendingConfirmations();
    }

    const bool isOtherInstance = m_shownState == ShownState::OtherInstance;
    std::vector<ActivityItem> items;
    for (const PDFMcpActivityRow& row : rows)
    {
        ActivityItem item;
        item.row = row;
        if (row.status == PDFMcpActivityStatus::Waiting)
        {
            const QString callId = row.detail.value(QStringLiteral("callId")).toString();
            for (const std::shared_ptr<PDFAppMcpConfirmation>& confirmation : confirmations)
            {
                if (!callId.isEmpty() && confirmation && confirmation->call && confirmation->call->getCallId() == callId)
                {
                    item.confirmationId = confirmation->id;
                    break;
                }
            }
        }
        items.push_back(std::move(item));
    }

    if (m_activityEmpty)
    {
        m_activityEmpty->setVisible(items.empty());
    }
    if (m_clearLink)
    {
        m_clearLink->setVisible(!items.empty());
    }

    PDFAppCard* card = qobject_cast<PDFAppCard*>(m_activityCard.data());
    if (!card)
    {
        return;
    }

    // The widgets are reused (the focus of a row stays), more are created, the rest hidden
    for (size_t i = 0; i < items.size(); ++i)
    {
        const ActivityItem& item = items[i];
        PDFComponentPainter::ActivityRow rowData;
        rowData.title = PDFAppMcpActivity::getTitle(item.row.action, item.row.detail);
        rowData.time = Helper::getTime(item.row.time);
        rowData.meta = PDFAppMcpActivity::getMetaText(PDFAppMcpClientNames::sanitizeName(item.row.client), item.row.document, item.row.detail);
        Helper::setStatusLook(item.row.status, rowData);

        const bool isWaitingHere = item.row.status == PDFMcpActivityStatus::Waiting && item.confirmationId != 0;
        const bool isWaitingElsewhere = item.row.status == PDFMcpActivityStatus::Waiting && item.confirmationId == 0 && isOtherInstance;
        rowData.status = PDFAppMcpActivity::getStatusText(item.row.status, PDFAppMcpClientNames::sanitizeName(item.row.client), item.row.detail, isWaitingElsewhere);
        if (isWaitingHere)
        {
            // "Waiting for your confirmation · " + "Show" (955:147547)
            rowData.status += QStringLiteral(" · ");
            rowData.linkText = tr("Show");
        }

        PDFActivityRow* rowWidget = i < size_t(m_activityRows.size()) ? m_activityRows[qsizetype(i)].data() : nullptr;
        if (!rowWidget)
        {
            rowWidget = new PDFActivityRow(card);
            rowWidget->setObjectName(QStringLiteral("activityRow"));
            connect(rowWidget, &PDFActivityRow::clicked, this, &PDFAppAiPage::onActivityClicked);
            card->getLayout()->addWidget(rowWidget);
            if (i < size_t(m_activityRows.size()))
            {
                m_activityRows[qsizetype(i)] = rowWidget;
            }
            else
            {
                m_activityRows.append(rowWidget);
            }
        }
        rowWidget->setRow(rowData);
        rowWidget->setClickable(isWaitingHere);
        rowWidget->setProperty("confirmationId", QVariant::fromValue(item.confirmationId));
        rowWidget->setVisible(true);
    }
    for (qsizetype i = qsizetype(items.size()); i < m_activityRows.size(); ++i)
    {
        if (m_activityRows[i])
        {
            m_activityRows[i]->setVisible(false);
            m_activityRows[i]->setClickable(false);
        }
    }
    m_activityItems = std::move(items);
}

void PDFAppAiPage::updateSetup()
{
    // The tile of the client (Claude Desktop is a variant of the tile Claude)
    const PDFAppMcpSetupClient tileClient = m_setupClient == PDFAppMcpSetupClient::ClaudeDesktop ? PDFAppMcpSetupClient::ClaudeCode : m_setupClient;
    for (const QPointer<PDFAssistantTile>& tile : std::as_const(m_tiles))
    {
        if (tile && m_tileGroup)
        {
            const bool isSelected = m_tileGroup->id(tile) == int(tileClient);
            if (tile->isSelected() != isSelected)
            {
                const QSignalBlocker blocker(m_tileGroup);
                tile->setSelected(isSelected);
            }
        }
    }

    // The card is built again only for other texts (the address, the token, the
    // client, the language)
    const PDFAppMcpSetup display = PDFAppMcpClientSetup::createForDisplay(m_setupClient, getShownAddress(), getShownToken());
    QStringList signatureParts = { QString::number(int(m_setupClient)), display.title, display.subtitle, display.copyButtonText, display.note };
    for (const PDFAppMcpSetupStep& step : display.steps)
    {
        signatureParts << step.text << step.code;
    }
    const QString signature = signatureParts.join(QChar(0x1F));
    if (signature != m_setupSignature)
    {
        m_setupSignature = signature;
        rebuildSetupCard();
    }

    // Copy Command is Primary while a server runs (653:5787), Secondary otherwise (653:6357)
    if (m_copySetupButton)
    {
        const bool isPrimary = m_shownState == ShownState::Running || m_shownState == ShownState::Remote || m_shownState == ShownState::OtherInstance;
        if (m_copySetupButton->property(PDFWidgetStyleProperties::PRIMARY).toBool() != isPrimary)
        {
            PDFWidgetStyleProperties::set(m_copySetupButton, PDFWidgetStyleProperties::PRIMARY, isPrimary);
        }
    }
}

void PDFAppAiPage::rebuildSetupCard()
{
    using Helper = PDFAppAiPageHelper;

    PDFAppCard* card = qobject_cast<PDFAppCard*>(m_setupCard.data());
    if (!card)
    {
        return;
    }

    // The old content goes (its focus is given back to the same kind of widget)
    const QString focusName = PDFAppPageRebuild::getFocusedName(card);
    for (QWidget* child : card->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
    {
        child->hide();
        child->deleteLater();
    }
    m_variantChips.clear();
    m_codeBoxes.clear();
    delete m_variantGroup.data();

    const PDFAppMcpSetup display = PDFAppMcpClientSetup::createForDisplay(m_setupClient, getShownAddress(), getShownToken());
    const PDFAppMcpSetup full = getSetup();
    QVBoxLayout* layout = card->getLayout();

    // header 653:5731: the title Semi Bold 14, the subtitle 12 and the variant chips
    QWidget* header = new QWidget(card);
    QVBoxLayout* headerLayout = PDFAppPageWidgets::createColumn(header, 2);
    QWidget* titles = new QWidget(header);
    QVBoxLayout* titlesLayout = PDFAppPageWidgets::createColumn(titles, 2);
    PDFAppParagraph* title = Helper::createText(display.title, QFont::DemiBold, 14, 17, PDFThemeColors::TextPrimary, titles);
    title->setObjectName(QStringLiteral("setupTitle"));
    titlesLayout->addWidget(title);
    titlesLayout->addWidget(Helper::createText(display.subtitle, QFont::Normal, 12, 15, PDFThemeColors::TextTertiary, titles));

    const PDFAppMcpSetupClient tileClient = m_setupClient == PDFAppMcpSetupClient::ClaudeDesktop ? PDFAppMcpSetupClient::ClaudeCode : m_setupClient;
    const std::vector<PDFAppMcpSetupClient> variants = PDFAppMcpClientSetup::getVariants(tileClient);
    QWidget* chips = nullptr;
    if (variants.size() > 1)
    {
        // Filter chips 653:5735 (gap 6)
        chips = new QWidget(header);
        PDFAppFlowLayout* chipsLayout = new PDFAppFlowLayout(chips, 6, 6);
        m_variantGroup = new QButtonGroup(chips);
        m_variantGroup->setExclusive(true);
        for (PDFAppMcpSetupClient variant : variants)
        {
            const QString name = PDFAppMcpClientSetup::getVariantName(variant);
            QPushButton* chip = new QPushButton(PDFAppPageWidgets::escapeMnemonic(name), chips);
            chip->setObjectName(QStringLiteral("variant-%1").arg(int(variant)));
            chip->setProperty(PDFWidgetStyleProperties::CHIP, true);
            chip->setCheckable(true);
            chip->setAutoDefault(false);
            chip->setChecked(variant == m_setupClient);
            chip->setAccessibleName(name);
            m_variantGroup->addButton(chip, int(variant));
            chipsLayout->addWidget(chip);
            m_variantChips.append(chip);
        }
        connect(m_variantGroup.data(), &QButtonGroup::idToggled, this, &PDFAppAiPage::onVariantToggled);
    }
    PDFAppResponsiveRow* headerRow = new PDFAppResponsiveRow(titles, chips ? chips : new QWidget(header), 560, 12, header);
    headerLayout->addWidget(headerRow);
    layout->addWidget(header);

    // steps 653:5748 (V gap 14): the first step in the whole width, the others in
    // two columns of the width (H gap 24, 653:5761)
    QWidget* steps = new QWidget(card);
    QVBoxLayout* stepsLayout = PDFAppPageWidgets::createColumn(steps, 14);
    PDFAppGrid* stepGrid = nullptr;
    for (size_t i = 0; i < display.steps.size(); ++i)
    {
        const PDFAppMcpSetupStep& step = display.steps[i];
        QWidget* stepWidget = new QWidget(steps);
        stepWidget->setObjectName(QStringLiteral("setupStep%1").arg(i + 1));
        QHBoxLayout* stepLayout = Helper::createRow(stepWidget, 12);
        PDFAppStepBadge* number = new PDFAppStepBadge(stepWidget);
        number->setText(QString::number(i + 1));
        stepLayout->addWidget(number, 0, Qt::AlignTop);
        QWidget* stepContent = new QWidget(stepWidget);
        QVBoxLayout* contentLayout = PDFAppPageWidgets::createColumn(stepContent, 8);
        contentLayout->setContentsMargins(0, 2, 0, 0);
        contentLayout->addWidget(Helper::createText(step.text, QFont::Normal, 13, Helper::STEP_LINE, PDFThemeColors::TextSecondary, stepContent));
        if (step.codeKind != PDFAppMcpSetupCodeKind::None && !step.code.isEmpty())
        {
            PDFCodeBox* codeBox = new PDFCodeBox(step.code, stepContent);
            codeBox->setObjectName(QStringLiteral("codeBox%1").arg(i + 1));
            codeBox->setMonospace(step.codeKind != PDFAppMcpSetupCodeKind::Prompt);
            codeBox->setCopyToolTip(tr("Copy"));
            codeBox->setAccessibleName(step.text);
            if (i < full.steps.size())
            {
                // The shown token is shortened, Copy gives the whole text
                codeBox->setCopyText(full.steps[i].code);
            }
            contentLayout->addWidget(codeBox);
            m_codeBoxes.append(codeBox);
        }
        contentLayout->addStretch(1);
        stepLayout->addWidget(stepContent, 1);

        if (i == 0)
        {
            stepsLayout->addWidget(stepWidget);
        }
        else
        {
            if (!stepGrid)
            {
                stepGrid = new PDFAppGrid(260, 2, 24, 14, steps);
                stepsLayout->addWidget(stepGrid);
            }
            stepGrid->addWidget(stepWidget);
        }
    }
    layout->addWidget(steps);

    // actions 653:5786 (gap 10): Copy Command, the documentation and the note,
    // which goes below the buttons in a narrow card (the minimal window)
    QWidget* buttons = new QWidget(card);
    PDFAppFlowLayout* buttonsLayout = new PDFAppFlowLayout(buttons, 10, 10);
    m_copySetupButton = PDFAppPageWidgets::createButton(display.copyButtonText, QStringLiteral("copy"), false, buttons);
    m_copySetupButton->setObjectName(QStringLiteral("copySetupButton"));
    connect(m_copySetupButton.data(), &QPushButton::clicked, this, &PDFAppAiPage::onCopySetupClicked);
    buttonsLayout->addWidget(m_copySetupButton);
    m_documentationButton = PDFAppPageWidgets::createButton(display.documentationText, QStringLiteral("external-link"), false, buttons);
    m_documentationButton->setObjectName(QStringLiteral("documentationButton"));
    withLink(m_documentationButton, display.documentationLink);
    m_documentationButton->setVisible(!display.documentationText.isEmpty() && !display.documentationLink.isEmpty());
    buttonsLayout->addWidget(m_documentationButton);

    // note 653:5797: pl 6, gap 8, the icon info 14 and Regular 12 text/muted
    QWidget* note = new QWidget(card);
    note->setObjectName(QStringLiteral("setupNote"));
    QHBoxLayout* noteLayout = Helper::createRow(note, 8);
    noteLayout->setContentsMargins(6, 0, 0, 0);
    PDFIconBadge* icon = new PDFIconBadge(QStringLiteral("info"), 14, 14, note);
    icon->setColors(std::nullopt, PDFThemeColors::IconMuted);
    noteLayout->addWidget(icon, 0, Qt::AlignVCenter);
    noteLayout->addWidget(Helper::createText(display.note, QFont::Normal, 12, 15, PDFThemeColors::TextMuted, note), 1);
    note->setVisible(!display.note.isEmpty());

    // The buttons keep their width beside the note, which takes the rest (653:5797)
    QSizePolicy buttonsPolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    buttonsPolicy.setHeightForWidth(true);
    buttons->setSizePolicy(buttonsPolicy);
    PDFAppResponsiveRow* actions = new PDFAppResponsiveRow(buttons, note, 960, 10, card);
    layout->addWidget(actions);

    PDFAppPageRebuild::restoreFocus(card, focusName);
}

PDFAppMcpSetup PDFAppAiPage::getSetup() const
{
    return PDFAppMcpClientSetup::create(m_setupClient, getShownAddress(), getShownToken());
}

void PDFAppAiPage::setSetupClient(PDFAppMcpSetupClient client)
{
    if (int(client) < int(PDFAppMcpSetupClient::ClaudeCode) || int(client) > int(PDFAppMcpSetupClient::Other) || client == m_setupClient)
    {
        return;
    }
    m_setupClient = client;
    updateSetup();
}

// ------------------------------------------------------------------ Parts (tests)

QList<PDFStatusBadge*> PDFAppAiPage::getStatusBadges() const
{
    QList<PDFStatusBadge*> badges;
    for (PDFStatusBadge* badge : { m_stateBadge.data(), m_remoteBadge.data() })
    {
        if (badge && !badge->isHidden())
        {
            badges.append(badge);
        }
    }
    return badges;
}

PDFAppSettingRow* PDFAppAiPage::findSettingRow(const QString& anchor) const
{
    for (const QPointer<PDFAppSettingRow>& row : m_settingRows)
    {
        if (row && row->getAnchor() == anchor)
        {
            return row;
        }
    }
    return nullptr;
}

QList<PDFAssistantRow*> PDFAppAiPage::getAssistantRows() const
{
    QList<PDFAssistantRow*> rows;
    for (const QPointer<PDFAssistantRow>& row : m_assistantRows)
    {
        if (row)
        {
            rows.append(row);
        }
    }
    return rows;
}

QList<PDFActivityRow*> PDFAppAiPage::getActivityRows() const
{
    QList<PDFActivityRow*> rows;
    for (const QPointer<PDFActivityRow>& row : m_activityRows)
    {
        if (row && !row->isHidden())
        {
            rows.append(row);
        }
    }
    return rows;
}

PDFAssistantTile* PDFAppAiPage::getTile(PDFAppMcpSetupClient client) const
{
    for (const QPointer<PDFAssistantTile>& tile : m_tiles)
    {
        if (tile && m_tileGroup && m_tileGroup->id(tile) == int(client))
        {
            return tile;
        }
    }
    return nullptr;
}

QList<QPushButton*> PDFAppAiPage::getVariantChips() const
{
    QList<QPushButton*> chips;
    for (const QPointer<QPushButton>& chip : m_variantChips)
    {
        if (chip)
        {
            chips.append(chip);
        }
    }
    return chips;
}

QList<PDFCodeBox*> PDFAppAiPage::getCodeBoxes() const
{
    QList<PDFCodeBox*> boxes;
    for (const QPointer<PDFCodeBox>& box : m_codeBoxes)
    {
        if (box)
        {
            boxes.append(box);
        }
    }
    return boxes;
}

// ------------------------------------------------------------------ Slots

void PDFAppAiPage::onStateChanged()
{
    updateServer();
    updateConnection();
    updateAssistants();
    updateActivity();
    updateSetup();
}

void PDFAppAiPage::onAssistantsChanged()
{
    updateAssistants();
    if (m_newTokenDialog)
    {
        QStringList names;
        for (const PDFMcpClientRow& client : m_shownClients)
        {
            names << PDFAppMcpClientNames::sanitizeName(client.name);
        }
        m_newTokenDialog->setClientNames(names);
    }
}

void PDFAppAiPage::onActivityChanged()
{
    updateActivity();
}

void PDFAppAiPage::onDatabaseChanged(const QString& topic, const QString& key)
{
    if (topic == PDFSettingsRepository::getTopic() && key.startsWith(QLatin1String("mcp.")))
    {
        for (const QPointer<PDFAppSettingRow>& row : std::as_const(m_settingRows))
        {
            if (row && row->getControl())
            {
                row->updateFromSettings(key);
            }
        }
        updateServer();
        updateConnection();
        updateSetup();
    }
}

void PDFAppAiPage::onEverythingChanged()
{
    m_hasShownClients = false;
    updateAll();
}

void PDFAppAiPage::onStartStopClicked()
{
    if (!m_controller)
    {
        return;
    }

    // The signals of the controller may delete the page (a navigation from them)
    QPointer<PDFAppAiPage> guard(this);
    if (m_controller->isRunning())
    {
        m_controller->stop();
    }
    else
    {
        m_controller->start();
    }
    if (guard)
    {
        updateAll();
    }
}

void PDFAppAiPage::onCopyAddressClicked()
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
    {
        clipboard->setText(getShownAddress());
    }
}

void PDFAppAiPage::onCalloutClicked()
{
    if (m_shownState == ShownState::OtherInstance)
    {
        if (m_controller)
        {
            m_controller->showOtherWindow();
        }
        return;
    }

    // Change Port: the port field gets the focus with its text selected
    QWidget* editor = m_portRow && m_portRow->getControl() ? m_portRow->getControl()->getEditor() : nullptr;
    if (PDFSpinBox* spinBox = qobject_cast<PDFSpinBox*>(editor))
    {
        scrollToWidget(m_portRow);
        spinBox->setFocus(Qt::OtherFocusReason);
        spinBox->selectAll();
    }
}

void PDFAppAiPage::onLocalClicked()
{
    PDFAppContext* context = getContext();
    if (context && context->getDatabase() && isRemoteAllowed())
    {
        context->getSettings().setValue(PDFSettingKeys::mcpAllowRemote(), false);
    }
}

void PDFAppAiPage::onRemoteClicked()
{
    PDFAppContext* context = getContext();
    if (context && context->getDatabase() && !isRemoteAllowed())
    {
        // Remote mode forces the token (generated, when missing)
        if (m_controller && m_controller->getToken().isEmpty())
        {
            context->getSettings().setValue(PDFSettingKeys::mcpToken(), PDFAppMcpController::generateToken());
        }
        context->getSettings().setValue(PDFSettingKeys::mcpAllowRemote(), true);
    }
}

void PDFAppAiPage::onCopyTokenClicked()
{
    const QString token = m_controller ? m_controller->getToken() : QString();
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard && !token.isEmpty())
    {
        clipboard->setText(token);
    }
}

void PDFAppAiPage::onNewTokenClicked()
{
    openNewTokenDialog();
}

bool PDFAppAiPage::openNewTokenDialog()
{
    if (m_newTokenDialog)
    {
        m_newTokenDialog->raise();
        return true;
    }

    PDFAppContext* context = getContext();
    if (!context || !context->getDatabase() || !m_controller)
    {
        return false;
    }

    PDFAppAiNewTokenDialog* dialog = new PDFAppAiNewTokenDialog(window());
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    PDFAppMainWindow::prepareDialog(dialog, this);
    QStringList names;
    for (const PDFMcpClientRow& client : m_shownClients)
    {
        names << PDFAppMcpClientNames::sanitizeName(client.name);
    }
    dialog->setClientNames(names);
    dialog->setTokenRequired(isTokenRequired());
    connect(dialog, &QDialog::finished, this, &PDFAppAiPage::onNewTokenFinished);
    m_newTokenDialog = dialog;
    dialog->open();
    return true;
}

void PDFAppAiPage::onNewTokenFinished(int result)
{
    m_newTokenDialog = nullptr;
    if (result != QDialog::Accepted || !m_controller)
    {
        return;
    }

    // A new token disconnects every assistant and restarts a running server [D2]
    QPointer<PDFAppAiPage> guard(this);
    m_controller->createNewToken();
    if (guard)
    {
        updateAll();
    }
}

void PDFAppAiPage::onDisconnectClicked()
{
    PDFAssistantRow* row = qobject_cast<PDFAssistantRow*>(sender());
    if (!row || !m_controller)
    {
        return;
    }

    // The row goes later (deleted by the update of the list from the event loop)
    const QString key = row->property("assistantKey").toString();
    m_controller->disconnectAssistant(key);
}

void PDFAppAiPage::onDisconnectAllClicked()
{
    if (m_controller)
    {
        m_controller->disconnectAll();
    }
}

void PDFAppAiPage::onActivityClicked()
{
    PDFActivityRow* row = qobject_cast<PDFActivityRow*>(sender());
    if (!row || !row->isClickable() || !m_controller)
    {
        return;
    }

    // Show reopens the dialog of the request (it switches to the tab of the document)
    const quint64 confirmationId = row->property("confirmationId").toULongLong();
    PDFAppMcpConfirmationUi* ui = PDFAppMcpConfirmationUi::find(m_controller->getHost());
    if (ui && confirmationId != 0)
    {
        ui->showDialog(confirmationId);
    }
}

void PDFAppAiPage::onClearClicked()
{
    // Clear keeps the waiting rows (also those of another window)
    PDFAppContext* context = getContext();
    if (context && context->getDatabase())
    {
        PDFMcpRepository(context->getDatabase()).clearActivity();
    }
}

void PDFAppAiPage::onTileToggled(int id, bool isChecked)
{
    if (!isChecked)
    {
        return;
    }

    // The tile Claude keeps its chosen variant
    const PDFAppMcpSetupClient client = PDFAppMcpSetupClient(id);
    if (client == PDFAppMcpSetupClient::ClaudeCode && m_setupClient == PDFAppMcpSetupClient::ClaudeDesktop)
    {
        return;
    }
    setSetupClient(client);
}

void PDFAppAiPage::onVariantToggled(int id, bool isChecked)
{
    if (isChecked)
    {
        // The card is built again from the event loop: the chip is emitting its signal
        m_setupClient = PDFAppMcpSetupClient(id);
        QMetaObject::invokeMethod(this, &PDFAppAiPage::updateSetup, Qt::QueuedConnection);
    }
}

void PDFAppAiPage::onCopySetupClicked()
{
    const PDFAppMcpSetup setup = getSetup();
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard && !setup.copyText.isEmpty())
    {
        clipboard->setText(setup.copyText);
    }
}

PDFAppPage* PDFAppAiPageFactory::createPage(PDFAppContext* context, QWidget* parent) const
{
    return new PDFAppAiPage(context, parent);
}

}   // namespace pdf
