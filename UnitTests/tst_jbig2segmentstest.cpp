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
#include "pdfccittfaxencoder.h"

#include <QtTest>

#include <optional>
#include <stdexcept>
#include <tuple>

/// Tests of the JBIG2 decoder for the segments, which the encoder of the library does
/// not produce - symbol dictionaries, text regions, pattern dictionaries and halftone
/// regions in both the arithmetic and the huffman variant, and the file organisations.
/// The data of the segments are built by small encoders of this file - the integer
/// arithmetic encoding procedure of the annex A, the standard huffman tables of the
/// annex B transcribed from the specification, the prefix assignment of B.3 and the
/// refinement coding of 6.3 - and the decoded page is compared with the expected image.
class JBIG2SegmentsTest : public QObject
{
    Q_OBJECT

private slots:
    void test_arithmetic_symbol_dictionary_and_text_region();
    void test_arithmetic_coding_context_reuse();
    void test_refinement_aggregate_symbol_dictionaries();
    void test_huffman_symbol_dictionaries();
    void test_huffman_text_regions();
    void test_huffman_table_selection_errors();
    void test_export_flags_errors();
    void test_text_region_errors();
    void test_pattern_dictionary_and_halftone_region();
    void test_halftone_region_errors();
    void test_file_organisations();
    void test_segment_header_and_data_length_errors();
    void test_segment_header_field_sizes();
    void test_arithmetic_decoder_termination();
    void test_refinement_region_typical_prediction();
    void test_huffman_table_segment_errors();

private:
    /// Image drawn as one line of text per row, a hash is a black pixel
    using Image = QStringList;

    static int imageWidth(const Image& image) { return image.isEmpty() ? 0 : int(image.front().size()); }
    static int imageHeight(const Image& image) { return int(image.size()); }

    /// Returns true, if the pixel is black. Pixels outside of the image are white.
    static bool isBlack(const Image& image, int x, int y);

    /// Creates a white (or black) image
    static Image createImage(int width, int height, bool black = false);

    /// Composes the source image onto the target image at the position by the
    /// combination operator - the composition of 7.4.1.5 with clipping
    static void compose(Image& target, const Image& source, int x, int y, pdf::PDFJBIG2BitOperation operation);

    static Image invertImage(const Image& image);

    /// Packed bitmap for the encoder of the library
    struct Bitmap
    {
        int width = 0;
        int height = 0;
        int stride = 0;
        std::vector<uint8_t> data;

        pdf::PDFBitonalBitmapView view() const;
    };

    static Bitmap createBitmap(const Image& image);

    /// Bitmap of the decoder (a byte per pixel, 0xFF is black), used as the skip bitmap
    static pdf::PDFJBIG2Bitmap createDecoderBitmap(const Image& image);

    /// Writer of the bits of the huffman coded data, the most significant bit first
    class BitWriter
    {
    public:
        void writeBit(uint32_t bit);
        void writeBits(uint32_t value, int count);
        void writeCode(const QByteArray& code);
        void align();
        void append(const QByteArray& bytes);
        const QByteArray& data() const { return m_data; }

    private:
        QByteArray m_data;
        int m_bitCount = 0;
    };

    /// A line of a huffman table, see B.2 of the specification
    struct HuffmanLine
    {
        enum Kind
        {
            Range,      ///< Values low ... low + 2^rangeBits - 1
            Lower,      ///< Values ... low, coded as low - value in 32 bits
            Upper,      ///< Values low ..., coded as value - low in 32 bits
            OutOfBand
        };

        Kind kind;
        int32_t low;
        int rangeBits;
        QByteArray prefix;
    };

    using HuffmanTable = std::vector<HuffmanLine>;

    /// Returns the standard huffman table B.1 (A) to B.15 (O) of the specification
    static HuffmanTable standardTable(char name);

    /// Writes the value (or the out-of-band value) by the huffman table
    static void writeHuffman(BitWriter& writer, const HuffmanTable& table, std::optional<int32_t> value);

    /// Assigns the prefix codes to the prefix lengths by the procedure of B.3. A zero
    /// length gets no code.
    static std::vector<QByteArray> assignPrefixCodes(const std::vector<int>& lengths);

    /// Custom huffman table - the source of both the data of a table segment (7.4.13,
    /// B.2) and of the table used by the encoder
    struct CustomTable
    {
        bool HTOOB = false;
        int HTPS = 4;
        int HTRS = 4;
        int32_t HTLOW = 0;
        std::vector<std::pair<int, int>> lines;     ///< Prefix length and range length of the range lines from HTLOW
        int lowerPrefixLength = 0;
        int upperPrefixLength = 0;
        int oobPrefixLength = 0;

        int32_t high() const;
        QByteArray segmentData() const;
        HuffmanTable table() const;
    };

    /// Contexts of the arithmetic coding of a symbol dictionary or a text region, see
    /// the annex A and 6.2.5.7 / 6.3.5.6
    struct ArithmeticContexts
    {
        pdf::PDFJBIG2ArithmeticDecoderState IADH;
        pdf::PDFJBIG2ArithmeticDecoderState IADW;
        pdf::PDFJBIG2ArithmeticDecoderState IAEX;
        pdf::PDFJBIG2ArithmeticDecoderState IAAI;
        pdf::PDFJBIG2ArithmeticDecoderState IADT;
        pdf::PDFJBIG2ArithmeticDecoderState IAFS;
        pdf::PDFJBIG2ArithmeticDecoderState IADS;
        pdf::PDFJBIG2ArithmeticDecoderState IAIT;
        pdf::PDFJBIG2ArithmeticDecoderState IARI;
        pdf::PDFJBIG2ArithmeticDecoderState IARDW;
        pdf::PDFJBIG2ArithmeticDecoderState IARDH;
        pdf::PDFJBIG2ArithmeticDecoderState IARDX;
        pdf::PDFJBIG2ArithmeticDecoderState IARDY;
        pdf::PDFJBIG2ArithmeticDecoderState IAID;
        pdf::PDFJBIG2ArithmeticDecoderState generic;
        pdf::PDFJBIG2ArithmeticDecoderState refinement;

        void reset(uint32_t symbolCodeLength, uint8_t GBTEMPLATE, uint8_t GRTEMPLATE);
    };

    /// Encodes the integer by the integer arithmetic encoding procedure - the inverse
    /// of the procedure A.2. An empty value is the out-of-band value.
    static void encodeInteger(pdf::PDFJBIG2ArithmeticEncoder& encoder, pdf::PDFJBIG2ArithmeticDecoderState& state, std::optional<int32_t> value);

    /// Encodes the symbol ID, the inverse of the procedure A.3
    static void encodeIAID(pdf::PDFJBIG2ArithmeticEncoder& encoder, pdf::PDFJBIG2ArithmeticDecoderState& state, uint32_t codeLength, uint32_t value);

    /// Encodes the target image by the generic refinement region decoding procedure
    /// (6.3). The context is formed in the order the decoder forms it, see figures 12
    /// and 13. With the typical prediction a row is typical, when every pixel, whose
    /// neighbourhood in the reference is uniform, has the value of the reference - such
    /// pixels of a typical row are not coded (6.3.5.6).
    /// \param encoder Arithmetic encoder
    /// \param state State of the refinement contexts
    /// \param target Coded image
    /// \param reference Reference image
    /// \param referenceX GRREFERENCEDX
    /// \param referenceY GRREFERENCEDY
    /// \param GRTEMPLATE Refinement template
    /// \param at Adaptive template pixels A1 and A2 of the template 0
    /// \param TPGRON Use the typical prediction
    static void encodeRefinement(pdf::PDFJBIG2ArithmeticEncoder& encoder,
                                 pdf::PDFJBIG2ArithmeticDecoderState& state,
                                 const Image& target,
                                 const Image& reference,
                                 int referenceX,
                                 int referenceY,
                                 uint8_t GRTEMPLATE,
                                 const pdf::PDFJBIG2ATPositions& at,
                                 bool TPGRON = false);

    /// Encodes the image by the arithmetic generic region decoding procedure into the
    /// encoder, with the nominal adaptive template pixels of the template
    static void encodeGeneric(pdf::PDFJBIG2ArithmeticEncoder& encoder, pdf::PDFJBIG2ArithmeticDecoderState& state, const Image& image, uint8_t GBTEMPLATE);

    /// Encodes the image by the MMR coding
    /// \param image Coded image
    /// \param endOfBlock Terminate the data by EOFB
    static QByteArray encodeMMR(const Image& image, bool endOfBlock);

    static constexpr pdf::PDFJBIG2ATPositions NOMINAL_REFINEMENT_AT = { { { -1, -1 }, { -1, -1 }, { 0, 0 }, { 0, 0 } } };

    /// Groups of the indices of the symbols with the same height - the height classes
    /// of a symbol dictionary. The symbols must be sorted by their height.
    static std::vector<std::vector<size_t>> getHeightClasses(const std::vector<Image>& symbols);

    /// Parameters of a symbol dictionary segment, see 7.4.2.1
    struct SymbolDictionaryOptions
    {
        bool huffman = false;
        bool refinement = false;
        uint8_t SDHUFFDH = 0;
        uint8_t SDHUFFDW = 0;
        uint8_t SDHUFFBMSIZE = 0;
        uint8_t SDHUFFAGGINST = 0;
        bool contextUsed = false;
        bool contextRetained = false;
        uint8_t SDTEMPLATE = 0;
        uint8_t SDRTEMPLATE = 0;
        pdf::PDFJBIG2ATPositions SDRAT = NOMINAL_REFINEMENT_AT;

        /// User tables in the order of the selections DH, DW, BMSIZE, AGGINST
        std::vector<HuffmanTable> userTables;

        /// Code the collective bitmap of a huffman dictionary uncompressed (BMSIZE = 0)
        bool uncompressedCollectiveBitmap = false;

        /// Export flags of the input and the new symbols, all symbols are exported, when empty
        std::vector<bool> exportFlags;

        /// Number of the symbols of the referred dictionaries
        uint32_t inputSymbolCount = 0;

        /// Number of the exported symbols written into the header, when set
        std::optional<uint32_t> exportedCountOverride;

        /// Values of the arithmetic export run lengths written instead of the computed ones, when set
        std::optional<std::vector<int32_t>> exportRunLengthsOverride;

        /// REFAGGNINST written for every symbol of a refinement dictionary
        int32_t refinementInstanceCount = 1;

        /// Symbol ID written for every symbol of a refinement dictionary, when set
        std::optional<uint32_t> refinementIdOverride;

        /// Arithmetically coded refinement data written for every symbol of a huffman
        /// refinement dictionary instead of the coded ones, when set
        std::optional<QByteArray> refinementDataOverride;

        /// Code the single symbol instance aggregation of an arithmetic refinement
        /// dictionary by the text region decoding procedure (the form of the Power JBIG-2
        /// encoder) instead of the form of 6.5.8.2.2
        bool textRegionForm = false;
    };

    /// A symbol of a refinement/aggregate symbol dictionary - the refinement of a
    /// symbol, which is either an input symbol or an already decoded new one
    struct RefinedSymbol
    {
        Image image;
        uint32_t referenceId = 0;
        int32_t RDX = 0;
        int32_t RDY = 0;
    };

    /// Returns the data of a symbol dictionary segment, see 7.4.2
    /// \param symbols New symbols, sorted by their height
    /// \param options Parameters of the dictionary
    /// \param contexts Contexts of the arithmetic coding, which are updated
    static QByteArray encodeSymbolDictionary(const std::vector<Image>& symbols, const SymbolDictionaryOptions& options, ArithmeticContexts& contexts);

    /// Returns the data of a symbol dictionary segment using the refinement/aggregate
    /// coding, whose every symbol is a refinement of a single symbol, see 6.5.8.2.2
    /// \param inputSymbols Symbols of the referred dictionaries
    /// \param symbols New symbols, sorted by their height
    /// \param options Parameters of the dictionary
    /// \param contexts Contexts of the arithmetic coding, which are updated
    static QByteArray encodeRefinementSymbolDictionary(const std::vector<Image>& inputSymbols, const std::vector<RefinedSymbol>& symbols, const SymbolDictionaryOptions& options, ArithmeticContexts& contexts);

    /// A symbol instance of a text region
    struct TextInstance
    {
        TextInstance(int x, int y, uint32_t id) :
            x(x), y(y), id(id)
        {

        }

        TextInstance(int x, int y, uint32_t id, bool refine, Image refined, int32_t RDW, int32_t RDH, int32_t RDX, int32_t RDY) :
            x(x), y(y), id(id), refine(refine), refined(std::move(refined)), RDW(RDW), RDH(RDH), RDX(RDX), RDY(RDY)
        {

        }

        int x = 0;
        int y = 0;
        uint32_t id = 0;
        bool refine = false;
        Image refined;
        int32_t RDW = 0;
        int32_t RDH = 0;
        int32_t RDX = 0;
        int32_t RDY = 0;
    };

    /// Parameters of a text region segment, see 7.4.3.1
    struct TextRegionOptions
    {
        uint32_t width = 0;
        uint32_t height = 0;
        int32_t x = 0;
        int32_t y = 0;
        uint8_t externalOperator = 0;
        bool huffman = false;
        bool refine = false;
        uint8_t logStrips = 0;
        uint8_t combinationOperator = 0;
        bool defaultPixel = false;
        int8_t dsOffset = 0;
        uint8_t SBRTEMPLATE = 0;
        pdf::PDFJBIG2ATPositions SBRAT = NOMINAL_REFINEMENT_AT;
        uint8_t SBHUFFFS = 0;
        uint8_t SBHUFFDS = 0;
        uint8_t SBHUFFDT = 0;
        uint8_t SBHUFFRDW = 0;
        uint8_t SBHUFFRDH = 0;
        uint8_t SBHUFFRDX = 0;
        uint8_t SBHUFFRDY = 0;
        uint8_t SBHUFFRSIZE = 0;

        /// User tables in the order of the selections FS, DS, DT, RDW, RDH, RDX, RDY, RSIZE
        std::vector<HuffmanTable> userTables;

        /// Prefix lengths of the symbol ID codes of a huffman region, equal lengths when empty
        std::vector<int> symbolCodeLengths;

        /// Number of the instances written into the header, when set
        std::optional<uint32_t> instanceCountOverride;

        /// Reserved bits of the huffman flags
        uint16_t reservedHuffmanFlags = 0;

        /// Run codes of the symbol ID code lengths written instead of the computed
        /// ones, when set - a run code and the value of its extra bits
        std::optional<std::vector<std::pair<int, int>>> runCodesOverride;
    };

    /// Returns the data of a text region segment, see 7.4.3
    /// \param instances Symbol instances sorted by the strip and by the column
    /// \param symbols Symbols of the referred dictionaries
    /// \param options Parameters of the region
    static QByteArray encodeTextRegion(const std::vector<TextInstance>& instances, const std::vector<Image>& symbols, const TextRegionOptions& options);

    /// Returns the data of a pattern dictionary segment, see 7.4.4
    /// \param patterns Patterns of the same size
    /// \param HDMMR MMR coding
    /// \param HDTEMPLATE Template of the arithmetic coding
    static QByteArray encodePatternDictionary(const std::vector<Image>& patterns, bool HDMMR, uint8_t HDTEMPLATE);

    /// Parameters of a halftone region segment, see 7.4.5.1
    struct HalftoneOptions
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint8_t externalOperator = 0;
        bool HMMR = false;
        uint8_t HTEMPLATE = 0;
        bool HENABLESKIP = false;
        uint8_t HCOMBOP = 0;
        bool HDEFPIXEL = false;
        int32_t HGX = 0;
        int32_t HGY = 0;
        uint16_t HRX = 0;
        uint16_t HRY = 0;
        int patternWidth = 0;
        int patternHeight = 0;
        uint32_t patternCount = 0;
    };

    /// Returns the position of the pattern of the cell of the grid, see 6.6.5.1
    static std::pair<int64_t, int64_t> getCellPosition(const HalftoneOptions& options, int m, int n);

    /// Returns true, if the pattern of the cell does not touch the region
    static bool isCellOutside(const HalftoneOptions& options, int64_t x, int64_t y);

    /// Returns the data of a halftone region segment, see 7.4.5
    /// \param values Gray-scale values of the cells of the grid, a row of the grid per item
    /// \param options Parameters of the region
    static QByteArray encodeHalftoneRegion(const std::vector<std::vector<uint32_t>>& values, const HalftoneOptions& options);

    /// Returns the image of a halftone region composed by the procedure of 6.6.5.2
    static Image composeHalftoneRegion(const std::vector<std::vector<uint32_t>>& values, const std::vector<Image>& patterns, const HalftoneOptions& options);

    static void appendUInt32(QByteArray& data, uint32_t value);
    static void appendUInt16(QByteArray& data, uint16_t value);
    static void appendInt8(QByteArray& data, int8_t value);

    /// Returns the byte as a character of a byte array
    static char toChar(unsigned value) { return static_cast<char>(static_cast<unsigned char>(value)); }
    static void appendATPositions(QByteArray& data, const pdf::PDFJBIG2ATPositions& at, int count);

    /// Appends the region segment information field, see 7.4.1
    static void appendRegionInformation(QByteArray& data, uint32_t width, uint32_t height, int32_t x, int32_t y, uint8_t externalOperator);

    /// Segment types, see 7.3
    enum SegmentType : uint8_t
    {
        SymbolDictionary = 0,
        IntermediateTextRegion = 4,
        ImmediateTextRegion = 6,
        PatternDictionary = 16,
        IntermediateHalftoneRegion = 20,
        ImmediateHalftoneRegion = 22,
        ImmediateGenericRegion = 38,
        ImmediateRefinementRegion = 42,
        PageInformation = 48,
        EndOfPage = 49,
        EndOfStripe = 50,
        EndOfFile = 51,
        Profiles = 52,
        Tables = 53,
        Extension = 62
    };

    /// Appends a segment - its header (7.2) and its data. The size of the referred segment
    /// numbers follows the segment number (7.2.5), more than four referred segments are
    /// written by the long form of the referred segments field (7.2.4), and the page
    /// association is a single byte, unless the long form is requested (7.2.6).
    static void appendSegment(QByteArray& stream, uint32_t segmentNumber, SegmentType type, const std::vector<uint32_t>& referredSegments, const QByteArray& data, bool longPageAssociation = false);

    /// Appends the segment with the given data length in the header
    static void appendSegmentWithLength(QByteArray& stream, uint32_t segmentNumber, SegmentType type, const std::vector<uint32_t>& referredSegments, const QByteArray& data, uint32_t dataLength, bool longPageAssociation = false);

    /// Returns a stream starting with a page information segment (segment 0) of the size
    static QByteArray createPageStream(uint32_t width, uint32_t height, uint8_t flags = 0x00);

    /// Returns the data of a page information segment
    static QByteArray createPageInformationData(uint32_t width, uint32_t height, uint8_t flags);

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

    /// Decodes the embedded stream and returns the page. The decoder must not report
    /// anything, unless the expected messages are given.
    static Image decodePage(const QByteArray& stream, const QStringList& expectedMessages = QStringList());

    /// Decodes the stream and returns the message of the exception, or an empty string
    static QString decodeExpectingError(const QByteArray& stream);

    /// Decodes the file and returns the page
    static Image decodeFile(const QByteArray& file);

    /// Decodes the file and returns the message of the exception, or an empty string
    static QString decodeFileExpectingError(const QByteArray& file);

    static Image drawImage(const pdf::PDFImageData& imageData);

    /// Symbols used by the tests, sorted by their height
    static std::vector<Image> getSymbols();
};

