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

#include "pdfapptoolmergeview.h"

#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumenttoolbar.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolmergemodel.h"

#include "pdfaccenthue.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the views of the pages of Merge
class PDFAppToolMergeViewHelper
{
public:
    PDFAppToolMergeViewHelper() = delete;

    /// Interval of the scroll of a drag at the edge of a view (ms)
    static constexpr int AUTO_SCROLL_INTERVAL = 30;

    /// Widths of the columns of Details (408:32003: the x of the columns 337, 381, 555, 709, 793, 917, 1011,
    /// the cells 40, 170, 150, 80, 120, 90 with the gap 4); each cell starts with the padding 10 of
    /// the Library row, the first one at the edge of the card (326 + 1 + 10 = 337). The header
    /// cell has the padding 10 also at its end: "Page" (27) needs 47, so Page is 4 wider and
    /// Source 4 narrower than in the frame (Type and the next columns at their places)
    static constexpr int COLUMN_WIDTHS[] = { 48, 170, 154, 84, 124, 94 };

    static QFont getTitleFont() { return PDFThemeFonts::getFont(QFont::DemiBold, 12); }
    static QFont getSubtitleFont() { return PDFThemeFonts::getFont(QFont::Normal, 11); }
    static QFont getCalloutTextFont() { return PDFThemeFonts::getFont(QFont::Normal, 11); }

    /// Lines of a text wrapped to the width
    static int getLineCount(const QString& text, const QFont& font, int width)
    {
        if (text.isEmpty() || width <= 0)
        {
            return 0;
        }
        const QFontMetrics metrics(font);
        const QRect bounds = metrics.boundingRect(QRect(0, 0, width, std::numeric_limits<int>::max() / 4), Qt::TextWordWrap, text);
        return qMax(1, (bounds.height() + metrics.lineSpacing() - 1) / qMax(1, metrics.lineSpacing()));
    }

    /// The keys of the changes of the pages (Delete, Alt + an arrow): the view takes them before
    /// the shortcuts of the window (Alt + Left / Right are Back / Forward of the history)
    static bool isEditKey(const QKeyEvent* event, bool isHorizontalMove)
    {
        const int key = event->key();
        const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
        if ((key == Qt::Key_Delete || key == Qt::Key_Backspace) && modifiers == Qt::NoModifier)
        {
            return true;
        }
        const bool isVertical = key == Qt::Key_Up || key == Qt::Key_Down;
        const bool isHorizontal = key == Qt::Key_Left || key == Qt::Key_Right;
        return modifiers == Qt::AltModifier && (isVertical || (isHorizontalMove && isHorizontal));
    }

    /// The rows of a selection model (column 0), sorted
    static std::vector<int> getSelectedRows(const QItemSelectionModel* selectionModel)
    {
        std::vector<int> rows;
        if (selectionModel)
        {
            for (const QItemSelectionRange& range : selectionModel->selection())
            {
                for (int row = range.top(); row <= range.bottom(); ++row)
                {
                    rows.push_back(row);
                }
            }
        }
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
        return rows;
    }
};

/// The rows of Details are 27 high (row 408:32336: the paddings 6 and the text 15); the
/// painting is the one of the style
class PDFAppToolMergeDetailsDelegate : public QStyledItemDelegate
{
private:
    using BaseClass = QStyledItemDelegate;

public:
    explicit PDFAppToolMergeDetailsDelegate(QObject* parent) : BaseClass(parent) { }

    virtual QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = BaseClass::sizeHint(option, index);
        size.setHeight(PDFAppToolMergeDetailsView::ROW_HEIGHT);
        return size;
    }
};

/// The header of Details is 29 high (head 408:32321: the paddings 8 and the text 13); the
/// tree view takes the height of its viewport margin from the size hint of the header
class PDFAppToolMergeDetailsHeader : public QHeaderView
{
private:
    using BaseClass = QHeaderView;

public:
    explicit PDFAppToolMergeDetailsHeader(QWidget* parent) : BaseClass(Qt::Horizontal, parent) { }

    static constexpr int HEIGHT = 29;

    virtual QSize sizeHint() const override
    {
        return QSize(BaseClass::sizeHint().width(), HEIGHT);
    }
};

// ------------------------------------------------------- PDFAppToolMergeDragGesture

void PDFAppToolMergeDragGesture::press(int row, const QPoint& position)
{
    m_pressedRow = row;
    m_pressPosition = position;
    m_targetRow = -1;
    m_isDragging = false;
}

bool PDFAppToolMergeDragGesture::move(const QPoint& position)
{
    if (m_pressedRow < 0 || m_isDragging)
    {
        return false;
    }
    if ((position - m_pressPosition).manhattanLength() < QApplication::startDragDistance())
    {
        return false;
    }
    m_isDragging = true;
    return true;
}

void PDFAppToolMergeDragGesture::reset()
{
    m_pressedRow = -1;
    m_targetRow = -1;
    m_isDragging = false;
}

// -------------------------------------------------------- PDFAppToolMergePageView

PDFAppToolMergePageView::PDFAppToolMergePageView(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("mergePageView"));
    setFrameShape(QFrame::NoFrame);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setProperty("overlayScrollBars", true);
    setMouseTracking(true);
    setAccessibleName(tr("Pages of the result"));
    viewport()->setAutoFillBackground(false);
    verticalScrollBar()->setSingleStep(24);

    m_autoScrollTimer = new QTimer(this);
    m_autoScrollTimer->setInterval(PDFAppToolMergeViewHelper::AUTO_SCROLL_INTERVAL);
    connect(m_autoScrollTimer.data(), &QTimer::timeout, this, &PDFAppToolMergePageView::onAutoScroll);
}

PDFAppToolMergePageView::~PDFAppToolMergePageView() = default;

void PDFAppToolMergePageView::setTileDelegate(PDFPageTileDelegate* delegate)
{
    m_delegate = delegate;
    setItemDelegate(delegate);
    layoutGroups();
}

void PDFAppToolMergePageView::setEditable(bool isEditable)
{
    m_isEditable = isEditable;
    if (!isEditable)
    {
        m_gesture.reset();
        m_autoScrollTimer->stop();
        viewport()->unsetCursor();
        viewport()->update();
    }
}

std::vector<int> PDFAppToolMergePageView::getSelectedRows() const
{
    return PDFAppToolMergeViewHelper::getSelectedRows(selectionModel());
}

QItemSelection PDFAppToolMergePageView::createRowSelection(int first, int last) const
{
    QItemSelection selection;
    const QAbstractItemModel* itemModel = model();
    if (itemModel && first >= 0 && last >= first && last < itemModel->rowCount())
    {
        selection.select(itemModel->index(first, 0), itemModel->index(last, qMax(0, itemModel->columnCount() - 1)));
    }
    return selection;
}

void PDFAppToolMergePageView::selectRows(const std::vector<int>& rows)
{
    QItemSelectionModel* selection = selectionModel();
    const QAbstractItemModel* itemModel = model();
    if (!selection || !itemModel)
    {
        return;
    }

    QItemSelection items;
    for (const int row : rows)
    {
        items.merge(createRowSelection(row, row), QItemSelectionModel::Select);
    }
    const int first = rows.empty() ? -1 : rows.front();
    m_anchorRow = first;
    if (first >= 0 && first < itemModel->rowCount())
    {
        selection->setCurrentIndex(itemModel->index(first, 0), QItemSelectionModel::NoUpdate);
    }
    else
    {
        selection->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
    }
    selection->select(items, QItemSelectionModel::ClearAndSelect);
    if (first >= 0 && first < itemModel->rowCount())
    {
        scrollTo(itemModel->index(first, 0));
    }
}

