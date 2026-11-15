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

#include "pdfapptoolimagelist.h"

#include "pdfappdocumentpanels_common.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"

#include "pdfcomponentpainter.h"
#include "pdfdocumenttool.h"
#include "pdfdocumenttool_extractimages.h"
#include "pdfexecutionpolicy.h"
#include "pdffluentwidgets.h"
#include "pdfoperationcontrol.h"
#include "pdfstylepainter.h"
#include "pdfstylepainterguard.h"
#include "pdfthemefonts.h"
#include "pdfthreadannotations.h"
#include "pdfwidgetstyleproperties.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QThreadPool>
#include <QVBoxLayout>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace pdf
{

/// A decoding of images (a worker of the pool). The job holds plain values: the shared document
/// (immutable), the color management and the keys - never the decoder.
class PDFAppToolImageDecodeJob : public PDFOperationControl
{
public:
    std::shared_ptr<PDFAppToolImageDecodeLink> link;
    quint64 ticket = 0;
    PDFDocumentPointer document;
    PDFCMSPointer cms;
    int maximumSide = 0;
    std::vector<PDFAppToolImageDecoder::Request> requests;
    QList<QImage> pictures;     ///< The results in the order of the requests (null = no pixels)

    virtual bool isOperationCanceled() const override { return m_isCanceled.load(std::memory_order_relaxed); }
    void cancel() { m_isCanceled.store(true, std::memory_order_relaxed); }

    void run();

private:
    std::atomic_bool m_isCanceled = false;
};

/// The way of the decoded images back to the decoder: cut by the destructor of the decoder (a
/// late result is dropped, nobody waits)
class PDFAppToolImageDecodeLink
{
public:
    explicit PDFAppToolImageDecodeLink(PDFAppToolImageDecoder* decoder) :
        m_decoder(decoder)
    {

    }

    void detach()
    {
        QMutexLocker locker(&m_mutex);
        m_decoder = nullptr;
    }

    void finished(const std::shared_ptr<PDFAppToolImageDecodeJob>& job)
    {
        QMutexLocker locker(&m_mutex);
        if (m_decoder)
        {
            PDFThreadAnnotations::happensBefore(job.get());
            PDFThreadAnnotations::TransportScope transportScope;
            QMetaObject::invokeMethod(m_decoder, &PDFAppToolImageDecoder::onJobFinished, Qt::QueuedConnection, job);
        }
    }

private:
    QMutex m_mutex;
    PDFAppToolImageDecoder* m_decoder;
};

/// The task of the pool: it holds the job, never the decoder
class PDFAppToolImageDecodeTask
{
public:
    explicit PDFAppToolImageDecodeTask(std::shared_ptr<PDFAppToolImageDecodeJob> job) :
        m_job(std::move(job))
    {

    }

    void operator()()
    {
        m_job->run();
        const std::shared_ptr<PDFAppToolImageDecodeLink> link = m_job->link;
        link->finished(m_job);
    }

private:
    std::shared_ptr<PDFAppToolImageDecodeJob> m_job;
};

/// Helpers of the list of the images
class PDFAppToolImageListHelper
{
public:
    PDFAppToolImageListHelper() = delete;

    /// Most thumbnails of one job (the visible rows of a tall panel)
    static constexpr int MAXIMUM_JOB_ROWS = 48;

    /// The picture at most \p maximumSide pixels long (0 = at most MAXIMUM_PIXELS of the decoder)
    static QImage limitSize(QImage picture, int maximumSide)
    {
        if (picture.isNull())
        {
            return picture;
        }
        if (maximumSide > 0 && qMax(picture.width(), picture.height()) > maximumSide)
        {
            return picture.scaled(QSize(maximumSide, maximumSide), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        const qint64 pixels = qint64(picture.width()) * qint64(picture.height());
        if (maximumSide <= 0 && pixels > PDFAppToolImageDecoder::MAXIMUM_PIXELS)
        {
            // A huge image is shown scaled down (the preview has no use for more pixels)
            const double scale = std::sqrt(double(PDFAppToolImageDecoder::MAXIMUM_PIXELS) / double(pixels));
            return picture.scaled(QSize(qMax(1, int(picture.width() * scale)), qMax(1, int(picture.height() * scale))), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return picture;
    }

    /// Resolution of an item: the smaller of the axes, 0 = not known
    static int getDpi(const PDFDocumentImageList::Item& item)
    {
        const qreal x = item.minimalDpi.x();
        const qreal y = item.minimalDpi.y();
        if (x > 0.0 && y > 0.0)
        {
            return qRound(qMin(x, y));
        }
        return qRound(qMax(qMax(x, y), 0.0));
    }

    /// Draws one line of the text of a row (an elided line of 13 at \p top) and moves \p top below it
    static void drawLine(QPainter* painter, const PDFComponentPainter::State& state, const QRectF& rowRect, qreal left, qreal width, qreal* top,
                         const QString& text, const QFont& font, PDFThemeColors::Color color)
    {
        const QRectF lineLogical(left, *top, width, PDFAppToolImageListDelegate::TEXT_LINE);
        PDFStylePainter::text(painter, state.visualRect(rowRect, lineLogical), state.leadingAlignment() | Qt::AlignVCenter,
                              PDFComponentPainter::elidedText(text, font, width), font, PDFStylePainter::color(color));
        *top += PDFAppToolImageListDelegate::TEXT_LINE + PDFAppToolImageListDelegate::LINE_GAP;
    }

    static QString getPixelText(const PDFDocumentImageList::Item& item)
    {
        return PDFAppToolImageListModel::tr("%1 × %2 px").arg(PDFAppLocale::formatNumber(item.pixelSize.width()), PDFAppLocale::formatNumber(item.pixelSize.height()));
    }
};

void PDFAppToolImageDecodeJob::run()
{
    for (const PDFAppToolImageDecoder::Request& request : requests)
    {
        pictures.append(isOperationCanceled() ? QImage() : PDFAppToolImageDecoder::decodeImage(document.data(), cms, request.item, maximumSide, this));
    }
}

// ---------------------------------------------------------- PDFAppToolImageDecoder

PDFAppToolImageDecoder::PDFAppToolImageDecoder(QObject* parent) :
    BaseClass(parent),
    m_link(std::make_shared<PDFAppToolImageDecodeLink>(this))
{

}

PDFAppToolImageDecoder::~PDFAppToolImageDecoder()
{
    // The decodings go on with their own values, their late results are dropped; nobody waits
    m_link->detach();
    cancelAll();
}

quint64 PDFAppToolImageDecoder::decode(PDFDocumentPointer document, PDFCMSPointer cms, std::vector<Request> requests, int maximumSide)
{
    // Checklist point 1: the job gets copies of plain values (the shared immutable document,
    // the color management, the keys), never the decoder or its user
    std::shared_ptr<PDFAppToolImageDecodeJob> job = std::make_shared<PDFAppToolImageDecodeJob>();
    job->link = m_link;
    job->ticket = m_nextTicket++;
    job->document = std::move(document);
    job->cms = std::move(cms);
    job->maximumSide = maximumSide;
    job->requests = std::move(requests);
    m_jobs[job->ticket] = job;
    const quint64 ticket = job->ticket;
    PDFExecutionPolicy::startTask(QThreadPool::globalInstance(), PDFAppToolImageDecodeTask(std::move(job)));
    return ticket;
}

void PDFAppToolImageDecoder::cancelAll()
{
    for (const auto& item : m_jobs)
    {
        item.second->cancel();
    }
    m_jobs.clear();
}

QImage PDFAppToolImageDecoder::decodeImage(const PDFDocument* document, const PDFCMSPointer& cms, const PDFDocumentImageList::Item& item, int maximumSide, const PDFOperationControl* control)
{
    if (!document || !item.key.isValid())
    {
        return QImage();
    }

    // The one decoding of an image of the list of the core (as Extract Images writes it)
    QString errorMessage;
    return PDFAppToolImageListHelper::limitSize(PDFDocumentToolExtractImages::readImage(document, item, cms, control, &errorMessage), maximumSide);
}

void PDFAppToolImageDecoder::onJobFinished(const std::shared_ptr<PDFAppToolImageDecodeJob>& job)
{
    PDFThreadAnnotations::happensAfter(job.get());
    PDFThreadAnnotations::forget(job.get());

    // A canceled decoding is not in the map any more: its result is dropped
    auto it = m_jobs.find(job->ticket);
    if (it == m_jobs.end() || it->second != job)
    {
        return;
    }
    m_jobs.erase(it);
    Q_EMIT imagesDecoded(job->ticket, job->pictures);
}

// ---------------------------------------------------------- PDFAppToolImageListModel

PDFAppToolImageListModel::PDFAppToolImageListModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolImageListModel::~PDFAppToolImageListModel() = default;

void PDFAppToolImageListModel::setImageList(std::shared_ptr<const PDFDocumentImageList> list, quint64 generation)
{
    beginResetModel();
    m_list = std::move(list);
    m_generation = generation;
    const size_t count = m_list ? m_list->getItems().size() : 0;
    m_states.assign(count, RowState());
    m_thumbnails.assign(count, QImage());
    m_hasThumbnail.assign(count, false);
    endResetModel();
}

void PDFAppToolImageListModel::setRowStyle(RowStyle rowStyle)
{
    if (m_rowStyle == rowStyle)
    {
        return;
    }

    beginResetModel();
    m_rowStyle = rowStyle;
    endResetModel();
}

void PDFAppToolImageListModel::setRowState(const PDFDocumentImageList::Key& key, const RowState& state)
{
    const int row = findRow(key);
    if (row < 0 || m_states[size_t(row)] == state)
    {
        return;
    }

    m_states[size_t(row)] = state;
    const QModelIndex changed = index(row, 0);
    Q_EMIT dataChanged(changed, changed);
}

void PDFAppToolImageListModel::setRowStates(std::vector<RowState> states)
{
    states.resize(m_states.size());
    if (states == m_states)
    {
        return;
    }

    m_states = std::move(states);
    if (!m_states.empty())
    {
        Q_EMIT dataChanged(index(0, 0), index(int(m_states.size()) - 1, 0));
    }
}

PDFAppToolImageListModel::RowState PDFAppToolImageListModel::getRowState(int row) const
{
    return row >= 0 && size_t(row) < m_states.size() ? m_states[size_t(row)] : RowState();
}

void PDFAppToolImageListModel::setThumbnail(const PDFDocumentImageList::Key& key, quint64 generation, const QImage& thumbnail)
{
    if (generation != m_generation)
    {
        return;
    }

    const int row = findRow(key);
    if (row < 0)
    {
        return;
    }

    m_thumbnails[size_t(row)] = thumbnail;
    m_hasThumbnail[size_t(row)] = true;
    const QModelIndex changed = index(row, 0);
    Q_EMIT dataChanged(changed, changed, { ThumbnailRole });
}

bool PDFAppToolImageListModel::hasThumbnail(int row) const
{
    return row >= 0 && size_t(row) < m_hasThumbnail.size() && m_hasThumbnail[size_t(row)];
}

int PDFAppToolImageListModel::findRow(const PDFDocumentImageList::Key& key) const
{
    return m_list ? m_list->indexOf(key) : -1;
}

const PDFDocumentImageList::Item* PDFAppToolImageListModel::getItem(int row) const
{
    if (!m_list || row < 0 || size_t(row) >= m_list->getItems().size())
    {
        return nullptr;
    }
    return &m_list->getItems()[size_t(row)];
}

QString PDFAppToolImageListModel::getTagText(Tag tag)
{
    switch (tag)
    {
        case Tag::None:
            return QString();
        case Tag::Override:
            return tr("override");
        case Tag::KeepOriginal:
            return tr("will keep original");
        case Tag::Excluded:
            return tr("excluded");
        case Tag::AlreadyGray:
            return tr("already gray");
        case Tag::Bitonal:
            return tr("bitonal");
        case Tag::InlineKept:
            return tr("inline · kept");
        case Tag::AppearanceKept:
            return tr("in an annotation · kept");
        case Tag::HiddenLayer:
            return tr("hidden layer");
        case Tag::CannotBeRead:
            return tr("cannot be read");
        case Tag::TooLarge:
            return tr("too large");
        case Tag::LeftOut:
            return tr("left out");
    }
    return QString();
}

QString PDFAppToolImageListModel::getFirstLine(const PDFDocumentImageList::Item& item) const
{
    if (m_rowStyle == RowStyle::Export)
    {
        // 341:7676 "Page 1 · 2480 × 3508 px", 341:7708 "Pages 308, 954 · 1654 × 620 px"
        const QString pages = PDFAppLocale::formatPageNumbers(item.pageIndices, 3);
        const QString pagesText = item.pageIndices.size() == 1 ? tr("Page %1").arg(pages) : tr("Pages %1").arg(pages);
        return tr("%1 · %2").arg(pagesText, PDFAppToolImageListHelper::getPixelText(item));
    }

    // 297:3663 "2480 × 3508 px · 300 dpi"
    const int dpi = PDFAppToolImageListHelper::getDpi(item);
    if (dpi <= 0)
    {
        return PDFAppToolImageListHelper::getPixelText(item);
    }
    return tr("%1 · %2 dpi").arg(PDFAppToolImageListHelper::getPixelText(item), PDFAppLocale::formatNumber(dpi));
}

QString PDFAppToolImageListModel::getSecondLine(const PDFDocumentImageList::Item& item) const
{
    // 297:3664 "DeviceRGB · DCTDecode"; a stencil mask has no color space (8.9.6.2)
    QString colorSpace = item.colorSpaceName;
    if (item.isImageMask)
    {
        colorSpace = tr("Image mask");
    }
    else if (colorSpace.isEmpty())
    {
        colorSpace = tr("No color space");
    }

    const QString filter = item.filterName.isEmpty() ? tr("not compressed") : item.filterName;
    return tr("%1 · %2").arg(colorSpace, filter);
}

int PDFAppToolImageListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid() || !m_list)
    {
        return 0;
    }
    return int(m_list->getItems().size());
}

QVariant PDFAppToolImageListModel::data(const QModelIndex& index, int role) const
{
    const PDFDocumentImageList::Item* item = index.isValid() && index.model() == this ? getItem(index.row()) : nullptr;
    if (!item)
    {
        return QVariant();
    }

    const size_t row = size_t(index.row());
    const RowState& state = m_states[row];
    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return tr("Image %1").arg(PDFAppLocale::formatNumber(index.row() + 1));
        case KeyRole:
            return item->key.toString();
        case NumberRole:
            return index.row() + 1;
        case TagRole:
            return int(state.tag);
        case TagTextRole:
            return state.badgeText.isEmpty() ? getTagText(state.tag) : state.badgeText;
        case FirstLineRole:
            return getFirstLine(*item);
        case SecondLineRole:
            return m_rowStyle == RowStyle::Export ? (state.sizeText.isEmpty() ? QString() : state.sizeText) : getSecondLine(*item);
        case SizeTextRole:
            return state.sizeText.isEmpty() ? PDFDocumentToolFormat::formatSize(item->streamBytes + item->maskBytes) : state.sizeText;
        case MarkedRole:
            return state.isMarked;
        case DimmedRole:
            return state.isDimmed;
        case ThumbnailRole:
            return m_thumbnails[row];
        case BytesRole:
            return qint64(item->streamBytes + item->maskBytes);
        case ImageColorRole:
            return int(item->color);
        case IndexRole:
            return index.row();
        case Qt::ToolTipRole:
            return tr("%1 · %2").arg(getFirstLine(*item), getSecondLine(*item));
        default:
            break;
    }
    return QVariant();
}

// ---------------------------------------------------------- PDFAppToolImageListProxyModel

PDFAppToolImageListProxyModel::PDFAppToolImageListProxyModel(QObject* parent) :
    BaseClass(parent)
{
    setDynamicSortFilter(true);
}

PDFAppToolImageListProxyModel::~PDFAppToolImageListProxyModel() = default;

void PDFAppToolImageListProxyModel::setChipId(const QString& chipId)
{
    if (m_chipId != chipId)
    {
        beginFilterChange();
        m_chipId = chipId;
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }
}

void PDFAppToolImageListProxyModel::setSort(Sort sort)
{
    if (m_sort != sort)
    {
        m_sort = sort;
        invalidate();
        this->sort(0, Qt::AscendingOrder);
    }
}

bool PDFAppToolImageListProxyModel::isAcceptedByChip(const QString& chipId, const QModelIndex& sourceIndex)
{
    using Model = PDFAppToolImageListModel;
    using Tag = Model::Tag;

    if (chipId.isEmpty() || chipId == QLatin1String("all"))
    {
        return true;
    }

    const Tag tag = Tag(sourceIndex.data(Model::TagRole).toInt());
    if (chipId == QLatin1String("overrides"))
    {
        return tag == Tag::Override;
    }
    if (chipId == QLatin1String("excluded"))
    {
        return tag == Tag::Excluded || tag == Tag::LeftOut;
    }
    if (chipId == QLatin1String("color"))
    {
        return PDFDocumentImageList::ImageColor(sourceIndex.data(Model::ImageColorRole).toInt()) == PDFDocumentImageList::ImageColor::Color;
    }
    if (chipId == QLatin1String("export"))
    {
        return sourceIndex.data(Model::MarkedRole).toBool();
    }
    return true;
}

void PDFAppToolImageListProxyModel::setPageFilter(std::optional<std::set<PDFInteger>> pages)
{
    if (m_pages != pages)
    {
        beginFilterChange();
        m_pages = std::move(pages);
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
    }
}

bool PDFAppToolImageListProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const PDFAppToolImageListModel* model = qobject_cast<const PDFAppToolImageListModel*>(sourceModel());
    if (!model || !isAcceptedByChip(m_chipId, model->index(sourceRow, 0, sourceParent)))
    {
        return false;
    }

    if (m_pages)
    {
        const PDFDocumentImageList::Item* item = model->getItem(sourceRow);
        if (!item)
        {
            return false;
        }
        for (PDFInteger pageIndex : item->pageIndices)
        {
            if (m_pages->count(pageIndex))
            {
                return true;
            }
        }
        return false;
    }
    return true;
}

bool PDFAppToolImageListProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    using Model = PDFAppToolImageListModel;

    const int leftIndex = left.data(Model::IndexRole).toInt();
    const int rightIndex = right.data(Model::IndexRole).toInt();
    if (m_sort != Sort::Document)
    {
        const qint64 leftBytes = left.data(Model::BytesRole).toLongLong();
        const qint64 rightBytes = right.data(Model::BytesRole).toLongLong();
        if (leftBytes != rightBytes)
        {
            return m_sort == Sort::Largest ? leftBytes > rightBytes : leftBytes < rightBytes;
        }
    }
    return leftIndex < rightIndex;
}

