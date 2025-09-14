// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pageitemmodel.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>

#include <iterator>

namespace pdfpagemaster
{

PageItemModel::PageItemModel(QObject* parent) :
    QAbstractItemModel(parent),
    m_showTitleInDescription(false)
{

}

QVariant PageItemModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    Q_UNUSED(section);
    Q_UNUSED(orientation);
    Q_UNUSED(role);

    return QVariant();
}

QModelIndex PageItemModel::index(int row, int column, const QModelIndex& parent) const
{
    if (hasIndex(row, column, parent))
    {
        return createIndex(row, column, nullptr);
    }

    return QModelIndex();
}

QModelIndex PageItemModel::parent(const QModelIndex& index) const
{
    Q_UNUSED(index);

    return QModelIndex();
}

int PageItemModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        return 0;
    }

    return int(m_pageGroupItems.size());
}

int PageItemModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        return 0;
    }

    return 1;
}

QVariant PageItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    const PageGroupItem* item = getItem(index);
    switch (role)
    {
        case Qt::DisplayRole:
            return getItemDisplayText(item);

        case Qt::ToolTipRole:
        {
            QStringList texts;
            texts << QString("<b>%1</b>").arg(getItemDisplayText(item));

            if (item->isGrouped())
            {
                texts << QString("%1 pages").arg(item->groups.size());
            }

            return texts.join("<br>");
        }


        default:
            break;
    }

    return QVariant();
}

int PageItemModel::insertDocument(QString fileName, pdf::PDFDocument document, const QModelIndex& index)
{
    Modifier modifier(this);
    auto it = std::find_if(m_documents.cbegin(), m_documents.cend(), [&](const auto& item) { return item.second.fileName == fileName; });
    if (it != m_documents.cend())
    {
        return -1;
    }

    int newIndex = 1;

    if (!m_documents.empty())
    {
        newIndex = (m_documents.rbegin()->first) + 1;
    }

    m_documents[newIndex] = { qMove(fileName), qMove(document) };
    createDocumentGroup(newIndex, index);
    return newIndex;
}

int PageItemModel::insertImage(QString fileName, const QModelIndex& index)
{
    Modifier modifier(this);
    QFile file(fileName);

    if (file.open(QFile::ReadOnly))
    {
        ImageItem item;
        item.imageData = file.readAll();

        QImageReader reader(fileName);
        item.image = reader.read();

        file.close();

        if (!item.image.isNull())
        {
            int newIndex = 1;

            if (!m_images.empty())
            {
                newIndex = (m_images.rbegin()->first) + 1;
            }

            m_images[newIndex] = qMove(item);

            // Insert image item
            PageGroupItem newItem;

            newItem.groups.reserve(1);

            PageGroupItem::GroupItem groupItem;
            groupItem.imageIndex = newIndex;
            groupItem.rotatedPageDimensionsMM = m_images[newIndex].image.size() * 0.1;
            groupItem.pageType = PT_Image;
            newItem.groups.push_back(qMove(groupItem));

            updateItemCaptionAndTags(newItem);
            int insertRow = index.isValid() ? index.row() + 1 : int(m_pageGroupItems.size());

            beginInsertRows(QModelIndex(), insertRow, insertRow);
            m_pageGroupItems.insert(std::next(m_pageGroupItems.begin(), insertRow), qMove(newItem));
            endInsertRows();

            return newIndex;
        }
    }

    return -1;
}

int PageItemModel::insertImage(QImage image, const QModelIndex& index)
{
    Modifier modifier(this);

    if (!image.isNull())
    {
        ImageItem item;
        item.image = image;
        int newIndex = 1;

        if (!m_images.empty())
        {
            newIndex = (m_images.rbegin()->first) + 1;
        }

        m_images[newIndex] = qMove(item);

        // Insert image item
        PageGroupItem newItem;

        newItem.groups.reserve(1);

        PageGroupItem::GroupItem groupItem;
        groupItem.imageIndex = newIndex;
        groupItem.rotatedPageDimensionsMM = m_images[newIndex].image.size() * 0.1;
        groupItem.pageType = PT_Image;
        newItem.groups.push_back(qMove(groupItem));

        updateItemCaptionAndTags(newItem);
        int insertRow = index.isValid() ? index.row() + 1 : int(m_pageGroupItems.size());

        beginInsertRows(QModelIndex(), insertRow, insertRow);
        m_pageGroupItems.insert(std::next(m_pageGroupItems.begin(), insertRow), qMove(newItem));
        endInsertRows();

        return newIndex;
    }

    return -1;
}

