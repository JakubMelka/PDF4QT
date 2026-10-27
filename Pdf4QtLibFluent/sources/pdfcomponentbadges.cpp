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

#include "pdfcomponentbadges.h"
#include "pdfthememanager.h"

#include <QEvent>
#include <QPainter>

namespace pdf
{

/// Common code of the painted widgets of this file
class PDFComponentBadgesHelper
{
public:
    PDFComponentBadgesHelper() = delete;

    /// Returns the state of a painted widget (enabled, direction)
    static PDFComponentPainter::State state(const QWidget* widget)
    {
        return PDFComponentPainter::State(widget->isEnabled() ? QStyle::State_Enabled : QStyle::State_None, widget->layoutDirection());
    }

    /// Returns true, if the change of the widget changes its picture
    static bool isRepaintChange(const QEvent* event)
    {
        switch (event->type())
        {
            case QEvent::EnabledChange:
            case QEvent::LayoutDirectionChange:
            case QEvent::FontChange:
            case QEvent::StyleChange:
                return true;

            default:
                break;
        }
        return false;
    }
};

PDFIconBadge::PDFIconBadge(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFIconBadge::PDFIconBadge(const QString& iconName, int extent, int iconExtent, QWidget* parent) :
    BaseClass(parent)
{
    m_badge.iconName = iconName;
    m_badge.extent = qMax(0, extent);
    m_badge.iconExtent = qMax(0, iconExtent);
    init();
}

void PDFIconBadge::init()
{
    // A picture: the mouse goes to the widget below it
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

void PDFIconBadge::setIconName(const QString& iconName)
{
    if (m_badge.iconName != iconName)
    {
        m_badge.iconName = iconName;
        update();
    }
}

void PDFIconBadge::setExtent(int extent)
{
    extent = qMax(0, extent);
    if (m_badge.extent != extent)
    {
        m_badge.extent = extent;
        updateGeometry();
        update();
    }
}

void PDFIconBadge::setIconExtent(int extent)
{
    extent = qMax(0, extent);
    if (m_badge.iconExtent != extent)
    {
        m_badge.iconExtent = extent;
        update();
    }
}

void PDFIconBadge::setRadius(qreal radius)
{
    if (!qIsFinite(radius))
    {
        radius = -1.0;
    }

    if (qAbs(m_badge.radius - radius) > 1e-9)
    {
        m_badge.radius = radius;
        update();
    }
}

void PDFIconBadge::setColors(std::optional<Color> fill, Color iconColor, std::optional<Color> border)
{
    m_badge.fill = fill;
    m_badge.iconColor = iconColor;
    m_badge.border = border;
    update();
}

void PDFIconBadge::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    setColors(PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Background), PDFAccentHueColors::getColor(hue, PDFAccentHueColors::Role::Icon));
}

void PDFIconBadge::setLogo(bool isLogo)
{
    if (m_badge.logo != isLogo)
    {
        m_badge.logo = isLogo;
        update();
    }
}

QSize PDFIconBadge::sizeHint() const
{
    return PDFComponentPainter::getIconBadgeSize(m_badge);
}

QSize PDFIconBadge::minimumSizeHint() const
{
    return sizeHint();
}

void PDFIconBadge::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintIconBadge(&painter, QRectF(rect()), m_badge, PDFComponentBadgesHelper::state(this));
}

void PDFIconBadge::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (PDFComponentBadgesHelper::isRepaintChange(event))
    {
        update();
    }
}