// ---------------------------------------------------------- PDFAppToolImageListDelegate

PDFAppToolImageListDelegate::PDFAppToolImageListDelegate(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppToolImageListDelegate::~PDFAppToolImageListDelegate() = default;

QSize PDFAppToolImageListDelegate::getThumbnailSize(PDFAppToolImageListModel::RowStyle style)
{
    return style == PDFAppToolImageListModel::RowStyle::Export ? QSize(56, 43) : QSize(64, 50);
}

QRectF PDFAppToolImageListDelegate::getMarkRect(const QRectF& rowRect, Qt::LayoutDirection direction)
{
    // icon/check 341:7670: 16 x 16 at the trailing edge, the padding 8, centered vertically
    const qreal left = direction == Qt::RightToLeft ? rowRect.left() + PADDING : rowRect.right() - PADDING - MARK_SIZE;
    return QRectF(left, rowRect.center().y() - MARK_SIZE * 0.5, MARK_SIZE, MARK_SIZE);
}

PDFAppToolImageListModel::RowStyle PDFAppToolImageListDelegate::getRowStyle(const QModelIndex& index)
{
    const QAbstractItemModel* model = index.model();
    while (const QAbstractProxyModel* proxy = qobject_cast<const QAbstractProxyModel*>(model))
    {
        model = proxy->sourceModel();
    }
    const PDFAppToolImageListModel* imageModel = qobject_cast<const PDFAppToolImageListModel*>(model);
    return imageModel ? imageModel->getRowStyle() : PDFAppToolImageListModel::RowStyle::Optimize;
}

QSize PDFAppToolImageListDelegate::getItemSize(const QStyleOptionViewItem& option, const QModelIndex& index, int width) const
{
    Q_UNUSED(option);

    // 297:3651 73 (79 with a Tag), 341:7672 59 (65 with a Tag)
    const PDFAppToolImageListModel::RowStyle style = getRowStyle(index);
    const bool hasTag = !getString(index, PDFAppToolImageListModel::TagTextRole).isEmpty();
    const int lines = style == PDFAppToolImageListModel::RowStyle::Export ? 2 : 3;
    const int textHeight = (hasTag ? TAG_LINE : NAME_LINE) + lines * (LINE_GAP + TEXT_LINE);
    const int height = 2 * PADDING + qMax(textHeight, getThumbnailSize(style).height());
    return QSize(width > 0 ? width : 275, height);
}

void PDFAppToolImageListDelegate::paintItem(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const PDFComponentPainter::State& state) const
{
    using Model = PDFAppToolImageListModel;
    using Color = PDFThemeColors::Color;

    const PDFStylePainterGuard guard(painter);
    painter->setRenderHint(QPainter::Antialiasing, true);

    const QRectF rect(option.rect);
    const PDFStylePainter::Radii radii(RADIUS);
    const bool isEnabled = state.isEnabled();
    const bool isSelected = state.isSelected();
    const bool isDimmed = getBool(index, Model::DimmedRole, false) || !isEnabled;

    // The row: bg/accent-subtle when selected (297:3651), bg/hover / bg/pressed by the pointer
    if (isSelected)
    {
        PDFStylePainter::fill(painter, rect, radii, state.isPressed() ? Color::BgAccentSubtlePressed : (state.isHovered() ? Color::BgAccentSubtleHover : Color::BgAccentSubtle));
    }
    else if (state.isPressed())
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgPressed);
    }
    else if (state.isHovered())
    {
        PDFStylePainter::fill(painter, rect, radii, Color::BgHover);
    }

    const Model::RowStyle style = getRowStyle(index);
    const bool isExport = style == Model::RowStyle::Export;

    // thumb 297:3652: the picture on the paper in its frame (border/strong), centered vertically
    const QSize thumbnailSize = getThumbnailSize(style);
    const QRectF thumbLogical(rect.left() + PADDING, rect.center().y() - thumbnailSize.height() * 0.5, thumbnailSize.width(), thumbnailSize.height());
    const QRectF thumbRect = state.visualRect(rect, thumbLogical);
    const PDFStylePainter::Radii thumbRadii(4.48);
    PDFStylePainter::fill(painter, thumbRect, thumbRadii, Color::SampleWhite);
    const QImage thumbnail = index.data(Model::ThumbnailRole).value<QImage>();
    if (!thumbnail.isNull())
    {
        const QRectF room = thumbRect.adjusted(2, 2, -2, -2);
        const QSizeF size = QSizeF(thumbnail.size()).scaled(room.size(), Qt::KeepAspectRatio);
        const QRectF target = PDFStylePainter::centered(room, size);
        const PDFStylePainterGuard imageGuard(painter);
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        if (isDimmed)
        {
            painter->setOpacity(0.5);
        }
        painter->drawImage(target, thumbnail);
    }
    else
    {
        PDFStylePainter::icon(painter, PDFStylePainter::centered(thumbRect, QSizeF(16, 16)), QStringLiteral("image"), isEnabled ? Color::IconMuted : Color::IconDisabled);
    }
    PDFStylePainter::strokeInside(painter, thumbRect, thumbRadii, Color::BorderStrong, 0.56);

    // text 297:3660: the lines from the top of the text, gap 1
    qreal textLeft = thumbLogical.right() + GAP;
    qreal textRight = rect.right() - PADDING;
    if (isExport)
    {
        textRight -= MARK_SIZE + GAP;
    }
    const qreal textWidth = qMax(qreal(1.0), textRight - textLeft);
    const QString tagText = getString(index, Model::TagTextRole);
    const int lines = isExport ? 2 : 3;
    const qreal textHeight = (tagText.isEmpty() ? NAME_LINE : TAG_LINE) + lines * (LINE_GAP + TEXT_LINE);
    qreal top = rect.top() + qMax(qreal(PADDING), (rect.height() - textHeight) * 0.5);

    const Qt::Alignment leading = state.leadingAlignment() | Qt::AlignVCenter;
    const QFont nameFont = PDFThemeFonts::getFont(QFont::DemiBold, 12);
    const QFont lineFont = PDFThemeFonts::getFont(QFont::Normal, 11);
    const QFont sizeFont = PDFThemeFonts::getFont(QFont::Medium, 11);

    // top 297:3676: the name and the Tag (gap 6); the line has the height of the Tag
    const qreal topLine = tagText.isEmpty() ? NAME_LINE : TAG_LINE;
    const QString name = getString(index, Qt::DisplayRole);
    const qreal nameWidth = qMin(textWidth, PDFStylePainter::textWidth(name, nameFont) + 1.0);
    const QRectF nameLogical(textLeft, top, nameWidth, topLine);
    PDFStylePainter::text(painter, state.visualRect(rect, nameLogical), leading, PDFComponentPainter::elidedText(name, nameFont, nameWidth),
                          nameFont, PDFStylePainter::color(isDimmed ? Color::TextDisabled : Color::TextPrimary));
    if (!tagText.isEmpty())
    {
        const PDFComponentPainter::Tag tag{ tagText };
        const QSize tagSize = PDFComponentPainter::getTagSize(tag);
        const qreal tagLeft = nameLogical.right() + TAG_GAP;
        const qreal tagWidth = qMin(qreal(tagSize.width()), textLeft + textWidth - tagLeft);
        if (tagWidth > 8.0)
        {
            const QRectF tagLogical(tagLeft, top + (topLine - tagSize.height()) * 0.5, tagWidth, tagSize.height());
            PDFComponentPainter::State tagState(isEnabled ? QStyle::State_Enabled : QStyle::State_None, state.direction);
            PDFComponentPainter::paintTag(painter, state.visualRect(rect, tagLogical), tag, tagState);
        }
    }
    top += topLine + LINE_GAP;

    // dimensions 297:3663 text/tertiary, format 297:3664 text/muted, size 297:3665 Medium
    // text/secondary (text/accent in the selected row); Export: the file name text/muted
    using Helper = PDFAppToolImageListHelper;
    Helper::drawLine(painter, state, rect, textLeft, textWidth, &top, getString(index, Model::FirstLineRole), lineFont, isDimmed ? Color::TextDisabled : Color::TextTertiary);
    Helper::drawLine(painter, state, rect, textLeft, textWidth, &top, getString(index, Model::SecondLineRole), lineFont, isDimmed ? Color::TextDisabled : Color::TextMuted);
    if (!isExport)
    {
        Helper::drawLine(painter, state, rect, textLeft, textWidth, &top, getString(index, Model::SizeTextRole), sizeFont,
                         isDimmed ? Color::TextDisabled : (isSelected ? Color::TextAccent : Color::TextSecondary));
    }

    // icon/check 341:7670: the image is exported
    if (isExport && getBool(index, Model::MarkedRole, true))
    {
        PDFStylePainter::icon(painter, getMarkRect(rect, state.direction), QStringLiteral("check"), isEnabled ? Color::IconAccent : Color::IconDisabled);
    }

    if (state.hasFocusRing())
    {
        PDFComponentPainter::paintFocusRingInside(painter, rect, radii);
    }
}