const PageGroupItem* PageItemModel::getItem(const QModelIndex& index) const
{
    if (index.isValid())
    {
        if (index.row() < m_pageGroupItems.size())
        {
            return &m_pageGroupItems.at(index.row());
        }
    }

    return nullptr;
}

PageGroupItem* PageItemModel::getItem(const QModelIndex& index)
{
    if (index.isValid())
    {
        if (index.row() < m_pageGroupItems.size())
        {
            return &m_pageGroupItems.at(index.row());
        }
    }

    return nullptr;
}

bool PageItemModel::isGrouped(const QModelIndexList& indices) const
{
    for (const QModelIndex& index : indices)
    {
        if (const PageGroupItem* item = getItem(index))
        {
            if (item->isGrouped())
            {
                return true;
            }
        }
    }

    return false;
}

QItemSelection PageItemModel::getSelectionEven() const
{
    return getSelectionImpl([](const PageGroupItem::GroupItem& groupItem) { return groupItem.pageIndex % 2 == 1; });
}

QItemSelection PageItemModel::getSelectionOdd() const
{
    return getSelectionImpl([](const PageGroupItem::GroupItem& groupItem) { return groupItem.pageIndex % 2 == 0; });
}

QItemSelection PageItemModel::getSelectionPortrait() const
{
    return getSelectionImpl([](const PageGroupItem::GroupItem& groupItem) { return groupItem.rotatedPageDimensionsMM.width() <= groupItem.rotatedPageDimensionsMM.height(); });
}

QItemSelection PageItemModel::getSelectionLandscape() const
{
    return getSelectionImpl([](const PageGroupItem::GroupItem& groupItem) { return groupItem.rotatedPageDimensionsMM.width() >= groupItem.rotatedPageDimensionsMM.height(); });
}

void PageItemModel::group(const QModelIndexList& list)
{
    if (list.isEmpty())
    {
        return;
    }

    Modifier modifier(this);

    std::vector<size_t> groupedIndices;
    groupedIndices.reserve(list.size());
    std::transform(list.cbegin(), list.cend(), std::back_inserter(groupedIndices), [](const auto& index) { return index.row(); });
    std::sort(groupedIndices.begin(), groupedIndices.end());

    std::vector<PageGroupItem> newPageGroupItems;
    std::vector<PageGroupItem::GroupItem> newGroups;
    newPageGroupItems.reserve(m_pageGroupItems.size());
    for (size_t i = 0; i < m_pageGroupItems.size(); ++i)
    {
        const PageGroupItem& item = m_pageGroupItems[i];
        if (std::binary_search(groupedIndices.cbegin(), groupedIndices.cend(), i))
        {
            newGroups.insert(newGroups.end(), item.groups.begin(), item.groups.end());
        }
        else
        {
            newPageGroupItems.push_back(item);
        }
    }

    PageGroupItem newItem;
    newItem.groups = qMove(newGroups);
    updateItemCaptionAndTags(newItem);
    newPageGroupItems.insert(std::next(newPageGroupItems.begin(), groupedIndices.front()), qMove(newItem));

    if (newPageGroupItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(newPageGroupItems);
        endResetModel();
    }
}

void PageItemModel::ungroup(const QModelIndexList& list)
{
    if (list.isEmpty())
    {
        return;
    }

    Modifier modifier(this);

    std::vector<size_t> ungroupedIndices;
    ungroupedIndices.reserve(list.size());
    std::transform(list.cbegin(), list.cend(), std::back_inserter(ungroupedIndices), [](const auto& index) { return index.row(); });
    std::sort(ungroupedIndices.begin(), ungroupedIndices.end());

    std::vector<PageGroupItem> newPageGroupItems;
    newPageGroupItems.reserve(m_pageGroupItems.size());
    for (size_t i = 0; i < m_pageGroupItems.size(); ++i)
    {
        const PageGroupItem& item = m_pageGroupItems[i];
        if (item.isGrouped() && std::binary_search(ungroupedIndices.cbegin(), ungroupedIndices.cend(), i))
        {
            for (const PageGroupItem::GroupItem& groupItem : item.groups)
            {
                PageGroupItem newItem;
                newItem.groups = { groupItem };
                updateItemCaptionAndTags(newItem);
                newPageGroupItems.push_back(qMove(newItem));
            }
        }
        else
        {
            newPageGroupItems.push_back(item);
        }
    }

    if (newPageGroupItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(newPageGroupItems);
        endResetModel();
    }
}

