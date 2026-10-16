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

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QDockWidget>
#include <QFocusFrame>
#include <QFrame>
#include <QGroupBox>
#include <QLayout>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QRubberBand>
#include <QScrollArea>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleOptionDockWidget>
#include <QStyleOptionFrame>
#include <QStyleOptionGroupBox>
#include <QStyleOptionRubberBand>
#include <QStyleOptionTitleBar>
#include <QStyleOptionToolBox>
#include <QStyleOptionToolButton>
#include <QTabBar>
#include <QToolBox>
#include <QToolButton>
#include <QtMath>

namespace pdf
{

/// Group boxes, frames, backgrounds of widgets, splitters, dock widgets, the
/// status bar, rubber bands, tool boxes, tooltips and focus frames
/// (sections 14, 15 and 16 of the design).
class PDFWidgetStyleContainers : public PDFWidgetStyleArea
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
    static constexpr int GROUP_TITLE_GAP = 8;
    static constexpr int GROUP_RADIUS = 10;
    static constexpr int CARD_RADIUS = 12;
    static constexpr int FRAME_RADIUS = 8;
    static constexpr int NOTE_RADIUS = 10;
    static constexpr int DOCK_TITLE_HEIGHT = 40;
    static constexpr int DOCK_BUTTON = 24;
    static constexpr int STATUS_ITEM_HEIGHT = 22;
    static constexpr int STATUS_BAR_HEIGHT = 28;
    static constexpr int TOOLBOX_TAB_HEIGHT = 36;
    static constexpr int TITLE_BAR_HEIGHT = 32;

private:
    static constexpr const char* SAVED_MARGINS = "_pdf4qt_containerMargins";
    static constexpr const char* SAVED_HEIGHT = "_pdf4qt_containerHeight";
    static constexpr const char* SAVED_SPACING = "_pdf4qt_containerSpacing";

    static bool isStatusItem(const QWidget* widget);
    static bool isDockButton(const QWidget* widget);
    static bool isCardGroupBox(const QWidget* widget);
    static int getGroupBoxTitleHeight(const QStyleOptionGroupBox* option, const QWidget* widget);
    static QFont getGroupBoxFont(const QWidget* widget);

    void drawWidgetBackground(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget, bool isShaped) const;
    void drawGroupBox(const QStyleOptionGroupBox* option, QPainter* painter, const QWidget* widget) const;
    void drawFocusFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawSplitter(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
    void drawDockTitle(const QStyleOptionDockWidget* option, QPainter* painter, const QWidget* widget) const;
    void drawToolBoxTab(const QStyleOptionToolBox* option, QPainter* painter, const QWidget* widget) const;
    void drawSmallToolButton(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const;
    void drawTitleBar(const QStyleOptionTitleBar* option, QPainter* painter, const QWidget* widget) const;
    QRect getGroupBoxRect(const QStyleOptionGroupBox* option, QStyle::SubControl subControl, const QWidget* widget) const;
};

std::unique_ptr<PDFWidgetStyleArea> PDFWidgetStyleAreas::createContainers(PDFWidgetStyleImpl* impl)
{
    return std::make_unique<PDFWidgetStyleContainers>(impl);
}

bool PDFWidgetStyleContainers::isStatusItem(const QWidget* widget)
{
    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::STATUS_ITEM);
}

bool PDFWidgetStyleContainers::isDockButton(const QWidget* widget)
{
    return widget && widget->parentWidget() && qobject_cast<const QDockWidget*>(widget->parentWidget()) && widget->inherits("QAbstractButton");
}

bool PDFWidgetStyleContainers::isCardGroupBox(const QWidget* widget)
{
    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CARD);
}

QFont PDFWidgetStyleContainers::getGroupBoxFont(const QWidget* widget)
{
    // Title of a card is Bold 15, title of a group box is Semi Bold 13
    return PDFWidgetStyleImpl::getFont(widget, isCardGroupBox(widget) ? PDFThemeFonts::Role::Title : PDFThemeFonts::Role::BodySemiBold);
}

int PDFWidgetStyleContainers::getGroupBoxTitleHeight(const QStyleOptionGroupBox* option, const QWidget* widget)
{
    const bool hasCheckBox = option->subControls.testFlag(QStyle::SC_GroupBoxCheckBox);
    const bool hasTitle = !option->text.isEmpty();

    if (!hasCheckBox && !hasTitle)
    {
        return 0;
    }

    int height = isCardGroupBox(widget) ? 18 : 16;
    if (hasCheckBox)
    {
        height = qMax(height, 18);
    }

    return height;
}

