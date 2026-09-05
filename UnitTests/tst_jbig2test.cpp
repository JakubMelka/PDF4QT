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
#include "pdfjbig2encoder.h"

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
    void test_region_combination_operators();
    void test_generic_region_with_unknown_data_length();
    void test_long_form_referred_segments();
    void test_striped_page_with_unknown_height();
    void test_intermediate_and_lossless_generic_regions();
    void test_auxiliary_segments_are_skipped();
    void test_custom_huffman_table_segment_is_parsed();
    void test_malformed_generic_region_segments_are_refused();
    void test_text_region_reference_corners();
    void test_refinement_region_segments();
    void test_symbol_dictionary_flag_validation();
    void test_pattern_dictionary_rejects_zero_pattern_size();
    void test_region_outside_page_is_clipped();
    void test_bitmap_api_validation();
    void test_referred_segments_are_resolved();

private:
    /// Segment types used by the tests, see the table in 7.3 of the specification
    enum SegmentType : uint8_t
    {
        SymbolDictionary = 0,
        IntermediateGenericRegion = 36,
        ImmediateGenericRegion = 38,
        ImmediateLosslessGenericRegion = 39,
        PageInformation = 48,
        EndOfStripe = 50,
        Profiles = 52,
        Tables = 53,
        Extension = 62
    };

    /// Offsets of the fields of an embedded stream created by \p createEncodedStream -
    /// the page information segment (a header of 11 bytes and 19 bytes of data) is
    /// followed by the region segment (a header of 11 bytes, the region segment
    /// information field of 17 bytes, the flags and the adaptive template pixels).
    enum StreamOffset : int
    {
        PageFlagsOffset = 27,
        RegionHeaderOffset = 30,
        RegionDataLengthOffset = 37,
        RegionInformationOffset = 41,
        RegionYOffset = 53,
        RegionOperatorOffset = 57,
        GenericFlagsOffset = 58,
        RegionHeaderSize = 11
    };

    /// Encodes the image drawn as text (a hash is a black pixel) into the embedded
    /// stream by the encoder - a page information segment and an immediate generic
    /// region segment covering the page
    /// \param rows Rows of the image
    /// \param MMR Use the MMR coding instead of the arithmetic one
    static QByteArray createEncodedStream(const QStringList& rows, bool MMR = false);

    /// Returns the image with the black and white pixels swapped
    static QStringList invertImage(const QStringList& rows);

    /// Replaces the four byte unsigned integer at the position
    static void writeUInt32(QByteArray& data, int position, uint32_t value);

    /// Returns the segments of the third page of the example datastream of the annex H
    /// of the specification - a page information segment, a symbol dictionary, a symbol
    /// dictionary using refinement/aggregate coding, which imports the symbol of the
    /// first one, and a text region. The bytes are the ones listed in H.2 for the
    /// segments 15 to 18.
    static QByteArray createAnnexHRefinementPageStream();

    /// Appends a segment header of a segment referring to the given segments, see 7.2
    static void appendSegmentHeaderWithReferences(QByteArray& data, uint32_t segmentNumber, uint8_t segmentType, const std::vector<uint8_t>& referredSegments, uint32_t dataLength);

    /// Returns the arithmetic coded decisions - the data of a refinement region with the
    /// typical prediction, whose every row is typical, so only the SLTP bit of each row
    /// is coded (1 for the first row, 0 for the others)
    /// \param rowCount Number of the rows
    /// \param GRTEMPLATE Refinement template, which selects the context of the SLTP bit
    static QByteArray createTypicalRefinementData(int rowCount, uint8_t GRTEMPLATE);

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

QByteArray JBIG2Test::createEncodedStream(const QStringList& rows, bool MMR)
{
    const int width = rows.front().size();
    const int height = rows.size();
    const int stride = (width + 7) / 8;

    std::vector<uint8_t> pixels(size_t(stride) * size_t(height), 0);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            if (rows[y][x] == '#')
            {
                pixels[size_t(y) * size_t(stride) + size_t(x >> 3)] |= uint8_t(0x80 >> (x & 7));
            }
        }
    }

    pdf::PDFBitonalBitmapView view;
    view.data = pixels.data();
    view.width = width;
    view.height = height;
    view.stride = stride;
    view.isOneBlack = true;

    pdf::PDFJBIG2EncoderParameters parameters;
    parameters.MMR = MMR;

    pdf::PDFJBIG2Encoder encoder(view, parameters);
    return encoder.encodeEmbeddedStream();
}

QStringList JBIG2Test::invertImage(const QStringList& rows)
{
    QStringList result;

    for (const QString& row : rows)
    {
        QString inverted;
        for (const QChar character : row)
        {
            inverted += (character == '#') ? '.' : '#';
        }
        result << inverted;
    }

    return result;
}

void JBIG2Test::writeUInt32(QByteArray& data, int position, uint32_t value)
{
    data[position] = char((value >> 24) & 0xFF);
    data[position + 1] = char((value >> 16) & 0xFF);
    data[position + 2] = char((value >> 8) & 0xFF);
    data[position + 3] = char(value & 0xFF);
}

