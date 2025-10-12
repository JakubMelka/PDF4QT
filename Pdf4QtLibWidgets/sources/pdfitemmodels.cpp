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

#include "pdfitemmodels.h"
#include "pdfdocument.h"
#include "pdfdrawspacecontroller.h"
#include "pdfdrawwidget.h"
#include "pdfwidgetutils.h"

#include <QFont>
#include <QStyle>
#include <QApplication>
#include <QMimeDatabase>
#include <QFileIconProvider>
#include <QMimeData>

#include "pdfdbgheap.h"

namespace pdf
{

PDFTreeItem::~PDFTreeItem()
{
    qDeleteAll(m_children);
}

PDFTreeItem* PDFTreeItem::takeChild(int index)
{
    PDFTreeItem* item = m_children.at(index);
    m_children.erase(m_children.begin() + index);
    return item;
}

PDFTreeItemModel::PDFTreeItemModel(QObject* parent) :
    QAbstractItemModel(parent),
    m_document(nullptr)
{

}

void PDFTreeItemModel::setDocument(const PDFModifiedDocument& document)
{
    if (m_document != document)
    {
        m_document = document;

        // We must only update only, when document has been reset, i.e.
        // all document content has been changed.
        if (document.hasReset())
        {
            update();
        }
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
        case Qt::EditRole:
            return outlineItem->getTitle();

        case Qt::ForegroundRole:
        {
            // We do not set item color if the dark theme is set
            if (pdf::PDFWidgetUtils::isDarkTheme())
            {
                return QVariant();
            }
            return QBrush(outlineItem->getTextColor());
        }

        case Qt::FontRole:
        {
            QFont font = QApplication::font();
            font.setPointSize(10);
            font.setBold(outlineItem->isFontBold());
            font.setItalic(outlineItem->isFontItalics());
            return font;
        }

        case Qt::DecorationRole:
        {
            if (!m_icon.isNull())
            {
                return m_icon;
            }
            break;
        }

        default:
            break;
    }

    return QVariant();
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
        if (m_editable)
        {
            outlineRoot = outlineRoot->clone();
        }

        m_rootItem.reset(new PDFOutlineTreeItem(nullptr, qMove(outlineRoot)));
    }
    else
    {
        if (m_editable && m_document)
        {
            outlineRoot.reset(new pdf::PDFOutlineItem());
            m_rootItem.reset(new PDFOutlineTreeItem(nullptr, qMove(outlineRoot)));
        }
        else
        {
            m_rootItem.reset();
        }
    }

    endResetModel();
}

Qt::ItemFlags PDFOutlineTreeItemModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = PDFTreeItemModel::flags(index);

    if (!index.isValid())
    {
        if (m_editable)
        {
            flags = flags | Qt::ItemIsDropEnabled;
        }
        return flags;
    }

    if (m_editable)
    {
        flags = flags | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    }

    return flags;
}

const PDFAction* PDFOutlineTreeItemModel::getAction(const QModelIndex& index) const
{
    if (index.isValid())
    {
        const PDFOutlineTreeItem* item = static_cast<const PDFOutlineTreeItem*>(index.internalPointer());
        const PDFOutlineItem* outlineItem = item->getOutlineItem();
        return outlineItem->getAction();
    }

    return nullptr;
}

const PDFOutlineItem* PDFOutlineTreeItemModel::getOutlineItem(const QModelIndex& index) const
{
    if (index.isValid())
    {
        const PDFOutlineTreeItem* item = static_cast<const PDFOutlineTreeItem*>(index.internalPointer());
        const PDFOutlineItem* outlineItem = item->getOutlineItem();
        return outlineItem;
    }

    return nullptr;
}

PDFOutlineItem* PDFOutlineTreeItemModel::getOutlineItem(const QModelIndex& index)
{
    if (index.isValid())
    {
        PDFOutlineTreeItem* item = static_cast<PDFOutlineTreeItem*>(index.internalPointer());
        PDFOutlineItem* outlineItem = item->getOutlineItem();
        return outlineItem;
    }

    return nullptr;
}

