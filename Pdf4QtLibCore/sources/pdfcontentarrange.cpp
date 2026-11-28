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

// Edit Content, stage E1, package P13: the geometry of the card Arrange and its command. The rules
// of the geometry are the comment of PDFContentArrange; the command does what Transform does
// (pdfcontentcommands.cpp), with one matrix per node.

#include "pdfcontentarrange.h"

#include "pdfcontentcommandengine.h"
#include "pdfcontentjson.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <set>
#include <utility>

namespace pdf
{

/// The geometry of PDFContentArrange: functions without a state over the normalized boxes. Every
/// loop runs over the boxes of the input (or over the pairs of the two columns of a form, which
/// are bounded by MAXIMUM_FORM_BOXES).
class PDFContentArrangeHelper
{
public:
    PDFContentArrangeHelper() = delete;

    using Operation = PDFContentArrange::Operation;

    /// An operation with its JSON type and name
    struct OperationInfo
    {
        Operation operation;
        const char* type;
        const char* name;
    };

    static constexpr std::array<OperationInfo, 16> OPERATIONS = { {
        { Operation::AlignLeft, PDFContentArrange::TYPE_ALIGN, "left" },
        { Operation::AlignHorizontalCenter, PDFContentArrange::TYPE_ALIGN, "horizontal-center" },
        { Operation::AlignRight, PDFContentArrange::TYPE_ALIGN, "right" },
        { Operation::AlignTop, PDFContentArrange::TYPE_ALIGN, "top" },
        { Operation::AlignVerticalCenter, PDFContentArrange::TYPE_ALIGN, "vertical-center" },
        { Operation::AlignBottom, PDFContentArrange::TYPE_ALIGN, "bottom" },
        { Operation::CenterHorizontallyOnPage, PDFContentArrange::TYPE_ALIGN, "page-horizontal-center" },
        { Operation::CenterVerticallyOnPage, PDFContentArrange::TYPE_ALIGN, "page-vertical-center" },
        { Operation::CenterOnPage, PDFContentArrange::TYPE_ALIGN, "page-center" },
        { Operation::DistributeInRow, PDFContentArrange::TYPE_DISTRIBUTE, "row" },
        { Operation::DistributeInColumn, PDFContentArrange::TYPE_DISTRIBUTE, "column" },
        { Operation::DistributeAsForm, PDFContentArrange::TYPE_DISTRIBUTE, "form" },
        { Operation::DistributeAsGrid, PDFContentArrange::TYPE_DISTRIBUTE, "grid" },
        { Operation::MatchWidth, PDFContentArrange::TYPE_MATCH_SIZE, "width" },
        { Operation::MatchHeight, PDFContentArrange::TYPE_MATCH_SIZE, "height" },
        { Operation::MatchSize, PDFContentArrange::TYPE_MATCH_SIZE, "size" }
    } };

    static const OperationInfo* getInfo(Operation operation)
    {
        for (const OperationInfo& info : OPERATIONS)
        {
            if (info.operation == operation)
            {
                return &info;
            }
        }
        return nullptr;
    }

    /// A value of a sort: the key and the index of its box in the input
    struct SortItem
    {
        qreal key = 0.0;
        size_t index = 0;
    };

    static bool isKeyLess(const SortItem& left, const SortItem& right) { return left.key < right.key; }
    static bool isKeyGreater(const SortItem& left, const SortItem& right) { return left.key > right.key; }

    /// A pair of a form: a box of the left column, a box of the right one and their overlap
    struct FormCandidate
    {
        qreal overlap = 0.0;
        size_t left = 0;
        size_t right = 0;
    };

    /// The largest overlap first, then the smaller index of the left box, then of the right one
    static bool isCandidateBefore(const FormCandidate& first, const FormCandidate& second)
    {
        if (first.overlap != second.overlap)
        {
            return first.overlap > second.overlap;
        }
        if (first.left != second.left)
        {
            return first.left < second.left;
        }
        return first.right < second.right;
    }

    static constexpr size_t NO_BOX = size_t(-1);

    /// A row of a form: its left box, its right box (NO_BOX for none) and the average of the
    /// centers in y of its boxes
    struct FormRow
    {
        size_t left = NO_BOX;
        size_t right = NO_BOX;
        qreal center = 0.0;
    };

    static bool isRowHigher(const FormRow& first, const FormRow& second) { return first.center > second.center; }

    static bool isFinite(const QRectF& box)
    {
        return std::isfinite(box.left()) && std::isfinite(box.right()) && std::isfinite(box.top()) && std::isfinite(box.bottom()) &&
               std::isfinite(box.width()) && std::isfinite(box.height());
    }