QByteArray JBIG2Test::createAnnexHRefinementPageStream()
{
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

    return QByteArray(reinterpret_cast<const char*>(DATA), int(sizeof(DATA)));
}

void JBIG2Test::appendSegmentHeaderWithReferences(QByteArray& data, uint32_t segmentNumber, uint8_t segmentType, const std::vector<uint8_t>& referredSegments, uint32_t dataLength)
{
    Q_ASSERT(referredSegments.size() <= 4);
    Q_ASSERT(segmentNumber <= 256);

    appendUInt32(data, segmentNumber);
    data.append(char(segmentType));

    // Referred-to segment count in the upper three bits, the retain bits are zero
    data.append(char(referredSegments.size() << 5));

    // Referred segment numbers are a single byte, because the segment number is at most 256
    for (const uint8_t referredSegment : referredSegments)
    {
        data.append(char(referredSegment));
    }

    data.append(char(0x01));
    appendUInt32(data, dataLength);
}

QByteArray JBIG2Test::createTypicalRefinementData(int rowCount, uint8_t GRTEMPLATE)
{
    // The contexts of the SLTP bit, see 6.3.5.6 of the specification
    const uint32_t LTPContext = (GRTEMPLATE == 0) ? 0x0100 : 0x0080;

    pdf::PDFJBIG2ArithmeticDecoderState state;
    state.reset(13);

    pdf::PDFJBIG2ArithmeticEncoder encoder;
    for (int row = 0; row < rowCount; ++row)
    {
        encoder.encodeBit(LTPContext, &state, (row == 0) ? 1 : 0);
    }

    return encoder.finish();
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
    const QByteArray stream = createAnnexHRefinementPageStream();

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

void JBIG2Test::test_region_combination_operators()
{
    // A region is combined with the page by its external combination operator, see
    // 7.4.1.5, and the page starts filled by the default pixel value of the page
    // information segment, see 7.4.8.5. The region covers the whole page, so every
    // pixel of the page is the result of the operator applied to the default pixel
    // value and to the pixel of the region.
    const QStringList image = { "#...#...", ".##..##.", "........", "########" };
    const QStringList white = { "........", "........", "........", "........" };
    const QStringList black = { "########", "########", "########", "########" };

    struct Case
    {
        uint8_t operation;
        bool isDefaultPixelBlack;
        QStringList expected;
    };

    const std::vector<Case> cases =
    {
        { 0, false, image },                  // OR
        { 0, true, black },
        { 1, false, white },                  // AND
        { 1, true, image },
        { 2, false, image },                  // XOR
        { 2, true, invertImage(image) },
        { 3, false, invertImage(image) },     // XNOR
        { 3, true, image },
        { 4, false, image },                  // REPLACE
        { 4, true, image }
    };

    for (const Case& testCase : cases)
    {
        for (const bool MMR : { false, true })
        {
            QByteArray stream = createEncodedStream(image, MMR);
            stream[RegionOperatorOffset] = char(testCase.operation);

            // The page default combination operator is the same as the one of the region
            // (the page has two bits for it, so REPLACE is written as OR), the page default
            // pixel value is the bit 2 and the bit 6 allows the regions to use any operator
            const uint8_t pageOperator = uint8_t(testCase.operation & 0x03);
            stream[PageFlagsOffset] = char(0x01 | (testCase.isDefaultPixelBlack ? 0x04 : 0x00) | (pageOperator << 3) | 0x40);

            ErrorCollector errorCollector;
            const pdf::PDFImageData imageData = decode(stream, &errorCollector);
            QCOMPARE(errorCollector.messages, QStringList());
            QCOMPARE(drawImage(imageData), testCase.expected);
        }
    }
}

void JBIG2Test::test_generic_region_with_unknown_data_length()
{
    // An immediate generic region can have the unknown data length 0xFFFFFFFF, see 7.2.7 -
    // the data then end with 0xFF 0xAC (arithmetic coding) or 0x00 0x00 (MMR coding)
    // followed by a four byte row count. The bytes of the sequence can also occur in
    // the page information segment, which precedes the region.
    const QStringList image = { "#..#..#.....", ".#..#..#....", "..#..#..#...", "...#..#..#..", "....#..#..#." };

    for (const bool MMR : { false, true })
    {
        QByteArray stream = createEncodedStream(image, MMR);
        QVERIFY(!MMR || !stream.mid(GenericFlagsOffset + 1).contains(QByteArray("\x00\x00", 2)));

        writeUInt32(stream, RegionDataLengthOffset, 0xFFFFFFFF);
        if (MMR)
        {
            stream.append(QByteArray("\x00\x00", 2));
        }
        appendUInt32(stream, uint32_t(image.size()));

        ErrorCollector errorCollector;
        const pdf::PDFImageData imageData = decode(stream, &errorCollector);
        QCOMPARE(errorCollector.messages, QStringList());
        QCOMPARE(drawImage(imageData), image);

        // Without the end sequence the region can not be decoded
        QByteArray truncated = stream.left(stream.size() - 4);
        if (MMR)
        {
            truncated.chop(2);
        }
        else
        {
            truncated[truncated.size() - 1] = static_cast<char>(static_cast<unsigned char>(0xAB));
        }
        QVERIFY(decodeExpectingError(truncated).contains("end of data byte sequence"));
    }
}

void JBIG2Test::test_long_form_referred_segments()
{
    // A segment referring to more than four segments uses the long form of the
    // referred-to segments field, see 7.2.4 - a four byte count with the top three
    // bits set, followed by the retain bits and by the referred segment numbers. A
    // generic region does not use the referred segments, so it is decoded as usual.
    const QStringList image = { "##..", "..##", "#..#" };
    const QByteArray stream = createEncodedStream(image);

    QByteArray longForm = stream.left(RegionHeaderOffset + 5);
    longForm.append(QByteArray("\xE0\x00\x00\x05", 4));    // Five referred segments
    longForm.append(char(0x00));                            // Retain bits, ceil((5 + 1) / 8) bytes
    longForm.append(QByteArray(5, char(0x00)));             // Referred segment numbers, one byte each
    longForm.append(stream.mid(RegionHeaderOffset + 6));

    ErrorCollector errorCollector;
    QCOMPARE(drawImage(decode(longForm, &errorCollector)), image);
    QCOMPARE(errorCollector.messages, QStringList());

    // The counts 5 and 6 of the short form are reserved
    for (const uint8_t count : { uint8_t(5), uint8_t(6) })
    {
        QByteArray reserved = stream;
        reserved[RegionHeaderOffset + 5] = char(count << 5);
        QVERIFY(decodeExpectingError(reserved).contains("bad referred segments"));
    }
}

void JBIG2Test::test_striped_page_with_unknown_height()
{
    // A striped page can have the unknown height 0xFFFFFFFF, see 7.4.8.2 - the page
    // grows with the regions painted onto it, and the end of stripe segments (7.4.10)
    // tell the end row of each stripe
    const QStringList stripe1 = { "#.#.#.#.", "........" };
    const QStringList stripe2 = { "........", "########", "#......#" };

    QByteArray stream = createEncodedStream(stripe1);
    writeUInt32(stream, 15, 0xFFFFFFFF);
    stream[28] = static_cast<char>(static_cast<unsigned char>(0x80));    // Page is striped, the maximum stripe size is 0x0003
    stream[29] = char(0x03);

    QByteArray endOfStripe;
    appendUInt32(endOfStripe, 1);
    appendSegmentHeader(stream, 2, EndOfStripe, uint32_t(endOfStripe.size()));
    stream.append(endOfStripe);

    QByteArray second = createEncodedStream(stripe2).mid(RegionHeaderOffset);
    writeUInt32(second, 0, 3);
    writeUInt32(second, RegionYOffset - RegionHeaderOffset, 2);
    stream.append(second);

    endOfStripe.clear();
    appendUInt32(endOfStripe, 4);
    appendSegmentHeader(stream, 4, EndOfStripe, uint32_t(endOfStripe.size()));
    stream.append(endOfStripe);

    ErrorCollector errorCollector;
    const pdf::PDFImageData imageData = decode(stream, &errorCollector);
    QCOMPARE(errorCollector.messages, QStringList());
    QCOMPARE(imageData.getHeight(), 5u);
    QCOMPARE(drawImage(imageData), stripe1 + stripe2);
}

void JBIG2Test::test_intermediate_and_lossless_generic_regions()
{
    // An intermediate region is kept for the later segments and it is not painted
    // onto the page, while an immediate lossless region is painted like the immediate one
    const QStringList image = { "#.#.", ".#.#" };
    const QStringList white = { "....", "...." };

    QByteArray intermediate = createEncodedStream(image);
    intermediate[RegionHeaderOffset + 4] = char(IntermediateGenericRegion);
    QCOMPARE(drawImage(decode(intermediate)), white);

    QByteArray lossless = createEncodedStream(image);
    lossless[RegionHeaderOffset + 4] = char(ImmediateLosslessGenericRegion);
    QCOMPARE(drawImage(decode(lossless)), image);
}

void JBIG2Test::test_auxiliary_segments_are_skipped()
{
    // Profiles (7.4.12) and extensions (7.4.14) carry no image data and they are
    // skipped, unless an extension is marked as necessary for the decoding
    const QStringList image = { "##..", "..##" };
    const QByteArray stream = createEncodedStream(image);

    QByteArray profiles = stream.left(RegionHeaderOffset);
    appendSegmentHeader(profiles, 5, Profiles, 4);
    appendUInt32(profiles, 0);
    profiles.append(stream.mid(RegionHeaderOffset));
    QCOMPARE(drawImage(decode(profiles)), image);

    // A single byte coded comment, see 7.4.15.1
    const QByteArray comment = QByteArray("\x20\x00\x00\x00", 4) + QByteArray("key\0value\0", 10);
    QByteArray extension = stream.left(RegionHeaderOffset);
    appendSegmentHeader(extension, 5, Extension, uint32_t(comment.size()));
    extension.append(comment);
    extension.append(stream.mid(RegionHeaderOffset));
    QCOMPARE(drawImage(decode(extension)), image);

    // The necessary bit is the bit 31 of the extension type
    QByteArray necessary = extension;
    necessary[RegionHeaderOffset + RegionHeaderSize] = static_cast<char>(static_cast<unsigned char>(0xA0));
    QVERIFY(decodeExpectingError(necessary).contains("necessary"));

    // The extension type alone is not marked as necessary by its lower bits
    QByteArray unnecessary = extension;
    unnecessary[RegionHeaderOffset + RegionHeaderSize] = char(0x28);
    QCOMPARE(drawImage(decode(unnecessary)), image);

    // An end of page segment is ignored with a warning, because it must not be in PDF
    QByteArray endOfPage = stream;
    appendSegmentHeader(endOfPage, 5, 49, 0);
    ErrorCollector errorCollector;
    QCOMPARE(drawImage(decode(endOfPage, &errorCollector)), image);
    QCOMPARE(errorCollector.messages.size(), 1);
}

void JBIG2Test::test_custom_huffman_table_segment_is_parsed()
{
    // A table segment, see 7.4.13 and B.2 - the flags select the out-of-band value and
    // the widths of the prefix and range length fields, then the lines of the table
    // follow for the values from the lowest to the highest one, terminated by the lower
    // range, upper range and out-of-band lines. The prefix lengths 1, 2, 3, 4 and 4
    // form a complete prefix code.
    QByteArray table;
    table.append(char(0x15));   // HTOOB = 1, HTPS = 3 bits, HTRS = 2 bits
    appendUInt32(table, 0);     // HTLOW
    appendUInt32(table, 4);     // HTHIGH

    // Lines: (prefix 1, range 1), (prefix 2, range 1), lower range prefix 3, upper range
    // prefix 4, out-of-band prefix 4 - the bits 001 01 010 01 011 100 100 padded by zeros
    table.append(char(0b00101010));
    table.append(char(0b01011100));
    table.append(static_cast<char>(static_cast<unsigned char>(0b10000000)));

    const QStringList image = { "#..#", ".##." };
    const QByteArray stream = createEncodedStream(image);

    QByteArray withTable = stream.left(RegionHeaderOffset);
    appendSegmentHeader(withTable, 5, Tables, uint32_t(table.size()));
    withTable.append(table);
    withTable.append(stream.mid(RegionHeaderOffset));

    ErrorCollector errorCollector;
    QCOMPARE(drawImage(decode(withTable, &errorCollector)), image);
    QCOMPARE(errorCollector.messages, QStringList());

    // The lowest value can not be decremented
    QByteArray underflow = withTable;
    writeUInt32(underflow, RegionHeaderOffset + RegionHeaderSize + 1, 0x80000000);
    QVERIFY(decodeExpectingError(underflow).contains("underflow"));
}

void JBIG2Test::test_malformed_generic_region_segments_are_refused()
{
    const QStringList image = { "#..#", ".##." };
    const QByteArray stream = createEncodedStream(image);

    // Reserved bits of the generic region flags
    QByteArray reservedFlags = stream;
    reservedFlags[GenericFlagsOffset] = char(0x18);
    QVERIFY(decodeExpectingError(reservedFlags).contains("generic region flags"));

    // Reserved bits of the region segment information flags and an unknown operator
    QByteArray reservedInformationFlags = stream;
    reservedInformationFlags[RegionOperatorOffset] = char(0x08);
    QVERIFY(decodeExpectingError(reservedInformationFlags).contains("flags are invalid"));

    QByteArray unknownOperator = stream;
    unknownOperator[RegionOperatorOffset] = char(0x05);
    QVERIFY(decodeExpectingError(unknownOperator).contains("bit operation"));

    // An empty region
    QByteArray zeroWidth = stream;
    writeUInt32(zeroWidth, RegionInformationOffset, 0);
    QVERIFY(decodeExpectingError(zeroWidth).contains("invalid bitmap size"));

    QByteArray zeroHeight = stream;
    writeUInt32(zeroHeight, RegionInformationOffset + 4, 0);
    QVERIFY(decodeExpectingError(zeroHeight).contains("invalid bitmap size"));

    // The data length beyond the end of the stream
    QByteArray longData = stream;
    writeUInt32(longData, RegionDataLengthOffset, 0x7FFFFFF0);
    QVERIFY(decodeExpectingError(longData).contains("invalid data length"));

    // An unknown segment type
    QByteArray unknownType = stream;
    unknownType[RegionHeaderOffset + 4] = char(37);
    QVERIFY(decodeExpectingError(unknownType).contains("segment type"));

    // The data length below the size of the region header
    QByteArray shortData = stream;
    writeUInt32(shortData, RegionDataLengthOffset, 10);
    QVERIFY(!decodeExpectingError(shortData).isEmpty());

    // A page information segment with an extra byte in the region data is refused,
    // because the segment data length does not match the read data
    QByteArray extraByte = stream;
    extraByte.insert(GenericFlagsOffset + 1, char(0x00));
    QVERIFY(!decodeExpectingError(extraByte).isEmpty());
}

void JBIG2Test::test_text_region_reference_corners()
{
    // The reference corner and the transposition of a text region (7.4.3.1.1) select,
    // how the symbol instances are placed, see 6.4.5 step 3 c) x). They do not change
    // the coded data, so the text region of the annex H can be decoded with every
    // combination - the symbols are just placed differently.
    const QByteArray stream = createAnnexHRefinementPageStream();
    const int flagsOffset = stream.indexOf(QByteArray::fromHex("8C1200000004"));
    QVERIFY(flagsOffset > 0);

    const QStringList expectedImage = drawImage(decode(stream));
    int variants = 0;

    for (const uint8_t REFCORNER : { uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3) })
    {
        for (const bool TRANSPOSED : { false, true })
        {
            QByteArray patched = stream;
            const uint16_t flags = uint16_t((0x8C12 & ~0x0070) | (REFCORNER << 4) | (TRANSPOSED ? 0x0040 : 0x0000));
            patched[flagsOffset] = static_cast<char>(static_cast<unsigned char>(flags >> 8));
            patched[flagsOffset + 1] = static_cast<char>(static_cast<unsigned char>(flags & 0xFF));

            ErrorCollector errorCollector;
            const pdf::PDFImageData imageData = decode(patched, &errorCollector);
            QCOMPARE(errorCollector.messages, QStringList());
            QCOMPARE(imageData.getWidth(), 37u);
            QCOMPARE(imageData.getHeight(), 8u);

            // The original combination is TOPLEFT without the transposition
            if (REFCORNER == 1 && !TRANSPOSED)
            {
                QCOMPARE(drawImage(imageData), expectedImage);
            }

            ++variants;
        }
    }

    QCOMPARE(variants, 8);
}

