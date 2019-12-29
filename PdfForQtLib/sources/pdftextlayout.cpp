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

#include "pdftextlayout.h"
#include "pdfutils.h"

#include <execution>

namespace pdf
{

PDFTextLayout::PDFTextLayout()
{

}

void PDFTextLayout::addCharacter(const PDFTextCharacterInfo& info)
{
    TextCharacter character;

    // Fill the basic info. For computing the angle, we must consider, if we are
    // in vertical writing system. If yes, take vertical edge of the character,
    // otherwise take horizontal edge.
    character.character = info.character;
    character.position = info.matrix.map(QPointF(0.0, 0.0));

    QLineF testLine(QPointF(0.0, 0.0), QPointF(info.isVerticalWritingSystem ? 0.0 : info.advance, !info.isVerticalWritingSystem ? 0.0 : info.advance));
    QLineF mappedLine = info.matrix.map(testLine);
    character.advance = mappedLine.length();
    character.angle = qRound(mappedLine.angle());

    QLineF fontTestLine(QPointF(0.0, 0.0), QPointF(0.0, info.fontSize));
    QLineF fontMappedLine = info.matrix.map(fontTestLine);
    character.fontSize = fontMappedLine.length();

    QRectF boundingBox = info.outline.boundingRect();
    character.boundingBox.addPolygon(info.matrix.map(boundingBox));

    m_characters.emplace_back(qMove(character));
    m_angles.insert(character.angle);
}

void PDFTextLayout::perform()
{
    for (PDFReal angle : m_angles)
    {
        performDoLayout(angle);
    }
}

void PDFTextLayout::optimize()
{
    m_characters.shrink_to_fit();
}

qint64 PDFTextLayout::getMemoryConsumptionEstimate() const
{
    qint64 estimate = sizeof(*this);
    estimate += sizeof(decltype(m_characters)::value_type) * m_characters.capacity();
    estimate += sizeof(decltype(m_angles)::value_type) * m_angles.size();
    return estimate;
}

struct NearestCharacterInfo
{
    size_t index = std::numeric_limits<size_t>::max();
    PDFReal distance = std::numeric_limits<PDFReal>::infinity();

