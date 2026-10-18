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
#include "pdfcomponentpainter.h"
#include "pdfthemeiconengine.h"

#include <QPainter>
#include <QPushButton>
#include <QStyleOptionButton>
#include <QStyleOptionTab>
#include <QStyleOptionTabWidgetFrame>
#include <QStyleOptionToolButton>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>

namespace pdf
{

/// Tab bars, tab widgets and the controls built of the same shapes: filter
/// chips, segments of a segmented control and the buttons of the document
/// tabs (section 11 of the design).
class PDFWidgetStyleTabs : public PDFWidgetStyleArea
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
    static constexpr int RING = 2;                      ///< Margin of a tab for its focus ring
    static constexpr int SEGMENT_PADDING = 14;              ///< Natural padding of a segment
    static constexpr int SEGMENT_MIN_PADDING = 6;           ///< Padding of a segment of a control of a given width
    static constexpr int CHIP_HEIGHT = 32;
    static constexpr int CHIP_RADIUS = 16;
    static constexpr int CHIP_PADDING = 14;
    static constexpr int CHIP_GAP = 6;
    static constexpr int CHIP_ICON = 14;
    static constexpr int CHIP_MIN_WIDTH = 64;
    static constexpr int NAV_ROW_HEIGHT = 38;
    static constexpr int NAV_ROW_GAP = 2;
    static constexpr int NAV_ROW_RADIUS = 10;
    static constexpr int NAV_COLUMN_WIDTH = 240;
    static constexpr int NAV_COLUMN_PADDING_H = 12;
    static constexpr int NAV_COLUMN_PADDING_V = 16;
    static constexpr int NAV_ICON = 20;
    static constexpr int DOCUMENT_TAB_HEIGHT = 32;
    static constexpr int DOCUMENT_TAB_TOP = 6;
    static constexpr int DOCUMENT_TAB_GAP = 8;
    static constexpr int DOCUMENT_TAB_ICON = 14;
    static constexpr int TAB_CLOSE_BUTTON = 20;
    static constexpr int TAB_BUTTON = 28;
    static constexpr int SEGMENT_HEIGHT = 32;
    static constexpr int SEGMENT_RADIUS = 16;
    static constexpr int PANE_GAP = 16;
    static constexpr int CARD_GAP = 12;
    static constexpr int CARD_PADDING = 16;

private:
    static constexpr const char* SAVED_SCROLL_BUTTONS = "_pdf4qt_scrollButtons";
    static constexpr const char* SAVED_EXPANDING = "_pdf4qt_expanding";
    static constexpr const char* SAVED_ELIDE_MODE = "_pdf4qt_elideMode";
    static constexpr const char* SAVED_DRAW_BASE = "_pdf4qt_drawBase";

    enum class TabKind
    {
        Chip,
        NavRow,
        Document
    };

    static bool isVertical(QTabBar::Shape shape);
    static TabKind getTabKind(const QStyleOptionTab* option, const QWidget* widget);
    static bool isSegment(const QWidget* widget);
    static bool isChip(const QWidget* widget);
    static bool isOptionChip(const QWidget* widget);
    static bool isTabBarButton(const QWidget* widget);
    static int getCount(const QStyleOptionTab* option, const QWidget* widget);
    static int getButtonChipCount(const QWidget* widget);
    static QString getTabText(const QStyleOptionTab* option, const QWidget* widget);

    QRect getTabShapeRect(const QStyleOptionTab* option, const QWidget* widget) const;
    int getChipWidth(const QString& text, bool hasIcon, int count, bool hasBorder, const QWidget* widget) const;
    void drawChip(QPainter* painter, const QRectF& rect, const PDFStyleState& state, bool isSelected, const QString& text, const QIcon& icon, int count, const QWidget* widget) const;
    void drawTabShape(const QStyleOptionTab* option, QPainter* painter, const QWidget* widget) const;
    void drawTabLabel(const QStyleOptionTab* option, QPainter* painter, const QWidget* widget) const;
    void drawSegment(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const;
    void drawTabBarButton(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const;
    void drawTabWidgetFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget) const;
};

std::unique_ptr<PDFWidgetStyleArea> PDFWidgetStyleAreas::createTabs(PDFWidgetStyleImpl* impl)
{
    return std::make_unique<PDFWidgetStyleTabs>(impl);
}

bool PDFWidgetStyleTabs::isVertical(QTabBar::Shape shape)
{
    return shape == QTabBar::RoundedWest || shape == QTabBar::RoundedEast || shape == QTabBar::TriangularWest || shape == QTabBar::TriangularEast;
}

PDFWidgetStyleTabs::TabKind PDFWidgetStyleTabs::getTabKind(const QStyleOptionTab* option, const QWidget* widget)
{
    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::DOCUMENT_TABS))
    {
        return TabKind::Document;
    }

    return isVertical(option->shape) ? TabKind::NavRow : TabKind::Chip;
}

bool PDFWidgetStyleTabs::isSegment(const QWidget* widget)
{
    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::SEGMENT);
}

bool PDFWidgetStyleTabs::isChip(const QWidget* widget)
{
    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CHIP);
}

bool PDFWidgetStyleTabs::isOptionChip(const QWidget* widget)
{
    // Chip, which is an on / off option (Whole words, Regular expression, Sync zoom)
    return isChip(widget) && PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::OPTION);
}

