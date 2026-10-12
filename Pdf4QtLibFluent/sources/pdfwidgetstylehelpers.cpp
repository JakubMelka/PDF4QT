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

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFocusFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStyleOptionGroupBox>
#include <QTabBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolBar>
#include <QToolBox>
#include <QToolButton>

namespace pdf
{

PDFFieldState PDFWidgetStyleImpl::getFieldState(const QStyleOption* option, const QWidget* widget)
{
    PDFFieldState state;

    if (!option)
    {
        return state;
    }

    state.enabled = option->state.testFlag(QStyle::State_Enabled);
    state.hover = state.enabled && option->state.testFlag(QStyle::State_MouseOver);
    state.focus = state.enabled && option->state.testFlag(QStyle::State_HasFocus);
    state.readOnly = option->state.testFlag(QStyle::State_ReadOnly);
    state.invalid = hasProperty(widget, PDFWidgetStyleProperties::INVALID);

    if (widget)
    {
        // Line edits and text editors have the property, the option of a frame does not have the state
        const QVariant readOnly = widget->property("readOnly");
        if (readOnly.isValid() && readOnly.toBool() && !widget->inherits("QAbstractSpinBox"))
        {
            state.readOnly = true;
        }
    }

    return state;
}

void PDFWidgetStyleImpl::drawFieldFrame(QPainter* painter, const QRectF& rect, const Radii& radii, const PDFFieldState& state)
{
    Color fill = Color::BgSurface;
    if (!state.enabled)
    {
        fill = Color::BgMuted;
    }
    else if (state.readOnly || state.open)
    {
        fill = Color::BgSubtle;
    }

    PDFStylePainter::fill(painter, rect, radii, fill);

    if (state.invalid && state.enabled)
    {
        PDFStylePainter::strokeInside(painter, rect, radii, Color::TextDanger, state.focus ? 2.0 : 1.0);
    }
    else if (state.focus)
    {
        // The focus ring replaces the border
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderFocus, 2.0);
    }
    else if (state.hover || state.open)
    {
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderStrong, 1.0);
    }
    else
    {
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
    }
}