void PDFOutlineTreeItemModel::setFontBold(const QModelIndex& index, bool value)
{
    if (PDFOutlineItem* outlineItem = getOutlineItem(index))
    {
        if (outlineItem->isFontBold() != value)
        {
            outlineItem->setFontBold(value);
            Q_EMIT dataChanged(index, index);
        }
    }
}

void PDFOutlineTreeItemModel::setFontItalics(const QModelIndex& index, bool value)
{
    if (PDFOutlineItem* outlineItem = getOutlineItem(index))
    {
        if (outlineItem->isFontItalics() != value)
        {
            outlineItem->setFontItalics(value);
            Q_EMIT dataChanged(index, index);
        }
    }
}

void PDFOutlineTreeItemModel::setDestination(const QModelIndex& index, const PDFDestination& destination)
{
    if (PDFOutlineItem* outlineItem = getOutlineItem(index))
    {
        outlineItem->setAction(PDFActionPtr(new PDFActionGoTo(destination, PDFDestination())));
        Q_EMIT dataChanged(index, index);
    }
}

const PDFOutlineItem* PDFOutlineTreeItemModel::getRootOutlineItem() const
{
    PDFOutlineTreeItem* item = static_cast<PDFOutlineTreeItem*>(m_rootItem.get());
    return item->getOutlineItem();
}

bool PDFOutlineTreeItemModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!m_editable || !index.isValid() || role != Qt::EditRole)
    {
        return false;
    }

    PDFOutlineTreeItem* item = static_cast<PDFOutlineTreeItem*>(index.internalPointer());
    PDFOutlineItem* outlineItem = item->getOutlineItem();
    if (outlineItem->getTitle() != value.toString())
    {
        outlineItem->setTitle(value.toString());
        Q_EMIT dataChanged(index, index);
    }

    return true;
}

Qt::DropActions PDFOutlineTreeItemModel::supportedDropActions() const
{
    if (!m_editable)
    {
        return Qt::IgnoreAction;
    }

    return Qt::CopyAction | Qt::MoveAction;
}

Qt::DropActions PDFOutlineTreeItemModel::supportedDragActions() const
{
    if (!m_editable)
    {
        return Qt::IgnoreAction;
    }

    return Qt::CopyAction | Qt::MoveAction;
}

QStringList PDFOutlineTreeItemModel::mimeTypes() const
{
    return QStringList() << "application/PDF4QT_PDFOutlineTreeItemModel";
}

QMimeData* PDFOutlineTreeItemModel::mimeData(const QModelIndexList& indexes) const
{
    QMimeData* mimeData = new QMimeData();

    if (indexes.size() == 1)
    {
        QByteArray ba;

        {
            QModelIndex index = indexes.front();
            const PDFOutlineTreeItem* item = static_cast<const PDFOutlineTreeItem*>(index.internalPointer());
            m_dragDropItem = item->getOutlineItem()->clone();
            QDataStream stream(&ba, QDataStream::WriteOnly);
            stream << reinterpret_cast<quintptr>(m_dragDropItem.data());
        }

        mimeData->setData(mimeTypes().front(), ba);
    }

    return mimeData;
}

bool PDFOutlineTreeItemModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(row);
    Q_UNUSED(column);
    Q_UNUSED(parent);

    return action == Qt::MoveAction && data->hasFormat(mimeTypes().front());
}

bool PDFOutlineTreeItemModel::dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent)
{
    if (!data || action != Qt::MoveAction)
    {
        return false;
    }

    QByteArray pointerData = data->data(mimeTypes().front());
    QDataStream stream(pointerData);
    quintptr pointer = 0;
    stream >> pointer;

    PDFOutlineItem* item = reinterpret_cast<PDFOutlineItem*>(pointer);
    if (item == m_dragDropItem.data())
    {
        if (row == -1)
        {
            row = rowCount(parent);
        }

        if (column == -1)
        {
            column = 0;
        }

        if (insertRow(row, parent))
        {
            QModelIndex targetIndex = this->index(row, column, parent);
            PDFOutlineTreeItem* targetTreeItem = static_cast<PDFOutlineTreeItem*>(targetIndex.internalPointer());
            PDFOutlineItem* targetOutlineItem = targetTreeItem->getOutlineItem();
            *targetOutlineItem = *item;

            while (targetTreeItem->getChildCount() > 0)
            {
                delete targetTreeItem->takeChild(0);
            }

            const size_t childCount = targetOutlineItem->getChildCount();
            for (size_t i = 0; i < childCount; ++i)
            {
                targetTreeItem->addCreatedChild(new PDFOutlineTreeItem(nullptr, targetOutlineItem->getChildPtr(i)));
            }

            return true;
        }
    }

    return false;
}