void PDFAppToolMergePageView::setModel(QAbstractItemModel* itemModel)
{
    if (m_mergeModel)
    {
        disconnect(m_mergeModel.data(), nullptr, this, nullptr);
    }
    BaseClass::setModel(itemModel);
    m_mergeModel = qobject_cast<PDFAppToolMergePageModel*>(itemModel);
    if (m_mergeModel)
    {
        connect(m_mergeModel.data(), &PDFAppToolMergePageModel::groupsChanged, this, &PDFAppToolMergePageView::onGroupsChanged);
    }
    layoutGroups();
}

void PDFAppToolMergePageView::reset()
{
    BaseClass::reset();
    m_gesture.reset();
    m_hoverRow = -1;
    layoutGroups();
}

void PDFAppToolMergePageView::onGroupsChanged()
{
    layoutGroups();
}

QSize PDFAppToolMergePageView::getTileSize() const
{
    PDFComponentPainter::PageTile tile;
    tile.slotSize = m_delegate ? m_delegate->getSlotSize() : PDFAppToolMergeCanvas::SMALL_SLOT;
    tile.label = QStringLiteral("1");
    return PDFComponentPainter::getPageTileSize(tile);
}

int PDFAppToolMergePageView::getInnerWidth() const
{
    return qMax(1, viewport()->width() - 2 * CARD_PADDING_SIDE);
}

int PDFAppToolMergePageView::getColumns() const
{
    const QSize tile = getTileSize();
    return qMax(1, (getInnerWidth() + TILE_GAP) / qMax(1, tile.width() + TILE_GAP));
}

int PDFAppToolMergePageView::getCalloutHeight(int group) const
{
    using Helper = PDFAppToolMergeViewHelper;

    const std::vector<PDFAppToolMergePageModel::Group>& groups = m_mergeModel->getGroups();
    const PDFAppToolMergePageModel::Group& item = groups[size_t(group)];
    const int textWidth = getInnerWidth() - 2 * CALLOUT_PADDING_H;
    const int textHeight = Helper::getLineCount(item.calloutText, Helper::getCalloutTextFont(), textWidth) * CALLOUT_TEXT_LINE;
    switch (item.callout)
    {
        case PDFAppToolMergePageModel::GroupCallout::Reading:
            // callout-neutral 1390:34213: the title, the progress 4, the text, 4 apart
            return 2 * CALLOUT_PADDING_V + HEADER_HEIGHT + CALLOUT_GAP + PROGRESS_HEIGHT + CALLOUT_GAP + textHeight;
        case PDFAppToolMergePageModel::GroupCallout::Locked:
            // callout-red 1390:34515: the lock 16 with the title, the text 4 below
            return 2 * CALLOUT_PADDING_V + CALLOUT_ICON + CALLOUT_GAP + textHeight;
        case PDFAppToolMergePageModel::GroupCallout::None:
            break;
    }
    return 0;
}

void PDFAppToolMergePageView::layoutGroups()
{
    m_layouts.clear();
    int top = 0;
    if (m_mergeModel)
    {
        const std::vector<PDFAppToolMergePageModel::Group>& groups = m_mergeModel->getGroups();
        const QSize tile = getTileSize();
        const int columns = getColumns();
        m_layouts.reserve(groups.size());
        for (size_t i = 0; i < groups.size(); ++i)
        {
            const PDFAppToolMergePageModel::Group& group = groups[i];
            GroupLayout layout;
            layout.top = top;
            layout.tilesTop = top + CARD_PADDING_TOP + (group.tag.isEmpty() ? HEADER_HEIGHT : HEADER_TAG_HEIGHT) + HEADER_GAP;
            int body = 0;
            if (group.callout != PDFAppToolMergePageModel::GroupCallout::None)
            {
                layout.calloutHeight = getCalloutHeight(int(i));
                body = layout.calloutHeight;
            }
            else
            {
                const int lines = (group.rowCount + columns - 1) / columns;
                body = lines > 0 ? lines * tile.height() + (lines - 1) * TILE_GAP : 0;
            }
            layout.height = layout.tilesTop - top + body + CARD_PADDING_BOTTOM;
            m_layouts.push_back(layout);
            top += layout.height + GROUP_GAP;
        }
    }
    m_contentHeight = m_layouts.empty() ? 0 : top - GROUP_GAP;
    updateGeometries();
    viewport()->update();
}

void PDFAppToolMergePageView::updateGeometries()
{
    BaseClass::updateGeometries();
    verticalScrollBar()->setPageStep(viewport()->height());
    verticalScrollBar()->setRange(0, qMax(0, m_contentHeight - viewport()->height()));
}

QRect PDFAppToolMergePageView::getTileRect(int row) const
{
    if (!m_mergeModel)
    {
        return QRect();
    }

    const int group = m_mergeModel->getGroupOfRow(row);
    if (group < 0 || size_t(group) >= m_layouts.size())
    {
        return QRect();
    }

    const PDFAppToolMergePageModel::Group& item = m_mergeModel->getGroups()[size_t(group)];
    const QSize tile = getTileSize();
    const int columns = getColumns();
    const int index = row - item.firstRow;
    const int line = index / columns;
    const int column = index % columns;
    return QRect(CARD_PADDING_SIDE + column * (tile.width() + TILE_GAP), m_layouts[size_t(group)].tilesTop + line * (tile.height() + TILE_GAP), tile.width(), tile.height());
}

QRect PDFAppToolMergePageView::toViewport(const QRect& contentRect) const
{
    if (contentRect.isNull())
    {
        return QRect();
    }

    // Right to left: the flow starts at the right edge (the content is mirrored)
    QRect rect = contentRect.translated(0, -verticalOffset());
    if (layoutDirection() == Qt::RightToLeft)
    {
        rect.moveLeft(viewport()->width() - rect.left() - rect.width());
    }
    return rect;
}

QPoint PDFAppToolMergePageView::toContent(const QPoint& viewportPoint) const
{
    const int x = layoutDirection() == Qt::RightToLeft ? viewport()->width() - 1 - viewportPoint.x() : viewportPoint.x();
    return QPoint(x, viewportPoint.y() + verticalOffset());
}

int PDFAppToolMergePageView::findGroupAt(int contentY) const
{
    // The cards are sorted by their tops
    auto it = std::upper_bound(m_layouts.cbegin(), m_layouts.cend(), contentY, PDFAppToolMergePageView::isAboveLayout);
    if (it == m_layouts.cbegin())
    {
        return -1;
    }
    --it;
    return contentY < it->top + it->height ? int(it - m_layouts.cbegin()) : -1;
}

