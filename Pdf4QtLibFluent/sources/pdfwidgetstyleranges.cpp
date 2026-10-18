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

#include "pdfwidgetstyleimpl.h"

#include <QDial>
#include <QLocale>
#include <QEasingCurve>
#include <QPainter>
#include <QProgressBar>
#include <QScrollBar>
#include <QSlider>
#include <QStyleOptionProgressBar>
#include <QStyleOptionSlider>
#include <QtMath>

namespace pdf
{

/// Sliders, dials, progress bars and scroll bars
/// (sections 07, 08 and 09 of the design).
class PDFWidgetStyleRanges : public PDFWidgetStyleArea
{
public:
    using PDFWidgetStyleArea::PDFWidgetStyleArea;

    virtual bool drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const override;
    virtual bool subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const override;
    virtual bool sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const override;
    virtual void polish(QWidget* widget) override;
    virtual void unpolish(QWidget* widget) override;

    // Geometry of the design
    static constexpr int SLIDER_THICKNESS = 20;
    static constexpr int SLIDER_THICKNESS_TICKS = 28;
    static constexpr int SLIDER_GROOVE = 4;
    static constexpr int SLIDER_HANDLE = 14;
    static constexpr int SLIDER_TICK_OFFSET = 4;
    static constexpr int SLIDER_TICK_LENGTH = 4;
    static constexpr int PROGRESS_HEIGHT = 16;
    static constexpr int PROGRESS_GROOVE = 4;
    static constexpr int PROGRESS_GROOVE_THIN = 2;
    static constexpr int PROGRESS_LABEL_GAP = 8;
    static constexpr int SCROLLBAR_EXTENT = 12;
    static constexpr int SCROLLBAR_BAR = 6;
    static constexpr int SCROLLBAR_SLIDER_MIN = 32;
    static constexpr int BUSY_PERIOD = 1200;

private:
    static constexpr const char* SAVED_OPAQUE = "_pdf4qt_opaque";
    static constexpr const char* SAVED_MINIMUM = "_pdf4qt_savedMinimum";
    static constexpr const char* SAVED_MAXIMUM = "_pdf4qt_savedMaximum";

    static bool isThin(const QWidget* widget);
    static bool isBusy(const QStyleOptionProgressBar* option);
    static int getSliderLine(const QStyleOptionSlider* option);
    static int getProgressLabelWidth(const QStyleOptionProgressBar* option, const QWidget* widget);

    /// Returns the part of the scroll bar along its axis
    /// \param rect Rectangle of the scroll bar
    /// \param isHorizontal Orientation of the scroll bar
    /// \param start Start of the part along the axis
    /// \param size Length of the part along the axis
    /// \param isBar The part is the bar of the slider (6 px), not the whole extent
    static QRect getScrollBarPart(const QRect& rect, bool isHorizontal, int start, int size, bool isBar);

    /// Draws a tick of the slider at the position along the slider and at the coordinate across it
    static void drawSliderTick(QPainter* painter, const QRect& rect, bool isHorizontal, int position, qreal across, const QColor& color);

    /// Converts the angle measured clockwise from the top in degrees to the angle of Qt
    /// (counterclockwise from the right in 1/16 of a degree)
    static int toQtAngle(qreal clockwiseFromTop);

    QRect getSliderRect(const QStyleOptionSlider* option, QStyle::SubControl subControl) const;
    QRect getScrollBarRect(const QStyleOptionSlider* option, QStyle::SubControl subControl) const;
    void drawSlider(const QStyleOptionSlider* option, QPainter* painter, const QWidget* widget) const;
    void drawDial(const QStyleOptionSlider* option, QPainter* painter, const QWidget* widget) const;
    void drawScrollBar(const QStyleOptionSlider* option, QPainter* painter, const QWidget* widget) const;
    void drawProgressGroove(const QStyleOptionProgressBar* option, QPainter* painter, const QWidget* widget) const;
    void drawProgressContents(const QStyleOptionProgressBar* option, QPainter* painter, const QWidget* widget) const;
    void drawHandle(QPainter* painter, const QRectF& rect, const PDFStyleState& state, bool isHandleActive) const;
};

std::unique_ptr<PDFWidgetStyleArea> PDFWidgetStyleAreas::createRanges(PDFWidgetStyleImpl* impl)
{
    return std::make_unique<PDFWidgetStyleRanges>(impl);
}

bool PDFWidgetStyleRanges::isThin(const QWidget* widget)
{
    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::THIN);
}