QModelIndexList PageItemModel::restoreRemovedItems()
{
    QModelIndexList result;

    if (m_trashBin.empty())
    {
        // Jakub Melka: nothing to do
        return result;
    }

    Modifier modifier(this);

    const int trashBinSize = int(m_trashBin.size());
    const int rowCount = this->rowCount(QModelIndex());
    beginInsertRows(QModelIndex(), rowCount, rowCount + trashBinSize - 1);
    m_pageGroupItems.insert(m_pageGroupItems.end(), std::make_move_iterator(m_trashBin.begin()), std::make_move_iterator(m_trashBin.end()));
    m_trashBin.clear();
    endInsertRows();

    result.reserve(trashBinSize);
    for (int i = rowCount; i < rowCount + trashBinSize; ++i)
    {
        result << index(i, 0, QModelIndex());
    }

    return result;
}

QModelIndexList PageItemModel::cloneSelection(const QModelIndexList& list)
{
    QModelIndexList result;

    if (list.empty())
    {
        // Jakub Melka: nothing to do
        return result;
    }

    Modifier modifier(this);

    std::vector<int> rows;
    rows.reserve(list.size());
    std::transform(list.cbegin(), list.cend(), std::back_inserter(rows), [](const auto& index) { return index.row(); });

    std::vector<PageGroupItem> clonedGroups;
    clonedGroups.reserve(rows.size());

    for (int row : rows)
    {
        clonedGroups.push_back(m_pageGroupItems[row]);
    }

    const int insertRow = rows.back() + 1;
    const int lastRow = insertRow + int(rows.size());

    beginResetModel();
    m_pageGroupItems.insert(std::next(m_pageGroupItems.begin(), insertRow), clonedGroups.begin(), clonedGroups.end());
    endResetModel();

    result.reserve(int(rows.size()));
    for (int i = insertRow; i < lastRow; ++i)
    {
        result << index(i, 0, QModelIndex());
    }

    return result;
}

void PageItemModel::removeSelection(const QModelIndexList& list)
{
    if (list.empty())
    {
        // Jakub Melka: nothing to do
        return;
    }

    Modifier modifier(this);

    std::vector<int> rows;
    rows.reserve(list.size());
    std::transform(list.cbegin(), list.cend(), std::back_inserter(rows), [](const auto& index) { return index.row(); });
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    beginResetModel();
    for (int row : rows)
    {
        m_trashBin.emplace_back(qMove(m_pageGroupItems[row]));
        m_pageGroupItems.erase(std::next(m_pageGroupItems.begin(), row));
    }
    endResetModel();
}

void PageItemModel::insertEmptyPage(const QModelIndexList& list)
{
    Modifier modifier(this);

    if (list.isEmpty())
    {
        insertEmptyPage(QModelIndex());
    }
    else
    {
        QModelIndexList listCopy = list;
        std::sort(listCopy.begin(), listCopy.end());
        std::reverse(listCopy.begin(), listCopy.end());

        for (const QModelIndex& index: listCopy)
        {
            insertEmptyPage(index);
        }
    }
}

void PageItemModel::insertEmptyPage(const QModelIndex& index)
{
    int insertRow = index.isValid()? index.row() + 1 : int(m_pageGroupItems.size());

    const int templateRow = index.isValid() ? index.row() : int(m_pageGroupItems.size()) - 1;
    const bool isTemplateRowValid = templateRow > -1;

    PageGroupItem::GroupItem groupItem;
    groupItem.pageAdditionalRotation = isTemplateRowValid ? m_pageGroupItems[templateRow].groups.back().pageAdditionalRotation : pdf::PageRotation::None;
    groupItem.pageType = PT_Empty;
    groupItem.rotatedPageDimensionsMM = isTemplateRowValid ? m_pageGroupItems[templateRow].groups.back().rotatedPageDimensionsMM : QSizeF(210, 297);

    PageGroupItem blankPageItem;
    blankPageItem.groups.push_back(groupItem);
    updateItemCaptionAndTags(blankPageItem);

    beginInsertRows(QModelIndex(), insertRow, insertRow);
    m_pageGroupItems.insert(std::next(m_pageGroupItems.begin(), insertRow), std::move(blankPageItem));
    endInsertRows();
}

std::vector<PageGroupItem::GroupItem> PageItemModel::extractItems(std::vector<PageGroupItem>& items,
                                                                  const QModelIndexList& selection) const
{
    std::vector<PageGroupItem::GroupItem> extractedItems;

    std::vector<int> rows;
    rows.reserve(selection.size());
    std::transform(selection.cbegin(), selection.cend(), std::back_inserter(rows), [](const auto& index) { return index.row(); });
    std::sort(rows.begin(), rows.end(), std::greater<int>());

    for (int row : rows)
    {
        extractedItems.insert(extractedItems.begin(), items[row].groups.cbegin(), items[row].groups.cend());
        items.erase(std::next(items.begin(), row));
    }

    return extractedItems;
}

