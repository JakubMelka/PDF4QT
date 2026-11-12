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

#include "pdfappmanagecanvas.h"

#include "pdfappdocumenttoolbar.h"
#include "pdfapplocale.h"
#include "pdfapppagelayout.h"
#include "pdfapptoolmergemodel.h"
#include "pdfapptoolmergeview.h"

#include "pdfcomponentdelegates.h"
#include "pdfcomponentpainter.h"
#include "pdffluentwidgets.h"
#include "pdfthemefonts.h"
#include "pdfthememanager.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QPainter>
#include <QStackedLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdf
{

/// Texts and numbers of the canvas of Manage Pages
class PDFAppManageCanvasHelper
{
public:
    PDFAppManageCanvasHelper() = delete;

    static int toInt(qint64 value)
    {
        return int(qBound(qint64(0), value, qint64(std::numeric_limits<int>::max())));
    }

    /// The page of a source is a page of a PDF (not a blank page, not an image)
    static bool isPdfPage(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Page& page)
    {
        const std::vector<PDFDocumentAssembly::Source>& sources = assembly.getSources();
        return page.plan.input >= 0 && size_t(page.plan.input) < sources.size() && sources[size_t(page.plan.input)].kind == PDFDocumentAssembly::SourceKind::Pdf;
    }

    static bool isImagePage(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Page& page)
    {
        const std::vector<PDFDocumentAssembly::Source>& sources = assembly.getSources();
        return page.plan.input >= 0 && size_t(page.plan.input) < sources.size() && sources[size_t(page.plan.input)].kind == PDFDocumentAssembly::SourceKind::Image;
    }

    static bool isBlankPage(const PDFDocumentAssembly::Page& page)
    {
        return page.plan.input == PDFDocumentAssembly::BLANK_INPUT;
    }

    static const PDFDocumentAssembly::Source* getSource(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Page& page)
    {
        const std::vector<PDFDocumentAssembly::Source>& sources = assembly.getSources();
        return page.plan.input >= 0 && size_t(page.plan.input) < sources.size() ? &sources[size_t(page.plan.input)] : nullptr;
    }

    static bool isCropped(const PDFDocumentAssembly::Page& page)
    {
        const QMarginsF& margins = page.plan.cropMargins;
        return margins.left() > 0.0 || margins.top() > 0.0 || margins.right() > 0.0 || margins.bottom() > 0.0;
    }

    /// "1–5" of the original pages from 0 (a run), "3" of one page
    static QString formatRun(PDFInteger first, PDFInteger last)
    {
        if (first == last)
        {
            return PDFAppLocale::formatNumber(first + 1);
        }
        return PDFAppManagePageModel::tr("%1–%2").arg(PDFAppLocale::formatNumber(first + 1), PDFAppLocale::formatNumber(last + 1));
    }

    /// The pages of the item are one ascending run of one PDF source: its first and last page
    static bool getRun(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item, int* input, PDFInteger* first, PDFInteger* last)
    {
        if (item.pages.empty() || !isPdfPage(assembly, item.pages.front()))
        {
            return false;
        }
        const PDFDocumentAssembly::Page& front = item.pages.front();
        for (size_t i = 1; i < item.pages.size(); ++i)
        {
            const PDFDocumentAssembly::Page& page = item.pages[i];
            if (page.plan.input != front.plan.input || page.plan.pageIndex != front.plan.pageIndex + PDFInteger(i))
            {
                return false;
            }
        }
        *input = front.plan.input;
        *first = front.plan.pageIndex;
        *last = item.pages.back().plan.pageIndex;
        return true;
    }

    /// The pages of the item come from one source: its index, else -1
    static int getSingleSource(const PDFDocumentAssembly::Item& item)
    {
        int input = item.pages.empty() ? -1 : item.pages.front().plan.input;
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            if (page.plan.input != input)
            {
                return -1;
            }
        }
        return input;
    }

    /// "rotated 90°" (387:12569)
    static QString getTurnCaption(PageRotation turn)
    {
        switch (turn)
        {
            case PageRotation::Rotate90:
                return PDFAppManagePageModel::tr("rotated 90°");
            case PageRotation::Rotate180:
                return PDFAppManagePageModel::tr("rotated 180°");
            case PageRotation::Rotate270:
                return PDFAppManagePageModel::tr("rotated 270°");
            default:
                break;
        }
        return QString();
    }

    /// The content of a QVariant of a size
    static QSizeF turned(QSizeF size, PageRotation turn)
    {
        if (turn == PageRotation::Rotate90 || turn == PageRotation::Rotate270)
        {
            size.transpose();
        }
        return size;
    }
};

// ------------------------------------------------------------- PDFAppManagePageModel

PDFAppManagePageModel::PDFAppManagePageModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppManagePageModel::~PDFAppManagePageModel() = default;

void PDFAppManagePageModel::setContent(PDFDocumentAssembly assembly,
                                       std::vector<PDFAppManageSourceView> sources,
                                       const std::set<QString>& shownItems,
                                       bool isFiltered,
                                       bool isDocumentHost,
                                       std::set<PDFObjectReference> newTargetPages)
{
    beginResetModel();
    m_assembly = std::move(assembly);
    m_sources = std::move(sources);
    m_isDocumentHost = isDocumentHost;
    m_newTargetPages = std::move(newTargetPages);
    createRows(shownItems, isFiltered);
    m_groups = createGroups();
    m_rowGroups.assign(m_rows.size(), -1);
    for (size_t group = 0; group < m_groups.size(); ++group)
    {
        for (int i = 0; i < m_groups[group].rowCount; ++i)
        {
            m_rowGroups[size_t(m_groups[group].firstRow + i)] = int(group);
        }
    }

    // The selection keeps the items, which are still there
    std::set<QString> selected;
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        if (m_selectedItems.count(item.id))
        {
            selected.insert(item.id);
        }
    }
    m_selectedItems = std::move(selected);

    // The pictures of the old content are no pictures of the new one (the keys differ, the
    // generation drops the cache of the model)
    invalidateAll();
    endResetModel();

    // Last: a slot may delete the view (the contract of the grouped model)
    Q_EMIT groupsChanged();
}

void PDFAppManagePageModel::createRows(const std::set<QString>& shownItems, bool isFiltered)
{
    m_rows.clear();
    const std::vector<PDFDocumentAssembly::Item>& items = m_assembly.getItems();
    int position = 0;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const PDFDocumentAssembly::Item& item = items[i];
        const bool isShown = !isFiltered || shownItems.count(item.id);
        for (size_t page = 0; page < item.pages.size(); ++page)
        {
            if (isShown)
            {
                m_rows.push_back(Row{ int(i), int(page), position });
            }
            ++position;
        }
    }
}