PDFStatusBadge::PDFStatusBadge(QWidget* parent) :
    BaseClass(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

PDFStatusBadge::PDFStatusBadge(const QString& text, const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    PDFStatusBadge(parent)
{
    setBadge(text, iconName, hue);
}

void PDFStatusBadge::setBadge(const QString& text, const QString& iconName, PDFAccentHue hue)
{
    const QString previousText = m_badge.text;
    m_badge.text = text;
    m_badge.iconName = iconName;
    m_badge.hue = PDFComponentPainter::normalized(hue);

    // The name follows the text, unless the application has set its own
    if (accessibleName().isEmpty() || accessibleName() == previousText)
    {
        setAccessibleName(text);
    }

    updateGeometry();
    update();
}

void PDFStatusBadge::setDotVisible(bool isVisible)
{
    if (m_badge.dot != isVisible)
    {
        m_badge.dot = isVisible;
        updateGeometry();
        update();
    }
}

void PDFStatusBadge::setTextColor(std::optional<PDFThemeColors::Color> color)
{
    // An invalid token would be drawn in the leak color
    if (color.has_value() && (static_cast<int>(*color) < 0 || static_cast<int>(*color) >= static_cast<int>(PDFThemeColors::ColorCount)))
    {
        color = std::nullopt;
    }

    if (m_badge.textColor != color)
    {
        m_badge.textColor = color;
        update();
    }
}

QSize PDFStatusBadge::sizeHint() const
{
    return PDFComponentPainter::getStatusBadgeSize(m_badge);
}

QSize PDFStatusBadge::minimumSizeHint() const
{
    return sizeHint();
}

void PDFStatusBadge::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintStatusBadge(&painter, QRectF(rect()), m_badge, PDFComponentBadgesHelper::state(this));
}

void PDFStatusBadge::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (PDFComponentBadgesHelper::isRepaintChange(event))
    {
        updateGeometry();
        update();
    }
}

PDFFactPill::PDFFactPill(QWidget* parent) :
    BaseClass(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

PDFFactPill::PDFFactPill(const QString& iconName, const QString& text, QWidget* parent) :
    PDFFactPill(parent)
{
    m_pill.iconName = iconName;
    setText(text);
}

void PDFFactPill::setText(const QString& text)
{
    if (m_pill.text == text && !text.isEmpty())
    {
        return;
    }

    // The name follows the text, unless the application has set its own
    if (accessibleName().isEmpty() || accessibleName() == m_pill.text)
    {
        setAccessibleName(text);
    }

    m_pill.text = text;
    updateGeometry();
    update();
}

void PDFFactPill::setIconName(const QString& iconName)
{
    if (m_pill.iconName != iconName)
    {
        m_pill.iconName = iconName;
        update();
    }
}

QSize PDFFactPill::sizeHint() const
{
    return PDFComponentPainter::getFactPillSize(m_pill);
}

QSize PDFFactPill::minimumSizeHint() const
{
    return sizeHint();
}

void PDFFactPill::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintFactPill(&painter, QRectF(rect()), m_pill, PDFComponentBadgesHelper::state(this));
}

void PDFFactPill::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (PDFComponentBadgesHelper::isRepaintChange(event))
    {
        updateGeometry();
        update();
    }
}

PDFAvatar::PDFAvatar(QWidget* parent) :
    BaseClass(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

PDFAvatar::PDFAvatar(const QString& initials, int extent, int fontPixelSize, QWidget* parent) :
    PDFAvatar(parent)
{
    m_avatar.initials = initials;
    m_avatar.extent = qMax(0, extent);
    m_avatar.fontPixelSize = qMax(1, fontPixelSize);
}

void PDFAvatar::setInitials(const QString& initials)
{
    if (m_avatar.initials != initials)
    {
        m_avatar.initials = initials;
        update();
    }
}

void PDFAvatar::setExtent(int extent)
{
    extent = qMax(0, extent);
    if (m_avatar.extent != extent)
    {
        m_avatar.extent = extent;
        updateGeometry();
        update();
    }
}

void PDFAvatar::setFontPixelSize(int pixelSize)
{
    pixelSize = qMax(1, pixelSize);
    if (m_avatar.fontPixelSize != pixelSize)
    {
        m_avatar.fontPixelSize = pixelSize;
        update();
    }
}

void PDFAvatar::setHue(PDFAccentHue hue)
{
    hue = PDFComponentPainter::normalized(hue);
    if (m_avatar.hue != hue)
    {
        m_avatar.hue = hue;
        update();
    }
}

QSize PDFAvatar::sizeHint() const
{
    return PDFComponentPainter::getAvatarSize(m_avatar);
}

QSize PDFAvatar::minimumSizeHint() const
{
    return sizeHint();
}

void PDFAvatar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintAvatar(&painter, QRectF(rect()), m_avatar, PDFComponentBadgesHelper::state(this));
}