// ---------------------------------------------------------- PDFAppToolImageList

PDFAppToolImageList::PDFAppToolImageList(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("toolImageList"));

    m_decoder = new PDFAppToolImageDecoder(this);
    connect(m_decoder.data(), &PDFAppToolImageDecoder::imagesDecoded, this, &PDFAppToolImageList::onThumbnailsDecoded);
    m_model = new PDFAppToolImageListModel(this);
    m_proxy = new PDFAppToolImageListProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->sort(0, Qt::AscendingOrder);
    m_delegate = new PDFAppToolImageListDelegate(this);

    // sidebar-left 297:3170: the gap 10
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    // header 297:3620: the title Semi Bold 14 and the subtitle Regular 11 (gap 1), Sort 126 x 32
    QWidget* header = new QWidget(this);
    header->setObjectName(QStringLiteral("toolImageListHeader"));
    QHBoxLayout* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);
    QWidget* titles = new QWidget(header);
    QVBoxLayout* titlesLayout = new QVBoxLayout(titles);
    titlesLayout->setContentsMargins(0, 0, 0, 0);
    titlesLayout->setSpacing(1);
    m_title = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 14, PDFThemeColors::TextPrimary, titles);
    m_title->setObjectName(QStringLiteral("toolImageListTitle"));
    m_head = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 11, PDFThemeColors::TextTertiary, titles);
    m_head->setObjectName(QStringLiteral("toolImageListHead"));
    m_head->setMinimumWidth(1);
    titlesLayout->addWidget(m_title);
    titlesLayout->addWidget(m_head);
    headerLayout->addWidget(titles, 1);
    m_sort = new PDFComboBox(header);
    m_sort->setObjectName(QStringLiteral("toolImageListSort"));
    m_sort->setFixedSize(SORT_WIDTH, CONTROL_HEIGHT);
    m_sort->addItem(QString(), int(PDFAppToolImageListProxyModel::Sort::Document));
    m_sort->addItem(QString(), int(PDFAppToolImageListProxyModel::Sort::Largest));
    m_sort->addItem(QString(), int(PDFAppToolImageListProxyModel::Sort::Smallest));
    connect(m_sort.data(), &QComboBox::currentIndexChanged, this, &PDFAppToolImageList::onSortIndexChanged);
    headerLayout->addWidget(m_sort, 0, Qt::AlignTop);
    layout->addWidget(header);

    // filters 297:3629: Filter chips 32 high, gap 6
    m_chipsWidget = new QWidget(this);
    m_chipsWidget->setObjectName(QStringLiteral("toolImageListChips"));
    QHBoxLayout* chipsLayout = new QHBoxLayout(m_chipsWidget);
    chipsLayout->setContentsMargins(0, 0, 0, 0);
    chipsLayout->setSpacing(CHIP_GAP);
    chipsLayout->addStretch(1);
    m_chipGroup = new QButtonGroup(this);
    m_chipGroup->setExclusive(true);
    connect(m_chipGroup.data(), &QButtonGroup::idToggled, this, &PDFAppToolImageList::onChipToggled);
    m_chipsWidget->hide();
    layout->addWidget(m_chipsWidget);

    // image-list 297:3650: the rows 2 apart
    m_view = new PDFAppPanelListView(this);
    m_view->setObjectName(QStringLiteral("toolImageListView"));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setItemDelegate(m_delegate);
    m_view->setModel(m_proxy);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setUniformItemSizes(false);
    m_view->viewport()->setAutoFillBackground(false);
    m_view->setSpacing(ROW_GAP / 2);
    m_view->setContentPaddings(QMargins(-ROW_GAP / 2, -ROW_GAP / 2, -ROW_GAP / 2, -ROW_GAP / 2));
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppToolImageList::onSelectionChanged);
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, &PDFAppToolImageList::onScrolled);
    connect(m_proxy.data(), &QAbstractItemModel::modelReset, this, &PDFAppToolImageList::onModelReset);
    connect(m_proxy.data(), &QAbstractItemModel::layoutChanged, this, &PDFAppToolImageList::onScrolled);
    connect(m_proxy.data(), &QAbstractItemModel::rowsInserted, this, &PDFAppToolImageList::onScrolled);
    connect(m_proxy.data(), &QAbstractItemModel::rowsRemoved, this, &PDFAppToolImageList::onScrolled);
    layout->addWidget(m_view, 1);

    // The state of a list without a shown row (341:8976 "No images on page 308")
    m_empty = PDFAppPageWidgets::createLabel(QString(), QFont::Normal, 12, PDFThemeColors::TextTertiary, this);
    m_empty->setObjectName(QStringLiteral("toolImageListEmpty"));
    m_empty->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_empty->setWordWrap(true);
    m_empty->hide();
    layout->addWidget(m_empty, 1);

    // tip 297:3764: Regular 11 text/muted
    m_tip = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 15, PDFThemeColors::TextMuted, this);
    m_tip->setObjectName(QStringLiteral("toolImageListTip"));
    m_tip->hide();
    layout->addWidget(m_tip);

    updateTexts();
    m_sort->setCurrentIndex(1);
    updateEmptyState();
}