std::vector<PDFAppManagePageModel::Group> PDFAppManagePageModel::createGroups() const
{
    // A group item is a card; consecutive single pages lie on the stage (387:12569)
    std::vector<Group> groups;
    const std::vector<PDFDocumentAssembly::Item>& items = m_assembly.getItems();
    size_t row = 0;
    while (row < m_rows.size())
    {
        const int itemIndex = m_rows[row].item;
        const PDFDocumentAssembly::Item& item = items[size_t(itemIndex)];
        Group group;
        group.firstRow = int(row);
        if (item.isGroup())
        {
            // 387:12807: icon/group 14, "Introduction" Semi Bold 12, "Group · 5 pages · Contract.pdf 1–5"
            size_t end = row;
            while (end < m_rows.size() && m_rows[end].item == itemIndex)
            {
                ++end;
            }
            group.inputId = item.id;
            group.rowCount = int(end - row);
            group.iconName = QStringLiteral("group");
            group.title = PDFAppManageItemModel::getItemName(m_assembly, item);
            group.subtitle = getGroupSubtitle(item);
            row = end;
        }
        else
        {
            // The single pages until the next group: the loose tiles with their captions (Small
            // 112 x 144: the square box of SquareSlotRole, 387:12937)
            size_t end = row;
            while (end < m_rows.size() && !items[size_t(m_rows[end].item)].isGroup())
            {
                ++end;
            }
            group.rowCount = int(end - row);
            group.isPlain = true;
            group.hasTileCaptions = true;
            row = end;
        }
        groups.push_back(std::move(group));
    }
    return groups;
}

QString PDFAppManagePageModel::getGroupSubtitle(const PDFDocumentAssembly::Item& item) const
{
    using Helper = PDFAppManageCanvasHelper;

    // "Group · 5 pages · Contract.pdf 1–5" (387:12811); more sources "Group · 7 pages · 2 sources"
    const int pageCount = Helper::toInt(qint64(item.pages.size()));
    const QString pages = pageCount == 1 ? tr("%Ln page", nullptr, pageCount) : tr("%Ln pages", nullptr, pageCount);
    int input = -1;
    PDFInteger first = 0;
    PDFInteger last = 0;
    QString source;
    if (Helper::getRun(m_assembly, item, &input, &first, &last))
    {
        source = tr("%1 %2").arg(m_assembly.getSources()[size_t(input)].name, Helper::formatRun(first, last));
    }
    else if (const int single = Helper::getSingleSource(item); single >= 0 && size_t(single) < m_assembly.getSources().size())
    {
        source = m_assembly.getSources()[size_t(single)].name;
    }
    else
    {
        std::set<int> inputs;
        for (const PDFDocumentAssembly::Page& page : item.pages)
        {
            inputs.insert(page.plan.input);
        }
        const int count = int(inputs.size());
        source = count == 1 ? tr("%Ln source", nullptr, count) : tr("%Ln sources", nullptr, count);
    }
    return tr("Group · %1 · %2").arg(pages, source);
}

void PDFAppManagePageModel::setSelectedItems(const std::set<QString>& itemIds)
{
    // One change of the range of the changed rows
    int first = -1;
    int last = -1;
    for (size_t row = 0; row < m_rows.size(); ++row)
    {
        const QString& id = m_assembly.getItems()[size_t(m_rows[row].item)].id;
        if (bool(m_selectedItems.count(id)) != bool(itemIds.count(id)))
        {
            first = first < 0 ? int(row) : first;
            last = int(row);
        }
    }
    m_selectedItems = itemIds;
    if (first >= 0)
    {
        Q_EMIT dataChanged(index(first, 0), index(last, 0), { PDFComponentRoles::PageTileStateRole });
    }
}

int PDFAppManagePageModel::getItemIndex(int row) const
{
    return row >= 0 && size_t(row) < m_rows.size() ? m_rows[size_t(row)].item : -1;
}

QString PDFAppManagePageModel::getItemId(int row) const
{
    const int item = getItemIndex(row);
    return item >= 0 ? m_assembly.getItems()[size_t(item)].id : QString();
}

std::vector<int> PDFAppManagePageModel::getRowsOfItem(const QString& itemId) const
{
    std::vector<int> rows;
    for (size_t row = 0; row < m_rows.size(); ++row)
    {
        if (m_assembly.getItems()[size_t(m_rows[row].item)].id == itemId)
        {
            rows.push_back(int(row));
        }
    }
    return rows;
}

const PDFDocumentAssembly::Page* PDFAppManagePageModel::getPage(int row) const
{
    if (row < 0 || size_t(row) >= m_rows.size())
    {
        return nullptr;
    }
    const Row& item = m_rows[size_t(row)];
    return &m_assembly.getItems()[size_t(item.item)].pages[size_t(item.page)];
}

int PDFAppManagePageModel::getPosition(int row) const
{
    return row >= 0 && size_t(row) < m_rows.size() ? m_rows[size_t(row)].position : -1;
}

QSizeF PDFAppManagePageModel::getPageSize(int row) const
{
    const PDFDocumentAssembly::Page* page = getPage(row);
    if (!page)
    {
        return QSizeF();
    }

    // The rule of the core (the sizes of the source pages, the crop, the turn), else the size of
    // the document or the image of the source view
    return getPageSize(m_assembly, *page, m_sources);
}

QSizeF PDFAppManagePageModel::getPageSize(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Page& page, const std::vector<PDFAppManageSourceView>& sources)
{
    const QSizeF size = PDFDocumentAssembly::getPageSize(assembly, page);
    return size.isValid() && !size.isEmpty() ? size : getPageSize(page, sources);
}

QSizeF PDFAppManagePageModel::getPageSize(const PDFDocumentAssembly::Page& page, const std::vector<PDFAppManageSourceView>& sources)
{
    QSizeF size;
    if (page.plan.input == PDFDocumentAssembly::BLANK_INPUT)
    {
        size = page.plan.pageSize;
    }
    else if (page.plan.input >= 0 && size_t(page.plan.input) < sources.size())
    {
        const PDFAppManageSourceView& source = sources[size_t(page.plan.input)];
        if (source.document)
        {
            const PDFCatalog* catalog = source.document->getCatalog();
            if (page.plan.pageIndex >= 0 && size_t(page.plan.pageIndex) < catalog->getPageCount())
            {
                // The visible area as the user sees it (the crop box with its rotation)
                const PDFPage* pdfPage = catalog->getPage(size_t(page.plan.pageIndex));
                const QSizeF box = pdfPage->getRotatedCropBox().size();
                size = QSizeF(box.width() * pdfPage->getUserUnit(), box.height() * pdfPage->getUserUnit());
            }
        }
        else if (!page.plan.pageSize.isEmpty())
        {
            size = page.plan.pageSize;
        }
        else
        {
            size = source.imagePageSize;
        }
    }

    if (!size.isValid() || size.isEmpty())
    {
        return QSizeF();
    }

    // Crop… cuts the margins of the visible area (Q17)
    const QMarginsF& margins = page.plan.cropMargins;
    size = QSizeF(qMax(1.0, size.width() - margins.left() - margins.right()), qMax(1.0, size.height() - margins.top() - margins.bottom()));
    return PDFAppManageCanvasHelper::turned(size, page.plan.turn);
}

