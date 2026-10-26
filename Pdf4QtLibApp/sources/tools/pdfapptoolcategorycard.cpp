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

#include "pdfapptoolcategorycard.h"

#include "pdfcomponentcards.h"
#include "pdfcomponentrows.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"

#include <QChildEvent>
#include <QKeyEvent>
#include <QStyle>
#include <QVBoxLayout>

#include <limits>

namespace pdf
{

/// Helpers of the cards of the categories
class PDFAppToolCategoryCardHelper
{
public:
    PDFAppToolCategoryCardHelper() = delete;

    /// Font of the caption of a subgroup: Semi Bold 11, letter spacing 4 % (0.44 px),
    /// capitals (the text keeps its case, the capitalization depends on the language)
    static QFont getCaptionFont()
    {
        QFont font = PDFThemeFonts::getFont(QFont::DemiBold, 11);
        font.setLetterSpacing(QFont::AbsoluteSpacing, 0.44);
        font.setCapitalization(QFont::AllUppercase);
        return font;
    }
};

PDFAppToolCategoryCard::PDFAppToolCategoryCard(const QString& title, const QString& description, const QString& count,
                                               const QString& iconName, PDFAccentHue hue, QWidget* parent) :
    BaseClass(QMargins(PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH, PDFAppPageWidgets::BORDER_WIDTH), 0, parent)
{
    // header 273:1648: the Tool category header inside of the border, its top
    // corners the corners of the inside of the card
    m_header = new PDFToolCategoryHeader(title, description, count, iconName, hue, this);
    m_header->setObjectName(QStringLiteral("categoryHeader"));
    m_header->setTopRadius(INNER_RADIUS);
    getLayout()->addWidget(m_header);

    // list 273:1649: padding 6, gap 2
    m_list = new QWidget(this);
    m_list->setObjectName(QStringLiteral("toolList"));
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(LIST_PADDING, LIST_PADDING, LIST_PADDING, LIST_PADDING);
    m_listLayout->setSpacing(LIST_SPACING);
    getLayout()->addWidget(m_list);
}

PDFAppToolCategoryCard::~PDFAppToolCategoryCard() = default;

PDFLabel* PDFAppToolCategoryCard::addCaption(const QString& text)
{
    // Line 13 of the design (QLabel would take the line spacing of the font)
    PDFAppParagraph* caption = PDFAppPageWidgets::createText(text, PDFAppToolCategoryCardHelper::getCaptionFont(), CAPTION_LINE_HEIGHT, PDFThemeColors::TextTertiary, m_list);
    caption->setObjectName(QStringLiteral("subgroupCaption"));
    caption->setContentsMargins(CAPTION_LEFT, CAPTION_TOP, 0, CAPTION_BOTTOM);
    if (m_listLayout)
    {
        m_listLayout->addWidget(caption);
    }

    Item item;
    item.caption = caption;
    m_items.push_back(item);
    return caption;
}

void PDFAppToolCategoryCard::addRow(PDFToolRow* row)
{
    if (!row || !m_listLayout)
    {
        return;
    }

    if (m_items.empty())
    {
        // Rows without a subgroup (Edit, the tools of a collection)
        m_items.push_back(Item());
    }

    row->setParent(m_list);
    m_listLayout->addWidget(row);
    m_items.back().rows.append(row);
}

QList<PDFToolRow*> PDFAppToolCategoryCard::getRows() const
{
    QList<PDFToolRow*> rows;
    for (const Item& item : m_items)
    {
        for (const QPointer<PDFToolRow>& row : item.rows)
        {
            if (row)
            {
                rows.append(row.data());
            }
        }
    }
    return rows;
}

QList<PDFToolRow*> PDFAppToolCategoryCard::getVisibleRows() const
{
    QList<PDFToolRow*> rows;
    for (PDFToolRow* row : getRows())
    {
        if (!row->isHidden())
        {
            rows.append(row);
        }
    }
    return rows;
}

QList<PDFLabel*> PDFAppToolCategoryCard::getCaptions() const
{
    QList<PDFLabel*> captions;
    for (const Item& item : m_items)
    {
        if (item.caption)
        {
            captions.append(item.caption.data());
        }
    }
    return captions;
}

int PDFAppToolCategoryCard::updateVisibility()
{
    int visibleCount = 0;
    bool isFirstCaption = true;
    for (const Item& item : m_items)
    {
        int itemCount = 0;
        for (const QPointer<PDFToolRow>& row : item.rows)
        {
            itemCount += (row && !row->isHidden()) ? 1 : 0;
        }
        visibleCount += itemCount;

        if (item.caption)
        {
            // The first visible caption is 19 high (273:1658), the others 25 (273:1718)
            item.caption->setHidden(itemCount == 0);
            if (itemCount > 0)
            {
                const int top = isFirstCaption ? FIRST_CAPTION_TOP : CAPTION_TOP;
                if (item.caption->contentsMargins().top() != top)
                {
                    item.caption->setContentsMargins(CAPTION_LEFT, top, 0, CAPTION_BOTTOM);
                }
                isFirstCaption = false;
            }
        }
    }

    setHidden(visibleCount == 0);
    return visibleCount;
}

PDFAppToolCategoryGrid::PDFAppToolCategoryGrid(QWidget* parent) :
    BaseClass(parent)
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

PDFAppToolCategoryGrid::~PDFAppToolCategoryGrid() = default;

void PDFAppToolCategoryGrid::addCard(QWidget* card)
{
    if (!card)
    {
        return;
    }

    if (card->parentWidget() != this)
    {
        card->setParent(this);
    }
    if (!m_cards.contains(card))
    {
        m_cards.append(card);
    }
    if (isVisible() && !(card->isHidden() && card->testAttribute(Qt::WA_WState_ExplicitShowHide)))
    {
        card->show();
    }
    updateCards();
}

QList<QWidget*> PDFAppToolCategoryGrid::getCards() const
{
    QList<QWidget*> cards;
    for (const QPointer<QWidget>& card : m_cards)
    {
        if (card && card->parentWidget() == this)
        {
            cards.append(card.data());
        }
    }
    return cards;
}

QList<QWidget*> PDFAppToolCategoryGrid::getVisibleCards() const
{
    QList<QWidget*> cards;
    for (QWidget* card : getCards())
    {
        if (!card->isHidden())
        {
            cards.append(card);
        }
    }
    return cards;
}

int PDFAppToolCategoryGrid::getColumnCount(int width) const
{
    // One visible card takes the whole width (530:2957)
    if (getVisibleCards().size() <= 1)
    {
        return 1;
    }
    return width >= 2 * MINIMUM_CARD_WIDTH + HORIZONTAL_SPACING ? 2 : 1;
}

void PDFAppToolCategoryGrid::updateCards()
{
    updateGeometry();
    placeCards(width(), true);
}

bool PDFAppToolCategoryGrid::hasHeightForWidth() const
{
    return true;
}

int PDFAppToolCategoryGrid::heightForWidth(int width) const
{
    return placeCards(width, false);
}

QSize PDFAppToolCategoryGrid::sizeHint() const
{
    const int width = 2 * MINIMUM_CARD_WIDTH + HORIZONTAL_SPACING;
    return QSize(width, heightForWidth(width));
}

QSize PDFAppToolCategoryGrid::minimumSizeHint() const
{
    // The cards elide their texts in a very narrow window (no horizontal scroll bar)
    return QSize(0, 0);
}

bool PDFAppToolCategoryGrid::event(QEvent* event)
{
    const bool result = BaseClass::event(event);
    switch (event->type())
    {
        case QEvent::LayoutRequest:
            // A card has been shown, hidden, or its rows have changed
            updateGeometry();
            placeCards(width(), true);
            break;

        case QEvent::Resize:
        case QEvent::LayoutDirectionChange:
        case QEvent::Show:
            placeCards(width(), true);
            break;

        case QEvent::ChildRemoved:
        {
            const QObject* child = static_cast<QChildEvent*>(event)->child();
            for (qsizetype i = m_cards.size() - 1; i >= 0; --i)
            {
                if (!m_cards[i] || m_cards[i].data() == child)
                {
                    m_cards.removeAt(i);
                }
            }
            updateGeometry();
            placeCards(width(), true);
            break;
        }

        default:
            break;
    }
    return result;
}

int PDFAppToolCategoryGrid::getCardHeight(QWidget* card, int width)
{
    if (!card)
    {
        return 0;
    }

    const int height = card->hasHeightForWidth() ? card->heightForWidth(width) : -1;
    return qMax(height >= 0 ? height : card->sizeHint().height(), card->minimumSizeHint().height());
}

int PDFAppToolCategoryGrid::placeCards(int width, bool isApplied) const
{
    if (isApplied && m_isPlacing)
    {
        return 0;
    }

    PDFAppToolCategoryGrid* self = const_cast<PDFAppToolCategoryGrid*>(this);
    if (isApplied)
    {
        self->m_isPlacing = true;
    }

    const QList<QWidget*> cards = getVisibleCards();
    const int columns = getColumnCount(width);
    const int cardWidth = columns == 2 ? qMax(0, (width - HORIZONTAL_SPACING) / 2) : qMax(0, width);

    int y = 0;
    for (qsizetype rowStart = 0; rowStart < cards.size(); rowStart += columns)
    {
        const qsizetype rowEnd = qMin(rowStart + columns, cards.size());
        int rowHeight = 0;
        for (qsizetype i = rowStart; i < rowEnd; ++i)
        {
            // The cards of a row are aligned to its top, each keeps its height
            const int height = getCardHeight(cards[i], cardWidth);
            rowHeight = qMax(rowHeight, height);
            if (isApplied)
            {
                const int column = int(i - rowStart);
                const int left = column == 0 ? 0 : width - cardWidth;
                const QRect logical(left, y, cardWidth, height);
                cards[i]->setGeometry(QStyle::visualRect(layoutDirection(), QRect(0, 0, width, qMax(height, this->height())), logical));
            }
        }
        y += rowHeight + VERTICAL_SPACING;
    }

    if (isApplied)
    {
        self->m_isPlacing = false;
    }
    return cards.isEmpty() ? 0 : y - VERTICAL_SPACING;
}

PDFAppToolFocusGroup::PDFAppToolFocusGroup(QWidget* root, QObject* parent) :
    BaseClass(parent),
    m_root(root)
{

}

PDFAppToolFocusGroup::~PDFAppToolFocusGroup() = default;

void PDFAppToolFocusGroup::addWidget(QWidget* widget)
{
    if (!widget || getWidgets().contains(widget))
    {
        return;
    }

    // The widgets of rebuilt lists (tiles, recent tools, a collection card) are
    // deleted: their entries go (the list does not grow with every refresh)
    for (qsizetype i = m_widgets.size() - 1; i >= 0; --i)
    {
        if (!m_widgets[i])
        {
            m_widgets.removeAt(i);
        }
    }
    m_widgets.append(widget);
    widget->installEventFilter(this);
}

void PDFAppToolFocusGroup::removeWidget(QWidget* widget)
{
    for (qsizetype i = m_widgets.size() - 1; i >= 0; --i)
    {
        if (!m_widgets[i] || m_widgets[i].data() == widget)
        {
            if (m_widgets[i])
            {
                m_widgets[i]->removeEventFilter(this);
            }
            m_widgets.removeAt(i);
        }
    }

    if (m_current.data() == widget)
    {
        m_current = nullptr;
    }
}

QList<QWidget*> PDFAppToolFocusGroup::getWidgets() const
{
    QList<QWidget*> widgets;
    for (const QPointer<QWidget>& widget : m_widgets)
    {
        if (widget)
        {
            widgets.append(widget.data());
        }
    }
    return widgets;
}

QList<QWidget*> PDFAppToolFocusGroup::getVisibleWidgets() const
{
    QList<QWidget*> widgets;
    for (QWidget* widget : getWidgets())
    {
        if (isShown(widget))
        {
            widgets.append(widget);
        }
    }
    return widgets;
}

QWidget* PDFAppToolFocusGroup::getCurrentWidget() const
{
    return m_current;
}

void PDFAppToolFocusGroup::setCurrentWidget(QWidget* widget)
{
    if (widget && getWidgets().contains(widget))
    {
        m_current = widget;
    }
    updateStops();
}

void PDFAppToolFocusGroup::updateStops()
{
    const QList<QWidget*> visible = getVisibleWidgets();
    if (!m_current || !visible.contains(m_current.data()))
    {
        m_current = visible.isEmpty() ? nullptr : visible.front();
    }

    for (QWidget* widget : getWidgets())
    {
        const Qt::FocusPolicy policy = widget == m_current.data() ? Qt::StrongFocus : Qt::ClickFocus;
        if (widget->focusPolicy() != policy)
        {
            widget->setFocusPolicy(policy);
        }
    }
}

bool PDFAppToolFocusGroup::moveFocus(QWidget* from, int key)
{
    QWidget* target = nullptr;
    switch (key)
    {
        case Qt::Key_Home:
        case Qt::Key_End:
        {
            const QList<QWidget*> visible = getVisibleWidgets();
            if (!visible.isEmpty())
            {
                target = key == Qt::Key_Home ? visible.front() : visible.back();
            }
            break;
        }

        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            target = findNeighbor(from, key);
            break;

        default:
            return false;
    }

    if (target && target != from)
    {
        m_current = target;
        updateStops();
        target->setFocus(Qt::TabFocusReason);
    }
    return true;
}

bool PDFAppToolFocusGroup::eventFilter(QObject* watched, QEvent* event)
{
    QWidget* widget = qobject_cast<QWidget*>(watched);
    if (!widget)
    {
        return BaseClass::eventFilter(watched, event);
    }

    switch (event->type())
    {
        case QEvent::FocusIn:
            // A widget focused by the mouse or by Tab becomes the stop of the group
            if (m_current.data() != widget)
            {
                m_current = widget;
                updateStops();
            }
            break;

        case QEvent::KeyPress:
        {
            const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
            const Qt::KeyboardModifiers modifiers = keyEvent->modifiers() & ~Qt::KeypadModifier;
            if (modifiers == Qt::NoModifier && moveFocus(widget, keyEvent->key()))
            {
                return true;
            }
            break;
        }

        default:
            break;
    }

    return BaseClass::eventFilter(watched, event);
}

bool PDFAppToolFocusGroup::isShown(const QWidget* widget) const
{
    if (!widget)
    {
        return false;
    }
    return m_root ? widget->isVisibleTo(m_root.data()) : !widget->isHidden();
}

QRect PDFAppToolFocusGroup::getRect(const QWidget* widget) const
{
    if (!widget)
    {
        return QRect();
    }

    const QPoint topLeft = m_root ? widget->mapTo(m_root.data(), QPoint(0, 0)) : widget->mapToGlobal(QPoint(0, 0));
    return QRect(topLeft, widget->size());
}

QWidget* PDFAppToolFocusGroup::findNeighbor(QWidget* from, int key) const
{
    if (!from)
    {
        return nullptr;
    }

    // The positions on the screen: a mirrored layout (right-to-left) moves the
    // focus to the widget, which the user sees at the side of the arrow
    const QRect source = getRect(from);
    const QPoint center = source.center();
    const bool isVertical = key == Qt::Key_Up || key == Qt::Key_Down;

    QWidget* best = nullptr;
    qint64 bestPrimary = std::numeric_limits<qint64>::max();
    qint64 bestSecondary = std::numeric_limits<qint64>::max();
    for (QWidget* widget : getVisibleWidgets())
    {
        if (widget == from)
        {
            continue;
        }

        const QRect rect = getRect(widget);
        const QPoint point = rect.center();
        const bool overlapsHorizontally = rect.left() <= source.right() && rect.right() >= source.left();

        qint64 primary = 0;
        qint64 secondary = 0;
        if (isVertical)
        {
            // The same column: the nearest widget above / below
            const int distance = key == Qt::Key_Down ? point.y() - center.y() : center.y() - point.y();
            if (!overlapsHorizontally || distance <= 0)
            {
                continue;
            }
            primary = distance;
            secondary = qAbs(point.x() - center.x());
        }
        else
        {
            // The neighbor column: the widget nearest to the same height
            const int distance = key == Qt::Key_Right ? rect.left() - source.right() : source.left() - rect.right();
            if (overlapsHorizontally || distance <= 0)
            {
                continue;
            }
            primary = distance;
            secondary = qAbs(point.y() - center.y());
            std::swap(primary, secondary);
        }

        if (primary < bestPrimary || (primary == bestPrimary && secondary < bestSecondary))
        {
            best = widget;
            bestPrimary = primary;
            bestSecondary = secondary;
        }
    }
    return best;
}

}   // namespace pdf
