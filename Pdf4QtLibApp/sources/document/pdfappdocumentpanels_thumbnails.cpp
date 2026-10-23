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

#include "pdfappdocumentpanels_thumbnails.h"
#include "pdfapplocale.h"
#include "pdfappcommand.h"
#include "pdfappcontext.h"
#include "pdfappdocument.h"
#include "pdfappdocumentpanels_common.h"
#include "pdfappdocumenttab.h"
#include "pdfappdocumentview.h"
#include "pdfapproute.h"
#include "pdfsettingdefinitions.h"
#include "pdfsettingsrepository.h"

#include "pdfcatalog.h"
#include "pdfcompiler.h"
#include "pdfdocumenttoolcatalog.h"
#include "pdfdrawspacecontroller.h"
#include "pdfthemeiconengine.h"
#include "pdfwidgetstyleproperties.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLocale>
#include <QMenu>
#include <QPixmapCache>
#include <QResizeEvent>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf
{

/// Helpers of the thumbnails
class PDFAppThumbnailsHelper
{
public:
    PDFAppThumbnailsHelper() = delete;

    /// Generation of the pictures, unique in the process (GUI thread only)
    static quint64 nextGeneration()
    {
        static quint64 generation = 0;
        return ++generation;
    }

    /// Gaps of the grid (sidebar-content 193:929: 8 between the columns, 6 between the rows)
    static constexpr int GRID_GAP_X = 8;
    static constexpr int GRID_GAP_Y = 6;

    /// Paddings of the grid (193:929: 4 above, 16 below, at least 14 at the sides)
    static constexpr int GRID_PADDING_TOP = 4;
    static constexpr int GRID_PADDING_BOTTOM = 16;
    static constexpr int GRID_PADDING_SIDE = 14;

    /// Spare pixels of the viewport beyond the columns
    static constexpr int GRID_SLACK = 2;

    /// Height of the slot of the page of the width (108 : 148 of the design)
    static int getSlotHeight(int width)
    {
        return int(std::lround(double(width) * 148.0 / 108.0));
    }
};

PDFAppThumbnailsModel::PDFAppThumbnailsModel(QObject* parent) :
    BaseClass(parent),
    m_generation(PDFAppThumbnailsHelper::nextGeneration())
{

}

PDFAppThumbnailsModel::~PDFAppThumbnailsModel() = default;

void PDFAppThumbnailsModel::setDocument(PDFDocumentPointer document, const QString& documentId)
{
    const int pageCount = document ? int(qMin<size_t>(document->getCatalog()->getPageCount(), size_t(std::numeric_limits<int>::max()))) : 0;
    const bool isReset = pageCount != m_pageCount || documentId != m_documentId;
    if (isReset)
    {
        beginResetModel();
    }

    m_document = std::move(document);
    m_documentId = documentId;
    m_pageCount = pageCount;
    m_pageGenerations.assign(size_t(pageCount), 0);
    m_generation = PDFAppThumbnailsHelper::nextGeneration();
    m_requestedPages.clear();

    if (isReset)
    {
        endResetModel();
    }
    else if (m_pageCount > 0)
    {
        Q_EMIT dataChanged(index(0), index(m_pageCount - 1));
    }
}

void PDFAppThumbnailsModel::setPictureSize(const QSize& size, qreal devicePixelRatio)
{
    const QSize validSize = size.isValid() ? size : QSize(108, 148);
    const qreal validRatio = (std::isfinite(devicePixelRatio) && devicePixelRatio > 0.0) ? qBound(1.0, devicePixelRatio, 8.0) : 1.0;
    if (validSize == m_pictureSize && qFuzzyCompare(validRatio, m_devicePixelRatio))
    {
        return;
    }

    m_pictureSize = validSize;
    m_devicePixelRatio = validRatio;
    invalidateAll();
}

void PDFAppThumbnailsModel::invalidateAll()
{
    m_generation = PDFAppThumbnailsHelper::nextGeneration();
    if (m_pageCount > 0)
    {
        Q_EMIT dataChanged(index(0), index(m_pageCount - 1), { PDFComponentRoles::PixmapRole });
    }
}

void PDFAppThumbnailsModel::invalidatePages(const std::vector<PDFInteger>& pages)
{
    for (const PDFInteger page : pages)
    {
        if (page >= 0 && page < m_pageCount)
        {
            ++m_pageGenerations[size_t(page)];
            const QModelIndex row = index(int(page));
            Q_EMIT dataChanged(row, row, { PDFComponentRoles::PixmapRole });
        }
    }
}

QString PDFAppThumbnailsModel::getKey(int pageIndex) const
{
    const quint32 pageGeneration = (pageIndex >= 0 && pageIndex < m_pageCount) ? m_pageGenerations[size_t(pageIndex)] : 0;
    return QStringLiteral("pdf4qt-thumbnail/%1/%2/%3/%4/%5x%6@%7").arg(m_documentId).arg(m_generation).arg(pageIndex).arg(pageGeneration)
                                                                  .arg(m_pictureSize.width()).arg(m_pictureSize.height()).arg(m_devicePixelRatio);
}

bool PDFAppThumbnailsModel::hasPicture(int pageIndex) const
{
    QPixmap pixmap;
    return QPixmapCache::find(getKey(pageIndex), &pixmap);
}

void PDFAppThumbnailsModel::setPicture(int pageIndex, const QPixmap& pixmap)
{
    if (pageIndex < 0 || pageIndex >= m_pageCount || pixmap.isNull())
    {
        return;
    }

    QPixmapCache::insert(getKey(pageIndex), pixmap);
    const QModelIndex row = index(pageIndex);
    Q_EMIT dataChanged(row, row, { PDFComponentRoles::PixmapRole });
}

std::vector<int> PDFAppThumbnailsModel::takeRequestedPages()
{
    std::vector<int> pages(m_requestedPages.cbegin(), m_requestedPages.cend());
    m_requestedPages.clear();
    return pages;
}

int PDFAppThumbnailsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_pageCount;
}

