#include "pdfwidgetstyleimpl.h"
#include "pdfthemeiconengine.h"

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QStyleOptionMenuItem>

namespace pdf
{

/// Menus and the menu bar (section 10 of the design)
class PDFWidgetStyleMenus : public PDFWidgetStyleArea
{
public:
    using PDFWidgetStyleArea::PDFWidgetStyleArea;

    virtual bool drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const override;

    // Geometry of the design
    static constexpr int ITEM_HEIGHT = 32;
    static constexpr int ITEM_RADIUS = 6;
    static constexpr int ITEM_PADDING = 10;
    static constexpr int ITEM_ICON = 16;
    static constexpr int ITEM_GAP = 10;
    static constexpr int ITEM_SHORTCUT_GAP = 24;
    static constexpr int ITEM_MIN_WIDTH = 186;      ///< Panel of 200 px without its border and padding
    static constexpr int SEPARATOR_HEIGHT = 7;
    static constexpr int SEPARATOR_MARGIN = 4;
    static constexpr int TITLE_HEIGHT = 24;
    static constexpr int SCROLLER_HEIGHT = 20;
    static constexpr int MENUBAR_HEIGHT = 36;
    static constexpr int MENUBAR_ITEM_HEIGHT = 28;
    static constexpr int MENUBAR_ITEM_PADDING = 10;

private:
    static bool hasRightSlot(const QStyleOptionMenuItem* option);
    static bool isDestructive(const QStyleOptionMenuItem* option, const QWidget* widget);

    void drawMenuItem(const QStyleOptionMenuItem* option, QPainter* painter, const QWidget* widget) const;
    void drawMenuBarItem(const QStyleOptionMenuItem* option, QPainter* painter, const QWidget* widget) const;
};

std::unique_ptr<PDFWidgetStyleArea> PDFWidgetStyleAreas::createMenus(PDFWidgetStyleImpl* impl)
{
    return std::make_unique<PDFWidgetStyleMenus>(impl);
}

bool PDFWidgetStyleMenus::hasRightSlot(const QStyleOptionMenuItem* option)
{
    // The right slot holds the check mark or the arrow of a submenu
    return option->menuItemType == QStyleOptionMenuItem::SubMenu ||
           (option->checkType == QStyleOptionMenuItem::NonExclusive && option->checked);
}

bool PDFWidgetStyleMenus::isDestructive(const QStyleOptionMenuItem* option, const QWidget* widget)
{
    // The property is set on the action of the item
    const QMenu* menu = qobject_cast<const QMenu*>(widget);
    if (!menu)
    {
        return false;
    }

    const QAction* action = menu->actionAt(option->rect.center());
    return action && PDFWidgetStyleProperties::isSet(action, PDFWidgetStyleProperties::DESTRUCTIVE);
}

bool PDFWidgetStyleMenus::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::PE_PanelMenu:
        {
            const PDFWidgetStyleImpl::PopupKind kind = PDFWidgetStyleImpl::getPopupKind(widget);

            if (kind == PDFWidgetStyleImpl::PopupKind::Popover)
            {
                PDFWidgetStyleImpl::drawPopupPanel(painter, option->rect, widget, kind, 12.0, Color::BorderDefault);
            }
            else
            {
                PDFWidgetStyleImpl::drawPopupPanel(painter, option->rect, widget, PDFWidgetStyleImpl::PopupKind::Menu, 10.0, Color::BorderDefault);
            }
            return true;
        }

        case QStyle::PE_FrameMenu:
            // The border belongs to the panel
            return true;