void PageItemModel::rotateLeft(const QModelIndexList& list)
{
    if (list.isEmpty())
    {
        return;
    }

    Modifier modifier(this);

    int rowMin = list.front().row();
    int rowMax = list.front().row();

    for (const QModelIndex& index : list)
    {
        if (PageGroupItem* item = getItem(index))
        {
            item->rotateLeft();
        }

        rowMin = qMin(rowMin, index.row());
        rowMax = qMax(rowMax, index.row());
    }

    rowMin = qMax(rowMin, 0);
    rowMax = qMin(rowMax, rowCount(QModelIndex()) - 1);

    Q_EMIT dataChanged(index(rowMin, 0, QModelIndex()), index(rowMax, 0, QModelIndex()));
}

void PageItemModel::rotateRight(const QModelIndexList& list)
{
    if (list.isEmpty())
    {
        return;
    }

    Modifier modifier(this);

    int rowMin = list.front().row();
    int rowMax = list.front().row();

    for (const QModelIndex& index : list)
    {
        if (PageGroupItem* item = getItem(index))
        {
            item->rotateRight();
        }

        rowMin = qMin(rowMin, index.row());
        rowMax = qMax(rowMax, index.row());
    }

    rowMin = qMax(rowMin, 0);
    rowMax = qMin(rowMax, rowCount(QModelIndex()) - 1);

    Q_EMIT dataChanged(index(rowMin, 0, QModelIndex()), index(rowMax, 0, QModelIndex()));
}

QString PageItemModel::getItemDisplayText(const PageGroupItem *item) const
{
    return isUseTitleInDescription() ? item->groupNameWithTitle : item->groupNameWithoutTitle;
}

void PageItemModel::regroupReversed(const QModelIndexList& list)
{
    if (list.empty())
    {
        return;
    }

    Modifier modifier(this);

    std::vector<PageGroupItem> pageGroupItems = m_pageGroupItems;
    std::vector<PageGroupItem::GroupItem> extractedItems = extractItems(pageGroupItems, list);
    std::reverse(extractedItems.begin(), extractedItems.end());

    if (!extractedItems.empty())
    {
        PageGroupItem item;
        item.groups = std::move(extractedItems);
        updateItemCaptionAndTags(item);
        pageGroupItems.emplace_back(std::move(item));
    }

    if (pageGroupItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(pageGroupItems);
        endResetModel();
    }
}

PageItemModel::SelectionInfo PageItemModel::getSelectionInfo(const QModelIndexList& list) const
{
    SelectionInfo info;

    std::set<int> documents;
    std::set<int> images;

    for (const QModelIndex& index : list)
    {
        const PageGroupItem* item = getItem(index);

        if (!item)
        {
            continue;
        }

        for (const PageGroupItem::GroupItem& groupItem : item->groups)
        {
            switch (groupItem.pageType)
            {
                case pdfpagemaster::PT_DocumentPage:
                    documents.insert(groupItem.documentIndex);
                    break;

                case pdfpagemaster::PT_Image:
                    images.insert(groupItem.imageIndex);
                    break;

                case pdfpagemaster::PT_Empty:
                    ++info.blankPageCount;
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }
        }
    }

    info.documentCount = int(documents.size());
    info.imageCount = int(images.size());
    info.firstDocumentIndex = !documents.empty() ? *documents.begin() : 0;

    return info;
}

void PageItemModel::regroupEvenOdd(const QModelIndexList& list)
{
    if (list.empty())
    {
        return;
    }

    Modifier modifier(this);

    std::vector<PageGroupItem> pageGroupItems = m_pageGroupItems;
    std::vector<PageGroupItem::GroupItem> extractedItems = extractItems(pageGroupItems, list);

    auto it = std::stable_partition(extractedItems.begin(), extractedItems.end(), [](const auto& item) { return item.pageIndex % 2 == 1; });
    std::vector<PageGroupItem::GroupItem> oddItems(extractedItems.begin(), it);
    std::vector<PageGroupItem::GroupItem> evenItems(it, extractedItems.end());

    if (!oddItems.empty())
    {
        PageGroupItem item;
        item.groups = std::move(oddItems);
        updateItemCaptionAndTags(item);
        pageGroupItems.emplace_back(std::move(item));
    }

    if (!evenItems.empty())
    {
        PageGroupItem item;
        item.groups = std::move(evenItems);
        updateItemCaptionAndTags(item);
        pageGroupItems.emplace_back(std::move(item));
    }

    if (pageGroupItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(pageGroupItems);
        endResetModel();
    }
}

