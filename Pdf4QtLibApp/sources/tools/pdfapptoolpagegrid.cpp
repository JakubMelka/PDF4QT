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

#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"

#include "pdfcatalog.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmapCache>
#include <QScrollBar>
#include <QTransform>
#include <QVBoxLayout>

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
};

// --------------------------------------------------------- PDFAppToolPageGridModel

PDFAppToolPageGridModel::PDFAppToolPageGridModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolPageGridModel::~PDFAppToolPageGridModel() = default;

void PDFAppToolPageGridModel::setPageDocument(PDFDocumentPointer document, const QString& documentId)
{
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
        if (!(tiles[i] == m_tiles[i]))
        {
            first = first < 0 ? int(i) : first;
            last = int(i);
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

QVariant PDFAppToolPageGridModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || size_t(index.row()) >= m_tiles.size())
    {
        return QVariant();
    }

    const Tile& tile = m_tiles[size_t(index.row())];
    switch (role)
    {
        case PDFComponentRoles::PageTileStateRole:
            return int(tile.state);

        case PDFComponentRoles::SubtitleRole:
            return tile.caption;

        case PDFComponentRoles::StateIconRole:
            return tile.badgeIconName;

        case Qt::AccessibleDescriptionRole:
            return tile.caption;

        case PDFComponentRoles::PageSizeRole:
        {
            // The page as it will be saved: a quarter turn swaps its sides
            const QSizeF size = m_pageSizes[size_t(index.row())];
            return PDFAppToolPageGridHelper::getQuarterTurns(tile.extraRotation) % 2 == 1 ? size.transposed() : size;
        }

        case PDFComponentRoles::PixmapRole:
        case Qt::DecorationRole:
        {
            const QVariant picture = BaseClass::data(index, PDFComponentRoles::PixmapRole);
            const int quarterTurns = PDFAppToolPageGridHelper::getQuarterTurns(tile.extraRotation);
            if (quarterTurns == 0 || picture.typeId() != QMetaType::QPixmap)
            {
                return picture;
            }

            // The picture of the thumbnails turned by the tool, kept by its own key
            const QString key = getKey(index.row()) + QStringLiteral("/turn%1").arg(quarterTurns);
            QPixmap turned;
            if (!QPixmapCache::find(key, &turned))
            {
                const QPixmap pixmap = picture.value<QPixmap>();
                turned = pixmap.transformed(QTransform().rotate(90.0 * quarterTurns));
                turned.setDevicePixelRatio(pixmap.devicePixelRatio());
                QPixmapCache::insert(key, turned);
            }
            return turned;
        }

        default:
            return BaseClass::data(index, role);
    }
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

void PDFAppToolPageGridView::mousePressEvent(QMouseEvent* event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    m_pressedPage = event->button() == Qt::LeftButton && index.isValid() ? index.row() : -1;
    BaseClass::mousePressEvent(event);
}

void PDFAppToolPageGridView::mouseReleaseEvent(QMouseEvent* event)
{
    const QModelIndex index = indexAt(event->position().toPoint());
    const int pressedPage = m_pressedPage;
    m_pressedPage = -1;
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    BaseClass::mouseReleaseEvent(event);

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

PDFAppToolPageGrid::PDFAppToolPageGrid(const QSize& slotSize, QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolPageGrid"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING - TILE_SPACING, PADDING, PADDING - TILE_SPACING, PADDING);
    layout->setSpacing(0);

    // preview-bar 389:20436: the text Semi Bold 13 text/primary 10 below the top of the bar
    m_header = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, this);
    m_header->setObjectName(QStringLiteral("toolPageGridHeader"));
    m_header->setFixedHeight(BAR_HEIGHT);
    m_header->setContentsMargins(TILE_SPACING, 0, TILE_SPACING, 0);
    m_header->setAlignment(Qt::AlignLeading | Qt::AlignVCenter);
    layout->addWidget(m_header);
    layout->addSpacing(BAR_GAP - TILE_SPACING);

    m_view = new PDFAppToolPageGridView(this);
    m_view->setObjectName(QStringLiteral("toolPageGridView"));
    PDFAppPanelWidgets::prepareView(m_view);
    m_view->setAccessibleName(tr("Pages"));
    m_model = new PDFAppToolPageGridModel(m_view);
    m_delegate = new PDFPageTileDelegate(m_view);
    m_delegate->setSlotSize(slotSize);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    m_scheduler = new PDFAppThumbnailScheduler(m_model, m_view, this);
    layout->addWidget(m_view, 1);
    layout->addSpacing(HINT_GAP - TILE_SPACING);

    // The hint 389:20890: Regular 11 text/tertiary in lines of 13
    m_hint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, this);
    m_hint->setObjectName(QStringLiteral("toolPageGridHint"));
    m_hint->setContentsMargins(TILE_SPACING, 0, TILE_SPACING, 0);
    layout->addWidget(m_hint);
    layout->addStretch(0);

    connect(m_view, &PDFAppToolPageGridView::pageClicked, this, &PDFAppToolPageGrid::pageClicked);
    connect(m_view, &PDFAppToolPageGridView::deleteRequested, this, &PDFAppToolPageGrid::deleteRequested);
    // The view emits the height from the event loop, after its layout
    connect(m_view, &PDFAppToolPageGridView::contentHeightChanged, this, &PDFAppToolPageGrid::onContentHeightChanged);

    updatePictureSize();
}

PDFAppToolPageGrid::~PDFAppToolPageGrid() = default;

void PDFAppToolPageGrid::setDocument(PDFDocumentPointer document, const QString& documentId, PDFAppDocumentView* view)
{
    if (!m_model || !m_scheduler)
    {
        return;
    }

    m_scheduler->setProxy(view ? view->getProxy() : nullptr);
    m_model->setPageDocument(std::move(document), documentId);
    m_scheduler->reset();
}

void PDFAppToolPageGrid::setHeaderText(const QString& text)
{
    if (m_header)
    {
        m_header->setText(text);
    }
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

}   // namespace pdf

#include "moc_pdfapptoolpagegrid.cpp"