    /// A box takes part in an operation: finite and not null (a line, which has one dimension
    /// zero, takes part)
    static bool isParticipating(const QRectF& normalizedBox)
    {
        return isFinite(normalizedBox) && (normalizedBox.width() > 0.0 || normalizedBox.height() > 0.0);
    }

    /// The union of the boxes, which take part (the edges are the extremes of theirs)
    static QRectF getUnion(const std::vector<QRectF>& boxes, const std::vector<size_t>& participants)
    {
        if (participants.empty())
        {
            return QRectF();
        }

        qreal left = boxes[participants.front()].left();
        qreal right = boxes[participants.front()].right();
        qreal lower = boxes[participants.front()].top();
        qreal upper = boxes[participants.front()].bottom();
        for (const size_t index : participants)
        {
            const QRectF& box = boxes[index];
            left = qMin(left, box.left());
            right = qMax(right, box.right());
            lower = qMin(lower, box.top());
            upper = qMax(upper, box.bottom());
        }
        return QRectF(QPointF(left, lower), QPointF(right, upper));
    }

    /// The boxes, which take part, sorted (stable) by their left edges (isHorizontal) or by their
    /// lower edges (QRectF::top) - the keys of the old code (LayoutHorizontally, LayoutVertically)
    static std::vector<SortItem> sortBy(const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, bool isHorizontal, bool isAscending)
    {
        std::vector<SortItem> result;
        result.reserve(participants.size());
        for (const size_t index : participants)
        {
            const QRectF& box = boxes[index];
            result.push_back(SortItem{ isHorizontal ? box.left() : box.top(), index });
        }
        std::stable_sort(result.begin(), result.end(), isAscending ? &isKeyLess : &isKeyGreater);
        return result;
    }

    static void computeAlign(Operation operation, const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, size_t keyIndex, std::vector<QTransform>* matrices)
    {
        const QRectF& key = boxes[keyIndex];
        for (const size_t index : participants)
        {
            if (index == keyIndex)
            {
                continue;
            }

            const QRectF& box = boxes[index];
            qreal dx = 0.0;
            qreal dy = 0.0;
            switch (operation)
            {
                case Operation::AlignLeft:
                    dx = key.left() - box.left();
                    break;
                case Operation::AlignHorizontalCenter:
                    dx = key.center().x() - box.center().x();
                    break;
                case Operation::AlignRight:
                    dx = key.right() - box.right();
                    break;
                case Operation::AlignTop:
                    dy = key.bottom() - box.bottom();
                    break;
                case Operation::AlignVerticalCenter:
                    dy = key.center().y() - box.center().y();
                    break;
                case Operation::AlignBottom:
                    dy = key.top() - box.top();
                    break;
                default:
                    Q_ASSERT(false);
                    break;
            }
            (*matrices)[index] = QTransform::fromTranslate(dx, dy);
        }
    }

    static void computeCenter(Operation operation, const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, const QRectF& pageBox, std::vector<QTransform>* matrices)
    {
        const QRectF unionBox = getUnion(boxes, participants);
        const bool isHorizontal = operation != Operation::CenterVerticallyOnPage;
        const bool isVertical = operation != Operation::CenterHorizontallyOnPage;
        const qreal dx = isHorizontal ? pageBox.center().x() - unionBox.center().x() : 0.0;
        const qreal dy = isVertical ? pageBox.center().y() - unionBox.center().y() : 0.0;

        for (const size_t index : participants)
        {
            (*matrices)[index] = QTransform::fromTranslate(dx, dy);
        }
    }

    static void computeRow(const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, std::vector<QTransform>* matrices)
    {
        qreal x = getUnion(boxes, participants).left();
        for (const SortItem& item : sortBy(boxes, participants, true, true))
        {
            const QRectF& box = boxes[item.index];
            (*matrices)[item.index] = QTransform::fromTranslate(x - box.left(), 0.0);
            x += box.width();
        }
    }

    static void computeColumn(const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, std::vector<QTransform>* matrices)
    {
        qreal y = getUnion(boxes, participants).bottom();
        for (const SortItem& item : sortBy(boxes, participants, false, false))
        {
            const QRectF& box = boxes[item.index];
            (*matrices)[item.index] = QTransform::fromTranslate(0.0, y - box.bottom());
            y -= box.height();
        }
    }

