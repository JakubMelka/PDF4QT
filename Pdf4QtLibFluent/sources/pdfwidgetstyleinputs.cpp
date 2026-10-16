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
#include "pdfthemeiconengine.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QFrame>
#include <QLineEdit>
#include <QPainter>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionSpinBox>

namespace pdf
{

/// Line edits, spin boxes, date and time edits and combo boxes
/// (sections 04, 05 and 06 of the design).
class PDFWidgetStyleInputs : public PDFWidgetStyleArea
{
public:
    using PDFWidgetStyleArea::PDFWidgetStyleArea;

    virtual bool drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const override;
    virtual bool subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const override;
    virtual bool sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const override;

    // Geometry of the design
    static constexpr int FIELD_HEIGHT = 34;
    static constexpr int FIELD_HEIGHT_PILL = 36;
    static constexpr int FIELD_RADIUS = 8;
    static constexpr int FIELD_TEXT_LEFT = 13;          ///< Border 1 + padding 12
    static constexpr int FIELD_TEXT_LEFT_PILL = 16;
    static constexpr int LINE_EDIT_MARGIN = 2;          ///< Margin added by QLineEdit itself
    static constexpr int FIELD_GAP = 8;
    static constexpr int STEPPER_WIDTH = 20;
    static constexpr int STEPPER_RIGHT = 7;             ///< Border 1 + padding 6
    static constexpr int COMBO_CHEVRON = 14;
    static constexpr int COMBO_CHEVRON_RIGHT = 11;      ///< Border 1 + padding 10
    static constexpr int COMBO_ARROW_BUTTON = 24;
    static constexpr int COMBO_ARROW_BUTTON_RIGHT = 6;  ///< Border 1 + padding 5
    static constexpr int COMBO_HEIGHT_COMPACT = 28;
    static constexpr int COMBO_HEIGHT_SELECT = 32;
    static constexpr int CALENDAR_ICON = 16;

private:
    enum class ComboKind
    {
        Standard,
        Compact,
        Select
    };

    static bool isEmbeddedLineEdit(const QWidget* widget);
    static bool isItemViewEditor(const QWidget* widget);
    static bool isPill(const QWidget* widget);
    static bool isCalendarEdit(const QWidget* widget);
    static ComboKind getComboKind(const QWidget* widget);
    static int getAffixWidth(const QWidget* widget, const char* property);
    static void countIconButtons(const QWidget* widget, int& leftCount, int& rightCount);

    void drawSpinBox(const QStyleOptionSpinBox* option, QPainter* painter, const QWidget* widget) const;
    void drawComboBox(const QStyleOptionComboBox* option, QPainter* painter, const QWidget* widget) const;
    void drawComboBoxLabel(const QStyleOptionComboBox* option, QPainter* painter, const QWidget* widget) const;
    void drawSelectFrame(QPainter* painter, const QRectF& rect, const PDFFieldState& state) const;
    QRect getSpinBoxRect(const QStyleOptionSpinBox* option, QStyle::SubControl subControl, const QWidget* widget) const;
    QRect getComboBoxRect(const QStyleOptionComboBox* option, QStyle::SubControl subControl, const QWidget* widget) const;
};

std::unique_ptr<PDFWidgetStyleArea> PDFWidgetStyleAreas::createInputs(PDFWidgetStyleImpl* impl)
{
    return std::make_unique<PDFWidgetStyleInputs>(impl);
}

bool PDFWidgetStyleInputs::isEmbeddedLineEdit(const QWidget* widget)
{
    const QWidget* parent = widget ? widget->parentWidget() : nullptr;
    return parent && (qobject_cast<const QComboBox*>(parent) || qobject_cast<const QAbstractSpinBox*>(parent));
}

bool PDFWidgetStyleInputs::isItemViewEditor(const QWidget* widget)
{
    const QWidget* parent = widget ? widget->parentWidget() : nullptr;
    return parent && parent->objectName() == QLatin1String("qt_scrollarea_viewport");
}

bool PDFWidgetStyleInputs::isPill(const QWidget* widget)
{
    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::PILL);
}