        case QStyle::PE_PanelMenuBar:
        {
            const QRectF rect(option->rect);
            PDFStylePainter::fillRect(painter, rect, Color::BgSubtle);
            PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.bottom() - 1.0, rect.width(), 1.0), Color::BorderDefault);
            return true;
        }

        case QStyle::PE_IndicatorMenuCheckMark:
        {
            const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
            const QStyleOptionMenuItem* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            const QRectF rect(option->rect);
            const Color color = isEnabled ? Color::IconDefault : Color::IconMuted;

            if (menuItem && menuItem->checkType == QStyleOptionMenuItem::Exclusive)
            {
                PDFStylePainter::fillEllipse(painter, PDFStylePainter::centered(rect, QSizeF(6.0, 6.0)), PDFWidgetStyleImpl::color(color));
            }
            else
            {
                PDFStylePainter::icon(painter, PDFStylePainter::centered(rect, QSizeF(ITEM_ICON, ITEM_ICON)), QStringLiteral("check"), color);
            }
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleMenus::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::CE_MenuItem:
        {
            if (const QStyleOptionMenuItem* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option))
            {
                drawMenuItem(menuItem, painter, widget);
            }
            return true;
        }

        case QStyle::CE_MenuScroller:
        {
            // Row of 20 px with a chevron of 12 px
            const QRectF rect(option->rect);
            const bool isDown = option->state.testFlag(QStyle::State_DownArrow);
            PDFStylePainter::fillRect(painter, rect, Color::BgSurface);
            PDFStylePainter::icon(painter, PDFStylePainter::centered(rect, QSizeF(12.0, 12.0)), isDown ? QStringLiteral("chevron-down") : QStringLiteral("chevron-up"), Color::IconMuted);
            return true;
        }

        case QStyle::CE_MenuTearoff:
        case QStyle::CE_MenuHMargin:
        case QStyle::CE_MenuVMargin:
        case QStyle::CE_MenuEmptyArea:
            // The panel is drawn below, there is nothing more
            return true;

        case QStyle::CE_MenuBarItem:
        {
            if (const QStyleOptionMenuItem* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option))
            {
                drawMenuBarItem(menuItem, painter, widget);
            }
            return true;
        }

        case QStyle::CE_MenuBarEmptyArea:
        {
            QStyleOption panelOption = *option;
            if (widget)
            {
                // The border runs along the whole bar
                panelOption.rect = widget->rect();
            }
            style()->drawPrimitive(QStyle::PE_PanelMenuBar, &panelOption, painter, widget);
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleMenus::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const
{
    switch (type)
    {
        case QStyle::CT_MenuItem:
        {
            const QStyleOptionMenuItem* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
            if (!menuItem)
            {
                size = QSize(qMax(ITEM_MIN_WIDTH, contentsSize.width() + 2 * ITEM_PADDING + ITEM_ICON + ITEM_GAP), ITEM_HEIGHT);
                return true;
            }

            if (menuItem->menuItemType == QStyleOptionMenuItem::Separator)
            {
                if (menuItem->text.isEmpty())
                {
                    size = QSize(ITEM_MIN_WIDTH, SEPARATOR_HEIGHT);
                }
                else
                {
                    const int textWidth = PDFWidgetStyleImpl::getTextWidth(menuItem->text, PDFThemeFonts::getFont(Font::Caption));
                    size = QSize(qMax(ITEM_MIN_WIDTH, textWidth + 2 * ITEM_PADDING), TITLE_HEIGHT);
                }
                return true;
            }

            // 10 + icon 16 + 10 + text (+ 24 before the shortcut) + 10 (+ right slot 16 + 10).
            // The width of the shortcut itself is added by the menu.
            const QString label = menuItem->text.section(QLatin1Char('\t'), 0, 0);
            const bool hasShortcut = menuItem->text.contains(QLatin1Char('\t'));

            int width = ITEM_PADDING + ITEM_ICON + ITEM_GAP + PDFWidgetStyleImpl::getTextWidth(label, PDFWidgetStyleImpl::getFont(widget, Font::Body)) + ITEM_PADDING;
            if (hasShortcut)
            {
                width += ITEM_SHORTCUT_GAP;
            }
            if (menuItem->menuItemType == QStyleOptionMenuItem::SubMenu || menuItem->checkType != QStyleOptionMenuItem::NotCheckable)
            {
                width += ITEM_ICON + ITEM_GAP;
            }

            size = QSize(qMax(ITEM_MIN_WIDTH, width), ITEM_HEIGHT);
            return true;
        }

        case QStyle::CT_MenuBarItem:
        {
            int textWidth = contentsSize.width();
            if (const QStyleOptionMenuItem* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option))
            {
                if (!menuItem->text.isEmpty())
                {
                    textWidth = PDFWidgetStyleImpl::getTextWidth(menuItem->text, PDFWidgetStyleImpl::getFont(widget, Font::BodyMedium));
                }
            }

            size = QSize(textWidth + 2 * MENUBAR_ITEM_PADDING, MENUBAR_ITEM_HEIGHT);
            return true;
        }

        case QStyle::CT_MenuBar:
            size = QSize(contentsSize.width(), qMax(contentsSize.height(), MENUBAR_HEIGHT));
            return true;

        case QStyle::CT_Menu:
            size = contentsSize;
            return true;

        default:
            break;
    }

    return false;
}

