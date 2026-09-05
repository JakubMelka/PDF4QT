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

#include <random>

/// Tests of the JBIG2 encoder. The arithmetic coder is verified against the test sequence
/// of the annex H.2 of the specification, the generic region coding against the example
/// datastream of the annex H.2, and everything by the round trips through the decoder over
/// randomly generated images, all templates and both codings.
class JBIG2EncoderTest : public QObject
{
    Q_OBJECT

private slots:
    void test_arithmetic_encoder_matches_annex_h_test_sequence();
    void test_arithmetic_encoder_roundtrip_data();
    void test_arithmetic_encoder_roundtrip();
    void test_nominal_at_positions();
    void test_annex_h_generic_region_is_reencoded_identically();
    void test_generic_region_roundtrip_data();
    void test_generic_region_roundtrip();
    void test_generic_region_custom_at_positions();
    void test_embedded_stream_structure();
    void test_file_stream_roundtrip();
    void test_scanned_page_is_compressed();
    void test_encoder_rejects_invalid_input();

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

    /// Returns the bitmap drawn as one line of text per row, so a failed comparison
    /// shows the images
    static QStringList drawBitmap(const Bitmap& bitmap);

    /// Creates a test image of the given kind, see \p getTestBitmapKinds
    static Bitmap createTestBitmap(const QString& kind, int width, int height, unsigned seed);

    /// Returns the names of the kinds of the test images
    static QStringList getTestBitmapKinds();

    /// Converts the decoded image into the test bitmap. The decoder writes a set bit for
    /// a white pixel, so the samples are inverted.
    static Bitmap toBitmap(const pdf::PDFImageData& imageData);

    /// Decodes the embedded stream. Throws \p pdf::PDFException for a malformed stream.
    static Bitmap decodeEmbeddedStream(const QByteArray& stream, pdf::PDFRenderErrorReporter* errorReporter = nullptr);

    /// Returns the region of the bitmap
    static Bitmap getRegion(const Bitmap& bitmap, int x, int y, int width, int height);

    static QByteArray toByteArray(const unsigned char* data, size_t size) { return QByteArray(reinterpret_cast<const char*>(data), int(size)); }
    static uint32_t readUInt32(const QByteArray& data, int position);
};

QStringList JBIG2EncoderTest::drawBitmap(const Bitmap& bitmap)
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

QStringList JBIG2EncoderTest::getTestBitmapKinds()
{
    return { "white", "black", "noise50", "noise5", "noise95", "columns", "rows", "checker", "diagonal", "blocks", "typical", "pixel" };
}

JBIG2EncoderTest::Bitmap JBIG2EncoderTest::createTestBitmap(const QString& kind, int width, int height, unsigned seed)
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
    else if (kind == "typical")
    {
        // Groups of identical rows separated by different ones, so the typical
        // prediction switches on and off - the first row is also identical to the
        // imaginary zero row above the image
        for (int y = 0; y < height; ++y)
        {
            const int group = y / 3;
            if (group % 3 == 0)
            {
                continue;
            }

            for (int x = 0; x < width; ++x)
            {
                if ((x + group) % 5 < 2)
                {
                    bitmap.setBlack(x, y);
                }
            }
        }
    }
    else if (kind == "pixel")
    {
        bitmap.setBlack(width - 1, height - 1);
    }

    return bitmap;
}

