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
#include "pdfstylepainterguard.h"
#include "pdfthemeiconengine.h"

#include <QAbstractButton>
#include <QFontMetrics>
#include <QMainWindow>
#include <QPainter>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QStyleOptionToolBar>
#include <QStyleOptionToolButton>
#include <QToolBar>
#include <QToolButton>
#include <QtMath>

namespace pdf
{

/// Push buttons, tool buttons, toolbars, check boxes and radio buttons
/// (sections 01, 02 and 03 of the design).
class PDFWidgetStyleButtons : public PDFWidgetStyleArea
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
    static constexpr int BUTTON_HEIGHT = 36;
    static constexpr int BUTTON_RADIUS = 8;
    static constexpr int BUTTON_PADDING_LEFT = 16;
    static constexpr int BUTTON_PADDING_RIGHT = 18;
    static constexpr int BUTTON_PADDING_RIGHT_MENU = 12;
    static constexpr int BUTTON_ICON = 16;
    static constexpr int BUTTON_GAP = 8;
    static constexpr int BUTTON_CHEVRON = 14;
    static constexpr int BUTTON_MIN_WIDTH = 0;          ///< Buttons of the design have the width of their content
    static constexpr int INDICATOR = 18;
    static constexpr int INDICATOR_GAP = 8;
    static constexpr int TOOL_BUTTON = 36;
    static constexpr int TOOL_BUTTON_RADIUS = 10;
    static constexpr int TOOL_BUTTON_PADDING = 8;
    static constexpr int TOOL_BUTTON_CHEVRON = 12;
    static constexpr int TOOL_BUTTON_CHEVRON_GAP = 2;
    static constexpr int TOOL_BUTTON_SPLIT = 18;
    static constexpr int TOOL_BUTTON_TEXT_ICON = 18;
    static constexpr int SIDEBAR_TAB = 32;

    /// Color bar of the Action tile 190:740 (property Color bar): 18 x 3, radius
    /// 1.5, centered right below the icon
    static constexpr int ACTION_TILE_COLOR_BAR_WIDTH = 18;
    static constexpr int ACTION_TILE_COLOR_BAR_HEIGHT = 3;
    static constexpr qreal ACTION_TILE_COLOR_BAR_RADIUS = 1.5;

    /// A color bar with a lower contrast ratio against the fill of the tile gets
    /// the outline border/strong (white on accent/neutral/bg)
    static constexpr qreal ACTION_TILE_COLOR_BAR_MINIMUM_CONTRAST = 1.3;

private:
    enum class ButtonKind
    {
        Secondary,
        Primary,
        Flat
    };

    static ButtonKind getButtonKind(const QStyleOptionButton* option, const QWidget* widget);
    static bool hasButtonBorder(const QStyleOptionButton* option, const QWidget* widget);
    static bool isIconOnly(const QStyleOptionButton* option);

    /// Returns true, if the tool button is drawn by another area of the style
    static bool isForeignToolButton(const QWidget* widget);
    static bool isSidebarTab(const QWidget* widget);
    static bool isSplitButton(const QStyleOptionToolButton* option);
    static bool hasMenuIndicator(const QStyleOptionToolButton* option, const QWidget* widget);
    static QString getArrowIconName(Qt::ArrowType arrowType);

    void drawButtonPanel(const QStyleOptionButton* option, QPainter* painter, const QWidget* widget) const;
    void drawButtonLabel(const QStyleOptionButton* option, QPainter* painter, const QWidget* widget) const;
    void drawIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget, bool isRadio) const;
    void drawIndicatorLabel(const QStyleOptionButton* option, QPainter* painter, const QWidget* widget) const;
    void drawToolButton(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const;
    void drawToolButtonLabel(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const;
    static void drawToolButtonGlyph(QPainter* painter, const QRectF& iconRect, const QStyleOptionToolButton* option, bool hasArrow, Color iconColor, const PDFStyleState& state);

    /// Draws the color bar of an Action tile (property colorBar) centered in the
    /// row \p row (its top is the bottom of the icon)
    static void drawToolButtonColorBar(QPainter* painter, const QRectF& row, const QWidget* widget, const PDFStyleState& state);

    /// Contrast ratio of two colors (WCAG 2.x, 1 … 21)
    static qreal getContrastRatio(const QColor& first, const QColor& second);
    static qreal getRelativeLuminance(const QColor& color);
    static qreal getLinearChannel(qreal value);
    void drawToolBar(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawArrow(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    QSize getButtonSize(const QStyleOptionButton* option, const QWidget* widget) const;
    QSize getToolButtonSize(const QStyleOptionToolButton* option, const QWidget* widget) const;
};

std::unique_ptr<PDFWidgetStyleArea> PDFWidgetStyleAreas::createButtons(PDFWidgetStyleImpl* impl)
{
    return std::make_unique<PDFWidgetStyleButtons>(impl);
}

PDFWidgetStyleButtons::ButtonKind PDFWidgetStyleButtons::getButtonKind(const QStyleOptionButton* option, const QWidget* widget)
{
    if (option->features.testFlag(QStyleOptionButton::DefaultButton) || PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::PRIMARY))
    {
        return ButtonKind::Primary;
    }

    if (option->features.testFlag(QStyleOptionButton::Flat))
    {
        return ButtonKind::Flat;
    }

    return ButtonKind::Secondary;
}

bool PDFWidgetStyleButtons::hasButtonBorder(const QStyleOptionButton* option, const QWidget* widget)
{
    // A button of a dialog becomes the default one, when it gets the focus. Its
    // size must not change with that, so it always has the space of the border.
    if (option->features.testFlag(QStyleOptionButton::AutoDefaultButton) && !PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::PRIMARY))
    {
        return !option->features.testFlag(QStyleOptionButton::Flat);
    }

    return getButtonKind(option, widget) == ButtonKind::Secondary;
}

bool PDFWidgetStyleButtons::isIconOnly(const QStyleOptionButton* option)
{
    return option->text.isEmpty() && !option->icon.isNull();
}

bool PDFWidgetStyleButtons::isForeignToolButton(const QWidget* widget)
{
    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SEGMENT) ||
           PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CHIP) ||
           PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::NEW_TAB) ||
           PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::STATUS_ITEM) ||
           (widget && widget->parentWidget() && (widget->parentWidget()->inherits("QTabBar") || widget->parentWidget()->inherits("QDockWidget")));
}