    static void computeForm(const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, std::vector<QTransform>* matrices)
    {
        const QRectF unionBox = getUnion(boxes, participants);

        // The columns: a box nearer to the left edge of the union is a label
        std::vector<size_t> leftColumn;
        std::vector<size_t> rightColumn;
        qreal rightWidth = 0.0;
        for (const size_t index : participants)
        {
            const QRectF& box = boxes[index];
            if (box.left() - unionBox.left() < unionBox.right() - box.right())
            {
                leftColumn.push_back(index);
            }
            else
            {
                rightColumn.push_back(index);
                rightWidth = qMax(rightWidth, box.width());
            }
        }

        // The pairs: every pair with an overlap larger than zero, taken greedily from the largest
        // overlap (at most MAXIMUM_FORM_BOXES^2 / 4 candidates)
        std::vector<FormCandidate> candidates;
        for (const size_t left : leftColumn)
        {
            const QRectF& leftBox = boxes[left];
            for (const size_t right : rightColumn)
            {
                const QRectF& rightBox = boxes[right];
                const qreal overlap = qMin(leftBox.bottom(), rightBox.bottom()) - qMax(leftBox.top(), rightBox.top());
                if (overlap > 0.0)
                {
                    candidates.push_back(FormCandidate{ overlap, left, right });
                }
            }
        }
        std::sort(candidates.begin(), candidates.end(), &isCandidateBefore);

        std::vector<bool> isPaired(boxes.size(), false);
        std::vector<FormRow> rows;
        for (const FormCandidate& candidate : candidates)
        {
            if (!isPaired[candidate.left] && !isPaired[candidate.right])
            {
                isPaired[candidate.left] = true;
                isPaired[candidate.right] = true;
                rows.push_back(FormRow{ candidate.left, candidate.right, (boxes[candidate.left].center().y() + boxes[candidate.right].center().y()) * 0.5 });
            }
        }

        for (const size_t left : leftColumn)
        {
            if (!isPaired[left])
            {
                rows.push_back(FormRow{ left, NO_BOX, boxes[left].center().y() });
            }
        }
        for (const size_t right : rightColumn)
        {
            if (!isPaired[right])
            {
                rows.push_back(FormRow{ NO_BOX, right, boxes[right].center().y() });
            }
        }

        std::stable_sort(rows.begin(), rows.end(), &isRowHigher);

        // The rows from the upper edge of the union down
        const qreal leftX = unionBox.left();
        const qreal rightX = unionBox.right() - rightWidth;
        qreal y = unionBox.bottom();
        for (const FormRow& row : rows)
        {
            qreal height = 0.0;
            for (const std::pair<size_t, qreal>& item : { std::make_pair(row.left, leftX), std::make_pair(row.right, rightX) })
            {
                if (item.first == NO_BOX)
                {
                    continue;
                }

                const QRectF& box = boxes[item.first];
                (*matrices)[item.first] = QTransform::fromTranslate(item.second - box.left(), y - box.bottom());
                height = qMax(height, box.height());
            }
            y -= height;
        }
    }

    /// The overlap, which the old code took for none (qFuzzyIsNull of its
    /// PDFPageContentElementManipulator::performOperation, LayoutGrid)
    static constexpr qreal GRID_OVERLAP_TOLERANCE = 1e-12;

    /// A remaining box of a grid line search: its key in the set of the members and its index
    using GridKey = std::pair<qreal, size_t>;

