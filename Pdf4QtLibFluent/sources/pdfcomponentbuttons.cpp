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

#include "pdfcomponentbuttons.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfwidgetstyle.h"
#include "pdfwidgetstyleproperties.h"

#include <QEvent>
#include <QPainter>
#include <QtMath>

namespace pdf
{

PDFIconButton::PDFIconButton(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFIconButton::PDFIconButton(const QString& iconName, Size size, QWidget* parent) :
    BaseClass(parent),
    m_iconName(iconName),
    m_size(size)
{
    init();
}

void PDFIconButton::init()
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // The focus ring of the style lies outside of the shape of this radius
    setProperty(PDFWidgetStyleProperties::RADIUS, getRadius(m_size));
}

void PDFIconButton::setIconName(const QString& iconName)
{
    if (m_iconName != iconName)
    {
        m_iconName = iconName;
        update();
    }
}

void PDFIconButton::setButtonSize(Size size)
{
    if (m_size != size)
    {
        m_size = size;
        setProperty(PDFWidgetStyleProperties::RADIUS, getRadius(m_size));
        updateGeometry();
        update();
    }
}

void PDFIconButton::setIconColor(PDFThemeColors::Color color)
{
    m_iconColor = color;
    update();
}

void PDFIconButton::resetIconColor()
{
    m_iconColor.reset();
    update();
}

PDFThemeColors::Color PDFIconButton::getCurrentIconColor() const
{
    if (!isEnabled())
    {
        return PDFThemeColors::IconDisabled;
    }

    if (m_iconColor.has_value())
    {
        return *m_iconColor;
    }

    if (isDown())
    {
        return PDFThemeColors::IconStrong;
    }

    if (underMouse())
    {
        return PDFThemeColors::IconDefault;
    }

    return PDFThemeColors::IconMuted;
}

QSize PDFIconButton::sizeHint() const
{
    const int extent = getExtent(m_size);
    return QSize(extent, extent);
}

QSize PDFIconButton::minimumSizeHint() const
{
    return sizeHint();
}

int PDFIconButton::getExtent(Size size)
{
    switch (size)
    {
        case Size::Small:
            return 24;
        case Size::Medium:
            return 28;
        case Size::TabClose:
            return 20;
    }

    return 24;
}

int PDFIconButton::getIconExtent(Size size)
{
    switch (size)
    {
        case Size::Small:
            return 14;
        case Size::Medium:
            return 16;
        case Size::TabClose:
            return 12;
    }

    return 14;
}

qreal PDFIconButton::getRadius(Size size)
{
    return size == Size::TabClose ? 4.0 : 6.0;
}

void PDFIconButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal extent = getExtent(m_size);
    const QRectF shape = PDFStylePainter::centered(QRectF(rect()), QSizeF(extent, extent));
    const PDFStylePainter::Radii radii(getRadius(m_size));

    // Hover and press add a background, the focus ring is drawn by the style outside
    if (isEnabled() && isDown())
    {
        PDFStylePainter::fill(&painter, shape, radii, PDFThemeColors::BgPressed);
    }
    else if (isEnabled() && underMouse())
    {
        PDFStylePainter::fill(&painter, shape, radii, PDFThemeColors::BgHover);
    }

    const qreal iconExtent = getIconExtent(m_size);
    PDFStylePainter::icon(&painter, PDFStylePainter::centered(shape, QSizeF(iconExtent, iconExtent)), m_iconName, getCurrentIconColor());
}

/// Geometry of the stamp chip
class PDFStampChipHelper
{
public:
    PDFStampChipHelper() = delete;

    static constexpr int HEIGHT = 26;
    static constexpr int PADDING = 11;      ///< Border 1 and padding 10
    static constexpr qreal RADIUS = 6.0;
};

PDFStampChip::PDFStampChip(QWidget* parent) :
    BaseClass(parent)
{
    init();
}

PDFStampChip::PDFStampChip(const QString& text, PDFAccentHue hue, QWidget* parent) :
    BaseClass(parent),
    m_hue(hue)
{
    setText(text);
    init();
}

void PDFStampChip::init()
{
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setProperty(PDFWidgetStyleProperties::RADIUS, PDFStampChipHelper::RADIUS);
}

void PDFStampChip::setHue(PDFAccentHue hue)
{
    if (m_hue != hue)
    {
        m_hue = hue;
        update();
    }
}

QFont PDFStampChip::getLabelFont()
{
    QFont font = PDFThemeFonts::getFont(PDFThemeFonts::Role::CaptionSemiBold);
    font.setCapitalization(QFont::AllUppercase);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
    return font;
}

QSize PDFStampChip::sizeHint() const
{
    const qreal textWidth = PDFStylePainter::textWidth(text().toUpper(), getLabelFont());
    return QSize(qRound(textWidth) + 2 * PDFStampChipHelper::PADDING, PDFStampChipHelper::HEIGHT);
}

QSize PDFStampChip::minimumSizeHint() const
{
    return sizeHint();
}

void PDFStampChip::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);

    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
    {
        updateGeometry();
    }
}

void PDFStampChip::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    using Role = PDFAccentHueColors::Role;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF shape(rect());
    const PDFStylePainter::Radii radii(PDFStampChipHelper::RADIUS);

    Role fill = Role::Background;
    if (isEnabled() && isDown())
    {
        fill = Role::BackgroundPressed;
    }
    else if (isEnabled() && underMouse())
    {
        fill = Role::BackgroundHover;
    }

    PDFStylePainter::fill(&painter, shape, radii, PDFAccentHueColors::getColor(m_hue, fill));

    // The focus ring outside of the shape replaces the border of the hue
    if (!PDFWidgetStyle::isFocusRingVisible(this))
    {
        PDFStylePainter::strokeInside(&painter, shape, radii, PDFAccentHueColors::getColor(m_hue, Role::BorderStrong), 1.0);
    }

    const PDFThemeColors::Color textColor = isEnabled() ? PDFAccentHueColors::getColor(m_hue, Role::Icon) : PDFThemeColors::TextDisabled;
    const QRectF textRect = shape.adjusted(PDFStampChipHelper::PADDING, 0.0, -PDFStampChipHelper::PADDING, 0.0);
    PDFStylePainter::text(&painter, textRect, Qt::AlignCenter | Qt::TextSingleLine, text(), getLabelFont(), PDFStylePainter::color(textColor));
}

}   // namespace pdf