bool PDFWidgetStyleButtons::isSidebarTab(const QWidget* widget)
{
    if (!widget)
    {
        return false;
    }

    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SIDEBAR_TAB) ||
           PDFWidgetStyleImpl::hasProperty(widget->parentWidget(), PDFWidgetStyleProperties::SIDEBAR_TABS);
}

bool PDFWidgetStyleButtons::isSplitButton(const QStyleOptionToolButton* option)
{
    return option->features.testFlag(QStyleOptionToolButton::MenuButtonPopup) && option->subControls.testFlag(QStyle::SC_ToolButtonMenu);
}

bool PDFWidgetStyleButtons::hasMenuIndicator(const QStyleOptionToolButton* option, const QWidget* widget)
{
    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::MENU_INDICATOR))
    {
        // The button opens a popup of its own
        return true;
    }

    // A button with the delayed popup opens the menu by a long press and it has no indicator
    return option->features.testFlag(QStyleOptionToolButton::HasMenu) &&
           !option->features.testFlag(QStyleOptionToolButton::PopupDelay) &&
           !isSplitButton(option);
}

QString PDFWidgetStyleButtons::getArrowIconName(Qt::ArrowType arrowType)
{
    switch (arrowType)
    {
        case Qt::UpArrow:
            return QStringLiteral("chevron-up");
        case Qt::DownArrow:
            return QStringLiteral("chevron-down");
        case Qt::LeftArrow:
            return QStringLiteral("chevron-left");
        case Qt::RightArrow:
            return QStringLiteral("chevron-right");
        default:
            break;
    }

    return QString();
}

bool PDFWidgetStyleButtons::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::PE_PanelButtonCommand:
        case QStyle::PE_PanelButtonBevel:
        {
            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                drawButtonPanel(button, painter, widget);
            }
            else
            {
                // A panel of a button without the option of a button: secondary face
                QStyleOptionButton buttonOption;
                buttonOption.QStyleOption::operator=(*option);
                drawButtonPanel(&buttonOption, painter, widget);
            }
            return true;
        }

        case QStyle::PE_IndicatorButtonDropDown:
        {
            const PDFStyleState state = m_impl->getState(option, widget);
            const QRectF rect(option->rect);
            PDFStylePainter::fill(painter, rect, Radii(4.0), state.enabled ? Color::BgSurface : Color::BgMuted);
            PDFWidgetStyleImpl::drawStateOverlay(painter, rect, Radii(4.0), state);
            PDFStylePainter::strokeInside(painter, rect, Radii(4.0), state.pressed ? Color::BorderStrong : Color::BorderDefault, 1.0);
            return true;
        }

        case QStyle::PE_FrameDefaultButton:
        case QStyle::PE_FrameButtonBevel:
        case QStyle::PE_FrameButtonTool:
            // The primary fill marks the default button, there is no extra frame
            return true;

        case QStyle::PE_PanelButtonTool:
        {
            if (PDFWidgetStyleImpl::inherits(widget, "QLineEditIconButton") || isForeignToolButton(widget))
            {
                return isForeignToolButton(widget) ? false : true;
            }

            const PDFStyleState state = m_impl->getState(option, widget);
            const QRectF rect(option->rect);
            const QVariant radius = widget ? widget->property(PDFWidgetStyleProperties::RADIUS) : QVariant();
            const Radii radii(radius.isValid() ? radius.toReal() : (isSidebarTab(widget) ? 8.0 : TOOL_BUTTON_RADIUS));

            if (state.on)
            {
                Color fill = Color::BgAccentSubtle;
                if (state.pressed)
                {
                    fill = Color::BgAccentSubtlePressed;
                }
                else if (state.hover)
                {
                    fill = Color::BgAccentSubtleHover;
                }
                PDFStylePainter::fill(painter, rect, radii, fill);
            }
            else
            {
                PDFWidgetStyleImpl::drawStateOverlay(painter, rect, radii, state);
            }
            return true;
        }

        case QStyle::PE_IndicatorCheckBox:
            drawIndicator(option, painter, widget, false);
            return true;

        case QStyle::PE_IndicatorRadioButton:
            drawIndicator(option, painter, widget, true);
            return true;

        case QStyle::PE_IndicatorItemViewItemCheck:
        {
            // The indicator of a view is drawn as hovered together with its row
            drawIndicator(option, painter, widget, false);
            return true;
        }

        case QStyle::PE_IndicatorArrowUp:
        case QStyle::PE_IndicatorArrowDown:
        case QStyle::PE_IndicatorArrowLeft:
        case QStyle::PE_IndicatorArrowRight:
            drawArrow(element, option, painter, widget);
            return true;

        case QStyle::PE_PanelToolBar:
            drawToolBar(option, painter, widget);
            return true;

        case QStyle::PE_FrameDockWidget:
            return false;

        case QStyle::PE_IndicatorToolBarSeparator:
        {
            const QRectF rect(option->rect);

            if (PDFWidgetStyleImpl::inherits(widget, "QComboBox"))
            {
                // Separator of the popup list of a combo box: line with margins of 6 px
                // inside of the rows (the rectangle is the width of the viewport)
                const QMargins insets = PDFWidgetStyleImpl::getComboPopupRowInsets(widget, widget->layoutDirection());
                const QRectF shape = rect.adjusted(insets.left(), 0.0, -insets.right(), 0.0);
                PDFStylePainter::fillRect(painter, QRectF(shape.left() + 6.0, rect.top() + qFloor((rect.height() - 1.0) / 2.0), shape.width() - 12.0, 1.0), Color::BorderDivider);
                return true;
            }

            // Line 1 x 20 centered in the extent of the separator
            const bool isHorizontal = option->state.testFlag(QStyle::State_Horizontal);
            const QRectF line = isHorizontal ? PDFStylePainter::centered(rect, QSizeF(1.0, qMin(20.0, rect.height())))
                                             : PDFStylePainter::centered(rect, QSizeF(qMin(20.0, rect.width()), 1.0));
            PDFStylePainter::fillRect(painter, QRectF(line.toAlignedRect()), Color::BorderSeparator);
            return true;
        }

        case QStyle::PE_IndicatorToolBarHandle:
        {
            // Six dots of 2 px in two columns
            const QRectF rect(option->rect);
            const bool isHorizontal = option->state.testFlag(QStyle::State_Horizontal);
            const QSizeF gripSize = isHorizontal ? QSizeF(6.0, 10.0) : QSizeF(10.0, 6.0);
            const QRectF grip = PDFStylePainter::centered(rect, gripSize);
            const QColor color = PDFWidgetStyleImpl::color(Color::IconMuted);

            for (int column = 0; column < (isHorizontal ? 2 : 3); ++column)
            {
                for (int row = 0; row < (isHorizontal ? 3 : 2); ++row)
                {
                    PDFStylePainter::fillEllipse(painter, QRectF(grip.left() + column * 4.0, grip.top() + row * 4.0, 2.0, 2.0), color);
                }
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleButtons::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::CE_PushButton:
        {
            if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CHIP))
            {
                return false;
            }

            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                style()->drawControl(QStyle::CE_PushButtonBevel, button, painter, widget);
                style()->drawControl(QStyle::CE_PushButtonLabel, button, painter, widget);
            }
            return true;
        }

        case QStyle::CE_PushButtonBevel:
        {
            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                style()->drawPrimitive(QStyle::PE_PanelButtonCommand, button, painter, widget);
            }
            return true;
        }

        case QStyle::CE_PushButtonLabel:
        {
            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                drawButtonLabel(button, painter, widget);
            }
            return true;
        }

        case QStyle::CE_CheckBox:
        case QStyle::CE_RadioButton:
        {
            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                const bool isRadio = element == QStyle::CE_RadioButton;

                QStyleOptionButton subOption = *button;
                subOption.rect = style()->subElementRect(isRadio ? QStyle::SE_RadioButtonIndicator : QStyle::SE_CheckBoxIndicator, button, widget);
                style()->drawPrimitive(isRadio ? QStyle::PE_IndicatorRadioButton : QStyle::PE_IndicatorCheckBox, &subOption, painter, widget);

                subOption.rect = style()->subElementRect(isRadio ? QStyle::SE_RadioButtonContents : QStyle::SE_CheckBoxContents, button, widget);
                style()->drawControl(isRadio ? QStyle::CE_RadioButtonLabel : QStyle::CE_CheckBoxLabel, &subOption, painter, widget);
            }
            return true;
        }

        case QStyle::CE_CheckBoxLabel:
        case QStyle::CE_RadioButtonLabel:
        {
            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                drawIndicatorLabel(button, painter, widget);
            }
            return true;
        }

        case QStyle::CE_ToolButtonLabel:
        {
            if (isForeignToolButton(widget))
            {
                return false;
            }

            if (const QStyleOptionToolButton* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option))
            {
                drawToolButtonLabel(toolButton, painter, widget);
            }
            return true;
        }

        case QStyle::CE_ToolBar:
            drawToolBar(option, painter, widget);
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleButtons::drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    if (control != QStyle::CC_ToolButton || isForeignToolButton(widget))
    {
        return false;
    }

    if (const QStyleOptionToolButton* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option))
    {
        drawToolButton(toolButton, painter, widget);
    }

    return true;
}

