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

#include "pdfappobjecttreemodel.h"
#include "pdfapptoolvalueformatter.h"

namespace pdf
{

PDFAppObjectTreeModel::PDFAppObjectTreeModel(QObject* parent) :
    BaseClass(parent)
{

}

PDFAppObjectTreeModel::~PDFAppObjectTreeModel() = default;

void PDFAppObjectTreeModel::setInspector(std::shared_ptr<const PDFObjectInspector> inspector, PDFObjectInspectorView view)
{
    beginResetModel();
    m_inspector = std::move(inspector);
    m_view = view;
    m_opened.clear();
    m_nextParts.clear();
    m_roots.clear();
    m_rootCount = m_inspector ? qMax<qint64>(0, m_inspector->getRootCount(view)) : 0;
    endResetModel();

    // The first part of the roots (the view never builds more than its roots: the children are lazy)
    if (canFetchMore(QModelIndex()))
    {
        fetchMore(QModelIndex());
    }
}

const PDFObjectInspectorNode* PDFAppObjectTreeModel::getNode(const QModelIndex& index) const
{
    const Item* item = getItem(index);
    return item ? &item->node : nullptr;
}

QModelIndex PDFAppObjectTreeModel::findReference(PDFObjectReference reference) const
{
    if (!reference.isValid())
    {
        return QModelIndex();
    }

    // The loaded rows breadth first (the roots first): iterative, the tree is at most MAXIMUM_PATH_DEPTH deep
    std::vector<Item*> queue;
    queue.reserve(m_roots.size());
    for (const std::unique_ptr<Item>& root : m_roots)
    {
        queue.push_back(root.get());
    }
    for (size_t i = 0; i < queue.size(); ++i)
    {
        Item* item = queue[i];
        if (item->node.reference == reference && item->node.key.isEmpty() && item->node.arrayIndex < 0)
        {
            return createIndex(item);
        }
        for (const std::unique_ptr<Item>& child : item->children)
        {
            queue.push_back(child.get());
        }
    }
    for (Item* item : queue)
    {
        if (item->node.reference == reference)
        {
            return createIndex(item);
        }
    }
    return QModelIndex();
}

QString PDFAppObjectTreeModel::getKeyText(const PDFObjectInspectorNode& node)
{
    if (node.isTrailer)
    {
        return tr("Trailer");
    }
    if (!node.key.isEmpty())
    {
        return QStringLiteral("/") + QString::fromLatin1(node.key);
    }
    if (node.arrayIndex >= 0)
    {
        return QStringLiteral("[%1]").arg(node.arrayIndex);
    }
    return QString();
}

QString PDFAppObjectTreeModel::getNodeText(const PDFObjectInspector* inspector, const PDFObjectInspectorNode& node)
{
    // "/Root 1 0 R  Dictionary [6 items]", "/Size 12406", "4 0 R  Dictionary [6 items]", "/Parent 2 0 R"
    QStringList parts;
    QString head = getKeyText(node);
    if (node.reference.isValid() && !node.isTrailer)
    {
        const QString reference = PDFObjectInspector::getReferenceText(node.reference);
        head = head.isEmpty() ? reference : QStringLiteral("%1 %2").arg(head, reference);
    }
    if (!head.isEmpty())
    {
        parts << head;
    }

    // A repeated reference is only its reference (482:15358 "/Parent 2 0 R"); a container its description, a
    // value its PDF syntax
    if (!node.isRepeated && inspector)
    {
        const PDFObject& object = node.object;
        if (object.isDictionary() || object.isArray() || object.isStream())
        {
            const PDFAppToolValueFormatter formatter;
            parts << inspector->describe(node, &formatter);
        }
        else
        {
            bool isTruncated = false;
            QString syntax = inspector->getSyntax(node, &isTruncated);
            syntax.replace(QLatin1Char('\n'), QLatin1Char(' '));
            parts << (syntax.size() > 200 ? syntax.left(200) + QStringLiteral("…") : syntax);
        }
    }
    return parts.join(QStringLiteral("  "));
}

PDFAppObjectTreeModel::Item* PDFAppObjectTreeModel::getItem(const QModelIndex& index) const
{
    return index.isValid() ? static_cast<Item*>(index.internalPointer()) : nullptr;
}

QModelIndex PDFAppObjectTreeModel::createIndex(Item* item) const
{
    return item ? BaseClass::createIndex(item->row, 0, item) : QModelIndex();
}

QModelIndex PDFAppObjectTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (column != 0 || row < 0)
    {
        return QModelIndex();
    }

    const Item* parentItem = getItem(parent);
    const std::vector<std::unique_ptr<Item>>& items = parentItem ? parentItem->children : m_roots;
    if (row >= int(items.size()))
    {
        return QModelIndex();
    }
    return BaseClass::createIndex(row, 0, items[row].get());
}