bool PDFWidgetStyleInputs::isCalendarEdit(const QWidget* widget)
{
    const QDateTimeEdit* dateTimeEdit = qobject_cast<const QDateTimeEdit*>(widget);
    return dateTimeEdit && dateTimeEdit->calendarPopup();
}

PDFWidgetStyleInputs::ComboKind PDFWidgetStyleInputs::getComboKind(const QWidget* widget)
{
    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::COMPACT))
    {
        return ComboKind::Compact;
    }

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SELECT))
    {
        return ComboKind::Select;
    }

    return ComboKind::Standard;
}

int PDFWidgetStyleInputs::getAffixWidth(const QWidget* widget, const char* property)
{
    if (!widget)
    {
        return 0;
    }

    const int width = widget->property(property).toInt();
    return width > 0 ? width + FIELD_GAP : 0;
}

void PDFWidgetStyleInputs::countIconButtons(const QWidget* widget, int& leftCount, int& rightCount)
{
    leftCount = 0;
    rightCount = 0;

    if (!widget)
    {
        return;
    }

    // Buttons of the actions and the clear button of a line edit
    for (const QObject* child : widget->children())
    {
        const QWidget* button = qobject_cast<const QWidget*>(child);
        if (!button || !button->isVisibleTo(widget) || !button->inherits("QLineEditIconButton"))
        {
            continue;
        }

        if (button->geometry().center().x() < widget->width() / 2)
        {
            ++leftCount;
        }
        else
        {
            ++rightCount;
        }
    }
}