void PageItemModel::regroupPaired(const QModelIndexList& list)
{
    if (list.empty())
    {
        return;
    }

    Modifier modifier(this);

    std::vector<PageGroupItem> pageGroupItems = m_pageGroupItems;
    std::vector<PageGroupItem::GroupItem> extractedItems = extractItems(pageGroupItems, list);

    auto it = extractedItems.begin();
    while (it != extractedItems.cend())
    {
        PageGroupItem item;
        item.groups = { *it++ };

        if (it != extractedItems.cend())
        {
            item.groups.emplace_back(std::move(*it++));
        }

        updateItemCaptionAndTags(item);
        pageGroupItems.emplace_back(std::move(item));
    }

    if (pageGroupItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(pageGroupItems);
        endResetModel();
    }
}

void PageItemModel::regroupOutline(const QModelIndexList& list, const std::vector<pdf::PDFInteger>& indices)
{
    if (list.empty())
    {
        return;
    }

    Modifier modifier(this);

    std::vector<PageGroupItem> pageGroupItems = m_pageGroupItems;
    std::vector<PageGroupItem::GroupItem> extractedItems = extractItems(pageGroupItems, list);
    std::sort(extractedItems.begin(), extractedItems.end(), [](const auto& l, const auto& r) { return l.pageIndex < r.pageIndex; });

    PageGroupItem item;

    for (auto it = extractedItems.begin(); it != extractedItems.cend(); ++it)
    {
        PageGroupItem::GroupItem groupItem = *it;

        if (std::binary_search(indices.cbegin(), indices.cend(), groupItem.pageIndex) &&
            !item.groups.empty())
        {
            updateItemCaptionAndTags(item);
            pageGroupItems.emplace_back(std::move(item));
            item = PageGroupItem();
        }

        item.groups.push_back(groupItem);
    }

    if (!item.groups.empty())
    {
        updateItemCaptionAndTags(item);
        pageGroupItems.emplace_back(std::move(item));
        item = PageGroupItem();
    }

    if (pageGroupItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(pageGroupItems);
        endResetModel();
    }
}

void PageItemModel::regroupAlternatingPages(const QModelIndexList& list, bool reversed)
{
    if (list.empty())
    {
        return;
    }

    Modifier modifier(this);

    std::vector<PageGroupItem> pageGroupItems = m_pageGroupItems;
    std::vector<PageGroupItem::GroupItem> extractedItems = extractItems(pageGroupItems, list);
    const int documentIndex = extractedItems.front().documentIndex;

    auto it = std::stable_partition(extractedItems.begin(), extractedItems.end(), [documentIndex](const auto& item) { return item.documentIndex == documentIndex; });
    std::vector<PageGroupItem::GroupItem> firstDocItems(extractedItems.begin(), it);
    std::vector<PageGroupItem::GroupItem> secondDocItems(it, extractedItems.end());

    if (reversed)
    {
        std::reverse(secondDocItems.begin(), secondDocItems.end());
    }

    auto itF = firstDocItems.begin();
    auto itS = secondDocItems.begin();

    PageGroupItem item;

    while (itF != firstDocItems.cend() || itS != secondDocItems.cend())
    {
        if (itF != firstDocItems.cend())
        {
            item.groups.emplace_back(std::move(*itF++));
        }

        if (itS != secondDocItems.cend())
        {
            item.groups.emplace_back(std::move(*itS++));
        }
    }

    updateItemCaptionAndTags(item);
    pageGroupItems.emplace_back(std::move(item));

    if (pageGroupItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(pageGroupItems);
        endResetModel();
    }
}

void PageItemModel::undo()
{
    performUndoRedo(m_undoSteps, m_redoSteps);
}

void PageItemModel::performUndoRedo(std::vector<PageItemModel::UndoRedoStep>& load,
                                    std::vector<PageItemModel::UndoRedoStep>& save)
{
    if (load.empty())
    {
        return;
    }

    save.emplace_back(getCurrentStep());
    UndoRedoStep step = std::move(load.back());
    load.pop_back();
    updateUndoRedoSteps();

    beginResetModel();
    m_pageGroupItems = std::move(step.pageGroupItems);
    m_trashBin = std::move(step.trashBin);
    endResetModel();
}

void PageItemModel::redo()
{
    performUndoRedo(m_redoSteps, m_undoSteps);
}