// ---------------------------------------------------------------------------------------
// Images and bitmaps

bool JBIG2SegmentsTest::isBlack(const Image& image, int x, int y)
{
    if (y < 0 || y >= image.size())
    {
        return false;
    }

    const QString& row = image[y];
    return x >= 0 && x < row.size() && row[x] == QChar('#');
}

JBIG2SegmentsTest::Image JBIG2SegmentsTest::createImage(int width, int height, bool black)
{
    Image image;
    for (int y = 0; y < height; ++y)
    {
        image << QString(width, black ? QChar('#') : QChar('.'));
    }
    return image;
}

void JBIG2SegmentsTest::compose(Image& target, const Image& source, int x, int y, pdf::PDFJBIG2BitOperation operation)
{
    for (int sy = 0; sy < imageHeight(source); ++sy)
    {
        for (int sx = 0; sx < imageWidth(source); ++sx)
        {
            const int tx = x + sx;
            const int ty = y + sy;

            if (ty < 0 || ty >= target.size() || tx < 0 || tx >= target[ty].size())
            {
                continue;
            }

            const bool s = isBlack(source, sx, sy);
            const bool t = isBlack(target, tx, ty);
            bool result = false;

            switch (operation)
            {
                case pdf::PDFJBIG2BitOperation::Or:
                    result = s || t;
                    break;
                case pdf::PDFJBIG2BitOperation::And:
                    result = s && t;
                    break;
                case pdf::PDFJBIG2BitOperation::Xor:
                    result = s != t;
                    break;
                case pdf::PDFJBIG2BitOperation::NotXor:
                    result = s == t;
                    break;
                case pdf::PDFJBIG2BitOperation::Replace:
                    result = s;
                    break;
                default:
                    throw std::logic_error("Invalid operation");
            }

            target[ty][tx] = result ? QChar('#') : QChar('.');
        }
    }
}

JBIG2SegmentsTest::Image JBIG2SegmentsTest::invertImage(const Image& image)
{
    Image result;
    for (const QString& row : image)
    {
        QString inverted = row;
        for (QChar& c : inverted)
        {
            c = (c == QChar('#')) ? QChar('.') : QChar('#');
        }
        result << inverted;
    }
    return result;
}

pdf::PDFBitonalBitmapView JBIG2SegmentsTest::Bitmap::view() const
{
    pdf::PDFBitonalBitmapView result;
    result.data = data.data();
    result.width = width;
    result.height = height;
    result.stride = stride;
    result.isOneBlack = true;
    return result;
}

JBIG2SegmentsTest::Bitmap JBIG2SegmentsTest::createBitmap(const Image& image)
{
    Bitmap bitmap;
    bitmap.width = imageWidth(image);
    bitmap.height = imageHeight(image);
    bitmap.stride = (bitmap.width + 7) / 8;
    bitmap.data.resize(size_t(bitmap.stride) * size_t(bitmap.height), 0);

    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int x = 0; x < bitmap.width; ++x)
        {
            if (isBlack(image, x, y))
            {
                bitmap.data[size_t(y) * size_t(bitmap.stride) + size_t(x >> 3)] |= uint8_t(0x80 >> (x & 7));
            }
        }
    }

    return bitmap;
}

pdf::PDFJBIG2Bitmap JBIG2SegmentsTest::createDecoderBitmap(const Image& image)
{
    pdf::PDFJBIG2Bitmap bitmap(imageWidth(image), imageHeight(image), 0x00);
    for (int y = 0; y < imageHeight(image); ++y)
    {
        for (int x = 0; x < imageWidth(image); ++x)
        {
            bitmap.setPixel(x, y, isBlack(image, x, y) ? 0xFF : 0x00);
        }
    }
    return bitmap;
}

// ---------------------------------------------------------------------------------------
// Bit writer and huffman tables

void JBIG2SegmentsTest::BitWriter::writeBit(uint32_t bit)
{
    if (m_bitCount % 8 == 0)
    {
        m_data.append(char(0));
    }

    if (bit)
    {
        char* last = m_data.data() + m_data.size() - 1;
        *last = char(uint8_t(*last) | uint8_t(0x80 >> (m_bitCount % 8)));
    }

    ++m_bitCount;
}

void JBIG2SegmentsTest::BitWriter::writeBits(uint32_t value, int count)
{
    for (int i = count - 1; i >= 0; --i)
    {
        writeBit((value >> i) & 1);
    }
}

void JBIG2SegmentsTest::BitWriter::writeCode(const QByteArray& code)
{
    for (const char c : code)
    {
        writeBit(c == '1' ? 1 : 0);
    }
}

void JBIG2SegmentsTest::BitWriter::align()
{
    m_bitCount = (m_bitCount + 7) / 8 * 8;
}

void JBIG2SegmentsTest::BitWriter::append(const QByteArray& bytes)
{
    align();
    m_data.append(bytes);
    m_bitCount += int(bytes.size()) * 8;
}

JBIG2SegmentsTest::HuffmanTable JBIG2SegmentsTest::standardTable(char name)
{
    // The tables B.1 to B.15 of the specification, transcribed from the column
    // "Encoding" of the tables - the prefix, the base value and the range length
    using L = HuffmanLine;

    switch (name)
    {
        case 'A':
            return { { L::Range, 0, 4, "0" }, { L::Range, 16, 8, "10" }, { L::Range, 272, 16, "110" }, { L::Upper, 65808, 32, "111" } };

        case 'B':
            return { { L::Range, 0, 0, "0" }, { L::Range, 1, 0, "10" }, { L::Range, 2, 0, "110" }, { L::Range, 3, 3, "1110" },
                     { L::Range, 11, 6, "11110" }, { L::Upper, 75, 32, "111110" }, { L::OutOfBand, 0, 0, "111111" } };

        case 'C':
            return { { L::Range, -256, 8, "11111110" }, { L::Range, 0, 0, "0" }, { L::Range, 1, 0, "10" }, { L::Range, 2, 0, "110" },
                     { L::Range, 3, 3, "1110" }, { L::Range, 11, 6, "11110" }, { L::Lower, -257, 32, "11111111" },
                     { L::Upper, 75, 32, "1111110" }, { L::OutOfBand, 0, 0, "111110" } };

        case 'D':
            return { { L::Range, 1, 0, "0" }, { L::Range, 2, 0, "10" }, { L::Range, 3, 0, "110" }, { L::Range, 4, 3, "1110" },
                     { L::Range, 12, 6, "11110" }, { L::Upper, 76, 32, "11111" } };

        case 'E':
            return { { L::Range, -255, 8, "1111110" }, { L::Range, 1, 0, "0" }, { L::Range, 2, 0, "10" }, { L::Range, 3, 0, "110" },
                     { L::Range, 4, 3, "1110" }, { L::Range, 12, 6, "11110" }, { L::Lower, -256, 32, "1111111" }, { L::Upper, 76, 32, "111110" } };

        case 'F':
            return { { L::Range, -2048, 10, "11100" }, { L::Range, -1024, 9, "1000" }, { L::Range, -512, 8, "1001" }, { L::Range, -256, 7, "1010" },
                     { L::Range, -128, 6, "11101" }, { L::Range, -64, 5, "11110" }, { L::Range, -32, 5, "1011" }, { L::Range, 0, 7, "00" },
                     { L::Range, 128, 7, "010" }, { L::Range, 256, 8, "011" }, { L::Range, 512, 9, "1100" }, { L::Range, 1024, 10, "1101" },
                     { L::Lower, -2049, 32, "111110" }, { L::Upper, 2048, 32, "111111" } };

        case 'G':
            return { { L::Range, -1024, 9, "1000" }, { L::Range, -512, 8, "000" }, { L::Range, -256, 7, "1001" }, { L::Range, -128, 6, "11010" },
                     { L::Range, -64, 5, "11011" }, { L::Range, -32, 5, "1010" }, { L::Range, 0, 5, "1011" }, { L::Range, 32, 5, "11100" },
                     { L::Range, 64, 6, "11101" }, { L::Range, 128, 7, "1100" }, { L::Range, 256, 8, "001" }, { L::Range, 512, 9, "010" },
                     { L::Range, 1024, 10, "011" }, { L::Lower, -1025, 32, "11110" }, { L::Upper, 2048, 32, "11111" } };

        case 'H':
            return { { L::Range, -15, 3, "11111100" }, { L::Range, -7, 1, "111111100" }, { L::Range, -5, 1, "11111101" }, { L::Range, -3, 0, "111111101" },
                     { L::Range, -2, 0, "1111100" }, { L::Range, -1, 0, "1010" }, { L::Range, 0, 1, "00" }, { L::Range, 2, 0, "11010" },
                     { L::Range, 3, 0, "111010" }, { L::Range, 4, 4, "100" }, { L::Range, 20, 1, "111011" }, { L::Range, 22, 4, "1011" },
                     { L::Range, 38, 5, "1100" }, { L::Range, 70, 6, "11011" }, { L::Range, 134, 7, "11100" }, { L::Range, 262, 7, "111100" },
                     { L::Range, 390, 8, "1111101" }, { L::Range, 646, 10, "111101" }, { L::Lower, -16, 32, "111111110" },
                     { L::Upper, 1670, 32, "111111111" }, { L::OutOfBand, 0, 0, "01" } };

        case 'I':
            return { { L::Range, -31, 4, "11111100" }, { L::Range, -15, 2, "111111100" }, { L::Range, -11, 2, "11111101" }, { L::Range, -7, 1, "111111101" },
                     { L::Range, -5, 1, "1111100" }, { L::Range, -3, 1, "1010" }, { L::Range, -1, 1, "010" }, { L::Range, 1, 1, "011" },
                     { L::Range, 3, 1, "11010" }, { L::Range, 5, 1, "111010" }, { L::Range, 7, 5, "100" }, { L::Range, 39, 2, "111011" },
                     { L::Range, 43, 5, "1011" }, { L::Range, 75, 6, "1100" }, { L::Range, 139, 7, "11011" }, { L::Range, 267, 8, "11100" },
                     { L::Range, 523, 8, "111100" }, { L::Range, 779, 9, "1111101" }, { L::Range, 1291, 11, "111101" }, { L::Lower, -32, 32, "111111110" },
                     { L::Upper, 3339, 32, "111111111" }, { L::OutOfBand, 0, 0, "00" } };

        case 'J':
            return { { L::Range, -21, 4, "1111010" }, { L::Range, -5, 0, "11111100" }, { L::Range, -4, 0, "1111011" }, { L::Range, -3, 0, "11000" },
                     { L::Range, -2, 2, "00" }, { L::Range, 2, 0, "11001" }, { L::Range, 3, 0, "110110" }, { L::Range, 4, 0, "1111100" },
                     { L::Range, 5, 0, "11111101" }, { L::Range, 6, 6, "01" }, { L::Range, 70, 5, "11010" }, { L::Range, 102, 5, "110111" },
                     { L::Range, 134, 6, "111000" }, { L::Range, 198, 7, "111001" }, { L::Range, 326, 8, "111010" }, { L::Range, 582, 9, "111011" },
                     { L::Range, 1094, 10, "111100" }, { L::Range, 2118, 11, "1111101" }, { L::Lower, -22, 32, "11111110" },
                     { L::Upper, 4166, 32, "11111111" }, { L::OutOfBand, 0, 0, "10" } };

        case 'K':
            return { { L::Range, 1, 0, "0" }, { L::Range, 2, 1, "10" }, { L::Range, 4, 0, "1100" }, { L::Range, 5, 1, "1101" },
                     { L::Range, 7, 1, "11100" }, { L::Range, 9, 2, "11101" }, { L::Range, 13, 2, "111100" }, { L::Range, 17, 2, "1111010" },
                     { L::Range, 21, 3, "1111011" }, { L::Range, 29, 4, "1111100" }, { L::Range, 45, 5, "1111101" }, { L::Range, 77, 6, "1111110" },
                     { L::Upper, 141, 32, "1111111" } };

        case 'L':
            return { { L::Range, 1, 0, "0" }, { L::Range, 2, 0, "10" }, { L::Range, 3, 1, "110" }, { L::Range, 5, 0, "11100" },
                     { L::Range, 6, 1, "11101" }, { L::Range, 8, 1, "111100" }, { L::Range, 10, 0, "1111010" }, { L::Range, 11, 1, "1111011" },
                     { L::Range, 13, 2, "1111100" }, { L::Range, 17, 3, "1111101" }, { L::Range, 25, 4, "1111110" }, { L::Range, 41, 5, "11111110" },
                     { L::Upper, 73, 32, "11111111" } };

        case 'M':
            return { { L::Range, 1, 0, "0" }, { L::Range, 2, 0, "100" }, { L::Range, 3, 0, "1100" }, { L::Range, 4, 0, "11100" },
                     { L::Range, 5, 1, "1101" }, { L::Range, 7, 3, "101" }, { L::Range, 15, 1, "111010" }, { L::Range, 17, 2, "111011" },
                     { L::Range, 21, 3, "111100" }, { L::Range, 29, 4, "111101" }, { L::Range, 45, 5, "111110" }, { L::Range, 77, 6, "1111110" },
                     { L::Upper, 141, 32, "1111111" } };

        case 'N':
            return { { L::Range, -2, 0, "100" }, { L::Range, -1, 0, "101" }, { L::Range, 0, 0, "0" }, { L::Range, 1, 0, "110" }, { L::Range, 2, 0, "111" } };

        case 'O':
            return { { L::Range, -24, 4, "1111100" }, { L::Range, -8, 2, "111100" }, { L::Range, -4, 1, "11100" }, { L::Range, -2, 0, "1100" },
                     { L::Range, -1, 0, "100" }, { L::Range, 0, 0, "0" }, { L::Range, 1, 0, "101" }, { L::Range, 2, 0, "1101" },
                     { L::Range, 3, 1, "11101" }, { L::Range, 5, 2, "111101" }, { L::Range, 9, 4, "1111101" }, { L::Lower, -25, 32, "1111110" },
                     { L::Upper, 25, 32, "1111111" } };

        default:
            break;
    }

    throw std::logic_error("Unknown standard table");
}

void JBIG2SegmentsTest::writeHuffman(BitWriter& writer, const HuffmanTable& table, std::optional<int32_t> value)
{
    for (const HuffmanLine& line : table)
    {
        switch (line.kind)
        {
            case HuffmanLine::Range:
                if (value && *value >= line.low && int64_t(*value) < int64_t(line.low) + (int64_t(1) << line.rangeBits))
                {
                    writer.writeCode(line.prefix);
                    writer.writeBits(uint32_t(int64_t(*value) - int64_t(line.low)), line.rangeBits);
                    return;
                }
                break;

            case HuffmanLine::Lower:
                if (value && *value <= line.low)
                {
                    writer.writeCode(line.prefix);
                    writer.writeBits(uint32_t(int64_t(line.low) - int64_t(*value)), 32);
                    return;
                }
                break;

            case HuffmanLine::Upper:
                if (value && *value >= line.low)
                {
                    writer.writeCode(line.prefix);
                    writer.writeBits(uint32_t(int64_t(*value) - int64_t(line.low)), 32);
                    return;
                }
                break;

            case HuffmanLine::OutOfBand:
                if (!value)
                {
                    writer.writeCode(line.prefix);
                    return;
                }
                break;
        }
    }

    throw std::logic_error("Value can not be coded by the huffman table");
}

std::vector<QByteArray> JBIG2SegmentsTest::assignPrefixCodes(const std::vector<int>& lengths)
{
    // B.3 Assigning the prefix codes
    int maxLength = 0;
    for (const int length : lengths)
    {
        maxLength = qMax(maxLength, length);
    }

    std::vector<int> lengthCounts(size_t(maxLength) + 1, 0);
    for (const int length : lengths)
    {
        if (length > 0)
        {
            ++lengthCounts[size_t(length)];
        }
    }

    std::vector<QByteArray> codes(lengths.size());
    uint32_t firstCode = 0;
    lengthCounts[0] = 0;

    for (int currentLength = 1; currentLength <= maxLength; ++currentLength)
    {
        firstCode = (firstCode + uint32_t(lengthCounts[size_t(currentLength) - 1])) << 1;
        uint32_t currentCode = firstCode;

        for (size_t i = 0; i < lengths.size(); ++i)
        {
            if (lengths[i] == currentLength)
            {
                QByteArray code;
                for (int bit = currentLength - 1; bit >= 0; --bit)
                {
                    code.append(((currentCode >> bit) & 1) ? '1' : '0');
                }
                codes[i] = code;
                ++currentCode;
            }
        }
    }

    return codes;
}

int32_t JBIG2SegmentsTest::CustomTable::high() const
{
    int64_t value = HTLOW;
    for (const auto& line : lines)
    {
        value += int64_t(1) << line.second;
    }
    return int32_t(value);
}

QByteArray JBIG2SegmentsTest::CustomTable::segmentData() const
{
    // B.2 Code table structure
    QByteArray data;
    data.append(char((HTOOB ? 0x01 : 0x00) | ((HTPS - 1) << 1) | ((HTRS - 1) << 4)));
    appendUInt32(data, uint32_t(HTLOW));
    appendUInt32(data, uint32_t(high()));

    BitWriter writer;
    for (const auto& line : lines)
    {
        writer.writeBits(uint32_t(line.first), HTPS);
        writer.writeBits(uint32_t(line.second), HTRS);
    }
    writer.writeBits(uint32_t(lowerPrefixLength), HTPS);
    writer.writeBits(uint32_t(upperPrefixLength), HTPS);
    if (HTOOB)
    {
        writer.writeBits(uint32_t(oobPrefixLength), HTPS);
    }

    data.append(writer.data());
    return data;
}

