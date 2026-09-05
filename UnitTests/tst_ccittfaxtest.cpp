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

#include "pdfccittfaxdecoder.h"
#include "pdfccittfaxencoder.h"
#include "pdfexception.h"

#include <QtTest>

#include <random>

/// Tests of the CCITT fax encoder and decoder. The encoder is verified against the code
/// words of ITU-T T.4 and T.6 by hand coded streams, against the MMR coded region of the
/// example datastream of ITU-T T.88 and by the round trips through the decoder over
/// randomly generated images and over every parameter of the CCITTFaxDecode filter.
class CCITTFaxTest : public QObject
{
    Q_OBJECT

private slots:
    void test_changing_elements();
    void test_hand_coded_group4_rows();
    void test_hand_coded_group3_rows();
    void test_run_length_code_words();
    void test_annex_h_mmr_region_is_reencoded_identically();
    void test_roundtrip_data();
    void test_roundtrip();
    void test_end_of_line_is_accepted_when_not_required();
    void test_end_of_block_is_found_without_row_count();
    void test_fill_bits_are_skipped();
    void test_row_count_and_end_of_block_precedence();
    void test_black_is_one_swaps_decode_array();
    void test_decoder_rejects_malformed_streams();
    void test_encoder_rejects_invalid_bitmap();

private:
    /// Bitonal image with packed rows, a set bit is a black pixel
    struct Bitmap
    {
        int width = 0;
        int height = 0;
        int stride = 0;
        std::vector<uint8_t> data;

        Bitmap() = default;
        Bitmap(int width, int height) :
            width(width),
            height(height),
            stride((width + 7) / 8),
            data(size_t((width + 7) / 8) * size_t(height), 0)
        {

        }

        void setBlack(int x, int y) { data[size_t(y) * size_t(stride) + size_t(x >> 3)] |= uint8_t(0x80 >> (x & 7)); }
        bool isBlack(int x, int y) const { return (data[size_t(y) * size_t(stride) + size_t(x >> 3)] >> (7 - (x & 7))) & 0x01; }

        pdf::PDFBitonalBitmapView view() const
        {
            pdf::PDFBitonalBitmapView result;
            result.data = data.data();
            result.width = width;
            result.height = height;
            result.stride = stride;
            result.isOneBlack = true;
            return result;
        }

        bool operator==(const Bitmap& other) const
        {
            if (width != other.width || height != other.height)
            {
                return false;
            }

            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    if (isBlack(x, y) != other.isBlack(x, y))
                    {
                        return false;
                    }
                }
            }

            return true;
        }
    };

    /// Creates a bitmap from the rows drawn as text, where a hash is a black pixel
    static Bitmap createBitmap(const QStringList& rows);

    /// Returns the bitmap drawn as one line of text per row, so a failed comparison
    /// shows the images
    static QStringList drawBitmap(const Bitmap& bitmap);

    /// Creates a test image of the given kind. The kinds are the cases, which are
    /// interesting for the coders - noise of various densities, the extreme numbers of
    /// the changing elements, runs longer than the largest make-up code word and the
    /// structured content of the scanned documents.
    /// \param kind Kind of the image
    /// \param width Width of the image
    /// \param height Height of the image
    /// \param seed Seed of the random generator
    static Bitmap createTestBitmap(const QString& kind, int width, int height, unsigned seed);

    /// Returns the names of the kinds of the test images
    static QStringList getTestBitmapKinds();

    static QByteArray encode(const Bitmap& bitmap, const pdf::PDFCCITTFaxEncoderParameters& parameters);

    /// Decodes the data. The decoder writes the value 0 for a black pixel, so the
    /// samples are inverted into the convention of the test bitmap. Throws
    /// \p pdf::PDFException for a malformed stream.
    static Bitmap decode(const QByteArray& data, const pdf::PDFCCITTFaxDecoderParameters& parameters);

    /// Returns the decoder parameters matching the encoder parameters
    static pdf::PDFCCITTFaxDecoderParameters createDecoderParameters(const pdf::PDFCCITTFaxEncoderParameters& parameters, int columns, int rows);

    /// Returns the bits of the data as a string of ones and zeros
    static QString toBits(const QByteArray& data);

    /// Returns the string of ones and zeros padded by zeros to a whole number of bytes
    static QString padBits(QString bits);

    /// Returns the bytes of a string of ones and zeros, padded by zeros
    static QByteArray fromBits(const QString& bits);

    /// Decodes the data and returns the message of the exception it has thrown, or
    /// an empty string, if it has been decoded without an error
    static QString decodeExpectingError(const QByteArray& data, const pdf::PDFCCITTFaxDecoderParameters& parameters);
};

CCITTFaxTest::Bitmap CCITTFaxTest::createBitmap(const QStringList& rows)
{
    Bitmap bitmap(rows.front().size(), rows.size());

    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int x = 0; x < bitmap.width; ++x)
        {
            if (rows[y][x] == '#')
            {
                bitmap.setBlack(x, y);
            }
        }
    }

    return bitmap;
}

