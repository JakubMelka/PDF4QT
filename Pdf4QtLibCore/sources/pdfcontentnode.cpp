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

// Edit Content, stage E0, package P02 (tree and state): the path as plain data, the node, the
// container with the index of its nodes and the editor of a container
// (GUI_overhaul/edit_tool/zadani_jadro.md 4.2, zadani_overeni.md 6.2).

#include "pdfcontentnode.h"

#include <QTransform>
#include <QtGlobal>

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pdf
{

void PDFContentPath::moveTo(QPointF point)
{
    Segment segment;
    segment.type = SegmentType::MoveTo;
    segment.points[0] = point;
    m_segments.push_back(segment);
    m_rectangle.reset();
}

void PDFContentPath::lineTo(QPointF point)
{
    Segment segment;
    segment.type = SegmentType::LineTo;
    segment.points[0] = point;
    m_segments.push_back(segment);
    m_rectangle.reset();
}

void PDFContentPath::curveTo(QPointF control1, QPointF control2, QPointF point)
{
    Segment segment;
    segment.type = SegmentType::CurveTo;
    segment.points[0] = control1;
    segment.points[1] = control2;
    segment.points[2] = point;
    m_segments.push_back(segment);
    m_rectangle.reset();
}

void PDFContentPath::close()
{
    Segment segment;
    segment.type = SegmentType::Close;
    m_segments.push_back(segment);
    m_rectangle.reset();
}

void PDFContentPath::addRectangle(const QRectF& rectangle)
{
    const bool isOnlyRectangle = m_segments.empty();

    // ISO 32000-2, 8.5.2.1, the operator re: "x y m, (x + width) y l, (x + width) (y + height) l,
    // x (y + height) l, h". QRectF keeps a negative width and height as they are.
    const qreal x = rectangle.x();
    const qreal y = rectangle.y();
    const qreal width = rectangle.width();
    const qreal height = rectangle.height();

    moveTo(QPointF(x, y));
    lineTo(QPointF(x + width, y));
    lineTo(QPointF(x + width, y + height));
    lineTo(QPointF(x, y + height));
    close();

    if (isOnlyRectangle)
    {
        m_rectangle = rectangle;
    }
}

bool PDFContentPath::isRectangle(QRectF* rectangle) const
{
    if (!m_rectangle)
    {
        return false;
    }

    if (rectangle)
    {
        *rectangle = *m_rectangle;
    }

    return true;
}

QRectF PDFContentPath::getBoundingBox() const
{
    bool hasPoint = false;
    qreal minX = 0.0;
    qreal minY = 0.0;
    qreal maxX = 0.0;
    qreal maxY = 0.0;

    for (const Segment& segment : m_segments)
    {
        size_t pointCount = 0;
        switch (segment.type)
        {
            case SegmentType::MoveTo:
            case SegmentType::LineTo:
                pointCount = 1;
                break;

            case SegmentType::CurveTo:
                pointCount = 3;
                break;

            case SegmentType::Close:
                break;
        }

        for (size_t i = 0; i < pointCount; ++i)
        {
            const QPointF& point = segment.points[i];
            if (!hasPoint)
            {
                hasPoint = true;
                minX = maxX = point.x();
                minY = maxY = point.y();
            }
            else
            {
                minX = qMin(minX, point.x());
                maxX = qMax(maxX, point.x());
                minY = qMin(minY, point.y());
                maxY = qMax(maxY, point.y());
            }
        }
    }

    if (!hasPoint)
    {
        return QRectF();
    }

    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

PDFContentPath PDFContentPath::transformed(const QTransform& matrix) const
{
    PDFContentPath result;
    result.m_segments.reserve(m_segments.size());

    for (const Segment& segment : m_segments)
    {
        Segment mapped;
        mapped.type = segment.type;
        for (size_t i = 0; i < std::size(segment.points); ++i)
        {
            mapped.points[i] = matrix.map(segment.points[i]);
        }
        result.m_segments.push_back(mapped);
    }

    // A rectangle stays the rectangle of "re", when the matrix only scales and moves: its sides
    // stay parallel to the axes and its first side stays the horizontal one (a mirror gives
    // a negative width or height). After a rotation - also by a right angle - the first side
    // is not horizontal, which "re" cannot say.
    if (m_rectangle && matrix.m12() == 0.0 && matrix.m21() == 0.0 && matrix.isAffine())
    {
        const QPointF origin = matrix.map(m_rectangle->topLeft());
        result.m_rectangle = QRectF(origin.x(), origin.y(), m_rectangle->width() * matrix.m11(), m_rectangle->height() * matrix.m22());
    }

    return result;
}

PDFContentNode::PDFContentNode(Data data, std::vector<PDFContentNodePointer> children) :
    m_data(std::move(data)),
    m_children(std::move(children))
{
    // A QTransform computes its type lazily, also in a const object: it is computed here once,
    // so the matrices of the immutable node are plain data for the threads, which read them
    if (m_data.override)
    {
        m_data.override->transform.type();
        for (auto& item : m_data.override->properties)
        {
            item.second.matrix.type();
        }
    }

    for (PDFContentTextItem& item : m_data.textItems)
    {
        item.textMatrix.type();
    }
}

bool PDFContentNode::isLeafKind(PDFContentNodeKind kind)
{
    switch (kind)
    {
        case PDFContentNodeKind::Root:
        case PDFContentNodeKind::Group:
        case PDFContentNodeKind::MarkedContent:
        case PDFContentNodeKind::TextObject:
            return false;

        case PDFContentNodeKind::MarkedPoint:
        case PDFContentNodeKind::TextShow:
        case PDFContentNodeKind::Path:
        case PDFContentNodeKind::Image:
        case PDFContentNodeKind::FormUse:
        case PDFContentNodeKind::Shading:
        case PDFContentNodeKind::StateRun:
        case PDFContentNodeKind::Opaque:
            return true;
    }

    return true;
}

bool PDFContentNode::isWrappableKind(PDFContentNodeKind kind)
{
    switch (kind)
    {
        case PDFContentNodeKind::Path:
        case PDFContentNodeKind::Image:
        case PDFContentNodeKind::Shading:
        case PDFContentNodeKind::FormUse:
        case PDFContentNodeKind::TextObject:
            return true;

        case PDFContentNodeKind::Root:
        case PDFContentNodeKind::Group:
        case PDFContentNodeKind::MarkedContent:
        case PDFContentNodeKind::MarkedPoint:
        case PDFContentNodeKind::TextShow:
        case PDFContentNodeKind::StateRun:
        case PDFContentNodeKind::Opaque:
            return false;
    }

    return false;
}

/// The nodes of a container by their identifiers.
///
/// Threads and ownership: immutable after its constructor; it is owned by the container and
/// points to nodes, which the root of the same container owns.
class PDFContentContainer::Index
{
public:
    /// One node (a plain value)
    struct Entry
    {
        quint64 id = 0;
        const PDFContentNode* parent = nullptr;     ///< nullptr for the root
        size_t indexInParent = 0;
    };

    explicit Index(const PDFContentNodePointer& root);

    /// The entry of an identifier, nullptr when there is none
    const Entry* find(PDFContentNodeId id) const;

    quint64 getMaximumId() const { return m_entries.empty() ? 0 : m_entries.back().id; }
    size_t getNodeCount() const { return m_nodeCount; }

private:
    std::vector<Entry> m_entries;   ///< Sorted by the identifier, each identifier once
    size_t m_nodeCount = 0;
};

PDFContentContainer::Index::Index(const PDFContentNodePointer& root)
{
    if (!root)
    {
        return;
    }

    // The walk has its own stack: the depth of a tree made by commands is limited by the
    // editor (PDFContentContainer::MAXIMUM_DEPTH), a tree made by hand need not be
    struct Item
    {
        const PDFContentNode* node = nullptr;
        const PDFContentNode* parent = nullptr;
        size_t indexInParent = 0;
    };

    std::vector<Item> stack;
    stack.push_back(Item{ root.get(), nullptr, 0 });

    while (!stack.empty())
    {
        const Item item = stack.back();
        stack.pop_back();

        ++m_nodeCount;
        if (item.node->getId().isValid())
        {
            m_entries.push_back(Entry{ item.node->getId().value, item.parent, item.indexInParent });
        }

        const std::vector<PDFContentNodePointer>& children = item.node->getChildren();
        for (size_t i = children.size(); i > 0; --i)
        {
            if (children[i - 1])
            {
                stack.push_back(Item{ children[i - 1].get(), item.node, i - 1 });
            }
        }
    }

    // An identifier used twice (a tree made by hand) names its first node in the order of the tree
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& left, const Entry& right) { return left.id < right.id; });
    m_entries.erase(std::unique(m_entries.begin(), m_entries.end(), [](const Entry& left, const Entry& right) { return left.id == right.id; }), m_entries.end());
}

