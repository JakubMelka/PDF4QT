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

#include "pdftextlayout.h"
#include "pdftextlayoutgenerator.h"
#include "pdfdocument.h"
#include "pdfdocumentreader.h"
#include "pdfcatalog.h"
#include "pdfpage.h"
#include "pdffont.h"
#include "pdfcms.h"
#include "pdfconstants.h"
#include "pdfoptionalcontent.h"
#include "pdfmeshqualitysettings.h"
#include "pdfexecutionpolicy.h"

#include <QtTest>
#include <QCryptographicHash>
#include <QElapsedTimer>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "psapi.lib")
#endif
#endif

#include <atomic>
#include <numeric>
#include <thread>
#include <random>

class TextLayoutTest : public QObject
{
    Q_OBJECT

private slots:
    void nearbyAnglesShareSelectionGeometry();
    void angleGroupsDoNotGrowTransitively();
    void cacheRetainsRecentlyUsedPages();
    void boundingBox();
    void layoutDoesNotHoldCharactersTwice();
    void selectionByRectangle();
    void storageRoundTrip();
    void storageIsFilledFromMultipleThreads();
    void linesAreDetectedByNearestCharacters();
    void documentBenchmark();

private:
    struct TextLayoutStatistics
    {
        size_t blockCount = 0;
        size_t lineCount = 0;
        size_t characterCount = 0;
    };

    static qint64 getPrivateMemoryUsage();

    /// Adds a character to the layout
    /// \param layout Layout
    /// \param character Character
    /// \param position Position of the character
    /// \param angle Angle of the character in degrees
    /// \param hasOutline Character has an outline (characters of Type 3 fonts have no outline)
    static void addCharacter(pdf::PDFTextLayout& layout, QChar character, QPointF position, qreal angle, bool hasOutline = true);

    /// Creates a layout of a page with paragraphs of horizontal text and with a rotated text
    /// \param paragraphCount Number of paragraphs
    static pdf::PDFTextLayout createPageLayout(int paragraphCount);

    /// Creates a layout of a page, which lies far from the origin of the coordinate
    /// system. It has a line with a skewed character, and a line of characters without
    /// an outline.
    /// \param offset Position of the page
    static pdf::PDFTextLayout createSpecialLayout(QPointF offset);

    /// Compares the layout read from the storage with the original layout. Storage
    /// uses single precision, so the geometry is compared with a tolerance.
    static void compareLayouts(const pdf::PDFTextLayout& layout, const pdf::PDFTextLayout& original);

    /// Returns a sum of the geometry of the layout, which is used as a fingerprint,
    /// and optionally updates the statistics and the hash of the text.
    static double getGeometrySum(const pdf::PDFTextLayout& textLayout, TextLayoutStatistics* statistics, QCryptographicHash* textHash);

    /// Returns a text description of the structure of the layout (blocks and lines in their
    /// order, with their geometry and text). Descriptions of two versions of the layout
    /// algorithm can be compared line by line.
    static QByteArray getStructureDump(const pdf::PDFTextLayout& textLayout);
};

qint64 TextLayoutTest::getPrivateMemoryUsage()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters = { };
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)))
    {
        return qint64(counters.PrivateUsage);
    }
#endif
    return 0;
}

void TextLayoutTest::addCharacter(pdf::PDFTextLayout& layout, QChar character, QPointF position, qreal angle, bool hasOutline)
{
    pdf::PDFTextCharacterInfo info;
    info.character = character;
    info.advance = 8.0;
    info.fontSize = 10.0;
    if (hasOutline)
    {
        info.outline.addRect(QRectF(0, 0, 7, 10));
    }
    info.matrix.translate(position.x(), position.y());
    info.matrix.rotate(-angle);
    layout.addCharacter(info);
}

void TextLayoutTest::nearbyAnglesShareSelectionGeometry()
{
    pdf::PDFTextLayout layout;
    addCharacter(layout, 'A', QPointF(100, 100), 359);
    addCharacter(layout, 'B', QPointF(108, 100), 0);
    addCharacter(layout, 'C', QPointF(116, 100), 0);
    layout.perform();

    QCOMPARE(layout.getTextBlocks().size(), size_t(1));
    const auto& block = layout.getTextBlocks().front();
    // Selection rectangles must use the same baseline as the layout algorithm,
    // even when the first glyph is the slightly skewed one.
    QCOMPARE(block.getLines().front().getCharacters().front().angle, 0.0);
    const auto selection = layout.selectBlock(0, 0, Qt::yellow);
    QCOMPARE(layout.getTextFromSelection(selection, 0).trimmed(), QString("ABC"));
}

void TextLayoutTest::angleGroupsDoNotGrowTransitively()
{
    pdf::PDFTextLayout layout;
    addCharacter(layout, 'A', QPointF(100, 100), 0);
    addCharacter(layout, 'B', QPointF(108, 100), 2);
    addCharacter(layout, 'C', QPointF(116, 100), 4);
    layout.perform();

    // A chain of small angle differences must not merge arbitrarily different
    // writing directions into one horizontal line.
    QCOMPARE(layout.getTextBlocks().size(), size_t(2));
}

