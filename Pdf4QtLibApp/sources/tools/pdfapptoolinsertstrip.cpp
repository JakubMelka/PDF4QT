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

#include "pdfapptoolinsertstrip.h"

#include "pdfappdocumentview.h"
#include "pdfappfiledrop.h"
#include "pdfapplocale.h"

#include "pdfcatalog.h"
#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdfthememanager.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <limits>

namespace pdf
{

PDFAppInsertPositionStrip::PDFAppInsertPositionStrip(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("insertPositionStrip"));
    setAccessibleName(tr("Insert position"));
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSelectionMode(QAbstractItemView::NoSelection);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->setAutoFillBackground(false);
    setAutoFillBackground(false);

    // The model and the scheduler are children of the strip: they die with it
    m_model = new PDFAppThumbnailsModel(this);
    setModel(m_model);
    m_scheduler = new PDFAppThumbnailScheduler(m_model, this, this);
    updatePictureSize();
    layoutItems();
}

PDFAppInsertPositionStrip::~PDFAppInsertPositionStrip() = default;

void PDFAppInsertPositionStrip::setDocument(PDFDocumentPointer document, const QString& documentId, PDFAppDocumentView* view)
{
    if (!m_model || !m_scheduler)
    {
        return;
    }

    // The scheduler gets the settings of the canvas as values when it renders; the document
    // pointer is shared and immutable
    m_document = document;
    m_scheduler->setProxy(view ? view->getProxy() : nullptr);
    m_scheduler->setAppDocument(view ? view->getDocument() : nullptr);
    m_model->setDocument(std::move(document), documentId);
    m_scheduler->reset();
    layoutItems();
}

void PDFAppInsertPositionStrip::setPlace(int gap, const std::vector<PDFInteger>& replacedPages)
{
    const int pageCount = getPageCount();
    m_replacedPages = replacedPages;
    m_gap = m_replacedPages.empty() ? qBound(0, gap, pageCount) : -1;
    layoutItems();
}

int PDFAppInsertPositionStrip::getPageCount() const
{
    const PDFCatalog* catalog = m_document ? m_document->getCatalog() : nullptr;
    return catalog ? int(qMin<size_t>(catalog->getPageCount(), size_t(std::numeric_limits<int>::max() - 1))) : 0;
}

std::pair<int, int> PDFAppInsertPositionStrip::getWindow() const
{
    const int pageCount = getPageCount();
    if (pageCount <= 0)
    {
        return { -1, -1 };
    }

    // 388:15601: three pages in front of the place and three after it; at an end of the document
    // the other side has more of them. Replace (388:17167) centers the first replaced page.
    const int place = m_replacedPages.empty() ? m_gap : int(qBound(PDFInteger(0), m_replacedPages.front(), PDFInteger(pageCount - 1)));
    const int windowSize = PAGES_BEFORE + PAGES_AFTER;
    const int first = qBound(0, place - PAGES_BEFORE, qMax(0, pageCount - windowSize));
    const int last = qMin(pageCount, first + windowSize) - 1;
    return { first, last };
}

int PDFAppInsertPositionStrip::getShownGap() const
{
    return m_dropGap >= 0 ? m_dropGap : m_gap;
}

QRect PDFAppInsertPositionStrip::toVisual(const QRect& logical) const
{
    if (layoutDirection() != Qt::RightToLeft)
    {
        return logical;
    }
    const int width = viewport()->width();
    return QRect(width - logical.right() - 1, logical.top(), logical.width(), logical.height());
}

