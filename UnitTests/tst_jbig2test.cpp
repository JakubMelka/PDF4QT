// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdfexception.h"
#include "pdfjbig2decoder.h"

#include <QtTest>

/// Tests of the JBIG2 decoder. The data of a JBIG2 image is fully controlled by the
/// document, so the decoder must survive a malformed stream without corrupting the
/// memory - these tests build such streams by hand and verify, that the decoder
/// refuses them by an exception instead of overflowing a buffer.
class JBIG2Test : public QObject
{
    Q_OBJECT

private slots:
    void test_page_information_size_is_decoded();
    void test_page_information_rejects_pixel_count_overflow();
    void test_generic_region_rejects_pixel_count_overflow();
    void test_segment_rejects_data_length_below_its_header();
    void test_symbol_dictionary_rejects_symbol_count();
    void test_symbol_dictionary_rejects_huffman_tables_without_huffman();
    void test_annex_h_page_with_refinement_aggregation();
    void test_annex_h_page_with_mmr_halftone_region();
    void test_halftone_grid_offset_is_divided_towards_negative_infinity();

private:
    /// Segment types used by the tests, see the table in 7.3 of the specification
    enum SegmentType : uint8_t
    {
        SymbolDictionary = 0,
        ImmediateGenericRegion = 38,
        PageInformation = 48
    };

    static void appendUInt32(QByteArray& data, uint32_t value);

    /// Appends a segment header of a segment, which refers to no other segment. The
    /// header layout is described in 7.2 of the specification.
    /// \param data Stream, into which the header is appended
    /// \param segmentNumber Number of the segment
    /// \param segmentType Type of the segment
    /// \param dataLength Length of the data part of the segment
    static void appendSegmentHeader(QByteArray& data, uint32_t segmentNumber, uint8_t segmentType, uint32_t dataLength);

    /// Creates the data part of a page information segment, see 7.4.8
    static QByteArray createPageInformationData(uint32_t width, uint32_t height);

    /// Creates the data part of an immediate generic region segment. Only the header
    /// of the region is created - the tests using it are refused before the encoded
    /// data would be read. See 7.4.6.
    static QByteArray createGenericRegionData(uint32_t width, uint32_t height);

    /// Creates the data part of a symbol dictionary segment, see 7.4.2
    /// \param flags Symbol dictionary flags, see 7.4.2.1.1
    /// \param numberOfExportedSymbols SDNUMEXSYMS
    /// \param numberOfNewSymbols SDNUMNEWSYMS
    static QByteArray createSymbolDictionaryData(uint16_t flags, uint32_t numberOfExportedSymbols, uint32_t numberOfNewSymbols);

    /// Reports the errors of the decoder into a list, so a test can verify, that a stream
    /// has been decoded without any complaint of the decoder
    class ErrorCollector : public pdf::PDFRenderErrorReporter
    {
    public:
        virtual void reportRenderError(pdf::RenderErrorType type, QString message) override
        {
            Q_UNUSED(type);
            messages << message;
        }

        virtual void reportRenderErrorOnce(pdf::RenderErrorType type, QString message) override
        {
            reportRenderError(type, message);
        }

        QStringList messages;
    };

    /// Decodes the stream. Throws \p pdf::PDFException for a malformed stream.
    /// \param stream Decoded stream
    /// \param errorReporter Reporter of the errors of the decoder, an own one is used, when
    ///        it is \p nullptr
    static pdf::PDFImageData decode(const QByteArray& stream, pdf::PDFRenderErrorReporter* errorReporter = nullptr);

    /// Returns the image drawn as one line of text per row of the image, where a hash is a
    /// black pixel and a dot is a white one. A failed comparison of the whole drawing shows
    /// what the decoded image looks like, which a comparison of the raw bytes does not.
    /// \param imageData Drawn image
    static QStringList drawImage(const pdf::PDFImageData& imageData);

    /// Decodes the stream and returns the message of the exception it has thrown, or
    /// an empty string, if it has been decoded without an error. The tests match the
    /// message, because a malformed stream ends by an exception even when the checked
    /// validation is missing - the decoder just runs out of the data later, and a test
    /// only requiring an exception would pass without the validation as well.
    static QString decodeExpectingError(const QByteArray& stream);
};