Qt::ItemFlags PDFAppThumbnailsModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
}

QVariant PDFAppThumbnailsModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_pageCount || !m_document)
    {
        return QVariant();
    }

    const int pageIndex = index.row();
    switch (role)
    {
        case Qt::DisplayRole:
            return PDFAppPanelWidgets::getPageLabel(m_document->getCatalog(), pageIndex);

        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
            return PDFAppPanelWidgets::getPageTitle(m_document->getCatalog(), pageIndex);

        case PDFComponentRoles::PixmapRole:
        {
            QPixmap pixmap;
            if (QPixmapCache::find(getKey(pageIndex), &pixmap))
            {
                return pixmap;
            }

            // The row is painted without its picture: the scheduler draws it
            const bool isFirstRequest = m_requestedPages.empty();
            m_requestedPages.insert(pageIndex);
            if (isFirstRequest)
            {
                Q_EMIT const_cast<PDFAppThumbnailsModel*>(this)->pictureRequested();
            }
            return QVariant();
        }

        default:
            break;
    }

    return QVariant();
}

PDFAppThumbnailScheduler::PDFAppThumbnailScheduler(PDFAppThumbnailsModel* model, QAbstractItemView* view, QObject* parent) :
    BaseClass(parent),
    m_model(model),
    m_view(view)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(0);
    connect(m_timer, &QTimer::timeout, this, &PDFAppThumbnailScheduler::process);

    // Visible pages, which wait for a free place of a compilation, are tried again
    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    m_retryTimer->setInterval(RETRY_INTERVAL_MS);
    connect(m_retryTimer, &QTimer::timeout, this, &PDFAppThumbnailScheduler::process);

    if (model)
    {
        connect(model, &PDFAppThumbnailsModel::pictureRequested, this, &PDFAppThumbnailScheduler::onPictureRequested);
    }
}

PDFAppThumbnailScheduler::~PDFAppThumbnailScheduler()
{
    if (m_proxy)
    {
        m_proxy->disconnect(this);
    }
}

void PDFAppThumbnailScheduler::setProxy(PDFDrawWidgetProxy* proxy)
{
    if (m_proxy == proxy)
    {
        return;
    }

    if (m_proxy)
    {
        m_proxy->disconnect(this);
    }

    m_proxy = proxy;
    reset();

    if (proxy)
    {
        connect(proxy, &PDFDrawWidgetProxy::pageImageChanged, this, &PDFAppThumbnailScheduler::onPageImageChanged);
        connect(proxy, &QObject::destroyed, this, &PDFAppThumbnailScheduler::onProxyDestroyed);
    }
}