bool PDFWidgetStyleImpl::getFocusRingShape(const QStyle* style, const QWidget* target, const QRectF& targetRect, QRectF& shape, Radii& radii)
{
    if (!target)
    {
        return false;
    }

    shape = targetRect;

    if (const QAbstractButton* button = qobject_cast<const QAbstractButton*>(target))
    {
        const bool isCheckBox = qobject_cast<const QCheckBox*>(target) != nullptr;
        const bool isRadioButton = qobject_cast<const QRadioButton*>(target) != nullptr;

        if (isCheckBox || isRadioButton)
        {
            // Checked indicator has the ring around its box, the unchecked one inside of it
            const QCheckBox* checkBox = qobject_cast<const QCheckBox*>(target);
            const bool isChecked = button->isChecked() || (checkBox && checkBox->checkState() != Qt::Unchecked);
            if (!isChecked)
            {
                return false;
            }

            QStyleOptionButton option;
            option.initFrom(target);
            const QRect indicator = style->subElementRect(isRadioButton ? QStyle::SE_RadioButtonIndicator : QStyle::SE_CheckBoxIndicator, &option, target);
            shape = QRectF(indicator).translated(targetRect.topLeft());
            radii = Radii(isRadioButton ? 9.0 : 5.0);
            return true;
        }

        if (qobject_cast<const QPushButton*>(target))
        {
            radii = Radii(hasProperty(target, PDFWidgetStyleProperties::CHIP) ? 16.0 : 8.0);
            return true;
        }

        if (const QToolButton* toolButton = qobject_cast<const QToolButton*>(target))
        {
            qreal toolRadius = 10.0;
            const QVariant ownRadius = target->property(PDFWidgetStyleProperties::RADIUS);

            if (ownRadius.isValid())
            {
                toolRadius = ownRadius.toReal();
            }
            else if (hasProperty(target, PDFWidgetStyleProperties::CHIP))
            {
                toolRadius = 16.0;
            }
            else if (hasProperty(target, PDFWidgetStyleProperties::SEGMENT))
            {
                toolRadius = 14.0;
            }
            else if (hasProperty(target, PDFWidgetStyleProperties::STATUS_ITEM) || qobject_cast<const QDockWidget*>(target->parentWidget()))
            {
                toolRadius = 6.0;
            }
            else if (hasProperty(target, PDFWidgetStyleProperties::NEW_TAB) || qobject_cast<const QTabBar*>(target->parentWidget()))
            {
                toolRadius = 4.0;
            }
            else if (hasProperty(target, PDFWidgetStyleProperties::SIDEBAR_TAB) || hasProperty(target->parentWidget(), PDFWidgetStyleProperties::SIDEBAR_TABS))
            {
                toolRadius = 8.0;
            }
            else if (!toolButton->autoRaise() && !inherits(target->parentWidget(), "QToolBar"))
            {
                toolRadius = 8.0;
            }

            radii = Radii(toolRadius);
            return true;
        }

        radii = Radii(8.0);
        return true;
    }

    if (const QGroupBox* groupBox = qobject_cast<const QGroupBox*>(target))
    {
        // The check box of a group box: the checked one has the ring around
        // its box, the unchecked one inside of it (drawn with the indicator)
        if (!groupBox->isCheckable() || !groupBox->isChecked())
        {
            return false;
        }

        QStyleOptionGroupBox option;
        option.initFrom(target);
        option.text = groupBox->title();
        option.subControls = QStyle::SC_GroupBoxFrame | QStyle::SC_GroupBoxCheckBox;
        if (!groupBox->title().isEmpty())
        {
            option.subControls |= QStyle::SC_GroupBoxLabel;
        }
        if (groupBox->isFlat())
        {
            option.features |= QStyleOptionFrame::Flat;
        }

        const QRect indicator = style->subControlRect(QStyle::CC_GroupBox, &option, QStyle::SC_GroupBoxCheckBox, target);
        shape = QRectF(indicator).translated(targetRect.topLeft());
        radii = Radii(5.0);
        return true;
    }

    const QVariant radius = target->property(PDFWidgetStyleProperties::RADIUS);
    if (radius.isValid())
    {
        // Widget, which does not draw its ring: the ring is 2 px away from the widget
        shape = targetRect.adjusted(-2.0, -2.0, 2.0, 2.0);
        radii = Radii(radius.toReal() + 2.0);
        return true;
    }

    return false;
}

bool PDFWidgetStyleImpl::isFocusRingShapeAccent(const QWidget* target)
{
    if (!target || !target->isEnabled())
    {
        return false;
    }

    if (const QCheckBox* checkBox = qobject_cast<const QCheckBox*>(target))
    {
        return checkBox->checkState() != Qt::Unchecked;
    }

    if (const QRadioButton* radioButton = qobject_cast<const QRadioButton*>(target))
    {
        return radioButton->isChecked();
    }

    if (const QGroupBox* groupBox = qobject_cast<const QGroupBox*>(target))
    {
        return groupBox->isCheckable() && groupBox->isChecked();
    }

    if (const QPushButton* pushButton = qobject_cast<const QPushButton*>(target))
    {
        return !pushButton->isDown() && !pushButton->isChecked() && !hasProperty(target, PDFWidgetStyleProperties::CHIP) &&
               !hasProperty(target, PDFWidgetStyleProperties::DESTRUCTIVE) && (pushButton->isDefault() || hasProperty(target, PDFWidgetStyleProperties::PRIMARY));
    }

    return false;
}

int PDFWidgetStyleImpl::getFocusFrameMargin(const QWidget* focusFrame)
{
    // Controls have the ring flush with their shape. A widget, which only says
    // its radius, has the ring 2 px away.
    const QFocusFrame* frame = qobject_cast<const QFocusFrame*>(focusFrame);
    const QWidget* target = frame ? frame->widget() : nullptr;

    if (target && target->property(PDFWidgetStyleProperties::RADIUS).isValid() && !qobject_cast<const QAbstractButton*>(target))
    {
        return 4;
    }

    return 2;
}

