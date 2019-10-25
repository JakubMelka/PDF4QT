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

static constexpr uint16_t HUFFMAN_LOW_VALUE = 0xFFFE;
static constexpr uint16_t HUFFMAN_OOB_VALUE = 0xFFFF;

struct PDFJBIG2HuffmanTableEntry
{
    enum class Type : uint8_t
    {
        Standard,
        Negative,
        OutOfBand
    };

    /// Returns true, if current row represents interval (-∞, value),
    /// it means 32bit number must be read and
    bool isLowValue() const { return rangeBitLength == 0xFFFE; }

    /// Returns true, if current row represents out-of-band value
    bool isOutOfBand() const { return rangeBitLength == HUFFMAN_OOB_VALUE; }

    int32_t value;              ///< Base value
    uint16_t prefixBitLength;   ///< Bit length of prefix
    uint16_t rangeBitLength;    ///< Bit length of additional value
    uint16_t prefix;            ///< Bit prefix of the huffman code
    Type type;                  ///< Type of the value
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_A[] =
{
    {     0, 1,  4,   0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    16, 2,  8,  0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   272, 3, 16, 0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 65808, 3, 32, 0b111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_B[] =
{
    {  0, 1,  0,      0b0,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  1, 2,  0,     0b10,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2, 3,  0,    0b110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  3, 4,  3,   0b1110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 11, 5,  6,  0b11110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  0, 6,  0, 0b111111, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    { 75, 6, 32, 0b111110,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_C[] =
{
    {    0, 1,  0,        0b0,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    1, 2,  0,       0b10,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    2, 3,  0,      0b110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 4,  3,     0b1110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   11, 5,  6,    0b11110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    0, 6,  0,   0b111110, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    {   75, 7, 32,  0b1111110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -257, 8, 32, 0b11111111,  PDFJBIG2HuffmanTableEntry::Type::Negative},
    { -256, 8,  8, 0b11111110,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_D[] =
{
    {  1, 1,  0,     0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2, 2,  0,    0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  3, 3,  0,   0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  4, 4,  3,  0b1110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 12, 5,  6, 0b11110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 76, 5, 32, 0b11111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_E[] =
{
    {    1, 1,  0,       0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    2, 2,  0,      0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 3,  0,     0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    4, 4,  3,    0b1110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   12, 5,  6,   0b11110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   76, 6, 32,  0b111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -256, 7, 32, 0b1111111, PDFJBIG2HuffmanTableEntry::Type::Negative},
    { -255, 7,  8, 0b1111110, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_F[] =
{
    {     0, 2,  7,     0b00, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   128, 3,  7,    0b010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   256, 3,  8,    0b011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -1024, 4,  9,   0b1000, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -512, 4,  8,   0b1001, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -256, 4,  7,   0b1010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -32, 4,  5,   0b1011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   512, 4,  9,   0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  1024, 4, 10,   0b1101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -2048, 5, 10,  0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -128, 5,  6,  0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -64, 5,  5,  0b11110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -2049, 6, 32, 0b111110, PDFJBIG2HuffmanTableEntry::Type::Negative},
    {  2048, 6, 32, 0b111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_G[] =
{
    {  -512, 3,  8,   0b000, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   256, 3,  8,   0b001, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   512, 3,  9,   0b010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  1024, 3, 10,   0b011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -1024, 4,  9,  0b1000, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -256, 4,  7,  0b1001, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -32, 4,  5,  0b1010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {     0, 4,  5,  0b1011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   128, 4,  7,  0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -1025, 5, 32, 0b11110, PDFJBIG2HuffmanTableEntry::Type::Negative},
    {  -128, 5,  6, 0b11010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -64, 5,  5, 0b11011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    32, 5,  5, 0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    64, 5,  6, 0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2048, 5, 32, 0b11111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_H[] =
{
    {    0, 2,  1,        0b00,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    0, 2,  0,        0b01, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    {    4, 3,  4,       0b100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -1, 4,  0,      0b1010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   22, 4,  4,      0b1011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   38, 4,  5,      0b1100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    2, 5,  0,     0b11010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   70, 5,  6,     0b11011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  134, 5,  7,     0b11100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 6,  0,    0b111010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   20, 6,  1,    0b111011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  262, 6,  7,    0b111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  646, 6, 10,    0b111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -2, 7,  0,   0b1111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  390, 7,  8,   0b1111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -15, 8,  3,  0b11111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -5, 8,  1,  0b11111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -16, 9, 32, 0b111111110,  PDFJBIG2HuffmanTableEntry::Type::Negative},
    {   -7, 9,  1, 0b111111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -3, 9,  0, 0b111111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 1670, 9, 32, 0b111111111,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_I[] =
{
    {    0, 2,  0,        0b00, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    {   -1, 3,  1,       0b010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    1, 3,  1,       0b011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    7, 3,  5,       0b100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -3, 4,  1,      0b1010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   43, 4,  5,      0b1011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   75, 4,  6,      0b1100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 5,  1,     0b11010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  139, 5,  7,     0b11011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  267, 5,  8,     0b11100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    5, 6,  1,    0b111010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   39, 6,  2,    0b111011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  523, 6,  8,    0b111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 1291, 6, 11,    0b111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -5, 7,  1,   0b1111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  779, 7,  9,   0b1111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -31, 8,  4,  0b11111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -11, 8,  2,  0b11111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -32, 9, 32, 0b111111110,  PDFJBIG2HuffmanTableEntry::Type::Negative},
    {  -15, 9,  2, 0b111111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -7, 9,  1, 0b111111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 3339, 9, 32, 0b111111111,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_J[] =
{
    {   -2, 2,  2,       0b00,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    0, 2,  0,       0b10, PDFJBIG2HuffmanTableEntry::Type::OutOfBand},
    {    6, 2,  6,       0b01,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -3, 5,  0,    0b11000,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    2, 5,  0,    0b11001,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   70, 5,  5,    0b11010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    3, 6,  0,   0b110110,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  102, 6,  5,   0b110111,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  134, 6,  6,   0b111000,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  198, 6,  7,   0b111001,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  326, 6,  8,   0b111010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  582, 6,  9,   0b111011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 1094, 6, 10,   0b111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -21, 7,  4,  0b1111010,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   -4, 7,  0,  0b1111011,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    4, 7,  0,  0b1111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 2118, 7, 11,  0b1111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -22, 8, 32, 0b11111110,  PDFJBIG2HuffmanTableEntry::Type::Negative},
    {   -5, 8,  0, 0b11111100,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    {    5, 8,  0, 0b11111101,  PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 4166, 8, 32, 0b11111111,  PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_K[] =
{
    {   1, 1,  0,       0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   2, 2,  1,      0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   4, 4,  0,    0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   5, 4,  1,    0b1101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   7, 5,  1,   0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   9, 5,  2,   0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  13, 6,  2,  0b111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  17, 7,  2, 0b1111010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  21, 7,  3, 0b1111011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  29, 7,  4, 0b1111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  45, 7,  5, 0b1111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  77, 7,  6, 0b1111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 141, 7, 32, 0b1111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_L[] =
{
    {  1, 1,  0,        0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2, 2,  0,       0b10, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  3, 3,  1,      0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  5, 5,  0,    0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  6, 5,  1,    0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  8, 6,  1,   0b111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 10, 7,  0,  0b1111010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 11, 7,  1,  0b1111011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 13, 7,  2,  0b1111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 17, 7,  3,  0b1111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 25, 7,  4,  0b1111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 41, 8,  5, 0b11111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 73, 8, 32, 0b11111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_M[] =
{
    {   1, 1,  0,       0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   2, 3,  0,     0b100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   7, 3,  3,     0b101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   3, 4,  0,    0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   5, 4,  1,    0b1101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   4, 5,  0,   0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  15, 6,  1,  0b111010, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  17, 6,  2,  0b111011, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  21, 6,  3,  0b111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  29, 6,  4,  0b111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  45, 6,  5,  0b111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  77, 7,  6, 0b1111110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { 141, 7, 32, 0b1111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_N[] =
{
    {  0, 1, 0,   0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -2, 3, 0, 0b100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -1, 3, 0, 0b101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  1, 3, 0, 0b110, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  2, 3, 0, 0b111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

static constexpr PDFJBIG2HuffmanTableEntry PDFJBIG2StandardHuffmanTable_O[] =
{
    {   0, 1,  0,       0b0, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -1, 3,  0,     0b100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   1, 3,  0,     0b101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -2, 4,  0,    0b1100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   2, 4,  0,    0b1101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -4, 5,  1,   0b11100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   3, 5,  1,   0b11101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  -8, 6,  2,  0b111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   5, 6,  2,  0b111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    { -25, 7, 32, 0b1111110, PDFJBIG2HuffmanTableEntry::Type::Negative},
    { -24, 7,  4, 0b1111100, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {   9, 7,  4, 0b1111101, PDFJBIG2HuffmanTableEntry::Type::Standard},
    {  25, 7, 32, 0b1111111, PDFJBIG2HuffmanTableEntry::Type::Standard}
};

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