void PDFWidgetStyleMenus::drawMenuItem(const QStyleOptionMenuItem* option, QPainter* painter, const QWidget* widget) const
{
    const QRectF rect(option->rect);
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool isSelected = isEnabled && option->state.testFlag(QStyle::State_Selected);
    const bool isPressed = isEnabled && option->state.testFlag(QStyle::State_Sunken);
    const bool isRightToLeft = option->direction == Qt::RightToLeft;

    switch (option->menuItemType)
    {
        case QStyleOptionMenuItem::Separator:
        {
            if (option->text.isEmpty())
            {
                // Line of 1 px with margins 3 above and below and 4 at the sides
                PDFStylePainter::fillRect(painter, QRectF(rect.left() + SEPARATOR_MARGIN, rect.top() + 3.0, rect.width() - 2 * SEPARATOR_MARGIN, 1.0), Color::BorderDivider);
            }
            else
            {
                // Title of a section: Medium 11, paddings 6 / 10 / 2
                const QRectF textRect = rect.adjusted(ITEM_PADDING, 6.0, -ITEM_PADDING, -2.0);
                PDFStylePainter::text(painter, textRect, Qt::AlignVCenter | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::TextSingleLine | Qt::TextHideMnemonic,
                                      option->text, PDFThemeFonts::getFont(Font::Caption), PDFWidgetStyleImpl::color(Color::TextMuted));
            }
            return;
        }

        case QStyleOptionMenuItem::TearOff:
        case QStyleOptionMenuItem::EmptyArea:
            return;

        case QStyleOptionMenuItem::Scroller:
        {
            QStyleOption scrollerOption = *option;
            style()->drawControl(QStyle::CE_MenuScroller, &scrollerOption, painter, widget);
            return;
        }

        default:
            break;
    }

    if (isPressed)
    {
        PDFStylePainter::fill(painter, rect, Radii(ITEM_RADIUS), Color::BgPressed);
    }
    else if (isSelected)
    {
        PDFStylePainter::fill(painter, rect, Radii(ITEM_RADIUS), Color::BgHover);
    }

    const bool destructive = isDestructive(option, widget);

    Color textColor = Color::TextSecondary;
    Color iconColor = Color::IconDefault;
    if (!isEnabled)
    {
        textColor = Color::TextMuted;
        iconColor = Color::IconMuted;
    }
    else if (destructive)
    {
        textColor = Color::TextDanger;
        iconColor = Color::IconDanger;
    }

    auto place = [&rect, isRightToLeft](qreal left, qreal width, qreal height)
    {
        const QRectF item(left, rect.top() + (rect.height() - height) / 2.0, width, height);
        return PDFWidgetStyleImpl::visualRect(isRightToLeft, rect, item);
    };

    // Icon slot of 16 px. The mark of an exclusive action is a dot in this slot.
    const QRectF iconRect = place(rect.left() + ITEM_PADDING, ITEM_ICON, ITEM_ICON);
    const bool isExclusiveChecked = option->checkType == QStyleOptionMenuItem::Exclusive && option->checked;

    if (isExclusiveChecked)
    {
        PDFStylePainter::fillEllipse(painter, PDFStylePainter::centered(iconRect, QSizeF(6.0, 6.0)), PDFWidgetStyleImpl::color(isEnabled ? Color::IconDefault : Color::IconMuted));
    }
    else if (!option->icon.isNull())
    {
        PDFStyleState iconState;
        iconState.enabled = isEnabled;
        PDFWidgetStyleImpl::drawIcon(painter, iconRect, option->icon, PDFWidgetStyleImpl::color(iconColor), iconState);
    }

    // Right slot of 16 px: check mark or arrow of a submenu
    qreal right = rect.right() - ITEM_PADDING;

    if (option->menuItemType == QStyleOptionMenuItem::SubMenu)
    {
        PDFStylePainter::icon(painter, place(right - 14.0, 14.0, 14.0), isRightToLeft ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right"), Color::IconMuted);
        right -= ITEM_ICON + ITEM_GAP;
    }
    else if (option->checkType == QStyleOptionMenuItem::NonExclusive && option->checked)
    {
        PDFStylePainter::icon(painter, place(right - ITEM_ICON, ITEM_ICON, ITEM_ICON), QStringLiteral("check"), isEnabled ? Color::IconDefault : Color::IconMuted);
        right -= ITEM_ICON + ITEM_GAP;
    }

    const QString label = option->text.section(QLatin1Char('\t'), 0, 0);
    const QString shortcut = option->text.section(QLatin1Char('\t'), 1);

    if (!shortcut.isEmpty())
    {
        const QFont shortcutFont = PDFThemeFonts::getFont(Font::Small);
        const qreal shortcutWidth = PDFWidgetStyleImpl::getTextWidth(shortcut, shortcutFont);
        PDFStylePainter::text(painter, place(right - shortcutWidth, shortcutWidth, rect.height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine | Qt::TextHideMnemonic,
                              shortcut, shortcutFont, PDFWidgetStyleImpl::color(Color::TextMuted));
        right -= shortcutWidth + ITEM_GAP;
    }

    const qreal textLeft = rect.left() + ITEM_PADDING + ITEM_ICON + ITEM_GAP;
    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::Body);
    const QString text = PDFWidgetStyleImpl::getElidedText(label, font, right - textLeft);
    m_impl->drawText(painter, place(textLeft, qMax(0.0, right - textLeft), rect.height()), Qt::AlignVCenter | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft),
                     text, font, PDFWidgetStyleImpl::color(textColor), widget);
}