const PDFContentContainer::Index::Entry* PDFContentContainer::Index::find(PDFContentNodeId id) const
{
    auto it = std::lower_bound(m_entries.cbegin(), m_entries.cend(), id.value, [](const Entry& entry, quint64 value) { return entry.id < value; });
    if (it != m_entries.cend() && it->id == id.value)
    {
        return &*it;
    }

    return nullptr;
}

PDFContentContainer::PDFContentContainer(Data data) :
    m_data(std::move(data)),
    m_index(std::make_shared<const Index>(m_data.root))
{
    // The lazily computed type of the matrix, see the constructor of PDFContentNode
    m_data.matrix.type();
}

PDFContentNodePointer PDFContentContainer::getNode(PDFContentNodeId id) const
{
    const Index::Entry* entry = id.isValid() ? m_index->find(id) : nullptr;
    if (!entry)
    {
        return PDFContentNodePointer();
    }

    if (!entry->parent)
    {
        return m_data.root;
    }

    return entry->parent->getChildren()[entry->indexInParent];
}

PDFContentNodePointer PDFContentContainer::getParent(PDFContentNodeId id) const
{
    const Index::Entry* entry = id.isValid() ? m_index->find(id) : nullptr;
    if (!entry || !entry->parent)
    {
        return PDFContentNodePointer();
    }

    return getNode(entry->parent->getId());
}