bool PDFWidgetStyleContainers::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::PE_Widget:
            drawWidgetBackground(option, painter, widget);
            return true;

        case QStyle::PE_Frame:
        case QStyle::PE_FrameWindow:
            drawFrame(option, painter, widget, false);
            return true;

        case QStyle::PE_FrameDockWidget:
            PDFStylePainter::strokeInside(painter, QRectF(option->rect), Radii(0.0), Color::BorderStrong, 1.0);
            return true;

        case QStyle::PE_FrameGroupBox:
        {
            const QStyleOptionFrame* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
            const QRectF rect(option->rect);

            if (frame && frame->features.testFlag(QStyleOptionFrame::Flat))
            {
                // Flat group box has only a line below its title
                PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.top(), rect.width(), 1.0), Color::BorderDivider);
            }
            else
            {
                const Radii radii(isCardGroupBox(widget) ? CARD_RADIUS : GROUP_RADIUS);
                PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
                PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
            }
            return true;
        }

        case QStyle::PE_FrameFocusRect:
        {
            // Generic focus rectangle: ring of 2 px inside, only after the keyboard
            if ((widget && widget->isVisible()) ? m_impl->isKeyboardModality() : option->state.testFlag(QStyle::State_KeyboardFocusChange))
            {
                PDFStylePainter::strokeInside(painter, QRectF(option->rect), Radii(6.0), Color::BorderFocus, 2.0);
            }
            return true;
        }

        case QStyle::PE_PanelTipLabel:
        {
            // Tooltip of Qt (a rich text, a tooltip shown by the application):
            // opaque panel without the shadow, it fills its window
            const QRectF rect(option->rect);
            PDFStylePainter::fillRect(painter, rect, Color::BgSurface);
            PDFStylePainter::strokeInside(painter, rect, Radii(0.0), Color::BorderStrong, 1.0);
            return true;
        }

        case QStyle::PE_PanelStatusBar:
        {
            const QRectF rect(option->rect);
            PDFStylePainter::fillRect(painter, rect, Color::BgSidebar);
            PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.top(), rect.width(), 1.0), Color::BorderDefault);
            return true;
        }

        case QStyle::PE_FrameStatusBarItem:
            // Items of the status bar have no frame
            return true;

        case QStyle::PE_IndicatorDockWidgetResizeHandle:
            drawSplitter(option, painter, widget);
            return true;

        case QStyle::PE_PanelButtonTool:
        {
            if (isStatusItem(widget) || isDockButton(widget))
            {
                // The panel is a part of the complex control
                return true;
            }
            break;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleContainers::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::CE_ShapedFrame:
            drawFrame(option, painter, widget, true);
            return true;

        case QStyle::CE_FocusFrame:
            drawFocusFrame(option, painter, widget);
            return true;

        case QStyle::CE_Splitter:
            drawSplitter(option, painter, widget);
            return true;

        case QStyle::CE_SizeGrip:
            // The size grip is not drawn
            return true;

        case QStyle::CE_RubberBand:
        {
            const QStyleOptionRubberBand* rubberBand = qstyleoption_cast<const QStyleOptionRubberBand*>(option);
            const QRectF rect(option->rect);

            if (rubberBand && rubberBand->shape == QRubberBand::Line)
            {
                PDFStylePainter::fillRect(painter, rect, Color::BgAccent);
            }
            else
            {
                PDFStylePainter::fill(painter, rect, Radii(2.0), Color::BgAccentSubtle);
                PDFStylePainter::strokeInside(painter, rect, Radii(2.0), Color::BgAccent, 1.0);
            }
            return true;
        }

        case QStyle::CE_DockWidgetTitle:
        {
            if (const QStyleOptionDockWidget* dockWidget = qstyleoption_cast<const QStyleOptionDockWidget*>(option))
            {
                drawDockTitle(dockWidget, painter, widget);
            }
            return true;
        }

        case QStyle::CE_ToolBoxTab:
        case QStyle::CE_ToolBoxTabShape:
        case QStyle::CE_ToolBoxTabLabel:
        {
            if (element != QStyle::CE_ToolBoxTab)
            {
                // Shape and label are drawn together
                return true;
            }

            if (const QStyleOptionToolBox* toolBox = qstyleoption_cast<const QStyleOptionToolBox*>(option))
            {
                drawToolBoxTab(toolBox, painter, widget);
            }
            return true;
        }

        case QStyle::CE_ToolButtonLabel:
        {
            if (isStatusItem(widget) || isDockButton(widget))
            {
                return true;
            }
            break;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleContainers::drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    switch (control)
    {
        case QStyle::CC_GroupBox:
        {
            if (const QStyleOptionGroupBox* groupBox = qstyleoption_cast<const QStyleOptionGroupBox*>(option))
            {
                drawGroupBox(groupBox, painter, widget);
            }
            return true;
        }

        case QStyle::CC_ToolButton:
        {
            const QStyleOptionToolButton* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option);
            if (toolButton && (isStatusItem(widget) || isDockButton(widget)))
            {
                drawSmallToolButton(toolButton, painter, widget);
                return true;
            }
            break;
        }

        case QStyle::CC_TitleBar:
        {
            if (const QStyleOptionTitleBar* titleBar = qstyleoption_cast<const QStyleOptionTitleBar*>(option))
            {
                drawTitleBar(titleBar, painter, widget);
            }
            return true;
        }

        case QStyle::CC_MdiControls:
            // MDI is not used
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleContainers::subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const
{
    switch (element)
    {
        case QStyle::SE_DockWidgetCloseButton:
        case QStyle::SE_DockWidgetFloatButton:
        case QStyle::SE_DockWidgetTitleBarText:
        case QStyle::SE_DockWidgetIcon:
        {
            const QStyleOptionDockWidget* dockWidget = qstyleoption_cast<const QStyleOptionDockWidget*>(option);
            if (!dockWidget || dockWidget->verticalTitleBar)
            {
                return false;
            }

            // Buttons of 24 px at the right side: float, close; 4 px apart, 12 px from the edge
            const QRect bar = option->rect;
            const int y = bar.top() + (bar.height() - 1 - DOCK_BUTTON) / 2;
            int right = bar.right() + 1 - 12;

            QRect close;
            QRect floatRect;

            if (dockWidget->closable)
            {
                close = QRect(right - DOCK_BUTTON, y, DOCK_BUTTON, DOCK_BUTTON);
                right -= DOCK_BUTTON + 4;
            }
            if (dockWidget->floatable)
            {
                floatRect = QRect(right - DOCK_BUTTON, y, DOCK_BUTTON, DOCK_BUTTON);
                right -= DOCK_BUTTON + 4;
            }

            switch (element)
            {
                case QStyle::SE_DockWidgetCloseButton:
                    rect = close;
                    break;
                case QStyle::SE_DockWidgetFloatButton:
                    rect = floatRect;
                    break;
                case QStyle::SE_DockWidgetTitleBarText:
                    rect = QRect(bar.left() + 16, bar.top(), qMax(0, right - 4 - bar.left() - 16), bar.height() - 1);
                    break;
                default:
                    rect = QRect();
                    break;
            }

            rect = PDFWidgetStyleImpl::visualRect(option, bar, rect);
            return true;
        }

        case QStyle::SE_ToolBoxTabContents:
            rect = option->rect;
            return true;

        default:
            break;
    }

    Q_UNUSED(widget);
    return false;
}

bool PDFWidgetStyleContainers::subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const
{
    switch (control)
    {
        case QStyle::CC_GroupBox:
        {
            if (const QStyleOptionGroupBox* groupBox = qstyleoption_cast<const QStyleOptionGroupBox*>(option))
            {
                rect = getGroupBoxRect(groupBox, subControl, widget);
                return true;
            }
            break;
        }

        case QStyle::CC_ToolButton:
        {
            if (isStatusItem(widget) || isDockButton(widget))
            {
                rect = subControl == QStyle::SC_ToolButton ? option->rect : QRect();
                return true;
            }
            break;
        }

        case QStyle::CC_TitleBar:
        {
            // Only the label of the title bar is used
            rect = subControl == QStyle::SC_TitleBarLabel ? option->rect.adjusted(16, 0, -16, -1) : QRect();
            return true;
        }

        case QStyle::CC_MdiControls:
            rect = QRect();
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleContainers::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const
{
    switch (type)
    {
        case QStyle::CT_GroupBox:
        {
            size = contentsSize;

            // The group box measures its title by its own font, the title is drawn by the font of the design
            if (const QStyleOptionGroupBox* groupBox = qstyleoption_cast<const QStyleOptionGroupBox*>(option))
            {
                int titleWidth = groupBox->text.isEmpty() ? 0 : PDFWidgetStyleImpl::getTextWidth(groupBox->text, getGroupBoxFont(widget));
                if (groupBox->subControls.testFlag(QStyle::SC_GroupBoxCheckBox))
                {
                    titleWidth += 18 + (groupBox->text.isEmpty() ? 0 : 8);
                }
                size.setWidth(qMax(size.width(), titleWidth));
            }
            return true;
        }

        case QStyle::CT_ToolButton:
        {
            const QStyleOptionToolButton* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option);

            if (isStatusItem(widget))
            {
                // Paddings 8, icon 14, gap 6, label Medium 11
                int width = 16;
                if (toolButton)
                {
                    width += PDFWidgetStyleImpl::getTextWidth(toolButton->text, PDFWidgetStyleImpl::getFont(widget, Font::Caption));
                    if (!toolButton->icon.isNull())
                    {
                        width += 14 + (toolButton->text.isEmpty() ? 0 : 6);
                    }
                }
                size = QSize(width, STATUS_ITEM_HEIGHT);
                return true;
            }

            if (isDockButton(widget))
            {
                size = QSize(DOCK_BUTTON, DOCK_BUTTON);
                return true;
            }
            break;
        }

        case QStyle::CT_SizeGrip:
            size = QSize(16, 16);
            return true;

        case QStyle::CT_Splitter:
            size = QSize(6, 6);
            return true;

        default:
            break;
    }

    return false;
}

void PDFWidgetStyleContainers::polish(QWidget* widget)
{
    if (QToolBox* toolBox = qobject_cast<QToolBox*>(widget))
    {
        // Container with the border 1 and padding 6, tabs and pages 2 px apart
        if (!toolBox->property(SAVED_MARGINS).isValid())
        {
            toolBox->setProperty(SAVED_MARGINS, QVariant::fromValue(toolBox->contentsMargins()));
            toolBox->setContentsMargins(7, 7, 7, 7);

            if (QLayout* layout = toolBox->layout())
            {
                toolBox->setProperty(SAVED_SPACING, layout->spacing());
                layout->setSpacing(2);
            }
        }
    }
    else if (widget->inherits("QToolBoxButton") || qobject_cast<QStatusBar*>(widget))
    {
        if (!widget->property(SAVED_HEIGHT).isValid())
        {
            widget->setProperty(SAVED_HEIGHT, widget->minimumHeight());
            widget->setMinimumHeight(qobject_cast<QStatusBar*>(widget) ? STATUS_BAR_HEIGHT : TOOLBOX_TAB_HEIGHT);
        }
    }
    else if (QRubberBand* rubberBand = qobject_cast<QRubberBand*>(widget))
    {
        if (rubberBand->isWindow() && PDFWidgetStyleImpl::isTranslucencyAvailable() && !rubberBand->testAttribute(Qt::WA_WState_Created))
        {
            rubberBand->setAttribute(Qt::WA_TranslucentBackground, true);
        }
    }
}

void PDFWidgetStyleContainers::unpolish(QWidget* widget)
{
    const QVariant savedMargins = widget->property(SAVED_MARGINS);
    if (savedMargins.isValid())
    {
        widget->setContentsMargins(savedMargins.value<QMargins>());
        widget->setProperty(SAVED_MARGINS, QVariant());

        const QVariant savedSpacing = widget->property(SAVED_SPACING);
        if (savedSpacing.isValid() && widget->layout())
        {
            widget->layout()->setSpacing(savedSpacing.toInt());
            widget->setProperty(SAVED_SPACING, QVariant());
        }
    }

    const QVariant savedHeight = widget->property(SAVED_HEIGHT);
    if (savedHeight.isValid())
    {
        widget->setMinimumHeight(savedHeight.toInt());
        widget->setProperty(SAVED_HEIGHT, QVariant());
    }
}

void PDFWidgetStyleContainers::drawWidgetBackground(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    if (!widget)
    {
        return;
    }

    const QRectF rect(option->rect);
    const bool isRightToLeft = option->direction == Qt::RightToLeft;

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::OVERLAY))
    {
        // Dialog shown inside of a tab: card with the radius 16
        const QMargins margins = PDFWidgetStyleImpl::getPopupMargins(widget);
        const QRectF card = rect.marginsRemoved(QMarginsF(margins));
        if (!margins.isNull())
        {
            PDFStylePainter::shadow(painter, card, Radii(16.0), QPointF(0.0, 8.0), 24.0, PDFWidgetStyleImpl::color(Color::EffectShadow));
        }
        PDFStylePainter::fill(painter, card, Radii(16.0), Color::BgSurface);
        PDFStylePainter::strokeInside(painter, card, Radii(16.0), Color::BorderDefault, 1.0);
        return;
    }

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CARD) && !qobject_cast<const QGroupBox*>(widget) && !widget->inherits("QTabWidget"))
    {
        PDFStylePainter::fill(painter, rect, Radii(CARD_RADIUS), Color::BgSurface);
        PDFStylePainter::strokeInside(painter, rect, Radii(CARD_RADIUS), Color::BorderDefault, 1.0);
        return;
    }

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::NOTE))
    {
        PDFStylePainter::fill(painter, rect, Radii(NOTE_RADIUS), Color::BgSubtle);
        return;
    }

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::PANEL) || PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SIDEBAR))
    {
        // Panel of a sidebar with the border on the side of the content
        PDFStylePainter::fillRect(painter, rect, Color::BgSidebar);
        PDFStylePainter::fillRect(painter, QRectF(isRightToLeft ? rect.left() : rect.right() - 1.0, rect.top(), 1.0, rect.height()), Color::BorderDefault);
        return;
    }

    if (qobject_cast<const QToolBox*>(widget))
    {
        PDFStylePainter::fill(painter, rect, Radii(FRAME_RADIUS), Color::BgSurface);
        PDFStylePainter::strokeInside(painter, rect, Radii(FRAME_RADIUS), Color::BorderDefault, 1.0);
        return;
    }

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::DOCUMENT_TABS))
    {
        // Strip of the document tabs: the tab bar and the top bar of the application, which holds it
        PDFStylePainter::fillRect(painter, rect, Color::BgTitlebar);
        return;
    }

    if (qobject_cast<const QTabBar*>(widget))
    {
        return;
    }

    if (qobject_cast<const QDockWidget*>(widget))
    {
        PDFStylePainter::fillRect(painter, rect, Color::BgSidebar);
        return;
    }

    if (widget->isWindow() || PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CANVAS))
    {
        PDFStylePainter::fillRect(painter, rect, Color::BgCanvas);
        return;
    }

    // Other widgets (viewports of scroll areas, pages) show what is below them
}

