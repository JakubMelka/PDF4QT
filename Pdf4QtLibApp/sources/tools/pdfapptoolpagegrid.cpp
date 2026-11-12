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

#include "pdfapptoolpagegrid.h"

#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolmergeview.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcatalog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfcomponentrows.h"
#include "pdfpainterutils.h"
#include "pdfstylepainter.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmapCache>
#include <QScrollBar>
#include <QTransform>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the grid of the pages
class PDFAppToolPageGridHelper
{
public:
    PDFAppToolPageGridHelper() = delete;

    /// Number of quarter turns of a rotation
    static int getQuarterTurns(PageRotation rotation)
    {
        return int(rotation);
    }

    /// The row lies before the group (the order of std::upper_bound over the groups)
    static bool isBeforeGroup(int row, const PDFAppToolGroupedPageModel::Group& group)
    {
        return row < group.firstRow;
    }
};

// --------------------------------------------------------- PDFAppToolPageGridModel

PDFAppToolPageGridModel::PDFAppToolPageGridModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolPageGridModel::~PDFAppToolPageGridModel() = default;

void PDFAppToolPageGridModel::setPageDocument(PDFDocumentPointer document, const QString& documentId)
{
    if (m_rowPages.has_value() || !m_groups.empty())
    {
        // The rows are every page of the old document first (one reset), the base resets them,
        // when the new document has another number of pages
        beginResetModel();
        m_rowPages.reset();
        m_pageRows.clear();
        m_groups.clear();
        endResetModel();
    }

    m_pageSizes.clear();
    if (document)
    {
        const PDFCatalog* catalog = document->getCatalog();
        const size_t pageCount = qMin<size_t>(catalog->getPageCount(), size_t(std::numeric_limits<int>::max()));
        m_pageSizes.reserve(pageCount);
        for (size_t i = 0; i < pageCount; ++i)
        {
            m_pageSizes.push_back(catalog->getPage(i)->getRotatedCropBox().size());
        }
    }
    m_tiles.resize(m_pageSizes.size());
    m_pageDocument = document;
    setDocument(std::move(document), documentId);
}

std::pair<int, int> PDFAppToolPageGridModel::setTiles(std::vector<Tile> tiles)
{
    tiles.resize(m_pageSizes.size());
    int first = -1;
    int last = -1;
    for (size_t i = 0; i < tiles.size(); ++i)
    {
        const int row = getRowOfPage(int(i));
        if (row >= 0 && !(tiles[i] == m_tiles[i]))
        {
            first = first < 0 ? row : qMin(first, row);
            last = qMax(last, row);
        }
    }

    m_tiles = std::move(tiles);
    if (first >= 0)
    {
        // One change of the range: each emission of a tile would queue a layout of all tiles
        Q_EMIT dataChanged(index(first), index(last));
    }
    return { first, last };
}

PDFAppToolPageGridModel::Tile PDFAppToolPageGridModel::getTile(int pageIndex) const
{
    return pageIndex >= 0 && size_t(pageIndex) < m_tiles.size() ? m_tiles[size_t(pageIndex)] : Tile();
}

void PDFAppToolPageGridModel::setRows(std::optional<std::vector<int>> pages, std::vector<Group> groups)
{
    // The valid pages, each once, in their order
    std::vector<int> pageRows(m_pageSizes.size(), -1);
    if (pages.has_value())
    {
        std::vector<int> validPages;
        validPages.reserve(pages->size());
        for (const int page : *pages)
        {
            if (page >= 0 && size_t(page) < pageRows.size() && pageRows[size_t(page)] < 0)
            {
                pageRows[size_t(page)] = int(validPages.size());
                validPages.push_back(page);
            }
        }
        pages = std::move(validPages);
    }

    // The groups take their rows in their order (the last one ends at the last row)
    const int rowCount = pages.has_value() ? int(pages->size()) : int(m_pageSizes.size());
    int firstRow = 0;
    for (Group& group : groups)
    {
        group.firstRow = qMin(firstRow, rowCount);
        group.rowCount = qBound(0, group.rowCount, rowCount - group.firstRow);
        firstRow = group.firstRow + group.rowCount;
    }

    const bool isRowsChanged = pages != m_rowPages;
    const bool isGroupsChanged = groups != m_groups;
    if (isRowsChanged)
    {
        beginResetModel();
        m_rowPages = std::move(pages);
        m_pageRows = m_rowPages.has_value() ? std::move(pageRows) : std::vector<int>();
        m_groups = std::move(groups);
        endResetModel();
    }
    else
    {
        m_groups = std::move(groups);
    }

    // The views lay out the groups again (the last thing: a slot may delete the model)
    if (isRowsChanged || isGroupsChanged)
    {
        Q_EMIT groupsChanged();
    }
}

int PDFAppToolPageGridModel::getPageOfRow(int row) const
{
    if (row < 0 || row >= rowCount())
    {
        return -1;
    }
    return m_rowPages.has_value() ? (*m_rowPages)[size_t(row)] : row;
}

int PDFAppToolPageGridModel::getRowOfPage(int page) const
{
    if (page < 0 || size_t(page) >= m_pageSizes.size())
    {
        return -1;
    }
    return m_rowPages.has_value() ? (size_t(page) < m_pageRows.size() ? m_pageRows[size_t(page)] : -1) : page;
}