PDFInteger PDFContentContainer::getIndexInParent(PDFContentNodeId id) const
{
    const Index::Entry* entry = id.isValid() ? m_index->find(id) : nullptr;
    if (!entry || !entry->parent)
    {
        return -1;
    }

    return PDFInteger(entry->indexInParent);
}

PDFContentNodeId PDFContentContainer::getMaximumNodeId() const
{
    return PDFContentNodeId{ m_index->getMaximumId() };
}

size_t PDFContentContainer::getNodeCount() const
{
    return m_index->getNodeCount();
}

/// What the container and its editor have to know about the end of a content: the nodes, which
/// must stay behind everything else, and the nodes, which the content does not close.
///
/// Threads and ownership: static functions without a state.
class PDFContentNodeEndHelper
{
public:
    /// The node is the tail node: an Opaque node without operations (the bytes of the tail are
    /// PDFContentOperationList::getTail)
    static bool isTailNode(const PDFContentNode::Data& data)
    {
        return data.kind == PDFContentNodeKind::Opaque && data.prefix.isEmpty() && data.suffix.isEmpty();
    }

    /// The node is the rest of a content after an inline image without an end: an Opaque node,
    /// which holds the refused operation of that image
    static bool isInlineImageRest(const PDFContentNode::Data& data, const PDFContentOperationList& operations)
    {
        if (data.kind != PDFContentNodeKind::Opaque)
        {
            return false;
        }

        const PDFContentOperationSpan& span = data.prefix;
        const size_t operationCount = operations.getCount();
        for (size_t i = span.first; i - span.first < span.count && i < operationCount; ++i)
        {
            const PDFContentOperation& operation = operations.getOperation(i);
            if (operation.getOperator() == PDFContentOperator::InlineImage && operation.getFlags().testFlag(PDFContentOperation::Failed))
            {
                return true;
            }
        }

        return false;
    }

    /// The content does not close the node: a node with children (never the root), which has
    /// opening operations and no closing ones
    static bool isUnclosed(const PDFContentNode::Data& data)
    {
        return data.kind != PDFContentNodeKind::Root && !PDFContentNode::isLeafKind(data.kind) && !data.prefix.isEmpty() && data.suffix.isEmpty();
    }

    /// Nothing can be written behind the node: it is the rest of an inline image without an
    /// end, or an unclosed node, whose last child is such a node (the chain of the last children
    /// is followed; it is as long as the tree is deep at most)
    static bool isSwallowingEnd(const PDFContentNode* node, const PDFContentOperationList& operations)
    {
        while (node)
        {
            if (PDFContentNode::isLeafKind(node->getKind()))
            {
                return isInlineImageRest(node->getData(), operations);
            }

            if (!isUnclosed(node->getData()) || node->getChildren().empty())
            {
                return false;
            }

            node = node->getChildren().back().get();
        }

        return false;
    }
};

PDFInteger PDFContentContainer::getAppendIndex(PDFContentNodeId parentId) const
{
    const PDFContentNodePointer parent = getNode(parentId);
    if (!parent || PDFContentNode::isLeafKind(parent->getKind()))
    {
        return -1;
    }

    // The children at the end, which must stay behind everything else: the tail node and,
    // before it, the rest of an inline image without an end (or the unclosed node, which holds
    // it). A content can have both.
    const std::vector<PDFContentNodePointer>& children = parent->getChildren();
    size_t count = children.size();
    while (count > 0)
    {
        const PDFContentNode* node = children[count - 1].get();
        if (!node || !(PDFContentNodeEndHelper::isTailNode(node->getData()) || PDFContentNodeEndHelper::isSwallowingEnd(node, m_data.operations)))
        {
            break;
        }

        --count;
    }

    return PDFInteger(count);
}

bool PDFContentContainer::isUnclosed(PDFContentNodeId id) const
{
    const PDFContentNodePointer node = getNode(id);
    return node && PDFContentNodeEndHelper::isUnclosed(node->getData());
}

