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
#include "pdfccittfaxcodes.h"
#include "pdfexception.h"
#include "pdfdbgheap.h"

namespace pdf
{

PDFCCITTFaxDecoder::PDFCCITTFaxDecoder(const QByteArray* stream, const PDFCCITTFaxDecoderParameters& parameters) :
    m_reader(stream, 1),
    m_parameters(parameters)
{

}

PDFImageData PDFCCITTFaxDecoder::decode()
{
    PDFBitWriter writer(1);
    std::vector<int> codingLine;
    std::vector<int> referenceLine;

    int row = 0;
    const size_t lineSize = m_parameters.columns + 2;
    codingLine.resize(lineSize, m_parameters.columns);
    referenceLine.resize(lineSize, m_parameters.columns);
    bool isUsing2DEncoding = m_parameters.K < 0;
    codingLine[0] = 0;

    auto updateIsUsing2DEncoding = [this, &isUsing2DEncoding]()
    {
        if (m_parameters.K > 0)
        {
            // Mixed encoding
            isUsing2DEncoding = !m_reader.read(1);
        }
    };

    // The data can start with the fill and the end of line code word, whether they are
    // required or not
    skipFillAndEOL();
    updateIsUsing2DEncoding();

    while (!m_reader.isAtEnd())
    {
        int a0_index = 0;
        bool isCurrentPixelBlack = false;

        if (isUsing2DEncoding)
        {
            size_t b1_index = 0;

            // 2D encoding
            while (codingLine[a0_index] < m_parameters.columns)
            {
                CCITT_2D_Code_Mode mode = get2DMode();
                switch (mode)
                {
                    case Pass:
                    {
                        // In this mode, we set a0 to the b2 (from reference line). In pass mode,
                        // we do not change pixel color. Why we are adding 2 to the b1_index?
                        // We want to skip both b1, b2, because they will be left of new a0.
                        const size_t b2_index = b1_index + 1;
                        if (b2_index < referenceLine.size())
                        {
                            addPixels(codingLine, a0_index, referenceLine[b2_index], isCurrentPixelBlack, false);

                            if (referenceLine[b2_index] < m_parameters.columns)
                            {
                                b1_index += 2;

                                if (b1_index >= referenceLine.size())
                                {
                                    throw PDFException(PDFTranslationContext::tr("Invalid pass encoding data in CCITT stream."));
                                }
                            }
                        }
                        else
                        {
                            throw PDFException(PDFTranslationContext::tr("CCITT b2 index out of range."));
                        }

                        break;
                    }

                    case Horizontal:
                    {
                        // We scan two sequence length.
                        const int a0a1 = getRunLength(!isCurrentPixelBlack);
                        const int a1a2 = getRunLength(isCurrentPixelBlack);

                        addPixels(codingLine, a0_index, codingLine[a0_index] + a0a1, isCurrentPixelBlack, false);
                        addPixels(codingLine, a0_index, codingLine[a0_index] + a1a2, !isCurrentPixelBlack, false);

                        while (referenceLine[b1_index] <= codingLine[a0_index] && referenceLine[b1_index] < m_parameters.columns)
                        {
                            // We do not want to change the color (b1 should have opposite color of a0,
                            // should be first changing element of reference line right of a0).
                            b1_index += 2;

                            if (b1_index >= referenceLine.size())
                            {
                                throw PDFException(PDFTranslationContext::tr("Invalid horizontal encoding data in CCITT stream."));
                            }
                        }

                        break;
                    }

                    case Vertical_3L:
                    case Vertical_2L:
                    case Vertical_1L:
                    case Vertical_0:
                    case Vertical_1R:
                    case Vertical_2R:
                    case Vertical_3R:
                    {
                        const int32_t a1 = static_cast<int32_t>(referenceLine[b1_index]) + mode - static_cast<int32_t>(Vertical_0);

                        if (a1 < 0 || a1 > m_parameters.columns)
                        {
                            throw PDFException(PDFTranslationContext::tr("Invalid vertical encoding data in CCITT stream."));
                        }

                        const bool isNegativeOffset = mode < Vertical_0;
                        addPixels(codingLine, a0_index, static_cast<uint32_t>(a1), isCurrentPixelBlack, isNegativeOffset);
                        isCurrentPixelBlack = !isCurrentPixelBlack;

                        if (codingLine[a0_index] < m_parameters.columns)
                        {
                            // We must upgrade b1 index in such a way that it is first index
                            // of opposite color, than a0 index. If we are using negative offsets, then
                            // current position can move backward, and so we must look for first b1 index,
                            // which is of opposite color, than a0. So we decrease index by 1. But what to do,
                            // if we have b1 index equal to zero? In this case, we add -1 + 2 = 1 index, so we do it in
                            // same way, as positive/zero shift.
                            b1_index += (isNegativeOffset && b1_index > 0) ? -1 : 1;

                            // Why we have this check, if same check is in while cycle? Because if we are adding
                            // to the b1_index, we can go outside of reference line range.
                            if (b1_index >= referenceLine.size())
                            {
                                throw PDFException(PDFTranslationContext::tr("Invalid vertical encoding data in CCITT stream."));
                            }

                            while (referenceLine[b1_index] <= codingLine[a0_index] && referenceLine[b1_index] < m_parameters.columns)
                            {
                                // We do not want to change the color (b1 should have opposite color of a0,
                                // should be first changing element of reference line right of a0).
                                b1_index += 2;

                                if (b1_index >= referenceLine.size())
                                {
                                    throw PDFException(PDFTranslationContext::tr("Invalid vertical encoding data in CCITT stream."));
                                }
                            }
                        }

                        break;
                    }

                    default:
                        Q_ASSERT(false);
                        break;
                }
            }
        }
        else
        {
            // Simple 1D encoding
            while (codingLine[a0_index] < m_parameters.columns)
            {
                const uint32_t sequenceLength = getRunLength(!isCurrentPixelBlack);
                addPixels(codingLine, a0_index, codingLine[a0_index] + sequenceLength, isCurrentPixelBlack, false);
                isCurrentPixelBlack = !isCurrentPixelBlack;
            }
        }

        // Write the line to the output buffer
        isCurrentPixelBlack = false;
        int index = 0;
        for (int i = 0; i < m_parameters.columns; ++i)
        {
            if (i == codingLine[index])
            {
                isCurrentPixelBlack = !isCurrentPixelBlack;
                ++index;
            }

            writer.write(isCurrentPixelBlack ? 0 : 1);
        }
        writer.finishLine();

        ++row;

        // Check if we have reached desired number of rows (and end-of-block mode
        // is not set). If yes, then break the reading.
        if (!m_parameters.hasEndOfBlock && row == m_parameters.rows)
        {
            // We have reached number of rows, stop reading the data
            break;
        }

        bool foundEndOfLine = false;
        if (m_parameters.hasEndOfLine)
        {
            // End of line is required, try to scan it (until end of stream is reached).
            while (!m_reader.isAtEnd())
            {
                if (m_reader.look(12) == 1)
                {
                    m_reader.read(12);
                    foundEndOfLine = true;
                    break;
                }
                else
                {
                    m_reader.read(1);
                }
            }
        }
        else if (!m_parameters.hasEncodedByteAlign)
        {
            // Skip fill zeros and possibly find EOL
            foundEndOfLine = skipFillAndEOL();
        }

        // If end of line is found, be do not perform align to bytes (end of line
        // has perference against byte align)
        if (m_parameters.hasEncodedByteAlign && !foundEndOfLine)
        {
            m_reader.alignToBytes();

            // An end of line can start at the byte boundary - the end of block is written
            // this way by the byte aligned streams. It must be recognized before the tag
            // bit of the mixed encoding is read, because the tag bit follows the end of
            // line. No code word starts by twelve zero bits, so the data of a row can not
            // be mistaken for the fill or for the end of line.
            if (!m_parameters.hasEndOfLine)
            {
                foundEndOfLine = skipFillAndEOL();
            }
        }

        if (m_reader.isAtEnd())
        {
            // Have we finished reading?
            break;
        }

        updateIsUsing2DEncoding();

        if (m_parameters.hasEndOfBlock && foundEndOfLine)
        {
            // The end of block consists of consecutive end of line code words (see the
            // specification) - the first one has been read above, so the second one
            // marks the end of the block
            if (m_reader.look(12) == 1)
            {
                // End of block found, stop reading the data
                break;
            }
        }

        std::swap(codingLine, referenceLine);
        std::fill(codingLine.begin(), codingLine.end(), m_parameters.columns);
        std::fill(std::next(referenceLine.begin(), a0_index + 1), referenceLine.end(), m_parameters.columns);
        codingLine[0] = 0;
    }

    Q_ASSERT(m_parameters.decode.size() == 2);
    std::vector<PDFReal> decode;
    if (m_parameters.hasBlackIsOne)
    {
        decode = { m_parameters.decode[1], m_parameters.decode[0] };
    }
    else
    {
        decode = { m_parameters.decode[0], m_parameters.decode[1] };
    }

    return PDFImageData(1, 1, m_parameters.columns, row, (m_parameters.columns + 7) / 8, m_parameters.maskingType, writer.takeByteArray(), { }, qMove(decode), { });
}

void PDFCCITTFaxDecoder::skipFill()
{
    // This functions skips zero bits (because codewords have at most 12 bits,
    // we use 12 bit lookahead to ensure, that we do not broke data sequence).

    while (!m_reader.isAtEnd() && m_reader.look(12) == 0)
    {
        m_reader.read(1);
    }
}

bool PDFCCITTFaxDecoder::skipEOL()
{
    if (m_reader.look(12) == 1)
    {
        m_reader.read(12);
        return true;
    }

    return false;
}

void PDFCCITTFaxDecoder::addPixels(std::vector<int>& line, int& a0_index, int a1, bool isCurrentPixelBlack, bool isA1LeftOfA0Allowed)
{
    if (a1 > line[a0_index])
    {
        if (a1 > m_parameters.columns)
        {
            throw PDFException(PDFTranslationContext::tr("Invalid index of CCITT changing element a1: a1 = %1, columns = %2.").arg(a1).arg(m_parameters.columns));
        }

        // If we are changing the color, increment a0_index. a0_index == 0 is white, a0_index == 1 is black, etc.,
        // sequence of white and black runs alternates.
        if ((a0_index & 1) != isCurrentPixelBlack)
        {
            ++a0_index;
        }

        line[a0_index] = a1;
    }
    else if (isA1LeftOfA0Allowed && a1 < line[a0_index])
    {
        // We want to find first index, for which it holds:
        //  a1 > line[a0_index - 1], so if we set line[a0_index] = a1,
        // then we get a valid increasing sequence.
        while (a0_index > 0 && a1 <= line[a0_index - 1])
        {
            --a0_index;
        }
        line[a0_index] = a1;
    }
}

uint32_t PDFCCITTFaxDecoder::getRunLength(bool white)
{
    uint32_t value = 0;

    while (true)
    {
        uint32_t currentValue = 0;
        if (white)
        {
            currentValue = getWhiteCode();
        }
        else
        {
            currentValue = getBlackCode();
        }
        value += currentValue;

        if (currentValue < 64)
        {
            break;
        }
    }

    return value;
}

uint32_t PDFCCITTFaxDecoder::getWhiteCode()
{
    return getCode(CCITT_WHITE_CODES, std::size(CCITT_WHITE_CODES));
}

uint32_t PDFCCITTFaxDecoder::getBlackCode()
{
    return getCode(CCITT_BLACK_CODES, std::size(CCITT_BLACK_CODES));
}

uint32_t PDFCCITTFaxDecoder::getCode(const PDFCCITTCode* codes, size_t codeCount)
{
    uint32_t code = 0;
    uint8_t bits = 0;

    while (bits <= MAX_CODE_BIT_LENGTH)
    {
        code = (code << 1) + m_reader.read(1);
        ++bits;

        for (size_t i = 0; i < codeCount; ++i)
        {
            const PDFCCITTCode& currentCode = codes[i];
            if (currentCode.bits == bits && currentCode.code == code)
            {
                return currentCode.length;
            }
        }
    }

    throw PDFException(PDFTranslationContext::tr("Invalid CCITT run length code word."));
}

CCITT_2D_Code_Mode PDFCCITTFaxDecoder::get2DMode()
{
    uint32_t code = 0;
    uint8_t bits = 0;

    while (bits <= MAX_2D_MODE_BIT_LENGTH)
    {
        code = (code << 1) + m_reader.read(1);
        ++bits;

        for (const PDFCCITT2DModeInfo& info : CCITT_2D_CODE_MODES)
        {
            if (info.bits == bits && info.code == code)
            {
                return info.mode;
            }
        }
    }

    throw PDFException(PDFTranslationContext::tr("Invalid CCITT 2D mode."));
}

}   // namespace pdf