int PDFAppToolPageGridModel::getGroupOfRow(int row) const
{
    // The groups are sorted by their first rows
    auto it = std::upper_bound(m_groups.cbegin(), m_groups.cend(), row, PDFAppToolPageGridHelper::isBeforeGroup);
    while (it != m_groups.cbegin())
    {
        --it;
        if (row >= it->firstRow && row < it->firstRow + it->rowCount)
        {
            return int(it - m_groups.cbegin());
        }
        if (it->rowCount > 0)
        {
            break;
        }
    }
    return -1;
}

bool PDFAppToolPageGridModel::isRowSelected(int row) const
{
    return getTile(getPageOfRow(row)).state == PDFComponentPainter::PageTileState::Selected;
}

QString PDFAppToolPageGridModel::getKey(int row) const
{
    // The picture of the page of the row: a page has one picture in every row
    return BaseClass::getKey(getPageOfRow(row));
}

PDFAppThumbnailSource PDFAppToolPageGridModel::getPictureSource(int row) const
{
    return BaseClass::getPictureSource(getPageOfRow(row));
}

int PDFAppToolPageGridModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return m_rowPages.has_value() ? int(m_rowPages->size()) : BaseClass::rowCount(parent);
}

QVariant PDFAppToolPageGridModel::data(const QModelIndex& index, int role) const
{
    const int page = index.isValid() ? getPageOfRow(index.row()) : -1;
    if (page < 0 || size_t(page) >= m_tiles.size())
    {
        return QVariant();
    }

    const Tile& tile = m_tiles[size_t(page)];
    const PDFCatalog* catalog = m_pageDocument ? m_pageDocument->getCatalog() : nullptr;
    switch (role)
    {
        case Qt::DisplayRole:
            return !tile.label.isEmpty() ? tile.label : PDFAppPanelWidgets::getPageLabel(catalog, page);

        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
            return PDFAppPanelWidgets::getPageTitle(catalog, page);

        case PDFComponentRoles::PageTileStateRole:
            return int(tile.state);

        case PDFComponentRoles::SubtitleRole:
            return tile.caption;

        case PDFComponentRoles::StateIconRole:
            return tile.badgeIconName;

        case PDFComponentRoles::HueRole:
            return tile.dotColor.has_value() ? QVariant(int(*tile.dotColor)) : QVariant();

        case PDFAppToolPageTileDelegate::DimmedRole:
            return tile.isDimmed;

        case PDFAppToolPageTileDelegate::CutBeforeRole:
            return tile.markerBefore;

        case PDFAppToolPageTileDelegate::PaperBorderRole:
            // The pages on the white cards of the groups have the border of their paper (383:3315
            // paper: border/strong), a blank page stays visible
            return !m_groups.empty();

        case Qt::AccessibleDescriptionRole:
            return tile.caption;

        case PDFComponentRoles::PageSizeRole:
        {
            // The page as it will be saved: a quarter turn swaps its sides
            const QSizeF size = m_pageSizes[size_t(page)];
            return PDFAppToolPageGridHelper::getQuarterTurns(tile.extraRotation) % 2 == 1 ? size.transposed() : size;
        }

        case PDFComponentRoles::PixmapRole:
        case Qt::DecorationRole:
            return getTurnedPicture(BaseClass::data(index, PDFComponentRoles::PixmapRole), getKey(index.row()), tile.extraRotation);

        default:
            return BaseClass::data(index, role);
    }
}

QVariant PDFAppToolPageGridModel::getTurnedPicture(const QVariant& picture, const QString& key, PageRotation turn)
{
    const int quarterTurns = PDFAppToolPageGridHelper::getQuarterTurns(turn);
    if (quarterTurns == 0 || picture.typeId() != QMetaType::QPixmap)
    {
        return picture;
    }

    // The picture of the thumbnails turned by the tool, kept by its own key
    const QString turnedKey = key + QStringLiteral("/turn%1").arg(quarterTurns);
    QPixmap turned;
    if (!QPixmapCache::find(turnedKey, &turned))
    {
        const QPixmap pixmap = picture.value<QPixmap>();
        turned = pixmap.transformed(QTransform().rotate(90.0 * quarterTurns));
        turned.setDevicePixelRatio(pixmap.devicePixelRatio());
        QPixmapCache::insert(turnedKey, turned);
    }
    return turned;
}

// ---------------------------------------------------------- PDFAppToolPageGridView

PDFAppToolPageGridView::PDFAppToolPageGridView(QWidget* parent) :
    BaseClass(parent)
{
    // 389:20910: tiles of different sizes in rows, the gap 6 (IconMode puts the spacing
    // between the tiles and in front of the first one)
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(false);
    setSpacing(PDFAppToolPageGrid::TILE_SPACING);
    setSelectionMode(QAbstractItemView::NoSelection);
    setDragEnabled(false);
    setAcceptDrops(false);
    setFocusPolicy(Qt::StrongFocus);

    // The tiles start at the spacing of the view: no margins of PDFListView around them
    setViewportMargins(0, 0, 0, 0);
}

PDFAppToolPageGridView::~PDFAppToolPageGridView() = default;

int PDFAppToolPageGridView::getContentHeight() const
{
    return contentsSize().height();
}

