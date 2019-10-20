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

#ifndef PDFJBIG2DECODER_H
#define PDFJBIG2DECODER_H

#include "pdfutils.h"

namespace pdf
{

/// Arithmetic decoder state for JBIG2 data streams. It contains state for context,
/// state is stored as 8-bit value, where only 7 bits are used. 6 bits are used
/// to store Qe value index (current row in the table, number 0-46), and lowest 1 bit
/// is used to store current MPS value (most probable symbol - 0/1).
class PDFJBIG2ArithmeticDecoderState
{
public:
    explicit inline PDFJBIG2ArithmeticDecoderState(size_t size) :
        m_state(size, 0)
    {

    }

    /// Returns row index to Qe value table, according to document ISO/IEC 14492:2001,
    /// annex E, table E.1 (Qe values and probability estimation process).
    inline uint8_t getQeRowIndex(size_t context) const
    {
        Q_ASSERT(context < m_state.size());
        return m_state[context] >> 1;
    }

    /// Returns current bit value of MPS (most probable symbol)
    inline uint8_t getMPS(size_t context) const
    {
        Q_ASSERT(context < m_state.size());
        return m_state[context] & 0x1;
    }

    /// Sets current row index to Qe value table, at given context, and also MPS bit value
    /// (most probable symbol).
    inline void setQeRowIndexAndMPS(size_t context, uint8_t QeRowIndex, uint8_t MPS)
    {
        Q_ASSERT(context < m_state.size());
        Q_ASSERT(MPS < 2);
        m_state[context] = (QeRowIndex << 1) + MPS;
    }

private:
    std::vector<uint8_t> m_state;
};

/// Arithmetic decoder for JBIG2 data streams. This arithmetic decoder is implementation
/// of decoder described in document ISO/IEC 14492:2001, T.88, annex G (arithmetic decoding
/// procedure). It uses 32-bit fixed point arithmetic instead of 16-bit fixed point
/// arithmetic described in the specification (it is much faster).
class PDFJBIG2ArithmeticDecoder
{
public:
    explicit inline PDFJBIG2ArithmeticDecoder() :
        m_c(0),
        m_a(0),
        m_ct(0),
        m_reader(nullptr)
    {

    }

    void initialize() { perform_INITDEC(); }

private:
    /// Performs INITDEC operation as described in the specification
    void perform_INITDEC();

    /// Performs BYTEIN operation as described in the specification
    void perform_BYTEIN();

    /// Performs DECODE operation as described in the specification
    /// \param context Context index
    /// \param state State of the arithmetic decoder
    /// \returns Single decoded bit (lowest bit, other bits are zero)
    uint32_t perform_DECODE(size_t context, PDFJBIG2ArithmeticDecoderState* state);

    /// This is 32 bit register consisting of two 16-bit subregisters - "c_high" and "c_low", as
    /// it is in specification. But we can work with it as 32 bit register (if we adjust some
    /// operations and fixed point arithmetic).
    uint32_t m_c;

    /// This is 32 bit register for interval range. In the specification, it is 16-bit register,
    /// but we use 32-bit fixed point arithmetic instead of 16-bit fixed point arithmetic.
    uint32_t m_a;

    /// Number of current unprocessed bits.
    uint32_t m_ct;

    /// Data source to read from
    PDFBitReader* m_reader;
};

class PDFJBIG2Decoder
{
public:
    PDFJBIG2Decoder();
};

}   // namespace pdf

#endif // PDFJBIG2DECODER_H