void TextLayoutTest::cacheRetainsRecentlyUsedPages()
{
    std::map<pdf::PDFInteger, int> calls;
    pdf::PDFTextLayoutCache cache([&calls](pdf::PDFInteger page)
    {
        ++calls[page];
        return pdf::PDFTextLayout();
    });

    const auto* first = &cache.getTextLayout(0);
    cache.getTextLayout(1);
    QCOMPARE(&cache.getTextLayout(0), first);
    QCOMPARE(calls[0], 1);
    QCOMPARE(calls[1], 1);
    cache.clear();
    cache.getTextLayout(0);
    QCOMPARE(calls[0], 2);
}

pdf::PDFTextLayout TextLayoutTest::createPageLayout(int paragraphCount)
{
    pdf::PDFTextLayout layout;

    // Paragraphs of three lines, each line has two words
    for (int paragraph = 0; paragraph < paragraphCount; ++paragraph)
    {
        for (int line = 0; line < 3; ++line)
        {
            const qreal y = 700.0 - paragraph * 80.0 - line * 12.0;
            for (int i = 0; i < 12; ++i)
            {
                if (i != 5)
                {
                    addCharacter(layout, QChar(char16_t(u'a' + (paragraph + line + i) % 26)), QPointF(100.0 + 8.0 * i, y), 0);
                }
            }
        }
    }

    // Text rotated by 90 degrees along the left edge of the page
    for (int i = 0; i < 10; ++i)
    {
        addCharacter(layout, QChar(char16_t(u'A' + i)), QPointF(40.0, 380.0 - 8.0 * i), 90);
    }

    layout.perform();
    return layout;
}

pdf::PDFTextLayout TextLayoutTest::createSpecialLayout(QPointF offset)
{
    pdf::PDFTextLayout layout;

    // First character is slightly skewed, so its bounding box is not parallel to the line
    addCharacter(layout, 'x', offset + QPointF(100, 100), 359);
    addCharacter(layout, 'y', offset + QPointF(108, 100), 0);
    addCharacter(layout, 'z', offset + QPointF(116, 100), 0);

    for (int i = 0; i < 4; ++i)
    {
        addCharacter(layout, QChar(char16_t(u'p' + i)), offset + QPointF(100.0 + 8.0 * i, 300.0), 0, false);
    }

    layout.perform();
    return layout;
}

void TextLayoutTest::compareLayouts(const pdf::PDFTextLayout& layout, const pdf::PDFTextLayout& original)
{
    constexpr double TOLERANCE = 1e-3;

    auto comparePoints = [](const QPointF& point, const QPointF& originalPoint)
    {
        QVERIFY2(qAbs(point.x() - originalPoint.x()) < TOLERANCE && qAbs(point.y() - originalPoint.y()) < TOLERANCE,
                 qPrintable(QString("(%1, %2) != (%3, %4)").arg(point.x()).arg(point.y()).arg(originalPoint.x()).arg(originalPoint.y())));
    };

    auto compareBoxes = [&comparePoints](const pdf::PDFTextBoundingBox& box, const pdf::PDFTextBoundingBox& originalBox)
    {
        for (size_t i = 0; i < box.getPoints().size(); ++i)
        {
            comparePoints(box.getPoints()[i], originalBox.getPoints()[i]);
        }
    };

    QCOMPARE(layout.getTextBlocks().size(), original.getTextBlocks().size());
    for (size_t blockIndex = 0; blockIndex < layout.getTextBlocks().size(); ++blockIndex)
    {
        const pdf::PDFTextBlock& block = layout.getTextBlocks()[blockIndex];
        const pdf::PDFTextBlock& originalBlock = original.getTextBlocks()[blockIndex];

        compareBoxes(block.getBoundingBox(), originalBlock.getBoundingBox());
        comparePoints(block.getTopLeft(), originalBlock.getTopLeft());
        QCOMPARE(block.getLines().size(), originalBlock.getLines().size());

        for (size_t lineIndex = 0; lineIndex < block.getLines().size(); ++lineIndex)
        {
            const pdf::PDFTextLine& line = block.getLines()[lineIndex];
            const pdf::PDFTextLine& originalLine = originalBlock.getLines()[lineIndex];

            compareBoxes(line.getBoundingBox(), originalLine.getBoundingBox());
            comparePoints(line.getTopLeft(), originalLine.getTopLeft());
            QCOMPARE(line.getCharacters().size(), originalLine.getCharacters().size());

            for (size_t characterIndex = 0; characterIndex < line.getCharacters().size(); ++characterIndex)
            {
                const pdf::TextCharacter& character = line.getCharacters()[characterIndex];
                const pdf::TextCharacter& originalCharacter = originalLine.getCharacters()[characterIndex];

                QCOMPARE(character.character, originalCharacter.character);
                QCOMPARE(character.angle, originalCharacter.angle);
                comparePoints(character.position, originalCharacter.position);
                compareBoxes(character.boundingBox, originalCharacter.boundingBox);
                QVERIFY(qAbs(character.fontSize - originalCharacter.fontSize) < TOLERANCE);
                QVERIFY(qAbs(character.advance - originalCharacter.advance) < TOLERANCE);
            }
        }
    }
}

