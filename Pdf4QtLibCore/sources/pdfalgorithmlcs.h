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

#ifndef PDFALGORITHMLCS_H
#define PDFALGORITHMLCS_H

#include "pdfglobal.h"
#include "pdfoperationcontrol.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <vector>

namespace pdf
{

class PDFAlgorithmLongestCommonSubsequenceBase
{
public:

    /// Most cells of the matrix of one computation (plan of step 7 of phase 2, B191, R7-6): the
    /// product of the lengths of the two sequences without their common beginning and end. 64 Mi
    /// cells are 8 MiB of the bits of the backtracking; a longer computation is never allocated.
    static constexpr size_t MAXIMUM_CELL_COUNT = size_t(64) * 1024 * 1024;

    /// The product of two lengths fits into MAXIMUM_CELL_COUNT (no overflow)
    /// \param size1 Length of the first sequence
    /// \param size2 Length of the second sequence
    static constexpr bool isWithinBudget(size_t size1, size_t size2)
    {
        return size1 == 0 || size2 <= MAXIMUM_CELL_COUNT / size1;
    }

    enum SequenceItemFlag
    {
        None            = 0x0000,
        MovedLeft       = 0x0001,   ///< Item has been moved from this position (is present in a sequence no. 1)
        MovedRight      = 0x0002,   ///< Item has been moved to this position (is present in a sequence no. 2)
        Moved           = 0x0004,   ///< Index of item has been changed
        Added           = 0x0008,   ///< Item has been added to a sequence no. 2
        Removed         = 0x0010,   ///< Item has been removed from a sequence no. 1
        Replaced        = 0x0020,   ///< Item has been replaced (or sequence of items has been replaced)
    };
    Q_DECLARE_FLAGS(SequenceItemFlags, SequenceItemFlag)

    struct SequenceItem
    {
        size_t index1 = std::numeric_limits<size_t>::max();
        size_t index2 = std::numeric_limits<size_t>::max();
        SequenceItemFlags flags = None;

        bool isLeftValid() const { return index1 != std::numeric_limits<size_t>::max(); }
        bool isRightValid() const { return index2 != std::numeric_limits<size_t>::max(); }
        bool isLeft() const { return isLeftValid() && !isRightValid(); }
        bool isRight() const { return isRightValid() && !isLeftValid(); }
        bool isMatch() const { return isLeftValid() && isRightValid(); }
        bool isMovedLeft() const { return flags.testFlag(MovedLeft); }
        bool isMovedRight() const { return flags.testFlag(MovedRight); }
        bool isMoved() const { return flags.testFlag(Moved); }
        bool isAdded() const { return flags.testFlag(Added); }
        bool isRemoved() const { return flags.testFlag(Removed); }
        bool isReplaced() const { return flags.testFlag(Replaced); }
        bool isModified() const { return isAdded() || isRemoved() || isReplaced(); }

        void markMovedLeft() { flags.setFlag(MovedLeft); }
        void markMovedRight() { flags.setFlag(MovedRight); }
        void markMoved() { flags.setFlag(Moved); }
        void markAdded() { flags.setFlag(Added); }
        void markRemoved() { flags.setFlag(Removed); }
        void markReplaced() { flags.setFlag(Replaced); }
    };

    using Sequence = typename std::vector<SequenceItem>;
    using SequenceIterator = typename Sequence::iterator;
    using SequenceItemRange = typename std::pair<SequenceIterator, SequenceIterator>;
    using SequenceItemRanges = typename std::vector<SequenceItemRange>;

    /// Marks a sequence with set of flags representing added/removed/replaced/moved
    /// items. Moved items sequences must be sorted.
    /// \param sequence Sequence to be marked
    /// \param movedItemsLeft Sorted sequence of left indices, which have been moved
    /// \param movedItemsRight sorted sequence of right indices, which have been moved
    static void markSequence(Sequence& sequence,
                             const std::vector<size_t>& movedItemsLeft,
                             const std::vector<size_t>& movedItemsRight);