bool PDFOutlineTreeItemModel::insertRows(int row, int count, const QModelIndex& parent)
{
    if (!m_editable || row < 0 || count <= 0 || row > rowCount(parent))
    {
        return false;
    }

    beginInsertRows(parent, row, row + count - 1);

    PDFOutlineTreeItem* item = parent.isValid() ? static_cast<PDFOutlineTreeItem*>(parent.internalPointer()) : static_cast<PDFOutlineTreeItem*>(m_rootItem.get());
    while (count > 0)
    {
        QSharedPointer<PDFOutlineItem> outlineItem(new PDFOutlineItem());
        outlineItem->setTitle(tr("Item %1").arg(row + 1));
        PDFOutlineTreeItem* newTreeItem = new PDFOutlineTreeItem(item, outlineItem);
        item->insertCreatedChild(row, newTreeItem);

        PDFOutlineItem* parentItem = item->getOutlineItem();
        parentItem->insertChild(row, outlineItem);

        ++row;
        --count;
    }

    endInsertRows();

    return true;
}

bool PDFOutlineTreeItemModel::removeRows(int row, int count, const QModelIndex& parent)
{
    if (!m_editable || count <= 0 || row < 0 || row + count > rowCount(parent))
    {
        return false;
    }

    beginRemoveRows(parent, row, row + count - 1);

    PDFOutlineTreeItem* item = parent.isValid() ? static_cast<PDFOutlineTreeItem*>(parent.internalPointer()) : static_cast<PDFOutlineTreeItem*>(m_rootItem.get());
    while (count > 0)
    {
        item->getOutlineItem()->removeChild(row);
        delete item->takeChild(row);
        --count;
    }

    endRemoveRows();

    return false;
}

bool PDFOutlineTreeItemModel::moveRows(const QModelIndex& sourceParent, int sourceRow, int count, const QModelIndex& destinationParent, int destinationChild)
{
    if (!m_editable || count <= 0 || sourceRow < 0 || !m_rootItem)
    {
        return false;
    }

    PDFOutlineTreeItem* sourceNode = sourceParent.isValid()
        ? static_cast<PDFOutlineTreeItem*>(sourceParent.internalPointer())
        : static_cast<PDFOutlineTreeItem*>(m_rootItem.get());
    PDFOutlineTreeItem* destNode = destinationParent.isValid()
        ? static_cast<PDFOutlineTreeItem*>(destinationParent.internalPointer())
        : static_cast<PDFOutlineTreeItem*>(m_rootItem.get());

    if (!sourceNode || !destNode)
    {
        return false;
    }

    PDFOutlineItem* sourceOutline = sourceNode->getOutlineItem();
    PDFOutlineItem* destOutline = destNode->getOutlineItem();
    if (!sourceOutline || !destOutline)
    {
        return false;
    }

    if (sourceRow + count > sourceNode->getChildCount())
    {
        return false;
    }

    auto* firstMovedNode = static_cast<PDFOutlineTreeItem*>(sourceNode->getChild(sourceRow));
    for (PDFOutlineTreeItem* ancestor = destNode; ancestor; ancestor = static_cast<PDFOutlineTreeItem*>(ancestor->getParent()))
    {
        if (ancestor == firstMovedNode)
        {
            return false;
        }
    }

    int moveDestination = destinationChild;
    const int destChildCount = destNode->getChildCount();
    if (moveDestination < 0 || moveDestination > destChildCount)
    {
        moveDestination = destChildCount;
    }

    if (sourceParent == destinationParent && moveDestination >= sourceRow && moveDestination <= sourceRow + count)
    {
        return false;
    }

    if (!beginMoveRows(sourceParent, sourceRow, sourceRow + count - 1, destinationParent, moveDestination))
    {
        return false;
    }

    int insertionRow = moveDestination;
    if (sourceParent == destinationParent && moveDestination > sourceRow)
    {
        insertionRow -= count;
    }

    QList<QSharedPointer<PDFOutlineItem>> outlineItems;
    outlineItems.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        outlineItems.append(sourceOutline->getChildPtr(static_cast<size_t>(sourceRow + i)));
    }

    QList<PDFOutlineTreeItem*> treeItems;
    treeItems.reserve(count);
    for (int i = count - 1; i >= 0; --i)
    {
        const int index = sourceRow + i;
        treeItems.prepend(static_cast<PDFOutlineTreeItem*>(sourceNode->takeChild(index)));
        sourceOutline->removeChild(static_cast<size_t>(index));
    }

    for (int i = 0; i < count; ++i)
    {
        destNode->insertCreatedChild(insertionRow + i, treeItems.at(i));
        destOutline->insertChild(static_cast<size_t>(insertionRow + i), outlineItems.at(i));
    }

    endMoveRows();
    return true;
}

int PDFAttachmentsTreeItemModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return EndColumn;
}

QVariant PDFAttachmentsTreeItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
    {
        return QVariant();
    }

    const PDFAttachmentsTreeItem* item = static_cast<const PDFAttachmentsTreeItem*>(index.internalPointer());
    switch (role)
    {
        case Qt::DisplayRole:
        {
            switch (index.column())
            {
                case Title:
                    return item->getTitle();

                case Description:
                    return item->getDescription();

                default:
                    Q_ASSERT(false);
                    break;
            }
            break;
        }

        case Qt::DecorationRole:
        {
            switch (index.column())
            {
                case Title:
                    return item->getIcon();

                case Description:
                    return QVariant();

                default:
                    Q_ASSERT(false);
                    break;
            }
            break;
        }

        default:
            break;
    }

    return QVariant();
}

void PDFAttachmentsTreeItemModel::update()
{
    beginResetModel();

    m_rootItem.reset();
    if (m_document)
    {
        const std::map<QByteArray, PDFFileSpecification>& embeddedFiles = m_document->getCatalog()->getEmbeddedFiles();
        if (!embeddedFiles.empty())
        {
            QMimeDatabase mimeDatabase;
            QFileIconProvider fileIconProvider;
            fileIconProvider.setOptions(QFileIconProvider::DontUseCustomDirectoryIcons);
            PDFAttachmentsTreeItem* root = new PDFAttachmentsTreeItem(nullptr, QIcon(), QString(), QString(), nullptr);
            m_rootItem.reset(root);

            std::map<QString, PDFAttachmentsTreeItem*> subroots;

            for (const auto& embeddedFile : embeddedFiles)
            {
                const PDFFileSpecification* specification = &embeddedFile.second;
                const PDFEmbeddedFile* file = specification->getPlatformFile();
                if (!file)
                {
                    continue;
                }

                QString fileName = specification->getPlatformFileName();
                QString description = specification->getDescription();

                // Jakub Melka: try to obtain mime type from subtype, if it fails, then form file name.
                // We do not obtain type from data, because it can be slow (files can be large).
                QMimeType type = mimeDatabase.mimeTypeForName(file->getSubtype());
                if (!type.isValid())
                {
                    type = mimeDatabase.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
                }

                // Get icon and select folder, to which file belongs
                QIcon icon;
                QString fileTypeName = "@GENERIC";
                QString fileTypeDescription = tr("Files");
                if (type.isValid())
                {
                    icon = QIcon::fromTheme(type.iconName());
                    if (icon.isNull())
                    {
                        icon = QIcon::fromTheme(type.genericIconName());
                    }

                    fileTypeName = type.name();
                    fileTypeDescription = type.comment();
                }

                if (icon.isNull())
                {
                    icon = fileIconProvider.icon(QFileInfo(fileName));
                }
                if (icon.isNull())
                {
                    icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
                }

                // Create subroot
                PDFAttachmentsTreeItem* subroot = nullptr;
                auto it = subroots.find(fileTypeName);
                if (it == subroots.cend())
                {
                    QIcon folderIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
                    subroot = new PDFAttachmentsTreeItem(nullptr, qMove(folderIcon), fileTypeDescription, QString(), nullptr);
                    root->addCreatedChild(subroot);
                    subroots[fileTypeName] = subroot;
                }
                else
                {
                    subroot = it->second;
                }

                // Create item
                subroot->addCreatedChild(new PDFAttachmentsTreeItem(nullptr, qMove(icon), qMove(fileName), qMove(description), specification));
            }
        }
    }

    endResetModel();
}

