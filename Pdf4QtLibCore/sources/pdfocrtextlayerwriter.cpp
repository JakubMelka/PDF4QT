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

#include "pdfocrtextlayerwriter.h"
#include "pdfocrproject.h"
#include "pdfocrpagepreparer.h"
#include "pdfdocument.h"
#include "pdfdocumentbuilder.h"
#include "pdfpage.h"
#include "pdfcatalog.h"
#include "pdfstreamfilters.h"
#include "pdfconstants.h"

#include <QUuid>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QtMath>

#include <cmath>

namespace pdf
{

// Glyphless TrueType font "GlyphLessFont" (572 bytes) from the Tesseract OCR
// project (tessdata/pdf.ttf), Copyright 2011 Google Inc., licensed under the
// Apache License, Version 2.0. The font has two glyphs; glyph 1 is a rectangle
// covering half of the em square horizontally and the whole em square vertically.
// All CIDs are mapped to glyph 1 by the CIDToGIDMap.
static const unsigned char s_glyphlessFont[] =
{
    0x00, 0x01, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x80, 0x00, 0x03, 0x00, 0x20, 0x4F, 0x53, 0x2F, 0x32,
    0x56, 0xDE, 0xC8, 0x94, 0x00, 0x00, 0x01, 0x28, 0x00, 0x00, 0x00, 0x60, 0x63, 0x6D, 0x61, 0x70,
    0x00, 0x0A, 0x00, 0x34, 0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x1E, 0x67, 0x6C, 0x79, 0x66,
    0x15, 0x22, 0x41, 0x24, 0x00, 0x00, 0x01, 0xB8, 0x00, 0x00, 0x00, 0x18, 0x68, 0x65, 0x61, 0x64,
    0x0B, 0x78, 0xF1, 0x65, 0x00, 0x00, 0x00, 0xAC, 0x00, 0x00, 0x00, 0x36, 0x68, 0x68, 0x65, 0x61,
    0x0C, 0x02, 0x04, 0x02, 0x00, 0x00, 0x00, 0xE4, 0x00, 0x00, 0x00, 0x24, 0x68, 0x6D, 0x74, 0x78,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x88, 0x00, 0x00, 0x00, 0x08, 0x6C, 0x6F, 0x63, 0x61,
    0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x01, 0xB0, 0x00, 0x00, 0x00, 0x06, 0x6D, 0x61, 0x78, 0x70,
    0x00, 0x04, 0x00, 0x05, 0x00, 0x00, 0x01, 0x08, 0x00, 0x00, 0x00, 0x20, 0x6E, 0x61, 0x6D, 0x65,
    0xF2, 0xEB, 0x16, 0xDA, 0x00, 0x00, 0x01, 0xD0, 0x00, 0x00, 0x00, 0x4B, 0x70, 0x6F, 0x73, 0x74,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x1C, 0x00, 0x00, 0x00, 0x20, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0xB0, 0x94, 0x71, 0x10, 0x5F, 0x0F, 0x3C, 0xF5, 0x04, 0x07, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xCF, 0x9A, 0xFC, 0x6E, 0x00, 0x00, 0x00, 0x00, 0xD4, 0xC3, 0xA7, 0xF2,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x08, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x04,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x01, 0x90, 0x00, 0x05,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x47, 0x4F, 0x4F, 0x47, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x14, 0x00, 0x06, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x31, 0x21, 0x11, 0x21, 0x04, 0x00, 0xFC, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x03, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05, 0x00, 0x16,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x0B, 0x00, 0x16, 0x00, 0x03,
    0x00, 0x01, 0x04, 0x09, 0x00, 0x05, 0x00, 0x16, 0x00, 0x00, 0x00, 0x56, 0x00, 0x65, 0x00, 0x72,
    0x00, 0x73, 0x00, 0x69, 0x00, 0x6F, 0x00, 0x6E, 0x00, 0x20, 0x00, 0x31, 0x00, 0x2E, 0x00, 0x30,
    0x56, 0x65, 0x72, 0x73, 0x69, 0x6F, 0x6E, 0x20, 0x31, 0x2E, 0x30, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Width of the glyph in 1/1000 em (half of the em square)
static constexpr int GLYPH_WIDTH = 500;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

QByteArray PDFOCRTextLayerWriter::getGlyphlessFontProgram()
{
    return QByteArray(reinterpret_cast<const char*>(s_glyphlessFont), int(sizeof(s_glyphlessFont)));
}

QByteArray PDFOCRTextLayerWriter::getToUnicodeCMap()
{
    return QByteArrayLiteral(
        "/CIDInit /ProcSet findresource begin\n"
        "12 dict begin\n"
        "begincmap\n"
        "/CIDSystemInfo\n"
        "<<\n"
        "  /Registry (Adobe)\n"
        "  /Ordering (UCS)\n"
        "  /Supplement 0\n"
        ">> def\n"
        "/CMapName /Adobe-Identity-UCS def\n"
        "/CMapType 2 def\n"
        "1 begincodespacerange\n"
        "<0000> <FFFF>\n"
        "endcodespacerange\n"
        "1 beginbfrange\n"
        "<0000> <FFFF> <0000>\n"
        "endbfrange\n"
        "endcmap\n"
        "CMapName currentdict /CMap defineresource pop\n"
        "end\n"
        "end\n");
}

QByteArray PDFOCRTextLayerWriter::encodeText(const QString& text, int* codeUnitCount)
{
    QByteArray result;
    int count = 0;

    for (int i = 0; i < text.size(); ++i)
    {
        const QChar character = text[i];

        if (character.isNull())
        {
            continue;
        }

        if (character.isHighSurrogate())
        {
            if (i + 1 < text.size() && text[i + 1].isLowSurrogate())
            {
                // Surrogate pair is written as two CIDs (UTF-16BE)
                result += QByteArray::number(int(character.unicode()), 16).rightJustified(4, '0').toUpper();
                result += QByteArray::number(int(text[i + 1].unicode()), 16).rightJustified(4, '0').toUpper();
                count += 2;
                ++i;
            }
            continue;
        }

        if (character.isLowSurrogate())
        {
            continue;
        }

        result += QByteArray::number(int(character.unicode()), 16).rightJustified(4, '0').toUpper();
        ++count;
    }

    if (codeUnitCount)
    {
        *codeUnitCount = count;
    }

    return result;
}

QByteArray PDFOCRTextLayerWriter::formatNumber(double value)
{
    if (!std::isfinite(value))
    {
        value = 0.0;
    }

    QByteArray text = QByteArray::number(value, 'f', 4);
    if (text.contains('.'))
    {
        while (text.endsWith('0'))
        {
            text.chop(1);
        }
        if (text.endsWith('.'))
        {
            text.chop(1);
        }
    }

    if (text == "-0")
    {
        text = "0";
    }

    return text;
}

double PDFOCRTextLayerWriter::computeHorizontalScaling(const PDFOCRWord& word)
{
    int codeUnits = 0;
    encodeText(word.text, &codeUnits);

    const double fontSize = word.quad.height();
    const double width = word.quad.width();
    if (codeUnits <= 0 || fontSize <= 0.0 || width <= 0.0)
    {
        return 100.0;
    }

    return 100.0 * width / (codeUnits * fontSize * GLYPH_WIDTH / 1000.0);
}

bool PDFOCRTextLayerWriter::isExtremeScaling(double horizontalScaling)
{
    return horizontalScaling < 20.0 || horizontalScaling > 500.0;
}

PDFObjectReference PDFOCRTextLayerWriter::createStream(PDFDocumentBuilder* builder, PDFDictionary dictionary, const QByteArray& data, bool compress)
{
    QByteArray content = data;
    if (compress)
    {
        content = PDFFlateDecodeFilter::compress(data);
        dictionary.setEntry(PDFInplaceOrMemoryString("Filter"), PDFObject::createName("FlateDecode"));
    }
    dictionary.setEntry(PDFInplaceOrMemoryString("Length"), PDFObject::createInteger(content.size()));
    return builder->addObject(PDFObject::createStream(std::make_shared<PDFStream>(std::move(dictionary), std::move(content))));
}

PDFObjectReference PDFOCRTextLayerWriter::createGlyphlessFont(PDFDocumentBuilder* builder, bool compress)
{
    // Font program
    const QByteArray fontProgram = getGlyphlessFontProgram();
    PDFDictionary fontFileDictionary;
    fontFileDictionary.setEntry(PDFInplaceOrMemoryString("Length1"), PDFObject::createInteger(fontProgram.size()));
    const PDFObjectReference fontFileReference = createStream(builder, std::move(fontFileDictionary), fontProgram, compress);

    // Font descriptor
    PDFObjectFactory descriptorFactory;
    descriptorFactory.beginDictionary();
    descriptorFactory.beginDictionaryItem("Type");
    descriptorFactory << WrapName("FontDescriptor");
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("FontName");
    descriptorFactory << WrapName("GlyphLessFont");
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("Flags");
    descriptorFactory << PDFInteger(5);
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("FontBBox");
    descriptorFactory << QRectF(0, 0, GLYPH_WIDTH, 1000);
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("ItalicAngle");
    descriptorFactory << PDFInteger(0);
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("Ascent");
    descriptorFactory << PDFInteger(1000);
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("Descent");
    descriptorFactory << PDFInteger(-1);
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("CapHeight");
    descriptorFactory << PDFInteger(1000);
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("StemV");
    descriptorFactory << PDFInteger(80);
    descriptorFactory.endDictionaryItem();
    descriptorFactory.beginDictionaryItem("FontFile2");
    descriptorFactory << fontFileReference;
    descriptorFactory.endDictionaryItem();
    descriptorFactory.endDictionary();
    const PDFObjectReference descriptorReference = builder->addObject(descriptorFactory.takeObject());

    // CIDToGIDMap: all CIDs are mapped to glyph 1
    QByteArray cidToGidMap;
    cidToGidMap.resize(2 * 65536);
    for (int i = 0; i < 65536; ++i)
    {
        cidToGidMap[2 * i] = 0;
        cidToGidMap[2 * i + 1] = 1;
    }
    const PDFObjectReference cidToGidMapReference = createStream(builder, PDFDictionary(), cidToGidMap, compress);

    // ToUnicode
    const PDFObjectReference toUnicodeReference = createStream(builder, PDFDictionary(), getToUnicodeCMap(), compress);

    // CIDFontType2
    PDFObjectFactory cidFontFactory;
    cidFontFactory.beginDictionary();
    cidFontFactory.beginDictionaryItem("Type");
    cidFontFactory << WrapName("Font");
    cidFontFactory.endDictionaryItem();
    cidFontFactory.beginDictionaryItem("Subtype");
    cidFontFactory << WrapName("CIDFontType2");
    cidFontFactory.endDictionaryItem();
    cidFontFactory.beginDictionaryItem("BaseFont");
    cidFontFactory << WrapName("GlyphLessFont");
    cidFontFactory.endDictionaryItem();
    cidFontFactory.beginDictionaryItem("CIDSystemInfo");
    cidFontFactory.beginDictionary();
    cidFontFactory.beginDictionaryItem("Registry");
    cidFontFactory << WrapString("Adobe");
    cidFontFactory.endDictionaryItem();
    cidFontFactory.beginDictionaryItem("Ordering");
    cidFontFactory << WrapString("Identity");
    cidFontFactory.endDictionaryItem();
    cidFontFactory.beginDictionaryItem("Supplement");
    cidFontFactory << PDFInteger(0);
    cidFontFactory.endDictionaryItem();
    cidFontFactory.endDictionary();
    cidFontFactory.endDictionaryItem();
    cidFontFactory.beginDictionaryItem("FontDescriptor");
    cidFontFactory << descriptorReference;
    cidFontFactory.endDictionaryItem();
    cidFontFactory.beginDictionaryItem("DW");
    cidFontFactory << PDFInteger(GLYPH_WIDTH);
    cidFontFactory.endDictionaryItem();
    cidFontFactory.beginDictionaryItem("CIDToGIDMap");
    cidFontFactory << cidToGidMapReference;
    cidFontFactory.endDictionaryItem();
    cidFontFactory.endDictionary();
    const PDFObjectReference cidFontReference = builder->addObject(cidFontFactory.takeObject());

    // Type0 font
    PDFObjectFactory fontFactory;
    fontFactory.beginDictionary();
    fontFactory.beginDictionaryItem("Type");
    fontFactory << WrapName("Font");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("Subtype");
    fontFactory << WrapName("Type0");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("BaseFont");
    fontFactory << WrapName("GlyphLessFont");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("Encoding");
    fontFactory << WrapName("Identity-H");
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("DescendantFonts");
    fontFactory.beginArray();
    fontFactory << cidFontReference;
    fontFactory.endArray();
    fontFactory.endDictionaryItem();
    fontFactory.beginDictionaryItem("ToUnicode");
    fontFactory << toUnicodeReference;
    fontFactory.endDictionaryItem();
    fontFactory.endDictionary();
    return builder->addObject(fontFactory.takeObject());
}

// -------------------------------------------------------------------------
// Content stream
// -------------------------------------------------------------------------

QByteArray PDFOCRTextLayerWriter::createContentStream(const PDFOCRPageResult& result,
                                                      const QByteArray& fontKey,
                                                      bool onlyReviewed,
                                                      int* writtenWords,
                                                      QStringList* warnings,
                                                      bool markAsArtifact)
{
    QByteArray stream;
    int wordCount = 0;

    stream += "q\n";
    if (markAsArtifact)
    {
        stream += "/Artifact BMC\n";
    }

    for (const PDFOCRBlock& block : result.blocks)
    {
        bool blockOpened = false;
        double currentFontSize = -1.0;

        for (const PDFOCRLine& line : block.lines)
        {
            std::vector<const PDFOCRWord*> words;
            for (const PDFOCRWord& word : line.words)
            {
                if (!word.isUsable())
                {
                    continue;
                }

                if (onlyReviewed && word.reviewState == PDFOCRReviewState::Unreviewed)
                {
                    continue;
                }

                if (word.overlapsExcludedRegion && word.reviewState != PDFOCRReviewState::Confirmed && word.reviewState != PDFOCRReviewState::Modified)
                {
                    // Word overlapping an excluded region must be reviewed (REGION-05)
                    if (warnings)
                    {
                        *warnings << PDFTranslationContext::tr("Page %1: word '%2' overlaps an excluded region and was not written.").arg(result.pageIndex + 1).arg(word.text);
                    }
                    continue;
                }

                words.push_back(&word);
            }

            const bool rightToLeft = line.direction == PDFOCRTextDirection::RightToLeft;

            for (size_t i = 0; i < words.size(); ++i)
            {
                const PDFOCRWord& word = *words[i];

                int codeUnits = 0;
                const QByteArray encodedText = encodeText(word.text, &codeUnits);
                if (codeUnits == 0)
                {
                    continue;
                }

                const double fontSize = word.quad.height();
                const double width = word.quad.width();
                if (fontSize <= 0.0 || width <= 0.0)
                {
                    continue;
                }

                if (!blockOpened)
                {
                    stream += "BT\n3 Tr\n";
                    blockOpened = true;
                }

                // Text matrix: rotation by the angle of the writing direction, origin
                // at the bottom-left corner (or bottom-right corner for RTL text).
                const QPointF direction = word.quad.direction();
                double a = direction.x();
                double b = direction.y();
                double c = -direction.y();
                double d = direction.x();
                QPointF origin = word.quad.points[0];

                if (rightToLeft)
                {
                    a = -a;
                    b = -b;
                    origin = word.quad.points[1];
                }

                stream += formatNumber(a) + " " + formatNumber(b) + " " + formatNumber(c) + " " + formatNumber(d) + " " +
                          formatNumber(origin.x()) + " " + formatNumber(origin.y()) + " Tm\n";

                if (!qFuzzyCompare(currentFontSize, fontSize))
                {
                    stream += "/" + fontKey + " " + formatNumber(fontSize) + " Tf\n";
                    currentFontSize = fontSize;
                }

                // Horizontal scaling to fit the text into the geometry of the word
                const double horizontalScaling = 100.0 * width / (codeUnits * fontSize * GLYPH_WIDTH / 1000.0);
                stream += formatNumber(horizontalScaling) + " Tz\n";
                stream += "[<" + encodedText + ">] TJ\n";
                ++wordCount;

                if (isExtremeScaling(horizontalScaling) && warnings)
                {
                    *warnings << PDFTranslationContext::tr("Page %1: word '%2' has extreme horizontal scaling (%3 %).").arg(result.pageIndex + 1).arg(word.text).arg(qRound(horizontalScaling));
                }

                // Space between words: width of the gap to the next word
                if (i + 1 < words.size())
                {
                    const PDFOCRWord& next = *words[i + 1];
                    double gap = 0.0;
                    if (rightToLeft)
                    {
                        gap = QPointF::dotProduct(word.quad.points[0] - next.quad.points[1], direction);
                    }
                    else
                    {
                        gap = QPointF::dotProduct(next.quad.points[0] - word.quad.points[1], direction);
                    }

                    const double minimumGap = 0.05 * fontSize;
                    const double spaceWidth = qMax(gap, minimumGap);
                    const double spaceScaling = 100.0 * spaceWidth / (fontSize * GLYPH_WIDTH / 1000.0);
                    stream += formatNumber(spaceScaling) + " Tz\n";
                    stream += "[<0020>] TJ\n";
                }
            }
        }

        if (blockOpened)
        {
            stream += "ET\n";
        }
    }

    if (markAsArtifact)
    {
        stream += "EMC\n";
    }
    stream += "Q\n";

    if (writtenWords)
    {
        *writtenWords = wordCount;
    }

    return stream;
}

// -------------------------------------------------------------------------
// Layer data
// -------------------------------------------------------------------------

QByteArray PDFOCRTextLayerWriter::serializeLayerData(const PDFOCRPageResult& result, const QString& layerId, bool keepReviewData)
{
    PDFOCRSerializationFlags flags = PDFOCRSerializationFlag::Geometry | PDFOCRSerializationFlag::Regions;
    if (keepReviewData)
    {
        flags |= PDFOCRSerializationFlag::ReviewData;
    }

    QJsonObject object;
    object[QStringLiteral("format")] = QStringLiteral("pdf4qt-ocr-layer");
    object[QStringLiteral("version")] = LAYER_VERSION;
    object[QStringLiteral("layerId")] = layerId;
    object[QStringLiteral("hasReviewData")] = keepReviewData;
    object[QStringLiteral("page")] = PDFOCRProjectSerializer::pageResultToJson(result, flags);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool PDFOCRTextLayerWriter::deserializeLayerData(const QByteArray& data, PDFOCRPageResult& result, bool* hasReviewData)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (!document.isObject())
    {
        return false;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("pdf4qt-ocr-layer"))
    {
        return false;
    }

    const int version = object.value(QStringLiteral("version")).toInt();
    if (version < 1 || version > LAYER_VERSION)
    {
        return false;
    }

    if (hasReviewData)
    {
        *hasReviewData = object.value(QStringLiteral("hasReviewData")).toBool();
    }

    result = PDFOCRProjectSerializer::pageResultFromJson(object.value(QStringLiteral("page")).toObject());
    return true;
}

// -------------------------------------------------------------------------
// Reading
// -------------------------------------------------------------------------

PDFObjectReference PDFOCRTextLayerWriter::getOwnLayerContentReference(const PDFDocument* document, PDFInteger pageIndex)
{
    const PDFCatalog* catalog = document->getCatalog();
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return PDFObjectReference();
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    const PDFDictionary* pieceInfo = document->getDictionaryFromObject(page->getPieceDictionary(&document->getStorage()));
    if (!pieceInfo || !pieceInfo->hasKey(PIECE_INFO_KEY))
    {
        return PDFObjectReference();
    }

    const PDFDictionary* entry = document->getDictionaryFromObject(pieceInfo->get(PIECE_INFO_KEY));
    const PDFDictionary* privateData = entry ? document->getDictionaryFromObject(entry->get("Private")) : nullptr;
    if (!privateData)
    {
        return PDFObjectReference();
    }

    PDFDocumentDataLoaderDecorator loader(document);
    return loader.readReferenceFromDictionary(privateData, "Contents");
}

PDFOCRTextLayerWriter::LayerInfo PDFOCRTextLayerWriter::readLayerInfo(const PDFDocument* document, PDFInteger pageIndex)
{
    LayerInfo info;

    const PDFCatalog* catalog = document->getCatalog();
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return info;
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    const PDFObjectStorage* storage = &document->getStorage();

    const PDFDictionary* pieceInfo = document->getDictionaryFromObject(page->getPieceDictionary(storage));
    if (!pieceInfo || !pieceInfo->hasKey(PIECE_INFO_KEY))
    {
        return info;
    }

    const PDFDictionary* entry = document->getDictionaryFromObject(pieceInfo->get(PIECE_INFO_KEY));
    if (!entry)
    {
        return info;
    }

    const PDFDictionary* privateData = document->getDictionaryFromObject(entry->get("Private"));
    if (!privateData)
    {
        return info;
    }

    PDFDocumentDataLoaderDecorator loader(document);
    info.isPresent = true;
    info.version = int(loader.readIntegerFromDictionary(privateData, "Version", 0));
    info.layerId = loader.readTextStringFromDictionary(privateData, "LayerId", QString());
    info.generation = int(loader.readIntegerFromDictionary(privateData, "Generation", 0));
    info.engineId = loader.readTextStringFromDictionary(privateData, "Engine", QString());
    info.engineVersion = loader.readTextStringFromDictionary(privateData, "EngineVersion", QString());
    info.modelSetHash = loader.readTextStringFromDictionary(privateData, "ModelSetHash", QString());
    info.pageFingerprint = QByteArray::fromHex(loader.readTextStringFromDictionary(privateData, "PageFingerprint", QString()).toLatin1());
    info.contentReference = loader.readReferenceFromDictionary(privateData, "Contents");
    info.fontReference = loader.readReferenceFromDictionary(privateData, "Font");
    info.dataReference = loader.readReferenceFromDictionary(privateData, "Data");
    info.fontKey = loader.readNameFromDictionary(privateData, "FontKey");
    info.hasReviewData = loader.readBooleanFromDictionary(privateData, "HasReviewData", false);
    info.wordCount = int(loader.readIntegerFromDictionary(privateData, "WordCount", 0));
    info.modelIds = loader.readTextStringList(privateData->get("Models"));

    // Verify the binding to the current content (PDF-09): the content stream
    // must be present in the page contents and the fingerprint must match.
    bool contentFound = false;
    const PDFObject& contents = page->getContents();
    if (contents.isReference() && contents.getReference() == info.contentReference)
    {
        contentFound = true;
    }
    else
    {
        const PDFObject& dereferencedContents = document->getObject(contents);
        if (dereferencedContents.isArray())
        {
            const PDFArray* array = dereferencedContents.getArray();
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                const PDFObject& item = array->getItem(i);
                if (item.isReference() && item.getReference() == info.contentReference)
                {
                    contentFound = true;
                    break;
                }
            }
        }
    }

    if (!contentFound)
    {
        // Metadata are orphaned - layer content is not part of the page anymore
        info.fingerprintMatches = false;
        return info;
    }

    const QByteArray currentFingerprint = PDFOCRPagePreparer::computePageFingerprint(document, pageIndex);
    info.fingerprintMatches = !info.pageFingerprint.isEmpty() && info.pageFingerprint == currentFingerprint;
    return info;
}

std::optional<PDFOCRPageResult> PDFOCRTextLayerWriter::readLayer(const PDFDocument* document, PDFInteger pageIndex, LayerInfo* infoOut)
{
    LayerInfo info = readLayerInfo(document, pageIndex);
    if (infoOut)
    {
        *infoOut = info;
    }

    if (!info.isPresent || !info.dataReference.isValid())
    {
        return std::nullopt;
    }

    const PDFObject& dataObject = document->getObjectByReference(info.dataReference);
    if (!dataObject.isStream())
    {
        return std::nullopt;
    }

    const QByteArray data = document->getDecodedStream(dataObject.getStream());

    PDFOCRPageResult result;
    bool hasReviewData = false;
    if (!deserializeLayerData(data, result, &hasReviewData))
    {
        return std::nullopt;
    }

    result.pageIndex = pageIndex;
    result.state = PDFOCRPageState::Done;
    result.pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(document, pageIndex);
    result.pageLabel = PDFOCRPagePreparer::getPageLabel(document, pageIndex);

    if (!hasReviewData)
    {
        // Original scores are not restored by estimation (PDF-10)
        for (PDFOCRWord* word : result.getWords())
        {
            word->originalText = word->text;
            word->confidence = PDFOCRConfidence::unknown();
            word->textOrigin = PDFOCRTextOrigin::Imported;
            word->geometryOrigin = PDFOCRGeometryOrigin::Imported;
            if (word->reviewState != PDFOCRReviewState::Discarded)
            {
                word->reviewState = PDFOCRReviewState::Unreviewed;
            }
        }
    }

    return result;
}

// -------------------------------------------------------------------------
// Writing
// -------------------------------------------------------------------------

static std::vector<PDFObjectReference> getContentReferences(PDFDocumentBuilder* builder, const PDFDictionary* pageDictionary)
{
    std::vector<PDFObjectReference> references;

    const PDFObject& contents = pageDictionary->get("Contents");
    if (contents.isReference())
    {
        const PDFObject& dereferenced = builder->getObjectByReference(contents.getReference());
        if (dereferenced.isStream())
        {
            references.push_back(contents.getReference());
        }
        else if (dereferenced.isArray())
        {
            const PDFArray* array = dereferenced.getArray();
            for (size_t i = 0; i < array->getCount(); ++i)
            {
                const PDFObject& item = array->getItem(i);
                if (item.isReference())
                {
                    references.push_back(item.getReference());
                }
            }
        }
    }
    else if (contents.isArray())
    {
        const PDFArray* array = contents.getArray();
        for (size_t i = 0; i < array->getCount(); ++i)
        {
            const PDFObject& item = array->getItem(i);
            if (item.isReference())
            {
                references.push_back(item.getReference());
            }
        }
    }

    return references;
}

static PDFDictionary copyDictionary(PDFDocumentBuilder* builder, const PDFObject& object)
{
    if (const PDFDictionary* dictionary = builder->getDictionaryFromObject(object))
    {
        return *dictionary;
    }

    return PDFDictionary();
}

static void removeDictionaryEntry(PDFDictionary& dictionary, const QByteArray& key)
{
    if (dictionary.hasKey(key))
    {
        dictionary.setEntry(PDFInplaceOrMemoryString(key), PDFObject());
        dictionary.removeNullObjects();
    }
}

/// Removes the own layer from the page dictionary. Returns true, if something was removed.
static bool removeLayerFromPage(PDFDocumentBuilder* builder,
                                PDFObjectReference pageReference,
                                const PDFOCRTextLayerWriter::LayerInfo& info,
                                std::vector<PDFObjectReference>& contentReferences,
                                PDFDictionary& resources,
                                PDFDictionary& pieceInfo)
{
    Q_UNUSED(builder);
    Q_UNUSED(pageReference);

    if (!info.isPresent)
    {
        return false;
    }

    bool removed = false;

    // Content stream
    const size_t oldSize = contentReferences.size();
    std::erase(contentReferences, info.contentReference);
    removed = removed || contentReferences.size() != oldSize;

    // Font
    PDFDictionary fonts = copyDictionary(builder, resources.get("Font"));
    std::vector<QByteArray> keysToRemove;
    for (size_t i = 0; i < fonts.getCount(); ++i)
    {
        const QByteArray key = fonts.getKey(i).getString();
        const PDFObject& value = fonts.getValue(i);
        if (key == info.fontKey || (value.isReference() && value.getReference() == info.fontReference))
        {
            keysToRemove.push_back(key);
        }
    }
    for (const QByteArray& key : keysToRemove)
    {
        removeDictionaryEntry(fonts, key);
        removed = true;
    }
    if (!keysToRemove.empty())
    {
        resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(fonts))));
    }

    // Piece info
    if (pieceInfo.hasKey(PDFOCRTextLayerWriter::PIECE_INFO_KEY))
    {
        removeDictionaryEntry(pieceInfo, PDFOCRTextLayerWriter::PIECE_INFO_KEY);
        removed = true;
    }

    // Orphaned private objects
    if (info.dataReference.isValid())
    {
        builder->setObject(info.dataReference, PDFObject());
    }
    if (info.contentReference.isValid())
    {
        builder->setObject(info.contentReference, PDFObject());
    }

    return removed;
}

