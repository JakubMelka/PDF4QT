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
#include <QCollator>

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
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
    {
        switch (section)
        {
            case ColumnOrder:
                return tr("Order");
            case ColumnName:
                return tr("Name");
            case ColumnType:
                return tr("Type");
            case ColumnSource:
                return tr("Source");
            case ColumnOriginalPage:
                return tr("Original Page");
            case ColumnGroupPages:
                return tr("Pages in Group");
            case ColumnSize:
                return tr("Size");
            case ColumnRotation:
                return tr("Rotation");
            case ColumnTags:
                return tr("Tags");
            default:
                break;
        }
    }

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

    return ColumnCount;
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
        {
            switch (index.column())
            {
                case ColumnOrder:
                    return index.row() + 1;
                case ColumnName:
                    return getItemDisplayText(item);
                case ColumnType:
                    return getItemTypeText(item);
                case ColumnSource:
                    return getItemSourceText(item);
                case ColumnOriginalPage:
                    return getItemOriginalPageText(item);
                case ColumnGroupPages:
                    return int(item->groups.size());
                case ColumnSize:
                    return getItemSizeText(item);
                case ColumnRotation:
                    return getItemRotationText(item);
                case ColumnTags:
                    return getItemTagsText(item);
                default:
                    break;
            }
            break;
        }

        case Qt::ToolTipRole:
            return getItemTooltipText(item);


        default:
            break;
    }

    return QVariant();
}

int PageItemModel::insertDocument(QString fileName, pdf::PDFDocument document, const QModelIndex& index)
{
    return insertDocument(qMove(fileName), qMove(document), index, {});
}

int PageItemModel::insertDocument(QString fileName, pdf::PDFDocument document, const QModelIndex& index, const std::vector<pdf::PDFInteger>& pages)
{
    Modifier modifier(this);
    auto it = std::find_if(m_documents.cbegin(), m_documents.cend(), [&](const auto& item) { return item.second.fileName == fileName; });
    if (it != m_documents.cend())
    {
        if (!pages.empty())
        {
            createDocumentGroup(it->first, index, pages);
            return it->first;
        }
        return -1;
    }

    int newIndex = 1;

    if (!m_documents.empty())
    {
        newIndex = (m_documents.rbegin()->first) + 1;
    }

    m_documents[newIndex] = { qMove(fileName), qMove(document) };
    createDocumentGroup(newIndex, index, pages);
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
        item.format = QString::fromLatin1(reader.format()).toLower();
        item.image = reader.read();

        file.close();

        if (!item.image.isNull())
        {
            int newIndex = 1;

            if (!m_images.empty())
            {
                newIndex = (m_images.rbegin()->first) + 1;
            }

            QFileInfo fileInfo(fileName);
            item.fileName = fileInfo.fileName();
            item.displayName = item.fileName;
            item.sourcePath = fileInfo.absoluteFilePath();

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

        int generatedImageIndex = 1;
        for (const auto& existingImage : m_images)
        {
            if (existingImage.second.fileName.isEmpty())
            {
                ++generatedImageIndex;
            }
        }
        item.displayName = tr("Pasted image %1").arg(generatedImageIndex);
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
    return getSelectionImpl([](const PageGroupItem::GroupItem& groupItem) { return PageItemModel::getCroppedPageDimensionsMM(groupItem).width() <= PageItemModel::getCroppedPageDimensionsMM(groupItem).height(); });
}

QItemSelection PageItemModel::getSelectionLandscape() const
{
    return getSelectionImpl([](const PageGroupItem::GroupItem& groupItem) { return PageItemModel::getCroppedPageDimensionsMM(groupItem).width() >= PageItemModel::getCroppedPageDimensionsMM(groupItem).height(); });
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

void PageItemModel::reorderItems(const QModelIndexList& list, std::function<void (std::vector<PageGroupItem>&)> reorder)
{
    if (m_pageGroupItems.empty())
    {
        return;
    }

    Modifier modifier(this);

    std::vector<int> rows;
    if (list.isEmpty())
    {
        rows.reserve(m_pageGroupItems.size());
        for (int i = 0; i < rowCount(QModelIndex()); ++i)
        {
            rows.push_back(i);
        }
    }
    else
    {
        rows.reserve(list.size());
        for (const QModelIndex& index : list)
        {
            if (index.isValid() && index.row() >= 0 && index.row() < rowCount(QModelIndex()))
            {
                rows.push_back(index.row());
            }
        }
        std::sort(rows.begin(), rows.end());
        rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    }

    if (rows.size() < 2)
    {
        return;
    }

    std::vector<PageGroupItem> selectedItems;
    selectedItems.reserve(rows.size());
    for (int row : rows)
    {
        selectedItems.push_back(m_pageGroupItems[row]);
    }

    reorder(selectedItems);

    std::vector<PageGroupItem> newItems = m_pageGroupItems;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        newItems[rows[i]] = selectedItems[i];
    }

    if (newItems != m_pageGroupItems)
    {
        beginResetModel();
        m_pageGroupItems = std::move(newItems);
        endResetModel();
    }
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

    Q_EMIT dataChanged(index(rowMin, 0, QModelIndex()), index(rowMax, ColumnCount - 1, QModelIndex()));
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

    Q_EMIT dataChanged(index(rowMin, 0, QModelIndex()), index(rowMax, ColumnCount - 1, QModelIndex()));
}

void PageItemModel::resetRotation(const QModelIndexList& list)
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
            for (PageGroupItem::GroupItem& groupItem : item->groups)
            {
                groupItem.pageAdditionalRotation = pdf::PageRotation::None;
            }
        }

        rowMin = qMin(rowMin, index.row());
        rowMax = qMax(rowMax, index.row());
    }

    rowMin = qMax(rowMin, 0);
    rowMax = qMin(rowMax, rowCount(QModelIndex()) - 1);

    Q_EMIT dataChanged(index(rowMin, 0, QModelIndex()), index(rowMax, ColumnCount - 1, QModelIndex()));
}