int PDFAppToolMergePageView::findRowAt(const QPoint& contentPoint) const
{
    const int group = findGroupAt(contentPoint.y());
    if (group < 0 || !m_mergeModel)
    {
        return -1;
    }

    const PDFAppToolMergePageModel::Group& item = m_mergeModel->getGroups()[size_t(group)];
    const QSize tile = getTileSize();
    const int x = contentPoint.x() - CARD_PADDING_SIDE;
    const int y = contentPoint.y() - m_layouts[size_t(group)].tilesTop;
    if (item.rowCount <= 0 || x < 0 || y < 0)
    {
        return -1;
    }

    const int column = x / (tile.width() + TILE_GAP);
    const int line = y / (tile.height() + TILE_GAP);
    if (column >= getColumns() || x % (tile.width() + TILE_GAP) >= tile.width() || y % (tile.height() + TILE_GAP) >= tile.height())
    {
        return -1;
    }

    const int index = line * getColumns() + column;
    return index < item.rowCount ? item.firstRow + index : -1;
}

QRect PDFAppToolMergePageView::visualRect(const QModelIndex& index) const
{
    return index.isValid() ? toViewport(getTileRect(index.row())) : QRect();
}

QRect PDFAppToolMergePageView::getGroupRect(int group) const
{
    if (group < 0 || size_t(group) >= m_layouts.size())
    {
        return QRect();
    }
    return toViewport(QRect(0, m_layouts[size_t(group)].top, viewport()->width(), m_layouts[size_t(group)].height));
}

void PDFAppToolMergePageView::scrollToGroup(int group)
{
    if (group >= 0 && size_t(group) < m_layouts.size())
    {
        verticalScrollBar()->setValue(m_layouts[size_t(group)].top);
    }
}

void PDFAppToolMergePageView::scrollTo(const QModelIndex& index, ScrollHint hint)
{
    const QRect rect = getTileRect(index.row());
    if (rect.isNull())
    {
        return;
    }

    const int top = verticalOffset();
    const int height = viewport()->height();
    switch (hint)
    {
        case PositionAtTop:
            verticalScrollBar()->setValue(rect.top());
            break;
        case PositionAtBottom:
            verticalScrollBar()->setValue(rect.bottom() - height + 1);
            break;
        case PositionAtCenter:
            verticalScrollBar()->setValue(rect.center().y() - height / 2);
            break;
        case EnsureVisible:
            if (rect.top() < top)
            {
                verticalScrollBar()->setValue(rect.top() - TILE_GAP);
            }
            else if (rect.bottom() >= top + height)
            {
                verticalScrollBar()->setValue(rect.bottom() - height + 1 + TILE_GAP);
            }
            break;
    }
}

QModelIndex PDFAppToolMergePageView::indexAt(const QPoint& point) const
{
    const int row = findRowAt(toContent(point));
    return row >= 0 && model() ? model()->index(row, 0) : QModelIndex();
}

int PDFAppToolMergePageView::getNeighborRow(int row, int lineDelta) const
{
    // The tile of the line above or below with the nearest center (also in another group)
    const QRect rect = getTileRect(row);
    const int rowCount = model() ? model()->rowCount() : 0;
    if (rect.isNull() || rowCount == 0)
    {
        return row;
    }

    const int step = lineDelta < 0 ? -1 : 1;
    int candidate = row + step;
    int lineTop = -1;
    int best = row;
    int bestDistance = std::numeric_limits<int>::max();
    for (; candidate >= 0 && candidate < rowCount; candidate += step)
    {
        const QRect candidateRect = getTileRect(candidate);
        if (candidateRect.top() == rect.top())
        {
            continue;
        }
        if (lineTop < 0)
        {
            lineTop = candidateRect.top();
        }
        if (candidateRect.top() != lineTop)
        {
            break;
        }
        const int distance = qAbs(candidateRect.center().x() - rect.center().x());
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = candidate;
        }
    }
    return best;
}

QModelIndex PDFAppToolMergePageView::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);

    const QAbstractItemModel* itemModel = model();
    const int rowCount = itemModel ? itemModel->rowCount() : 0;
    if (rowCount == 0)
    {
        return QModelIndex();
    }

    const QModelIndex current = currentIndex();
    const int row = current.isValid() ? current.row() : -1;
    if (row < 0)
    {
        return itemModel->index(0, 0);
    }

    // The flow goes from the leading edge: in right-to-left the left arrow goes on
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    int target = row;
    switch (cursorAction)
    {
        case MoveLeft:
            target = row + (isRightToLeft ? 1 : -1);
            break;
        case MoveRight:
            target = row + (isRightToLeft ? -1 : 1);
            break;
        case MovePrevious:
            target = row - 1;
            break;
        case MoveNext:
            target = row + 1;
            break;
        case MoveUp:
            target = getNeighborRow(row, -1);
            break;
        case MoveDown:
            target = getNeighborRow(row, 1);
            break;
        case MoveHome:
            target = 0;
            break;
        case MoveEnd:
            target = rowCount - 1;
            break;
        case MovePageUp:
        case MovePageDown:
        {
            const int lines = qMax(1, viewport()->height() / qMax(1, getTileSize().height() + TILE_GAP));
            for (int i = 0; i < lines; ++i)
            {
                target = getNeighborRow(target, cursorAction == MovePageUp ? -1 : 1);
            }
            break;
        }
    }
    return itemModel->index(qBound(0, target, rowCount - 1), 0);
}

int PDFAppToolMergePageView::horizontalOffset() const
{
    return 0;
}

int PDFAppToolMergePageView::verticalOffset() const
{
    return verticalScrollBar()->value();
}

bool PDFAppToolMergePageView::isIndexHidden(const QModelIndex& index) const
{
    Q_UNUSED(index);
    return false;
}

void PDFAppToolMergePageView::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command)
{
    // The tiles under the rectangle: the range of the rows from the first to the last one
    int first = std::numeric_limits<int>::max();
    int last = -1;
    const int rowCount = model() ? model()->rowCount() : 0;
    const QRect normalized = rect.normalized();
    for (int row = 0; row < rowCount; ++row)
    {
        if (visualRect(model()->index(row, 0)).intersects(normalized))
        {
            first = qMin(first, row);
            last = qMax(last, row);
        }
    }
    if (selectionModel())
    {
        selectionModel()->select(last >= 0 ? createRowSelection(first, last) : QItemSelection(), command | QItemSelectionModel::Rows);
    }
}

QRegion PDFAppToolMergePageView::visualRegionForSelection(const QItemSelection& selection) const
{
    QRegion region;
    for (const QItemSelectionRange& range : selection)
    {
        for (int row = range.top(); row <= range.bottom(); ++row)
        {
            const QRect rect = visualRect(model()->index(row, 0));
            if (rect.intersects(viewport()->rect()))
            {
                region += rect.adjusted(-1, -1, 1, 1);
            }
        }
    }
    return region;
}

void PDFAppToolMergePageView::selectRange(int anchorRow, int row, bool isAdded)
{
    if (!selectionModel())
    {
        return;
    }

    const int first = qMin(anchorRow, row);
    const int last = qMax(anchorRow, row);
    selectionModel()->setCurrentIndex(model()->index(row, 0), QItemSelectionModel::NoUpdate);
    selectionModel()->select(createRowSelection(first, last), (isAdded ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect) | QItemSelectionModel::Rows);
}