void PDFAvatar::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (PDFComponentBadgesHelper::isRepaintChange(event))
    {
        update();
    }
}

PDFStatusRow::PDFStatusRow(QWidget* parent) :
    BaseClass(parent)
{
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

PDFStatusRow::PDFStatusRow(const QString& title, const QString& subtitle, const QString& status, bool isActive, QWidget* parent) :
    PDFStatusRow(parent)
{
    m_row.title = title;
    m_row.subtitle = subtitle;
    m_row.status = status;
    m_row.active = isActive;
    updateAccessibility();
}

void PDFStatusRow::setTitle(const QString& title)
{
    if (m_row.title != title)
    {
        m_row.title = title;
        updateAccessibility();
        updateGeometry();
        update();
    }
}

void PDFStatusRow::setSubtitle(const QString& subtitle)
{
    if (m_row.subtitle != subtitle)
    {
        m_row.subtitle = subtitle;
        updateAccessibility();
        updateGeometry();
        update();
    }
}

void PDFStatusRow::setStatus(const QString& status)
{
    if (m_row.status != status)
    {
        m_row.status = status;
        updateAccessibility();
        updateGeometry();
        update();
    }
}

void PDFStatusRow::setActive(bool isActive)
{
    if (m_row.active != isActive)
    {
        m_row.active = isActive;
        update();
    }
}

void PDFStatusRow::updateAccessibility()
{
    // "Title" with the description "Subtitle, Status"; the texts set by the application are kept
    QStringList parts;
    for (const QString& part : { m_row.subtitle, m_row.status })
    {
        if (!part.isEmpty())
        {
            parts << part;
        }
    }
    const QString description = parts.join(tr(", "));

    if (accessibleName().isEmpty() || accessibleName() == m_name)
    {
        setAccessibleName(m_row.title);
    }
    if (accessibleDescription().isEmpty() || accessibleDescription() == m_description)
    {
        setAccessibleDescription(description);
    }

    m_name = m_row.title;
    m_description = description;
}

QSize PDFStatusRow::sizeHint() const
{
    return PDFComponentPainter::getStatusRowSize(m_row);
}

QSize PDFStatusRow::minimumSizeHint() const
{
    return PDFComponentPainter::getStatusRowMinimumSize(m_row);
}

void PDFStatusRow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintStatusRow(&painter, QRectF(rect()), m_row, PDFComponentBadgesHelper::state(this));
}

void PDFStatusRow::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (PDFComponentBadgesHelper::isRepaintChange(event))
    {
        updateGeometry();
        update();
    }
}

PDFTintedCard::PDFTintedCard(QWidget* parent) :
    BaseClass(parent)
{
    setFrameShape(QFrame::NoFrame);
    connect(PDFThemeManager::getInstance(), &PDFThemeManager::themeChanged, this, qOverload<>(&QWidget::update));
}

PDFTintedCard::PDFTintedCard(std::optional<PDFAccentHue> hue, QWidget* parent) :
    PDFTintedCard(parent)
{
    setHue(hue);
}

void PDFTintedCard::setHue(std::optional<PDFAccentHue> hue)
{
    if (hue.has_value())
    {
        hue = PDFComponentPainter::normalized(*hue);
    }

    if (m_hue != hue)
    {
        m_hue = hue;
        update();
    }
}

void PDFTintedCard::setRadius(qreal radius)
{
    // NaN fails the comparison: 0
    const qreal validRadius = radius > 0.0 ? radius : 0.0;
    if (!qFuzzyCompare(m_radius + 1.0, validRadius + 1.0))
    {
        m_radius = validRadius;
        update();
    }
}

void PDFTintedCard::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    PDFComponentPainter::paintTintedCard(&painter, QRectF(rect()), m_hue, m_radius);
}

}   // namespace pdf