QItemSelection PageItemModel::getSelectionImpl(std::function<bool (const PageGroupItem::GroupItem&)> filter) const
{
    QItemSelection result;

    for (int i = 0; i < rowCount(QModelIndex()); ++i)
    {
        QModelIndex rowIndex = index(i, 0, QModelIndex());
        if (const PageGroupItem* item = getItem(rowIndex))
        {
            bool isApplied = false;
            for (const auto& groupItem : item->groups)
            {
                if (filter(groupItem))
                {
                    isApplied = true;
                    break;
                }
            }

            if (isApplied)
            {
                result.select(rowIndex, rowIndex);
            }
        }
    }

    return result;
}

void PageItemModel::updateUndoRedoSteps()
{
    while (m_undoSteps.size() > MAX_UNDO_REDO_STEPS)
    {
        m_undoSteps.erase(m_undoSteps.begin());
    }

    while (m_redoSteps.size() > MAX_UNDO_REDO_STEPS)
    {
        m_redoSteps.erase(m_redoSteps.begin());
    }
}

void PageItemModel::clearUndoRedo()
{
    m_undoSteps.clear();
    m_redoSteps.clear();
}

bool PageItemModel::isUseTitleInDescription() const
{
    return m_showTitleInDescription;
}

void PageItemModel::setShowTitleInDescription(bool newShowTitleInDescription)
{
    m_showTitleInDescription = newShowTitleInDescription;

    const int rowCount = this->rowCount(QModelIndex());
    const int columnCount = this->columnCount(QModelIndex());

    if (rowCount > 0)
    {
        Q_EMIT dataChanged(index(0, 0, QModelIndex()), index(rowCount - 1, columnCount - 1, QModelIndex()));
    }
}

void PageItemModel::createDocumentGroup(int index, const QModelIndex& insertIndex)
{
    const DocumentItem& item = m_documents.at(index);
    const pdf::PDFInteger pageCount = item.document.getCatalog()->getPageCount();
    pdf::PDFClosedIntervalSet pageSet;
    pageSet.addInterval(1, pageCount);

    PageGroupItem newItem;
    newItem.groupNameWithTitle = getGroupNameFromDocument(index, true);
    newItem.groupNameWithoutTitle = getGroupNameFromDocument(index, false);
    newItem.pagesCaption = pageSet.toText(true);

    if (pageCount > 1)
    {
        newItem.tags = QStringList() << QString("#00CC00@+%1").arg(pageCount - 1);
    }

    newItem.groups.reserve(pageCount);
    for (pdf::PDFInteger i = 1; i <= pageCount; ++i)
    {
        PageGroupItem::GroupItem groupItem;
        groupItem.documentIndex = index;
        groupItem.pageIndex = i;
        groupItem.rotatedPageDimensionsMM = item.document.getCatalog()->getPage(i - 1)->getRotatedMediaBoxMM().size();
        newItem.groups.push_back(qMove(groupItem));
    }

    int insertRow = rowCount(QModelIndex());
    if (insertIndex.isValid())
    {
        insertRow = insertIndex.row() + 1;
    }

    beginInsertRows(QModelIndex(), insertRow, insertRow);
    m_pageGroupItems.insert(std::next(m_pageGroupItems.begin(), insertRow), qMove(newItem));
    endInsertRows();
}

QString PageItemModel::getGroupNameFromDocument(int index, bool useTitle) const
{
    if (index == -1)
    {
        return tr("Page Group");
    }

    const DocumentItem& item = m_documents.at(index);

    if (useTitle)
    {
        QString title = item.document.getInfo()->title;
        if (!title.isEmpty())
        {
            return title;
        }
    }

    QFileInfo fileInfo(item.fileName);
    return fileInfo.fileName();
}