void PDFAppToolPageGridView::updateGeometries()
{
    BaseClass::updateGeometries();

    // The layout of the tiles is done: the page of the tool takes the new height of its content.
    // The height is announced from the event loop - Qt uses the view after updateGeometries
    // (its layout, its resize, its timer), a slot may delete the view.
    if (getContentHeight() != m_lastContentHeight && !m_isContentHeightQueued)
    {
        m_isContentHeightQueued = true;
        QMetaObject::invokeMethod(this, &PDFAppToolPageGridView::emitContentHeight, Qt::QueuedConnection);
    }
}

void PDFAppToolPageGridView::emitContentHeight()
{
    m_isContentHeightQueued = false;
    const int height = getContentHeight();
    if (height != m_lastContentHeight)
    {
        // The last thing: a slot may delete the view
        m_lastContentHeight = height;
        Q_EMIT contentHeightChanged(height);
    }
}

void PDFAppToolPageGridView::setGapClicks(bool isGapClicks)
{
    m_isGapClicks = isGapClicks;
    setMouseTracking(isGapClicks);
    viewport()->setMouseTracking(isGapClicks);
    if (!isGapClicks)
    {
        viewport()->unsetCursor();
    }
}

int PDFAppToolPageGridView::getGapAt(const QPoint& point) const
{
    const QAbstractItemModel* itemModel = model();
    const int count = itemModel ? itemModel->rowCount() : 0;
    if (count < 2)
    {
        return -1;
    }

    // The flow goes from the leading edge (right to left: from the right edge); the gap of a
    // tile is in front of its leading edge (with the mark of its cut), the gap after it behind
    // its trailing edge
    const bool isRightToLeft = layoutDirection() == Qt::RightToLeft;
    const int direction = isRightToLeft ? -1 : 1;
    QModelIndex index = indexAt(point);
    if (!index.isValid())
    {
        // In the gap between two tiles: the tile behind the gap, else the tile in front of it
        const int reach = spacing() + 1;
        const QModelIndex next = indexAt(point + QPoint(direction * reach, 0));
        if (next.isValid())
        {
            return next.row() > 0 ? next.row() : -1;
        }
        const QModelIndex previous = indexAt(point - QPoint(direction * reach, 0));
        return previous.isValid() && previous.row() + 1 < count ? previous.row() + 1 : -1;
    }

    const QRect rect = visualRect(index);
    const int markWidth = index.data(PDFAppToolPageTileDelegate::CutBeforeRole).toBool() ? PDFAppToolPageTileDelegate::CUT_SLOT : 0;
    const int leading = isRightToLeft ? rect.right() - point.x() : point.x() - rect.left();
    const int trailing = isRightToLeft ? point.x() - rect.left() : rect.right() - point.x();
    if (leading < markWidth + GAP_ZONE)
    {
        return index.row() > 0 ? index.row() : -1;
    }
    if (trailing < GAP_ZONE)
    {
        return index.row() + 1 < count ? index.row() + 1 : -1;
    }
    return -1;
}

void PDFAppToolPageGridView::mousePressEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    const QModelIndex index = indexAt(position);
    m_pressedPage = event->button() == Qt::LeftButton && index.isValid() ? index.row() : -1;
    m_pressedGap = event->button() == Qt::LeftButton && m_isGapClicks ? getGapAt(position) : -1;
    BaseClass::mousePressEvent(event);
}

void PDFAppToolPageGridView::mouseMoveEvent(QMouseEvent* event)
{
    BaseClass::mouseMoveEvent(event);
    if (m_isGapClicks)
    {
        // The pointer over a gap says, that a click places a cut there
        if (getGapAt(event->position().toPoint()) > 0)
        {
            viewport()->setCursor(Qt::SplitHCursor);
        }
        else
        {
            viewport()->unsetCursor();
        }
    }
}

void PDFAppToolPageGridView::mouseReleaseEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    const QModelIndex index = indexAt(position);
    const int pressedPage = m_pressedPage;
    const int pressedGap = m_pressedGap;
    m_pressedPage = -1;
    m_pressedGap = -1;
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const int releasedGap = m_isGapClicks ? getGapAt(position) : -1;
    BaseClass::mouseReleaseEvent(event);

    if (m_isGapClicks)
    {
        // A press and a release in one gap
        if (event->button() == Qt::LeftButton && releasedGap > 0 && releasedGap == pressedGap)
        {
            queueClick(releasedGap, modifiers, false);
        }
        return;
    }

    // A press and a release on one tile
    if (event->button() == Qt::LeftButton && index.isValid() && index.row() == pressedPage)
    {
        queueClick(index.row(), modifiers, false);
    }
}

void PDFAppToolPageGridView::keyPressEvent(QKeyEvent* event)
{
    const QModelIndex current = currentIndex();
    const bool isSpace = event->key() == Qt::Key_Space || event->key() == Qt::Key_Select;
    if (current.isValid() && isSpace && (event->modifiers() & ~(Qt::ShiftModifier | Qt::ControlModifier | Qt::KeypadModifier)) == Qt::NoModifier)
    {
        queueClick(current.row(), event->modifiers(), false);
        event->accept();
        return;
    }
    if (current.isValid() && event->key() == Qt::Key_Delete && event->modifiers() == Qt::NoModifier)
    {
        queueClick(current.row(), Qt::NoModifier, true);
        event->accept();
        return;
    }
    BaseClass::keyPressEvent(event);
}

