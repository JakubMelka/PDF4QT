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

#include "pdfwidgetstyle.h"
#include "pdfstylepainterguard.h"
#include "pdfwidgetstyleimpl.h"
#include "pdfthememanager.h"
#include "pdfthemeiconengine.h"

#include <QApplication>
#include <QPainter>
#include <QStyleOption>
#include <QWidget>

namespace pdf
{

PDFWidgetStyle::PDFWidgetStyle() :
    BaseClass(),
    m_impl(new PDFWidgetStyleImpl(this))
{
    setObjectName(QStringLiteral("pdf4qtfluent"));
}

PDFWidgetStyle::~PDFWidgetStyle()
{
    // The implementation is deleted first (it has no parent), while the style is whole
    m_impl.reset();
}

void PDFWidgetStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (!painter || !option)
    {
        return;
    }

    for (const auto& area : m_impl->getAreas())
    {
        if (area->drawPrimitive(element, option, painter, widget))
        {
            return;
        }
    }

    // Nothing is drawn by the base class: its look does not belong to the design
}

void PDFWidgetStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (!painter || !option)
    {
        return;
    }

    for (const auto& area : m_impl->getAreas())
    {
        if (area->drawControl(element, option, painter, widget))
        {
            return;
        }
    }
}

void PDFWidgetStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    if (!painter || !option)
    {
        return;
    }

    for (const auto& area : m_impl->getAreas())
    {
        if (area->drawComplexControl(control, option, painter, widget))
        {
            return;
        }
    }
}

QRect PDFWidgetStyle::subElementRect(SubElement element, const QStyleOption* option, const QWidget* widget) const
{
    if (!option)
    {
        return QRect();
    }

    QRect rect;
    for (const auto& area : m_impl->getAreas())
    {
        if (area->subElementRect(element, option, widget, rect))
        {
            return rect;
        }
    }

    return BaseClass::subElementRect(element, option, widget);
}

QRect PDFWidgetStyle::subControlRect(ComplexControl control, const QStyleOptionComplex* option, SubControl subControl, const QWidget* widget) const
{
    if (!option)
    {
        return QRect();
    }

    QRect rect;
    for (const auto& area : m_impl->getAreas())
    {
        if (area->subControlRect(control, option, subControl, widget, rect))
        {
            return rect;
        }
    }

    return BaseClass::subControlRect(control, option, subControl, widget);
}

QStyle::SubControl PDFWidgetStyle::hitTestComplexControl(ComplexControl control, const QStyleOptionComplex* option, const QPoint& point, const QWidget* widget) const
{
    if (!option)
    {
        return SC_None;
    }

    // The base class tests the rectangles returned by subControlRect of this style
    SubControl subControl = BaseClass::hitTestComplexControl(control, option, point, widget);

    // The targets of the pointer are larger than the drawn glyphs: the strip from
    // the stepper (the arrow, the icon of the calendar) to the edge of the field
    // belongs to it in the whole height of the field
    const bool isRightToLeft = option->direction == Qt::RightToLeft;

    if (control == CC_SpinBox && subControl != SC_SpinBoxUp && subControl != SC_SpinBoxDown && option->rect.contains(point))
    {
        const QRect up = proxy()->subControlRect(CC_SpinBox, option, SC_SpinBoxUp, widget);
        const QRect down = proxy()->subControlRect(CC_SpinBox, option, SC_SpinBoxDown, widget);

        if (up.isValid() && down.isValid() && (isRightToLeft ? point.x() <= up.right() : point.x() >= up.left()))
        {
            subControl = point.y() < down.top() ? SC_SpinBoxUp : SC_SpinBoxDown;
        }
    }
    else if (control == CC_ComboBox && subControl != SC_ComboBoxArrow && option->rect.contains(point))
    {
        const QRect arrow = proxy()->subControlRect(CC_ComboBox, option, SC_ComboBoxArrow, widget);

        if (arrow.isValid() && (isRightToLeft ? point.x() <= arrow.right() : point.x() >= arrow.left()))
        {
            subControl = SC_ComboBoxArrow;
        }
    }

    return subControl;
}

QSize PDFWidgetStyle::sizeFromContents(ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget) const
{
    QSize size;
    for (const auto& area : m_impl->getAreas())
    {
        if (area->sizeFromContents(type, option, contentsSize, widget, size))
        {
            return size;
        }
    }

    return BaseClass::sizeFromContents(type, option, contentsSize, widget);
}

int PDFWidgetStyle::pixelMetric(PixelMetric metric, const QStyleOption* option, const QWidget* widget) const
{
    int value = 0;
    if (PDFWidgetStyleMetrics::pixelMetric(m_impl.get(), metric, option, widget, value))
    {
        return value;
    }

    return BaseClass::pixelMetric(metric, option, widget);
}

int PDFWidgetStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const
{
    int value = 0;
    if (PDFWidgetStyleMetrics::styleHint(m_impl.get(), hint, option, widget, returnData, value))
    {
        return value;
    }

    return BaseClass::styleHint(hint, option, widget, returnData);
}

QIcon PDFWidgetStyle::standardIcon(StandardPixmap standardIcon, const QStyleOption* option, const QWidget* widget) const
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    return PDFWidgetStyleMetrics::standardIcon(standardIcon);
}