void TextLayoutTest::linesAreDetectedByNearestCharacters()
{
    // Lines are detected by the nearest characters of each character, which are found
    // by a spatial index. Result is compared with a simple search of nearest characters
    // among all characters of the page. Characters have random positions, advances and
    // font sizes (so no two distances are the same), and they are added in random order.
    // Small numbers of characters test the boundaries of the nodes of the index.
    const pdf::PDFTextLayoutSettings settings;
    std::mt19937 generator(20261010);

    struct Character
    {
        QPointF position;
        pdf::PDFReal advance = 0.0;
        pdf::PDFReal fontSize = 0.0;
    };

    using Line = std::vector<std::pair<pdf::PDFReal, pdf::PDFReal>>;

    std::vector<size_t> characterCounts = { 1, 2, 5, 6, 7, 15, 16, 17, 31, 32, 33, 100, 500, 3000 };
    for (const size_t characterCount : characterCounts)
    {
        // Rows of characters with random gaps, rows are close to each other,
        // so the nearest characters are often characters of other rows
        std::vector<Character> characters;
        std::uniform_real_distribution<pdf::PDFReal> advanceDistribution(2.0, 9.0);
        std::uniform_real_distribution<pdf::PDFReal> gapDistribution(0.0, 1.0);
        std::uniform_real_distribution<pdf::PDFReal> jitterDistribution(-1.5, 1.5);
        std::uniform_int_distribution<int> fontSizeDistribution(0, 9);

        const size_t rowLength = qMax<size_t>(size_t(std::sqrt(double(characterCount)) * 2.0), 1);
        pdf::PDFReal x = 50.0;
        for (size_t i = 0; i < characterCount; ++i)
        {
            if (i % rowLength == 0)
            {
                x = 50.0 + gapDistribution(generator) * 20.0;
            }

            Character character;
            character.advance = advanceDistribution(generator);
            character.fontSize = (fontSizeDistribution(generator) == 0) ? 24.0 : 9.0 + gapDistribution(generator) * 3.0;
            character.position = QPointF(x, 700.0 - 7.0 * pdf::PDFReal(i / rowLength) + jitterDistribution(generator));
            characters.push_back(character);

            // Mostly a small gap, sometimes a gap of a word or of a column
            const pdf::PDFReal gap = gapDistribution(generator);
            x += character.advance + ((gap < 0.7) ? 0.0 : (gap < 0.9) ? gap * 8.0 : gap * 40.0);
        }

        std::shuffle(characters.begin(), characters.end(), generator);

        pdf::PDFTextLayout layout;
        for (const Character& character : characters)
        {
            pdf::PDFTextCharacterInfo info;
            info.character = 'x';
            info.advance = character.advance;
            info.fontSize = character.fontSize;
            info.outline.addRect(QRectF(0, 0, character.advance, character.fontSize));
            info.matrix.translate(character.position.x(), character.position.y());
            layout.addCharacter(info);
        }
        layout.perform();

        std::vector<Line> lines;
        for (const pdf::PDFTextBlock& block : layout.getTextBlocks())
        {
            for (const pdf::PDFTextLine& textLine : block.getLines())
            {
                Line line;
                for (const pdf::TextCharacter& character : textLine.getCharacters())
                {
                    line.emplace_back(character.position.x(), character.position.y());
                }
                std::sort(line.begin(), line.end());
                lines.push_back(std::move(line));
            }
        }
        std::sort(lines.begin(), lines.end());

        // Expected lines - components of the characters connected with some of their nearest characters
        std::vector<size_t> components(characterCount);
        std::iota(components.begin(), components.end(), size_t(0));
        auto findComponent = [&components](size_t index)
        {
            while (components[index] != index)
            {
                index = components[index];
            }
            return index;
        };

        size_t connectionCount = 0;
        for (size_t i = 0; i < characterCount; ++i)
        {
            std::vector<std::pair<pdf::PDFReal, size_t>> distances;
            for (size_t j = 0; j < characterCount; ++j)
            {
                if (i != j)
                {
                    const pdf::PDFReal dx = characters[j].position.x() - characters[i].position.x();
                    const pdf::PDFReal dy = characters[j].position.y() - characters[i].position.y();
                    distances.emplace_back(dx * dx + dy * dy, j);
                }
            }
            std::sort(distances.begin(), distances.end());
            distances.resize(qMin(distances.size(), settings.samples));

            for (const auto& [squaredDistance, j] : distances)
            {
                const pdf::PDFReal maximalDistance = settings.distanceSensitivity * characters[i].advance;
                const pdf::PDFReal fontSizeMax = qMax(characters[i].fontSize, characters[j].fontSize);
                const pdf::PDFReal fontSizeMin = qMin(characters[i].fontSize, characters[j].fontSize);

                if (squaredDistance < maximalDistance * maximalDistance &&
                    std::fabs(characters[i].position.y() - characters[j].position.y()) < fontSizeMin * settings.charactersOnLineSensitivity &&
                    fontSizeMax / fontSizeMin < settings.fontSensitivity)
                {
                    components[findComponent(i)] = findComponent(j);
                    ++connectionCount;
                }
            }
        }

        std::map<size_t, Line> expectedLineMap;
        for (size_t i = 0; i < characterCount; ++i)
        {
            expectedLineMap[findComponent(i)].emplace_back(characters[i].position.x(), characters[i].position.y());
        }

        std::vector<Line> expectedLines;
        for (auto& item : expectedLineMap)
        {
            std::sort(item.second.begin(), item.second.end());
            expectedLines.push_back(std::move(item.second));
        }
        std::sort(expectedLines.begin(), expectedLines.end());

        QCOMPARE(lines.size(), expectedLines.size());
        QVERIFY2(lines == expectedLines, qPrintable(QString("Different lines for %1 characters.").arg(characterCount)));

        if (characterCount >= 100)
        {
            // The test is not trivial - characters are connected to lines, but not to a single one,
            // and a lot of the nearest characters do not belong to the line of the character
            QVERIFY(lines.size() > characterCount / 20);
            QVERIFY(lines.size() < characterCount / 2);
            QVERIFY(connectionCount > characterCount);
            QVERIFY(connectionCount < characterCount * settings.samples * 3 / 4);
        }
    }
}

