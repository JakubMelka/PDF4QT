//    Copyright (C) 2019-2020 Jakub Melka
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

template<typename T>
QDataStream& operator>>(QDataStream& stream, std::vector<T>& vector)
{
    std::vector<T>::size_type size = 0;
    stream >> size;
    vector.resize(size);
    for (T& item : vector)
    {
        stream >> item;
    }
    return stream;
}

template<typename T>
QDataStream& operator<<(QDataStream& stream, const std::vector<T>& vector)
{
    stream << vector.size();
    for (const T& item : vector)
    {
        stream << item;
    }
    return stream;
}

template<typename T>
QDataStream& operator>>(QDataStream& stream, std::set<T>& set)
{
    std::set<T>::size_type size = 0;
    stream >> size;
    for (size_t i = 0; i < size; ++i)
    {
        T item;
        stream >> item;
        set.insert(set.end(), qMove(item));
    }
    return stream;
}

template<typename T>
QDataStream& operator<<(QDataStream& stream, const std::set<T>& set)
{
    stream << set.size();
    for (const T& item : set)
    {
        stream << item;
    }
    return stream;
}

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

QDataStream& operator>>(QDataStream& stream, PDFTextLayout& layout)
{
    stream >> layout.m_characters;
    stream >> layout.m_angles;
    stream >> layout.m_settings;
    stream >> layout.m_blocks;
    return stream;
}