    inline bool operator<(const NearestCharacterInfo& other) const { return distance < other.distance; }
};

void PDFTextLayout::performDoLayout(PDFReal angle)
{
    // We will implement variation of 'docstrum' algorithm, we have divided characters by angles,
    // for each angle we get characters for that particular angle, and run 'docstrum' algorithm.
    // We will do following steps:
    //      1) Rotate the plane with characters so that they are all in horizontal line
    //      2) Find k-nearest characters for each character (so each character will have
    //         k pointers to the nearest characters)
    //      3) Find text lines. We will do that by creating transitive closure of characters, i.e.
    //         characters, which are close and are on horizontal line, are marked as in one text line.
    //         Consider also font size and empty space size between different characters.
    //      4) Merge text lines into text blocks using various criteria, such as overlap,
    //         distance between the lines, and also using again, transitive closure.
    //      5) Sort blocks using topological ordering
    TextCharacters characters = getCharactersForAngle(angle);

    // Step 1) - rotate blocks
    QMatrix angleMatrix;
    angleMatrix.rotate(angle);
    applyTransform(characters, angleMatrix);

    // Step 2) - find k-nearest characters
    const size_t characterCount = characters.size();
    const size_t bucketSize = m_settings.samples + 1;
    std::vector<NearestCharacterInfo> nearestCharacters(bucketSize * characters.size(), NearestCharacterInfo());

    auto findNearestCharacters = [this, bucketSize, characterCount, &characters, &nearestCharacters](size_t currentCharacterIndex)
    {
        // It will be iterator to the start of the nearest neighbour sequence
        auto it = std::next(nearestCharacters.begin(), currentCharacterIndex * bucketSize);
        auto itLast = std::next(it, m_settings.samples);
        NearestCharacterInfo& insertInfo = *itLast;
        QPointF currentPoint = characters[currentCharacterIndex].position;

        for (size_t i = 0; i < characterCount; ++i)
        {
            if (i == currentCharacterIndex)
            {
                continue;
            }

            insertInfo.index = i;
            insertInfo.distance = QLineF(currentPoint, characters[i].position).length();

            // Now, use insert sort to sort the array of samples + 1 elements (#samples elements
            // are sorted, we use only insert sort on the last element).
            auto itLeft = std::prev(itLast);
            auto itRight = itLast;
            while (true)
            {
                if (*itRight < *itLeft)
                {
                    std::swap(*itRight, *itLeft);
                    itRight = itLeft;

                    if (itLeft == it)
                    {
                        // We have reached the end
                        break;
                    }

                    --itLeft;
                }
                else
                {
                    // We have proper order, break the cycle
                    break;
                }
            }
        }
    };

    auto range = PDFIntegerRange<size_t>(0, characterCount);
    std::for_each(std::execution::parallel_policy(), range.begin(), range.end(), findNearestCharacters);

    // Step 3) - detect lines
    PDFUnionFindAlgorithm<size_t> textLinesUF(characterCount);
    for (size_t i = 0; i < characterCount; ++i)
    {
        auto it = std::next(nearestCharacters.begin(), i * bucketSize);
        auto itEnd = std::next(it, m_settings.samples);

        for (; it != itEnd; ++it)
        {
            const NearestCharacterInfo& info = *it;
            if (info.index == std::numeric_limits<size_t>::max())
            {
                // We have reached the end - or we do not have enough characters
                break;
            }

            // Criteria:
            //   1) Distance of characters is not too large
            //   2) Characters are approximately at same line
            //   3) Font size of characters are approximately equal

            PDFReal fontSizeMax = qMax(characters[i].fontSize, characters[info.index].fontSize);
            PDFReal fontSizeMin = qMin(characters[i].fontSize, characters[info.index].fontSize);

            if (info.distance < m_settings.distanceSensitivity * characters[i].advance && // 1)
                std::fabs(characters[i].position.y() - characters[info.index].position.y()) < fontSizeMin * m_settings.charactersOnLineSensitivity && // 2)
                fontSizeMax / fontSizeMin < m_settings.fontSensitivity) // 3)
            {
                textLinesUF.unify(i, info.index);
            }
        }
    }

    std::map<size_t, TextCharacters> lineToCharactersMap;
    for (size_t i = 0; i < characterCount; ++i)
    {
        lineToCharactersMap[textLinesUF.find(i)].push_back(characters[i]);
    }

    PDFTextLines lines;
    lines.reserve(lineToCharactersMap.size());
    for (auto& item : lineToCharactersMap)
    {
        lines.emplace_back(qMove(item.second));
    }

    // Step 4) - detect text blocks
    const size_t lineCount = lines.size();
    PDFUnionFindAlgorithm<size_t> textBlocksUF(lineCount);
    for (size_t i = 0; i < lineCount; ++i)
    {
        for (size_t j = i + 1; j < lineCount; ++j)
        {
            QRectF bb1 = lines[i].getBoundingBox().boundingRect();
            QRectF bb2 = lines[j].getBoundingBox().boundingRect();

            // Jakub Melka: we will join two blocks, if these two conditions both holds:
            //     1) bounding boxes overlap horizontally by large portion
            //     2) vertical space between bounding boxes is not too large

            QRectF bbUnion = bb1.united(bb2);
            const PDFReal height = bbUnion.height();
            const PDFReal heightLimit = (bb1.height() + bb2.height()) * m_settings.blockVerticalSensitivity;
            const PDFReal overlap = qMax(0.0, bb1.width() + bb2.width() - bbUnion.width());
            const PDFReal minimalOverlap = qMin(bb1.width(), bb2.width()) * m_settings.blockOverlapSensitivity;
            if (height < heightLimit && overlap > minimalOverlap)
            {
                textBlocksUF.unify(i, j);
            }
        }
    }

    std::map<size_t, PDFTextLines> blockToLines;
    for (size_t i = 0; i < lineCount; ++i)
    {
        blockToLines[textBlocksUF.find(i)].push_back(qMove(lines[i]));
    }

    PDFTextBlocks blocks;
    blocks.reserve(blockToLines.size());
    for (auto& item : blockToLines)
    {
        blocks.emplace_back(qMove(item.second));
    }

    // 5) Sort block by topological ordering. We will use approache described in paper
    // "High Performance Document Layout Analysis", T.M. Breuel, 2003, where are described
    // two rules, which are used to determine block precedence.
    //
    // Rule 1: a<b, if:
    //    - blocks a,b have overlap in x-axis
    //    - block a is above block b
    //
    // Rule 2: a<b, if:
    //    - block a is entirely on left side of block b
    //    - there doesn't exist block c, which is between a,b in y-axis
    //      and moreover, overlaps both a and b in x-axis.

    auto isBeforeByRule1 = [&blocks](const size_t aIndex, const size_t bIndex)
    {
        QRectF aBB = blocks[aIndex].getBoundingBox().boundingRect();
        QRectF bBB = blocks[bIndex].getBoundingBox().boundingRect();

        const bool isOverlappedOnHorizontalAxis = isRectangleHorizontallyOverlapped(aBB, bBB);
        const bool isAoverB = aBB.bottom() > bBB.top();
        return isOverlappedOnHorizontalAxis && isAoverB;
    };
    auto isBeforeByRule2 = [&blocks](const size_t aIndex, const size_t bIndex)
    {
        QRectF aBB = blocks[aIndex].getBoundingBox().boundingRect();
        QRectF bBB = blocks[bIndex].getBoundingBox().boundingRect();
        QRectF abBB = aBB.united(bBB);

        if (aBB.right() < bBB.left())
        {
            // Check, if 'c' block doesn't exist
            for (size_t i = 0, count = blocks.size(); i < count; ++i)
            {
                if (i == aIndex || i == bIndex)
                {
                    continue;
                }

                QRectF cBB = blocks[i].getBoundingBox().boundingRect();
                if (cBB.top() >= abBB.top() && cBB.bottom() <= abBB.bottom())
                {
                    const bool isAOverlappedOnHorizontalAxis = isRectangleHorizontallyOverlapped(aBB, cBB);
                    const bool isBOverlappedOnHorizontalAxis = isRectangleHorizontallyOverlapped(bBB, cBB);
                    if (isAOverlappedOnHorizontalAxis && isBOverlappedOnHorizontalAxis)
                    {
                        return false;
                    }
                }
            }

            return true;
        }

        return false;
    };

    // Order blocks using topological sort (https://en.wikipedia.org/wiki/Topological_sorting,
    // Kahn's algorithm is used)
    std::set<size_t> workBlocks;
    std::vector<size_t> ordering;
    std::vector<std::set<size_t>> orderingEdges(blocks.size(), std::set<size_t>());
    ordering.reserve(blocks.size());
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        workBlocks.insert(workBlocks.end(), i);
        for (size_t j = 0; j < blocks.size(); ++j)
        {
            if (i != j && (isBeforeByRule1(j, i) || isBeforeByRule2(j, i)))
            {
                orderingEdges[i].insert(j);
            }
        }
    }

