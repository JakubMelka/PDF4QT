//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfitemmodels.h"
#include "pdfdocument.h"

#include <QFont>
#include <QApplication>

namespace pdf
{

PDFTreeItem::~PDFTreeItem()
{
    qDeleteAll(m_children);
}

PDFTreeItemModel::PDFTreeItemModel(QObject* parent) :
    QAbstractItemModel(parent),
    m_document(nullptr)
{

}

void PDFTreeItemModel::setDocument(const PDFDocument* document)
{
    if (m_document != document)
    {
        m_document = document;
        update();
    }
}

bool PDFTreeItemModel::isEmpty() const
{
    return rowCount(QModelIndex()) == 0;
}

QModelIndex PDFTreeItemModel::index(int row, int column, const QModelIndex& parent) const
{
    if (hasIndex(row, column, parent))
    {
        const PDFTreeItem* parentItem = nullptr;

        if (!parent.isValid())
        {
            parentItem = m_rootItem.get();
        }
        else
        {
            parentItem = static_cast<PDFTreeItem*>(parent.internalPointer());
        }

        return createIndex(row, column, const_cast<PDFTreeItem*>(parentItem->getChild(row)));
    }

    return QModelIndex();
}

QModelIndex PDFTreeItemModel::parent(const QModelIndex& child) const
{
    if (child.isValid())
    {
        const PDFTreeItem* childItem = static_cast<PDFTreeItem*>(child.internalPointer());
        const PDFTreeItem* parentItem = childItem->getParent();

        if (parentItem != m_rootItem.get())
        {
            return createIndex(parentItem->getRow(), child.column(), const_cast<PDFTreeItem*>(parentItem));
        }
    }

    return QModelIndex();
}

int PDFTreeItemModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        const PDFTreeItem* parentItem = static_cast<PDFTreeItem*>(parent.internalPointer());
        return parentItem->getChildCount();
    }

    return m_rootItem ? m_rootItem->getChildCount() : 0;
}

bool PDFTreeItemModel::hasChildren(const QModelIndex& parent) const
{
    return rowCount(parent) > 0;
}

Qt::ItemFlags PDFTreeItemModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    return QAbstractItemModel::flags(index);
}


void PDFOptionalContentTreeItemModel::setActivity(PDFOptionalContentActivity* activity)
{
    m_activity = activity;
}

int PDFOptionalContentTreeItemModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant PDFOptionalContentTreeItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    const PDFOptionalContentTreeItem* item = static_cast<const PDFOptionalContentTreeItem*>(index.internalPointer());
    switch (role)
    {
        case Qt::DisplayRole:
            return item->getText();

        case Qt::CheckStateRole:
        {
            if (item->getReference() != PDFObjectReference())
            {
                if (m_activity)
                {
                    switch (m_activity->getState(item->getReference()))
                    {
                        case OCState::ON:
                            return Qt::Checked;
                        case OCState::OFF:
                            return Qt::Unchecked;
                        case OCState::Unknown:
                            return Qt::PartiallyChecked;

                        default:
                        {
                            Q_ASSERT(false);
                            break;
                        }
                    }
                }

                return Qt::PartiallyChecked;
            }

            break;
        }

        default:
            break;
    }

    return QVariant();
}

void PDFOptionalContentTreeItemModel::update()
{
    beginResetModel();

    PDFOptionalContentTreeItem* root = new PDFOptionalContentTreeItem(nullptr, PDFObjectReference(), QString(), false);
    m_rootItem.reset(root);

    if (m_document)
    {
        const PDFOptionalContentProperties* optionalContentProperties = m_document->getCatalog()->getOptionalContentProperties();
        if (optionalContentProperties->isValid())
        {
            const PDFOptionalContentConfiguration& configuration = optionalContentProperties->getDefaultConfiguration();
            const PDFObject& orderObject = m_document->getObject(configuration.getOrder());
            const std::vector<PDFObjectReference>& ocgs = optionalContentProperties->getAllOptionalContentGroups();
            const std::vector<PDFObjectReference>& locked = configuration.getLocked();

            // We must detect cycles in the reference array
            std::set<PDFObjectReference> lockedOptionalContentGroups(locked.cbegin(), locked.cend());
            std::set<PDFObjectReference> optionalContentGroups(ocgs.cbegin(), ocgs.cend());
            std::set<PDFObjectReference> processedReferences;

            PDFDocumentDataLoaderDecorator loader(m_document);
            std::function<PDFOptionalContentTreeItem*(const PDFObject&)> processObject = [&, this](const PDFObject& object) -> PDFOptionalContentTreeItem*
            {
                PDFObject dereferencedObject = object;
                if (object.isReference())
                {
                    PDFObjectReference reference = object.getReference();
                    if (optionalContentGroups.count(reference))
                    {
                        const PDFOptionalContentGroup& ocg = optionalContentProperties->getOptionalContentGroup(reference);
                        return new PDFOptionalContentTreeItem(nullptr, reference, ocg.getName(), lockedOptionalContentGroups.count(reference));
                    }
                    else if (!processedReferences.count(reference))
                    {
                        processedReferences.insert(reference);
                        dereferencedObject = m_document->getStorage().getObject(reference);
                    }
                    else
                    {
                        // Error - we have cyclic references
                        return nullptr;
                    }
                }

                if (dereferencedObject.isArray())
                {
                    const PDFArray* array = dereferencedObject.getArray();
                    const size_t arraySize = array->getCount();

                    // We must have at least one item!
                    if (arraySize == 0)
                    {
                        return nullptr;
                    }

                    QString text;
                    size_t i = 0;

                    // Try to retrieve group name
                    const PDFObject& firstItem = m_document->getObject(array->getItem(0));
                    if (firstItem.isString())
                    {
                        text = loader.readTextString(firstItem, QString());
                        ++i;
                    }

                    std::unique_ptr<PDFOptionalContentTreeItem> parentItem(new PDFOptionalContentTreeItem(nullptr, PDFObjectReference(), text, false));
                    for (; i < arraySize; ++i)
                    {
                        if (PDFOptionalContentTreeItem* item = processObject(array->getItem(i)))
                        {
                            parentItem->addCreatedChild(item);
                        }
                        else
                        {
                            // Item cannot be parsed properly
                            return nullptr;
                        }
                    }

                    return parentItem.release();
                }

                return nullptr;
            };

            m_rootItem.reset(processObject(orderObject));
        }
    }

    endResetModel();
}