void PDFAppToolMergePageView::setCurrentRow(int row, QItemSelectionModel::SelectionFlags flags)
{
    if (!selectionModel() || !model() || row < 0 || row >= model()->rowCount())
    {
        return;
    }

    const QModelIndex index = model()->index(row, 0);
    selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    if (flags != QItemSelectionModel::NoUpdate)
    {
        selectionModel()->select(createRowSelection(row, row), flags | QItemSelectionModel::Rows);
    }
    scrollTo(index);
}

void PDFAppToolMergePageView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !model())
    {
        BaseClass::mousePressEvent(event);
        return;
    }

    setFocus(Qt::MouseFocusReason);
    const QPoint position = event->position().toPoint();
    const int row = findRowAt(toContent(position));
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    event->accept();
    if (row < 0)
    {
        // A press beside the tiles clears the selection
        if (!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) && selectionModel())
        {
            selectionModel()->clearSelection();
        }
        return;
    }

    if (modifiers & Qt::ShiftModifier)
    {
        selectRange(m_anchorRow >= 0 ? m_anchorRow : row, row, modifiers & Qt::ControlModifier);
        return;
    }
    if (modifiers & Qt::ControlModifier)
    {
        m_anchorRow = row;
        setCurrentRow(row, QItemSelectionModel::Toggle);
        return;
    }

    // A press on a selected page may start a drag of the selection: the selection stays until
    // the release (a click without a drag selects the page alone)
    if (selectionModel() && selectionModel()->isRowSelected(row, QModelIndex()))
    {
        if (m_isEditable)
        {
            m_gesture.press(row, position);
        }
        else
        {
            m_anchorRow = row;
            setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
        }
        return;
    }

    m_anchorRow = row;
    setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
    if (m_isEditable)
    {
        m_gesture.press(row, position);
    }
}

void PDFAppToolMergePageView::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    if (m_gesture.isPressed() && (event->buttons() & Qt::LeftButton))
    {
        if (m_gesture.move(position))
        {
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (m_gesture.isDragging())
        {
            updateDrag(position);
        }
        event->accept();
        return;
    }

    // The tile under the pointer is hovered
    const int row = findRowAt(toContent(position));
    if (row != m_hoverRow)
    {
        const int oldRow = m_hoverRow;
        m_hoverRow = row;
        for (const int changed : { oldRow, row })
        {
            if (changed >= 0)
            {
                viewport()->update(visualRect(model()->index(changed, 0)).adjusted(-2, -2, 2, 2));
            }
        }
    }
    event->accept();
}

void PDFAppToolMergePageView::updateDrag(const QPoint& viewportPosition)
{
    m_lastDragPosition = viewportPosition;
    m_gesture.setTargetRow(getDropRow(viewportPosition));

    // At the edge the view scrolls while the pointer stays
    if (viewportPosition.y() < AUTO_SCROLL_MARGIN)
    {
        m_autoScrollDelta = -AUTO_SCROLL_STEP;
    }
    else if (viewportPosition.y() > viewport()->height() - AUTO_SCROLL_MARGIN)
    {
        m_autoScrollDelta = AUTO_SCROLL_STEP;
    }
    else
    {
        m_autoScrollDelta = 0;
    }

    if (m_autoScrollDelta != 0 && !m_autoScrollTimer->isActive())
    {
        m_autoScrollTimer->start();
    }
    else if (m_autoScrollDelta == 0)
    {
        m_autoScrollTimer->stop();
    }
    viewport()->update();
}

void PDFAppToolMergePageView::onAutoScroll()
{
    if (!m_gesture.isDragging() || m_autoScrollDelta == 0)
    {
        m_autoScrollTimer->stop();
        return;
    }
    verticalScrollBar()->setValue(verticalScrollBar()->value() + m_autoScrollDelta);
    m_gesture.setTargetRow(getDropRow(m_lastDragPosition));
    viewport()->update();
}

void PDFAppToolMergePageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_gesture.isPressed())
    {
        BaseClass::mouseReleaseEvent(event);
        return;
    }

    event->accept();
    const bool isDragging = m_gesture.isDragging();
    const int pressedRow = m_gesture.getPressedRow();
    const int targetRow = m_gesture.getTargetRow();
    m_gesture.reset();
    m_autoScrollTimer->stop();
    viewport()->unsetCursor();
    viewport()->update();

    if (!isDragging)
    {
        // A click on a page of a selection of more pages selects it alone
        m_anchorRow = pressedRow;
        setCurrentRow(pressedRow, QItemSelectionModel::ClearAndSelect);
        return;
    }

    // The drop is the last thing: the slot changes the model of the view
    const std::vector<int> rows = getSelectedRows();
    if (!rows.empty() && targetRow >= 0)
    {
        Q_EMIT moveRequested(rows, targetRow);
    }
}

void PDFAppToolMergePageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    // A double click is no action of a page (no editor, no activation of the base)
    event->accept();
}

int PDFAppToolMergePageView::getDropRow(const QPoint& position) const
{
    const int rowCount = model() ? model()->rowCount() : 0;
    if (!m_mergeModel || rowCount == 0)
    {
        return 0;
    }

    // The nearest tile of the line under the pointer: its leading half puts the pages in front
    // of it, its trailing half after it
    const QPoint content = toContent(position);
    if (content.y() < 0)
    {
        return 0;
    }
    if (content.y() >= m_contentHeight)
    {
        return rowCount;
    }

    int group = findGroupAt(content.y());
    if (group < 0)
    {
        // The gap between two cards: in front of the next card
        auto it = std::upper_bound(m_layouts.cbegin(), m_layouts.cend(), content.y(), PDFAppToolMergePageView::isAboveLayout);
        group = int(it - m_layouts.cbegin());
        while (size_t(group) < m_layouts.size() && m_mergeModel->getGroups()[size_t(group)].rowCount == 0)
        {
            ++group;
        }
        return size_t(group) < m_layouts.size() ? m_mergeModel->getGroups()[size_t(group)].firstRow : rowCount;
    }

    const PDFAppToolMergePageModel::Group& item = m_mergeModel->getGroups()[size_t(group)];
    if (item.rowCount == 0)
    {
        return item.firstRow;
    }

    const QSize tile = getTileSize();
    const int columns = getColumns();
    const int lines = (item.rowCount + columns - 1) / columns;
    const int line = qBound(0, (content.y() - m_layouts[size_t(group)].tilesTop) / (tile.height() + TILE_GAP), lines - 1);
    const int column = qBound(0, (content.x() - CARD_PADDING_SIDE + TILE_GAP / 2) / (tile.width() + TILE_GAP), columns);
    const int lineFirst = line * columns;
    const int lineLast = qMin(item.rowCount, lineFirst + columns);
    const int index = qBound(lineFirst, lineFirst + column, lineLast);
    return item.firstRow + index;
}