QStringList CCITTFaxTest::drawBitmap(const Bitmap& bitmap)
{
    QStringList rows;

    for (int y = 0; y < bitmap.height; ++y)
    {
        QString row;
        for (int x = 0; x < bitmap.width; ++x)
        {
            row += bitmap.isBlack(x, y) ? '#' : '.';
        }
        rows << row;
    }

    return rows;
}

QStringList CCITTFaxTest::getTestBitmapKinds()
{
    return { "white", "black", "noise50", "noise5", "noise95", "columns", "rows", "checker", "diagonal", "blocks", "longruns" };
}

CCITTFaxTest::Bitmap CCITTFaxTest::createTestBitmap(const QString& kind, int width, int height, unsigned seed)
{
    Bitmap bitmap(width, height);
    std::mt19937 generator(seed);

    auto fillNoise = [&](double blackProbability)
    {
        std::bernoulli_distribution distribution(blackProbability);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (distribution(generator))
                {
                    bitmap.setBlack(x, y);
                }
            }
        }
    };

    if (kind == "black")
    {
        std::fill(bitmap.data.begin(), bitmap.data.end(), 0xFF);
    }
    else if (kind == "noise50")
    {
        fillNoise(0.5);
    }
    else if (kind == "noise5")
    {
        fillNoise(0.05);
    }
    else if (kind == "noise95")
    {
        fillNoise(0.95);
    }
    else if (kind == "columns")
    {
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; x += 2)
            {
                bitmap.setBlack(x, y);
            }
        }
    }
    else if (kind == "rows")
    {
        for (int y = 0; y < height; y += 2)
        {
            for (int x = 0; x < width; ++x)
            {
                bitmap.setBlack(x, y);
            }
        }
    }
    else if (kind == "checker")
    {
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if ((x + y) % 2 == 0)
                {
                    bitmap.setBlack(x, y);
                }
            }
        }
    }
    else if (kind == "diagonal")
    {
        // Lines of both slopes, so the vertical modes of both directions are used
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if ((x - y) % 7 == 0 || (x + 2 * y) % 11 == 0)
                {
                    bitmap.setBlack(x, y);
                }
            }
        }
    }
    else if (kind == "blocks")
    {
        std::uniform_int_distribution<int> xDistribution(0, width - 1);
        std::uniform_int_distribution<int> yDistribution(0, height - 1);
        std::uniform_int_distribution<int> sizeDistribution(1, 12);

        for (int i = 0; i < 20; ++i)
        {
            const int x0 = xDistribution(generator);
            const int y0 = yDistribution(generator);
            const int x1 = std::min(width, x0 + sizeDistribution(generator));
            const int y1 = std::min(height, y0 + sizeDistribution(generator));

            for (int y = y0; y < y1; ++y)
            {
                for (int x = x0; x < x1; ++x)
                {
                    bitmap.setBlack(x, y);
                }
            }
        }
    }
    else if (kind == "longruns")
    {
        // Runs of both colours longer than 2560 pixels, which is the longest run
        // with its own make-up code word
        for (int y = 0; y < height; ++y)
        {
            const int start = std::min(width, 3000 + y);
            const int end = std::min(width, start + 2700);

            for (int x = start; x < end; ++x)
            {
                bitmap.setBlack(x, y);
            }
        }
    }

    return bitmap;
}

QByteArray CCITTFaxTest::encode(const Bitmap& bitmap, const pdf::PDFCCITTFaxEncoderParameters& parameters)
{
    pdf::PDFCCITTFaxEncoder encoder(bitmap.view(), parameters);
    return encoder.encode();
}

CCITTFaxTest::Bitmap CCITTFaxTest::decode(const QByteArray& data, const pdf::PDFCCITTFaxDecoderParameters& parameters)
{
    pdf::PDFCCITTFaxDecoder decoder(&data, parameters);
    const pdf::PDFImageData imageData = decoder.decode();

    Bitmap bitmap(int(imageData.getWidth()), int(imageData.getHeight()));
    const QByteArray& samples = imageData.getData();
    const int stride = int(imageData.getStride());

    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int i = 0; i < bitmap.stride; ++i)
        {
            bitmap.data[size_t(y) * size_t(bitmap.stride) + size_t(i)] = uint8_t(~samples[y * stride + i]);
        }
    }

    return bitmap;
}

pdf::PDFCCITTFaxDecoderParameters CCITTFaxTest::createDecoderParameters(const pdf::PDFCCITTFaxEncoderParameters& parameters, int columns, int rows)
{
    pdf::PDFCCITTFaxDecoderParameters result;
    result.K = parameters.K;
    result.columns = columns;
    result.rows = rows;
    result.hasEndOfLine = parameters.K >= 0 && parameters.hasEndOfLine;
    result.hasEncodedByteAlign = parameters.hasEncodedByteAlign;
    result.hasEndOfBlock = parameters.hasEndOfBlock;
    result.decode = { 0.0, 1.0 };
    return result;
}

QString CCITTFaxTest::toBits(const QByteArray& data)
{
    QString bits;
    bits.reserve(data.size() * 8);

    for (const char byte : data)
    {
        for (int i = 7; i >= 0; --i)
        {
            bits += ((uint8_t(byte) >> i) & 0x01) ? '1' : '0';
        }
    }

    return bits;
}