Qt::ItemFlags PDFAttachmentsTreeItemModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
    {
        return Qt::NoItemFlags;
    }

    if (rowCount(index) > 0)
    {
        return Qt::ItemIsEnabled;
    }

    if (index.column() == Title)
    {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemNeverHasChildren;
    }

    return Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
}

const PDFFileSpecification* PDFAttachmentsTreeItemModel::getFileSpecification(const QModelIndex& index) const
{
    if (index.isValid())
    {
        return static_cast<const PDFAttachmentsTreeItem*>(index.internalPointer())->getFileSpecification();
    }

    return nullptr;
}

PDFThumbnailsItemModel::PDFThumbnailsItemModel(const PDFDrawWidgetProxy* proxy, QObject* parent) :
    QAbstractItemModel(parent),
    m_proxy(proxy),
    m_thumbnailSize(100),
    m_extraItemWidthHint(0),
    m_extraItemHeighHint(0),
    m_pageCount(0),
    m_document(nullptr)
{
    connect(proxy, &PDFDrawWidgetProxy::pageImageChanged, this, &PDFThumbnailsItemModel::onPageImageChanged);
}

bool PDFThumbnailsItemModel::isEmpty() const
{
    return rowCount(QModelIndex()) == 0;
}

QModelIndex PDFThumbnailsItemModel::index(int row, int column, const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        return QModelIndex();
    }

    return createIndex(row, column, nullptr);
}

QModelIndex PDFThumbnailsItemModel::parent(const QModelIndex& child) const
{
    Q_UNUSED(child);
    return QModelIndex();
}

int PDFThumbnailsItemModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
    {
        return 0;
    }

    return m_pageCount;
}

int PDFThumbnailsItemModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant PDFThumbnailsItemModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_document)
    {
        return QVariant();
    }

    const int pageIndex = index.row();
    switch (role)
    {
        case Qt::DisplayRole:
            return QString::number(pageIndex + 1);

        case Qt::DecorationRole:
        {
            QString key = getKey(index.row());

            QPixmap pixmap;
            if (!m_thumbnailCache.find(key, &pixmap))
            {
                const qreal devicePixelRatio = m_proxy->getWidget()->devicePixelRatioF();
                QImage thumbnail = m_proxy->drawThumbnailImage(index.row(), m_thumbnailSize * devicePixelRatio);
                if (!thumbnail.isNull())
                {
                    thumbnail.setDevicePixelRatio(devicePixelRatio);
                    pixmap = QPixmap::fromImage(qMove(thumbnail));
                    m_thumbnailCache.insert(key, pixmap);
                }
            }

            return pixmap;
        }

        case Qt::TextAlignmentRole:
        {
            return int(Qt::AlignHCenter | Qt::AlignBottom);
        }

        case Qt::SizeHintRole:
        {
            const PDFPage* page = m_document->getCatalog()->getPage(index.row());
            QSizeF pageSize = page->getRotatedMediaBox().size();
            pageSize.scale(m_thumbnailSize, m_thumbnailSize, Qt::KeepAspectRatio);
            return pageSize.toSize() + QSize(m_extraItemWidthHint, m_extraItemHeighHint);
        }

        default:
            break;
    }

    return QVariant();
}

void PDFThumbnailsItemModel::setThumbnailsSize(int size)
{
    if (m_thumbnailSize != size)
    {
        Q_EMIT layoutAboutToBeChanged();
        m_thumbnailSize = size;
        m_thumbnailCache.clear();
        Q_EMIT layoutChanged();
    }
}