bool PDFWidgetStyleButtons::subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const
{
    switch (element)
    {
        case QStyle::SE_PushButtonContents:
        case QStyle::SE_PushButtonFocusRect:
        case QStyle::SE_PushButtonBevel:
            rect = option->rect;
            return true;

        case QStyle::SE_CheckBoxIndicator:
        case QStyle::SE_RadioButtonIndicator:
        case QStyle::SE_CheckBoxFocusRect:
        case QStyle::SE_RadioButtonFocusRect:
        {
            const QRect indicator(option->rect.left(), option->rect.top() + (option->rect.height() - INDICATOR) / 2, INDICATOR, INDICATOR);
            rect = PDFWidgetStyleImpl::visualRect(option, option->rect, indicator);
            return true;
        }

        case QStyle::SE_CheckBoxContents:
        case QStyle::SE_RadioButtonContents:
        {
            const QRect contents = option->rect.adjusted(INDICATOR + INDICATOR_GAP, 0, 0, 0);
            rect = PDFWidgetStyleImpl::visualRect(option, option->rect, contents);
            return true;
        }

        case QStyle::SE_CheckBoxClickRect:
        case QStyle::SE_RadioButtonClickRect:
            rect = option->rect;
            return true;

        case QStyle::SE_ToolBarHandle:
        {
            // The handle of a movable toolbar of a main window lies inside of the paddings of the panel
            const QToolBar* toolBar = qobject_cast<const QToolBar*>(widget);
            const int extent = style()->pixelMetric(QStyle::PM_ToolBarHandleExtent, option, widget);

            rect = QRect();
            if (toolBar && extent > 0 && toolBar->isMovable() && qobject_cast<const QMainWindow*>(toolBar->parentWidget()))
            {
                const QMargins paddings = PDFWidgetStyleImpl::getToolBarPaddings(toolBar);

                if (toolBar->orientation() == Qt::Horizontal)
                {
                    const QRect handle(option->rect.left() + paddings.left(), option->rect.top() + paddings.top(), extent, option->rect.height() - paddings.top() - paddings.bottom());
                    rect = PDFWidgetStyleImpl::visualRect(option, option->rect, handle);
                }
                else
                {
                    rect = QRect(option->rect.left() + paddings.left(), option->rect.top() + paddings.top(), option->rect.width() - paddings.left() - paddings.right(), extent);
                }
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleButtons::subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const
{
    if (control != QStyle::CC_ToolButton || isForeignToolButton(widget))
    {
        return false;
    }

    const QStyleOptionToolButton* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option);
    if (!toolButton)
    {
        return false;
    }

    const int menuWidth = isSplitButton(toolButton) ? TOOL_BUTTON_SPLIT : 0;

    switch (subControl)
    {
        case QStyle::SC_ToolButton:
            rect = PDFWidgetStyleImpl::visualRect(option, option->rect, option->rect.adjusted(0, 0, -menuWidth, 0));
            return true;

        case QStyle::SC_ToolButtonMenu:
            rect = menuWidth > 0 ? PDFWidgetStyleImpl::visualRect(option, option->rect, option->rect.adjusted(option->rect.width() - menuWidth, 0, 0, 0)) : QRect();
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleButtons::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const
{
    switch (type)
    {
        case QStyle::CT_PushButton:
        {
            if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CHIP))
            {
                return false;
            }

            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                size = getButtonSize(button, widget);
            }
            else
            {
                size = QSize(qMax(BUTTON_MIN_WIDTH, contentsSize.width() + BUTTON_PADDING_LEFT + BUTTON_PADDING_RIGHT + 2), BUTTON_HEIGHT);
            }
            return true;
        }

        case QStyle::CT_CheckBox:
        case QStyle::CT_RadioButton:
        {
            int width = INDICATOR;

            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                int contents = 0;
                if (!button->icon.isNull())
                {
                    contents += button->iconSize.width();
                }
                if (!button->text.isEmpty())
                {
                    contents += (contents > 0 ? INDICATOR_GAP : 0) + PDFWidgetStyleImpl::getTextWidth(button->text, PDFWidgetStyleImpl::getFont(widget, Font::Body));
                }
                if (contents > 0)
                {
                    width += INDICATOR_GAP + contents;
                }
            }
            else if (contentsSize.width() > 0)
            {
                width += INDICATOR_GAP + contentsSize.width();
            }

            size = QSize(width, qMax(INDICATOR, QFontMetrics(PDFWidgetStyleImpl::getFont(widget, Font::Body)).height()));
            return true;
        }

        case QStyle::CT_ToolButton:
        {
            if (isForeignToolButton(widget))
            {
                return false;
            }

            if (const QStyleOptionToolButton* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option))
            {
                size = getToolButtonSize(toolButton, widget);
            }
            else
            {
                size = QSize(qMax(TOOL_BUTTON, contentsSize.width() + 2 * TOOL_BUTTON_PADDING), TOOL_BUTTON);
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

void PDFWidgetStyleButtons::polish(QWidget* widget)
{
    if (QToolBar* toolBar = qobject_cast<QToolBar*>(widget))
    {
        // The items are positioned by the layout of the toolbar from the corner
        // of the widget, so the paddings are the margins of the layout (margins
        // of the contents of the widget only enlarge its size hint)
        PDFWidgetStyleImpl::applyToolBarMargins(toolBar);
        m_impl->watchToolBar(toolBar);
    }
}

void PDFWidgetStyleButtons::unpolish(QWidget* widget)
{
    if (QToolBar* toolBar = qobject_cast<QToolBar*>(widget))
    {
        // The margins of the layout are computed again by the toolbar from the
        // metrics of its next style
        m_impl->unwatchToolBar(toolBar);
    }
}

void PDFWidgetStyleButtons::drawButtonPanel(const QStyleOptionButton* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const ButtonKind kind = getButtonKind(option, widget);
    const QRectF rect(option->rect);
    const Radii radii(BUTTON_RADIUS);

    if (state.on)
    {
        // Checked button: subtle accent fill with an accent border
        if (!state.enabled)
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgMuted);
            PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
            return;
        }

        if (kind != ButtonKind::Flat)
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
        }

        Color fill = Color::BgAccentSubtle;
        if (state.pressed)
        {
            fill = Color::BgAccentSubtlePressed;
        }
        else if (state.hover)
        {
            fill = Color::BgAccentSubtleHover;
        }

        PDFStylePainter::fill(painter, rect, radii, fill);
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderFocus, 1.0);
        return;
    }

    switch (kind)
    {
        case ButtonKind::Primary:
        {
            Color fill = Color::BgAccent;
            if (!state.enabled)
            {
                fill = Color::BgMuted;
            }
            else if (state.pressed)
            {
                fill = Color::BgAccentPressed;
            }
            else if (state.hover)
            {
                fill = Color::BgAccentHover;
            }

            PDFStylePainter::fill(painter, rect, radii, fill);
            break;
        }

        case ButtonKind::Flat:
            PDFWidgetStyleImpl::drawStateOverlay(painter, rect, radii, state);
            break;

        case ButtonKind::Secondary:
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
            PDFWidgetStyleImpl::drawStateOverlay(painter, rect, radii, state);

            // The focus ring replaces the border
            if (!state.focusVisible)
            {
                PDFStylePainter::strokeInside(painter, rect, radii, state.pressed ? Color::BorderStrong : Color::BorderDefault, 1.0);
            }
            break;
        }
    }
}