JBIG2SegmentsTest::HuffmanTable JBIG2SegmentsTest::CustomTable::table() const
{
    HuffmanTable table;
    std::vector<int> lengths;

    int64_t low = HTLOW;
    for (const auto& line : lines)
    {
        table.push_back({ HuffmanLine::Range, int32_t(low), line.second, QByteArray() });
        lengths.push_back(line.first);
        low += int64_t(1) << line.second;
    }

    table.push_back({ HuffmanLine::Lower, int32_t(int64_t(HTLOW) - 1), 32, QByteArray() });
    lengths.push_back(lowerPrefixLength);
    table.push_back({ HuffmanLine::Upper, high(), 32, QByteArray() });
    lengths.push_back(upperPrefixLength);

    if (HTOOB)
    {
        table.push_back({ HuffmanLine::OutOfBand, 0, 0, QByteArray() });
        lengths.push_back(oobPrefixLength);
    }

    const std::vector<QByteArray> codes = assignPrefixCodes(lengths);
    HuffmanTable result;
    for (size_t i = 0; i < table.size(); ++i)
    {
        if (lengths[i] > 0)
        {
            table[i].prefix = codes[i];
            result.push_back(table[i]);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------------------
// Arithmetic coding

void JBIG2SegmentsTest::ArithmeticContexts::reset(uint32_t symbolCodeLength, uint8_t GBTEMPLATE, uint8_t GRTEMPLATE)
{
    for (pdf::PDFJBIG2ArithmeticDecoderState* state : { &IADH, &IADW, &IAEX, &IAAI, &IADT, &IAFS, &IADS, &IAIT, &IARI, &IARDW, &IARDH, &IARDX, &IARDY })
    {
        state->reset(9);
    }

    IAID.reset(uint8_t(symbolCodeLength));
    generic.reset(pdf::PDFJBIG2EncoderParameters::getContextBitCount(GBTEMPLATE));
    refinement.reset((GRTEMPLATE == 0) ? 13 : 10);
}

void JBIG2SegmentsTest::encodeInteger(pdf::PDFJBIG2ArithmeticEncoder& encoder, pdf::PDFJBIG2ArithmeticDecoderState& state, std::optional<int32_t> value)
{
    // The inverse of the procedure A.2 - the context PREV is formed from the coded bits
    // in the same way as the decoder forms it
    uint32_t PREV = 1;

    auto writeBit = [&](uint32_t bit)
    {
        encoder.encodeBit(PREV, &state, bit);
        PREV = (PREV < 256) ? ((PREV << 1) | bit) : (((((PREV << 1) | bit)) & 0x01FF) | 0x0100);
    };

    auto writeBits = [&](uint32_t bits, int count)
    {
        for (int i = count - 1; i >= 0; --i)
        {
            writeBit((bits >> i) & 1);
        }
    };

    if (!value)
    {
        // Out-of-band value - the sign 1 and the value 0
        writeBit(1);
        writeBit(0);
        writeBits(0, 2);
        return;
    }

    const int64_t v = *value;
    const uint32_t S = (v < 0) ? 1 : 0;
    const uint32_t V = uint32_t(qAbs(v));

    writeBit(S);

    if (V <= 3)
    {
        writeBit(0);
        writeBits(V, 2);
    }
    else if (V <= 19)
    {
        writeBit(1);
        writeBit(0);
        writeBits(V - 4, 4);
    }
    else if (V <= 83)
    {
        writeBits(0b110, 3);
        writeBits(V - 20, 6);
    }
    else if (V <= 339)
    {
        writeBits(0b1110, 4);
        writeBits(V - 84, 8);
    }
    else if (V <= 4435)
    {
        writeBits(0b11110, 5);
        writeBits(V - 340, 12);
    }
    else
    {
        writeBits(0b11111, 5);
        writeBits(V - 4436, 32);
    }
}

void JBIG2SegmentsTest::encodeIAID(pdf::PDFJBIG2ArithmeticEncoder& encoder, pdf::PDFJBIG2ArithmeticDecoderState& state, uint32_t codeLength, uint32_t value)
{
    // The inverse of the procedure A.3
    uint32_t PREV = 1;
    for (int i = int(codeLength) - 1; i >= 0; --i)
    {
        const uint32_t bit = (value >> i) & 1;
        encoder.encodeBit(PREV, &state, bit);
        PREV = (PREV << 1) | bit;
    }
}

void JBIG2SegmentsTest::encodeRefinement(pdf::PDFJBIG2ArithmeticEncoder& encoder,
                                         pdf::PDFJBIG2ArithmeticDecoderState& state,
                                         const Image& target,
                                         const Image& reference,
                                         int referenceX,
                                         int referenceY,
                                         uint8_t GRTEMPLATE,
                                         const pdf::PDFJBIG2ATPositions& at,
                                         bool TPGRON)
{
    auto t = [&target](int x, int y) -> uint32_t { return isBlack(target, x, y) ? 1 : 0; };
    auto r = [&reference](int x, int y) -> uint32_t { return isBlack(reference, x, y) ? 1 : 0; };

    // A pixel, whose neighbourhood of 3 x 3 pixels in the reference has a single value
    auto isUniform = [&](int x, int y)
    {
        const int rx = x - referenceX;
        const int ry = y - referenceY;
        const uint32_t value = r(rx, ry);

        for (int dy = -1; dy <= 1; ++dy)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (r(rx + dx, ry + dy) != value)
                {
                    return false;
                }
            }
        }

        return true;
    };

    const uint32_t LTPContext = (GRTEMPLATE == 0) ? 0x0100 : 0x0080;
    uint32_t LTP = 0;

    for (int y = 0; y < imageHeight(target); ++y)
    {
        if (TPGRON)
        {
            bool isTypical = true;
            for (int x = 0; x < imageWidth(target); ++x)
            {
                if (isUniform(x, y) && t(x, y) != r(x - referenceX, y - referenceY))
                {
                    isTypical = false;
                }
            }

            const uint32_t SLTP = isTypical ? 1 : 0;
            encoder.encodeBit(LTPContext, &state, SLTP ^ LTP);
            LTP = SLTP;
        }

        for (int x = 0; x < imageWidth(target); ++x)
        {
            if (LTP && isUniform(x, y))
            {
                continue;
            }

            uint32_t context = 0;
            int shift = 0;
            auto add = [&](uint32_t bit)
            {
                context |= bit << shift;
                ++shift;
            };

            const int rx = x - referenceX;
            const int ry = y - referenceY;

            if (GRTEMPLATE == 0)
            {
                add(t(x - 1, y));
                add(t(x + 1, y - 1));
                add(t(x, y - 1));
                add(t(x + at[0].x, y + at[0].y));
                add(r(rx + 1, ry + 1));
                add(r(rx, ry + 1));
                add(r(rx - 1, ry + 1));
                add(r(rx + 1, ry));
                add(r(rx, ry));
                add(r(rx - 1, ry));
                add(r(rx + 1, ry - 1));
                add(r(rx, ry - 1));
                add(r(rx + at[1].x, ry + at[1].y));
            }
            else
            {
                add(t(x - 1, y));
                add(t(x + 1, y - 1));
                add(t(x, y - 1));
                add(t(x - 1, y - 1));
                add(r(rx + 1, ry + 1));
                add(r(rx, ry + 1));
                add(r(rx + 1, ry));
                add(r(rx, ry));
                add(r(rx - 1, ry));
                add(r(rx, ry - 1));
            }

            encoder.encodeBit(context, &state, t(x, y));
        }
    }
}

void JBIG2SegmentsTest::encodeGeneric(pdf::PDFJBIG2ArithmeticEncoder& encoder, pdf::PDFJBIG2ArithmeticDecoderState& state, const Image& image, uint8_t GBTEMPLATE)
{
    const Bitmap bitmap = createBitmap(image);

    pdf::PDFJBIG2EncoderParameters parameters;
    parameters.MMR = false;
    parameters.TPGDON = false;
    parameters.GBTEMPLATE = GBTEMPLATE;
    parameters.GBAT = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(GBTEMPLATE);

    pdf::PDFJBIG2Encoder::encodeGenericBitmap(bitmap.view(), parameters, encoder, state);
}

QByteArray JBIG2SegmentsTest::encodeMMR(const Image& image, bool endOfBlock)
{
    const Bitmap bitmap = createBitmap(image);

    pdf::PDFCCITTFaxEncoderParameters parameters;
    parameters.K = -1;
    parameters.hasEndOfLine = false;
    parameters.hasEncodedByteAlign = false;
    parameters.hasEndOfBlock = endOfBlock;

    pdf::PDFCCITTFaxEncoder encoder(bitmap.view(), parameters);
    return encoder.encode();
}

// ---------------------------------------------------------------------------------------
// Symbol dictionaries

std::vector<std::vector<size_t>> JBIG2SegmentsTest::getHeightClasses(const std::vector<Image>& symbols)
{
    std::vector<std::vector<size_t>> classes;

    for (size_t i = 0; i < symbols.size(); ++i)
    {
        if (i > 0 && imageHeight(symbols[i]) < imageHeight(symbols[i - 1]))
        {
            throw std::logic_error("Symbols must be sorted by their height");
        }

        if (i == 0 || imageHeight(symbols[i]) != imageHeight(symbols[i - 1]))
        {
            classes.emplace_back();
        }

        classes.back().push_back(i);
    }

    return classes;
}

QByteArray JBIG2SegmentsTest::encodeSymbolDictionary(const std::vector<Image>& symbols, const SymbolDictionaryOptions& options, ArithmeticContexts& contexts)
{
    Q_ASSERT(!options.refinement);

    const uint32_t symbolCount = options.inputSymbolCount + uint32_t(symbols.size());
    std::vector<bool> exportFlags = options.exportFlags;
    if (exportFlags.empty())
    {
        exportFlags.assign(symbolCount, true);
    }
    const uint32_t exportedCount = options.exportedCountOverride.value_or(uint32_t(std::count(exportFlags.cbegin(), exportFlags.cend(), true)));

    // 7.4.2.1.1 Symbol dictionary flags
    uint16_t flags = 0;
    flags |= options.huffman ? 0x0001 : 0x0000;
    flags |= uint16_t(options.SDHUFFDH) << 2;
    flags |= uint16_t(options.SDHUFFDW) << 4;
    flags |= uint16_t(options.SDHUFFBMSIZE) << 6;
    flags |= uint16_t(options.SDHUFFAGGINST) << 7;
    flags |= options.contextUsed ? 0x0100 : 0x0000;
    flags |= options.contextRetained ? 0x0200 : 0x0000;
    flags |= uint16_t(options.SDTEMPLATE) << 10;

    QByteArray data;
    appendUInt16(data, flags);
    if (!options.huffman)
    {
        appendATPositions(data, pdf::PDFJBIG2EncoderParameters::getNominalATPositions(options.SDTEMPLATE), pdf::PDFJBIG2EncoderParameters::getATPositionCount(options.SDTEMPLATE));
    }
    appendUInt32(data, exportedCount);
    appendUInt32(data, uint32_t(symbols.size()));

    const std::vector<std::vector<size_t>> heightClasses = getHeightClasses(symbols);

    // Export flags, see 6.5.10 - the runs of the symbols with the same flag
    std::vector<int32_t> exportRunLengths;
    bool currentFlag = false;
    for (size_t i = 0; i < exportFlags.size();)
    {
        size_t run = 0;
        while (i + run < exportFlags.size() && exportFlags[i + run] == currentFlag)
        {
            ++run;
        }
        exportRunLengths.push_back(int32_t(run));
        i += run;
        currentFlag = !currentFlag;
    }
    if (options.exportRunLengthsOverride)
    {
        exportRunLengths = *options.exportRunLengthsOverride;
    }

    if (!options.huffman)
    {
        // 6.5.5 with the arithmetic coding
        pdf::PDFJBIG2ArithmeticEncoder encoder;
        int32_t HCHEIGHT = 0;

        for (const std::vector<size_t>& heightClass : heightClasses)
        {
            const int32_t height = imageHeight(symbols[heightClass.front()]);
            encodeInteger(encoder, contexts.IADH, height - HCHEIGHT);
            HCHEIGHT = height;

            int32_t SYMWIDTH = 0;
            for (const size_t index : heightClass)
            {
                const int32_t width = imageWidth(symbols[index]);
                encodeInteger(encoder, contexts.IADW, width - SYMWIDTH);
                SYMWIDTH = width;
                encodeGeneric(encoder, contexts.generic, symbols[index], options.SDTEMPLATE);
            }

            encodeInteger(encoder, contexts.IADW, std::nullopt);
        }

        for (const int32_t runLength : exportRunLengths)
        {
            encodeInteger(encoder, contexts.IAEX, runLength);
        }

        data.append(encoder.finish());
        return data;
    }

    // 6.5.5 with the huffman coding, tables of 7.4.2.1.6
    size_t userTableIndex = 0;
    auto userTable = [&]()
    {
        if (userTableIndex >= options.userTables.size())
        {
            throw std::logic_error("Missing user table");
        }
        return options.userTables[userTableIndex++];
    };

    const HuffmanTable tableDH = (options.SDHUFFDH == 0) ? standardTable('D') : ((options.SDHUFFDH == 1) ? standardTable('E') : userTable());
    const HuffmanTable tableDW = (options.SDHUFFDW == 0) ? standardTable('B') : ((options.SDHUFFDW == 1) ? standardTable('C') : userTable());
    const HuffmanTable tableBMSIZE = (options.SDHUFFBMSIZE == 0) ? standardTable('A') : userTable();
    const HuffmanTable tableEX = standardTable('A');

    BitWriter writer;
    int32_t HCHEIGHT = 0;

    for (const std::vector<size_t>& heightClass : heightClasses)
    {
        const int32_t height = imageHeight(symbols[heightClass.front()]);
        writeHuffman(writer, tableDH, height - HCHEIGHT);
        HCHEIGHT = height;

        int32_t SYMWIDTH = 0;
        Image collectiveBitmap = createImage(0, height);
        for (const size_t index : heightClass)
        {
            const int32_t width = imageWidth(symbols[index]);
            writeHuffman(writer, tableDW, width - SYMWIDTH);
            SYMWIDTH = width;

            for (int y = 0; y < height; ++y)
            {
                collectiveBitmap[y] += symbols[index][y];
            }
        }
        writeHuffman(writer, tableDW, std::nullopt);

        // 6.5.9 Height class collective bitmap
        if (options.uncompressedCollectiveBitmap)
        {
            writeHuffman(writer, tableBMSIZE, 0);
            writer.align();
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < imageWidth(collectiveBitmap); ++x)
                {
                    writer.writeBit(isBlack(collectiveBitmap, x, y) ? 1 : 0);
                }
                writer.align();
            }
        }
        else
        {
            const QByteArray mmr = encodeMMR(collectiveBitmap, false);
            writeHuffman(writer, tableBMSIZE, int32_t(mmr.size()));
            writer.append(mmr);
        }
    }

    for (const int32_t runLength : exportRunLengths)
    {
        writeHuffman(writer, tableEX, runLength);
    }
    writer.align();

    data.append(writer.data());
    return data;
}

QByteArray JBIG2SegmentsTest::encodeRefinementSymbolDictionary(const std::vector<Image>& inputSymbols, const std::vector<RefinedSymbol>& symbols, const SymbolDictionaryOptions& options, ArithmeticContexts& contexts)
{
    const uint32_t symbolCount = uint32_t(inputSymbols.size()) + uint32_t(symbols.size());
    std::vector<bool> exportFlags = options.exportFlags;
    if (exportFlags.empty())
    {
        exportFlags.assign(symbolCount, true);
    }
    const uint32_t exportedCount = uint32_t(std::count(exportFlags.cbegin(), exportFlags.cend(), true));

    std::vector<Image> images;
    for (const RefinedSymbol& symbol : symbols)
    {
        images.push_back(symbol.image);
    }
    const std::vector<std::vector<size_t>> heightClasses = getHeightClasses(images);

    // All symbols - the input ones and the new ones - can be referred to
    std::vector<Image> allSymbols = inputSymbols;
    allSymbols.insert(allSymbols.end(), images.cbegin(), images.cend());

    // 7.4.2.1.1 Symbol dictionary flags
    uint16_t flags = 0x0002;
    flags |= options.huffman ? 0x0001 : 0x0000;
    flags |= uint16_t(options.SDHUFFDH) << 2;
    flags |= uint16_t(options.SDHUFFDW) << 4;
    flags |= uint16_t(options.SDHUFFBMSIZE) << 6;
    flags |= uint16_t(options.SDHUFFAGGINST) << 7;
    flags |= options.contextUsed ? 0x0100 : 0x0000;
    flags |= options.contextRetained ? 0x0200 : 0x0000;
    flags |= uint16_t(options.SDTEMPLATE) << 10;
    flags |= uint16_t(options.SDRTEMPLATE) << 12;

    QByteArray data;
    appendUInt16(data, flags);
    if (!options.huffman)
    {
        appendATPositions(data, pdf::PDFJBIG2EncoderParameters::getNominalATPositions(options.SDTEMPLATE), pdf::PDFJBIG2EncoderParameters::getATPositionCount(options.SDTEMPLATE));
    }
    if (options.SDRTEMPLATE == 0)
    {
        appendATPositions(data, options.SDRAT, 2);
    }
    appendUInt32(data, exportedCount);
    appendUInt32(data, uint32_t(symbols.size()));

    // 6.5.8.2.3 Setting SBSYMCODELEN
    uint32_t symbolCodeLength = 0;
    while ((1u << symbolCodeLength) < symbolCount)
    {
        ++symbolCodeLength;
    }
    if (options.huffman)
    {
        symbolCodeLength = qMax(symbolCodeLength, 1u);
    }

    std::vector<int32_t> exportRunLengths;
    bool currentFlag = false;
    for (size_t i = 0; i < exportFlags.size();)
    {
        size_t run = 0;
        while (i + run < exportFlags.size() && exportFlags[i + run] == currentFlag)
        {
            ++run;
        }
        exportRunLengths.push_back(int32_t(run));
        i += run;
        currentFlag = !currentFlag;
    }

    if (!options.huffman)
    {
        pdf::PDFJBIG2ArithmeticEncoder encoder;
        int32_t HCHEIGHT = 0;

        for (const std::vector<size_t>& heightClass : heightClasses)
        {
            const int32_t height = imageHeight(images[heightClass.front()]);
            encodeInteger(encoder, contexts.IADH, height - HCHEIGHT);
            HCHEIGHT = height;

            int32_t SYMWIDTH = 0;
            for (const size_t index : heightClass)
            {
                const RefinedSymbol& symbol = symbols[index];
                const int32_t width = imageWidth(symbol.image);
                encodeInteger(encoder, contexts.IADW, width - SYMWIDTH);
                SYMWIDTH = width;

                // 6.5.8.2 Refinement/aggregate-coded symbol bitmap with a single instance
                encodeInteger(encoder, contexts.IAAI, options.refinementInstanceCount);
                const uint32_t id = options.refinementIdOverride.value_or(symbol.referenceId);

                if (!options.textRegionForm)
                {
                    encodeIAID(encoder, contexts.IAID, symbolCodeLength, id);
                    encodeInteger(encoder, contexts.IARDX, symbol.RDX);
                    encodeInteger(encoder, contexts.IARDY, symbol.RDY);
                    encodeRefinement(encoder, contexts.refinement, symbol.image, allSymbols[symbol.referenceId], symbol.RDX, symbol.RDY, options.SDRTEMPLATE, options.SDRAT);
                }
                else
                {
                    // The text region decoding procedure with the parameters of the table 17 -
                    // a single strip with a single refined instance at (0, 0). The reference
                    // offset of the text region is FLOOR(RDW / 2) + RDX, so the deltas are
                    // adjusted to refine at the offset of the symbol.
                    const Image& reference = allSymbols[symbol.referenceId];
                    const int32_t RDW = width - imageWidth(reference);
                    const int32_t RDH = height - imageHeight(reference);

                    encodeInteger(encoder, contexts.IADT, 0);
                    encodeInteger(encoder, contexts.IADT, 0);
                    encodeInteger(encoder, contexts.IAFS, 0);
                    encodeIAID(encoder, contexts.IAID, symbolCodeLength, id);
                    encodeInteger(encoder, contexts.IARI, 1);
                    encodeInteger(encoder, contexts.IARDW, RDW);
                    encodeInteger(encoder, contexts.IARDH, RDH);
                    encodeInteger(encoder, contexts.IARDX, symbol.RDX - (RDW >> 1));
                    encodeInteger(encoder, contexts.IARDY, symbol.RDY - (RDH >> 1));
                    encodeRefinement(encoder, contexts.refinement, symbol.image, reference, symbol.RDX, symbol.RDY, options.SDRTEMPLATE, options.SDRAT);
                    encodeInteger(encoder, contexts.IADS, std::nullopt);
                }
            }

            encodeInteger(encoder, contexts.IADW, std::nullopt);
        }

        for (const int32_t runLength : exportRunLengths)
        {
            encodeInteger(encoder, contexts.IAEX, runLength);
        }

        data.append(encoder.finish());
        return data;
    }

    size_t userTableIndex = 0;
    auto userTable = [&]()
    {
        if (userTableIndex >= options.userTables.size())
        {
            throw std::logic_error("Missing user table");
        }
        return options.userTables[userTableIndex++];
    };

    const HuffmanTable tableDH = (options.SDHUFFDH == 0) ? standardTable('D') : ((options.SDHUFFDH == 1) ? standardTable('E') : userTable());
    const HuffmanTable tableDW = (options.SDHUFFDW == 0) ? standardTable('B') : ((options.SDHUFFDW == 1) ? standardTable('C') : userTable());
    if (options.SDHUFFBMSIZE != 0)
    {
        // The table is selected, but a refinement dictionary has no collective bitmap
        userTable();
    }
    const HuffmanTable tableAGGINST = (options.SDHUFFAGGINST == 0) ? standardTable('A') : userTable();
    const HuffmanTable tableO = standardTable('O');
    const HuffmanTable tableA = standardTable('A');

    BitWriter writer;
    int32_t HCHEIGHT = 0;

    for (const std::vector<size_t>& heightClass : heightClasses)
    {
        const int32_t height = imageHeight(images[heightClass.front()]);
        writeHuffman(writer, tableDH, height - HCHEIGHT);
        HCHEIGHT = height;

        int32_t SYMWIDTH = 0;
        for (const size_t index : heightClass)
        {
            const RefinedSymbol& symbol = symbols[index];
            const int32_t width = imageWidth(symbol.image);
            writeHuffman(writer, tableDW, width - SYMWIDTH);
            SYMWIDTH = width;

            // 6.5.8.2.2 with SDHUFF equal to 1 - the ID, the offsets and the size of the
            // arithmetically coded refinement data, which follow at a byte boundary
            writeHuffman(writer, tableAGGINST, options.refinementInstanceCount);
            writer.writeBits(symbol.referenceId, int(symbolCodeLength));
            writeHuffman(writer, tableO, symbol.RDX);
            writeHuffman(writer, tableO, symbol.RDY);

            pdf::PDFJBIG2ArithmeticEncoder encoder;
            encodeRefinement(encoder, contexts.refinement, symbol.image, allSymbols[symbol.referenceId], symbol.RDX, symbol.RDY, options.SDRTEMPLATE, options.SDRAT);
            const QByteArray refinementData = options.refinementDataOverride.value_or(encoder.finish());

            writeHuffman(writer, tableA, int32_t(refinementData.size()));
            writer.append(refinementData);
        }

        writeHuffman(writer, tableDW, std::nullopt);
    }

    for (const int32_t runLength : exportRunLengths)
    {
        writeHuffman(writer, tableA, runLength);
    }
    writer.align();

    data.append(writer.data());
    return data;
}