void PageItemModel::reverseItems(const QModelIndexList& list)
{
    reorderItems(list, [](std::vector<PageGroupItem>& items) { std::reverse(items.begin(), items.end()); });
}

void PageItemModel::sortItems(const QModelIndexList& list, SortMode mode, Qt::SortOrder order)
{
    reorderItems(list, [this, mode, order](std::vector<PageGroupItem>& items)
    {
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);

        struct SortKey
        {
            QString primary;
            QString secondary;
            pdf::PDFInteger pageIndex = -1;
        };

        auto itemKey = [this, mode](const PageGroupItem& item)
        {
            const PageGroupItem::GroupItem* groupItem = item.groups.empty() ? nullptr : &item.groups.front();
            SortKey key;
            if (groupItem)
            {
                key.pageIndex = groupItem->pageIndex;
            }

            switch (mode)
            {
                case SortMode::FileName:
                    if (groupItem)
                    {
                        key.primary = getSourceFileName(*groupItem);
                    }
                    key.secondary = getItemDisplayText(&item);
                    break;

                case SortMode::Source:
                    key.primary = getItemSourceText(&item);
                    key.secondary = getItemDisplayText(&item);
                    break;

                case SortMode::PageNumber:
                    key.primary = getItemSourceText(&item);
                    key.secondary = getItemDisplayText(&item);
                    break;

                case SortMode::Type:
                    key.primary = getItemTypeText(&item);
                    key.secondary = getItemSourceFileName(&item);
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }

            return key;
        };

        auto compareText = [&collator](const QString& left, const QString& right)
        {
            return collator.compare(left, right);
        };

        auto comparePageIndex = [](pdf::PDFInteger left, pdf::PDFInteger right)
        {
            if (left < right)
            {
                return -1;
            }
            if (left > right)
            {
                return 1;
            }
            return 0;
        };

        std::stable_sort(items.begin(), items.end(), [&](const PageGroupItem& left, const PageGroupItem& right)
        {
            const SortKey leftKey = itemKey(left);
            const SortKey rightKey = itemKey(right);

            int result = 0;
            switch (mode)
            {
                case SortMode::FileName:
                case SortMode::Source:
                    result = compareText(leftKey.primary, rightKey.primary);
                    if (result == 0)
                    {
                        result = comparePageIndex(leftKey.pageIndex, rightKey.pageIndex);
                    }
                    if (result == 0)
                    {
                        result = compareText(leftKey.secondary, rightKey.secondary);
                    }
                    break;

                case SortMode::PageNumber:
                    result = comparePageIndex(leftKey.pageIndex, rightKey.pageIndex);
                    if (result == 0)
                    {
                        result = compareText(leftKey.primary, rightKey.primary);
                    }
                    if (result == 0)
                    {
                        result = compareText(leftKey.secondary, rightKey.secondary);
                    }
                    break;

                case SortMode::Type:
                    result = compareText(leftKey.primary, rightKey.primary);
                    if (result == 0)
                    {
                        result = compareText(leftKey.secondary, rightKey.secondary);
                    }
                    if (result == 0)
                    {
                        result = comparePageIndex(leftKey.pageIndex, rightKey.pageIndex);
                    }
                    break;

                default:
                    Q_ASSERT(false);
                    break;
            }

            if (order == Qt::DescendingOrder)
            {
                return result > 0;
            }
            return result < 0;
        });
    });
}