QString CCITTFaxTest::padBits(QString bits)
{
    while (bits.size() % 8 != 0)
    {
        bits += '0';
    }

    return bits;
}

QByteArray CCITTFaxTest::fromBits(const QString& bits)
{
    const QString padded = padBits(bits);
    QByteArray data;

    for (int i = 0; i < padded.size(); i += 8)
    {
        uint8_t byte = 0;
        for (int j = 0; j < 8; ++j)
        {
            byte = uint8_t((byte << 1) | (padded[i + j] == '1' ? 1 : 0));
        }
        data.append(char(byte));
    }

    return data;
}

QString CCITTFaxTest::decodeExpectingError(const QByteArray& data, const pdf::PDFCCITTFaxDecoderParameters& parameters)
{
    try
    {
        decode(data, parameters);
    }
    catch (const pdf::PDFException& exception)
    {
        return exception.getMessage();
    }

    return QString();
}

void CCITTFaxTest::test_changing_elements()
{
    std::vector<int> changingElements;

    // The imaginary pixel before the row is white, so a row starting with a black
    // pixel has its first changing element at zero. The list is terminated by the
    // width three times.
    const Bitmap bitmap = createBitmap({ "##...#..##", ".........." , "##########" });
    bitmap.view().getChangingElements(0, changingElements);
    QCOMPARE(changingElements, std::vector<int>({ 0, 2, 5, 6, 8, 10, 10, 10 }));
    bitmap.view().getChangingElements(1, changingElements);
    QCOMPARE(changingElements, std::vector<int>({ 10, 10, 10 }));
    bitmap.view().getChangingElements(2, changingElements);
    QCOMPARE(changingElements, std::vector<int>({ 0, 10, 10, 10 }));

    // The whole bytes are skipped at once, so the changing elements inside a byte
    // following a skipped byte must be found
    const Bitmap wide = createBitmap({ "........########........#.......", "#########.......#######..######." });
    wide.view().getChangingElements(0, changingElements);
    QCOMPARE(changingElements, std::vector<int>({ 8, 16, 24, 25, 32, 32, 32 }));
    wide.view().getChangingElements(1, changingElements);
    QCOMPARE(changingElements, std::vector<int>({ 0, 9, 16, 23, 25, 31, 32, 32, 32 }));

    // The value of a black pixel is a property of the view
    pdf::PDFBitonalBitmapView inverted = wide.view();
    inverted.isOneBlack = false;
    inverted.getChangingElements(0, changingElements);
    QCOMPARE(changingElements, std::vector<int>({ 0, 8, 16, 24, 25, 32, 32, 32 }));
    QVERIFY(!inverted.isPixelBlack(8, 0));
    QVERIFY(inverted.isPixelBlack(0, 0));

    // Validity of the view
    pdf::PDFBitonalBitmapView invalid;
    QVERIFY(!invalid.isValid());
    invalid = wide.view();
    invalid.stride = 3;
    QVERIFY(!invalid.isValid());
    invalid = wide.view();
    invalid.height = 0;
    QVERIFY(!invalid.isValid());
    QVERIFY(wide.view().isValid());
}