void PDFAppToolMergePageView::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const int key = event->key();

    // Delete removes the selected pages, Alt + an arrow moves them (in the order of the flow)
    if (m_isEditable && (key == Qt::Key_Delete || key == Qt::Key_Backspace) && modifiers == Qt::NoModifier)
    {
        event->accept();
        if (!getSelectedRows().empty())
        {
            Q_EMIT removeRequested();
        }
        return;
    }
    if (m_isEditable && modifiers == Qt::AltModifier && (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right))
    {
        event->accept();
        const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
        const bool isBack = key == Qt::Key_Up || (key == Qt::Key_Left) != isRightToLeft;
        if (!getSelectedRows().empty())
        {
            Q_EMIT moveByRequested(isBack ? -1 : 1);
        }
        return;
    }
    if (key == Qt::Key_Escape && m_gesture.isDragging())
    {
        event->accept();
        m_gesture.reset();
        m_autoScrollTimer->stop();
        viewport()->unsetCursor();
        viewport()->update();
        return;
    }

    // The arrows: the current page, Shift extends the range from the anchor, Ctrl moves only
    // the current page
    CursorAction action = MoveNext;
    bool isNavigation = true;
    switch (key)
    {
        case Qt::Key_Left: action = MoveLeft; break;
        case Qt::Key_Right: action = MoveRight; break;
        case Qt::Key_Up: action = MoveUp; break;
        case Qt::Key_Down: action = MoveDown; break;
        case Qt::Key_Home: action = MoveHome; break;
        case Qt::Key_End: action = MoveEnd; break;
        case Qt::Key_PageUp: action = MovePageUp; break;
        case Qt::Key_PageDown: action = MovePageDown; break;
        default: isNavigation = false; break;
    }
    if (isNavigation && (modifiers & ~(Qt::ShiftModifier | Qt::ControlModifier | Qt::KeypadModifier)) == Qt::NoModifier)
    {
        event->accept();
        const QModelIndex target = moveCursor(action, modifiers);
        if (!target.isValid())
        {
            return;
        }
        if (modifiers & Qt::ShiftModifier)
        {
            selectRange(m_anchorRow >= 0 ? m_anchorRow : target.row(), target.row(), false);
            scrollTo(target);
        }
        else if (modifiers & Qt::ControlModifier)
        {
            setCurrentRow(target.row(), QItemSelectionModel::NoUpdate);
            viewport()->update();
        }
        else
        {
            m_anchorRow = target.row();
            setCurrentRow(target.row(), QItemSelectionModel::ClearAndSelect);
        }
        return;
    }
    if (key == Qt::Key_Space && modifiers == Qt::ControlModifier && currentIndex().isValid())
    {
        event->accept();
        m_anchorRow = currentIndex().row();
        setCurrentRow(currentIndex().row(), QItemSelectionModel::Toggle);
        return;
    }

    BaseClass::keyPressEvent(event);
}

bool PDFAppToolMergePageView::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && m_isEditable && PDFAppToolMergeViewHelper::isEditKey(static_cast<QKeyEvent*>(event), true))
    {
        event->accept();
        return true;
    }
    return BaseClass::event(event);
}

void PDFAppToolMergePageView::leaveEvent(QEvent* event)
{
    BaseClass::leaveEvent(event);
    if (m_hoverRow >= 0)
    {
        m_hoverRow = -1;
        viewport()->update();
    }
}

void PDFAppToolMergePageView::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LayoutDirectionChange || event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
    {
        layoutGroups();
    }
}

void PDFAppToolMergePageView::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutGroups();
}

void PDFAppToolMergePageView::scrollContentsBy(int dx, int dy)
{
    Q_UNUSED(dx);
    Q_UNUSED(dy);
    viewport()->update();
}