// ---------------------------------------------------------------------------------------
// Text regions

QByteArray JBIG2SegmentsTest::encodeTextRegion(const std::vector<TextInstance>& instances, const std::vector<Image>& symbols, const TextRegionOptions& options)
{
    const uint32_t symbolCount = uint32_t(symbols.size());
    const int strips = 1 << options.logStrips;

    // 7.4.3.1.1 Text region segment flags - the reference corner is TOPLEFT and the
    // region is not transposed
    uint16_t flags = 0;
    flags |= options.huffman ? 0x0001 : 0x0000;
    flags |= options.refine ? 0x0002 : 0x0000;
    flags |= uint16_t(options.logStrips) << 2;
    flags |= uint16_t(1) << 4;
    flags |= uint16_t(options.combinationOperator) << 7;
    flags |= options.defaultPixel ? 0x0200 : 0x0000;
    flags |= uint16_t(uint8_t(options.dsOffset) & 0x1F) << 10;
    flags |= uint16_t(options.SBRTEMPLATE) << 15;

    QByteArray data;
    appendRegionInformation(data, options.width, options.height, options.x, options.y, options.externalOperator);
    appendUInt16(data, flags);

    if (options.huffman)
    {
        // 7.4.3.1.2 Text region segment huffman flags
        uint16_t huffmanFlags = options.reservedHuffmanFlags;
        huffmanFlags |= uint16_t(options.SBHUFFFS);
        huffmanFlags |= uint16_t(options.SBHUFFDS) << 2;
        huffmanFlags |= uint16_t(options.SBHUFFDT) << 4;
        huffmanFlags |= uint16_t(options.SBHUFFRDW) << 6;
        huffmanFlags |= uint16_t(options.SBHUFFRDH) << 8;
        huffmanFlags |= uint16_t(options.SBHUFFRDX) << 10;
        huffmanFlags |= uint16_t(options.SBHUFFRDY) << 12;
        huffmanFlags |= uint16_t(options.SBHUFFRSIZE) << 14;
        appendUInt16(data, huffmanFlags);
    }

    if (options.refine && options.SBRTEMPLATE == 0)
    {
        appendATPositions(data, options.SBRAT, 2);
    }

    appendUInt32(data, options.instanceCountOverride.value_or(uint32_t(instances.size())));

    // Strips of the instances, see 6.4.5 - the instances are sorted by the strip and
    // by the column, so consecutive instances of the same strip form a strip
    std::vector<std::vector<size_t>> stripInstances;
    for (size_t i = 0; i < instances.size(); ++i)
    {
        if (i == 0 || instances[i].y / strips != instances[i - 1].y / strips)
        {
            stripInstances.emplace_back();
        }
        stripInstances.back().push_back(i);
    }

    // The width of an instance is the width of the refined bitmap, when it is refined
    auto instanceWidth = [&](const TextInstance& instance)
    {
        return instance.refine ? imageWidth(instance.refined) : imageWidth(symbols[instance.id]);
    };

    if (!options.huffman)
    {
        uint32_t symbolCodeLength = 0;
        while ((1u << symbolCodeLength) < symbolCount)
        {
            ++symbolCodeLength;
        }

        ArithmeticContexts contexts;
        contexts.reset(symbolCodeLength, 0, options.SBRTEMPLATE);
        pdf::PDFJBIG2ArithmeticEncoder encoder;

        // 6.4.5 step 2) - STRIPT
        encodeInteger(encoder, contexts.IADT, 0);
        int32_t STRIPT = 0;
        int32_t FIRSTS = 0;

        for (const std::vector<size_t>& strip : stripInstances)
        {
            const int32_t stripT = instances[strip.front()].y / strips * strips;
            encodeInteger(encoder, contexts.IADT, (stripT - STRIPT) / strips);
            STRIPT = stripT;

            int32_t CURS = 0;
            for (size_t i = 0; i < strip.size(); ++i)
            {
                const TextInstance& instance = instances[strip[i]];
                if (i == 0)
                {
                    encodeInteger(encoder, contexts.IAFS, instance.x - FIRSTS);
                    FIRSTS = instance.x;
                }
                else
                {
                    encodeInteger(encoder, contexts.IADS, instance.x - CURS - options.dsOffset);
                }
                CURS = instance.x;

                if (strips > 1)
                {
                    encodeInteger(encoder, contexts.IAIT, instance.y - stripT);
                }

                encodeIAID(encoder, contexts.IAID, symbolCodeLength, instance.id);

                if (options.refine)
                {
                    encodeInteger(encoder, contexts.IARI, instance.refine ? 1 : 0);
                    if (instance.refine)
                    {
                        encodeInteger(encoder, contexts.IARDW, instance.RDW);
                        encodeInteger(encoder, contexts.IARDH, instance.RDH);
                        encodeInteger(encoder, contexts.IARDX, instance.RDX);
                        encodeInteger(encoder, contexts.IARDY, instance.RDY);
                        encodeRefinement(encoder, contexts.refinement, instance.refined, symbols[instance.id], (instance.RDW >> 1) + instance.RDX, (instance.RDH >> 1) + instance.RDY, options.SBRTEMPLATE, options.SBRAT);
                    }
                }

                CURS += instanceWidth(instance) - 1;
            }

            encodeInteger(encoder, contexts.IADS, std::nullopt);
        }

        data.append(encoder.finish());
        return data;
    }

    size_t userTableIndex = 0;
    auto userTable = [&]()
    {
        if (userTableIndex >= options.userTables.size())
        {
            throw std::logic_error("Missing user table");
        }
        return options.userTables[userTableIndex++];
    };

    auto selectTable = [&](uint8_t selection, char table0, char table1, char table2)
    {
        switch (selection)
        {
            case 0:
                return standardTable(table0);
            case 1:
                return standardTable(table1);
            case 2:
                if (table2 != ' ')
                {
                    return standardTable(table2);
                }
                break;
            default:
                break;
        }
        return userTable();
    };

    // 7.4.3.1.2 - the tables in the order of the huffman flags
    const HuffmanTable tableFS = selectTable(options.SBHUFFFS, 'F', 'G', ' ');
    const HuffmanTable tableDS = selectTable(options.SBHUFFDS, 'H', 'I', 'J');
    const HuffmanTable tableDT = selectTable(options.SBHUFFDT, 'K', 'L', 'M');
    const HuffmanTable tableRDW = selectTable(options.SBHUFFRDW, 'N', 'O', ' ');
    const HuffmanTable tableRDH = selectTable(options.SBHUFFRDH, 'N', 'O', ' ');
    const HuffmanTable tableRDX = selectTable(options.SBHUFFRDX, 'N', 'O', ' ');
    const HuffmanTable tableRDY = selectTable(options.SBHUFFRDY, 'N', 'O', ' ');
    const HuffmanTable tableRSIZE = (options.SBHUFFRSIZE == 0) ? standardTable('A') : userTable();

    // 7.4.3.1.7 Symbol ID huffman decoding - the code lengths of the symbols are
    // coded by the run codes of the table 29
    std::vector<int> symbolCodeLengths = options.symbolCodeLengths;
    if (symbolCodeLengths.empty())
    {
        int length = 1;
        while ((1u << length) < symbolCount)
        {
            ++length;
        }
        symbolCodeLengths.assign(symbolCount, length);
    }

    std::vector<std::pair<int, int>> runCodes;
    for (size_t i = 0; i < symbolCodeLengths.size();)
    {
        const int length = symbolCodeLengths[i];
        size_t run = 1;
        while (i + run < symbolCodeLengths.size() && symbolCodeLengths[i + run] == length)
        {
            ++run;
        }

        if (length == 0 && run >= 11)
        {
            run = qMin<size_t>(run, 74);
            runCodes.emplace_back(34, int(run) - 11);
        }
        else if (length == 0 && run >= 3)
        {
            run = qMin<size_t>(run, 10);
            runCodes.emplace_back(33, int(run) - 3);
        }
        else if (i > 0 && symbolCodeLengths[i - 1] == length && run >= 3)
        {
            run = qMin<size_t>(run, 6);
            runCodes.emplace_back(32, int(run) - 3);
        }
        else
        {
            run = 1;
            runCodes.emplace_back(length, 0);
        }

        i += run;
    }
    if (options.runCodesOverride)
    {
        runCodes = *options.runCodesOverride;
    }

    std::vector<int> runCodeLengths(35, 0);
    int usedRunCodes = 0;
    for (const auto& runCode : runCodes)
    {
        if (runCodeLengths[size_t(runCode.first)] == 0)
        {
            ++usedRunCodes;
            runCodeLengths[size_t(runCode.first)] = 1;
        }
    }
    int runCodeLength = 1;
    while ((1 << runCodeLength) < usedRunCodes)
    {
        ++runCodeLength;
    }
    for (int& length : runCodeLengths)
    {
        length = length ? runCodeLength : 0;
    }
    const std::vector<QByteArray> runCodePrefixes = assignPrefixCodes(runCodeLengths);
    const std::vector<QByteArray> symbolCodes = assignPrefixCodes(symbolCodeLengths);

    BitWriter writer;
    for (const int length : runCodeLengths)
    {
        writer.writeBits(uint32_t(length), 4);
    }
    for (const auto& runCode : runCodes)
    {
        writer.writeCode(runCodePrefixes[size_t(runCode.first)]);
        switch (runCode.first)
        {
            case 32:
                writer.writeBits(uint32_t(runCode.second), 2);
                break;
            case 33:
                writer.writeBits(uint32_t(runCode.second), 3);
                break;
            case 34:
                writer.writeBits(uint32_t(runCode.second), 7);
                break;
            default:
                break;
        }
    }
    writer.align();

    // The tables of the strip deltas code only positive values, so the initial STRIPT
    // is one strip above the region
    writeHuffman(writer, tableDT, 1);
    int32_t STRIPT = -strips;
    int32_t FIRSTS = 0;

    pdf::PDFJBIG2ArithmeticDecoderState refinementContexts;
    refinementContexts.reset((options.SBRTEMPLATE == 0) ? 13 : 10);

    for (const std::vector<size_t>& strip : stripInstances)
    {
        const int32_t stripT = instances[strip.front()].y / strips * strips;
        writeHuffman(writer, tableDT, (stripT - STRIPT) / strips);
        STRIPT = stripT;

        int32_t CURS = 0;
        for (size_t i = 0; i < strip.size(); ++i)
        {
            const TextInstance& instance = instances[strip[i]];
            if (i == 0)
            {
                writeHuffman(writer, tableFS, instance.x - FIRSTS);
                FIRSTS = instance.x;
            }
            else
            {
                writeHuffman(writer, tableDS, instance.x - CURS - options.dsOffset);
            }
            CURS = instance.x;

            if (strips > 1)
            {
                writer.writeBits(uint32_t(instance.y - stripT), options.logStrips);
            }

            writer.writeCode(symbolCodes[instance.id]);

            if (options.refine)
            {
                writer.writeBit(instance.refine ? 1 : 0);
                if (instance.refine)
                {
                    // 6.4.11 - the refinement data are coded arithmetically at a byte
                    // boundary, and their size is written before them
                    writeHuffman(writer, tableRDW, instance.RDW);
                    writeHuffman(writer, tableRDH, instance.RDH);
                    writeHuffman(writer, tableRDX, instance.RDX);
                    writeHuffman(writer, tableRDY, instance.RDY);

                    pdf::PDFJBIG2ArithmeticEncoder encoder;
                    encodeRefinement(encoder, refinementContexts, instance.refined, symbols[instance.id], (instance.RDW >> 1) + instance.RDX, (instance.RDH >> 1) + instance.RDY, options.SBRTEMPLATE, options.SBRAT);
                    const QByteArray refinementData = encoder.finish();

                    writeHuffman(writer, tableRSIZE, int32_t(refinementData.size()));
                    writer.append(refinementData);
                }
            }

            CURS += instanceWidth(instance) - 1;
        }

        writeHuffman(writer, tableDS, std::nullopt);
    }

    writer.align();
    data.append(writer.data());
    return data;
}

// ---------------------------------------------------------------------------------------
// Pattern dictionaries and halftone regions

QByteArray JBIG2SegmentsTest::encodePatternDictionary(const std::vector<Image>& patterns, bool HDMMR, uint8_t HDTEMPLATE)
{
    const int HDPW = imageWidth(patterns.front());
    const int HDPH = imageHeight(patterns.front());

    // 7.4.4.1 Pattern dictionary segment data header
    QByteArray data;
    data.append(char((HDMMR ? 0x01 : 0x00) | (HDTEMPLATE << 1)));
    data.append(char(HDPW));
    data.append(char(HDPH));
    appendUInt32(data, uint32_t(patterns.size()) - 1);

    // 6.7.5 - the collective bitmap of all patterns side by side
    Image collectiveBitmap = createImage(0, HDPH);
    for (const Image& pattern : patterns)
    {
        for (int y = 0; y < HDPH; ++y)
        {
            collectiveBitmap[y] += pattern[y];
        }
    }

    if (HDMMR)
    {
        data.append(encodeMMR(collectiveBitmap, false));
    }
    else
    {
        pdf::PDFJBIG2EncoderParameters parameters;
        parameters.MMR = false;
        parameters.TPGDON = false;
        parameters.GBTEMPLATE = HDTEMPLATE;
        parameters.GBAT = { { { int8_t(-HDPW), 0 }, { -3, -1 }, { 2, -2 }, { -2, -2 } } };

        pdf::PDFJBIG2ArithmeticDecoderState state;
        state.reset(pdf::PDFJBIG2EncoderParameters::getContextBitCount(HDTEMPLATE));
        pdf::PDFJBIG2ArithmeticEncoder encoder;

        const Bitmap bitmap = createBitmap(collectiveBitmap);
        pdf::PDFJBIG2Encoder::encodeGenericBitmap(bitmap.view(), parameters, encoder, state);
        data.append(encoder.finish());
    }

    return data;
}

std::pair<int64_t, int64_t> JBIG2SegmentsTest::getCellPosition(const HalftoneOptions& options, int m, int n)
{
    // 6.6.5.1 and 6.6.5.2
    const int64_t x = (int64_t(options.HGX) + int64_t(m) * int64_t(options.HRY) + int64_t(n) * int64_t(options.HRX)) >> 8;
    const int64_t y = (int64_t(options.HGY) + int64_t(m) * int64_t(options.HRX) - int64_t(n) * int64_t(options.HRY)) >> 8;
    return std::make_pair(x, y);
}

bool JBIG2SegmentsTest::isCellOutside(const HalftoneOptions& options, int64_t x, int64_t y)
{
    return (x + options.patternWidth <= 0) || (x >= int64_t(options.width)) || (y + options.patternHeight <= 0) || (y >= int64_t(options.height));
}