void PDFWidgetStyleButtons::drawButtonLabel(const QStyleOptionButton* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const ButtonKind kind = getButtonKind(option, widget);
    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::BodySemiBold);
    const bool hasMenu = option->features.testFlag(QStyleOptionButton::HasMenu);
    const bool hasIcon = !option->icon.isNull();
    const int border = hasButtonBorder(option, widget) ? 1 : 0;

    Color textColor = Color::TextSecondary;
    Color iconColor = Color::IconDefault;

    if (!state.enabled)
    {
        textColor = Color::TextDisabled;
        iconColor = Color::IconDisabled;
    }
    else if (state.on)
    {
        textColor = Color::TextAccent;
        iconColor = Color::IconAccent;
    }
    else if (kind == ButtonKind::Primary)
    {
        textColor = Color::IconOnAccent;
        iconColor = Color::IconOnAccent;
    }
    else if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::DESTRUCTIVE))
    {
        textColor = Color::TextDanger;
        iconColor = Color::IconDanger;
    }

    const QRectF rect(option->rect);
    const qreal iconExtent = hasIcon ? qMin(option->iconSize.width() > 0 ? option->iconSize.width() : BUTTON_ICON, option->rect.height()) : 0.0;

    if (isIconOnly(option) && !hasMenu)
    {
        PDFWidgetStyleImpl::drawIcon(painter, PDFStylePainter::centered(rect, QSizeF(iconExtent, iconExtent)), option->icon, PDFWidgetStyleImpl::color(iconColor), state);
        return;
    }

    const qreal paddingLeft = BUTTON_PADDING_LEFT + border;
    const qreal paddingRight = (hasMenu ? BUTTON_PADDING_RIGHT_MENU : BUTTON_PADDING_RIGHT) + border;
    const qreal available = qMax(0.0, rect.width() - paddingLeft - paddingRight);
    const qreal menuWidth = hasMenu ? (option->text.isEmpty() && !hasIcon ? BUTTON_CHEVRON : BUTTON_GAP + BUTTON_CHEVRON) : 0.0;
    const qreal iconWidth = hasIcon ? iconExtent + (option->text.isEmpty() ? 0.0 : BUTTON_GAP) : 0.0;

    qreal textWidth = PDFWidgetStyleImpl::getTextWidth(option->text, font);
    QString text = option->text;

    if (iconWidth + textWidth + menuWidth > available)
    {
        textWidth = qMax(0.0, available - iconWidth - menuWidth);
        text = PDFWidgetStyleImpl::getElidedText(option->text, font, textWidth);
    }

    // The content is centered between the paddings
    const qreal contentWidth = iconWidth + textWidth + menuWidth;
    qreal x = rect.left() + paddingLeft + (available - contentWidth) / 2.0;
    x = qRound(x);

    if (hasIcon)
    {
        PDFWidgetStyleImpl::drawIcon(painter, placeInRow(rect, state.rightToLeft, x, iconExtent, iconExtent), option->icon, PDFWidgetStyleImpl::color(iconColor), state);
        x += iconWidth;
    }

    if (!text.isEmpty())
    {
        m_impl->drawText(painter, placeInRow(rect, state.rightToLeft, x, textWidth, rect.height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute, text, font, PDFWidgetStyleImpl::color(textColor), widget);
        x += textWidth;
    }

    if (hasMenu)
    {
        x += menuWidth - BUTTON_CHEVRON;
        PDFStylePainter::icon(painter, placeInRow(rect, state.rightToLeft, x, BUTTON_CHEVRON, BUTTON_CHEVRON), QStringLiteral("chevron-down"), iconColor);
    }
}