void PDFWidgetStyleMenus::drawMenuBarItem(const QStyleOptionMenuItem* option, QPainter* painter, const QWidget* widget) const
{
    const QRectF rect(option->rect);
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool isSelected = isEnabled && option->state.testFlag(QStyle::State_Selected);
    const bool isOpen = isEnabled && option->state.testFlag(QStyle::State_Sunken);

    // The item is drawn clipped to its rectangle, so it draws the bar below itself
    PDFStylePainter::fillRect(painter, rect, Color::BgSubtle);

    const QRectF shape = PDFStylePainter::centered(rect, QSizeF(rect.width(), qMin<qreal>(MENUBAR_ITEM_HEIGHT, rect.height())));
    Color textColor = isEnabled ? Color::TextSecondary : Color::TextMuted;

    if (isOpen)
    {
        PDFStylePainter::fill(painter, shape, Radii(ITEM_RADIUS), Color::BgAccentSubtle);
        textColor = Color::TextAccent;
    }
    else if (isSelected)
    {
        PDFStylePainter::fill(painter, shape, Radii(ITEM_RADIUS), Color::BgHover);
    }

    if (isSelected && !isOpen && m_impl->isKeyboardModality() && option->state.testFlag(QStyle::State_HasFocus))
    {
        PDFStylePainter::strokeInside(painter, shape, Radii(ITEM_RADIUS), Color::BorderFocus, 2.0);
    }

    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::BodyMedium);

    if (!option->icon.isNull() && option->text.isEmpty())
    {
        PDFStyleState iconState;
        iconState.enabled = isEnabled;
        PDFWidgetStyleImpl::drawIcon(painter, PDFStylePainter::centered(shape, QSizeF(ITEM_ICON, ITEM_ICON)), option->icon,
                                     PDFWidgetStyleImpl::color(isEnabled ? Color::IconDefault : Color::IconMuted), iconState);
        return;
    }

    m_impl->drawText(painter, shape, Qt::AlignCenter, option->text, font, PDFWidgetStyleImpl::color(textColor), widget);
}

}   // namespace pdf