QString PDFAppManagePageModel::getLabel(int row) const
{
    using Helper = PDFAppManageCanvasHelper;

    const PDFDocumentAssembly::Page* page = getPage(row);
    if (!page)
    {
        return QString();
    }

    const QString position = PDFAppLocale::formatNumber(qint64(getPosition(row)) + 1);
    if (Helper::isBlankPage(*page))
    {
        // 387:13099 "12": a blank page has no source
        return position;
    }
    const QString letter = page->plan.input >= 0 && size_t(page->plan.input) < m_sources.size() ? m_sources[size_t(page->plan.input)].letter : QString();
    const PDFDocumentAssembly::Source* source = Helper::getSource(m_assembly, *page);
    if (!m_isDocumentHost)
    {
        // "A · 6": the letter of the source and the position in the assembly (QB5)
        return tr("%1 · %2").arg(letter, position);
    }

    // The tab of a document (1479:34632): "3 · p. 7" of the document, "7 · B p. 2" of another PDF,
    // "10 · C" of an image
    if (source && source->isTarget)
    {
        return tr("%1 · p. %2").arg(position, PDFAppLocale::formatNumber(page->plan.pageIndex + 1));
    }
    if (Helper::isImagePage(m_assembly, *page))
    {
        return tr("%1 · %2").arg(position, letter);
    }
    return tr("%1 · %2 p. %3").arg(position, letter, PDFAppLocale::formatNumber(page->plan.pageIndex + 1));
}

QString PDFAppManagePageModel::getCaption(int row) const
{
    using Helper = PDFAppManageCanvasHelper;

    const PDFDocumentAssembly::Page* page = getPage(row);
    const int itemIndex = getItemIndex(row);
    if (!page || itemIndex < 0)
    {
        return QString();
    }
    const PDFDocumentAssembly::Item& item = m_assembly.getItems()[size_t(itemIndex)];
    if (item.isGroup())
    {
        // The tiles of a card have no caption (387:12813)
        return QString();
    }

    // 387:12569: the own name ("Site plan"), "rotated 90°", "cropped", "image", "blank page"; in the
    // tab of a document (1479:34632) the name of another source and "new in the document"
    if (!item.name.isEmpty())
    {
        return item.name;
    }
    if (page->plan.turn != PageRotation::None)
    {
        return Helper::getTurnCaption(page->plan.turn);
    }
    if (Helper::isCropped(*page))
    {
        return tr("cropped");
    }
    if (Helper::isBlankPage(*page))
    {
        return tr("blank page");
    }
    if (Helper::isImagePage(m_assembly, *page))
    {
        return tr("image");
    }
    if (m_isDocumentHost)
    {
        const PDFDocumentAssembly::Source* source = Helper::getSource(m_assembly, *page);
        if (source && !source->isTarget)
        {
            return source->name;
        }
        if (source && source->isTarget && page->targetPage.isValid() && m_newTargetPages.count(page->targetPage))
        {
            return tr("new in the document");
        }
    }
    return QString();
}

int PDFAppManagePageModel::getGroupOfRow(int row) const
{
    return row >= 0 && size_t(row) < m_rowGroups.size() ? m_rowGroups[size_t(row)] : -1;
}

bool PDFAppManagePageModel::isRowSelected(int row) const
{
    const int item = getItemIndex(row);
    return item >= 0 && m_selectedItems.count(m_assembly.getItems()[size_t(item)].id);
}

int PDFAppManagePageModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant PDFAppManagePageModel::data(const QModelIndex& index, int role) const
{
    using Helper = PDFAppManageCanvasHelper;

    const PDFDocumentAssembly::Page* page = index.isValid() ? getPage(index.row()) : nullptr;
    if (!page)
    {
        return QVariant();
    }

    const int row = index.row();
    switch (role)
    {
        case Qt::DisplayRole:
            return getLabel(row);

        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
        {
            const PDFDocumentAssembly::Item& item = m_assembly.getItems()[size_t(getItemIndex(row))];
            return tr("Page %1 of the result · %2").arg(PDFAppLocale::formatNumber(qint64(getPosition(row)) + 1), PDFAppManageItemModel::getItemName(m_assembly, item));
        }

        case Qt::AccessibleDescriptionRole:
            return getCaption(row);

        case PDFComponentRoles::PageTileStateRole:
            return int(isRowSelected(row) ? PDFComponentPainter::PageTileState::Selected : PDFComponentPainter::PageTileState::Normal);

        case PDFComponentRoles::PageSizeRole:
        {
            const QSizeF size = getPageSize(row);
            return size.isValid() ? QVariant(size) : QVariant();
        }

        case PDFComponentRoles::PixmapRole:
            return PDFAppToolPageGridModel::getTurnedPicture(BaseClass::data(index, PDFComponentRoles::PixmapRole), getKey(row), page->plan.turn);

        case PDFComponentRoles::SubtitleRole:
        {
            const QString caption = getCaption(row);
            return caption.isEmpty() ? QVariant() : QVariant(caption);
        }

        case PDFComponentRoles::HueRole:
            // The dot of the source (387:12835); a blank page has none (387:13098)
            if (!Helper::isBlankPage(*page) && page->plan.input >= 0 && size_t(page->plan.input) < m_sources.size())
            {
                return int(m_sources[size_t(page->plan.input)].hue);
            }
            return QVariant();

        case PDFAppToolPageTileDelegate::PaperBorderRole:
            // A blank page shows its paper (387:13077)
            return Helper::isBlankPage(*page);

        case PDFAppToolPageTileDelegate::SquareSlotRole:
        {
            // Owner Q4 of 12. 11. (plan 3b, 13 point 4): a page outside of a group is a box 100 x 100
            // in a tile 112 x 128 (387:12938, 1479:35324), a landscape page 100 wide; the tiles of
            // a card keep 73 x 100 (387:12814)
            const int group = getGroupOfRow(row);
            return group >= 0 && size_t(group) < m_groups.size() && m_groups[size_t(group)].isPlain;
        }

        case Qt::TextAlignmentRole:
            return QVariant(Qt::AlignLeading | Qt::AlignVCenter);

        default:
            break;
    }
    return QVariant();
}

Qt::ItemFlags PDFAppManagePageModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
}

QString PDFAppManagePageModel::getKey(int row) const
{
    const PDFDocumentAssembly::Page* page = getPage(row);
    if (!page || page->plan.input < 0 || size_t(page->plan.input) >= m_sources.size())
    {
        return QString();
    }

    // The picture of the page of the content of the source of the size of the model; the
    // rotation of the page is drawn by getTurnedPicture (its own key)
    const PDFAppManageSourceView& source = m_sources[size_t(page->plan.input)];
    const QSize size = getPictureSize();
    return QStringLiteral("%1/%2/%3x%4@%5/%6").arg(source.pictureKey).arg(page->plan.pageIndex).arg(size.width()).arg(size.height()).arg(getDevicePixelRatio()).arg(getGeneration());
}

bool PDFAppManagePageModel::hasPictureSources() const
{
    return true;
}

PDFAppThumbnailSource PDFAppManagePageModel::getPictureSource(int row) const
{
    const PDFDocumentAssembly::Page* page = getPage(row);
    PDFAppThumbnailSource source;
    if (!page || page->plan.input < 0 || size_t(page->plan.input) >= m_sources.size() || page->plan.pageIndex > std::numeric_limits<int>::max())
    {
        return source;
    }
    const PDFAppManageSourceView& view = m_sources[size_t(page->plan.input)];
    source.document = view.document;
    source.imageData = view.document ? QByteArray() : view.imageData;
    source.pageIndex = int(page->plan.pageIndex);
    return source;
}