bool PDFWidgetStyleInputs::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::PE_PanelLineEdit:
        {
            if (isEmbeddedLineEdit(widget))
            {
                // The frame is drawn by the combo box or by the spin box
                return true;
            }

            const QStyleOptionFrame* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
            const bool hasFrame = !frame || frame->lineWidth > 0;
            const QRectF rect(option->rect);

            if (!hasFrame)
            {
                // Editor of a cell of a view paints nothing: the view paints the edited
                // cell (bg/surface and the focus ring) below the editor, so the cell has
                // one ring, whatever part of the cell the delegate gives to the editor
                return true;
            }

            const Radii radii(isPill(widget) ? rect.height() / 2.0 : FIELD_RADIUS);
            PDFWidgetStyleImpl::drawFieldFrame(painter, rect, radii, PDFWidgetStyleImpl::getFieldState(option, widget));
            return true;
        }

        case QStyle::PE_FrameLineEdit:
            // The frame is a part of the panel
            return true;

        case QStyle::PE_IndicatorSpinUp:
        case QStyle::PE_IndicatorSpinDown:
        case QStyle::PE_IndicatorSpinPlus:
        case QStyle::PE_IndicatorSpinMinus:
        {
            const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);

            QString name = QStringLiteral("chevron-up");
            if (element == QStyle::PE_IndicatorSpinDown)
            {
                name = QStringLiteral("chevron-down");
            }
            else if (element == QStyle::PE_IndicatorSpinPlus)
            {
                name = QStringLiteral("plus");
            }
            else if (element == QStyle::PE_IndicatorSpinMinus)
            {
                name = QStringLiteral("minus");
            }

            PDFStylePainter::icon(painter, PDFStylePainter::centered(QRectF(option->rect), QSizeF(12.0, 12.0)), name, isEnabled ? Color::IconDefault : Color::IconDisabled);
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleInputs::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::CE_ComboBoxLabel:
        {
            if (const QStyleOptionComboBox* comboBox = qstyleoption_cast<const QStyleOptionComboBox*>(option))
            {
                drawComboBoxLabel(comboBox, painter, widget);
            }
            return true;
        }

        case QStyle::CE_ShapedFrame:
        {
            if (PDFWidgetStyleImpl::inherits(widget, "QComboBoxPrivateContainer"))
            {
                // Panel of the popup list: radius 10, shadow 0 / 8 / 24
                PDFWidgetStyleImpl::drawPopupPanel(painter, option->rect, widget, PDFWidgetStyleImpl::PopupKind::Popover, 10.0, Color::BorderDefault);
                return true;
            }
            break;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleInputs::drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    switch (control)
    {
        case QStyle::CC_SpinBox:
        {
            if (const QStyleOptionSpinBox* spinBox = qstyleoption_cast<const QStyleOptionSpinBox*>(option))
            {
                drawSpinBox(spinBox, painter, widget);
            }
            return true;
        }

        case QStyle::CC_ComboBox:
        {
            if (const QStyleOptionComboBox* comboBox = qstyleoption_cast<const QStyleOptionComboBox*>(option))
            {
                drawComboBox(comboBox, painter, widget);
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleInputs::subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const
{
    switch (element)
    {
        case QStyle::SE_LineEditContents:
        {
            if (isEmbeddedLineEdit(widget))
            {
                // The position of the editor is given by the combo box or by the spin box
                rect = option->rect;
                return true;
            }

            const QVariant textInsets = widget ? widget->property(PDFWidgetStyleProperties::FIELD_TEXT_INSETS) : QVariant();
            if (textInsets.canConvert<QMargins>())
            {
                // The field places its own icons and buttons beside the text
                const QMargins insets = textInsets.value<QMargins>();
                rect = option->rect.adjusted(qMax(0, insets.left() - LINE_EDIT_MARGIN), 0, -qMax(0, insets.right() - LINE_EDIT_MARGIN), 0);
                return true;
            }

            const QStyleOptionFrame* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
            const bool hasFrame = !frame || frame->lineWidth > 0;

            int inset = FIELD_TEXT_LEFT - LINE_EDIT_MARGIN;
            if (isPill(widget))
            {
                inset = FIELD_TEXT_LEFT_PILL - LINE_EDIT_MARGIN;
            }
            else if (!hasFrame)
            {
                inset = isItemViewEditor(widget) ? 8 - LINE_EDIT_MARGIN : FIELD_TEXT_LEFT - 1 - LINE_EDIT_MARGIN;
            }

            // QLineEdit reserves the margin and the width of each of its icon buttons
            // (22 px) beside the text. The design has the icon of 16 px at the padding
            // of the field and the gap of 8 px (10 px in a pill) before the text.
            int leftCount = 0;
            int rightCount = 0;
            countIconButtons(widget, leftCount, rightCount);

            const int correction = isPill(widget) ? 9 : 8;
            const int leftInset = qMax(0, inset - correction * leftCount);
            const int rightInset = qMax(0, inset - correction * rightCount);

            rect = option->rect.adjusted(leftInset, 0, -rightInset, 0);
            return true;
        }

        case QStyle::SE_ShapedFrameContents:
        case QStyle::SE_FrameContents:
        {
            if (PDFWidgetStyleImpl::inherits(widget, "QComboBoxPrivateContainer"))
            {
                // Margins of the shadow, border 1 and padding 6. Rows of the list have
                // 1 px of the gap between them above and below.
                const QMargins margins = PDFWidgetStyleImpl::getPopupMargins(widget);
                rect = option->rect.marginsRemoved(margins).adjusted(7, 6, -7, -6);
                return true;
            }
            break;
        }

        case QStyle::SE_ComboBoxFocusRect:
            rect = option->rect;
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleInputs::subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const
{
    switch (control)
    {
        case QStyle::CC_SpinBox:
        {
            if (const QStyleOptionSpinBox* spinBox = qstyleoption_cast<const QStyleOptionSpinBox*>(option))
            {
                rect = getSpinBoxRect(spinBox, subControl, widget);
                return true;
            }
            break;
        }

        case QStyle::CC_ComboBox:
        {
            if (const QStyleOptionComboBox* comboBox = qstyleoption_cast<const QStyleOptionComboBox*>(option))
            {
                rect = getComboBoxRect(comboBox, subControl, widget);
                return true;
            }
            break;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleInputs::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const
{
    switch (type)
    {
        case QStyle::CT_LineEdit:
        {
            if (isEmbeddedLineEdit(widget))
            {
                size = contentsSize;
                return true;
            }

            const QStyleOptionFrame* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
            const bool hasFrame = !frame || frame->lineWidth > 0;

            if (!hasFrame && isItemViewEditor(widget))
            {
                size = contentsSize + QSize(2 * (8 - LINE_EDIT_MARGIN), 0);
                return true;
            }

            const bool pill = isPill(widget);
            const int inset = (pill ? FIELD_TEXT_LEFT_PILL : FIELD_TEXT_LEFT) - LINE_EDIT_MARGIN;
            int horizontal = 2 * inset;
            int height = pill ? FIELD_HEIGHT_PILL : FIELD_HEIGHT;

            const QVariant textInsets = widget ? widget->property(PDFWidgetStyleProperties::FIELD_TEXT_INSETS) : QVariant();
            if (textInsets.canConvert<QMargins>())
            {
                const QMargins insets = textInsets.value<QMargins>();
                horizontal = qMax(0, insets.left() - LINE_EDIT_MARGIN) + qMax(0, insets.right() - LINE_EDIT_MARGIN);
            }

            bool hasHeight = false;
            const int ownHeight = widget ? widget->property(PDFWidgetStyleProperties::FIELD_HEIGHT).toInt(&hasHeight) : 0;
            if (hasHeight && ownHeight > 0)
            {
                // The field of its own height keeps it (its font does not change)
                size = QSize(contentsSize.width() + horizontal, ownHeight);
                return true;
            }

            // A larger font set by the application makes the field higher
            size = QSize(contentsSize.width() + horizontal, qMax(height, contentsSize.height() + 12));
            return true;
        }

        case QStyle::CT_SpinBox:
        {
            const QStyleOptionSpinBox* spinBox = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
            const bool hasButtons = !spinBox || spinBox->buttonSymbols != QAbstractSpinBox::NoButtons;

            // Text + 12 + 8 + 26 (+ prefix and suffix drawn by the widget)
            int width = contentsSize.width() + FIELD_TEXT_LEFT - LINE_EDIT_MARGIN + STEPPER_RIGHT;
            if (isCalendarEdit(widget))
            {
                width += FIELD_GAP + CALENDAR_ICON;
            }
            else if (hasButtons)
            {
                width += FIELD_GAP + STEPPER_WIDTH;
            }

            width += getAffixWidth(widget, PDFWidgetStyleProperties::SUFFIX_WIDTH);
            width += getAffixWidth(widget, PDFWidgetStyleProperties::PREFIX_WIDTH);
            size = QSize(width, FIELD_HEIGHT);
            return true;
        }

        case QStyle::CT_ComboBox:
        {
            if (isCalendarEdit(widget))
            {
                // QDateTimeEdit asks for the width of an empty combo box, which is added to its text
                size = QSize(contentsSize.width() + FIELD_GAP + CALENDAR_ICON, FIELD_HEIGHT);
                return true;
            }

            const QStyleOptionComboBox* comboBox = qstyleoption_cast<const QStyleOptionComboBox*>(option);
            const bool hasIcon = comboBox && !comboBox->currentIcon.isNull();
            const bool isEditable = comboBox && comboBox->editable;

            switch (getComboKind(widget))
            {
                case ComboKind::Compact:
                    // Border 1 + padding 10, gap 6, chevron 12, padding 8 + border 1
                    size = QSize(contentsSize.width() + 11 + 6 + 12 + 9, COMBO_HEIGHT_COMPACT);
                    return true;

                case ComboKind::Select:
                {
                    const QString prefix = widget ? widget->property(PDFWidgetStyleProperties::SELECT_PREFIX).toString() : QString();
                    int width = FIELD_TEXT_LEFT + contentsSize.width() + FIELD_GAP + COMBO_CHEVRON + COMBO_CHEVRON_RIGHT;
                    if (!prefix.isEmpty())
                    {
                        width += PDFWidgetStyleImpl::getTextWidth(prefix, PDFWidgetStyleImpl::getFont(widget, Font::Small)) + FIELD_GAP;
                    }
                    size = QSize(width, COMBO_HEIGHT_SELECT);
                    return true;
                }

                case ComboKind::Standard:
                    break;
            }

            int width = FIELD_TEXT_LEFT + contentsSize.width() + FIELD_GAP;
            width += isEditable ? COMBO_ARROW_BUTTON + COMBO_ARROW_BUTTON_RIGHT : COMBO_CHEVRON + COMBO_CHEVRON_RIGHT;
            if (hasIcon)
            {
                // QComboBox counts the icon with a gap of 4 px, the design has 8 px
                width += FIELD_GAP - 4;
            }

            size = QSize(width, FIELD_HEIGHT);
            return true;
        }

        default:
            break;
    }

    return false;
}

QRect PDFWidgetStyleInputs::getSpinBoxRect(const QStyleOptionSpinBox* option, QStyle::SubControl subControl, const QWidget* widget) const
{
    const QRect rect = option->rect;
    const bool hasButtons = option->buttonSymbols != QAbstractSpinBox::NoButtons;
    const int stepperHeight = qMax(0, rect.height() - 2);
    const int halfHeight = stepperHeight / 2;
    const int stepperLeft = rect.right() + 1 - STEPPER_RIGHT - STEPPER_WIDTH;

    QRect result;

    switch (subControl)
    {
        case QStyle::SC_SpinBoxFrame:
            return rect;

        case QStyle::SC_SpinBoxUp:
            if (!hasButtons)
            {
                return QRect();
            }
            result = QRect(stepperLeft, rect.top() + 1, STEPPER_WIDTH, halfHeight);
            break;

        case QStyle::SC_SpinBoxDown:
            if (!hasButtons)
            {
                return QRect();
            }
            result = QRect(stepperLeft, rect.top() + 1 + halfHeight, STEPPER_WIDTH, stepperHeight - halfHeight);
            break;

        case QStyle::SC_SpinBoxEditField:
        {
            // QLineEdit adds its own margin of 2 px before the text
            int left = rect.left() + FIELD_TEXT_LEFT - LINE_EDIT_MARGIN + getAffixWidth(widget, PDFWidgetStyleProperties::PREFIX_WIDTH);
            int right = rect.right() + 1 - STEPPER_RIGHT;
            if (hasButtons)
            {
                right -= STEPPER_WIDTH + FIELD_GAP;
            }
            right -= getAffixWidth(widget, PDFWidgetStyleProperties::SUFFIX_WIDTH);
            result = QRect(left, rect.top() + 1, qMax(0, right - left), rect.height() - 2);
            break;
        }

        default:
            return QRect();
    }

    return PDFWidgetStyleImpl::visualRect(option, rect, result);
}

QRect PDFWidgetStyleInputs::getComboBoxRect(const QStyleOptionComboBox* option, QStyle::SubControl subControl, const QWidget* widget) const
{
    const QRect rect = option->rect;
    const bool isCalendar = isCalendarEdit(widget);
    const ComboKind kind = getComboKind(widget);

    // Rectangle of the arrow: the chevron, the arrow button of an editable box, or the calendar icon
    QRect arrow;
    if (isCalendar)
    {
        arrow = QRect(rect.right() + 1 - STEPPER_RIGHT - CALENDAR_ICON, rect.top() + (rect.height() - CALENDAR_ICON) / 2, CALENDAR_ICON, CALENDAR_ICON);
    }
    else if (option->editable)
    {
        arrow = QRect(rect.right() + 1 - COMBO_ARROW_BUTTON_RIGHT - COMBO_ARROW_BUTTON, rect.top() + (rect.height() - COMBO_ARROW_BUTTON) / 2, COMBO_ARROW_BUTTON, COMBO_ARROW_BUTTON);
    }
    else if (kind == ComboKind::Compact)
    {
        arrow = QRect(rect.right() + 1 - 9 - 12, rect.top() + (rect.height() - 12) / 2, 12, 12);
    }
    else
    {
        arrow = QRect(rect.right() + 1 - COMBO_CHEVRON_RIGHT - COMBO_CHEVRON, rect.top() + (rect.height() - COMBO_CHEVRON) / 2, COMBO_CHEVRON, COMBO_CHEVRON);
    }

    QRect result;

    switch (subControl)
    {
        case QStyle::SC_ComboBoxFrame:
            return rect;

        case QStyle::SC_ComboBoxArrow:
            result = arrow;
            break;

        case QStyle::SC_ComboBoxEditField:
        {
            const bool hasEditor = option->editable || isCalendar;
            int left = rect.left() + (kind == ComboKind::Compact ? 11 : FIELD_TEXT_LEFT);
            if (hasEditor)
            {
                left -= LINE_EDIT_MARGIN;
            }
            const int right = arrow.left() - (kind == ComboKind::Compact ? 6 : FIELD_GAP);
            result = QRect(left, rect.top() + 1, qMax(0, right - left), rect.height() - 2);
            break;
        }

        case QStyle::SC_ComboBoxListBoxPopup:
        {
            // The popup is 4 px below the box (above it, if there is no room) and it
            // has the width of the box. The window of the popup is larger by the
            // margins of its shadow, only the corners of this rectangle are used.
            const QMargins margins = PDFWidgetStyleImpl::getShadowMarginsIfTranslucent(PDFWidgetStyleImpl::PopupKind::Popover);
            const int gap = 4;
            return QRect(QPoint(rect.left() - margins.left(), rect.top() - gap + margins.bottom()),
                         QPoint(rect.right() + margins.right(), rect.bottom() + 1 + gap - margins.top()));
        }

        default:
            return QRect();
    }

    return PDFWidgetStyleImpl::visualRect(option, rect, result);
}

void PDFWidgetStyleInputs::drawSpinBox(const QStyleOptionSpinBox* option, QPainter* painter, const QWidget* widget) const
{
    const QRectF rect(option->rect);

    if (option->frame && option->subControls.testFlag(QStyle::SC_SpinBoxFrame))
    {
        PDFWidgetStyleImpl::drawFieldFrame(painter, rect, Radii(FIELD_RADIUS), PDFWidgetStyleImpl::getFieldState(option, widget));
    }

    if (option->buttonSymbols == QAbstractSpinBox::NoButtons)
    {
        return;
    }

    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool isPlusMinus = option->buttonSymbols == QAbstractSpinBox::PlusMinus;

    auto drawHalf = [&](QStyle::SubControl subControl, QAbstractSpinBox::StepEnabledFlag step, QStyle::PrimitiveElement glyph)
    {
        if (!option->subControls.testFlag(subControl))
        {
            return;
        }

        const QRect halfRect = style()->subControlRect(QStyle::CC_SpinBox, option, subControl, widget);
        const bool isStepEnabled = isEnabled && option->stepEnabled.testFlag(step);
        const bool isActive = option->activeSubControls.testFlag(subControl);

        if (isStepEnabled && isActive)
        {
            if (option->state.testFlag(QStyle::State_Sunken))
            {
                PDFStylePainter::fill(painter, QRectF(halfRect), Radii(4.0), Color::BgPressed);
            }
            else if (option->state.testFlag(QStyle::State_MouseOver))
            {
                PDFStylePainter::fill(painter, QRectF(halfRect), Radii(4.0), Color::BgHover);
            }
        }

        QStyleOption glyphOption = *option;
        glyphOption.rect = halfRect;
        glyphOption.state.setFlag(QStyle::State_Enabled, isStepEnabled);
        style()->drawPrimitive(glyph, &glyphOption, painter, widget);
    };

    drawHalf(QStyle::SC_SpinBoxUp, QAbstractSpinBox::StepUpEnabled, isPlusMinus ? QStyle::PE_IndicatorSpinPlus : QStyle::PE_IndicatorSpinUp);
    drawHalf(QStyle::SC_SpinBoxDown, QAbstractSpinBox::StepDownEnabled, isPlusMinus ? QStyle::PE_IndicatorSpinMinus : QStyle::PE_IndicatorSpinDown);
}

void PDFWidgetStyleInputs::drawComboBox(const QStyleOptionComboBox* option, QPainter* painter, const QWidget* widget) const
{
    const QRectF rect(option->rect);
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool isCalendar = isCalendarEdit(widget);
    const bool isOpen = !option->editable && !isCalendar && (option->state.testFlag(QStyle::State_On) || option->state.testFlag(QStyle::State_Sunken));
    const QRect arrow = style()->subControlRect(QStyle::CC_ComboBox, option, QStyle::SC_ComboBoxArrow, widget);

    if (option->frame && option->subControls.testFlag(QStyle::SC_ComboBoxFrame))
    {
        PDFFieldState fieldState = PDFWidgetStyleImpl::getFieldState(option, widget);
        fieldState.open = isOpen;
        fieldState.readOnly = false;

        if (getComboKind(widget) == ComboKind::Select && !option->editable && !isCalendar)
        {
            drawSelectFrame(painter, rect, fieldState);
        }
        else
        {
            PDFWidgetStyleImpl::drawFieldFrame(painter, rect, Radii(FIELD_RADIUS), fieldState);
        }
    }

    if (!option->subControls.testFlag(QStyle::SC_ComboBoxArrow))
    {
        return;
    }

    if (isCalendar)
    {
        PDFStylePainter::icon(painter, QRectF(arrow), QStringLiteral("calendar"), isEnabled ? Color::IconDefault : Color::IconDisabled);
        return;
    }

    if (option->editable)
    {
        // The arrow of an editable box is a button of its own
        const bool isArrowActive = isEnabled && option->activeSubControls.testFlag(QStyle::SC_ComboBoxArrow);
        const bool isPopupShown = isEnabled && option->state.testFlag(QStyle::State_On);

        if ((isArrowActive && option->state.testFlag(QStyle::State_Sunken)) || isPopupShown)
        {
            PDFStylePainter::fill(painter, QRectF(arrow), Radii(6.0), Color::BgPressed);
        }
        else if (isArrowActive && option->state.testFlag(QStyle::State_MouseOver))
        {
            PDFStylePainter::fill(painter, QRectF(arrow), Radii(6.0), Color::BgHover);
        }

        PDFStylePainter::icon(painter, PDFStylePainter::centered(QRectF(arrow), QSizeF(COMBO_CHEVRON, COMBO_CHEVRON)), QStringLiteral("chevron-down"), isEnabled ? Color::IconDefault : Color::IconDisabled);
        return;
    }

    const Color chevronColor = isEnabled ? Color::IconDefault : Color::IconDisabled;
    PDFStylePainter::icon(painter, QRectF(arrow), QStringLiteral("chevron-down"), chevronColor);
}

void PDFWidgetStyleInputs::drawSelectFrame(QPainter* painter, const QRectF& rect, const PDFFieldState& state) const
{
    // Compact select: the state is the fill (bg/hover, bg/pressed), not the border
    // of the combo box. The focus ring of 2 px lies inside in place of the border.
    const Radii radii(FIELD_RADIUS);
    PDFStylePainter::fill(painter, rect, radii, state.enabled ? Color::BgSurface : Color::BgMuted);

    const bool isPressed = state.enabled && state.open;
    if (isPressed)
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgPressed);
    }
    else if (state.enabled && state.hover)
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgHover);
    }

    if (state.enabled && state.focus)
    {
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderFocus, 2.0);
    }
    else
    {
        PDFStylePainter::strokeInside(painter, rect, radii, isPressed ? Color::BorderStrong : Color::BorderDefault, 1.0);
    }
}

void PDFWidgetStyleInputs::drawComboBoxLabel(const QStyleOptionComboBox* option, QPainter* painter, const QWidget* widget) const
{
    if (option->editable)
    {
        // The text is drawn by the editor
        return;
    }

    const PDFStyleState state = m_impl->getState(option, widget);
    const ComboKind kind = getComboKind(widget);
    QRectF rect(style()->subControlRect(QStyle::CC_ComboBox, option, QStyle::SC_ComboBoxEditField, widget));

    const QComboBox* comboBox = qobject_cast<const QComboBox*>(widget);
    const bool isPlaceholder = comboBox && comboBox->currentIndex() < 0 && !comboBox->placeholderText().isEmpty();

    auto takeLeft = [&rect, &state](qreal width, qreal gap)
    {
        QRectF item;
        if (state.rightToLeft)
        {
            item = QRectF(rect.right() - width, rect.top(), width, rect.height());
            rect.setRight(rect.right() - width - gap);
        }
        else
        {
            item = QRectF(rect.left(), rect.top(), width, rect.height());
            rect.setLeft(rect.left() + width + gap);
        }
        return item;
    };

    const int alignment = Qt::AlignVCenter | (state.rightToLeft ? Qt::AlignRight : Qt::AlignLeft);

    if (kind == ComboKind::Select)
    {
        // Select: muted prefix and the value in Semi Bold 12
        const QString prefix = widget ? widget->property(PDFWidgetStyleProperties::SELECT_PREFIX).toString() : QString();
        if (!prefix.isEmpty())
        {
            const QFont prefixFont = PDFWidgetStyleImpl::getFont(widget, Font::Small);
            const QRectF prefixRect = takeLeft(PDFWidgetStyleImpl::getTextWidth(prefix, prefixFont), FIELD_GAP);
            PDFStylePainter::text(painter, prefixRect, alignment | Qt::TextSingleLine, prefix, prefixFont, PDFWidgetStyleImpl::color(state.enabled ? Color::TextMuted : Color::TextDisabled));
        }

        const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::SmallSemiBold);
        const QString text = PDFWidgetStyleImpl::getElidedText(option->currentText, font, rect.width());
        PDFStylePainter::text(painter, rect, alignment | Qt::TextSingleLine, text, font, PDFWidgetStyleImpl::color(state.enabled ? Color::TextSecondary : Color::TextDisabled));
        return;
    }

    if (!option->currentIcon.isNull())
    {
        const qreal extent = kind == ComboKind::Compact ? 14.0 : 16.0;
        const QRectF iconRect = PDFStylePainter::centered(takeLeft(extent, kind == ComboKind::Compact ? 6.0 : FIELD_GAP), QSizeF(extent, extent));
        PDFWidgetStyleImpl::drawIcon(painter, iconRect, option->currentIcon, PDFWidgetStyleImpl::color(state.enabled ? Color::IconDefault : Color::IconDisabled), PDFStyleState{ state.enabled });
    }

    const QFont font = PDFWidgetStyleImpl::getFont(widget, kind == ComboKind::Compact ? Font::Small : Font::Body);
    const QString text = PDFWidgetStyleImpl::getElidedText(option->currentText, font, rect.width());

    Color textColor = Color::TextPrimary;
    if (!state.enabled)
    {
        textColor = Color::TextDisabled;
    }
    else if (isPlaceholder)
    {
        textColor = Color::TextMuted;
    }

    PDFStylePainter::text(painter, rect, alignment | Qt::TextSingleLine, text, font, PDFWidgetStyleImpl::color(textColor));
}

}   // namespace pdf