void PDFAppThumbnailScheduler::reset()
{
    m_waiting.clear();
    m_pending.clear();
}

void PDFAppThumbnailScheduler::onProxyDestroyed()
{
    // The proxy is not whole any more: only the pointer is forgotten
    m_proxy = nullptr;
    reset();
}

void PDFAppThumbnailScheduler::onPictureRequested()
{
    schedule();
}

void PDFAppThumbnailScheduler::schedule()
{
    if (m_timer && !m_timer->isActive())
    {
        m_timer->start();
    }
}

void PDFAppThumbnailScheduler::onPageImageChanged(bool all, const std::vector<PDFInteger>& pages)
{
    if (!m_model)
    {
        return;
    }

    if (all)
    {
        // The drawing has changed (features, color mode, document): every picture
        // is drawn again, the compiler has dropped its tasks
        m_pending.clear();
        m_model->invalidateAll();
    }
    else
    {
        for (const PDFInteger page : pages)
        {
            m_pending.erase(page);
        }
        m_model->invalidatePages(pages);
    }
    schedule();
}

bool PDFAppThumbnailScheduler::isRowVisible(int row) const
{
    if (!m_view || !m_model || !m_view->isVisible())
    {
        return false;
    }

    const QRect rect = m_view->visualRect(m_model->index(row));
    return rect.isValid() && rect.intersects(m_view->viewport()->rect());
}

void PDFAppThumbnailScheduler::process()
{
    if (!m_model || !m_view)
    {
        return;
    }

    for (const int page : m_model->takeRequestedPages())
    {
        m_waiting.insert(page);
    }

    // Requests, which have not ended, expire (a compiler stopped by a change)
    for (auto it = m_pending.begin(); it != m_pending.end();)
    {
        if (it->second.hasExpired(PENDING_TIMEOUT_MS))
        {
            it = m_pending.erase(it);
        }
        else
        {
            ++it;
        }
    }

    PDFAsynchronousPageCompiler* compiler = m_proxy ? m_proxy->getCompiler() : nullptr;
    const qreal devicePixelRatio = m_view->devicePixelRatioF();
    const QSize pictureSize = m_model->getPictureSize();
    const int pixelSize = qBound(1, int(std::ceil(qMax(pictureSize.width(), pictureSize.height()) * devicePixelRatio)), 4096);

    int drawn = 0;
    bool hasMore = false;
    for (auto it = m_waiting.begin(); it != m_waiting.end();)
    {
        const int page = *it;
        if (!isRowVisible(page) || m_model->hasPicture(page))
        {
            // Only the visible rows are drawn; a row scrolled into the view again
            // asks for its picture again
            it = m_waiting.erase(it);
            continue;
        }

        if (!compiler || compiler->getState() != PDFAsynchronousPageCompiler::State::Active)
        {
            ++it;
            continue;
        }

        const PDFPrecompiledPage* compiledPage = compiler->getCompiledPage(page, PDFAsynchronousPageCompiler::CompileMode::None);
        if (compiledPage)
        {
            if (drawn >= MAXIMUM_PICTURES_PER_PASS)
            {
                hasMore = true;
                ++it;
                continue;
            }

            QImage image = m_proxy->drawThumbnailImage(page, pixelSize);
            ++drawn;
            ++m_drawnCount;
            it = m_waiting.erase(it);
            if (!image.isNull())
            {
                image.setDevicePixelRatio(devicePixelRatio);
                m_model->setPicture(page, QPixmap::fromImage(std::move(image)));
            }
            continue;
        }

        if (m_pending.count(page) == 0 && int(m_pending.size()) < MAXIMUM_PENDING_COMPILES)
        {
            // The page is compiled by the compiler of the canvas; the request
            // cannot be canceled, so only a few of them wait at once
            compiler->getCompiledPage(page, PDFAsynchronousPageCompiler::CompileMode::Persistent);
            QElapsedTimer timer;
            timer.start();
            m_pending[page] = timer;
            m_maximumPending = qMax(m_maximumPending, int(m_pending.size()));
        }
        ++it;
    }

    if (hasMore)
    {
        schedule();
    }
    else if (!m_waiting.empty() && m_retryTimer && !m_retryTimer->isActive())
    {
        m_retryTimer->start();
    }
}