// ------------------------------------------------------------- PDFAppManageItemModel

PDFAppManageItemModel::PDFAppManageItemModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppManageItemModel::~PDFAppManageItemModel() = default;

void PDFAppManageItemModel::setContent(PDFDocumentAssembly assembly, std::vector<PDFAppManageSourceView> sources, const std::set<QString>& shownItems, bool isFiltered)
{
    beginResetModel();
    m_assembly = std::move(assembly);
    m_sources = std::move(sources);
    m_rows.clear();
    const std::vector<PDFDocumentAssembly::Item>& items = m_assembly.getItems();
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (!isFiltered || shownItems.count(items[i].id))
        {
            m_rows.push_back(int(i));
        }
    }
    applySort();
    endResetModel();
}

int PDFAppManageItemModel::getItemIndex(int row) const
{
    return row >= 0 && size_t(row) < m_rows.size() ? m_rows[size_t(row)] : -1;
}

QString PDFAppManageItemModel::getItemId(int row) const
{
    const int item = getItemIndex(row);
    return item >= 0 ? m_assembly.getItems()[size_t(item)].id : QString();
}

int PDFAppManageItemModel::getRowOfItem(const QString& itemId) const
{
    for (size_t row = 0; row < m_rows.size(); ++row)
    {
        if (m_assembly.getItems()[size_t(m_rows[row])].id == itemId)
        {
            return int(row);
        }
    }
    return -1;
}

QString PDFAppManageItemModel::getItemName(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item)
{
    // Owner Q5 of 12. 11. (plan 3b, 13 point 5), 387:15072: the own name as the user gave it, else
    // the name of the file of the pages ("Annex_A.pdf", "signature_page.png") - the pages are said
    // by the tile ("B · 7") and by Orig. page; the core derives the rest ("Blank page", "3 pages"
    // of more sources). The part titles of the output stay the titles of the core (QB3).
    if (item.name.isEmpty() && !item.pages.empty())
    {
        const int input = PDFAppManageCanvasHelper::getSingleSource(item);
        if (input >= 0 && size_t(input) < assembly.getSources().size())
        {
            return assembly.getSources()[size_t(input)].name;
        }
    }
    return PDFDocumentAssembly::getItemName(assembly, item);
}

QString PDFAppManageItemModel::getItemType(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item)
{
    using Helper = PDFAppManageCanvasHelper;

    if (item.isGroup())
    {
        return tr("Group");
    }
    if (item.pages.empty() || Helper::isBlankPage(item.pages.front()))
    {
        return tr("Blank page");
    }
    return Helper::isImagePage(assembly, item.pages.front()) ? tr("Image") : tr("PDF page");
}

QString PDFAppManageItemModel::getItemSource(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item)
{
    using Helper = PDFAppManageCanvasHelper;

    const int input = Helper::getSingleSource(item);
    if (input == PDFDocumentAssembly::BLANK_INPUT)
    {
        return QStringLiteral("–");
    }
    if (input >= 0 && size_t(input) < assembly.getSources().size())
    {
        return assembly.getSources()[size_t(input)].name;
    }
    std::set<int> inputs;
    for (const PDFDocumentAssembly::Page& page : item.pages)
    {
        inputs.insert(page.plan.input);
    }
    const int count = int(inputs.size());
    return count == 1 ? tr("%Ln source", nullptr, count) : tr("%Ln sources", nullptr, count);
}

QString PDFAppManageItemModel::getOriginalPages(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item)
{
    using Helper = PDFAppManageCanvasHelper;

    // "1–5", "6", "–" of an image of one frame and of a blank page (387:15072)
    int input = -1;
    PDFInteger first = 0;
    PDFInteger last = 0;
    if (Helper::getRun(assembly, item, &input, &first, &last))
    {
        return Helper::formatRun(first, last);
    }
    if (item.pages.size() == 1 && Helper::isImagePage(assembly, item.pages.front()))
    {
        const PDFDocumentAssembly::Source* source = Helper::getSource(assembly, item.pages.front());
        return source && source->pageCount > 1 ? PDFAppLocale::formatNumber(item.pages.front().plan.pageIndex + 1) : QStringLiteral("–");
    }
    if (item.pages.size() == 1 && Helper::isPdfPage(assembly, item.pages.front()))
    {
        return PDFAppLocale::formatNumber(item.pages.front().plan.pageIndex + 1);
    }
    return QStringLiteral("–");
}

QString PDFAppManageItemModel::getRotationText(PageRotation turn)
{
    switch (turn)
    {
        case PageRotation::Rotate90:
            return tr("90°");
        case PageRotation::Rotate180:
            return tr("180°");
        case PageRotation::Rotate270:
            return tr("270°");
        default:
            break;
    }
    return tr("None");
}

QString PDFAppManageItemModel::getItemTags(const PDFDocumentAssembly& assembly, const PDFDocumentAssembly::Item& item)
{
    using Helper = PDFAppManageCanvasHelper;

    // 387:15072: "+4" (the further pages of a group), Image, Blank, Crop
    QStringList tags;
    if (item.isGroup() && item.pages.size() > 1)
    {
        tags << tr("+%1").arg(PDFAppLocale::formatNumber(qint64(item.pages.size()) - 1));
    }
    bool hasImage = false;
    bool hasBlank = false;
    bool hasCrop = false;
    for (const PDFDocumentAssembly::Page& page : item.pages)
    {
        hasImage = hasImage || Helper::isImagePage(assembly, page);
        hasBlank = hasBlank || Helper::isBlankPage(page);
        hasCrop = hasCrop || Helper::isCropped(page);
    }
    if (hasImage)
    {
        tags << tr("Image");
    }
    if (hasBlank)
    {
        tags << tr("Blank");
    }
    if (hasCrop)
    {
        tags << tr("Crop");
    }
    return tags.join(tr(", "));
}

int PDFAppManageItemModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int PDFAppManageItemModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PDFAppManageItemModel::data(const QModelIndex& index, int role) const
{
    const int itemIndex = index.isValid() ? getItemIndex(index.row()) : -1;
    if (itemIndex < 0 || index.column() >= ColumnCount)
    {
        return QVariant();
    }
    if (role != Qt::DisplayRole && role != Qt::ToolTipRole)
    {
        return QVariant();
    }

    const PDFDocumentAssembly::Item& item = m_assembly.getItems()[size_t(itemIndex)];
    const QSizeF size = item.pages.empty() ? QSizeF() : PDFAppManagePageModel::getPageSize(m_assembly, item.pages.front(), m_sources);
    switch (index.column())
    {
        case NumberColumn:
            return PDFAppLocale::formatNumber(qint64(itemIndex) + 1);
        case NameColumn:
            return getItemName(m_assembly, item);
        case TypeColumn:
            return getItemType(m_assembly, item);
        case SourceColumn:
            return getItemSource(m_assembly, item);
        case OriginalPageColumn:
            return getOriginalPages(m_assembly, item);
        case PagesColumn:
            return PDFAppLocale::formatNumber(qint64(item.pages.size()));
        case SizeColumn:
            return PDFAppToolMergePageModel::getSizeText(size, false);
        case OrientationColumn:
            return PDFAppToolMergePageModel::getOrientationText(size);
        case RotationColumn:
            return item.pages.empty() ? QString() : getRotationText(item.pages.front().plan.turn);
        case TagsColumn:
            return getItemTags(m_assembly, item);
        default:
            break;
    }
    return QVariant();
}