bool PDFWidgetStyleTabs::isTabBarButton(const QWidget* widget)
{
    // Scroll buttons of a tab bar and the button of a new tab
    return PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::NEW_TAB) ||
           (widget && widget->parentWidget() && widget->parentWidget()->inherits("QTabBar") && qobject_cast<const QToolButton*>(widget));
}

int PDFWidgetStyleTabs::getButtonChipCount(const QWidget* widget)
{
    // Count of a chip, which is a button, is its dynamic property
    if (!widget)
    {
        return -1;
    }

    bool ok = false;
    const int count = widget->property(PDFWidgetStyleProperties::CHIP_COUNT).toInt(&ok);
    return ok && count >= 0 ? count : -1;
}

int PDFWidgetStyleTabs::getCount(const QStyleOptionTab* option, const QWidget* widget)
{
    // Number stored as the data of the tab is shown as its count
    const QTabBar* tabBar = qobject_cast<const QTabBar*>(widget);
    if (!tabBar || option->tabIndex < 0 || option->tabIndex >= tabBar->count())
    {
        return -1;
    }

    const QVariant data = tabBar->tabData(option->tabIndex);
    if (data.typeId() != QMetaType::Int && data.typeId() != QMetaType::UInt && data.typeId() != QMetaType::LongLong)
    {
        return -1;
    }

    return data.toInt();
}

QString PDFWidgetStyleTabs::getTabText(const QStyleOptionTab* option, const QWidget* widget)
{
    // The tab bar elides the text of the option by its own font, the style
    // elides the whole text by the font of the design
    const QTabBar* tabBar = qobject_cast<const QTabBar*>(widget);
    if (tabBar && option->tabIndex >= 0 && option->tabIndex < tabBar->count())
    {
        return tabBar->tabText(option->tabIndex);
    }

    return option->text;
}

QRect PDFWidgetStyleTabs::getTabShapeRect(const QStyleOptionTab* option, const QWidget* widget) const
{
    const QRect rect = option->rect;

    switch (getTabKind(option, widget))
    {
        case TabKind::Chip:
        {
            // Margin of the focus ring around the chip, the rest of the gap of 6 px after it
            const QRect shape(rect.left() + RING, rect.top() + (rect.height() - CHIP_HEIGHT) / 2, qMax(0, rect.width() - 2 * RING - (CHIP_GAP - 2 * RING)), CHIP_HEIGHT);
            return PDFWidgetStyleImpl::visualRect(option, rect, shape);
        }

        case TabKind::NavRow:
        {
            // Rows are 2 px apart and they have the paddings of the column at their sides
            const bool isEast = option->shape == QTabBar::RoundedEast || option->shape == QTabBar::TriangularEast;
            const int left = isEast ? NAV_COLUMN_PADDING_H + 1 : NAV_COLUMN_PADDING_H;
            const int right = isEast ? NAV_COLUMN_PADDING_H : NAV_COLUMN_PADDING_H + 1;
            return rect.adjusted(left, NAV_ROW_GAP / 2, -right, -NAV_ROW_GAP / 2);
        }

        case TabKind::Document:
        {
            const QRect shape(rect.left(), rect.bottom() + 1 - DOCUMENT_TAB_HEIGHT, qMax(0, rect.width() - DOCUMENT_TAB_GAP), DOCUMENT_TAB_HEIGHT);
            return PDFWidgetStyleImpl::visualRect(option, rect, shape);
        }
    }

    return rect;
}

int PDFWidgetStyleTabs::getChipWidth(const QString& text, bool hasIcon, int count, bool hasBorder, const QWidget* widget) const
{
    int width = 2 * CHIP_PADDING + PDFWidgetStyleImpl::getTextWidth(text, PDFWidgetStyleImpl::getFont(widget, Font::SmallSemiBold));

    if (hasIcon)
    {
        width += CHIP_ICON + (text.isEmpty() ? 0 : CHIP_GAP);
    }
    if (count >= 0)
    {
        width += CHIP_GAP + PDFWidgetStyleImpl::getTextWidth(QString::number(count), PDFThemeFonts::getFont(Font::Caption));
    }
    // The border of 1 px of an unselected chip is the padding of 15 px of a selected
    // chip, so the chip keeps its width, when it is selected
    Q_UNUSED(hasBorder);
    width += 2;

    return qMax(CHIP_MIN_WIDTH, width);
}