void PDFAppToolMergePageView::paintEvent(QPaintEvent* event)
{
    QPainter painter(viewport());
    const QRect paintRect = event->rect();

    // The stage lies on the canvas (bg/track)
    painter.fillRect(paintRect, PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
    if (!m_mergeModel)
    {
        return;
    }

    const int top = verticalOffset() + paintRect.top();
    const int bottom = verticalOffset() + paintRect.bottom();
    int group = findGroupAt(top);
    if (group < 0)
    {
        auto it = std::upper_bound(m_layouts.cbegin(), m_layouts.cend(), top, PDFAppToolMergePageView::isAboveLayout);
        group = int(it - m_layouts.cbegin());
    }
    for (; group >= 0 && size_t(group) < m_layouts.size() && m_layouts[size_t(group)].top <= bottom; ++group)
    {
        paintGroup(&painter, group, paintRect);
    }
    paintDropLine(&painter);
}

void PDFAppToolMergePageView::paintGroup(QPainter* painter, int group, const QRect& paintRect)
{
    using Color = PDFThemeColors::Color;
    using Helper = PDFAppToolMergeViewHelper;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const PDFAppToolMergePageModel::Group& item = m_mergeModel->getGroups()[size_t(group)];
    const GroupLayout& layout = m_layouts[size_t(group)];
    const Qt::LayoutDirection direction = layoutDirection();
    PDFComponentPainter::State state(QStyle::State_Enabled, direction);
    if (!isEnabled())
    {
        state.flags = QStyle::State_None;
    }

    // The card (bg/surface, border/default, radius 10)
    const QRectF card = QRectF(toViewport(QRect(0, layout.top, viewport()->width(), layout.height)));
    PDFStylePainter::fill(painter, card, PDFStylePainter::Radii(CARD_RADIUS), Color::BgSurface);
    PDFStylePainter::strokeInside(painter, card, PDFStylePainter::Radii(CARD_RADIUS), Color::BorderDefault);

    // The header: the dot, the title, the run, the Tag (378:1243) on one line
    const int headerHeight = item.tag.isEmpty() ? HEADER_HEIGHT : HEADER_TAG_HEIGHT;
    const int headerTop = layout.top + CARD_PADDING_TOP;
    int x = CARD_PADDING_SIDE;
    const QRect dotRect(x, headerTop + (headerHeight - DOT) / 2, DOT, DOT);
    PDFStylePainter::fillEllipse(painter, QRectF(toViewport(dotRect)), PDFStylePainter::color(PDFAccentHueColors::getColor(item.hue, PDFAccentHueColors::Role::Icon)));
    x += DOT + DOT_GAP;

    const int available = viewport()->width() - CARD_PADDING_SIDE - x;
    const QFont titleFont = Helper::getTitleFont();
    const int titleWidth = qMin(available, int(std::ceil(PDFStylePainter::textWidth(item.title, titleFont))) + 1);
    PDFComponentPainter::drawElidedText(painter, state, QRectF(toViewport(QRect(x, headerTop, titleWidth, headerHeight))), item.title, titleFont,
                                        isEnabled() ? Color::TextPrimary : Color::TextDisabled, PDFComponentPainter::TextKind::FileName);
    x += titleWidth + DOT_GAP;

    const QFont subtitleFont = Helper::getSubtitleFont();
    const int subtitleWidth = qMax(0, qMin(viewport()->width() - CARD_PADDING_SIDE - x, int(std::ceil(PDFStylePainter::textWidth(item.subtitle, subtitleFont))) + 1));
    PDFComponentPainter::drawElidedText(painter, state, QRectF(toViewport(QRect(x, headerTop, subtitleWidth, headerHeight))), item.subtitle, subtitleFont,
                                        isEnabled() ? Color::TextTertiary : Color::TextDisabled);
    x += subtitleWidth + DOT_GAP;

    if (!item.tag.isEmpty())
    {
        PDFComponentPainter::Tag tag;
        tag.text = item.tag;
        const QSize tagSize = PDFComponentPainter::getTagSize(tag);
        PDFComponentPainter::paintTag(painter, QRectF(toViewport(QRect(x, headerTop, tagSize.width(), tagSize.height()))), tag, state);
    }

    // The callout of a pending input
    if (item.callout != PDFAppToolMergePageModel::GroupCallout::None)
    {
        const bool isLocked = item.callout == PDFAppToolMergePageModel::GroupCallout::Locked;
        const QRect calloutContent(CARD_PADDING_SIDE, layout.tilesTop, getInnerWidth(), layout.calloutHeight);
        const QRectF callout = QRectF(toViewport(calloutContent));
        PDFComponentPainter::paintTintedCard(painter, callout, isLocked ? PDFAccentHue::Red : PDFAccentHue::Neutral, CALLOUT_RADIUS);

        int y = layout.tilesTop + CALLOUT_PADDING_V;
        int textLeft = CARD_PADDING_SIDE + CALLOUT_PADDING_H;
        const int textWidth = getInnerWidth() - 2 * CALLOUT_PADDING_H;
        if (isLocked)
        {
            PDFStylePainter::icon(painter, QRectF(toViewport(QRect(textLeft, y, CALLOUT_ICON, CALLOUT_ICON))), QStringLiteral("lock"),
                                  PDFAccentHueColors::getColor(PDFAccentHue::Red, PDFAccentHueColors::Role::Icon));
            const int titleLeft = textLeft + CALLOUT_ICON + 8;
            PDFComponentPainter::drawElidedText(painter, state, QRectF(toViewport(QRect(titleLeft, y, textWidth - CALLOUT_ICON - 8, CALLOUT_ICON))), item.calloutTitle, titleFont, Color::TextPrimary);
            y += CALLOUT_ICON + CALLOUT_GAP;
        }
        else
        {
            PDFComponentPainter::drawElidedText(painter, state, QRectF(toViewport(QRect(textLeft, y, textWidth, HEADER_HEIGHT))), item.calloutTitle, titleFont, Color::TextPrimary);
            y += HEADER_HEIGHT + CALLOUT_GAP;
            PDFComponentPainter::paintProgressBar(painter, QRectF(toViewport(QRect(textLeft, y, textWidth, PROGRESS_HEIGHT))), qMax(0.0, item.progress), state);
            y += PROGRESS_HEIGHT + CALLOUT_GAP;
        }

        const QFont textFont = Helper::getCalloutTextFont();
        const int lines = Helper::getLineCount(item.calloutText, textFont, textWidth);
        const int flags = int(state.leadingAlignment()) | Qt::AlignTop | Qt::TextWordWrap;
        const PDFComponentPainter::DirectionGuard directionGuard(painter, state);
        PDFStylePainter::text(painter, QRectF(toViewport(QRect(textLeft, y, textWidth, lines * CALLOUT_TEXT_LINE + 2))), flags, item.calloutText, textFont,
                              PDFStylePainter::color(Color::TextSecondary));
        return;
    }

    // The tiles of the group, which lie in the painted rectangle
    if (!m_delegate || item.rowCount <= 0)
    {
        return;
    }

    const QSize tile = getTileSize();
    const int columns = getColumns();
    const int lineHeight = tile.height() + TILE_GAP;
    const int paintTop = verticalOffset() + paintRect.top();
    const int paintBottom = verticalOffset() + paintRect.bottom();
    const int firstLine = qMax(0, (paintTop - layout.tilesTop) / lineHeight);
    const int lastLine = qMax(0, (paintBottom - layout.tilesTop) / lineHeight);
    const int firstIndex = firstLine * columns;
    const int lastIndex = qMin(item.rowCount - 1, (lastLine + 1) * columns - 1);
    const QModelIndex current = currentIndex();
    for (int index = firstIndex; index <= lastIndex; ++index)
    {
        const int row = item.firstRow + index;
        const QModelIndex modelIndex = model()->index(row, 0);
        QStyleOptionViewItem option;
        initViewItemOption(&option);
        option.rect = visualRect(modelIndex);
        option.state |= QStyle::State_Enabled;
        if (!isEnabled())
        {
            option.state &= ~QStyle::State_Enabled;
        }
        if (row == m_hoverRow)
        {
            option.state |= QStyle::State_MouseOver;
        }
        if (current == modelIndex && hasFocus())
        {
            option.state |= QStyle::State_HasFocus;
        }
        if (m_mergeModel->isRowSelected(row))
        {
            // The selected tile has its border outside of the slot of the flow (87 x 130, 378:1293)
            option.rect.adjust(-1, -1, 1, 1);
        }
        m_delegate->paint(painter, option, modelIndex);
    }
}

void PDFAppToolMergePageView::paintDropLine(QPainter* painter)
{
    if (!m_gesture.isDragging() || !m_mergeModel)
    {
        return;
    }

    const int rowCount = model()->rowCount();
    const int target = m_gesture.getTargetRow();
    if (target < 0 || rowCount == 0)
    {
        return;
    }

    // The line in the gap at the leading edge of the target tile, after the last tile of a line
    // or of the content at its trailing edge
    // (a place between two lines or two cards is drawn where the pointer is: after the tile of
    // its line, else in front of the target)
    QRect line;
    const QRect targetRect = target < rowCount ? getTileRect(target) : QRect();
    const QRect previousRect = target > 0 ? getTileRect(target - 1) : QRect();
    const int pointerY = toContent(m_lastDragPosition).y();
    const bool isPointerAtPrevious = !previousRect.isNull() && pointerY >= previousRect.top() - TILE_GAP && pointerY <= previousRect.bottom() + TILE_GAP;
    const bool isAfterPrevious = targetRect.isNull() || (isPointerAtPrevious && previousRect.top() != targetRect.top());
    if (!isAfterPrevious)
    {
        line = QRect(targetRect.left() - TILE_GAP / 2 - DROP_LINE / 2, targetRect.top(), DROP_LINE, targetRect.height());
    }
    else if (!previousRect.isNull())
    {
        line = QRect(previousRect.right() + 1 + TILE_GAP / 2 - DROP_LINE / 2, previousRect.top(), DROP_LINE, previousRect.height());
    }

    if (!line.isNull())
    {
        const PDFStylePainterGuard guard(painter);
        PDFStylePainter::fillRect(painter, QRectF(toViewport(line)), PDFThemeColors::BgAccent);
    }
}

bool PDFAppToolMergePageView::isAboveLayout(int contentY, const GroupLayout& layout)
{
    return contentY < layout.top;
}

// ----------------------------------------------------- PDFAppToolMergeDetailsView

PDFAppToolMergeDetailsView::PDFAppToolMergeDetailsView(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("mergeDetailsView"));
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setUniformRowHeights(true);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    // The last column takes the rest of the card (408:32003 has no horizontal scroll bar)
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setProperty("overlayScrollBars", true);
    setFrameShape(QFrame::NoFrame);
    setAccessibleName(tr("Pages of the result"));
    // details 408:32320: the plain header, the cells with the padding 10, the selection only
    // fills the row (bg/accent-subtle, the texts keep their colors) - the Library row; rows 27
    setRowStyle(RowStyle::Library);
    setItemDelegate(new PDFAppToolMergeDetailsDelegate(this));
    setHeader(new PDFAppToolMergeDetailsHeader(this));
    header()->setStretchLastSection(true);
    header()->setMinimumSectionSize(24);
    header()->setSectionsMovable(false);
    header()->setSectionsClickable(false);
    setDefaultColumnWidths();
}