JBIG2EncoderTest::Bitmap JBIG2EncoderTest::toBitmap(const pdf::PDFImageData& imageData)
{
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

JBIG2EncoderTest::Bitmap JBIG2EncoderTest::decodeEmbeddedStream(const QByteArray& stream, pdf::PDFRenderErrorReporter* errorReporter)
{
    pdf::PDFRenderErrorReporterDummy dummyErrorReporter;
    pdf::PDFJBIG2Decoder decoder(stream, QByteArray(), errorReporter ? errorReporter : &dummyErrorReporter);
    return toBitmap(decoder.decode(pdf::PDFImageData::MaskingType::None));
}

JBIG2EncoderTest::Bitmap JBIG2EncoderTest::getRegion(const Bitmap& bitmap, int x, int y, int width, int height)
{
    Bitmap region(width, height);

    for (int row = 0; row < height; ++row)
    {
        for (int column = 0; column < width; ++column)
        {
            if (bitmap.isBlack(x + column, y + row))
            {
                region.setBlack(column, row);
            }
        }
    }

    return region;
}

uint32_t JBIG2EncoderTest::readUInt32(const QByteArray& data, int position)
{
    return (uint32_t(uint8_t(data[position])) << 24) | (uint32_t(uint8_t(data[position + 1])) << 16) |
           (uint32_t(uint8_t(data[position + 2])) << 8) | uint32_t(uint8_t(data[position + 3]));
}

void JBIG2EncoderTest::test_arithmetic_encoder_matches_annex_h_test_sequence()
{
    // The test sequence of the annex H.2 of the specification - 256 decisions coded
    // with a single context, whose state starts at the index 0 and MPS 0. The table
    // H.1 traces the registers of the encoder decision by decision.
    static const unsigned char DECISIONS[] =
    {
        0x00, 0x02, 0x00, 0x51, 0x00, 0x00, 0x00, 0xC0, 0x03, 0x52, 0x87, 0x2A, 0xAA, 0xAA, 0xAA, 0xAA,
        0x82, 0xC0, 0x20, 0x00, 0xFC, 0xD7, 0x9E, 0xF6, 0xBF, 0x7F, 0xED, 0x90, 0x4F, 0x46, 0xA3, 0xBF
    };

    static const unsigned char EXPECTED[] =
    {
        0x84, 0xC7, 0x3B, 0xFC, 0xE1, 0xA1, 0x43, 0x04, 0x02, 0x20, 0x00, 0x00, 0x41, 0x0D, 0xBB, 0x86,
        0xF4, 0x31, 0x7F, 0xFF, 0x88, 0xFF, 0x37, 0x47, 0x1A, 0xDB, 0x6A, 0xDF, 0xFF, 0xAC
    };

    pdf::PDFJBIG2ArithmeticDecoderState state(1);
    pdf::PDFJBIG2ArithmeticEncoder encoder;

    for (const unsigned char byte : DECISIONS)
    {
        for (int i = 7; i >= 0; --i)
        {
            encoder.encodeBit(0, &state, (byte >> i) & 0x01);
        }
    }

    const QByteArray encoded = encoder.finish();
    QCOMPARE(encoded.toHex(), toByteArray(EXPECTED, sizeof(EXPECTED)).toHex());

    // The decoder must produce the decisions back
    pdf::PDFJBIG2ArithmeticDecoderState decoderState(1);
    pdf::PDFBitReader reader(&encoded, 8);
    pdf::PDFJBIG2ArithmeticDecoder decoder(&reader);
    decoder.initialize();

    QByteArray decoded;
    for (size_t i = 0; i < sizeof(DECISIONS); ++i)
    {
        decoded.append(char(decoder.readByte(0, &decoderState)));
    }

    QCOMPARE(decoded.toHex(), toByteArray(DECISIONS, sizeof(DECISIONS)).toHex());
}

void JBIG2EncoderTest::test_arithmetic_encoder_roundtrip_data()
{
    QTest::addColumn<QString>("distribution");
    QTest::addColumn<int>("count");
    QTest::addColumn<int>("contextBits");

    for (const QString& distribution : { "zeros", "ones", "balanced", "rare", "frequent", "alternating" })
    {
        for (const int count : { 0, 1, 2, 17, 1000, 100000 })
        {
            for (const int contextBits : { 0, 4, 16 })
            {
                QTest::newRow(qPrintable(QString("%1 %2 decisions %3 context bits").arg(distribution).arg(count).arg(contextBits))) << distribution << count << contextBits;
            }
        }
    }
}

void JBIG2EncoderTest::test_arithmetic_encoder_roundtrip()
{
    QFETCH(QString, distribution);
    QFETCH(int, count);
    QFETCH(int, contextBits);

    // The decisions and their contexts are generated in advance, so the same sequence
    // is coded and decoded
    std::mt19937 generator(unsigned(count * 7 + contextBits));
    std::uniform_int_distribution<uint32_t> contextDistribution(0, (1u << contextBits) - 1);
    std::bernoulli_distribution bitDistribution(distribution == "rare" ? 0.01 : (distribution == "frequent" ? 0.99 : 0.5));

    std::vector<std::pair<uint32_t, uint32_t>> decisions;
    decisions.reserve(size_t(count));

    for (int i = 0; i < count; ++i)
    {
        uint32_t bit = 0;
        if (distribution == "ones")
        {
            bit = 1;
        }
        else if (distribution == "alternating")
        {
            bit = uint32_t(i & 1);
        }
        else if (distribution != "zeros")
        {
            bit = bitDistribution(generator) ? 1 : 0;
        }

        decisions.emplace_back(contextDistribution(generator), bit);
    }

    pdf::PDFJBIG2ArithmeticDecoderState encoderState;
    encoderState.reset(uint8_t(contextBits));
    pdf::PDFJBIG2ArithmeticEncoder encoder;

    for (const auto& [context, bit] : decisions)
    {
        encoder.encodeBit(context, &encoderState, bit);
    }

    const QByteArray encoded = encoder.finish();
    QVERIFY(encoded.size() >= 2);
    QCOMPARE(uint8_t(encoded[encoded.size() - 2]), uint8_t(0xFF));
    QCOMPARE(uint8_t(encoded[encoded.size() - 1]), uint8_t(0xAC));

    // A byte 0xFF is always followed by a byte below 0x90, see the bit stuffing
    for (int i = 0; i + 1 < encoded.size() - 1; ++i)
    {
        if (uint8_t(encoded[i]) == 0xFF)
        {
            QVERIFY(uint8_t(encoded[i + 1]) < 0x90);
        }
    }

    pdf::PDFJBIG2ArithmeticDecoderState decoderState;
    decoderState.reset(uint8_t(contextBits));
    pdf::PDFBitReader reader(&encoded, 8);
    pdf::PDFJBIG2ArithmeticDecoder decoder(&reader);
    decoder.initialize();

    for (size_t i = 0; i < decisions.size(); ++i)
    {
        const uint32_t decoded = decoder.readBit(decisions[i].first, &decoderState);
        if (decoded != decisions[i].second)
        {
            QFAIL(qPrintable(QString("Decision %1 of %2 differs.").arg(i).arg(decisions.size())));
        }
    }
}

void JBIG2EncoderTest::test_nominal_at_positions()
{
    // Figures 4, 5 and 6 of the specification
    const pdf::PDFJBIG2ATPositions template0 = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(0);
    QCOMPARE(int(template0[0].x), 3);
    QCOMPARE(int(template0[0].y), -1);
    QCOMPARE(int(template0[1].x), -3);
    QCOMPARE(int(template0[1].y), -1);
    QCOMPARE(int(template0[2].x), 2);
    QCOMPARE(int(template0[2].y), -2);
    QCOMPARE(int(template0[3].x), -2);
    QCOMPARE(int(template0[3].y), -2);

    const pdf::PDFJBIG2ATPositions template1 = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(1);
    QCOMPARE(int(template1[0].x), 3);
    QCOMPARE(int(template1[0].y), -1);

    for (const uint8_t GBTEMPLATE : { uint8_t(2), uint8_t(3) })
    {
        const pdf::PDFJBIG2ATPositions positions = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(GBTEMPLATE);
        QCOMPARE(int(positions[0].x), 2);
        QCOMPARE(int(positions[0].y), -1);
    }

    // The default parameters use the nominal positions of the template 0
    const pdf::PDFJBIG2EncoderParameters parameters;
    QCOMPARE(parameters.GBTEMPLATE, uint8_t(0));
    QVERIFY(std::memcmp(&parameters.GBAT, &template0, sizeof(template0)) == 0);

    QCOMPARE(pdf::PDFJBIG2EncoderParameters::getATPositionCount(0), 4);
    QCOMPARE(pdf::PDFJBIG2EncoderParameters::getATPositionCount(1), 1);
    QCOMPARE(pdf::PDFJBIG2EncoderParameters::getATPositionCount(2), 1);
    QCOMPARE(pdf::PDFJBIG2EncoderParameters::getATPositionCount(3), 1);
}

void JBIG2EncoderTest::test_annex_h_generic_region_is_reencoded_identically()
{
    // The example datastream of the annex H.2 of the specification codes the same
    // region of 54 by 44 pixels (figure H.6) twice - the segment 4 by MMR and the
    // segment 11 by the arithmetic coding with the template 0, TPGDON and the nominal
    // AT pixels. Both are decoded here and the region is encoded again with the same
    // parameters - the output must be identical, because both codings are
    // deterministic for the given decisions.
    static const unsigned char PAGE_INFORMATION[] =
    {
        0x00, 0x00, 0x00, 0x01, 0x30, 0x00, 0x01, 0x00, 0x00, 0x00, 0x13,
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00
    };

    static const unsigned char MMR_REGION[] =
    {
        0x00, 0x00, 0x00, 0x04, 0x27, 0x00, 0x01, 0x00, 0x00, 0x00, 0x2C,
        0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0B, 0x00,
        0x01, 0x26, 0xA0, 0x71, 0xCE, 0xA7, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0xF0
    };

    static const unsigned char ARITHMETIC_REGION[] =
    {
        0x00, 0x00, 0x00, 0x0B, 0x27, 0x00, 0x02, 0x00, 0x00, 0x00, 0x23,
        0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x0B, 0x00,
        0x08, 0x03, 0xFF, 0xFD, 0xFF, 0x02, 0xFE, 0xFE, 0xFE, 0x04, 0xEE, 0xED, 0x87, 0xFB, 0xCB, 0x2B, 0xFF, 0xAC
    };

    const QByteArray pageInformation = toByteArray(PAGE_INFORMATION, sizeof(PAGE_INFORMATION));

    ErrorCollector errorCollector;
    const Bitmap mmrPage = decodeEmbeddedStream(pageInformation + toByteArray(MMR_REGION, sizeof(MMR_REGION)), &errorCollector);
    const Bitmap arithmeticPage = decodeEmbeddedStream(pageInformation + toByteArray(ARITHMETIC_REGION, sizeof(ARITHMETIC_REGION)), &errorCollector);
    QCOMPARE(errorCollector.messages, QStringList());

    QCOMPARE(mmrPage.width, 64);
    QCOMPARE(mmrPage.height, 56);
    QCOMPARE(drawBitmap(arithmeticPage), drawBitmap(mmrPage));

    const Bitmap region = getRegion(mmrPage, 4, 11, 54, 44);

    // The region is not trivial
    int blackPixels = 0;
    for (int y = 0; y < region.height; ++y)
    {
        for (int x = 0; x < region.width; ++x)
        {
            blackPixels += region.isBlack(x, y) ? 1 : 0;
        }
    }
    QVERIFY(blackPixels > 100);
    QVERIFY(blackPixels < region.width * region.height - 100);

    pdf::PDFJBIG2EncoderParameters arithmeticParameters;
    arithmeticParameters.MMR = false;
    arithmeticParameters.GBTEMPLATE = 0;
    arithmeticParameters.TPGDON = true;
    arithmeticParameters.GBAT = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(0);

    pdf::PDFJBIG2Encoder arithmeticEncoder(region.view(), arithmeticParameters);
    QCOMPARE(arithmeticEncoder.encodeGenericRegion().toHex(), QByteArray("\x04\xEE\xED\x87\xFB\xCB\x2B\xFF\xAC", 9).toHex());

    pdf::PDFJBIG2EncoderParameters mmrParameters;
    mmrParameters.MMR = true;

    pdf::PDFJBIG2Encoder mmrEncoder(region.view(), mmrParameters);
    QCOMPARE(mmrEncoder.encodeGenericRegion().toHex(), toByteArray(MMR_REGION + 29, sizeof(MMR_REGION) - 29).toHex());

    // The whole segments of the encoder differ from the example only by the placement
    // of the region and by the type of the segment, so the data part of the region
    // segment must match from the flags on
    const QByteArray arithmeticStream = arithmeticEncoder.encodeEmbeddedStream();
    QVERIFY(arithmeticStream.endsWith(toByteArray(ARITHMETIC_REGION + 28, sizeof(ARITHMETIC_REGION) - 28)));

    const QByteArray mmrStream = mmrEncoder.encodeEmbeddedStream();
    QVERIFY(mmrStream.endsWith(toByteArray(MMR_REGION + 28, sizeof(MMR_REGION) - 28)));
}

void JBIG2EncoderTest::test_generic_region_roundtrip_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<int>("width");
    QTest::addColumn<int>("height");
    QTest::addColumn<bool>("MMR");
    QTest::addColumn<int>("GBTEMPLATE");
    QTest::addColumn<bool>("TPGDON");

    const std::vector<std::pair<int, int>> sizes = { { 1, 1 }, { 7, 3 }, { 8, 8 }, { 9, 5 }, { 33, 17 }, { 100, 40 }, { 257, 3 }, { 64, 64 } };
    unsigned index = 0;

    for (const QString& kind : getTestBitmapKinds())
    {
        for (int configuration = 0; configuration < 9; ++configuration)
        {
            const bool MMR = configuration == 8;
            const int GBTEMPLATE = MMR ? 0 : configuration / 2;
            const bool TPGDON = !MMR && (configuration % 2) == 1;

            const std::pair<int, int> size = sizes[index % sizes.size()];
            const QString name = MMR ? QString("%1 %2x%3 MMR").arg(kind).arg(size.first).arg(size.second)
                                     : QString("%1 %2x%3 template %4%5").arg(kind).arg(size.first).arg(size.second).arg(GBTEMPLATE).arg(TPGDON ? " TPGDON" : "");
            QTest::newRow(qPrintable(name)) << kind << size.first << size.second << MMR << GBTEMPLATE << TPGDON;
            ++index;
        }
    }
}