void PDFAppInsertPositionStrip::layoutItems()
{
    m_items.clear();

    const std::pair<int, int> window = getWindow();
    const int shownGap = m_replacedPages.empty() ? getShownGap() : -1;
    bool hasCaption = false;
    for (const PDFInteger page : m_replacedPages)
    {
        hasCaption = hasCaption || (page >= window.first && page <= window.second);
    }
    const int rowHeight = TILE_HEIGHT + (hasCaption ? CAPTION_HEIGHT : 0);
    const int width = qMax(viewport()->width(), TILE_WIDTH);

    // The flow of the window: the line takes its place in front of the page of its gap
    std::vector<int> flow;
    for (int page = window.first; window.first >= 0 && page <= window.second; ++page)
    {
        if (page == shownGap)
        {
            flow.push_back(-1);
        }
        flow.push_back(page);
    }
    if (shownGap >= 0 && (window.first < 0 || shownGap == window.second + 1))
    {
        flow.push_back(-1);
    }

    int x = 0;
    int y = 0;
    for (const int pageIndex : flow)
    {
        const QSize size = pageIndex < 0 ? QSize(MARKER_WIDTH, MARKER_HEIGHT) : QSize(TILE_WIDTH, rowHeight);
        if (x > 0 && x + size.width() > width)
        {
            x = 0;
            y += rowHeight + GAP;
        }
        m_items.push_back({ pageIndex, toVisual(QRect(QPoint(x, y), size)) });
        x += size.width() + GAP;
    }

    // The strip is as high as its rows: the layout of the panel takes the height of sizeHint
    // (the vertical policy is Fixed); no resize inside of a resize of the scroll area
    const int contentHeight = m_items.empty() ? 0 : y + rowHeight;
    if (contentHeight != m_contentHeight)
    {
        m_contentHeight = contentHeight;
        updateGeometry();
    }
    viewport()->update();
}

QRect PDFAppInsertPositionStrip::getLineRect() const
{
    for (const Item& item : m_items)
    {
        if (item.pageIndex < 0)
        {
            return item.rect;
        }
    }
    return QRect();
}

QRect PDFAppInsertPositionStrip::getTileRect(int pageIndex) const
{
    for (const Item& item : m_items)
    {
        if (item.pageIndex == pageIndex && pageIndex >= 0)
        {
            return item.rect;
        }
    }
    return QRect();
}

int PDFAppInsertPositionStrip::getGapAt(const QPoint& position) const
{
    // The tile of the row nearest to the position decides by its half; the line keeps its gap
    const Item* nearest = nullptr;
    int nearestDistance = std::numeric_limits<int>::max();
    for (const Item& item : m_items)
    {
        if (position.y() < item.rect.top() || position.y() > item.rect.top() + TILE_HEIGHT + CAPTION_HEIGHT + GAP)
        {
            continue;
        }
        if (item.pageIndex < 0 && item.rect.adjusted(-GAP, 0, GAP, 0).contains(position.x(), item.rect.top()) && position.y() <= item.rect.bottom() + GAP)
        {
            return getShownGap();
        }
        if (item.pageIndex < 0)
        {
            continue;
        }

        const int distance = position.x() < item.rect.left() ? item.rect.left() - position.x() : (position.x() > item.rect.right() ? position.x() - item.rect.right() : 0);
        if (distance < nearestDistance)
        {
            nearestDistance = distance;
            nearest = &item;
        }
    }
    if (!nearest || nearestDistance > TILE_WIDTH)
    {
        return -1;
    }

    // The leading half of the tile is the gap in front of the page (the right half for right-to-left)
    const bool isLeftHalf = position.x() < nearest->rect.center().x();
    const bool isLeadingHalf = layoutDirection() == Qt::RightToLeft ? !isLeftHalf : isLeftHalf;
    return isLeadingHalf ? nearest->pageIndex : nearest->pageIndex + 1;
}

QRect PDFAppInsertPositionStrip::visualRect(const QModelIndex& index) const
{
    return index.isValid() ? getTileRect(index.row()) : QRect();
}

void PDFAppInsertPositionStrip::scrollTo(const QModelIndex& index, ScrollHint hint)
{
    // The strip shows its window, it does not scroll
    Q_UNUSED(index);
    Q_UNUSED(hint);
}

QModelIndex PDFAppInsertPositionStrip::indexAt(const QPoint& point) const
{
    for (const Item& item : m_items)
    {
        if (item.pageIndex >= 0 && item.rect.contains(point) && m_model)
        {
            return m_model->index(item.pageIndex);
        }
    }
    return QModelIndex();
}

