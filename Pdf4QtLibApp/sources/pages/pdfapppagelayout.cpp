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

#include "pdfapppagelayout.h"
#include "pdfappmarkdown.h"

#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QChildEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QStyle>

#include <cmath>

namespace pdf
{

PDFAppColumns::PDFAppColumns(QWidget* parent) :
    BaseClass(parent)
{
    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(GAP);
}

PDFAppColumns::~PDFAppColumns() = default;

PDFAppColumns* PDFAppColumns::createMainAndSide(QWidget* parent, int minimumMainWidth, int sideWidth)
{
    PDFAppColumns* columns = new PDFAppColumns(parent);
    columns->addColumn(0, 1, minimumMainWidth)->setObjectName(QStringLiteral("mainColumn"));
    columns->addColumn(sideWidth, 0, 0)->setObjectName(QStringLiteral("sideColumn"));
    return columns;
}

PDFAppColumns* PDFAppColumns::createInContent(QWidget* content, QWidget* main, QWidget* side, int minimumMainWidth)
{
    QVBoxLayout* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    PDFAppColumns* columns = new PDFAppColumns(content);
    columns->setObjectName(QStringLiteral("columns"));
    layout->addWidget(columns);
    columns->addColumn(main, 0, 1, minimumMainWidth, Qt::AlignTop);
    columns->addColumn(side, SIDE_WIDTH, 0, 0, Qt::AlignTop);
    return columns;
}

QWidget* PDFAppColumns::addColumn(int fixedWidth, int stretch, int minimumWidth)
{
    QWidget* widget = new QWidget(this);
    widget->setObjectName(QStringLiteral("column%1").arg(m_columns.size()));
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(GAP);
    return addColumn(widget, fixedWidth, stretch, minimumWidth);
}

QWidget* PDFAppColumns::addColumn(QWidget* widget, int fixedWidth, int stretch, int minimumWidth, Qt::Alignment alignment)
{
    if (!widget || !m_layout)
    {
        return widget;
    }

    widget->setParent(this);

    Column column;
    column.widget = widget;
    column.fixedWidth = qMax(0, fixedWidth);
    column.stretch = qMax(0, stretch);
    column.minimumWidth = qMax(0, minimumWidth);
    m_columns.push_back(column);

    m_layout->addWidget(widget, m_isStacked ? 0 : column.stretch, alignment);
    if (isVisible())
    {
        widget->show();
    }

    // The arrangement is decided again for the new set of the columns
    m_isStacked = false;
    updateArrangement(width());
    return widget;
}

QBoxLayout* PDFAppColumns::getColumnLayout(int index) const
{
    QWidget* widget = getColumn(index);
    return widget ? qobject_cast<QBoxLayout*>(widget->layout()) : nullptr;
}

QWidget* PDFAppColumns::getColumn(int index) const
{
    return index >= 0 && index < int(m_columns.size()) ? m_columns[size_t(index)].widget.data() : nullptr;
}

bool PDFAppColumns::isStackedAt(int width) const
{
    int required = 0;
    int count = 0;
    for (const Column& column : m_columns)
    {
        if (column.widget)
        {
            required += column.fixedWidth > 0 ? column.fixedWidth : column.minimumWidth;
            ++count;
        }
    }
    required += GAP * qMax(0, count - 1);
    return width < required;
}

void PDFAppColumns::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateArrangement(event->size().width());
}

bool PDFAppColumns::event(QEvent* event)
{
    if (event->type() == QEvent::ChildRemoved)
    {
        // A deleted column (its pointer is already cleared) or a column, which the
        // page has taken away, is forgotten
        QObject* child = static_cast<QChildEvent*>(event)->child();
        for (auto it = m_columns.begin(); it != m_columns.end();)
        {
            it = (!it->widget || it->widget.data() == child) ? m_columns.erase(it) : std::next(it);
        }
    }
    return BaseClass::event(event);
}