void JBIG2EncoderTest::test_generic_region_roundtrip()
{
    QFETCH(QString, kind);
    QFETCH(int, width);
    QFETCH(int, height);
    QFETCH(bool, MMR);
    QFETCH(int, GBTEMPLATE);
    QFETCH(bool, TPGDON);

    const Bitmap bitmap = createTestBitmap(kind, width, height, unsigned(width * 13 + height * 5 + GBTEMPLATE));

    pdf::PDFJBIG2EncoderParameters parameters;
    parameters.MMR = MMR;
    parameters.GBTEMPLATE = uint8_t(GBTEMPLATE);
    parameters.TPGDON = TPGDON;
    parameters.GBAT = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(uint8_t(GBTEMPLATE));

    QByteArray stream;
    Bitmap decoded;
    ErrorCollector errorCollector;

    try
    {
        pdf::PDFJBIG2Encoder encoder(bitmap.view(), parameters);
        stream = encoder.encodeEmbeddedStream();
        decoded = decodeEmbeddedStream(stream, &errorCollector);
    }
    catch (const pdf::PDFException& exception)
    {
        QFAIL(qPrintable(exception.getMessage()));
    }

    QCOMPARE(errorCollector.messages, QStringList());
    QCOMPARE(decoded.width, width);
    QCOMPARE(decoded.height, height);

    if (!(decoded == bitmap) && width <= 80)
    {
        QCOMPARE(drawBitmap(decoded), drawBitmap(bitmap));
    }
    QVERIFY(decoded == bitmap);
}