QVariant PDFAppManageItemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    {
        return QVariant();
    }

    // The columns of 387:15072
    switch (section)
    {
        case NumberColumn:
            return tr("#");
        case NameColumn:
            return tr("Name");
        case TypeColumn:
            return tr("Type");
        case SourceColumn:
            return tr("Source");
        case OriginalPageColumn:
            return tr("Orig. page");
        case PagesColumn:
            return tr("Pages");
        case SizeColumn:
            return tr("Size");
        case OrientationColumn:
            return tr("Orientation");
        case RotationColumn:
            return tr("Rotation");
        case TagsColumn:
            return tr("Tags");
        default:
            break;
    }
    return QVariant();
}

Qt::ItemFlags PDFAppManageItemModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
}

QVariant PDFAppManageItemModel::getSortValue(int item, int column) const
{
    const PDFDocumentAssembly::Item& value = m_assembly.getItems()[size_t(item)];
    switch (column)
    {
        case NumberColumn:
            return item;
        case PagesColumn:
            return qint64(value.pages.size());
        case OriginalPageColumn:
            return value.pages.empty() ? qint64(-1) : qint64(value.pages.front().plan.pageIndex);
        default:
            break;
    }

    // The other columns are sorted by their texts (applySort)
    return QVariant();
}

void PDFAppManageItemModel::sort(int column, Qt::SortOrder order)
{
    // Only this view (387:15072): the order of the rows, never the assembly
    beginResetModel();
    m_sortColumn = column;
    m_sortOrder = order;
    if (column < 0 || column >= ColumnCount)
    {
        // No column: the order of the assembly
        std::sort(m_rows.begin(), m_rows.end());
    }
    applySort();
    endResetModel();
}

void PDFAppManageItemModel::applySort()
{
    if (m_sortColumn < 0 || m_sortColumn >= ColumnCount)
    {
        return;
    }

    // The texts of the column (the numbers by their values), stable
    std::vector<std::pair<QVariant, int>> keys;
    keys.reserve(m_rows.size());
    for (const int item : m_rows)
    {
        QVariant key = getSortValue(item, m_sortColumn);
        if (!key.isValid())
        {
            const PDFDocumentAssembly::Item& value = m_assembly.getItems()[size_t(item)];
            const QSizeF size = value.pages.empty() ? QSizeF() : PDFAppManagePageModel::getPageSize(m_assembly, value.pages.front(), m_sources);
            switch (m_sortColumn)
            {
                case NameColumn:
                    key = getItemName(m_assembly, value).toCaseFolded();
                    break;
                case TypeColumn:
                    key = getItemType(m_assembly, value);
                    break;
                case SourceColumn:
                    key = getItemSource(m_assembly, value).toCaseFolded();
                    break;
                case SizeColumn:
                    key = size.isValid() ? size.width() * size.height() : 0.0;
                    break;
                case OrientationColumn:
                    key = PDFAppToolMergePageModel::getOrientationText(size);
                    break;
                case RotationColumn:
                    key = value.pages.empty() ? 0 : int(value.pages.front().plan.turn);
                    break;
                case TagsColumn:
                    key = getItemTags(m_assembly, value);
                    break;
                default:
                    break;
            }
        }
        keys.emplace_back(key, item);
    }

    // A stable insertion sort by QVariant::compare (no comparator object: the conventions)
    for (size_t i = 1; i < keys.size(); ++i)
    {
        std::pair<QVariant, int> current = keys[i];
        size_t j = i;
        while (j > 0)
        {
            const QPartialOrdering ordering = QVariant::compare(current.first, keys[j - 1].first);
            const bool isBefore = m_sortOrder == Qt::AscendingOrder ? ordering == QPartialOrdering::Less : ordering == QPartialOrdering::Greater;
            if (!isBefore)
            {
                break;
            }
            keys[j] = keys[j - 1];
            --j;
        }
        keys[j] = current;
    }
    for (size_t i = 0; i < keys.size(); ++i)
    {
        m_rows[i] = keys[i].second;
    }
}

// ------------------------------------------------------------- PDFAppManageCanvas