void TextLayoutTest::boundingBox()
{
    // Null rectangle is not a box
    const pdf::PDFTextBoundingBox emptyBox{ QRectF() };
    QVERIFY(emptyBox.boundingRect().isNull());
    QVERIFY(emptyBox.toPath().isEmpty());
    QVERIFY(!emptyBox.contains(QPointF(0, 0)));
    QVERIFY(pdf::PDFTextBoundingBox().boundingRect().isNull());

    pdf::PDFTextBoundingBox box(QRectF(10, 20, 30, 40));
    QCOMPARE(box.getPoints()[0], QPointF(10, 20));
    QCOMPARE(box.getPoints()[1], QPointF(40, 20));
    QCOMPARE(box.getPoints()[2], QPointF(40, 60));
    QCOMPARE(box.getPoints()[3], QPointF(10, 60));
    QCOMPARE(box.boundingRect(), QRectF(10, 20, 30, 40));
    QCOMPARE(box.toPath().boundingRect(), QRectF(10, 20, 30, 40));
    QVERIFY(box.contains(QPointF(25, 40)));
    QVERIFY(!box.contains(QPointF(45, 40)));
    QVERIFY(!box.contains(QPointF(25, 65)));

    // Rotated box - corners of its bounding rectangle are not in the box
    QTransform matrix;
    matrix.translate(25, 40);
    matrix.rotate(45);
    matrix.translate(-25, -40);
    box.applyTransform(matrix);

    const QRectF boundingRect = box.boundingRect();
    QVERIFY(boundingRect.width() > 49.0 && boundingRect.width() < 50.0);
    QVERIFY(qAbs(boundingRect.center().x() - 25.0) < 1e-9 && qAbs(boundingRect.center().y() - 40.0) < 1e-9);
    QVERIFY(box.contains(QPointF(25, 40)));
    QVERIFY(box.contains(matrix.map(QPointF(11, 21))));
    QVERIFY(!box.contains(matrix.map(QPointF(9, 21))));
    QVERIFY(!box.contains(boundingRect.topLeft() + QPointF(1, 1)));
    QVERIFY(!box.contains(boundingRect.bottomRight() - QPointF(1, 1)));
    QCOMPARE(box.toPath().boundingRect(), boundingRect);
    QVERIFY(box.toPath().contains(QPointF(25, 40)));

    // Character without an outline has a box of zero size at its position
    pdf::PDFTextLayout layout;
    pdf::PDFTextCharacterInfo info;
    info.character = 'A';
    info.advance = 8.0;
    info.fontSize = 10.0;
    info.matrix.translate(100, 200);
    layout.addCharacter(info);
    layout.perform();

    QCOMPARE(layout.getTextBlocks().size(), size_t(1));
    const pdf::TextCharacter& character = layout.getTextBlocks().front().getLines().front().getCharacters().front();
    QCOMPARE(character.boundingBox.boundingRect(), QRectF(100, 200, 0, 0));
    QVERIFY(layout.getTextBlocks().front().getBoundingBox().boundingRect().isNull());
    QVERIFY(!layout.isHoveringOverTextBlock(QPointF(100, 200)));
}

void TextLayoutTest::layoutDoesNotHoldCharactersTwice()
{
    pdf::PDFTextLayout layout = createPageLayout(8);

    size_t characterCount = 0;
    for (const pdf::PDFTextBlock& block : layout.getTextBlocks())
    {
        for (const pdf::PDFTextLine& line : block.getLines())
        {
            characterCount += line.getCharacters().size();
        }
    }
    QCOMPARE(characterCount, size_t(8 * 3 * 11 + 10));

    // Characters are stored only in the text blocks, and character is a plain structure
    const qint64 charactersSize = qint64(characterCount * sizeof(pdf::TextCharacter));
    QVERIFY(layout.getMemoryConsumptionEstimate() >= charactersSize);
    QVERIFY(layout.getMemoryConsumptionEstimate() < charactersSize * 3 / 2);
    QVERIFY(std::is_trivially_copyable_v<pdf::TextCharacter>);
}