void PDFAppToolPageGridView::queueClick(int pageIndex, Qt::KeyboardModifiers modifiers, bool isDelete)
{
    const bool isQueued = m_queuedPage >= 0;
    m_queuedPage = pageIndex;
    m_queuedModifiers = modifiers;
    m_isQueuedDelete = isDelete;
    if (!isQueued)
    {
        QMetaObject::invokeMethod(this, &PDFAppToolPageGridView::emitQueued, Qt::QueuedConnection);
    }
}

void PDFAppToolPageGridView::emitQueued()
{
    const int page = m_queuedPage;
    const bool isDelete = m_isQueuedDelete;
    m_queuedPage = -1;
    if (page < 0 || !model() || page >= model()->rowCount())
    {
        return;
    }

    // The last thing: a slot may delete the view
    if (isDelete)
    {
        Q_EMIT deleteRequested(page);
    }
    else
    {
        Q_EMIT pageClicked(page, m_queuedModifiers);
    }
}

// -------------------------------------------------------------- PDFAppToolPageGrid

double PDFAppToolPageGrid::getTileScale(TileSize size)
{
    switch (size)
    {
        case TileSize::Small:
            break;
        case TileSize::Medium:
            return 1.5;
        case TileSize::Large:
            return 2.0;
    }
    return 1.0;
}

QString PDFAppToolPageGrid::getTileSizeId(TileSize size)
{
    switch (size)
    {
        case TileSize::Small:
            return QStringLiteral("small");
        case TileSize::Large:
            return QStringLiteral("large");
        case TileSize::Medium:
            break;
    }
    return QStringLiteral("medium");
}

PDFAppToolPageGrid::TileSize PDFAppToolPageGrid::getTileSizeFromId(const QString& id)
{
    for (const TileSize size : { TileSize::Small, TileSize::Medium, TileSize::Large })
    {
        if (getTileSizeId(size) == id)
        {
            return size;
        }
    }
    return TileSize::Medium;
}

PDFAppToolPageGrid::PDFAppToolPageGrid(const QSize& slotSize, QWidget* parent) :
    BaseClass(parent),
    m_baseSlotSize(slotSize)
{
    setObjectName(QStringLiteral("toolPageGrid"));

    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(0);

    // preview-bar 389:20436: the text Semi Bold 13 text/primary 10 below the top of the bar
    m_header = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, this);
    m_header->setObjectName(QStringLiteral("toolPageGridHeader"));
    m_header->setFixedHeight(BAR_HEIGHT);
    m_header->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);

    m_view = new PDFAppToolPageGridView(this);
    m_view->setObjectName(QStringLiteral("toolPageGridView"));
    PDFAppPanelWidgets::prepareView(m_view);
    m_view->setAccessibleName(tr("Pages"));
    m_model = new PDFAppToolPageGridModel(m_view);
    m_delegate = new PDFAppToolPageTileDelegate(m_view);
    m_delegate->setSlotSize(getScaledSlotSize(m_tileSize));
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);

    // The cards of the groups (383:3315): the grouped view of the same model, its tiles on the
    // cards (bg/surface below the veil of a muted page); shown in the place of the grid
    m_groupPage = new QWidget(this);
    m_groupPage->setObjectName(QStringLiteral("toolPageGridGroups"));
    QVBoxLayout* groupLayout = new QVBoxLayout(m_groupPage);
    groupLayout->setContentsMargins(0, 0, 0, 0);
    m_groupView = new PDFAppToolMergePageView(m_groupPage);
    m_groupView->setObjectName(QStringLiteral("toolPageGridGroupView"));
    m_groupView->setAccessibleName(tr("Pages"));
    m_groupView->setEditable(false);
    m_groupView->setMaximumHeight(1);
    PDFAppToolPageTileDelegate* groupDelegate = new PDFAppToolPageTileDelegate(m_groupView);
    groupDelegate->setBackgroundColor(PDFThemeColors::BgSurface);
    groupDelegate->setSlotSize(getScaledSlotSize(m_tileSize));
    m_groupDelegate = groupDelegate;
    m_groupView->setModel(m_model);
    m_groupView->setTileDelegate(m_groupDelegate);
    groupLayout->addWidget(m_groupView, 1);
    groupLayout->addStretch(0);
    m_groupPage->hide();

    // The hint 389:20890: Regular 11 text/tertiary in lines of 13
    m_hint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, this);
    m_hint->setObjectName(QStringLiteral("toolPageGridHint"));

    connect(m_view, &PDFAppToolPageGridView::pageClicked, this, &PDFAppToolPageGrid::onViewPageClicked);
    connect(m_view, &PDFAppToolPageGridView::deleteRequested, this, &PDFAppToolPageGrid::onViewDeleteRequested);
    // The views emit the heights from the event loop, after their layouts
    connect(m_view, &PDFAppToolPageGridView::contentHeightChanged, this, &PDFAppToolPageGrid::onContentHeightChanged);
    connect(m_groupView.data(), &PDFAppToolMergePageView::contentHeightChanged, this, &PDFAppToolPageGrid::onGroupContentHeightChanged);

    updateLayout();
    updateScheduler();
    updatePictureSize();
}

PDFAppToolPageGrid::~PDFAppToolPageGrid() = default;

PDFAppToolMergePageView* PDFAppToolPageGrid::getGroupView() const
{
    return m_groupView.data();
}

