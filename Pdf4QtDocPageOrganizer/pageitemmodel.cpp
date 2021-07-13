//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of Pdf4Qt.
//
//    Pdf4Qt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    Pdf4Qt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with Pdf4Qt.  If not, see <https://www.gnu.org/licenses/>.

#include "pageitemmodel.h"

#include <QFileInfo>

#include <iterator>

namespace pdfdocpage
{

PageItemModel::PageItemModel(QObject* parent) :
    QAbstractItemModel(parent)
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

    switch (role)
    {
        case Qt::DisplayRole:
            return m_pageGroupItems.at(index.row()).groupName;

        default:
            break;
    }

    return QVariant();
}

int PageItemModel::addDocument(QString fileName, pdf::PDFDocument document)
{
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
    createDocumentGroup(newIndex);
    return newIndex;
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

void PageItemModel::createDocumentGroup(int index)
{
    const DocumentItem& item = m_documents.at(index);
    const pdf::PDFInteger pageCount = item.document.getCatalog()->getPageCount();
    pdf::PDFClosedIntervalSet pageSet;
    pageSet.addInterval(1, pageCount);

    PageGroupItem newItem;
    newItem.groupName = getGroupNameFromDocument(index);
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

    beginInsertRows(QModelIndex(), rowCount(QModelIndex()), rowCount(QModelIndex()));
    m_pageGroupItems.push_back(qMove(newItem));
    endInsertRows();
}

QString PageItemModel::getGroupNameFromDocument(int index) const
{
    const DocumentItem& item = m_documents.at(index);

    QString title = item.document.getInfo()->title;
    if (!title.isEmpty())
    {
        return title;
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
            pageSet.addInterval(groupItem.pageIndex, groupItem.pageIndex);
        }

        item.groupName = getGroupNameFromDocument(*documentIndices.begin());
        item.pagesCaption = pageSet.toText(true);
    }
    else
    {
        item.groupName = tr("Document collection");
        item.pagesCaption = tr("Page Count: %1").arg(item.groups.size());
    }

    item.tags.clear();
    if (pageCount > 1)
    {
        item.tags << QString("#00CC00@+%1").arg(pageCount - 1);
    }
    if (documentIndices.size() > 1)
    {
        item.tags << QString("#BBBB00@Collection");
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
        int row = -1;
        stream >> row;
        rows.push_back(row);
    }

    qSort(rows);

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

void PageItemModel::clear()
{
    beginResetModel();
    m_pageGroupItems.clear();
    m_documents.clear();
    m_trashBin.clear();
    endResetModel();
}

}   // namespace pdfdocpage