void PDFWidgetStyleButtons::drawIndicator(const QStyleOption* option, QPainter* painter, const QWidget* widget, bool isRadio) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF rect = PDFStylePainter::centered(QRectF(option->rect), QSizeF(INDICATOR, INDICATOR));
    const Radii radii(isRadio ? 9.0 : 5.0);
    const bool isChecked = state.on || state.partial;

    if (!isChecked)
    {
        // Hover and pressed lay the translucent overlay over the surface of the box,
        // so the box keeps its own surface also on a colored background
        const bool isActive = state.enabled && (state.hover || state.pressed);
        PDFStylePainter::fill(painter, rect, radii, state.enabled ? Color::BgSurface : Color::BgMuted);
        if (isActive)
        {
            PDFStylePainter::fill(painter, rect, radii, state.pressed ? Color::BgPressed : Color::BgHover);
        }

        if (!state.enabled)
        {
            PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.5);
        }
        else if (state.focusVisible)
        {
            // Unchecked indicator has the focus ring inside of its box
            PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderFocus, 2.0);
        }
        else if (isActive)
        {
            PDFStylePainter::strokeInside(painter, rect, radii, Color::IconMuted, 1.5);
        }
        else
        {
            // The radio button at rest has a thinner border
            PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderStrong, isRadio ? 1.0 : 1.5);
        }
        return;
    }

    Color fill = Color::BgAccent;
    if (!state.enabled)
    {
        fill = Color::BorderStrong;
    }
    else if (state.pressed)
    {
        fill = Color::BgAccentPressed;
    }
    else if (state.hover)
    {
        fill = Color::BgAccentHover;
    }

    PDFStylePainter::fill(painter, rect, radii, fill);

    if (isRadio)
    {
        PDFStylePainter::fillEllipse(painter, PDFStylePainter::centered(rect, QSizeF(8.0, 8.0)), PDFWidgetStyleImpl::color(Color::IconOnAccent));
    }
    else
    {
        PDFStylePainter::icon(painter, PDFStylePainter::centered(rect, QSizeF(12.0, 12.0)), state.partial ? QStringLiteral("minus") : QStringLiteral("check"), Color::IconOnAccent);
    }
}

void PDFWidgetStyleButtons::drawIndicatorLabel(const QStyleOptionButton* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::Body);
    QRectF rect(option->rect);

    if (!option->icon.isNull())
    {
        const qreal extent = option->iconSize.width() > 0 ? option->iconSize.width() : BUTTON_ICON;
        const QRectF iconRect(rect.left(), rect.top() + (rect.height() - extent) / 2.0, extent, extent);
        PDFWidgetStyleImpl::drawIcon(painter, PDFWidgetStyleImpl::visualRect(state.rightToLeft, rect, iconRect), option->icon,
                                     PDFWidgetStyleImpl::color(state.enabled ? Color::IconDefault : Color::IconDisabled), state);

        if (state.rightToLeft)
        {
            rect.setRight(rect.right() - extent - INDICATOR_GAP);
        }
        else
        {
            rect.setLeft(rect.left() + extent + INDICATOR_GAP);
        }
    }

    if (!option->text.isEmpty())
    {
        const QString text = PDFWidgetStyleImpl::getElidedText(option->text, font, rect.width());
        m_impl->drawText(painter, rect, Qt::AlignVCenter | PDFWidgetStyleImpl::getLeadingAlignment(state.rightToLeft), text, font,
                         PDFWidgetStyleImpl::color(state.enabled ? Color::TextPrimary : Color::TextDisabled), widget);
    }
}