QSize PDFAppInsertPositionStrip::sizeHint() const
{
    return QSize(3 * TILE_WIDTH + 2 * GAP, m_contentHeight);
}

QSize PDFAppInsertPositionStrip::minimumSizeHint() const
{
    return QSize(TILE_WIDTH, m_contentHeight);
}

QModelIndex PDFAppInsertPositionStrip::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    // The keys move the line (keyPressEvent), not a current item
    Q_UNUSED(cursorAction);
    Q_UNUSED(modifiers);
    return QModelIndex();
}

int PDFAppInsertPositionStrip::horizontalOffset() const
{
    return 0;
}

int PDFAppInsertPositionStrip::verticalOffset() const
{
    return 0;
}

bool PDFAppInsertPositionStrip::isIndexHidden(const QModelIndex& index) const
{
    return !index.isValid() || getTileRect(index.row()).isEmpty();
}

void PDFAppInsertPositionStrip::setSelection(const QRect& rect, QItemSelectionModel::SelectionFlags command)
{
    // No selection: a click places the line
    Q_UNUSED(rect);
    Q_UNUSED(command);
}

QRegion PDFAppInsertPositionStrip::visualRegionForSelection(const QItemSelection& selection) const
{
    Q_UNUSED(selection);
    return QRegion();
}

void PDFAppInsertPositionStrip::updateGeometries()
{
    BaseClass::updateGeometries();
}

void PDFAppInsertPositionStrip::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, true);
    const PDFComponentPainter::State state(isEnabled() ? QStyle::State_Enabled : QStyle::State_None, layoutDirection());
    const PDFCatalog* catalog = m_document ? m_document->getCatalog() : nullptr;

    for (const Item& item : m_items)
    {
        if (item.pageIndex < 0)
        {
            // insert-marker 388:15670: the line 3 wide, 3.5 from the leading edge of its place
            const QColor color = PDFThemeManager::getInstance()->getColor(isEnabled() ? PDFThemeColors::BgAccent : PDFThemeColors::BorderStrong);
            const double left = layoutDirection() == Qt::RightToLeft ? item.rect.right() + 1 - LINE_OFFSET - LINE_WIDTH : item.rect.left() + LINE_OFFSET;
            painter.fillRect(QRectF(left, item.rect.top(), LINE_WIDTH, MARKER_HEIGHT), color);
            continue;
        }

        // The tile of the page (as the grids of the tools): the picture of the thumbnails, the
        // number of the page; a replaced page dimmed with its caption and its badge (388:17167)
        PDFComponentPainter::PageTile tile;
        tile.slotSize = SLOT_SIZE;
        tile.label = PDFAppLocale::formatNumber(qint64(item.pageIndex) + 1);
        if (m_model)
        {
            tile.pixmap = m_model->index(item.pageIndex).data(PDFComponentRoles::PixmapRole).value<QPixmap>();
        }
        const PDFPage* page = catalog && size_t(item.pageIndex) < catalog->getPageCount() ? catalog->getPage(size_t(item.pageIndex)) : nullptr;
        if (page)
        {
            tile.pageSize = page->getRotatedCropBox().size();
        }
        if (std::binary_search(m_replacedPages.cbegin(), m_replacedPages.cend(), PDFInteger(item.pageIndex)))
        {
            tile.tileState = PDFComponentPainter::PageTileState::Marked;
            tile.caption = tr("replaced");
            tile.badgeIconName = QStringLiteral("replace");
        }
        PDFComponentPainter::paintPageTile(&painter, QRectF(item.rect), tile, state);
    }
}

void PDFAppInsertPositionStrip::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    layoutItems();
}

void PDFAppInsertPositionStrip::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        setFocus(Qt::MouseFocusReason);
        m_pressedGap = getGapAt(event->position().toPoint());
        event->accept();
        return;
    }
    BaseClass::mousePressEvent(event);
}

