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

#ifndef PDFCCITTFAXENCODER_H
#define PDFCCITTFAXENCODER_H

#include "pdfglobal.h"

#include <QByteArray>

#include <cstdint>
#include <vector>

namespace pdf
{

/// A read-only view of a bitonal image, whose rows are packed - one bit per pixel and
/// the most significant bit of a byte is the leftmost pixel of the byte. The value of
/// a black pixel is a property of the view, because the sources of the images differ:
/// the samples of a PDF image of the DeviceGray color space are 0 for black, while the
/// fax coders and JBIG2 use 1 for black. The view does not own the data.
struct PDF4QTLIBCORESHARED_EXPORT PDFBitonalBitmapView
{
    const uint8_t* data = nullptr;
    int width = 0;
    int height = 0;

    /// Number of bytes of a row, including the padding at the end of the row
    int stride = 0;

    /// True, if a set bit is a black pixel
    bool isOneBlack = true;

    /// Returns true, if the view is valid - it has a positive size, the data is set and
    /// a row fits into the stride.
    bool isValid() const;

    /// Returns true, if the pixel is black. The pixel must lie inside the image.
    inline bool isPixelBlack(int x, int y) const
    {
        const uint8_t byte = data[size_t(y) * size_t(stride) + size_t(x >> 3)];
        const bool isSet = (byte >> (7 - (x & 7))) & 0x01;
        return isSet == isOneBlack;
    }

    /// Fills the positions of the changing elements of a row, see 4.1.1 of ITU-T T.4.
    /// A changing element is a pixel, whose colour differs from the previous pixel of
    /// the same row, and the imaginary pixel before the row is white, so the element
    /// at an even index changes the colour to black and the element at an odd index
    /// changes it to white. The list is terminated by the width of the image repeated
    /// three times, so the coder can look two elements ahead without a range check.
    /// \param y Row
    /// \param changingElements Filled positions
    void getChangingElements(int y, std::vector<int>& changingElements) const;
};

/// Parameters of the encoder. They have the same meaning as the parameters of the
/// CCITTFaxDecode filter of a PDF stream, so a stream created by the encoder is
/// decoded by the parameters, with which it has been encoded.
struct PDFCCITTFaxEncoderParameters
{
    /// Type of the encoding:
    ///    K < 0 - pure two dimensional encoding (Group 4, ITU-T T.6)
    ///    K = 0 - pure one dimensional encoding (Group 3, ITU-T T.4, modified Huffman)
    ///    K > 0 - mixed encoding; a row encoded one dimensionally is followed by K - 1 rows
    ///            encoded two dimensionally
    PDFInteger K = -1;

    /// Write the end-of-line code word before each row. Group 4 encoding never contains
    /// end-of-line code words, so this flag is ignored for a negative K.
    bool hasEndOfLine = false;

    /// Start each row on a byte boundary. Unused bits of the last byte of a row are
    /// zero. When end-of-line code words are written, they start at a byte boundary
    /// and the row follows them directly.
    bool hasEncodedByteAlign = false;

    /// Terminate the data by the end-of-facsimile-block code (EOFB) for Group 4, or by
    /// the return-to-control code (RTC) for Group 3. A decoder, which knows the number
    /// of the rows, does not need the terminator.
    bool hasEndOfBlock = true;
};

/// Encoder of bitonal images by the CCITT Group 3 and Group 4 fax encoding, described
/// in ITU-T T.4 and ITU-T T.6, as it is used by the CCITTFaxDecode filter of PDF. The
/// data produced by the encoder are the runs of the white and black pixels, so the
/// value, which represents the black pixel in the decoded data, is not a property of
/// the encoded data - the decoder selects it by its BlackIs1 parameter.
class PDF4QTLIBCORESHARED_EXPORT PDFCCITTFaxEncoder
{
public:
    /// \param bitmap Encoded image, it must outlive the encoder
    /// \param parameters Parameters of the encoding
    explicit PDFCCITTFaxEncoder(const PDFBitonalBitmapView& bitmap, const PDFCCITTFaxEncoderParameters& parameters);

    /// Encodes the image. Throws \p PDFException, if the image is invalid. The
    /// encoded data always end at a byte boundary, the unused bits are zero.
    QByteArray encode();

private:
    /// Writes the bits of a code word, the most significant bit first
    /// \param code Code word
    /// \param bitCount Number of the bits of the code word
    void writeBits(uint32_t code, int bitCount);

    /// Pads the data by zero bits up to the next byte boundary
    void alignToByte();

    /// Writes the end-of-line code word 000000000001
    void writeEndOfLine();

    /// Writes the tag bit of a mixed encoding, which selects the coding of the next row
    /// \param isTwoDimensional Next row is coded two dimensionally
    void writeRowTag(bool isTwoDimensional);

    /// Writes a run of the pixels of a single colour, as a sequence of the make-up code
    /// words and a terminating code word, see 4.1.1 of ITU-T T.4.
    /// \param length Length of the run
    /// \param isWhite Colour of the run
    void writeRun(int length, bool isWhite);

    /// Writes the code word of a run length of at most 2560 pixels. A length of at
    /// least 64 pixels must be a multiple of 64, because such lengths are only
    /// available as the make-up code words.
    /// \param length Length of the run
    /// \param isWhite Colour of the run
    void writeRunCode(int length, bool isWhite);

    /// Writes the code word of a mode of the two dimensional coding, see 4.2.1.3.1 of ITU-T T.4
    void writeMode(int mode);

    /// Encodes a row one dimensionally
    /// \param codingLine Changing elements of the row
    void encodeRow1D(const std::vector<int>& codingLine);

    /// Encodes a row two dimensionally, see 4.2.1.3 of ITU-T T.4 and 2.2 of ITU-T T.6
    /// \param codingLine Changing elements of the row
    /// \param referenceLine Changing elements of the previous row
    void encodeRow2D(const std::vector<int>& codingLine, const std::vector<int>& referenceLine);

    /// Writes the end-of-facsimile-block (Group 4) or return-to-control (Group 3) code
    void writeEndOfBlock();

    PDFBitonalBitmapView m_bitmap;
    PDFCCITTFaxEncoderParameters m_parameters;
    QByteArray m_output;
    uint64_t m_bitBuffer = 0;
    int m_bitsInBuffer = 0;
};

}   // namespace pdf

#endif // PDFCCITTFAXENCODER_H