QMargins PDFWidgetStyleImpl::getShadowMarginsIfTranslucent(PopupKind kind)
{
    return isTranslucencyAvailable() ? getShadowMargins(kind) : QMargins();
}

bool PDFWidgetStyleImpl::needsHover(const QWidget* widget)
{
    if (!widget)
    {
        return false;
    }

    // Controls with a look of the hovered state
    if (qobject_cast<const QAbstractButton*>(widget) || qobject_cast<const QComboBox*>(widget) || qobject_cast<const QAbstractSpinBox*>(widget) ||
        qobject_cast<const QLineEdit*>(widget) || qobject_cast<const QTextEdit*>(widget) || qobject_cast<const QPlainTextEdit*>(widget) ||
        qobject_cast<const QAbstractSlider*>(widget) || qobject_cast<const QTabBar*>(widget) || qobject_cast<const QHeaderView*>(widget) ||
        qobject_cast<const QGroupBox*>(widget) || widget->inherits("QSplitterHandle"))
    {
        return true;
    }

    // Rows of item views are hovered in their viewports
    const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(widget->parentWidget());
    return view && view->viewport() == widget;
}

bool PDFWidgetStyleImpl::needsStyledBackground(const QWidget* widget)
{
    if (!widget || widget->testAttribute(Qt::WA_TranslucentBackground))
    {
        return false;
    }

    return widget->isWindow() ||
           widget->autoFillBackground() ||
           hasProperty(widget, PDFWidgetStyleProperties::CARD) ||
           hasProperty(widget, PDFWidgetStyleProperties::NOTE) ||
           hasProperty(widget, PDFWidgetStyleProperties::CANVAS) ||
           hasProperty(widget, PDFWidgetStyleProperties::PANEL) ||
           hasProperty(widget, PDFWidgetStyleProperties::SIDEBAR) ||
           hasProperty(widget, PDFWidgetStyleProperties::OVERLAY) ||
           hasProperty(widget, PDFWidgetStyleProperties::DOCUMENT_TABS) ||
           qobject_cast<const QToolBox*>(widget) ||
           qobject_cast<const QDockWidget*>(widget);
}

QMargins PDFWidgetStyleImpl::getToolBarPaddings(const QToolBar* toolBar)
{
    // Paddings of the panel: 12 along the bar, 8 across it (8 + 36 + 8 = 52).
    // The row of the icon tabs of a sidebar has the paddings 10 / 12 / 6 / 12.
    if (hasProperty(toolBar, PDFWidgetStyleProperties::SIDEBAR_TABS))
    {
        return QMargins(12, 10, 12, 6);
    }

    if (toolBar && toolBar->orientation() == Qt::Vertical)
    {
        return QMargins(8, 12, 8, 12);
    }

    return QMargins(12, 8, 12, 8);
}

void PDFWidgetStyleImpl::applyToolBarMargins(QToolBar* toolBar)
{
    QLayout* layout = toolBar ? toolBar->layout() : nullptr;
    if (!layout)
    {
        return;
    }

    // The layout of a toolbar (QToolBarLayout) is written for the same margin at
    // all sides. It takes the sizes from the sums of the opposite margins, but
    // the position of the items from the top margin along the x axis and from
    // the left margin along the y axis. The margins are set so, that the items
    // lie at the paddings of the design and the sums are kept. The unit test of
    // the style checks the geometry of the items against the paddings.
    const QMargins paddings = getToolBarPaddings(toolBar);
    const QMargins margins(paddings.top(), paddings.left(),
                           paddings.left() + paddings.right() - paddings.top(),
                           paddings.top() + paddings.bottom() - paddings.left());

    if (layout->contentsMargins() != margins)
    {
        layout->setContentsMargins(margins);
    }
}