void PDFAppInsertPositionStrip::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        BaseClass::mouseReleaseEvent(event);
        return;
    }

    // A press and a release at one gap is a click; the line of the gap of now does nothing
    const int gap = getGapAt(event->position().toPoint());
    const bool isClick = gap >= 0 && gap == m_pressedGap && (gap != m_gap || !m_replacedPages.empty());
    m_pressedGap = -1;
    event->accept();
    if (isClick)
    {
        Q_EMIT gapChosen(gap);
    }
}

void PDFAppInsertPositionStrip::mouseMoveEvent(QMouseEvent* event)
{
    // No drag of the pages, no rubber band
    event->accept();
}

void PDFAppInsertPositionStrip::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

void PDFAppInsertPositionStrip::keyPressEvent(QKeyEvent* event)
{
    const int pageCount = getPageCount();
    const int current = m_replacedPages.empty() ? m_gap : int(m_replacedPages.front());
    int gap = -1;
    switch (event->key())
    {
        case Qt::Key_Left:
        case Qt::Key_Right:
        {
            // The flow runs from the right edge for right-to-left: Left moves forward there
            const bool isForward = (event->key() == Qt::Key_Right) != (layoutDirection() == Qt::RightToLeft);
            gap = qBound(0, current + (isForward ? 1 : -1), pageCount);
            break;
        }

        case Qt::Key_Home:
            gap = 0;
            break;

        case Qt::Key_End:
            gap = pageCount;
            break;

        default:
            BaseClass::keyPressEvent(event);
            return;
    }

    event->accept();
    if (pageCount > 0 && (gap != m_gap || !m_replacedPages.empty()))
    {
        Q_EMIT gapChosen(gap);
    }
}

void PDFAppInsertPositionStrip::dragEnterEvent(QDragEnterEvent* event)
{
    if (!isEnabled() || !PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
    m_dropGap = getGapAt(event->position().toPoint());
    layoutItems();
}

void PDFAppInsertPositionStrip::dragMoveEvent(QDragMoveEvent* event)
{
    if (!isEnabled() || !PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
    const int gap = getGapAt(event->position().toPoint());
    if (gap >= 0 && gap != m_dropGap)
    {
        m_dropGap = gap;
        layoutItems();
    }
}

void PDFAppInsertPositionStrip::dragLeaveEvent(QDragLeaveEvent* event)
{
    event->accept();
    m_dropGap = -1;
    layoutItems();
}

void PDFAppInsertPositionStrip::dropEvent(QDropEvent* event)
{
    if (!isEnabled() || !PDFAppFileDrop::hasLocalFiles(event->mimeData()))
    {
        event->ignore();
        return;
    }

    // The data of the drop are read now (PDF files and images, K8 of step 2); the line shows the
    // place again, the GUI adds the files there - the last statement (a slot may close the tab)
    const int droppedGap = m_dropGap >= 0 ? m_dropGap : getGapAt(event->position().toPoint());
    const int gap = droppedGap >= 0 ? droppedGap : (m_replacedPages.empty() ? m_gap : int(m_replacedPages.front()));
    const PDFAppFileDrop::Result result = PDFAppFileDrop::getFiles(event->mimeData(), PDFAppFileDrop::MAXIMUM_FILES, true);
    QStringList refused;
    for (const PDFAppFileDrop::RefusedItem& item : result.refused)
    {
        refused << tr("%1 – %2").arg(item.text, PDFAppFileDrop::getRefusalText(item.refusal));
    }
    event->acceptProposedAction();
    m_dropGap = -1;
    layoutItems();
    Q_EMIT filesDropped(gap, result.files, refused);
}

void PDFAppInsertPositionStrip::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    switch (event->type())
    {
        case QEvent::LayoutDirectionChange:
        case QEvent::EnabledChange:
            layoutItems();
            break;

        case QEvent::DevicePixelRatioChange:
            updatePictureSize();
            break;

        default:
            break;
    }
}

void PDFAppInsertPositionStrip::updatePictureSize()
{
    if (m_model)
    {
        m_model->setPictureSize(SLOT_SIZE, devicePixelRatioF());
    }
}

}   // namespace pdf

#include "moc_pdfapptoolinsertstrip.cpp"