void PDFWidgetStyleContainers::drawFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget, bool isShaped) const
{
    // Only a shaped frame (QFrame) says its shape, the option of the primitive
    // PE_Frame has no shape by default and it is a panel
    const QStyleOptionFrame* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
    const QRectF rect(option->rect);
    const QFrame::Shape shape = (frame && isShaped) ? frame->frameShape : QFrame::StyledPanel;

    if (PDFWidgetStyleImpl::getPopupKind(widget) == PDFWidgetStyleImpl::PopupKind::Popover)
    {
        // Popover panel: radius 12, shadow 0 / 8 / 24
        PDFWidgetStyleImpl::drawPopupPanel(painter, option->rect, widget, PDFWidgetStyleImpl::PopupKind::Popover, 12.0, Color::BorderDefault);
        return;
    }

    switch (shape)
    {
        case QFrame::NoFrame:
            return;

        case QFrame::HLine:
            PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.top() + qFloor((rect.height() - 1.0) / 2.0), rect.width(), 1.0), Color::BorderDivider);
            return;

        case QFrame::VLine:
            PDFStylePainter::fillRect(painter, QRectF(rect.left() + qFloor((rect.width() - 1.0) / 2.0), rect.top(), 1.0, rect.height()), Color::BorderSeparator);
            return;

        default:
            break;
    }

    const bool hasContainerProperty = PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CARD) ||
                                      PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::PANEL) ||
                                      PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::OVERLAY);
    if (hasContainerProperty)
    {
        // The shape of the container is its background
        return;
    }

    if (qobject_cast<const QAbstractScrollArea*>(widget))
    {
        const bool isTextEditor = widget->inherits("QTextEdit") || widget->inherits("QPlainTextEdit");

        if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::NOTE))
        {
            PDFStylePainter::fill(painter, rect, Radii(NOTE_RADIUS), Color::BgSubtle);
            return;
        }

        if (isTextEditor)
        {
            // Multi-line editor is a field
            PDFWidgetStyleImpl::drawFieldFrame(painter, rect, Radii(FRAME_RADIUS), PDFWidgetStyleImpl::getFieldState(option, widget));
            return;
        }

        if (!qobject_cast<const QScrollArea*>(widget))
        {
            // Item views are filled, a scroll area of widgets shows what is below it
            PDFStylePainter::fill(painter, rect, Radii(FRAME_RADIUS), Color::BgSurface);
        }

        PDFStylePainter::strokeInside(painter, rect, Radii(FRAME_RADIUS), Color::BorderDefault, 1.0);
        return;
    }

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::NOTE))
    {
        return;
    }

    PDFStylePainter::strokeInside(painter, rect, Radii(widget ? FRAME_RADIUS : 0.0), Color::BorderDefault, 1.0);
}

