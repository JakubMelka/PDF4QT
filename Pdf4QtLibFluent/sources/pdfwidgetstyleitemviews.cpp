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
#include <QApplication>
#include <QComboBox>
#include <QFontMetrics>
#include <QHeaderView>
#include <QPainter>
#include <QStyleOptionHeader>
#include <QStyleOptionViewItem>
#include <QTextLayout>
#include <QTreeView>

namespace pdf
{

/// Item views and their headers (sections 12 and 13 of the design)
class PDFWidgetStyleItemViews : public PDFWidgetStyleArea
{
public:
    using PDFWidgetStyleArea::PDFWidgetStyleArea;

    virtual bool drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const override;
    virtual bool subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const override;
    virtual bool sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const override;
    virtual void polish(QWidget* widget) override;
    virtual void unpolish(QWidget* widget) override;

    // Geometry of the design
    static constexpr int ROW_HEIGHT = 30;
    static constexpr int ROW_RADIUS = 6;
    static constexpr int ROW_PADDING = 8;
    static constexpr int ROW_GAP = 8;
    static constexpr int ROW_CHECK = 18;
    static constexpr int ROW_ICON = 16;
    static constexpr int POPUP_ROW_HEIGHT = 32;
    static constexpr int POPUP_ROW_GAP = 2;
    static constexpr int POPUP_ROW_PADDING = 10;
    static constexpr int HEADER_HEIGHT = 28;
    static constexpr int HEADER_PADDING = 12;
    static constexpr int HEADER_ARROW = 12;
    static constexpr int HEADER_ARROW_GAP = 4;
    static constexpr int HEADER_VERTICAL_WIDTH = 40;

    /// The row of a tree has the padding of 8 px before the slot of the branch,
    /// the tree view starts the slot at the edge of the row. The chevron and
    /// the contents of the item are shifted by this distance.
    static constexpr int TREE_BRANCH_SHIFT = 8;

    static constexpr const char* SAVED_HEADER_ALIGNMENT = "_pdf4qt_savedHeaderAlignment";
    static constexpr const char* SAVED_TREE_ANIMATED = "_pdf4qt_savedTreeAnimated";

private:
    struct ItemLayout
    {
        QRect shape;        ///< Shape of the background of the item
        QRect check;
        QRect decoration;
        QRect text;
    };

    static bool isComboPopup(const QWidget* widget);
    static bool isTreeBranchColumn(const QStyleOptionViewItem* option, const QWidget* widget);
    /// Part of the tree column, for which the panel of a row is drawn
    enum class TreeArea
    {
        None,       ///< Another column, or an item without the slot of a branch
        Branch,     ///< Area of the branches before the item
        Item        ///< Item, which follows the area of the branches
    };

    static int getTreeColumn(const QTreeView* treeView);
    static TreeArea getTreeArea(const QRect& rect, const QWidget* widget);
    static QRect getDisclosureRect(const QStyleOption* option);
    static QSize getDecorationSize(const QStyleOptionViewItem* option);
    static QFont getItemFont(const QStyleOptionViewItem* option, const QWidget* widget, bool isSelected);
    static bool isViewFocused(const QWidget* widget);
    static bool isEdited(const QStyleOptionViewItem* option, const QWidget* widget);