void CCITTFaxTest::test_hand_coded_group4_rows()
{
    // The code words of the tables 4 (modes) and 2/3 (run lengths) of ITU-T T.4, the
    // streams are derived by hand from the coding algorithm of 4.2.1.3.
    pdf::PDFCCITTFaxEncoderParameters parameters;
    parameters.K = -1;
    parameters.hasEndOfBlock = false;

    // A white row has no changing elements - a1 is the imaginary element at the end
    // of the row and b1 as well, so a single V0 codes the row
    QCOMPARE(toBits(encode(createBitmap({ "........" }), parameters)), padBits("1"));

    // The end of block of T.6 is two end-of-line code words
    parameters.hasEndOfBlock = true;
    QCOMPARE(toBits(encode(createBitmap({ "........" }), parameters)), padBits("1" "000000000001" "000000000001"));
    parameters.hasEndOfBlock = false;

    // A black row - a1 = 0 is far from b1 = 8, so the horizontal mode codes the empty
    // white run and the black run of 8 pixels. The run ends at the end of the row,
    // so the row is complete without coding the imaginary changing element.
    QCOMPARE(toBits(encode(createBitmap({ "########" }), parameters)), padBits("001" "00110101" "000101"));

    // Two black pixels at the start of the row - the horizontal mode with the runs of
    // 0 white and 2 black pixels, followed by V0 of the imaginary element
    QCOMPARE(toBits(encode(createBitmap({ "##......" }), parameters)), padBits("001" "00110101" "11" "1"));

    // The second row is identical to the first one, so it is coded by V0 only
    QCOMPARE(toBits(encode(createBitmap({ "...##...", "...##..." }), parameters)), padBits("001" "1000" "11" "1" "111"));

    // Pass mode - the black run of the reference row lies completely left of a1, then
    // VL2 codes a1 = 6 against b1 = 8 and V0 the end of the row
    QCOMPARE(toBits(encode(createBitmap({ "..##....", "......##" }), parameters)), padBits("001" "0111" "11" "1" "0001" "000010" "1"));

    // The black run moves by one pixel to the right in each row (VR1 and VR1) and then
    // by three pixels to the left (VL3 and VL3). The run of the reference row then lies
    // completely left of the imaginary element, so the pass mode precedes the final V0.
    QCOMPARE(toBits(encode(createBitmap({ "##......", ".##.....", "..##....", "...##...", "##......" }), parameters)),
             padBits("001" "00110101" "11" "1" "011" "011" "1" "011" "011" "1" "011" "011" "1" "0000010" "0000010" "0001" "1"));

    // VR2, VR3, VL2 and VL1 - the run grows and shrinks at its end
    QCOMPARE(toBits(encode(createBitmap({ "#.......", "###.....", "######..", "####....", "###....." }), parameters)),
             padBits("001" "00110101" "010" "1" "1" "000011" "1" "1" "0000011" "1" "1" "000010" "1" "1" "010" "1"));

    // Byte aligned rows start at a byte boundary and the end of block is aligned as well
    parameters.hasEncodedByteAlign = true;
    parameters.hasEndOfBlock = true;
    QCOMPARE(encode(createBitmap({ "........", "........" }), parameters), QByteArray("\x80\x80\x00\x10\x01", 5));

    // All streams above are decoded back to the images
    for (const QStringList& image : { QStringList{ "........" }, QStringList{ "########" }, QStringList{ "##......" }, QStringList{ "...##...", "...##..." },
                                      QStringList{ "..##....", "......##" }, QStringList{ "##......", ".##.....", "..##....", "...##...", "##......" },
                                      QStringList{ "#.......", "###.....", "######..", "####....", "###....." } })
    {
        for (const bool hasEncodedByteAlign : { false, true })
        {
            for (const bool hasEndOfBlock : { false, true })
            {
                parameters.hasEncodedByteAlign = hasEncodedByteAlign;
                parameters.hasEndOfBlock = hasEndOfBlock;

                const Bitmap bitmap = createBitmap(image);
                const QByteArray data = encode(bitmap, parameters);
                QCOMPARE(drawBitmap(decode(data, createDecoderParameters(parameters, bitmap.width, bitmap.height))), image);
            }
        }
    }
}

void CCITTFaxTest::test_hand_coded_group3_rows()
{
    pdf::PDFCCITTFaxEncoderParameters parameters;
    parameters.K = 0;
    parameters.hasEndOfBlock = false;

    // One dimensional coding - the runs alternate starting with a white run, so a row
    // starting with a black pixel has an empty white run first
    QCOMPARE(toBits(encode(createBitmap({ "........" }), parameters)), padBits("10011"));
    QCOMPARE(toBits(encode(createBitmap({ "###....." }), parameters)), padBits("00110101" "10" "1100"));
    QCOMPARE(toBits(encode(createBitmap({ "..#.#..." }), parameters)), padBits("0111" "010" "000111" "010" "1000"));

    // A run of 64 pixels is a make-up code word followed by the terminating code word
    // of the zero length, a run of 2560 + 64 + 1 pixels needs three code words
    QCOMPARE(toBits(encode(createTestBitmap("white", 64, 1, 0), parameters)), padBits("11011" "00110101"));
    QCOMPARE(toBits(encode(createTestBitmap("black", 64, 1, 0), parameters)), padBits("00110101" "0000001111" "0000110111"));
    QCOMPARE(toBits(encode(createTestBitmap("white", 2625, 1, 0), parameters)), padBits("000000011111" "11011" "000111"));

    // End-of-line code words precede the rows and the return-to-control code is six
    // end-of-line code words
    parameters.hasEndOfLine = true;
    parameters.hasEndOfBlock = true;
    QCOMPARE(toBits(encode(createBitmap({ "........", "###....." }), parameters)),
             padBits("000000000001" "10011" "000000000001" "00110101" "10" "1100"
                     "000000000001" "000000000001" "000000000001" "000000000001" "000000000001" "000000000001"));

    // Mixed coding - a tag bit selects the coding of the row, the second row is coded
    // two dimensionally against the first one and the return-to-control code carries
    // the tag bits as well
    parameters.K = 2;
    parameters.hasEndOfLine = false;
    parameters.hasEndOfBlock = false;
    QCOMPARE(encode(createBitmap({ "........", "........" }), parameters), QByteArray("\xCD", 1));
    parameters.hasEndOfBlock = true;
    QCOMPARE(toBits(encode(createBitmap({ "........", "........" }), parameters)),
             padBits("1" "10011" "0" "1" "0000000000011" "0000000000011" "0000000000011" "0000000000011" "0000000000011" "0000000000011"));

    // With end-of-line code words the tag bit follows the code word, and a byte aligned
    // stream starts the code word at the byte boundary
    parameters.hasEndOfLine = true;
    parameters.hasEncodedByteAlign = true;
    parameters.hasEndOfBlock = false;
    QCOMPARE(toBits(encode(createBitmap({ "........", "........", "........" }), parameters)),
             padBits("000000000001" "1" "10011" "000000" "000000000001" "0" "1" "00" "000000000001" "1" "10011"));

    // All streams above are decoded back to the images
    for (const int K : { 0, 2 })
    {
        for (const bool hasEndOfLine : { false, true })
        {
            for (const bool hasEncodedByteAlign : { false, true })
            {
                for (const bool hasEndOfBlock : { false, true })
                {
                    parameters.K = K;
                    parameters.hasEndOfLine = hasEndOfLine;
                    parameters.hasEncodedByteAlign = hasEncodedByteAlign;
                    parameters.hasEndOfBlock = hasEndOfBlock;

                    const QStringList image = { "........", "###.....", "..#.#...", "........" };
                    const Bitmap bitmap = createBitmap(image);
                    const QByteArray data = encode(bitmap, parameters);
                    QCOMPARE(drawBitmap(decode(data, createDecoderParameters(parameters, bitmap.width, bitmap.height))), image);
                }
            }
        }
    }
}