QByteArray JBIG2SegmentsTest::encodeHalftoneRegion(const std::vector<std::vector<uint32_t>>& values, const HalftoneOptions& options)
{
    const uint32_t HGH = uint32_t(values.size());
    const uint32_t HGW = HGH ? uint32_t(values.front().size()) : 0;

    // 7.4.5.1 Halftone region segment data header
    QByteArray data;
    appendRegionInformation(data, options.width, options.height, 0, 0, options.externalOperator);
    data.append(char((options.HMMR ? 0x01 : 0x00) | (options.HTEMPLATE << 1) | (options.HENABLESKIP ? 0x08 : 0x00) | (options.HCOMBOP << 4) | (options.HDEFPIXEL ? 0x80 : 0x00)));
    appendUInt32(data, HGW);
    appendUInt32(data, HGH);
    appendUInt32(data, uint32_t(options.HGX));
    appendUInt32(data, uint32_t(options.HGY));
    appendUInt16(data, options.HRX);
    appendUInt16(data, options.HRY);

    // 6.6.5.1 - the skipped cells
    Image skip = createImage(int(HGW), int(HGH));
    for (int m = 0; m < int(HGH); ++m)
    {
        for (int n = 0; n < int(HGW); ++n)
        {
            const auto [x, y] = getCellPosition(options, m, n);
            if (isCellOutside(options, x, y))
            {
                skip[m][n] = QChar('#');
            }
        }
    }
    const pdf::PDFJBIG2Bitmap skipBitmap = createDecoderBitmap(skip);

    // C.5 - the bit planes of the gray code of the values, the most significant plane
    // first, coded by a single arithmetic coder or by a single MMR stream with EOFB
    // between the planes
    uint32_t HBPP = 0;
    while ((1u << HBPP) < options.patternCount)
    {
        ++HBPP;
    }

    pdf::PDFJBIG2EncoderParameters parameters;
    parameters.MMR = false;
    parameters.TPGDON = false;
    parameters.GBTEMPLATE = options.HTEMPLATE;
    parameters.GBAT = { { { int8_t((options.HTEMPLATE <= 1) ? 3 : 2), -1 }, { -3, -1 }, { 2, -2 }, { -2, -2 } } };

    pdf::PDFJBIG2ArithmeticDecoderState state;
    state.reset(pdf::PDFJBIG2EncoderParameters::getContextBitCount(options.HTEMPLATE));
    pdf::PDFJBIG2ArithmeticEncoder encoder;
    QByteArray mmrData;

    for (int J = int(HBPP) - 1; J >= 0; --J)
    {
        Image plane = createImage(int(HGW), int(HGH));
        for (int m = 0; m < int(HGH); ++m)
        {
            for (int n = 0; n < int(HGW); ++n)
            {
                const uint32_t value = values[size_t(m)][size_t(n)];
                const uint32_t bit = (J == int(HBPP) - 1) ? ((value >> J) & 1) : (((value >> J) ^ (value >> (J + 1))) & 1);
                if (bit)
                {
                    plane[m][n] = QChar('#');
                }
            }
        }

        if (options.HMMR)
        {
            mmrData.append(encodeMMR(plane, true));
        }
        else
        {
            const Bitmap bitmap = createBitmap(plane);
            pdf::PDFJBIG2Encoder::encodeGenericBitmap(bitmap.view(), parameters, encoder, state, options.HENABLESKIP ? &skipBitmap : nullptr);
        }
    }

    data.append(options.HMMR ? mmrData : encoder.finish());
    return data;
}

JBIG2SegmentsTest::Image JBIG2SegmentsTest::composeHalftoneRegion(const std::vector<std::vector<uint32_t>>& values, const std::vector<Image>& patterns, const HalftoneOptions& options)
{
    // 6.6.5.2 Rendering the patterns
    constexpr pdf::PDFJBIG2BitOperation operations[] = { pdf::PDFJBIG2BitOperation::Or, pdf::PDFJBIG2BitOperation::And, pdf::PDFJBIG2BitOperation::Xor, pdf::PDFJBIG2BitOperation::NotXor, pdf::PDFJBIG2BitOperation::Replace };

    Image region = createImage(int(options.width), int(options.height), options.HDEFPIXEL);
    for (size_t m = 0; m < values.size(); ++m)
    {
        for (size_t n = 0; n < values[m].size(); ++n)
        {
            const auto [x, y] = getCellPosition(options, int(m), int(n));
            if (isCellOutside(options, x, y))
            {
                continue;
            }

            compose(region, patterns[values[m][n]], int(x), int(y), operations[options.HCOMBOP]);
        }
    }

    return region;
}

// ---------------------------------------------------------------------------------------
// Segments and streams

void JBIG2SegmentsTest::appendUInt32(QByteArray& data, uint32_t value)
{
    data.append(char((value >> 24) & 0xFF));
    data.append(char((value >> 16) & 0xFF));
    data.append(char((value >> 8) & 0xFF));
    data.append(char(value & 0xFF));
}

void JBIG2SegmentsTest::appendUInt16(QByteArray& data, uint16_t value)
{
    data.append(char((value >> 8) & 0xFF));
    data.append(char(value & 0xFF));
}

void JBIG2SegmentsTest::appendInt8(QByteArray& data, int8_t value)
{
    data.append(char(value));
}

void JBIG2SegmentsTest::appendATPositions(QByteArray& data, const pdf::PDFJBIG2ATPositions& at, int count)
{
    for (int i = 0; i < count; ++i)
    {
        appendInt8(data, at[size_t(i)].x);
        appendInt8(data, at[size_t(i)].y);
    }
}

void JBIG2SegmentsTest::appendRegionInformation(QByteArray& data, uint32_t width, uint32_t height, int32_t x, int32_t y, uint8_t externalOperator)
{
    appendUInt32(data, width);
    appendUInt32(data, height);
    appendUInt32(data, uint32_t(x));
    appendUInt32(data, uint32_t(y));
    data.append(char(externalOperator));
}

void JBIG2SegmentsTest::appendSegment(QByteArray& stream, uint32_t segmentNumber, SegmentType type, const std::vector<uint32_t>& referredSegments, const QByteArray& data, bool longPageAssociation)
{
    appendSegmentWithLength(stream, segmentNumber, type, referredSegments, data, uint32_t(data.size()), longPageAssociation);
}

void JBIG2SegmentsTest::appendSegmentWithLength(QByteArray& stream, uint32_t segmentNumber, SegmentType type, const std::vector<uint32_t>& referredSegments, const QByteArray& data, uint32_t dataLength, bool longPageAssociation)
{
    appendUInt32(stream, segmentNumber);
    stream.append(char(type | (longPageAssociation ? 0x40 : 0x00)));

    if (referredSegments.size() <= 4)
    {
        stream.append(char(referredSegments.size() << 5));
    }
    else
    {
        // 7.2.4 - the long form is a four byte count with the top three bits set,
        // followed by the retain bits
        const uint32_t count = uint32_t(referredSegments.size());
        appendUInt32(stream, 0xE0000000 | count);
        stream.append(QByteArray(int((count + 8) / 8), char(0)));
    }

    // 7.2.5 - the size of the referred segment numbers is given by the segment number
    for (const uint32_t referredSegment : referredSegments)
    {
        if (segmentNumber <= 256)
        {
            stream.append(char(referredSegment));
        }
        else if (segmentNumber <= 65536)
        {
            appendUInt16(stream, uint16_t(referredSegment));
        }
        else
        {
            appendUInt32(stream, referredSegment);
        }
    }

    if (longPageAssociation)
    {
        appendUInt32(stream, 1);
    }
    else
    {
        stream.append(char(0x01));
    }

    appendUInt32(stream, dataLength);
    stream.append(data);
}

QByteArray JBIG2SegmentsTest::createPageInformationData(uint32_t width, uint32_t height, uint8_t flags)
{
    QByteArray data;
    appendUInt32(data, width);
    appendUInt32(data, height);
    appendUInt32(data, 0);
    appendUInt32(data, 0);
    data.append(char(flags));
    appendUInt16(data, 0);
    return data;
}

QByteArray JBIG2SegmentsTest::createPageStream(uint32_t width, uint32_t height, uint8_t flags)
{
    QByteArray stream;
    appendSegment(stream, 0, PageInformation, { }, createPageInformationData(width, height, flags));
    return stream;
}

JBIG2SegmentsTest::Image JBIG2SegmentsTest::drawImage(const pdf::PDFImageData& imageData)
{
    Image rows;
    const QByteArray& data = imageData.getData();

    for (uint32_t row = 0; row < imageData.getHeight(); ++row)
    {
        QString line;
        for (uint32_t column = 0; column < imageData.getWidth(); ++column)
        {
            const uint8_t byte = uint8_t(data[int(row * imageData.getStride() + column / 8)]);
            line += ((byte >> (7 - (column % 8))) & 1) ? QChar('.') : QChar('#');
        }
        rows << line;
    }

    return rows;
}

JBIG2SegmentsTest::Image JBIG2SegmentsTest::decodePage(const QByteArray& stream, const QStringList& expectedMessages)
{
    ErrorCollector errorCollector;
    pdf::PDFJBIG2Decoder decoder(stream, QByteArray(), &errorCollector);
    const Image image = drawImage(decoder.decode(pdf::PDFImageData::MaskingType::None));

    if (errorCollector.messages != expectedMessages)
    {
        throw std::runtime_error(qPrintable(QString("Unexpected messages of the decoder: %1").arg(errorCollector.messages.join("; "))));
    }

    return image;
}

QString JBIG2SegmentsTest::decodeExpectingError(const QByteArray& stream)
{
    try
    {
        pdf::PDFRenderErrorReporterDummy errorReporter;
        pdf::PDFJBIG2Decoder decoder(stream, QByteArray(), &errorReporter);
        decoder.decode(pdf::PDFImageData::MaskingType::None);
    }
    catch (const pdf::PDFException& exception)
    {
        return exception.getMessage();
    }

    return QString();
}

JBIG2SegmentsTest::Image JBIG2SegmentsTest::decodeFile(const QByteArray& file)
{
    ErrorCollector errorCollector;
    pdf::PDFJBIG2Decoder decoder(file, QByteArray(), &errorCollector);
    const Image image = drawImage(decoder.decodeFileStream());

    if (!errorCollector.messages.isEmpty())
    {
        throw std::runtime_error(qPrintable(QString("Unexpected messages of the decoder: %1").arg(errorCollector.messages.join("; "))));
    }

    return image;
}

QString JBIG2SegmentsTest::decodeFileExpectingError(const QByteArray& file)
{
    try
    {
        pdf::PDFRenderErrorReporterDummy errorReporter;
        pdf::PDFJBIG2Decoder decoder(file, QByteArray(), &errorReporter);
        decoder.decodeFileStream();
    }
    catch (const pdf::PDFException& exception)
    {
        return exception.getMessage();
    }

    return QString();
}

std::vector<JBIG2SegmentsTest::Image> JBIG2SegmentsTest::getSymbols()
{
    // The symbols are sorted by their height - the height classes of the dictionaries
    // are the small dot and the three letters
    return
    {
        { "##",
          "##" },

        { ".##.",
          "#..#",
          "####",
          "#..#",
          "#..#" },

        { "###.",
          "#..#",
          "###.",
          "#..#",
          "###." },

        { ".###",
          "#...",
          "#...",
          "#...",
          ".###" }
    };
}

// ---------------------------------------------------------------------------------------
// Tests

