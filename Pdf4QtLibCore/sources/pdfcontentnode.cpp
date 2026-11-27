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

#include <QRandomGenerator>
#include <QTransform>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
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
    m_index(std::make_shared<const Index>(m_data.root)),
    m_version(allocateVersion()),
    m_operationCount(m_data.operations.getCount())
{
    // The lazily computed type of the matrix, see the constructor of PDFContentNode
    m_data.matrix.type();
}

PDFContentContainer::PDFContentContainer(Data data, quint64 version, size_t operationCount) :
    m_data(std::move(data)),
    m_index(std::make_shared<const Index>(m_data.root)),
    m_version(version),
    m_operationCount(operationCount)
{
    m_data.matrix.type();
}

quint64 PDFContentContainer::allocateVersion()
{
    // Containers are made by workers too (the build of a container): one counter of the process
    static std::atomic<quint64> counter{ 0 };
    return ++counter;
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

    /// New content must not be written behind the node: isSwallowingEnd, or a node at which the
    /// content ends with something left open (PDFContentNode::isOpenEnd), or an unclosed node,
    /// whose last child is such a node (the chain of the last children, as deep as the tree).
    /// The clip of an unclosed text object (the one open end, which is a TextObject) ends with
    /// the Q of an unclosed group around it, which the model writes too: behind such a group the
    /// place is free.
    static bool isPinnedEnd(const PDFContentNode* node, const PDFContentOperationList& operations)
    {
        bool isInsideGroup = false;
        while (node)
        {
            if (node->isOpenEnd())
            {
                return !(isInsideGroup && node->getKind() == PDFContentNodeKind::TextObject);
            }

            isInsideGroup = isInsideGroup || node->getKind() == PDFContentNodeKind::Group;

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

    /// The two sources stand for the same setting of a property: the same origin and, unless
    /// inherited, the same operation (an operation of a content is at one place only)
    static bool isSameSource(const PDFContentPropertySource& left, const PDFContentPropertySource& right)
    {
        if (left.origin != right.origin)
        {
            return false;
        }

        return left.origin == PDFContentPropertySource::Origin::Inherited ||
               (left.operationIndex == right.operationIndex && left.resourceName == right.resourceName && left.extGStateKey == right.extGStateKey);
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
        if (!node || !(PDFContentNodeEndHelper::isTailNode(node->getData()) || PDFContentNodeEndHelper::isPinnedEnd(node, m_data.operations)))
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

std::optional<PDFContentStateOverride> PDFContentContainer::getEffectiveOverride(PDFContentNodeId id) const
{
    const PDFContentNodePointer node = getNode(id);
    if (!node)
    {
        return std::nullopt;
    }

    // The ancestors from the root down to the parent (as many as the tree is deep)
    std::vector<PDFContentNodePointer> ancestors;
    for (PDFContentNodePointer parent = getParent(id); parent; parent = getParent(parent->getId()))
    {
        ancestors.push_back(parent);
    }
    std::reverse(ancestors.begin(), ancestors.end());

    const QTransform nodeMatrix = node->getState() ? node->getState()->getCurrentTransformationMatrix() : QTransform();
    const PDFContentSymbolicState* nodeSymbolic = node->getSymbolicState().get();

    // R' = R x C(inner) x ... x C(outer), C = S^-1 x M x S of each ancestor with a matrix; the
    // product of the ancestors is built from the outermost one inward: C(inner) x (the product)
    QTransform ancestorsMatrix;
    PDFContentStateOverride result;
    for (const PDFContentNodePointer& ancestor : ancestors)
    {
        const PDFContentStateOverride* override = ancestor->getOverride();
        if (!override)
        {
            continue;
        }

        if (!override->transform.isIdentity())
        {
            const QTransform start = ancestor->getState() ? ancestor->getState()->getCurrentTransformationMatrix() : QTransform();
            if (!start.isInvertible())
            {
                return std::nullopt;
            }
            ancestorsMatrix = start.inverted() * override->transform * start * ancestorsMatrix;
        }

        // A nearer wrap is visited later and wins
        const PDFContentSymbolicState* ancestorSymbolic = ancestor->getSymbolicState().get();
        for (const auto& item : override->properties)
        {
            const bool isReaching = !nodeSymbolic || !ancestorSymbolic ||
                                    PDFContentNodeEndHelper::isSameSource(nodeSymbolic->getSource(item.first), ancestorSymbolic->getSource(item.first));
            if (isReaching)
            {
                result.properties[item.first] = item.second;
            }
            else
            {
                // Set between the beginning of the ancestor and the node: the value of a farther
                // wrap does not reach the node either (it was set after that one began too)
                result.properties.erase(item.first);
            }
        }
    }

    QTransform ownMatrix;
    if (const PDFContentStateOverride* override = node->getOverride())
    {
        ownMatrix = override->transform;
        for (const auto& item : override->properties)
        {
            result.properties[item.first] = item.second;
        }
    }

    if (ancestorsMatrix.isIdentity())
    {
        result.transform = ownMatrix;
    }
    else
    {
        if (!nodeMatrix.isInvertible())
        {
            return std::nullopt;
        }

        // T x R = M(own) x R x (the product of the ancestors)
        result.transform = ownMatrix * nodeMatrix * ancestorsMatrix * nodeMatrix.inverted();
    }

    return result;
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
    /// One node of the tree being changed. Owned by the map of the editor (the address of an
    /// item of the map never changes, so the nodes point to each other).
    ///
    /// The children of a node are a sequence kept as a balanced tree over the work nodes
    /// themselves (a treap ordered by the position, with the number of the nodes of every
    /// subtree): the index of a child, the child at an index, an insertion and a removal cost
    /// the logarithm of the number of the children. A command over many children of one parent
    /// (the root of a flat page) needs it: with a plain array every removal and every query of
    /// an index costs the number of the children.
    struct WorkNode
    {
        PDFContentNodePointer original;                 ///< The node of the source container; nullptr for a new node
        std::unique_ptr<PDFContentNode::Data> data;     ///< The data, when they differ from the original ones (always for a new node)
        quint64 id = 0;
        WorkNode* parent = nullptr;                     ///< nullptr for the root
        WorkNode* children = nullptr;                   ///< The root of the sequence of the children; nullptr without children
        int depth = 0;                                  ///< 0 for the root
        bool isChanged = false;                         ///< The node, or a node below it, differs from the original one
        bool isClipChanging = false;                    ///< The node changes the clipping path for what follows it (clipping effect, or a leak, which says so)

        // The place of the node in the sequence of the children of its parent
        WorkNode* left = nullptr;                       ///< The nodes in front of this one in its subtree of the sequence
        WorkNode* right = nullptr;                      ///< The nodes behind this one in its subtree of the sequence
        WorkNode* up = nullptr;                         ///< The node of the sequence above; nullptr for the root of the sequence
        size_t count = 1;                               ///< Nodes of the subtree of the sequence, this one included
        size_t clipChangingCount = 0;                   ///< Nodes of the subtree of the sequence with isClipChanging
        quint64 priority = 0;                           ///< Heap order of the sequence: a node has a priority not smaller than the nodes below it

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

    /// A new work node of an identifier, which is not used (its place in a sequence is set by
    /// insertChild)
    WorkNode* createNode(quint64 id);

    /// Marks a node and the nodes above it as changed and forgets the immutable nodes made for
    /// them (WorkNode::current)
    void markChanged(WorkNode* node);

    /// Replaces the data of a node (its identifiers are set from the tree)
    void setData(WorkNode* node, PDFContentNode::Data data);

    /// The data of a node as its own copy with the parent of the tree (after a move)
    void updateParentOfData(WorkNode* node);

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
    static int getHeight(const WorkNode* node);

    /// Sets the depth of a node and of the nodes below it
    static void setDepth(WorkNode* node, int depth);

    /// Removes a node and the nodes below it from the map (not from the children of its parent)
    void eraseSubtree(WorkNode* node);

    /// The index of the children of a node, at which a child is inserted: \p index, or the end
    /// for -1; no value for an index out of range
    static std::optional<size_t> getInsertPosition(const WorkNode& parent, PDFInteger index);

    // ---- the sequence of the children of a node ----

    static size_t getCount(const WorkNode* sequence) { return sequence ? sequence->count : 0; }
    static size_t getChildCount(const WorkNode& parent) { return getCount(parent.children); }

    /// The priority of the node of an identifier in a sequence: a mix of the bits of the
    /// identifier and of a seed chosen once per process (the identifiers are known - given by
    /// the tree builder and by the session -, so a fixed mix would let one order command choose
    /// the shape of the treap). The order of the children does not depend on it.
    static quint64 getPriority(quint64 id);

    /// The node changes the clipping path for what follows it
    static bool isClipChanging(const PDFContentNode::Data& data) { return data.hasClipEffect || data.leak.isClipChanged; }

    /// Computes the counts of a node of a sequence from its two subtrees and makes it the node
    /// above them
    static void updateSequenceNode(WorkNode* node);

    /// Updates the counts of a node of a sequence and of the nodes above it
    static void updateSequencePath(WorkNode* node);

    /// One step of a walk down a sequence (mergeSequences, splitSequence)
    struct SequenceStep
    {
        WorkNode* node = nullptr;
        bool isFirstSide = false;   ///< mergeSequences: a node of the first sequence (the rest goes to its right side); splitSequence: the node belongs to the first result
    };

    /// One sequence of two: every node of \p first stands in front of every node of \p second.
    /// The node above the result is not set.
    WorkNode* mergeSequences(WorkNode* first, WorkNode* second);

    /// Two sequences of one: the first \p count nodes and the rest. The nodes above the two
    /// results are not set.
    void splitSequence(WorkNode* sequence, size_t count, WorkNode** first, WorkNode** second);

    /// Puts a node, which is in no sequence, among the children of a parent at a position
    /// (0 ... the number of the children)
    void insertChild(WorkNode* parent, WorkNode* node, size_t position);

    /// Takes a node out of the children of its parent
    void removeChild(WorkNode* parent, WorkNode* node);

    /// The position of a node among the children of its parent
    static size_t getPosition(const WorkNode* node);

    /// The child of a parent at a position; nullptr out of range
    static WorkNode* getChild(const WorkNode& parent, size_t position);

    /// The last child of a parent; nullptr without children
    static WorkNode* getLastChild(const WorkNode& parent);

    /// Appends the children of a parent in their order
    static void addChildren(const WorkNode& parent, std::vector<WorkNode*>* children);

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
    std::vector<SequenceStep> sequencePath;     ///< The walk of mergeSequences / splitSequence (kept to be used again)

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

    nodes.reserve(container->getNodeCount());

    rootId = root->getId().value;

    WorkNode* rootNode = createNode(rootId);
    rootNode->original = root;
    rootNode->isClipChanging = isClipChanging(root->getData());
    if (root->getParentId().isValid())
    {
        // The root says a parent: its data are corrected
        rootNode->isChanged = true;
        updateParentOfData(rootNode);
    }

    std::vector<WorkNode*> stack;
    std::vector<WorkNode*> changedNodes;
    stack.push_back(rootNode);

    while (!stack.empty())
    {
        WorkNode* workNode = stack.back();
        stack.pop_back();

        const quint64 id = workNode->id;
        for (const PDFContentNodePointer& child : workNode->original->getChildren())
        {
            // A missing child and an identifier used twice (a tree made by hand) are left out
            if (!child || !child->getId().isValid() || nodes.count(child->getId().value))
            {
                workNode->isChanged = true;
                continue;
            }

            WorkNode* childNode = createNode(child->getId().value);
            childNode->original = child;
            childNode->parent = workNode;
            childNode->depth = workNode->depth + 1;
            childNode->isClipChanging = isClipChanging(child->getData());
            childNode->clipChangingCount = childNode->isClipChanging ? 1 : 0;

            if (child->getParentId().value != id)
            {
                // The node says another parent than the tree: its data are corrected
                childNode->isChanged = true;
                updateParentOfData(childNode);
            }

            // The children of the source are in their order: each one goes to the end
            workNode->children = mergeSequences(workNode->children, childNode);
            workNode->children->up = nullptr;

            stack.push_back(childNode);
        }

        if (workNode->isChanged)
        {
            changedNodes.push_back(workNode);
        }
    }

    // A change below a node is a change of every node above it
    for (WorkNode* node : changedNodes)
    {
        node->isChanged = false;
        markChanged(node);
    }
}

void PDFContentContainerEditor::Impl::createRoot()
{
    rootId = 1;

    WorkNode* root = createNode(rootId);
    root->data = std::make_unique<PDFContentNode::Data>();
    root->data->kind = PDFContentNodeKind::Root;
    root->data->id = PDFContentNodeId{ rootId };
    root->isChanged = true;
}

PDFContentContainerEditor::Impl::WorkNode* PDFContentContainerEditor::Impl::find(quint64 id)
{
    auto it = nodes.find(id);
    return it != nodes.end() ? &it->second : nullptr;
}

PDFContentContainerEditor::Impl::WorkNode* PDFContentContainerEditor::Impl::createNode(quint64 id)
{
    WorkNode* node = &nodes[id];
    node->id = id;
    node->priority = getPriority(id);
    return node;
}

void PDFContentContainerEditor::Impl::markChanged(WorkNode* node)
{
    // The node itself is always marked. Above it the walk stops at the first node, which is
    // changed and has no immutable node made for it: a changed node has only changed nodes above
    // it, and none of them has such a node then (the rule of WorkNode::current).
    bool isFirst = true;
    while (node && (isFirst || !node->isChanged || node->current))
    {
        node->isChanged = true;
        node->current.reset();
        node = node->parent;
        isFirst = false;
    }
}

void PDFContentContainerEditor::Impl::setData(WorkNode* node, PDFContentNode::Data data)
{
    data.id = PDFContentNodeId{ node->id };
    data.parentId = PDFContentNodeId{ node->parent ? node->parent->id : 0 };

    const bool isNodeClipChanging = isClipChanging(data);
    if (node->data)
    {
        *node->data = std::move(data);
    }
    else
    {
        node->data = std::make_unique<PDFContentNode::Data>(std::move(data));
    }

    if (node->isClipChanging != isNodeClipChanging)
    {
        node->isClipChanging = isNodeClipChanging;
        updateSequencePath(node);
    }
}

void PDFContentContainerEditor::Impl::updateParentOfData(WorkNode* node)
{
    if (!node->data)
    {
        node->data = std::make_unique<PDFContentNode::Data>(node->original->getData());
    }

    node->data->id = PDFContentNodeId{ node->id };
    node->data->parentId = PDFContentNodeId{ node->parent ? node->parent->id : 0 };
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

        if (!PDFContentNodeEndHelper::isUnclosed(data) || !node->children)
        {
            return false;
        }

        node = getLastChild(*node);
    }

    return false;
}

quint64 PDFContentContainerEditor::Impl::getPriority(quint64 id)
{
    // The seed is chosen once per process (a function-local static: initialized on the first
    // call, thread-safe, no global constructor). Without it the priorities were a known function
    // of the identifiers, which are known too (the builder numbers the nodes depth first): an
    // order command naming the children in the order of decreasing priority made the treap one
    // chain, and every later editor of that page cost the square of the number of the children.
    // The order of the children never depends on the priorities, so nothing else changes.
    static const quint64 s_seed = QRandomGenerator::system()->generate64();

    // The finalizer of SplitMix64 (a bijection of 64 bits, which spreads consecutive numbers)
    quint64 value = (id ^ s_seed) + 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

void PDFContentContainerEditor::Impl::updateSequenceNode(WorkNode* node)
{
    node->count = 1;
    node->clipChangingCount = node->isClipChanging ? 1 : 0;

    for (WorkNode* below : { node->left, node->right })
    {
        if (below)
        {
            node->count += below->count;
            node->clipChangingCount += below->clipChangingCount;
            below->up = node;
        }
    }
}

void PDFContentContainerEditor::Impl::updateSequencePath(WorkNode* node)
{
    for (; node; node = node->up)
    {
        updateSequenceNode(node);
    }
}

PDFContentContainerEditor::Impl::WorkNode* PDFContentContainerEditor::Impl::mergeSequences(WorkNode* first, WorkNode* second)
{
    // The node with the larger priority stays on top; the walk goes down the right side of the
    // first sequence and the left side of the second one, as deep as they are (the logarithm
    // of their sizes on average). The path is kept in a list, so nothing recurses.
    if (!first)
    {
        return second;
    }
    if (!second)
    {
        return first;
    }

    std::vector<SequenceStep>& path = sequencePath;
    path.clear();

    while (first && second)
    {
        if (first->priority > second->priority)
        {
            path.push_back(SequenceStep{ first, true });
            first = first->right;
        }
        else
        {
            path.push_back(SequenceStep{ second, false });
            second = second->left;
        }
    }

    WorkNode* result = first ? first : second;
    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        (it->isFirstSide ? it->node->right : it->node->left) = result;
        updateSequenceNode(it->node);
        result = it->node;
    }

    return result;
}

void PDFContentContainerEditor::Impl::splitSequence(WorkNode* sequence, size_t count, WorkNode** first, WorkNode** second)
{
    // The walk goes down to the place of the cut; each node on the way goes to one of the two
    // results, with the rest of the walk at the side the walk left it
    std::vector<SequenceStep>& path = sequencePath;
    path.clear();

    WorkNode* node = sequence;
    while (node)
    {
        const size_t leftCount = getCount(node->left);
        if (leftCount >= count)
        {
            path.push_back(SequenceStep{ node, false });
            node = node->left;
        }
        else
        {
            path.push_back(SequenceStep{ node, true });
            count -= leftCount + 1;
            node = node->right;
        }
    }

    WorkNode* firstResult = nullptr;
    WorkNode* secondResult = nullptr;
    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        if (it->isFirstSide)
        {
            it->node->right = firstResult;
            updateSequenceNode(it->node);
            firstResult = it->node;
        }
        else
        {
            it->node->left = secondResult;
            updateSequenceNode(it->node);
            secondResult = it->node;
        }
    }

    *first = firstResult;
    *second = secondResult;
}

void PDFContentContainerEditor::Impl::insertChild(WorkNode* parent, WorkNode* node, size_t position)
{
    node->left = nullptr;
    node->right = nullptr;
    node->up = nullptr;
    updateSequenceNode(node);

    WorkNode* first = nullptr;
    WorkNode* second = nullptr;
    splitSequence(parent->children, position, &first, &second);

    parent->children = mergeSequences(mergeSequences(first, node), second);
    parent->children->up = nullptr;
    node->parent = parent;
}

void PDFContentContainerEditor::Impl::removeChild(WorkNode* parent, WorkNode* node)
{
    // The two subtrees of the node take its place; the counts above it are computed again
    WorkNode* merged = mergeSequences(node->left, node->right);
    WorkNode* above = node->up;

    if (merged)
    {
        merged->up = above;
    }

    if (!above)
    {
        parent->children = merged;
    }
    else
    {
        (above->left == node ? above->left : above->right) = merged;
        updateSequencePath(above);
    }

    node->left = nullptr;
    node->right = nullptr;
    node->up = nullptr;
    updateSequenceNode(node);
}

size_t PDFContentContainerEditor::Impl::getPosition(const WorkNode* node)
{
    size_t position = getCount(node->left);
    for (const WorkNode* current = node; current->up; current = current->up)
    {
        if (current->up->right == current)
        {
            position += getCount(current->up->left) + 1;
        }
    }
    return position;
}

PDFContentContainerEditor::Impl::WorkNode* PDFContentContainerEditor::Impl::getChild(const WorkNode& parent, size_t position)
{
    WorkNode* node = parent.children;
    while (node)
    {
        const size_t leftCount = getCount(node->left);
        if (position < leftCount)
        {
            node = node->left;
        }
        else if (position == leftCount)
        {
            return node;
        }
        else
        {
            position -= leftCount + 1;
            node = node->right;
        }
    }
    return nullptr;
}

PDFContentContainerEditor::Impl::WorkNode* PDFContentContainerEditor::Impl::getLastChild(const WorkNode& parent)
{
    WorkNode* node = parent.children;
    while (node && node->right)
    {
        node = node->right;
    }
    return node;
}

void PDFContentContainerEditor::Impl::addChildren(const WorkNode& parent, std::vector<WorkNode*>* children)
{
    // The sequence in its order (left subtree, node, right subtree), without recursion
    children->reserve(children->size() + getChildCount(parent));

    std::vector<WorkNode*> stack;
    WorkNode* node = parent.children;
    while (node || !stack.empty())
    {
        while (node)
        {
            stack.push_back(node);
            node = node->left;
        }

        node = stack.back();
        stack.pop_back();
        children->push_back(node);
        node = node->right;
    }
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
    WorkNode* root = find(rootId);
    if (!root || !root->isChanged)
    {
        return;
    }

    struct Frame
    {
        WorkNode* node = nullptr;
        std::vector<WorkNode*> children;    ///< The children of the node, when it is entered
        size_t remainingChildren = 0;
        bool isEntered = false;
    };

    bool isFollowed = false;

    std::vector<Frame> stack;
    stack.push_back(Frame{ root, {}, 0, false });

    while (!stack.empty())
    {
        Frame& frame = stack.back();
        WorkNode* node = frame.node;

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
                setData(node, std::move(closedData));   // "data" is not used after this line
                markChanged(node);
            }

            // The closing operators, the Q of a wrap
            const PDFContentNode::Data& currentData = getData(*node);
            if (!currentData.suffix.isEmpty() || currentData.override)
            {
                isFollowed = true;
            }

            addChildren(*node, &frame.children);
            frame.remainingChildren = frame.children.size();
        }

        if (frame.remainingChildren > 0)
        {
            WorkNode* child = frame.children[--frame.remainingChildren];
            stack.push_back(Frame{ child, {}, 0, false });    // "frame" is not used after this line
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

int PDFContentContainerEditor::Impl::getHeight(const WorkNode* node)
{
    int height = 0;

    std::vector<WorkNode*> children;
    std::vector<std::pair<const WorkNode*, int>> stack;
    stack.emplace_back(node, 0);

    while (!stack.empty())
    {
        const auto [current, level] = stack.back();
        stack.pop_back();

        height = qMax(height, level);

        children.clear();
        addChildren(*current, &children);
        for (const WorkNode* child : children)
        {
            stack.emplace_back(child, level + 1);
        }
    }

    return height;
}

void PDFContentContainerEditor::Impl::setDepth(WorkNode* node, int depth)
{
    std::vector<WorkNode*> children;
    std::vector<std::pair<WorkNode*, int>> stack;
    stack.emplace_back(node, depth);

    while (!stack.empty())
    {
        const auto [current, currentDepth] = stack.back();
        stack.pop_back();

        current->depth = currentDepth;

        children.clear();
        addChildren(*current, &children);
        for (WorkNode* child : children)
        {
            stack.emplace_back(child, currentDepth + 1);
        }
    }
}

void PDFContentContainerEditor::Impl::eraseSubtree(WorkNode* node)
{
    std::vector<WorkNode*> stack;
    stack.push_back(node);

    while (!stack.empty())
    {
        WorkNode* current = stack.back();
        stack.pop_back();

        // The children are read, before the node is destroyed
        addChildren(*current, &stack);
        nodes.erase(current->id);
    }
}

std::optional<size_t> PDFContentContainerEditor::Impl::getInsertPosition(const WorkNode& parent, PDFInteger index)
{
    const size_t count = getChildCount(parent);
    if (index == -1)
    {
        return count;
    }

    if (index < 0 || size_t(index) > count)
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
        WorkNode* node = nullptr;
        std::vector<WorkNode*> workChildren;    ///< The children of the node in their order
        size_t nextChild = 0;
        std::vector<PDFContentNodePointer> children;
    };

    auto createFrame = [](WorkNode* node)
    {
        Frame frame;
        frame.node = node;
        addChildren(*node, &frame.workChildren);
        frame.children.reserve(frame.workChildren.size());
        return frame;
    };

    std::vector<Frame> stack;
    stack.push_back(createFrame(top));

    PDFContentNodePointer result;
    while (!stack.empty())
    {
        Frame& frame = stack.back();

        if (frame.nextChild < frame.workChildren.size())
        {
            WorkNode* child = frame.workChildren[frame.nextChild++];

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
                stack.push_back(createFrame(child));    // "frame" is not used after this line
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
        data.id = PDFContentNodeId{ frame.node->id };
        data.parentId = PDFContentNodeId{ frame.node->parent ? frame.node->parent->id : 0 };

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

PDFContentNodeId PDFContentContainerEditor::getParentId(PDFContentNodeId id) const
{
    const Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    return PDFContentNodeId{ node && node->parent ? node->parent->id : 0 };
}

PDFInteger PDFContentContainerEditor::getIndexInParent(PDFContentNodeId id) const
{
    const Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    return node && node->parent ? PDFInteger(Impl::getPosition(node)) : -1;
}

PDFInteger PDFContentContainerEditor::getChildCount(PDFContentNodeId id) const
{
    const Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    return node ? PDFInteger(Impl::getChildCount(*node)) : 0;
}

PDFContentNodeId PDFContentContainerEditor::getChildId(PDFContentNodeId parentId, PDFInteger index) const
{
    const Impl::WorkNode* parent = !m_impl->isBuilt ? m_impl->find(parentId.value) : nullptr;
    const Impl::WorkNode* child = parent && index >= 0 ? Impl::getChild(*parent, size_t(index)) : nullptr;
    return PDFContentNodeId{ child ? child->id : 0 };
}

const PDFContentNode::Data* PDFContentContainerEditor::getNodeData(PDFContentNodeId id) const
{
    const Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    return node ? &m_impl->getData(*node) : nullptr;
}

bool PDFContentContainerEditor::isClipChangedByChildren(PDFContentNodeId id) const
{
    const Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    return node && node->children && node->children->clipChangingCount > 0;
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

    // The address of an item of the map does not change, when the map grows
    Impl::WorkNode* node = m_impl->createNode(data.id.value);
    node->parent = parent;
    node->depth = parent->depth + 1;
    node->isChanged = true;
    node->isClipChanging = Impl::isClipChanging(data);

    data.parentId = parentId;
    node->data = std::make_unique<PDFContentNode::Data>(std::move(data));

    m_impl->insertChild(parent, node, *position);
    m_impl->markChanged(parent);
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
    if (isRoot != (data.kind == PDFContentNodeKind::Root) || (node->children && PDFContentNode::isLeafKind(data.kind)))
    {
        return false;
    }

    m_impl->setData(node, std::move(data));
    m_impl->markChanged(node);
    return true;
}

bool PDFContentContainerEditor::removeNode(PDFContentNodeId id)
{
    Impl::WorkNode* node = !m_impl->isBuilt ? m_impl->find(id.value) : nullptr;
    if (!node || id.value == m_impl->rootId || !node->parent)
    {
        return false;
    }

    Impl::WorkNode* parent = node->parent;
    m_impl->removeChild(parent, node);
    m_impl->eraseSubtree(node);
    m_impl->markChanged(parent);
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
    for (const Impl::WorkNode* ancestor = newParent; ancestor; ancestor = ancestor->parent)
    {
        if (ancestor == node)
        {
            return false;
        }
    }

    if (newParent->depth + 1 + Impl::getHeight(node) > Impl::MAXIMUM_NODE_DEPTH)
    {
        return false;
    }

    Impl::WorkNode* oldParent = node->parent;
    if (!oldParent)
    {
        return false;
    }

    // The index is one of the children of the new parent without the moved node
    const size_t oldPosition = Impl::getPosition(node);
    m_impl->removeChild(oldParent, node);

    const std::optional<size_t> position = Impl::getInsertPosition(*newParent, index);
    if (!position)
    {
        // Nothing has changed: the node goes back to its place
        m_impl->insertChild(oldParent, node, oldPosition);
        return false;
    }

    m_impl->insertChild(newParent, node, *position);
    Impl::setDepth(node, newParent->depth + 1);

    // The parent identifier is a part of the data of the node: the node gets new ones
    m_impl->updateParentOfData(node);
    m_impl->markChanged(node);
    m_impl->markChanged(oldParent);
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

/// The data of a difference of two versions of a container (see PDFContentContainerDifference).
///
/// Threads and ownership: immutable after create; owned by the differences, which share it.
class PDFContentContainerDifference::Impl
{
public:
    /// A child at an index of the children of its parent
    struct Place
    {
        size_t index = 0;
        PDFContentNodePointer node;
    };

    /// A node, which is in both versions under the same parent and is kept in its order among
    /// the children, but is not the same object: its own data and what differs among its children
    struct Patch
    {
        quint64 id = 0;
        size_t indexBefore = 0;                                     ///< Among the children of its parent in the version before (0 for the root)
        size_t indexAfter = 0;                                      ///< The same in the version after
        std::shared_ptr<const PDFContentNode::Data> dataBefore;
        std::shared_ptr<const PDFContentNode::Data> dataAfter;
        std::vector<Place> removed;                                 ///< The children of the version before, which the version after does not keep at their place, by their index before (ascending)
        std::vector<Place> inserted;                                ///< The children of the version after, which are not kept children, by their index after (ascending)
        std::vector<Patch> children;                                ///< The kept children, which differ themselves (ascending by both indices)
    };

    /// An operation, whose flags the change has set
    struct FlagChange
    {
        size_t index = 0;
        PDFContentOperation::Flags before;
        PDFContentOperation::Flags after;
    };

    /// An entry added to the resources
    struct ResourceEntry
    {
        PDFContentResourceCategory category = PDFContentResourceCategory::Font;
        QByteArray name;
        QByteArray prefix;      ///< The name without its number (PDFContentResources::addObject gives the name again)
        PDFObject object;
    };

    /// A tree deeper than this is not kept by its changed places (the editor makes none)
    static constexpr int MAXIMUM_PATCH_DEPTH = PDFContentContainer::MAXIMUM_DEPTH + 8;

    PDFContentContainerKey key;
    quint64 versionBefore = 0;
    quint64 versionAfter = 0;
    bool isCompact = true;
    size_t storedItemCount = 0;

    // ---- a compact difference ----

    bool isTreeChanged = false;
    Patch root;
    size_t operationCountBefore = 0;
    size_t operationCountAfter = 0;
    std::vector<PDFContentOperation> appendedOperations;
    std::vector<FlagChange> flagChanges;
    std::vector<ResourceEntry> addedResources;

    // ---- a difference, which is not compact: both versions ----

    PDFContentContainerPointer before;
    PDFContentContainerPointer after;

    /// The parts of the difference; false, when one of them cannot be kept by its changed places
    bool createTree(const PDFContentContainer& containerBefore, const PDFContentContainer& containerAfter);
    bool createOperations(const PDFContentOperationList& operationsBefore, size_t countBefore, const PDFContentOperationList& operationsAfter, size_t countAfter);
    bool createResources(const PDFContentResources& resourcesBefore, const PDFContentResources& resourcesAfter);

    /// The other data of the two containers are the same, as far as it can be told
    static bool isOtherDataEqual(const PDFContentContainer::Data& dataBefore, const PDFContentContainer::Data& dataAfter);

    static bool createPatch(const PDFContentNodePointer& nodeBefore, const PDFContentNodePointer& nodeAfter, int depth, Patch* patch);

    /// The indices of a longest strictly increasing subsequence of values
    static std::vector<size_t> getLongestIncreasingSubsequence(const std::vector<size_t>& values);

    /// The node of the other version made from a node of one version
    /// \param isForward From the version before to the version after
    /// \returns nullptr, when the node is not the one the patch was made from
    static PDFContentNodePointer applyPatch(const Patch& patch, const PDFContentNodePointer& node, bool isForward);

    /// The data of the other version made from a container of one version; false, when the
    /// container is not that version
    bool createData(const PDFContentContainer& container, bool isForward, PDFContentContainer::Data* data) const;

    size_t getStoredItemCount(const Patch& patch) const;

    static void addObjectsOfData(const PDFContentNode::Data& data, std::vector<PDFObject>* objects);
    static void addObjectsOfSubtree(const PDFContentNodePointer& node, std::vector<PDFObject>* objects);
    static void addObjectsOfPatch(const Patch& patch, std::vector<PDFObject>* objects);
};

bool PDFContentContainerDifference::Impl::isOtherDataEqual(const PDFContentContainer::Data& dataBefore, const PDFContentContainer::Data& dataAfter)
{
    // A command changes the tree, the operations and the resources. Whatever else may differ
    // one day is not restored by the changed places, so the difference keeps both versions then.
    return dataBefore.initialState == dataAfter.initialState &&
           dataBefore.initialSymbolicState == dataAfter.initialSymbolicState &&
           dataBefore.boundingBox == dataAfter.boundingBox &&
           dataBefore.matrix == dataAfter.matrix &&
           dataBefore.structuralParentKey == dataAfter.structuralParentKey &&
           dataBefore.fingerprint == dataAfter.fingerprint &&
           dataBefore.issues.size() == dataAfter.issues.size() &&
           dataBefore.isReadOnly == dataAfter.isReadOnly &&
           dataBefore.isColorIgnored == dataAfter.isColorIgnored &&
           dataBefore.inheritedResourcesPageIndex == dataAfter.inheritedResourcesPageIndex &&
           dataBefore.inheritedResources.getType() == dataAfter.inheritedResources.getType() &&
           (!dataBefore.inheritedResources.isDictionary() || dataBefore.inheritedResources.getDictionary() == dataAfter.inheritedResources.getDictionary());
}

std::vector<size_t> PDFContentContainerDifference::Impl::getLongestIncreasingSubsequence(const std::vector<size_t>& values)
{
    // Patience sorting: tails[k] is the index of the smallest last value of an increasing
    // subsequence of the length k + 1; previous links each value to the one before it
    std::vector<size_t> tails;
    std::vector<size_t> previous(values.size(), 0);

    for (size_t i = 0; i < values.size(); ++i)
    {
        auto it = std::lower_bound(tails.begin(), tails.end(), values[i], [&values](size_t tail, size_t value) { return values[tail] < value; });
        previous[i] = it == tails.begin() ? values.size() : *(it - 1);

        if (it == tails.end())
        {
            tails.push_back(i);
        }
        else
        {
            *it = i;
        }
    }

    std::vector<size_t> result(tails.size());
    size_t current = tails.empty() ? values.size() : tails.back();
    for (size_t i = tails.size(); i > 0; --i)
    {
        result[i - 1] = current;
        current = previous[current];
    }
    return result;
}

bool PDFContentContainerDifference::Impl::createPatch(const PDFContentNodePointer& nodeBefore, const PDFContentNodePointer& nodeAfter, int depth, Patch* patch)
{
    if (depth > MAXIMUM_PATCH_DEPTH)
    {
        return false;
    }

    patch->id = nodeBefore->getId().value;
    patch->dataBefore = std::make_shared<const PDFContentNode::Data>(nodeBefore->getData());
    patch->dataAfter = std::make_shared<const PDFContentNode::Data>(nodeAfter->getData());

    const std::vector<PDFContentNodePointer>& childrenBefore = nodeBefore->getChildren();
    const std::vector<PDFContentNodePointer>& childrenAfter = nodeAfter->getChildren();

    // The children at the beginning and at the end, which are the same objects, stay: a command
    // over a few children of a long parent leaves a short middle part
    const size_t shorter = qMin(childrenBefore.size(), childrenAfter.size());
    size_t prefix = 0;
    while (prefix < shorter && childrenBefore[prefix] == childrenAfter[prefix])
    {
        ++prefix;
    }

    size_t suffix = 0;
    while (suffix < shorter - prefix && childrenBefore[childrenBefore.size() - 1 - suffix] == childrenAfter[childrenAfter.size() - 1 - suffix])
    {
        ++suffix;
    }

    const size_t countBefore = childrenBefore.size() - prefix - suffix;
    const size_t countAfter = childrenAfter.size() - prefix - suffix;

    for (size_t i = 0; i < countBefore; ++i)
    {
        if (!childrenBefore[prefix + i])
        {
            return false;
        }
    }
    for (size_t i = 0; i < countAfter; ++i)
    {
        if (!childrenAfter[prefix + i])
        {
            return false;
        }
    }

    // The children of the middle part, which are in both versions (by their identifiers) and
    // stay in their order: offsets in the middle part, before and after
    std::vector<std::pair<size_t, size_t>> kept;

    bool isAligned = countBefore == countAfter;
    for (size_t i = 0; isAligned && i < countBefore; ++i)
    {
        isAligned = childrenBefore[prefix + i]->getId() == childrenAfter[prefix + i]->getId();
    }

    if (isAligned)
    {
        // The same children at the same places (they were changed in their place)
        kept.reserve(countBefore);
        for (size_t i = 0; i < countBefore; ++i)
        {
            kept.emplace_back(i, i);
        }
    }
    else
    {
        std::unordered_map<quint64, size_t> offsetsAfter;
        offsetsAfter.reserve(countAfter);
        for (size_t i = 0; i < countAfter; ++i)
        {
            const PDFContentNodeId id = childrenAfter[prefix + i]->getId();
            if (!id.isValid() || !offsetsAfter.emplace(id.value, i).second)
            {
                // A tree made by hand: an identifier used twice
                return false;
            }
        }

        std::vector<std::pair<size_t, size_t>> candidates;
        std::vector<size_t> values;
        for (size_t i = 0; i < countBefore; ++i)
        {
            auto it = offsetsAfter.find(childrenBefore[prefix + i]->getId().value);
            if (it != offsetsAfter.end())
            {
                candidates.emplace_back(i, it->second);
                values.push_back(it->second);
            }
        }

        // The children, which keep their order: a child outside of the longest common order
        // was moved, it is taken away at its old place and put in at its new one
        const bool isIncreasing = std::adjacent_find(values.cbegin(), values.cend(), [](size_t left, size_t right) { return left >= right; }) == values.cend();
        if (isIncreasing)
        {
            kept = std::move(candidates);
        }
        else
        {
            for (const size_t index : getLongestIncreasingSubsequence(values))
            {
                kept.push_back(candidates[index]);
            }
        }
    }

    std::vector<char> isKeptBefore(countBefore, 0);
    std::vector<char> isKeptAfter(countAfter, 0);

    for (const auto& item : kept)
    {
        const PDFContentNodePointer& childBefore = childrenBefore[prefix + item.first];
        const PDFContentNodePointer& childAfter = childrenAfter[prefix + item.second];

        if (childBefore == childAfter)
        {
            isKeptBefore[item.first] = 1;
            isKeptAfter[item.second] = 1;
            continue;
        }

        if (childBefore->getChildren().empty() && childAfter->getChildren().empty())
        {
            // A leaf (or an empty node) with other data: the old node is taken away and the
            // new one put in at the same place
            continue;
        }

        Patch child;
        child.indexBefore = prefix + item.first;
        child.indexAfter = prefix + item.second;
        if (!createPatch(childBefore, childAfter, depth + 1, &child))
        {
            return false;
        }

        patch->children.push_back(std::move(child));
        isKeptBefore[item.first] = 1;
        isKeptAfter[item.second] = 1;
    }

    for (size_t i = 0; i < countBefore; ++i)
    {
        if (!isKeptBefore[i])
        {
            patch->removed.push_back(Place{ prefix + i, childrenBefore[prefix + i] });
        }
    }
    for (size_t i = 0; i < countAfter; ++i)
    {
        if (!isKeptAfter[i])
        {
            patch->inserted.push_back(Place{ prefix + i, childrenAfter[prefix + i] });
        }
    }

    return true;
}

bool PDFContentContainerDifference::Impl::createTree(const PDFContentContainer& containerBefore, const PDFContentContainer& containerAfter)
{
    const PDFContentNodePointer& rootBefore = containerBefore.getRoot();
    const PDFContentNodePointer& rootAfter = containerAfter.getRoot();

    if (rootBefore == rootAfter)
    {
        return true;
    }

    if (!rootBefore || !rootAfter || rootBefore->getId() != rootAfter->getId())
    {
        return false;
    }

    isTreeChanged = true;
    return createPatch(rootBefore, rootAfter, 0, &root);
}

bool PDFContentContainerDifference::Impl::createOperations(const PDFContentOperationList& operationsBefore, size_t countBefore, const PDFContentOperationList& operationsAfter, size_t countAfter)
{
    // The operations, which belong to each version (PDFContentContainer::m_operationCount: the
    // whole pool of every version, also of one made again by getBefore or getAfter)
    operationCountBefore = countBefore;
    operationCountAfter = countAfter;

    if (operationCountAfter < operationCountBefore ||
        operationsBefore.getCount() < operationCountBefore ||
        operationsAfter.getCount() < operationCountAfter ||
        operationsBefore.getOriginalCount() != operationsAfter.getOriginalCount() ||
        operationsBefore.getTail() != operationsAfter.getTail())
    {
        return false;
    }

    // The two lists share the blocks of the operations, which the change did not touch: an
    // operation at the same address is the same operation. Of the others only the flags can
    // differ (the pool is appended to, and an operation changes only by its flags).
    for (size_t i = 0; i < operationCountBefore; ++i)
    {
        const PDFContentOperation& operationBefore = operationsBefore.getOperation(i);
        const PDFContentOperation& operationAfter = operationsAfter.getOperation(i);
        if (&operationBefore != &operationAfter && operationBefore.getFlags() != operationAfter.getFlags())
        {
            flagChanges.push_back(FlagChange{ i, operationBefore.getFlags(), operationAfter.getFlags() });
        }
    }

    appendedOperations.reserve(operationCountAfter - operationCountBefore);
    for (size_t i = operationCountBefore; i < operationCountAfter; ++i)
    {
        appendedOperations.push_back(operationsAfter.getOperation(i));
    }

    return true;
}

bool PDFContentContainerDifference::Impl::createResources(const PDFContentResources& resourcesBefore, const PDFContentResources& resourcesAfter)
{
    bool isChanged = false;

    for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
    {
        const std::vector<PDFContentResources::Entry>& entriesBefore = resourcesBefore.getEntries(category);
        const std::vector<PDFContentResources::Entry>& entriesAfter = resourcesAfter.getEntries(category);

        // The data of a category, which was not changed, are shared by the two versions
        if (&entriesBefore == &entriesAfter)
        {
            continue;
        }

        if (entriesAfter.size() < entriesBefore.size())
        {
            return false;
        }

        for (size_t i = 0; i < entriesBefore.size(); ++i)
        {
            const PDFContentResources::Entry& entryBefore = entriesBefore[i];
            const PDFContentResources::Entry& entryAfter = entriesAfter[i];
            if (entryBefore.name != entryAfter.name || entryBefore.isOriginal != entryAfter.isOriginal ||
                (!entryBefore.isOriginal && !(entryBefore.object == entryAfter.object)))
            {
                return false;
            }
        }

        for (size_t i = entriesBefore.size(); i < entriesAfter.size(); ++i)
        {
            const PDFContentResources::Entry& entry = entriesAfter[i];

            // The name of PDFContentResources::allocateName: a prefix and a number
            qsizetype prefixLength = entry.name.size();
            while (prefixLength > 0 && entry.name[prefixLength - 1] >= '0' && entry.name[prefixLength - 1] <= '9')
            {
                --prefixLength;
            }

            addedResources.push_back(ResourceEntry{ category, entry.name, entry.name.left(prefixLength), entry.object });
            isChanged = true;
        }
    }

    if (!isChanged)
    {
        return true;
    }

    // The entries must come back with their names, when they are added again to the old
    // resources (getAfter does exactly this)
    PDFContentResources resources = resourcesBefore;
    for (const ResourceEntry& entry : addedResources)
    {
        if (resources.addObject(entry.category, entry.prefix, entry.object) != entry.name)
        {
            return false;
        }
    }

    for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
    {
        if (resources.getEntries(category).size() != resourcesAfter.getEntries(category).size())
        {
            return false;
        }
    }

    return true;
}

PDFContentNodePointer PDFContentContainerDifference::Impl::applyPatch(const Patch& patch, const PDFContentNodePointer& node, bool isForward)
{
    if (!node || node->getId().value != patch.id)
    {
        return PDFContentNodePointer();
    }

    const std::vector<Place>& removed = isForward ? patch.removed : patch.inserted;
    const std::vector<Place>& inserted = isForward ? patch.inserted : patch.removed;
    const std::vector<PDFContentNodePointer>& source = node->getChildren();

    if (source.size() < removed.size())
    {
        return PDFContentNodePointer();
    }

    // The children, which stay, in their order; a child with a patch as its other version
    std::vector<PDFContentNodePointer> kept;
    kept.reserve(source.size() - removed.size());

    size_t removedIndex = 0;
    size_t patchIndex = 0;
    for (size_t i = 0; i < source.size(); ++i)
    {
        if (removedIndex < removed.size() && removed[removedIndex].index == i)
        {
            if (!source[i] || source[i]->getId() != removed[removedIndex].node->getId())
            {
                return PDFContentNodePointer();
            }

            ++removedIndex;
            continue;
        }

        if (patchIndex < patch.children.size() && (isForward ? patch.children[patchIndex].indexBefore : patch.children[patchIndex].indexAfter) == i)
        {
            PDFContentNodePointer child = applyPatch(patch.children[patchIndex], source[i], isForward);
            if (!child)
            {
                return PDFContentNodePointer();
            }

            kept.push_back(std::move(child));
            ++patchIndex;
            continue;
        }

        kept.push_back(source[i]);
    }

    if (removedIndex != removed.size() || patchIndex != patch.children.size())
    {
        return PDFContentNodePointer();
    }

    // The children put in stand at their indices, the kept ones fill the places between them
    const size_t count = kept.size() + inserted.size();
    std::vector<PDFContentNodePointer> children;
    children.reserve(count);

    size_t keptIndex = 0;
    size_t insertedIndex = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (insertedIndex < inserted.size() && inserted[insertedIndex].index == i)
        {
            children.push_back(inserted[insertedIndex++].node);
        }
        else if (keptIndex < kept.size())
        {
            children.push_back(std::move(kept[keptIndex++]));
        }
        else
        {
            return PDFContentNodePointer();
        }
    }

    if (insertedIndex != inserted.size() || keptIndex != kept.size())
    {
        return PDFContentNodePointer();
    }

    return std::make_shared<const PDFContentNode>(isForward ? *patch.dataAfter : *patch.dataBefore, std::move(children));
}

bool PDFContentContainerDifference::Impl::createData(const PDFContentContainer& container, bool isForward, PDFContentContainer::Data* data) const
{
    if (container.getKey() != key || container.getVersion() != (isForward ? versionBefore : versionAfter))
    {
        return false;
    }

    *data = container.getData();

    // The tree
    if (isTreeChanged)
    {
        data->root = applyPatch(root, container.getRoot(), isForward);
        if (!data->root)
        {
            return false;
        }
    }

    // The operations. A version owns exactly its own operations: the version before has the
    // first operationCountBefore operations of the pool (the operations the change appended
    // are cut away, whether the old object is still held or made again here), the version after
    // has them and the appended ones behind them at the same indices. A pool longer than its
    // version (a container made from the data of another one by hand) is cut first.
    PDFContentOperationList& operations = data->operations;
    if (operations.getCount() < (isForward ? operationCountBefore : operationCountAfter) || appendedOperations.size() != operationCountAfter - operationCountBefore)
    {
        return false;
    }

    operations = operations.getPrefix(operationCountBefore);
    if (operations.getCount() != operationCountBefore)
    {
        return false;
    }

    if (isForward)
    {
        for (const PDFContentOperation& operation : appendedOperations)
        {
            operations.append(operation);
        }
    }

    for (const FlagChange& change : flagChanges)
    {
        operations.setFlags(change.index, isForward ? change.after : change.before);
    }

    // The resources
    if (isForward)
    {
        for (const ResourceEntry& entry : addedResources)
        {
            if (data->resources.addObject(entry.category, entry.prefix, entry.object) != entry.name)
            {
                return false;
            }
        }
    }
    else
    {
        for (auto it = addedResources.crbegin(); it != addedResources.crend(); ++it)
        {
            if (!data->resources.removeAdded(it->category, it->name))
            {
                return false;
            }
        }
    }

    return true;
}

size_t PDFContentContainerDifference::Impl::getStoredItemCount(const Patch& patch) const
{
    // The patch with its two copies of the data, the places of the children
    size_t count = 3 + patch.removed.size() + patch.inserted.size();
    for (const Patch& child : patch.children)
    {
        count += getStoredItemCount(child);
    }
    return count;
}

void PDFContentContainerDifference::Impl::addObjectsOfData(const PDFContentNode::Data& data, std::vector<PDFObject>* objects)
{
    auto addObject = [objects](const PDFObject& object)
    {
        if (!object.isNull())
        {
            objects->push_back(object);
        }
    };

    addObject(data.properties);

    if (data.xobject.isValid())
    {
        objects->push_back(PDFObject::createReference(data.xobject));
    }
    if (data.optionalContent.isValid())
    {
        objects->push_back(PDFObject::createReference(data.optionalContent));
    }

    if (data.override)
    {
        for (const auto& item : data.override->properties)
        {
            addObject(item.second.object);
            addObject(item.second.color.colorSpaceObject);
            addObject(item.second.color.patternObject);
        }
    }
}

void PDFContentContainerDifference::Impl::addObjectsOfSubtree(const PDFContentNodePointer& node, std::vector<PDFObject>* objects)
{
    std::vector<const PDFContentNode*> stack;
    if (node)
    {
        stack.push_back(node.get());
    }

    while (!stack.empty())
    {
        const PDFContentNode* current = stack.back();
        stack.pop_back();

        addObjectsOfData(current->getData(), objects);
        for (const PDFContentNodePointer& child : current->getChildren())
        {
            if (child)
            {
                stack.push_back(child.get());
            }
        }
    }
}

void PDFContentContainerDifference::Impl::addObjectsOfPatch(const Patch& patch, std::vector<PDFObject>* objects)
{
    addObjectsOfData(*patch.dataBefore, objects);
    addObjectsOfData(*patch.dataAfter, objects);

    for (const std::vector<Place>* places : { &patch.removed, &patch.inserted })
    {
        for (const Place& place : *places)
        {
            addObjectsOfSubtree(place.node, objects);
        }
    }

    for (const Patch& child : patch.children)
    {
        addObjectsOfPatch(child, objects);
    }
}

PDFContentContainerDifference::PDFContentContainerDifference() = default;
PDFContentContainerDifference::~PDFContentContainerDifference() = default;
PDFContentContainerDifference::PDFContentContainerDifference(const PDFContentContainerDifference&) = default;
PDFContentContainerDifference::PDFContentContainerDifference(PDFContentContainerDifference&&) noexcept = default;
PDFContentContainerDifference& PDFContentContainerDifference::operator=(const PDFContentContainerDifference&) = default;
PDFContentContainerDifference& PDFContentContainerDifference::operator=(PDFContentContainerDifference&&) noexcept = default;

PDFContentContainerDifference PDFContentContainerDifference::create(const PDFContentContainerPointer& before, const PDFContentContainerPointer& after)
{
    PDFContentContainerDifference result;
    if (!before || !after || before->getKey() != after->getKey())
    {
        return result;
    }

    std::shared_ptr<Impl> impl = std::make_shared<Impl>();
    impl->key = before->getKey();
    impl->versionBefore = before->getVersion();
    impl->versionAfter = after->getVersion();

    if (before != after)
    {
        impl->isCompact = Impl::isOtherDataEqual(before->getData(), after->getData()) &&
                          impl->createTree(*before, *after) &&
                          impl->createOperations(before->getOperations(), before->m_operationCount, after->getOperations(), after->m_operationCount) &&
                          impl->createResources(before->getResources(), after->getResources());
    }

    if (impl->isCompact)
    {
        impl->storedItemCount = (impl->isTreeChanged ? impl->getStoredItemCount(impl->root) : 0) +
                                impl->appendedOperations.size() + impl->flagChanges.size() + impl->addedResources.size();
    }
    else
    {
        // Both versions as they are; whatever was collected is dropped
        impl->isTreeChanged = false;
        impl->root = Impl::Patch();
        impl->appendedOperations.clear();
        impl->flagChanges.clear();
        impl->addedResources.clear();
        impl->before = before;
        impl->after = after;
        impl->storedItemCount = before->getNodeCount() + after->getNodeCount();
    }

    result.m_impl = std::move(impl);
    return result;
}

bool PDFContentContainerDifference::isValid() const
{
    return m_impl != nullptr;
}

bool PDFContentContainerDifference::isCompact() const
{
    return m_impl && m_impl->isCompact;
}

quint64 PDFContentContainerDifference::getVersionBefore() const
{
    return m_impl ? m_impl->versionBefore : 0;
}

quint64 PDFContentContainerDifference::getVersionAfter() const
{
    return m_impl ? m_impl->versionAfter : 0;
}

PDFContentContainerPointer PDFContentContainerDifference::getAfter(const PDFContentContainerPointer& before) const
{
    if (!m_impl || !before)
    {
        return PDFContentContainerPointer();
    }

    if (!m_impl->isCompact)
    {
        return before->getVersion() == m_impl->versionBefore ? m_impl->after : PDFContentContainerPointer();
    }

    if (m_impl->versionBefore == m_impl->versionAfter)
    {
        // The difference of a version and itself
        return before->getVersion() == m_impl->versionBefore ? before : PDFContentContainerPointer();
    }

    PDFContentContainer::Data data;
    if (!m_impl->createData(*before, true, &data))
    {
        return PDFContentContainerPointer();
    }

    return PDFContentContainerPointer(new PDFContentContainer(std::move(data), m_impl->versionAfter, m_impl->operationCountAfter));
}

PDFContentContainerPointer PDFContentContainerDifference::getBefore(const PDFContentContainerPointer& after) const
{
    if (!m_impl || !after)
    {
        return PDFContentContainerPointer();
    }

    if (!m_impl->isCompact)
    {
        return after->getVersion() == m_impl->versionAfter ? m_impl->before : PDFContentContainerPointer();
    }

    if (m_impl->versionBefore == m_impl->versionAfter)
    {
        return after->getVersion() == m_impl->versionAfter ? after : PDFContentContainerPointer();
    }

    PDFContentContainer::Data data;
    if (!m_impl->createData(*after, false, &data))
    {
        return PDFContentContainerPointer();
    }

    return PDFContentContainerPointer(new PDFContentContainer(std::move(data), m_impl->versionBefore, m_impl->operationCountBefore));
}

size_t PDFContentContainerDifference::getStoredItemCount() const
{
    return m_impl ? m_impl->storedItemCount : 0;
}

void PDFContentContainerDifference::addObjects(std::vector<PDFObject>* objects) const
{
    if (!m_impl || !objects)
    {
        return;
    }

    if (!m_impl->isCompact)
    {
        for (const PDFContentContainerPointer& container : { m_impl->before, m_impl->after })
        {
            Impl::addObjectsOfSubtree(container->getRoot(), objects);
            for (const PDFContentResourceCategory category : PDFContentResources::getCategories())
            {
                for (const PDFContentResources::Entry& entry : container->getResources().getEntries(category))
                {
                    if (!entry.isOriginal)
                    {
                        objects->push_back(entry.object);
                    }
                }
            }
        }
        return;
    }

    if (m_impl->isTreeChanged)
    {
        Impl::addObjectsOfPatch(m_impl->root, objects);
    }

    for (const Impl::ResourceEntry& entry : m_impl->addedResources)
    {
        objects->push_back(entry.object);
    }
}

}   // namespace pdf