    ItemLayout getItemLayout(const QStyleOptionViewItem* option, const QWidget* widget) const;
    Radii getItemRadii(const QStyleOptionViewItem* option, const QWidget* widget) const;
    void drawItemBackground(const QStyleOptionViewItem* option, QPainter* painter, const QWidget* widget) const;
    void drawItem(const QStyleOptionViewItem* option, QPainter* painter, const QWidget* widget) const;
    void drawHeaderSection(const QStyleOptionHeader* option, QPainter* painter, const QWidget* widget) const;
    void drawHeaderLabel(const QStyleOptionHeader* option, QPainter* painter, const QWidget* widget) const;
    QRect getHeaderRect(const QStyleOptionHeader* option, const QWidget* widget, bool isArrow) const;
};

std::unique_ptr<PDFWidgetStyleArea> PDFWidgetStyleAreas::createItemViews(PDFWidgetStyleImpl* impl)
{
    return std::make_unique<PDFWidgetStyleItemViews>(impl);
}

bool PDFWidgetStyleItemViews::isComboPopup(const QWidget* widget)
{
    return widget && widget->parentWidget() && widget->parentWidget()->inherits("QComboBoxPrivateContainer");
}

bool PDFWidgetStyleItemViews::isTreeBranchColumn(const QStyleOptionViewItem* option, const QWidget* widget)
{
    // The item of the tree column continues the shape, which starts in the area of the branches
    const QTreeView* treeView = qobject_cast<const QTreeView*>(widget);
    if (!treeView || !option->index.isValid())
    {
        return false;
    }

    if (option->index.column() != getTreeColumn(treeView))
    {
        return false;
    }

    return treeView->rootIsDecorated() || option->index.parent().isValid();
}

int PDFWidgetStyleItemViews::getTreeColumn(const QTreeView* treeView)
{
    // The tree lies in the first visual column, unless the view says another one
    if (treeView->treePosition() >= 0)
    {
        return treeView->treePosition();
    }

    return treeView->header() ? treeView->header()->logicalIndex(0) : 0;
}

PDFWidgetStyleItemViews::TreeArea PDFWidgetStyleItemViews::getTreeArea(const QRect& rect, const QWidget* widget)
{
    const QTreeView* treeView = qobject_cast<const QTreeView*>(widget);
    if (!treeView || rect.isEmpty())
    {
        return TreeArea::None;
    }

    const QModelIndex index = treeView->indexAt(rect.center());
    if (!index.isValid() || index.column() != getTreeColumn(treeView))
    {
        return TreeArea::None;
    }

    // The area of the branches lies outside of the rectangle of the item
    if (!treeView->visualRect(index).intersects(rect))
    {
        return TreeArea::Branch;
    }

    return (treeView->rootIsDecorated() || index.parent().isValid()) ? TreeArea::Item : TreeArea::None;
}

QRect PDFWidgetStyleItemViews::getDisclosureRect(const QStyleOption* option)
{
    // The slot of the chevron is the last indentation step of the item
    const int shift = option->direction == Qt::RightToLeft ? -TREE_BRANCH_SHIFT : TREE_BRANCH_SHIFT;
    return option->rect.translated(shift, 0);
}

QSize PDFWidgetStyleItemViews::getDecorationSize(const QStyleOptionViewItem* option)
{
    if (!option->features.testFlag(QStyleOptionViewItem::HasDecoration))
    {
        return QSize();
    }

    QSize size = option->decorationSize;
    if (!size.isValid() || size.isEmpty())
    {
        size = QSize(ROW_ICON, ROW_ICON);
    }

    return size;
}

QFont PDFWidgetStyleItemViews::getItemFont(const QStyleOptionViewItem* option, const QWidget* widget, bool isSelected)
{
    // The font of the item comes from the model, or from the view
    QFont font = option->font;

    if (!option->index.isValid() || !option->index.data(Qt::FontRole).isValid())
    {
        font = PDFWidgetStyleImpl::getFont(widget, isComboPopup(widget) || !isSelected ? Font::Body : Font::BodyMedium);

        if (isSelected && isComboPopup(widget))
        {
            font = PDFWidgetStyleImpl::getFont(widget, Font::BodyMedium);
        }
    }

    return font;
}

bool PDFWidgetStyleItemViews::isViewFocused(const QWidget* widget)
{
    if (!widget)
    {
        return true;
    }

    if (isComboPopup(widget))
    {
        return true;
    }

    // An editor of a cell keeps the selection of its view active
    const QWidget* focusWidget = QApplication::focusWidget();
    return focusWidget && (focusWidget == widget || widget->isAncestorOf(focusWidget));
}

bool PDFWidgetStyleItemViews::isEdited(const QStyleOptionViewItem* option, const QWidget* widget)
{
    // The editor of the item is a child of the viewport, which has the focus
    const QAbstractItemView* view = qobject_cast<const QAbstractItemView*>(widget);
    const QWidget* focusWidget = QApplication::focusWidget();

    if (!view || !focusWidget || !view->viewport() || focusWidget == view->viewport())
    {
        return false;
    }

    const QWidget* editor = focusWidget;
    while (editor && editor->parentWidget() != view->viewport())
    {
        editor = editor->parentWidget();
    }

    return editor && editor->geometry().intersects(option->rect) && editor->geometry().center().y() >= option->rect.top() &&
           editor->geometry().center().y() <= option->rect.bottom() && option->rect.contains(editor->geometry().center());
}

PDFWidgetStyleItemViews::ItemLayout PDFWidgetStyleItemViews::getItemLayout(const QStyleOptionViewItem* option, const QWidget* widget) const
{
    ItemLayout layout;
    const bool isPopup = isComboPopup(widget);
    const QRect rect = option->rect;

    // Rows of a popup list are 2 px apart
    layout.shape = isPopup ? rect.adjusted(0, POPUP_ROW_GAP / 2, 0, -POPUP_ROW_GAP / 2) : rect;

    const int padding = isPopup ? POPUP_ROW_PADDING : ROW_PADDING;
    const int gap = isPopup ? 10 : ROW_GAP;
    const QSize decorationSize = getDecorationSize(option);
    const bool hasCheck = option->features.testFlag(QStyleOptionViewItem::HasCheckIndicator);
    const bool hasDecoration = !decorationSize.isEmpty();

    if (option->decorationPosition == QStyleOptionViewItem::Top || option->decorationPosition == QStyleOptionViewItem::Bottom)
    {
        // Icon mode: the decoration above (below) the text, both centered
        const QRect contents = layout.shape.adjusted(padding, padding, -padding, -padding);
        const bool isTop = option->decorationPosition == QStyleOptionViewItem::Top;

        if (hasDecoration)
        {
            const int x = contents.left() + (contents.width() - decorationSize.width()) / 2;
            const int y = isTop ? contents.top() : contents.bottom() + 1 - decorationSize.height();
            layout.decoration = QRect(QPoint(x, y), decorationSize);
        }

        layout.text = contents;
        if (hasDecoration)
        {
            if (isTop)
            {
                layout.text.setTop(layout.decoration.bottom() + 1 + 6);
            }
            else
            {
                layout.text.setBottom(layout.decoration.top() - 6);
            }
        }

        if (hasCheck)
        {
            layout.check = QRect(contents.left(), contents.top(), ROW_CHECK, ROW_CHECK);
        }
        return layout;
    }

    int left = layout.shape.left() + padding;
    int right = layout.shape.right() + 1 - padding;

    auto centered = [&layout](int x, const QSize& size)
    {
        return QRect(x, layout.shape.top() + (layout.shape.height() - size.height()) / 2, size.width(), size.height());
    };

    if (isTreeBranchColumn(option, widget))
    {
        // The slot of the branch lies 8 px inside of the row and the contents
        // of the item follow it after the gap
        left += TREE_BRANCH_SHIFT;
    }

    if (hasCheck)
    {
        layout.check = centered(left, QSize(ROW_CHECK, ROW_CHECK));
        left += ROW_CHECK + gap;
    }

    if (hasDecoration)
    {
        if (option->decorationPosition == QStyleOptionViewItem::Right)
        {
            layout.decoration = centered(right - decorationSize.width(), decorationSize);
            right -= decorationSize.width() + gap;
        }
        else
        {
            layout.decoration = centered(left, decorationSize);
            left += decorationSize.width() + gap;
        }
    }

    if (isPopup)
    {
        // Space of the check mark of the current item
        const QComboBox* comboBox = qobject_cast<const QComboBox*>(widget->parentWidget()->parentWidget());
        if (comboBox && option->index.isValid() && comboBox->currentIndex() == option->index.row())
        {
            right -= ROW_ICON + gap;
        }
    }

    layout.text = QRect(left, layout.shape.top(), qMax(0, right - left), layout.shape.height());

    // Mirror the layout for the right-to-left direction
    layout.check = PDFWidgetStyleImpl::visualRect(option, layout.shape, layout.check);
    layout.decoration = PDFWidgetStyleImpl::visualRect(option, layout.shape, layout.decoration);
    layout.text = PDFWidgetStyleImpl::visualRect(option, layout.shape, layout.text);
    return layout;
}

PDFWidgetStyleArea::Radii PDFWidgetStyleItemViews::getItemRadii(const QStyleOptionViewItem* option, const QWidget* widget) const
{
    // The selection of a row of a table is one shape, only its outer cells are rounded
    bool roundLeft = true;
    bool roundRight = true;

    switch (option->viewItemPosition)
    {
        case QStyleOptionViewItem::Beginning:
            roundRight = false;
            break;

        case QStyleOptionViewItem::Middle:
            roundLeft = false;
            roundRight = false;
            break;

        case QStyleOptionViewItem::End:
            roundLeft = false;
            break;

        default:
            break;
    }

    if (isTreeBranchColumn(option, widget))
    {
        roundLeft = false;
    }

    if (option->direction == Qt::RightToLeft)
    {
        std::swap(roundLeft, roundRight);
    }

    const qreal left = roundLeft ? ROW_RADIUS : 0.0;
    const qreal right = roundRight ? ROW_RADIUS : 0.0;
    return Radii(left, right, right, left);
}

bool PDFWidgetStyleItemViews::drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::PE_PanelItemViewItem:
        {
            if (const QStyleOptionViewItem* viewItem = qstyleoption_cast<const QStyleOptionViewItem*>(option))
            {
                drawItemBackground(viewItem, painter, widget);
            }
            return true;
        }