bool PDFWidgetStyleTabs::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::PE_FrameTabWidget:
            drawTabWidgetFrame(option, painter, widget);
            return true;

        case QStyle::PE_FrameTabBarBase:
        case QStyle::PE_IndicatorTabTear:
        case QStyle::PE_IndicatorTabTearRight:
            // Tab bars have no base line and no tear indicator
            return true;

        case QStyle::PE_IndicatorTabClose:
        {
            // Button of 20 px with the cross of 12 px
            const PDFStyleState state = m_impl->getState(option, widget);
            const QRectF rect = PDFStylePainter::centered(QRectF(option->rect), QSizeF(TAB_CLOSE_BUTTON, TAB_CLOSE_BUTTON));
            PDFWidgetStyleImpl::drawStateOverlay(painter, rect, Radii(4.0), state);

            Color color = state.enabled ? Color::IconMuted : Color::IconDisabled;
            if (state.pressed)
            {
                color = Color::IconStrong;
            }
            else if (state.hover)
            {
                color = Color::IconDefault;
            }

            PDFStylePainter::icon(painter, PDFStylePainter::centered(rect, QSizeF(12.0, 12.0)), QStringLiteral("x"), color);
            return true;
        }

        case QStyle::PE_PanelButtonTool:
        {
            if (isSegment(widget) || isChip(widget) || isTabBarButton(widget))
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

bool PDFWidgetStyleTabs::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::CE_TabBarTab:
        {
            if (const QStyleOptionTab* tab = qstyleoption_cast<const QStyleOptionTab*>(option))
            {
                drawTabShape(tab, painter, widget);
                drawTabLabel(tab, painter, widget);
            }
            return true;
        }

        case QStyle::CE_TabBarTabShape:
        {
            if (const QStyleOptionTab* tab = qstyleoption_cast<const QStyleOptionTab*>(option))
            {
                drawTabShape(tab, painter, widget);
            }
            return true;
        }

        case QStyle::CE_TabBarTabLabel:
        {
            if (const QStyleOptionTab* tab = qstyleoption_cast<const QStyleOptionTab*>(option))
            {
                drawTabLabel(tab, painter, widget);
            }
            return true;
        }

        case QStyle::CE_PushButton:
        {
            if (!isChip(widget))
            {
                break;
            }

            if (const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option))
            {
                const PDFStyleState state = m_impl->getState(option, widget);
                drawChip(painter, QRectF(option->rect), state, state.on, button->text, button->icon, getButtonChipCount(widget), widget);
            }
            return true;
        }

        case QStyle::CE_ToolButtonLabel:
        {
            if (isSegment(widget) || isChip(widget) || isTabBarButton(widget))
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

bool PDFWidgetStyleTabs::drawComplexControl(QStyle::ComplexControl control, const QStyleOptionComplex* option, QPainter* painter, const QWidget* widget) const
{
    if (control != QStyle::CC_ToolButton)
    {
        return false;
    }

    const QStyleOptionToolButton* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option);
    if (!toolButton)
    {
        return false;
    }

    if (isSegment(widget))
    {
        drawSegment(toolButton, painter, widget);
        return true;
    }

    if (isChip(widget))
    {
        const PDFStyleState state = m_impl->getState(option, widget);
        drawChip(painter, QRectF(option->rect), state, state.on, toolButton->text, toolButton->icon, getButtonChipCount(widget), widget);
        return true;
    }

    if (isTabBarButton(widget))
    {
        drawTabBarButton(toolButton, painter, widget);
        return true;
    }

    return false;
}