void PDFAppToolPageGrid::updateLayout()
{
    // The order of the stage: the bar, then the hint above the tiles (383:5715) or the tiles
    // and the hint below them (389:20890, 385:9405); the spacing of the grid lies around its
    // tiles, so the margins and the gaps lose it (the texts get it back as their padding)
    while (QLayoutItem* item = m_layout->takeAt(0))
    {
        // The widgets stay children of the grid, the spacers die
        delete item;
    }

    const int spacing = m_tileSpacing;
    m_layout->setContentsMargins(PADDING - spacing, PADDING, PADDING - spacing, PADDING);
    m_header->setContentsMargins(spacing, 0, spacing, 0);
    m_hint->setContentsMargins(spacing, 0, spacing, 0);
    m_groupPage->layout()->setContentsMargins(spacing, spacing, spacing, 0);

    m_layout->addWidget(m_header);
    if (m_isHintAbove)
    {
        m_layout->addSpacing(BAR_GAP);
        m_layout->addWidget(m_hint);
        m_layout->addSpacing(m_hintGap - spacing);
        m_layout->addWidget(m_view, 1);
        m_layout->addWidget(m_groupPage, 1);
    }
    else
    {
        m_layout->addSpacing(BAR_GAP - spacing);
        m_layout->addWidget(m_view, 1);
        m_layout->addWidget(m_groupPage, 1);
        m_layout->addSpacing(m_hintGap - spacing);
        m_layout->addWidget(m_hint);
    }
    m_layout->addStretch(0);
    m_layout->invalidate();
}

void PDFAppToolPageGrid::updateScheduler()
{
    // The pictures of the shown view: a scheduler asks only for the rows visible in its view
    QAbstractItemView* view = m_isGrouped ? static_cast<QAbstractItemView*>(m_groupView.data()) : static_cast<QAbstractItemView*>(m_view.data());
    if (m_scheduler)
    {
        // Nobody waits for its workers; it is the sender of nothing, which runs now
        delete m_scheduler.data();
    }
    m_scheduler = new PDFAppThumbnailScheduler(m_model, view, this);
    m_scheduler->setProxy(m_proxy);
    m_scheduler->setAppDocument(m_appDocument);
}

void PDFAppToolPageGrid::setDocument(PDFDocumentPointer document, const QString& documentId, PDFAppDocumentView* view)
{
    if (!m_model || !m_scheduler)
    {
        return;
    }

    m_proxy = view ? view->getProxy() : nullptr;
    m_appDocument = view ? view->getDocument() : nullptr;
    m_scheduler->setProxy(m_proxy);
    m_scheduler->setAppDocument(m_appDocument);
    m_model->setPageDocument(std::move(document), documentId);
    m_scheduler->reset();
}

void PDFAppToolPageGrid::setRows(std::optional<std::vector<int>> pages, std::vector<PDFAppToolGroupedPageModel::Group> groups)
{
    if (!m_model)
    {
        return;
    }

    const bool isGrouped = !groups.empty();
    QPointer<PDFAppToolPageGrid> guard(this);
    m_model->setRows(std::move(pages), std::move(groups));
    if (!guard || isGrouped == m_isGrouped)
    {
        return;
    }

    // The other view is shown, the scheduler draws its pictures
    m_isGrouped = isGrouped;
    m_view->setVisible(!isGrouped);
    m_groupPage->setVisible(isGrouped);
    updateScheduler();
}

void PDFAppToolPageGrid::setTileSpacing(int spacing)
{
    if (m_tileSpacing != spacing && spacing >= 0)
    {
        m_tileSpacing = spacing;
        m_view->setSpacing(spacing);
        updateLayout();
    }
}

void PDFAppToolPageGrid::setHintPlacement(bool isAbove, int gap)
{
    if (m_isHintAbove != isAbove || m_hintGap != gap)
    {
        m_isHintAbove = isAbove;
        m_hintGap = gap;
        updateLayout();
    }
}

void PDFAppToolPageGrid::setGapClicks(bool isGapClicks)
{
    m_view->setGapClicks(isGapClicks);
}

void PDFAppToolPageGrid::scrollToPage(int pageIndex)
{
    const int row = m_model ? m_model->getRowOfPage(pageIndex) : -1;
    if (row < 0)
    {
        return;
    }

    if (m_isGrouped)
    {
        // The card of the group of the page at the top of the view
        const int group = m_model->getGroupOfRow(row);
        if (group >= 0)
        {
            m_groupView->scrollToGroup(group);
        }
    }
    else
    {
        m_view->scrollTo(m_model->index(row), QAbstractItemView::PositionAtTop);
    }
}

void PDFAppToolPageGrid::onViewPageClicked(int row, Qt::KeyboardModifiers modifiers)
{
    // The tool knows the pages, the grid the rows (the last thing: a slot may delete the grid)
    const int page = m_model ? m_model->getPageOfRow(row) : -1;
    if (page >= 0)
    {
        Q_EMIT pageClicked(page, modifiers);
    }
}

void PDFAppToolPageGrid::onViewDeleteRequested(int row)
{
    const int page = m_model ? m_model->getPageOfRow(row) : -1;
    if (page >= 0)
    {
        Q_EMIT deleteRequested(page);
    }
}

void PDFAppToolPageGrid::onGroupContentHeightChanged(int height)
{
    // The cards hug their content (the hint follows them), a longer content scrolls
    if (m_groupView)
    {
        m_groupView->setMaximumHeight(qMax(height, 1));
    }
}