        case QStyle::PE_PanelItemViewRow:
        {
            const QStyleOptionViewItem* viewItem = qstyleoption_cast<const QStyleOptionViewItem*>(option);
            if (!viewItem)
            {
                return true;
            }

            QRectF rect(option->rect);

            // The area of the branches of a tree starts the shape of the selected
            // row. The tree view draws this primitive also below each item (for
            // the alternating rows), where the fill belongs to the item. The fill
            // of the area of the branches continues below the chevron, which
            // reaches into the item, so the start of the item belongs to it.
            const TreeArea treeArea = getTreeArea(option->rect, widget);
            const bool isBranchArea = treeArea == TreeArea::Branch;
            const bool isRightToLeft = option->direction == Qt::RightToLeft;

            if (treeArea == TreeArea::Branch)
            {
                if (isRightToLeft)
                {
                    rect.setLeft(rect.left() - TREE_BRANCH_SHIFT);
                }
                else
                {
                    rect.setRight(rect.right() + TREE_BRANCH_SHIFT);
                }
            }
            else if (treeArea == TreeArea::Item)
            {
                if (isRightToLeft)
                {
                    rect.setRight(rect.right() - TREE_BRANCH_SHIFT);
                }
                else
                {
                    rect.setLeft(rect.left() + TREE_BRANCH_SHIFT);
                }
            }

            if (viewItem->features.testFlag(QStyleOptionViewItem::Alternate))
            {
                PDFStylePainter::fillRect(painter, rect, Color::BgSubtle);
            }

            const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);