PDFAppThumbnailListView::PDFAppThumbnailListView(QWidget* parent) :
    BaseClass(parent)
{
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setFlow(QListView::LeftToRight);
    setWrapping(true);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(true);
    setSpacing(0);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSelectionRectVisible(false);
    setDragEnabled(false);
    setAcceptDrops(false);
    setContextMenuPolicy(Qt::DefaultContextMenu);
}

PDFAppThumbnailListView::~PDFAppThumbnailListView() = default;

void PDFAppThumbnailListView::setItemSize(const QSize& size)
{
    if (m_itemSize == size)
    {
        return;
    }

    m_itemSize = size;
    setGridSize(size + QSize(PDFAppThumbnailsHelper::GRID_GAP_X, PDFAppThumbnailsHelper::GRID_GAP_Y));
    updateMargins();
}

void PDFAppThumbnailListView::resizeEvent(QResizeEvent* event)
{
    BaseClass::resizeEvent(event);
    updateMargins();
}

void PDFAppThumbnailListView::updateGeometries()
{
    BaseClass::updateGeometries();

    // The scroll bar may have appeared or vanished (the viewport has another width)
    if (!m_isUpdatingMargins)
    {
        m_isUpdatingMargins = true;
        updateMargins();
        m_isUpdatingMargins = false;
    }
}

void PDFAppThumbnailListView::updateMargins()
{
    if (!m_itemSize.isValid())
    {
        return;
    }

    // The columns are centered in the sidebar (193:929: x 21.5 and 153.5 of 299);
    // the width is the width of the viewport with its margins (without a scroll bar)
    const QMargins current = viewportMargins();
    const int total = viewport()->width() + current.left() + current.right();
    const int gridWidth = m_itemSize.width() + PDFAppThumbnailsHelper::GRID_GAP_X;
    const int available = qMax(0, total - 2 * PDFAppThumbnailsHelper::GRID_PADDING_SIDE + PDFAppThumbnailsHelper::GRID_GAP_X - PDFAppThumbnailsHelper::GRID_SLACK);
    const int columns = qMax(1, available / qMax(1, gridWidth));
    const int contentWidth = columns * gridWidth;
    const int side = qMax(0, (total - contentWidth) / 2);

    // QListView wraps the row, when the grid does not fit with a pixel to spare
    const int right = qMax(0, total - contentWidth - side - PDFAppThumbnailsHelper::GRID_SLACK);
    const QMargins margins(side, PDFAppThumbnailsHelper::GRID_PADDING_TOP, right, PDFAppThumbnailsHelper::GRID_PADDING_BOTTOM);
    if (viewportMargins() != margins)
    {
        setViewportMargins(margins);
    }
}

void PDFAppThumbnailListView::contextMenuEvent(QContextMenuEvent* event)
{
    Q_EMIT contextMenuRequested(event->globalPos());
    event->accept();
}

PDFAppThumbnailsPanel::PDFAppThumbnailsPanel(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("thumbnailsPanel"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_view = new PDFAppThumbnailListView(this);
    m_view->setObjectName(QStringLiteral("thumbnailsView"));
    PDFAppPanelWidgets::prepareView(m_view);
    m_view->setAccessibleName(tr("Thumbnails"));

    m_model = new PDFAppThumbnailsModel(m_view);
    m_delegate = new PDFThumbnailDelegate(m_view);
    m_view->setModel(m_model);
    m_view->setItemDelegate(m_delegate);
    m_scheduler = new PDFAppThumbnailScheduler(m_model, m_view, this);
    layout->addWidget(m_view, 1);

    connect(m_view, &QAbstractItemView::clicked, this, &PDFAppThumbnailsPanel::onViewClicked);
    connect(m_view, &QAbstractItemView::activated, this, &PDFAppThumbnailsPanel::onViewActivated);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppThumbnailsPanel::onSelectionChanged);
    connect(m_view, &PDFAppThumbnailListView::contextMenuRequested, this, &PDFAppThumbnailsPanel::onContextMenuRequested);

    updatePictureSize();
}

PDFAppThumbnailsPanel::~PDFAppThumbnailsPanel()
{
    if (m_connectedDocument)
    {
        m_connectedDocument->disconnect(this);
    }
    if (m_connectedView)
    {
        m_connectedView->disconnect(this);
    }
}

