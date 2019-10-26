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
class PDFRenderErrorReporter;

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

enum class JBIG2SegmentType : uint32_t
{
    Invalid,
    SymbolDictionary,           ///< See chapter 7.4.2  in specification
    TextRegion,                 ///< See chapter 7.4.3  in specification
    PatternDictionary,          ///< See chapter 7.4.4  in specification
    HalftoneRegion,             ///< See chapter 7.4.5  in specification
    GenericRegion,              ///< See chapter 7.4.6  in specification
    GenericRefinementRegion,    ///< See chapter 7.4.7  in specification
    PageInformation,            ///< See chapter 7.4.8  in specification
    EndOfPage,                  ///< See chapter 7.4.9  in specification
    EndOfStripe,                ///< See chapter 7.4.10 in specification
    EndOfFile,                  ///< See chapter 7.4.11 in specification
    Profiles,                   ///< See chapter 7.4.12 in specification
    Tables,                     ///< See chapter 7.4.13 in specification
    Extension                   ///< See chapter 7.4.14 in specification
};

class PDFJBIG2SegmentHeader
{
public:
    explicit inline PDFJBIG2SegmentHeader() = default;

    /// Returns segment type
    inline JBIG2SegmentType getSegmentType() const { return m_segmentType; }

    /// Returns segment number
    inline uint32_t getSegmentNumber() const { return m_segmentNumber; }

    /// Returns segment data length (or 0xFFFFFFFF, if length is not defined)
    /// \sa isSegmentDataLengthDefined
    inline uint32_t getSegmentDataLength() const { return m_segmentDataLength; }

    /// Returns true, if segment is immediate (direct paint on page's bitmap)
    inline bool isImmediate() const { return m_immediate; }

    /// Returns true, if segment is lossless
    inline bool isLossless() const { return m_lossless; }

    /// Returns true, if segmend data length is defined
    inline bool isSegmentDataLengthDefined() const { return m_segmentDataLength != 0xFFFFFFFF; }

    /// Reads the segment header from the data stream. If error occurs, then
    /// exception is thrown.
    static PDFJBIG2SegmentHeader read(PDFBitReader* reader);

private:
    uint32_t m_segmentNumber = 0;
    uint32_t m_pageAssociation = 0;
    uint32_t m_segmentDataLength = 0;
    JBIG2SegmentType m_segmentType = JBIG2SegmentType::Invalid;
    bool m_immediate = false;
    bool m_lossless = false;
    std::vector<uint32_t> m_referredSegments;
};

/// Decoder of JBIG2 data streams. Decodes the black/white monochrome image.
/// Handles also global segments. Decoder decodes data using the specification
/// ISO/IEC 14492:2001, T.88.
class PDFJBIG2Decoder
{
public:
    explicit inline PDFJBIG2Decoder(QByteArray data, QByteArray globalData, PDFRenderErrorReporter* errorReporter) :
        m_data(qMove(data)),
        m_globalData(qMove(globalData)),
        m_errorReporter(errorReporter),
        m_reader(nullptr, 8)
    {

    }

    void decode();

private:
    /// Processes current data stream (reads all data from the stream, interprets
    /// them as segments and processes the segments).
    void processStream();

    void processSymbolDictionary(const PDFJBIG2SegmentHeader& header);
    void processTextRegion(const PDFJBIG2SegmentHeader& header);
    void processPatternDictionary(const PDFJBIG2SegmentHeader& header);
    void processHalftoneRegion(const PDFJBIG2SegmentHeader& header);
    void processGenericRegion(const PDFJBIG2SegmentHeader& header);
    void processGenericRefinementRegion(const PDFJBIG2SegmentHeader& header);
    void processPageInformation(const PDFJBIG2SegmentHeader& header);
    void processEndOfPage(const PDFJBIG2SegmentHeader& header);
    void processEndOfStripe(const PDFJBIG2SegmentHeader& header);
    void processEndOfFile(const PDFJBIG2SegmentHeader& header);
    void processProfiles(const PDFJBIG2SegmentHeader& header);
    void processCodeTables(const PDFJBIG2SegmentHeader& header);
    void processExtension(const PDFJBIG2SegmentHeader& header);

    QByteArray m_data;
    QByteArray m_globalData;
    PDFRenderErrorReporter* m_errorReporter;
    PDFBitReader m_reader;
};

}   // namespace pdf

#endif // PDFJBIG2DECODER_H