void JBIG2Test::appendUInt32(QByteArray& data, uint32_t value)
{
    data.append(char((value >> 24) & 0xFF));
    data.append(char((value >> 16) & 0xFF));
    data.append(char((value >> 8) & 0xFF));
    data.append(char(value & 0xFF));
}

void JBIG2Test::appendSegmentHeader(QByteArray& data, uint32_t segmentNumber, uint8_t segmentType, uint32_t dataLength)
{
    appendUInt32(data, segmentNumber);

    // Segment header flags - the type is in the bits 0-5, the cleared bit 6 means,
    // that the page association is a single byte
    data.append(char(segmentType));

    // Referred-to segment count (the upper three bits) and the retention flags
    data.append(char(0x00));

    // Page association
    data.append(char(0x01));

    appendUInt32(data, dataLength);
}

QByteArray JBIG2Test::createPageInformationData(uint32_t width, uint32_t height)
{
    QByteArray data;

    appendUInt32(data, width);
    appendUInt32(data, height);
    appendUInt32(data, 0);      // X resolution, unused by the decoder
    appendUInt32(data, 0);      // Y resolution, unused by the decoder
    data.append(char(0x00));    // Page segment flags
    data.append(char(0x00));    // Page striping information
    data.append(char(0x00));

    return data;
}

QByteArray JBIG2Test::createGenericRegionData(uint32_t width, uint32_t height)
{
    QByteArray data;

    // Region segment information field, see 7.4.1
    appendUInt32(data, width);
    appendUInt32(data, height);
    appendUInt32(data, 0);      // X location
    appendUInt32(data, 0);      // Y location
    data.append(char(0x00));    // External combination operator OR

    // Generic region segment flags - arithmetic coding, template 0, no TPGDON
    data.append(char(0x00));

    // Adaptive template pixels of the template 0 - four signed byte pairs
    const char atPixels[] = { 3, -1, -3, -1, 2, -2, -2, -2 };
    data.append(atPixels, sizeof(atPixels));

    return data;
}

QByteArray JBIG2Test::createSymbolDictionaryData(uint16_t flags, uint32_t numberOfExportedSymbols, uint32_t numberOfNewSymbols)
{
    QByteArray data;

    // Symbol dictionary flags, see 7.4.2.1.1
    data.append(char((flags >> 8) & 0xFF));
    data.append(char(flags & 0xFF));

    // Symbol dictionary AT flags - present, because SDHUFF is 0 and SDTEMPLATE is 0
    const char atPixels[] = { 3, -1, -3, -1, 2, -2, -2, -2 };
    data.append(atPixels, sizeof(atPixels));

    appendUInt32(data, numberOfExportedSymbols);
    appendUInt32(data, numberOfNewSymbols);

    return data;
}

pdf::PDFImageData JBIG2Test::decode(const QByteArray& stream, pdf::PDFRenderErrorReporter* errorReporter)
{
    pdf::PDFRenderErrorReporterDummy dummyErrorReporter;
    pdf::PDFJBIG2Decoder decoder(stream, QByteArray(), errorReporter ? errorReporter : &dummyErrorReporter);
    return decoder.decode(pdf::PDFImageData::MaskingType::None);
}

QStringList JBIG2Test::drawImage(const pdf::PDFImageData& imageData)
{
    QStringList rows;
    const QByteArray& data = imageData.getData();

    for (uint32_t row = 0; row < imageData.getHeight(); ++row)
    {
        QString line;

        for (uint32_t column = 0; column < imageData.getWidth(); ++column)
        {
            // The decoder writes a set bit for a white pixel, because the value of the
            // image data is the value of the color and not the value of the ink
            const uint8_t byte = uint8_t(data[int(row * imageData.getStride() + column / 8)]);
            line += ((byte >> (7 - (column % 8))) & 1) ? QChar('.') : QChar('#');
        }

        rows << line;
    }

    return rows;
}

QString JBIG2Test::decodeExpectingError(const QByteArray& stream)
{
    try
    {
        decode(stream);
    }
    catch (const pdf::PDFException& exception)
    {
        return exception.getMessage();
    }

    return QString();
}

void JBIG2Test::test_page_information_size_is_decoded()
{
    // A page of a sane size is decoded, so the tests below really refuse the malformed
    // streams and not every stream
    const QByteArray pageInformation = createPageInformationData(64, 32);

    QByteArray stream;
    appendSegmentHeader(stream, 0, PageInformation, uint32_t(pageInformation.size()));
    stream.append(pageInformation);

    const pdf::PDFImageData imageData = decode(stream);
    QCOMPARE(imageData.getWidth(), 64u);
    QCOMPARE(imageData.getHeight(), 32u);
    QCOMPARE(imageData.getBitsPerComponent(), 1u);
}