QModelIndex PDFAppObjectTreeModel::parent(const QModelIndex& child) const
{
    const Item* item = getItem(child);
    return item ? createIndex(item->parent) : QModelIndex();
}

int PDFAppObjectTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
    {
        return 0;
    }
    const Item* item = getItem(parent);
    return int(item ? item->children.size() : m_roots.size());
}

int PDFAppObjectTreeModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant PDFAppObjectTreeModel::data(const QModelIndex& index, int role) const
{
    const Item* item = getItem(index);
    if (!item)
    {
        return QVariant();
    }

    switch (role)
    {
        case Qt::DisplayRole:
        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole:
            if (item->text.isEmpty())
            {
                item->text = getNodeText(m_inspector.get(), item->node);
            }

            // The last loaded child of a node with more children: its next part (the view fetches more only at
            // the end of the whole tree)
            if (item->parent && !item->parent->isFetched && item->row + 1 == int(item->parent->children.size()))
            {
                requestNextPart(item->parent);
            }
            return item->text;

        default:
            break;
    }
    return QVariant();
}

bool PDFAppObjectTreeModel::hasChildren(const QModelIndex& parent) const
{
    const Item* item = getItem(parent);
    if (!item)
    {
        return !m_roots.empty() || m_rootCount > 0;
    }
    return item->isFetched ? !item->children.empty() : item->childCount > 0;
}

bool PDFAppObjectTreeModel::canFetchMore(const QModelIndex& parent) const
{
    const Item* item = getItem(parent);
    if (!item)
    {
        return m_inspector && qint64(m_roots.size()) < m_rootCount;
    }
    return !item->isFetched && item->childCount > 0;
}

void PDFAppObjectTreeModel::requestNextPart(const Item* item) const
{
    const QModelIndex index = createIndex(const_cast<Item*>(item));
    for (const QPersistentModelIndex& queued : m_nextParts)
    {
        if (queued == index)
        {
            return;
        }
    }
    m_nextParts.emplace_back(index);

    if (!m_isNextPartQueued)
    {
        m_isNextPartQueued = true;
        QMetaObject::invokeMethod(const_cast<PDFAppObjectTreeModel*>(this), &PDFAppObjectTreeModel::onNextPartQueued, Qt::QueuedConnection);
    }
}

void PDFAppObjectTreeModel::onNextPartQueued()
{
    // A reset meanwhile has made the indexes invalid; each node gets one part (its last child shown again asks
    // for the next one)
    m_isNextPartQueued = false;
    const std::vector<QPersistentModelIndex> nextParts = std::move(m_nextParts);
    m_nextParts.clear();
    for (const QPersistentModelIndex& index : nextParts)
    {
        if (index.isValid() && canFetchMore(index))
        {
            fetchMore(index);
        }
    }
}

void PDFAppObjectTreeModel::fetchMore(const QModelIndex& parent)
{
    if (!m_inspector || !canFetchMore(parent))
    {
        return;
    }

    Item* parentItem = getItem(parent);
    std::vector<PDFObjectInspectorNode> nodes;
    if (parentItem)
    {
        // The children of one node, on request only (a lazy tree, B158), a part of CHILD_BATCH at once: the core
        // adds the new references to the one visited set of the view
        nodes = m_inspector->getChildren(parentItem->node, &m_opened, qint64(parentItem->children.size()), CHILD_BATCH);
        parentItem->isFetched = nodes.empty() || qint64(parentItem->children.size() + nodes.size()) >= parentItem->childCount;
    }
    else
    {
        nodes = m_inspector->getRoots(m_view, &m_opened, qint64(m_roots.size()), ROOT_BATCH);
        if (nodes.empty())
        {
            // The view has fewer roots than it said: nothing more to fetch
            m_rootCount = qint64(m_roots.size());
        }
    }

    std::vector<std::unique_ptr<Item>>& items = parentItem ? parentItem->children : m_roots;
    if (nodes.empty())
    {
        if (parentItem && items.empty())
        {
            // The expander goes away
            const QModelIndex index = createIndex(parentItem);
            Q_EMIT dataChanged(index, index);
        }
        return;
    }

    const int first = int(items.size());
    beginInsertRows(parent, first, first + int(nodes.size()) - 1);
    items.reserve(items.size() + nodes.size());
    for (PDFObjectInspectorNode& node : nodes)
    {
        std::unique_ptr<Item> item = std::make_unique<Item>();
        item->node = std::move(node);
        item->childCount = PDFObjectInspector::getChildCount(item->node);
        item->parent = parentItem;
        item->row = int(items.size());
        items.push_back(std::move(item));
    }
    endInsertRows();
}

}   // namespace pdf

#include "moc_pdfappobjecttreemodel.cpp"