Qt::ItemFlags PDFOptionalContentTreeItemModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = PDFTreeItemModel::flags(index);

    if (!index.isValid())
    {
        return flags;
    }

    const PDFOptionalContentTreeItem* item = static_cast<const PDFOptionalContentTreeItem*>(index.internalPointer());
    if (item->getReference() != PDFObjectReference())
    {
        flags = flags | Qt::ItemIsUserCheckable | Qt::ItemNeverHasChildren;

        if (item->isLocked())
        {
            flags &= ~Qt::ItemIsEnabled;
        }
    }

    return flags;
}

QString PDFOptionalContentTreeItem::getText() const
{
    if (!m_text.isEmpty())
    {
        return m_text;
    }
    else if (getParent())
    {
        return static_cast<const PDFOptionalContentTreeItem*>(getParent())->getText();
    }

    return QString();
}

bool PDFOptionalContentTreeItemModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid())
    {
        return false;
    }

    if (m_activity && role == Qt::CheckStateRole)
    {
        const PDFOptionalContentTreeItem* item = static_cast<const PDFOptionalContentTreeItem*>(index.internalPointer());
        if (item->getReference() != PDFObjectReference() && !item->isLocked())
        {
            Qt::CheckState newState = static_cast<Qt::CheckState>(value.toInt());
            m_activity->setState(item->getReference(), (newState == Qt::Checked) ? OCState::ON : OCState::OFF);
            return true;
        }
    }

    return false;
}

PDFOutlineTreeItem::PDFOutlineTreeItem(PDFOutlineTreeItem* parent, QSharedPointer<PDFOutlineItem> outlineItem) :
    PDFTreeItem(parent),
    m_outlineItem(qMove(outlineItem))
{
    size_t childCount = m_outlineItem->getChildCount();
    for (size_t i = 0; i < childCount; ++i)
    {
        addCreatedChild(new PDFOutlineTreeItem(nullptr, m_outlineItem->getChildPtr(i)));
    }
}

int PDFOutlineTreeItemModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant PDFOutlineTreeItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    const PDFOutlineTreeItem* item = static_cast<const PDFOutlineTreeItem*>(index.internalPointer());
    const PDFOutlineItem* outlineItem = item->getOutlineItem();
    switch (role)
    {
        case Qt::DisplayRole:
            return outlineItem->getTitle();

        case Qt::TextColorRole:
            return outlineItem->getTextColor();

        case Qt::FontRole:
        {
            QFont font = QApplication::font();
            font.setBold(outlineItem->isFontBold());
            font.setItalic(outlineItem->isFontItalics());
            return font;
        }

        default:
            break;
    }

    return QString();
}

void PDFOutlineTreeItemModel::update()
{
    beginResetModel();

    QSharedPointer<PDFOutlineItem> outlineRoot;
    if (m_document)
    {
        outlineRoot = m_document->getCatalog()->getOutlineRootPtr();
    }
    if (outlineRoot)
    {
        m_rootItem.reset(new PDFOutlineTreeItem(nullptr, qMove(outlineRoot)));
    }
    else
    {
        m_rootItem.reset();
    }

    endResetModel();
}

Qt::ItemFlags PDFOutlineTreeItemModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = PDFTreeItemModel::flags(index);

    if (!index.isValid())
    {
        return flags;
    }

    const PDFOutlineTreeItem* item = static_cast<const PDFOutlineTreeItem*>(index.internalPointer());
    if (item->getChildCount() == 0)
    {
        flags = flags | Qt::ItemNeverHasChildren;
    }

    return flags;
}

}   // namespace pdf