PDFAppManageCanvas::PDFAppManageCanvas(QWidget* parent) :
    BaseClass(parent)
{
    setObjectName(QStringLiteral("manageCanvas"));

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(PADDING, PADDING, PADDING, PADDING);
    layout->setSpacing(0);

    // preview-bar 387:12784: "10 items · 17 pages", search-assembly 210 x 36, Size: Small, the segments
    m_bar = new QWidget(this);
    m_bar->setObjectName(QStringLiteral("managePreviewBar"));
    m_bar->setFixedHeight(BAR_HEIGHT);
    QHBoxLayout* barLayout = new QHBoxLayout(m_bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(BAR_SPACING);
    m_header = PDFAppPageWidgets::createLabel(QString(), QFont::DemiBold, 13, PDFThemeColors::TextPrimary, m_bar);
    m_header->setObjectName(QStringLiteral("manageHeader"));
    m_header->setMinimumWidth(1);
    barLayout->addWidget(m_header, 1);
    m_searchField = new PDFLineEdit(m_bar);
    m_searchField->setObjectName(QStringLiteral("manageSearch"));
    m_searchField->setSearchField();
    m_searchField->setFixedSize(SEARCH_WIDTH, BAR_HEIGHT);
    m_searchField->installEventFilter(this);
    connect(m_searchField.data(), &QLineEdit::textEdited, this, &PDFAppManageCanvas::onSearchEdited);
    barLayout->addWidget(m_searchField);
    m_tileSizeButton = PDFAppDocumentToolbar::createToolbarMenuButton(QStringLiteral("manageTileSize"), QStringLiteral("pages"), m_bar);
    barLayout->addWidget(m_tileSizeButton);
    m_viewSwitch = new PDFSegmentedControl(m_bar);
    m_viewSwitch->setObjectName(QStringLiteral("manageViewSwitch"));
    m_viewSwitch->addSegment(QString());
    m_viewSwitch->addSegment(QString());
    m_viewSwitch->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    connect(m_viewSwitch.data(), &PDFSegmentedControl::currentIndexChanged, this, &PDFAppManageCanvas::onViewSwitchChanged);
    barLayout->addWidget(m_viewSwitch);
    layout->addWidget(m_bar);
    layout->addSpacing(BAR_GAP);

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(SEARCH_DELAY);
    connect(m_searchTimer.data(), &QTimer::timeout, this, &PDFAppManageCanvas::onSearchTimer);

    // The pages: the grouped view (with the hint of the search) or Details
    m_pages = new QWidget(this);
    m_pages->setObjectName(QStringLiteral("managePages"));
    m_stack = new QStackedLayout(m_pages);
    m_stack->setContentsMargins(0, 0, 0, 0);

    m_pageModel = new PDFAppManagePageModel(this);
    PDFAppToolPageTileDelegate* delegate = new PDFAppToolPageTileDelegate(this);
    delegate->setBackgroundColor(PDFThemeColors::BgSurface);
    m_delegate = delegate;

    m_thumbnailsPage = new QWidget(m_pages);
    m_thumbnailsPage->setObjectName(QStringLiteral("manageThumbnailsPage"));
    QVBoxLayout* thumbnailsLayout = new QVBoxLayout(m_thumbnailsPage);
    thumbnailsLayout->setContentsMargins(0, 0, 0, 0);
    thumbnailsLayout->setSpacing(NOTE_GAP);
    m_pageView = new PDFAppToolMergePageView(m_thumbnailsPage);
    m_pageView->setObjectName(QStringLiteral("managePageView"));
    m_pageView->setModel(m_pageModel);
    m_pageView->setTileDelegate(m_delegate);
    m_pageView->setContextMenuPolicy(Qt::CustomContextMenu);
    thumbnailsLayout->addWidget(m_pageView, 1);
    // 387:15594: the hint of the search, 10 below the tiles
    m_searchHint = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, m_thumbnailsPage);
    m_searchHint->setObjectName(QStringLiteral("manageSearchHint"));
    m_searchHint->hide();
    thumbnailsLayout->addWidget(m_searchHint);
    m_stack->addWidget(m_thumbnailsPage);
    m_scheduler = new PDFAppThumbnailScheduler(m_pageModel, m_pageView, this);
    connect(m_pageView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppManageCanvas::onPageSelectionChanged);
    connect(m_pageView.data(), &PDFAppToolMergePageView::moveRequested, this, &PDFAppManageCanvas::onPageMoveRequested);
    connect(m_pageView.data(), &PDFAppToolMergePageView::moveByRequested, this, &PDFAppManageCanvas::moveByRequested);
    connect(m_pageView.data(), &PDFAppToolMergePageView::removeRequested, this, &PDFAppManageCanvas::removeRequested);
    connect(m_pageView.data(), &PDFAppToolMergePageView::contentHeightChanged, this, &PDFAppManageCanvas::onContentHeightChanged);
    connect(m_pageView.data(), &QWidget::customContextMenuRequested, this, &PDFAppManageCanvas::onPageContextMenu);

    // 387:15072: the table of the items in a card and its note below
    m_itemModel = new PDFAppManageItemModel(this);
    m_detailsPage = new QWidget(m_pages);
    m_detailsPage->setObjectName(QStringLiteral("manageDetailsPage"));
    QVBoxLayout* detailsLayout = new QVBoxLayout(m_detailsPage);
    detailsLayout->setContentsMargins(0, 0, 0, 0);
    detailsLayout->setSpacing(NOTE_GAP);
    PDFAppCard* card = PDFAppPageWidgets::createCard(PDFAppPageWidgets::BORDER_WIDTH, 0, m_detailsPage);
    card->setObjectName(QStringLiteral("manageDetailsCard"));
    m_detailsView = new PDFAppToolMergeDetailsView(card);
    m_detailsView->setObjectName(QStringLiteral("manageDetailsView"));
    m_detailsView->header()->setDefaultSectionSize(DETAILS_LAST_COLUMN_WIDTH);
    m_detailsView->setModel(m_itemModel);
    m_detailsView->setColumnWidths(std::vector<int>(std::cbegin(DETAILS_COLUMN_WIDTHS), std::cend(DETAILS_COLUMN_WIDTHS)));
    m_detailsView->setSortingEnabled(true);
    // The order of the assembly until the user clicks a heading (QHeaderView starts at column 0)
    m_detailsView->header()->setSortIndicator(-1, Qt::AscendingOrder);
    m_detailsView->setContextMenuPolicy(Qt::CustomContextMenu);
    card->getLayout()->addWidget(m_detailsView);
    detailsLayout->addWidget(card);
    connect(m_itemModel.data(), &QAbstractItemModel::modelReset, m_detailsView.data(), &QWidget::updateGeometry);
    connect(m_detailsView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &PDFAppManageCanvas::onDetailsSelectionChanged);
    connect(m_detailsView.data(), &PDFAppToolMergeDetailsView::moveRequested, this, &PDFAppManageCanvas::onDetailsMoveRequested);
    connect(m_detailsView.data(), &PDFAppToolMergeDetailsView::moveByRequested, this, &PDFAppManageCanvas::moveByRequested);
    connect(m_detailsView.data(), &PDFAppToolMergeDetailsView::removeRequested, this, &PDFAppManageCanvas::removeRequested);
    connect(m_detailsView.data(), &QWidget::customContextMenuRequested, this, &PDFAppManageCanvas::onDetailsContextMenu);
    m_detailsNote = PDFAppPageWidgets::createText(QString(), PDFThemeFonts::getFont(QFont::Normal, 11), 13, PDFThemeColors::TextTertiary, m_detailsPage);
    m_detailsNote->setObjectName(QStringLiteral("manageDetailsNote"));
    detailsLayout->addWidget(m_detailsNote);
    detailsLayout->addStretch(1);
    m_stack->addWidget(m_detailsPage);
    layout->addWidget(m_pages, 1);

    setTileSize(m_tileSize);
    retranslate();
}

PDFAppManageCanvas::~PDFAppManageCanvas() = default;

void PDFAppManageCanvas::setContent(const PDFDocumentAssembly& assembly, std::vector<PDFAppManageSourceView> sources, bool isDocumentHost, std::set<PDFObjectReference> newTargetPages)
{
    m_assembly = assembly;
    m_sources = std::move(sources);
    m_isDocumentHost = isDocumentHost;
    m_newTargetPages = std::move(newTargetPages);

    // The selection keeps the items, which are still there, in the order of the assembly
    std::set<QString> selected(m_selection.cbegin(), m_selection.cend());
    m_selection.clear();
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        if (selected.count(item.id))
        {
            m_selection.push_back(item.id);
        }
    }
    applyContent();
}

std::set<QString> PDFAppManageCanvas::getShownItems() const
{
    // 387:15594: the items, which the search finds (the rule of the core, Select › Search results)
    std::set<QString> shown;
    if (!m_isFiltered)
    {
        return shown;
    }
    PDFDocumentAssembly::SelectSettings settings;
    settings.searchText = m_appliedSearch;
    for (const QString& id : PDFDocumentAssembly::select(m_assembly, PDFDocumentAssembly::Selection(), PDFDocumentAssembly::SelectRule::SearchResults, settings))
    {
        shown.insert(id);
    }
    return shown;
}

int PDFAppManageCanvas::getShownItemCount() const
{
    return m_itemModel ? m_itemModel->rowCount() : 0;
}