QString PDFAppThumbnailsPanel::getId() const
{
    return PDFAppDocumentPanelIds::thumbnails();
}

QString PDFAppThumbnailsPanel::getTitle() const
{
    return tr("Thumbnails");
}

QString PDFAppThumbnailsPanel::getSubtitle() const
{
    const int pageCount = m_model ? m_model->rowCount() : 0;
    if (pageCount == 0)
    {
        return QString();
    }

    const size_t selected = getSelectedPages().size();
    if (selected > 1)
    {
        // 266:12502 "3 of 1,592 pages selected"
        return tr("%1 of %Ln pages selected", nullptr, pageCount).arg(PDFAppLocale::formatNumber(qint64(selected)));
    }
    // 193:756 "1,592 pages"
    return pageCount == 1 ? tr("%Ln page", nullptr, pageCount) : tr("%Ln pages", nullptr, pageCount);
}

QString PDFAppThumbnailsPanel::getIconName() const
{
    return QStringLiteral("pages");
}

QWidget* PDFAppThumbnailsPanel::createHeaderWidget(QWidget* parent)
{
    // thumbnail-size 193:921 (slider 84 x 20) and thumbnail-sync 193:925 (32 x 32), gap 8
    QWidget* widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("thumbnailsHeader"));
    QHBoxLayout* layout = new QHBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_sizeSlider = new QSlider(Qt::Horizontal, widget);
    m_sizeSlider->setObjectName(QStringLiteral("thumbnailSize"));
    m_sizeSlider->setRange(MINIMUM_SLOT_WIDTH, MAXIMUM_SLOT_WIDTH);
    m_sizeSlider->setSingleStep(4);
    m_sizeSlider->setPageStep(24);
    m_sizeSlider->setValue(m_slotWidth);
    m_sizeSlider->setFixedSize(84, 20);
    m_sizeSlider->setToolTip(tr("Thumbnail size"));
    m_sizeSlider->setAccessibleName(tr("Thumbnail size"));
    m_sizeSlider->setFocusPolicy(Qt::TabFocus);
    layout->addWidget(m_sizeSlider, 0, Qt::AlignVCenter);
    connect(m_sizeSlider, &QSlider::valueChanged, this, &PDFAppThumbnailsPanel::onSizeSliderChanged);

    m_syncButton = PDFAppPanelWidgets::createHeaderButton(widget, QStringLiteral("locate"), tr("Follow the current page"), QStringLiteral("thumbnailSync"));
    m_syncButton->setCheckable(true);
    m_syncButton->setChecked(m_isSyncEnabled);
    layout->addWidget(m_syncButton, 0, Qt::AlignVCenter);
    connect(m_syncButton, &QToolButton::toggled, this, &PDFAppThumbnailsPanel::onSyncToggled);
    return widget;
}

void PDFAppThumbnailsPanel::setDocument(PDFAppDocument* document, PDFAppDocumentView* view)
{
    if (m_connectedDocument && m_connectedDocument != document)
    {
        m_connectedDocument->disconnect(this);
    }
    if (m_connectedView && m_connectedView != view)
    {
        m_connectedView->disconnect(this);
    }

    BaseClass::setDocument(document, view);

    if (document && m_connectedDocument != document)
    {
        connect(document, &PDFAppDocument::documentChanged, this, &PDFAppThumbnailsPanel::onDocumentChanged);
        connect(document, &QObject::destroyed, this, &PDFAppThumbnailsPanel::onDocumentDestroyed);
    }
    if (view && m_connectedView != view)
    {
        connect(view, &PDFAppDocumentView::currentPageChanged, this, &PDFAppThumbnailsPanel::onCurrentPageChanged);
    }
    m_connectedDocument = document;
    m_connectedView = view;

    if (m_scheduler)
    {
        m_scheduler->setProxy(view ? view->getProxy() : nullptr);
    }
    applyDocument();
}

void PDFAppThumbnailsPanel::applyDocument()
{
    PDFAppDocument* document = getDocument();
    if (!m_model)
    {
        return;
    }

    PDFDocumentPointer pointer = document && document->isReady() ? document->getDocument() : PDFDocumentPointer();
    m_model->setDocument(pointer, document ? document->getId() : QString());
    if (m_scheduler)
    {
        m_scheduler->reset();
    }
    syncToCurrentPage(true);
    Q_EMIT titleChanged();
}