void TextLayoutTest::selectionByRectangle()
{
    pdf::PDFTextLayout layout;
    addCharacter(layout, 'A', QPointF(100, 100), 0);
    addCharacter(layout, 'B', QPointF(108, 100), 0);
    addCharacter(layout, 'C', QPointF(116, 100), 0);
    addCharacter(layout, 'D', QPointF(100, 88), 0);
    addCharacter(layout, 'E', QPointF(108, 88), 0);
    addCharacter(layout, 'F', QPointF(116, 88), 0);
    layout.perform();

    QCOMPARE(layout.getTextBlocks().size(), size_t(1));
    QCOMPARE(layout.getTextBlocks().front().getLines().size(), size_t(2));
    QCOMPARE(layout.getTextBlocks().front().getBoundingBox().boundingRect(), QRectF(100, 88, 23, 22));
    QCOMPARE(layout.getTextBlocks().front().getTopLeft(), QPointF(100, 88));

    QVERIFY(layout.isHoveringOverTextBlock(QPointF(110, 95)));
    QVERIFY(!layout.isHoveringOverTextBlock(QPointF(130, 95)));

    // Both points are on the first line
    pdf::PDFTextSelection selection = layout.createTextSelection(0, QPointF(107.5, 103), QPointF(117, 107));
    QVERIFY(!selection.isEmpty());
    QCOMPARE(layout.getTextFromSelection(selection, 0).trimmed(), QString("B"));

    // Selection from the middle of the first line to the middle of the second line
    selection = layout.createTextSelection(0, QPointF(107.5, 105), QPointF(115.5, 93));
    QVERIFY(!selection.isEmpty());
    QCOMPARE(layout.getTextFromSelection(selection, 0).trimmed(), QString("BC DE"));

    // Rectangle outside of the text block, and rectangle, which only touches the block
    QVERIFY(layout.createTextSelection(0, QPointF(300, 300), QPointF(320, 320)).isEmpty());
    QVERIFY(layout.createTextSelection(0, QPointF(123, 80), QPointF(140, 120)).isEmpty());

    // Text with an angle of 30 degrees. Positions and points of the selection are
    // defined in the coordinate system of the line and mapped to the page.
    QTransform lineToPage;
    lineToPage.rotate(-30);

    pdf::PDFTextLayout rotatedLayout;
    for (int i = 0; i < 4; ++i)
    {
        addCharacter(rotatedLayout, QChar(char16_t(u'A' + i)), lineToPage.map(QPointF(200.0 + 8.0 * i, 300.0)), 30);
    }
    rotatedLayout.perform();

    QCOMPARE(rotatedLayout.getTextBlocks().size(), size_t(1));
    const pdf::PDFTextBlock& rotatedBlock = rotatedLayout.getTextBlocks().front();
    QCOMPARE(rotatedBlock.getLines().front().getCharacters().front().angle, 30.0);

    // Bounding box of the block is a rotated rectangle 31 x 10
    const pdf::PDFTextBoundingBox originalBox = rotatedBlock.getBoundingBox();
    QVERIFY(originalBox.boundingRect().width() > 31.0);
    QVERIFY(originalBox.boundingRect().height() > 24.0);
    QVERIFY(qAbs(QLineF(originalBox.getPoints()[0], originalBox.getPoints()[1]).length() - 31.0) < 1e-9);
    QVERIFY(qAbs(QLineF(originalBox.getPoints()[0], originalBox.getPoints()[3]).length() - 10.0) < 1e-9);
    QVERIFY(rotatedLayout.isHoveringOverTextBlock(lineToPage.map(QPointF(215, 305))));
    QVERIFY(!rotatedLayout.isHoveringOverTextBlock(lineToPage.map(QPointF(215, 312))));

    selection = rotatedLayout.createTextSelection(0, lineToPage.map(QPointF(207.5, 303)), lineToPage.map(QPointF(217, 307)));
    QVERIFY(!selection.isEmpty());
    QCOMPARE(rotatedLayout.getTextFromSelection(selection, 0).trimmed(), QString("B"));

    selection = rotatedLayout.createTextSelection(0, lineToPage.map(QPointF(207.5, 303)), lineToPage.map(QPointF(225, 307)));
    QCOMPARE(rotatedLayout.getTextFromSelection(selection, 0).trimmed(), QString("BC"));

    // Selection rotates the block and then it rotates it back, layout is not changed
    for (size_t i = 0; i < originalBox.getPoints().size(); ++i)
    {
        const QPointF difference = rotatedBlock.getBoundingBox().getPoints()[i] - originalBox.getPoints()[i];
        QVERIFY(qAbs(difference.x()) < 1e-9 && qAbs(difference.y()) < 1e-9);
    }
}