PDFAppToolImageList::~PDFAppToolImageList()
{
    // The decoder (a child) cuts its link: the decodings go on with their own values, their late
    // thumbnails are dropped; nobody waits. It goes first: no result reaches a half-destroyed list.
    delete m_decoder.data();
}

QPushButton* PDFAppToolImageList::getChipButton(const QString& chipId) const
{
    for (size_t i = 0; i < m_chipIds.size() && i < m_chipButtons.size(); ++i)
    {
        if (m_chipIds[i] == chipId)
        {
            return m_chipButtons[i].data();
        }
    }
    return nullptr;
}

void PDFAppToolImageList::setRowStyle(PDFAppToolImageListModel::RowStyle style)
{
    m_model->setRowStyle(style);
}

void PDFAppToolImageList::setHeadText(const QString& text)
{
    m_head->setText(text);
}

void PDFAppToolImageList::setChips(const std::vector<Chip>& chips, const QString& currentChipId)
{
    // The same chips keep their buttons: only the counts change (no flicker of the focus)
    bool isSameIds = chips.size() == m_chipIds.size();
    for (size_t i = 0; isSameIds && i < chips.size(); ++i)
    {
        isSameIds = chips[i].id == m_chipIds[i];
    }

    m_isUpdatingChips = true;
    if (!isSameIds)
    {
        for (const QPointer<QPushButton>& button : m_chipButtons)
        {
            if (button)
            {
                m_chipGroup->removeButton(button);
                delete button.data();
            }
        }
        m_chipButtons.clear();
        m_chipIds.clear();

        QHBoxLayout* chipsLayout = qobject_cast<QHBoxLayout*>(m_chipsWidget->layout());
        for (size_t i = 0; i < chips.size(); ++i)
        {
            QPushButton* chip = new QPushButton(m_chipsWidget);
            chip->setObjectName(QStringLiteral("toolImageListChip-") + chips[i].id);
            chip->setProperty(PDFWidgetStyleProperties::CHIP, true);
            chip->setCheckable(true);
            chip->setAutoDefault(false);
            chip->setFixedHeight(CONTROL_HEIGHT);
            m_chipGroup->addButton(chip, int(i));
            chipsLayout->insertWidget(int(i), chip);
            m_chipButtons.push_back(chip);
            m_chipIds.push_back(chips[i].id);
        }
    }

    for (size_t i = 0; i < chips.size(); ++i)
    {
        QPushButton* chip = m_chipButtons[i];
        chip->setText(PDFAppPageWidgets::escapeMnemonic(chips[i].text));
        chip->setAccessibleName(chips[i].count >= 0 ? tr("%1 %2").arg(chips[i].text, PDFAppLocale::formatNumber(chips[i].count)) : chips[i].text);
        if (chips[i].count >= 0)
        {
            PDFWidgetStyleProperties::set(chip, PDFWidgetStyleProperties::CHIP_COUNT, chips[i].count);
        }
        chip->setChecked(chips[i].id == currentChipId);
    }
    m_chipsWidget->setVisible(!chips.empty());

    m_currentChipId = currentChipId;
    m_proxy->setChipId(currentChipId);
    m_isUpdatingChips = false;
    updateEmptyState();
}