void JBIG2Test::test_refinement_region_segments()
{
    // A generic refinement region (7.4.7) refines either the bitmap of an intermediate
    // region it refers to, or the page area it covers. With the typical prediction every
    // pixel, whose neighbourhood in the reference is uniform, is copied without decoding.
    // The pixels outside of the reference are zero, so a white reference is uniform
    // everywhere and the region is the white reference itself - the coded data are just
    // the SLTP bits of the rows. The region is combined with the page by XNOR, so the
    // white region paints the black rectangle, which proves it has been painted.
    const QStringList white = { "......", "......", "......" };
    const QStringList page = { "........", "........", "........", "........", "........" };
    const QStringList expected = { "........", ".######.", ".######.", ".######.", "........" };

    auto createRefinementData = [](uint32_t width, uint32_t height, int32_t x, int32_t y, uint8_t combinationOperator, uint8_t GRTEMPLATE, bool TPGRON, const QByteArray& codedData)
    {
        QByteArray data;
        appendUInt32(data, width);
        appendUInt32(data, height);
        appendUInt32(data, uint32_t(x));
        appendUInt32(data, uint32_t(y));
        data.append(char(combinationOperator));

        // Generic refinement region segment flags, see 7.4.7.2, and the AT pixels of the
        // template 0, see 7.4.7.3
        data.append(char(GRTEMPLATE | (TPGRON ? 0x02 : 0x00)));
        if (GRTEMPLATE == 0)
        {
            const char atPixels[] = { -1, -1, -1, -1 };
            data.append(atPixels, sizeof(atPixels));
        }

        data.append(codedData);
        return data;
    };

    // The intermediate region is stored under its segment number 1 and it is not painted
    const QByteArray intermediateRegion = createEncodedStream(white).mid(RegionHeaderOffset);
    QByteArray stream = createEncodedStream(page).left(RegionHeaderOffset);
    stream.append(intermediateRegion);
    stream[RegionHeaderOffset + 4] = char(IntermediateGenericRegion);

    {
        QByteArray refinement = stream;
        const QByteArray data = createRefinementData(6, 3, 1, 1, 3, 0, true, createTypicalRefinementData(3, 0));
        appendSegmentHeaderWithReferences(refinement, 2, 42, { 1 }, uint32_t(data.size()));
        refinement.append(data);

        ErrorCollector errorCollector;
        QCOMPARE(drawImage(decode(refinement, &errorCollector)), expected);
        QCOMPARE(errorCollector.messages, QStringList());

        // An intermediate refinement region is stored instead of being painted, and
        // a lossless one is painted
        QByteArray intermediate = refinement;
        intermediate[stream.size() + 4] = char(40);
        QCOMPARE(drawImage(decode(intermediate)), page);

        QByteArray lossless = refinement;
        lossless[stream.size() + 4] = char(43);
        QCOMPARE(drawImage(decode(lossless)), expected);
    }

    // The templates 0 and 1 without the typical prediction decode every pixel - the
    // decisions are then interpreted as pixels, so only the size is checked
    for (const uint8_t GRTEMPLATE : { uint8_t(0), uint8_t(1) })
    {
        QByteArray refinement = stream;
        const QByteArray data = createRefinementData(6, 3, 1, 1, 0, GRTEMPLATE, false, createTypicalRefinementData(30, GRTEMPLATE));
        appendSegmentHeaderWithReferences(refinement, 2, 42, { 1 }, uint32_t(data.size()));
        refinement.append(data);

        ErrorCollector errorCollector;
        const pdf::PDFImageData imageData = decode(refinement, &errorCollector);
        QCOMPARE(errorCollector.messages, QStringList());
        QCOMPARE(imageData.getWidth(), 8u);
        QCOMPARE(imageData.getHeight(), 5u);
    }

    // The template 1 with the typical prediction
    {
        QByteArray refinement = stream;
        const QByteArray data = createRefinementData(6, 3, 1, 1, 3, 1, true, createTypicalRefinementData(3, 1));
        appendSegmentHeaderWithReferences(refinement, 2, 42, { 1 }, uint32_t(data.size()));
        refinement.append(data);
        QCOMPARE(drawImage(decode(refinement)), expected);
    }

    // A region referring to no segment refines the page area it covers, and it must
    // replace it. The white page is uniform, so the refined area stays white.
    {
        QByteArray pageRefinement = createEncodedStream(page).left(RegionHeaderOffset);
        const QByteArray data = createRefinementData(8, 5, 0, 0, 4, 0, true, createTypicalRefinementData(5, 0));
        appendSegmentHeaderWithReferences(pageRefinement, 1, 42, { }, uint32_t(data.size()));
        pageRefinement.append(data);

        ErrorCollector errorCollector;
        QCOMPARE(drawImage(decode(pageRefinement, &errorCollector)), page);
        QCOMPARE(errorCollector.messages, QStringList());

        QByteArray notReplaced = pageRefinement;
        notReplaced[RegionHeaderOffset + RegionHeaderSize + 16] = char(0x00);
        QVERIFY(decodeExpectingError(notReplaced).contains("REPLACE"));
    }

    // The referred bitmap must have the size of the region
    for (const std::pair<uint32_t, uint32_t> size : { std::make_pair(5u, 3u), std::make_pair(6u, 2u) })
    {
        QByteArray mismatch = stream;
        const QByteArray data = createRefinementData(size.first, size.second, 1, 1, 0, 0, true, createTypicalRefinementData(3, 0));
        appendSegmentHeaderWithReferences(mismatch, 2, 42, { 1 }, uint32_t(data.size()));
        mismatch.append(data);
        QVERIFY(decodeExpectingError(mismatch).contains("invalid referred bitmap size"));
    }

    // The referred segment must exist and there must be at most one
    {
        QByteArray missing = stream;
        const QByteArray data = createRefinementData(6, 3, 1, 1, 0, 0, true, createTypicalRefinementData(3, 0));
        appendSegmentHeaderWithReferences(missing, 2, 42, { 7 }, uint32_t(data.size()));
        missing.append(data);
        QVERIFY(!decodeExpectingError(missing).isEmpty());

        QByteArray pageInformationReference = stream;
        appendSegmentHeaderWithReferences(pageInformationReference, 2, 42, { 0 }, uint32_t(data.size()));
        pageInformationReference.append(data);
        QVERIFY(!decodeExpectingError(pageInformationReference).isEmpty());

        QByteArray two = stream;
        appendSegmentHeaderWithReferences(two, 2, 42, { 1, 1 }, uint32_t(data.size()));
        two.append(data);
        QVERIFY(decodeExpectingError(two).contains("invalid referred segments"));
    }

    // Reserved flags of the refinement region
    {
        QByteArray reserved = stream;
        const QByteArray data = createRefinementData(6, 3, 1, 1, 0, 4, true, createTypicalRefinementData(3, 0));
        appendSegmentHeaderWithReferences(reserved, 2, 42, { 1 }, uint32_t(data.size()));
        reserved.append(data);
        QVERIFY(decodeExpectingError(reserved).contains("invalid flags"));
    }
}