void JBIG2SegmentsTest::test_arithmetic_symbol_dictionary_and_text_region()
{
    // An arithmetically coded symbol dictionary of every template and a text region
    // placing its symbols, with every combination operator of the text region and
    // both default pixel values. The strips of four rows exercise the coding of the
    // row inside of a strip, and the negative offset of the S coordinate the sign of
    // the integer arithmetic coding.
    const std::vector<Image> symbols = getSymbols();

    const std::vector<TextInstance> instances =
    {
        { 1, 1, 1 }, { 6, 1, 2 }, { 11, 2, 3 }, { 17, 3, 0 },
        { 0, 7, 3 }, { 3, 7, 1 }, { 9, 9, 0 }, { 12, 8, 2 }
    };

    for (const uint8_t SDTEMPLATE : { uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3) })
    {
        for (const uint8_t combinationOperator : { uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3) })
        {
            for (const bool defaultPixel : { false, true })
            {
                SymbolDictionaryOptions dictionaryOptions;
                dictionaryOptions.SDTEMPLATE = SDTEMPLATE;

                ArithmeticContexts contexts;
                contexts.reset(0, SDTEMPLATE, 0);

                TextRegionOptions regionOptions;
                regionOptions.width = 24;
                regionOptions.height = 14;
                regionOptions.logStrips = 2;
                regionOptions.combinationOperator = combinationOperator;
                regionOptions.defaultPixel = defaultPixel;
                regionOptions.dsOffset = -2;

                QByteArray stream = createPageStream(24, 14);
                appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(symbols, dictionaryOptions, contexts));
                appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion(instances, symbols, regionOptions));

                constexpr pdf::PDFJBIG2BitOperation operations[] = { pdf::PDFJBIG2BitOperation::Or, pdf::PDFJBIG2BitOperation::And, pdf::PDFJBIG2BitOperation::Xor, pdf::PDFJBIG2BitOperation::NotXor };
                Image expected = createImage(24, 14, defaultPixel);
                for (const TextInstance& instance : instances)
                {
                    compose(expected, symbols[instance.id], instance.x, instance.y, operations[combinationOperator]);
                }

                QCOMPARE(decodePage(stream), expected);
            }
        }
    }

    // An intermediate text region is stored and refined by a refinement region referring
    // to it - the refinement inverts the region
    {
        SymbolDictionaryOptions dictionaryOptions;
        ArithmeticContexts contexts;
        contexts.reset(0, 0, 0);

        TextRegionOptions regionOptions;
        regionOptions.width = 24;
        regionOptions.height = 14;
        regionOptions.logStrips = 2;

        Image text = createImage(24, 14);
        for (const TextInstance& instance : instances)
        {
            compose(text, symbols[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
        }
        const Image inverted = invertImage(text);

        pdf::PDFJBIG2ArithmeticDecoderState state;
        state.reset(13);
        pdf::PDFJBIG2ArithmeticEncoder encoder;
        encodeRefinement(encoder, state, inverted, text, 0, 0, 0, NOMINAL_REFINEMENT_AT);

        QByteArray refinement;
        appendRegionInformation(refinement, 24, 14, 0, 0, 0);
        refinement.append(char(0x00));
        appendATPositions(refinement, NOMINAL_REFINEMENT_AT, 2);
        refinement.append(encoder.finish());

        QByteArray stream = createPageStream(24, 14);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(symbols, dictionaryOptions, contexts));
        appendSegment(stream, 2, IntermediateTextRegion, { 1 }, encodeTextRegion(instances, symbols, regionOptions));
        appendSegment(stream, 3, ImmediateRefinementRegion, { 2 }, refinement);
        QCOMPARE(decodePage(stream), inverted);
    }

    // A new symbol, which is not exported, is left out of the symbols of the text region
    {
        SymbolDictionaryOptions dictionaryOptions;
        dictionaryOptions.exportFlags = { true, false, true, true };

        ArithmeticContexts contexts;
        contexts.reset(0, 0, 0);

        const std::vector<Image> exported = { symbols[0], symbols[2], symbols[3] };
        const std::vector<TextInstance> exportedInstances = { { 0, 0, 0 }, { 3, 0, 1 }, { 8, 0, 2 } };

        TextRegionOptions regionOptions;
        regionOptions.width = 14;
        regionOptions.height = 6;

        QByteArray stream = createPageStream(14, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(symbols, dictionaryOptions, contexts));
        appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion(exportedInstances, exported, regionOptions));

        Image expected = createImage(14, 6);
        for (const TextInstance& instance : exportedInstances)
        {
            compose(expected, exported[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
        }
        QCOMPARE(decodePage(stream), expected);
    }
}

void JBIG2SegmentsTest::test_arithmetic_coding_context_reuse()
{
    // A symbol dictionary can retain the contexts of its arithmetic coding (7.4.2.1.1,
    // bit 9) and a later dictionary referring to it can start with them (bit 8) instead
    // of the initial ones - the symbols of the second dictionary are then coded with the
    // adapted probabilities. The second dictionary imports the symbols of the first one.
    const std::vector<Image> symbols = getSymbols();
    const std::vector<Image> firstSymbols = { symbols[0], symbols[1] };
    const std::vector<Image> secondSymbols = { symbols[2], symbols[3] };

    SymbolDictionaryOptions firstOptions;
    firstOptions.contextRetained = true;

    ArithmeticContexts firstContexts;
    firstContexts.reset(0, 0, 0);
    const QByteArray firstDictionary = encodeSymbolDictionary(firstSymbols, firstOptions, firstContexts);

    SymbolDictionaryOptions secondOptions;
    secondOptions.contextUsed = true;
    secondOptions.inputSymbolCount = 2;

    ArithmeticContexts secondContexts;
    secondContexts.reset(0, 0, 0);
    secondContexts.generic = firstContexts.generic;
    const QByteArray secondDictionary = encodeSymbolDictionary(secondSymbols, secondOptions, secondContexts);

    const std::vector<TextInstance> instances = { { 0, 0, 0 }, { 3, 0, 1 }, { 8, 0, 2 }, { 13, 0, 3 } };
    TextRegionOptions regionOptions;
    regionOptions.width = 18;
    regionOptions.height = 6;

    QByteArray stream = createPageStream(18, 6);
    appendSegment(stream, 1, SymbolDictionary, { }, firstDictionary);
    appendSegment(stream, 2, SymbolDictionary, { 1 }, secondDictionary);
    appendSegment(stream, 3, ImmediateTextRegion, { 2 }, encodeTextRegion(instances, symbols, regionOptions));

    Image expected = createImage(18, 6);
    for (const TextInstance& instance : instances)
    {
        compose(expected, symbols[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
    }
    QCOMPARE(decodePage(stream), expected);

    // The contexts can not be used without a referred dictionary
    QByteArray withoutReference = createPageStream(18, 6);
    appendSegment(withoutReference, 1, SymbolDictionary, { }, secondDictionary);
    QVERIFY(decodeExpectingError(withoutReference).contains("previous symbol dictionary"));

    // A refinement dictionary using the contexts of the first dictionary refines its
    // symbols with the initial refinement contexts, because the first dictionary has
    // not used any. The huffman variant checks the referred dictionary separately.
    for (const bool huffman : { false, true })
    {
        SymbolDictionaryOptions refinementOptions;
        refinementOptions.huffman = huffman;
        refinementOptions.refinement = true;
        refinementOptions.contextUsed = true;

        const std::vector<RefinedSymbol> refinedSymbols = { { symbols[2], 1, 0, 0 } };

        ArithmeticContexts refinementContexts;
        refinementContexts.reset(2, 0, 0);
        refinementContexts.generic = firstContexts.generic;

        QByteArray refinementStream = createPageStream(18, 6);
        appendSegment(refinementStream, 1, SymbolDictionary, { }, firstDictionary);
        appendSegment(refinementStream, 2, SymbolDictionary, { 1 }, encodeRefinementSymbolDictionary(firstSymbols, refinedSymbols, refinementOptions, refinementContexts));

        const std::vector<TextInstance> refinedInstances = { { 0, 0, 0 }, { 3, 0, 1 }, { 8, 0, 2 } };
        appendSegment(refinementStream, 3, ImmediateTextRegion, { 2 }, encodeTextRegion(refinedInstances, symbols, regionOptions));

        Image refinedExpected = createImage(18, 6);
        for (const TextInstance& instance : refinedInstances)
        {
            compose(refinedExpected, symbols[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
        }
        QCOMPARE(decodePage(refinementStream), refinedExpected);

        QByteArray refinementWithoutReference = createPageStream(18, 6);
        appendSegment(refinementWithoutReference, 1, SymbolDictionary, { }, encodeRefinementSymbolDictionary({ }, { }, refinementOptions, refinementContexts));
        QVERIFY(decodeExpectingError(refinementWithoutReference).contains("previous symbol dictionary"));
    }
}

void JBIG2SegmentsTest::test_refinement_aggregate_symbol_dictionaries()
{
    // A symbol of a refinement/aggregate dictionary can be a refinement of a single
    // symbol (6.5.8.2.2) - an input symbol or a symbol decoded before it in the same
    // dictionary, at an offset. Both refinement templates and both codings are tested.
    const std::vector<Image> symbols = getSymbols();
    const std::vector<Image> inputSymbols = { symbols[1], symbols[2] };

    // The refined symbols - the letter "a" with a dot, the letter "b" shifted, and the
    // letter "c" refined from the new letter "a"
    const Image refinedA = { ".##.", "#..#", "####", "#.##", "#..#" };
    const Image refinedB = { ".###", ".#..", ".###", ".#.#", ".###" };
    const std::vector<RefinedSymbol> refinedSymbols =
    {
        { refinedA, 0, 0, 0 },
        { refinedB, 1, -1, 0 },
        { symbols[3], 2, 0, 0 }
    };

    for (const bool huffman : { false, true })
    {
        for (const uint8_t SDRTEMPLATE : { uint8_t(0), uint8_t(1) })
        {
            SymbolDictionaryOptions inputOptions;
            inputOptions.huffman = huffman;
            ArithmeticContexts inputContexts;
            inputContexts.reset(0, 0, 0);

            SymbolDictionaryOptions options;
            options.huffman = huffman;
            options.refinement = true;
            options.SDRTEMPLATE = SDRTEMPLATE;
            options.SDRAT = { { { -2, -2 }, { 1, 1 }, { 0, 0 }, { 0, 0 } } };

            // Only the new symbols are exported
            options.exportFlags = { false, false, true, true, true };

            ArithmeticContexts contexts;
            contexts.reset(3, 0, SDRTEMPLATE);

            QByteArray stream = createPageStream(16, 6);
            appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(inputSymbols, inputOptions, inputContexts));
            appendSegment(stream, 2, SymbolDictionary, { 1 }, encodeRefinementSymbolDictionary(inputSymbols, refinedSymbols, options, contexts));

            const std::vector<TextInstance> instances = { { 0, 0, 0 }, { 5, 0, 1 }, { 10, 0, 2 } };
            TextRegionOptions regionOptions;
            regionOptions.width = 16;
            regionOptions.height = 6;
            appendSegment(stream, 3, ImmediateTextRegion, { 2 }, encodeTextRegion(instances, { refinedA, refinedB, symbols[3] }, regionOptions));

            Image expected = createImage(16, 6);
            compose(expected, refinedA, 0, 0, pdf::PDFJBIG2BitOperation::Or);
            compose(expected, refinedB, 5, 0, pdf::PDFJBIG2BitOperation::Or);
            compose(expected, symbols[3], 10, 0, pdf::PDFJBIG2BitOperation::Or);
            QCOMPARE(decodePage(stream), expected);
        }
    }

    // The Power JBIG-2 encoder codes the aggregation of a single symbol instance by the
    // whole text region decoding procedure - the decoder falls back to this form, when
    // the form of the specification fails, and it reports the fallback
    {
        SymbolDictionaryOptions inputOptions;
        ArithmeticContexts inputContexts;
        inputContexts.reset(0, 0, 0);

        SymbolDictionaryOptions options;
        options.refinement = true;
        options.textRegionForm = true;
        options.exportFlags = { false, false, true, true, true };

        ArithmeticContexts contexts;
        contexts.reset(3, 0, 0);

        QByteArray stream = createPageStream(16, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(inputSymbols, inputOptions, inputContexts));
        appendSegment(stream, 2, SymbolDictionary, { 1 }, encodeRefinementSymbolDictionary(inputSymbols, refinedSymbols, options, contexts));

        const std::vector<TextInstance> instances = { { 0, 0, 0 }, { 5, 0, 1 }, { 10, 0, 2 } };
        TextRegionOptions regionOptions;
        regionOptions.width = 16;
        regionOptions.height = 6;
        appendSegment(stream, 3, ImmediateTextRegion, { 2 }, encodeTextRegion(instances, { refinedA, refinedB, symbols[3] }, regionOptions));

        Image expected = createImage(16, 6);
        compose(expected, refinedA, 0, 0, pdf::PDFJBIG2BitOperation::Or);
        compose(expected, refinedB, 5, 0, pdf::PDFJBIG2BitOperation::Or);
        compose(expected, symbols[3], 10, 0, pdf::PDFJBIG2BitOperation::Or);
        QCOMPARE(decodePage(stream, { "JBIG2 symbol dictionary uses the text region decoding procedure for a single symbol instance aggregation." }), expected);
    }

    // The refinement data of a huffman dictionary are followed by the data of the next
    // symbol at the declared size, so the arithmetic decoder need not reach the marker
    // terminating them - the decoder of a single decision reads two bytes only
    {
        const std::vector<Image> dot = { { "." } };

        SymbolDictionaryOptions inputOptions;
        inputOptions.huffman = true;
        ArithmeticContexts inputContexts;

        SymbolDictionaryOptions options;
        options.huffman = true;
        options.refinement = true;
        options.exportFlags = { false, true };
        options.refinementDataOverride = QByteArray("\x00\x00\xFF\xAC", 4);

        ArithmeticContexts contexts;
        contexts.reset(1, 0, 0);

        QByteArray stream = createPageStream(4, 4);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(dot, inputOptions, inputContexts));
        appendSegment(stream, 2, SymbolDictionary, { 1 }, encodeRefinementSymbolDictionary(dot, { { dot.front(), 0, 0, 0 } }, options, contexts));

        TextRegionOptions regionOptions;
        regionOptions.width = 4;
        regionOptions.height = 4;
        appendSegment(stream, 3, ImmediateTextRegion, { 2 }, encodeTextRegion({ { 1, 1, 0 }, { 2, 2, 0 } }, dot, regionOptions));
        QCOMPARE(decodePage(stream), createImage(4, 4));
    }

    // The number of the instances of the aggregation must be positive, and the
    // refined symbol must exist already. A dictionary using the refinement/aggregate
    // coding, which fails to decode, is decoded again by the form of the Power JBIG-2
    // encoder, and the error of the form of the specification is reported, when the
    // other form fails as well.
    {
        SymbolDictionaryOptions options;
        options.refinement = true;
        options.refinementInstanceCount = 0;
        ArithmeticContexts contexts;
        contexts.reset(2, 0, 0);

        QByteArray stream = createPageStream(16, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeRefinementSymbolDictionary({ }, { { symbols[1], 0, 0, 0 }, { symbols[2], 0, 0, 0 } }, options, contexts));
        QVERIFY(decodeExpectingError(stream).contains("number of symbol instances"));
    }

    {
        SymbolDictionaryOptions options;
        options.refinement = true;
        options.refinementIdOverride = 1;
        ArithmeticContexts contexts;
        contexts.reset(1, 0, 0);

        QByteArray stream = createPageStream(16, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeRefinementSymbolDictionary({ }, { { symbols[1], 0, 0, 0 }, { symbols[2], 0, 0, 0 } }, options, contexts));
        const QString message = decodeExpectingError(stream);
        QVERIFY2(message.contains("reference bitmap 1"), qPrintable(message));
    }
}

void JBIG2SegmentsTest::test_huffman_symbol_dictionaries()
{
    // A huffman coded symbol dictionary selects the tables of the height class delta
    // height (B.4, B.5 or a custom one), of the delta width (B.2, B.3 or a custom one)
    // and of the size of the collective bitmap (B.1 or a custom one), see 7.4.2.1.1. The
    // collective bitmap of a height class is either MMR coded or uncompressed.
    const std::vector<Image> symbols = getSymbols();

    // Custom tables covering the values of the dictionary - the delta heights 2 and 3,
    // the delta widths -4 ... 4 with the out-of-band value, and the sizes 0 ... 63
    CustomTable heightTable;
    heightTable.HTPS = 3;
    heightTable.HTRS = 3;
    heightTable.HTLOW = 0;
    heightTable.lines = { { 2, 2 }, { 1, 1 } };
    heightTable.lowerPrefixLength = 3;
    heightTable.upperPrefixLength = 3;

    CustomTable widthTable;
    widthTable.HTOOB = true;
    widthTable.HTPS = 3;
    widthTable.HTRS = 3;
    widthTable.HTLOW = -4;
    widthTable.lines = { { 3, 2 }, { 2, 2 }, { 3, 1 } };
    widthTable.lowerPrefixLength = 4;
    widthTable.upperPrefixLength = 4;
    widthTable.oobPrefixLength = 2;

    CustomTable sizeTable;
    sizeTable.HTPS = 2;
    sizeTable.HTRS = 3;
    sizeTable.HTLOW = 0;
    sizeTable.lines = { { 1, 6 } };
    sizeTable.lowerPrefixLength = 2;
    sizeTable.upperPrefixLength = 2;

    const std::vector<TextInstance> instances = { { 0, 0, 0 }, { 3, 0, 1 }, { 8, 0, 2 }, { 13, 0, 3 } };
    TextRegionOptions regionOptions;
    regionOptions.width = 18;
    regionOptions.height = 6;
    const QByteArray textRegion = encodeTextRegion(instances, symbols, regionOptions);

    Image expected = createImage(18, 6);
    for (const TextInstance& instance : instances)
    {
        compose(expected, symbols[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
    }

    struct Case
    {
        uint8_t SDHUFFDH;
        uint8_t SDHUFFDW;
        uint8_t SDHUFFBMSIZE;
        bool uncompressed;
    };

    const std::vector<Case> cases =
    {
        { 0, 0, 0, false },
        { 1, 1, 0, true },
        { 3, 3, 1, false },
        { 3, 3, 1, true },
        { 1, 0, 1, false },
        { 0, 1, 0, true }
    };

    for (const Case& testCase : cases)
    {
        SymbolDictionaryOptions options;
        options.huffman = true;
        options.SDHUFFDH = testCase.SDHUFFDH;
        options.SDHUFFDW = testCase.SDHUFFDW;
        options.SDHUFFBMSIZE = testCase.SDHUFFBMSIZE;
        options.uncompressedCollectiveBitmap = testCase.uncompressed;

        std::vector<uint32_t> referredSegments;
        QByteArray stream = createPageStream(18, 6);
        uint32_t segmentNumber = 1;

        if (testCase.SDHUFFDH == 3)
        {
            options.userTables.push_back(heightTable.table());
            appendSegment(stream, segmentNumber, Tables, { }, heightTable.segmentData());
            referredSegments.push_back(segmentNumber++);
        }
        if (testCase.SDHUFFDW == 3)
        {
            options.userTables.push_back(widthTable.table());
            appendSegment(stream, segmentNumber, Tables, { }, widthTable.segmentData());
            referredSegments.push_back(segmentNumber++);
        }
        if (testCase.SDHUFFBMSIZE == 1)
        {
            options.userTables.push_back(sizeTable.table());
            appendSegment(stream, segmentNumber, Tables, { }, sizeTable.segmentData());
            referredSegments.push_back(segmentNumber++);
        }

        ArithmeticContexts contexts;
        appendSegment(stream, segmentNumber, SymbolDictionary, referredSegments, encodeSymbolDictionary(symbols, options, contexts));
        appendSegment(stream, segmentNumber + 1, ImmediateTextRegion, { segmentNumber }, textRegion);

        QVERIFY2(decodePage(stream) == expected, qPrintable(QString("DH %1, DW %2, BMSIZE %3, uncompressed %4").arg(testCase.SDHUFFDH).arg(testCase.SDHUFFDW).arg(testCase.SDHUFFBMSIZE).arg(testCase.uncompressed)));
    }

    // A refinement dictionary selects the table of the number of the instances of the
    // aggregation (B.1 or a custom one) and it reads the selected table of the size of
    // the collective bitmap, although it has no collective bitmap
    CustomTable instanceTable;
    instanceTable.HTPS = 2;
    instanceTable.HTRS = 2;
    instanceTable.HTLOW = 1;
    instanceTable.lines = { { 1, 1 } };
    instanceTable.lowerPrefixLength = 2;
    instanceTable.upperPrefixLength = 2;

    for (const uint8_t SDHUFFAGGINST : { uint8_t(0), uint8_t(1) })
    {
        SymbolDictionaryOptions inputOptions;
        inputOptions.huffman = true;
        ArithmeticContexts inputContexts;

        SymbolDictionaryOptions options;
        options.huffman = true;
        options.refinement = true;
        options.SDHUFFBMSIZE = 1;
        options.SDHUFFAGGINST = SDHUFFAGGINST;
        options.exportFlags = { false, false, true, true };
        options.userTables.push_back(sizeTable.table());
        if (SDHUFFAGGINST == 1)
        {
            options.userTables.push_back(instanceTable.table());
        }

        const std::vector<Image> inputSymbols = { symbols[1], symbols[2] };
        const std::vector<RefinedSymbol> refinedSymbols = { { symbols[3], 0, 0, 0 }, { symbols[1], 1, 0, 0 } };

        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(inputSymbols, inputOptions, inputContexts));
        appendSegment(stream, 2, Tables, { }, sizeTable.segmentData());
        appendSegment(stream, 3, Tables, { }, instanceTable.segmentData());

        ArithmeticContexts contexts;
        contexts.reset(2, 0, 0);
        std::vector<uint32_t> referredSegments = { 1, 2 };
        if (SDHUFFAGGINST == 1)
        {
            referredSegments.push_back(3);
        }
        appendSegment(stream, 4, SymbolDictionary, referredSegments, encodeRefinementSymbolDictionary(inputSymbols, refinedSymbols, options, contexts));

        const std::vector<TextInstance> refinedInstances = { { 0, 0, 0 }, { 5, 0, 1 } };
        appendSegment(stream, 5, ImmediateTextRegion, { 4 }, encodeTextRegion(refinedInstances, { symbols[3], symbols[1] }, regionOptions));

        Image refinedExpected = createImage(18, 6);
        compose(refinedExpected, symbols[3], 0, 0, pdf::PDFJBIG2BitOperation::Or);
        compose(refinedExpected, symbols[1], 5, 0, pdf::PDFJBIG2BitOperation::Or);
        QCOMPARE(decodePage(stream), refinedExpected);
    }
}

void JBIG2SegmentsTest::test_huffman_text_regions()
{
    // A huffman coded text region selects the tables of the first S coordinate (B.6,
    // B.7 or a custom one), of the delta S (B.8, B.9, B.10 or a custom one), of the
    // delta T (B.11, B.12, B.13 or a custom one), of the refinement deltas (B.14, B.15
    // or a custom one) and of the size of the refinement data (B.1 or a custom one), see
    // 7.4.3.1.2. The symbol ID codes are coded by the run codes of 7.4.3.1.7.
    const std::vector<Image> symbols = getSymbols();

    // The refined instances - the letter "a" grown by a column on the left, and the
    // letter "c" refined with the same size
    const Image grownA = { "..##.", ".#..#", ".####", "##..#", ".#..#" };
    const Image refinedC = { ".###", "#...", "#.##", "#...", ".###" };

    const std::vector<TextInstance> instances =
    {
        { 1, 1, 1, true, grownA, 1, 0, 0, 0 }, { 7, 1, 2 }, { 12, 3, 3, true, refinedC, 0, 0, 0, 0 }, { 18, 3, 0 },
        { 0, 7, 3 }, { 3, 7, 1 }, { 12, 8, 2 }, { 9, 9, 0 }
    };

    Image expected = createImage(24, 14);
    for (const TextInstance& instance : instances)
    {
        compose(expected, instance.refine ? instance.refined : symbols[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
    }

    // Custom tables - the first S in -16 ... 47, the delta S in -8 ... 23 with the
    // out-of-band value, the delta T in 0 ... 15, the refinement deltas in -2 ... 5 and
    // the sizes 0 ... 63
    CustomTable firstTable;
    firstTable.HTPS = 3;
    firstTable.HTRS = 4;
    firstTable.HTLOW = -16;
    firstTable.lines = { { 2, 4 }, { 1, 5 } };
    firstTable.lowerPrefixLength = 3;
    firstTable.upperPrefixLength = 3;

    CustomTable deltaTable;
    deltaTable.HTOOB = true;
    deltaTable.HTPS = 3;
    deltaTable.HTRS = 4;
    deltaTable.HTLOW = -8;
    deltaTable.lines = { { 3, 3 }, { 2, 4 }, { 3, 3 } };
    deltaTable.lowerPrefixLength = 4;
    deltaTable.upperPrefixLength = 4;
    deltaTable.oobPrefixLength = 2;

    CustomTable stripTable;
    stripTable.HTPS = 2;
    stripTable.HTRS = 3;
    stripTable.HTLOW = 0;
    stripTable.lines = { { 1, 4 } };
    stripTable.lowerPrefixLength = 2;
    stripTable.upperPrefixLength = 2;

    CustomTable refinementTable;
    refinementTable.HTPS = 2;
    refinementTable.HTRS = 2;
    refinementTable.HTLOW = -2;
    refinementTable.lines = { { 1, 3 } };
    refinementTable.lowerPrefixLength = 2;
    refinementTable.upperPrefixLength = 2;

    CustomTable sizeTable;
    sizeTable.HTPS = 2;
    sizeTable.HTRS = 3;
    sizeTable.HTLOW = 0;
    sizeTable.lines = { { 1, 6 } };
    sizeTable.lowerPrefixLength = 2;
    sizeTable.upperPrefixLength = 2;

    struct Case
    {
        uint8_t FS;
        uint8_t DS;
        uint8_t DT;
        uint8_t RD;
        uint8_t RSIZE;
        uint8_t SBRTEMPLATE;
        uint8_t logStrips;
    };

    const std::vector<Case> cases =
    {
        { 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 1, 1, 0, 1, 1 },
        { 0, 2, 2, 0, 1, 0, 2 },
        { 3, 3, 3, 3, 1, 1, 0 }
    };

    for (const Case& testCase : cases)
    {
        TextRegionOptions options;
        options.width = 24;
        options.height = 14;
        options.huffman = true;
        options.refine = true;
        options.logStrips = testCase.logStrips;
        options.SBRTEMPLATE = testCase.SBRTEMPLATE;
        options.SBHUFFFS = testCase.FS;
        options.SBHUFFDS = testCase.DS;
        options.SBHUFFDT = testCase.DT;
        options.SBHUFFRDW = testCase.RD;
        options.SBHUFFRDH = testCase.RD;
        options.SBHUFFRDX = testCase.RD;
        options.SBHUFFRDY = testCase.RD;
        options.SBHUFFRSIZE = testCase.RSIZE;

        // The dictionary is arithmetic, the tables are referred to after it
        SymbolDictionaryOptions dictionaryOptions;
        ArithmeticContexts contexts;
        contexts.reset(0, 0, 0);

        QByteArray stream = createPageStream(24, 14);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(symbols, dictionaryOptions, contexts));

        std::vector<uint32_t> referredSegments = { 1 };
        uint32_t segmentNumber = 2;
        auto addTable = [&](const CustomTable& table)
        {
            options.userTables.push_back(table.table());
            appendSegment(stream, segmentNumber, Tables, { }, table.segmentData());
            referredSegments.push_back(segmentNumber++);
        };

        if (testCase.FS == 3)
        {
            addTable(firstTable);
        }
        if (testCase.DS == 3)
        {
            addTable(deltaTable);
        }
        if (testCase.DT == 3)
        {
            addTable(stripTable);
        }
        if (testCase.RD == 3)
        {
            addTable(refinementTable);
            addTable(refinementTable);
            addTable(refinementTable);
            addTable(refinementTable);
        }
        if (testCase.RSIZE == 1)
        {
            addTable(sizeTable);
        }

        appendSegment(stream, segmentNumber, ImmediateTextRegion, referredSegments, encodeTextRegion(instances, symbols, options));
        QVERIFY2(decodePage(stream) == expected, qPrintable(QString("FS %1, DS %2, DT %3, RD %4, RSIZE %5").arg(testCase.FS).arg(testCase.DS).arg(testCase.DT).arg(testCase.RD).arg(testCase.RSIZE)));
    }

    // The lower range line of a table codes the values below the table, so the first
    // S coordinate can be far to the left of the region, and the upper range line the
    // values above it - the next instance is then far to the right of the first one
    {
        const std::vector<TextInstance> farInstances = { { -3000, 0, 1 }, { 2, 0, 2 }, { 7, 0, 3 } };

        TextRegionOptions options;
        options.width = 12;
        options.height = 6;
        options.huffman = true;

        SymbolDictionaryOptions dictionaryOptions;
        ArithmeticContexts contexts;
        contexts.reset(0, 0, 0);

        QByteArray stream = createPageStream(12, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(symbols, dictionaryOptions, contexts));
        appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion(farInstances, symbols, options));

        Image farExpected = createImage(12, 6);
        compose(farExpected, symbols[2], 2, 0, pdf::PDFJBIG2BitOperation::Or);
        compose(farExpected, symbols[3], 7, 0, pdf::PDFJBIG2BitOperation::Or);
        QCOMPARE(decodePage(stream), farExpected);
    }

    // The run codes of the symbol ID code lengths - a dictionary of many symbols, of
    // which only a few are used, has runs of the zero length (the run codes 33 and 34),
    // and the used symbols with the same length form a run of the run code 32
    {
        std::vector<Image> manySymbols;
        for (int i = 0; i < 40; ++i)
        {
            manySymbols.push_back(createImage(2, 2, (i % 2) == 0));
        }
        manySymbols.insert(manySymbols.end(), symbols.cbegin() + 1, symbols.cend());

        // The lengths of the used symbols - the run of the four equal lengths is coded
        // by the run code 32, the three unused symbols between the symbols 20 and 24 by
        // the run code 33 and the longer runs of the unused symbols by the run code 34
        std::vector<int> lengths(manySymbols.size(), 0);
        lengths[0] = 4;
        lengths[1] = 4;
        lengths[2] = 4;
        lengths[3] = 4;
        lengths[20] = 3;
        lengths[24] = 3;
        lengths[40] = 3;
        lengths[41] = 3;
        lengths[42] = 3;

        const std::vector<TextInstance> manyInstances = { { 0, 0, 40 }, { 5, 0, 41 }, { 10, 0, 42 }, { 15, 0, 0 }, { 18, 0, 2 }, { 21, 0, 20 }, { 15, 3, 1 }, { 18, 3, 3 }, { 21, 3, 24 } };

        TextRegionOptions options;
        options.width = 24;
        options.height = 6;
        options.huffman = true;
        options.symbolCodeLengths = lengths;

        SymbolDictionaryOptions dictionaryOptions;
        ArithmeticContexts contexts;
        contexts.reset(0, 0, 0);

        QByteArray dictionaryStream = createPageStream(24, 6);
        appendSegment(dictionaryStream, 1, SymbolDictionary, { }, encodeSymbolDictionary(manySymbols, dictionaryOptions, contexts));

        QByteArray stream = dictionaryStream;
        appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion(manyInstances, manySymbols, options));

        Image manyExpected = createImage(24, 6);
        for (const TextInstance& instance : manyInstances)
        {
            compose(manyExpected, manySymbols[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
        }
        QCOMPARE(decodePage(stream), manyExpected);

        // The run code 32 repeats the previous length, so it can not be the first one,
        // and a run must not leave the table
        for (const std::vector<std::pair<int, int>>& runCodes : { std::vector<std::pair<int, int>>{ { 32, 0 } }, std::vector<std::pair<int, int>>{ { 2, 0 }, { 34, 63 } } })
        {
            TextRegionOptions malformedOptions = options;
            malformedOptions.runCodesOverride = runCodes;

            QByteArray malformed = dictionaryStream;
            appendSegment(malformed, 2, ImmediateTextRegion, { 1 }, encodeTextRegion(manyInstances, manySymbols, malformedOptions));
            QVERIFY(decodeExpectingError(malformed).contains("symbol length code table"));
        }
    }
}

void JBIG2SegmentsTest::test_huffman_table_selection_errors()
{
    // The table selections 2 of the delta height, the delta width, the first S and the
    // refinement deltas are reserved (7.4.2.1.1 and 7.4.3.1.2), a selected custom table
    // must be referred to, all referred tables of a dictionary must be used, and the
    // reserved bit 15 of the huffman flags of a text region is read as the selection 2
    // of the size table, which is reserved as well
    const std::vector<Image> symbols = getSymbols();

    CustomTable table;
    table.HTPS = 2;
    table.HTRS = 3;
    table.HTLOW = 0;
    table.lines = { { 1, 6 } };
    table.lowerPrefixLength = 2;
    table.upperPrefixLength = 2;

    ArithmeticContexts contexts;
    SymbolDictionaryOptions dictionaryOptions;
    dictionaryOptions.huffman = true;

    // The dictionary segment data are used with the modified flags, the encoder of
    // the tests refuses to code them itself
    const QByteArray dictionary = encodeSymbolDictionary(symbols, dictionaryOptions, contexts);
    auto withFlags = [&dictionary](uint16_t flags)
    {
        QByteArray data = dictionary;
        data[0] = char(flags >> 8);
        data[1] = char(flags & 0xFF);
        return data;
    };

    for (const uint16_t flags : { uint16_t(0x0001 | (2 << 2)), uint16_t(0x0001 | (2 << 4)) })
    {
        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, withFlags(flags));
        QVERIFY(decodeExpectingError(stream).contains("invalid user huffman code table"));
    }

    // A custom table is selected, but no table is referred to
    for (const uint16_t flags : { uint16_t(0x0001 | (3 << 2)), uint16_t(0x0001 | (3 << 4)), uint16_t(0x0001 | (1 << 6)), uint16_t(0x1003 | (1 << 7)) })
    {
        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, withFlags(flags));
        QVERIFY(decodeExpectingError(stream).contains("invalid user huffman code table"));
    }

    // A referred table is not used
    {
        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, Tables, { }, table.segmentData());
        appendSegment(stream, 2, SymbolDictionary, { 1 }, dictionary);
        QVERIFY(decodeExpectingError(stream).contains("unused"));
    }

    // Text region huffman flags
    ArithmeticContexts arithmeticContexts;
    arithmeticContexts.reset(0, 0, 0);
    const QByteArray arithmeticDictionary = encodeSymbolDictionary(symbols, SymbolDictionaryOptions(), arithmeticContexts);
    const std::vector<TextInstance> instances = { { 0, 0, 0 }, { 3, 0, 1 } };

    struct Case
    {
        uint8_t FS;
        uint8_t DS;
        uint8_t DT;
        uint8_t RDW;
        uint8_t RDH;
        uint8_t RDX;
        uint8_t RDY;
        uint8_t RSIZE;
        uint16_t reserved;
        const char* message;
    };

    const std::vector<Case> cases =
    {
        { 2, 0, 0, 0, 0, 0, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 2, 0, 0, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 0, 2, 0, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 0, 0, 2, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 0, 0, 0, 2, 0, 0, "invalid user huffman code table" },
        { 3, 0, 0, 0, 0, 0, 0, 0, 0, "invalid user huffman code table" },
        { 0, 3, 0, 0, 0, 0, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 3, 0, 0, 0, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 3, 0, 0, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 0, 3, 0, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 0, 0, 3, 0, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 0, 0, 0, 3, 0, 0, "invalid user huffman code table" },
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, "invalid user huffman code table" },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0x8000, "invalid user huffman code table" }
    };

    for (const Case& testCase : cases)
    {
        TextRegionOptions options;
        options.width = 18;
        options.height = 6;
        options.huffman = true;

        // The region is coded with the standard tables and the flags are replaced,
        // because the encoder of the tests can not code a reserved selection
        QByteArray region = encodeTextRegion(instances, symbols, options);
        const uint16_t huffmanFlags = uint16_t(testCase.FS | (testCase.DS << 2) | (testCase.DT << 4) | (testCase.RDW << 6) | (testCase.RDH << 8) | (testCase.RDX << 10) | (testCase.RDY << 12) | (testCase.RSIZE << 14) | testCase.reserved);
        region[19] = char(huffmanFlags >> 8);
        region[20] = char(huffmanFlags & 0xFF);

        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, arithmeticDictionary);
        appendSegment(stream, 2, ImmediateTextRegion, { 1 }, region);
        QVERIFY2(decodeExpectingError(stream).contains(testCase.message), qPrintable(QString::number(huffmanFlags, 16)));
    }
}