void PageItemModel::renameItems(const QModelIndexList& list, const QString& name)
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
            item->customName = name;
        }

        rowMin = qMin(rowMin, index.row());
        rowMax = qMax(rowMax, index.row());
    }

    rowMin = qMax(rowMin, 0);
    rowMax = qMin(rowMax, rowCount(QModelIndex()) - 1);

    Q_EMIT dataChanged(index(rowMin, 0, QModelIndex()), index(rowMax, ColumnCount - 1, QModelIndex()));
}

void PageItemModel::setImageDisplayName(int imageIndex, const QString& displayName)
{
    auto imageIt = m_images.find(imageIndex);
    if (imageIt == m_images.end())
    {
        return;
    }

    const QString trimmedDisplayName = displayName.trimmed();
    if (imageIt->second.displayName == trimmedDisplayName)
    {
        return;
    }

    Modifier modifier(this);
    imageIt->second.displayName = trimmedDisplayName;

    int rowMin = rowCount(QModelIndex());
    int rowMax = -1;
    for (int row = 0; row < rowCount(QModelIndex()); ++row)
    {
        PageGroupItem& item = m_pageGroupItems[row];
        const bool containsImage = std::any_of(item.groups.cbegin(), item.groups.cend(), [imageIndex](const PageGroupItem::GroupItem& groupItem)
        {
            return groupItem.pageType == PT_Image && groupItem.imageIndex == imageIndex;
        });

        if (containsImage)
        {
            updateItemCaptionAndTags(item);
            rowMin = qMin(rowMin, row);
            rowMax = qMax(rowMax, row);
        }
    }

    if (rowMax >= rowMin)
    {
        Q_EMIT dataChanged(index(rowMin, 0, QModelIndex()), index(rowMax, ColumnCount - 1, QModelIndex()));
    }
}

void PageItemModel::setWorkspaceData(std::map<int, DocumentItem> documents, std::map<int, ImageItem> images, std::vector<PageGroupItem> pageGroupItems)
{
    beginResetModel();
    m_documents = std::move(documents);
    m_images = std::move(images);
    for (PageGroupItem& item : pageGroupItems)
    {
        updateItemCaptionAndTags(item);
    }
    m_pageGroupItems = std::move(pageGroupItems);
    m_trashBin.clear();
    clearUndoRedo();
    endResetModel();
}

void PageItemModel::setWorkspaceState(std::map<int, ImageItem> images, std::vector<PageGroupItem> pageGroupItems)
{
    beginResetModel();
    m_images = std::move(images);
    for (PageGroupItem& item : pageGroupItems)
    {
        updateItemCaptionAndTags(item);
    }
    m_pageGroupItems = std::move(pageGroupItems);
    m_trashBin.clear();
    clearUndoRedo();
    endResetModel();
}