bool PDFContentContainer::isUnchanged() const
{
    if (m_data.resources.isChanged())
    {
        return false;
    }

    // The content is the original one, when the tree names the original operations in their
    // order, each once and all of them (so no node has moved, was removed or copied and no
    // generated operation is written), no node is modified or has an override and no original
    // operation is suppressed.
    const size_t originalCount = m_data.operations.getOriginalCount();
    size_t expectedOperation = 0;

    struct Item
    {
        const PDFContentNode* node = nullptr;
        size_t nextChild = 0;
    };

    std::vector<Item> stack;
    if (m_data.root)
    {
        stack.push_back(Item{ m_data.root.get(), 0 });
    }

    while (!stack.empty())
    {
        Item& item = stack.back();
        const PDFContentNode* node = item.node;

        if (item.nextChild == 0)
        {
            // The node is entered: its opening operations
            if (node->isModified() || node->getOverride())
            {
                return false;
            }

            const PDFContentOperationSpan& prefix = node->getPrefix();
            if (!prefix.isEmpty())
            {
                if (prefix.first != expectedOperation)
                {
                    return false;
                }
                expectedOperation += prefix.count;
            }
        }

        const std::vector<PDFContentNodePointer>& children = node->getChildren();
        if (item.nextChild < children.size())
        {
            // The counter of the children is at least one from now on, so the node is not
            // entered again. A missing child (a tree made by hand) is skipped.
            const PDFContentNode* child = children[item.nextChild++].get();
            if (child)
            {
                stack.push_back(Item{ child, 0 });  // "item" is not used after this line
            }
            continue;
        }

        // The node is left: its closing operations
        const PDFContentOperationSpan& suffix = node->getSuffix();
        if (!suffix.isEmpty())
        {
            if (suffix.first != expectedOperation)
            {
                return false;
            }
            expectedOperation += suffix.count;
        }

        stack.pop_back();
    }

    if (expectedOperation != originalCount)
    {
        return false;
    }

    for (size_t i = 0; i < originalCount; ++i)
    {
        if (m_data.operations.getOperation(i).isModified())
        {
            return false;
        }
    }

    return true;
}

/// The state of the editor of a container: the tree being changed as a map of light work nodes.
/// A work node keeps the node of the source container (shared, never copied) and gets data of
/// its own only when they change; build() creates new nodes only for the changed nodes and for
/// the nodes above them, everything else is shared with the source container.
///
/// Threads and ownership: owned by its editor, one thread.
class PDFContentContainerEditor::Impl
{
public:
    /// One node of the tree being changed. Owned by the map of the editor.
    struct WorkNode
    {
        PDFContentNodePointer original;             ///< The node of the source container; nullptr for a new node
        std::optional<PDFContentNode::Data> data;   ///< The data, when they differ from the original ones (always for a new node)
        quint64 parentId = 0;                       ///< 0 for the root
        std::vector<quint64> children;
        int depth = 0;                              ///< 0 for the root
        bool isChanged = false;                     ///< The node, or a node below it, differs from the original one

        /// The immutable node made for the changed node by materialize, valid as long as neither
        /// the node nor anything below it changes (markChanged forgets it). It is shared with
        /// the callers of getNode. Rule kept by materialize and markChanged: a changed node,
        /// which has such a node, has one for every changed node below it - so a changed node
        /// without one has none above it either.
        PDFContentNodePointer current;
    };

    /// The deepest node allowed: MAXIMUM_DEPTH nested nodes with children below the root and
    /// their leaves
    static constexpr int MAXIMUM_NODE_DEPTH = PDFContentContainer::MAXIMUM_DEPTH + 1;

    explicit Impl(const PDFContentContainerKey& key);
    explicit Impl(const PDFContentContainerPointer& container);

    WorkNode* find(quint64 id);
    const PDFContentNode::Data& getData(const WorkNode& node) const { return node.data ? *node.data : node.original->getData(); }

    /// Marks a node and the nodes above it as changed and forgets the immutable nodes made for
    /// them (WorkNode::current)
    void markChanged(quint64 id);

    /// Replaces the data of a node, which the editor changes itself
    void setData(quint64 id, PDFContentNode::Data data);

    /// Nothing can be written behind the node: the rest of an inline image without an end, or
    /// an unclosed node, whose last child is such a node (PDFContentNodeEndHelper::isSwallowingEnd
    /// for the tree being changed)
    bool isSwallowingEnd(const WorkNode* node);

    /// Gives the unclosed nodes, behind whose end an operation is written or which have an
    /// override, their closing operators (PDFContentContainer::isUnclosed)
    void closeUnclosedNodes();

    /// Appends the closing operators of an unclosed node to the operations
    /// \returns The span of the appended operations
    PDFContentOperationSpan appendClosingOperations(const PDFContentNode::Data& data);

    /// Number of the levels below a node (0 for a node without children)
    int getHeight(quint64 id);

    /// Sets the depth of a node and of the nodes below it
    void setDepth(quint64 id, int depth);

    /// Removes a node and the nodes below it from the map (not from the children of its parent)
    void eraseSubtree(quint64 id);

    /// The index of the children of a node, at which a child is inserted: \p index, or the end
    /// for -1; no value for an index out of range
    static std::optional<size_t> getInsertPosition(const WorkNode& parent, PDFInteger index);

    /// Creates the immutable node of an identifier with the nodes below it. Unchanged nodes of
    /// the source container are shared, and so is every node made by an earlier call, which is
    /// still valid (WorkNode::current): a call makes only the nodes changed since they were made
    /// last.
    /// \param id The node
    /// \param isConsumed The data of the work nodes are moved into the nodes (the editor is
    ///        not used afterwards)
    PDFContentNodePointer materialize(quint64 id, bool isConsumed);