void JBIG2Test::test_symbol_dictionary_flag_validation()
{
    // The flags of a symbol dictionary (7.4.2.1.1) restrict each other - the huffman
    // table selections are only valid with the huffman coding, the arithmetic coding
    // state flags and the refinement template only with the refinement/aggregate coding
    // when the huffman coding is used, and the upper bits are reserved
    const QByteArray pageInformation = createPageInformationData(64, 32);

    auto createStream = [&pageInformation](uint16_t flags, uint32_t numberOfExportedSymbols, uint32_t numberOfNewSymbols)
    {
        QByteArray data;
        data.append(char((flags >> 8) & 0xFF));
        data.append(char(flags & 0xFF));

        const bool SDHUFF = flags & 0x0001;
        const bool SDREFAGG = flags & 0x0002;
        const uint8_t SDTEMPLATE = (flags >> 10) & 0x03;
        const uint8_t SDRTEMPLATE = (flags >> 12) & 0x01;

        if (!SDHUFF)
        {
            data.append((SDTEMPLATE == 0) ? 8 : 2, char(0x00));
        }

        if (SDREFAGG && SDRTEMPLATE == 0)
        {
            data.append(4, char(0x00));
        }

        appendUInt32(data, numberOfExportedSymbols);
        appendUInt32(data, numberOfNewSymbols);

        QByteArray stream;
        appendSegmentHeader(stream, 0, PageInformation, uint32_t(pageInformation.size()));
        stream.append(pageInformation);
        appendSegmentHeader(stream, 1, SymbolDictionary, uint32_t(data.size()));
        stream.append(data);
        return stream;
    };

    // Huffman table selections without the huffman coding, each of them alone
    for (const uint16_t flags : { uint16_t(0x0004), uint16_t(0x0010), uint16_t(0x0040), uint16_t(0x0080) })
    {
        QVERIFY2(decodeExpectingError(createStream(flags, 1, 1)).contains("invalid flags"), qPrintable(QString::number(flags, 16)));
    }

    // Arithmetic coding state flags and the refinement template with the huffman coding
    // without the refinement/aggregate coding, each of them alone
    for (const uint16_t flags : { uint16_t(0x0101), uint16_t(0x0201), uint16_t(0x1001) })
    {
        QVERIFY2(decodeExpectingError(createStream(flags, 1, 1)).contains("invalid flags"), qPrintable(QString::number(flags, 16)));
    }

    // The template must be zero with the huffman coding, and the upper bits are reserved
    QVERIFY(decodeExpectingError(createStream(0x0401, 1, 1)).contains("invalid flags"));
    QVERIFY(decodeExpectingError(createStream(0x2000, 1, 1)).contains("invalid flags"));

    // Either symbol count can exceed the limit, including a dictionary using the
    // refinement/aggregate coding with the template 1, which has no refinement AT pixels
    QVERIFY(decodeExpectingError(createStream(0x0000, 1, 0x7FFFFFFF)).contains("symbol count"));
    QVERIFY(decodeExpectingError(createStream(0x0000, 0x7FFFFFFF, 1)).contains("symbol count"));
    QVERIFY(decodeExpectingError(createStream(0x1002, 0x7FFFFFFF, 1)).contains("symbol count"));
    QVERIFY(decodeExpectingError(createStream(0x0002, 1, 0x7FFFFFFF)).contains("symbol count"));
}