void CCITTFaxTest::test_run_length_code_words()
{
    // Every run length up to 2600 of both colours is coded one dimensionally and
    // decoded back, so every terminating and make-up code word of the tables is used
    // by the encoder and it is decoded by the decoder to the same length. A white run
    // is followed by a single black pixel, a black run by a single white pixel.
    pdf::PDFCCITTFaxEncoderParameters parameters;
    parameters.K = 0;
    parameters.hasEndOfBlock = false;

    std::vector<int> lengths;
    for (int length = 0; length <= 2600; ++length)
    {
        lengths.push_back(length);
    }
    for (const int length : { 5120, 5121, 5184, 7681, 7745 })
    {
        lengths.push_back(length);
    }

    for (const int length : lengths)
    {
        Bitmap white(length + 1, 1);
        white.setBlack(length, 0);

        const QByteArray whiteData = encode(white, parameters);
        const Bitmap decodedWhite = decode(whiteData, createDecoderParameters(parameters, white.width, white.height));
        QVERIFY2(decodedWhite == white, qPrintable(QString("White run of %1 pixels.").arg(length)));

        if (length == 0)
        {
            continue;
        }

        Bitmap black(length + 1, 1);
        for (int x = 0; x < length; ++x)
        {
            black.setBlack(x, 0);
        }

        const QByteArray blackData = encode(black, parameters);
        const Bitmap decodedBlack = decode(blackData, createDecoderParameters(parameters, black.width, black.height));
        QVERIFY2(decodedBlack == black, qPrintable(QString("Black run of %1 pixels.").arg(length)));
    }
}

void CCITTFaxTest::test_annex_h_mmr_region_is_reencoded_identically()
{
    // The MMR coded data of the segment 4 of the example datastream of ITU-T T.88,
    // annex H.2 - a region of 54 by 44 pixels. The coding of T.6 is deterministic, so
    // the data decoded from the example and encoded again must give the same bytes.
    static const unsigned char DATA[] =
    {
        0x26, 0xA0, 0x71, 0xCE, 0xA7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0xF0
    };

    const QByteArray data(reinterpret_cast<const char*>(DATA), int(sizeof(DATA)));

    pdf::PDFCCITTFaxDecoderParameters decoderParameters;
    decoderParameters.K = -1;
    decoderParameters.columns = 54;
    decoderParameters.rows = 44;
    decoderParameters.hasEndOfBlock = false;
    decoderParameters.decode = { 0.0, 1.0 };

    const Bitmap bitmap = decode(data, decoderParameters);
    QCOMPARE(bitmap.width, 54);
    QCOMPARE(bitmap.height, 44);

    // The region shows the text of the figure H.6 - the frame of the region is black
    // and the letters inside are white on the black background of the first rows
    int blackPixels = 0;
    for (int y = 0; y < bitmap.height; ++y)
    {
        for (int x = 0; x < bitmap.width; ++x)
        {
            blackPixels += bitmap.isBlack(x, y) ? 1 : 0;
        }
    }
    QVERIFY(blackPixels > 0);
    QVERIFY(blackPixels < bitmap.width * bitmap.height);

    pdf::PDFCCITTFaxEncoderParameters encoderParameters;
    encoderParameters.K = -1;
    encoderParameters.hasEndOfBlock = false;

    QCOMPARE(encode(bitmap, encoderParameters).toHex(), data.toHex());
}