void PDFAppToolImageList::setSortVisible(bool isVisible)
{
    m_sort->setVisible(isVisible);
    if (!isVisible)
    {
        setSort(PDFAppToolImageListProxyModel::Sort::Document);
    }
}

void PDFAppToolImageList::setSort(PDFAppToolImageListProxyModel::Sort sort)
{
    const int index = m_sort->findData(int(sort));
    if (index >= 0)
    {
        m_sort->setCurrentIndex(index);
    }
    m_proxy->setSort(sort);
}

void PDFAppToolImageList::setTipText(const QString& text)
{
    m_tip->setText(text);
    m_tip->setVisible(!text.isEmpty());
}

void PDFAppToolImageList::setEmptyText(const QString& text)
{
    m_emptyText = text;
    updateEmptyState();
}

std::vector<PDFDocumentImageList::Key> PDFAppToolImageList::getSelectedKeys() const
{
    std::vector<int> rows;
    const QModelIndexList selected = m_view->selectionModel() ? m_view->selectionModel()->selectedIndexes() : QModelIndexList();
    for (const QModelIndex& index : selected)
    {
        rows.push_back(m_proxy->mapToSource(index).row());
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    std::vector<PDFDocumentImageList::Key> keys;
    for (int row : rows)
    {
        if (const PDFDocumentImageList::Item* item = m_model->getItem(row))
        {
            keys.push_back(item->key);
        }
    }
    return keys;
}

void PDFAppToolImageList::setSelectedKeys(const std::vector<PDFDocumentImageList::Key>& keys)
{
    QItemSelectionModel* selectionModel = m_view->selectionModel();
    if (!selectionModel)
    {
        return;
    }

    const QSignalBlocker blocker(selectionModel);
    QItemSelection selection;
    QModelIndex first;
    for (const PDFDocumentImageList::Key& key : keys)
    {
        const QModelIndex index = m_proxy->mapFromSource(m_model->index(m_model->findRow(key), 0));
        if (index.isValid())
        {
            selection.select(index, index);
            if (!first.isValid())
            {
                first = index;
            }
        }
    }
    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect);
    selectionModel->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    if (first.isValid())
    {
        m_view->scrollTo(first);
    }
    m_view->viewport()->update();
}