void PageItemModel::cropItems(const QModelIndexList& list, const QMarginsF& cropMarginsMM, bool applyToSameSource)
{
    if (list.isEmpty())
    {
        return;
    }

    std::set<int> selectedRows;
    std::set<int> documentSources;
    std::set<int> imageSources;
    for (const QModelIndex& index : list)
    {
        if (!index.isValid())
        {
            continue;
        }

        selectedRows.insert(index.row());
        if (const PageGroupItem* item = getItem(index))
        {
            for (const PageGroupItem::GroupItem& groupItem : item->groups)
            {
                if (groupItem.pageType == PT_DocumentPage && groupItem.documentIndex != -1)
                {
                    documentSources.insert(groupItem.documentIndex);
                }
                else if (groupItem.pageType == PT_Image && groupItem.imageIndex != -1)
                {
                    imageSources.insert(int(groupItem.imageIndex));
                }
            }
        }
    }

    if (selectedRows.empty())
    {
        return;
    }

    Modifier modifier(this);
    int rowMin = rowCount(QModelIndex());
    int rowMax = -1;

    for (int row = 0; row < rowCount(QModelIndex()); ++row)
    {
        PageGroupItem& item = m_pageGroupItems[row];
        bool itemChanged = false;
        const bool isSelectedRow = selectedRows.count(row) != 0;

        for (PageGroupItem::GroupItem& groupItem : item.groups)
        {
            bool shouldCrop = isSelectedRow;
            if (applyToSameSource)
            {
                shouldCrop = (groupItem.pageType == PT_DocumentPage && documentSources.count(groupItem.documentIndex) != 0) ||
                             (groupItem.pageType == PT_Image && imageSources.count(int(groupItem.imageIndex)) != 0);
            }

            if (!shouldCrop)
            {
                continue;
            }

            const QSizeF pageSize = groupItem.rotatedPageDimensionsMM;
            QMarginsF normalizedMargins(qMax(0.0, cropMarginsMM.left()),
                                        qMax(0.0, cropMarginsMM.top()),
                                        qMax(0.0, cropMarginsMM.right()),
                                        qMax(0.0, cropMarginsMM.bottom()));

            const double maxHorizontalCrop = qMax(0.0, pageSize.width() - 1.0);
            if (normalizedMargins.left() + normalizedMargins.right() > maxHorizontalCrop)
            {
                const double scale = maxHorizontalCrop / (normalizedMargins.left() + normalizedMargins.right());
                normalizedMargins.setLeft(normalizedMargins.left() * scale);
                normalizedMargins.setRight(normalizedMargins.right() * scale);
            }

            const double maxVerticalCrop = qMax(0.0, pageSize.height() - 1.0);
            if (normalizedMargins.top() + normalizedMargins.bottom() > maxVerticalCrop)
            {
                const double scale = maxVerticalCrop / (normalizedMargins.top() + normalizedMargins.bottom());
                normalizedMargins.setTop(normalizedMargins.top() * scale);
                normalizedMargins.setBottom(normalizedMargins.bottom() * scale);
            }

            if (groupItem.cropMarginsMM != normalizedMargins)
            {
                groupItem.cropMarginsMM = normalizedMargins;
                itemChanged = true;
            }
        }

        if (itemChanged)
        {
            updateItemCaptionAndTags(item);
            rowMin = qMin(rowMin, row);
            rowMax = qMax(rowMax, row);
        }
    }

    if (rowMax >= rowMin)
    {
        Q_EMIT dataChanged(index(rowMin, 0, QModelIndex()), index(rowMax, ColumnCount - 1, QModelIndex()));
    }
}

QString PageItemModel::getItemDisplayText(const PageGroupItem *item) const
{
    if (!item->customName.isEmpty())
    {
        return item->customName;
    }

    return isUseTitleInDescription() ? item->groupNameWithTitle : item->groupNameWithoutTitle;
}

QString PageItemModel::getItemTypeText(const PageGroupItem* item) const
{
    if (!item || item->groups.empty())
    {
        return QString();
    }

    QStringList types;
    for (const PageGroupItem::GroupItem& groupItem : item->groups)
    {
        const QString type = getTypeText(groupItem);
        if (!types.contains(type))
        {
            types << type;
        }
    }
    return types.join(", ");
}

QString PageItemModel::getItemSourceText(const PageGroupItem* item) const
{
    if (!item || item->groups.empty())
    {
        return QString();
    }

    QStringList sources;
    for (const PageGroupItem::GroupItem& groupItem : item->groups)
    {
        const QString source = getSourceFileName(groupItem);
        if (!source.isEmpty() && !sources.contains(source))
        {
            sources << source;
        }
    }
    return sources.join(", ");
}

QString PageItemModel::getItemOriginalPageText(const PageGroupItem* item) const
{
    if (!item || item->groups.empty())
    {
        return QString();
    }

    QStringList pages;
    for (const PageGroupItem::GroupItem& groupItem : item->groups)
    {
        const QString page = getPageText(groupItem);
        if (!page.isEmpty())
        {
            pages << page;
        }
    }
    return pages.join(", ");
}

QString PageItemModel::getItemSizeText(const PageGroupItem* item) const
{
    if (!item || item->groups.empty())
    {
        return QString();
    }

    QStringList sizes;
    for (const PageGroupItem::GroupItem& groupItem : item->groups)
    {
        const QString size = getSizeText(groupItem);
        if (!size.isEmpty() && !sizes.contains(size))
        {
            sizes << size;
        }
    }
    return sizes.join(", ");
}

QString PageItemModel::getItemRotationText(const PageGroupItem* item) const
{
    if (!item || item->groups.empty())
    {
        return QString();
    }

    QStringList rotations;
    for (const PageGroupItem::GroupItem& groupItem : item->groups)
    {
        const QString rotation = getRotationText(groupItem);
        if (!rotations.contains(rotation))
        {
            rotations << rotation;
        }
    }
    return rotations.join(", ");
}