bool PDFWidgetStyleRanges::isBusy(const QStyleOptionProgressBar* option)
{
    return option->minimum == 0 && option->maximum == 0;
}

int PDFWidgetStyleRanges::getSliderLine(const QStyleOptionSlider* option)
{
    // Distance of the axis of the groove from the top (left) edge of the slider.
    // Tick marks take the space on their side, the groove keeps its distance
    // from the other edge.
    const bool isHorizontal = option->orientation == Qt::Horizontal;
    const int thickness = isHorizontal ? option->rect.height() : option->rect.width();

    switch (option->tickPosition)
    {
        case QSlider::TicksAbove:
            return thickness - SLIDER_THICKNESS / 2;

        case QSlider::TicksBothSides:
            return thickness / 2;

        case QSlider::TicksBelow:
            return SLIDER_THICKNESS / 2;

        default:
            break;
    }

    return thickness / 2;
}

int PDFWidgetStyleRanges::getProgressLabelWidth(const QStyleOptionProgressBar* option, const QWidget* widget)
{
    if (!option->textVisible || isThin(widget))
    {
        return 0;
    }

    // The width is reserved for the widest text, so the groove does not change its length
    const QFont font = PDFWidgetStyleImpl::getFont(widget, PDFThemeFonts::Role::Small);
    int width = PDFWidgetStyleImpl::getTextWidth(option->text, font);

    if (const QProgressBar* progressBar = qobject_cast<const QProgressBar*>(widget))
    {
        QString widest = progressBar->format();
        const QString maximumText = QLocale().toString(progressBar->maximum());
        widest.replace(QLatin1String("%p"), QLatin1String("100"));
        widest.replace(QLatin1String("%v"), maximumText);
        widest.replace(QLatin1String("%m"), maximumText);
        width = qMax(width, PDFWidgetStyleImpl::getTextWidth(widest, font));
    }

    return width > 0 ? width + PROGRESS_LABEL_GAP : 0;
}