void PDFWidgetStyleButtons::drawToolButton(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF rect(option->rect);
    const QRect buttonRect = style()->subControlRect(QStyle::CC_ToolButton, option, QStyle::SC_ToolButton, widget);
    const QRect menuRect = style()->subControlRect(QStyle::CC_ToolButton, option, QStyle::SC_ToolButtonMenu, widget);
    const bool isSplit = isSplitButton(option);
    const bool isAutoRaise = option->state.testFlag(QStyle::State_AutoRaise) || PDFWidgetStyleImpl::inherits(widget ? widget->parentWidget() : nullptr, "QToolBar");
    const bool isTextUnderIcon = option->toolButtonStyle == Qt::ToolButtonTextUnderIcon && !option->text.isEmpty();
    const bool isMenuActive = option->activeSubControls.testFlag(QStyle::SC_ToolButtonMenu);
    const bool isButtonActive = option->activeSubControls.testFlag(QStyle::SC_ToolButton);
    const bool isMenuOpen = !isSplit && option->features.testFlag(QStyleOptionToolButton::HasMenu) && state.pressed && isMenuActive;

    if (PDFWidgetStyleImpl::inherits(widget, "QLineEditIconButton"))
    {
        return;
    }

    if (!isAutoRaise && !isSidebarTab(widget) && !isTextUnderIcon)
    {
        // Framed tool button is the icon-only secondary button
        QStyleOptionButton buttonOption;
        buttonOption.QStyleOption::operator=(*option);
        drawButtonPanel(&buttonOption, painter, widget);
    }
    else if (isSplit)
    {
        const bool isAnyActive = state.enabled && (state.hover || state.pressed);

        // The two halves react separately, they share the rounded shape
        {
            const PDFStylePainterGuard guard(painter);
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setClipPath(PDFStylePainter::roundedPath(rect, Radii(TOOL_BUTTON_RADIUS)), Qt::IntersectClip);

            if (state.on)
            {
                painter->fillRect(rect, PDFWidgetStyleImpl::color(Color::BgAccentSubtle));
            }

            if (state.enabled && isButtonActive)
            {
                painter->fillRect(QRectF(buttonRect), PDFWidgetStyleImpl::color(state.pressed ? Color::BgPressed : Color::BgHover));
            }
            if (state.enabled && isMenuActive)
            {
                painter->fillRect(QRectF(menuRect), PDFWidgetStyleImpl::color(state.pressed ? Color::BgPressed : Color::BgHover));
            }
        }

        if (isAnyActive)
        {
            // Divider 1 x 20 between the halves
            const qreal x = state.rightToLeft ? menuRect.right() : menuRect.left() - 1.0;
            PDFStylePainter::fillRect(painter, QRectF(x + (state.rightToLeft ? 1.0 : 1.0) - 1.0, rect.top() + (rect.height() - 20.0) / 2.0, 1.0, 20.0), Color::BorderDivider);
        }
    }
    else if (isTextUnderIcon)
    {
        // Action tile has a neutral fill at rest
        const Radii radii(TOOL_BUTTON_RADIUS);
        if (state.on || isMenuOpen)
        {
            PDFStylePainter::fill(painter, rect, radii, state.hover ? Color::BgAccentSubtleHover : Color::BgAccentSubtle);
        }
        else
        {
            PDFStylePainter::fill(painter, rect, radii, Color::AccentNeutralBg);
            PDFWidgetStyleImpl::drawStateOverlay(painter, rect, radii, state);
        }

        // The target of an editor beside the tile (1295:31357): 1 px border/strong inside
        if (state.enabled && PDFWidgetStyleProperties::isSet(widget, PDFWidgetStyleProperties::EDIT_TARGET))
        {
            PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderStrong);
        }
    }
    else
    {
        QStyleOption panelOption = *option;
        if (isMenuOpen)
        {
            // The button of an open menu looks as the checked one
            panelOption.state |= QStyle::State_On;
            panelOption.state &= ~QStyle::State_Sunken;
        }
        style()->drawPrimitive(QStyle::PE_PanelButtonTool, &panelOption, painter, widget);
    }

    QStyleOptionToolButton labelOption = *option;
    labelOption.rect = buttonRect;
    if (isMenuOpen)
    {
        labelOption.state |= QStyle::State_On;
    }
    style()->drawControl(QStyle::CE_ToolButtonLabel, &labelOption, painter, widget);

    if (isSplit)
    {
        const Color chevronColor = state.enabled ? Color::IconDefault : Color::IconDisabled;
        PDFStylePainter::icon(painter, PDFStylePainter::centered(QRectF(menuRect), QSizeF(TOOL_BUTTON_CHEVRON, TOOL_BUTTON_CHEVRON)), QStringLiteral("chevron-down"), chevronColor);
    }
}

void PDFWidgetStyleButtons::drawToolButtonLabel(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF rect(option->rect);
    const bool isAutoRaise = option->state.testFlag(QStyle::State_AutoRaise) || PDFWidgetStyleImpl::inherits(widget ? widget->parentWidget() : nullptr, "QToolBar") || isSidebarTab(widget);
    const bool hasArrow = option->features.testFlag(QStyleOptionToolButton::Arrow) && option->arrowType != Qt::NoArrow;
    const bool hasIcon = hasArrow || !option->icon.isNull();
    bool hasIndicator = hasMenuIndicator(option, widget);

    Qt::ToolButtonStyle buttonStyle = option->toolButtonStyle;
    if (option->text.isEmpty() || (buttonStyle == Qt::ToolButtonFollowStyle))
    {
        buttonStyle = Qt::ToolButtonIconOnly;
    }
    if (!hasIcon && buttonStyle != Qt::ToolButtonIconOnly)
    {
        buttonStyle = Qt::ToolButtonTextOnly;
    }
    if (buttonStyle == Qt::ToolButtonTextBesideIcon)
    {
        // The menu button of a toolbar (an icon with a text) has no chevron
        hasIndicator = false;
    }
    if (!hasIcon && option->text.isEmpty())
    {
        return;
    }
    if (!hasIcon && buttonStyle == Qt::ToolButtonIconOnly)
    {
        buttonStyle = Qt::ToolButtonTextOnly;
    }

    Color textColor = Color::TextSecondary;
    Color iconColor = Color::IconDefault;

    if (!state.enabled)
    {
        textColor = Color::TextDisabled;
        iconColor = Color::IconDisabled;
    }
    else if (state.on)
    {
        textColor = Color::TextAccent;
        iconColor = Color::IconAccent;
    }
    else if (isAutoRaise && (state.hover || state.pressed))
    {
        iconColor = Color::IconStrong;
    }

    const qreal indicatorWidth = hasIndicator ? TOOL_BUTTON_CHEVRON_GAP + TOOL_BUTTON_CHEVRON : 0.0;
    const Color indicatorColor = !state.enabled ? Color::IconDisabled : (state.on ? Color::IconAccent : Color::IconMuted);

    switch (buttonStyle)
    {
        case Qt::ToolButtonTextUnderIcon:
        {
            // Action tile: icon 18 over the label Medium 11, paddings 10 / 2 / 8, gap 5
            const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::Caption);
            const qreal textHeight = QFontMetricsF(font).height();
            const qreal contentHeight = TOOL_BUTTON_TEXT_ICON + 5.0 + textHeight;
            const qreal top = rect.top() + 10.0 + qMax(0.0, (rect.height() - 18.0 - contentHeight) / 2.0);

            drawToolButtonGlyph(painter, QRectF(rect.left() + (rect.width() - TOOL_BUTTON_TEXT_ICON) / 2.0, top, TOOL_BUTTON_TEXT_ICON, TOOL_BUTTON_TEXT_ICON), option, hasArrow, iconColor, state);
            drawToolButtonColorBar(painter, QRectF(rect.left(), top + TOOL_BUTTON_TEXT_ICON, rect.width(), ACTION_TILE_COLOR_BAR_HEIGHT), widget, state);

            const QRectF textRect(rect.left() + 2.0, top + TOOL_BUTTON_TEXT_ICON + 5.0, rect.width() - 4.0, textHeight);
            const QString text = PDFWidgetStyleImpl::getElidedText(option->text, font, textRect.width());
            m_impl->drawText(painter, textRect, Qt::AlignCenter, text, font, PDFWidgetStyleImpl::color(textColor), widget);
            break;
        }

        case Qt::ToolButtonTextBesideIcon:
        case Qt::ToolButtonTextOnly:
        {
            // Toolbar menu button: paddings 10 / 12, icon 18, gap 8, label Medium 13
            const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::BodyMedium);
            const bool isTextOnly = buttonStyle == Qt::ToolButtonTextOnly;
            const qreal iconWidth = isTextOnly ? 0.0 : TOOL_BUTTON_TEXT_ICON + 8.0;
            const qreal available = qMax(0.0, rect.width() - 10.0 - 12.0);

            qreal textWidth = PDFWidgetStyleImpl::getTextWidth(option->text, font);
            QString text = option->text;
            if (iconWidth + textWidth + indicatorWidth > available)
            {
                textWidth = qMax(0.0, available - iconWidth - indicatorWidth);
                text = PDFWidgetStyleImpl::getElidedText(option->text, font, textWidth);
            }

            qreal x = qRound(rect.left() + 10.0 + (available - iconWidth - textWidth - indicatorWidth) / 2.0);

            if (!isTextOnly)
            {
                drawToolButtonGlyph(painter, placeInRow(rect, state.rightToLeft, x, TOOL_BUTTON_TEXT_ICON, TOOL_BUTTON_TEXT_ICON), option, hasArrow, iconColor, state);
                x += iconWidth;
            }

            m_impl->drawText(painter, placeInRow(rect, state.rightToLeft, x, textWidth, rect.height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::AlignAbsolute, text, font, PDFWidgetStyleImpl::color(textColor), widget);
            x += textWidth;

            if (hasIndicator)
            {
                PDFStylePainter::icon(painter, placeInRow(rect, state.rightToLeft, x + TOOL_BUTTON_CHEVRON_GAP, TOOL_BUTTON_CHEVRON, TOOL_BUTTON_CHEVRON), QStringLiteral("chevron-down"), indicatorColor);
            }
            break;
        }

        default:
        {
            qreal extent = option->iconSize.width() > 0 ? option->iconSize.width() : 16.0;
            if (isSidebarTab(widget))
            {
                extent = TOOL_BUTTON_TEXT_ICON;
            }
            extent = qMin(extent, qMin(rect.width(), rect.height()));

            qreal x = rect.left() + (rect.width() - extent - indicatorWidth) / 2.0;
            drawToolButtonGlyph(painter, placeInRow(rect, state.rightToLeft, x, extent, extent), option, hasArrow, iconColor, state);

            if (hasIndicator)
            {
                PDFStylePainter::icon(painter, placeInRow(rect, state.rightToLeft, x + extent + TOOL_BUTTON_CHEVRON_GAP, TOOL_BUTTON_CHEVRON, TOOL_BUTTON_CHEVRON), QStringLiteral("chevron-down"), indicatorColor);
            }
            break;
        }
    }
}

