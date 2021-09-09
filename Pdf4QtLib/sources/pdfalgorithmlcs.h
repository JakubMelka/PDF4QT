//    Copyright (C) 2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT.  If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFALGORITHMLCS_H
#define PDFALGORITHMLCS_H

#include "pdfglobal.h"

namespace pdf
{

/// Algorithm for computing longest common subsequence, on two sequences
/// of objects, which are implementing operator "==" (equal operator).
/// Constructor takes bidirectional iterators to the sequence. So, iterators
/// are requred to be bidirectional.
template<typename Iterator, typename Comparator>
class PDFAlgorithmLongestCommonSubsequence
{
public:
    PDFAlgorithmLongestCommonSubsequence(Iterator it1,
                                         Iterator it1End,
                                         Iterator it2,
                                         Iterator it2End,
                                         Comparator comparator);

    void perform();

private:
    Iterator m_it1;
    Iterator m_it1End;
    Iterator m_it2;
    Iterator m_it2End;

    size_t m_size1;
    size_t m_size2;
    size_t m_matrixSize;

    Comparator m_comparator;

    std::vector<bool> m_backtrackData;
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
    m_matrixSize(0),
    m_comparator(std::move(comparator))
{
    m_size1 = std::distance(m_it1, m_it1End) + 1;
    m_size2 = std::distance(m_it2, m_it2End) + 1;
    m_matrixSize = m_size1 * m_size2;
}

template<typename Iterator, typename Comparator>
void PDFAlgorithmLongestCommonSubsequence<Iterator, Comparator>::perform()
{
    m_backtrackData.resize(m_matrixSize);

    std::vector<size_t> rowTop(m_size1, size_t());
    std::vector<size_t> rowBottom(m_size1, size_t());

    // Jakub Melka: we will have columns consisting of it1...it1End
    // and rows consisting of it2...it2End. We iterate trough rows,
    // and for each row, we update longest common subsequence data.

    auto it2 = m_it2;
    for (size_t i2 = 1; i2 < m_size2; ++i2, ++it2)
    {
        auto it1 = m_it1;
        for (size_t i1 = 1; i1 < m_size1; ++i1, ++it1)
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
                    m_backtrackData[i2 * m_size1 + i1] = true;
                }
                else
                {
                    rowBottom[i1] = upperCellValue;
                    m_backtrackData[i2 * m_size1 + i1] = false;
                }
            }
        }

        // Bottom row will become top row
        std::swap(rowTop, rowBottom);
    }
}

}   // namespace pdf

#endif // PDFALGORITHMLCS_H