QRect PDFWidgetStyleContainers::getGroupBoxRect(const QStyleOptionGroupBox* option, QStyle::SubControl subControl, const QWidget* widget) const
{
    const QRect rect = option->rect;
    const bool hasCheckBox = option->subControls.testFlag(QStyle::SC_GroupBoxCheckBox);
    const bool isFlat = option->features.testFlag(QStyleOptionFrame::Flat);
    const int titleHeight = getGroupBoxTitleHeight(option, widget);
    const int frameTop = titleHeight > 0 ? titleHeight + GROUP_TITLE_GAP : 0;
    const QRect frame(rect.left(), rect.top() + frameTop, rect.width(), qMax(0, rect.height() - frameTop));

    QRect result;

    switch (subControl)
    {
        case QStyle::SC_GroupBoxFrame:
            return frame;

        case QStyle::SC_GroupBoxContents:
        {
            // The paddings of the contents are the margins of the layout of the group box
            if (isFlat)
            {
                return frame.adjusted(0, 1, 0, 0);
            }
            return frame.adjusted(1, 1, -1, -1);
        }

        case QStyle::SC_GroupBoxCheckBox:
        {
            if (!hasCheckBox)
            {
                return QRect();
            }
            result = QRect(rect.left(), rect.top() + (titleHeight - 18) / 2, 18, 18);
            break;
        }

        case QStyle::SC_GroupBoxLabel:
        {
            if (option->text.isEmpty())
            {
                return QRect();
            }

            const int left = hasCheckBox ? 18 + 8 : 0;
            const int textWidth = qMin(PDFWidgetStyleImpl::getTextWidth(option->text, getGroupBoxFont(widget)), qMax(0, rect.width() - left));
            result = QRect(rect.left() + left, rect.top(), textWidth, titleHeight);
            break;
        }

        default:
            return QRect();
    }

    return PDFWidgetStyleImpl::visualRect(option, rect, result);
}