void JBIG2Test::test_page_information_rejects_pixel_count_overflow()
{
    // Both dimensions are exactly at the limit allowed for a single dimension, but
    // their product is 2^32 - it overflows the int and the buffer allocated for the
    // page bitmap would be empty, while the bitmap would report the full size. Every
    // region painted onto such a page writes outside of the allocated memory.
    const QByteArray pageInformation = createPageInformationData(65536, 65536);

    QByteArray stream;
    appendSegmentHeader(stream, 0, PageInformation, uint32_t(pageInformation.size()));
    stream.append(pageInformation);

    QVERIFY(decodeExpectingError(stream).contains("pixel count exceeded"));
}

void JBIG2Test::test_generic_region_rejects_pixel_count_overflow()
{
    const QByteArray pageInformation = createPageInformationData(64, 32);

    // The region is followed by enough encoded data for the decoding to really start.
    // Without it the decoder stops on the end of the data before it writes a single
    // pixel, so the test would pass even for a decoder, which does not check the size.
    QByteArray regionData = createGenericRegionData(65536, 65536);
    regionData.append(4096, char(0x55));

    QByteArray stream;
    appendSegmentHeader(stream, 0, PageInformation, uint32_t(pageInformation.size()));
    stream.append(pageInformation);
    appendSegmentHeader(stream, 1, ImmediateGenericRegion, uint32_t(regionData.size()));
    stream.append(regionData);

    QVERIFY(decodeExpectingError(stream).contains("pixel count exceeded"));
}

void JBIG2Test::test_segment_rejects_data_length_below_its_header()
{
    const QByteArray pageInformation = createPageInformationData(64, 32);
    const QByteArray regionData = createGenericRegionData(8, 8);

    QByteArray stream;
    appendSegmentHeader(stream, 0, PageInformation, uint32_t(pageInformation.size()));
    stream.append(pageInformation);

    // The segment declares less data than its own region header occupies. The length
    // of the encoded data is computed as a difference of the declared length and the
    // already read header, which underflows in the unsigned arithmetic.
    appendSegmentHeader(stream, 1, ImmediateGenericRegion, 5);
    stream.append(regionData);

    QVERIFY(decodeExpectingError(stream).contains("invalid data length"));
}

void JBIG2Test::test_symbol_dictionary_rejects_symbol_count()
{
    // The number of the new symbols is used to allocate the array of the symbol
    // bitmaps before anything is decoded, so an unlimited value asks for an allocation
    // of billions of bitmaps
    QByteArray dictionaryData = createSymbolDictionaryData(0x0000, 1, 0xFFFFFFFF);

    // Encoded data of the symbols must follow, otherwise the decoder stops on the end
    // of the data before it allocates anything
    dictionaryData.append(64, char(0x55));

    QByteArray stream;
    appendSegmentHeader(stream, 0, SymbolDictionary, uint32_t(dictionaryData.size()));
    stream.append(dictionaryData);

    QVERIFY(decodeExpectingError(stream).contains("symbol count exceeded"));
}

void JBIG2Test::test_symbol_dictionary_rejects_huffman_tables_without_huffman()
{
    // The specification requires in 7.4.2.1.1, that the SDHUFFDH and SDHUFFDW fields
    // contain zero when SDHUFF is zero. SDHUFFDH occupies the bits 2-3 and SDHUFFDW
    // the bits 4-5 - both must be rejected here.
    for (const uint16_t flags : { uint16_t(0x0004), uint16_t(0x0010) })
    {
        const QByteArray dictionaryData = createSymbolDictionaryData(flags, 1, 1);

        QByteArray stream;
        appendSegmentHeader(stream, 0, SymbolDictionary, uint32_t(dictionaryData.size()));
        stream.append(dictionaryData);

        QVERIFY(decodeExpectingError(stream).contains("invalid flags for symbol dictionary"));
    }
}