QDataStream& operator<<(QDataStream& stream, const PDFTextLayout& layout)
{
    stream << layout.m_characters;
    stream << layout.m_angles;
    stream << layout.m_settings;
    stream << layout.m_blocks;
    return stream;
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

QDataStream& operator>>(QDataStream& stream, PDFTextLine& line)
{
    stream >> line.m_characters;
    stream >> line.m_boundingBox;
    stream >> line.m_topLeft;
    return stream;
}

QDataStream& operator<<(QDataStream& stream, const PDFTextLine& line)
{
    stream << line.m_characters;
    stream << line.m_boundingBox;
    stream << line.m_topLeft;
    return stream;
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

QDataStream& operator>>(QDataStream& stream, PDFTextBlock& block)
{
    stream >> block.m_lines;
    stream >> block.m_boundingBox;
    stream >> block.m_topLeft;
    return stream;
}

QDataStream& operator<<(QDataStream& stream, const PDFTextBlock& block)
{
    stream << block.m_lines;
    stream << block.m_boundingBox;
    stream << block.m_topLeft;
    return stream;
}

void TextCharacter::applyTransform(const QMatrix& matrix)
{
    position = matrix.map(position);
    boundingBox = matrix.map(boundingBox);
}

QDataStream& operator<<(QDataStream& stream, const TextCharacter& character)
{
    stream << character.character;
    stream << character.position;
    stream << character.angle;
    stream << character.fontSize;
    stream << character.advance;
    stream << character.boundingBox;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, TextCharacter& character)
{
    stream >> character.character;
    stream >> character.position;
    stream >> character.angle;
    stream >> character.fontSize;
    stream >> character.advance;
    stream >> character.boundingBox;
    return stream;
}

PDFTextLayout PDFTextLayoutStorage::getTextLayout(PDFInteger pageIndex) const
{
    PDFTextLayout result;

    if (pageIndex >= 0 && pageIndex < static_cast<PDFInteger>(m_offsets.size()))
    {
        QDataStream layoutStream(const_cast<QByteArray*>(&m_textLayouts), QIODevice::ReadOnly);
        layoutStream.skipRawData(m_offsets[pageIndex]);

        QByteArray buffer;
        layoutStream >> buffer;
        buffer = qUncompress(buffer);

        QDataStream stream(&buffer, QIODevice::ReadOnly);
        stream >> result;
    }

    return result;
}

void PDFTextLayoutStorage::setTextLayout(PDFInteger pageIndex, const PDFTextLayout& layout, QMutex* mutex)
{
    QByteArray result;
    {
        QDataStream stream(&result, QIODevice::WriteOnly);
        stream << layout;
    }
    result = qCompress(result, 9);

    QMutexLocker lock(mutex);
    m_offsets[pageIndex] = m_textLayouts.size();

    QDataStream layoutStream(&m_textLayouts, QIODevice::Append | QIODevice::WriteOnly);
    layoutStream << result;
}

QDataStream& operator<<(QDataStream& stream, const PDFTextLayoutSettings& settings)
{
    stream << settings.samples;
    stream << settings.distanceSensitivity;
    stream << settings.charactersOnLineSensitivity;
    stream << settings.fontSensitivity;
    stream << settings.blockVerticalSensitivity;
    stream << settings.blockOverlapSensitivity;
    return stream;
}

QDataStream& operator>>(QDataStream& stream, PDFTextLayoutSettings& settings)
{
    stream >> settings.samples;
    stream >> settings.distanceSensitivity;
    stream >> settings.charactersOnLineSensitivity;
    stream >> settings.fontSensitivity;
    stream >> settings.blockVerticalSensitivity;
    stream >> settings.blockOverlapSensitivity;
    return stream;
}

PDFTextSelection::PDFTextSelection(PDFTextSelectionItems&& items) :
    m_items(qMove(items))
{

}

PDFFindResults PDFTextFlow::find(const QString& text, Qt::CaseSensitivity caseSensitivity) const
{
    PDFFindResults results;

    int index = m_text.indexOf(text, 0, caseSensitivity);
    while (index != -1)
    {
        PDFFindResult result;
        result.matched = text;
        result.textSelectionItems = getTextSelectionItems(index, text.length());
        result.context = getContext(index, text.length());

        if (!result.textSelectionItems.empty())
        {
            results.emplace_back(qMove(result));
        }

        index = m_text.indexOf(text, index + 1, caseSensitivity);
    }

    return results;
}

void PDFTextFlow::merge(const PDFTextFlow& next)
{
    m_text += next.m_text;
    m_characterPointers.insert(m_characterPointers.end(), next.m_characterPointers.cbegin(), next.m_characterPointers.cend());
}

PDFTextFlows PDFTextFlow::createTextFlows(const PDFTextLayout& layout, FlowFlags flags, PDFInteger pageIndex)
{
    PDFTextFlows result;

    if (!flags.testFlag(SeparateBlocks))
    {
        result.emplace_back();
    }

    QString lineBreak(" ");
    if (flags.testFlag(AddLineBreaks))
    {
#if defined(Q_OS_WIN)
        lineBreak = QString("\r\n");
#elif defined(Q_OS_UNIX)
        linebreak = QString("\n");
#elif defined(Q_OS_MAC)
        lineBreak = QString("\r");
#else
        static_assert(false, "Fix this code!");
#endif
    }

    size_t textBlockIndex = 0;
    for (const PDFTextBlock& textBlock : layout.getTextBlocks())
    {
        PDFTextFlow currentFlow;

        size_t textLineIndex = 0;
        for (const PDFTextLine& textLine : textBlock.getLines())
        {
            const TextCharacters& characters = textLine.getCharacters();
            for (size_t i = 0, characterCount = characters.size(); i < characterCount; ++i)
            {
                const TextCharacter& currentCharacter = characters[i];
                if (i > 0 && !currentCharacter.character.isSpace())
                {
                    // Jakub Melka: try to guess space between letters
                    const TextCharacter& previousCharacter = characters[i - 1];
                    if (!previousCharacter.character.isSpace() && QLineF(previousCharacter.position, currentCharacter.position).length() > previousCharacter.advance * 1.1)
                    {
                        currentFlow.m_text += QChar(' ');
                        currentFlow.m_characterPointers.emplace_back();
                    }
                }

                currentFlow.m_text += currentCharacter.character;

                PDFCharacterPointer pointer;
                pointer.pageIndex = pageIndex;
                pointer.blockIndex = textBlockIndex;
                pointer.lineIndex = textLineIndex;
                pointer.characterIndex = i;
                currentFlow.m_characterPointers.emplace_back(qMove(pointer));
            }

            // Remove soft hyphen, if it is enabled
            if (flags.testFlag(RemoveSoftHyphen) && !characters.empty() && currentFlow.m_text.back() == QChar(QChar::SoftHyphen))
            {
                currentFlow.m_text.chop(1);
                currentFlow.m_characterPointers.pop_back();

                if (!flags.testFlag(AddLineBreaks))
                {
                    // Do not add single empty space - because soft hypen probably breaks a word
                    ++textLineIndex;
                    continue;
                }
            }

            // Add line break
            currentFlow.m_text += lineBreak;
            currentFlow.m_characterPointers.insert(currentFlow.m_characterPointers.end(), lineBreak.length(), PDFCharacterPointer());

            ++textLineIndex;
        }

        // If we are producing separate blocks, then make flow for each
        // text block, otherwise join flows.
        if (flags.testFlag(SeparateBlocks))
        {
            result.emplace_back(qMove(currentFlow));
        }
        else
        {
            result.back().merge(currentFlow);
        }

        ++textBlockIndex;
    }

    return result;
}

PDFTextSelectionItems PDFTextFlow::getTextSelectionItems(size_t index, size_t length) const
{
    PDFTextSelectionItems items;

    auto it = std::next(m_characterPointers.cbegin(), index);
    auto itEnd = std::next(m_characterPointers.cbegin(), index + length);
    s

    return items;
}

QString PDFTextFlow::getContext(size_t index, size_t length) const
{
    Q_ASSERT(length > 0);

    while (index > 0 && m_characterPointers[index - 1].hasSameLine(m_characterPointers[index]))
    {
        --index;
        ++length;
    }

    size_t currentEnd = index + length - 1;
    size_t last = m_characterPointers.size() - 1;
    while (currentEnd < last && m_characterPointers[currentEnd].hasSameLine(m_characterPointers[currentEnd + 1]))
    {
        ++currentEnd;
        ++length;
    }

    return m_text.mid(int(index), int(length));
}

bool PDFCharacterPointer::hasSameLine(const PDFCharacterPointer& other) const
{
    return pageIndex == other.pageIndex && blockIndex == other.blockIndex && lineIndex == other.lineIndex;
}

}   // namespace pdf