void PDFWidgetStyleContainers::drawGroupBox(const QStyleOptionGroupBox* option, QPainter* painter, const QWidget* widget) const
{
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool hasCheckBox = option->subControls.testFlag(QStyle::SC_GroupBoxCheckBox);
    const bool isChecked = option->state.testFlag(QStyle::State_On);

    if (option->subControls.testFlag(QStyle::SC_GroupBoxFrame))
    {
        QStyleOptionFrame frameOption;
        frameOption.QStyleOption::operator=(*option);
        frameOption.features = option->features;
        frameOption.lineWidth = option->lineWidth;
        frameOption.midLineWidth = option->midLineWidth;
        frameOption.rect = style()->subControlRect(QStyle::CC_GroupBox, option, QStyle::SC_GroupBoxFrame, widget);
        style()->drawPrimitive(QStyle::PE_FrameGroupBox, &frameOption, painter, widget);
    }

    if (option->subControls.testFlag(QStyle::SC_GroupBoxLabel) && !option->text.isEmpty())
    {
        const QRectF labelRect(style()->subControlRect(QStyle::CC_GroupBox, option, QStyle::SC_GroupBoxLabel, widget));
        const QFont font = getGroupBoxFont(widget);

        // The title of an unchecked group box is muted as its disabled content
        const bool isMuted = !isEnabled || (hasCheckBox && !isChecked);
        const QString text = PDFWidgetStyleImpl::getElidedText(option->text, font, labelRect.width() + 1.0);
        m_impl->drawText(painter, labelRect, Qt::AlignVCenter | (option->direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft), text, font,
                         PDFWidgetStyleImpl::color(isMuted ? Color::TextDisabled : Color::TextPrimary), widget);
    }

    if (hasCheckBox)
    {
        QStyleOptionButton checkOption;
        checkOption.QStyleOption::operator=(*option);
        checkOption.rect = style()->subControlRect(QStyle::CC_GroupBox, option, QStyle::SC_GroupBoxCheckBox, widget);

        // Hover and press belong to the check box only, when the pointer is over it
        const bool isActive = option->activeSubControls.testFlag(QStyle::SC_GroupBoxCheckBox) || option->activeSubControls.testFlag(QStyle::SC_GroupBoxLabel);
        if (!isActive)
        {
            checkOption.state &= ~(QStyle::State_MouseOver | QStyle::State_Sunken);
        }

        style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &checkOption, painter, widget);
    }
}