void PDFAppToolPageGrid::setHeaderText(const QString& text)
{
    if (m_header)
    {
        m_header->setText(text);
    }
}

QString PDFAppToolPageGrid::getHeaderText() const
{
    return m_header ? m_header->text() : QString();
}

void PDFAppToolPageGrid::setHintText(const QString& text)
{
    if (m_hint)
    {
        m_hint->setText(text);
    }
}

void PDFAppToolPageGrid::setTiles(std::vector<PDFAppToolPageGridModel::Tile> tiles)
{
    if (!m_model || !m_delegate)
    {
        return;
    }

    // The signal of the model reaches the views only; the grid is held all the same
    QPointer<PDFAppToolPageGrid> guard(this);
    const std::pair<int, int> range = m_model->setTiles(std::move(tiles));
    if (guard && m_delegate && m_model && range.first >= 0)
    {
        // A tile with a caption is higher, a selected one larger: one layout of all tiles
        Q_EMIT m_delegate->sizeHintChanged(m_model->index(range.first));
    }
}

void PDFAppToolPageGrid::setTileSize(TileSize size)
{
    if (m_tileSize == size || !m_delegate || !m_model || !m_view)
    {
        m_tileSize = size;
        return;
    }

    m_tileSize = size;
    m_delegate->setSlotSize(getScaledSlotSize(size));
    if (m_groupDelegate)
    {
        m_groupDelegate->setSlotSize(getScaledSlotSize(size));
    }

    // New keys of the pictures (their size), then one layout of all tiles; the signals reach the views only
    QPointer<PDFAppToolPageGrid> guard(this);
    updatePictureSize();
    if (guard && m_groupView && m_groupDelegate)
    {
        m_groupView->setTileDelegate(m_groupDelegate);
    }
    if (guard && m_delegate && m_model && m_model->rowCount() > 0)
    {
        Q_EMIT m_delegate->sizeHintChanged(m_model->index(0));
    }
}

QSize PDFAppToolPageGrid::getScaledSlotSize(TileSize size) const
{
    const double scale = getTileScale(size);
    return QSize(int(std::lround(m_baseSlotSize.width() * scale)), int(std::lround(m_baseSlotSize.height() * scale)));
}

void PDFAppToolPageGrid::updatePictureSize()
{
    if (m_model && m_delegate && m_view)
    {
        m_model->setPictureSize(m_delegate->getSlotSize(), m_view->devicePixelRatioF());
    }
}

void PDFAppToolPageGrid::onContentHeightChanged(int height)
{
    // The grid is as high as its tiles (the hint follows them), at most the rest of the canvas
    if (m_view)
    {
        m_view->setMaximumHeight(qMax(height + 2 * m_view->frameWidth(), 1));
    }
}

void PDFAppToolPageGrid::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // The canvas of the tool (document-canvas 389:20210: bg/track)
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

void PDFAppToolPageGrid::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::DevicePixelRatioChange)
    {
        updatePictureSize();
    }
    else if (event->type() == QEvent::LanguageChange && m_view)
    {
        m_view->setAccessibleName(tr("Pages"));
    }
}

// ---------------------------------------------------------- PDFAppToolPageTileDelegate

PDFAppToolPageTileDelegate::PDFAppToolPageTileDelegate(QObject* parent) :
    BaseClass(parent)
{

}

QSize PDFAppToolPageTileDelegate::getScaledSlotSize(double scale) const
{
    const QSize slot = getSlotSize();
    if (!(scale > 0.0) || qFuzzyCompare(scale, 1.0))
    {
        return slot;
    }
    return QSize(int(std::lround(slot.width() * scale)), int(std::lround(slot.height() * scale)));
}

QSize PDFAppToolPageTileDelegate::getSquareSlotSize(const QSize& slot)
{
    const int side = qMax(slot.width(), slot.height());
    return QSize(side, side);
}

PDFComponentPainter::PageTile PDFAppToolPageTileDelegate::getTileContent(const QModelIndex& index, bool withPixmap) const
{
    PDFComponentPainter::PageTile content = getContent(index, withPixmap);
    const QVariant scale = index.isValid() ? index.data(SlotScaleRole) : QVariant();
    if (scale.isValid())
    {
        content.slotSize = getScaledSlotSize(scale.toDouble());
    }
    if (index.isValid() && index.data(SquareSlotRole).toBool())
    {
        // 387:12938: the page is fitted into the square box (a landscape page as wide as the box)
        content.slotSize = getSquareSlotSize(content.slotSize.isValid() ? content.slotSize : getSlotSize());
    }
    return content;
}

std::optional<PDFAccentHue> PDFAppToolPageTileDelegate::getDotHue(const QModelIndex& index)
{
    const QVariant hue = index.isValid() ? index.data(PDFComponentRoles::HueRole) : QVariant();
    if (!hue.isValid())
    {
        return std::nullopt;
    }
    return static_cast<PDFAccentHue>(hue.toInt());
}

QSize PDFAppToolPageTileDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    Q_UNUSED(option);
    Q_UNUSED(width);

    // A tile with a cut has the mark of the cut in front of it (383:5715: insert-marker 10 + 4)
    const QSize size = PDFComponentPainter::getPageTileSize(getTileContent(index, false));
    return index.isValid() && index.data(CutBeforeRole).toBool() ? QSize(size.width() + CUT_SLOT, size.height()) : size;
}