QString PageItemModel::getItemTagsText(const PageGroupItem* item) const
{
    if (!item)
    {
        return QString();
    }

    QStringList result;
    for (const QString& tag : item->tags)
    {
        const QStringList parts = tag.split('@', Qt::KeepEmptyParts);
        result << (parts.size() == 2 ? parts.back() : tag);
    }
    return result.join(", ");
}

QString PageItemModel::getItemTooltipText(const PageGroupItem* item) const
{
    if (!item)
    {
        return QString();
    }

    auto tableRow = [](const QString& label, const QString& value)
    {
        if (value.isEmpty())
        {
            return QString();
        }

        return QString("<tr><td style=\"padding:2px 10px 2px 0; white-space:nowrap; color:#666666;\">%1</td>"
                       "<td style=\"padding:2px 0;\"><b>%2</b></td></tr>")
                .arg(label.toHtmlEscaped(), value.toHtmlEscaped());
    };

    QStringList texts;
    texts << "<html><body>";
    texts << QString("<div style=\"font-size:110%; font-weight:600; margin-bottom:6px;\">%1</div>").arg(getItemDisplayText(item).toHtmlEscaped());
    texts << "<table cellspacing=\"0\" cellpadding=\"0\">";
    texts << tableRow(tr("File name"), getItemDisplayText(item));
    texts << tableRow(tr("Type"), getItemTypeText(item));
    texts << tableRow(tr("Source"), getItemSourceText(item));
    texts << tableRow(tr("Original page"), getItemOriginalPageText(item));
    texts << tableRow(tr("Page count"), QString::number(item->groups.size()));
    texts << tableRow(tr("Size"), getItemSizeText(item));
    texts << tableRow(tr("Rotation"), getItemRotationText(item));
    texts << tableRow(tr("Tags"), getItemTagsText(item));

    QStringList paths;
    QStringList formats;
    for (const PageGroupItem::GroupItem& groupItem : item->groups)
    {
        if (groupItem.pageType == PT_Image)
        {
            auto it = m_images.find(groupItem.imageIndex);
            if (it != m_images.cend())
            {
                const ImageItem& imageItem = it->second;
                if (!imageItem.sourcePath.isEmpty() && !paths.contains(imageItem.sourcePath))
                {
                    paths << imageItem.sourcePath;
                }
                if (!imageItem.format.isEmpty() && !formats.contains(imageItem.format))
                {
                    formats << imageItem.format;
                }
            }
        }
    }

    texts << tableRow(tr("Path"), paths.join(", "));
    texts << tableRow(tr("Format"), formats.join(", "));
    texts << "</table>";

    if (item->isGrouped())
    {
        texts << QString("<div style=\"margin-top:8px; margin-bottom:3px; font-weight:600;\">%1</div>").arg(tr("Group items").toHtmlEscaped());
        texts << "<table cellspacing=\"0\" cellpadding=\"0\">";
        int itemIndex = 1;
        for (const PageGroupItem::GroupItem& groupItem : item->groups)
        {
            texts << QString("<tr><td style=\"padding:1px 8px 1px 0; color:#666666;\">%1.</td><td style=\"padding:1px 0;\">%2</td></tr>")
                     .arg(itemIndex++)
                     .arg(getSourceFileName(groupItem).toHtmlEscaped());
        }
        texts << "</table>";
    }

    texts << "</body></html>";
    return texts.join(QString());
}

QString PageItemModel::getItemSourceFileName(const PageGroupItem* item) const
{
    if (!item || item->groups.empty())
    {
        return QString();
    }

    return getSourceFileName(item->groups.front());
}

QString PageItemModel::getItemSourceBaseName(const PageGroupItem* item) const
{
    if (!item || item->groups.empty())
    {
        return QString();
    }

    return getSourceBaseName(item->groups.front());
}

QString PageItemModel::getItemSourceExtension(const PageGroupItem* item) const
{
    if (!item || item->groups.empty())
    {
        return QString();
    }

    return getSourceExtension(item->groups.front());
}

QSizeF PageItemModel::getCroppedPageDimensionsMM(const PageGroupItem::GroupItem& groupItem)
{
    return QSizeF(qMax(1.0, groupItem.rotatedPageDimensionsMM.width() - groupItem.cropMarginsMM.left() - groupItem.cropMarginsMM.right()),
                  qMax(1.0, groupItem.rotatedPageDimensionsMM.height() - groupItem.cropMarginsMM.top() - groupItem.cropMarginsMM.bottom()));
}