    PDFContentContainer::Data containerData;    ///< Its root, operations and resources are not used
    PDFContentOperationList operations;
    PDFContentResources resources;
    std::unordered_map<quint64, WorkNode> nodes;
    quint64 rootId = 0;
    bool isBuilt = false;

private:
    void createRoot();
};

PDFContentContainerEditor::Impl::Impl(const PDFContentContainerKey& key)
{
    containerData.key = key;
    createRoot();
}

PDFContentContainerEditor::Impl::Impl(const PDFContentContainerPointer& container)
{
    if (!container)
    {
        createRoot();
        return;
    }

    containerData = container->getData();
    operations = std::move(containerData.operations);
    resources = std::move(containerData.resources);
    containerData.operations = PDFContentOperationList();
    containerData.resources = PDFContentResources();

    const PDFContentNodePointer root = std::move(containerData.root);
    containerData.root.reset();

    if (!root || !root->getId().isValid())
    {
        // A container made by hand without a usable root: the editor starts with an empty one
        createRoot();
        return;
    }

    struct Item
    {
        PDFContentNodePointer node;
        quint64 parentId = 0;
        int depth = 0;
    };

    rootId = root->getId().value;
    std::vector<Item> stack;
    stack.push_back(Item{ root, 0, 0 });

    while (!stack.empty())
    {
        Item item = std::move(stack.back());
        stack.pop_back();

        const quint64 id = item.node->getId().value;
        WorkNode& workNode = nodes[id];
        workNode.original = item.node;
        workNode.parentId = item.parentId;
        workNode.depth = item.depth;

        if (item.node->getParentId().value != item.parentId)
        {
            // The node says another parent than the tree: its data are corrected by build()
            workNode.isChanged = true;
        }

        // References to the items of the map stay valid, when the map grows
        const std::vector<PDFContentNodePointer>& children = item.node->getChildren();
        workNode.children.reserve(children.size());
        for (const PDFContentNodePointer& child : children)
        {
            // A missing child and an identifier used twice (a tree made by hand) are left out
            if (!child || !child->getId().isValid() || nodes.count(child->getId().value))
            {
                workNode.isChanged = true;
                continue;
            }

            // The identifier is taken now, the node is filled when it is taken from the stack
            const quint64 childId = child->getId().value;
            workNode.children.push_back(childId);
            nodes[childId];
            stack.push_back(Item{ child, id, item.depth + 1 });
        }
    }

    // A change below a node is a change of every node above it
    std::vector<quint64> changedIdentifiers;
    for (const auto& item : nodes)
    {
        if (item.second.isChanged)
        {
            changedIdentifiers.push_back(item.first);
        }
    }
    for (const quint64 id : changedIdentifiers)
    {
        nodes[id].isChanged = false;
        markChanged(id);
    }
}

void PDFContentContainerEditor::Impl::createRoot()
{
    rootId = 1;

    WorkNode& root = nodes[rootId];
    root.data = PDFContentNode::Data();
    root.data->kind = PDFContentNodeKind::Root;
    root.data->id = PDFContentNodeId{ rootId };
    root.isChanged = true;
}

PDFContentContainerEditor::Impl::WorkNode* PDFContentContainerEditor::Impl::find(quint64 id)
{
    auto it = nodes.find(id);
    return it != nodes.end() ? &it->second : nullptr;
}

void PDFContentContainerEditor::Impl::markChanged(quint64 id)
{
    // The node itself is always marked. Above it the walk stops at the first node, which is
    // changed and has no immutable node made for it: a changed node has only changed nodes above
    // it, and none of them has such a node then (the rule of WorkNode::current).
    WorkNode* node = find(id);
    bool isFirst = true;
    while (node && (isFirst || !node->isChanged || node->current))
    {
        node->isChanged = true;
        node->current.reset();
        node = find(node->parentId);
        isFirst = false;
    }
}

void PDFContentContainerEditor::Impl::setData(quint64 id, PDFContentNode::Data data)
{
    if (WorkNode* node = find(id))
    {
        data.id = PDFContentNodeId{ id };
        data.parentId = PDFContentNodeId{ node->parentId };
        node->data = std::move(data);
        markChanged(id);
    }
}

bool PDFContentContainerEditor::Impl::isSwallowingEnd(const WorkNode* node)
{
    // The chain of the last children; it is as long as the tree is deep at most
    while (node)
    {
        const PDFContentNode::Data& data = getData(*node);
        if (PDFContentNode::isLeafKind(data.kind))
        {
            return PDFContentNodeEndHelper::isInlineImageRest(data, operations);
        }

        if (!PDFContentNodeEndHelper::isUnclosed(data) || node->children.empty())
        {
            return false;
        }

        node = find(node->children.back());
    }

    return false;
}