void JBIG2EncoderTest::test_generic_region_custom_at_positions()
{
    // Adaptive template pixels away from their nominal positions, including a pixel
    // of the current row, which is allowed to the left of the coded pixel, and pixels
    // outside of the image, which are always zero
    const Bitmap bitmap = createTestBitmap("blocks", 90, 30, 42);

    struct Configuration
    {
        uint8_t GBTEMPLATE;
        pdf::PDFJBIG2ATPositions GBAT;
    };

    const std::vector<Configuration> configurations =
    {
        { 0, { { { 5, -3 }, { -7, -1 }, { 0, -5 }, { -128, -128 } } } },
        { 0, { { { -1, 0 }, { -2, 0 }, { -3, 0 }, { -4, 0 } } } },
        { 0, { { { 127, -1 }, { -128, 0 }, { 127, -128 }, { -1, -1 } } } },
        { 1, { { { -6, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } } },
        { 2, { { { 1, -3 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } } },
        { 3, { { { -2, -1 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } } }
    };

    for (size_t i = 0; i < configurations.size(); ++i)
    {
        for (const bool TPGDON : { false, true })
        {
            pdf::PDFJBIG2EncoderParameters parameters;
            parameters.GBTEMPLATE = configurations[i].GBTEMPLATE;
            parameters.GBAT = configurations[i].GBAT;
            parameters.TPGDON = TPGDON;

            pdf::PDFJBIG2Encoder encoder(bitmap.view(), parameters);
            const QByteArray stream = encoder.encodeEmbeddedStream();

            ErrorCollector errorCollector;
            const Bitmap decoded = decodeEmbeddedStream(stream, &errorCollector);
            QCOMPARE(errorCollector.messages, QStringList());
            QVERIFY2(decoded == bitmap, qPrintable(QString("Configuration %1, TPGDON %2").arg(i).arg(TPGDON)));

            // The AT pixels are written after the flags of the region
            const int atOffset = 11 + 19 + 11 + 17 + 1;
            const int atCount = pdf::PDFJBIG2EncoderParameters::getATPositionCount(parameters.GBTEMPLATE);
            for (int j = 0; j < atCount; ++j)
            {
                QCOMPARE(int(int8_t(stream[atOffset + 2 * j])), int(parameters.GBAT[j].x));
                QCOMPARE(int(int8_t(stream[atOffset + 2 * j + 1])), int(parameters.GBAT[j].y));
            }
        }
    }
}

void JBIG2EncoderTest::test_embedded_stream_structure()
{
    // The layout of the segments, see 7.2 and 7.4 of the specification and 7.4.7 of
    // the PDF specification - the page information segment and the region segment,
    // both associated with the page 1, and no file header, end of page or end of file
    const Bitmap bitmap = createTestBitmap("diagonal", 300, 20, 1);

    pdf::PDFJBIG2EncoderParameters parameters;
    pdf::PDFJBIG2Encoder encoder(bitmap.view(), parameters);
    const QByteArray stream = encoder.encodeEmbeddedStream();

    // Page information segment header - the segment number 0, the type 48, no referred
    // segments, the page 1 and the length of 19 bytes
    QCOMPARE(readUInt32(stream, 0), 0u);
    QCOMPARE(uint8_t(stream[4]), uint8_t(48));
    QCOMPARE(uint8_t(stream[5]), uint8_t(0));
    QCOMPARE(uint8_t(stream[6]), uint8_t(1));
    QCOMPARE(readUInt32(stream, 7), 19u);

    // Page information - the size, the unknown resolution, the flags (eventually
    // lossless) and no striping
    QCOMPARE(readUInt32(stream, 11), 300u);
    QCOMPARE(readUInt32(stream, 15), 20u);
    QCOMPARE(readUInt32(stream, 19), 0u);
    QCOMPARE(readUInt32(stream, 23), 0u);
    QCOMPARE(uint8_t(stream[27]), uint8_t(0x01));
    QCOMPARE(uint8_t(stream[28]), uint8_t(0));
    QCOMPARE(uint8_t(stream[29]), uint8_t(0));

    // Region segment header - the segment number 1, the type 38 (immediate generic
    // region), the page 1 and the length covering the rest of the stream
    QCOMPARE(readUInt32(stream, 30), 1u);
    QCOMPARE(uint8_t(stream[34]), uint8_t(38));
    QCOMPARE(uint8_t(stream[35]), uint8_t(0));
    QCOMPARE(uint8_t(stream[36]), uint8_t(1));
    QCOMPARE(int(readUInt32(stream, 37)), stream.size() - 41);

    // Region segment information field - the size, the position and the operator OR
    QCOMPARE(readUInt32(stream, 41), 300u);
    QCOMPARE(readUInt32(stream, 45), 20u);
    QCOMPARE(readUInt32(stream, 49), 0u);
    QCOMPARE(readUInt32(stream, 53), 0u);
    QCOMPARE(uint8_t(stream[57]), uint8_t(0));

    // Generic region flags - the template 0 with TPGDON, and the nominal AT pixels
    QCOMPARE(uint8_t(stream[58]), uint8_t(0x08));
    QCOMPARE(stream.mid(59, 8), QByteArray("\x03\xFF\xFD\xFF\x02\xFE\xFE\xFE", 8));

    // The arithmetic data end with the marker
    QVERIFY(stream.endsWith(QByteArray("\xFF\xAC", 2)));

    // The MMR variant has the flag 1 and no AT pixels, and the other templates have
    // a single AT pixel
    parameters.MMR = true;
    const QByteArray mmrStream = pdf::PDFJBIG2Encoder(bitmap.view(), parameters).encodeEmbeddedStream();
    QCOMPARE(uint8_t(mmrStream[58]), uint8_t(0x01));
    QCOMPARE(int(readUInt32(mmrStream, 37)), mmrStream.size() - 41);

    parameters.MMR = false;
    parameters.TPGDON = false;
    parameters.GBTEMPLATE = 2;
    parameters.GBAT = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(2);
    const QByteArray template2Stream = pdf::PDFJBIG2Encoder(bitmap.view(), parameters).encodeEmbeddedStream();
    QCOMPARE(uint8_t(template2Stream[58]), uint8_t(0x04));
    QCOMPARE(template2Stream.mid(59, 2), QByteArray("\x02\xFF", 2));
}

void JBIG2EncoderTest::test_file_stream_roundtrip()
{
    const Bitmap bitmap = createTestBitmap("blocks", 120, 50, 11);

    for (const bool MMR : { false, true })
    {
        pdf::PDFJBIG2EncoderParameters parameters;
        parameters.MMR = MMR;

        pdf::PDFJBIG2Encoder encoder(bitmap.view(), parameters);
        const QByteArray file = encoder.encodeFile();
        const QByteArray embedded = encoder.encodeEmbeddedStream();

        // The file header of the annex D.4 - the identifier, the sequential organisation
        // with a known number of the pages, and a single page. The segments of the
        // embedded stream follow, terminated by the end of page and the end of file.
        QVERIFY(file.startsWith(QByteArray("\x97\x4A\x42\x32\x0D\x0A\x1A\x0A\x01\x00\x00\x00\x01", 13)));
        QCOMPARE(file.mid(13, embedded.size()), embedded);
        QCOMPARE(file.mid(13 + embedded.size()), QByteArray("\x00\x00\x00\x02\x31\x00\x01\x00\x00\x00\x00" "\x00\x00\x00\x03\x33\x00\x01\x00\x00\x00\x00", 22));

        ErrorCollector errorCollector;
        pdf::PDFJBIG2Decoder decoder(file, QByteArray(), &errorCollector);
        const Bitmap decoded = toBitmap(decoder.decodeFileStream());
        QCOMPARE(errorCollector.messages, QStringList());
        QVERIFY(decoded == bitmap);
    }
}

void JBIG2EncoderTest::test_scanned_page_is_compressed()
{
    // A page of the size of a scanned document with a text-like content. The
    // arithmetic coding with the typical prediction must compress it far below the
    // raw size, and the image must survive the round trip.
    const int width = 1700;
    const int height = 1100;
    Bitmap bitmap(width, height);

    std::mt19937 generator(5);
    std::uniform_int_distribution<int> glyphWidth(3, 14);
    std::uniform_int_distribution<int> glyphHeight(6, 16);
    std::uniform_int_distribution<int> gap(2, 8);
    std::bernoulli_distribution ink(0.6);

    for (int lineTop = 60; lineTop + 20 < height - 60; lineTop += 28)
    {
        for (int x = 80; x < width - 80; )
        {
            const int w = glyphWidth(generator);
            const int h = glyphHeight(generator);

            for (int y = lineTop; y < lineTop + h; ++y)
            {
                for (int i = 0; i < w; ++i)
                {
                    if (ink(generator))
                    {
                        bitmap.setBlack(x + i, y);
                    }
                }
            }

            x += w + gap(generator);
        }
    }

    pdf::PDFJBIG2EncoderParameters parameters;
    pdf::PDFJBIG2Encoder encoder(bitmap.view(), parameters);
    const QByteArray stream = encoder.encodeEmbeddedStream();

    const int rawSize = bitmap.stride * bitmap.height;
    QVERIFY2(stream.size() * 2 < rawSize, qPrintable(QString("%1 bytes of %2 raw bytes").arg(stream.size()).arg(rawSize)));

    ErrorCollector errorCollector;
    const Bitmap decoded = decodeEmbeddedStream(stream, &errorCollector);
    QCOMPARE(errorCollector.messages, QStringList());
    QVERIFY(decoded == bitmap);

    // The MMR coding of the same page
    parameters.MMR = true;
    const QByteArray mmrStream = pdf::PDFJBIG2Encoder(bitmap.view(), parameters).encodeEmbeddedStream();
    QVERIFY(mmrStream.size() < rawSize);
    QVERIFY(decodeEmbeddedStream(mmrStream, &errorCollector) == bitmap);
    QCOMPARE(errorCollector.messages, QStringList());
}

void JBIG2EncoderTest::test_encoder_rejects_invalid_input()
{
    const Bitmap bitmap = createTestBitmap("white", 8, 2, 0);
    pdf::PDFJBIG2EncoderParameters parameters;

    // Invalid view
    pdf::PDFBitonalBitmapView invalidView;
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Encoder(invalidView, parameters).encodeEmbeddedStream());
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Encoder(invalidView, parameters).encodeFile());
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Encoder(invalidView, parameters).encodeGenericRegion());

    // Too many pixels - the size is refused before the data are touched
    pdf::PDFBitonalBitmapView hugeView = bitmap.view();
    hugeView.width = 40000;
    hugeView.height = 40000;
    hugeView.stride = 5000;
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Encoder(hugeView, parameters).encodeGenericRegion());

    // Invalid template
    parameters.GBTEMPLATE = 4;
    QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Encoder(bitmap.view(), parameters).encodeGenericRegion());
    parameters.GBTEMPLATE = 0;

    // An adaptive template pixel must lie above the current row, or to the left of the
    // coded pixel in the current row
    for (const pdf::PDFJBIG2ATPosition position : { pdf::PDFJBIG2ATPosition{ 0, 0 }, pdf::PDFJBIG2ATPosition{ 1, 0 }, pdf::PDFJBIG2ATPosition{ 0, 1 }, pdf::PDFJBIG2ATPosition{ -1, 1 } })
    {
        for (int i = 0; i < 4; ++i)
        {
            parameters.GBAT = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(0);
            parameters.GBAT[i] = position;
            QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Encoder(bitmap.view(), parameters).encodeGenericRegion());
        }

        // Only the first pixel is used by the other templates
        parameters.GBTEMPLATE = 1;
        parameters.GBAT = pdf::PDFJBIG2EncoderParameters::getNominalATPositions(1);
        parameters.GBAT[1] = position;
        pdf::PDFJBIG2Encoder(bitmap.view(), parameters).encodeGenericRegion();
        parameters.GBAT[0] = position;
        QVERIFY_THROWS_EXCEPTION(pdf::PDFException, pdf::PDFJBIG2Encoder(bitmap.view(), parameters).encodeGenericRegion());
        parameters.GBTEMPLATE = 0;
    }

    // The invalid template and pixels do not matter to the MMR coding
    parameters.MMR = true;
    parameters.GBTEMPLATE = 4;
    parameters.GBAT[0] = { 1, 1 };
    QVERIFY(!pdf::PDFJBIG2Encoder(bitmap.view(), parameters).encodeGenericRegion().isEmpty());
}

QTEST_APPLESS_MAIN(JBIG2EncoderTest)

#include "tst_jbig2encodertest.moc"