void PDFWidgetStyleContainers::drawFocusFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const QFocusFrame* focusFrame = qobject_cast<const QFocusFrame*>(widget);
    const QWidget* target = focusFrame ? focusFrame->widget() : nullptr;
    const QColor color = PDFWidgetStyleImpl::color(Color::BorderFocus);

    if (!target)
    {
        // The frame without its widget (drawn directly): ring inside of the rectangle
        PDFStylePainter::strokeInside(painter, QRectF(option->rect), Radii(10.0), color, 2.0);
        return;
    }

    // The rectangle of the frame is the widget grown by the margin of the frame
    const int margin = style()->pixelMetric(QStyle::PM_FocusFrameHMargin, option, widget);
    const QRectF targetRect = QRectF(option->rect).adjusted(margin, margin, -margin, -margin);

    QRectF shape;
    Radii radii;
    if (!PDFWidgetStyleImpl::getFocusRingShape(style(), target, targetRect, shape, radii))
    {
        return;
    }

    PDFStylePainter::focusRing(painter, shape, radii, PDFWidgetStyleImpl::isFocusRingShapeAccent(target));
}

void PDFWidgetStyleContainers::drawSplitter(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    Q_UNUSED(widget);

    // The handle shows nothing at rest. A line of 2 px is drawn along it, when
    // the pointer rests over it or it is dragged.
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool isPressed = option->state.testFlag(QStyle::State_Sunken);
    bool isHovered = false;

    QPaintDevice* device = painter->device();
    if (device && device->devType() == QInternal::Widget)
    {
        const QWidget* handle = static_cast<const QWidget*>(device);
        isHovered = handle->property(PDFWidgetStyleImpl::SPLITTER_HOVER).toBool();

        if (!handle->inherits("QSplitterHandle"))
        {
            // Separator of the docks of a main window: the window tells the hovered one
            isHovered = option->state.testFlag(QStyle::State_MouseOver);
        }
    }

    if (!isEnabled || !(isPressed || isHovered))
    {
        return;
    }

    const QRectF rect(option->rect);
    const bool isVerticalHandle = option->state.testFlag(QStyle::State_Horizontal);
    const QRectF line = isVerticalHandle ? QRectF(rect.left() + qFloor((rect.width() - 2.0) / 2.0), rect.top(), 2.0, rect.height())
                                         : QRectF(rect.left(), rect.top() + qFloor((rect.height() - 2.0) / 2.0), rect.width(), 2.0);
    PDFStylePainter::fillRect(painter, line, Color::BgAccent);
}

