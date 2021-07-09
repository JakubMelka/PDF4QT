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


}   // namespace pdfdocpage
