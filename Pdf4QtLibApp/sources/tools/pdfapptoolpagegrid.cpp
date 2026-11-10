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
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumentview.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolpanelpopup.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcatalog.h"
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

        case PDFComponentRoles::HueRole:
            return tile.dotColor.has_value() ? QVariant(int(*tile.dotColor)) : QVariant();

        case PDFAppToolPageTileDelegate::DimmedRole:
            return tile.isDimmed;

        case PDFAppToolPageTileDelegate::CutBeforeRole:
            return tile.markerBefore;

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
    m_delegate = new PDFAppToolPageTileDelegate(m_view);
    m_delegate->setSlotSize(getScaledSlotSize(m_tileSize));
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
    m_scheduler->setAppDocument(view ? view->getDocument() : nullptr);
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

    // New keys of the pictures (their size), then one layout of all tiles; the signals reach the views only
    QPointer<PDFAppToolPageGrid> guard(this);
    updatePictureSize();
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

PDFComponentPainter::PageTile PDFAppToolPageTileDelegate::getTileContent(const QModelIndex& index, bool withPixmap) const
{
    PDFComponentPainter::PageTile content = getContent(index, withPixmap);
    const QVariant scale = index.isValid() ? index.data(SlotScaleRole) : QVariant();
    if (scale.isValid())
    {
        content.slotSize = getScaledSlotSize(scale.toDouble());
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
    return PDFComponentPainter::getPageTileSize(getTileContent(index, false));
}

PDFAppToolPageTileDelegate::MarkRects PDFAppToolPageTileDelegate::getMarkRects(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    MarkRects rects;
    if (!index.isValid())
    {
        return rects;
    }

    const QRectF rect = option.rect;
    const PDFComponentPainter::PageTile content = getTileContent(index, false);
    rects.page = PDFComponentPainter::getPageTilePageRect(rect, content);
    if (rects.page.isEmpty())
    {
        return MarkRects();
    }

    // The leading edge of the tile is mirrored for a right-to-left layout
    const bool isRightToLeft = option.direction == Qt::RightToLeft;
    if (index.data(CutBeforeRole).toBool())
    {
        // 383:5715: the amber line of the cut beside the page, inside of the tile
        rects.cut = getMirrored(rect, QRectF(rect.left(), rects.page.top(), CUT_WIDTH, rects.page.height()), isRightToLeft);
    }

    if (getDotHue(index).has_value())
    {
        // 383:5715: the dot in front of the label, which is centered below the slot (the page
        // is centered in its slot)
        const QSize slotSize = PDFComponentPainter::getPageTileSlotSize(content);
        const qreal labelTop = rects.page.center().y() + slotSize.height() / 2.0 + LABEL_GAP;
        const QFontMetricsF metrics(PDFThemeFonts::getFont(PDFThemeFonts::Role::Caption));
        const qreal labelWidth = qMin<qreal>(metrics.horizontalAdvance(content.label), rect.width());
        const qreal dotLeft = qMax(rect.left(), rect.center().x() - labelWidth / 2.0 - DOT_GAP - DOT_SIZE);
        rects.dot = getMirrored(rect, QRectF(dotLeft, labelTop + (LABEL_HEIGHT - DOT_SIZE) / 2.0, DOT_SIZE, DOT_SIZE), isRightToLeft);
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

    // The page tile with the slot of the row (the base paints the slot of the delegate)
    PDFComponentPainter::paintPageTile(painter, QRectF(option.rect), getTileContent(index), state);

    const bool isDimmed = index.data(DimmedRole).toBool();
    const bool hasPaperBorder = index.data(PaperBorderRole).toBool();
    const std::optional<PDFAccentHue> dotHue = getDotHue(index);
    if (!isDimmed && !hasPaperBorder && !dotHue.has_value() && !index.data(CutBeforeRole).toBool())
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
        { TileSize::Small, tr("S", "the tile of the size Small"), tr("Small"), tr("More pages on the screen at once") },
        { TileSize::Medium, tr("M", "the tile of the size Medium"), tr("Medium"), tr("The default size") },
        { TileSize::Large, tr("L", "the tile of the size Large"), tr("Large"), tr("More detail of each page") },
    };
    for (const SizeRow& sizeRow : rows)
    {
        PDFTileRow* row = m_panel->addRow(sizeRow.title, sizeRow.description, QString(), PDFTileRow::Kind::MenuItem);
        row->setTileText(sizeRow.tileText);
        row->setObjectName(QStringLiteral("toolTileSize-%1").arg(PDFAppToolPageGrid::getTileSizeId(sizeRow.size)));
        row->setSelectionBehavior(PDFComponentButton::SelectionBehavior::Exclusive);
        connect(row, &PDFTileRow::clicked, this, &PDFAppToolTileSizeMenu::onRowClicked);
        m_rows.emplace_back(sizeRow.size, row);
    }
    update();
}

PDFAppToolTileSizeMenu::~PDFAppToolTileSizeMenu() = default;

PDFAppToolTileSizeMenu::TileSize PDFAppToolTileSizeMenu::readSize(const PDFAppContext* context)
{
    if (context && context->getDatabase())
    {
        return PDFAppToolPageGrid::getTileSizeFromId(context->getSettings().getString(PDFSettingKeys::toolsPageGridSize()));
    }
    return TileSize::Medium;
}

void PDFAppToolTileSizeMenu::storeSize(PDFAppContext* context, TileSize size)
{
    if (context && context->getDatabase() && readSize(context) != size)
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