static void writePageUpdate(PDFDocumentBuilder* builder,
                            PDFObjectReference pageReference,
                            const std::vector<PDFObjectReference>& contentReferences,
                            PDFDictionary resources,
                            PDFDictionary pieceInfo)
{
    PDFObjectFactory factory;
    factory.beginDictionary();

    factory.beginDictionaryItem("Contents");
    if (contentReferences.size() == 1)
    {
        factory << contentReferences.front();
    }
    else
    {
        factory << contentReferences;
    }
    factory.endDictionaryItem();

    factory.beginDictionaryItem("Resources");
    factory << std::move(resources);
    factory.endDictionaryItem();

    factory.beginDictionaryItem("PieceInfo");
    if (pieceInfo.isEmpty())
    {
        factory << nullptr;
    }
    else
    {
        factory << std::move(pieceInfo);
    }
    factory.endDictionaryItem();

    factory.endDictionary();
    builder->mergeTo(pageReference, factory.takeObject());
}

PDFOCRTextLayerWriter::Report PDFOCRTextLayerWriter::apply(PDFDocumentBuilder* builder,
                                                           const PDFDocument* originalDocument,
                                                           const std::vector<PageRequest>& pages,
                                                           const Options& options)
{
    Report report;

    const PDFCatalog* catalog = originalDocument->getCatalog();
    PDFObjectReference fontReference;

    for (const PageRequest& request : pages)
    {
        const PDFInteger pageIndex = request.pageIndex;
        if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
        {
            report.error = PDFOCRError::create(PDFOCRErrorCode::WriteFailed, PDFTranslationContext::tr("Page %1 does not exist.").arg(pageIndex + 1), PDFTranslationContext::tr("Writing text layer"));
            return report;
        }

        const PDFOCRPageResult& result = request.result;

        // Validation (DATA-03)
        const QStringList validationErrors = PDFOCRValidator::validate(result);
        if (!validationErrors.isEmpty())
        {
            report.error = PDFOCRError::create(PDFOCRErrorCode::WriteFailed,
                                               PDFTranslationContext::tr("Result of the page %1 is invalid: %2").arg(pageIndex + 1).arg(validationErrors.front()),
                                               PDFTranslationContext::tr("Writing text layer"),
                                               validationErrors.join(QChar('\n')));
            return report;
        }

        const PDFPage* page = catalog->getPage(pageIndex);
        const PDFObjectReference pageReference = page->getPageReference();
        const PDFObject& pageObject = builder->getObjectByReference(pageReference);
        const PDFDictionary* pageDictionary = builder->getDictionaryFromObject(pageObject);
        if (!pageDictionary)
        {
            report.error = PDFOCRError::create(PDFOCRErrorCode::WriteFailed, PDFTranslationContext::tr("Page %1 has invalid dictionary.").arg(pageIndex + 1), PDFTranslationContext::tr("Writing text layer"));
            return report;
        }

        // Existing layer
        const LayerInfo existingLayer = readLayerInfo(originalDocument, pageIndex);

        // Font key
        PDFDictionary resources = copyDictionary(builder, page->getResources());
        PDFDictionary fonts = copyDictionary(builder, resources.get("Font"));
        QByteArray fontKey = FONT_RESOURCE_PREFIX;
        int suffix = 1;
        while (fonts.hasKey(fontKey) && fontKey != existingLayer.fontKey)
        {
            fontKey = QByteArray(FONT_RESOURCE_PREFIX) + "_" + QByteArray::number(suffix++);
        }

        // Content stream
        int wordCount = 0;
        QStringList warnings;
        const QByteArray content = createContentStream(result, fontKey, options.onlyReviewed, &wordCount, &warnings, options.markAsArtifact);
        report.messages << warnings;

        if (wordCount == 0)
        {
            report.skippedPages.push_back(pageIndex);
            report.messages << PDFTranslationContext::tr("Page %1: no text to write.").arg(pageIndex + 1);
            continue;
        }

        // Idempotency (PDF-11): identical layer is not written again
        if (existingLayer.isPresent && existingLayer.fingerprintMatches && existingLayer.hasReviewData == options.keepReviewData && existingLayer.contentReference.isValid())
        {
            const PDFObject& existingContentObject = originalDocument->getObjectByReference(existingLayer.contentReference);
            if (existingContentObject.isStream())
            {
                const QByteArray existingContent = originalDocument->getDecodedStream(existingContentObject.getStream());
                if (existingContent == content)
                {
                    report.unchangedPages.push_back(pageIndex);
                    continue;
                }
            }
        }

        // Remove the existing layer
        std::vector<PDFObjectReference> contentReferences = getContentReferences(builder, pageDictionary);
        PDFDictionary pieceInfo = copyDictionary(builder, pageDictionary->get("PieceInfo"));
        removeLayerFromPage(builder, pageReference, existingLayer, contentReferences, resources, pieceInfo);
        fonts = copyDictionary(builder, resources.get("Font"));

        // Font
        if (!fontReference.isValid())
        {
            fontReference = createGlyphlessFont(builder, options.compress);
        }
        fonts.setEntry(PDFInplaceOrMemoryString(fontKey), PDFObject::createReference(fontReference));
        resources.setEntry(PDFInplaceOrMemoryString("Font"), PDFObject::createDictionary(std::make_shared<PDFDictionary>(std::move(fonts))));

        // Content stream object
        const PDFObjectReference contentReference = createStream(builder, PDFDictionary(), content, options.compress);
        contentReferences.push_back(contentReference);

        // Private data
        const QString layerId = request.layerId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : request.layerId;
        const QByteArray layerData = serializeLayerData(result, layerId, options.keepReviewData);
        const PDFObjectReference dataReference = createStream(builder, PDFDictionary(), layerData, options.compress);
        const QByteArray pageFingerprint = PDFOCRPagePreparer::computePageFingerprint(originalDocument, pageIndex);

        PDFObjectFactory privateFactory;
        privateFactory.beginDictionary();
        privateFactory.beginDictionaryItem("Version");
        privateFactory << PDFInteger(LAYER_VERSION);
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("LayerId");
        privateFactory << layerId;
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("Generation");
        privateFactory << PDFInteger(result.generation);
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("Engine");
        privateFactory << result.provenance.engineId;
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("EngineVersion");
        privateFactory << result.provenance.engineVersion;
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("Models");
        privateFactory.beginArray();
        for (const QString& modelId : result.provenance.modelIds)
        {
            privateFactory << modelId;
        }
        privateFactory.endArray();
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("ModelSetHash");
        privateFactory << result.provenance.modelSetHash;
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("PageFingerprint");
        privateFactory << QString::fromLatin1(pageFingerprint.toHex());
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("Contents");
        privateFactory << contentReference;
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("Font");
        privateFactory << fontReference;
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("FontKey");
        privateFactory << WrapName(fontKey);
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("Data");
        privateFactory << dataReference;
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("HasReviewData");
        privateFactory << options.keepReviewData;
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("WordCount");
        privateFactory << PDFInteger(wordCount);
        privateFactory.endDictionaryItem();
        privateFactory.beginDictionaryItem("Producer");
        privateFactory << QString::fromLatin1(PDF_LIBRARY_NAME);
        privateFactory.endDictionaryItem();
        privateFactory.endDictionary();

        PDFObjectFactory entryFactory;
        entryFactory.beginDictionary();
        entryFactory.beginDictionaryItem("LastModified");
        entryFactory << WrapCurrentDateTime();
        entryFactory.endDictionaryItem();
        entryFactory.beginDictionaryItem("Private");
        entryFactory << privateFactory.takeObject();
        entryFactory.endDictionaryItem();
        entryFactory.endDictionary();

        pieceInfo.setEntry(PDFInplaceOrMemoryString(PIECE_INFO_KEY), entryFactory.takeObject());

        writePageUpdate(builder, pageReference, contentReferences, std::move(resources), std::move(pieceInfo));

        report.writtenPages.push_back(pageIndex);
        report.writtenWords += wordCount;
    }

    return report;
}