void PDFWidgetStyleImpl::placeToolBarExtension(QWidget* extension)
{
    // The extension button is placed by the margins of the layout, see applyToolBarMargins
    const QToolBar* toolBar = extension ? qobject_cast<const QToolBar*>(extension->parentWidget()) : nullptr;
    if (!toolBar)
    {
        return;
    }

    const QMargins paddings = getToolBarPaddings(toolBar);
    const QRect rect = toolBar->rect();
    QRect geometry = extension->geometry();

    // Both coordinates are set: the layout takes the position across the bar
    // from other margins in the bottom and in the right area of a main window
    if (toolBar->orientation() == Qt::Horizontal)
    {
        const bool isRightToLeft = toolBar->layoutDirection() == Qt::RightToLeft;
        geometry.moveLeft(isRightToLeft ? rect.left() + paddings.left() : rect.right() + 1 - paddings.right() - geometry.width());
        geometry.moveTop(rect.top() + paddings.top());
    }
    else
    {
        geometry.moveTop(rect.bottom() + 1 - paddings.bottom() - geometry.height());
        geometry.moveLeft(rect.left() + paddings.left());
    }

    if (extension->geometry() != geometry)
    {
        extension->setGeometry(geometry);
    }
}

bool PDFWidgetStyleImpl::navigateVerticalTabBar(QObject* watched, int key)
{
    // A tab bar handles only the left and right arrows, rows of a column are
    // walked by the up and down arrows
    QTabBar* tabBar = qobject_cast<QTabBar*>(watched);
    if (!tabBar || (key != Qt::Key_Up && key != Qt::Key_Down))
    {
        return false;
    }

    const QTabBar::Shape shape = tabBar->shape();
    const bool isVertical = shape == QTabBar::RoundedWest || shape == QTabBar::RoundedEast || shape == QTabBar::TriangularWest || shape == QTabBar::TriangularEast;
    if (!isVertical)
    {
        return false;
    }

    const int step = key == Qt::Key_Down ? 1 : -1;
    for (int index = tabBar->currentIndex() + step; index >= 0 && index < tabBar->count(); index += step)
    {
        if (tabBar->isTabEnabled(index) && tabBar->isTabVisible(index))
        {
            tabBar->setCurrentIndex(index);
            break;
        }
    }

    return true;
}

bool PDFWidgetStyleImpl::paintLineEditButton(QObject* watched)
{
    // Buttons inside of a line edit (clear button, actions) draw their icon
    // without the style. They are drawn here: muted icon, default color on hover.
    QToolButton* button = qobject_cast<QToolButton*>(watched);
    if (!button || !button->inherits("QLineEditIconButton"))
    {
        return false;
    }

    const qreal opacity = button->property("opacity").isValid() ? button->property("opacity").toReal() : 1.0;
    if (opacity <= 0.0)
    {
        return true;
    }

    QPainter painter(button);
    painter.setOpacity(opacity);

    Color color = Color::IconMuted;
    if (button->isEnabled() && (button->underMouse() || button->isDown()))
    {
        color = Color::IconDefault;
    }

    PDFStyleState state;
    state.enabled = button->isEnabled();
    state.hover = button->underMouse();
    state.pressed = button->isDown();

    const QSizeF iconSize(16.0, 16.0);
    drawIcon(&painter, PDFStylePainter::centered(QRectF(button->rect()), iconSize), button->icon(), PDFWidgetStyleImpl::color(color), state);
    return true;
}

void PDFWidgetStyleImpl::setSplitterHover(QObject* watched, bool isEntered)
{
    QWidget* handle = qobject_cast<QWidget*>(watched);
    if (!handle || !handle->inherits("QSplitterHandle"))
    {
        return;
    }

    if (!isEntered)
    {
        if (handle->property(SPLITTER_HOVER).toBool())
        {
            handle->setProperty(SPLITTER_HOVER, false);
            handle->update();
        }
        return;
    }

    // The line appears, when the pointer rests over the handle for a while
    const int delay = isAnimated() ? 300 : 0;
    QPointer<QWidget> guard(handle);
    QTimer::singleShot(delay, this, [guard]()
    {
        if (guard && guard->underMouse() && !guard->property(SPLITTER_HOVER).toBool())
        {
            guard->setProperty(SPLITTER_HOVER, true);
            guard->update();
        }
    });
}

}   // namespace pdf