void JBIG2Test::test_annex_h_page_with_refinement_aggregation()
{
    // The segments of the third page of the example datastream of the annex H of the
    // specification - a page information segment, a symbol dictionary, a symbol dictionary
    // using refinement/aggregate coding, which imports the symbol of the first one, and a
    // text region. The bytes are the ones listed in H.2 for the segments 15 to 18.
    //
    // This is the only stream, which covers the form of the aggregation of a single symbol
    // instance prescribed by 6.5.8.2.2 - the test files of the Power JBIG-2 encoder use the
    // other one, so a regression of this form would stay unnoticed without this test.
    static const unsigned char DATA[] =
    {
        0x00, 0x00, 0x00, 0x0F, 0x30, 0x00, 0x03, 0x00, 0x00, 0x00, 0x13, 0x00,
        0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x16, 0x08, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x4F, 0xE7, 0x8D, 0x68, 0x1B, 0x14, 0x2F,
        0x3F, 0xFF, 0xAC, 0x00, 0x00, 0x00, 0x11, 0x00, 0x21, 0x10, 0x03, 0x00,
        0x00, 0x00, 0x20, 0x08, 0x02, 0x02, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
        0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x4F, 0xE9, 0xD7, 0xD5, 0x90,
        0xC3, 0xB5, 0x26, 0xA7, 0xFB, 0x6D, 0x14, 0x98, 0x3F, 0xFF, 0xAC, 0x00,
        0x00, 0x00, 0x12, 0x07, 0x20, 0x11, 0x03, 0x00, 0x00, 0x00, 0x25, 0x00,
        0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x8C, 0x12, 0x00, 0x00, 0x00, 0x04, 0xA9, 0x5C,
        0x8B, 0xF4, 0xC3, 0x7D, 0x96, 0x6A, 0x28, 0xE5, 0x76, 0x8F, 0xFF, 0xAC
    };

    const QByteArray stream(reinterpret_cast<const char*>(DATA), int(sizeof(DATA)));

    ErrorCollector errorCollector;
    pdf::PDFImageData imageData;

    try
    {
        imageData = decode(stream, &errorCollector);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    QCOMPARE(imageData.getWidth(), 37u);
    QCOMPARE(imageData.getHeight(), 8u);

    // The page consists of the text region only, so the page bitmap is the bitmap of the
    // figure H.5 - the annex describes it as the symbol "c" drawn at (0, 0), the symbol "a"
    // at (8, 0), the symbol "p" at (16, 0) and the symbol, which is an aggregation of the
    // two symbol instances "a" and "c", at (23, 0).
    const QStringList expectedImage =
    {
        ".####....####...####....####....####.",
        "#....#.......#..#...#.......#..#....#",
        "#........#####..#...#...#####..#.....",
        "#.......#....#..#...#..#....#..#.....",
        "#....#..#....#..####...#....#..#....#",
        ".####....#####..#.......#####...####.",
        "................#....................",
        "................#...................."
    };

    QCOMPARE(drawImage(imageData), expectedImage);

    // The stream is a correct one, so the decoder must decode it by the procedure of the
    // specification - it must not fall back to the form of the Power JBIG-2 encoder, which
    // is reported as a warning
    QCOMPARE(errorCollector.messages, QStringList());
}


void JBIG2Test::test_annex_h_page_with_mmr_halftone_region()
{
    // The segments 1, 5 and 6 of the example datastream of the annex H - a page information
    // segment, an MMR coded pattern dictionary of sixteen patterns of four by four pixels
    // and an MMR coded halftone region. The other region segments of the page are left out,
    // so the page bitmap contains the halftone region only.
    //
    // The MMR variant of the halftone decoding has no other coverage - the test files of the
    // Power JBIG-2 encoder code the gray-scale image arithmetically.
    static const unsigned char DATA[] =
    {
        0x00, 0x00, 0x00, 0x01, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x00,
        0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x10, 0x01,
        0x01, 0x00, 0x00, 0x00, 0x2D, 0x01, 0x04, 0x04, 0x00, 0x00, 0x00, 0x0F,
        0x20, 0xD1, 0x84, 0x61, 0x18, 0x45, 0xF2, 0xF9, 0x7C, 0x8F, 0x11, 0xC3,
        0x9E, 0x45, 0xF2, 0xF9, 0x7D, 0x42, 0x85, 0x0A, 0xAA, 0x84, 0x62, 0x2F,
        0xEE, 0xEC, 0x44, 0x62, 0x22, 0x35, 0x2A, 0x0A, 0x83, 0xB9, 0xDC, 0xEE,
        0x77, 0x80, 0x00, 0x00, 0x00, 0x06, 0x17, 0x20, 0x05, 0x01, 0x00, 0x00,
        0x00, 0x57, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00,
        0x00, 0x10, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0xAA, 0xAA, 0xAA, 0xAA, 0x80, 0x08, 0x00, 0x80,
        0x36, 0xD5, 0x55, 0x6B, 0x5A, 0xD4, 0x00, 0x40, 0x04, 0x2E, 0xE9, 0x52,
        0xD2, 0xD2, 0xD2, 0x8A, 0xA5, 0x4A, 0x00, 0x20, 0x02, 0x23, 0xE0, 0x95,
        0x24, 0xB4, 0x92, 0x8A, 0x4A, 0x92, 0x54, 0x92, 0xD2, 0x4A, 0x29, 0x2A,
        0x49, 0x40, 0x04, 0x00, 0x40
    };

    const QByteArray stream(reinterpret_cast<const char*>(DATA), int(sizeof(DATA)));

    ErrorCollector errorCollector;
    pdf::PDFImageData imageData;

    try
    {
        imageData = decode(stream, &errorCollector);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    QCOMPARE(imageData.getWidth(), 64u);
    QCOMPARE(imageData.getHeight(), 56u);

    // The annex lists the decoded gray-scale image as the array of the values GI[ng, mg] of
    // ng + mg, of the size of eight by nine, and the grid as the patterns of four by four
    // pixels placed by a step of four pixels. The region is drawn at (16, 15) of the page,
    // so the page is a wedge growing from the pattern 0 to the pattern 15 diagonally.
    const QStringList expectedImage =
    {
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "......................................#...#...#.................",
        "......................#..##..##..##..##.###.###.................",
        "..............................#..##..##..##..##.................",
        ".............................................#..................",
        "..................................#...#...#...#.................",
        "..................#..##..##..##..##.###.###.###.................",
        "..........................#..##..##..##..##..###................",
        ".........................................#...#..................",
        "..............................#...#...#...#..##.................",
        ".................##..##..##..##.###.###.###.###.................",
        "......................#..##..##..##..##..#######................",
        ".....................................#...#...#..................",
        "..........................#...#...#...#..##..##.................",
        ".................##..##..##.###.###.###.###.###.................",
        "..................#..##..##..##..##..###########................",
        ".................................#...#...#...##.................",
        "......................#...#...#...#..##..##..##.................",
        ".................##..##.###.###.###.###.###.####................",
        ".................##..##..##..##..###############................",
        ".............................#...#...#...##..##.................",
        "..................#...#...#...#..##..##..##.###.................",
        ".................##.###.###.###.###.###.########................",
        ".................##..##..##..###################................",
        ".........................#...#...#...##..##..##.................",
        "..................#...#...#..##..##..##.###.####................",
        "................###.###.###.###.###.############................",
        ".................##..##..#######################................",
        ".....................#...#...#...##..##..##..##.................",
        "..................#...#..##..##..##.###.########................",
        "................###.###.###.###.################................",
        ".................##..###########################................",
        ".................#...#...#...##..##..##..##..###................",
        "..................#..##..##..##.###.############................",
        "................###.###.###.####################................",
        ".................###############################................",
        ".................#...#...##..##..##..##..#######................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................"
    };

    QCOMPARE(drawImage(imageData), expectedImage);
    QCOMPARE(errorCollector.messages, QStringList());
}


void JBIG2Test::test_halftone_grid_offset_is_divided_towards_negative_infinity()
{
    // The same stream as the previous test, but the halftone grid is placed at the offset
    // of -1000 in both directions. The specification computes the location of a cell of the
    // grid in 6.6.5.1 and 6.6.5.2 by the operator >>A, which is defined in 4.3 as the shift
    // filling the vacated bits by the sign, so it rounds towards the negative infinity. The
    // integer division of C++ rounds towards zero instead, which places the cells of the
    // grid left of the origin one pixel to the right.
    //
    // -1000 >>A 8 is -4 and (-1000 + I * 1024) >>A 8 is I * 4 - 4 for every positive I, so
    // the whole grid is shifted by four pixels and the image is the image of the previous
    // test shifted by four pixels, clipped by the region. The truncating division gives -3
    // for the first row and the first column of the grid, which draws a row and a column of
    // the pixels, which must stay empty.
    static const unsigned char DATA[] =
    {
        0x00, 0x00, 0x00, 0x01, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13, 0x00,
        0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x10, 0x01,
        0x01, 0x00, 0x00, 0x00, 0x2D, 0x01, 0x04, 0x04, 0x00, 0x00, 0x00, 0x0F,
        0x20, 0xD1, 0x84, 0x61, 0x18, 0x45, 0xF2, 0xF9, 0x7C, 0x8F, 0x11, 0xC3,
        0x9E, 0x45, 0xF2, 0xF9, 0x7D, 0x42, 0x85, 0x0A, 0xAA, 0x84, 0x62, 0x2F,
        0xEE, 0xEC, 0x44, 0x62, 0x22, 0x35, 0x2A, 0x0A, 0x83, 0xB9, 0xDC, 0xEE,
        0x77, 0x80, 0x00, 0x00, 0x00, 0x06, 0x17, 0x20, 0x05, 0x01, 0x00, 0x00,
        0x00, 0x57, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00,
        0x00, 0x10, 0x00, 0x00, 0x00, 0x0F, 0x00, 0x01, 0x00, 0x00, 0x00, 0x08,
        0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0xAA, 0xAA, 0xAA, 0xAA, 0x80, 0x08, 0x00, 0x80,
        0x36, 0xD5, 0x55, 0x6B, 0x5A, 0xD4, 0x00, 0x40, 0x04, 0x2E, 0xE9, 0x52,
        0xD2, 0xD2, 0xD2, 0x8A, 0xA5, 0x4A, 0x00, 0x20, 0x02, 0x23, 0xE0, 0x95,
        0x24, 0xB4, 0x92, 0x8A, 0x4A, 0x92, 0x54, 0x92, 0xD2, 0x4A, 0x29, 0x2A,
        0x49, 0x40, 0x04, 0x00, 0x40
    };

    QByteArray stream(reinterpret_cast<const char*>(DATA), int(sizeof(DATA)));

    // HGX and HGY of the halftone region segment, see 7.4.5.1.2 - both are signed
    const int offsetOfHGX = 124;
    const int offsetOfHGY = 128;
    const int32_t gridOffset = -1000;
    for (const int offset : { offsetOfHGX, offsetOfHGY })
    {
        for (int i = 0; i < 4; ++i)
        {
            stream[offset + i] = static_cast<char>(static_cast<unsigned char>(uint32_t(gridOffset) >> (8 * (3 - i))));
        }
    }

    ErrorCollector errorCollector;
    pdf::PDFImageData imageData;

    try
    {
        imageData = decode(stream, &errorCollector);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    const QStringList expectedImage =
    {
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "..............................#...#...#...#.....................",
        ".................##..##..##..##.###.###.###.....................",
        "......................#..##..##..##..##..###....................",
        ".....................................#...#......................",
        "..........................#...#...#...#..##.....................",
        ".................##..##..##.###.###.###.###.....................",
        "..................#..##..##..##..##..#######....................",
        ".................................#...#...#......................",
        "......................#...#...#...#..##..##.....................",
        ".................##..##.###.###.###.###.###.....................",
        ".................##..##..##..##..###########....................",
        ".............................#...#...#...##.....................",
        "..................#...#...#...#..##..##..##.....................",
        ".................##.###.###.###.###.###.####....................",
        ".................##..##..##..###############....................",
        ".........................#...#...#...##..##.....................",
        "..................#...#...#..##..##..##.###.....................",
        "................###.###.###.###.###.########....................",
        ".................##..##..###################....................",
        ".....................#...#...#...##..##..##.....................",
        "..................#...#..##..##..##.###.####....................",
        "................###.###.###.###.############....................",
        ".................##..#######################....................",
        ".................#...#...#...##..##..##..##.....................",
        "..................#..##..##..##.###.########....................",
        "................###.###.###.################....................",
        ".................###########################....................",
        ".................#...#...##..##..##..##..###....................",
        ".................##..##..##.###.############....................",
        "................###.###.####################....................",
        "................############################....................",
        ".................#...##..##..##..##..#######....................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................",
        "................................................................"
    };

    QCOMPARE(drawImage(imageData), expectedImage);
    QCOMPARE(errorCollector.messages, QStringList());
}

QTEST_APPLESS_MAIN(JBIG2Test)

#include "tst_jbig2test.moc"