    // Topological sort
    QMatrix invertedAngleMatrix = angleMatrix.inverted();
    while (!workBlocks.empty())
    {
        auto it = std::min_element(workBlocks.begin(), workBlocks.end(), [&orderingEdges](const size_t l, const size_t r) { return orderingEdges[l].size() < orderingEdges[r].size(); });
        ordering.push_back(*it);
        for (std::set<size_t>& edges : orderingEdges)
        {
            edges.erase(*it);
        }

        blocks[*it].applyTransform(invertedAngleMatrix);
        m_blocks.emplace_back(qMove(blocks[*it]));
        workBlocks.erase(it);
    }
}

TextCharacters PDFTextLayout::getCharactersForAngle(PDFReal angle) const
{
    TextCharacters result;
    std::copy_if(m_characters.cbegin(), m_characters.cend(), std::back_inserter(result), [angle](const TextCharacter& character) { return character.angle == angle; });
    return result;
}

void PDFTextLayout::applyTransform(TextCharacters& characters, const QMatrix& matrix)
{
    for (TextCharacter& character : characters)
    {
        character.position = matrix.map(character.position);
        character.boundingBox = matrix.map(character.boundingBox);
    }
}

PDFTextLine::PDFTextLine(TextCharacters characters) :
    m_characters(qMove(characters))
{
    std::sort(m_characters.begin(), m_characters.end(), [](const TextCharacter& l, const TextCharacter& r) { return l.position.x() < r.position.x(); });

    QRectF boundingBox;
    for (const TextCharacter& character : m_characters)
    {
        boundingBox = boundingBox.united(character.boundingBox.boundingRect());
    }
    m_boundingBox.addRect(boundingBox);
    m_topLeft = boundingBox.topLeft();
}

void PDFTextLine::applyTransform(const QMatrix& matrix)
{
    m_boundingBox = matrix.map(m_boundingBox);
    m_topLeft = matrix.map(m_topLeft);
    for (TextCharacter& character : m_characters)
    {
        character.applyTransform(matrix);
    }
}

PDFTextBlock::PDFTextBlock(PDFTextLines textLines) :
    m_lines(qMove(textLines))
{
    auto sortFunction = [](const PDFTextLine& l, const PDFTextLine& r)
    {
        QRectF bl = l.getBoundingBox().boundingRect();
        QRectF br = r.getBoundingBox().boundingRect();
        const PDFReal xL = bl.x();
        const PDFReal xR = br.x();
        const PDFReal yL = qRound(bl.y() * 100.0);
        const PDFReal yR = qRound(br.y() * 100.0);
        return std::tie(-yL, xL) < std::tie(-yR, xR);
    };
    std::sort(m_lines.begin(), m_lines.end(), sortFunction);

    QRectF boundingBox;
    for (const PDFTextLine& line : m_lines)
    {
        boundingBox = boundingBox.united(line.getBoundingBox().boundingRect());
    }
    m_boundingBox.addRect(boundingBox);
    m_topLeft = boundingBox.topLeft();
}

void PDFTextBlock::applyTransform(const QMatrix& matrix)
{
    m_boundingBox = matrix.map(m_boundingBox);
    m_topLeft = matrix.map(m_topLeft);
    for (PDFTextLine& textLine : m_lines)
    {
        textLine.applyTransform(matrix);
    }
}

void TextCharacter::applyTransform(const QMatrix& matrix)
{
    position = matrix.map(position);
    boundingBox = matrix.map(boundingBox);
}

}   // namespace pdf