bool PDFWidgetStyleTabs::subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const
{
    switch (element)
    {
        case QStyle::SE_TabBarTabText:
        case QStyle::SE_TabBarTabLeftButton:
        case QStyle::SE_TabBarTabRightButton:
        {
            const QStyleOptionTab* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
            if (!tab)
            {
                return false;
            }

            const QRect shape = getTabShapeRect(tab, widget);
            const TabKind kind = getTabKind(tab, widget);
            const int paddingLeft = kind == TabKind::Chip ? CHIP_PADDING : (kind == TabKind::NavRow ? 12 : 16);
            const int paddingRight = kind == TabKind::Chip ? CHIP_PADDING : (kind == TabKind::NavRow ? 12 : 10);

            if (element == QStyle::SE_TabBarTabLeftButton || element == QStyle::SE_TabBarTabRightButton)
            {
                const QSize buttonSize = element == QStyle::SE_TabBarTabLeftButton ? tab->leftButtonSize : tab->rightButtonSize;
                if (buttonSize.isEmpty())
                {
                    rect = QRect();
                    return true;
                }

                const int y = shape.top() + (shape.height() - buttonSize.height()) / 2;
                const QRect button = element == QStyle::SE_TabBarTabLeftButton ? QRect(shape.left() + paddingLeft, y, buttonSize.width(), buttonSize.height())
                                                                              : QRect(shape.right() + 1 - paddingRight - buttonSize.width(), y, buttonSize.width(), buttonSize.height());
                rect = PDFWidgetStyleImpl::visualRect(option, shape, button);
                return true;
            }

            int left = shape.left() + paddingLeft;
            int right = shape.right() + 1 - paddingRight;

            if (!tab->icon.isNull())
            {
                left += (kind == TabKind::NavRow ? NAV_ICON + 10 : (kind == TabKind::Chip ? CHIP_ICON + CHIP_GAP : DOCUMENT_TAB_ICON + 8));
            }
            if (!tab->leftButtonSize.isEmpty())
            {
                left += tab->leftButtonSize.width() + 8;
            }
            if (!tab->rightButtonSize.isEmpty())
            {
                right -= tab->rightButtonSize.width() + 8;
            }

            rect = PDFWidgetStyleImpl::visualRect(option, shape, QRect(left, shape.top(), qMax(0, right - left), shape.height()));
            return true;
        }

        case QStyle::SE_TabBarTearIndicatorLeft:
        case QStyle::SE_TabBarTearIndicatorRight:
            rect = QRect();
            return true;

        case QStyle::SE_TabBarScrollLeftButton:
        case QStyle::SE_TabBarScrollRightButton:
        {
            // Two buttons of 28 px at the end of the bar, 2 px apart
            const QRect bar = option->rect;
            const bool isVerticalBar = bar.width() < bar.height();
            const bool isFirst = element == QStyle::SE_TabBarScrollLeftButton;
            const int offset = isFirst ? 2 * TAB_BUTTON + 2 : TAB_BUTTON;

            if (isVerticalBar)
            {
                rect = QRect(bar.left() + (bar.width() - TAB_BUTTON) / 2, bar.bottom() + 1 - offset, TAB_BUTTON, TAB_BUTTON);
            }
            else
            {
                const int y = bar.top() + qMax(0, bar.height() - DOCUMENT_TAB_HEIGHT) + (qMin(bar.height(), DOCUMENT_TAB_HEIGHT) - TAB_BUTTON) / 2;
                rect = PDFWidgetStyleImpl::visualRect(option, bar, QRect(bar.right() + 1 - offset, y, TAB_BUTTON, TAB_BUTTON));
            }
            return true;
        }

        case QStyle::SE_TabWidgetTabBar:
        case QStyle::SE_TabWidgetTabPane:
        case QStyle::SE_TabWidgetTabContents:
        {
            const QStyleOptionTabWidgetFrame* frame = qstyleoption_cast<const QStyleOptionTabWidgetFrame*>(option);
            if (!frame)
            {
                return false;
            }

            const QRect outer = option->rect;
            const bool isCard = PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CARD);
            const QSize barSize = frame->tabBarSize;
            const bool hasBar = !barSize.isEmpty();

            QRect bar;
            QRect pane = outer;

            switch (frame->shape)
            {
                case QTabBar::RoundedNorth:
                case QTabBar::TriangularNorth:
                {
                    // The row of chips has the margin of the focus ring around the chips
                    const int gap = (isCard ? CARD_GAP : PANE_GAP) - RING;
                    bar = QRect(outer.left(), outer.top(), qMin(barSize.width(), outer.width()), barSize.height());
                    if (hasBar)
                    {
                        pane.setTop(outer.top() + barSize.height() + gap);
                    }
                    pane.adjust(RING, 0, -RING, 0);
                    break;
                }

                case QTabBar::RoundedSouth:
                case QTabBar::TriangularSouth:
                {
                    const int gap = (isCard ? CARD_GAP : PANE_GAP) - RING;
                    bar = QRect(outer.left(), outer.bottom() + 1 - barSize.height(), qMin(barSize.width(), outer.width()), barSize.height());
                    if (hasBar)
                    {
                        pane.setBottom(outer.bottom() - barSize.height() - gap);
                    }
                    pane.adjust(RING, 0, -RING, 0);
                    break;
                }

                case QTabBar::RoundedWest:
                case QTabBar::TriangularWest:
                {
                    // Column of the navigation with its paddings, the pane lies beside it
                    const int top = NAV_COLUMN_PADDING_V - NAV_ROW_GAP / 2;
                    bar = QRect(outer.left(), outer.top() + top, NAV_COLUMN_WIDTH, qMin(barSize.height(), qMax(0, outer.height() - top)));
                    if (hasBar)
                    {
                        pane.setLeft(outer.left() + NAV_COLUMN_WIDTH);
                    }
                    break;
                }

                case QTabBar::RoundedEast:
                case QTabBar::TriangularEast:
                {
                    const int top = NAV_COLUMN_PADDING_V - NAV_ROW_GAP / 2;
                    bar = QRect(outer.right() + 1 - NAV_COLUMN_WIDTH, outer.top() + top, NAV_COLUMN_WIDTH, qMin(barSize.height(), qMax(0, outer.height() - top)));
                    if (hasBar)
                    {
                        pane.setRight(outer.right() - NAV_COLUMN_WIDTH);
                    }
                    break;
                }
            }

            if (element == QStyle::SE_TabWidgetTabBar)
            {
                rect = isVertical(frame->shape) ? bar : PDFWidgetStyleImpl::visualRect(option, outer, bar);
            }
            else if (element == QStyle::SE_TabWidgetTabPane)
            {
                rect = pane;
            }
            else
            {
                const int padding = (isCard || isVertical(frame->shape)) ? CARD_PADDING : 0;
                rect = pane.adjusted(padding, padding, -padding, -padding);
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleTabs::subControlRect(QStyle::ComplexControl control, const QStyleOptionComplex* option, QStyle::SubControl subControl, const QWidget* widget, QRect& rect) const
{
    if (control != QStyle::CC_ToolButton || !(isSegment(widget) || isChip(widget) || isTabBarButton(widget)))
    {
        return false;
    }

    rect = subControl == QStyle::SC_ToolButton ? option->rect : QRect();
    return true;
}

bool PDFWidgetStyleTabs::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const
{
    switch (type)
    {
        case QStyle::CT_TabBarTab:
        {
            const QStyleOptionTab* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
            if (!tab)
            {
                return false;
            }

            // The text of the option can be elided, the size comes from the whole text
            QString text = tab->text;
            if (const QTabBar* tabBar = qobject_cast<const QTabBar*>(widget))
            {
                if (tab->tabIndex >= 0 && tab->tabIndex < tabBar->count())
                {
                    text = tabBar->tabText(tab->tabIndex);
                }
            }

            const bool isSelected = tab->state.testFlag(QStyle::State_Selected);

            switch (getTabKind(tab, widget))
            {
                case TabKind::Chip:
                {
                    const int chipWidth = getChipWidth(text, !tab->icon.isNull(), getCount(tab, widget), !isSelected, widget);
                    size = QSize(chipWidth + CHIP_GAP, CHIP_HEIGHT + 2 * RING);
                    return true;
                }

                case TabKind::NavRow:
                    size = QSize(NAV_COLUMN_WIDTH, NAV_ROW_HEIGHT + NAV_ROW_GAP);
                    return true;

                case TabKind::Document:
                {
                    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::BodySemiBold);
                    int width = 16 + PDFWidgetStyleImpl::getTextWidth(text, font) + 10 + DOCUMENT_TAB_GAP;
                    if (!tab->icon.isNull())
                    {
                        width += DOCUMENT_TAB_ICON + 8;
                    }
                    if (!tab->leftButtonSize.isEmpty())
                    {
                        width += tab->leftButtonSize.width() + 8;
                    }
                    if (!tab->rightButtonSize.isEmpty())
                    {
                        width += tab->rightButtonSize.width() + 8;
                    }
                    size = QSize(width, DOCUMENT_TAB_HEIGHT + DOCUMENT_TAB_TOP);
                    return true;
                }
            }
            break;
        }

        case QStyle::CT_TabWidget:
        {
            const QStyleOptionTabWidgetFrame* frame = qstyleoption_cast<const QStyleOptionTabWidgetFrame*>(option);
            if (!frame)
            {
                return false;
            }

            const bool isCard = PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CARD);
            const int padding = (isCard || isVertical(frame->shape)) ? 2 * CARD_PADDING : 0;

            if (isVertical(frame->shape))
            {
                size = contentsSize + QSize(padding, qMax(padding, 2 * NAV_COLUMN_PADDING_V));
            }
            else
            {
                size = contentsSize + QSize(padding + 2 * RING, padding + (isCard ? CARD_GAP : PANE_GAP) - RING);
            }
            return true;
        }

        case QStyle::CT_ToolButton:
        {
            const QStyleOptionToolButton* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option);

            if (isSegment(widget))
            {
                // Natural size: paddings 14, icon 14, gap 6, label of 11 px. All segments
                // of a control have the width of the widest one; a control of a given
                // width (the theme switch of the sidebar) divides it equally.
                int width = 2 * SEGMENT_PADDING;
                if (toolButton)
                {
                    width += PDFWidgetStyleImpl::getTextWidth(toolButton->text, PDFWidgetStyleImpl::getFont(widget, Font::CaptionSemiBold));
                    if (!toolButton->icon.isNull())
                    {
                        width += 14 + (toolButton->text.isEmpty() ? 0 : 6);
                    }
                }
                size = QSize(width, SEGMENT_HEIGHT);
                return true;
            }

            if (isChip(widget))
            {
                const bool isChecked = option && option->state.testFlag(QStyle::State_On);
                size = QSize(toolButton ? getChipWidth(toolButton->text, !toolButton->icon.isNull() || isOptionChip(widget), getButtonChipCount(widget), !isChecked, widget) : CHIP_MIN_WIDTH, CHIP_HEIGHT);
                return true;
            }

            if (isTabBarButton(widget))
            {
                size = QSize(TAB_BUTTON, TAB_BUTTON);
                return true;
            }
            break;
        }

        case QStyle::CT_PushButton:
        {
            if (!isChip(widget))
            {
                break;
            }

            const QStyleOptionButton* button = qstyleoption_cast<const QStyleOptionButton*>(option);
            const bool isChecked = option && option->state.testFlag(QStyle::State_On);
            size = QSize(button ? getChipWidth(button->text, !button->icon.isNull() || isOptionChip(widget), getButtonChipCount(widget), !isChecked, widget) : CHIP_MIN_WIDTH, CHIP_HEIGHT);
            return true;
        }

        default:
            break;
    }

    return false;
}