void JBIG2SegmentsTest::test_export_flags_errors()
{
    // The runs of the export flags (6.5.10) must not exceed the number of the symbols.
    // The run lengths 100, 400 and 4436 are coded by the three longest forms of the
    // integer arithmetic coding (A.2).
    const std::vector<Image> symbols = getSymbols();

    for (const bool huffman : { false, true })
    {
        for (const int32_t runLength : { int32_t(5), int32_t(100), int32_t(400), int32_t(4436), int32_t(70000) })
        {
            SymbolDictionaryOptions options;
            options.huffman = huffman;
            options.exportRunLengthsOverride = std::vector<int32_t>{ 0, runLength };

            ArithmeticContexts contexts;
            contexts.reset(0, 0, 0);

            QByteArray stream = createPageStream(18, 6);
            appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(symbols, options, contexts));
            QVERIFY2(decodeExpectingError(stream).contains("invalid export flags"), qPrintable(QString("huffman %1, run %2").arg(huffman).arg(runLength)));
        }
    }
}

void JBIG2SegmentsTest::test_text_region_errors()
{
    // A text region must refer to symbols, it must not contain more instances than its
    // header declares, a symbol ID must exist, and a refined instance must have a
    // positive size not exceeding the limit of the decoder
    const std::vector<Image> symbols = getSymbols();

    ArithmeticContexts contexts;
    contexts.reset(0, 0, 0);
    const QByteArray dictionary = encodeSymbolDictionary(symbols, SymbolDictionaryOptions(), contexts);

    TextRegionOptions options;
    options.width = 18;
    options.height = 6;

    {
        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, ImmediateTextRegion, { }, encodeTextRegion({ { 0, 0, 0 } }, symbols, options));
        QVERIFY(decodeExpectingError(stream).contains("no referred symbols"));
    }

    {
        TextRegionOptions fewer = options;
        fewer.instanceCountOverride = 1;

        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, dictionary);
        appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion({ { 0, 0, 0 }, { 3, 0, 1 } }, symbols, fewer));
        QVERIFY(decodeExpectingError(stream).contains("more symbol instances"));
    }

    {
        // Three symbols are coded by two bits, so the ID 3 can be coded
        const std::vector<Image> threeSymbols = { symbols[1], symbols[2], symbols[3] };
        ArithmeticContexts threeContexts;
        threeContexts.reset(0, 0, 0);

        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, encodeSymbolDictionary(threeSymbols, SymbolDictionaryOptions(), threeContexts));
        appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion({ { 0, 0, 3 } }, { symbols[1], symbols[2], symbols[3], symbols[0] }, options));
        QVERIFY(decodeExpectingError(stream).contains("symbol index 3"));
    }

    // The refined size - the width and the height of the letters is 4 and 5
    struct Case
    {
        int32_t RDW;
        int32_t RDH;
    };

    const std::vector<Case> cases = { { -4, 0 }, { 0, -5 }, { 65533, 0 }, { 0, 65532 }, { -10, -10 } };
    for (const Case& testCase : cases)
    {
        TextRegionOptions refine = options;
        refine.refine = true;

        // The refined bitmap is not decoded, so its image does not matter
        const std::vector<TextInstance> instances = { { 0, 0, 1, true, symbols[1], testCase.RDW, testCase.RDH, 0, 0 } };

        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, dictionary);
        appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion(instances, symbols, refine));
        QVERIFY2(decodeExpectingError(stream).contains("refined symbol instance"), qPrintable(QString("RDW %1, RDH %2").arg(testCase.RDW).arg(testCase.RDH)));
    }

    // A valid arithmetic refinement of both templates, for comparison
    const Image grownA = { "..##.", ".#..#", ".####", "##..#", ".#..#" };
    for (const uint8_t SBRTEMPLATE : { uint8_t(0), uint8_t(1) })
    {
        TextRegionOptions refine = options;
        refine.refine = true;
        refine.SBRTEMPLATE = SBRTEMPLATE;

        const std::vector<TextInstance> instances = { { 0, 0, 1, true, grownA, 1, 0, 0, 0 }, { 6, 0, 2 } };

        QByteArray stream = createPageStream(18, 6);
        appendSegment(stream, 1, SymbolDictionary, { }, dictionary);
        appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion(instances, symbols, refine));

        Image expected = createImage(18, 6);
        compose(expected, grownA, 0, 0, pdf::PDFJBIG2BitOperation::Or);
        compose(expected, symbols[2], 6, 0, pdf::PDFJBIG2BitOperation::Or);
        QCOMPARE(decodePage(stream), expected);
    }
}

void JBIG2SegmentsTest::test_pattern_dictionary_and_halftone_region()
{
    // A halftone region (6.6) places the patterns of a pattern dictionary (6.7) on a
    // grid by the gray-scale values decoded from the bit planes. The region is tested
    // with every combination operator, both default pixel values, all templates, the
    // MMR coding, a grid partially outside of the region with and without the skipping
    // of the outside cells, and as an intermediate region refined afterwards.
    const std::vector<Image> patterns =
    {
        { "....", "....", "....", "...." },
        { "....", ".#..", "..#.", "...." },
        { "##..", "##..", "..##", "..##" },
        { "####", "####", "####", "####" }
    };

    const std::vector<std::vector<uint32_t>> values =
    {
        { 0, 1, 2 },
        { 3, 2, 1 },
        { 1, 3, 0 }
    };

    HalftoneOptions base;
    base.width = 12;
    base.height = 12;
    base.HRX = 4 << 8;
    base.HRY = 0;
    base.patternWidth = 4;
    base.patternHeight = 4;
    base.patternCount = uint32_t(patterns.size());

    // Combination operators and default pixels, arithmetic and MMR
    for (const uint8_t HCOMBOP : { uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4) })
    {
        for (const bool HDEFPIXEL : { false, true })
        {
            for (const bool MMR : { false, true })
            {
                HalftoneOptions options = base;
                options.HCOMBOP = HCOMBOP;
                options.HDEFPIXEL = HDEFPIXEL;
                options.HMMR = MMR;

                QByteArray stream = createPageStream(12, 12);
                appendSegment(stream, 1, PatternDictionary, { }, encodePatternDictionary(patterns, MMR, 0));
                appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, encodeHalftoneRegion(values, options));
                QVERIFY2(decodePage(stream) == composeHalftoneRegion(values, patterns, options), qPrintable(QString("HCOMBOP %1, HDEFPIXEL %2, MMR %3").arg(HCOMBOP).arg(HDEFPIXEL).arg(MMR)));
            }
        }
    }

    // Templates of the pattern dictionary and of the gray-scale image
    for (const uint8_t HDTEMPLATE : { uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3) })
    {
        for (const uint8_t HTEMPLATE : { uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3) })
        {
            HalftoneOptions options = base;
            options.HTEMPLATE = HTEMPLATE;

            QByteArray stream = createPageStream(12, 12);
            appendSegment(stream, 1, PatternDictionary, { }, encodePatternDictionary(patterns, false, HDTEMPLATE));
            appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, encodeHalftoneRegion(values, options));
            QVERIFY2(decodePage(stream) == composeHalftoneRegion(values, patterns, options), qPrintable(QString("HDTEMPLATE %1, HTEMPLATE %2").arg(HDTEMPLATE).arg(HTEMPLATE)));
        }
    }

    // A grid starting to the left of the region - the first column of the cells lies
    // outside - and a grid ending behind the right and the bottom edge of the region.
    // With the skipping enabled the outside cells are not decoded at all.
    for (const bool HENABLESKIP : { false, true })
    {
        for (const bool MMR : { false, true })
        {
            for (const bool isGridBehind : { false, true })
            {
                HalftoneOptions options = base;
                options.width = 8;
                options.HGX = isGridBehind ? 1024 : -1024;
                options.HGY = isGridBehind ? 1024 : -256;
                options.HENABLESKIP = HENABLESKIP;
                options.HMMR = MMR;

                QByteArray stream = createPageStream(8, 12);
                appendSegment(stream, 1, PatternDictionary, { }, encodePatternDictionary(patterns, false, 0));
                appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, encodeHalftoneRegion(values, options));
                QVERIFY2(decodePage(stream) == composeHalftoneRegion(values, patterns, options), qPrintable(QString("HENABLESKIP %1, MMR %2, behind %3").arg(HENABLESKIP).arg(MMR).arg(isGridBehind)));
            }
        }
    }

    // A rotated grid
    {
        HalftoneOptions options = base;
        options.HRX = 3 << 8;
        options.HRY = 2 << 8;
        options.HGX = 1 << 8;
        options.HGY = 5 << 8;

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, encodePatternDictionary(patterns, false, 0));
        appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, encodeHalftoneRegion(values, options));
        QCOMPARE(decodePage(stream), composeHalftoneRegion(values, patterns, options));
    }

    // An intermediate halftone region is stored and refined by a refinement region
    // referring to it - the refinement inverts the region
    {
        const Image halftone = composeHalftoneRegion(values, patterns, base);
        const Image inverted = invertImage(halftone);

        pdf::PDFJBIG2ArithmeticDecoderState state;
        state.reset(13);
        pdf::PDFJBIG2ArithmeticEncoder encoder;
        encodeRefinement(encoder, state, inverted, halftone, 0, 0, 0, NOMINAL_REFINEMENT_AT);

        QByteArray refinement;
        appendRegionInformation(refinement, 12, 12, 0, 0, 0);
        refinement.append(char(0x00));
        appendATPositions(refinement, NOMINAL_REFINEMENT_AT, 2);
        refinement.append(encoder.finish());

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, encodePatternDictionary(patterns, false, 0));
        appendSegment(stream, 2, IntermediateHalftoneRegion, { 1 }, encodeHalftoneRegion(values, base));
        appendSegment(stream, 3, ImmediateRefinementRegion, { 2 }, refinement);
        QCOMPARE(decodePage(stream), inverted);
    }
}

