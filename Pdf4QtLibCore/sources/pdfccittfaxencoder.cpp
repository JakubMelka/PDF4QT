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

#include "pdfccittfaxencoder.h"
#include "pdfccittfaxcodes.h"
#include "pdfexception.h"
#include "pdfdbgheap.h"

#include <algorithm>
#include <cstdlib>

namespace pdf
{

/// The longest run length, which has its own make-up code word. A longer run is coded
/// by this code word repeated as many times as needed, see 4.1.1 of ITU-T T.4.
static constexpr int CCITT_MAX_MAKEUP_RUN_LENGTH = 2560;

/// Length of the end-of-line code word 000000000001
static constexpr int CCITT_EOL_BIT_LENGTH = 12;

bool PDFBitonalBitmapView::isValid() const
{
    return data && width > 0 && height > 0 && stride >= (width + 7) / 8;
}

void PDFBitonalBitmapView::getChangingElements(int y, std::vector<int>& changingElements) const
{
    changingElements.clear();

    const uint8_t* row = data + size_t(y) * size_t(stride);
    bool isBlack = false;
    int x = 0;

    while (x < width)
    {
        if ((x & 0x07) == 0 && x + 8 <= width)
        {
            // A whole byte of the pixels of the current colour is skipped at once
            const uint8_t byteOfCurrentColour = (isBlack == isOneBlack) ? 0xFF : 0x00;
            if (row[x >> 3] == byteOfCurrentColour)
            {
                x += 8;
                continue;
            }
        }

        const bool isCurrentPixelBlack = isPixelBlack(x, y);
        if (isCurrentPixelBlack != isBlack)
        {
            changingElements.push_back(x);
            isBlack = isCurrentPixelBlack;
        }

        ++x;
    }

    changingElements.push_back(width);
    changingElements.push_back(width);
    changingElements.push_back(width);
}

PDFCCITTFaxEncoder::PDFCCITTFaxEncoder(const PDFBitonalBitmapView& bitmap, const PDFCCITTFaxEncoderParameters& parameters) :
    m_bitmap(bitmap),
    m_parameters(parameters)
{

}

QByteArray PDFCCITTFaxEncoder::encode()
{
    if (!m_bitmap.isValid())
    {
        throw PDFException(PDFTranslationContext::tr("Invalid bitonal image for the CCITT fax encoding."));
    }

    m_output.clear();
    m_output.reserve(m_bitmap.stride * m_bitmap.height / 4 + 16);
    m_bitBuffer = 0;
    m_bitsInBuffer = 0;

    const bool isGroup4 = m_parameters.K < 0;
    const bool isMixed = m_parameters.K > 0;
    const bool hasEndOfLine = !isGroup4 && m_parameters.hasEndOfLine;

    std::vector<int> codingLine;
    std::vector<int> referenceLine;

    // The reference line of the first row is an imaginary white row above the image,
    // see 4.2.1.3.1 of ITU-T T.4 - it has no changing elements
    referenceLine.assign(3, m_bitmap.width);

    for (int y = 0; y < m_bitmap.height; ++y)
    {
        m_bitmap.getChangingElements(y, codingLine);

        if (m_parameters.hasEncodedByteAlign)
        {
            alignToByte();
        }

        if (hasEndOfLine)
        {
            writeEndOfLine();
        }

        bool isTwoDimensional = isGroup4;
        if (isMixed)
        {
            // At most K - 1 rows coded two dimensionally follow a row coded one
            // dimensionally, see 4.2.1.1 of ITU-T T.4
            isTwoDimensional = (y % m_parameters.K) != 0;
            writeRowTag(isTwoDimensional);
        }

        if (isTwoDimensional)
        {
            encodeRow2D(codingLine, referenceLine);
        }
        else
        {
            encodeRow1D(codingLine);
        }

        std::swap(codingLine, referenceLine);
    }

    if (m_parameters.hasEndOfBlock)
    {
        writeEndOfBlock();
    }

    alignToByte();
    return m_output;
}

void PDFCCITTFaxEncoder::writeBits(uint32_t code, int bitCount)
{
    Q_ASSERT(bitCount > 0 && bitCount <= 16);
    Q_ASSERT((code >> bitCount) == 0);

    m_bitBuffer = (m_bitBuffer << bitCount) | code;
    m_bitsInBuffer += bitCount;

    while (m_bitsInBuffer >= 8)
    {
        m_bitsInBuffer -= 8;
        m_output.push_back(static_cast<char>(static_cast<uint8_t>((m_bitBuffer >> m_bitsInBuffer) & 0xFF)));
    }
}

void PDFCCITTFaxEncoder::alignToByte()
{
    if (m_bitsInBuffer > 0)
    {
        writeBits(0, 8 - m_bitsInBuffer);
    }

    Q_ASSERT(m_bitsInBuffer == 0);
}

void PDFCCITTFaxEncoder::writeEndOfLine()
{
    writeBits(1, CCITT_EOL_BIT_LENGTH);
}

void PDFCCITTFaxEncoder::writeRowTag(bool isTwoDimensional)
{
    // A single bit after the end-of-line code word - 1 for a row coded one
    // dimensionally and 0 for a row coded two dimensionally, see 4.2.1.3.2 of ITU-T T.4
    writeBits(isTwoDimensional ? 0 : 1, 1);
}

void PDFCCITTFaxEncoder::writeRun(int length, bool isWhite)
{
    Q_ASSERT(length >= 0);

    while (length >= CCITT_MAX_MAKEUP_RUN_LENGTH)
    {
        writeRunCode(CCITT_MAX_MAKEUP_RUN_LENGTH, isWhite);
        length -= CCITT_MAX_MAKEUP_RUN_LENGTH;
    }

    if (length >= 64)
    {
        // Make-up code word of the largest multiple of 64 not exceeding the length
        writeRunCode(length & ~63, isWhite);
        length &= 63;
    }

    // Terminating code word, it is always present - a run, which is a multiple of 64,
    // is terminated by the code word of the zero length
    writeRunCode(length, isWhite);
}

void PDFCCITTFaxEncoder::writeRunCode(int length, bool isWhite)
{
    // The tables contain the terminating code words of the lengths 0-63, followed by
    // the make-up code words of the multiples of 64 up to 2560
    Q_ASSERT(length >= 0 && length <= CCITT_MAX_MAKEUP_RUN_LENGTH);
    Q_ASSERT(length < 64 || (length % 64) == 0);

    const size_t index = (length < 64) ? size_t(length) : size_t(63 + length / 64);
    const PDFCCITTCode& code = isWhite ? CCITT_WHITE_CODES[index] : CCITT_BLACK_CODES[index];
    Q_ASSERT(code.length == length);

    writeBits(code.code, code.bits);
}

void PDFCCITTFaxEncoder::writeMode(int mode)
{
    Q_ASSERT(mode >= Pass && mode <= Vertical_3R);

    const PDFCCITT2DModeInfo& info = CCITT_2D_CODE_MODES[mode];
    Q_ASSERT(info.mode == mode);

    writeBits(info.code, info.bits);
}

void PDFCCITTFaxEncoder::encodeRow1D(const std::vector<int>& codingLine)
{
    // The runs of a row alternate, starting with a white run, which is empty, when
    // the row starts with a black pixel, see 4.1.1 of ITU-T T.4
    int position = 0;
    bool isWhite = true;
    size_t index = 0;

    do
    {
        const int nextPosition = codingLine[index++];
        writeRun(nextPosition - position, isWhite);
        position = nextPosition;
        isWhite = !isWhite;
    }
    while (position < m_bitmap.width);
}

void PDFCCITTFaxEncoder::encodeRow2D(const std::vector<int>& codingLine, const std::vector<int>& referenceLine)
{
    // Coding modes of 4.2.1.3 of ITU-T T.4. The changing element a0 starts at an
    // imaginary white pixel before the row, a1 is the next changing element of the
    // coding line, b1 is the first changing element of the reference line to the
    // right of a0 with the colour opposite to the colour of a0, and b2 is the changing
    // element following b1. The index of a changing element in the list gives its
    // colour - the elements at the even indices change the colour to black.
    const int width = m_bitmap.width;
    int a0 = -1;
    size_t a1Index = 0;
    size_t referenceIndex = 0;

    while (a0 < width)
    {
        const int a1 = codingLine[a1Index];

        // The first changing element of the reference line to the right of a0. It
        // never moves back, because a0 only grows. The element of the required colour
        // is either this element or the next one, but the parity is not stored in the
        // index - a vertical mode changes the colour, and the element skipped now can
        // be the b1 of the next mode.
        while (referenceLine[referenceIndex] <= a0)
        {
            ++referenceIndex;
        }

        const size_t b1Index = ((referenceIndex & 1) == (a1Index & 1)) ? referenceIndex : referenceIndex + 1;
        const int b1 = referenceLine[b1Index];
        const int b2 = referenceLine[b1Index + 1];

        if (b2 < a1)
        {
            // Pass mode - a0 is moved under b2 and the colour is not changed
            writeMode(Pass);
            a0 = b2;
        }
        else if (std::abs(a1 - b1) <= 3)
        {
            // Vertical mode - a1 is coded relatively to b1 and the colour changes
            writeMode(Vertical_0 + (a1 - b1));
            a0 = a1;
            ++a1Index;
        }
        else
        {
            // Horizontal mode - two runs a0a1 and a1a2 are coded by the one dimensional
            // code words. At the start of the row, a0 is the imaginary element before
            // the row, and the first run is measured from the first pixel instead.
            const int a2 = codingLine[a1Index + 1];
            const bool isWhite = (a1Index & 1) == 0;

            writeMode(Horizontal);
            writeRun(a1 - std::max(a0, 0), isWhite);
            writeRun(a2 - a1, !isWhite);

            a0 = a2;
            a1Index += 2;
        }
    }
}

void PDFCCITTFaxEncoder::writeEndOfBlock()
{
    if (m_parameters.hasEncodedByteAlign)
    {
        alignToByte();
    }

    if (m_parameters.K < 0)
    {
        // End-of-facsimile-block of ITU-T T.6 - two end-of-line code words
        writeEndOfLine();
        writeEndOfLine();
    }
    else
    {
        // Return-to-control of ITU-T T.4 - six end-of-line code words, each followed
        // by the tag bit 1 in the mixed encoding
        for (int i = 0; i < 6; ++i)
        {
            writeEndOfLine();

            if (m_parameters.K > 0)
            {
                writeBits(1, 1);
            }
        }
    }
}

}   // namespace pdf
