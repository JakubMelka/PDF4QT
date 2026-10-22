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

#include "pdfappdocumentinfobar.h"
#include "pdfappdocumenttoolbar.h"

#include "pdfaccenthue.h"
#include "pdfcomponentbadges.h"
#include "pdfcomponentbuttons.h"
#include "pdffluentwidgets.h"
#include "pdfstylepainter.h"
#include "pdfthememanager.h"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace pdf
{

/// Values of the notification bars of the design
class PDFAppInfoBarHelper
{
public:
    PDFAppInfoBarHelper() = delete;

    /// Property of a button of an action: the id of the action
    static constexpr const char* ACTION_PROPERTY = "pdf4qtInfoBarAction";

    /// Hue of the icon circle of the tone
    static PDFAccentHue getHue(PDFAppInfoBarTone tone)
    {
        switch (tone)
        {
            case PDFAppInfoBarTone::Information:
            case PDFAppInfoBarTone::Progress:
                return PDFAccentHue::Neutral;
            case PDFAppInfoBarTone::Warning:
                return PDFAccentHue::Amber;
            case PDFAppInfoBarTone::Error:
                return PDFAccentHue::Red;
        }
        return PDFAccentHue::Neutral;
    }
};

QMargins PDFAppInfoBar::getShadowMargins()
{
    // Shadow 0 / 4 / 16 (blur 16, offset 4 down)
    return QMargins(16, 12, 16, 20);
}

QString PDFAppInfoBar::getToneIconName(PDFAppInfoBarTone tone)
{
    switch (tone)
    {
        case PDFAppInfoBarTone::Information:
            return QStringLiteral("info");
        case PDFAppInfoBarTone::Warning:
            return QStringLiteral("alert-triangle");
        case PDFAppInfoBarTone::Error:
            return QStringLiteral("ban");
        case PDFAppInfoBarTone::Progress:
            return QStringLiteral("hourglass");
    }
    return QStringLiteral("info");
}

PDFAppInfoBar::PDFAppInfoBar(const PDFAppInfoBarRequest& request, QWidget* parent) :
    BaseClass(parent),
    m_request(request)
{
    setObjectName(QStringLiteral("infoBar:") + request.id);
    setFixedHeight(CARD_HEIGHT + getShadowMargins().top() + getShadowMargins().bottom());

    // blocked-notification 1028:24277: padding 5 / 10 / 5 / 12, gap 8
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(getShadowMargins() + QMargins(13, 6, 11, 6));
    m_layout->setSpacing(8);

    m_badge = new PDFIconBadge(this);
    m_badge->setExtent(24);
    m_badge->setIconExtent(14);
    m_layout->addWidget(m_badge);

    QWidget* texts = new QWidget(this);
    texts->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    QVBoxLayout* textsLayout = new QVBoxLayout(texts);
    textsLayout->setContentsMargins(0, 0, 0, 0);
    textsLayout->setSpacing(1);
    m_title = new PDFLabel(QString(), PDFThemeFonts::Role::BodySemiBold, PDFThemeColors::TextPrimary, texts);
    m_title->setObjectName(QStringLiteral("infoBarTitle"));
    m_title->setTextFormat(Qt::PlainText);
    m_title->setMinimumWidth(1);
    textsLayout->addWidget(m_title);
    m_text = new PDFLabel(QString(), PDFThemeFonts::Role::CaptionRegular, PDFThemeColors::TextMuted, texts);
    m_text->setObjectName(QStringLiteral("infoBarText"));
    m_text->setTextFormat(Qt::PlainText);
    m_text->setMinimumWidth(1);
    textsLayout->addWidget(m_text);
    m_layout->addWidget(texts, 1);

    // print-progress 1030:25657: the progress line 160 x 4
    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("infoBarProgress"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedWidth(PROGRESS_WIDTH);
    m_layout->addWidget(m_progressBar);

    m_closeButton = new PDFIconButton(QStringLiteral("x"), PDFIconButton::Size::TabClose, this);
    m_closeButton->setObjectName(QStringLiteral("infoBarClose"));
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setAccessibleName(tr("Close"));
    connect(m_closeButton.data(), &QAbstractButton::clicked, this, &PDFAppInfoBar::onCloseClicked);

    rebuild();
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&PDFAppInfoBar::update));
}

PDFAppInfoBar::~PDFAppInfoBar() = default;

void PDFAppInfoBar::setRequest(const PDFAppInfoBarRequest& request)
{
    if (m_request == request)
    {
        return;
    }

    m_request = request;
    rebuild();
}