    /// The rows (isHorizontal false) or the columns (true) of a grid by the old rules of
    /// PDFPageContentElementManipulator (LayoutGrid): the index of the line of every box, which
    /// takes part, and the sizes of the lines (the first line is the top row or the left column).
    ///  - Rows: the boxes sorted by their lower edges ascending (stable); the last remaining box (the
    ///    highest lower edge) starts a row and takes every remaining box, which overlaps it in y by
    ///    more than the tolerance. As the starter has the highest lower edge, the overlap of a box is
    ///    min(its upper edge, the upper edge of the starter) - the lower edge of the starter: the
    ///    members are the boxes, whose upper edge lies above the lower edge of the starter by more
    ///    than the tolerance (the starter itself not a line).
    ///  - Columns: the boxes sorted by their left edges descending (stable); the last remaining box
    ///    (the smallest left edge) starts a column and takes every remaining box, which overlaps it
    ///    in x by more than the tolerance: min(its right edge, the right edge of the starter) - its
    ///    left edge, so the members are the boxes of a width above the tolerance, whose left edge
    ///    lies left of the right edge of the starter by more than the tolerance.
    /// O(n log n): the members are taken from an ordered set of the remaining boxes from its end
    /// (rows: by the upper edges, the highest first) or from its beginning (columns: by the left
    /// edges, of the boxes of a width), each box once.
    static std::vector<qreal> findGridLines(const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, bool isHorizontal, std::vector<size_t>* lineOfBox)
    {
        std::vector<SortItem> sorted;
        sorted.reserve(participants.size());
        for (const size_t index : participants)
        {
            sorted.push_back(SortItem{ isHorizontal ? boxes[index].left() : boxes[index].top(), index });
        }
        std::stable_sort(sorted.begin(), sorted.end(), isHorizontal ? &isKeyGreater : &isKeyLess);

        // The candidates of the members: rows by the upper edge, columns by the left edge (only
        // the boxes of a width: a box without one overlaps nothing in x)
        std::set<GridKey> candidates;
        for (const size_t index : participants)
        {
            const QRectF& box = boxes[index];
            if (!isHorizontal)
            {
                candidates.insert(GridKey(box.bottom(), index));
            }
            else if (box.width() > GRID_OVERLAP_TOLERANCE)
            {
                candidates.insert(GridKey(box.left(), index));
            }
        }

        std::vector<bool> isPlaced(boxes.size(), false);
        std::vector<qreal> sizes;
        size_t end = sorted.size();
        while (true)
        {
            while (end > 0 && isPlaced[sorted[end - 1].index])
            {
                --end;
            }
            if (end == 0)
            {
                break;
            }

            const size_t starter = sorted[end - 1].index;
            const QRectF& starterBox = boxes[starter];
            const size_t line = sizes.size();
            qreal size = isHorizontal ? starterBox.width() : starterBox.height();
            isPlaced[starter] = true;
            (*lineOfBox)[starter] = line;
            candidates.erase(GridKey(isHorizontal ? starterBox.left() : starterBox.bottom(), starter));

            if (!isHorizontal && starterBox.height() > GRID_OVERLAP_TOLERANCE)
            {
                // The boxes, whose upper edge is above the lower edge of the starter
                while (!candidates.empty() && std::prev(candidates.end())->first - starterBox.top() > GRID_OVERLAP_TOLERANCE)
                {
                    const size_t index = std::prev(candidates.end())->second;
                    candidates.erase(std::prev(candidates.end()));
                    isPlaced[index] = true;
                    (*lineOfBox)[index] = line;
                    size = qMax(size, boxes[index].height());
                }
            }
            else if (isHorizontal)
            {
                // The boxes of a width, whose left edge is left of the right edge of the starter
                while (!candidates.empty() && starterBox.right() - candidates.begin()->first > GRID_OVERLAP_TOLERANCE)
                {
                    const size_t index = candidates.begin()->second;
                    candidates.erase(candidates.begin());
                    isPlaced[index] = true;
                    (*lineOfBox)[index] = line;
                    size = qMax(size, boxes[index].width());
                }
            }

            sizes.push_back(size);
        }

        return sizes;
    }

    static void computeGrid(const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, std::vector<QTransform>* matrices)
    {
        const QRectF unionBox = getUnion(boxes, participants);

        std::vector<size_t> rowOfBox(boxes.size(), 0);
        std::vector<size_t> columnOfBox(boxes.size(), 0);
        const std::vector<qreal> rowHeights = findGridLines(boxes, participants, false, &rowOfBox);
        const std::vector<qreal> columnWidths = findGridLines(boxes, participants, true, &columnOfBox);

        // The offsets of the cells: the sums of the sizes of the lines before
        std::vector<qreal> rowOffsets(rowHeights.size(), 0.0);
        for (size_t i = 1; i < rowHeights.size(); ++i)
        {
            rowOffsets[i] = rowOffsets[i - 1] + rowHeights[i - 1];
        }
        std::vector<qreal> columnOffsets(columnWidths.size(), 0.0);
        for (size_t i = 1; i < columnWidths.size(); ++i)
        {
            columnOffsets[i] = columnOffsets[i - 1] + columnWidths[i - 1];
        }

        for (const size_t index : participants)
        {
            const QRectF& box = boxes[index];
            const qreal left = unionBox.left() + columnOffsets[columnOfBox[index]];
            const qreal upper = unionBox.bottom() - rowOffsets[rowOfBox[index]];
            (*matrices)[index] = QTransform::fromTranslate(left - box.left(), upper - box.bottom());
        }
    }