    /// Returns item ranges, which should be checked - for example,
    /// for text modification.
    /// \param sequence Sequence
    static SequenceItemRanges getModifiedRanges(Sequence& sequence);

    /// Collect flags from given item range
    /// \param range Range
    static SequenceItemFlags collectFlags(const SequenceItemRange& range);
};

/// Algorithm for computing longest common subsequence, on two sequences
/// of objects, which are implementing operator "==" (equal operator).
/// Constructor takes bidirectional iterators to the sequence. So, iterators
/// are requred to be bidirectional.
///
/// The common beginning and end of the two sequences are matched first (an optimal solution
/// always matches them), the matrix is computed only over the rest - within the budget
/// MAXIMUM_CELL_COUNT (B191, R7-6): a computation over it is refused before anything is
/// allocated, the caller decides what to do instead. The cancellation is checked per row.
template<typename Iterator, typename Comparator>
class PDFAlgorithmLongestCommonSubsequence : public PDFAlgorithmLongestCommonSubsequenceBase
{
public:
    PDFAlgorithmLongestCommonSubsequence(Iterator it1,
                                         Iterator it1End,
                                         Iterator it2,
                                         Iterator it2End,
                                         Comparator comparator);

    /// Computes the sequence. Returns false (the sequence is empty), when the rest of the
    /// sequences without their common beginning and end is over the budget (isOverBudget) or
    /// when the operation was canceled.
    /// \param control Cancellation (optional), checked per row of the matrix
    bool perform(const PDFOperationControl* control = nullptr);

    /// The last perform was refused by the budget of the cells
    bool isOverBudget() const { return m_isOverBudget; }

    const Sequence& getSequence() const { return m_sequence; }

private:
    Iterator m_it1;
    Iterator m_it1End;
    Iterator m_it2;
    Iterator m_it2End;

    size_t m_size1;
    size_t m_size2;
    bool m_isOverBudget = false;

    Comparator m_comparator;