PDFContentOperationSpan PDFContentContainerEditor::Impl::appendClosingOperations(const PDFContentNode::Data& data)
{
    PDFContentOperator closingOperator = PDFContentOperator::Invalid;
    const char* closingText = nullptr;
    size_t count = 1;

    switch (data.kind)
    {
        case PDFContentNodeKind::Group:
        {
            // One Q for each q of the chain (the opening operations of a group are its q)
            closingOperator = PDFContentOperator::RestoreGraphicState;
            closingText = "Q";

            size_t saveCount = 0;
            const size_t operationCount = operations.getCount();
            for (size_t i = data.prefix.first; i - data.prefix.first < data.prefix.count && i < operationCount; ++i)
            {
                if (operations.getOperation(i).getOperator() == PDFContentOperator::SaveGraphicState)
                {
                    ++saveCount;
                }
            }

            // A group made by hand, whose operations are not in the list: its depth, one at least
            count = saveCount > 0 ? saveCount : size_t(qMax(data.saveDepth, 1));
            break;
        }

        case PDFContentNodeKind::MarkedContent:
            closingOperator = PDFContentOperator::MarkedContentEnd;
            closingText = "EMC";
            break;

        case PDFContentNodeKind::TextObject:
            closingOperator = PDFContentOperator::TextEnd;
            closingText = "ET";
            break;

        default:
            return PDFContentOperationSpan();
    }

    // Generated operations are appended one after the other, so they are one span
    PDFContentOperationSpan span;
    for (size_t i = 0; i < count; ++i)
    {
        const size_t index = operations.append(PDFContentOperation::createGenerated(closingOperator, QByteArray(closingText), PDFContentOperation::Operands(), std::vector<PDFContentResourceName>()));
        if (i == 0)
        {
            span.first = index;
        }
        ++span.count;
    }

    return span;
}

void PDFContentContainerEditor::Impl::closeUnclosedNodes()
{
    // zadani_jadro.md 4.4, R-7: a missing Q, ET or EMC is not added, until the node is changed.
    // The change, which makes the missing operator matter, is an operation written behind the
    // end of the node (it would be read inside of the node: in its graphics state, in its
    // marked-content sequence, in its text object) or a wrap of the node (its Q would end the
    // wrap inside of the node). The tree is walked in the reverse order of writing with one
    // flag: something is written behind the place the walk is at.
    //
    //
    // An unchanged closed node of the source container is not entered: the unclosed nodes of
    // a container of the builder are a chain of last children from the root, and a container of
    // the editor has passed this function. Nothing is to do, when nothing has changed.
    const WorkNode* root = find(rootId);
    if (!root || !root->isChanged)
    {
        return;
    }

    struct Frame
    {
        quint64 id = 0;
        size_t remainingChildren = 0;
        bool isEntered = false;
    };

    bool isFollowed = false;

    std::vector<Frame> stack;
    stack.push_back(Frame{ rootId, 0, false });

    while (!stack.empty())
    {
        Frame& frame = stack.back();
        WorkNode* node = find(frame.id);
        if (!node)
        {
            stack.pop_back();
            continue;
        }

        if (!frame.isEntered)
        {
            // The end of the node
            frame.isEntered = true;

            const PDFContentNode::Data& data = getData(*node);
            const bool isUnclosed = PDFContentNodeEndHelper::isUnclosed(data);

            if (PDFContentNode::isLeafKind(data.kind))
            {
                // Also a residual state run without operations (it writes its override) and
                // a path written from its data
                if (!data.prefix.isEmpty() || !data.suffix.isEmpty() || data.override || data.isModified)
                {
                    isFollowed = true;
                }

                stack.pop_back();
                continue;
            }

            if (!node->isChanged && !data.suffix.isEmpty())
            {
                // A whole unchanged subtree, which ends with its closing operators
                isFollowed = true;
                stack.pop_back();
                continue;
            }

            if (isUnclosed && (isFollowed || data.override) && !isSwallowingEnd(node))
            {
                PDFContentNode::Data closedData = data;
                closedData.suffix = appendClosingOperations(closedData);
                setData(frame.id, std::move(closedData));   // "data" is not used after this line
            }

            // The closing operators, the Q of a wrap
            const PDFContentNode::Data& currentData = getData(*node);
            if (!currentData.suffix.isEmpty() || currentData.override)
            {
                isFollowed = true;
            }

            frame.remainingChildren = node->children.size();
        }

        if (frame.remainingChildren > 0)
        {
            const quint64 childId = node->children[--frame.remainingChildren];
            stack.push_back(Frame{ childId, 0, false });    // "frame" is not used after this line
            continue;
        }

        // The beginning of the node: its opening operators, the q of a wrap
        const PDFContentNode::Data& data = getData(*node);
        if (!data.prefix.isEmpty() || data.override)
        {
            isFollowed = true;
        }

        stack.pop_back();
    }
}

int PDFContentContainerEditor::Impl::getHeight(quint64 id)
{
    int height = 0;

    std::vector<std::pair<quint64, int>> stack;
    stack.emplace_back(id, 0);

    while (!stack.empty())
    {
        const auto [currentId, level] = stack.back();
        stack.pop_back();

        height = qMax(height, level);
        if (const WorkNode* node = find(currentId))
        {
            for (const quint64 childId : node->children)
            {
                stack.emplace_back(childId, level + 1);
            }
        }
    }

    return height;
}

