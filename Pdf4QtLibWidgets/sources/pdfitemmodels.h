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

#ifndef PDFITEMMODELS_H
#define PDFITEMMODELS_H

#include "pdfwidgetsglobal.h"
#include "pdfglobal.h"
#include "pdfobject.h"

#include <QIcon>
#include <QPixmapCache>
#include <QAbstractItemModel>

#include <set>

namespace pdf
{
class PDFAction;
class PDFDocument;
class PDFOutlineItem;
class PDFModifiedDocument;
class PDFFileSpecification;
class PDFOptionalContentActivity;
class PDFDrawWidgetProxy;
class PDFDestination;

/// Represents tree item in the GUI tree
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFTreeItem
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

    template<typename T, typename... Arguments>
    inline T* insertChild(int position, Arguments&&... arguments)
    {
        T* item = new T(this, std::forward(arguments)...);
        m_children.insert(std::next(m_children.begin(), position), item);
        return item;
    }

    void insertCreatedChild(int position, PDFTreeItem* item)
    {
        item->m_parent = this;
        m_children.insert(std::next(m_children.begin(), position), item);
    }

    void addCreatedChild(PDFTreeItem* item)
    {
        item->m_parent = this;
        m_children.push_back(item);
    }

    int getRow() const { return m_parent->m_children.indexOf(const_cast<PDFTreeItem*>(this)); }
    int getChildCount() const { return m_children.size(); }
    const PDFTreeItem* getChild(int index) const { return m_children.at(index); }
    PDFTreeItem* getChild(int index) { return m_children.at(index); }
    const PDFTreeItem* getParent() const { return m_parent; }
    PDFTreeItem* getParent() { return m_parent; }
    PDFTreeItem* takeChild(int index);

private:
    PDFTreeItem* m_parent = nullptr;
    QList<PDFTreeItem*> m_children;
};

/// Root of all tree item models. Reimplementations of this model
/// must handle "soft" document updates, such as only annotations changed etc.
/// Model should be rebuilded only, if it is neccessary.
class PDF4QTLIBWIDGETSSHARED_EXPORT PDFTreeItemModel : public QAbstractItemModel
{
public:
    explicit PDFTreeItemModel(QObject* parent);

    void setDocument(const PDFModifiedDocument& document);

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

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFOptionalContentTreeItemModel : public PDFTreeItemModel
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
    PDFOutlineItem* getOutlineItem() { return m_outlineItem.data(); }

private:
    QSharedPointer<PDFOutlineItem> m_outlineItem;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFOutlineTreeItemModel : public PDFTreeItemModel
{
    Q_OBJECT
public:
    PDFOutlineTreeItemModel(QIcon icon, bool editable, QObject* parent) :
        PDFTreeItemModel(parent),
        m_icon(qMove(icon)),
        m_editable(editable)
    {

    }

    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual void update() override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    virtual Qt::DropActions supportedDropActions() const override;
    virtual Qt::DropActions supportedDragActions() const override;
    virtual bool insertRows(int row, int count, const QModelIndex& parent) override;
    virtual bool removeRows(int row, int count, const QModelIndex& parent) override;
    virtual bool moveRows(const QModelIndex& sourceParent, int sourceRow, int count, const QModelIndex& destinationParent, int destinationChild) override;
    virtual QStringList mimeTypes() const override;
    virtual QMimeData* mimeData(const QModelIndexList& indexes) const override;
    virtual bool canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const override;
    virtual bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) override;

    /// Returns action assigned to the index. If index is invalid, or
    /// points to the invalid item, nullptr is returned.
    /// \param index Index of the outline item
    const PDFAction* getAction(const QModelIndex& index) const;

    /// Returns the outline item for the given index, or nullptr
    /// if no outline item is assigned to that index.
    const PDFOutlineItem* getOutlineItem(const QModelIndex& index) const;

    /// Returns the outline item for the given index, or nullptr
    /// if no outline item is assigned to that index.
    PDFOutlineItem* getOutlineItem(const QModelIndex& index);

    void setFontBold(const QModelIndex& index, bool value);
    void setFontItalics(const QModelIndex& index, bool value);
    void setDestination(const QModelIndex& index, const PDFDestination& destination);

    const PDFOutlineItem* getRootOutlineItem() const;

    bool isEditable() const { return m_editable; }

private:
    QIcon m_icon;
    bool m_editable;
    mutable QSharedPointer<PDFOutlineItem> m_dragDropItem;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFSelectableOutlineTreeItemModel : public PDFOutlineTreeItemModel
{
    Q_OBJECT

private:
    using BaseClass = PDFOutlineTreeItemModel;

public:
    PDFSelectableOutlineTreeItemModel(QIcon icon, QObject* parent) :
        BaseClass(qMove(icon), false, parent)
    {

    }

    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual void update() override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    virtual bool setData(const QModelIndex& index, const QVariant& value, int role) override;

    std::vector<const PDFOutlineItem*> getSelectedItems() const;

private:
    std::set<const PDFOutlineItem*> m_selectedItems;
};

class PDFAttachmentsTreeItem : public PDFTreeItem
{
public:
    explicit PDFAttachmentsTreeItem(PDFAttachmentsTreeItem* parent, QIcon icon, QString title, QString description, const PDFFileSpecification* fileSpecification);
    virtual ~PDFAttachmentsTreeItem() override;

    const QIcon& getIcon() const { return m_icon; }
    const QString& getTitle() const { return m_title; }
    const QString& getDescription() const { return m_description; }
    const PDFFileSpecification* getFileSpecification() const { return m_fileSpecification.get(); }

private:
    QIcon m_icon;
    QString m_title;
    QString m_description;
    std::unique_ptr<PDFFileSpecification> m_fileSpecification;
};

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFAttachmentsTreeItemModel : public PDFTreeItemModel
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

class PDF4QTLIBWIDGETSSHARED_EXPORT PDFThumbnailsItemModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    explicit PDFThumbnailsItemModel(const PDFDrawWidgetProxy* proxy, QObject* parent);

    bool isEmpty() const;

    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;

    void setThumbnailsSize(int size);
    void setDocument(const PDFModifiedDocument& document);

    /// Sets the extra item width/height for size hint. This space will be added to the size hint (pixmap size)
    void setExtraItemSizeHint(int width, int height) { m_extraItemWidthHint = width; m_extraItemHeighHint = height; }

    PDFInteger getPageIndex(const QModelIndex& index) const;

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