void JBIG2Test::test_pattern_dictionary_rejects_zero_pattern_size()
{
    // A pattern dictionary (7.4.4) with an empty pattern has nothing to decode
    const QByteArray pageInformation = createPageInformationData(64, 32);

    for (const std::pair<uint8_t, uint8_t> size : { std::make_pair(uint8_t(0), uint8_t(4)), std::make_pair(uint8_t(4), uint8_t(0)) })
    {
        QByteArray data;
        data.append(char(0x01));    // MMR coding
        data.append(char(size.first));
        data.append(char(size.second));
        appendUInt32(data, 0);      // GRAYMAX

        QByteArray stream;
        appendSegmentHeader(stream, 0, PageInformation, uint32_t(pageInformation.size()));
        stream.append(pageInformation);
        appendSegmentHeader(stream, 1, 16, uint32_t(data.size()));
        stream.append(data);

        QVERIFY(decodeExpectingError(stream).contains("invalid pattern size"));
    }

    // Reserved flags
    QByteArray data;
    data.append(char(0x08));
    data.append(char(4));
    data.append(char(4));
    appendUInt32(data, 0);

    QByteArray stream;
    appendSegmentHeader(stream, 0, PageInformation, uint32_t(pageInformation.size()));
    stream.append(pageInformation);
    appendSegmentHeader(stream, 1, 16, uint32_t(data.size()));
    stream.append(data);
    QVERIFY(decodeExpectingError(stream).contains("pattern dictionary flags"));
}