void PDFAppThumbnailsPanel::onDocumentChanged(const PDFModifiedDocument& modified)
{
    Q_UNUSED(modified);

    // A new version of the document: new keys of the pictures (also, when the
    // number of pages is the same, a page may be different)
    const std::vector<PDFInteger> selection = getSelectedPages();
    applyDocument();
    if (selection.size() > 1)
    {
        selectPages(selection);
    }
}

void PDFAppThumbnailsPanel::onDocumentDestroyed()
{
    // The document is not whole any more: the rows are cleared
    if (m_model)
    {
        m_model->setDocument(PDFDocumentPointer(), QString());
    }
}

std::vector<PDFInteger> PDFAppThumbnailsPanel::getSelectedPages() const
{
    std::vector<PDFInteger> pages;
    if (!m_view || !m_view->selectionModel())
    {
        return pages;
    }

    for (const QModelIndex& index : m_view->selectionModel()->selectedIndexes())
    {
        if (index.isValid())
        {
            pages.push_back(index.row());
        }
    }
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    return pages;
}

void PDFAppThumbnailsPanel::selectPages(const std::vector<PDFInteger>& pages)
{
    if (!m_view || !m_model || !m_view->selectionModel())
    {
        return;
    }

    QItemSelection selection;
    QModelIndex first;
    for (const PDFInteger page : pages)
    {
        if (page >= 0 && page < m_model->rowCount())
        {
            const QModelIndex index = m_model->index(int(page));
            selection.select(index, index);
            if (!first.isValid())
            {
                first = index;
            }
        }
    }

    const bool wasSyncing = m_isSyncing;
    m_isSyncing = true;
    m_view->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    if (first.isValid())
    {
        m_view->selectionModel()->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    }
    m_isSyncing = wasSyncing;
    Q_EMIT titleChanged();
}

void PDFAppThumbnailsPanel::setSyncEnabled(bool enabled)
{
    if (m_isSyncEnabled == enabled)
    {
        return;
    }

    m_isSyncEnabled = enabled;
    if (m_syncButton && m_syncButton->isChecked() != enabled)
    {
        m_syncButton->setChecked(enabled);
    }
    if (enabled)
    {
        syncToCurrentPage(true);
    }
}

void PDFAppThumbnailsPanel::setSlotWidth(int width)
{
    const int validWidth = qBound(MINIMUM_SLOT_WIDTH, width, MAXIMUM_SLOT_WIDTH);
    if (m_slotWidth == validWidth)
    {
        return;
    }

    m_slotWidth = validWidth;
    if (m_sizeSlider && m_sizeSlider->value() != validWidth)
    {
        m_sizeSlider->setValue(validWidth);
    }
    updatePictureSize();
}

void PDFAppThumbnailsPanel::updatePictureSize()
{
    if (!m_view || !m_delegate || !m_model)
    {
        return;
    }

    const QSize slot(m_slotWidth, PDFAppThumbnailsHelper::getSlotHeight(m_slotWidth));
    m_delegate->setPageSize(slot);

    PDFComponentPainter::ThumbnailItem item;
    item.pageSize = slot;
    m_view->setItemSize(PDFComponentPainter::getThumbnailItemSize(item));
    m_model->setPictureSize(slot, m_view->devicePixelRatioF());
    m_view->doItemsLayout();

    const QModelIndex current = m_view->currentIndex();
    if (current.isValid())
    {
        m_view->scrollTo(current, QAbstractItemView::EnsureVisible);
    }
}

void PDFAppThumbnailsPanel::onSizeSliderChanged(int value)
{
    setSlotWidth(value);
}

void PDFAppThumbnailsPanel::onSyncToggled(bool checked)
{
    setSyncEnabled(checked);
}

void PDFAppThumbnailsPanel::onCurrentPageChanged(PDFInteger pageIndex)
{
    Q_UNUSED(pageIndex);
    syncToCurrentPage(false);
    updateViewSelection();
}