void JBIG2SegmentsTest::test_halftone_region_errors()
{
    // A halftone region must refer to exactly one pattern dictionary, its combination
    // operator must be valid, the gray-scale values must select existing patterns, the
    // MMR coded data must have a known length and they must contain all rows of the
    // bit planes, and the collective bitmap of a pattern dictionary is limited
    const std::vector<Image> patterns =
    {
        { "....", "....", "....", "...." },
        { "....", ".##.", ".##.", "...." },
        { "####", "####", "####", "####" }
    };

    HalftoneOptions base;
    base.width = 12;
    base.height = 12;
    base.HRX = 4 << 8;
    base.patternWidth = 4;
    base.patternHeight = 4;
    base.patternCount = uint32_t(patterns.size());

    const std::vector<std::vector<uint32_t>> values = { { 0, 1, 2 }, { 2, 1, 0 }, { 1, 1, 1 } };
    const QByteArray dictionary = encodePatternDictionary(patterns, false, 0);

    {
        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, ImmediateHalftoneRegion, { }, encodeHalftoneRegion(values, base));
        QVERIFY(decodeExpectingError(stream).contains("pattern dictionaries"));
    }

    for (const uint8_t HCOMBOP : { uint8_t(5), uint8_t(6), uint8_t(7) })
    {
        HalftoneOptions options = base;
        options.HCOMBOP = HCOMBOP;

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, dictionary);
        appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, encodeHalftoneRegion(values, options));
        QVERIFY(decodeExpectingError(stream).contains("invalid bit operation"));
    }

    {
        // Three patterns are coded by two bit planes, so the value 3 can be coded
        const std::vector<std::vector<uint32_t>> outOfBounds = { { 0, 1, 3 }, { 2, 1, 0 }, { 1, 1, 1 } };

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, dictionary);
        appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, encodeHalftoneRegion(outOfBounds, base));
        QVERIFY(decodeExpectingError(stream).contains("out of bounds"));
    }

    {
        HalftoneOptions options = base;
        options.HMMR = true;

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, dictionary);
        appendSegmentWithLength(stream, 2, ImmediateHalftoneRegion, { 1 }, encodeHalftoneRegion(values, options), 0xFFFFFFFF);
        QVERIFY(decodeExpectingError(stream).contains("unknown data length of the segment 2"));

        QByteArray unknownDictionary = createPageStream(12, 12);
        appendSegmentWithLength(unknownDictionary, 1, PatternDictionary, { }, encodePatternDictionary(patterns, true, 0), 0xFFFFFFFF);
        QVERIFY(decodeExpectingError(unknownDictionary).contains("unknown data length of the segment 1"));
    }

    {
        // The MMR data of a grid of one row are empty (the region consists of its header
        // of 38 bytes only), so the plane has no row
        HalftoneOptions options = base;
        options.HMMR = true;
        const QByteArray header = encodeHalftoneRegion({ { 0, 1, 2 } }, options).left(38);

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, dictionary);
        appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, header);
        QVERIFY(decodeExpectingError(stream).contains("bit plane"));
    }

    {
        // An empty grid coded by MMR - a row of no columns consumes no data, so the MMR
        // decoder must reject it instead of decoding the data forever. The grid size
        // follows the flags byte of the header, and the data are a byte of ones, which
        // is no fill and no end of block.
        HalftoneOptions options = base;
        options.HMMR = true;
        QByteArray region = encodeHalftoneRegion({ { 0, 1, 2 } }, options).left(38);
        for (int i = 18; i < 26; ++i)
        {
            region[i] = 0;
        }
        region.append(toChar(0xFF));

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, dictionary);
        appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, region);
        const QString message = decodeExpectingError(stream);
        QVERIFY2(message.contains("number of columns"), qPrintable(message));
    }

    // The gray-scale image of the grid is allocated before the planes are decoded, so
    // the grid is limited like a bitmap - by each dimension and by the number of cells
    for (const auto& [HGW, HGH, expectedMessage] : { std::make_tuple(65537u, 1u, "maximum bitmap size"), std::make_tuple(1u, 65537u, "maximum bitmap size"), std::make_tuple(65536u, 65536u, "pixel count") })
    {
        QByteArray region = encodeHalftoneRegion({ { 0, 1, 2 } }, base);
        region[18] = toChar(HGW >> 24);
        region[19] = toChar(HGW >> 16);
        region[20] = toChar(HGW >> 8);
        region[21] = toChar(HGW);
        region[22] = toChar(HGH >> 24);
        region[23] = toChar(HGH >> 16);
        region[24] = toChar(HGH >> 8);
        region[25] = toChar(HGH);

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, dictionary);
        appendSegment(stream, 2, ImmediateHalftoneRegion, { 1 }, region);
        const QString message = decodeExpectingError(stream);
        QVERIFY2(message.contains(expectedMessage), qPrintable(message));
    }

    {
        // The MMR data of a pattern dictionary are empty, so no row is decoded
        QByteArray emptyDictionary = encodePatternDictionary(patterns, true, 0).left(7);

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, emptyDictionary);
        QVERIFY(decodeExpectingError(stream).contains("collective bitmap"));
    }

    {
        // GRAYMAX of 0xFFFFFFFF patterns of the width 4
        QByteArray hugeDictionary = dictionary;
        hugeDictionary[3] = toChar(0xFF);
        hugeDictionary[4] = toChar(0xFF);
        hugeDictionary[5] = toChar(0xFF);
        hugeDictionary[6] = toChar(0xFF);

        QByteArray stream = createPageStream(12, 12);
        appendSegment(stream, 1, PatternDictionary, { }, hugeDictionary);
        QVERIFY(decodeExpectingError(stream).contains("maximum bitmap size"));
    }
}

void JBIG2SegmentsTest::test_file_organisations()
{
    // A JBIG2 file (annex D) has the sequential organisation, where the data of a segment
    // follow its header, or the random-access organisation, where all headers precede all
    // data. The embedded stream of PDF has no file header at all, and its end of page and
    // end of file segments are ignored with a warning.
    const Image image = { "#..#..", ".##...", "...##.", "#....#" };
    const Bitmap bitmap = createBitmap(image);

    pdf::PDFJBIG2Encoder encoder(bitmap.view(), pdf::PDFJBIG2EncoderParameters());
    const QByteArray embedded = encoder.encodeEmbeddedStream();
    const QByteArray sequential = encoder.encodeFile();

    // The embedded stream consists of two segments with headers of 11 bytes - the page
    // information segment of 19 data bytes and the region segment
    const QByteArray pageHeader = embedded.left(11);
    const QByteArray pageData = embedded.mid(11, 19);
    const QByteArray regionHeader = embedded.mid(30, 11);
    const QByteArray regionData = embedded.mid(41);

    QByteArray endOfPage;
    appendSegment(endOfPage, 2, EndOfPage, { }, QByteArray());
    QByteArray endOfFile;
    appendSegment(endOfFile, 3, EndOfFile, { }, QByteArray());

    const QByteArray fileHeader = QByteArray("\x97\x4A\x42\x32\x0D\x0A\x1A\x0A", 8);

    QByteArray randomAccess = fileHeader;
    randomAccess.append(char(0x00));
    appendUInt32(randomAccess, 1);
    randomAccess.append(pageHeader);
    randomAccess.append(regionHeader);
    randomAccess.append(endOfPage);
    randomAccess.append(endOfFile);
    randomAccess.append(pageData);
    randomAccess.append(regionData);

    QCOMPARE(decodeFile(sequential), image);
    QCOMPARE(decodeFile(randomAccess), image);

    // The file header - the identifier, the reserved flags, the unknown number of pages
    // and a number of pages other than one
    QByteArray badIdentifier = sequential;
    badIdentifier[0] = toChar(0x98);
    QVERIFY(decodeFileExpectingError(badIdentifier).contains("file header"));

    QByteArray reservedFlags = sequential;
    reservedFlags[8] = char(0x05);
    QVERIFY(decodeFileExpectingError(reservedFlags).contains("header flags"));

    QByteArray unknownPageCount = sequential;
    unknownPageCount[8] = char(0x03);
    QVERIFY(decodeFileExpectingError(unknownPageCount).contains("unknown number of pages"));

    QByteArray twoPages = sequential;
    twoPages[12] = char(0x02);
    QVERIFY(decodeFileExpectingError(twoPages).contains("number of pages (2)"));

    // The random-access organisation needs the lengths of all segments
    QByteArray unknownLength = randomAccess;
    for (int i = 0; i < 4; ++i)
    {
        unknownLength[13 + 11 + 7 + i] = toChar(0xFF);
    }
    QVERIFY(decodeFileExpectingError(unknownLength).contains("segment length is not defined"));

    // The end of page and the end of file segments in the embedded stream
    QByteArray embeddedWithEnds = embedded + endOfPage + endOfFile;
    QCOMPARE(decodePage(embeddedWithEnds, { "JBIG2 end-of-page segment detected and ignored.", "JBIG2 end-of-file segment detected and ignored." }), image);

    // Neither of them can carry data
    QByteArray endOfPageWithData = embedded;
    appendSegment(endOfPageWithData, 2, EndOfPage, { }, QByteArray(4, char(0)));
    QVERIFY(decodeExpectingError(endOfPageWithData).contains("end-of-page"));

    QByteArray endOfFileWithData = embedded;
    appendSegment(endOfFileWithData, 2, EndOfFile, { }, QByteArray(4, char(0)));
    QVERIFY(decodeExpectingError(endOfFileWithData).contains("end-of-file"));
}

void JBIG2SegmentsTest::test_segment_header_and_data_length_errors()
{
    // A segment handler must read exactly the declared data - the rest is skipped with
    // a warning, reading past the end is an error. The unknown data length is allowed
    // for the immediate generic region only, see 7.2.7.
    const Image image = { "#..#..", ".##...", "...##.", "#....#" };
    const Bitmap bitmap = createBitmap(image);

    pdf::PDFJBIG2Encoder encoder(bitmap.view(), pdf::PDFJBIG2EncoderParameters());
    const QByteArray embedded = encoder.encodeEmbeddedStream();
    const QByteArray region = embedded.mid(30);

    QByteArray longer;
    appendSegmentWithLength(longer, 0, PageInformation, { }, createPageInformationData(6, 4, 0x00) + QByteArray(1, char(0)), 20);
    longer.append(region);
    QCOMPARE(decodePage(longer, { "JBIG2 bad segment data - handler doesn't process all segment data - 1 bytes left." }), image);

    QByteArray shorter;
    appendSegmentWithLength(shorter, 0, PageInformation, { }, createPageInformationData(6, 4, 0x00), 18);
    shorter.append(region);
    QVERIFY(decodeExpectingError(shorter).contains("past segment end"));

    QByteArray extension = createPageStream(6, 4);
    appendSegmentWithLength(extension, 1, Extension, { }, QByteArray("\x20\x00\x00\x00", 4) + QByteArray("k\0v\0", 4), 0xFFFFFFFF);
    extension.append(region);
    QVERIFY(decodeExpectingError(extension).contains("unknown data length of the segment 1"));

    QByteArray endOfStripe = createPageStream(6, 4);
    QByteArray endOfStripeData;
    appendUInt32(endOfStripeData, 3);
    appendSegmentWithLength(endOfStripe, 1, EndOfStripe, { }, endOfStripeData, 0xFFFFFFFF);
    endOfStripe.append(region);
    QVERIFY(decodeExpectingError(endOfStripe).contains("unknown data length of the segment 1"));

    // An intermediate generic region and a refinement region can not have the unknown
    // length either
    QByteArray intermediate = createPageStream(6, 4);
    intermediate.append(region);
    intermediate[30 + 4] = char(36);
    for (int i = 0; i < 4; ++i)
    {
        intermediate[30 + 7 + i] = toChar(0xFF);
    }
    QVERIFY(decodeExpectingError(intermediate).contains("unknown data length of the segment 1"));

    QByteArray refinementData;
    appendRegionInformation(refinementData, 6, 4, 0, 0, 4);
    refinementData.append(char(0x00));
    appendATPositions(refinementData, NOMINAL_REFINEMENT_AT, 2);
    refinementData.append(QByteArray(4, toChar(0xFF)));

    QByteArray refinement = createPageStream(6, 4);
    appendSegmentWithLength(refinement, 1, ImmediateRefinementRegion, { }, refinementData, 0xFFFFFFFF);
    QVERIFY(decodeExpectingError(refinement).contains("unknown data length of the segment 1"));
}

void JBIG2SegmentsTest::test_segment_header_field_sizes()
{
    // The referred segment numbers are one, two or four bytes long by the number of the
    // referring segment (7.2.5), and the page association is one or four bytes long (7.2.6)
    const std::vector<Image> symbols = getSymbols();
    const std::vector<TextInstance> instances = { { 0, 0, 0 }, { 3, 0, 1 }, { 8, 0, 2 }, { 13, 0, 3 } };

    Image expected = createImage(18, 6);
    for (const TextInstance& instance : instances)
    {
        compose(expected, symbols[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
    }

    TextRegionOptions regionOptions;
    regionOptions.width = 18;
    regionOptions.height = 6;

    for (const uint32_t segmentNumber : { uint32_t(1), uint32_t(300), uint32_t(70000) })
    {
        for (const bool longPageAssociation : { false, true })
        {
            ArithmeticContexts contexts;
            contexts.reset(0, 0, 0);

            QByteArray stream = createPageStream(18, 6);
            appendSegment(stream, segmentNumber, SymbolDictionary, { }, encodeSymbolDictionary(symbols, SymbolDictionaryOptions(), contexts), longPageAssociation);
            appendSegment(stream, segmentNumber + 1, ImmediateTextRegion, { segmentNumber }, encodeTextRegion(instances, symbols, regionOptions), longPageAssociation);
            QVERIFY2(decodePage(stream) == expected, qPrintable(QString("segment %1, long page association %2").arg(segmentNumber).arg(longPageAssociation)));
        }
    }
}

void JBIG2SegmentsTest::test_arithmetic_decoder_termination()
{
    // The arithmetic decoder skips the marker 0xFF 0xAC terminating the data. Any byte
    // above 0x8F after 0xFF is a marker for the decoding procedure (E.3.4), so the data
    // are decoded correctly with another marker, but the decoder does not skip it - the
    // byte is left in the segment, which the reader of the segments reports.
    const std::vector<Image> symbols = getSymbols();
    const std::vector<TextInstance> instances = { { 0, 0, 0 }, { 3, 0, 1 }, { 8, 0, 2 }, { 13, 0, 3 } };

    Image expected = createImage(18, 6);
    for (const TextInstance& instance : instances)
    {
        compose(expected, symbols[instance.id], instance.x, instance.y, pdf::PDFJBIG2BitOperation::Or);
    }

    TextRegionOptions regionOptions;
    regionOptions.width = 18;
    regionOptions.height = 6;

    ArithmeticContexts contexts;
    contexts.reset(0, 0, 0);
    QByteArray dictionary = encodeSymbolDictionary(symbols, SymbolDictionaryOptions(), contexts);
    QCOMPARE(uint8_t(dictionary[dictionary.size() - 2]), uint8_t(0xFF));
    QCOMPARE(uint8_t(dictionary[dictionary.size() - 1]), uint8_t(0xAC));
    dictionary[dictionary.size() - 1] = toChar(0xD9);

    QByteArray stream = createPageStream(18, 6);
    appendSegment(stream, 1, SymbolDictionary, { }, dictionary);
    appendSegment(stream, 2, ImmediateTextRegion, { 1 }, encodeTextRegion(instances, symbols, regionOptions));
    QCOMPARE(decodePage(stream, { "JBIG2 bad segment data - handler doesn't process all segment data - 1 bytes left." }), expected);
}

void JBIG2SegmentsTest::test_refinement_region_typical_prediction()
{
    // With the typical prediction (6.3.5.6) the pixels of a typical row, whose
    // neighbourhood in the reference is uniform, are copied from the reference, and only
    // the other pixels are decoded. The reference has two isolated black pixels, so the
    // eight pixels around each of them see the black pixel at every position of the
    // neighbourhood. The target changes the pixels around the first one, which is
    // allowed in a typical row, and a pixel with a uniform neighbourhood in the first
    // row, which makes the row non-typical.
    const Image reference =
    {
        "..........",
        "..........",
        "..........",
        "....#.....",
        "..........",
        "..........",
        ".......#.."
    };

    const Image target =
    {
        "#.........",
        "..........",
        "...#......",
        "..........",
        ".....#....",
        "..........",
        ".......#.."
    };

    const Bitmap bitmap = createBitmap(reference);
    pdf::PDFJBIG2Encoder encoder(bitmap.view(), pdf::PDFJBIG2EncoderParameters());
    QByteArray intermediate = encoder.encodeEmbeddedStream().mid(30);
    intermediate[4] = char(36);

    for (const uint8_t GRTEMPLATE : { uint8_t(0), uint8_t(1) })
    {
        pdf::PDFJBIG2ArithmeticDecoderState state;
        state.reset((GRTEMPLATE == 0) ? 13 : 10);
        pdf::PDFJBIG2ArithmeticEncoder refinementEncoder;
        encodeRefinement(refinementEncoder, state, target, reference, 0, 0, GRTEMPLATE, NOMINAL_REFINEMENT_AT, true);

        QByteArray refinement;
        appendRegionInformation(refinement, 10, 7, 0, 0, 0);
        refinement.append(char(0x02 | GRTEMPLATE));
        if (GRTEMPLATE == 0)
        {
            appendATPositions(refinement, NOMINAL_REFINEMENT_AT, 2);
        }
        refinement.append(refinementEncoder.finish());

        QByteArray stream = createPageStream(10, 7);
        stream.append(intermediate);
        appendSegment(stream, 2, ImmediateRefinementRegion, { 1 }, refinement);
        QCOMPARE(decodePage(stream), target);
    }
}

void JBIG2SegmentsTest::test_huffman_table_segment_errors()
{
    // The prefix lengths of a table must form a prefix code, and a refinement region
    // can refer to a bitmap only
    CustomTable overflow;
    overflow.HTPS = 2;
    overflow.HTRS = 2;
    overflow.HTLOW = 0;
    overflow.lines = { { 1, 1 }, { 1, 1 } };
    overflow.lowerPrefixLength = 1;
    overflow.upperPrefixLength = 0;

    QByteArray stream = createPageStream(6, 4);
    appendSegment(stream, 1, Tables, { }, overflow.segmentData());
    QVERIFY(decodeExpectingError(stream).contains("overflow of prefix bit values"));

    CustomTable table;
    table.HTPS = 2;
    table.HTRS = 2;
    table.HTLOW = 0;
    table.lines = { { 1, 1 }, { 2, 1 } };
    table.lowerPrefixLength = 3;
    table.upperPrefixLength = 3;

    QByteArray refinement;
    appendRegionInformation(refinement, 6, 4, 0, 0, 0);
    refinement.append(char(0x00));
    appendATPositions(refinement, NOMINAL_REFINEMENT_AT, 2);

    QByteArray notBitmap = createPageStream(6, 4);
    appendSegment(notBitmap, 1, Tables, { }, table.segmentData());
    appendSegment(notBitmap, 2, ImmediateRefinementRegion, { 1 }, refinement);
    QVERIFY(decodeExpectingError(notBitmap).contains("is not a bitmap"));

    // A table, whose every prefix length is zero, has no code, so nothing can be decoded
    // by it - the huffman dictionary selects it for the height class delta height
    CustomTable empty;
    empty.HTPS = 2;
    empty.HTRS = 2;
    empty.HTLOW = 0;
    empty.lines = { { 0, 1 } };
    empty.lowerPrefixLength = 0;
    empty.upperPrefixLength = 0;

    SymbolDictionaryOptions options;
    options.huffman = true;
    ArithmeticContexts contexts;
    QByteArray dictionary = encodeSymbolDictionary(getSymbols(), options, contexts);
    dictionary[1] = char(0x01 | (3 << 2));

    QByteArray emptyTable = createPageStream(6, 4);
    appendSegment(emptyTable, 1, Tables, { }, empty.segmentData());
    appendSegment(emptyTable, 2, SymbolDictionary, { 1 }, dictionary);
    QVERIFY(decodeExpectingError(emptyTable).contains("can't read integer"));
}

QTEST_APPLESS_MAIN(JBIG2SegmentsTest)

#include "tst_jbig2segmentstest.moc"