void PDFWidgetStyleTabs::polish(QWidget* widget)
{
    QTabBar* tabBar = qobject_cast<QTabBar*>(widget);
    if (!tabBar || tabBar->property(SAVED_DRAW_BASE).isValid())
    {
        return;
    }

    tabBar->setProperty(SAVED_DRAW_BASE, tabBar->drawBase());
    tabBar->setProperty(SAVED_SCROLL_BUTTONS, tabBar->usesScrollButtons());
    tabBar->setProperty(SAVED_EXPANDING, tabBar->expanding());
    tabBar->setProperty(SAVED_ELIDE_MODE, static_cast<int>(tabBar->elideMode()));
    tabBar->setDrawBase(false);

    if (!PDFWidgetStyleImpl::hasProperty(tabBar, PDFWidgetStyleProperties::DOCUMENT_TABS))
    {
        // Chips and rows of the navigation keep their widths. They have no scroll
        // buttons by the design (SH_TabBar_PreferNoArrows): chips, which do not
        // fit, elide their labels down to the minimal width of a chip. That is
        // the minimal size of the tab bar, so an application with many tabs
        // turns the scroll buttons on (QTabBar::setUsesScrollButtons), which
        // is not changed here.
        tabBar->setExpanding(false);
    }
    else
    {
        // Only the tabs of the documents scroll. They keep their labels, the
        // tab bar would elide all of them before it shows the scroll buttons.
        tabBar->setUsesScrollButtons(true);
        tabBar->setExpanding(false);
        tabBar->setElideMode(Qt::ElideNone);
    }

    // The focus ring of a tab reaches into the gap between the tabs
    m_impl->watchTabBar(tabBar);
}

