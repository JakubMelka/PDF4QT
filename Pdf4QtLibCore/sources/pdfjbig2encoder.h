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

#ifndef PDFJBIG2ENCODER_H
#define PDFJBIG2ENCODER_H

#include "pdfjbig2decoder.h"
#include "pdfccittfaxencoder.h"

#include <QByteArray>

namespace pdf
{

/// Arithmetic encoder of JBIG2 data streams - the MQ coder described in the annex E of
/// ISO/IEC 14492:2001 (ITU-T T.88). It is the counterpart of \p PDFJBIG2ArithmeticDecoder
/// and it shares the state of the contexts with it, so the probability estimation of
/// both is driven by the same table. The encoder works with the 16-bit registers of the
/// specification, because its output must match the specification byte by byte.
class PDF4QTLIBCORESHARED_EXPORT PDFJBIG2ArithmeticEncoder
{
public:
    /// Initializes the encoder (procedure INITENC)
    explicit PDFJBIG2ArithmeticEncoder();

    /// Encodes a single decision (procedure CODEMPS or CODELPS)
    /// \param context Context of the decision
    /// \param state State of the contexts, which is updated by the coded decision
    /// \param bit Coded decision, 0 or 1
    void encodeBit(size_t context, PDFJBIG2ArithmeticDecoderState* state, uint32_t bit);

    /// Terminates the encoding (procedure FLUSH) and returns the encoded data, which
    /// end with the marker 0xFF 0xAC. The encoder can not be used after this call.
    QByteArray finish();

private:
    /// Procedure RENORME
    void renormalize();

    /// Procedure BYTEOUT, which moves a byte of the code register into the byte B
    void byteOut();

    /// Advances the pointer of the output buffer. The byte B is written into the
    /// buffer, unless it is the imaginary byte preceding the buffer, which the
    /// specification uses to start the encoder.
    void emitByte();

    QByteArray m_output;

    /// Code register
    uint32_t m_c = 0;

    /// Interval register
    uint32_t m_a = 0x8000;

    /// Number of the shifts before the next byte is moved out of the code register
    uint32_t m_ct = 12;

    /// Byte B pointed to by the buffer pointer - the byte, which is going to be
    /// written into the output as the next one. A carry can still change it.
    uint32_t m_b = 0;

    /// True, if the byte B is the imaginary byte preceding the output buffer
    bool m_isBeforeFirstByte = true;

    /// True, if the encoding has been terminated
    bool m_isFinished = false;
};

/// Parameters of the encoding of a bitmap by the generic region decoding procedure,
/// see 6.2 of the specification
struct PDF4QTLIBCORESHARED_EXPORT PDFJBIG2EncoderParameters
{
    /// Use the MMR coding (ITU-T T.6) instead of the arithmetic coding. The template
    /// and the typical prediction are not used with MMR.
    bool MMR = false;

    /// Use typical prediction - a row identical to the previous one is coded by a
    /// single decision
    bool TPGDON = true;

    /// Template of the arithmetic coding, 0-3. The template 0 uses the largest
    /// context and gives the best compression.
    uint8_t GBTEMPLATE = 0;

    /// Positions of the adaptive template pixels. The template 0 uses all four of
    /// them, the other templates use only the first one. The default values are the
    /// nominal positions of the template 0, see \p getNominalATPositions.
    PDFJBIG2ATPositions GBAT = { { { 3, -1 }, { -3, -1 }, { 2, -2 }, { -2, -2 } } };

    /// Returns the nominal positions of the adaptive template pixels of a template,
    /// see 6.2.5.4 of the specification. The unused positions are zero.
    /// \param GBTEMPLATE Template, 0-3
    static PDFJBIG2ATPositions getNominalATPositions(uint8_t GBTEMPLATE);

    /// Returns the number of the adaptive template pixels used by a template
    /// \param GBTEMPLATE Template, 0-3
    static int getATPositionCount(uint8_t GBTEMPLATE);
};

/// Encoder of bitonal images into the JBIG2 format, described in ISO/IEC 14492:2001
/// (ITU-T T.88). The whole image is coded as a single generic region - no symbols
/// are detected, so the coding is always lossless. The encoder produces either the
/// embedded stream organisation used by the JBIG2Decode filter of PDF, or a stand
/// alone JBIG2 file with a single page.
class PDF4QTLIBCORESHARED_EXPORT PDFJBIG2Encoder
{
public:
    /// \param bitmap Encoded image, it must outlive the encoder
    /// \param parameters Parameters of the encoding
    explicit PDFJBIG2Encoder(const PDFBitonalBitmapView& bitmap, const PDFJBIG2EncoderParameters& parameters);

    /// Encodes the image into the embedded stream organisation of the PDF filter
    /// JBIG2Decode (see 7.4.7 of the PDF specification) - a page information segment
    /// followed by an immediate generic region segment covering the whole page. Both
    /// segments are associated with the page 1 and no global segments are needed.
    /// Throws \p PDFException, if the image or the parameters are invalid.
    QByteArray encodeEmbeddedStream();

    /// Encodes the image into a JBIG2 file with the sequential organisation and a single
    /// page, see the annex D of the specification. The file contains the file header,
    /// the segments of \p encodeEmbeddedStream, an end of page segment and an end of
    /// file segment. Throws \p PDFException, if the image or the parameters are invalid.
    QByteArray encodeFile();

    /// Encodes only the bitmap by the generic region decoding procedure, see 6.2 of the
    /// specification, and returns the coded data without any segment header. The data
    /// of the arithmetic coding are terminated by the marker 0xFF 0xAC, the data of the
    /// MMR coding are not terminated by EOFB - the length of the data is known.
    /// Throws \p PDFException, if the image or the parameters are invalid.
    QByteArray encodeGenericRegion();

private:
    /// Segment types used by the encoder, see 7.3 of the specification
    enum SegmentType : uint8_t
    {
        ImmediateGenericRegion = 38,
        PageInformation = 48,
        EndOfPage = 49,
        EndOfFile = 51
    };

    /// Checks the image and the parameters, throws \p PDFException, if they are invalid
    void validate() const;

    /// Encodes the bitmap by the arithmetic coding, see 6.2.5.7 of the specification
    QByteArray encodeGenericRegionArithmetic() const;

    /// Encodes the bitmap by the MMR coding, see 6.2.6 of the specification
    QByteArray encodeGenericRegionMMR() const;

    /// Creates the data part of the page information segment, see 7.4.8
    QByteArray createPageInformationData() const;

    /// Creates the data part of the generic region segment, see 7.4.6
    QByteArray createGenericRegionData();

    /// Appends a segment - its header (see 7.2) and its data. The segment refers to
    /// no other segments and it is associated with the page 1.
    /// \param stream Stream, into which the segment is appended
    /// \param segmentNumber Number of the segment
    /// \param type Type of the segment
    /// \param data Data part of the segment
    static void appendSegment(QByteArray& stream, uint32_t segmentNumber, SegmentType type, const QByteArray& data);

    /// Appends a 32-bit unsigned integer, the most significant byte first
    static void appendUInt32(QByteArray& stream, uint32_t value);

    PDFBitonalBitmapView m_bitmap;
    PDFJBIG2EncoderParameters m_parameters;
};

}   // namespace pdf

#endif // PDFJBIG2ENCODER_H