    std::vector<bool> m_backtrackData;
    Sequence m_sequence;
};

template<typename Iterator, typename Comparator>
PDFAlgorithmLongestCommonSubsequence<Iterator, Comparator>::PDFAlgorithmLongestCommonSubsequence(Iterator it1,
                                                                                                 Iterator it1End,
                                                                                                 Iterator it2,
                                                                                                 Iterator it2End,
                                                                                                 Comparator comparator) :
    m_it1(std::move(it1)),
    m_it1End(std::move(it1End)),
    m_it2(std::move(it2)),
    m_it2End(std::move(it2End)),
    m_size1(0),
    m_size2(0),
    m_comparator(std::move(comparator))
{
    m_size1 = std::distance(m_it1, m_it1End) + 1;
    m_size2 = std::distance(m_it2, m_it2End) + 1;
}

template<typename Iterator, typename Comparator>
bool PDFAlgorithmLongestCommonSubsequence<Iterator, Comparator>::perform(const PDFOperationControl* control)
{
    m_sequence.clear();
    m_backtrackData.clear();
    m_isOverBudget = false;

    const size_t length1 = m_size1 - 1;
    const size_t length2 = m_size2 - 1;

    // The common beginning and the common end are matched (an optimal solution matches them),
    // the end never overlaps the beginning
    size_t prefix = 0;
    Iterator middleBegin1 = m_it1;
    Iterator middleBegin2 = m_it2;
    while (prefix < length1 && prefix < length2 && m_comparator(*middleBegin1, *middleBegin2))
    {
        ++prefix;
        ++middleBegin1;
        ++middleBegin2;
    }

    size_t suffix = 0;
    Iterator middleEnd1 = m_it1End;
    Iterator middleEnd2 = m_it2End;
    while (prefix + suffix < length1 && prefix + suffix < length2)
    {
        Iterator previous1 = std::prev(middleEnd1);
        Iterator previous2 = std::prev(middleEnd2);
        if (!m_comparator(*previous1, *previous2))
        {
            break;
        }

        middleEnd1 = previous1;
        middleEnd2 = previous2;
        ++suffix;
    }

    // B191, R7-6: the budget is checked before anything is allocated
    const size_t middleLength1 = length1 - prefix - suffix;
    const size_t middleLength2 = length2 - prefix - suffix;
    if (!isWithinBudget(middleLength1, middleLength2))
    {
        m_isOverBudget = true;
        return false;
    }

    const size_t size1 = middleLength1 + 1;
    const size_t size2 = middleLength2 + 1;
    Sequence middleSequence;

    if (middleLength1 > 0 && middleLength2 > 0)
    {
        m_backtrackData.resize(size1 * size2);

        std::vector<size_t> rowTop(size1, size_t());
        std::vector<size_t> rowBottom(size1, size_t());

        // Jakub Melka: we will have columns consisting of it1...it1End
        // and rows consisting of it2...it2End. We iterate trough rows,
        // and for each row, we update longest common subsequence data.

        auto it2 = middleBegin2;
        for (size_t i2 = 1; i2 < size2; ++i2, ++it2)
        {
            if (PDFOperationControl::isOperationCanceled(control))
            {
                m_backtrackData.clear();
                return false;
            }

            auto it1 = middleBegin1;
            for (size_t i1 = 1; i1 < size1; ++i1, ++it1)
            {
                if (m_comparator(*it1, *it2))
                {
                    // We have match
                    rowBottom[i1] = rowTop[i1 - 1] + 1;
                }
                else
                {
                    const size_t leftCellValue = rowBottom[i1 - 1];
                    const size_t upperCellValue = rowTop[i1];
                    bool isLeftBigger = leftCellValue > upperCellValue;

                    if (isLeftBigger)
                    {
                        rowBottom[i1] = leftCellValue;
                        m_backtrackData[i2 * size1 + i1] = true;
                    }
                    else
                    {
                        rowBottom[i1] = upperCellValue;
                        m_backtrackData[i2 * size1 + i1] = false;
                    }
                }
            }

            // Bottom row will become top row
            std::swap(rowTop, rowBottom);
        }
    }

    size_t i1 = size1 - 1;
    size_t i2 = size2 - 1;

    while (i1 > 0 && i2 > 0)
    {
        SequenceItem item;

        const size_t index1 = i1 - 1;
        const size_t index2 = i2 - 1;

        auto cit1 = std::next(middleBegin1, index1);
        auto cit2 = std::next(middleBegin2, index2);

        if (m_comparator(*cit1, *cit2))
        {
            item.index1 = prefix + index1;
            item.index2 = prefix + index2;

            --i1;
            --i2;
        }
        else
        {
            if (m_backtrackData[i2 * size1 + i1])
            {
                item.index1 = prefix + index1;
                --i1;
            }
            else
            {
                item.index2 = prefix + index2;
                --i2;
            }
        }

        middleSequence.push_back(item);
    }

    while (i1 > 0)
    {
        SequenceItem item;

        const size_t index1 = i1 - 1;
        item.index1 = prefix + index1;
        --i1;

        middleSequence.push_back(item);
    }

    while (i2 > 0)
    {
        SequenceItem item;

        const size_t index2 = i2 - 1;
        item.index2 = prefix + index2;
        --i2;

        middleSequence.push_back(item);
    }

    m_backtrackData.clear();
    m_backtrackData.shrink_to_fit();

    m_sequence.reserve(prefix + middleSequence.size() + suffix);
    for (size_t i = 0; i < prefix; ++i)
    {
        SequenceItem item;
        item.index1 = i;
        item.index2 = i;
        m_sequence.push_back(item);
    }

    m_sequence.insert(m_sequence.end(), middleSequence.rbegin(), middleSequence.rend());

    for (size_t i = 0; i < suffix; ++i)
    {
        SequenceItem item;
        item.index1 = length1 - suffix + i;
        item.index2 = length2 - suffix + i;
        m_sequence.push_back(item);
    }

    return true;
}

}   // namespace pdf

#endif // PDFALGORITHMLCS_H