void PDFWidgetStyleTabs::unpolish(QWidget* widget)
{
    QTabBar* tabBar = qobject_cast<QTabBar*>(widget);
    if (!tabBar || !tabBar->property(SAVED_DRAW_BASE).isValid())
    {
        return;
    }

    m_impl->unwatchTabBar(tabBar);
    tabBar->setDrawBase(tabBar->property(SAVED_DRAW_BASE).toBool());
    tabBar->setUsesScrollButtons(tabBar->property(SAVED_SCROLL_BUTTONS).toBool());
    tabBar->setExpanding(tabBar->property(SAVED_EXPANDING).toBool());
    tabBar->setElideMode(static_cast<Qt::TextElideMode>(tabBar->property(SAVED_ELIDE_MODE).toInt()));
    tabBar->setProperty(SAVED_DRAW_BASE, QVariant());
    tabBar->setProperty(SAVED_SCROLL_BUTTONS, QVariant());
    tabBar->setProperty(SAVED_EXPANDING, QVariant());
    tabBar->setProperty(SAVED_ELIDE_MODE, QVariant());
}

void PDFWidgetStyleTabs::drawChip(QPainter* painter, const QRectF& rect, const PDFStyleState& state, bool isSelected, const QString& text, const QIcon& icon, int count, const QWidget* widget) const
{
    const Radii radii(CHIP_RADIUS);
    Color textColor = Color::TextSecondary;
    Color iconColor = Color::IconDefault;
    Color countColor = Color::TextMuted;

    if (isSelected)
    {
        // Selected chip has the subtle accent of an active toolbar button over its own
        // surface and no border; the full accent belongs only to the primary button
        if (!state.enabled)
        {
            PDFStylePainter::fill(painter, rect, radii, Color::BgMuted);
        }
        else
        {
            Color overlay = Color::BgAccentSubtle;
            if (state.pressed)
            {
                overlay = Color::BgAccentSubtlePressed;
            }
            else if (state.hover)
            {
                overlay = Color::BgAccentSubtleHover;
            }

            PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
            PDFStylePainter::fill(painter, rect, radii, overlay);
        }

        textColor = countColor = Color::TextAccent;
        iconColor = Color::IconAccent;
    }
    else
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
        PDFWidgetStyleImpl::drawStateOverlay(painter, rect, radii, state);

        // The focus ring replaces the border
        if (!state.focusVisible)
        {
            PDFStylePainter::strokeInside(painter, rect, radii, Color::BorderDefault, 1.0);
        }
    }

    if (!state.enabled)
    {
        textColor = countColor = Color::TextDisabled;
        iconColor = Color::IconDisabled;
    }

    if (state.focusVisible)
    {
        PDFStylePainter::focusRing(painter, rect, radii, false);
    }

    // An on / off option shows the check mark instead of its icon, when it is on
    const bool isOption = isOptionChip(widget);
    const bool isCheckShown = isOption && isSelected;
    const bool hasIcon = !icon.isNull() || isOption;

    // Icon 14, label Semi Bold 12 and the count Medium 11 are centered as a group
    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::SmallSemiBold);
    const QFont countFont = PDFThemeFonts::getFont(Font::Caption);
    const QString countText = count >= 0 ? QString::number(count) : QString();
    const qreal iconWidth = !hasIcon ? 0.0 : CHIP_ICON + (text.isEmpty() ? 0.0 : CHIP_GAP);
    const qreal countWidth = countText.isEmpty() ? 0.0 : CHIP_GAP + PDFWidgetStyleImpl::getTextWidth(countText, countFont);
    const qreal available = qMax(0.0, rect.width() - 2 * CHIP_PADDING);

    qreal textWidth = PDFWidgetStyleImpl::getTextWidth(text, font);
    QString shownText = text;
    if (iconWidth + textWidth + countWidth > available)
    {
        textWidth = qMax(0.0, available - iconWidth - countWidth);
        shownText = PDFWidgetStyleImpl::getElidedText(text, font, textWidth);
    }

    qreal x = qRound(rect.left() + CHIP_PADDING + (available - iconWidth - textWidth - countWidth) / 2.0);

    if (isCheckShown)
    {
        PDFStylePainter::icon(painter, placeInRow(rect, state.rightToLeft, x, CHIP_ICON, CHIP_ICON), QStringLiteral("check"), iconColor);
    }
    else if (!icon.isNull())
    {
        PDFWidgetStyleImpl::drawIcon(painter, placeInRow(rect, state.rightToLeft, x, CHIP_ICON, CHIP_ICON), icon, PDFWidgetStyleImpl::color(iconColor), state);
    }
    x += iconWidth;

    m_impl->drawText(painter, placeInRow(rect, state.rightToLeft, x, textWidth, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, shownText, font, PDFWidgetStyleImpl::color(textColor), widget);
    x += textWidth;

    if (!countText.isEmpty())
    {
        PDFStylePainter::text(painter, placeInRow(rect, state.rightToLeft, x + CHIP_GAP, countWidth - CHIP_GAP, rect.height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, countText, countFont, PDFWidgetStyleImpl::color(countColor));
    }
}

void PDFWidgetStyleTabs::drawTabShape(const QStyleOptionTab* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF shape(getTabShapeRect(option, widget));

    switch (getTabKind(option, widget))
    {
        case TabKind::Chip:
            // The chip is drawn as a whole with its label
            break;

        case TabKind::NavRow:
        {
            const Radii radii(NAV_ROW_RADIUS);

            if (state.selected)
            {
                // The selected item of the navigation does not react to the pointer
                PDFStylePainter::fill(painter, shape, radii, state.enabled ? Color::BgAccentSubtle : Color::BgMuted);
            }
            else
            {
                PDFWidgetStyleImpl::drawStateOverlay(painter, shape, radii, state);
            }

            if (state.focusVisible)
            {
                // Rows of lists and menus have the focus ring inside of their shape
                PDFStylePainter::strokeInside(painter, shape, radii, Color::BorderFocus, 2.0);
            }
            break;
        }

        case TabKind::Document:
        {
            const Radii radii(8.0, 8.0, 0.0, 0.0);

            if (state.selected)
            {
                // Selected tab continues to the toolbar below, it has no bottom border
                PDFStylePainter::fill(painter, shape, radii, Color::BgSubtle);

                painter->save();
                painter->setClipRect(shape.adjusted(0.0, 0.0, 0.0, -1.0), Qt::IntersectClip);
                PDFStylePainter::strokeInside(painter, shape.adjusted(0.0, 0.0, 0.0, 2.0), radii, Color::BorderDefault, 1.0);
                painter->restore();
            }
            else
            {
                PDFWidgetStyleImpl::drawStateOverlay(painter, shape, radii, state);
            }

            if (state.focusVisible)
            {
                // The strip clips the tabs, the focus ring is inside
                PDFStylePainter::strokeInside(painter, shape, radii, Color::BorderFocus, 2.0);
            }
            break;
        }
    }
}

void PDFWidgetStyleTabs::drawTabLabel(const QStyleOptionTab* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const TabKind kind = getTabKind(option, widget);
    const QRectF shape(getTabShapeRect(option, widget));

    if (kind == TabKind::Chip)
    {
        drawChip(painter, shape, state, state.selected, getTabText(option, widget), option->icon, getCount(option, widget), widget);
        return;
    }

    const QRectF textRect(style()->subElementRect(QStyle::SE_TabBarTabText, option, widget));
    const int alignment = Qt::AlignVCenter | (state.rightToLeft ? Qt::AlignRight : Qt::AlignLeft);

    if (kind == TabKind::NavRow)
    {
        // Icon 20 at the padding 12, the text is never rotated
        Color textColor = Color::TextSecondary;
        Color iconColor = Color::IconDefault;

        if (!state.enabled)
        {
            textColor = Color::TextDisabled;
            iconColor = Color::IconDisabled;
        }
        else if (state.selected)
        {
            textColor = Color::TextAccent;
            iconColor = Color::IconAccent;
        }

        if (!option->icon.isNull())
        {
            const QRectF iconRect(shape.left() + 12.0 + (option->leftButtonSize.isEmpty() ? 0.0 : option->leftButtonSize.width() + 8.0), shape.top() + (shape.height() - NAV_ICON) / 2.0, NAV_ICON, NAV_ICON);
            PDFWidgetStyleImpl::drawIcon(painter, PDFWidgetStyleImpl::visualRect(state.rightToLeft, shape, iconRect), option->icon, PDFWidgetStyleImpl::color(iconColor), state);
        }

        const QFont font = PDFWidgetStyleImpl::getFont(widget, state.selected ? Font::BodySemiBold : Font::Body);
        const QString text = PDFWidgetStyleImpl::getElidedText(getTabText(option, widget), font, textRect.width());
        m_impl->drawText(painter, textRect, alignment, text, font, PDFWidgetStyleImpl::color(textColor), widget);
        return;
    }

    // Document tab: icon 14, Semi Bold 13 for the selected tab, Medium 13 otherwise
    Color textColor = state.selected ? Color::TextPrimary : Color::TextTertiary;
    Color iconColor = state.selected ? Color::IconAccent : Color::IconMuted;

    if (!state.enabled)
    {
        textColor = Color::TextDisabled;
        iconColor = Color::IconDisabled;
    }

    if (!option->icon.isNull())
    {
        const QRectF iconRect(shape.left() + 16.0 + (option->leftButtonSize.isEmpty() ? 0.0 : option->leftButtonSize.width() + 8.0), shape.top() + (shape.height() - DOCUMENT_TAB_ICON) / 2.0, DOCUMENT_TAB_ICON, DOCUMENT_TAB_ICON);
        PDFWidgetStyleImpl::drawIcon(painter, PDFWidgetStyleImpl::visualRect(state.rightToLeft, shape, iconRect), option->icon, PDFWidgetStyleImpl::color(iconColor), state);
    }

    // The title of a document tab is data, the name of a file (components.md 6.6,
    // decision 9): elided in the middle keeping its extension, drawn in the
    // direction of the whole title (the alignment of the tab is kept)
    const QFont font = PDFWidgetStyleImpl::getFont(widget, state.selected ? Font::BodySemiBold : Font::BodyMedium);
    const QString title = getTabText(option, widget);
    const QString text = PDFComponentPainter::elidedFileName(title, font, textRect.width());
    PDFComponentPainter::drawDataText(painter, textRect, alignment | Qt::TextSingleLine | m_impl->getMnemonicFlag(widget), text, font, PDFWidgetStyleImpl::color(textColor),
                                      PDFComponentPainter::getDataTextDirection(title));
}

void PDFWidgetStyleTabs::drawSegment(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const
{
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF rect(option->rect);
    const Radii radii(SEGMENT_RADIUS);

    Color textColor = Color::TextMuted;
    Color iconColor = Color::IconMuted;

    if (state.on)
    {
        // Selected segment lies on the track as a raised plate (its shadow is painted
        // by the segmented control, the segment would clip it)
        PDFStylePainter::fill(painter, rect, radii, Color::BgSurface);
        textColor = Color::TextSecondary;
        iconColor = Color::IconDefault;
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
    else if (widget && widget->property(PDFWidgetStyleProperties::ICON_COLOR).isValid())
    {
        // Own color of the icon of the segment, the same in all enabled states
        const int value = widget->property(PDFWidgetStyleProperties::ICON_COLOR).toInt();
        if (value >= 0 && value < PDFThemeColors::ColorCount)
        {
            iconColor = static_cast<Color>(value);
        }
    }

    const QFont font = PDFWidgetStyleImpl::getFont(widget, state.on ? Font::CaptionSemiBold : Font::Caption);
    const qreal iconWidth = option->icon.isNull() ? 0.0 : 14.0 + (option->text.isEmpty() ? 0.0 : 6.0);
    const qreal available = qMax(0.0, rect.width() - 2 * SEGMENT_MIN_PADDING);

    qreal textWidth = PDFWidgetStyleImpl::getTextWidth(option->text, font);
    QString text = option->text;
    if (iconWidth + textWidth > available)
    {
        textWidth = qMax(0.0, available - iconWidth);
        text = PDFWidgetStyleImpl::getElidedText(option->text, font, textWidth);
    }

    qreal x = qRound(rect.left() + SEGMENT_MIN_PADDING + (available - iconWidth - textWidth) / 2.0);

    if (!option->icon.isNull())
    {
        PDFWidgetStyleImpl::drawIcon(painter, placeInRow(rect, state.rightToLeft, x, 14.0, 14.0), option->icon, PDFWidgetStyleImpl::color(iconColor), state);
        x += iconWidth;
    }

    m_impl->drawText(painter, placeInRow(rect, state.rightToLeft, x, textWidth, rect.height()), Qt::AlignVCenter | Qt::AlignLeft, text, font, PDFWidgetStyleImpl::color(textColor), widget);
}

void PDFWidgetStyleTabs::drawTabBarButton(const QStyleOptionToolButton* option, QPainter* painter, const QWidget* widget) const
{
    // Button of 28 px with radius 4 on the strip of the document tabs
    const PDFStyleState state = m_impl->getState(option, widget);
    const QRectF rect = PDFStylePainter::centered(QRectF(option->rect), QSizeF(qMin(TAB_BUTTON, option->rect.width()), qMin(TAB_BUTTON, option->rect.height())));
    const Radii radii(4.0);

    PDFStylePainter::fill(painter, rect, radii, Color::BgOverlay);
    PDFWidgetStyleImpl::drawStateOverlay(painter, rect, radii, state);

    const QRectF glyphRect = PDFStylePainter::centered(rect, QSizeF(12.0, 12.0));
    const Color color = state.enabled ? Color::IconDefault : Color::IconDisabled;

    if (option->features.testFlag(QStyleOptionToolButton::Arrow) && option->arrowType != Qt::NoArrow)
    {
        QString name = QStringLiteral("chevron-right");
        switch (option->arrowType)
        {
            case Qt::LeftArrow:
                name = QStringLiteral("chevron-left");
                break;
            case Qt::UpArrow:
                name = QStringLiteral("chevron-up");
                break;
            case Qt::DownArrow:
                name = QStringLiteral("chevron-down");
                break;
            default:
                break;
        }
        PDFStylePainter::icon(painter, glyphRect, name, color);
    }
    else if (!option->icon.isNull())
    {
        PDFWidgetStyleImpl::drawIcon(painter, glyphRect, option->icon, PDFWidgetStyleImpl::color(color), state);
    }
    else
    {
        PDFStylePainter::icon(painter, glyphRect, QStringLiteral("plus"), color);
    }
}

void PDFWidgetStyleTabs::drawTabWidgetFrame(const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    const QStyleOptionTabWidgetFrame* frame = qstyleoption_cast<const QStyleOptionTabWidgetFrame*>(option);
    const QTabWidget* tabWidget = qobject_cast<const QTabWidget*>(widget);

    if (frame && tabWidget && isVertical(frame->shape))
    {
        // Column of the navigation: sidebar fill with a border on the side of the pane
        const QRect outer = tabWidget->rect();
        const bool isEast = frame->shape == QTabBar::RoundedEast || frame->shape == QTabBar::TriangularEast;
        const QRectF column = isEast ? QRectF(outer.right() + 1 - NAV_COLUMN_WIDTH, outer.top(), NAV_COLUMN_WIDTH, outer.height())
                                     : QRectF(outer.left(), outer.top(), NAV_COLUMN_WIDTH, outer.height());
        PDFStylePainter::fillRect(painter, column, Color::BgSidebar);
        PDFStylePainter::fillRect(painter, isEast ? QRectF(column.left(), column.top(), 1.0, column.height()) : QRectF(column.right() - 1.0, column.top(), 1.0, column.height()), Color::BorderDefault);
        return;
    }

    if (PDFWidgetStyleImpl::hasProperty(widget, PDFWidgetStyleProperties::CARD))
    {
        // Pane as a settings card
        const QRectF rect(option->rect);
        PDFStylePainter::fill(painter, rect, Radii(12.0), Color::BgSurface);
        PDFStylePainter::strokeInside(painter, rect, Radii(12.0), Color::BorderDefault, 1.0);
    }
}

}   // namespace pdf