PDFAppToolMergeDetailsView::~PDFAppToolMergeDetailsView() = default;

void PDFAppToolMergeDetailsView::setEditable(bool isEditable)
{
    m_isEditable = isEditable;
    if (!isEditable)
    {
        m_gesture.reset();
        viewport()->unsetCursor();
        viewport()->update();
    }
}

QSize PDFAppToolMergeDetailsView::sizeHint() const
{
    const int rowCount = model() ? model()->rowCount() : 0;
    const int headerHeight = isHeaderHidden() ? 0 : header()->sizeHint().height();
    return QSize(BaseClass::sizeHint().width(), headerHeight + rowCount * ROW_HEIGHT + 2 * frameWidth());
}

void PDFAppToolMergeDetailsView::setDefaultColumnWidths()
{
    int column = 0;
    for (const int width : PDFAppToolMergeViewHelper::COLUMN_WIDTHS)
    {
        header()->resizeSection(column++, width);
    }
}

std::vector<int> PDFAppToolMergeDetailsView::getSelectedRows() const
{
    return PDFAppToolMergeViewHelper::getSelectedRows(selectionModel());
}

int PDFAppToolMergeDetailsView::getDropRow(const QPoint& position) const
{
    const int rowCount = model() ? model()->rowCount() : 0;
    const QModelIndex index = indexAt(QPoint(qBound(0, position.x(), qMax(0, viewport()->width() - 1)), position.y()));
    if (!index.isValid())
    {
        return position.y() < 0 ? 0 : rowCount;
    }

    // The upper half of a row puts the rows in front of it, the lower half after it
    const QRect rect = visualRect(index);
    return position.y() < rect.center().y() ? index.row() : index.row() + 1;
}

void PDFAppToolMergeDetailsView::mousePressEvent(QMouseEvent* event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    const bool isPlainPress = event->button() == Qt::LeftButton && event->modifiers() == Qt::NoModifier && index.isValid();
    if (m_isEditable && isPlainPress && selectionModel() && selectionModel()->isRowSelected(index.row(), QModelIndex()))
    {
        // A drag of the selected rows may start: the selection stays until the release
        m_gesture.press(index.row(), event->position().toPoint());
        setFocus(Qt::MouseFocusReason);
        event->accept();
        return;
    }

    BaseClass::mousePressEvent(event);
    if (m_isEditable && isPlainPress)
    {
        m_gesture.press(index.row(), event->position().toPoint());
    }
}

void PDFAppToolMergeDetailsView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_gesture.isPressed() && (event->buttons() & Qt::LeftButton))
    {
        if (m_gesture.move(event->position().toPoint()))
        {
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        if (m_gesture.isDragging())
        {
            m_gesture.setTargetRow(getDropRow(event->position().toPoint()));
            viewport()->update();
            event->accept();
            return;
        }
    }
    BaseClass::mouseMoveEvent(event);
}

void PDFAppToolMergeDetailsView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_gesture.isPressed())
    {
        BaseClass::mouseReleaseEvent(event);
        return;
    }

    const bool isDragging = m_gesture.isDragging();
    const int pressedRow = m_gesture.getPressedRow();
    const int targetRow = m_gesture.getTargetRow();
    m_gesture.reset();
    viewport()->unsetCursor();
    viewport()->update();
    if (!isDragging)
    {
        BaseClass::mouseReleaseEvent(event);
        if (selectionModel() && model() && pressedRow >= 0 && pressedRow < model()->rowCount() && getSelectedRows().size() > 1)
        {
            // A click on a row of a selection of more rows selects it alone
            const QModelIndex index = model()->index(pressedRow, 0);
            selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        return;
    }

    event->accept();
    const std::vector<int> rows = getSelectedRows();
    if (!rows.empty() && targetRow >= 0)
    {
        Q_EMIT moveRequested(rows, targetRow);
    }
}

void PDFAppToolMergeDetailsView::keyPressEvent(QKeyEvent* event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const int key = event->key();
    if (m_isEditable && (key == Qt::Key_Delete || key == Qt::Key_Backspace) && modifiers == Qt::NoModifier)
    {
        event->accept();
        if (!getSelectedRows().empty())
        {
            Q_EMIT removeRequested();
        }
        return;
    }
    if (m_isEditable && modifiers == Qt::AltModifier && (key == Qt::Key_Up || key == Qt::Key_Down))
    {
        event->accept();
        if (!getSelectedRows().empty())
        {
            Q_EMIT moveByRequested(key == Qt::Key_Up ? -1 : 1);
        }
        return;
    }
    if (key == Qt::Key_Escape && m_gesture.isDragging())
    {
        event->accept();
        m_gesture.reset();
        viewport()->unsetCursor();
        viewport()->update();
        return;
    }
    BaseClass::keyPressEvent(event);
}

bool PDFAppToolMergeDetailsView::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride && m_isEditable && PDFAppToolMergeViewHelper::isEditKey(static_cast<QKeyEvent*>(event), false))
    {
        event->accept();
        return true;
    }
    return BaseClass::event(event);
}

void PDFAppToolMergeDetailsView::paintEvent(QPaintEvent* event)
{
    BaseClass::paintEvent(event);
    if (!m_gesture.isDragging() || !model())
    {
        return;
    }

    // The line of the drop between two rows (bg/accent)
    const int rowCount = model()->rowCount();
    const int target = m_gesture.getTargetRow();
    int y = 0;
    if (target < rowCount && target >= 0)
    {
        y = visualRect(model()->index(target, 0)).top();
    }
    else if (rowCount > 0)
    {
        y = visualRect(model()->index(rowCount - 1, 0)).bottom() + 1;
    }

    QPainter painter(viewport());
    PDFStylePainter::fillRect(&painter, QRectF(0, y - PDFAppToolMergePageView::DROP_LINE / 2, viewport()->width(), PDFAppToolMergePageView::DROP_LINE), PDFThemeColors::BgAccent);
}

// --------------------------------------------------------- PDFAppToolMergeCanvas