void JBIG2Test::test_region_outside_page_is_clipped()
{
    // A region can lie partially or completely outside of the page - the part outside
    // is not painted. The location of a region is unsigned (7.4.1.3 and 7.4.1.4), so
    // a region can not start before the page.
    const QStringList image = { "####", "#..#", "####" };

    struct Case
    {
        int32_t x;
        int32_t y;
        QStringList expected;
    };

    const std::vector<Case> cases =
    {
        { 8, 0, { "........", "........", "........", "........" } },
        { 0, 4, { "........", "........", "........", "........" } },
        { 6, 2, { "........", "........", "......##", "......#." } }
    };

    for (const Case& testCase : cases)
    {
        QByteArray stream = createEncodedStream(image);
        writeUInt32(stream, 15, 4);     // The page is 8 by 4 pixels
        writeUInt32(stream, 11, 8);
        writeUInt32(stream, RegionYOffset - 4, uint32_t(testCase.x));
        writeUInt32(stream, RegionYOffset, uint32_t(testCase.y));

        ErrorCollector errorCollector;
        QCOMPARE(drawImage(decode(stream, &errorCollector)), testCase.expected);
        QCOMPARE(errorCollector.messages, QStringList());
    }

    QByteArray negative = createEncodedStream(image);
    writeUInt32(negative, RegionYOffset - 4, uint32_t(-2));
    QVERIFY(!decodeExpectingError(negative).isEmpty());

    // A page of the unknown height grows with the regions, but a region inside the
    // already painted area does not grow it
    QByteArray striped = createEncodedStream(image);
    writeUInt32(striped, 11, 8);
    writeUInt32(striped, 15, 0xFFFFFFFF);
    striped[28] = static_cast<char>(static_cast<unsigned char>(0x80));
    striped[29] = char(0x10);

    QByteArray inner = createEncodedStream({ "##" }).mid(RegionHeaderOffset);
    writeUInt32(inner, 0, 2);
    writeUInt32(inner, RegionYOffset - RegionHeaderOffset - 4, 5);
    writeUInt32(inner, RegionYOffset - RegionHeaderOffset, 1);
    striped.append(inner);

    QCOMPARE(drawImage(decode(striped)), QStringList({ "####....", "#..#.##.", "####...." }));
}