QRectF PDFAppToolPageTileDelegate::getTileRect(const QRectF& rect, bool hasCut, bool isRightToLeft)
{
    if (!hasCut)
    {
        return rect;
    }
    return isRightToLeft ? rect.adjusted(0, 0, -CUT_SLOT, 0) : rect.adjusted(CUT_SLOT, 0, 0, 0);
}

PDFAppToolPageTileDelegate::MarkRects PDFAppToolPageTileDelegate::getMarkRects(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    MarkRects rects;
    if (!index.isValid())
    {
        return rects;
    }

    // The leading edge of the tile is mirrored for a right-to-left layout
    const bool isRightToLeft = option.direction == Qt::RightToLeft;
    const bool hasCut = index.data(CutBeforeRole).toBool();
    const QRectF rect = option.rect;
    const QRectF tileRect = getTileRect(rect, hasCut, isRightToLeft);
    const PDFComponentPainter::PageTile content = getTileContent(index, false);
    rects.tile = tileRect;
    rects.page = PDFComponentPainter::getPageTilePageRect(tileRect, content);
    if (rects.page.isEmpty())
    {
        return MarkRects();
    }

    const QSize slotSize = PDFComponentPainter::getPageTileSlotSize(content);
    if (hasCut)
    {
        // 383:5715: the amber line of 3 in the middle of the mark of 10, from the top of the tile
        // as high as the slot of the page
        rects.cut = getMirrored(rect, QRectF(rect.left() + (CUT_MARKER - CUT_WIDTH) / 2.0, rect.top(), CUT_WIDTH, slotSize.height()), isRightToLeft);
    }

    if (getDotHue(index).has_value())
    {
        // 383:5715: the dot in front of the label, which is centered below the slot (the page
        // is centered in its slot)
        const qreal labelTop = rects.page.center().y() + slotSize.height() / 2.0 + LABEL_GAP;
        const QFontMetricsF metrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption));
        const qreal labelWidth = qMin<qreal>(metrics.horizontalAdvance(content.label), tileRect.width());
        const qreal dotLeft = qMax(tileRect.left(), tileRect.center().x() - labelWidth / 2.0 - DOT_GAP - DOT_SIZE);
        const qreal dotRight = tileRect.left() + tileRect.right() - (dotLeft + DOT_SIZE);
        rects.dot = QRectF(isRightToLeft ? dotRight : dotLeft, labelTop + (LABEL_HEIGHT - DOT_SIZE) / 2.0, DOT_SIZE, DOT_SIZE);
    }
    return rects;
}

QRectF PDFAppToolPageTileDelegate::getMirrored(const QRectF& rect, const QRectF& leftToRight, bool isRightToLeft)
{
    return isRightToLeft ? QRectF(rect.left() + rect.right() - leftToRight.right(), leftToRight.top(), leftToRight.width(), leftToRight.height()) : leftToRight;
}

void PDFAppToolPageTileDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    if (!painter)
    {
        return;
    }

    // The page tile with the slot of the row (the base paints the slot of the delegate), behind
    // the mark of a cut
    const bool hasCut = index.data(CutBeforeRole).toBool();
    PDFComponentPainter::paintPageTile(painter, getTileRect(QRectF(option.rect), hasCut, option.direction == Qt::RightToLeft), getTileContent(index), state);

    const bool isDimmed = index.data(DimmedRole).toBool();
    const bool hasPaperBorder = index.data(PaperBorderRole).toBool();
    const std::optional<PDFAccentHue> dotHue = getDotHue(index);
    if (!isDimmed && !hasPaperBorder && !dotHue.has_value() && !hasCut)
    {
        return;
    }

    const MarkRects rects = getMarkRects(option, index);
    if (rects.page.isEmpty())
    {
        return;
    }

    PDFPainterStateGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    const PDFThemeManager* theme = PDFThemeManager::getInstance();
    if (isDimmed)
    {
        // 388:15487, 388:17167: the page not taken is muted - the page and the border of its paper
        // at 35 % over the color below the tiles (the label stays as it is)
        QColor veil = theme->getColor(m_backgroundColor);
        veil.setAlphaF(float(DIMMED_OPACITY));
        painter->fillRect(rects.page, veil);
    }
    if (isDimmed || hasPaperBorder)
    {
        // The border of the paper (388:17078 border/strong; a muted page at 35 %)
        PDFPainterStateGuard borderGuard(painter);
        painter->setOpacity(painter->opacity() * (isDimmed ? 1.0 - DIMMED_OPACITY : 1.0));
        PDFStylePainter::strokeInside(painter, rects.page, PDFStylePainter::Radii(0.0), PDFThemeColors::BorderStrong, 1.0);
    }
    if (!rects.cut.isEmpty())
    {
        painter->setBrush(theme->getColor(PDFAccentHueColors::getColor(PDFAccentHue::Amber, PDFAccentHueColors::Role::BorderStrong)));
        painter->drawRoundedRect(rects.cut, CUT_WIDTH / 2.0, CUT_WIDTH / 2.0);
    }
    if (!rects.dot.isEmpty() && dotHue.has_value())
    {
        painter->setBrush(theme->getColor(PDFAccentHueColors::getColor(*dotHue, PDFAccentHueColors::Role::Icon)));
        painter->drawEllipse(rects.dot);
    }
}

// ---------------------------------------------------------- PDFAppToolTileSizeMenu