void PDFAppInfoBar::rebuild()
{
    if (!m_layout)
    {
        return;
    }

    // The buttons and the dividers of the previous request go; a button may be the
    // sender of the signal, whose slot has replaced the request (deleteLater)
    for (const QPointer<QWidget>& widget : m_dynamicWidgets)
    {
        if (widget)
        {
            m_layout->removeWidget(widget);
            widget->hide();
            widget->deleteLater();
        }
    }
    m_dynamicWidgets.clear();
    m_actionButtons.clear();

    if (m_badge)
    {
        m_badge->setIconName(m_request.iconName.isEmpty() ? getToneIconName(m_request.tone) : m_request.iconName);
        m_badge->setHue(PDFAppInfoBarHelper::getHue(m_request.tone));
    }
    if (m_title)
    {
        m_title->setText(m_request.title);
        m_title->setToolTip(m_request.title);
    }
    if (m_text)
    {
        m_text->setText(m_request.text);
        m_text->setVisible(!m_request.text.isEmpty());
    }
    if (m_progressBar)
    {
        const bool hasProgress = m_request.tone == PDFAppInfoBarTone::Progress && m_request.progress >= 0;
        m_progressBar->setValue(qBound(0, m_request.progress, 100));
        m_progressBar->setVisible(hasProgress);
    }

    // divider, the buttons (Secondary), divider, the close button
    if (!m_request.actions.empty())
    {
        PDFAppToolbarDivider* divider = new PDFAppToolbarDivider(Qt::Vertical, 18, 0, PDFThemeColors::BorderSeparator, this);
        m_layout->addWidget(divider);
        m_dynamicWidgets.emplace_back(divider);

        for (const PDFAppInfoBarAction& action : m_request.actions)
        {
            QPushButton* button = new QPushButton(action.text, this);
            button->setObjectName(QStringLiteral("infoBarAction:") + action.id);
            button->setAutoDefault(false);
            button->setProperty(PDFAppInfoBarHelper::ACTION_PROPERTY, action.id);
            connect(button, &QPushButton::clicked, this, &PDFAppInfoBar::onActionClicked);
            m_layout->addWidget(button);
            m_dynamicWidgets.emplace_back(button);
            m_actionButtons.emplace_back(button);
        }
    }

    if (m_closeButton)
    {
        m_layout->removeWidget(m_closeButton);
        if (m_request.isClosable)
        {
            PDFAppToolbarDivider* divider = new PDFAppToolbarDivider(Qt::Vertical, 18, 0, PDFThemeColors::BorderSeparator, this);
            m_layout->addWidget(divider);
            m_dynamicWidgets.emplace_back(divider);
            m_layout->addWidget(m_closeButton);
        }
        m_closeButton->setVisible(m_request.isClosable);
    }

    setAccessibleName(m_request.title);
    setAccessibleDescription(m_request.text);
    updateGeometry();
    update();
}

QRect PDFAppInfoBar::getCardRect() const
{
    return rect().marginsRemoved(getShadowMargins());
}

std::vector<QPushButton*> PDFAppInfoBar::getActionButtons() const
{
    std::vector<QPushButton*> buttons;
    for (const QPointer<QPushButton>& button : m_actionButtons)
    {
        if (button)
        {
            buttons.push_back(button.data());
        }
    }
    return buttons;
}

PDFIconButton* PDFAppInfoBar::getCloseButton() const
{
    return m_request.isClosable ? m_closeButton.data() : nullptr;
}

QProgressBar* PDFAppInfoBar::getProgressBar() const
{
    return (m_progressBar && !m_progressBar->isHidden()) ? m_progressBar.data() : nullptr;
}

void PDFAppInfoBar::onActionClicked()
{
    // The buttons do not identify themselves in the signal
    QPushButton* button = qobject_cast<QPushButton*>(sender());
    if (!button)
    {
        return;
    }

    // The owner may hide the bar or delete the tab: the signal is sent from the
    // event loop, not from the signal of the button
    const QString actionId = button->property(PDFAppInfoBarHelper::ACTION_PROPERTY).toString();
    QMetaObject::invokeMethod(this, &PDFAppInfoBar::onActionQueued, Qt::QueuedConnection, actionId);
}

void PDFAppInfoBar::onActionQueued(const QString& actionId)
{
    Q_EMIT actionTriggered(m_request.id, actionId);
}

void PDFAppInfoBar::onCloseClicked()
{
    QMetaObject::invokeMethod(this, &PDFAppInfoBar::onCloseQueued, Qt::QueuedConnection);
}

void PDFAppInfoBar::onCloseQueued()
{
    Q_EMIT closeRequested(m_request.id);
}

void PDFAppInfoBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // The card bg/surface, border border/default, radius 14, shadow 0 / 4 / 16
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF card(getCardRect());
    const PDFStylePainter::Radii radii(CARD_RADIUS);
    PDFStylePainter::shadow(&painter, card, radii, QPointF(0.0, 4.0), 16.0, PDFThemeManager::getInstance()->getColor(PDFThemeColors::EffectShadow));
    PDFStylePainter::fill(&painter, card, radii, PDFThemeColors::BgSurface);
    PDFStylePainter::strokeInside(&painter, card, radii, PDFThemeColors::BorderDefault, 1.0);
}

void PDFAppInfoBar::mousePressEvent(QMouseEvent* event)
{
    // The margins of the shadow are not a part of the bar for the pointer
    if (!getCardRect().contains(event->position().toPoint()))
    {
        event->ignore();
        return;
    }

    BaseClass::mousePressEvent(event);
}

}   // namespace pdf