bool PDFOCRTextLayerWriter::removeConformanceDeclaration(PDFDocumentBuilder* builder, const PDFDocument* document)
{
    const PDFCatalog* catalog = document->getCatalog();
    const PDFObject& metadataObject = document->getObject(catalog->getMetadata());
    if (!metadataObject.isStream())
    {
        return false;
    }

    QString metadata = QString::fromUtf8(document->getDecodedStream(metadataObject.getStream()));
    const QString original = metadata;

    // Element and attribute forms of the pdfaid and pdfuaid properties
    static const QRegularExpression elementExpression(QStringLiteral("<(pdfaid|pdfuaid):(part|conformance|amd|rev|corr)>[^<]*</\\1:\\2>\\s*"));
    static const QRegularExpression attributeExpression(QStringLiteral("\\s(pdfaid|pdfuaid):(part|conformance|amd|rev|corr)\\s*=\\s*(\"[^\"]*\"|'[^']*')"));
    metadata.remove(elementExpression);
    metadata.remove(attributeExpression);

    if (metadata == original)
    {
        return false;
    }

    builder->setCatalogMetadata(metadata.toUtf8());
    return true;
}

bool PDFOCRTextLayerWriter::removeLayer(PDFDocumentBuilder* builder, const PDFDocument* originalDocument, PDFInteger pageIndex)
{
    const PDFCatalog* catalog = originalDocument->getCatalog();
    if (pageIndex < 0 || size_t(pageIndex) >= catalog->getPageCount())
    {
        return false;
    }

    const LayerInfo info = readLayerInfo(originalDocument, pageIndex);
    if (!info.isPresent)
    {
        return false;
    }

    const PDFPage* page = catalog->getPage(pageIndex);
    const PDFObjectReference pageReference = page->getPageReference();
    const PDFDictionary* pageDictionary = builder->getDictionaryFromObject(builder->getObjectByReference(pageReference));
    if (!pageDictionary)
    {
        return false;
    }

    std::vector<PDFObjectReference> contentReferences = getContentReferences(builder, pageDictionary);
    PDFDictionary resources = copyDictionary(builder, page->getResources());
    PDFDictionary pieceInfo = copyDictionary(builder, pageDictionary->get("PieceInfo"));

    if (!removeLayerFromPage(builder, pageReference, info, contentReferences, resources, pieceInfo))
    {
        return false;
    }

    writePageUpdate(builder, pageReference, contentReferences, std::move(resources), std::move(pieceInfo));
    return true;
}

}   // namespace pdf