void PDFAppManageCanvas::applyContent()
{
    // The models are reset (their views lay out again); the selection is drawn by both
    const std::set<QString> shown = getShownItems();
    QPointer<PDFAppManageCanvas> guard(this);
    m_isSyncing = true;
    m_itemModel->setContent(m_assembly, m_sources, shown, m_isFiltered);
    m_pageModel->setContent(m_assembly, m_sources, shown, m_isFiltered, m_isDocumentHost, m_newTargetPages);
    if (!guard)
    {
        return;
    }
    m_isSyncing = false;
    selectInViews();
    updateSearchHint();
}

void PDFAppManageCanvas::setSelection(const PDFDocumentAssembly::Selection& selection)
{
    // In the order of the assembly, only the items, which exist
    std::set<QString> wanted(selection.cbegin(), selection.cend());
    m_selection.clear();
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        if (wanted.count(item.id))
        {
            m_selection.push_back(item.id);
        }
    }
    selectInViews();
}

void PDFAppManageCanvas::selectInViews()
{
    // The rows of the selected items in both views, without the signals of the canvas
    // One pass over the rows of each view (plan 6: 100 000 items selected by Select all)
    const bool wasSyncing = m_isSyncing;
    m_isSyncing = true;
    const std::set<QString> selected(m_selection.cbegin(), m_selection.cend());
    selectPageRows(selected);
    m_detailsView->selectionModel()->select(createRanges(m_itemModel, selected, PDFAppManageItemModel::ColumnCount), QItemSelectionModel::ClearAndSelect);
    m_pageModel->setSelectedItems(std::set<QString>(m_selection.cbegin(), m_selection.cend()));
    m_isSyncing = wasSyncing;
}

QItemSelection PDFAppManageCanvas::createRanges(const QAbstractItemModel* model, const std::set<QString>& itemIds, int columnCount)
{
    // The rows of the items as ranges of consecutive rows (a merge per row is quadratic; plan 6:
    // Select all of 100 000 items)
    QItemSelection selection;
    const PDFAppManagePageModel* pageModel = qobject_cast<const PDFAppManagePageModel*>(model);
    const PDFAppManageItemModel* itemModel = qobject_cast<const PDFAppManageItemModel*>(model);
    const int rowCount = model ? model->rowCount() : 0;
    int first = -1;
    for (int row = 0; row <= rowCount; ++row)
    {
        const QString id = row < rowCount ? (pageModel ? pageModel->getItemId(row) : (itemModel ? itemModel->getItemId(row) : QString())) : QString();
        const bool isSelected = row < rowCount && itemIds.count(id);
        if (isSelected && first < 0)
        {
            first = row;
        }
        else if (!isSelected && first >= 0)
        {
            selection.append(QItemSelectionRange(model->index(first, 0), model->index(row - 1, columnCount - 1)));
            first = -1;
        }
    }
    return selection;
}

void PDFAppManageCanvas::selectPageRows(const std::set<QString>& itemIds)
{
    // As PDFAppToolMergePageView::selectRows (the first row is the current one and comes into
    // the view), with the ranges of createRanges
    QItemSelectionModel* selectionModel = m_pageView->selectionModel();
    const QItemSelection selection = createRanges(m_pageModel, itemIds, 1);
    const QModelIndex first = selection.isEmpty() ? QModelIndex() : QModelIndex(selection.front().topLeft());
    selectionModel->setCurrentIndex(first, QItemSelectionModel::NoUpdate);
    selectionModel->select(selection, QItemSelectionModel::ClearAndSelect);
    if (first.isValid())
    {
        m_pageView->scrollTo(first);
    }
}

void PDFAppManageCanvas::onPageSelectionChanged()
{
    if (m_isSyncing)
    {
        return;
    }

    // A click on a page selects its item: the selected rows give the items (387:12569 "2 pages")
    std::set<QString> ids;
    for (const int row : m_pageView->getSelectedRows())
    {
        const QString id = m_pageModel->getItemId(row);
        if (!id.isEmpty())
        {
            ids.insert(id);
        }
    }
    m_selection.clear();
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        if (ids.count(item.id))
        {
            m_selection.push_back(item.id);
        }
    }

    // Details follows, the tiles of the whole items are drawn selected
    m_isSyncing = true;
    m_detailsView->selectionModel()->select(createRanges(m_itemModel, ids, PDFAppManageItemModel::ColumnCount), QItemSelectionModel::ClearAndSelect);
    m_pageModel->setSelectedItems(ids);
    m_isSyncing = false;

    // Last: the slot may rebuild the canvas
    Q_EMIT selectionChanged();
}

void PDFAppManageCanvas::onDetailsSelectionChanged()
{
    if (m_isSyncing)
    {
        return;
    }

    std::set<QString> ids;
    for (const QModelIndex& index : m_detailsView->selectionModel()->selectedRows())
    {
        const QString id = m_itemModel->getItemId(index.row());
        if (!id.isEmpty())
        {
            ids.insert(id);
        }
    }
    m_selection.clear();
    for (const PDFDocumentAssembly::Item& item : m_assembly.getItems())
    {
        if (ids.count(item.id))
        {
            m_selection.push_back(item.id);
        }
    }

    // "The selection is shared with the thumbnails" (387:15072)
    m_isSyncing = true;
    selectPageRows(ids);
    m_pageModel->setSelectedItems(ids);
    m_isSyncing = false;

    Q_EMIT selectionChanged();
}

void PDFAppManageCanvas::onPageMoveRequested(const std::vector<int>& rows, int targetRow)
{
    Q_UNUSED(rows);

    // The selected items go in front of the item of the target row; a drop inside of a card goes
    // after that card (an item is never split by a drag)
    QString beforeItemId;
    const int rowCount = m_pageModel->rowCount();
    if (targetRow >= 0 && targetRow < rowCount)
    {
        const QString itemId = m_pageModel->getItemId(targetRow);
        const std::vector<int> itemRows = m_pageModel->getRowsOfItem(itemId);
        if (!itemRows.empty() && itemRows.front() == targetRow)
        {
            beforeItemId = itemId;
        }
        else if (!itemRows.empty())
        {
            beforeItemId = m_pageModel->getItemId(itemRows.back() + 1);
        }
    }
    if (!m_selection.empty())
    {
        Q_EMIT moveRequested(m_selection, beforeItemId);
    }
}

void PDFAppManageCanvas::onDetailsMoveRequested(const std::vector<int>& rows, int targetRow)
{
    Q_UNUSED(rows);
    const QString beforeItemId = m_itemModel->getItemId(targetRow);
    if (!m_selection.empty())
    {
        Q_EMIT moveRequested(m_selection, beforeItemId);
    }
}

void PDFAppManageCanvas::onPageContextMenu(const QPoint& position)
{
    // A right click on a page, which is not selected, selects it first (387:14220)
    const QModelIndex index = m_pageView->indexAt(position);
    if (index.isValid())
    {
        const QString id = m_pageModel->getItemId(index.row());
        if (std::find(m_selection.cbegin(), m_selection.cend(), id) == m_selection.cend())
        {
            QPointer<PDFAppManageCanvas> guard(this);
            m_pageView->selectRows(m_pageModel->getRowsOfItem(id));
            if (!guard)
            {
                return;
            }
        }
    }
    Q_EMIT contextMenuRequested(m_pageView->viewport()->mapToGlobal(position));
}