    static void computeMatchSize(Operation operation, const std::vector<QRectF>& boxes, const std::vector<size_t>& participants, size_t keyIndex, std::vector<QTransform>* matrices)
    {
        const QRectF& key = boxes[keyIndex];
        const bool isWidth = operation != Operation::MatchHeight;
        const bool isHeight = operation != Operation::MatchWidth;

        for (const size_t index : participants)
        {
            if (index == keyIndex)
            {
                continue;
            }

            // A zero dimension is not scaled (the matrix would be singular)
            const QRectF& box = boxes[index];
            const qreal sx = (isWidth && key.width() > 0.0 && box.width() > 0.0) ? key.width() / box.width() : 1.0;
            const qreal sy = (isHeight && key.height() > 0.0 && box.height() > 0.0) ? key.height() / box.height() : 1.0;

            // The upper left corner stays: x' = sx (x - ax) + ax, y' = sy (y - ay) + ay
            const qreal ax = box.left();
            const qreal ay = box.bottom();
            (*matrices)[index] = QTransform(sx, 0.0, 0.0, sy, ax - sx * ax, ay - sy * ay);
        }
    }

    static void setError(QString* errorText, const QString& text)
    {
        if (errorText)
        {
            *errorText = text;
        }
    }
};

/// Transforms each node by its own matrix of the default space of its container, as Transform
/// does for one matrix (R-1, R-5, R-6): one undo step for the whole operation of Arrange
class PDFContentArrangeCommand : public PDFContentModelCommand
{
public:
    using Operation = PDFContentArrange::Operation;

    explicit PDFContentArrangeCommand(Operation operation, std::vector<PDFContentNodeReference> nodes, std::vector<QTransform> matrices) :
        m_operation(operation),
        m_nodes(std::move(nodes)),
        m_matrices(std::move(matrices))
    {
        if (!m_nodes.empty())
        {
            setPlace(nullptr, m_nodes.front().container);
        }
    }

    virtual QString getText() const override
    {
        const QByteArray type = getType();
        const int count = int(m_nodes.size());
        if (type == PDFContentArrange::TYPE_DISTRIBUTE)
        {
            return PDFContentArrange::tr("Distribute %Ln Object(s)", nullptr, count);
        }
        if (type == PDFContentArrange::TYPE_MATCH_SIZE)
        {
            return PDFContentArrange::tr("Match Size of %Ln Object(s)", nullptr, count);
        }
        return PDFContentArrange::tr("Align %Ln Object(s)", nullptr, count);
    }

    virtual QByteArray getType() const override { return PDFContentArrange::getType(m_operation); }

    virtual QJsonObject toJson() const override
    {
        QJsonArray matrices;
        for (const QTransform& matrix : m_matrices)
        {
            matrices.append(PDFContentJson::writeMatrix(matrix));
        }

        QJsonObject object;
        object.insert(QLatin1String(PDFContentJson::KEY_TYPE), QString::fromLatin1(getType()));
        object.insert(QLatin1String(PDFContentArrange::KEY_OPERATION), QString::fromLatin1(PDFContentArrange::getOperationName(m_operation)));
        object.insert(QLatin1String(PDFContentCommandJson::KEY_IDS), writeIds(m_nodes));
        object.insert(QLatin1String(PDFContentArrange::KEY_MATRICES), matrices);
        return object;
    }

    static PDFContentCommandPointer fromJsonAlign(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        return fromJson(PDFContentArrange::TYPE_ALIGN, object, session, path, errors);
    }

    static PDFContentCommandPointer fromJsonDistribute(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        return fromJson(PDFContentArrange::TYPE_DISTRIBUTE, object, session, path, errors);
    }