void PDFAppThumbnailsPanel::syncToCurrentPage(bool force)
{
    PDFAppDocumentView* view = getView();
    if (!m_view || !m_model || !view || (!m_isSyncEnabled && !force))
    {
        return;
    }

    const PDFInteger page = view->getCurrentPageIndex();
    if (page < 0 || page >= m_model->rowCount())
    {
        return;
    }

    // A selection of more pages is kept (the user works with it), only the
    // current page is shown
    const QModelIndex index = m_model->index(int(page));
    if (getSelectedPages().size() <= 1)
    {
        const bool wasSyncing = m_isSyncing;
        m_isSyncing = true;
        m_view->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
        m_isSyncing = wasSyncing;
    }

    if (m_isSyncEnabled)
    {
        m_view->scrollTo(index, QAbstractItemView::EnsureVisible);
    }
}

void PDFAppThumbnailsPanel::onViewClicked(const QModelIndex& index)
{
    // A click without a modifier goes to the page also, when it is selected already
    PDFAppDocumentView* view = getView();
    if (view && index.isValid() && getSelectedPages().size() == 1)
    {
        view->goToPage(index.row());
    }
}

void PDFAppThumbnailsPanel::onViewActivated(const QModelIndex& index)
{
    PDFAppDocumentView* view = getView();
    if (view && index.isValid())
    {
        view->goToPage(index.row());
    }
}

void PDFAppThumbnailsPanel::updateViewSelection()
{
    // The view reports the selected pages (Rotate, Print - O9) only for a real
    // selection: more pages, or one page other than the current one
    PDFAppDocumentView* view = getView();
    if (!view)
    {
        return;
    }

    std::vector<PDFInteger> pages = getSelectedPages();
    if (pages.size() == 1 && pages.front() == view->getCurrentPageIndex())
    {
        pages.clear();
    }
    view->setSelectedPages(std::move(pages));
}

void PDFAppThumbnailsPanel::onSelectionChanged()
{
    Q_EMIT titleChanged();
    updateViewSelection();

    if (m_isSyncing)
    {
        return;
    }

    // One selected page (a click, the keyboard) is shown in the canvas
    const std::vector<PDFInteger> pages = getSelectedPages();
    PDFAppDocumentView* view = getView();
    if (view && pages.size() == 1 && view->getCurrentPageIndex() != pages.front())
    {
        view->goToPage(pages.front());
    }
}

QMenu* PDFAppThumbnailsPanel::createContextMenu()
{
    // Context menu / Thumbnails 266:12848
    QMenu* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("thumbnailsContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);

    PDFAppDocumentTab* tab = PDFAppPanelWidgets::findDocumentTab(this);
    PDFAppDocument* document = getDocument();
    const bool hasSelection = !getSelectedPages().empty();
    const bool isLocked = document && document->isLocked();

    QAction* rotateLeft = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("rotate-ccw")), tr("Rotate Left"));
    rotateLeft->setObjectName(QStringLiteral("rotateLeft"));
    rotateLeft->setEnabled(hasSelection && tab && tab->isCommandEnabled(PDFAppCommands::rotatePagesLeft()));
    connect(rotateLeft, &QAction::triggered, this, &PDFAppThumbnailsPanel::onRotateLeft);

    QAction* rotateRight = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("rotate-cw")), tr("Rotate Right"));
    rotateRight->setObjectName(QStringLiteral("rotateRight"));
    rotateRight->setEnabled(hasSelection && tab && tab->isCommandEnabled(PDFAppCommands::rotatePagesRight()));
    connect(rotateRight, &QAction::triggered, this, &PDFAppThumbnailsPanel::onRotateRight);

    menu->addSeparator();

    QAction* insertPages = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("file-plus")), tr("Insert Pages…"));
    insertPages->setObjectName(QStringLiteral("insertPages"));
    insertPages->setEnabled(document && document->isReady() && !isLocked);
    connect(insertPages, &QAction::triggered, this, &PDFAppThumbnailsPanel::onInsertPages);

    QAction* movePages = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("arrow-up-down")), tr("Move to…"));
    movePages->setObjectName(QStringLiteral("movePages"));
    movePages->setEnabled(hasSelection && !isLocked);
    connect(movePages, &QAction::triggered, this, &PDFAppThumbnailsPanel::onMovePages);

    menu->addSeparator();

    QAction* extractPages = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("copy")), tr("Extract to New Document"));
    extractPages->setObjectName(QStringLiteral("extractPages"));
    extractPages->setEnabled(hasSelection);
    connect(extractPages, &QAction::triggered, this, &PDFAppThumbnailsPanel::onExtractPages);

    QAction* removePages = menu->addAction(PDFThemeIcons::getIcon(QStringLiteral("trash")), tr("Remove Pages"));
    removePages->setObjectName(QStringLiteral("removePages"));
    removePages->setProperty(PDFWidgetStyleProperties::DESTRUCTIVE, true);
    removePages->setEnabled(hasSelection && !isLocked);
    connect(removePages, &QAction::triggered, this, &PDFAppThumbnailsPanel::onRemovePages);
    return menu;
}