void PDFWidgetStyleButtons::drawToolButtonGlyph(QPainter* painter, const QRectF& iconRect, const QStyleOptionToolButton* option, bool hasArrow, Color iconColor, const PDFStyleState& state)
{
    if (hasArrow)
    {
        PDFStylePainter::icon(painter, iconRect, getArrowIconName(option->arrowType), iconColor);
    }
    else
    {
        PDFWidgetStyleImpl::drawIcon(painter, iconRect, option->icon, PDFWidgetStyleImpl::color(iconColor), state);
    }
}

void PDFWidgetStyleButtons::drawToolButtonColorBar(QPainter* painter, const QRectF& row, const QWidget* widget, const PDFStyleState& state)
{
    const QVariant value = widget ? widget->property(PDFWidgetStyleProperties::COLOR_BAR) : QVariant();
    if (!painter || value.typeId() != QMetaType::QColor)
    {
        return;
    }

    const QColor barColor = value.value<QColor>();
    if (!barColor.isValid())
    {
        return;
    }

    // A canonical annotation color (the light-theme value of a token, as the
    // color rows report it) is drawn as the token of the current theme
    QColor color = barColor;
    static const PDFThemeColors light;
    for (int token = PDFThemeColors::AnnotationYellow; token <= PDFThemeColors::AnnotationWhite; ++token)
    {
        if (light.getColor(static_cast<Color>(token)).rgb() == barColor.rgb())
        {
            color = PDFWidgetStyleImpl::color(static_cast<Color>(token));
            break;
        }
    }
    color.setAlpha(255);

    const QRectF barRect(qRound(row.left() + (row.width() - ACTION_TILE_COLOR_BAR_WIDTH) / 2.0), qRound(row.top()), ACTION_TILE_COLOR_BAR_WIDTH, ACTION_TILE_COLOR_BAR_HEIGHT);
    const Radii radii(ACTION_TILE_COLOR_BAR_RADIUS);
    if (!state.enabled)
    {
        PDFStylePainter::fill(painter, barRect, radii, Color::IconDisabled);
        return;
    }

    // A color, which cannot be told from the fill of the tile (drawToolButton:
    // bg/accent-subtle of a checked tile, accent/neutral/bg at rest - a translucent
    // fill lies over the bg/surface of the panel of the tiles), gets an outline
    const Color fill = state.on ? (state.hover ? Color::BgAccentSubtleHover : Color::BgAccentSubtle) : Color::AccentNeutralBg;
    const QColor fillColor = PDFWidgetStyleImpl::color(fill);
    const QColor surface = PDFWidgetStyleImpl::color(Color::BgSurface);
    const qreal alpha = fillColor.alphaF();
    const QColor background = QColor::fromRgbF(float(fillColor.redF() * alpha + surface.redF() * (1.0 - alpha)),
                                               float(fillColor.greenF() * alpha + surface.greenF() * (1.0 - alpha)),
                                               float(fillColor.blueF() * alpha + surface.blueF() * (1.0 - alpha)));
    if (getContrastRatio(color, background) < ACTION_TILE_COLOR_BAR_MINIMUM_CONTRAST)
    {
        PDFStylePainter::fill(painter, barRect.adjusted(-1.0, -1.0, 1.0, 1.0), radii.adjusted(-1.0), Color::BorderStrong);
    }
    PDFStylePainter::fill(painter, barRect, radii, color);
}

qreal PDFWidgetStyleButtons::getContrastRatio(const QColor& first, const QColor& second)
{
    const qreal a = getRelativeLuminance(first);
    const qreal b = getRelativeLuminance(second);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
}

qreal PDFWidgetStyleButtons::getRelativeLuminance(const QColor& color)
{
    return 0.2126 * getLinearChannel(color.redF()) + 0.7152 * getLinearChannel(color.greenF()) + 0.0722 * getLinearChannel(color.blueF());
}