void CCITTFaxTest::test_roundtrip_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<int>("K");
    QTest::addColumn<bool>("hasEndOfLine");
    QTest::addColumn<bool>("hasEncodedByteAlign");
    QTest::addColumn<bool>("hasEndOfBlock");

    // The widths cross the byte boundaries in every way, and the long runs need
    // a width above the longest make-up code word
    const std::vector<std::pair<int, int>> sizes = { { 1, 1 }, { 7, 3 }, { 8, 8 }, { 9, 5 }, { 63, 2 }, { 64, 1 }, { 65, 17 }, { 200, 50 }, { 1728, 4 } };
    unsigned seed = 1;

    for (const QString& kind : getTestBitmapKinds())
    {
        for (const int K : { -1, 0, 4 })
        {
            for (const bool hasEndOfLine : { false, true })
            {
                if (K < 0 && hasEndOfLine)
                {
                    // Group 4 has no end-of-line code words
                    continue;
                }

                for (const bool hasEncodedByteAlign : { false, true })
                {
                    for (const bool hasEndOfBlock : { false, true })
                    {
                        const std::pair<int, int> size = (kind == "longruns") ? std::make_pair(6000, 3) : sizes[seed % sizes.size()];
                        const QString name = QString("%1 %2x%3 K=%4%5%6%7").arg(kind).arg(size.first).arg(size.second).arg(K)
                                                 .arg(hasEndOfLine ? " EOL" : "").arg(hasEncodedByteAlign ? " aligned" : "").arg(hasEndOfBlock ? " EOFB" : "");
                        QTest::newRow(qPrintable(name)) << kind << size.first << size.second << K << hasEndOfLine << hasEncodedByteAlign << hasEndOfBlock;
                        ++seed;
                    }
                }
            }
        }
    }
}

