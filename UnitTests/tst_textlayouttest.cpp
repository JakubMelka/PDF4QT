#include "pdftextlayout.h"

#include <QtTest>

class TextLayoutTest : public QObject
{
    Q_OBJECT

private slots:
    void nearbyAnglesShareSelectionGeometry();
    void angleGroupsDoNotGrowTransitively();
    void cacheRetainsRecentlyUsedPages();
};

static void addCharacter(pdf::PDFTextLayout& layout, QChar character, QPointF position, qreal angle)
{
    pdf::PDFTextCharacterInfo info;
    info.character = character;
    info.advance = 8.0;
    info.fontSize = 10.0;
    info.outline.addRect(QRectF(0, 0, 7, 10));
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

QTEST_GUILESS_MAIN(TextLayoutTest)

#include "tst_textlayouttest.moc"
