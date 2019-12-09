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
#include "pdfdrawspacecontroller.h"

#include <QFont>
#include <QStyle>
#include <QApplication>
#include <QMimeDatabase>
#include <QFileIconProvider>

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

    const int page = index.row();
    switch (role)
    {
        case Qt::DisplayRole:
            return QString::number(page + 1);

        case Qt::DecorationRole:
        {
            QString key = getKey(index.row());

            QPixmap pixmap;
            if (!m_thumbnailCache.find(key, pixmap))
            {
                QImage thumbnail = m_proxy->drawThumbnailImage(index.row(), m_thumbnailSize);
                if (!thumbnail.isNull())
                {
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
            break;
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
        emit layoutAboutToBeChanged();
        m_thumbnailSize = size;
        m_thumbnailCache.clear();
        emit layoutChanged();
    }
}

void PDFThumbnailsItemModel::setDocument(const PDFDocument* document)
{
    if (m_document != document)
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
        emit dataChanged(index(0, 0, QModelIndex()), index(rowCount(QModelIndex()) - 1, 0, QModelIndex()));
    }
    else
    {
        const int rowCount = this->rowCount(QModelIndex());
        for (const PDFInteger pageIndex : pages)
        {
            if (pageIndex < rowCount)
            {
                m_thumbnailCache.remove(getKey(pageIndex));
                emit dataChanged(index(pageIndex, 0, QModelIndex()), index(pageIndex, 0, QModelIndex()));
            }
        }
    }
}

QString PDFThumbnailsItemModel::getKey(int pageIndex) const
{
    return QString("PDF_THUMBNAIL_%1").arg(pageIndex);
}

}   // namespace pdf