void PDFWidgetStyleContainers::drawDockTitle(const QStyleOptionDockWidget* option, QPainter* painter, const QWidget* widget) const
{
    const QRectF rect(option->rect);

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SIDEBAR))
    {
        // The sidebar has its own header instead of the title bar
        return;
    }

    PDFStylePainter::fillRect(painter, rect, Color::BgSidebar);

    if (option->verticalTitleBar)
    {
        PDFStylePainter::fillRect(painter, QRectF(rect.right() - 1.0, rect.top(), 1.0, rect.height()), Color::BorderDefault);
        return;
    }

    PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.bottom() - 1.0, rect.width(), 1.0), Color::BorderDefault);

    const QRectF textRect(style()->subElementRect(QStyle::SE_DockWidgetTitleBarText, option, widget));
    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::BodySemiBold);
    const QString text = PDFWidgetStyleImpl::getElidedText(option->title, font, textRect.width());
    m_impl->drawText(painter, textRect, Qt::AlignVCenter | (option->direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft), text, font,
                     PDFWidgetStyleImpl::color(Color::TextPrimary), widget);
}

void PDFWidgetStyleContainers::drawToolBoxTab(const QStyleOptionToolBox* option, QPainter* painter, const QWidget* widget) const
{
    // Row of 36 px: icon 16, label Medium 13, chevron 14 at the right
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF rect(option->rect);
    const Radii radii(8.0);

    Color textColor = Color::TextSecondary;
    Color iconColor = Color::IconDefault;

    if (state.selected)
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgAccentSubtle);
        textColor = Color::TextAccent;
        iconColor = Color::IconAccent;
    }
    else
    {
        PDFWidgetStyleImpl::drawStateOverlay(painter, rect, radii, state);
    }

    if (!state.enabled)
    {
        textColor = Color::TextDisabled;
        iconColor = Color::IconDisabled;
    }

    if (state.focusVisible)
    {
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderFocus, 2.0);
    }

    auto place = [&rect, &state](qreal left, qreal width, qreal height)
    {
        const QRectF item(left, rect.top() + (rect.height() - height) / 2.0, width, height);
        return PDFWidgetStyleImpl::visualRect(state.rightToLeft, rect, item);
    };

    qreal left = rect.left() + 10.0;
    const qreal right = rect.right() - 12.0 - 14.0;

    if (!option->icon.isNull())
    {
        PDFWidgetStyleImpl::drawIcon(painter, place(left, 16.0, 16.0), option->icon, PDFWidgetStyleImpl::color(iconColor), state);
        left += 16.0 + 10.0;
    }

    QString chevron = QStringLiteral("chevron-down");
    if (!state.selected)
    {
        chevron = state.rightToLeft ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right");
    }
    PDFStylePainter::icon(painter, place(right, 14.0, 14.0), chevron, state.enabled ? Color::IconMuted : Color::IconDisabled);

    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::BodyMedium);
    const qreal textWidth = qMax(0.0, right - 10.0 - left);
    const QString text = PDFWidgetStyleImpl::getElidedText(option->text, font, textWidth);
    m_impl->drawText(painter, place(left, textWidth, rect.height()), Qt::AlignVCenter | (state.rightToLeft ? Qt::AlignRight : Qt::AlignLeft), text, font,
                     PDFWidgetStyleImpl::color(textColor), widget);
}