void PDFAppManageCanvas::onDetailsContextMenu(const QPoint& position)
{
    const QModelIndex index = m_detailsView->indexAt(position);
    if (index.isValid())
    {
        const QString id = m_itemModel->getItemId(index.row());
        if (std::find(m_selection.cbegin(), m_selection.cend(), id) == m_selection.cend())
        {
            QPointer<PDFAppManageCanvas> guard(this);
            m_detailsView->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            if (!guard)
            {
                return;
            }
        }
    }
    Q_EMIT contextMenuRequested(m_detailsView->viewport()->mapToGlobal(position));
}

QString PDFAppManageCanvas::getSearchText() const
{
    return m_searchField ? m_searchField->text() : QString();
}

void PDFAppManageCanvas::setSearchText(const QString& text)
{
    m_searchTimer->stop();
    if (m_searchField->text() != text)
    {
        m_searchField->setText(text);
    }
    m_appliedSearch = text.trimmed();
    m_isFiltered = !m_appliedSearch.isEmpty();
    applyContent();
}

void PDFAppManageCanvas::onSearchEdited(const QString& text)
{
    Q_UNUSED(text);
    m_searchTimer->start();
}

void PDFAppManageCanvas::onSearchTimer()
{
    const QString text = m_searchField->text();
    setSearchText(text);

    // Last: the slot says the status (387:15594 "2 of 10 items match …")
    Q_EMIT searchChanged(text);
}

void PDFAppManageCanvas::updateSearchHint()
{
    // 387:15594: below the found items
    m_searchHint->setVisible(m_isFiltered);
    if (!m_isFiltered)
    {
        m_pageView->setMaximumHeight(QWIDGETSIZE_MAX);
    }
}

void PDFAppManageCanvas::onContentHeightChanged(int height)
{
    // The hint of the search follows the found tiles (the view hugs them while it is shown)
    if (m_isFiltered)
    {
        const int frame = m_pageView->frameWidth() * 2;
        m_pageView->setMaximumHeight(qMax(1, height + frame));
    }
}

void PDFAppManageCanvas::setEmptyState(QWidget* emptyState)
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

    // 409:32739: the stage without the bar of the preview
    const bool isEmpty = !m_emptyState.isNull();
    m_bar->setVisible(!isEmpty);
    m_pages->setVisible(!isEmpty);
    canvasLayout->itemAt(1)->spacerItem()->changeSize(0, isEmpty ? 0 : BAR_GAP, QSizePolicy::Minimum, QSizePolicy::Fixed);
    canvasLayout->invalidate();
}

void PDFAppManageCanvas::setDetailsShown(bool isDetailsShown)
{
    m_stack->setCurrentWidget(isDetailsShown ? m_detailsPage.data() : m_thumbnailsPage.data());
    {
        const QSignalBlocker blocker(m_viewSwitch);
        m_viewSwitch->setCurrentIndex(isDetailsShown ? 1 : 0);
    }

    // 387:15072: Size is Disabled in Details
    m_tileSizeButton->setEnabled(m_isEditable && !isDetailsShown);
}

bool PDFAppManageCanvas::isDetailsShown() const
{
    return m_stack->currentWidget() == m_detailsPage;
}

void PDFAppManageCanvas::setTileSize(PDFAppToolPageGrid::TileSize size)
{
    m_tileSize = size;
    const double scale = PDFAppToolPageGrid::getTileScale(size);
    m_delegate->setSlotSize(QSize(int(std::lround(SMALL_SLOT.width() * scale)), int(std::lround(SMALL_SLOT.height() * scale))));
    updatePictureSize();
    m_pageView->setTileDelegate(m_delegate);
}

void PDFAppManageCanvas::setEditable(bool isEditable)
{
    m_isEditable = isEditable;
    m_pageView->setEditable(isEditable);
    m_detailsView->setEditable(isEditable);
    m_viewSwitch->setEnabled(isEditable);
    m_searchField->setEnabled(isEditable);
    m_tileSizeButton->setEnabled(isEditable && !isDetailsShown());
}

void PDFAppManageCanvas::setHeaderText(const QString& text)
{
    m_header->setText(text);
}

void PDFAppManageCanvas::retranslate()
{
    const QSignalBlocker blocker(m_viewSwitch);
    const int index = m_viewSwitch->getCurrentIndex();

    // The texts of the segments: the control has no setter of a text, it is built again
    QHBoxLayout* barLayout = qobject_cast<QHBoxLayout*>(m_bar->layout());
    PDFSegmentedControl* viewSwitch = new PDFSegmentedControl(m_bar);
    viewSwitch->setObjectName(QStringLiteral("manageViewSwitch"));
    viewSwitch->addSegment(tr("Thumbnails"));
    viewSwitch->addSegment(tr("Details"));
    viewSwitch->setFixedSize(2 * SEGMENT_WIDTH, SEGMENT_HEIGHT);
    viewSwitch->setCurrentIndex(qMax(0, index));
    viewSwitch->setEnabled(m_viewSwitch->isEnabled());
    connect(viewSwitch, &PDFSegmentedControl::currentIndexChanged, this, &PDFAppManageCanvas::onViewSwitchChanged);
    delete barLayout->replaceWidget(m_viewSwitch, viewSwitch);
    m_viewSwitch->hide();
    m_viewSwitch->deleteLater();
    m_viewSwitch = viewSwitch;

    m_searchField->setPlaceholderText(tr("Search pages"));
    m_searchField->setAccessibleName(tr("Search pages"));
    m_detailsNote->setText(tr("The selection is shared with the thumbnails. Column headings sort only this view – the page order is changed with Sort."));
    m_searchHint->setText(tr("Search looks into names, sources, sizes and the words grouped, portrait, landscape, image and blank. Select › Search results selects everything that is shown."));
    m_pageView->setAccessibleName(tr("Pages of the assembly"));
    m_detailsView->setAccessibleName(tr("Items of the assembly"));
}

void PDFAppManageCanvas::onViewSwitchChanged(int index)
{
    setDetailsShown(index == 1);
    Q_EMIT viewSwitched(index == 1);
}

void PDFAppManageCanvas::updatePictureSize()
{
    // The scheduler renders the longer side of the slot: a landscape page of the square box of the
    // loose pages (387:12938, 100 wide) gets its full width too
    m_pageModel->setPictureSize(m_delegate->getSlotSize(), devicePixelRatioF());
}

void PDFAppManageCanvas::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    // document-canvas 387:12602: bg/track
    QPainter painter(this);
    painter.fillRect(rect(), PDFThemeManager::getInstance()->getColor(PDFThemeColors::BgTrack));
}

void PDFAppManageCanvas::changeEvent(QEvent* event)
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

bool PDFAppManageCanvas::eventFilter(QObject* watched, QEvent* event)
{
    // 387:15594 "Esc clears the search"
    if (watched == m_searchField && event->type() == QEvent::KeyPress)
    {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape && !m_searchField->text().isEmpty())
        {
            setSearchText(QString());
            Q_EMIT searchChanged(QString());
            return true;
        }
    }
    return BaseClass::eventFilter(watched, event);
}

}   // namespace pdf

#include "moc_pdfappmanagecanvas.cpp"