void TextLayoutTest::storageRoundTrip()
{
    const pdf::PDFTextLayout original = createPageLayout(6);
    QCOMPARE(original.getTextBlocks().size(), size_t(7));

    pdf::PDFTextLayoutStorage storage(3);
    QCOMPARE(storage.getCount(), size_t(3));
    const qint64 emptyStorageSize = storage.getMemoryConsumptionEstimate();

    storage.setTextLayout(1, original);

    QVERIFY(storage.getTextLayout(0).getTextBlocks().empty());
    QVERIFY(storage.getTextLayout(2).getTextBlocks().empty());
    QVERIFY(storage.getTextLayout(-1).getTextBlocks().empty());
    QVERIFY(storage.getTextLayout(3).getTextBlocks().empty());

    const pdf::PDFTextLayout layout = storage.getTextLayout(1);
    compareLayouts(layout, original);

    // Text and selections are the same as in the original layout
    for (size_t blockIndex = 0; blockIndex < original.getTextBlocks().size(); ++blockIndex)
    {
        const QString text = layout.getTextFromSelection(layout.selectBlock(blockIndex, 1, Qt::yellow), 1);
        QVERIFY(!text.trimmed().isEmpty());
        QCOMPARE(text, original.getTextFromSelection(original.selectBlock(blockIndex, 1, Qt::yellow), 1));
    }

    const pdf::PDFFindResults findResults = storage.find(QString("ABCD"), Qt::CaseSensitive, pdf::PDFTextFlow::SeparateBlocks);
    QCOMPARE(findResults.size(), size_t(1));
    QCOMPARE(findResults.front().textSelectionItems.front().first.pageIndex, pdf::PDFInteger(1));

    // Page of 208 characters has only a few shapes of characters, so the compact
    // layout is much smaller, than the characters of the layout
    const qint64 storedSize = storage.getMemoryConsumptionEstimate() - emptyStorageSize;
    QVERIFY(storedSize > 0);
    QVERIFY2(storedSize < qint64(208 * sizeof(pdf::TextCharacter) / 3), qPrintable(QString::number(storedSize)));

    // Page far from the origin of the coordinate system (single precision has a step
    // of 0.0625 there, but the geometry is stored relative to the first character),
    // with a skewed character and with characters without an outline
    const QPointF offset(1000000.3, 1000000.7);
    const pdf::PDFTextLayout specialLayout = createSpecialLayout(offset);
    QCOMPARE(specialLayout.getTextBlocks().size(), size_t(2));

    for (const pdf::PDFTextBlock& block : specialLayout.getTextBlocks())
    {
        QCOMPARE(block.getLines().size(), size_t(1));
        const pdf::PDFTextLine& line = block.getLines().front();

        if (line.getCharacters().front().character == QChar('p'))
        {
            // Line has no bounding box, its top left point is the position of the last character
            QCOMPARE(line.getCharacters().size(), size_t(4));
            QVERIFY(line.getBoundingBox().boundingRect().isNull());
            QCOMPARE(line.getTopLeft(), offset + QPointF(124, 300));
        }
        else
        {
            QCOMPARE(line.getCharacters().size(), size_t(3));
            const pdf::PDFTextBoundingBox::Points& points = line.getCharacters().front().boundingBox.getPoints();
            QVERIFY(qAbs(points[0].y() - points[1].y()) > 0.1);
        }
    }

    storage.setTextLayout(2, specialLayout);
    compareLayouts(storage.getTextLayout(2), specialLayout);
    storage.setTextLayout(2, pdf::PDFTextLayout());

    // Layout of the page can be replaced, also by an empty layout
    storage.setTextLayout(1, createPageLayout(1));
    QCOMPARE(storage.getTextLayout(1).getTextBlocks().size(), size_t(2));
    storage.setTextLayout(1, pdf::PDFTextLayout());
    QVERIFY(storage.getTextLayout(1).getTextBlocks().empty());
    QCOMPARE(storage.getMemoryConsumptionEstimate(), emptyStorageSize);
}