PDFAppToolMergeCanvas::PDFAppToolMergeCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("mergeCanvas"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(0);

    // preview-bar 378:1224: the title, Size (1370:34102), the segments Thumbnails / Details
    m_bar = new QWidget(this);
    m_bar->setObjectName(QStringLiteral("mergePreviewBar"));
    m_bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(m_bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(8);
    m_header = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, m_bar);
    m_header->setObjectName(QStringLiteral("mergePreviewTitle"));
    m_header->setMinimumWidth(1);
    barLayout->addWidget(m_header, 1);
    m_tileSizeButton = PDFAppDocumentToolbar::createToolbarMenuButton(QStringLiteral("mergeTileSize"), QStringLiteral("pages"), m_bar);
    barLayout->addWidget(m_tileSizeButton);
    m_viewSwitch = new PDFSegmentedControl(m_bar);
    m_viewSwitch->setObjectName(QStringLiteral("mergeViewSwitch"));
    m_viewSwitch->addSegment(QString());
    m_viewSwitch->addSegment(QString());
    m_viewSwitch->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    connect(m_viewSwitch.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppToolMergeCanvas::onViewSwitchChanged);
    barLayout->addWidget(m_viewSwitch);
    layout->addWidget(m_bar);
    layout->addSpacing(BAR_GAP);

    // The pages: the groups or Details
    m_pages = new QWidget(this);
    m_pages->setObjectName(QStringLiteral("mergePages"));
    m_stack = new QStackedLayout(m_pages);
    m_stack->setContentsMargins(0, 0, 0, 0);

    m_model = new PDFAppToolMergePageModel(this);
    m_delegate = new PDFPageTileDelegate(this);
    m_pageView = new PDFAppToolMergePageView(m_pages);
    m_pageView->setModel(m_model);
    m_pageView->setTileDelegate(m_delegate);
    m_stack->addWidget(m_pageView);
    m_scheduler = new PDFAppThumbnailScheduler(m_model, m_pageView, this);

    // pages-merge-details 408:32003: the table in a card and its note below
    m_detailsPage = new QWidget(m_pages);
    m_detailsPage->setObjectName(QStringLiteral("mergeDetailsPage"));
    QVBoxLayout* detailsLayout = new QVBoxLayout(m_detailsPage);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(NOTE_GAP);
    PDFAppCard* card = PDFAppPageWidgets::createCard(PDFAppPageWidgets::BORDER_WIDTH, 0, m_detailsPage);
    card->setObjectName(QStringLiteral("mergeDetailsCard"));
    m_detailsView = new PDFAppToolMergeDetailsView(card);
    m_detailsView->setModel(m_model);
    m_detailsView->setSelectionModel(m_pageView->selectionModel());
    m_detailsView->setDefaultColumnWidths();
    card->getLayout()->addWidget(m_detailsView);
    // The card hugs its rows, the note follows 10 below them (408:32592); the rest of the
    // canvas stays empty. A new count of the rows asks for a new size.
    detailsLayout->addWidget(card);
    connect(m_model.data(), &QAbstractItemModel::rowsInserted, m_detailsView.data(), &QWidget::updateGeometry);
    connect(m_model.data(), &QAbstractItemModel::rowsRemoved, m_detailsView.data(), &QWidget::updateGeometry);
    connect(m_model.data(), &QAbstractItemModel::modelReset, m_detailsView.data(), &QWidget::updateGeometry);
    m_detailsNote = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, m_detailsPage);
    m_detailsNote->setObjectName(QStringLiteral("mergeDetailsNote"));
    detailsLayout->addWidget(m_detailsNote);
    detailsLayout->addStretch(1);
    m_stack->addWidget(m_detailsPage);
    layout->addWidget(m_pages, 1);

    setTileSize(m_tileSize);
    retranslate();
}

PDFAppToolMergeCanvas::~PDFAppToolMergeCanvas() = default;

void PDFAppToolMergeCanvas::setEmptyState(QWidget* emptyState)
{
    if (m_emptyState == emptyState)
    {
        return;
    }
    if (m_emptyState)
    {
        // The old one may be the sender of the change (Add of an open document): later
        m_emptyState->hide();
        m_emptyState->deleteLater();
    }

    m_emptyState = emptyState;
    QVBoxLayout* canvasLayout = qobject_cast<QVBoxLayout*>(layout());
    if (m_emptyState)
    {
        m_emptyState->setParent(this);
        canvasLayout->addWidget(m_emptyState, 1);
        m_emptyState->show();
    }

    // 378:1657: the stage without the bar of the preview
    const bool isEmpty = !m_emptyState.isNull();
    m_bar->setVisible(!isEmpty);
    m_pages->setVisible(!isEmpty);
    canvasLayout->itemAt(1)->spacerItem()->changeSize(0, isEmpty ? 0 : BAR_GAP, QSizePolicy::Minimum, QSizePolicy::Fixed);
    canvasLayout->invalidate();
}

void PDFAppToolMergeCanvas::setDetailsShown(bool isDetailsShown)
{
    m_stack->setCurrentWidget(isDetailsShown ? m_detailsPage.data() : static_cast<QWidget*>(m_pageView.data()));
    {
        const QSignalBlocker blocker(m_viewSwitch);
        m_viewSwitch->setCurrentIndex(isDetailsShown ? 1 : 0);
    }

    // 408:32003: the size of the tiles means nothing in Details
    m_tileSizeButton->setEnabled(m_isEditable && !isDetailsShown);
}

bool PDFAppToolMergeCanvas::isDetailsShown() const
{
    return m_stack->currentWidget() == m_detailsPage;
}

void PDFAppToolMergeCanvas::setHeaderText(const QString& text)
{
    m_header->setText(text);
}

void PDFAppToolMergeCanvas::setTileSize(PDFAppToolPageGrid::TileSize size)
{
    m_tileSize = size;
    const double scale = PDFAppToolPageGrid::getTileScale(size);
    m_delegate->setSlotSize(QSize(int(std::lround(SMALL_SLOT.width() * scale)), int(std::lround(SMALL_SLOT.height() * scale))));
    updatePictureSize();
    m_pageView->setTileDelegate(m_delegate);
}

void PDFAppToolMergeCanvas::setEditable(bool isEditable)
{
    m_isEditable = isEditable;
    m_pageView->setEditable(isEditable);
    m_detailsView->setEditable(isEditable);
    m_viewSwitch->setEnabled(isEditable);
    m_tileSizeButton->setEnabled(isEditable && !isDetailsShown());
}

void PDFAppToolMergeCanvas::retranslate()
{
    const QSignalBlocker blocker(m_viewSwitch);
    const int index = m_viewSwitch->getCurrentIndex();
    // The texts of the segments: the control has no setter of a text, it is built again
    QHBoxLayout* barLayout = qobject_cast<QHBoxLayout*>(m_bar->layout());
    PDFSegmentedControl* viewSwitch = new PDFSegmentedControl(m_bar);
    viewSwitch->setObjectName(QStringLiteral("mergeViewSwitch"));
    viewSwitch->addSegment(tr("Thumbnails"));
    viewSwitch->addSegment(tr("Details"));
    viewSwitch->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    viewSwitch->setCurrentIndex(index);
    viewSwitch->setEnabled(m_viewSwitch->isEnabled());
    connect(viewSwitch, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppToolMergeCanvas::onViewSwitchChanged);
    // The layout gives back the item of the old control (owned by the caller)
    delete barLayout->replaceWidget(m_viewSwitch, viewSwitch);
    m_viewSwitch->hide();
    m_viewSwitch->deleteLater();
    m_viewSwitch = viewSwitch;

    m_detailsNote->setText(tr("The same pages as a list: one row per page of the result, in the order of the result. The selection is shared with the thumbnails; rows can be dragged, rotated and removed the same way."));
    m_pageView->setAccessibleName(tr("Pages of the result"));
    m_detailsView->setAccessibleName(tr("Pages of the result"));
}

void PDFAppToolMergeCanvas::onViewSwitchChanged(int index)
{
    setDetailsShown(index == 1);
    Q_EMIT viewSwitched(index == 1);
}

void PDFAppToolMergeCanvas::updatePictureSize()
{
    m_model->setPictureSize(m_delegate->getSlotSize(), devicePixelRatioF());
}

void PDFAppToolMergeCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas 378:1082: bg/track
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

void PDFAppToolMergeCanvas::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        retranslate();
    }
    if (event->type() == QEvent::DevicePixelRatioChange)
    {
        updatePictureSize();
    }
}

}   // namespace pdf

#include "moc_pdfapptoolmergeview.cpp"