void PDFContentContainerEditor::Impl::setDepth(quint64 id, int depth)
{
    std::vector<std::pair<quint64, int>> stack;
    stack.emplace_back(id, depth);

    while (!stack.empty())
    {
        const auto [currentId, currentDepth] = stack.back();
        stack.pop_back();

        if (WorkNode* node = find(currentId))
        {
            node->depth = currentDepth;
            for (const quint64 childId : node->children)
            {
                stack.emplace_back(childId, currentDepth + 1);
            }
        }
    }
}

void PDFContentContainerEditor::Impl::eraseSubtree(quint64 id)
{
    std::vector<quint64> stack;
    stack.push_back(id);

    while (!stack.empty())
    {
        const quint64 currentId = stack.back();
        stack.pop_back();

        auto it = nodes.find(currentId);
        if (it != nodes.end())
        {
            stack.insert(stack.end(), it->second.children.cbegin(), it->second.children.cend());
            nodes.erase(it);
        }
    }
}

std::optional<size_t> PDFContentContainerEditor::Impl::getInsertPosition(const WorkNode& parent, PDFInteger index)
{
    if (index == -1)
    {
        return parent.children.size();
    }

    if (index < 0 || size_t(index) > parent.children.size())
    {
        return std::nullopt;
    }

    return size_t(index);
}

PDFContentNodePointer PDFContentContainerEditor::Impl::materialize(quint64 id, bool isConsumed)
{
    WorkNode* top = find(id);
    if (!top)
    {
        return PDFContentNodePointer();
    }

    if (!top->isChanged && top->original)
    {
        return top->original;
    }

    if (top->current)
    {
        // Made by an earlier call; nothing at the node or below it has changed since
        return top->current;
    }

    // Children first, with a stack of its own (no recursion over the depth of the tree)
    struct Frame
    {
        quint64 id = 0;
        WorkNode* node = nullptr;
        size_t nextChild = 0;
        std::vector<PDFContentNodePointer> children;
    };

    std::vector<Frame> stack;
    stack.push_back(Frame{ id, top, 0, {} });
    stack.back().children.reserve(top->children.size());

    PDFContentNodePointer result;
    while (!stack.empty())
    {
        Frame& frame = stack.back();

        if (frame.nextChild < frame.node->children.size())
        {
            const quint64 childId = frame.node->children[frame.nextChild++];
            WorkNode* child = find(childId);
            if (!child)
            {
                continue;
            }

            if (!child->isChanged && child->original)
            {
                frame.children.push_back(child->original);
            }
            else if (child->current)
            {
                frame.children.push_back(child->current);
            }
            else
            {
                Frame childFrame{ childId, child, 0, {} };
                childFrame.children.reserve(child->children.size());
                stack.push_back(std::move(childFrame));     // "frame" is not used after this line
            }
            continue;
        }

        PDFContentNode::Data data;
        if (frame.node->data)
        {
            if (isConsumed)
            {
                data = std::move(*frame.node->data);
            }
            else
            {
                data = *frame.node->data;
            }
        }
        else
        {
            data = frame.node->original->getData();
        }
        data.id = PDFContentNodeId{ frame.id };
        data.parentId = PDFContentNodeId{ frame.node->parentId };

        PDFContentNodePointer node = std::make_shared<const PDFContentNode>(std::move(data), std::move(frame.children));
        if (!isConsumed)
        {
            // Kept for the next call. Every changed node below has its node by now (it was
            // made or reused before this one), which is the rule of WorkNode::current.
            frame.node->current = node;
        }
        stack.pop_back();

        if (stack.empty())
        {
            result = std::move(node);
        }
        else
        {
            stack.back().children.push_back(std::move(node));
        }
    }

    return result;
}

PDFContentContainerEditor::PDFContentContainerEditor(const PDFContentContainerKey& key) :
    m_impl(std::make_unique<Impl>(key))
{

}

PDFContentContainerEditor::PDFContentContainerEditor(const PDFContentContainerPointer& container) :
    m_impl(std::make_unique<Impl>(container))
{

}

PDFContentContainerEditor::~PDFContentContainerEditor() = default;

PDFContentOperationList& PDFContentContainerEditor::getOperations()
{
    return m_impl->operations;
}

PDFContentResources& PDFContentContainerEditor::getResources()
{
    return m_impl->resources;
}

PDFContentContainer::Data& PDFContentContainerEditor::getContainerData()
{
    return m_impl->containerData;
}

PDFContentNodePointer PDFContentContainerEditor::getNode(PDFContentNodeId id) const
{
    if (m_impl->isBuilt || !id.isValid())
    {
        return PDFContentNodePointer();
    }

    return m_impl->materialize(id.value, false);
}