void JBIG2Test::test_bitmap_api_validation()
{
    // The bitmap refuses negative and oversized dimensions and invalid row copies
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Bitmap::checkSize(-1, 1));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Bitmap::checkSize(1, -1));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Bitmap::checkSize(65536, 65536));
    pdf::PDFJBIG2Bitmap::checkSize(0, 0);
    pdf::PDFJBIG2Bitmap::checkSize(65536, 16384);

    pdf::PDFJBIG2Bitmap bitmap(4, 3, 0x00);
    bitmap.setPixel(1, 0, 0xFF);
    bitmap.copyRow(2, 0);
    QCOMPARE(int(bitmap.getPixel(1, 2)), 0xFF);
    QCOMPARE(int(bitmap.getPixel(1, 1)), 0x00);
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, bitmap.copyRow(3, 0));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, bitmap.copyRow(0, 3));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, bitmap.copyRow(-1, 0));
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, bitmap.copyRow(0, -1));

    // A subbitmap outside of the bitmap is zero, the pixels outside are zero as well
    const pdf::PDFJBIG2Bitmap subbitmap = bitmap.getSubbitmap(-1, -1, 6, 5);
    QCOMPARE(subbitmap.getWidth(), 6);
    QCOMPARE(subbitmap.getHeight(), 5);
    QCOMPARE(int(subbitmap.getPixel(2, 1)), 0xFF);
    QCOMPARE(int(subbitmap.getPixel(0, 0)), 0x00);
    QCOMPARE(int(subbitmap.getPixel(5, 4)), 0x00);
    QCOMPARE(int(bitmap.getPixelSafe(-1, 0)), 0x00);
    QCOMPARE(int(bitmap.getPixelSafe(4, 0)), 0x00);
    QCOMPARE(int(bitmap.getPixelSafe(0, 3)), 0x00);

    // Painting an invalid bitmap does nothing
    pdf::PDFJBIG2Bitmap invalid;
    QVERIFY(!invalid.isValid());
    bitmap.paint(invalid, 0, 0, pdf::PDFJBIG2BitOperation::Or, false, 0x00);
    QCOMPARE(bitmap.getPixelCount(), 12);

    // The paint operation must be valid, and a bitmap painted outside of the bitmap
    // is clipped - also to the left and to the top
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, bitmap.paint(subbitmap, 0, 0, pdf::PDFJBIG2BitOperation::Invalid, false, 0x00));
    pdf::PDFJBIG2Bitmap black(2, 2, 0xFF);
    bitmap.fillZero();
    bitmap.paint(black, -1, -1, pdf::PDFJBIG2BitOperation::Or, false, 0x00);
    bitmap.paint(black, 3, 2, pdf::PDFJBIG2BitOperation::Or, false, 0x00);
    QCOMPARE(int(bitmap.getPixel(0, 0)), 0xFF);
    QCOMPARE(int(bitmap.getPixel(1, 0)), 0x00);
    QCOMPARE(int(bitmap.getPixel(0, 1)), 0x00);
    QCOMPARE(int(bitmap.getPixel(3, 2)), 0xFF);
    QCOMPARE(int(bitmap.getPixel(2, 2)), 0x00);
    QCOMPARE(int(bitmap.getPixel(3, 1)), 0x00);
}