PDFDocumentImageList::Key PDFAppToolImageList::getCurrentKey() const
{
    const QModelIndex current = m_view->selectionModel() ? m_view->selectionModel()->currentIndex() : QModelIndex();
    const PDFDocumentImageList::Item* item = current.isValid() && m_view->selectionModel()->isSelected(current) ? m_model->getItem(m_proxy->mapToSource(current).row()) : nullptr;
    if (item)
    {
        return item->key;
    }

    // The current row is not selected: the first selected one
    const std::vector<PDFDocumentImageList::Key> keys = getSelectedKeys();
    return keys.empty() ? PDFDocumentImageList::Key() : keys.front();
}

void PDFAppToolImageList::setThumbnailSource(PDFDocumentPointer document, PDFCMSPointer cms)
{
    m_decoder->cancelAll();
    m_pendingTicket = 0;
    m_pendingKeys.clear();
    m_thumbnailDocument = std::move(document);
    m_thumbnailCms = std::move(cms);
    m_requestedRows.clear();
    requestVisibleThumbnails();
}

bool PDFAppToolImageList::isThumbnailDecodingBusy() const
{
    return m_pendingTicket != 0;
}

void PDFAppToolImageList::requestVisibleThumbnails()
{
    if (m_pendingTicket != 0 || !m_thumbnailDocument || !m_model->getImageList() || !isVisible())
    {
        return;
    }

    // The visible rows of the view (and one screen below), at most MAXIMUM_JOB_ROWS of them
    std::vector<int> rows;
    const QRect viewportRect = m_view->viewport()->rect();
    const QRect area = viewportRect.adjusted(0, 0, 0, viewportRect.height());
    QModelIndex index = m_view->indexAt(QPoint(viewportRect.center().x(), viewportRect.top() + 1));
    if (!index.isValid() && m_proxy->rowCount() > 0)
    {
        index = m_proxy->index(0, 0);
    }
    while (index.isValid() && int(rows.size()) < PDFAppToolImageListHelper::MAXIMUM_JOB_ROWS)
    {
        const QRect rowRect = m_view->visualRect(index);
        if (rowRect.top() > area.bottom())
        {
            break;
        }
        const int sourceRow = m_proxy->mapToSource(index).row();
        if (sourceRow >= 0 && !m_requestedRows.count(sourceRow) && !m_model->hasThumbnail(sourceRow))
        {
            rows.push_back(sourceRow);
        }
        index = m_proxy->index(index.row() + 1, 0);
    }
    if (rows.empty())
    {
        return;
    }

    // The decoder gets the shared immutable document, the color management and the keys
    std::vector<PDFAppToolImageDecoder::Request> requests;
    m_pendingKeys.clear();
    for (int row : rows)
    {
        const PDFDocumentImageList::Item* item = m_model->getItem(row);
        PDFAppToolImageDecoder::Request request;
        request.item = *item;
        requests.push_back(std::move(request));
        m_pendingKeys.push_back(item->key);
        m_requestedRows.insert(row);
    }
    m_pendingGeneration = m_model->getGeneration();
    m_pendingTicket = m_decoder->decode(m_thumbnailDocument, m_thumbnailCms, std::move(requests), THUMBNAIL_SIDE);
}