QPixmap PDFWidgetStyle::standardPixmap(StandardPixmap standardPixmap, const QStyleOption* option, const QWidget* widget) const
{
    const QIcon icon = standardIcon(standardPixmap, option, widget);

    if (icon.isNull())
    {
        return QPixmap();
    }

    const bool isMessageBoxIcon = standardPixmap == SP_MessageBoxInformation || standardPixmap == SP_MessageBoxWarning ||
                                  standardPixmap == SP_MessageBoxCritical || standardPixmap == SP_MessageBoxQuestion;
    const int extent = pixelMetric(isMessageBoxIcon ? PM_MessageBoxIconSize : PM_SmallIconSize, option, widget);
    const qreal devicePixelRatio = widget ? widget->devicePixelRatioF() : (qApp ? qApp->devicePixelRatio() : 1.0);
    return icon.pixmap(QSize(extent, extent), devicePixelRatio);
}

QPixmap PDFWidgetStyle::generatedIconPixmap(QIcon::Mode iconMode, const QPixmap& pixmap, const QStyleOption* option) const
{
    Q_UNUSED(option);

    if (iconMode != QIcon::Disabled || pixmap.isNull())
    {
        return pixmap;
    }

    // Icons, which do not come from the design, are faded, when they are disabled
    QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);

    for (int y = 0; y < image.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));

        for (int x = 0; x < image.width(); ++x)
        {
            const int gray = qGray(line[x]);
            line[x] = qRgba(gray, gray, gray, qAlpha(line[x]) * 2 / 5);
        }
    }

    QPixmap resultPixmap = QPixmap::fromImage(std::move(image));
    resultPixmap.setDevicePixelRatio(pixmap.devicePixelRatio());
    return resultPixmap;
}

QPalette PDFWidgetStyle::standardPalette() const
{
    return PDFThemeColors::createLeakPalette();
}

int PDFWidgetStyle::layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2, Qt::Orientation orientation, const QStyleOption* option, const QWidget* widget) const
{
    Q_UNUSED(control1);
    Q_UNUSED(control2);

    return pixelMetric(orientation == Qt::Horizontal ? PM_LayoutHorizontalSpacing : PM_LayoutVerticalSpacing, option, widget);
}

void PDFWidgetStyle::drawItemText(QPainter* painter, const QRect& rect, int flags, const QPalette& palette, bool enabled, const QString& text, QPalette::ColorRole textRole) const
{
    Q_UNUSED(palette);

    if (!painter || text.isEmpty())
    {
        return;
    }

    // The role says, which kind of a text is drawn, the color comes from the theme
    PDFThemeColors::Color color = PDFThemeColors::TextPrimary;
    bool hasColor = true;

    switch (textRole)
    {
        case QPalette::WindowText:
        case QPalette::Text:
        case QPalette::ToolTipText:
            color = PDFThemeColors::TextPrimary;
            break;

        case QPalette::ButtonText:
            color = PDFThemeColors::TextSecondary;
            break;

        case QPalette::HighlightedText:
            color = PDFThemeColors::IconOnAccent;
            break;

        case QPalette::PlaceholderText:
            color = PDFThemeColors::TextMuted;
            break;

        case QPalette::Link:
            color = PDFThemeColors::TextAccent;
            break;

        case QPalette::LinkVisited:
            color = PDFThemeColors::TextAccentPressed;
            break;

        case QPalette::BrightText:
            color = PDFThemeColors::TextDanger;
            break;

        default:
            // The caller has set the pen of the painter
            hasColor = false;
            break;
    }

    const PDFStylePainterGuard guard(painter);

    if (hasColor)
    {
        painter->setPen(PDFStylePainter::color(enabled ? color : PDFThemeColors::TextDisabled));
    }

    painter->drawText(rect, flags, text);
}

void PDFWidgetStyle::drawItemPixmap(QPainter* painter, const QRect& rect, int alignment, const QPixmap& pixmap) const
{
    BaseClass::drawItemPixmap(painter, rect, alignment, pixmap);
}

void PDFWidgetStyle::polish(QWidget* widget)
{
    BaseClass::polish(widget);
    m_impl->polish(widget);
}

void PDFWidgetStyle::unpolish(QWidget* widget)
{
    m_impl->unpolish(widget);
    BaseClass::unpolish(widget);
}

void PDFWidgetStyle::polish(QApplication* application)
{
    BaseClass::polish(application);
    m_impl->polish(application);
}

void PDFWidgetStyle::unpolish(QApplication* application)
{
    m_impl->unpolish(application);
    BaseClass::unpolish(application);
}

void PDFWidgetStyle::polish(QPalette& palette)
{
    palette = PDFThemeColors::createLeakPalette();
}

bool PDFWidgetStyle::isActive()
{
    // The style of the application can be wrapped by a style sheet or by a proxy style
    return PDFWidgetStyleImpl::isApplicationStyle();
}

bool PDFWidgetStyle::isFocusRingVisible(const QWidget* widget)
{
    if (!widget || !widget->hasFocus() || !widget->isEnabled())
    {
        return false;
    }

    QStyleOption option;
    option.initFrom(widget);
    return PDFWidgetStyleImpl::isApplicationFocusVisible(&option, widget);
}

void PDFWidgetStyle::setAnimationTime(qint64 milliseconds)
{
    m_impl->setAnimationTime(milliseconds);
}

bool PDFWidgetStyle::isSplitterHoverPending() const
{
    return m_impl->isSplitterHoverPending();
}

}   // namespace pdf
