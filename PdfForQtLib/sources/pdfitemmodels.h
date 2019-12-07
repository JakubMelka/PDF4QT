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

#ifndef PDFITEMMODELS_H
#define PDFITEMMODELS_H

#include "pdfglobal.h"
#include "pdfobject.h"

#include <QIcon>
#include <QPixmapCache>
#include <QAbstractItemModel>

namespace pdf
{
class PDFAction;
class PDFDocument;
class PDFOutlineItem;
class PDFFileSpecification;
class PDFOptionalContentActivity;
class PDFDrawWidgetProxy;

/// Represents tree item in the GUI tree
class PDFTreeItem
{
public:
    inline explicit PDFTreeItem() = default;
    inline explicit PDFTreeItem(PDFTreeItem* parent) : m_parent(parent) { }
    virtual ~PDFTreeItem();

    template<typename T, typename... Arguments>
    inline T* addChild(Arguments&&... arguments)
    {
        T* item = new T(this, std::forward(arguments)...);
        m_children.push_back(item);
        return item;
    }

    void addCreatedChild(PDFTreeItem* item)
    {
        item->m_parent = this;
        m_children.push_back(item);
    }

    int getRow() const { return m_parent->m_children.indexOf(const_cast<PDFTreeItem*>(this)); }
    int getChildCount() const { return m_children.size(); }
    const PDFTreeItem* getChild(int index) const { return m_children.at(index); }
    const PDFTreeItem* getParent() const { return m_parent; }

private:
    PDFTreeItem* m_parent = nullptr;
    QList<PDFTreeItem*> m_children;
};

/// Root of all tree item models
class PDFFORQTLIBSHARED_EXPORT PDFTreeItemModel : public QAbstractItemModel
{
public:
    explicit PDFTreeItemModel(QObject* parent);

    void setDocument(const pdf::PDFDocument* document);

    bool isEmpty() const;

    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual bool hasChildren(const QModelIndex& parent) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual void update() = 0;

protected:
    const PDFDocument* m_document;
    std::unique_ptr<PDFTreeItem> m_rootItem;
};

class PDFOptionalContentTreeItem : public PDFTreeItem
{
public:
    inline explicit PDFOptionalContentTreeItem(PDFOptionalContentTreeItem* parent, PDFObjectReference reference, QString text, bool locked) :
        PDFTreeItem(parent),
        m_reference(reference),
        m_text(qMove(text)),
        m_locked(locked)
    {

    }

    PDFObjectReference getReference() const { return m_reference; }
    QString getText() const;
    bool isLocked() const { return m_locked; }

private:
    PDFObjectReference m_reference; ///< Reference to optional content group
    QString m_text; ///< Node display name
    bool m_locked; ///< Node is locked (user can't change it)
};

class PDFFORQTLIBSHARED_EXPORT PDFOptionalContentTreeItemModel : public PDFTreeItemModel
{
    Q_OBJECT
public:
    inline explicit PDFOptionalContentTreeItemModel(QObject* parent) :
        PDFTreeItemModel(parent),
        m_activity(nullptr)
    {

    }

    void setActivity(PDFOptionalContentActivity* activity);

    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual void update() override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role) override;

private:
    PDFOptionalContentActivity* m_activity;
};

class PDFOutlineTreeItem : public PDFTreeItem
{
public:
    explicit PDFOutlineTreeItem(PDFOutlineTreeItem* parent, QSharedPointer<PDFOutlineItem> outlineItem);

    const PDFOutlineItem* getOutlineItem() const { return m_outlineItem.data(); }

private:
    QSharedPointer<PDFOutlineItem> m_outlineItem;
};

class PDFFORQTLIBSHARED_EXPORT PDFOutlineTreeItemModel : public PDFTreeItemModel
{
    Q_OBJECT
public:
    PDFOutlineTreeItemModel(QIcon icon, QObject* parent) :
        PDFTreeItemModel(parent),
        m_icon(qMove(icon))
    {

    }

    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual void update() override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

    /// Returns action assigned to the index. If index is invalid, or
    /// points to the invalid item, nullptr is returned.
    /// \param index Index of the outline item
    const PDFAction* getAction(const QModelIndex& index) const;

private:
    QIcon m_icon;
};

class PDFAttachmentsTreeItem : public PDFTreeItem
{
public:
    explicit PDFAttachmentsTreeItem(PDFAttachmentsTreeItem* parent, QIcon icon, QString title, QString description, const PDFFileSpecification* fileSpecification) :
        PDFTreeItem(parent),
        m_icon(qMove(icon)),
        m_title(qMove(title)),
        m_description(qMove(description)),
        m_fileSpecification(fileSpecification)
    {

    }

    const QIcon& getIcon() const { return m_icon; }
    const QString& getTitle() const { return m_title; }
    const QString& getDescription() const { return m_description; }
    const PDFFileSpecification* getFileSpecification() const { return m_fileSpecification; }

private:
    QIcon m_icon;
    QString m_title;
    QString m_description;
    const PDFFileSpecification* m_fileSpecification;
};

class PDFFORQTLIBSHARED_EXPORT PDFAttachmentsTreeItemModel : public PDFTreeItemModel
{
    Q_OBJECT
public:
    PDFAttachmentsTreeItemModel(QObject* parent) :
        PDFTreeItemModel(parent)
    {

    }

    enum Column
    {
        Title,
        Description,
        EndColumn
    };

    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual void update() override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;

    const PDFFileSpecification* getFileSpecification(const QModelIndex& index) const;
};

class PDFFORQTLIBSHARED_EXPORT PDFThumbnailsItemModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit inline PDFThumbnailsItemModel(const PDFDrawWidgetProxy* proxy, QObject* parent);

    bool isEmpty() const;

    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

    void setThumbnailsSize(int size);
    void setDocument(const PDFDocument* document);

    /// Sets the extra item width/height for size hint. This space will be added to the size hint (pixmap size)
    void setExtraItemSizeHint(int width, int height) { m_extraItemWidthHint = width; m_extraItemHeighHint = height; }

private:
    void onPageImageChanged(bool all, const std::vector<PDFInteger>& pages);

    /// Returns generated key for page index
    QString getKey(int pageIndex) const;

    const PDFDrawWidgetProxy* m_proxy;
    int m_thumbnailSize;
    int m_extraItemWidthHint;
    int m_extraItemHeighHint;
    int m_pageCount;
    const PDFDocument* m_document;
    QPixmapCache m_thumbnailCache;
};

}   // namespace pdf

#endif // PDFITEMMODELS_H