qreal PDFWidgetStyleButtons::getLinearChannel(qreal value)
{
    return value <= 0.04045 ? value / 12.92 : qPow((value + 0.055) / 1.055, 2.4);
}

void PDFWidgetStyleButtons::drawToolBar(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const QRectF rect(option->rect);

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SIDEBAR_TABS))
    {
        // Row of the icon tabs of a sidebar has no border
        PDFStylePainter::fillRect(painter, rect, Color::BgSidebar);
        return;
    }

    PDFStylePainter::fillRect(painter, rect, Color::BgSubtle);

    // The border lies on the edge, which faces the content of the window
    Qt::ToolBarArea area = Qt::TopToolBarArea;
    if (const QStyleOptionToolBar* toolBar = qstyleoption_cast<const QStyleOptionToolBar*>(option))
    {
        area = toolBar->toolBarArea;
    }

    QRectF line;
    switch (area)
    {
        case Qt::BottomToolBarArea:
            line = QRectF(rect.left(), rect.top(), rect.width(), 1.0);
            break;
        case Qt::LeftToolBarArea:
            line = QRectF(rect.right() - 1.0, rect.top(), 1.0, rect.height());
            break;
        case Qt::RightToolBarArea:
            line = QRectF(rect.left(), rect.top(), 1.0, rect.height());
            break;
        default:
            line = QRectF(rect.left(), rect.bottom() - 1.0, rect.width(), 1.0);
            break;
    }

    PDFStylePainter::fillRect(painter, line, Color::BorderDefault);
}

void PDFWidgetStyleButtons::drawArrow(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);

    QString name;
    switch (element)
    {
        case QStyle::PE_IndicatorArrowUp:
            name = QStringLiteral("chevron-up");
            break;
        case QStyle::PE_IndicatorArrowDown:
            name = QStringLiteral("chevron-down");
            break;
        case QStyle::PE_IndicatorArrowLeft:
            name = QStringLiteral("chevron-left");
            break;
        default:
            name = QStringLiteral("chevron-right");
            break;
    }

    const QRectF rect(option->rect);
    const qreal extent = qMin(14.0, qMin(rect.width(), rect.height()));
    PDFStylePainter::icon(painter, PDFStylePainter::centered(rect, QSizeF(extent, extent)), name, state.enabled ? Color::IconDefault : Color::IconDisabled);
}

QSize PDFWidgetStyleButtons::getButtonSize(const QStyleOptionButton* option, const QWidget* widget) const
{
    const bool hasMenu = option->features.testFlag(QStyleOptionButton::HasMenu);
    const bool hasIcon = !option->icon.isNull();

    if (isIconOnly(option) && !hasMenu)
    {
        return QSize(BUTTON_HEIGHT, BUTTON_HEIGHT);
    }

    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::BodySemiBold);
    const int border = hasButtonBorder(option, widget) ? 1 : 0;

    int width = 2 * border + BUTTON_PADDING_LEFT + (hasMenu ? BUTTON_PADDING_RIGHT_MENU : BUTTON_PADDING_RIGHT);
    width += PDFWidgetStyleImpl::getTextWidth(option->text, font);

    if (hasIcon)
    {
        width += (option->iconSize.width() > 0 ? option->iconSize.width() : BUTTON_ICON) + (option->text.isEmpty() ? 0 : BUTTON_GAP);
    }
    if (hasMenu)
    {
        width += BUTTON_GAP + BUTTON_CHEVRON;
    }

    // A larger font set by the application makes the button higher
    return QSize(qMax(BUTTON_MIN_WIDTH, width), qMax(BUTTON_HEIGHT, QFontMetrics(font).height() + 14));
}

QSize PDFWidgetStyleButtons::getToolButtonSize(const QStyleOptionToolButton* option, const QWidget* widget) const
{
    if (isSidebarTab(widget))
    {
        return QSize(SIDEBAR_TAB, SIDEBAR_TAB);
    }

    const bool hasArrow = option->features.testFlag(QStyleOptionToolButton::Arrow) && option->arrowType != Qt::NoArrow;
    const bool hasIcon = hasArrow || !option->icon.isNull();
    const int indicatorWidth = hasMenuIndicator(option, widget) ? TOOL_BUTTON_CHEVRON_GAP + TOOL_BUTTON_CHEVRON : 0;
    const int splitWidth = option->features.testFlag(QStyleOptionToolButton::MenuButtonPopup) ? TOOL_BUTTON_SPLIT : 0;

    Qt::ToolButtonStyle buttonStyle = option->toolButtonStyle;
    if (option->text.isEmpty() || buttonStyle == Qt::ToolButtonFollowStyle)
    {
        buttonStyle = Qt::ToolButtonIconOnly;
    }
    if (!hasIcon)
    {
        buttonStyle = Qt::ToolButtonTextOnly;
    }

    switch (buttonStyle)
    {
        case Qt::ToolButtonTextUnderIcon:
        {
            const int textWidth = PDFWidgetStyleImpl::getTextWidth(option->text, PDFWidgetStyleImpl::getFont(widget, Font::Caption));
            return QSize(qMax(88, textWidth + 4), 56);
        }

        case Qt::ToolButtonTextBesideIcon:
        {
            const int textWidth = PDFWidgetStyleImpl::getTextWidth(option->text, PDFWidgetStyleImpl::getFont(widget, Font::BodyMedium));
            // The menu button of a toolbar (an icon with a text) has no chevron
            return QSize(10 + TOOL_BUTTON_TEXT_ICON + 8 + textWidth + 12 + splitWidth, TOOL_BUTTON);
        }

        case Qt::ToolButtonTextOnly:
        {
            const int textWidth = PDFWidgetStyleImpl::getTextWidth(option->text, PDFWidgetStyleImpl::getFont(widget, Font::BodyMedium));
            return QSize(qMax(TOOL_BUTTON, 10 + textWidth + indicatorWidth + 12) + splitWidth, TOOL_BUTTON);
        }

        default:
            break;
    }

    // The button grows with an icon larger than the icon of the design (the
    // size of the icons of a toolbar can be chosen by the user)
    const int extent = option->iconSize.width() > 0 ? option->iconSize.width() : 16;
    const int side = qMax(TOOL_BUTTON, extent + 2 * TOOL_BUTTON_PADDING);
    return QSize(side + indicatorWidth + splitWidth, qMax(TOOL_BUTTON, qMax(extent, option->iconSize.height()) + 2 * TOOL_BUTTON_PADDING));
}

}   // namespace pdf