bool PDFWidgetStyleRanges::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    Q_UNUSED(option);
    Q_UNUSED(painter);
    Q_UNUSED(widget);

    switch (element)
    {
        case QStyle::PE_IndicatorProgressChunk:
        case QStyle::PE_PanelScrollAreaCorner:
            // Chunks are not used. The corner of a scroll area shows the fill of its frame.
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleRanges::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::CE_ProgressBar:
        {
            if (const QStyleOptionProgressBar* progressBar = qstyleoption_cast<const QStyleOptionProgressBar*>(option))
            {
                QStyleOptionProgressBar subOption = *progressBar;
                subOption.rect = style()->subElementRect(QStyle::SE_ProgressBarGroove, progressBar, widget);
                style()->drawControl(QStyle::CE_ProgressBarGroove, &subOption, painter, widget);
                subOption.rect = style()->subElementRect(QStyle::SE_ProgressBarContents, progressBar, widget);
                style()->drawControl(QStyle::CE_ProgressBarContents, &subOption, painter, widget);

                if (progressBar->textVisible && !isThin(widget))
                {
                    subOption.rect = style()->subElementRect(QStyle::SE_ProgressBarLabel, progressBar, widget);
                    style()->drawControl(QStyle::CE_ProgressBarLabel, &subOption, painter, widget);
                }
            }
            return true;
        }

        case QStyle::CE_ProgressBarGroove:
        {
            if (const QStyleOptionProgressBar* progressBar = qstyleoption_cast<const QStyleOptionProgressBar*>(option))
            {
                drawProgressGroove(progressBar, painter, widget);
            }
            return true;
        }

        case QStyle::CE_ProgressBarContents:
        {
            if (const QStyleOptionProgressBar* progressBar = qstyleoption_cast<const QStyleOptionProgressBar*>(option))
            {
                drawProgressContents(progressBar, painter, widget);
            }
            return true;
        }

        case QStyle::CE_ProgressBarLabel:
        {
            if (const QStyleOptionProgressBar* progressBar = qstyleoption_cast<const QStyleOptionProgressBar*>(option))
            {
                const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::Small);
                const bool isRightToLeft = progressBar->direction == Qt::RightToLeft;
                PDFStylePainter::text(painter, QRectF(progressBar->rect), Qt::AlignVCenter | (isRightToLeft ? Qt::AlignLeft : Qt::AlignRight) | Qt::TextSingleLine,
                                      progressBar->text, font, PDFWidgetStyleImpl::color(Color::TextMuted));
            }
            return true;
        }

        case QStyle::CE_ScrollBarAddLine:
        case QStyle::CE_ScrollBarSubLine:
        case QStyle::CE_ScrollBarAddPage:
        case QStyle::CE_ScrollBarSubPage:
        case QStyle::CE_ScrollBarFirst:
        case QStyle::CE_ScrollBarLast:
            // The scroll bar has no buttons and its pages are the groove
            return true;

        case QStyle::CE_ScrollBarSlider:
        {
            const PDFStyleState state = m_impl->getState(option, widget);

            Color fill = Color::BorderStrong;
            if (!state.enabled)
            {
                fill = Color::BorderDefault;
            }
            else if (state.pressed)
            {
                fill = Color::TextTertiary;
            }
            else if (state.hover)
            {
                fill = Color::IconMuted;
            }

            PDFStylePainter::fill(painter, QRectF(option->rect), Radii(SCROLLBAR_BAR / 2.0), fill);
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleRanges::drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    const QStyleOptionSlider* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);

    switch (control)
    {
        case QStyle::CC_Slider:
            if (slider)
            {
                drawSlider(slider, painter, widget);
            }
            return true;

        case QStyle::CC_Dial:
            if (slider)
            {
                drawDial(slider, painter, widget);
            }
            return true;

        case QStyle::CC_ScrollBar:
            if (slider)
            {
                drawScrollBar(slider, painter, widget);
            }
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleRanges::subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const
{
    switch (element)
    {
        case QStyle::SE_ProgressBarGroove:
        case QStyle::SE_ProgressBarContents:
        case QStyle::SE_ProgressBarLabel:
        {
            const QStyleOptionProgressBar* progressBar = qstyleoption_cast<const QStyleOptionProgressBar*>(option);
            if (!progressBar)
            {
                return false;
            }

            const bool isHorizontal = progressBar->state.testFlag(QStyle::State_Horizontal);
            const int labelWidth = isHorizontal ? getProgressLabelWidth(progressBar, widget) : 0;
            const QRect outer = option->rect;

            if (element == QStyle::SE_ProgressBarLabel)
            {
                const QRect label(outer.right() + 1 - labelWidth + PROGRESS_LABEL_GAP, outer.top(), qMax(0, labelWidth - PROGRESS_LABEL_GAP), outer.height());
                rect = labelWidth > 0 ? PDFWidgetStyleImpl::visualRect(option, outer, label) : QRect();
                return true;
            }

            const int thickness = isThin(widget) ? PROGRESS_GROOVE_THIN : PROGRESS_GROOVE;
            QRect groove;
            if (isHorizontal)
            {
                groove = QRect(outer.left(), outer.top() + (outer.height() - thickness) / 2, outer.width() - labelWidth, thickness);
            }
            else
            {
                groove = QRect(outer.left() + (outer.width() - thickness) / 2, outer.top(), thickness, outer.height());
            }

            rect = PDFWidgetStyleImpl::visualRect(option, outer, groove);
            return true;
        }

        case QStyle::SE_SliderFocusRect:
            rect = option->rect;
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleRanges::subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const
{
    Q_UNUSED(widget);

    const QStyleOptionSlider* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
    if (!slider)
    {
        return false;
    }

    switch (control)
    {
        case QStyle::CC_Slider:
            rect = getSliderRect(slider, subControl);
            return true;

        case QStyle::CC_ScrollBar:
            rect = getScrollBarRect(slider, subControl);
            return true;

        case QStyle::CC_Dial:
            rect = option->rect;
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleRanges::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const
{
    switch (type)
    {
        case QStyle::CT_Slider:
        {
            const QStyleOptionSlider* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
            const bool hasTicks = slider && slider->tickPosition != QSlider::NoTicks;
            const bool isHorizontal = !slider || slider->orientation == Qt::Horizontal;
            const int thickness = hasTicks ? SLIDER_THICKNESS_TICKS : SLIDER_THICKNESS;
            size = isHorizontal ? QSize(contentsSize.width(), thickness) : QSize(thickness, contentsSize.height());
            return true;
        }

        case QStyle::CT_ProgressBar:
        {
            const QStyleOptionProgressBar* progressBar = qstyleoption_cast<const QStyleOptionProgressBar*>(option);
            const bool isHorizontal = !progressBar || progressBar->state.testFlag(QStyle::State_Horizontal);
            const int thickness = isThin(widget) ? PROGRESS_GROOVE_THIN : PROGRESS_HEIGHT;
            size = isHorizontal ? QSize(contentsSize.width(), thickness) : QSize(thickness, contentsSize.height());
            return true;
        }

        case QStyle::CT_ScrollBar:
            size = contentsSize;
            return true;

        default:
            break;
    }

    return false;
}

void PDFWidgetStyleRanges::polish(QWidget* widget)
{
    if (QProgressBar* progressBar = qobject_cast<QProgressBar*>(widget))
    {
        // The progress bar asks at least for the height of its font (its minimal
        // size hint), which is more than the bar of the design, and layouts
        // enlarge the size hint to it. Only the explicit sizes of the widget
        // have the priority, so the thickness is set, unless the application
        // has set its own sizes.
        if (!widget->property(SAVED_MINIMUM).isValid())
        {
            widget->setProperty(SAVED_MINIMUM, widget->minimumSize());
            widget->setProperty(SAVED_MAXIMUM, widget->maximumSize());
        }

        const QSize savedMinimum = widget->property(SAVED_MINIMUM).toSize();
        const QSize savedMaximum = widget->property(SAVED_MAXIMUM).toSize();
        const int thickness = isThin(widget) ? PROGRESS_GROOVE_THIN : PROGRESS_HEIGHT;

        if (progressBar->orientation() == Qt::Horizontal && savedMinimum.height() == 0 && savedMaximum.height() == QWIDGETSIZE_MAX)
        {
            widget->setFixedHeight(thickness);
        }
        else if (progressBar->orientation() == Qt::Vertical && savedMinimum.width() == 0 && savedMaximum.width() == QWIDGETSIZE_MAX)
        {
            widget->setFixedWidth(thickness);
        }
    }

    if (qobject_cast<QScrollBar*>(widget) && !widget->property(SAVED_OPAQUE).isValid())
    {
        // Only the groove and the slider are drawn, the gutters show the widget below
        widget->setProperty(SAVED_OPAQUE, widget->testAttribute(Qt::WA_OpaquePaintEvent));
        widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
    }
}

void PDFWidgetStyleRanges::unpolish(QWidget* widget)
{
    const QVariant savedMinimum = widget->property(SAVED_MINIMUM);
    if (savedMinimum.isValid())
    {
        widget->setMinimumSize(savedMinimum.toSize());
        widget->setMaximumSize(widget->property(SAVED_MAXIMUM).toSize());
        widget->setProperty(SAVED_MINIMUM, QVariant());
        widget->setProperty(SAVED_MAXIMUM, QVariant());
    }

    const QVariant savedOpaque = widget->property(SAVED_OPAQUE);
    if (savedOpaque.isValid())
    {
        widget->setAttribute(Qt::WA_OpaquePaintEvent, savedOpaque.toBool());
        widget->setProperty(SAVED_OPAQUE, QVariant());
    }
}

QRect PDFWidgetStyleRanges::getSliderRect(const QStyleOptionSlider* option, QStyle::SubControl subControl) const
{
    const QRect rect = option->rect;
    const bool isHorizontal = option->orientation == Qt::Horizontal;
    const int line = getSliderLine(option);
    const int length = isHorizontal ? rect.width() : rect.height();

    switch (subControl)
    {
        case QStyle::SC_SliderGroove:
        {
            if (isHorizontal)
            {
                return QRect(rect.left(), rect.top() + line - SLIDER_GROOVE / 2, rect.width(), SLIDER_GROOVE);
            }
            return QRect(rect.left() + line - SLIDER_GROOVE / 2, rect.top(), SLIDER_GROOVE, rect.height());
        }

        case QStyle::SC_SliderHandle:
        {
            const int position = QStyle::sliderPositionFromValue(option->minimum, option->maximum, option->sliderPosition, qMax(0, length - SLIDER_HANDLE), option->upsideDown);

            if (isHorizontal)
            {
                return QRect(rect.left() + position, rect.top() + line - SLIDER_HANDLE / 2, SLIDER_HANDLE, SLIDER_HANDLE);
            }
            return QRect(rect.left() + line - SLIDER_HANDLE / 2, rect.top() + position, SLIDER_HANDLE, SLIDER_HANDLE);
        }

        case QStyle::SC_SliderTickmarks:
            return rect;

        default:
            break;
    }

    return QRect();
}

QRect PDFWidgetStyleRanges::getScrollBarPart(const QRect& rect, bool isHorizontal, int start, int size, bool isBar)
{
    if (isHorizontal)
    {
        const int thickness = isBar ? SCROLLBAR_BAR : rect.height();
        return QRect(rect.left() + start, rect.top() + (rect.height() - thickness) / 2, size, thickness);
    }

    const int thickness = isBar ? SCROLLBAR_BAR : rect.width();
    return QRect(rect.left() + (rect.width() - thickness) / 2, rect.top() + start, thickness, size);
}

void PDFWidgetStyleRanges::drawSliderTick(QPainter* painter, const QRect& rect, bool isHorizontal, int position, qreal across, const QColor& color)
{
    if (isHorizontal)
    {
        PDFStylePainter::fillRect(painter, QRectF(rect.left() + position, across, 1.0, SLIDER_TICK_LENGTH), color);
    }
    else
    {
        PDFStylePainter::fillRect(painter, QRectF(across, rect.top() + position, SLIDER_TICK_LENGTH, 1.0), color);
    }
}

int PDFWidgetStyleRanges::toQtAngle(qreal clockwiseFromTop)
{
    return qRound((90.0 - clockwiseFromTop) * 16.0);
}

QRect PDFWidgetStyleRanges::getScrollBarRect(const QStyleOptionSlider* option, QStyle::SubControl subControl) const
{
    const QRect rect = option->rect;
    const bool isHorizontal = option->orientation == Qt::Horizontal;
    const int length = isHorizontal ? rect.width() : rect.height();
    const qint64 range = qint64(option->maximum) - option->minimum;

    // Slider: 6 px bar centered across the extent
    int sliderLength = length;
    if (range > 0)
    {
        // QAbstractSlider keeps the page step positive, an option filled by the
        // application can have any value (range + page step must not be zero)
        const qint64 pageStep = qMax(0, option->pageStep);
        sliderLength = static_cast<int>((pageStep * length) / (range + pageStep));
        sliderLength = qBound(qMin(SCROLLBAR_SLIDER_MIN, length), sliderLength, length);
    }

    const int sliderStart = range > 0 ? QStyle::sliderPositionFromValue(option->minimum, option->maximum, option->sliderPosition, length - sliderLength, option->upsideDown) : 0;

    QRect result;

    switch (subControl)
    {
        case QStyle::SC_ScrollBarGroove:
            return rect;

        case QStyle::SC_ScrollBarSlider:
            if (range <= 0)
            {
                return QRect();
            }
            result = getScrollBarPart(rect, isHorizontal, sliderStart, sliderLength, false);
            break;

        case QStyle::SC_ScrollBarSubPage:
            result = getScrollBarPart(rect, isHorizontal, 0, sliderStart, false);
            break;

        case QStyle::SC_ScrollBarAddPage:
            result = getScrollBarPart(rect, isHorizontal, sliderStart + sliderLength, length - sliderStart - sliderLength, false);
            break;

        default:
            // There are no buttons
            return QRect();
    }

    return isHorizontal ? PDFWidgetStyleImpl::visualRect(option, rect, result) : result;
}

void PDFWidgetStyleRanges::drawHandle(QPainter* painter, const QRectF& rect, const PDFStyleState& state, bool isHandleActive) const
{
    if (!state.enabled)
    {
        PDFStylePainter::fillEllipse(painter, rect, PDFWidgetStyleImpl::color(Color::BgMuted));
        PDFStylePainter::strokeEllipseInside(painter, rect, PDFWidgetStyleImpl::color(Color::BorderStrong), 2.0);
        return;
    }

    if (state.pressed && isHandleActive)
    {
        PDFStylePainter::fillEllipse(painter, rect, PDFWidgetStyleImpl::color(Color::BgAccent));
        return;
    }

    PDFStylePainter::fillEllipse(painter, rect, PDFWidgetStyleImpl::color(Color::BgSurface));
    PDFStylePainter::strokeEllipseInside(painter, rect, PDFWidgetStyleImpl::color(state.hover && isHandleActive ? Color::AccentBlueIcon : Color::BgAccent), 2.0);
}

void PDFWidgetStyleRanges::drawSlider(const QStyleOptionSlider* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const bool isHorizontal = option->orientation == Qt::Horizontal;
    const QRectF groove(style()->subControlRect(QStyle::CC_Slider, option, QStyle::SC_SliderGroove, widget));
    const QRectF handle(style()->subControlRect(QStyle::CC_Slider, option, QStyle::SC_SliderHandle, widget));
    const Radii grooveRadii(SLIDER_GROOVE / 2.0);

    if (option->subControls.testFlag(QStyle::SC_SliderGroove))
    {
        PDFStylePainter::fill(painter, groove, grooveRadii, Color::BgTrack);

        // The filled part runs from the end of the minimum to the center of the handle
        QRectF filled = groove;
        const QPointF center = handle.center();

        if (isHorizontal)
        {
            if (option->upsideDown)
            {
                filled.setLeft(center.x());
            }
            else
            {
                filled.setRight(center.x());
            }
        }
        else
        {
            if (option->upsideDown)
            {
                filled.setTop(center.y());
            }
            else
            {
                filled.setBottom(center.y());
            }
        }

        if (option->maximum > option->minimum)
        {
            PDFStylePainter::fill(painter, filled, grooveRadii, state.enabled ? Color::BgAccent : Color::BorderStrong);
        }
    }

    if (option->subControls.testFlag(QStyle::SC_SliderTickmarks) && option->tickPosition != QSlider::NoTicks)
    {
        int interval = option->tickInterval;
        if (interval <= 0)
        {
            interval = qMax(1, option->pageStep);
        }

        const int length = isHorizontal ? option->rect.width() : option->rect.height();
        const int available = qMax(0, length - SLIDER_HANDLE);
        const qreal grooveStart = isHorizontal ? groove.top() : groove.left();
        const qreal grooveEnd = isHorizontal ? groove.bottom() : groove.right();
        const bool hasBefore = option->tickPosition == QSlider::TicksAbove || option->tickPosition == QSlider::TicksBothSides;
        const bool hasAfter = option->tickPosition == QSlider::TicksBelow || option->tickPosition == QSlider::TicksBothSides;
        const QColor tickColor = PDFWidgetStyleImpl::color(Color::BorderStrong);

        // A slider with a huge range would draw ticks one over another
        const qint64 count = (qint64(option->maximum) - option->minimum) / interval;
        if (count <= qMax(1, available))
        {
            for (qint64 value = option->minimum; value <= option->maximum; value += interval)
            {
                const int position = QStyle::sliderPositionFromValue(option->minimum, option->maximum, static_cast<int>(value), available, option->upsideDown) + SLIDER_HANDLE / 2;

                if (hasBefore)
                {
                    drawSliderTick(painter, option->rect, isHorizontal, position, grooveStart - SLIDER_TICK_OFFSET - SLIDER_TICK_LENGTH, tickColor);
                }
                if (hasAfter)
                {
                    drawSliderTick(painter, option->rect, isHorizontal, position, grooveEnd + SLIDER_TICK_OFFSET, tickColor);
                }
            }
        }
    }

    if (option->subControls.testFlag(QStyle::SC_SliderHandle))
    {
        if (state.focusVisible)
        {
            // The focus ring touches the handle from the outside. It reaches 1 px
            // below the handle, which is drawn over it, so no seam is left between them.
            PDFStylePainter::strokeEllipseInside(painter, handle.adjusted(-2.0, -2.0, 2.0, 2.0), PDFWidgetStyleImpl::color(Color::BorderFocus), 3.0);
        }

        drawHandle(painter, handle, state, option->activeSubControls.testFlag(QStyle::SC_SliderHandle));
    }
}

void PDFWidgetStyleRanges::drawDial(const QStyleOptionSlider* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF rect(option->rect);

    // The handle of 12 px lies on the axis of the ring and it exceeds the ring
    // by 4 px, so the ring is smaller than the widget by this distance.
    const qreal extent = qMin(rect.width(), rect.height()) - 8.0;
    if (extent <= 8.0)
    {
        return;
    }

    const QRectF outer = PDFStylePainter::centered(rect, QSizeF(extent, extent));
    const QRectF axis = outer.adjusted(2.0, 2.0, -2.0, -2.0);
    const qreal axisRadius = axis.width() / 2.0;
    const QPointF center = outer.center();

    const qint64 range = qint64(option->maximum) - option->minimum;
    const qint64 position = option->upsideDown ? qint64(option->sliderPosition) : (qint64(option->maximum) - option->sliderPosition + option->minimum);
    const qreal fraction = range > 0 ? qBound(0.0, qreal(position - option->minimum) / qreal(range), 1.0) : 0.0;

    // Angles are measured clockwise from the top. The dial runs 270 degrees from
    // the bottom left, a wrapping dial runs the whole circle from the bottom.
    const bool isWrapping = option->dialWrapping;
    const qreal startAngle = isWrapping ? 180.0 : 225.0;
    const qreal span = isWrapping ? 360.0 : 270.0;
    const qreal valueAngle = startAngle + fraction * span;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(Qt::NoBrush);

    // The track is the whole ring, also for a dial, which does not wrap
    painter->setPen(QPen(PDFWidgetStyleImpl::color(Color::BgTrack), 4.0, Qt::SolidLine, Qt::FlatCap));
    painter->drawEllipse(axis);

    if (fraction > 0.0)
    {
        painter->setPen(QPen(PDFWidgetStyleImpl::color(state.enabled ? Color::BgAccent : Color::BorderStrong), 4.0, Qt::SolidLine, Qt::FlatCap));
        painter->drawArc(axis, toQtAngle(startAngle), qRound(-fraction * span * 16.0));
    }

    if (option->subControls.testFlag(QStyle::SC_DialTickmarks))
    {
        // Notches: lines 1 x 3 outside of the ring
        int interval = option->tickInterval > 0 ? option->tickInterval : qMax(1, option->pageStep);
        const qint64 count = range / interval;

        if (range > 0 && count <= 360)
        {
            painter->setPen(QPen(PDFWidgetStyleImpl::color(Color::BorderStrong), 1.0, Qt::SolidLine, Qt::FlatCap));
            for (qint64 i = 0; i <= count; ++i)
            {
                const qreal angle = qDegreesToRadians(startAngle + (qreal(i * interval) / qreal(range)) * span);
                const QPointF direction(qSin(angle), -qCos(angle));
                painter->drawLine(center + direction * (extent / 2.0 + 1.0), center + direction * (extent / 2.0 + 4.0));
            }
        }
    }

    painter->restore();

    const qreal angle = qDegreesToRadians(valueAngle);
    const QPointF handleCenter = center + QPointF(qSin(angle), -qCos(angle)) * axisRadius;
    const QRectF handle(handleCenter.x() - 6.0, handleCenter.y() - 6.0, 12.0, 12.0);

    if (state.focusVisible)
    {
        // The ring reaches 1 px below the handle, see the handle of a slider
        PDFStylePainter::strokeEllipseInside(painter, handle.adjusted(-2.0, -2.0, 2.0, 2.0), PDFWidgetStyleImpl::color(Color::BorderFocus), 3.0);
    }

    drawHandle(painter, handle, state, true);
}

void PDFWidgetStyleRanges::drawScrollBar(const QStyleOptionSlider* option, QPainter* painter, const QWidget* widget) const
{
    const QRect rect = option->rect;
    const bool isHorizontal = option->orientation == Qt::Horizontal;

    // Groove: bar of 6 px along the whole length
    const QRectF groove = isHorizontal ? QRectF(rect.left(), rect.top() + (rect.height() - SCROLLBAR_BAR) / 2.0, rect.width(), SCROLLBAR_BAR)
                                       : QRectF(rect.left() + (rect.width() - SCROLLBAR_BAR) / 2.0, rect.top(), SCROLLBAR_BAR, rect.height());
    PDFStylePainter::fill(painter, groove, Radii(SCROLLBAR_BAR / 2.0), Color::BgTrack);

    const QRect slider = style()->subControlRect(QStyle::CC_ScrollBar, option, QStyle::SC_ScrollBarSlider, widget);
    if (!slider.isValid() || !option->subControls.testFlag(QStyle::SC_ScrollBarSlider))
    {
        return;
    }

    QStyleOption sliderOption = *option;
    sliderOption.rect = isHorizontal ? QRect(slider.left(), rect.top() + (rect.height() - SCROLLBAR_BAR) / 2, slider.width(), SCROLLBAR_BAR)
                                     : QRect(rect.left() + (rect.width() - SCROLLBAR_BAR) / 2, slider.top(), SCROLLBAR_BAR, slider.height());

    // The slider is pressed only when it is dragged, a click to the groove does not press it
    const bool isSliderPressed = option->state.testFlag(QStyle::State_Sunken) && option->activeSubControls.testFlag(QStyle::SC_ScrollBarSlider);
    sliderOption.state.setFlag(QStyle::State_Sunken, isSliderPressed);
    style()->drawControl(QStyle::CE_ScrollBarSlider, &sliderOption, painter, widget);
}

void PDFWidgetStyleRanges::drawProgressGroove(const QStyleOptionProgressBar* option, QPainter* painter, const QWidget* widget) const
{
    const bool thin = isThin(widget);
    PDFStylePainter::fill(painter, QRectF(option->rect), Radii(thin ? 0.0 : PROGRESS_GROOVE / 2.0), thin ? Color::BgTrack : Color::BorderDefault);
}

void PDFWidgetStyleRanges::drawProgressContents(const QStyleOptionProgressBar* option, QPainter* painter, const QWidget* widget) const
{
    const bool thin = isThin(widget);
    const bool isHorizontal = option->state.testFlag(QStyle::State_Horizontal);
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const QRectF groove(option->rect);
    const qreal length = isHorizontal ? groove.width() : groove.height();
    const Radii radii(thin ? 0.0 : PROGRESS_GROOVE / 2.0);

    // The contents start at the end of the minimum
    // (the vertical bar grows from the bottom, the coordinate grows from the top)
    bool isReversed = isHorizontal ? (option->direction == Qt::RightToLeft) : true;
    if (option->invertedAppearance)
    {
        isReversed = !isReversed;
    }

    qreal start = 0.0;
    qreal size = 0.0;
    Color fill = isEnabled ? Color::BgAccent : Color::BorderStrong;

    if (isBusy(option))
    {
        // Chunk of 30 % of the groove, which slides through it in 1.2 s
        qreal phase = 0.4;
        if (m_impl->isAnimated())
        {
            const qint64 time = m_impl->getAnimationTime();
            phase = QEasingCurve(QEasingCurve::InOutSine).valueForProgress(qreal(time % BUSY_PERIOD) / qreal(BUSY_PERIOD));
            m_impl->animate(widget);
        }
        else
        {
            // Without the animation the chunk rests in the middle
            phase = 0.35 / 1.3 + 0.3 / 1.3;
        }

        size = 0.3 * length;
        start = (phase * 1.3 - 0.3) * length;
    }
    else
    {
        const qint64 range = qint64(option->maximum) - option->minimum;
        if (range <= 0)
        {
            return;
        }

        const qreal fraction = qBound(0.0, qreal(qint64(option->progress) - option->minimum) / qreal(range), 1.0);
        size = fraction * length;

        if (fraction >= 1.0 && isEnabled && !thin)
        {
            // The finished bar is green
            fill = Color::AccentGreenIcon;
        }
    }

    if (size <= 0.0)
    {
        return;
    }

    if (isReversed)
    {
        start = length - start - size;
    }

    const QRectF contents = isHorizontal ? QRectF(groove.left() + start, groove.top(), size, groove.height())
                                         : QRectF(groove.left(), groove.top() + start, groove.width(), size);

    // The contents are clipped by the rounded groove
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setClipPath(PDFStylePainter::roundedPath(groove, radii), Qt::IntersectClip);
    PDFStylePainter::fill(painter, contents, radii, fill);
    painter->restore();
}

}   // namespace pdf