void CCITTFaxTest::test_roundtrip()
{
    QFETCH(QString, kind);
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(int, K);
    QFETCH(bool, hasEndOfLine);
    QFETCH(bool, hasEncodedByteAlign);
    QFETCH(bool, hasEndOfBlock);

    const Bitmap bitmap = createTestBitmap(kind, width, height, unsigned(width * 31 + height * 7 + K));

    pdf::PDFCCITTFaxEncoderParameters parameters;
    parameters.K = K;
    parameters.hasEndOfLine = hasEndOfLine;
    parameters.hasEncodedByteAlign = hasEncodedByteAlign;
    parameters.hasEndOfBlock = hasEndOfBlock;

    QByteArray data;
    Bitmap decoded;

    try
    {
        data = encode(bitmap, parameters);
        decoded = decode(data, createDecoderParameters(parameters, width, height));
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    QVERIFY(!data.isEmpty());
    QCOMPARE(decoded.width, width);
    QCOMPARE(decoded.height, height);

    if (!(decoded == bitmap) && width <= 80)
    {
        QCOMPARE(drawBitmap(decoded), drawBitmap(bitmap));
    }
    QVERIFY(decoded == bitmap);
}

void CCITTFaxTest::test_end_of_line_is_accepted_when_not_required()
{
    // The filter accepts the end-of-line code words even when it does not require
    // them (see the description of the EndOfLine parameter in the PDF specification),
    // so a stream with the code words is decoded with the parameter set to false, in
    // the byte aligned form as well - the code word starts at the byte boundary.
    const Bitmap bitmap = createTestBitmap("blocks", 100, 20, 7);

    for (const int K : { 0, 3 })
    {
        for (const bool hasEncodedByteAlign : { false, true })
        {
            for (const bool hasEndOfBlock : { false, true })
            {
                pdf::PDFCCITTFaxEncoderParameters parameters;
                parameters.K = K;
                parameters.hasEndOfLine = true;
                parameters.hasEncodedByteAlign = hasEncodedByteAlign;
                parameters.hasEndOfBlock = hasEndOfBlock;

                const QByteArray data = encode(bitmap, parameters);

                pdf::PDFCCITTFaxDecoderParameters decoderParameters = createDecoderParameters(parameters, bitmap.width, bitmap.height);
                decoderParameters.hasEndOfLine = false;

                const Bitmap decoded = decode(data, decoderParameters);
                QVERIFY2(decoded == bitmap, qPrintable(QString("K = %1, aligned = %2, end of block = %3").arg(K).arg(hasEncodedByteAlign).arg(hasEndOfBlock)));
            }
        }
    }
}

void CCITTFaxTest::test_end_of_block_is_found_without_row_count()
{
    // The number of the rows is optional in the filter parameters - the decoder stops
    // at the end of block. The end of block of a byte aligned mixed encoding follows
    // the alignment and the tag bits of its end-of-line code words must not be read
    // as the tag of a next row.
    const Bitmap bitmap = createTestBitmap("diagonal", 40, 9, 3);

    for (const int K : { -1, 0, 2 })
    {
        for (const bool hasEndOfLine : { false, true })
        {
            for (const bool hasEncodedByteAlign : { false, true })
            {
                pdf::PDFCCITTFaxEncoderParameters parameters;
                parameters.K = K;
                parameters.hasEndOfLine = hasEndOfLine;
                parameters.hasEncodedByteAlign = hasEncodedByteAlign;
                parameters.hasEndOfBlock = true;

                const QByteArray data = encode(bitmap, parameters);

                pdf::PDFCCITTFaxDecoderParameters decoderParameters = createDecoderParameters(parameters, bitmap.width, 0);
                const Bitmap decoded = decode(data, decoderParameters);
                QVERIFY2(decoded == bitmap, qPrintable(QString("K = %1, end of line = %2, aligned = %3").arg(K).arg(hasEndOfLine).arg(hasEncodedByteAlign)));
            }
        }
    }
}

void CCITTFaxTest::test_fill_bits_are_skipped()
{
    // Fill bits (zeros) may precede an end-of-line code word, see 4.1.2 of ITU-T T.4,
    // and a decoder, which does not require the code words, must skip the fill as well
    // as the code word. The fill is longer than any code word, so it can not be read
    // as a run length.
    const QString fill = "00000000000000000000";
    const QString eol = "000000000001";
    const QString whiteRow = "10011";
    const QString blackRow = "00110101" "000101";

    pdf::PDFCCITTFaxEncoderParameters encoderParameters;
    encoderParameters.K = 0;

    for (const bool hasEndOfLine : { false, true })
    {
        for (const bool hasEndOfBlock : { false, true })
        {
            QString bits = fill + eol + whiteRow + fill + eol + blackRow;
            if (hasEndOfBlock)
            {
                bits += fill;
                for (int i = 0; i < 6; ++i)
                {
                    bits += eol;
                }
            }

            pdf::PDFCCITTFaxDecoderParameters parameters = createDecoderParameters(encoderParameters, 8, 2);
            parameters.hasEndOfLine = hasEndOfLine;
            parameters.hasEndOfBlock = hasEndOfBlock;

            const Bitmap decoded = decode(fromBits(bits), parameters);
            QCOMPARE(drawBitmap(decoded), QStringList({ "........", "########" }));
        }
    }
}

void CCITTFaxTest::test_row_count_and_end_of_block_precedence()
{
    // The number of the rows is ignored, when the end of block is expected - the data
    // are decoded up to the end of block, or up to the end of the data
    const Bitmap bitmap = createTestBitmap("blocks", 64, 6, 21);

    for (const int K : { -1, 0, 3 })
    {
        pdf::PDFCCITTFaxEncoderParameters encoderParameters;
        encoderParameters.K = K;
        encoderParameters.hasEndOfBlock = true;

        const QByteArray withEndOfBlock = encode(bitmap, encoderParameters);

        pdf::PDFCCITTFaxDecoderParameters parameters = createDecoderParameters(encoderParameters, bitmap.width, 2);
        parameters.hasEndOfBlock = true;
        QCOMPARE(decode(withEndOfBlock, parameters).height, 6);

        parameters.hasEndOfBlock = false;
        QCOMPARE(decode(withEndOfBlock, parameters).height, 2);

        // Without the end of block the decoder stops at the end of the data
        encoderParameters.hasEndOfBlock = false;
        const QByteArray withoutEndOfBlock = encode(bitmap, encoderParameters);

        parameters.hasEndOfBlock = true;
        parameters.rows = 0;
        const Bitmap decoded = decode(withoutEndOfBlock, parameters);
        QCOMPARE(decoded.height, 6);
        QVERIFY(decoded == bitmap);
    }
}

void CCITTFaxTest::test_black_is_one_swaps_decode_array()
{
    // The decoder always writes 0 for a black pixel, so BlackIs1 is expressed by the
    // swapped decode array of the image data
    const Bitmap bitmap = createBitmap({ "#...#..." });

    pdf::PDFCCITTFaxEncoderParameters encoderParameters;
    encoderParameters.K = 0;

    const QByteArray data = encode(bitmap, encoderParameters);
    pdf::PDFCCITTFaxDecoderParameters parameters = createDecoderParameters(encoderParameters, 8, 1);
    parameters.decode = { 0.0, 1.0 };

    pdf::PDFCCITTFaxDecoder decoder(&data, parameters);
    const pdf::PDFImageData imageData = decoder.decode();
    QCOMPARE(imageData.getDecode(), std::vector<pdf::PDFReal>({ 0.0, 1.0 }));
    QCOMPARE(uint8_t(imageData.getData()[0]), uint8_t(0b01110111));

    parameters.hasBlackIsOne = true;
    pdf::PDFCCITTFaxDecoder invertedDecoder(&data, parameters);
    const pdf::PDFImageData invertedImageData = invertedDecoder.decode();
    QCOMPARE(invertedImageData.getDecode(), std::vector<pdf::PDFReal>({ 1.0, 0.0 }));
    QCOMPARE(uint8_t(invertedImageData.getData()[0]), uint8_t(0b01110111));
}

void CCITTFaxTest::test_decoder_rejects_malformed_streams()
{
    pdf::PDFCCITTFaxDecoderParameters group3;
    group3.K = 0;
    group3.columns = 8;
    group3.rows = 1;
    group3.hasEndOfBlock = false;
    group3.decode = { 0.0, 1.0 };

    pdf::PDFCCITTFaxDecoderParameters group4 = group3;
    group4.K = -1;

    // No code word starts with eight zeros - the longest run of the zeros in a code word
    // is seven, and twelve zeros would be a fill before an end of line
    QVERIFY(decodeExpectingError(fromBits("00000000" "11111"), group3).contains("run length"));

    // No mode code word starts with 0000 001
    QVERIFY(decodeExpectingError(fromBits("00000010" "1111"), group4).contains("2D mode"));

    // A run of 64 white pixels does not fit into a row of 8 pixels
    QVERIFY(decodeExpectingError(fromBits("11011" "00110101"), group3).contains("changing element a1"));

    // VR3 against the imaginary changing element at the end of the row places a1
    // behind the row
    QVERIFY(decodeExpectingError(fromBits("0000011"), group4).contains("vertical"));

    // VL3 against a changing element at the start of the reference row places a1
    // before the row - the reference row starts with a black pixel
    group4.rows = 2;
    QVERIFY(decodeExpectingError(fromBits("001" "00110101" "000101" "0000010"), group4).contains("vertical"));
    group4.rows = 1;

    // A run of 8 white pixels coded as 0 + 8 in the horizontal mode is fine, but the
    // black run behind it leaves the row
    QVERIFY(decodeExpectingError(fromBits("001" "10011" "0000111"), group4).contains("changing element a1"));

    // A truncated stream ends inside a code word - the white run of 2 pixels is followed
    // by the four zero bits of the padding, which are not a black code word yet
    QVERIFY(decodeExpectingError(fromBits("0111"), group3).contains("Not enough data"));

    // The reference row #. of two columns has a changing element at every column, so
    // the changing elements fill the reference line up to its two terminating entries.
    // VR1 against the first changing element moves b1 behind the last changing element,
    // and its colour lands it on the last entry of the line - the pass mode then needs
    // b2 behind the line.
    pdf::PDFCCITTFaxDecoderParameters narrow = group4;
    narrow.columns = 2;
    narrow.rows = 2;
    QVERIFY(decodeExpectingError(fromBits("000010" "010" "1" "011" "0001"), narrow).contains("b2 index"));

    // A pass mode against a white reference row moves a0 to the end of the row, because
    // both changing elements b1 and b2 are the end of the row - the row stays white
    pdf::PDFCCITTFaxDecoderParameters passAtEnd = group4;
    passAtEnd.rows = 2;
    QCOMPARE(drawBitmap(decode(fromBits("1" "0001"), passAtEnd)), QStringList({ "........", "........" }));

    // A required end of line is searched up to the end of the data - the data of two
    // rows end without the return to control, so the search behind the last row finds
    // nothing, with and without the byte alignment
    for (const bool hasEncodedByteAlign : { false, true })
    {
        pdf::PDFCCITTFaxDecoderParameters endOfLine = group3;
        endOfLine.rows = 0;
        endOfLine.hasEndOfLine = true;
        endOfLine.hasEndOfBlock = true;
        endOfLine.hasEncodedByteAlign = hasEncodedByteAlign;
        QCOMPARE(drawBitmap(decode(fromBits("000000000001" "10011" "000000000001" "10011"), endOfLine)), QStringList({ "........", "........" }));
    }

    // A vertical mode can place a1 to the left of a0 - the decoder tolerates it and
    // moves a0 back. The reference row is ....##..#......., the coded row passes the
    // first black run (a0 = 6), then VL3 against b1 = 8 gives a1 = 5, and the rest is
    // coded by V0 against the changing elements 6, 8, 9 and the end of the row.
    pdf::PDFCCITTFaxDecoderParameters wide = group4;
    wide.columns = 16;
    wide.rows = 2;
    const Bitmap tolerated = decode(fromBits("001" "1011" "11" "001" "0111" "010" "1" "0001" "0000010" "1" "1" "1" "1"), wide);
    QCOMPARE(drawBitmap(tolerated), QStringList({ "....##..#.......", ".....#..#......." }));

    // The same tolerance with the changing elements already recorded in the coded row -
    // the horizontal mode codes a white and a black pixel first. In the first stream
    // VL3 gives a1 = 3 against b1 = 6 after the pass mode, which does not reach back
    // behind the recorded elements; in the second stream VL3 gives a1 = 1 against
    // b1 = 4, which moves a0 back to the first recorded element.
    const Bitmap toleratedAfterElements = decode(fromBits("001" "1000" "11" "001" "000111" "010" "1" "001" "000111" "010" "0001" "0000010" "1" "1" "1" "1"), wide);
    QCOMPARE(drawBitmap(toleratedAfterElements), QStringList({ "...##.#.........", ".#.##.#........." }));

    const Bitmap toleratedBehindElements = decode(fromBits("001" "1011" "11" "1" "001" "000111" "010" "0000010" "1" "1"), wide);
    QCOMPARE(drawBitmap(toleratedBehindElements), QStringList({ "....##..........", ".#####.........." }));

    // The valid streams of the same rows are decoded, so the messages above are not
    // produced by something else
    QCOMPARE(drawBitmap(decode(fromBits("10011"), group3)), QStringList({ "........" }));
    QCOMPARE(drawBitmap(decode(fromBits("1"), group4)), QStringList({ "........" }));
}

void CCITTFaxTest::test_encoder_rejects_invalid_bitmap()
{
    pdf::PDFCCITTFaxEncoderParameters parameters;

    pdf::PDFBitonalBitmapView view;
    pdf::PDFCCITTFaxEncoder encoder(view, parameters);
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, encoder.encode());

    const Bitmap bitmap = createTestBitmap("white", 8, 1, 0);
    view = bitmap.view();
    view.width = 0;
    pdf::PDFCCITTFaxEncoder emptyEncoder(view, parameters);
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, emptyEncoder.encode());
}

QTEST_APPLESS_MAIN(CCITTFaxTest)

#include "tst_ccittfaxtest.moc"