            if (isBranchArea && isEnabled)
            {
                const Radii radii = isRightToLeft ? Radii(0.0, ROW_RADIUS, ROW_RADIUS, 0.0) : Radii(ROW_RADIUS, 0.0, 0.0, ROW_RADIUS);
                const bool isHovered = option->state.testFlag(QStyle::State_MouseOver);

                if (option->state.testFlag(QStyle::State_Selected))
                {
                    Color fill = isHovered ? Color::BgAccentSubtleHover : Color::BgAccentSubtle;
                    if (!option->state.testFlag(QStyle::State_Active) || !isViewFocused(widget))
                    {
                        fill = Color::BgMuted;
                    }
                    PDFStylePainter::fill(painter, rect, radii, fill);
                }
                else if (isHovered)
                {
                    PDFStylePainter::fill(painter, rect, radii, Color::BgHover);
                }
            }
            return true;
        }

        case QStyle::PE_IndicatorBranch:
        {
            // Chevron of 14 px for items with children, no lines between the items
            if (!option->state.testFlag(QStyle::State_Children))
            {
                return true;
            }

            QString name = QStringLiteral("chevron-down");
            if (!option->state.testFlag(QStyle::State_Open))
            {
                name = option->direction == Qt::RightToLeft ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right");
            }

            PDFStylePainter::icon(painter, PDFStylePainter::centered(QRectF(getDisclosureRect(option)), QSizeF(14.0, 14.0)), name, Color::IconMuted);
            return true;
        }

        case QStyle::PE_IndicatorItemViewItemDrop:
        {
            const QRectF rect(option->rect);

            if (rect.height() <= 1.0 || rect.width() <= 1.0)
            {
                // Drop between the items: line of 2 px
                const QRectF line = rect.height() <= 1.0 ? QRectF(rect.left(), rect.top() - 1.0, rect.width(), 2.0)
                                                         : QRectF(rect.left() - 1.0, rect.top(), 2.0, rect.height());
                PDFStylePainter::fill(painter, line, Radii(1.0), Color::BgAccent);
            }
            else
            {
                // Drop onto an item: ring around the item
                PDFStylePainter::strokeInside(painter, rect, Radii(ROW_RADIUS), Color::BorderFocus, 1.0);
            }
            return true;
        }

        case QStyle::PE_FrameFocusRect:
        {
            if (!qobject_cast<const QAbstractItemView*>(widget))
            {
                break;
            }

            // Current item of a view: ring of 1 px inside of the item, only after the keyboard
            if (m_impl->isKeyboardModality())
            {
                PDFStylePainter::strokeInside(painter, QRectF(option->rect), Radii(ROW_RADIUS), Color::BorderFocus, 1.0);
            }
            return true;
        }

        case QStyle::PE_IndicatorHeaderArrow:
        {
            const QStyleOptionHeader* header = qstyleoption_cast<const QStyleOptionHeader*>(option);
            if (!header || header->sortIndicator == QStyleOptionHeader::None)
            {
                return true;
            }

            // Ascending order has the chevron pointing up
            const QString name = header->sortIndicator == QStyleOptionHeader::SortDown ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down");
            PDFStylePainter::icon(painter, PDFStylePainter::centered(QRectF(option->rect), QSizeF(HEADER_ARROW, HEADER_ARROW)), name, Color::IconMuted);
            return true;
        }

        case QStyle::PE_IndicatorColumnViewArrow:
        {
            const bool isRightToLeft = option->direction == Qt::RightToLeft;
            PDFStylePainter::icon(painter, PDFStylePainter::centered(QRectF(option->rect), QSizeF(14.0, 14.0)), isRightToLeft ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right"), Color::IconMuted);
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleItemViews::drawControl(QStyle::ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
        case QStyle::CE_ItemViewItem:
        {
            if (const QStyleOptionViewItem* viewItem = qstyleoption_cast<const QStyleOptionViewItem*>(option))
            {
                drawItem(viewItem, painter, widget);
            }
            return true;
        }

        case QStyle::CE_Header:
        {
            if (const QStyleOptionHeader* header = qstyleoption_cast<const QStyleOptionHeader*>(option))
            {
                drawHeaderSection(header, painter, widget);
                drawHeaderLabel(header, painter, widget);

                if (header->sortIndicator != QStyleOptionHeader::None)
                {
                    QStyleOptionHeader arrowOption = *header;
                    arrowOption.rect = style()->subElementRect(QStyle::SE_HeaderArrow, header, widget);
                    style()->drawPrimitive(QStyle::PE_IndicatorHeaderArrow, &arrowOption, painter, widget);
                }
            }
            return true;
        }

        case QStyle::CE_HeaderSection:
        {
            if (const QStyleOptionHeader* header = qstyleoption_cast<const QStyleOptionHeader*>(option))
            {
                drawHeaderSection(header, painter, widget);
            }
            return true;
        }

        case QStyle::CE_HeaderLabel:
        {
            if (const QStyleOptionHeader* header = qstyleoption_cast<const QStyleOptionHeader*>(option))
            {
                drawHeaderLabel(header, painter, widget);
            }
            return true;
        }

        case QStyle::CE_HeaderEmptyArea:
        {
            // Band of the header with the border on the side of the cells
            const QRectF rect(option->rect);
            const bool isHorizontal = option->state.testFlag(QStyle::State_Horizontal);
            PDFStylePainter::fillRect(painter, rect, Color::BgSubtle);
            PDFStylePainter::fillRect(painter, isHorizontal ? QRectF(rect.left(), rect.bottom() - 1.0, rect.width(), 1.0)
                                                            : QRectF(rect.right() - 1.0, rect.top(), 1.0, rect.height()), Color::BorderDefault);
            return true;
        }

        case QStyle::CE_ColumnViewGrip:
        {
            const QRectF rect(option->rect);
            PDFStylePainter::fillRect(painter, QRectF(rect.center().x(), rect.top(), 1.0, rect.height()), Color::BorderDivider);
            return true;
        }

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleItemViews::subElementRect(QStyle::SubElement element, const QStyleOption* option, const QWidget* widget, QRect& rect) const
{
    switch (element)
    {
        case QStyle::SE_ItemViewItemCheckIndicator:
        case QStyle::SE_ItemViewItemDecoration:
        case QStyle::SE_ItemViewItemText:
        case QStyle::SE_ItemViewItemFocusRect:
        {
            const QStyleOptionViewItem* viewItem = qstyleoption_cast<const QStyleOptionViewItem*>(option);
            if (!viewItem)
            {
                return false;
            }

            const ItemLayout layout = getItemLayout(viewItem, widget);

            switch (element)
            {
                case QStyle::SE_ItemViewItemCheckIndicator:
                    rect = layout.check;
                    break;
                case QStyle::SE_ItemViewItemDecoration:
                    rect = layout.decoration;
                    break;
                case QStyle::SE_ItemViewItemText:
                    rect = layout.text;
                    break;
                default:
                    rect = layout.shape;
                    break;
            }
            return true;
        }

        case QStyle::SE_HeaderLabel:
        case QStyle::SE_HeaderArrow:
        {
            const QStyleOptionHeader* header = qstyleoption_cast<const QStyleOptionHeader*>(option);
            if (!header)
            {
                return false;
            }

            rect = getHeaderRect(header, widget, element == QStyle::SE_HeaderArrow);
            return true;
        }

        case QStyle::SE_TreeViewDisclosureItem:
            rect = getDisclosureRect(option);
            return true;

        default:
            break;
    }

    return false;
}

bool PDFWidgetStyleItemViews::sizeFromContents(QStyle::ContentsType type, const QStyleOption* option, const QSize& contentsSize, const QWidget* widget, QSize& size) const
{
    switch (type)
    {
        case QStyle::CT_ItemViewItem:
        {
            const QStyleOptionViewItem* viewItem = qstyleoption_cast<const QStyleOptionViewItem*>(option);
            if (!viewItem)
            {
                return false;
            }

            const bool isPopup = isComboPopup(widget);
            const int padding = isPopup ? POPUP_ROW_PADDING : ROW_PADDING;
            const int gap = isPopup ? 10 : ROW_GAP;
            const QSize decorationSize = getDecorationSize(viewItem);
            const QFont font = getItemFont(viewItem, widget, true);
            const QFontMetrics metrics(font);

            // Size of the text, which can have more lines
            QSize textSize;
            if (viewItem->features.testFlag(QStyleOptionViewItem::HasDisplay) && !viewItem->text.isEmpty())
            {
                const bool isWrapped = viewItem->features.testFlag(QStyleOptionViewItem::WrapText) && viewItem->rect.width() > 0;
                if (isWrapped)
                {
                    int available = viewItem->rect.width() - 2 * padding;
                    if (viewItem->features.testFlag(QStyleOptionViewItem::HasCheckIndicator))
                    {
                        available -= ROW_CHECK + gap;
                    }
                    if (!decorationSize.isEmpty())
                    {
                        available -= decorationSize.width() + gap;
                    }
                    textSize = metrics.boundingRect(QRect(0, 0, qMax(1, available), 100000), Qt::TextWordWrap | static_cast<int>(viewItem->displayAlignment), viewItem->text).size();
                }
                else
                {
                    textSize = metrics.size(0, viewItem->text);
                }
            }

            const bool isIconMode = viewItem->decorationPosition == QStyleOptionViewItem::Top || viewItem->decorationPosition == QStyleOptionViewItem::Bottom;

            if (isIconMode)
            {
                const int width = qMax(decorationSize.width(), textSize.width()) + 2 * padding;
                int height = 2 * padding + decorationSize.height() + textSize.height();
                if (!decorationSize.isEmpty() && !textSize.isEmpty())
                {
                    height += 6;
                }
                size = QSize(width, height);
                return true;
            }

            int width = 2 * padding + textSize.width();
            if (isTreeBranchColumn(viewItem, widget))
            {
                width += TREE_BRANCH_SHIFT;
            }
            if (viewItem->features.testFlag(QStyleOptionViewItem::HasCheckIndicator))
            {
                width += ROW_CHECK + (textSize.isEmpty() && decorationSize.isEmpty() ? 0 : gap);
            }
            if (!decorationSize.isEmpty())
            {
                width += decorationSize.width() + (textSize.isEmpty() ? 0 : gap);
            }
            if (isPopup)
            {
                width += ROW_ICON + gap;
            }

            const int rowHeight = isPopup ? POPUP_ROW_HEIGHT + POPUP_ROW_GAP : ROW_HEIGHT;
            const int contentHeight = qMax(textSize.height(), decorationSize.height()) + (isPopup ? POPUP_ROW_GAP : 0) + 8;
            size = QSize(width, qMax(rowHeight, contentHeight));
            return true;
        }

        case QStyle::CT_HeaderSection:
        {
            const QStyleOptionHeader* header = qstyleoption_cast<const QStyleOptionHeader*>(option);
            if (!header)
            {
                return false;
            }

            const QFont font = PDFWidgetStyleImpl::getFont(widget, header->orientation == Qt::Horizontal ? Font::Caption : Font::CaptionRegular);
            int width = 2 * HEADER_PADDING + PDFWidgetStyleImpl::getTextWidth(header->text, font);

            if (!header->icon.isNull())
            {
                width += 14 + (header->text.isEmpty() ? 0 : HEADER_ARROW_GAP);
            }
            if (header->sortIndicator != QStyleOptionHeader::None)
            {
                width += HEADER_ARROW_GAP + HEADER_ARROW;
            }

            if (header->orientation == Qt::Horizontal)
            {
                size = QSize(width, HEADER_HEIGHT);
            }
            else
            {
                size = QSize(qMax(HEADER_VERTICAL_WIDTH, width), ROW_HEIGHT);
            }

            Q_UNUSED(contentsSize);
            return true;
        }

        default:
            break;
    }

    return false;
}

void PDFWidgetStyleItemViews::polish(QWidget* widget)
{
    if (QTreeView* treeView = qobject_cast<QTreeView*>(widget))
    {
        // The tree view animates the expanding of its branches, when the style
        // has animations. It draws the animation from pictures of the tree,
        // which it fills with the base color of its palette (the leak color).
        if (!treeView->property(SAVED_TREE_ANIMATED).isValid())
        {
            treeView->setProperty(SAVED_TREE_ANIMATED, treeView->isAnimated());
            treeView->setAnimated(false);
        }
    }

    QHeaderView* headerView = qobject_cast<QHeaderView*>(widget);

    if (headerView && headerView->orientation() == Qt::Horizontal && headerView->defaultAlignment() == Qt::AlignCenter &&
        !headerView->property(SAVED_HEADER_ALIGNMENT).isValid())
    {
        // Labels of the sections start at the padding of the section. Columns of
        // numbers are aligned to the right by their models (Qt::TextAlignmentRole).
        headerView->setProperty(SAVED_HEADER_ALIGNMENT, true);
        headerView->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
}

void PDFWidgetStyleItemViews::unpolish(QWidget* widget)
{
    if (QTreeView* treeView = qobject_cast<QTreeView*>(widget))
    {
        const QVariant savedAnimated = treeView->property(SAVED_TREE_ANIMATED);
        if (savedAnimated.isValid())
        {
            treeView->setAnimated(savedAnimated.toBool());
            treeView->setProperty(SAVED_TREE_ANIMATED, QVariant());
        }
    }

    QHeaderView* headerView = qobject_cast<QHeaderView*>(widget);

    if (headerView && headerView->property(SAVED_HEADER_ALIGNMENT).isValid())
    {
        headerView->setProperty(SAVED_HEADER_ALIGNMENT, QVariant());

        if (headerView->defaultAlignment() == (Qt::AlignLeft | Qt::AlignVCenter))
        {
            headerView->setDefaultAlignment(Qt::AlignCenter);
        }
    }
}

void PDFWidgetStyleItemViews::drawItemBackground(const QStyleOptionViewItem* option, QPainter* painter, const QWidget* widget) const
{
    const ItemLayout layout = getItemLayout(option, widget);
    QRectF shape(layout.shape);

    if (isTreeBranchColumn(option, widget))
    {
        // The start of the item is filled together with the area of the branches,
        // because the chevron of the branch lies there
        if (option->direction == Qt::RightToLeft)
        {
            shape.setRight(shape.right() - TREE_BRANCH_SHIFT);
        }
        else
        {
            shape.setLeft(shape.left() + TREE_BRANCH_SHIFT);
        }
    }
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool isSelected = option->state.testFlag(QStyle::State_Selected) && !isComboPopup(widget);
    const bool isHovered = isEnabled && option->state.testFlag(QStyle::State_MouseOver);
    const bool isPopup = isComboPopup(widget);
    const Radii radii = getItemRadii(option, widget);

    if (option->backgroundBrush.style() != Qt::NoBrush)
    {
        // Background given by the model
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(option->backgroundBrush);
        painter->drawPath(PDFStylePainter::roundedPath(shape, radii));
        painter->restore();
    }

    if (isSelected)
    {
        Color fill = isHovered ? Color::BgAccentSubtleHover : Color::BgAccentSubtle;
        if (!option->state.testFlag(QStyle::State_Active) || !isViewFocused(widget))
        {
            // Selection of a view, which does not have the focus
            fill = Color::BgMuted;
        }
        PDFStylePainter::fill(painter, shape, radii, fill);
    }
    else if (isHovered || (isPopup && isEnabled && option->state.testFlag(QStyle::State_Selected)))
    {
        // The hovered row of a popup list is its selected row
        PDFStylePainter::fill(painter, shape, radii, Color::BgHover);
    }
}

void PDFWidgetStyleItemViews::drawItem(const QStyleOptionViewItem* option, QPainter* painter, const QWidget* widget) const
{
    const ItemLayout layout = getItemLayout(option, widget);
    const bool isPopup = isComboPopup(widget);
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool isSelected = option->state.testFlag(QStyle::State_Selected) && !isPopup;
    const bool isEditing = isEdited(option, widget);

    const QComboBox* comboBox = isPopup ? qobject_cast<const QComboBox*>(widget->parentWidget()->parentWidget()) : nullptr;
    const bool isCurrentOfCombo = comboBox && option->index.isValid() && comboBox->currentIndex() == option->index.row();

    // Separator of a popup list
    if (isPopup && option->index.isValid() && option->index.data(Qt::AccessibleDescriptionRole).toString() == QLatin1String("separator"))
    {
        const QRectF rect(option->rect);
        PDFStylePainter::fillRect(painter, QRectF(rect.left() + 6.0, rect.top() + (rect.height() - 1.0) / 2.0, rect.width() - 12.0, 1.0), Color::BorderDivider);
        return;
    }

    style()->drawPrimitive(QStyle::PE_PanelItemViewItem, option, painter, widget);

    if (option->features.testFlag(QStyleOptionViewItem::HasCheckIndicator))
    {
        QStyleOptionViewItem checkOption = *option;
        checkOption.rect = layout.check;
        checkOption.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange | QStyle::State_HasFocus | QStyle::State_Selected);

        switch (option->checkState)
        {
            case Qt::Checked:
                checkOption.state |= QStyle::State_On;
                break;
            case Qt::PartiallyChecked:
                checkOption.state |= QStyle::State_NoChange;
                break;
            default:
                checkOption.state |= QStyle::State_Off;
                break;
        }

        style()->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &checkOption, painter, widget);
    }

    Color textColor = Color::TextPrimary;
    Color iconColor = Color::IconDefault;

    if (!isEnabled)
    {
        textColor = Color::TextMuted;
        iconColor = Color::IconMuted;
    }
    else if (isSelected || isCurrentOfCombo)
    {
        textColor = Color::TextAccent;
        iconColor = isCurrentOfCombo ? Color::IconDefault : Color::IconAccent;
    }

    if (!option->icon.isNull() && layout.decoration.isValid())
    {
        PDFStyleState iconState;
        iconState.enabled = isEnabled;
        iconState.selected = isSelected;
        PDFWidgetStyleImpl::drawIcon(painter, QRectF(layout.decoration), option->icon, PDFWidgetStyleImpl::color(iconColor), iconState);
    }

    if (!option->text.isEmpty() && !isEditing)
    {
        QColor color = PDFWidgetStyleImpl::color(textColor);

        // Color of the text given by the model
        if (isEnabled && !isSelected && !isCurrentOfCombo && option->index.isValid())
        {
            const QVariant foreground = option->index.data(Qt::ForegroundRole);
            if (foreground.canConvert<QBrush>())
            {
                const QBrush brush = qvariant_cast<QBrush>(foreground);
                if (brush.style() != Qt::NoBrush)
                {
                    color = brush.color();
                }
            }
        }

        const QFont font = getItemFont(option, widget, isSelected || isCurrentOfCombo);
        const bool isWrapped = option->features.testFlag(QStyleOptionViewItem::WrapText);
        const QRectF textRect(layout.text);

        painter->save();
        painter->setFont(font);
        painter->setPen(color);

        if (isWrapped || option->text.contains(QLatin1Char('\n')))
        {
            painter->drawText(textRect, static_cast<int>(option->displayAlignment) | (isWrapped ? Qt::TextWordWrap : 0), option->text);
        }
        else
        {
            const QFontMetricsF metrics(font);
            const QString text = metrics.elidedText(option->text, option->textElideMode, textRect.width());
            const int alignment = static_cast<int>(QStyle::visualAlignment(option->direction, option->displayAlignment));
            painter->drawText(textRect, (alignment & Qt::AlignHorizontal_Mask) | Qt::AlignVCenter | Qt::TextSingleLine, text);
        }

        painter->restore();
    }

    if (isCurrentOfCombo)
    {
        // Check mark of the current item of a combo box at the right side
        const QRectF shape(layout.shape);
        const QRectF checkRect(shape.right() - POPUP_ROW_PADDING - ROW_ICON, shape.top() + (shape.height() - ROW_ICON) / 2.0, ROW_ICON, ROW_ICON);
        PDFStylePainter::icon(painter, PDFWidgetStyleImpl::visualRect(option->direction == Qt::RightToLeft, shape, checkRect), QStringLiteral("check"), Color::IconAccent);
    }

    if (isEditing)
    {
        // The editor is frameless, the ring of the edited cell is drawn by the style
        PDFStylePainter::strokeInside(painter, QRectF(layout.shape), Radii(ROW_RADIUS), Color::BorderFocus, 2.0);
    }
    else if (!isPopup && option->state.testFlag(QStyle::State_HasFocus) && isEnabled && m_impl->isKeyboardModality())
    {
        PDFStylePainter::strokeInside(painter, QRectF(layout.shape), Radii(ROW_RADIUS), Color::BorderFocus, 1.0);
    }
}

QRect PDFWidgetStyleItemViews::getHeaderRect(const QStyleOptionHeader* option, const QWidget* widget, bool isArrow) const
{
    const QRect rect = option->rect;
    const bool isHorizontal = option->orientation == Qt::Horizontal;

    if (!isHorizontal)
    {
        return isArrow ? QRect() : rect;
    }

    const QRect contents = rect.adjusted(HEADER_PADDING, 0, -HEADER_PADDING, -1);
    const bool hasArrow = option->sortIndicator != QStyleOptionHeader::None;
    const QFont font = PDFWidgetStyleImpl::getFont(widget, Font::Caption);
    const int iconWidth = option->icon.isNull() ? 0 : 14 + (option->text.isEmpty() ? 0 : HEADER_ARROW_GAP);
    const int arrowWidth = hasArrow ? HEADER_ARROW + HEADER_ARROW_GAP : 0;
    const int labelWidth = qMin(iconWidth + PDFWidgetStyleImpl::getTextWidth(option->text, font), qMax(0, contents.width() - arrowWidth));

    // The label is aligned by the column, the arrow follows a label aligned to
    // the left and it precedes a label aligned to the right
    const Qt::Alignment alignment = QStyle::visualAlignment(option->direction, option->textAlignment);
    const bool isRight = alignment.testFlag(Qt::AlignRight);
    const bool isCenter = alignment.testFlag(Qt::AlignHCenter);

    int labelLeft = contents.left();
    int arrowLeft = contents.left() + labelWidth + HEADER_ARROW_GAP;

    if (isRight)
    {
        labelLeft = contents.right() + 1 - labelWidth;
        arrowLeft = labelLeft - HEADER_ARROW_GAP - HEADER_ARROW;
    }
    else if (isCenter)
    {
        labelLeft = contents.left() + (contents.width() - labelWidth - arrowWidth) / 2;
        arrowLeft = labelLeft + labelWidth + HEADER_ARROW_GAP;
    }

    if (isArrow)
    {
        return hasArrow ? QRect(arrowLeft, contents.top() + (contents.height() - HEADER_ARROW) / 2, HEADER_ARROW, HEADER_ARROW) : QRect();
    }

    return QRect(labelLeft, contents.top(), labelWidth, contents.height());
}

void PDFWidgetStyleItemViews::drawHeaderSection(const QStyleOptionHeader* option, QPainter* painter, const QWidget* widget) const
{
    Q_UNUSED(widget);

    const QRectF rect(option->rect);
    const bool isHorizontal = option->orientation == Qt::Horizontal;
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    // The header view sets State_On to a section, which intersects the selection,
    // and State_Sunken to a pressed section and to a section selected as a whole
    const bool isSelected = option->state.testFlag(QStyle::State_On) && option->state.testFlag(QStyle::State_Sunken);
    const bool isPressed = isEnabled && option->state.testFlag(QStyle::State_Sunken) && !isSelected;
    const bool isHovered = isEnabled && option->state.testFlag(QStyle::State_MouseOver);
    const bool isLast = option->position == QStyleOptionHeader::End || option->position == QStyleOptionHeader::OnlyOneSection;

    PDFStylePainter::fillRect(painter, rect, Color::BgSubtle);

    if (isSelected)
    {
        PDFStylePainter::fillRect(painter, rect, Color::BgAccentSubtle);
    }
    else if (isPressed)
    {
        PDFStylePainter::fillRect(painter, rect, Color::BgPressed);
    }
    else if (isHovered)
    {
        PDFStylePainter::fillRect(painter, rect, Color::BgHover);
    }

    if (isHorizontal)
    {
        // Border of the band below, separator of 16 px between the sections
        PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.bottom() - 1.0, rect.width(), 1.0), Color::BorderDefault);

        if (!isLast)
        {
            const bool isRightToLeft = option->direction == Qt::RightToLeft;
            const qreal x = isRightToLeft ? rect.left() : rect.right() - 1.0;
            PDFStylePainter::fillRect(painter, QRectF(x, rect.top() + qRound((rect.height() - 1.0 - 16.0) / 2.0), 1.0, 16.0), Color::BorderDivider);
        }
    }
    else
    {
        // Vertical header: borders to the cells and between the rows
        const bool isRightToLeft = option->direction == Qt::RightToLeft;
        PDFStylePainter::fillRect(painter, QRectF(isRightToLeft ? rect.left() : rect.right() - 1.0, rect.top(), 1.0, rect.height()), Color::BorderDefault);

        if (!isLast)
        {
            PDFStylePainter::fillRect(painter, QRectF(rect.left(), rect.bottom() - 1.0, rect.width(), 1.0), Color::BorderDefault);
        }
    }
}