bool PDFContentContainerEditor::insertNode(PDFContentNode::Data data, PDFContentNodeId parentId, PDFInteger index)
{
    if (m_impl->isBuilt || !data.id.isValid() || data.kind == PDFContentNodeKind::Root || m_impl->find(data.id.value))
    {
        return false;
    }

    Impl::WorkNode* parent = m_impl->find(parentId.value);
    if (!parent || PDFContentNode::isLeafKind(m_impl->getData(*parent).kind) || parent->depth + 1 > Impl::MAXIMUM_NODE_DEPTH)
    {
        return false;
    }

    const std::optional<size_t> position = Impl::getInsertPosition(*parent, index);
    if (!position)
    {
        return false;
    }

    const quint64 id = data.id.value;
    const int depth = parent->depth + 1;
    parent->children.insert(parent->children.begin() + *position, id);

    data.parentId = parentId;

    Impl::WorkNode& node = m_impl->nodes[id];
    node.data = std::move(data);
    node.parentId = parentId.value;
    node.depth = depth;
    node.isChanged = true;

    m_impl->markChanged(parentId.value);
    return true;
}

bool PDFContentContainerEditor::setNodeData(PDFContentNodeId id, PDFContentNode::Data data)
{
    Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    if (!node)
    {
        return false;
    }

    // The root stays the root and nothing else becomes one; a node with children stays a node,
    // which can have them
    const bool isRoot = id.value == m_impl->rootId;
    if (isRoot != (data.kind == PDFContentNodeKind::Root) || (!node->children.empty() && PDFContentNode::isLeafKind(data.kind)))
    {
        return false;
    }

    data.id = id;
    data.parentId = PDFContentNodeId{ node->parentId };
    node->data = std::move(data);
    node->isChanged = false;    // The walk of markChanged starts at this node
    m_impl->markChanged(id.value);
    return true;
}

bool PDFContentContainerEditor::removeNode(PDFContentNodeId id)
{
    Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    if (!node || id.value == m_impl->rootId)
    {
        return false;
    }

    const quint64 parentId = node->parentId;
    if (Impl::WorkNode* parent = m_impl->find(parentId))
    {
        parent->children.erase(std::remove(parent->children.begin(), parent->children.end(), id.value), parent->children.end());
    }

    m_impl->eraseSubtree(id.value);
    m_impl->markChanged(parentId);
    return true;
}

bool PDFContentContainerEditor::moveNode(PDFContentNodeId id, PDFContentNodeId newParentId, PDFInteger index)
{
    Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    Impl::WorkNode* newParent = !m_impl->isBuilt ? m_impl->find(newParentId.value) : nullptr;
    if (!node || !newParent || id.value == m_impl->rootId || PDFContentNode::isLeafKind(m_impl->getData(*newParent).kind))
    {
        return false;
    }

    // Not under itself or under a node of its own subtree: the node must not be above its new parent
    for (const Impl::WorkNode* ancestor = newParent; ancestor; ancestor = m_impl->find(ancestor->parentId))
    {
        if (ancestor == node)
        {
            return false;
        }
    }

    if (newParent->depth + 1 + m_impl->getHeight(id.value) > Impl::MAXIMUM_NODE_DEPTH)
    {
        return false;
    }

    Impl::WorkNode* oldParent = m_impl->find(node->parentId);
    if (!oldParent)
    {
        return false;
    }

    // The index is one of the children of the new parent without the moved node
    const auto oldIt = std::find(oldParent->children.begin(), oldParent->children.end(), id.value);
    const size_t oldPosition = size_t(std::distance(oldParent->children.begin(), oldIt));
    if (oldIt != oldParent->children.end())
    {
        oldParent->children.erase(oldIt);
    }

    const std::optional<size_t> position = Impl::getInsertPosition(*newParent, index);
    if (!position)
    {
        // Nothing has changed: the node goes back to its place
        oldParent->children.insert(oldParent->children.begin() + qMin(oldPosition, oldParent->children.size()), id.value);
        return false;
    }

    const quint64 oldParentId = node->parentId;
    newParent->children.insert(newParent->children.begin() + *position, id.value);
    node->parentId = newParentId.value;
    m_impl->setDepth(id.value, newParent->depth + 1);

    // The parent identifier is a part of the data of the node: the node gets new ones
    node->isChanged = false;
    m_impl->markChanged(id.value);
    m_impl->markChanged(oldParentId);
    return true;
}

PDFContentContainerPointer PDFContentContainerEditor::build()
{
    if (m_impl->isBuilt)
    {
        return PDFContentContainerPointer();
    }

    // The closing operators of the unclosed nodes, behind which the new container writes
    m_impl->closeUnclosedNodes();

    PDFContentContainer::Data data = std::move(m_impl->containerData);
    data.root = m_impl->materialize(m_impl->rootId, true);
    data.operations = std::move(m_impl->operations);
    data.resources = std::move(m_impl->resources);

    m_impl->isBuilt = true;
    m_impl->nodes.clear();
    m_impl->containerData = PDFContentContainer::Data();
    m_impl->operations = PDFContentOperationList();
    m_impl->resources = PDFContentResources();

    return std::make_shared<const PDFContentContainer>(std::move(data));
}

}   // namespace pdf