void PDFAppToolImageList::onThumbnailsDecoded(quint64 ticket, const QList<QImage>& images)
{
    if (ticket != m_pendingTicket)
    {
        // A decoding of another source or of a list, which has been replaced
        return;
    }
    m_pendingTicket = 0;

    // A thumbnail of another generation is dropped by the model
    for (size_t i = 0; i < m_pendingKeys.size() && qsizetype(i) < images.size(); ++i)
    {
        m_model->setThumbnail(m_pendingKeys[i], m_pendingGeneration, images[qsizetype(i)]);
    }
    m_pendingKeys.clear();
    requestVisibleThumbnails();
}

void PDFAppToolImageList::toggleMarks(const std::vector<PDFDocumentImageList::Key>& keys)
{
    if (keys.empty())
    {
        return;
    }

    // All marked rows are left out; otherwise all of them are marked (341:7774 "Space leaves the
    // selected images out or puts them back")
    bool isAllMarked = true;
    for (const PDFDocumentImageList::Key& key : keys)
    {
        isAllMarked = isAllMarked && m_model->getRowState(m_model->findRow(key)).isMarked;
    }

    // From the event loop: the key or the mouse event of the view is still being delivered (a slot
    // may delete the list)
    QMetaObject::invokeMethod(this, &PDFAppToolImageList::emitMarkToggled, Qt::QueuedConnection, keys, !isAllMarked);
}