void PDFAppThumbnailsPanel::onContextMenuRequested(const QPoint& globalPosition)
{
    if (!m_view || !m_model || m_model->rowCount() == 0)
    {
        return;
    }

    // A click on a page outside of the selection selects the page (the canvas stays)
    const QModelIndex index = m_view->indexAt(m_view->viewport()->mapFromGlobal(globalPosition));
    if (index.isValid() && !m_view->selectionModel()->isSelected(index))
    {
        selectPages({ PDFInteger(index.row()) });
    }

    QPoint position = globalPosition;
    if (!index.isValid() || !m_view->viewport()->rect().contains(m_view->viewport()->mapFromGlobal(globalPosition)))
    {
        // The keyboard: the menu is placed at the current page
        const QModelIndex current = m_view->currentIndex();
        const QRect rect = current.isValid() ? m_view->visualRect(current) : QRect();
        position = m_view->viewport()->mapToGlobal(rect.isValid() ? rect.center() : QPoint(0, 0));
    }

    QMenu* menu = createContextMenu();
    menu->popup(position);
}

void PDFAppThumbnailsPanel::executeCommand(const QString& commandId)
{
    PDFAppDocumentTab* tab = PDFAppPanelWidgets::findDocumentTab(this);
    if (tab && tab->isCommandEnabled(commandId))
    {
        tab->executeCommand(commandId);
    }
}

void PDFAppThumbnailsPanel::onRotateLeft()
{
    executeCommand(PDFAppCommands::rotatePagesLeft());
}

void PDFAppThumbnailsPanel::onRotateRight()
{
    executeCommand(PDFAppCommands::rotatePagesRight());
}

void PDFAppThumbnailsPanel::openTool(const QString& toolId)
{
    // The page tools are steps of phase 2: their routes show Page Not Found now
    PDFAppDocument* document = getDocument();
    if (document && PDFDocumentToolCatalog::findTool(toolId))
    {
        PDFAppPanelWidgets::navigate(this, PDFAppRoutes::documentTool(document->getId(), toolId));
    }
}

void PDFAppThumbnailsPanel::onInsertPages()
{
    openTool(QStringLiteral("pages.insert"));
}

void PDFAppThumbnailsPanel::onMovePages()
{
    openTool(QStringLiteral("pages.manage"));
}

void PDFAppThumbnailsPanel::onExtractPages()
{
    openTool(QStringLiteral("pages.extract"));
}

void PDFAppThumbnailsPanel::onRemovePages()
{
    openTool(QStringLiteral("pages.remove"));
}

void PDFAppThumbnailsPanel::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::DevicePixelRatioChange)
    {
        // Another screen: the pictures are drawn for its pixels
        updatePictureSize();
    }
    else if (event->type() == QEvent::LanguageChange)
    {
        if (m_sizeSlider)
        {
            m_sizeSlider->setToolTip(tr("Thumbnail size"));
            m_sizeSlider->setAccessibleName(tr("Thumbnail size"));
        }
        if (m_syncButton)
        {
            m_syncButton->setToolTip(tr("Follow the current page"));
            m_syncButton->setAccessibleName(tr("Follow the current page"));
        }
        Q_EMIT titleChanged();
    }
}

QString PDFAppThumbnailsPanelFactory::getId() const
{
    return PDFAppDocumentPanelIds::thumbnails();
}

int PDFAppThumbnailsPanelFactory::getOrder() const
{
    return 20;
}

QString PDFAppThumbnailsPanelFactory::getTitle() const
{
    return PDFAppThumbnailsPanel::tr("Thumbnails");
}

QString PDFAppThumbnailsPanelFactory::getIconName() const
{
    return QStringLiteral("pages");
}

PDFAppDocumentPanel* PDFAppThumbnailsPanelFactory::createPanel(QWidget* parent) const
{
    return new PDFAppThumbnailsPanel(parent);
}

}   // namespace pdf
