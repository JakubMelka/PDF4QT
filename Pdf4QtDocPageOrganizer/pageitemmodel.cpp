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
    if (!parent.isValid())
    {
        return 0;
    }

    return int(m_pageGroupItems.size());
}

int PageItemModel::columnCount(const QModelIndex& parent) const
{
    if (!parent.isValid())
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
        newItem.tags = QStringList() << QString("0x00FF00@+%1").arg(pageCount - 1);
    }

    newItem.groups.reserve(pageCount);
    for (pdf::PDFInteger i = 1; i <= pageCount; ++i)
    {
        PageGroupItem::GroupItem groupItem;
        groupItem.documentIndex = index;
        groupItem.pageIndex = i;
        groupItem.rotatedPageDimensionsMM = item.document.getCatalog()->getPage(i)->getRotatedMediaBoxMM().size();
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

}   // namespace pdfdocpage
