//    Copyright (C) 2019 Jakub Melka
//
//    This file is part of PdfForQt.
//
//    PdfForQt is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    PdfForQt is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDFForQt.  If not, see <https://www.gnu.org/licenses/>.

#include "pdfjbig2decoder.h"

namespace pdf
{

struct PDFJBIG2ArithmeticDecoderQeValue
{
    uint32_t Qe;        ///< Value of Qe
    uint8_t newMPS;     ///< New row if MPS (more probable symbol)
    uint8_t newLPS;     ///< New row if LPS (less probable symbol)
    uint8_t switchFlag; ///< Meaning of MPS/LPS is switched
};

static constexpr PDFJBIG2ArithmeticDecoderQeValue JBIG2_ARITHMETIC_DECODER_QE_VALUES[] =
{
    { 0x56010000, 1,   1, 1 },
    { 0x34010000, 2,   6, 0 },
    { 0x18010000, 3,   9, 0 },
    { 0x0AC10000, 4,  12, 0 },
    { 0x05210000, 5,  29, 0 },
    { 0x02210000, 38, 33, 0 },
    { 0x56010000, 7,   6, 1 },
    { 0x54010000, 8,  14, 0 },
    { 0x48010000, 9,  14, 0 },
    { 0x38010000, 10, 14, 0 },
    { 0x30010000, 11, 17, 0 },
    { 0x24010000, 12, 18, 0 },
    { 0x1C010000, 13, 20, 0 },
    { 0x16010000, 29, 21, 0 },
    { 0x56010000, 15, 14, 1 },
    { 0x54010000, 16, 14, 0 },
    { 0x51010000, 17, 15, 0 },
    { 0x48010000, 18, 16, 0 },
    { 0x38010000, 19, 17, 0 },
    { 0x34010000, 20, 18, 0 },
    { 0x30010000, 21, 19, 0 },
    { 0x28010000, 22, 19, 0 },
    { 0x24010000, 23, 20, 0 },
    { 0x22010000, 24, 21, 0 },
    { 0x1C010000, 25, 22, 0 },
    { 0x18010000, 26, 23, 0 },
    { 0x16010000, 27, 24, 0 },
    { 0x14010000, 28, 25, 0 },
    { 0x12010000, 29, 26, 0 },
    { 0x11010000, 30, 27, 0 },
    { 0x0AC10000, 31, 28, 0 },
    { 0x09C10000, 32, 29, 0 },
    { 0x08A10000, 33, 30, 0 },
    { 0x05210000, 34, 31, 0 },
    { 0x04410000, 35, 32, 0 },
    { 0x02A10000, 36, 33, 0 },
    { 0x02210000, 37, 34, 0 },
    { 0x01410000, 38, 35, 0 },
    { 0x01110000, 39, 36, 0 },
    { 0x00850000, 40, 37, 0 },
    { 0x00490000, 41, 38, 0 },
    { 0x00250000, 42, 39, 0 },
    { 0x00150000, 43, 40, 0 },
    { 0x00090000, 44, 41, 0 },
    { 0x00050000, 45, 42, 0 },
    { 0x00010000, 45, 43, 0 },
    { 0x56010000, 46, 46, 0 }
};

void PDFJBIG2ArithmeticDecoder::perform_INITDEC()
{
    // Used figure G.1, in annex G, of specification
    uint32_t B = m_reader->read(8);
    m_c = (B ^ 0xFF) << 16;
    perform_BYTEIN();
    m_c = m_c << 7;
    m_ct -= 7;
    m_a = 0x80000000;
}

void PDFJBIG2ArithmeticDecoder::perform_BYTEIN()
{
    // Used figure G.3, in annex G, of specification
    const uint32_t B = m_reader->read(8);
    if (B == 0xFF)
    {
        const uint32_t B1 = m_reader->look(8);
        if (B1 > 0x8F)
        {
            m_ct = 8;
        }
        else
        {
            m_c = m_c + (0xFE00 - (B << 9));
            m_ct = 7;
            m_reader->read(8);
        }
    }
    else
    {
        m_c = m_c + (0xFF00 - (B << 8));
        m_ct = 8;
    }
}

uint32_t PDFJBIG2ArithmeticDecoder::perform_DECODE(size_t context, PDFJBIG2ArithmeticDecoderState* state)
{
    // Used figure G.2, in annex G, of specification
    const uint8_t QeRowIndex = state->getQeRowIndex(context);
    uint8_t MPS = state->getMPS(context);
    uint8_t D = MPS;

    // Sanity checks
    Q_ASSERT(QeRowIndex < std::size(JBIG2_ARITHMETIC_DECODER_QE_VALUES));
    Q_ASSERT(MPS < 2);

    const PDFJBIG2ArithmeticDecoderQeValue& QeInfo = JBIG2_ARITHMETIC_DECODER_QE_VALUES[QeRowIndex];
    const uint32_t Qe = QeInfo.Qe;
    m_a -= Qe;

    if (m_c < m_a)
    {
        if ((m_a & 0x80000000) == 0)
        {
            // We must perform MPS_EXCHANGE algorithm, according to figure E.16, in annex E, of specification
            if (m_a < Qe)
            {
                D = 1 - MPS;
                if (QeInfo.switchFlag)
                {
                    MPS = 1 - MPS;
                }

                state->setQeRowIndexAndMPS(context, QeInfo.newLPS, MPS);
            }
            else
            {
                state->setQeRowIndexAndMPS(context, QeInfo.newMPS, MPS);
            }
        }
        else
        {
            // Do nothing, we are finished
            return D;
        }
    }
    else
    {
        m_c -= m_a;
        m_a = Qe;

        // We must perform LPS_EXCHANGE algorithm, according to figure E.17, in annex E, of specification
        if (m_a < Qe)
        {
            state->setQeRowIndexAndMPS(context, QeInfo.newMPS, MPS);
        }
        else
        {
            D = 1 - MPS;
            if (QeInfo.switchFlag)
            {
                MPS = 1 - MPS;
            }
            state->setQeRowIndexAndMPS(context, QeInfo.newLPS, MPS);
        }
    }

    // Perform RENORMD algorithm, according to figure E.18, in annex E, of specification
    do
    {
        if (m_ct == 0)
        {
            perform_BYTEIN();
        }

        m_a = m_a << 1;
        m_c = m_c << 1;
        --m_ct;
    }
    while ((m_a & 0x80000000) == 0);

    return D;
}

PDFJBIG2Decoder::PDFJBIG2Decoder()
{

}

}   // namespace pdf