void JBIG2Test::test_referred_segments_are_resolved()
{
    // The segments referred to by a symbol dictionary are looked up before it is decoded -
    // a missing segment is an error, and a bitmap of an intermediate region is accepted
    // as a referred segment
    const QByteArray dictionaryData = createSymbolDictionaryData(0x0000, 1, 1) + QByteArray(8, char(0x55));

    QByteArray missing = createEncodedStream({ "#.#." }).left(RegionHeaderOffset);
    appendSegmentHeaderWithReferences(missing, 1, SymbolDictionary, { 7 }, uint32_t(dictionaryData.size()));
    missing.append(dictionaryData);
    QVERIFY(decodeExpectingError(missing).contains("invalid referred segment 7"));

    QByteArray bitmapReference = createEncodedStream({ "#.#." });
    bitmapReference[RegionHeaderOffset + 4] = char(IntermediateGenericRegion);
    appendSegmentHeaderWithReferences(bitmapReference, 2, SymbolDictionary, { 1 }, uint32_t(dictionaryData.size()));
    bitmapReference.append(dictionaryData);

    // The dictionary itself is not decodable from the random data, but the reference
    // to the bitmap is resolved without an error
    QVERIFY(!decodeExpectingError(bitmapReference).contains("invalid referred segment"));
}

QTEST_APPLESS_MAIN(JBIG2Test)

#include "tst_jbig2test.moc"