void PDFWidgetStyleItemViews::drawHeaderLabel(const QStyleOptionHeader* option, QPainter* painter, const QWidget* widget) const
{
    const bool isHorizontal = option->orientation == Qt::Horizontal;
    const bool isEnabled = option->state.testFlag(QStyle::State_Enabled);
    const bool isSelected = option->state.testFlag(QStyle::State_On) && option->state.testFlag(QStyle::State_Sunken);
    const bool isActive = isEnabled && !isSelected && (option->state.testFlag(QStyle::State_MouseOver) || option->state.testFlag(QStyle::State_Sunken));

    Color textColor = Color::TextMuted;
    if (isSelected)
    {
        textColor = Color::TextAccent;
    }
    else if (isActive && isHorizontal)
    {
        textColor = Color::TextTertiary;
    }

    const QFont font = PDFWidgetStyleImpl::getFont(widget, isHorizontal ? Font::Caption : Font::CaptionRegular);
    QRectF rect(style()->subElementRect(QStyle::SE_HeaderLabel, option, widget));

    if (!isHorizontal)
    {
        PDFStylePainter::text(painter, rect.adjusted(2.0, 0.0, -3.0, 0.0), Qt::AlignCenter | Qt::TextSingleLine, option->text, font, PDFWidgetStyleImpl::color(textColor));
        return;
    }

    const bool isRightToLeft = option->direction == Qt::RightToLeft;

    if (!option->icon.isNull())
    {
        const QRectF iconRect(isRightToLeft ? rect.right() - 14.0 : rect.left(), rect.top() + (rect.height() - 14.0) / 2.0, 14.0, 14.0);
        PDFStyleState iconState;
        iconState.enabled = isEnabled;
        PDFWidgetStyleImpl::drawIcon(painter, iconRect, option->icon, PDFWidgetStyleImpl::color(isEnabled ? Color::IconDefault : Color::IconMuted), iconState);

        if (isRightToLeft)
        {
            rect.setRight(rect.right() - 14.0 - HEADER_ARROW_GAP);
        }
        else
        {
            rect.setLeft(rect.left() + 14.0 + HEADER_ARROW_GAP);
        }
    }

    const QString text = PDFWidgetStyleImpl::getElidedText(option->text, font, rect.width());
    PDFStylePainter::text(painter, rect, Qt::AlignVCenter | (isRightToLeft ? Qt::AlignRight : Qt::AlignLeft) | Qt::TextSingleLine, text, font, PDFWidgetStyleImpl::color(textColor));
}

}   // namespace pdf