void TextLayoutTest::storageIsFilledFromMultipleThreads()
{
    constexpr int PAGE_COUNT = 256;
    constexpr int THREAD_COUNT = 8;

    // Layouts of the pages are set without any locking, each thread sets its own pages
    pdf::PDFTextLayoutStorage storage(PAGE_COUNT);
    std::vector<pdf::PDFTextLayout> layouts;
    for (int i = 0; i < 4; ++i)
    {
        layouts.push_back(createPageLayout(i + 1));
    }

    std::vector<std::thread> threads;
    for (int threadIndex = 0; threadIndex < THREAD_COUNT; ++threadIndex)
    {
        threads.emplace_back([&storage, &layouts, threadIndex]()
        {
            for (int pageIndex = threadIndex; pageIndex < PAGE_COUNT; pageIndex += THREAD_COUNT)
            {
                storage.setTextLayout(pageIndex, layouts[pageIndex % layouts.size()]);
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    for (int pageIndex = 0; pageIndex < PAGE_COUNT; ++pageIndex)
    {
        QCOMPARE(storage.getTextLayout(pageIndex).getTextBlocks().size(), layouts[pageIndex % layouts.size()].getTextBlocks().size());
    }

    // Copy of the storage shares the data of the pages
    const pdf::PDFTextLayoutStorage copy = storage;
    compareLayouts(copy.getTextLayout(PAGE_COUNT - 1), layouts[(PAGE_COUNT - 1) % layouts.size()]);
}

double TextLayoutTest::getGeometrySum(const pdf::PDFTextLayout& textLayout, TextLayoutStatistics* statistics, QCryptographicHash* textHash)
{
    double geometrySum = 0.0;

    for (const pdf::PDFTextBlock& block : textLayout.getTextBlocks())
    {
        const QRectF blockRect = block.getBoundingBox().boundingRect();
        geometrySum += blockRect.x() + blockRect.y() + blockRect.width() + blockRect.height();

        for (const pdf::PDFTextLine& line : block.getLines())
        {
            const QRectF lineRect = line.getBoundingBox().boundingRect();
            geometrySum += lineRect.x() + lineRect.y() + lineRect.width() + lineRect.height();

            for (const pdf::TextCharacter& character : line.getCharacters())
            {
                const QRectF characterRect = character.boundingBox.boundingRect();
                geometrySum += characterRect.x() + characterRect.y() + characterRect.width() + characterRect.height();
                geometrySum += character.position.x() + character.position.y() + character.advance + character.fontSize + character.angle;

                if (textHash)
                {
                    const char16_t unicode = character.character.unicode();
                    textHash->addData(QByteArrayView(reinterpret_cast<const char*>(&unicode), sizeof(unicode)));
                }
            }

            if (statistics)
            {
                ++statistics->lineCount;
                statistics->characterCount += line.getCharacters().size();
            }
        }

        if (statistics)
        {
            ++statistics->blockCount;
        }
    }

    return geometrySum;
}

QByteArray TextLayoutTest::getStructureDump(const pdf::PDFTextLayout& textLayout)
{
    QByteArray dump;

    auto formatRect = [](const QRectF& rect)
    {
        return QByteArray::number(rect.x(), 'f', 4) + ' ' + QByteArray::number(rect.y(), 'f', 4) + ' ' +
               QByteArray::number(rect.width(), 'f', 4) + ' ' + QByteArray::number(rect.height(), 'f', 4);
    };

    for (const pdf::PDFTextBlock& block : textLayout.getTextBlocks())
    {
        dump += "B " + formatRect(block.getBoundingBox().boundingRect()) + '\n';

        for (const pdf::PDFTextLine& line : block.getLines())
        {
            // Sum of the positions detects a change of the characters of the line,
            // even if the text and the bounding box of the line are the same
            QString text;
            double positionSum = 0.0;
            for (const pdf::TextCharacter& character : line.getCharacters())
            {
                text += character.character;
                positionSum += character.position.x() + character.position.y();
            }

            dump += "L " + formatRect(line.getBoundingBox().boundingRect()) + ' ' + QByteArray::number(positionSum, 'f', 4) + ' ' + text.toUtf8() + '\n';
        }
    }

    return dump;
}

void TextLayoutTest::documentBenchmark()
{
    // Measures creation of the text layout of a whole document, as it is done
    // by the asynchronous text layout compiler. Run only on demand.
    const QString fileName = qEnvironmentVariable("PDF4QT_TEXT_LAYOUT_BENCHMARK");
    if (fileName.isEmpty())
    {
        QSKIP("Set PDF4QT_TEXT_LAYOUT_BENCHMARK to a PDF file to run the benchmark.");
    }

    const bool isSingleThreaded = qEnvironmentVariable("PDF4QT_TEXT_LAYOUT_BENCHMARK_THREADS") == QLatin1String("single");
    pdf::PDFExecutionPolicy::setStrategy(isSingleThreaded ? pdf::PDFExecutionPolicy::Strategy::SingleThreaded : pdf::PDFExecutionPolicy::Strategy::PageMultithreaded);

    pdf::PDFDocumentReader reader(nullptr, nullptr, false, false);
    pdf::PDFDocument document = reader.readFromFile(fileName);
    QCOMPARE(reader.getReadingResult(), pdf::PDFDocumentReader::Result::OK);

    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Export, nullptr);
    pdf::PDFCMSGeneric cms;
    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFMeshQualitySettings meshQualitySettings;
    pdf::PDFModifiedDocument modifiedDocument(&document, &optionalContentActivity);
    fontCache.setDocument(modifiedDocument);
    fontCache.setCacheShrinkEnabled(this, false);

    const pdf::PDFCatalog* catalog = document.getCatalog();
    const pdf::PDFInteger pageCount = pdf::PDFInteger(catalog->getPageCount());

    std::atomic<qint64> processTime = 0;
    std::atomic<qint64> layoutTime = 0;
    std::atomic<qint64> storeTime = 0;

    const qint64 memoryBefore = getPrivateMemoryUsage();

    QElapsedTimer totalTimer;
    totalTimer.start();

    pdf::PDFTextLayoutStorage storage(pageCount);
    std::vector<double> directGeometrySums(pageCount, 0.0);

    const QString dumpFileName = qEnvironmentVariable("PDF4QT_TEXT_LAYOUT_BENCHMARK_DUMP");
    std::vector<QByteArray> structureDumps(dumpFileName.isEmpty() ? 0 : pageCount);
    auto generateTextLayout = [&](pdf::PDFInteger pageIndex)
    {
        const pdf::PDFPage* page = catalog->getPage(pageIndex);

        QElapsedTimer timer;
        timer.start();
        pdf::PDFTextLayoutGenerator generator(pdf::PDFRenderer::getDefaultFeatures(), page, &document, &fontCache, &cms, &optionalContentActivity, QTransform(), meshQualitySettings);
        generator.processContents();
        processTime += timer.nsecsElapsed();

        timer.restart();
        pdf::PDFTextLayout textLayout = generator.createTextLayout();
        layoutTime += timer.nsecsElapsed();

        timer.restart();
        storage.setTextLayout(pageIndex, textLayout);
        storeTime += timer.nsecsElapsed();

        directGeometrySums[pageIndex] = getGeometrySum(textLayout, nullptr, nullptr);

        if (!structureDumps.empty())
        {
            structureDumps[pageIndex] = getStructureDump(textLayout);
        }
    };

    auto pageRange = pdf::PDFIntegerRange<pdf::PDFInteger>(0, pageCount);
    pdf::PDFExecutionPolicy::execute(pdf::PDFExecutionPolicy::Scope::Page, pageRange.begin(), pageRange.end(), generateTextLayout);

    const qint64 totalTime = totalTimer.nsecsElapsed();
    const qint64 memoryAfter = getPrivateMemoryUsage();
    fontCache.setCacheShrinkEnabled(this, true);

    if (!dumpFileName.isEmpty())
    {
        QFile dumpFile(dumpFileName);
        QVERIFY(dumpFile.open(QFile::WriteOnly | QFile::Truncate));
        for (pdf::PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
        {
            dumpFile.write("P " + QByteArray::number(pageIndex) + '\n');
            dumpFile.write(structureDumps[pageIndex]);
        }
    }

    // Read everything back - both to measure it, and to get a fingerprint
    // of the result, which can be compared between two implementations.
    QElapsedTimer readTimer;
    readTimer.start();

    TextLayoutStatistics statistics;
    double geometrySum = 0.0;
    QCryptographicHash textHash(QCryptographicHash::Sha1);
    for (pdf::PDFInteger pageIndex = 0; pageIndex < pageCount; ++pageIndex)
    {
        geometrySum += getGeometrySum(storage.getTextLayout(pageIndex), &statistics, &textHash);
    }
    const qint64 readTime = readTimer.nsecsElapsed();

    QElapsedTimer findTimer;
    findTimer.start();
    const pdf::PDFFindResults findResults = storage.find(QString("a"), Qt::CaseInsensitive, pdf::PDFTextFlow::FlowFlags(pdf::PDFTextFlow::SeparateBlocks) | pdf::PDFTextFlow::RemoveSoftHyphen);
    const qint64 findTime = findTimer.nsecsElapsed();

    auto toMilliseconds = [](qint64 nanoseconds) { return double(nanoseconds) / 1000000.0; };
    auto toMegabytes = [](qint64 bytes) { return double(bytes) / (1024.0 * 1024.0); };

    qInfo().noquote() << QString("Pages: %1, blocks: %2, lines: %3, characters: %4").arg(pageCount).arg(statistics.blockCount).arg(statistics.lineCount).arg(statistics.characterCount);
    qInfo().noquote() << QString("Total wall time: %1 ms (%2)").arg(toMilliseconds(totalTime), 0, 'f', 1).arg(isSingleThreaded ? QString("single threaded") : QString("multithreaded"));
    qInfo().noquote() << QString("  content processing (summed over threads): %1 ms").arg(toMilliseconds(processTime), 0, 'f', 1);
    qInfo().noquote() << QString("  layout algorithm (summed over threads):   %1 ms").arg(toMilliseconds(layoutTime), 0, 'f', 1);
    qInfo().noquote() << QString("  setTextLayout (summed over threads):      %1 ms").arg(toMilliseconds(storeTime), 0, 'f', 1);
    qInfo().noquote() << QString("Storage: %1 MB, private memory growth: %2 MB").arg(toMegabytes(storage.getMemoryConsumptionEstimate()), 0, 'f', 1).arg(toMegabytes(memoryAfter - memoryBefore), 0, 'f', 1);
    qInfo().noquote() << QString("Reading of all layouts: %1 ms, find: %2 ms (%3 results)").arg(toMilliseconds(readTime), 0, 'f', 1).arg(toMilliseconds(findTime), 0, 'f', 1).arg(findResults.size());
    qInfo().noquote() << QString("Text fingerprint: %1").arg(QString::fromLatin1(textHash.result().toHex()));
    qInfo().noquote() << QString("Geometry sum: %1 (created layouts), %2 (layouts from the storage)").arg(std::accumulate(directGeometrySums.cbegin(), directGeometrySums.cend(), 0.0), 0, 'f', 3).arg(geometrySum, 0, 'f', 3);
}

QTEST_MAIN(TextLayoutTest)

#include "tst_textlayouttest.moc"