void PageItemModel::updateItemCaptionAndTags(PageGroupItem& item) const
{
    std::set<int> documentIndices = item.getDocumentIndices();
    const size_t pageCount = item.groups.size();

    if (documentIndices.size() == 1)
    {
        pdf::PDFClosedIntervalSet pageSet;
        for (const auto& groupItem : item.groups)
        {
            if (groupItem.pageIndex != -1)
            {
                pageSet.addInterval(groupItem.pageIndex, groupItem.pageIndex);
            }
        }

        item.groupNameWithTitle = getGroupNameFromDocument(*documentIndices.begin(), true);
        item.groupNameWithoutTitle = getGroupNameFromDocument(*documentIndices.begin(), false);
        item.pagesCaption = pageSet.toText(true);
    }
    else
    {
        item.groupNameWithTitle = tr("Document collection");
        item.groupNameWithoutTitle = item.groupNameWithTitle;
        item.pagesCaption = tr("Page Count: %1").arg(item.groups.size());
    }

    bool hasImages = false;
    bool hasEmptyPage = false;

    size_t imageCount = 0;
    size_t emptyPageCount = 0;

    for (const PageGroupItem::GroupItem& group : item.groups)
    {
        switch (group.pageType)
        {
            case pdfpagemaster::PT_DocumentPage:
                break;

            case pdfpagemaster::PT_Image:
                hasImages = true;
                ++imageCount;
                break;

            case pdfpagemaster::PT_Empty:
                hasEmptyPage = true;
                ++emptyPageCount;
                break;

            case pdfpagemaster::PT_Last:
                Q_ASSERT(false);
                break;
        }
    }

    if (imageCount == pageCount)
    {
        item.groupNameWithTitle = imageCount == 1 ? tr("Image") : tr("Images");
        item.groupNameWithoutTitle = item.groupNameWithTitle;
    }

    if (emptyPageCount == pageCount)
    {
        item.groupNameWithTitle = emptyPageCount == 1 ? tr("Blank Page") : tr("Blank Pages");
        item.groupNameWithoutTitle = item.groupNameWithTitle;
    }

    item.tags.clear();
    if (pageCount > 1)
    {
        item.tags << QString("#00CC00@+%1").arg(pageCount - 1);
    }
    if (documentIndices.size() > 1)
    {
        item.tags << tr("#BBBB00@Collection");
    }
    if (hasEmptyPage)
    {
        item.tags << tr("#D98335@Blank");
    }
    if (hasImages)
    {
        item.tags << tr("#24A5EA@Image");
    }
}

std::set<int> PageGroupItem::getDocumentIndices() const
{
    std::set<int> result;

    for (const auto& groupItem : groups)
    {
        result.insert(groupItem.documentIndex);
    }

    return result;
}

void PageGroupItem::rotateLeft()
{
    for (auto& groupItem : groups)
    {
        groupItem.pageAdditionalRotation = pdf::getPageRotationRotatedLeft(groupItem.pageAdditionalRotation);
    }
}

void PageGroupItem::rotateRight()
{
    for (auto& groupItem : groups)
    {
        groupItem.pageAdditionalRotation = pdf::getPageRotationRotatedRight(groupItem.pageAdditionalRotation);
    }
}

QStringList PageItemModel::mimeTypes() const
{
    return { getMimeDataType() };
}

QMimeData* PageItemModel::mimeData(const QModelIndexList& indexes) const
{
    if (indexes.isEmpty())
    {
        return nullptr;
    }

    QMimeData* mimeData = new QMimeData;

    QByteArray serializedData;
    {
        QDataStream stream(&serializedData, QIODevice::WriteOnly);
        for (const QModelIndex& index : indexes)
        {
            stream << index.row();
        }
    }

    mimeData->setData(getMimeDataType(), serializedData);
    return mimeData;
}

bool PageItemModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(row);
    Q_UNUSED(column);
    Q_UNUSED(parent);

    switch (action)
    {
        case Qt::CopyAction:
        case Qt::MoveAction:
        case Qt::IgnoreAction:
            break;

        case Qt::LinkAction:
        case Qt::TargetMoveAction:
            return false;

        default:
            Q_ASSERT(false);
            break;
    }

    return data && data->hasFormat(getMimeDataType());
}

bool PageItemModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent)
{
    if (action == Qt::IgnoreAction)
    {
        return true;
    }

    if (!data->hasFormat(getMimeDataType()))
    {
        return false;
    }

    if (column > 0)
    {
        return false;
    }

    Modifier modifier(this);

    int insertRow = rowCount(QModelIndex());
    if (row > -1)
    {
        insertRow = row;
    }
    else if (parent.isValid())
    {
        insertRow = parent.row();
    }

    std::vector<int> rows;

    QByteArray serializedData = data->data(getMimeDataType());
    QDataStream stream(&serializedData, QIODevice::ReadOnly);
    while (!stream.atEnd())
    {
        int currentRow = -1;
        stream >> currentRow;
        rows.push_back(currentRow);
    }

    std::sort(rows.begin(), rows.end());

    // Sanity checks on rows
    if (rows.empty())
    {
        return false;
    }
    if (rows.front() < 0 || rows.back() >= rowCount(QModelIndex()))
    {
        return false;
    }

    std::vector<PageGroupItem> newItems = m_pageGroupItems;
    std::vector<PageGroupItem> workItems;

    // When moving, update insert row
    if (action == Qt::MoveAction)
    {
        const int originalInsertRow = insertRow;
        if (rows.front() < originalInsertRow)
        {
            ++insertRow;
        }

        for (int currentRow : rows)
        {
            if (currentRow < originalInsertRow)
            {
                --insertRow;
            }
        }
    }

    workItems.reserve(rows.size());
    for (int currentRow : rows)
    {
        workItems.push_back(m_pageGroupItems[currentRow]);
    }

    // When move, delete old page items
    if (action == Qt::MoveAction)
    {
        for (auto it = rows.rbegin(); it != rows.rend(); ++it)
        {
            newItems.erase(std::next(newItems.begin(), *it));
        }
    }

    // Insert work items at a given position
    newItems.insert(std::next(newItems.begin(), insertRow), workItems.begin(), workItems.end());

    if (newItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(newItems);
        endResetModel();
    }

    return true;
}