void PDFAppToolImageList::emitMarkToggled(std::vector<PDFDocumentImageList::Key> keys, bool isMarked)
{
    Q_EMIT markToggled(keys, isMarked);
}

void PDFAppToolImageList::emitChipChanged()
{
    m_isChipChangePending = false;
    Q_EMIT chipChanged(m_currentChipId);
}

const PDFDocumentImageList::Item* PDFAppToolImageList::getMarkItemAt(const QMouseEvent* mouseEvent) const
{
    if (!m_view || mouseEvent->button() != Qt::LeftButton || m_model->getRowStyle() != PDFAppToolImageListModel::RowStyle::Export)
    {
        return nullptr;
    }

    const QModelIndex index = m_view->indexAt(mouseEvent->position().toPoint());
    if (!index.isValid())
    {
        return nullptr;
    }
    const QRectF markRect = PDFAppToolImageListDelegate::getMarkRect(QRectF(m_view->visualRect(index)), layoutDirection()).adjusted(-4, -4, 4, 4);
    return markRect.contains(mouseEvent->position()) ? m_model->getItem(m_proxy->mapToSource(index).row()) : nullptr;
}

bool PDFAppToolImageList::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event->type() == QEvent::KeyPress)
    {
        const QKeyEvent* keyEvent = static_cast<const QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Space && keyEvent->modifiers() == Qt::NoModifier)
        {
            toggleMarks(getSelectedKeys());
            return true;
        }
    }

    if (m_view && watched == m_view->viewport())
    {
        switch (event->type())
        {
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
            {
                // The press on the mark of an Export row does not select the row (RS4APP): the
                // click on the mark is the mark only
                if (getMarkItemAt(static_cast<const QMouseEvent*>(event)))
                {
                    return true;
                }
                break;
            }

            case QEvent::MouseButtonRelease:
            {
                // The mark of an Export row (341:7670) toggles the mark of its row only
                if (const PDFDocumentImageList::Item* item = getMarkItemAt(static_cast<const QMouseEvent*>(event)))
                {
                    // The click on the mark is the mark only (not the selection of the row)
                    toggleMarks({ item->key });
                    return true;
                }
                break;
            }

            case QEvent::Resize:
            case QEvent::Show:
                QMetaObject::invokeMethod(this, &PDFAppToolImageList::requestVisibleThumbnails, Qt::QueuedConnection);
                break;

            default:
                break;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

void PDFAppToolImageList::changeEvent(QEvent* event)
{
    BaseClass::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
    {
        updateTexts();
    }
}

void PDFAppToolImageList::updateTexts()
{
    m_title->setText(tr("Images"));
    m_sort->setSelectPrefix(tr("Sort:"));
    m_sort->setItemText(0, tr("Page order"));
    m_sort->setItemText(1, tr("Largest"));
    m_sort->setItemText(2, tr("Smallest"));
    m_sort->setAccessibleName(tr("Sort the images"));
    m_view->setAccessibleName(tr("Images"));
    updateEmptyState();
}

void PDFAppToolImageList::updateEmptyState()
{
    const bool isEmpty = m_proxy->rowCount() == 0;
    QString text = m_emptyText;
    if (text.isEmpty())
    {
        text = m_model->rowCount() == 0 ? tr("The document has no images.") : tr("No image is shown by this filter.");
    }
    m_empty->setText(text);
    m_empty->setVisible(isEmpty);
    m_view->setVisible(!isEmpty);
}

void PDFAppToolImageList::onChipToggled(int id, bool isChecked)
{
    if (!isChecked || m_isUpdatingChips || id < 0 || size_t(id) >= m_chipIds.size())
    {
        return;
    }

    m_currentChipId = m_chipIds[size_t(id)];
    m_proxy->setChipId(m_currentChipId);
    updateEmptyState();

    // The button of the chip is still inside of its click (toggled, then clicked): the signal
    // comes from the event loop
    if (!m_isChipChangePending)
    {
        m_isChipChangePending = true;
        QMetaObject::invokeMethod(this, &PDFAppToolImageList::emitChipChanged, Qt::QueuedConnection);
    }
}

void PDFAppToolImageList::onSortIndexChanged(int index)
{
    const QVariant sort = m_sort->itemData(index);
    if (sort.isValid())
    {
        m_proxy->setSort(PDFAppToolImageListProxyModel::Sort(sort.toInt()));
    }
}

void PDFAppToolImageList::onSelectionChanged()
{
    // The selection model of Qt is still inside of its change (QAbstractItemView::keyPressEvent sets
    // the current index after the selection): a slot, which deletes the list, would delete it under
    // Qt - the signal comes from the event loop, once for more changes
    if (!m_isSelectionChangePending)
    {
        m_isSelectionChangePending = true;
        QMetaObject::invokeMethod(this, &PDFAppToolImageList::emitSelectionChanged, Qt::QueuedConnection);
    }
}

void PDFAppToolImageList::emitSelectionChanged()
{
    m_isSelectionChangePending = false;
    Q_EMIT selectionChanged();
}

void PDFAppToolImageList::onScrolled()
{
    updateEmptyState();
    requestVisibleThumbnails();
}

void PDFAppToolImageList::onModelReset()
{
    // A new list (another generation): its thumbnails are decoded again
    m_decoder->cancelAll();
    m_pendingTicket = 0;
    m_pendingKeys.clear();
    m_requestedRows.clear();
    updateEmptyState();
    QMetaObject::invokeMethod(this, &PDFAppToolImageList::requestVisibleThumbnails, Qt::QueuedConnection);
}

}   // namespace pdf

#include "moc_pdfapptoolimagelist.cpp"