void PDFThumbnailsItemModel::setDocument(const PDFModifiedDocument& document)
{
    if (m_document != document)
    {
        if (document.hasReset() || document.hasPageContentsChanged() || document.hasFlag(PDFModifiedDocument::Annotation))
        {
            beginResetModel();
            m_thumbnailCache.clear();
            m_document = document;

            m_pageCount = 0;
            if (m_document)
            {
                m_pageCount = static_cast<int>(m_document->getCatalog()->getPageCount());
            }
            endResetModel();
        }
        else
        {
            // Soft reset
            m_document = document;
            Q_ASSERT(!m_document || m_pageCount == static_cast<int>(m_document->getCatalog()->getPageCount()));
        }
    }
}

PDFInteger PDFThumbnailsItemModel::getPageIndex(const QModelIndex& index) const
{
    return index.row();
}

void PDFThumbnailsItemModel::onPageImageChanged(bool all, const std::vector<PDFInteger>& pages)
{
    Q_UNUSED(all);
    Q_UNUSED(pages);

    if (all)
    {
        m_thumbnailCache.clear();
        Q_EMIT dataChanged(index(0, 0, QModelIndex()), index(rowCount(QModelIndex()) - 1, 0, QModelIndex()));
    }
    else
    {
        const int rowCount = this->rowCount(QModelIndex());
        for (const PDFInteger pageIndex : pages)
        {
            if (pageIndex < rowCount)
            {
                m_thumbnailCache.remove(getKey(pageIndex));
                Q_EMIT dataChanged(index(pageIndex, 0, QModelIndex()), index(pageIndex, 0, QModelIndex()));
            }
        }
    }
}

QString PDFThumbnailsItemModel::getKey(int pageIndex) const
{
    return QString("PDF_THUMBNAIL_%1").arg(pageIndex);
}

PDFAttachmentsTreeItem::PDFAttachmentsTreeItem(PDFAttachmentsTreeItem* parent, QIcon icon, QString title, QString description, const PDFFileSpecification* fileSpecification) :
    PDFTreeItem(parent),
    m_icon(qMove(icon)),
    m_title(qMove(title)),
    m_description(qMove(description)),
    m_fileSpecification(nullptr)
{
    if (fileSpecification)
    {
        m_fileSpecification = std::make_unique<PDFFileSpecification>(*fileSpecification);
    }
}

PDFAttachmentsTreeItem::~PDFAttachmentsTreeItem()
{

}

QVariant PDFSelectableOutlineTreeItemModel::data(const QModelIndex& index, int role) const
{
    if (role == Qt::CheckStateRole)
    {
        if (!index.isValid())
        {
            return QVariant();
        }

        const PDFOutlineTreeItem* item = static_cast<const PDFOutlineTreeItem*>(index.internalPointer());
        const PDFOutlineItem* outlineItem = item->getOutlineItem();
        const bool isChecked = m_selectedItems.count(outlineItem);
        return isChecked ? Qt::Checked : Qt::Unchecked;
    }

    return BaseClass::data(index, role);
}

void PDFSelectableOutlineTreeItemModel::update()
{
    BaseClass::update();
    m_selectedItems.clear();
}

Qt::ItemFlags PDFSelectableOutlineTreeItemModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags flags = BaseClass::flags(index);

    if (index.isValid())
    {
        flags |= Qt::ItemIsUserCheckable;
    }

    return flags;
}

bool PDFSelectableOutlineTreeItemModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::CheckStateRole || !index.isValid())
    {
        return false;
    }

    const PDFOutlineTreeItem* item = static_cast<const PDFOutlineTreeItem*>(index.internalPointer());
    const PDFOutlineItem* outlineItem = item->getOutlineItem();

    if (outlineItem)
    {
        if (value.toInt() == Qt::Checked)
        {
            m_selectedItems.insert(outlineItem);
        }
        else
        {
            m_selectedItems.erase(outlineItem);
        }

        return true;
    }

    return false;
}

std::vector<const PDFOutlineItem*> PDFSelectableOutlineTreeItemModel::getSelectedItems() const
{
    return std::vector<const PDFOutlineItem*>(m_selectedItems.cbegin(), m_selectedItems.cend());
}

}   // namespace pdf