Qt::DropActions PageItemModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

Qt::DropActions PageItemModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

Qt::ItemFlags PageItemModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = BaseClass::flags(index);

    if (index.isValid())
    {
        flags |= Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    }

    return flags;
}

std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> PageItemModel::getAssembledPages(AssembleMode mode) const
{
    std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> result;

    auto createAssembledPage = [this](const PageGroupItem::GroupItem& item)
    {
        pdf::PDFDocumentManipulator::AssembledPage assembledPage;

        assembledPage.documentIndex = item.documentIndex;
        assembledPage.imageIndex = item.imageIndex;
        assembledPage.pageIndex = item.pageIndex;

        if (assembledPage.pageIndex > 0)
        {
            --assembledPage.pageIndex;
        }

        pdf::PageRotation originalPageRotation = pdf::PageRotation::None;
        if (item.pageType == PT_DocumentPage)
        {
            auto it = m_documents.find(item.documentIndex);
            if (it != m_documents.cend())
            {
                const pdf::PDFPage* page = it->second.document.getCatalog()->getPage(item.pageIndex - 1);
                originalPageRotation = page->getPageRotation();
            }
        }

        assembledPage.pageRotation = pdf::getPageRotationCombined(originalPageRotation, item.pageAdditionalRotation);
        assembledPage.pageSize = item.rotatedPageDimensionsMM;

        return assembledPage;
    };

    switch (mode)
    {
        case AssembleMode::Unite:
        {
            std::vector<pdf::PDFDocumentManipulator::AssembledPage> unitedDocument;

            for (const auto& pageGroupItem : m_pageGroupItems)
            {
                for (const auto& groupItem : pageGroupItem.groups)
                {
                    unitedDocument.emplace_back(createAssembledPage(groupItem));
                }
            }

            result.emplace_back(std::move(unitedDocument));
            break;
        }

        case AssembleMode::Separate:
        {
            for (const auto& pageGroupItem : m_pageGroupItems)
            {
                for (const auto& groupItem : pageGroupItem.groups)
                {
                    result.emplace_back().emplace_back(createAssembledPage(groupItem));
                }
            }
            break;
        }

        case AssembleMode::SeparateGrouped:
        {
            for (const auto& pageGroupItem : m_pageGroupItems)
            {
                std::vector<pdf::PDFDocumentManipulator::AssembledPage> groupDocument;
                for (const auto& groupItem : pageGroupItem.groups)
                {
                    groupDocument.emplace_back(createAssembledPage(groupItem));
                }
                result.emplace_back(std::move(groupDocument));
            }
            break;
        }

        default:
        {
            Q_ASSERT(false);
            break;
        }
    }

    // Remove empty documents
    result.erase(std::remove_if(result.begin(), result.end(), [](const auto& pages) { return pages.empty(); }), result.end());

    return result;
}

void PageItemModel::clear()
{
    beginResetModel();
    m_pageGroupItems.clear();
    m_documents.clear();
    m_trashBin.clear();
    clearUndoRedo();
    endResetModel();
}

PageItemModel::Modifier::Modifier(PageItemModel* model) :
    m_model(model)
{
    Q_ASSERT(model);

    m_stateBeforeModification = m_model->getCurrentStep();
}

PageItemModel::Modifier::~Modifier()
{
    UndoRedoStep stateAfterModification = m_model->getCurrentStep();

    if (m_stateBeforeModification != stateAfterModification)
    {
        m_model->m_undoSteps.emplace_back(std::move(m_stateBeforeModification));
        m_model->m_redoSteps.clear();
        m_model->updateUndoRedoSteps();
    }
}

}   // namespace pdfpagemaster
