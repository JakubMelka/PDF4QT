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

#include "pdfjbig2encoder.h"
#include "pdfexception.h"
#include "pdfdbgheap.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace pdf
{

struct PDFJBIG2ArithmeticEncoderQeValue
{
    uint16_t Qe;        ///< Value of Qe
    uint8_t newMPS;     ///< New row if MPS (more probable symbol)
    uint8_t newLPS;     ///< New row if LPS (less probable symbol)
    uint8_t switchFlag; ///< Meaning of MPS/LPS is switched
};

/// Table E.1 of the specification. The decoder has the same table with the values of
/// Qe shifted by 16 bits, because it uses 32-bit fixed point arithmetic - the encoder
/// keeps the 16-bit values of the specification, so its output can be compared with
/// the test sequence of the annex H.2 register by register.
static constexpr PDFJBIG2ArithmeticEncoderQeValue JBIG2_ARITHMETIC_ENCODER_QE_VALUES[] =
{
    { 0x5601, 1,   1, 1 },
    { 0x3401, 2,   6, 0 },
    { 0x1801, 3,   9, 0 },
    { 0x0AC1, 4,  12, 0 },
    { 0x0521, 5,  29, 0 },
    { 0x0221, 38, 33, 0 },
    { 0x5601, 7,   6, 1 },
    { 0x5401, 8,  14, 0 },
    { 0x4801, 9,  14, 0 },
    { 0x3801, 10, 14, 0 },
    { 0x3001, 11, 17, 0 },
    { 0x2401, 12, 18, 0 },
    { 0x1C01, 13, 20, 0 },
    { 0x1601, 29, 21, 0 },
    { 0x5601, 15, 14, 1 },
    { 0x5401, 16, 14, 0 },
    { 0x5101, 17, 15, 0 },
    { 0x4801, 18, 16, 0 },
    { 0x3801, 19, 17, 0 },
    { 0x3401, 20, 18, 0 },
    { 0x3001, 21, 19, 0 },
    { 0x2801, 22, 19, 0 },
    { 0x2401, 23, 20, 0 },
    { 0x2201, 24, 21, 0 },
    { 0x1C01, 25, 22, 0 },
    { 0x1801, 26, 23, 0 },
    { 0x1601, 27, 24, 0 },
    { 0x1401, 28, 25, 0 },
    { 0x1201, 29, 26, 0 },
    { 0x1101, 30, 27, 0 },
    { 0x0AC1, 31, 28, 0 },
    { 0x09C1, 32, 29, 0 },
    { 0x08A1, 33, 30, 0 },
    { 0x0521, 34, 31, 0 },
    { 0x0441, 35, 32, 0 },
    { 0x02A1, 36, 33, 0 },
    { 0x0221, 37, 34, 0 },
    { 0x0141, 38, 35, 0 },
    { 0x0111, 39, 36, 0 },
    { 0x0085, 40, 37, 0 },
    { 0x0049, 41, 38, 0 },
    { 0x0025, 42, 39, 0 },
    { 0x0015, 43, 40, 0 },
    { 0x0009, 44, 41, 0 },
    { 0x0005, 45, 42, 0 },
    { 0x0001, 45, 43, 0 },
    { 0x5601, 46, 46, 0 }
};

PDFJBIG2ArithmeticEncoder::PDFJBIG2ArithmeticEncoder()
{
    // Procedure INITENC, figure E.10. The byte preceding the output buffer is
    // assumed to be zero (see the annex H.2), so it does not cause a bit stuffing
    // and the counter of the shifts starts at 12.
}

void PDFJBIG2ArithmeticEncoder::encodeBit(size_t context, PDFJBIG2ArithmeticDecoderState* state, uint32_t bit)
{
    Q_ASSERT(!m_isFinished);
    Q_ASSERT(bit < 2);

    const uint8_t QeRowIndex = state->getQeRowIndex(context);
    uint8_t MPS = state->getMPS(context);

    Q_ASSERT(QeRowIndex < std::size(JBIG2_ARITHMETIC_ENCODER_QE_VALUES));

    const PDFJBIG2ArithmeticEncoderQeValue& QeInfo = JBIG2_ARITHMETIC_ENCODER_QE_VALUES[QeRowIndex];
    const uint32_t Qe = QeInfo.Qe;

    if (bit == MPS)
    {
        // Procedure CODEMPS, figure E.7. The more probable symbol is the upper
        // subinterval, so the code register is moved up by the size of the lower
        // one, unless the conditional exchange makes the lower subinterval larger.
        m_a -= Qe;

        if ((m_a & 0x8000) == 0)
        {
            if (m_a < Qe)
            {
                m_a = Qe;
            }
            else
            {
                m_c += Qe;
            }

            state->setQeRowIndexAndMPS(context, QeInfo.newMPS, MPS);
            renormalize();
        }
        else
        {
            m_c += Qe;
        }
    }
    else
    {
        // Procedure CODELPS, figure E.6
        m_a -= Qe;

        if (m_a < Qe)
        {
            m_c += Qe;
        }
        else
        {
            m_a = Qe;
        }

        if (QeInfo.switchFlag)
        {
            MPS = 1 - MPS;
        }

        state->setQeRowIndexAndMPS(context, QeInfo.newLPS, MPS);
        renormalize();
    }
}

QByteArray PDFJBIG2ArithmeticEncoder::finish()
{
    Q_ASSERT(!m_isFinished);
    m_isFinished = true;

    // Procedure FLUSH, figure E.11. SETBITS (figure E.12) sets as many low bits
    // of the code register to one as possible, while the register stays inside
    // the current interval.
    const uint32_t tempC = m_c + m_a;
    m_c |= 0xFFFF;
    if (m_c >= tempC)
    {
        m_c -= 0x8000;
    }

    m_c <<= m_ct;
    byteOut();
    m_c <<= m_ct;
    byteOut();

    if (m_b != 0xFF)
    {
        emitByte();
        m_b = 0xFF;
    }

    emitByte();
    m_b = 0xAC;
    emitByte();

    return m_output;
}

void PDFJBIG2ArithmeticEncoder::renormalize()
{
    // Procedure RENORME, figure E.8
    do
    {
        m_a <<= 1;
        m_c <<= 1;
        --m_ct;

        if (m_ct == 0)
        {
            byteOut();
        }
    }
    while ((m_a & 0x8000) == 0);
}

void PDFJBIG2ArithmeticEncoder::byteOut()
{
    // Procedure BYTEOUT, figure E.9. A byte 0xFF is followed by a byte, from which
    // one bit is stuffed, so a carry can never propagate beyond the byte B.
    if (m_b == 0xFF)
    {
        emitByte();
        m_b = m_c >> 20;
        m_c &= 0xFFFFF;
        m_ct = 7;
        return;
    }

    if (m_c >= 0x8000000)
    {
        // The carry bit is set - it is added to the byte B. The byte preceding the
        // output buffer can not receive a carry: the code register can not exceed
        // the initial interval before the first byte is moved out of it.
        Q_ASSERT(!m_isBeforeFirstByte);
        ++m_b;

        if (m_b == 0xFF)
        {
            m_c &= 0x7FFFFFF;
            emitByte();
            m_b = m_c >> 20;
            m_c &= 0xFFFFF;
            m_ct = 7;
            return;
        }
    }

    // The carry bit has been added to the byte B, so it is dropped from the register
    emitByte();
    m_b = (m_c >> 19) & 0xFF;
    m_c &= 0x7FFFF;
    m_ct = 8;
}

void PDFJBIG2ArithmeticEncoder::emitByte()
{
    if (m_isBeforeFirstByte)
    {
        m_isBeforeFirstByte = false;
        return;
    }

    Q_ASSERT(m_b <= 0xFF);
    m_output.push_back(static_cast<char>(static_cast<uint8_t>(m_b)));
}

PDFJBIG2ATPositions PDFJBIG2EncoderParameters::getNominalATPositions(uint8_t GBTEMPLATE)
{
    // Figures 4, 5 and 6 of the specification
    switch (GBTEMPLATE)
    {
        case 0:
            return { { { 3, -1 }, { -3, -1 }, { 2, -2 }, { -2, -2 } } };

        case 1:
            return { { { 3, -1 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } };

        case 2:
        case 3:
            return { { { 2, -1 }, { 0, 0 }, { 0, 0 }, { 0, 0 } } };

        default:
            break;
    }

    return { };
}

int PDFJBIG2EncoderParameters::getATPositionCount(uint8_t GBTEMPLATE)
{
    return (GBTEMPLATE == 0) ? 4 : 1;
}

uint8_t PDFJBIG2EncoderParameters::getContextBitCount(uint8_t GBTEMPLATE)
{
    // Figures 4 to 7 of the specification - 16, 13, 10 and 10 pixels
    return (GBTEMPLATE == 0) ? 16 : ((GBTEMPLATE == 1) ? 13 : 10);
}

PDFJBIG2Encoder::PDFJBIG2Encoder(const PDFBitonalBitmapView& bitmap, const PDFJBIG2EncoderParameters& parameters) :
    m_bitmap(bitmap),
    m_parameters(parameters)
{

}

QByteArray PDFJBIG2Encoder::encodeEmbeddedStream()
{
    validate();

    QByteArray stream;
    appendSegment(stream, 0, PageInformation, createPageInformationData());
    appendSegment(stream, 1, ImmediateGenericRegion, createGenericRegionData());
    return stream;
}

QByteArray PDFJBIG2Encoder::encodeFile()
{
    validate();

    // File header, see D.4 of the specification - the identifier, the flags (the
    // sequential organisation, the number of the pages is known) and the number
    // of the pages
    QByteArray stream("\x97\x4A\x42\x32\x0D\x0A\x1A\x0A", 8);
    stream.append(char(0x01));
    appendUInt32(stream, 1);

    appendSegment(stream, 0, PageInformation, createPageInformationData());
    appendSegment(stream, 1, ImmediateGenericRegion, createGenericRegionData());
    appendSegment(stream, 2, EndOfPage, QByteArray());
    appendSegment(stream, 3, EndOfFile, QByteArray());
    return stream;
}

QByteArray PDFJBIG2Encoder::encodeGenericRegion()
{
    validate();
    return m_parameters.MMR ? encodeGenericRegionMMR() : encodeGenericRegionArithmetic();
}

void PDFJBIG2Encoder::validate() const
{
    validate(m_bitmap, m_parameters);
}

void PDFJBIG2Encoder::validate(const PDFBitonalBitmapView& bitmap, const PDFJBIG2EncoderParameters& parameters)
{
    if (!bitmap.isValid())
    {
        throw PDFException(PDFTranslationContext::tr("Invalid bitonal image for the JBIG2 encoding."));
    }

    // The decoder allocates a byte per pixel, so the same limit is used here
    PDFJBIG2Bitmap::checkSize(bitmap.width, bitmap.height);

    if (parameters.MMR)
    {
        return;
    }

    if (parameters.GBTEMPLATE > 3)
    {
        throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 generic region template %1.").arg(parameters.GBTEMPLATE));
    }

    // An adaptive template pixel must refer to a pixel, which is decoded before the
    // current one, see figure 7 of the specification
    const int atCount = PDFJBIG2EncoderParameters::getATPositionCount(parameters.GBTEMPLATE);
    for (int i = 0; i < atCount; ++i)
    {
        const PDFJBIG2ATPosition& position = parameters.GBAT[i];
        if (position.y > 0 || (position.y == 0 && position.x >= 0))
        {
            throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 adaptive template pixel position A%1 = (%2, %3).").arg(i + 1).arg(position.x).arg(position.y));
        }
    }
}

QByteArray PDFJBIG2Encoder::encodeGenericRegionArithmetic() const
{
    PDFJBIG2ArithmeticDecoderState state;
    state.reset(PDFJBIG2EncoderParameters::getContextBitCount(m_parameters.GBTEMPLATE));

    PDFJBIG2ArithmeticEncoder encoder;
    encodeGenericBitmap(m_bitmap, m_parameters, encoder, state);
    return encoder.finish();
}

void PDFJBIG2Encoder::encodeGenericBitmap(const PDFBitonalBitmapView& bitmap,
                                          const PDFJBIG2EncoderParameters& parameters,
                                          PDFJBIG2ArithmeticEncoder& encoder,
                                          PDFJBIG2ArithmeticDecoderState& state,
                                          const PDFJBIG2Bitmap* skip)
{
    PDFJBIG2EncoderParameters arithmeticParameters = parameters;
    arithmeticParameters.MMR = false;
    validate(bitmap, arithmeticParameters);

    const int width = bitmap.width;
    const int height = bitmap.height;

    if (skip && (skip->getWidth() != width || skip->getHeight() != height))
    {
        throw PDFException(PDFTranslationContext::tr("Invalid size (%1 x %2) of the JBIG2 skip bitmap, the image size is %3 x %4.").arg(skip->getWidth()).arg(skip->getHeight()).arg(width).arg(height));
    }

    // The bitmap is unpacked into a byte per pixel, where 1 is a black pixel, as
    // the decoder does. The typical prediction then compares the rows directly.
    // A skipped pixel is zero, because the decoder does not decode it and sets it
    // to zero - the contexts of its neighbours must see the same value.
    std::vector<uint8_t> pixels(size_t(width) * size_t(height), 0);
    for (int y = 0; y < height; ++y)
    {
        uint8_t* row = pixels.data() + size_t(y) * size_t(width);
        for (int x = 0; x < width; ++x)
        {
            const bool isSkipped = skip && skip->getPixel(x, y);
            row[x] = (!isSkipped && bitmap.isPixelBlack(x, y)) ? 1 : 0;
        }
    }

    auto getPixel = [&pixels, width](int x, int y) -> uint32_t
    {
        if (x < 0 || x >= width || y < 0)
        {
            return 0;
        }

        return pixels[size_t(y) * size_t(width) + size_t(x)];
    };

    // The pixels of the template in the order of the bits of the context - the bit 0
    // is the first one. The order must match the decoder exactly, see figures 4-7
    // of the specification - the pixel above the row is the most significant bit
    // and the pixel to the left of the coded pixel is the least significant one.
    struct TemplatePixel
    {
        int x;
        int y;
    };

    std::vector<TemplatePixel> templatePixels;
    const PDFJBIG2ATPositions& AT = parameters.GBAT;
    uint16_t LTPContext = 0;

    if (parameters.GBTEMPLATE == 0)
    {
        templatePixels = { { -1, 0 }, { -2, 0 }, { -3, 0 }, { -4, 0 }, { AT[0].x, AT[0].y },
                           { 2, -1 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { -2, -1 }, { AT[1].x, AT[1].y },
                           { AT[2].x, AT[2].y }, { 1, -2 }, { 0, -2 }, { -1, -2 }, { AT[3].x, AT[3].y } };
        LTPContext = 0x9B25;

    }
    else if (parameters.GBTEMPLATE == 1)
    {
        templatePixels = { { -1, 0 }, { -2, 0 }, { -3, 0 }, { AT[0].x, AT[0].y },
                           { 2, -1 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { -2, -1 },
                           { 2, -2 }, { 1, -2 }, { 0, -2 }, { -1, -2 } };
        LTPContext = 0x0795;

    }
    else if (parameters.GBTEMPLATE == 2)
    {
        templatePixels = { { -1, 0 }, { -2, 0 }, { AT[0].x, AT[0].y },
                           { 1, -1 }, { 0, -1 }, { -1, -1 }, { -2, -1 },
                           { 1, -2 }, { 0, -2 }, { -1, -2 } };
        LTPContext = 0x00E5;

    }
    else
    {
        templatePixels = { { -1, 0 }, { -2, 0 }, { -3, 0 }, { -4, 0 }, { AT[0].x, AT[0].y },
                           { 1, -1 }, { 0, -1 }, { -1, -1 }, { -2, -1 }, { -3, -1 } };
        LTPContext = 0x0195;
    }

    // Typical prediction, see 6.2.5.7 of the specification. The decoder toggles LTP
    // by the decoded SLTP bit, so the bit is the change of the state - a row is typical,
    // when it is identical to the previous row, and the row before the first one is
    // all zeros.
    uint32_t LTP = 0;

    for (int y = 0; y < height; ++y)
    {
        const uint8_t* row = pixels.data() + size_t(y) * size_t(width);

        if (parameters.TPGDON)
        {
            bool isTypical = false;
            if (y > 0)
            {
                isTypical = std::memcmp(row, row - width, size_t(width)) == 0;
            }
            else
            {
                isTypical = std::all_of(row, row + width, [](uint8_t pixel) { return pixel == 0; });
            }

            const uint32_t SLTP = isTypical ? 1 : 0;
            encoder.encodeBit(LTPContext, &state, SLTP ^ LTP);
            LTP = SLTP;

            if (isTypical)
            {
                continue;
            }
        }

        for (int x = 0; x < width; ++x)
        {
            if (skip && skip->getPixel(x, y))
            {
                continue;
            }

            uint32_t context = 0;
            for (size_t i = 0; i < templatePixels.size(); ++i)
            {
                const TemplatePixel& templatePixel = templatePixels[i];
                context |= getPixel(x + templatePixel.x, y + templatePixel.y) << i;
            }

            encoder.encodeBit(context, &state, row[x]);
        }
    }
}

QByteArray PDFJBIG2Encoder::encodeGenericRegionMMR() const
{
    // The MMR coding is the pure two dimensional coding of ITU-T T.6 with the black
    // pixels having the value 1. The data of a generic region have a known length,
    // so EOFB is not needed, see 6.2.6 of the specification. The data must end at
    // a byte boundary, which the encoder does by itself.
    PDFCCITTFaxEncoderParameters parameters;
    parameters.K = -1;
    parameters.hasEndOfLine = false;
    parameters.hasEncodedByteAlign = false;
    parameters.hasEndOfBlock = false;

    PDFCCITTFaxEncoder encoder(m_bitmap, parameters);
    return encoder.encode();
}

QByteArray PDFJBIG2Encoder::createPageInformationData() const
{
    // See 7.4.8 of the specification
    QByteArray data;
    appendUInt32(data, uint32_t(m_bitmap.width));
    appendUInt32(data, uint32_t(m_bitmap.height));

    // Resolution of the page is unknown
    appendUInt32(data, 0);
    appendUInt32(data, 0);

    // Page segment flags, see 7.4.8.5 - the page is eventually lossless, it might
    // not contain refinements, the default pixel value is 0, the default combination
    // operator is OR, no auxiliary buffers are required and the combination operator
    // is not overridden by the regions
    data.append(char(0x01));

    // Page striping information, see 7.4.8.6 - the page is not striped
    data.append(char(0x00));
    data.append(char(0x00));

    return data;
}

QByteArray PDFJBIG2Encoder::createGenericRegionData()
{
    // See 7.4.6 of the specification. The region segment information field (7.4.1)
    // places the region at the top left corner of the page and combines it by OR.
    QByteArray data;
    appendUInt32(data, uint32_t(m_bitmap.width));
    appendUInt32(data, uint32_t(m_bitmap.height));
    appendUInt32(data, 0);
    appendUInt32(data, 0);
    data.append(char(0x00));

    // Generic region segment flags, see 7.4.6.2
    if (m_parameters.MMR)
    {
        data.append(char(0x01));
        data.append(encodeGenericRegionMMR());
    }
    else
    {
        uint8_t flags = uint8_t(m_parameters.GBTEMPLATE << 1);
        if (m_parameters.TPGDON)
        {
            flags |= 0x08;
        }
        data.append(char(flags));

        // Generic region segment AT flags, see 7.4.6.3
        const int atCount = PDFJBIG2EncoderParameters::getATPositionCount(m_parameters.GBTEMPLATE);
        for (int i = 0; i < atCount; ++i)
        {
            data.append(char(m_parameters.GBAT[i].x));
            data.append(char(m_parameters.GBAT[i].y));
        }

        data.append(encodeGenericRegionArithmetic());
    }

    return data;
}

void PDFJBIG2Encoder::appendSegment(QByteArray& stream, uint32_t segmentNumber, SegmentType type, const QByteArray& data)
{
    // Segment header, see 7.2 of the specification
    appendUInt32(stream, segmentNumber);

    // Segment header flags - the type of the segment, the page association is one
    // byte long and the segment is not deferred non-retain
    stream.append(char(type));

    // Referred-to segments - none, so the count and the retain bits are all zero
    stream.append(char(0x00));

    // Page association
    stream.append(char(0x01));

    // Segment data length
    appendUInt32(stream, uint32_t(data.size()));

    stream.append(data);
}

void PDFJBIG2Encoder::appendUInt32(QByteArray& stream, uint32_t value)
{
    stream.append(char((value >> 24) & 0xFF));
    stream.append(char((value >> 16) & 0xFF));
    stream.append(char((value >> 8) & 0xFF));
    stream.append(char(value & 0xFF));
}

}   // namespace pdf