    static PDFContentCommandPointer fromJsonMatchSize(const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        return fromJson(PDFContentArrange::TYPE_MATCH_SIZE, object, session, path, errors);
    }

protected:
    virtual bool compute(Work& work, QString* errorText) override
    {
        if (!checkNodes(m_nodes, PDFContentArrange::tr("There is no object to arrange."), errorText))
        {
            return false;
        }

        if (m_matrices.size() != m_nodes.size())
        {
            *errorText = PDFContentArrange::tr("The command needs one matrix for every object.");
            return false;
        }

        // A singular or infinite matrix is an error of the command (zadani_overeni.md 6.2)
        for (const QTransform& matrix : m_matrices)
        {
            if (!Helper::isRegularMatrix(matrix))
            {
                *errorText = PDFContentArrange::tr("A transformation is not a regular matrix of finite numbers.");
                return false;
            }
        }

        // As Transform: all the objects are checked in the tree as it is before the command,
        // then they move; an object below another named one moves with it only
        const std::set<PDFContentNodeReference> named(m_nodes.cbegin(), m_nodes.cend());
        std::set<PDFContentContainerKey> placed;
        std::vector<std::pair<Work::Container*, size_t>> moved;

        for (size_t i = 0; i < m_nodes.size(); ++i)
        {
            const PDFContentNodeReference& reference = m_nodes[i];
            Work::Container* container = nullptr;
            const PDFContentNodePointer node = work.getNode(reference, &container, errorText);
            if (!node)
            {
                return false;
            }

            if (placed.insert(reference.container).second)
            {
                setPlace(work.getSession(), reference.container);
            }

            bool isCovered = false;
            for (const PDFContentNodePointer& ancestor : Work::getSourceAncestors(container, reference.node))
            {
                if (ancestor->getKind() == Kind::TextObject)
                {
                    *errorText = PDFTranslationContext::tr("A part of a text cannot be moved yet.");
                    return false;
                }

                isCovered = isCovered || named.count(PDFContentNodeReference{ reference.container, ancestor->getId() }) > 0;
            }

            if (container->source->isUnclosed(reference.node))
            {
                *errorText = PDFTranslationContext::tr("The object is not closed in the content, so it cannot be moved.");
                return false;
            }

            if (!isCovered && !m_matrices[i].isIdentity())
            {
                moved.emplace_back(container, i);
            }
        }

        size_t count = 0;
        for (const auto& item : moved)
        {
            if (!work.transformNode(item.first, m_nodes[item.second].node, m_matrices[item.second], true, 0, &count, errorText))
            {
                return false;
            }
        }

        if (count == 0)
        {
            // Every object is where the operation puts it already, or holds nothing, which moves
            *errorText = PDFContentArrange::tr("The objects are arranged so already.");
            setRefusedAsUnchanged(true);
            return false;
        }

        return true;
    }

private:
    static PDFContentCommandPointer fromJson(const char* type, const QJsonObject& object, const PDFContentEditSession* session, const QString& path, std::vector<QString>* errors)
    {
        using Json = PDFContentCommandJson;

        std::vector<PDFContentNodeReference> nodes;
        bool result = Json::checkKeys(object, { Json::KEY_IDS, PDFContentArrange::KEY_OPERATION, PDFContentArrange::KEY_MATRICES }, path, errors);
        const bool isNodesRead = Json::readNodes(object, session, path, errors, &nodes);
        result = isNodesRead && result;

        std::optional<Operation> operation;
        const QJsonValue operationValue = object.value(QLatin1String(PDFContentArrange::KEY_OPERATION));
        if (operationValue.isString())
        {
            operation = PDFContentArrange::getOperation(QByteArray(type), operationValue.toString().toUtf8());
        }
        if (!operation)
        {
            Json::addError(errors, path, PDFContentArrange::KEY_OPERATION, PDFContentArrange::tr("An operation of the type '%1' is expected.").arg(QString::fromLatin1(type)));
            result = false;
        }

        std::vector<QTransform> matrices;
        const QJsonValue matricesValue = object.value(QLatin1String(PDFContentArrange::KEY_MATRICES));
        const QJsonArray array = matricesValue.toArray();
        if (!matricesValue.isArray())
        {
            Json::addError(errors, path, PDFContentArrange::KEY_MATRICES, PDFContentArrange::tr("A list of matrices is expected."));
            result = false;
        }
        else if (size_t(array.size()) > PDFContentCommand::MAXIMUM_NODES_OF_COMMAND)
        {
            Json::addError(errors, path, PDFContentArrange::KEY_MATRICES, PDFContentArrange::tr("The operation has more than %Ln matrix(es).", nullptr, int(PDFContentCommand::MAXIMUM_NODES_OF_COMMAND)));
            result = false;
        }
        else
        {
            bool isMatricesRead = true;
            for (qsizetype i = 0; i < array.size(); ++i)
            {
                const std::optional<QTransform> matrix = PDFContentJson::readMatrix(array[i]);
                if (!matrix || !PDFContentCommandHelper::isRegularMatrix(*matrix))
                {
                    const QByteArray key = QByteArray(PDFContentArrange::KEY_MATRICES) + '[' + QByteArray::number(qlonglong(i)) + ']';
                    Json::addError(errors, path, key.constData(), PDFContentArrange::tr("A regular matrix of six finite numbers is expected."));
                    isMatricesRead = false;
                    continue;
                }
                matrices.push_back(*matrix);
            }

            if (isMatricesRead && isNodesRead && matrices.size() != nodes.size())
            {
                Json::addError(errors, path, PDFContentArrange::KEY_MATRICES, PDFContentArrange::tr("One matrix for every object is expected."));
                isMatricesRead = false;
            }
            result = isMatricesRead && result;
        }

        return result ? PDFContentArrange::createCommand(*operation, std::move(nodes), std::move(matrices)) : nullptr;
    }