void PDFAppColumns::updateArrangement(int width)
{
    if (!m_layout)
    {
        return;
    }

    // A hidden widget has no width yet: it starts beside each other
    const bool isStacked = width > 0 && isStackedAt(width);
    m_isStacked = isStacked;
    m_layout->setDirection(isStacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);

    for (const Column& column : m_columns)
    {
        if (!column.widget)
        {
            continue;
        }

        // Only the width of a fixed column is set (a column of the page keeps its
        // own limits of the size)
        if (column.fixedWidth > 0)
        {
            if (isStacked)
            {
                column.widget->setMinimumWidth(0);
                column.widget->setMaximumWidth(QWIDGETSIZE_MAX);
            }
            else
            {
                column.widget->setFixedWidth(column.fixedWidth);
            }
        }
        m_layout->setStretchFactor(column.widget, isStacked ? 0 : column.stretch);
    }
}

PDFAppGrid::PDFAppGrid(int minimumCellWidth, int maximumColumns, int horizontalSpacing, int verticalSpacing, QWidget* parent) :
    BaseClass(parent),
    m_minimumCellWidth(qMax(1, minimumCellWidth)),
    m_maximumColumns(qMax(1, maximumColumns)),
    m_horizontalSpacing(qMax(0, horizontalSpacing)),
    m_verticalSpacing(qMax(0, verticalSpacing))
{
    QSizePolicy policy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
}

PDFAppGrid::~PDFAppGrid() = default;

void PDFAppGrid::addWidget(QWidget* widget)
{
    if (!widget)
    {
        return;
    }

    removeDeletedWidgets();
    if (widget->parentWidget() != this)
    {
        widget->setParent(this);
    }
    if (!m_widgets.contains(widget))
    {
        m_widgets.append(widget);
    }
    // A new child of a shown widget is not shown by itself; a cell, which the page
    // has hidden explicitly, stays hidden
    if (isVisible() && !(widget->isHidden() && widget->testAttribute(Qt::WA_WState_ExplicitShowHide)))
    {
        widget->show();
    }
    updateGeometry();
    placeWidgets();
}

QList<QWidget*> PDFAppGrid::getVisibleWidgets() const
{
    QList<QWidget*> widgets;
    for (const QPointer<QWidget>& widget : m_widgets)
    {
        if (widget && !widget->isHidden() && widget->parentWidget() == this)
        {
            widgets.append(widget.data());
        }
    }
    return widgets;
}

int PDFAppGrid::getColumnCount(int width) const
{
    const int columns = (qMax(0, width) + m_horizontalSpacing) / (m_minimumCellWidth + m_horizontalSpacing);
    return qBound(1, columns, m_maximumColumns);
}

void PDFAppGrid::updateCells()
{
    removeDeletedWidgets();
    updateGeometry();
    placeWidgets();
}

void PDFAppGrid::removeDeletedWidgets()
{
    // A deleted cell has its pointer cleared, a cell given to another parent is
    // not a cell any more
    for (qsizetype i = m_widgets.size() - 1; i >= 0; --i)
    {
        if (!m_widgets[i] || m_widgets[i]->parentWidget() != this)
        {
            m_widgets.removeAt(i);
        }
    }
}

std::pair<int, int> PDFAppGrid::getColumnEdges(int width, int columns, int column) const
{
    // The columns share the whole width: the fractional width of a column, the
    // edges rounded to pixels (223.6 px tiles of Home, 28:4)
    const qreal columnWidth = qMax(qreal(0.0), (qreal(width) - qreal((columns - 1) * m_horizontalSpacing)) / qreal(qMax(1, columns)));
    const int left = int(std::round(qreal(column) * (columnWidth + m_horizontalSpacing)));
    const int right = int(std::round(qreal(column) * (columnWidth + m_horizontalSpacing) + columnWidth));
    return { left, qMax(left, right) };
}

int PDFAppGrid::getCellWidth(int width, int columns) const
{
    if (columns <= 0)
    {
        return 0;
    }
    return qMax(0, int(std::floor((qreal(width) - qreal((columns - 1) * m_horizontalSpacing)) / qreal(columns))));
}