bool PageItemModel::isCropped(const PageGroupItem::GroupItem& groupItem)
{
    return !qFuzzyIsNull(groupItem.cropMarginsMM.left()) ||
           !qFuzzyIsNull(groupItem.cropMarginsMM.top()) ||
           !qFuzzyIsNull(groupItem.cropMarginsMM.right()) ||
           !qFuzzyIsNull(groupItem.cropMarginsMM.bottom());
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
    m_images = std::move(step.images);
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

void PageItemModel::createDocumentGroup(int index, const QModelIndex& insertIndex, const std::vector<pdf::PDFInteger>& pages)
{
    const DocumentItem& item = m_documents.at(index);
    const pdf::PDFInteger pageCount = item.document.getCatalog()->getPageCount();
    std::vector<pdf::PDFInteger> insertedPages = pages;
    if (insertedPages.empty())
    {
        insertedPages.reserve(pageCount);
        for (pdf::PDFInteger i = 1; i <= pageCount; ++i)
        {
            insertedPages.push_back(i);
        }
    }

    std::vector<pdf::PDFInteger> validPages;
    validPages.reserve(insertedPages.size());
    for (pdf::PDFInteger page : insertedPages)
    {
        if (page >= 1 && page <= pageCount && std::find(validPages.cbegin(), validPages.cend(), page) == validPages.cend())
        {
            validPages.push_back(page);
        }
    }
    insertedPages = std::move(validPages);
    if (insertedPages.empty())
    {
        return;
    }

    pdf::PDFClosedIntervalSet pageSet;
    for (pdf::PDFInteger page : insertedPages)
    {
        pageSet.addInterval(page, page);
    }

    PageGroupItem newItem;
    newItem.groupNameWithTitle = getGroupNameFromDocument(index, true);
    newItem.groupNameWithoutTitle = getGroupNameFromDocument(index, false);
    newItem.pagesCaption = pageSet.toText(true);

    if (insertedPages.size() > 1)
    {
        newItem.tags = QStringList() << QString("#00CC00@+%1").arg(insertedPages.size() - 1);
    }

    newItem.groups.reserve(insertedPages.size());
    for (pdf::PDFInteger pageIndex : insertedPages)
    {
        PageGroupItem::GroupItem groupItem;
        groupItem.documentIndex = index;
        groupItem.pageIndex = pageIndex;
        groupItem.rotatedPageDimensionsMM = item.document.getCatalog()->getPage(pageIndex - 1)->getRotatedMediaBoxMM().size();
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

QString PageItemModel::getImageDisplayName(int imageIndex) const
{
    auto it = m_images.find(imageIndex);
    if (it == m_images.cend())
    {
        return tr("Image");
    }

    if (!it->second.displayName.isEmpty())
    {
        return it->second.displayName;
    }

    if (!it->second.fileName.isEmpty())
    {
        return it->second.fileName;
    }

    return tr("Image");
}

QString PageItemModel::getSourceFileName(const PageGroupItem::GroupItem& groupItem) const
{
    switch (groupItem.pageType)
    {
        case PT_DocumentPage:
        {
            auto it = m_documents.find(groupItem.documentIndex);
            if (it != m_documents.cend())
            {
                return QFileInfo(it->second.fileName).fileName();
            }
            break;
        }

        case PT_Image:
            return getImageDisplayName(groupItem.imageIndex);

        case PT_Empty:
            return tr("Blank Page");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PageItemModel::getSourceBaseName(const PageGroupItem::GroupItem& groupItem) const
{
    switch (groupItem.pageType)
    {
        case PT_DocumentPage:
        {
            auto it = m_documents.find(groupItem.documentIndex);
            if (it != m_documents.cend())
            {
                return QFileInfo(it->second.fileName).completeBaseName();
            }
            break;
        }

        case PT_Image:
        {
            auto it = m_images.find(groupItem.imageIndex);
            if (it != m_images.cend())
            {
                const QString name = !it->second.fileName.isEmpty() ? it->second.fileName : it->second.displayName;
                return QFileInfo(name).completeBaseName();
            }
            break;
        }

        case PT_Empty:
            return tr("blank-page");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PageItemModel::getSourceExtension(const PageGroupItem::GroupItem& groupItem) const
{
    switch (groupItem.pageType)
    {
        case PT_DocumentPage:
        {
            auto it = m_documents.find(groupItem.documentIndex);
            if (it != m_documents.cend())
            {
                return QFileInfo(it->second.fileName).suffix();
            }
            break;
        }

        case PT_Image:
        {
            auto it = m_images.find(groupItem.imageIndex);
            if (it != m_images.cend())
            {
                if (!it->second.format.isEmpty())
                {
                    return it->second.format;
                }
                return QFileInfo(it->second.fileName).suffix();
            }
            break;
        }

        case PT_Empty:
            break;

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PageItemModel::getTypeText(const PageGroupItem::GroupItem& groupItem) const
{
    switch (groupItem.pageType)
    {
        case PT_DocumentPage:
            return tr("PDF Page");
        case PT_Image:
            return tr("Image");
        case PT_Empty:
            return tr("Blank Page");
        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

QString PageItemModel::getPageText(const PageGroupItem::GroupItem& groupItem) const
{
    if (groupItem.pageIndex > 0)
    {
        return QString::number(groupItem.pageIndex);
    }

    return QString();
}

QString PageItemModel::getSizeText(const PageGroupItem::GroupItem& groupItem) const
{
    const QSizeF croppedPageDimensions = getCroppedPageDimensionsMM(groupItem);
    QString text = QString("%1 x %2 mm")
            .arg(croppedPageDimensions.width(), 0, 'f', 1)
            .arg(croppedPageDimensions.height(), 0, 'f', 1);

    if (groupItem.pageType == PT_Image)
    {
        auto it = m_images.find(groupItem.imageIndex);
        if (it != m_images.cend() && !it->second.image.isNull())
        {
            text += QString(" (%1 x %2 px)").arg(it->second.image.width()).arg(it->second.image.height());
        }
    }

    return text;
}

QString PageItemModel::getRotationText(const PageGroupItem::GroupItem& groupItem) const
{
    switch (groupItem.pageAdditionalRotation)
    {
        case pdf::PageRotation::None:
            return tr("None");
        case pdf::PageRotation::Rotate90:
            return tr("90°");
        case pdf::PageRotation::Rotate180:
            return tr("180°");
        case pdf::PageRotation::Rotate270:
            return tr("270°");
        default:
            break;
    }

    return QString::number(int(groupItem.pageAdditionalRotation));
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
        if (imageCount == 1 && !item.groups.empty())
        {
            item.groupNameWithTitle = getImageDisplayName(item.groups.front().imageIndex);
        }
        else
        {
            item.groupNameWithTitle = tr("%1 Images").arg(imageCount);
        }
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
    if (std::any_of(item.groups.cbegin(), item.groups.cend(), [](const PageGroupItem::GroupItem& group) { return PageItemModel::isCropped(group); }))
    {
        item.tags << tr("#9C5BD1@Crop");
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
    std::vector<int> rows;
    rows.reserve(indexes.size());
    for (const QModelIndex& index : indexes)
    {
        if (index.isValid())
        {
            rows.push_back(index.row());
        }
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    QByteArray serializedData;
    {
        QDataStream stream(&serializedData, QIODevice::WriteOnly);
        for (int row : rows)
        {
            stream << row;
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
    Q_UNUSED(column);

    if (action == Qt::IgnoreAction)
    {
        return true;
    }

    if (!data->hasFormat(getMimeDataType()))
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
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

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

pdf::PDFDocumentManipulator::AssembledPage PageItemModel::createAssembledPage(const PageGroupItem::GroupItem& item) const
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
    assembledPage.pageSize = getCroppedPageDimensionsMM(item);
    assembledPage.cropMarginsMM = item.cropMarginsMM;
    assembledPage.sourcePageSize = item.rotatedPageDimensionsMM;
    assembledPage.sourcePageRotation = originalPageRotation;

    return assembledPage;
}

std::vector<PageGroupItem::GroupItem> PageItemModel::getSelectedGroupItems(const QModelIndexList& list) const
{
    std::vector<int> rows;
    rows.reserve(list.size());
    for (const QModelIndex& index : list)
    {
        if (index.isValid())
        {
            rows.push_back(index.row());
        }
    }

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    std::vector<PageGroupItem::GroupItem> result;
    for (int row : rows)
    {
        const PageGroupItem* item = getItem(index(row, 0, QModelIndex()));
        if (!item)
        {
            continue;
        }

        result.insert(result.end(), item->groups.cbegin(), item->groups.cend());
    }

    return result;
}

qint64 PageItemModel::getApproximateSourceByteSize(const PageGroupItem::GroupItem& item) const
{
    switch (item.pageType)
    {
        case PT_DocumentPage:
        {
            auto it = m_documents.find(item.documentIndex);
            if (it != m_documents.cend())
            {
                const pdf::PDFInteger pageCount = qMax<pdf::PDFInteger>(it->second.document.getCatalog()->getPageCount(), 1);
                return qMax<qint64>(QFileInfo(it->second.fileName).size() / pageCount, 1);
            }
            break;
        }

        case PT_Image:
        {
            auto it = m_images.find(item.imageIndex);
            if (it != m_images.cend())
            {
                return qMax<qint64>(it->second.imageData.size(), 1);
            }
            break;
        }

        case PT_Empty:
            return 1024;

        default:
            Q_ASSERT(false);
            break;
    }

    return 1;
}

std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> PageItemModel::getSplitAssembledPagesEveryN(const QModelIndexList& list, int pageCount) const
{
    std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> result;
    const std::vector<PageGroupItem::GroupItem> selectedItems = getSelectedGroupItems(list);
    if (pageCount < 1 || selectedItems.empty())
    {
        return result;
    }

    for (int i = 0; i < int(selectedItems.size()); ++i)
    {
        if (i % pageCount == 0)
        {
            result.emplace_back();
        }

        result.back().emplace_back(createAssembledPage(selectedItems[i]));
    }

    return result;
}

std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> PageItemModel::getSplitAssembledPagesAtPagePositions(const QModelIndexList& list, const std::vector<int>& pagePositions) const
{
    std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> result;
    const std::vector<PageGroupItem::GroupItem> selectedItems = getSelectedGroupItems(list);
    if (selectedItems.empty())
    {
        return result;
    }

    std::vector<int> breakPositions = pagePositions;
    std::sort(breakPositions.begin(), breakPositions.end());
    breakPositions.erase(std::unique(breakPositions.begin(), breakPositions.end()), breakPositions.end());

    for (int i = 0; i < int(selectedItems.size()); ++i)
    {
        const int pagePosition = i + 1;
        if (result.empty() || (pagePosition > 1 && std::binary_search(breakPositions.cbegin(), breakPositions.cend(), pagePosition)))
        {
            result.emplace_back();
        }

        result.back().emplace_back(createAssembledPage(selectedItems[i]));
    }

    return result;
}

std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> PageItemModel::getSplitAssembledPagesAtDocumentPages(const QModelIndexList& list, const std::vector<pdf::PDFInteger>& pageIndices) const
{
    std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> result;
    std::vector<PageGroupItem::GroupItem> selectedItems = getSelectedGroupItems(list);
    if (selectedItems.empty())
    {
        return result;
    }

    std::sort(selectedItems.begin(), selectedItems.end(), [](const PageGroupItem::GroupItem& left, const PageGroupItem::GroupItem& right)
    {
        return left.pageIndex < right.pageIndex;
    });

    std::vector<pdf::PDFInteger> breakPageIndices = pageIndices;
    std::sort(breakPageIndices.begin(), breakPageIndices.end());
    breakPageIndices.erase(std::unique(breakPageIndices.begin(), breakPageIndices.end()), breakPageIndices.end());

    for (const PageGroupItem::GroupItem& item : selectedItems)
    {
        if (result.empty() || (item.pageIndex > 1 && std::binary_search(breakPageIndices.cbegin(), breakPageIndices.cend(), item.pageIndex)))
        {
            result.emplace_back();
        }

        result.back().emplace_back(createAssembledPage(item));
    }

    return result;
}

std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> PageItemModel::getSplitAssembledPagesByApproximateSize(const QModelIndexList& list, qint64 maximumSizeBytes) const
{
    std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> result;
    const std::vector<PageGroupItem::GroupItem> selectedItems = getSelectedGroupItems(list);
    if (maximumSizeBytes < 1 || selectedItems.empty())
    {
        return result;
    }

    qint64 currentSize = 0;
    for (const PageGroupItem::GroupItem& item : selectedItems)
    {
        const qint64 itemSize = getApproximateSourceByteSize(item);
        if (result.empty() || (!result.back().empty() && currentSize + itemSize > maximumSizeBytes))
        {
            result.emplace_back();
            currentSize = 0;
        }

        result.back().emplace_back(createAssembledPage(item));
        currentSize += itemSize;
    }

    return result;
}

std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> PageItemModel::getAssembledPages(AssembleMode mode) const
{
    std::vector<std::vector<pdf::PDFDocumentManipulator::AssembledPage>> result;

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
    m_images.clear();
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