PDFAppToolTileSizeMenu::PDFAppToolTileSizeMenu(PDFAppToolPanelPopup* panel, const QString& titleFormat, QObject* parent) :
    BaseClass(parent),
    m_panel(panel),
    m_titleFormat(titleFormat)
{
    if (!m_panel)
    {
        return;
    }

    // panel-thumbnail-size 1359:33719: S / M / L tiles, the title and its description, the
    // check of the chosen size
    struct SizeRow
    {
        TileSize size;
        QString tileText;
        QString title;
        QString description;
    };
    const SizeRow rows[] = {
        { TileSize::Small, tr("S", "the tile of the size Small"), tr("Small"), QString() },
        { TileSize::Medium, tr("M", "the tile of the size Medium"), tr("Medium"), QString() },
        { TileSize::Large, tr("L", "the tile of the size Large"), tr("Large"), QString() },
    };
    for (const SizeRow& sizeRow : rows)
    {
        PDFTileRow* row = m_panel->addRow(sizeRow.title, getDescription(sizeRow.size), QString(), PDFTileRow::Kind::MenuItem);
        row->setTileText(sizeRow.tileText);
        row->setObjectName(QStringLiteral("toolTileSize-%1").arg(PDFAppToolPageGrid::getTileSizeId(sizeRow.size)));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolTileSizeMenu::onRowClicked);
        m_rows.emplace_back(sizeRow.size, row);
    }
    update();
}

PDFAppToolTileSizeMenu::~PDFAppToolTileSizeMenu() = default;

QString PDFAppToolTileSizeMenu::getDescription(TileSize size) const
{
    // 1359:33719: the size of the tool says it is the default
    if (size == m_defaultSize)
    {
        return tr("The default size");
    }
    switch (size)
    {
        case TileSize::Small:
            return tr("More pages on the screen at once");
        case TileSize::Medium:
            return tr("Larger pages, fewer on the screen");
        case TileSize::Large:
            break;
    }
    return tr("More detail of each page");
}

void PDFAppToolTileSizeMenu::setDefaultSize(TileSize size)
{
    m_defaultSize = size;
    for (const auto& [rowSize, row] : m_rows)
    {
        if (row)
        {
            row->setDescription(getDescription(rowSize));
        }
    }
}

PDFAppToolTileSizeMenu::TileSize PDFAppToolTileSizeMenu::readSize(const PDFAppContext* context, TileSize defaultSize)
{
    // A size, which the user has not chosen (the default of the setting), is the default of the tool
    if (context && context->getDatabase())
    {
        const QString id = context->getSettings().getString(PDFSettingKeys::toolsPageGridSize());
        for (const TileSize size : { TileSize::Small, TileSize::Medium, TileSize::Large })
        {
            if (PDFAppToolPageGrid::getTileSizeId(size) == id)
            {
                return size;
            }
        }
    }
    return defaultSize;
}

void PDFAppToolTileSizeMenu::storeSize(PDFAppContext* context, TileSize size)
{
    // Every chosen size is stored (also the default of a tool: the user has chosen it)
    if (context && context->getDatabase() && context->getSettings().getString(PDFSettingKeys::toolsPageGridSize()) != PDFAppToolPageGrid::getTileSizeId(size))
    {
        const PDFDatabaseError error = context->getSettings().setValue(PDFSettingKeys::toolsPageGridSize(), PDFAppToolPageGrid::getTileSizeId(size));
        if (error.isError())
        {
            qWarning("The size of the page grid cannot be stored: %s", qPrintable(error.toString()));
        }
    }
}

void PDFAppToolTileSizeMenu::setSize(TileSize size)
{
    m_size = size;
    update();
}

PDFAppToolPanelPopup* PDFAppToolTileSizeMenu::getPanel() const
{
    return m_panel.data();
}

QToolButton* PDFAppToolTileSizeMenu::getButton() const
{
    return m_panel ? m_panel->getButton() : nullptr;
}

PDFTileRow* PDFAppToolTileSizeMenu::getRow(TileSize size) const
{
    for (const auto& [rowSize, row] : m_rows)
    {
        if (rowSize == size)
        {
            return row.data();
        }
    }
    return nullptr;
}

void PDFAppToolTileSizeMenu::update()
{
    // "Thumbnails: Medium" / "Size: Small" and the check of the chosen size
    if (QToolButton* button = getButton())
    {
        const PDFTileRow* row = getRow(m_size);
        button->setText(m_titleFormat.arg(row ? row->getTitle() : QString()));
        button->setToolTip(tr("Size of the page thumbnails"));
        button->setAccessibleName(button->text());
        button->setAccessibleDescription(button->toolTip());
    }
    for (const auto& [size, row] : m_rows)
    {
        if (row)
        {
            row->setChecked(size == m_size);
        }
    }
}

void PDFAppToolTileSizeMenu::onRowClicked()
{
    QObject* row = sender();
    TileSize size = m_size;
    for (const auto& [rowSize, rowPointer] : m_rows)
    {
        if (rowPointer == row)
        {
            size = rowSize;
        }
    }

    // The panel closes first (it may run code of the application), then the size is announced
    QPointer<PDFAppToolTileSizeMenu> guard(this);
    if (m_panel)
    {
        m_panel->close();
    }
    if (guard)
    {
        Q_EMIT sizeChosen(size);
    }
}

}   // namespace pdf

#include "moc_pdfapptoolpagegrid.cpp"