void PDFWidgetStyleContainers::drawSmallToolButton(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF rect(option->rect);
    const Radii radii(6.0);

    PDFWidgetStyleImpl::drawStateOverlay(painter, rect, radii, state);

    if (isDockButton(widget))
    {
        // Button of the title bar of a dock widget: icon 14
        Color color = state.enabled ? Color::IconMuted : Color::IconDisabled;
        if (state.enabled && (state.hover || state.pressed))
        {
            color = Color::IconDefault;
        }
        PDFWidgetStyleImpl::drawIcon(painter, PDFStylePainter::centered(rect, QSizeF(14.0, 14.0)), option->icon, PDFWidgetStyleImpl::color(color), state);
        return;
    }

    // Item of the status bar: icon 14, label Medium 11
    const bool isWarning = widget && widget->property(PDFWidgetStyleProperties::TONE).toString() == QLatin1String("warning");

    Color textColor = isWarning ? Color::TextSecondary : Color::TextTertiary;
    Color iconColor = isWarning ? Color::AccentAmberIcon : Color::IconDefault;
    if (!state.enabled)
    {
        textColor = Color::TextDisabled;
        iconColor = Color::IconDisabled;
    }

    if (state.focusVisible)
    {
        PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderFocus, 2.0);
    }

    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::Caption);
    const qreal iconWidth = option->icon.isNull() ? 0.0 : 14.0 + (option->text.isEmpty() ? 0.0 : 6.0);
    const qreal available = qMax(0.0, rect.width() - 16.0);

    qreal textWidth = PDFWidgetStyleImpl::getTextWidth(option->text, font);
    QString text = option->text;
    if (iconWidth + textWidth > available)
    {
        textWidth = qMax(0.0, available - iconWidth);
        text = PDFWidgetStyleImpl::getElidedText(option->text, font, textWidth);
    }

    qreal x = qRound(rect.left() + 8.0 + (available - iconWidth - textWidth) / 2.0);

    auto place = [&rect, &state](qreal left, qreal width, qreal height)
    {
        const QRectF item(left, rect.top() + (rect.height() - height) / 2.0, width, height);
        return PDFWidgetStyleImpl::visualRect(state.rightToLeft, rect, item);
    };

    if (!option->icon.isNull())
    {
        PDFWidgetStyleImpl::drawIcon(painter, place(x, 14.0, 14.0), option->icon, PDFWidgetStyleImpl::color(iconColor), state);
        x += iconWidth;
    }

    m_impl->drawText(painter, place(x, textWidth, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, text, font, PDFWidgetStyleImpl::color(textColor), widget);
}

void PDFWidgetStyleContainers::drawTitleBar(const QStyleOptionTitleBar* option, QPainter* painter, const QWidget* widget) const
{
    // Title bar drawn by a widget itself (windows have native title bars)
    const QRectF rect(option->rect);
    PDFStylePainter::fillRect(painter, rect, Color::BgSidebar);
    PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.bottom() - 1.0, rect.width(), 1.0), Color::BorderDefault);

    if (!option->subControls.testFlag(QStyle::SC_TitleBarLabel) || option->text.isEmpty())
    {
        return;
    }

    const QRectF textRect(style()->subControlRect(QStyle::CC_TitleBar, option, QStyle::SC_TitleBarLabel, widget));
    const QFont font = PDFThemeFonts::getFont(Font::BodySemiBold);
    const QString text = PDFWidgetStyleImpl::getElidedText(option->text, font, textRect.width());
    PDFStylePainter::text(painter, textRect, Qt::AlignVCenter | (option->direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::TextSingleLine,
                          text, font, PDFWidgetStyleImpl::color(Color::TextPrimary));
}

}   // namespace pdf