int PDFAppGrid::getCellHeight(QWidget* widget, int cellWidth) const
{
    if (!widget)
    {
        return 0;
    }

    const int height = widget->hasHeightForWidth() ? widget->heightForWidth(cellWidth) : -1;
    return qMax(qMax(height >= 0 ? height : widget->sizeHint().height(), widget->minimumSizeHint().height()), widget->minimumHeight());
}

bool PDFAppGrid::hasHeightForWidth() const
{
    return true;
}

int PDFAppGrid::heightForWidth(int width) const
{
    const QList<QWidget*> widgets = getVisibleWidgets();
    const int columns = getColumnCount(width);
    const int cellWidth = getCellWidth(width, columns);

    int height = 0;
    for (qsizetype rowStart = 0; rowStart < widgets.size(); rowStart += columns)
    {
        int rowHeight = 0;
        for (qsizetype i = rowStart; i < qMin(rowStart + columns, widgets.size()); ++i)
        {
            rowHeight = qMax(rowHeight, getCellHeight(widgets[i], cellWidth));
        }
        height += (rowStart > 0 ? m_verticalSpacing : 0) + rowHeight;
    }
    return height;
}

QSize PDFAppGrid::sizeHint() const
{
    const int columns = qMin(m_maximumColumns, qMax(1, int(getVisibleWidgets().size())));
    const int width = columns * m_minimumCellWidth + (columns - 1) * m_horizontalSpacing;
    return QSize(width, heightForWidth(width));
}

QSize PDFAppGrid::minimumSizeHint() const
{
    // A cell may be narrower than the minimal width in a very narrow window (no
    // horizontal scroll bar), the height follows the width
    return QSize(0, 0);
}

bool PDFAppGrid::event(QEvent* event)
{
    const bool result = BaseClass::event(event);
    switch (event->type())
    {
        case QEvent::LayoutRequest:
            // A cell has been shown, hidden, or it has changed its size
            updateGeometry();
            placeWidgets();
            break;

        case QEvent::Resize:
        case QEvent::LayoutDirectionChange:
        case QEvent::Show:
            placeWidgets();
            break;

        case QEvent::ChildRemoved:
        {
            // A deleted cell (its pointer is already cleared) or a cell given to
            // another parent (it has the grid as the parent until the event) is forgotten
            const QObject* child = static_cast<QChildEvent*>(event)->child();
            for (qsizetype i = m_widgets.size() - 1; i >= 0; --i)
            {
                if (m_widgets[i].data() == child)
                {
                    m_widgets.removeAt(i);
                }
            }
            removeDeletedWidgets();
            updateGeometry();
            placeWidgets();
            break;
        }

        default:
            break;
    }
    return result;
}

void PDFAppGrid::placeWidgets()
{
    if (m_isPlacing)
    {
        return;
    }
    m_isPlacing = true;

    const QList<QWidget*> widgets = getVisibleWidgets();
    const int columns = getColumnCount(width());
    const int cellWidth = getCellWidth(width(), columns);

    int y = 0;
    for (qsizetype rowStart = 0; rowStart < widgets.size(); rowStart += columns)
    {
        const qsizetype rowEnd = qMin(rowStart + columns, widgets.size());
        int rowHeight = 0;
        for (qsizetype i = rowStart; i < rowEnd; ++i)
        {
            rowHeight = qMax(rowHeight, getCellHeight(widgets[i], cellWidth));
        }

        for (qsizetype i = rowStart; i < rowEnd; ++i)
        {
            const auto [left, right] = getColumnEdges(width(), columns, int(i - rowStart));
            const QRect logical(left, y, right - left, rowHeight);
            widgets[i]->setGeometry(QStyle::visualRect(layoutDirection(), rect(), logical));
        }
        y += rowHeight + m_verticalSpacing;
    }

    m_isPlacing = false;
}

