// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#ifndef PDFAPPOBJECTTREEMODEL_H
#define PDFAPPOBJECTTREEMODEL_H

#include "pdfappglobal.h"
#include "pdfobjectinspector.h"

#include <QAbstractItemModel>

#include <memory>
#include <vector>

namespace pdf
{

/// The lazy tree of Object Inspector (plan of step 7 of phase 2, 4.3; package P2): the roots of a view of
/// PDFObjectInspector, the children fetched on request (hasChildren / canFetchMore / fetchMore over
/// PDFObjectInspector::getChildren - never the whole graph in the GUI thread, B158), the one set of the opened
/// references of the view (a repeated reference is shown, not expanded: "A reference is opened only once…"),
/// a path at most PDFObjectInspector::MAXIMUM_PATH_DEPTH deep (the core gives no children below it). The roots
/// of a large view (the Object List) come in parts of ROOT_BATCH (canFetchMore of the invisible root), the children
/// of a node in parts of CHILD_BATCH (RS7APP: an array or a dictionary of millions of entries of a hostile file is
/// never fetched whole in the GUI thread); the next part of a node comes, when the last loaded child of it is shown
/// (from the event loop) or when the view asks (canFetchMore / fetchMore). Nothing is decoded here (the GUI decodes
/// in its job). The texts of a row: "/Root 1 0 R  Dictionary [6 items]", "/Size 12406", "Trailer  Dictionary [5
/// items]" (482:14847), "4 0 R  Dictionary [6 items]" (482:15358).
///
/// GUI thread only. The inspector is shared (std::shared_ptr of an immutable object prepared by the job of the
/// GUI); the items are owned by the model (std::unique_ptr, at most MAXIMUM_PATH_DEPTH deep).
class PDF4QTLIBAPPSHARED_EXPORT PDFAppObjectTreeModel : public QAbstractItemModel
{
    Q_OBJECT

private:
    using BaseClass = QAbstractItemModel;

public:
    /// The roots fetched at once (the Object List of a document of millions of objects)
    static constexpr int ROOT_BATCH = 1000;

    /// The children of a node fetched at once (an array of millions of items of a hostile file)
    static constexpr int CHILD_BATCH = 1000;

    explicit PDFAppObjectTreeModel(QObject* parent = nullptr);
    virtual ~PDFAppObjectTreeModel() override;

    /// Shows the view of the inspector (a new set of the opened references); null = an empty tree
    void setInspector(std::shared_ptr<const PDFObjectInspector> inspector, PDFObjectInspectorView view);

    const std::shared_ptr<const PDFObjectInspector>& getInspector() const { return m_inspector; }
    PDFObjectInspectorView getView() const { return m_view; }

    /// The node of a row, nullptr for an invalid index
    const PDFObjectInspectorNode* getNode(const QModelIndex& index) const;

    /// The row of the indirect object among the rows already loaded (the selection restored after a change of
    /// the document); a root first, else the first loaded row of the object; invalid = none
    QModelIndex findReference(PDFObjectReference reference) const;

    /// Number of the roots of the view and of the roots loaded
    qint64 getRootCount() const { return m_rootCount; }
    int getLoadedRootCount() const { return int(m_roots.size()); }

    /// The text of a row of a node ("/Root 1 0 R  Dictionary [6 items]")
    /// \param inspector The inspector (the description of a container, the syntax of a value)
    /// \param node The node
    static QString getNodeText(const PDFObjectInspector* inspector, const PDFObjectInspectorNode& node);

    /// The key part of a row ("/Root", "[3]", "Trailer"; empty for a root of an object)
    static QString getKeyText(const PDFObjectInspectorNode& node);

    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& child) const override;
    virtual int rowCount(const QModelIndex& parent) const override;
    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual bool hasChildren(const QModelIndex& parent) const override;
    virtual bool canFetchMore(const QModelIndex& parent) const override;
    virtual void fetchMore(const QModelIndex& parent) override;

private:
    struct Item
    {
        PDFObjectInspectorNode node;
        Item* parent = nullptr;
        int row = 0;
        qint64 childCount = 0;  ///< The children of the node (PDFObjectInspector::getChildCount)
        bool isFetched = false; ///< Every child is loaded (or the core gave no more)
        mutable QString text;   ///< Cached text of the row (made on its first show)
        std::vector<std::unique_ptr<Item>> children;
    };

    Item* getItem(const QModelIndex& index) const;
    QModelIndex createIndex(Item* item) const;

    /// The last loaded child of a node with more children is shown: the next part comes from the event loop
    /// (a model cannot insert rows inside of data())
    void requestNextPart(const Item* item) const;
    void onNextPartQueued();

    std::shared_ptr<const PDFObjectInspector> m_inspector;
    mutable std::vector<QPersistentModelIndex> m_nextParts;    ///< The nodes, whose next part is queued
    mutable bool m_isNextPartQueued = false;
    PDFObjectInspectorView m_view = PDFObjectInspectorView::Document;
    PDFObjectInspector::OpenedReferences m_opened;     ///< The one visited set of the view
    std::vector<std::unique_ptr<Item>> m_roots;
    qint64 m_rootCount = 0;
};

}   // namespace pdf

#endif // PDFAPPOBJECTTREEMODEL_H