    static bool registerTypes()
    {
        bool result = PDFContentCommandFactory::registerType(PDFContentArrange::TYPE_ALIGN, &PDFContentArrangeCommand::fromJsonAlign);
        result = PDFContentCommandFactory::registerType(PDFContentArrange::TYPE_DISTRIBUTE, &PDFContentArrangeCommand::fromJsonDistribute) && result;
        result = PDFContentCommandFactory::registerType(PDFContentArrange::TYPE_MATCH_SIZE, &PDFContentArrangeCommand::fromJsonMatchSize) && result;
        return result;
    }

    static const bool s_isRegistered;

    Operation m_operation;
    std::vector<PDFContentNodeReference> m_nodes;
    std::vector<QTransform> m_matrices;     ///< One per node, in the default space of its container
};

const bool PDFContentArrangeCommand::s_isRegistered = PDFContentArrangeCommand::registerTypes();

QByteArray PDFContentArrange::getType(Operation operation)
{
    const PDFContentArrangeHelper::OperationInfo* info = PDFContentArrangeHelper::getInfo(operation);
    return info ? QByteArray(info->type) : QByteArray();
}

QByteArray PDFContentArrange::getOperationName(Operation operation)
{
    const PDFContentArrangeHelper::OperationInfo* info = PDFContentArrangeHelper::getInfo(operation);
    return info ? QByteArray(info->name) : QByteArray();
}

std::optional<PDFContentArrange::Operation> PDFContentArrange::getOperation(const QByteArray& type, const QByteArray& name)
{
    for (const PDFContentArrangeHelper::OperationInfo& info : PDFContentArrangeHelper::OPERATIONS)
    {
        if (type == info.type && name == info.name)
        {
            return info.operation;
        }
    }
    return std::nullopt;
}

QTransform PDFContentArrange::getDisplayedPageMatrix(PageRotation rotation)
{
    // QTransform(m11, m12, m21, m22, dx, dy) maps (x, y) to (m11 x + m21 y, m12 x + m22 y)
    switch (rotation)
    {
        case PageRotation::Rotate90:
            // Clockwise by 90 degrees: the x axis of the page points down, its y axis right
            return QTransform(0.0, -1.0, 1.0, 0.0, 0.0, 0.0);

        case PageRotation::Rotate180:
            return QTransform(-1.0, 0.0, 0.0, -1.0, 0.0, 0.0);

        case PageRotation::Rotate270:
            // Clockwise by 270 degrees: the x axis of the page points up, its y axis left
            return QTransform(0.0, 1.0, -1.0, 0.0, 0.0, 0.0);

        case PageRotation::None:
            break;
    }

    return QTransform();
}

size_t PDFContentArrange::getMinimumBoxCount(Operation operation)
{
    switch (operation)
    {
        case Operation::CenterHorizontallyOnPage:
        case Operation::CenterVerticallyOnPage:
        case Operation::CenterOnPage:
            return 1;

        default:
            return 2;
    }
}

std::optional<std::vector<QTransform>> PDFContentArrange::computeMatrices(Operation operation,
                                                                          const std::vector<QRectF>& boxes,
                                                                          size_t keyIndex,
                                                                          const QRectF& pageBox,
                                                                          QString* errorText)
{
    using Helper = PDFContentArrangeHelper;

    if (!Helper::getInfo(operation))
    {
        Helper::setError(errorText, tr("The operation is not known."));
        return std::nullopt;
    }

    // The pairing of a form is quadratic: the bound comes before any work
    if (operation == Operation::DistributeAsForm && boxes.size() > MAXIMUM_FORM_BOXES)
    {
        Helper::setError(errorText, tr("A form can be made of at most %Ln object(s).", nullptr, int(MAXIMUM_FORM_BOXES)));
        return std::nullopt;
    }

    std::vector<QRectF> normalized;
    std::vector<size_t> participants;
    std::vector<bool> isParticipating(boxes.size(), false);
    normalized.reserve(boxes.size());
    for (size_t i = 0; i < boxes.size(); ++i)
    {
        normalized.push_back(boxes[i].normalized());
        if (Helper::isParticipating(normalized.back()))
        {
            participants.push_back(i);
            isParticipating[i] = true;
        }
    }

    const size_t minimumCount = getMinimumBoxCount(operation);
    if (participants.size() < minimumCount)
    {
        Helper::setError(errorText, tr("The operation needs at least %Ln object(s) with a size.", nullptr, int(minimumCount)));
        return std::nullopt;
    }

    std::vector<QTransform> matrices(boxes.size(), QTransform());
    switch (operation)
    {
        case Operation::AlignLeft:
        case Operation::AlignHorizontalCenter:
        case Operation::AlignRight:
        case Operation::AlignTop:
        case Operation::AlignVerticalCenter:
        case Operation::AlignBottom:
        case Operation::MatchWidth:
        case Operation::MatchHeight:
        case Operation::MatchSize:
        {
            if (keyIndex >= boxes.size() || !isParticipating[keyIndex])
            {
                Helper::setError(errorText, tr("The key object is not one of the objects, or it has no size."));
                return std::nullopt;
            }

            const bool isAlign = operation != Operation::MatchWidth && operation != Operation::MatchHeight && operation != Operation::MatchSize;
            if (isAlign)
            {
                Helper::computeAlign(operation, normalized, participants, keyIndex, &matrices);
            }
            else
            {
                Helper::computeMatchSize(operation, normalized, participants, keyIndex, &matrices);
            }
            break;
        }

        case Operation::CenterHorizontallyOnPage:
        case Operation::CenterVerticallyOnPage:
        case Operation::CenterOnPage:
        {
            const QRectF page = pageBox.normalized();
            if (!Helper::isFinite(page) || page.isEmpty())
            {
                Helper::setError(errorText, tr("The page has no area, so the objects cannot be centered on it."));
                return std::nullopt;
            }
            Helper::computeCenter(operation, normalized, participants, page, &matrices);
            break;
        }

        case Operation::DistributeInRow:
            Helper::computeRow(normalized, participants, &matrices);
            break;

        case Operation::DistributeInColumn:
            Helper::computeColumn(normalized, participants, &matrices);
            break;

        case Operation::DistributeAsForm:
            Helper::computeForm(normalized, participants, &matrices);
            break;

        case Operation::DistributeAsGrid:
            Helper::computeGrid(normalized, participants, &matrices);
            break;
    }

    // Every number of the result is finite, and every matrix can be undone
    for (const size_t index : participants)
    {
        const QTransform& matrix = matrices[index];
        if (!PDFContentCommandHelper::isRegularMatrix(matrix) || !Helper::isFinite(matrix.mapRect(normalized[index])))
        {
            Helper::setError(errorText, tr("The objects are too large or too far apart to be arranged."));
            return std::nullopt;
        }
    }

    return matrices;
}

PDFContentCommandPointer PDFContentArrange::createCommand(Operation operation,
                                                          std::vector<PDFContentNodeReference> nodes,
                                                          const std::vector<QRectF>& boxes,
                                                          size_t keyIndex,
                                                          const QRectF& pageBox,
                                                          const QTransform& containerToPage,
                                                          QString* errorText)
{
    using Helper = PDFContentArrangeHelper;

    if (nodes.size() != boxes.size())
    {
        Helper::setError(errorText, tr("The objects and their boxes do not match."));
        return nullptr;
    }

    QTransform pageToContainer;
    if (!PDFContentCommandHelper::isRegularMatrix(containerToPage) || !PDFContentCommandHelper::invertMatrix(containerToPage, &pageToContainer))
    {
        Helper::setError(errorText, tr("The space of the objects is not a regular matrix of finite numbers."));
        return nullptr;
    }

    std::optional<std::vector<QTransform>> matrices = computeMatrices(operation, boxes, keyIndex, pageBox, errorText);
    if (!matrices)
    {
        return nullptr;
    }

    // The matrix T of the page becomes C x T x C^-1 in the container (row vectors: a point of the
    // container goes to the page, moves there and comes back)
    for (QTransform& matrix : *matrices)
    {
        if (matrix.isIdentity())
        {
            continue;
        }

        matrix = containerToPage * matrix * pageToContainer;
        if (!PDFContentCommandHelper::isRegularMatrix(matrix))
        {
            Helper::setError(errorText, tr("The objects are too large or too far apart to be arranged."));
            return nullptr;
        }
    }

    return createCommand(operation, std::move(nodes), std::move(*matrices));
}

PDFContentCommandPointer PDFContentArrange::createCommand(Operation operation,
                                                          std::vector<PDFContentNodeReference> nodes,
                                                          std::vector<QTransform> matrices)
{
    return std::make_shared<PDFContentArrangeCommand>(operation, std::move(nodes), std::move(matrices));
}

}   // namespace pdf