PDFAppFlowLayout::PDFAppFlowLayout(QWidget* parent, int horizontalSpacing, int verticalSpacing) :
    BaseClass(parent),
    m_horizontalSpacing(qMax(0, horizontalSpacing)),
    m_verticalSpacing(qMax(0, verticalSpacing))
{
    setContentsMargins(0, 0, 0, 0);
}

PDFAppFlowLayout::~PDFAppFlowLayout()
{
    while (!m_items.isEmpty())
    {
        delete m_items.takeLast();
    }
}

void PDFAppFlowLayout::addItem(QLayoutItem* item)
{
    if (item)
    {
        m_items.append(item);
    }
}

int PDFAppFlowLayout::count() const
{
    return int(m_items.size());
}

QLayoutItem* PDFAppFlowLayout::itemAt(int index) const
{
    return index >= 0 && index < m_items.size() ? m_items[index] : nullptr;
}

QLayoutItem* PDFAppFlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
}

Qt::Orientations PDFAppFlowLayout::expandingDirections() const
{
    return Qt::Orientations();
}

bool PDFAppFlowLayout::hasHeightForWidth() const
{
    return true;
}

int PDFAppFlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, qMax(0, width), 0), false, nullptr);
}

QSize PDFAppFlowLayout::minimumSize() const
{
    // The widest item, so a narrow window wraps every item to its own row
    QSize size;
    for (const QLayoutItem* item : m_items)
    {
        if (!item->isEmpty())
        {
            size = size.expandedTo(item->minimumSize());
        }
    }
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

QSize PDFAppFlowLayout::sizeHint() const
{
    // All items in one row
    int width = 0;
    int height = 0;
    for (const QLayoutItem* item : m_items)
    {
        if (item->isEmpty())
        {
            continue;
        }
        const QSize itemSize = item->sizeHint();
        width += (width > 0 ? m_horizontalSpacing : 0) + itemSize.width();
        height = qMax(height, itemSize.height());
    }
    const QMargins margins = contentsMargins();
    return QSize(width + margins.left() + margins.right(), height + margins.top() + margins.bottom());
}

void PDFAppFlowLayout::setGeometry(const QRect& rect)
{
    BaseClass::setGeometry(rect);
    doLayout(rect, true, nullptr);
}

int PDFAppFlowLayout::getRowCount(int width) const
{
    int rowCount = 0;
    doLayout(QRect(0, 0, qMax(0, width), 0), false, &rowCount);
    return rowCount;
}

int PDFAppFlowLayout::doLayout(const QRect& rect, bool isApplied, int* rowCount) const
{
    const QMargins margins = contentsMargins();
    const QRect area = rect.marginsRemoved(margins);
    const Qt::LayoutDirection direction = parentWidget() ? parentWidget()->layoutDirection() : Qt::LeftToRight;

    // Rows of the items: each row is centered vertically in its height
    QList<QList<QLayoutItem*>> rows;
    QList<int> rowHeights;
    int x = 0;
    for (QLayoutItem* item : m_items)
    {
        if (item->isEmpty())
        {
            continue;
        }

        const QSize itemSize = item->sizeHint();
        if (rows.isEmpty() || (x > 0 && x + m_horizontalSpacing + itemSize.width() > area.width()))
        {
            rows.append(QList<QLayoutItem*>());
            rowHeights.append(0);
            x = 0;
        }

        x += (x > 0 ? m_horizontalSpacing : 0) + itemSize.width();
        rows.back().append(item);
        rowHeights.back() = qMax(rowHeights.back(), itemSize.height());
    }

    int y = area.top();
    for (qsizetype rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
    {
        if (rowIndex > 0)
        {
            y += m_verticalSpacing;
        }

        if (isApplied)
        {
            int left = area.left();
            for (QLayoutItem* item : rows[rowIndex])
            {
                const QSize itemSize = item->sizeHint();
                const int itemWidth = qMin(itemSize.width(), area.width());
                const QRect logical(left, y + (rowHeights[rowIndex] - itemSize.height()) / 2, itemWidth, itemSize.height());
                item->setGeometry(QStyle::visualRect(direction, rect, logical));
                left += itemWidth + m_horizontalSpacing;
            }
        }
        y += rowHeights[rowIndex];
    }

    if (rowCount)
    {
        *rowCount = int(rows.size());
    }

    return y - area.top() + margins.top() + margins.bottom();
}

PDFAppResponsiveRow::PDFAppResponsiveRow(QWidget* leading, QWidget* trailing, int threshold, int spacing, QWidget* parent) :
    BaseClass(parent),
    m_threshold(qMax(0, threshold))
{
    m_layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(qMax(0, spacing));
    if (leading)
    {
        leading->setParent(this);
        m_layout->addWidget(leading, 1);
    }
    if (trailing)
    {
        trailing->setParent(this);
        m_layout->addWidget(trailing, 0);
    }
    updateArrangement(width());
}

PDFAppResponsiveRow::~PDFAppResponsiveRow() = default;

void PDFAppResponsiveRow::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateArrangement(event->size().width());
}

void PDFAppResponsiveRow::updateArrangement(int width)
{
    const bool isStacked = width < m_threshold;
    if (!m_layout || isStacked == m_isStacked)
    {
        return;
    }

    m_isStacked = isStacked;
    m_layout->setDirection(isStacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}

PDFAppSectionHeader::PDFAppSectionHeader(const QString& title, QWidget* parent) :
    BaseClass(parent)
{
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(12);

    // Title Bold 15, line 18
    m_title = PDFAppPageWidgets::createText(title, PDFThemeFonts::getFont(PDFThemeFonts::Role::Title), 18, PDFThemeColors::TextPrimary, this);
    m_title->setObjectName(QStringLiteral("sectionTitle"));
    m_layout->addWidget(m_title, 1);
}

PDFAppSectionHeader::~PDFAppSectionHeader() = default;

void PDFAppSectionHeader::setTrailingWidget(QWidget* widget)
{
    if (m_trailing.data() == widget)
    {
        return;
    }

    if (m_trailing)
    {
        delete m_trailing.data();
    }

    m_trailing = widget;
    if (widget && m_layout)
    {
        widget->setParent(this);
        m_layout->addWidget(widget, 0, Qt::AlignVCenter);
    }
}

PDFAppCard::PDFAppCard(const QMargins& margins, int spacing, QWidget* parent) :
    BaseClass(parent)
{
    PDFWidgetStyleProperties::set(this, PDFWidgetStyleProperties::CARD);
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(margins);
    m_layout->setSpacing(spacing);
}

PDFAppCard::~PDFAppCard() = default;

PDFLabel* PDFAppPageWidgets::createLabel(const QString& text, PDFThemeFonts::Role role, PDFThemeColors::Color color, QWidget* parent)
{
    PDFLabel* label = new PDFLabel(QString(), role, color, parent);
    label->setTextFormat(Qt::PlainText);
    label->setText(text);
    return label;
}

PDFLabel* PDFAppPageWidgets::createLabel(const QString& text, QFont::Weight weight, int pixelSize, PDFThemeColors::Color color, QWidget* parent)
{
    PDFLabel* label = createLabel(text, PDFThemeFonts::Role::Body, color, parent);
    label->setFont(PDFThemeFonts::getFont(weight, pixelSize));
    return label;
}

PDFAppParagraph* PDFAppPageWidgets::createText(const QString& text, const QFont& font, int lineHeight, PDFThemeColors::Color color, QWidget* parent)
{
    PDFAppParagraph* paragraph = new PDFAppParagraph(font, lineHeight, color, parent);
    paragraph->setText(text);
    return paragraph;
}

PDFAppParagraph* PDFAppPageWidgets::createHint(const QString& text, QWidget* parent)
{
    return createText(text, PDFThemeFonts::getFont(PDFThemeFonts::Role::Small), 15, PDFThemeColors::TextMuted, parent);
}

PDFAppSectionHeader* PDFAppPageWidgets::createSectionHeader(const QString& title, QWidget* trailing, QWidget* parent)
{
    PDFAppSectionHeader* header = new PDFAppSectionHeader(title, parent);
    header->setTrailingWidget(trailing);
    return header;
}

PDFAppCard* PDFAppPageWidgets::createCard(int padding, int spacing, QWidget* parent)
{
    const int margin = qMax(0, padding) + BORDER_WIDTH;
    return new PDFAppCard(QMargins(margin, margin, margin, margin), spacing, parent);
}

QPushButton* PDFAppPageWidgets::createButton(const QString& text, const QString& iconName, bool isPrimary, QWidget* parent)
{
    // The icon engine of the design gives also an empty name an icon (its place is
    // a part of the size of the buttons of the dialogs 941:135742 ...)
    QPushButton* button = new QPushButton(PDFThemeIcons::getIcon(iconName), text, parent);
    button->setAutoDefault(false);
    if (isPrimary)
    {
        PDFWidgetStyleProperties::set(button, PDFWidgetStyleProperties::PRIMARY);
    }
    return button;
}

void PDFAppPageWidgets::setLink(QWidget* widget, const QString& link)
{
    if (widget)
    {
        widget->setProperty(LINK_PROPERTY, link);
    }
}

QString PDFAppPageWidgets::getLink(const QObject* object)
{
    return object ? object->property(LINK_PROPERTY).toString() : QString();
}

PDFAppContentPage::PDFAppContentPage(PDFAppContext* context, QWidget* parent) :
    BaseClass(context, parent)
{

}

PDFAppContentPage::~PDFAppContentPage() = default;

QVariantMap PDFAppContentPage::saveViewState() const
{
    QVariantMap state;
    const int position = getScrollPosition();
    if (position >= 0)
    {
        state.insert(getScrollKey(), position);
    }
    return state;
}

void PDFAppContentPage::restoreViewState(const QVariantMap& state)
{
    // A stored value from the history (never trusted beyond its type)
    bool ok = false;
    const int position = state.value(getScrollKey()).toInt(&ok);
    if (ok && position >= 0)
    {
        requestDeferredScroll(position, QString());
    }
}

QScrollArea* PDFAppContentPage::getScrollArea() const
{
    for (QWidget* widget = parentWidget(); widget; widget = widget->parentWidget())
    {
        if (QScrollArea* scrollArea = qobject_cast<QScrollArea*>(widget))
        {
            if (scrollArea->widget() && scrollArea->widget()->isAncestorOf(this))
            {
                return scrollArea;
            }
        }
    }
    return nullptr;
}

int PDFAppContentPage::getScrollPosition() const
{
    QScrollArea* scrollArea = getScrollArea();
    return scrollArea ? scrollArea->verticalScrollBar()->value() : -1;
}

bool PDFAppContentPage::setScrollPosition(int position)
{
    QScrollArea* scrollArea = getScrollArea();
    if (!scrollArea || !scrollArea->widget())
    {
        return false;
    }

    // The layout of the content is finished first (a page, which has just been
    // shown, has its height only after the posted layout requests)
    QWidget* content = scrollArea->widget();
    if (content->layout())
    {
        content->layout()->activate();
    }
    QCoreApplication::sendPostedEvents(content, QEvent::LayoutRequest);

    QScrollBar* scrollBar = scrollArea->verticalScrollBar();
    scrollBar->setValue(position);
    return scrollBar->value() == qBound(scrollBar->minimum(), position, scrollBar->maximum()) && position <= scrollBar->maximum();
}

bool PDFAppContentPage::scrollToWidget(QWidget* widget, int offset)
{
    QScrollArea* scrollArea = getScrollArea();
    QWidget* content = scrollArea ? scrollArea->widget() : nullptr;
    if (!content || !widget || !content->isAncestorOf(widget))
    {
        return false;
    }

    if (content->layout())
    {
        content->layout()->activate();
    }
    QCoreApplication::sendPostedEvents(content, QEvent::LayoutRequest);

    const int position = widget->mapTo(content, QPoint(0, 0)).y() + offset;
    setScrollPosition(qMax(position, 0));
    return true;
}

bool PDFAppContentPage::openLink(const QString& link)
{
    if (link.isEmpty())
    {
        return true;
    }

    switch (PDFAppMarkdownParser::classifyLink(link))
    {
        case PDFAppMarkdownLinkKind::Route:
            return requestNavigation(PDFAppRoute::parse(link), PDFAppNavigationMode::Push);

        case PDFAppMarkdownLinkKind::External:
            return openExternalUrl(QUrl(link, QUrl::StrictMode));

        case PDFAppMarkdownLinkKind::Anchor:
            scrollToAnchor(link.mid(1));
            return true;

        case PDFAppMarkdownLinkKind::None:
            break;
    }

    qWarning("A link of the page was refused: %s", qPrintable(link.left(128)));
    return true;
}

bool PDFAppContentPage::scrollToAnchor(const QString& anchor)
{
    Q_UNUSED(anchor);
    return false;
}

void PDFAppContentPage::requestDeferredScroll(int position, const QString& anchor)
{
    m_anchorWidget = nullptr;
    if (position >= 0)
    {
        m_pendingScroll = position;
        m_pendingAnchor.clear();
    }
    else
    {
        m_pendingScroll.reset();
        m_pendingAnchor = anchor;
    }

    // The tab resets the scroll of a new page, when it gets it: the scroll is done
    // after the page has been shown and laid out
    if (isVisible())
    {
        queueDeferredScroll();
    }
}

void PDFAppContentPage::setAnchorWidget(QWidget* widget)
{
    m_anchorWidget = widget;
    m_pendingScroll.reset();
    m_pendingAnchor.clear();
    if (widget && isVisible())
    {
        queueDeferredScroll();
    }
}

bool PDFAppContentPage::openExternalUrl(const QUrl& url)
{
    QPointer<PDFAppContentPage> guard(this);
    Q_EMIT externalUrlRequested(url);
    return !guard.isNull();
}

QAbstractButton* PDFAppContentPage::withLink(QAbstractButton* button, const QString& link)
{
    if (button)
    {
        PDFAppPageWidgets::setLink(button, link);
        connect(button, &QAbstractButton::clicked, this, &PDFAppContentPage::onLinkClicked, Qt::UniqueConnection);
    }
    return button;
}

void PDFAppContentPage::onLinkClicked()
{
    // The clicked signal does not identify the button: its link is its property
    const QAbstractButton* button = qobject_cast<const QAbstractButton*>(sender());
    if (button)
    {
        openLink(PDFAppPageWidgets::getLink(button));
    }
}

void PDFAppContentPage::showEvent(QShowEvent* event)
{
    BaseClass::showEvent(event);

    if (m_pendingScroll || m_anchorWidget || !m_pendingAnchor.isEmpty())
    {
        queueDeferredScroll();
    }
}

void PDFAppContentPage::queueDeferredScroll()
{
    if (!m_isScrollQueued)
    {
        m_isScrollQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppContentPage::performDeferredScroll, Qt::QueuedConnection);
    }
}

void PDFAppContentPage::performDeferredScroll()
{
    m_isScrollQueued = false;
    if (!isVisible())
    {
        // Done, when the page is shown
        return;
    }

    if (m_pendingScroll)
    {
        const int position = *m_pendingScroll;
        m_pendingScroll.reset();
        setScrollPosition(position);
    }
    else if (m_anchorWidget)
    {
        // A widget outside of a scroll area stays pending (the page is not in a tab yet)
        if (scrollToWidget(m_anchorWidget))
        {
            m_anchorWidget = nullptr;
        }
    }
    else if (!m_pendingAnchor.isEmpty())
    {
        const QString anchor = m_pendingAnchor;
        m_pendingAnchor.clear();
        scrollToAnchor(anchor);
    }
}

}   // namespace pdf
