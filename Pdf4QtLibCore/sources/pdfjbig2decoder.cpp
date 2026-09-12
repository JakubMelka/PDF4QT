// MIT License
//
// Copyright (c) 2018-2026 Jakub Melka and Contributors
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

#include "pdfjbig2decoder.h"
#include "pdfexception.h"
#include "pdfccittfaxdecoder.h"
#include "pdfdbgheap.h"

namespace pdf
{

static constexpr uint32_t MAX_JBIG2_SYMBOL_COUNT = 1 << 20;
static constexpr uint32_t MAX_JBIG2_SEGMENT_COUNT = 65536;

static int32_t checkedJBIG2Integer(int64_t value)
{
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 integer value out of range."));
    }
    return int32_t(value);
}

QByteArray PDFJBIG2Decoder::readRefinementData(PDFBitReader* reader, int32_t size)
{
    reader->alignToBytes();
    if (size < 2 || size > reader->getStream()->size() - reader->getPosition())
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid refinement data length %1.").arg(size));
    }
    consumeDecodedBytes(uint64_t(size));
    return reader->readSubstream(size);
}

/// Returns the value divided by two, rounded towards the negative infinity, which is
/// the floor function used by the specification. The integer division of C++ rounds
/// towards zero instead, so the results differ for negative odd values. The right
/// shift of a signed value is defined as an arithmetic one since C++20.
/// \param value Divided value
static constexpr int32_t floorDivideByTwo(int32_t value)
{
    return value >> 1;
}

class PDFJBIG2HuffmanCodeTable : public PDFJBIG2Segment
{
public:
    explicit PDFJBIG2HuffmanCodeTable(std::vector<PDFJBIG2HuffmanTableEntry>&& entries);
    virtual ~PDFJBIG2HuffmanCodeTable();

    virtual const PDFJBIG2HuffmanCodeTable* asHuffmanCodeTable() const override { return this; }

    const std::vector<PDFJBIG2HuffmanTableEntry>& getEntries() const { return m_entries; }

    /// Builds prefixes using algorithm in annex B.3 of specification. Unused rows are removed.
    /// Rows are sorted according the criteria. Prefixes are then filled.
    /// \param entries Entries for building the table
    static std::vector<PDFJBIG2HuffmanTableEntry> buildPrefixes(const std::vector<PDFJBIG2HuffmanTableEntry>& entries);

private:
    std::vector<PDFJBIG2HuffmanTableEntry> m_entries;
};

class PDFJBIG2SymbolDictionary : public PDFJBIG2Segment
{
public:
    explicit inline PDFJBIG2SymbolDictionary() = default;
    explicit inline PDFJBIG2SymbolDictionary(std::vector<PDFJBIG2Bitmap>&& bitmaps,
                                             PDFJBIG2ArithmeticDecoderState&& genericState,
                                             PDFJBIG2ArithmeticDecoderState&& genericRefinementState,
                                             uint16_t flags, const PDFJBIG2ATPositions& at, const PDFJBIG2ATPositions& refinementAt) :
        m_bitmaps(qMove(bitmaps)),
        m_genericState(qMove(genericState)),
        m_genericRefinementState(qMove(genericRefinementState)),
        m_flags(flags), m_at(at), m_refinementAt(refinementAt)
    {

    }

    virtual const PDFJBIG2SymbolDictionary* asSymbolDictionary() const override { return this; }

    const std::vector<PDFJBIG2Bitmap>& getBitmaps() const { return m_bitmaps; }
    const PDFJBIG2ArithmeticDecoderState& getGenericState() const { return m_genericState; }
    const PDFJBIG2ArithmeticDecoderState& getGenericRefinementState() const { return m_genericRefinementState; }
    bool canReuse(uint16_t flags, const PDFJBIG2ATPositions& at, const PDFJBIG2ATPositions& refinementAt) const
    {
        const auto equal = [](const auto& a, const auto& b) { return a.x == b.x && a.y == b.y; };
        return (m_flags & 0x0200) && ((m_flags ^ flags) & 0x1C03) == 0 &&
               std::equal(m_at.begin(), m_at.end(), at.begin(), equal) &&
               std::equal(m_refinementAt.begin(), m_refinementAt.end(), refinementAt.begin(), equal);
    }

private:
    std::vector<PDFJBIG2Bitmap> m_bitmaps;
    PDFJBIG2ArithmeticDecoderState m_genericState;
    PDFJBIG2ArithmeticDecoderState m_genericRefinementState;
    uint16_t m_flags = 0;
    PDFJBIG2ATPositions m_at = { };
    PDFJBIG2ATPositions m_refinementAt = { };
};

class PDFJBIG2PatternDictionary : public PDFJBIG2Segment
{
public:
    explicit inline PDFJBIG2PatternDictionary() = default;
    explicit inline PDFJBIG2PatternDictionary(std::vector<PDFJBIG2Bitmap>&& bitmaps) :
        m_bitmaps(qMove(bitmaps))
    {

    }

    virtual const PDFJBIG2PatternDictionary* asPatternDictionary() const override { return this; }

    const std::vector<PDFJBIG2Bitmap>& getBitmaps() const { return m_bitmaps; }

private:
    std::vector<PDFJBIG2Bitmap> m_bitmaps;
};

/// Structure containing arithmetic decoder states
struct PDFJBIG2ArithmeticDecoderStates
{
    enum
    {
        IADH,
        IADW,
        IAEX,
        IADT,
        IAFS,
        IADS,
        IAIT,
        IARI,
        IARDW,
        IARDH,
        IARDX,
        IARDY,
        IAAI,
        IAID,
        Generic,
        Refinement,
        End
    };

    /// Resets integer arithmetic decoder statistics. For normal register, it uses context
    /// of length 9 bits (512 states), for IAID, it uses \p IAIDbits bits for the context.
    /// \param IAIDbits Bit length of context for IAID
    void resetArithmeticStatesInteger(const uint8_t IAIDbits);

    /// Reset arithmetic decoder stats for generic
    /// \param templateMode Template mode
    /// \param state State to copy from (can be nullptr)
    void resetArithmeticStatesGeneric(const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state);

    /// Reset arithmetic decoder stats for generic refinement
    /// \param templateMode Template mode
    /// \param state State to copy from (can be nullptr)
    void resetArithmeticStatesGenericRefinement(const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state);

    /// Reset arithmetic decoder stats for generic
    /// \param newState State to be reset
    /// \param templateMode Template mode
    /// \param state State to copy from (can be nullptr)
    static void resetArithmeticStatesGeneric(PDFJBIG2ArithmeticDecoderState* newState, const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state);

    /// Reset arithmetic decoder stats for generic refinement
    /// \param newState State to be reset
    /// \param templateMode Template mode
    /// \param state State to copy from (can be nullptr)
    static void resetArithmeticStatesGenericRefinement(PDFJBIG2ArithmeticDecoderState* newState, const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state);

    std::array<PDFJBIG2ArithmeticDecoderState, End> states;
};

void PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesInteger(const uint8_t IAIDbits)
{
    for (auto context : { IADH, IADW, IAEX, IADT, IAFS, IADS, IAIT, IARI, IARDW, IARDH, IARDX, IARDY, IAAI })
    {
        states[context].reset(9);
    }
    states[IAID].reset(IAIDbits);
}

void PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesGeneric(const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state)
{
    resetArithmeticStatesGeneric(&states[Generic], templateMode, state);
}

void PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesGenericRefinement(const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state)
{
    resetArithmeticStatesGenericRefinement(&states[Refinement], templateMode, state);
}

void PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesGeneric(PDFJBIG2ArithmeticDecoderState* newState, const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state)
{
    // Number of the bits of the context of each template, see figures 4 to 7 of the
    // specification. The template is a two bit field, so every value is a template.
    constexpr std::array<uint8_t, 4> contextBits = { 16, 13, 10, 10 };
    Q_ASSERT(templateMode < contextBits.size());
    const uint8_t bits = contextBits[templateMode];

    if (!state)
    {
        newState->reset(bits);
    }
    else
    {
        newState->reset(bits, *state);
    }
}

void PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesGenericRefinement(PDFJBIG2ArithmeticDecoderState* newState, const uint8_t templateMode, const PDFJBIG2ArithmeticDecoderState* state)
{
    // Number of the bits of the context of each refinement template, see figures 12
    // and 13 of the specification. The template is a single bit field.
    constexpr std::array<uint8_t, 2> contextBits = { 13, 10 };
    Q_ASSERT(templateMode < contextBits.size());
    const uint8_t bits = contextBits[templateMode];

    if (!state)
    {
        newState->reset(bits);
    }
    else
    {
        newState->reset(bits, *state);
    }
}

/// Structure containing state pointers for arithmetic decoder
struct PDFJBIG2ArithmeticDecoderStatePointers
{
    void initializeFrom(PDFJBIG2ArithmeticDecoderStates* states);

    PDFJBIG2ArithmeticDecoderState* IADT = nullptr;
    PDFJBIG2ArithmeticDecoderState* IAFS = nullptr;
    PDFJBIG2ArithmeticDecoderState* IADS = nullptr;
    PDFJBIG2ArithmeticDecoderState* IAIT = nullptr;
    PDFJBIG2ArithmeticDecoderState* IAID = nullptr;
    PDFJBIG2ArithmeticDecoderState* IARI = nullptr;
    PDFJBIG2ArithmeticDecoderState* IARDW = nullptr;
    PDFJBIG2ArithmeticDecoderState* IARDH = nullptr;
    PDFJBIG2ArithmeticDecoderState* IARDX = nullptr;
    PDFJBIG2ArithmeticDecoderState* IARDY = nullptr;
    PDFJBIG2ArithmeticDecoderState* genericDecoderState = nullptr;
    PDFJBIG2ArithmeticDecoderState* refinementDecoderState = nullptr;
};

void PDFJBIG2ArithmeticDecoderStatePointers::initializeFrom(PDFJBIG2ArithmeticDecoderStates* states)
{
    IADT = &states->states[PDFJBIG2ArithmeticDecoderStates::IADT];
    IAFS = &states->states[PDFJBIG2ArithmeticDecoderStates::IAFS];
    IADS = &states->states[PDFJBIG2ArithmeticDecoderStates::IADS];
    IAIT = &states->states[PDFJBIG2ArithmeticDecoderStates::IAIT];
    IAID = &states->states[PDFJBIG2ArithmeticDecoderStates::IAID];
    IARI = &states->states[PDFJBIG2ArithmeticDecoderStates::IARI];
    IARDW = &states->states[PDFJBIG2ArithmeticDecoderStates::IARDW];
    IARDH = &states->states[PDFJBIG2ArithmeticDecoderStates::IARDH];
    IARDX = &states->states[PDFJBIG2ArithmeticDecoderStates::IARDX];
    IARDY = &states->states[PDFJBIG2ArithmeticDecoderStates::IARDY];
    genericDecoderState = &states->states[PDFJBIG2ArithmeticDecoderStates::Generic];
    refinementDecoderState = &states->states[PDFJBIG2ArithmeticDecoderStates::Refinement];
}

/// Info structure for text region decoding structure
struct PDFJBIG2TextRegionDecodingParameters : public PDFJBIG2ArithmeticDecoderStatePointers
{
    enum : uint8_t
    {
        BOTTOMLEFT = 0,
        TOPLEFT = 1,
        BOTTOMRIGHT = 2,
        TOPRIGHT = 3
    };

    bool SBHUFF = false;
    bool SBREFINE = false;
    uint8_t SBDEFPIXEL = 0;
    PDFJBIG2BitOperation SBCOMBOP = PDFJBIG2BitOperation::Invalid;
    bool TRANSPOSED = false;
    uint8_t REFCORNER = 0;
    int32_t SBDSOFFSET = 0;
    uint32_t SBW = 0;
    uint32_t SBH = 0;
    uint32_t SBNUMINSTANCES = 0;
    uint8_t LOG2SBSTRIPS = 0;
    uint8_t SBSTRIPS = 0;
    uint32_t SBNUMSYMS = 0;
    std::vector<const PDFJBIG2Bitmap*> SBSYMS;
    uint8_t SBSYMCODELEN = 0;
    PDFJBIG2HuffmanDecoder SBSYMCODES;
    PDFJBIG2HuffmanDecoder SBHUFFFS;
    PDFJBIG2HuffmanDecoder SBHUFFDS;
    PDFJBIG2HuffmanDecoder SBHUFFDT;
    PDFJBIG2HuffmanDecoder SBHUFFRDW;
    PDFJBIG2HuffmanDecoder SBHUFFRDH;
    PDFJBIG2HuffmanDecoder SBHUFFRDX;
    PDFJBIG2HuffmanDecoder SBHUFFRDY;
    PDFJBIG2HuffmanDecoder SBHUFFRSIZE;
    uint8_t SBRTEMPLATE = 0;
    PDFJBIG2ATPositions SBRAT = { };
    PDFJBIG2ArithmeticDecoder* arithmeticDecoder = nullptr;
    PDFBitReader* reader = nullptr;
};

/// Info structure for bitmap decoding parameters
struct PDFJBIG2BitmapDecodingParameters
{
    /// Is Modified-Modified-Read encoding used? This encoding is simalr to CCITT pure 2D encoding.
    bool MMR = false;

    /// Is typical prediction for generic direct coding used?
    bool TPGDON = false;

    /// Width of the image
    int GBW = 0;

    /// Height of the image
    int GBH = 0;

    /// Template mode (not used for MMR).
    uint8_t GBTEMPLATE = 0;

    /// Positions of adaptative pixels
    PDFJBIG2ATPositions GBAT = { };

    /// Data with encoded image
    QByteArray data;

    /// Gray-scale planes require EOFB, followed by byte alignment.
    bool requireMMREndOfBlock = false;
    int dataEndPosition = 0;

    /// State of arithmetic decoder
    PDFJBIG2ArithmeticDecoderState* arithmeticDecoderState = nullptr;

    /// Skip bitmap (pixel is skipped if corresponding pixel in the
    /// skip bitmap is 1). Set to nullptr, if not used.
    const PDFJBIG2Bitmap* SKIP = nullptr;

    /// Arithmetic decoder (used, if MMR == false)
    PDFJBIG2ArithmeticDecoder* arithmeticDecoder = nullptr;
};

/// Info structure for refinement bitmap decoding parameters
struct PDFJBIG2BitmapRefinementDecodingParameters
{
    /// Template mode used (0/1)
    uint8_t GRTEMPLATE = 0;

    /// Prediction (same as previous row)
    bool TPGRON = false;

    /// Bitmap width
    uint32_t GRW = 0;

    /// Bitmap height
    uint32_t GRH = 0;

    /// Reference bitmap
    const PDFJBIG2Bitmap* GRREFERENCE = nullptr;

    /// Offset x
    int64_t GRREFERENCEX = 0;

    /// Offset y
    int64_t GRREFERENCEY = 0;

    /// State of arithmetic decoder
    PDFJBIG2ArithmeticDecoderState* arithmeticDecoderState = nullptr;

    /// Positions of adaptative pixels
    PDFJBIG2ATPositions GRAT = { };

    PDFJBIG2ArithmeticDecoder* decoder = nullptr;
};

/// Info structure for symbol dictionary decoding procedure
struct PDFJBIG2SymbolDictionaryDecodingParameters
{
    /// If true, huffman encoding is used to decode dictionary,
    /// otherwise arithmetic decoding is used to decode dictionary.
    bool SDHUFF = false;

    /// If true, each symbol is refinement/aggregate. If false,
    /// then symbols are ordinary bitmaps.
    bool SDREFAGG = false;

    /// Table selector for huffman table encoding (height)
    uint8_t SDHUFFDH = 0;

    /// Table selector for huffman table encoding (width)
    uint8_t SDHUFFDW = 0;

    /// Table selector for huffman table encoding
    uint8_t SDHUFFBMSIZE = 0;

    /// Table selector for huffman table encoding
    uint8_t SDHUFFAGGINST = 0;

    /// Is statistics for arithmetic coding used from previous symbol dictionary?
    bool isArithmeticCodingStateUsed = false;

    /// Is statistics for arithmetic coding symbols retained for future use?
    bool isArithmeticCodingStateRetained = false;

    /// Template for decoding
    uint8_t SDTEMPLATE = 0;

    /// Template for decoding refinements
    uint8_t SDRTEMPLATE = 0;

    /// Adaptative pixel positions
    PDFJBIG2ATPositions SDAT = { };

    /// Adaptative pixel positions
    PDFJBIG2ATPositions SDRAT = { };

    /// Number of exported symbols
    uint32_t SDNUMEXSYMS = 0;

    /// Number of new symbols
    uint32_t SDNUMNEWSYMS = 0;

    PDFJBIG2HuffmanDecoder SDHUFFDH_Decoder;
    PDFJBIG2HuffmanDecoder SDHUFFDW_Decoder;
    PDFJBIG2HuffmanDecoder SDHUFFBMSIZE_Decoder;
    PDFJBIG2HuffmanDecoder SDHUFFAGGINST_Decoder;
    PDFJBIG2HuffmanDecoder EXRUNLENGTH_Decoder;

    /// Input bitmaps
    std::vector<const PDFJBIG2Bitmap*> SDINSYMS;

    /// Number of input bitmaps
    uint32_t SDNUMINSYMS = 0;

    /// Output bitmaps
    std::vector<PDFJBIG2Bitmap> SDNEWSYMS;

    /// Widths
    std::vector<int32_t> SDNEWSYMWIDTHS;
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

uint32_t PDFJBIG2ArithmeticDecoder::readByte(size_t context, PDFJBIG2ArithmeticDecoderState* state)
{
    uint32_t byte = 0;
    for (int i = 0; i < 8; ++i)
    {
        byte = (byte << 1) | readBit(context, state);
    }

    return byte;
}

int32_t PDFJBIG2ArithmeticDecoder::getIAID(uint32_t size, PDFJBIG2ArithmeticDecoderState* state)
{
    if (size > 31)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid symbol ID bit count %1.").arg(size));
    }
    // Algorithm A.3 in annex A in the specification
    uint32_t PREV = 1;

    for (uint32_t i = 0; i < size; ++i)
    {
        uint32_t bit = readBit(PREV, state);
        PREV = (PREV << 1) | bit;
    }

    // Jakub Melka: we must subtract 1 << size, because at the start of the algorithm,
    // PREV is initialized to 1, which we don't want in the result, so we subtract the value.
    return int32_t(PREV - (uint32_t(1) << size));
}

std::optional<int32_t> PDFJBIG2ArithmeticDecoder::getSignedInteger(PDFJBIG2ArithmeticDecoderState* state)
{
    // Algorithm A.2 in annex A in the specification
    uint32_t PREV = 1;

    auto readIntBit = [this, &PREV, state]()
    {
        uint32_t bit = readBit(PREV, state);

        if (PREV < 256)
        {
            PREV = (PREV << 1) | bit;
        }
        else
        {
            PREV = (((PREV << 1) | bit) & 0x01FF) | 0x0100;
        }
        Q_ASSERT(PREV < 512);

        return bit;
    };

    auto readIntBits = [&readIntBit](uint32_t bits)
    {
        uint32_t result = 0;

        for (uint32_t i = 0; i < bits; ++i)
        {
            result = (result << 1) | readIntBit();
        }

        return result;
    };

    uint32_t S = readIntBit(); // S = sign of number
    uint64_t V = 0; // Keep the magnitude and its offset before checking the signed range
    if (!readIntBit())
    {
        V = readIntBits(2);
    }
    else if (!readIntBit())
    {
        V = readIntBits(4) + 4;
    }
    else if (!readIntBit())
    {
        V = readIntBits(6) + 20;
    }
    else if (!readIntBit())
    {
        V = readIntBits(8) + 84;
    }
    else if (!readIntBit())
    {
        V = readIntBits(12) + 340;
    }
    else
    {
        V = uint64_t(readIntBits(32)) + 4436;
    }

    if (S)
    {
        if (V == 0)
        {
            return std::nullopt;
        }
        else
        {
            return checkedJBIG2Integer(-int64_t(V));
        }
    }
    else
    {
        return checkedJBIG2Integer(int64_t(V));
    }
}

void PDFJBIG2ArithmeticDecoder::finalize()
{
    if (m_lastByte == 0xFF)
    {
        if (m_reader->look(8) == 0xAC)
        {
            m_reader->read(8);
        }
    }
}

void PDFJBIG2ArithmeticDecoder::perform_INITDEC()
{
    // Used figure G.1, in annex G, of specification
    uint32_t B = m_reader->readUnsignedByte();
    m_lastByte = B;
    m_c = B << 16;
    perform_BYTEIN();
    m_c = m_c << 7;
    m_ct -= 7;
    m_a = 0x80000000;
}

void PDFJBIG2ArithmeticDecoder::perform_BYTEIN()
{
    // Used figure G.3, in annex G, of specification
    if (m_lastByte == 0xFF)
    {
        const uint32_t B1 = m_reader->look(8);
        if (B1 > 0x8F)
        {
            m_c += 0xFF00;
            m_ct = 8;
        }
        else
        {
            m_c = m_c + (B1 << 9);
            m_ct = 7;
            m_lastByte = m_reader->readUnsignedByte();
        }
    }
    else
    {
        const uint32_t B = m_reader->readUnsignedByte();
        m_lastByte = B;
        m_c = m_c + (B << 8);
        m_ct = 8;
    }
}

uint32_t PDFJBIG2ArithmeticDecoder::perform_DECODE(size_t context, PDFJBIG2ArithmeticDecoderState* state)
{
    if (!state || (m_workRemaining && *m_workRemaining == 0))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 decoding work limit exceeded or invalid state."));
    }
    if (m_workRemaining)
    {
        --*m_workRemaining;
    }
    // Used figure G.2, in annex G, of specification
    const uint8_t QeRowIndex = state->getQeRowIndex(context);
    uint8_t MPS = state->getMPS(context);
    uint8_t D = MPS;

    // Sanity checks
    if (QeRowIndex >= std::size(JBIG2_ARITHMETIC_DECODER_QE_VALUES))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid arithmetic state."));
    }
    Q_ASSERT(MPS < 2);

    const PDFJBIG2ArithmeticDecoderQeValue& QeInfo = JBIG2_ARITHMETIC_DECODER_QE_VALUES[QeRowIndex];
    const uint32_t Qe = QeInfo.Qe;
    m_a -= Qe;

    if (m_c >= Qe)
    {
        // We are substracting this value according figure E.15 in the specification
        m_c -= Qe;

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

        m_a = Qe;
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

PDFJBIG2SegmentHeader PDFJBIG2SegmentHeader::read(PDFBitReader* reader)
{
    PDFJBIG2SegmentHeader header;

    // Parse segment headers and segment flags
    header.m_segmentNumber = reader->readUnsignedInt();
    const uint8_t flags = reader->readUnsignedByte();
    const uint8_t type = flags & 0x3F;
    const bool isPageAssociationSize4ByteLong = flags & 0x40;
    header.m_deferredNonRetain = flags & 0x80;

    // Parse both the referred-to count and retention flags (7.2.4).
    uint32_t retentionField = reader->readUnsignedByte();
    uint32_t referredSegmentsCount = retentionField >> 5; // Bits 6,7,8

    if (referredSegmentsCount == 5 || referredSegmentsCount == 6)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid header - bad referred segments."));
    }

    if (referredSegmentsCount == 7)
    {
        retentionField = (retentionField << 24) | reader->read(24);
        referredSegmentsCount = retentionField & 0x1FFFFFFF;
        const uint32_t bytes = (referredSegmentsCount + 8) / 8;
        if (referredSegmentsCount < 5 || referredSegmentsCount > MAX_JBIG2_SEGMENT_COUNT ||
            bytes > reader->getStream()->size() - reader->getPosition())
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid referred segment count."));
        }
        header.m_retainFlags.resize(size_t(referredSegmentsCount) + 1);
        for (uint32_t i = 0; i < bytes; ++i)
        {
            const uint8_t value = reader->readUnsignedByte();
            const uint32_t validBits = qMin(8u, referredSegmentsCount + 1 - i * 8);
            if ((uint32_t(value) >> validBits) != 0)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid retention padding bits."));
            }
            for (uint32_t bit = 0; bit < 8 && i * 8 + bit <= referredSegmentsCount; ++bit)
            {
                header.m_retainFlags[i * 8 + bit] = (value >> bit) & 1;
            }
        }
    }
    else
    {
        if ((retentionField & 0x1F) >> (referredSegmentsCount + 1))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid retention padding bits."));
        }
        for (uint32_t i = 0; i <= referredSegmentsCount; ++i)
        {
            header.m_retainFlags.push_back((retentionField >> i) & 1);
        }
    }

    // Read referred segment numbers. According to specification, chapter 7.2.5, referred segments should have
    // segment number lesser than actual segment number. So, if segment number is less, or equal to 256, then
    // 8-bit value is used to store referred segment number, if segment number is less, or equal to 65536, then
    // 16-bit value is used, otherwise 32 bit value is used.
    const PDFBitReader::Value referredSegmentNumberBits = (header.m_segmentNumber <= 256) ? 8 : ((header.m_segmentNumber <= 65536) ? 16 : 32);
    const int64_t remainingBytes = reader->getStream()->size() - reader->getPosition();
    if (referredSegmentsCount > MAX_JBIG2_SEGMENT_COUNT ||
        uint64_t(referredSegmentsCount) * (referredSegmentNumberBits / 8) > uint64_t(remainingBytes))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid referred segment count."));
    }
    header.m_referredSegments.reserve(referredSegmentsCount);
    for (uint32_t i = 0; i < referredSegmentsCount; ++i)
    {
        const uint32_t number = reader->read(referredSegmentNumberBits);
        if (number >= header.m_segmentNumber)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 reference must precede its segment."));
        }
        header.m_referredSegments.push_back(number);
    }

    header.m_pageAssociation = reader->read(isPageAssociationSize4ByteLong ? 32 : 8);
    header.m_segmentDataLength = reader->readUnsignedInt();
    header.m_lossless = type & 0x01;
    header.m_immediate = type & 0x02;

    switch (type)
    {
        case 0:
            header.m_segmentType = JBIG2SegmentType::SymbolDictionary;
            break;

        case 4:
        case 6:
        case 7:
            header.m_segmentType = JBIG2SegmentType::TextRegion;
            break;

        case 16:
            header.m_segmentType = JBIG2SegmentType::PatternDictionary;
            break;

        case 20:
        case 22:
        case 23:
            header.m_segmentType = JBIG2SegmentType::HalftoneRegion;
            break;

        case 36:
        case 38:
        case 39:
            header.m_segmentType = JBIG2SegmentType::GenericRegion;
            break;

        case 40:
        case 42:
        case 43:
            header.m_segmentType = JBIG2SegmentType::GenericRefinementRegion;
            break;

        case 48:
            header.m_segmentType = JBIG2SegmentType::PageInformation;
            break;

        case 49:
            header.m_segmentType = JBIG2SegmentType::EndOfPage;
            break;

        case 50:
            header.m_segmentType = JBIG2SegmentType::EndOfStripe;
            break;

        case 51:
            header.m_segmentType = JBIG2SegmentType::EndOfFile;
            break;

        case 52:
            header.m_segmentType = JBIG2SegmentType::Profiles;
            break;

        case 53:
            header.m_segmentType = JBIG2SegmentType::Tables;
            break;

        case 62:
            header.m_segmentType = JBIG2SegmentType::Extension;
            break;

        default:
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid segment type %1.").arg(type));
    }

    return header;
}

PDFJBIG2Decoder::~PDFJBIG2Decoder()
{

}

PDFImageData PDFJBIG2Decoder::decode(PDFImageData::MaskingType maskingType)
{
    if (uint64_t(m_data.size()) + uint64_t(m_globalData.size()) > MAX_INPUT_BYTES)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 input size limit exceeded."));
    }
    for (const QByteArray* data :  { &m_globalData, &m_data })
    {
        if (!data->isEmpty())
        {
            m_readingGlobals = data == &m_globalData;
            m_reader = PDFBitReader(data, 8);
            processStream();
        }
    }

    finishPage();
    if (m_pageBitmap.isValid())
    {
        consumeDecodedBytes(uint64_t((m_pageBitmap.getWidth() + 7) / 8) * m_pageBitmap.getHeight());
        consumeWork(m_pageBitmap.getPixelCount());
        PDFBitWriter writer(1);

        const int columns = m_pageBitmap.getWidth();
        const int rows = m_pageBitmap.getHeight();

        for (int row = 0; row < rows; ++row)
        {
            for (int column = 0; column < columns; ++column)
            {
                writer.write(!m_pageBitmap.getPixel(column, row));
            }
            writer.finishLine();
        }

        return PDFImageData(1, 1, static_cast<uint32_t>(columns), static_cast<uint32_t>(rows), static_cast<uint32_t>((columns + 7) / 8), maskingType, writer.takeByteArray(), { }, { }, { });
    }

    return PDFImageData();
}

PDFImageData PDFJBIG2Decoder::decodeFileStream()
{
    if (uint64_t(m_data.size()) + uint64_t(m_globalData.size()) > MAX_INPUT_BYTES)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 input size limit exceeded."));
    }
    m_reader = PDFBitReader(&m_data, 8);
    m_isDecodingFile = true;
    consumeDecodedBytes(uint64_t(m_data.size()) * 2);

    constexpr const char* JBIG2_FILE_HEADER = "\x97\x4A\x42\x32\x0D\x0A\x1A\x0A";
    if (!m_data.startsWith(JBIG2_FILE_HEADER))
    {
        throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 file header."));
    }

    m_reader.skipBytes(std::strlen(JBIG2_FILE_HEADER));

    // File flags
    const uint8_t fileFlags = m_reader.readUnsignedByte();

    if (fileFlags & 0xFC)
    {
        // Jakub Melka: According the specification, bits 2-7 should be reserved and zero.
        // If they are nonzero, probably a new version of JBIG2 format exists, but
        // is not decodable by this decoder. So, in this case, we don't do decoding
        // and report error immediately.
        throw PDFException(PDFTranslationContext::tr("Unsupported JBIG2 file header flags (extended templates are not supported)."));
    }

    const bool isFileOrganizationSequential = fileFlags & 0x01;
    const bool isUknownNumberOfPages = fileFlags & 0x02;

    const uint32_t numberOfPages = isUknownNumberOfPages ? 1 : m_reader.readUnsignedInt();
    if (numberOfPages != 1)
    {
        throw PDFException(PDFTranslationContext::tr("Unsupported JBIG2 file - expected one page (%1).").arg(numberOfPages));
    }

    if (isFileOrganizationSequential)
    {
        // We are lucky, file organization is sequential. Just copy the data.
        m_data = m_reader.readSubstream(-1);
    }
    else
    {
        // We must transform random organization to the sequential one
        QByteArray sequentialData;

        struct SegmentInfo
        {
            PDFJBIG2SegmentHeader header;
            QByteArray headerData;
            QByteArray segmentData;
        };

        std::vector<SegmentInfo> segmentInfos;
        while (true)
        {
            const int headerStartPosition = m_reader.getPosition();
            SegmentInfo segmentInfo{ PDFJBIG2SegmentHeader::read(&m_reader), QByteArray(), QByteArray() };
            const int headerEndPosition = m_reader.getPosition();
            segmentInfo.headerData = m_data.mid(headerStartPosition, headerEndPosition - headerStartPosition);
            if (segmentInfos.size() >= MAX_JBIG2_SEGMENT_COUNT)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 segment count limit exceeded."));
            }
            consumeDecodedBytes(1024 + uint64_t(segmentInfo.header.getReferredSegments().size()) * 8);
            segmentInfos.push_back(qMove(segmentInfo));

            if (segmentInfos.back().header.getSegmentType() == JBIG2SegmentType::EndOfFile)
            {
                break;
            }
        }

        for (SegmentInfo& info : segmentInfos)
        {
            if (!info.header.isSegmentDataLengthDefined())
            {
                throw PDFException(PDFTranslationContext::tr("Invalid JBIG2 file - segment length is not defined."));
            }

            info.segmentData = m_reader.readSubstream(info.header.getSegmentDataLength());
        }

        if (!m_reader.isAtEnd())
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 extra data after random-access file segments."));
        }
        for (const SegmentInfo& info : segmentInfos)
        {
            sequentialData.append(info.headerData);
            sequentialData.append(info.segmentData);
        }

        m_data = qMove(sequentialData);
    }

    PDFImageData result = decode(PDFImageData::MaskingType::None);
    if (!m_fileEnded)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 file is missing its end-of-file segment."));
    }
    return result;
}

void PDFJBIG2Decoder::processStream()
{
    while (!m_reader.isAtEnd())
    {
        // Read the segment header, then process the segment data
        PDFJBIG2SegmentHeader segmentHeader = PDFJBIG2SegmentHeader::read(&m_reader);
        validateSegment(segmentHeader);
        consumeDecodedBytes(1024 + uint64_t(segmentHeader.getReferredSegments().size()) * 32);
        // The unknown data length is allowed for the immediate generic region only, see 7.2.7
        if (!segmentHeader.isSegmentDataLengthDefined() && (segmentHeader.getSegmentType() != JBIG2SegmentType::GenericRegion || !segmentHeader.isImmediate()))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 unknown data length of the segment %1 - it is allowed for an immediate generic region only.").arg(segmentHeader.getSegmentNumber()));
        }

        // Keep each handler inside its segment, including arithmetic lookahead and
        // nested refinement blocks. Restore the outer reader on every exit path.
        QByteArray segmentData;
        std::optional<PDFTemporaryValueChange<PDFBitReader>> segmentReaderGuard;
        if (segmentHeader.isSegmentDataLengthDefined())
        {
            const uint32_t length = segmentHeader.getSegmentDataLength();
            if (length > uint64_t(m_reader.getStream()->size() - m_reader.getPosition()) ||
                length > uint32_t(std::numeric_limits<int>::max()))
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid segment data length."));
            }
            consumeDecodedBytes(length);
            segmentData = m_reader.readSubstream(int(length));
            segmentReaderGuard.emplace(&m_reader, PDFBitReader(&segmentData, 8));
        }
        const int64_t segmentDataStartPosition = m_reader.getPosition();

        switch (segmentHeader.getSegmentType())
        {
            case JBIG2SegmentType::SymbolDictionary:
                processSymbolDictionary(segmentHeader);
                break;

            case JBIG2SegmentType::TextRegion:
                processTextRegion(segmentHeader);
                break;

            case JBIG2SegmentType::PatternDictionary:
                processPatternDictionary(segmentHeader);
                break;

            case JBIG2SegmentType::HalftoneRegion:
                processHalftoneRegion(segmentHeader);
                break;

            case JBIG2SegmentType::GenericRegion:
                processGenericRegion(segmentHeader);
                break;

            case JBIG2SegmentType::GenericRefinementRegion:
                processGenericRefinementRegion(segmentHeader);
                break;

            case JBIG2SegmentType::PageInformation:
                processPageInformation(segmentHeader);
                break;

            case JBIG2SegmentType::EndOfPage:
                processEndOfPage(segmentHeader);
                break;

            case JBIG2SegmentType::EndOfStripe:
                processEndOfStripe(segmentHeader);
                break;

            case JBIG2SegmentType::EndOfFile:
                processEndOfFile(segmentHeader);
                break;

            case JBIG2SegmentType::Profiles:
                processProfiles(segmentHeader);
                break;

            case JBIG2SegmentType::Tables:
                processCodeTables(segmentHeader);
                break;

            case JBIG2SegmentType::Extension:
                processExtension(segmentHeader);
                break;
        }

        // Make sure, that all data are processed by segment header. Positive offset means,
        // that we did not read all the data bytes. Negative offset means, that we read more
        // bytes in segment handler, that the segment has specified.
        if (segmentHeader.isSegmentDataLengthDefined())
        {
            const int64_t offset = static_cast<int64_t>(segmentDataStartPosition) + static_cast<int64_t>(segmentHeader.getSegmentDataLength()) - static_cast<int64_t>(m_reader.getPosition());
            if (offset > 0)
            {
                m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 bad segment data - handler doesn't process all segment data - %1 bytes left.").arg(offset));
            }
            else if (offset < 0)
            {
                // This is fatal error, we have read data, which doesn't belong to this segment
                throw PDFException(PDFTranslationContext::tr("JBIG2 bad segment data - handler reads %1 bytes past segment end.").arg(-offset));
            }

            // Always seek to the right position
            m_reader.seek(segmentDataStartPosition + segmentHeader.getSegmentDataLength());
        }
        m_segmentPages[segmentHeader.getSegmentNumber()] = segmentHeader.getPageAssociation();
        const auto stored = m_segments.find(segmentHeader.getSegmentNumber());
        if (stored != m_segments.end())
        {
            if (stored->second->asBitmap())
            {
                m_regionInformation[segmentHeader.getSegmentNumber()] = m_currentRegionInformation;
            }
        }
        releaseSegments(segmentHeader);
    }
}

void PDFJBIG2Decoder::validateSegment(const PDFJBIG2SegmentHeader& header)
{
    const uint32_t number = header.getSegmentNumber();
    const uint32_t page = header.getPageAssociation();
    const auto type = header.getSegmentType();
    if (m_fileEnded || m_seenSegments.size() >= MAX_JBIG2_SEGMENT_COUNT || !m_seenSegments.insert(number).second)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 duplicate segment, segment limit exceeded or data after EOF."));
    }
    if (m_readingGlobals && page != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 global segment is associated with a page."));
    }
    const bool region = type == JBIG2SegmentType::TextRegion || type == JBIG2SegmentType::HalftoneRegion ||
                        type == JBIG2SegmentType::GenericRegion || type == JBIG2SegmentType::GenericRefinementRegion;
    if (type == JBIG2SegmentType::PageInformation)
    {
        if (!page || m_pageAssociation)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 duplicate or unassociated page information; only one page is supported."));
        }
    }
    else if (region || type == JBIG2SegmentType::EndOfPage || type == JBIG2SegmentType::EndOfStripe)
    {
        if (!m_pageAssociation || page != m_pageAssociation || m_pageEnded)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid page association or segment after end of page."));
        }
    }
    else if ((page && m_pageAssociation && page != m_pageAssociation) ||
             (m_pageEnded && page && type != JBIG2SegmentType::EndOfFile))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid segment page association."));
    }
    // The two page flags only promise what the page will contain, so that a decoder can
    // reserve its buffers in advance - they are not needed to decode the segment. Files
    // which leave them at zero are still decodable, so they are reported and ignored.
    if (region && !header.isImmediate() && !m_pageMayUseAuxiliary && !m_auxiliaryBufferWarningReported)
    {
        m_auxiliaryBufferWarningReported = true;
        m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 intermediate region on a page which does not announce auxiliary buffers; the page flag is ignored."));
    }
    if (type == JBIG2SegmentType::GenericRefinementRegion && !m_pageMayRefine && !m_refinementFlagWarningReported)
    {
        m_refinementFlagWarningReported = true;
        m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 refinement region on a page which does not announce refinements; the page flag is ignored."));
    }

    size_t tables = 0;
    std::set<uint32_t> references;
    for (uint32_t ref : header.getReferredSegments())
    {
        if (!references.insert(ref).second)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 duplicate segment reference."));
        }
        if (type == JBIG2SegmentType::Extension)
        {
            const auto pageIt = m_segmentPages.find(ref);
            if (pageIt == m_segmentPages.end() || (pageIt->second != 0 && pageIt->second != page))
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid extension reference."));
            }
            continue;
        }
        const auto it = m_segments.find(ref);
        const auto pageIt = m_segmentPages.find(ref);
        if (it == m_segments.end() || pageIt == m_segmentPages.end() ||
            (pageIt->second != 0 && pageIt->second != page))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid referred segment or reference across pages."));
        }
        if (m_nonRetainedSegments.count(ref) && !m_retentionWarningReported)
        {
            m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 reuses a non-retained segment; kept under the decoding memory limit for compatibility."));
            m_retentionWarningReported = true;
        }
        const auto* segment = it->second.get();
        bool valid = false;
        switch (type)
        {
            case JBIG2SegmentType::SymbolDictionary:
            case JBIG2SegmentType::TextRegion:
                valid = segment->asSymbolDictionary() || segment->asHuffmanCodeTable();
                tables += segment->asHuffmanCodeTable() ? 1 : 0;
                break;
            case JBIG2SegmentType::HalftoneRegion:
                valid = segment->asPatternDictionary();
                break;
            case JBIG2SegmentType::GenericRefinementRegion:
                valid = segment->asBitmap();
                break;
            default:
                break;
        }
        if (!valid)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid referred segment type."));
        }
    }
    const size_t count = references.size();
    if ((type == JBIG2SegmentType::SymbolDictionary && tables > 4) ||
        (type == JBIG2SegmentType::TextRegion && tables > 8) ||
        (type == JBIG2SegmentType::HalftoneRegion && count != 1) ||
        (type == JBIG2SegmentType::GenericRefinementRegion && (count > 1 || (!header.isImmediate() && count != 1))))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid referred segment count."));
    }
}

void PDFJBIG2Decoder::discardSegment(uint32_t number)
{
    m_segments.erase(number);
    m_segmentPages.erase(number);
    m_regionInformation.erase(number);
}

void PDFJBIG2Decoder::releaseSegments(const PDFJBIG2SegmentHeader& header)
{
    const auto& flags = header.getRetainFlags();
    auto release = [&](uint32_t number)
    {
        if (header.isDeferredNonRetain())
        {
            m_deferredDiscards.insert(number);
        }
        else
        {
            // Some historical encoders clear retention on every stripe although
            // subsequent stripes reuse the dictionary. Keep the cached data until
            // end of page; the cumulative budget still charges every allocation.
            m_nonRetainedSegments.insert(number);
        }
    };
    if (!flags[0])
    {
        release(header.getSegmentNumber());
    }
    for (size_t i = 0; i < header.getReferredSegments().size(); ++i)
    {
        if (!flags[i + 1])
        {
            release(header.getReferredSegments()[i]);
        }
    }
    if (header.getSegmentType() == JBIG2SegmentType::EndOfPage)
    {
        for (uint32_t number : m_deferredDiscards)
        {
            discardSegment(number);
        }
        m_deferredDiscards.clear();
        for (uint32_t number : m_nonRetainedSegments)
        {
            discardSegment(number);
        }
        m_nonRetainedSegments.clear();
    }
}

void PDFJBIG2Decoder::paintPage(const PDFJBIG2Bitmap& bitmap, const PDFJBIG2RegionSegmentInformationField& field)
{
    if (!bitmap.isValid())
    {
        return;
    }
    const int64_t bottom = int64_t(field.offsetY) + bitmap.getHeight();
    if (m_pageStriped && bitmap.isValid())
    {
        if (field.offsetY <= m_lastStripeRow || bottom > m_lastStripeRow + 1 + m_maximumStripeHeight)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 region crosses a stripe boundary."));
        }
        m_stripeBottom = qMax(m_stripeBottom, bottom);
    }
    if (m_pageSizeUndefined && bottom > m_pageBitmap.getHeight())
    {
        checkBitmapSize(uint32_t(bottom));
        consumeDecodedBytes(uint64_t(m_pageBitmap.getWidth()) * (bottom - m_pageBitmap.getHeight()));
    }
    consumeWork(bitmap.getPixelCount());
    m_pageBitmap.paint(bitmap, field.offsetX, field.offsetY, field.operation, m_pageSizeUndefined, m_pageDefaultPixelValue);
}

void PDFJBIG2Decoder::checkRegionCompositionOperator(const PDFJBIG2RegionSegmentInformationField& field)
{
    if (!m_pageDefaultCompositionOperatorOverriden && field.operation != m_pageDefaultCompositionOperator &&
        !m_compositionOperatorWarningReported)
    {
        m_compositionOperatorWarningReported = true;
        m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 region combination operator contradicts the page flags; the operator of the region is used."));
    }
}

void PDFJBIG2Decoder::finishPage()
{
    if (m_pageSizeUndefined && (m_lastStripeRow < 0 || m_stripeBottom > m_lastStripeRow + 1))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 unknown page height requires a final end-of-stripe segment."));
    }
}

void PDFJBIG2Decoder::processSymbolDictionary(const PDFJBIG2SegmentHeader& header)
{
    // Only a dictionary using refinement/aggregate coding can be affected by the two
    // forms of the coding of a single symbol instance aggregation, so the flags are read
    // in advance - a dictionary, which does not use it, must not be decoded twice.
    constexpr uint16_t SDREFAGG_FLAG = 0x0002;
    const bool isRefinementAggregateUsed = (uint16_t(m_reader.look(16)) & SDREFAGG_FLAG) != 0;

    if (!isRefinementAggregateUsed)
    {
        processSymbolDictionaryImpl(header, false);
        return;
    }

    const int segmentDataStartPosition = m_reader.getPosition();
    QString errorMessage;

    try
    {
        processSymbolDictionaryImpl(header, false);
        return;
    }
    catch (const PDFException& exception)
    {
        // The symbol dictionary has not been decoded by the procedure of the specification.
        // The Power JBIG-2 encoder of the University of British Columbia, which produced the
        // test streams of the format, codes a symbol aggregated from a single symbol instance
        // by the whole text region decoding procedure, and not by the shortened form of
        // 6.5.8.2.2, which decodes only the values, that the text region does not know in
        // advance. The two forms are not distinguishable in the data, so the dictionary is
        // decoded again by the other one - the segment is read from the beginning, and
        // nothing has been stored yet, because the decoded symbols are stored as the last
        // step of the decoding.
        errorMessage = exception.getMessage();
        m_reader.seek(segmentDataStartPosition);
    }

    try
    {
        processSymbolDictionaryImpl(header, true);
    }
    catch (const PDFException&)
    {
        // Neither form decodes the dictionary - the error of the form of the specification
        // is reported, the other form is just a fallback
        throw PDFException(errorMessage);
    }

    m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 symbol dictionary uses the text region decoding procedure for a single symbol instance aggregation."));
}

void PDFJBIG2Decoder::processSymbolDictionaryImpl(const PDFJBIG2SegmentHeader& header, bool isSingleInstanceAggregateTextRegion)
{
    /* 7.4.2.2 step 1) */
    PDFJBIG2SymbolDictionaryDecodingParameters parameters;
    const uint16_t symbolDictionaryFlags = m_reader.readUnsignedWord();
    parameters.SDHUFF = symbolDictionaryFlags & 0x0001;
    parameters.SDREFAGG = symbolDictionaryFlags & 0x0002;
    parameters.SDHUFFDH = (symbolDictionaryFlags >> 2) & 0x0003;
    parameters.SDHUFFDW = (symbolDictionaryFlags >> 4) & 0x0003;
    parameters.SDHUFFBMSIZE = (symbolDictionaryFlags >> 6) & 0x0001;
    parameters.SDHUFFAGGINST = (symbolDictionaryFlags >> 7) & 0x0001;
    parameters.isArithmeticCodingStateUsed = (symbolDictionaryFlags >> 8) & 0x0001;
    parameters.isArithmeticCodingStateRetained = (symbolDictionaryFlags >> 9) & 0x0001;
    parameters.SDTEMPLATE = (symbolDictionaryFlags >> 10) & 0x0003;
    parameters.SDRTEMPLATE = (symbolDictionaryFlags >> 12) & 0x0001;
    parameters.SDAT = readATTemplatePixelPositions((!parameters.SDHUFF) ? ((parameters.SDTEMPLATE == 0) ? 4 : 1) : 0);
    parameters.SDRAT = readATTemplatePixelPositions((parameters.SDREFAGG && parameters.SDRTEMPLATE == 0) ? 2 : 0, true);
    parameters.SDNUMEXSYMS = m_reader.readUnsignedInt();
    parameters.SDNUMNEWSYMS = m_reader.readUnsignedInt();

    /* sanity checks */

    // Both counts are used to allocate arrays of the symbols before anything is
    // decoded, so they must be limited - otherwise a four byte field of a malformed
    // document requests an allocation of billions of bitmaps.
    if (parameters.SDNUMEXSYMS > MAX_SYMBOL_COUNT || parameters.SDNUMNEWSYMS > MAX_SYMBOL_COUNT)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 maximum symbol count exceeded (%1 / %2 > %3).").arg(parameters.SDNUMEXSYMS).arg(parameters.SDNUMNEWSYMS).arg(MAX_SYMBOL_COUNT));
    }

    if ((symbolDictionaryFlags >> 13) != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
    }

    if (!parameters.SDHUFF || !parameters.SDREFAGG)
    {
        if (parameters.SDHUFFAGGINST != 0)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
        }
    }

    if (!parameters.SDHUFF)
    {
        // SDHUFFAGGINST has been checked above
        if (parameters.SDHUFFDH != 0 || parameters.SDHUFFDW != 0 || parameters.SDHUFFBMSIZE != 0)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
        }
    }
    else
    {
        if (!parameters.SDREFAGG && (parameters.isArithmeticCodingStateUsed || parameters.isArithmeticCodingStateRetained || parameters.SDRTEMPLATE != 0))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
        }

        if (parameters.SDTEMPLATE != 0)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid flags for symbol dictionary segment."));
        }
    }

    /* 7.4.2.2 step 2) */
    PDFJBIG2ReferencedSegments references = getReferencedSegments(header);
    parameters.SDINSYMS = references.getSymbolBitmaps();
    parameters.SDNUMINSYMS = static_cast<uint32_t>(parameters.SDINSYMS.size());
    if (parameters.SDNUMNEWSYMS > MAX_SYMBOL_COUNT - parameters.SDNUMINSYMS)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 maximum combined symbol count exceeded."));
    }

    if (parameters.isArithmeticCodingStateUsed &&
        (references.symbolDictionaries.empty() ||
         !references.symbolDictionaries.back()->canReuse(symbolDictionaryFlags, parameters.SDAT, parameters.SDRAT)))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 incompatible or unretained arithmetic coding context."));
    }
    consumeDecodedBytes(2 * 65536);

    /* Arithmetic decoder stats */
    PDFJBIG2ArithmeticDecoderStates arithmeticDecoderStates;

    /* 7.4.2.1.6 - huffman table selection */

    if (parameters.SDHUFF)
    {
        switch (parameters.SDHUFFDH)
        {
            case 0:
                parameters.SDHUFFDH_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_D), std::end(PDFJBIG2StandardHuffmanTable_D));
                break;

            case 1:
                parameters.SDHUFFDH_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_E), std::end(PDFJBIG2StandardHuffmanTable_E));
                break;

            case 3:
                parameters.SDHUFFDH_Decoder = references.getUserTable(&m_reader, &m_workRemaining);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (parameters.SDHUFFDW)
        {
            case 0:
                parameters.SDHUFFDW_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_B), std::end(PDFJBIG2StandardHuffmanTable_B));
                break;

            case 1:
                parameters.SDHUFFDW_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_C), std::end(PDFJBIG2StandardHuffmanTable_C));
                break;

            case 3:
                parameters.SDHUFFDW_Decoder = references.getUserTable(&m_reader, &m_workRemaining);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        // Both selections are single bit fields
        if (parameters.SDHUFFBMSIZE == 0)
        {
            parameters.SDHUFFBMSIZE_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));
        }
        else
        {
            parameters.SDHUFFBMSIZE_Decoder = references.getUserTable(&m_reader, &m_workRemaining);
        }

        if (parameters.SDHUFFAGGINST == 0)
        {
            parameters.SDHUFFAGGINST_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));
        }
        else
        {
            parameters.SDHUFFAGGINST_Decoder = references.getUserTable(&m_reader, &m_workRemaining);
        }

        if (parameters.SDHUFFDH_Decoder.hasOutOfBand() || !parameters.SDHUFFDW_Decoder.hasOutOfBand() ||
            parameters.SDHUFFBMSIZE_Decoder.hasOutOfBand() || parameters.SDHUFFAGGINST_Decoder.hasOutOfBand())
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid OOB capability of dictionary huffman table."));
        }
        parameters.EXRUNLENGTH_Decoder = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));

        if (references.currentUserCodeTableIndex != references.codeTables.size())
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid number of huffam code table - %1 unused.").arg(references.codeTables.size() - references.currentUserCodeTableIndex));
        }
    }
    else
    {
        /* 7.4.2.2 step 3) and 4) - initialize arithmetic encoder */
        if (parameters.isArithmeticCodingStateUsed)
        {
            if (references.symbolDictionaries.empty())
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 trying to use aritmetic decoder context from previous symbol dictionary, but it doesn't exist."));
            }

            arithmeticDecoderStates.resetArithmeticStatesGeneric(parameters.SDTEMPLATE, &references.symbolDictionaries.back()->getGenericState());
        }
        else
        {
            arithmeticDecoderStates.resetArithmeticStatesGeneric(parameters.SDTEMPLATE, nullptr);
        }
    }

    if (parameters.SDREFAGG)
    {
        if (parameters.isArithmeticCodingStateUsed)
        {
            if (references.symbolDictionaries.empty())
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 trying to use aritmetic decoder context from previous symbol dictionary, but it doesn't exist."));
            }

            arithmeticDecoderStates.resetArithmeticStatesGenericRefinement(parameters.SDRTEMPLATE, &references.symbolDictionaries.back()->getGenericRefinementState());
        }
        else
        {
            arithmeticDecoderStates.resetArithmeticStatesGenericRefinement(parameters.SDRTEMPLATE, nullptr);
        }
    }

    uint8_t SBSYMCODELENGTH = log2ceil(parameters.SDNUMINSYMS + parameters.SDNUMNEWSYMS);
    if (parameters.SDHUFF)
    {
        SBSYMCODELENGTH = qMax<uint8_t>(SBSYMCODELENGTH, 1);
    }

    consumeDecodedBytes((uint64_t(1) << SBSYMCODELENGTH) + 2 * 65536 + 13 * 512);
    consumeDecodedBytes(uint64_t(parameters.SDNUMNEWSYMS + parameters.SDNUMINSYMS) *
                         (2 * sizeof(PDFJBIG2Bitmap) + sizeof(PDFJBIG2HuffmanTableEntry) * 2 + 16));
    arithmeticDecoderStates.resetArithmeticStatesInteger(SBSYMCODELENGTH);
    PDFJBIG2ArithmeticDecoder arithmeticDecoder(&m_reader, &m_workRemaining);
    if (!parameters.SDHUFF)
    {
        arithmeticDecoder.initialize();
    }

    // The standard huffman tables of the refinement/aggregate path (6.5.8.2) are the same
    // for every symbol, so they are built once here instead of for each decoded symbol.
    // Only SBSYMCODES depends on the number of the already decoded symbols and stays in
    // the loop, as do all the plain value fields of the text region parameters.
    PDFJBIG2HuffmanDecoder refinementDecoderO;
    PDFJBIG2HuffmanDecoder refinementDecoderA;
    PDFJBIG2TextRegionDecodingParameters aggregateTextParameters;
    if (parameters.SDREFAGG)
    {
        if (parameters.SDHUFF)
        {
            refinementDecoderO = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
            refinementDecoderA = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));
        }

        aggregateTextParameters.SBHUFFFS = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_F), std::end(PDFJBIG2StandardHuffmanTable_F));
        aggregateTextParameters.SBHUFFDS = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_H), std::end(PDFJBIG2StandardHuffmanTable_H));
        aggregateTextParameters.SBHUFFDT = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_K), std::end(PDFJBIG2StandardHuffmanTable_K));
        aggregateTextParameters.SBHUFFRDW = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
        aggregateTextParameters.SBHUFFRDH = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
        aggregateTextParameters.SBHUFFRDX = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
        aggregateTextParameters.SBHUFFRDY = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
        aggregateTextParameters.SBHUFFRSIZE = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));
    }

    /* 6.5.5 - algorithm for decoding symbol dictionary */

    /* 6.5.5 step 1) - create output bitmaps */
    parameters.SDNEWSYMS.resize(parameters.SDNUMNEWSYMS);

    /* 6.5.5 step 2) - initalize width array */
    if (parameters.SDHUFF == 1 && parameters.SDREFAGG == 0)
    {
        parameters.SDNEWSYMWIDTHS.resize(parameters.SDNUMNEWSYMS, 0);
    }

    /* 6.5.5 step 3) - initalize variables to zero */
    uint32_t HCHEIGHT = 0;
    uint32_t NSYMSDECODED = 0;

    /* 6.5.5 step 4) - read all bitmaps */
    while (NSYMSDECODED < parameters.SDNUMNEWSYMS)
    {
        /* 6.5.5 step 4) b) - decode height class delta height according to 6.5.6 */
        int32_t HCDH = checkInteger(parameters.SDHUFF ? parameters.SDHUFFDH_Decoder.readSignedInteger() : arithmeticDecoder.getSignedInteger(&arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::IADH]));
        const int64_t height = int64_t(HCHEIGHT) + HCDH;
        if (height < 0 || height > MAX_BITMAP_SIZE)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid symbol height."));
        }
        HCHEIGHT = uint32_t(height);
        uint32_t SYMWIDTH = 0;
        uint32_t TOTWIDTH = 0;
        uint32_t HCFIRSTSYM = NSYMSDECODED;

        /* 6.5.5 step 4) c) - read height class */
        while (NSYMSDECODED <= parameters.SDNUMNEWSYMS)
        {
            /* 6.5.5 step 4) c) i) - Delta width acc. to 6.5.7 */
            std::optional<int32_t> DW = parameters.SDHUFF ? parameters.SDHUFFDW_Decoder.readSignedInteger() : arithmeticDecoder.getSignedInteger(&arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::IADW]);

            if (!DW.has_value())
            {
                // All symbols of this height class have been decoded
                break;
            }

            if (NSYMSDECODED >= parameters.SDNUMNEWSYMS)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 symbol height class has more symbols, than defined in the symbol dictionary header."));
            }

            const int64_t width = int64_t(SYMWIDTH) + *DW;
            if (width < 0 || width > MAX_BITMAP_SIZE || (parameters.SDHUFF && !parameters.SDREFAGG && uint64_t(TOTWIDTH) + uint64_t(width) > MAX_BITMAP_SIZE))
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid symbol or collective bitmap width."));
            }
            SYMWIDTH = uint32_t(width);
            if (parameters.SDHUFF && !parameters.SDREFAGG)
            {
                TOTWIDTH += SYMWIDTH;
            }

            if (parameters.SDHUFF == 0 || parameters.SDREFAGG == 1)
            {
                /* 6.5.5 step 4) c) ii) - read bitmap acc. to 6.5.8 */

                if (parameters.SDREFAGG == 0)
                {
                    /* 6.5.8.1 Direct-coded symbol bitmap, using Table 16 */
                    PDFJBIG2BitmapDecodingParameters bitmapParameters;
                    bitmapParameters.MMR = false;
                    bitmapParameters.GBW = SYMWIDTH;
                    bitmapParameters.GBH = HCHEIGHT;
                    bitmapParameters.GBTEMPLATE = parameters.SDTEMPLATE;
                    bitmapParameters.TPGDON = false;
                    bitmapParameters.GBAT = parameters.SDAT;
                    bitmapParameters.arithmeticDecoder = &arithmeticDecoder;
                    bitmapParameters.arithmeticDecoderState = &arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::Generic];
                    parameters.SDNEWSYMS[NSYMSDECODED] = readBitmap(bitmapParameters);
                }
                else
                {
                    /* 6.5.8.2 Refinement/aggregate-coded symbol bitmap */
                    int32_t REFAGGNINST = checkInteger(parameters.SDHUFF ? parameters.SDHUFFAGGINST_Decoder.readSignedInteger() : arithmeticDecoder.getSignedInteger(&arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::IAAI]));

                    // 6.5.8.2 defines the decoding only for a value equal to one and for
                    // a value greater than one, so anything else is invalid
                    if (REFAGGNINST < 1)
                    {
                        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid number of symbol instances in the aggregation (%1).").arg(REFAGGNINST));
                    }

                    if (REFAGGNINST == 1 && !isSingleInstanceAggregateTextRegion)
                    {
                        uint32_t ID = 0;
                        int32_t RDXI = 0;
                        int32_t RDYI = 0;
                        QByteArray refinementData;
                        PDFBitReader refinementReader(&refinementData, 8);
                        PDFJBIG2ArithmeticDecoder refinementDecoder(&refinementReader, &m_workRemaining);

                        if (parameters.SDHUFF)
                        {
                            ID = m_reader.read(SBSYMCODELENGTH);
                            RDXI = checkInteger(refinementDecoderO.readSignedInteger());
                            RDYI = checkInteger(refinementDecoderO.readSignedInteger());
                            const int32_t BMSIZE = checkInteger(refinementDecoderA.readSignedInteger());
                            refinementData = readRefinementData(&m_reader, BMSIZE);
                            refinementDecoder.initialize();
                        }
                        else
                        {
                            ID = arithmeticDecoder.getIAID(SBSYMCODELENGTH, &arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::IAID]);
                            RDXI = checkInteger(arithmeticDecoder.getSignedInteger(&arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::IARDX]));
                            RDYI = checkInteger(arithmeticDecoder.getSignedInteger(&arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::IARDY]));
                        }

                        if (ID >= parameters.SDNUMINSYMS + NSYMSDECODED)
                        {
                            throw PDFException(PDFTranslationContext::tr("Trying to use reference bitmap %1, but number of decoded bitmaps is %2.").arg(ID).arg(parameters.SDNUMINSYMS + NSYMSDECODED));
                        }

                        // Decode the bitmap
                        PDFJBIG2BitmapRefinementDecodingParameters refinementParameters;
                        refinementParameters.GRW = SYMWIDTH;
                        refinementParameters.GRH = HCHEIGHT;
                        refinementParameters.GRTEMPLATE = parameters.SDRTEMPLATE;
                        refinementParameters.GRREFERENCE =  (ID < parameters.SDNUMINSYMS) ? parameters.SDINSYMS[ID] : &parameters.SDNEWSYMS[ID - parameters.SDNUMINSYMS];
                        refinementParameters.GRREFERENCEX = RDXI;
                        refinementParameters.GRREFERENCEY = RDYI;
                        refinementParameters.TPGRON = false;
                        refinementParameters.GRAT = parameters.SDRAT;
                        refinementParameters.decoder = parameters.SDHUFF ? &refinementDecoder : &arithmeticDecoder;
                        refinementParameters.arithmeticDecoderState = &arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::Refinement];
                        parameters.SDNEWSYMS[NSYMSDECODED] = readRefinementBitmap(refinementParameters);

                        if (parameters.SDHUFF)
                        {
                            refinementDecoder.finalize();
                        }
                    }
                    else
                    {
                        // Use table 17 to decode text region bitmap. The huffman tables of
                        // aggregateTextParameters have been built before the symbol loop
                        PDFJBIG2TextRegionDecodingParameters& textParameters = aggregateTextParameters;
                        textParameters.SBSYMS.clear();
                        textParameters.SBHUFF = parameters.SDHUFF;
                        textParameters.SBREFINE = true;
                        textParameters.SBDEFPIXEL = 0;
                        textParameters.SBCOMBOP = PDFJBIG2BitOperation::Or;
                        textParameters.TRANSPOSED = false;
                        textParameters.REFCORNER = PDFJBIG2TextRegionDecodingParameters::TOPLEFT;
                        textParameters.SBDSOFFSET = 0;
                        textParameters.SBW = SYMWIDTH;
                        textParameters.SBH = HCHEIGHT;
                        textParameters.SBNUMINSTANCES = uint32_t(REFAGGNINST);
                        textParameters.LOG2SBSTRIPS = 0;
                        textParameters.SBSTRIPS = 1;
                        consumeDecodedBytes(uint64_t(parameters.SDNUMINSYMS + NSYMSDECODED) *
                                            (2 * sizeof(PDFJBIG2HuffmanTableEntry) + 2 * sizeof(void*)));
                        textParameters.SBSYMS.reserve(parameters.SDNUMINSYMS + NSYMSDECODED);
                        textParameters.SBSYMS.insert(textParameters.SBSYMS.end(), parameters.SDINSYMS.begin(), parameters.SDINSYMS.end());

                        for (uint32_t i = 0; i < NSYMSDECODED; ++i)
                        {
                            textParameters.SBSYMS.push_back(&parameters.SDNEWSYMS[i]);
                        }
                        textParameters.SBNUMSYMS = static_cast<uint32_t>(textParameters.SBSYMS.size());
                        textParameters.SBSYMCODELEN = SBSYMCODELENGTH;
                        textParameters.SBRTEMPLATE = parameters.SDRTEMPLATE;
                        textParameters.SBRAT = parameters.SDRAT;
                        textParameters.arithmeticDecoder = &arithmeticDecoder;
                        textParameters.reader = &m_reader;
                        textParameters.initializeFrom(&arithmeticDecoderStates);

                        std::vector<PDFJBIG2HuffmanTableEntry> symbols(textParameters.SBNUMSYMS, PDFJBIG2HuffmanTableEntry());
                        for (uint32_t i = 0; i < textParameters.SBNUMSYMS; ++i)
                        {
                            symbols[i].value = i;
                            symbols[i].prefixBitLength = SBSYMCODELENGTH;
                        }

                        textParameters.SBSYMCODES = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, PDFJBIG2HuffmanCodeTable::buildPrefixes(symbols));

                        // Now, we can read the bitmap using text decode procedure
                        parameters.SDNEWSYMS[NSYMSDECODED] = readTextBitmap(textParameters);
                    }
                }
            }
            else
            {
                /* 6.5.5 step 4) c) iii) - update value of widths */
                parameters.SDNEWSYMWIDTHS[NSYMSDECODED] = SYMWIDTH;
            }

            /* 6.5.5 step 4) c) iv) - update decoded symbols counter */
            ++NSYMSDECODED;
        }

        if (NSYMSDECODED == HCFIRSTSYM)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 empty symbol height class."));
        }

        /* 6.5.5 step 4) d) - create collective bitmap */
        if (parameters.SDHUFF && parameters.SDREFAGG == 0)
        {
            PDFJBIG2Bitmap collectiveBitmap;
            int32_t BMSIZE = checkInteger(parameters.SDHUFFBMSIZE_Decoder.readSignedInteger());
            if (BMSIZE < 0)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid collective bitmap data length."));
            }
            m_reader.alignToBytes();

            if (BMSIZE == 0)
            {
                // Uncompressed data
                consumeDecodedBytes(uint64_t(TOTWIDTH) * HCHEIGHT);
                collectiveBitmap = PDFJBIG2Bitmap(TOTWIDTH, HCHEIGHT, 0x00);
                // BMSIZE is computed BMSIZE = HCHEIGHT * (TOTWIDTH + 7) / 8;
                for (uint32_t y = 0; y < HCHEIGHT; ++y)
                {
                    for (uint32_t x = 0; x < TOTWIDTH; ++x)
                    {
                        collectiveBitmap.setPixel(x, y, m_reader.read(1) ? 0xFF : 0x00);
                    }

                    m_reader.alignToBytes();
                }
            }
            else
            {
                PDFJBIG2BitmapDecodingParameters bitmapParameters;
                bitmapParameters.MMR = true;
                bitmapParameters.GBW = TOTWIDTH;
                bitmapParameters.GBH = HCHEIGHT;
                consumeDecodedBytes(uint64_t(BMSIZE));
                bitmapParameters.data = m_reader.readSubstream(BMSIZE);
                collectiveBitmap = readBitmap(bitmapParameters);
            }

            m_reader.alignToBytes();

            for (int32_t x = 0; HCFIRSTSYM < NSYMSDECODED; ++HCFIRSTSYM)
            {
                consumeDecodedBytes(uint64_t(parameters.SDNEWSYMWIDTHS[HCFIRSTSYM]) * HCHEIGHT);
                parameters.SDNEWSYMS[HCFIRSTSYM] = collectiveBitmap.getSubbitmap(x, 0, parameters.SDNEWSYMWIDTHS[HCFIRSTSYM], HCHEIGHT);
                x += parameters.SDNEWSYMWIDTHS[HCFIRSTSYM];
            }
        }
    }

    /* 6.5.5 step 5) - determine exports according to 6.5.10 */
    std::vector<bool> EXFLAGS;
    const size_t symbolsSize = parameters.SDNUMINSYMS + parameters.SDNEWSYMS.size();
    EXFLAGS.reserve(symbolsSize);
    bool CUREXFLAG = false;
    size_t exportRuns = 0;
    while (EXFLAGS.size() < symbolsSize)
    {
        if (++exportRuns > 2 * symbolsSize + 1)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 excessive export flag runs."));
        }
        const uint32_t EXRUNLENGTH = static_cast<uint32_t>(checkInteger(parameters.SDHUFF ? parameters.EXRUNLENGTH_Decoder.readSignedInteger() : arithmeticDecoder.getSignedInteger(&arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::IAEX])));

        if (EXRUNLENGTH + EXFLAGS.size() > symbolsSize)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid export flags in symbol dictionary."));
        }

        EXFLAGS.insert(EXFLAGS.end(), EXRUNLENGTH, CUREXFLAG);
        CUREXFLAG = !CUREXFLAG;
    }
    m_reader.alignToBytes();
    if (!parameters.SDHUFF)
    {
        // Skipneme 1 byte na konci
        arithmeticDecoder.finalize();
    }

    if (size_t(std::count(EXFLAGS.begin(), EXFLAGS.end(), true)) != parameters.SDNUMEXSYMS)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid exported symbol count."));
    }
    std::vector<PDFJBIG2Bitmap> bitmaps;
    bitmaps.reserve(parameters.SDNUMEXSYMS);

    // Insert input bitmaps
    for (size_t i = 0; i < parameters.SDNUMINSYMS; ++i)
    {
        if (EXFLAGS[i])
        {
            consumeDecodedBytes(parameters.SDINSYMS[i]->getPixelCount());
            bitmaps.push_back(*parameters.SDINSYMS[i]);
        }
    }

    // Insert output bitmaps
    for (size_t i = 0; i < NSYMSDECODED; ++i)
    {
        if (EXFLAGS[i + parameters.SDNUMINSYMS])
        {
            bitmaps.push_back(qMove(parameters.SDNEWSYMS[i]));
        }
    }

    PDFJBIG2ArithmeticDecoderState savedGeneric;
    PDFJBIG2ArithmeticDecoderState savedRefine;

    if (parameters.isArithmeticCodingStateRetained)
    {
        savedGeneric = qMove(arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::Generic]);
        savedRefine = qMove(arithmeticDecoderStates.states[PDFJBIG2ArithmeticDecoderStates::Refinement]);
    }

    m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2SymbolDictionary>(qMove(bitmaps), qMove(savedGeneric), qMove(savedRefine), symbolDictionaryFlags, parameters.SDAT, parameters.SDRAT);
}

void PDFJBIG2Decoder::processTextRegion(const PDFJBIG2SegmentHeader& header)
{
    // Combination operators of the two bit field SBCOMBOP, see 7.4.3.1.1
    constexpr std::array<PDFJBIG2BitOperation, 4> combinationOperators = { PDFJBIG2BitOperation::Or, PDFJBIG2BitOperation::And, PDFJBIG2BitOperation::Xor, PDFJBIG2BitOperation::NotXor };

    PDFJBIG2RegionSegmentInformationField regionSegmentInfo = readRegionSegmentInformationField();
    checkRegionCompositionOperator(regionSegmentInfo);
    const uint16_t flags = m_reader.readUnsignedWord();
    const bool SBHUFF = flags & 0x0001;
    const bool SBREFINE = flags & 0x0002;
    const uint8_t LOG2SBSTRIPS = ((flags >> 2) & 0x03);
    const uint8_t SBSTRIPS = 1 << LOG2SBSTRIPS;
    const uint8_t REFCORNER = (flags >> 4) & 0x03;
    const bool TRANSPOSED = (flags >> 6) & 0x01;
    const PDFJBIG2BitOperation SBCOMBOOP = combinationOperators[(flags >> 7) & 0x03];
    const uint8_t SBDEFPIXEL = ((flags >> 9) & 0x01) ? 0xFF : 0x00;
    const int32_t SBDSOFFSET = (flags >> 10) & 0x1F;
    const uint8_t SBRTEMPLATE = (flags >> 15) & 0x01;
    const int32_t SBDSOFFSET_SIGNED = (SBDSOFFSET & 0b10000) ? (SBDSOFFSET - 0b100000) : SBDSOFFSET;

    // Decoding parameters
    PDFJBIG2TextRegionDecodingParameters parameters;
    parameters.SBHUFF = SBHUFF;
    parameters.SBREFINE = SBREFINE;
    parameters.SBDEFPIXEL = SBDEFPIXEL;
    parameters.SBCOMBOP = SBCOMBOOP;
    parameters.TRANSPOSED = TRANSPOSED;
    parameters.REFCORNER = REFCORNER;
    parameters.SBDSOFFSET = SBDSOFFSET_SIGNED;
    parameters.SBW = regionSegmentInfo.width;
    parameters.SBH = regionSegmentInfo.height;
    parameters.SBRTEMPLATE = SBRTEMPLATE;
    parameters.SBSTRIPS = SBSTRIPS;
    parameters.LOG2SBSTRIPS = LOG2SBSTRIPS;

    // Referenced segments data
    PDFJBIG2ReferencedSegments references = getReferencedSegments(header);

    if (SBHUFF)
    {
        uint16_t huffmanFlags = m_reader.readUnsignedWord();

        auto readHuffmanTableSelection = [&huffmanFlags]() -> uint8_t
        {
            const uint8_t result = huffmanFlags & 0x03;
            huffmanFlags = huffmanFlags >> 2;
            return result;
        };

        const uint8_t SBHUFFFS = readHuffmanTableSelection();
        const uint8_t SBHUFFDS = readHuffmanTableSelection();
        const uint8_t SBHUFFDT = readHuffmanTableSelection();
        const uint8_t SBHUFFRDW = readHuffmanTableSelection();
        const uint8_t SBHUFFRDH = readHuffmanTableSelection();
        const uint8_t SBHUFFRDX = readHuffmanTableSelection();
        const uint8_t SBHUFFRDY = readHuffmanTableSelection();
        // The reserved bit 15 is read as the upper bit of the selection SBHUFFRSIZE, so
        // it is refused as an invalid selection
        const uint8_t SBHUFFRSIZE = readHuffmanTableSelection();

        // Create huffman tables
        switch (SBHUFFFS)
        {
            case 0:
                parameters.SBHUFFFS = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_F), std::end(PDFJBIG2StandardHuffmanTable_F));
                break;

            case 1:
                parameters.SBHUFFFS = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_G), std::end(PDFJBIG2StandardHuffmanTable_G));
                break;

            case 3:
                parameters.SBHUFFFS = references.getUserTable(&m_reader, &m_workRemaining);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        // Every value of the two bit selections of SBHUFFDS and SBHUFFDT is valid
        if (SBHUFFDS == 0)
        {
            parameters.SBHUFFDS = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_H), std::end(PDFJBIG2StandardHuffmanTable_H));
        }
        else if (SBHUFFDS == 1)
        {
            parameters.SBHUFFDS = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_I), std::end(PDFJBIG2StandardHuffmanTable_I));
        }
        else if (SBHUFFDS == 2)
        {
            parameters.SBHUFFDS = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_J), std::end(PDFJBIG2StandardHuffmanTable_J));
        }
        else
        {
            parameters.SBHUFFDS = references.getUserTable(&m_reader, &m_workRemaining);
        }

        if (SBHUFFDT == 0)
        {
            parameters.SBHUFFDT = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_K), std::end(PDFJBIG2StandardHuffmanTable_K));
        }
        else if (SBHUFFDT == 1)
        {
            parameters.SBHUFFDT = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_L), std::end(PDFJBIG2StandardHuffmanTable_L));
        }
        else if (SBHUFFDT == 2)
        {
            parameters.SBHUFFDT = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_M), std::end(PDFJBIG2StandardHuffmanTable_M));
        }
        else
        {
            parameters.SBHUFFDT = references.getUserTable(&m_reader, &m_workRemaining);
        }

        switch (SBHUFFRDW)
        {
            case 0:
                parameters.SBHUFFRDW = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_N), std::end(PDFJBIG2StandardHuffmanTable_N));
                break;

            case 1:
                parameters.SBHUFFRDW = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
                break;

            case 3:
                parameters.SBHUFFRDW = references.getUserTable(&m_reader, &m_workRemaining);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRDH)
        {
            case 0:
                parameters.SBHUFFRDH = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_N), std::end(PDFJBIG2StandardHuffmanTable_N));
                break;

            case 1:
                parameters.SBHUFFRDH = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
                break;

            case 3:
                parameters.SBHUFFRDH = references.getUserTable(&m_reader, &m_workRemaining);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRDX)
        {
            case 0:
                parameters.SBHUFFRDX = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_N), std::end(PDFJBIG2StandardHuffmanTable_N));
                break;

            case 1:
                parameters.SBHUFFRDX = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
                break;

            case 3:
                parameters.SBHUFFRDX = references.getUserTable(&m_reader, &m_workRemaining);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRDY)
        {
            case 0:
                parameters.SBHUFFRDY = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_N), std::end(PDFJBIG2StandardHuffmanTable_N));
                break;

            case 1:
                parameters.SBHUFFRDY = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_O), std::end(PDFJBIG2StandardHuffmanTable_O));
                break;

            case 3:
                parameters.SBHUFFRDY = references.getUserTable(&m_reader, &m_workRemaining);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }

        switch (SBHUFFRSIZE)
        {
            case 0:
                parameters.SBHUFFRSIZE = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, std::begin(PDFJBIG2StandardHuffmanTable_A), std::end(PDFJBIG2StandardHuffmanTable_A));
                break;

            case 1:
                parameters.SBHUFFRSIZE = references.getUserTable(&m_reader, &m_workRemaining);
                break;

            default:
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
        }
    }

    if (SBHUFF && (references.currentUserCodeTableIndex != references.codeTables.size() ||
        parameters.SBHUFFFS.hasOutOfBand() || !parameters.SBHUFFDS.hasOutOfBand() || parameters.SBHUFFDT.hasOutOfBand() ||
        parameters.SBHUFFRDW.hasOutOfBand() || parameters.SBHUFFRDH.hasOutOfBand() ||
        parameters.SBHUFFRDX.hasOutOfBand() || parameters.SBHUFFRDY.hasOutOfBand() || parameters.SBHUFFRSIZE.hasOutOfBand()))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid text region huffman tables."));
    }

    if (SBREFINE && SBRTEMPLATE == 0)
    {
        parameters.SBRAT = readATTemplatePixelPositions(2, true);
    }

    parameters.SBSYMS = references.getSymbolBitmaps();
    parameters.SBNUMSYMS = static_cast<uint32_t>(parameters.SBSYMS.size());
    parameters.SBNUMINSTANCES = m_reader.readUnsignedInt();
    parameters.SBSYMCODELEN = log2ceil(parameters.SBNUMSYMS);

    if (parameters.SBNUMSYMS == 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 no referred symbols in text region segment."));
    }

    PDFJBIG2ArithmeticDecoder decoder(&m_reader, &m_workRemaining);
    if (SBHUFF)
    {
        // Read run code lengths
        std::vector<PDFJBIG2HuffmanTableEntry> rangeLengthTable(35, PDFJBIG2HuffmanTableEntry());
        for (int32_t i = 0; i < static_cast<int32_t>(rangeLengthTable.size()); ++i)
        {
            rangeLengthTable[i].value = i;
            rangeLengthTable[i].prefixBitLength = m_reader.read(4);
        }
        rangeLengthTable = PDFJBIG2HuffmanCodeTable::buildPrefixes(rangeLengthTable);
        PDFJBIG2HuffmanDecoder runLengthDecoder(&m_reader, &m_workRemaining, qMove(rangeLengthTable));

        consumeDecodedBytes(uint64_t(parameters.SBNUMSYMS) * sizeof(PDFJBIG2HuffmanTableEntry) * 4);
        std::vector<PDFJBIG2HuffmanTableEntry> symCodeTable(parameters.SBNUMSYMS, PDFJBIG2HuffmanTableEntry());
        for (uint32_t i = 0; i < parameters.SBNUMSYMS;)
        {
            symCodeTable[i].value = i;
            uint32_t code = checkInteger(runLengthDecoder.readSignedInteger());
            switch (code)
            {
                default:
                    symCodeTable[i++].prefixBitLength = code;
                    break;

                case 32:
                case 33:
                case 34:
                {
                    // The run codes of the table 29 - the run code 32 repeats the previous
                    // length, the run codes 33 and 34 repeat the length zero
                    uint32_t length = 0;
                    uint32_t range = 0;

                    if (code == 32)
                    {
                        if (i == 0)
                        {
                            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid symbol length code table for text region segment."));
                        }
                        length = symCodeTable[i - 1].prefixBitLength;
                        range = m_reader.read(2) + 3;
                    }
                    else if (code == 33)
                    {
                        range = m_reader.read(3) + 3;
                    }
                    else
                    {
                        range = m_reader.read(7) + 11;
                    }

                    // The run is written into the table, so it must fit into it
                    if (range > parameters.SBNUMSYMS - i)
                    {
                        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid symbol length code table for text region segment."));
                    }

                    for (uint32_t j = 0; j < range; ++j)
                    {
                        symCodeTable[i].value = i;
                        symCodeTable[i].prefixBitLength = length;
                        ++i;
                    }
                    break;
                }
            }
        }
        symCodeTable = PDFJBIG2HuffmanCodeTable::buildPrefixes(symCodeTable);
        parameters.SBSYMCODES = PDFJBIG2HuffmanDecoder(&m_reader, &m_workRemaining, qMove(symCodeTable));
        m_reader.alignToBytes();
    }
    else
    {
        // Arithmetic decoder
        decoder.initialize();
    }

    // The refinement of a symbol instance is always coded arithmetically, even in a huffman
    // coded text region - there it is a separate block of the data, which the decoder of the
    // refinement initializes for itself. So the decoder is needed in both cases.
    parameters.arithmeticDecoder = &decoder;

    PDFJBIG2ArithmeticDecoderStates arithmeticDecoderStates;
    consumeDecodedBytes((uint64_t(1) << parameters.SBSYMCODELEN) + 2 * 65536 + 13 * 512);
    arithmeticDecoderStates.resetArithmeticStatesInteger(parameters.SBSYMCODELEN);
    parameters.initializeFrom(&arithmeticDecoderStates);

    if (parameters.SBREFINE)
    {
        arithmeticDecoderStates.resetArithmeticStatesGenericRefinement(parameters.SBRTEMPLATE, nullptr);
    }

    parameters.reader = &m_reader;

    // The bitmap has the size of the region, which has been validated with the
    // region segment information field
    PDFJBIG2Bitmap bitmap = readTextBitmap(parameters);
    Q_ASSERT(bitmap.isValid());

    if (header.isImmediate())
    {
        paintPage(bitmap, regionSegmentInfo);
    }
    else
    {
        m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2Bitmap>(qMove(bitmap));
    }

    if (!parameters.SBHUFF)
    {
        decoder.finalize();
    }
}

void PDFJBIG2Decoder::processPatternDictionary(const PDFJBIG2SegmentHeader& header)
{
    const int segmentStartPosition = m_reader.getPosition();
    const uint8_t flags = m_reader.readUnsignedByte();
    const uint8_t HDPW = m_reader.readUnsignedByte();
    const uint8_t HDPH = m_reader.readUnsignedByte();
    const uint32_t GRAYMAX = m_reader.readUnsignedInt();
    const bool HDMMR = flags & 0x01;
    const uint8_t HDTEMPLATE = (flags >> 1) &0x03;

    if ((flags & 0b11111000) != 0 || (HDMMR && HDTEMPLATE != 0))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid pattern dictionary flags."));
    }

    QByteArray mmrData;
    PDFJBIG2ArithmeticDecoder arithmeticDecoder(&m_reader, &m_workRemaining);
    PDFJBIG2ArithmeticDecoderState genericState;
    if (!HDMMR)
    {
        arithmeticDecoder.initialize();
        consumeDecodedBytes(65536);
        PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesGeneric(&genericState, HDTEMPLATE, nullptr);
    }
    else
    {
        // Determine segment data length
        const int segmentDataStartPosition = m_reader.getPosition();
        const int segmentHeaderBytes = segmentDataStartPosition - segmentStartPosition;
        const int segmentDataBytes = getSegmentDataBytes(header, segmentHeaderBytes);
        consumeDecodedBytes(uint64_t(segmentDataBytes));
        mmrData = m_reader.readSubstream(segmentDataBytes);
    }

    if (HDPW == 0 || HDPH == 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid pattern size (%1 x %2) in the pattern dictionary.").arg(HDPW).arg(HDPH));
    }

    // All patterns are decoded as a single collective bitmap, whose width is a product
    // of two values read from the data. The product is computed in 64 bits - in 32 bits
    // it can wrap to a small value, which passes the check of the decoded bitmap below,
    // and the loop extracting the patterns then runs billions of times.
    const int64_t collectiveBitmapWidth = (int64_t(GRAYMAX) + 1) * int64_t(HDPW);

    if (collectiveBitmapWidth > MAX_BITMAP_SIZE)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 maximum bitmap size exceeded (%1 > %2).").arg(collectiveBitmapWidth).arg(MAX_BITMAP_SIZE));
    }

    const int16_t gbat0_x = -int16_t(HDPW);
    PDFJBIG2BitmapDecodingParameters parameters;
    parameters.MMR = HDMMR;
    parameters.GBW = int(collectiveBitmapWidth);
    parameters.GBH = HDPH;
    parameters.GBTEMPLATE = HDTEMPLATE;
    parameters.TPGDON = false;
    parameters.SKIP = nullptr;
    parameters.GBAT[0] = { gbat0_x, 0 };
    parameters.GBAT[1] = { -3, -1 };
    parameters.GBAT[2] = { 2, -2 };
    parameters.GBAT[3] = { -2, -2 };
    parameters.arithmeticDecoder = &arithmeticDecoder;
    parameters.arithmeticDecoderState = &genericState;
    parameters.data = qMove(mmrData);

    PDFJBIG2Bitmap collectiveBitmap = readBitmap(parameters);

    if (!HDMMR)
    {
        arithmeticDecoder.finalize();
    }

    // A decoded bitmap always has the requested width, but the MMR decoder decodes
    // less rows, when the data end before the last row
    Q_ASSERT(collectiveBitmap.getWidth() == parameters.GBW);
    if (collectiveBitmap.getHeight() != parameters.GBH)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid pattern dictionary collective bitmap."));
    }

    std::vector<PDFJBIG2Bitmap> bitmaps;
    consumeDecodedBytes(uint64_t(GRAYMAX + 1) * sizeof(PDFJBIG2Bitmap) + uint64_t(collectiveBitmapWidth) * HDPH);
    bitmaps.reserve(GRAYMAX + 1);

    int offsetX = 0;
    for (uint32_t i = 0; i <= GRAYMAX; ++i)
    {
        bitmaps.push_back(collectiveBitmap.getSubbitmap(offsetX, 0, HDPW, HDPH));
        offsetX += HDPW;
    }

    m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2PatternDictionary>(qMove(bitmaps));
}

void PDFJBIG2Decoder::processHalftoneRegion(const PDFJBIG2SegmentHeader& header)
{
    const int segmentStartPosition = m_reader.getPosition();
    PDFJBIG2RegionSegmentInformationField field = readRegionSegmentInformationField();
    checkRegionCompositionOperator(field);
    const uint8_t flags = m_reader.readUnsignedByte();
    const bool HMMR = flags & 0x01;
    const uint8_t HTEMPLATE = (flags >> 1) & 0x03;
    const bool HENABLESKIP = flags & 0x08;
    if (HMMR && (HTEMPLATE != 0 || HENABLESKIP))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid MMR halftone flags."));
    }
    const uint8_t HCOMBOOP = (flags >> 4) & 0x07;
    const uint8_t HDEFPIXEL = (flags >> 7) & 0x01;
    const uint32_t HGW = m_reader.readUnsignedInt();
    const uint32_t HGH = m_reader.readUnsignedInt();
    const int32_t HGX = m_reader.readSignedInt();
    const int32_t HGY = m_reader.readSignedInt();
    const uint16_t HRX = m_reader.readUnsignedWord();
    const uint16_t HRY = m_reader.readUnsignedWord();
    const int HBW = field.width;
    const int HBH = field.height;

    // The gray-scale image of the grid is allocated before the bit planes are decoded,
    // so the size of the grid is validated like the size of a bitmap
    checkBitmapSize(HGW);
    checkBitmapSize(HGH);
    PDFJBIG2Bitmap::checkSize(HGW, HGH);
    if (HGW == 0 || HGH == 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid halftone grid dimensions."));
    }

    PDFJBIG2BitOperation HCOMBOOPValue = PDFJBIG2BitOperation::Invalid;
    switch (HCOMBOOP)
    {
        case 0:
            HCOMBOOPValue = PDFJBIG2BitOperation::Or;
            break;

        case 1:
            HCOMBOOPValue = PDFJBIG2BitOperation::And;
            break;

        case 2:
            HCOMBOOPValue = PDFJBIG2BitOperation::Xor;
            break;

        case 3:
            HCOMBOOPValue = PDFJBIG2BitOperation::NotXor;
            break;

        case 4:
            HCOMBOOPValue = PDFJBIG2BitOperation::Replace;
            break;

        default:
            throw PDFException(PDFTranslationContext::tr("JBIG2 region segment information - invalid bit operation mode."));
    }

    PDFJBIG2ReferencedSegments references = getReferencedSegments(header);
    if (references.patternDictionaries.size() != 1)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid referenced pattern dictionaries for halftone segment."));
    }

    // A pattern dictionary defines GRAYMAX + 1 patterns, so it is never empty
    std::vector<const PDFJBIG2Bitmap*> HPATS = references.getPatternBitmaps();
    const uint32_t HNUMPATS = static_cast<uint32_t>(HPATS.size());
    Q_ASSERT(HNUMPATS > 0);

    const PDFJBIG2Bitmap* firstBitmap = HPATS.front();
    const int HPW = firstBitmap->getWidth();
    const int HPH = firstBitmap->getHeight();

    /* 6.6 step 1) */
    consumeDecodedBytes(uint64_t(HBW) * HBH);
    PDFJBIG2Bitmap HTREG(HBW, HBH, HDEFPIXEL ? 0xFF : 0x00);

    // Position of the upper left pixel of the pattern of a cell of the grid, see the
    // formula of 6.6.5.1 and 6.6.5.2. The grid is placed by a signed offset and its steps
    // are arbitrary, so the position is computed in 64 bits - it can lie far outside of
    // the region. The specification divides by the arithmetic shift, which rounds towards
    // the negative infinity, and not by the integer division of C++, which rounds towards
    // zero - the results differ for a grid placed at a negative offset, which is the usual
    // case of a rotated grid.
    auto getCellPosition = [HGX, HGY, HRX, HRY](int MG, int NG)
    {
        const int64_t x = (int64_t(HGX) + int64_t(MG) * int64_t(HRY) + int64_t(NG) * int64_t(HRX)) >> 8;
        const int64_t y = (int64_t(HGY) + int64_t(MG) * int64_t(HRX) - int64_t(NG) * int64_t(HRY)) >> 8;
        return std::make_pair(x, y);
    };

    // Returns true, if a pattern drawn at the position does not touch the region at all
    auto isCellOutside = [HPW, HPH, HBW, HBH](int64_t x, int64_t y)
    {
        return (x + HPW <= 0) || (x >= HBW) || (y + HPH <= 0) || (y >= HBH);
    };

    /* 6.6 step 2) compute HSKIP bitmap */
    PDFJBIG2Bitmap HSKIP;
    if (HENABLESKIP)
    {
        /* 6.6.5.1 */
        consumeDecodedBytes(uint64_t(HGW) * HGH);
        HSKIP = PDFJBIG2Bitmap(HGW, HGH, 0x00);

        for (int MG = 0; MG < static_cast<int>(HGH); ++MG)
        {
            for (int NG = 0; NG < static_cast<int>(HGW); ++NG)
            {
                /* 6.6.5.1 1) a) i) */
                const auto [x, y] = getCellPosition(MG, NG);

                /* 6.6.5.1 1) a) ii) */
                if (isCellOutside(x, y))
                {
                    HSKIP.setPixel(NG, MG, 0xFF);
                }
            }
        }
    }

    /* 6.6 step 3) */
    // A dictionary of a single pattern gives zero bit planes - no plane is then decoded
    // and every cell of the grid uses that single pattern
    const uint8_t HBPP = log2ceil(HNUMPATS);

    /* 6.6 step 4) */

    QByteArray mmrData;
    PDFJBIG2ArithmeticDecoder arithmeticDecoder(&m_reader, &m_workRemaining);
    PDFJBIG2ArithmeticDecoderState genericState;
    if (!HMMR)
    {
        arithmeticDecoder.initialize();
        consumeDecodedBytes(65536);
        PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesGeneric(&genericState, HTEMPLATE, nullptr);
    }
    else
    {
        // Determine segment data length
        const int segmentDataStartPosition = m_reader.getPosition();
        const int segmentHeaderBytes = segmentDataStartPosition - segmentStartPosition;
        const int segmentDataBytes = getSegmentDataBytes(header, segmentHeaderBytes);
        consumeDecodedBytes(uint64_t(segmentDataBytes));
        mmrData = m_reader.readSubstream(segmentDataBytes);
    }

    /*  Annex C5 decoding procedure */
    const int8_t gbat0_x = ((HTEMPLATE <= 1) ? 3 : 2);
    PDFJBIG2BitmapDecodingParameters parameters;
    parameters.MMR = HMMR;
    parameters.requireMMREndOfBlock = HMMR;
    parameters.GBW = HGW;
    parameters.GBH = HGH;
    parameters.GBTEMPLATE = HTEMPLATE;
    parameters.SKIP = HENABLESKIP ? &HSKIP : nullptr;
    parameters.TPGDON = false;
    parameters.GBAT[0] = { gbat0_x, -1 };
    parameters.GBAT[1] = { -3, -1 };
    parameters.GBAT[2] = { 2, -2 };
    parameters.GBAT[3] = { -2, -2 };
    parameters.arithmeticDecoder = &arithmeticDecoder;
    parameters.arithmeticDecoderState = &genericState;
    parameters.data = qMove(mmrData);

    // The gray-scale image is not a bitmap - a halftone of a large pattern needs more than
    // the eight bits of a pixel of a bitmap for its value. A pattern dictionary of a screen
    // of 20 by 20 pixels, for example, defines 401 patterns, which needs nine bit planes.
    consumeDecodedBytes(uint64_t(HGW) * HGH * sizeof(uint32_t));
    std::vector<uint32_t> GI(size_t(HGW) * size_t(HGH), 0);

    for (int J = HBPP - 1; J >= 0; --J)
    {
        PDFJBIG2Bitmap PLANE = readBitmap(parameters);

        if (HMMR)
        {
            consumeDecodedBytes(uint64_t(parameters.data.size() - parameters.dataEndPosition));
            parameters.data = parameters.data.mid(parameters.dataEndPosition);
        }

        // A decoded bitmap always has the requested width, but the MMR decoder decodes
        // less rows, when the data end before the last row
        Q_ASSERT(uint32_t(PLANE.getWidth()) == HGW);
        if (uint32_t(PLANE.getHeight()) != HGH)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid halftone grayscale bit plane image."));
        }

        for (int x = 0; x < static_cast<int>(HGW); ++x)
        {
            for (int y = 0; y < static_cast<int>(HGH); ++y)
            {
                // Old bit is in the first position of grayscale image
                const uint32_t oldValue = GI[size_t(y) * size_t(HGW) + size_t(x)];
                const uint32_t bit = (oldValue ^ PLANE.getPixel(x, y)) & 0x01;
                GI[size_t(y) * size_t(HGW) + size_t(x)] = (oldValue << 1) | bit;
            }
        }
    }

    /* 6.6 step 5) - 6.6.5.2 render the grid */
    for (int MG = 0; MG < static_cast<int>(HGH); ++MG)
    {
        for (int NG = 0; NG < static_cast<int>(HGW); ++NG)
        {
            /* 6.6.5.2 1) a) i) */
            const auto [x, y] = getCellPosition(MG, NG);

            if (isCellOutside(x, y))
            {
                // The pattern of the cell does not touch the region. Such a cell is not
                // decoded at all, when the skipping is enabled, so its value is undefined
                // and must not be used to select a pattern.
                continue;
            }

            /* 6.6.5.2 1) a) ii) */
            const uint32_t index = GI[size_t(MG) * size_t(HGW) + size_t(NG)];
            if (Q_UNLIKELY(index >= HNUMPATS))
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 halftoning pattern index %1 out of bounds [0, %2]").arg(index).arg(HNUMPATS - 1));
            }

            consumeWork(uint64_t(HPATS[index]->getPixelCount()) + 1);
            HTREG.paint(*HPATS[index], x, y, HCOMBOOPValue, false, 0x00);
        }
    }

    // The bitmap has the size of the region, which has been validated with the
    // region segment information field
    Q_ASSERT(HTREG.isValid());

    if (header.isImmediate())
    {
        paintPage(HTREG, field);
    }
    else
    {
        m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2Bitmap>(qMove(HTREG));
    }

    if (!HMMR)
    {
        arithmeticDecoder.finalize();
    }
}

void PDFJBIG2Decoder::processGenericRegion(const PDFJBIG2SegmentHeader& header)
{
    const int segmentStartPosition = m_reader.getPosition();
    PDFJBIG2RegionSegmentInformationField field = readRegionSegmentInformationField();
    checkRegionCompositionOperator(field);
    const uint8_t flags = m_reader.readUnsignedByte();

    PDFJBIG2BitmapDecodingParameters parameters;
    parameters.MMR = flags & 0b0001;
    parameters.TPGDON = flags & 0b1000;
    parameters.GBTEMPLATE = (flags >> 1) & 0b0011;

    if ((flags & 0b11110000) != 0 || (parameters.MMR && parameters.GBTEMPLATE != 0))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - malformed or unsupported generic region flags (extended templates are not supported)."));
    }

    PDFJBIG2ArithmeticDecoderState genericState;

    if (!parameters.MMR)
    {
        // We will use arithmetic coding, read template pixels and reset arithmetic coder state
        parameters.GBAT = readATTemplatePixelPositions((parameters.GBTEMPLATE == 0) ? 4 : 1);
        consumeDecodedBytes(65536);
        PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesGeneric(&genericState, parameters.GBTEMPLATE, nullptr);
    }

    // Determine segment data length
    const int segmentDataStartPosition = m_reader.getPosition();
    const int segmentHeaderBytes = segmentDataStartPosition - segmentStartPosition;
    int segmentDataBytes = 0;
    if (header.isSegmentDataLengthDefined())
    {
        segmentDataBytes = getSegmentDataBytes(header, segmentHeaderBytes);
    }
    else
    {
        // We must find byte sequence { 0x00, 0x00 } for MMR and { 0xFF, 0xAC } for arithmetic decoder
        const QByteArray* stream = m_reader.getStream();

        QByteArray endSequence(2, 0);
        if (!parameters.MMR)
        {
            endSequence[0] = (unsigned char)(0xFF);
            endSequence[1] = (unsigned char)(0xAC);
        }

        // The end sequence is searched from the start of the coded data - the segments before
        // this one and the header of the region can contain the same bytes
        int endPosition = stream->indexOf(endSequence, segmentDataStartPosition);
        if (endPosition == -1)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 - end of data byte sequence not found for generic region."));
        }

        // Add end bytes (they are also a part of stream)
        endPosition += endSequence.size();

        segmentDataBytes = endPosition - segmentDataStartPosition;
        PDFBitReader trailer = m_reader;
        trailer.seek(endPosition);
        const uint32_t rows = trailer.readUnsignedInt();
        if (rows > field.height)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid actual generic region row count."));
        }
        field.height = rows;
    }

    consumeDecodedBytes(uint64_t(segmentDataBytes));
    parameters.data = m_reader.getStream()->mid(segmentDataStartPosition, segmentDataBytes);
    parameters.GBW = field.width;
    parameters.GBH = field.height;
    parameters.arithmeticDecoderState = &genericState;


    PDFBitReader reader(&parameters.data, 1);
    PDFJBIG2ArithmeticDecoder decoder(&reader, &m_workRemaining);

    if (!parameters.MMR)
    {
        decoder.initialize();
        parameters.arithmeticDecoder = &decoder;
    }

    PDFJBIG2Bitmap bitmap = readBitmap(parameters);
    if (bitmap.isValid() || field.height == 0)
    {
        if (header.isImmediate())
        {
            paintPage(bitmap, field);
        }
        else
        {
            m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2Bitmap>(qMove(bitmap));
        }
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid bitmap for generic region."));
    }

    // Now skip the data
    m_reader.skipBytes(segmentDataBytes);

    // The unknown data length is allowed for an immediate region only, and its data are
    // followed by the row count, see 7.2.7
    if (!header.isSegmentDataLengthDefined())
    {
        m_reader.skipBytes(4);
    }
}

void PDFJBIG2Decoder::processGenericRefinementRegion(const PDFJBIG2SegmentHeader& header)
{
    PDFJBIG2RegionSegmentInformationField field = readRegionSegmentInformationField();
    const uint8_t flags = m_reader.readUnsignedByte();

    if ((flags & 0b11111100) != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid flags for generic refinement region."));
    }

    const uint8_t GRTEMPLATE = flags & 0x01;
    const bool TPGRON = flags & 0x02;

    PDFJBIG2ATPositions GRAT = { };
    if (GRTEMPLATE == 0)
    {
        GRAT = readATTemplatePixelPositions(2, true);
    }

    PDFJBIG2Bitmap GRREFERENCE;
    const std::vector<uint32_t>& referredSegments = header.getReferredSegments();
    switch (referredSegments.size())
    {
        case 0:
        {
            // According the specification, operator must be REPLACE
            if (field.operation != PDFJBIG2BitOperation::Replace)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 - operation must be REPLACE for generic refinement region."));
            }

            consumeDecodedBytes(uint64_t(field.width) * field.height);
            GRREFERENCE = m_pageBitmap.getSubbitmap(field.offsetX, field.offsetY, field.width, field.height);
            break;
        }

        case 1:
        {
            const auto info = m_regionInformation.find(referredSegments.front());
            if (info == m_regionInformation.end() || info->second.width != field.width ||
                info->second.height != field.height || info->second.offsetX != field.offsetX ||
                info->second.offsetY != field.offsetY || info->second.operation != field.operation)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 refinement reference region metadata mismatch."));
            }
            GRREFERENCE = takeBitmap(referredSegments.front());
            break;
        }

        default:
            throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid referred segments (%1) for generic refinement region.").arg(referredSegments.size()));
    }

    if (uint32_t(GRREFERENCE.getWidth()) != field.width || uint32_t(GRREFERENCE.getHeight()) != field.height)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid referred bitmap size [%1 x %2] instead of [%3 x %4] for generic refinement region.").arg(GRREFERENCE.getWidth()).arg(GRREFERENCE.getHeight()).arg(field.width).arg(field.height));
    }

    PDFJBIG2ArithmeticDecoderState refinementState;
    consumeDecodedBytes(8192);
    PDFJBIG2ArithmeticDecoderStates::resetArithmeticStatesGenericRefinement(&refinementState, GRTEMPLATE, nullptr);

    PDFJBIG2BitmapRefinementDecodingParameters parameters;
    parameters.GRTEMPLATE = GRTEMPLATE;
    parameters.TPGRON = TPGRON;
    parameters.GRW = field.width;
    parameters.GRH = field.height;
    parameters.GRAT = GRAT;
    parameters.arithmeticDecoderState = &refinementState;
    parameters.GRREFERENCE = &GRREFERENCE;
    parameters.GRREFERENCEX = 0;
    parameters.GRREFERENCEY = 0;

    PDFJBIG2ArithmeticDecoder decoder(&m_reader, &m_workRemaining);
    decoder.initialize();
    parameters.decoder = &decoder;

    // The bitmap has the size of the region, which has been validated with the
    // region segment information field
    PDFJBIG2Bitmap refinementBitmap = readRefinementBitmap(parameters);
    Q_ASSERT(refinementBitmap.isValid());

    if (header.isImmediate())
    {
        paintPage(refinementBitmap, field);
    }
    else
    {
        m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2Bitmap>(qMove(refinementBitmap));
    }

    decoder.finalize();

    // The bounded segment reader is advanced by processStream after checking consumption.
}

void PDFJBIG2Decoder::processPageInformation(const PDFJBIG2SegmentHeader& header)
{
    const uint32_t width = m_reader.readUnsignedInt();
    const uint32_t height = m_reader.readUnsignedInt();

    // Skip 8 bites - resolution. We do not need the resolution values.
    m_reader.skipBytes(sizeof(uint32_t) * 2);

    const uint8_t flags = m_reader.readUnsignedByte();
    const uint16_t striping = m_reader.readUnsignedWord();

    if ((flags & 0x80) || width == 0 || height == 0 ||
        (height == 0xFFFFFFFF && !(striping & 0x8000)))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid page information flags or size."));
    }
    m_pageStriped = striping & 0x8000;
    m_maximumStripeHeight = striping & 0x7FFF;
    if (m_pageStriped && m_maximumStripeHeight == 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid maximum stripe height."));
    }
    m_pageMayRefine = flags & 0x02;
    m_pageMayUseAuxiliary = flags & 0x20;
    m_pageAssociation = header.getPageAssociation();

    m_pageDefaultPixelValue = (flags & 0x04) ? 0xFF : 0x00;
    m_pageDefaultCompositionOperatorOverriden = (flags & 0x40);

    // Combination operators of the two bit field, see 7.4.8.5
    constexpr std::array<PDFJBIG2BitOperation, 4> combinationOperators = { PDFJBIG2BitOperation::Or, PDFJBIG2BitOperation::And, PDFJBIG2BitOperation::Xor, PDFJBIG2BitOperation::NotXor };
    m_pageDefaultCompositionOperator = combinationOperators[(flags >> 3) & 0b11];

    const uint32_t correctedWidth = width;
    const uint32_t correctedHeight = (height != 0xFFFFFFFF) ? height : 0;
    m_pageSizeUndefined = height == 0xFFFFFFFF;

    checkBitmapSize(correctedWidth);
    checkBitmapSize(correctedHeight);
    PDFJBIG2Bitmap::checkSize(correctedWidth, correctedHeight);

    consumeDecodedBytes(uint64_t(correctedWidth) * correctedHeight);
    m_pageBitmap = PDFJBIG2Bitmap(correctedWidth, correctedHeight, m_pageDefaultPixelValue);
}

void PDFJBIG2Decoder::processEndOfPage(const PDFJBIG2SegmentHeader& header)
{
    if (header.getSegmentDataLength() != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 end-of-page segment shouldn't contain any data, but has extra data of %1 bytes.").arg(header.getSegmentDataLength()));
    }

    finishPage();
    m_pageEnded = true;

    // We will write a warning, because end-of-page segments should not be in PDF according to specification
    if (!m_isDecodingFile)
    {
        m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 end-of-page segment detected and ignored."));
    }
}

void PDFJBIG2Decoder::processEndOfStripe(const PDFJBIG2SegmentHeader& header)
{
    if (!m_pageStriped || header.getSegmentDataLength() != 4)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid end-of-stripe segment."));
    }
    const int64_t row = m_reader.readUnsignedInt();
    if (row <= m_lastStripeRow || row - m_lastStripeRow > m_maximumStripeHeight ||
        row + 1 < m_stripeBottom || row >= MAX_BITMAP_SIZE ||
        (!m_pageSizeUndefined && row >= m_pageBitmap.getHeight()))
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid stripe end row."));
    }
    if (m_pageSizeUndefined && row + 1 > m_pageBitmap.getHeight())
    {
        consumeDecodedBytes(uint64_t(m_pageBitmap.getWidth()) * (row + 1 - m_pageBitmap.getHeight()));
        m_pageBitmap.resizeHeight(int(row + 1), m_pageDefaultPixelValue);
    }
    m_lastStripeRow = row;
    m_stripeBottom = row + 1;
}

void PDFJBIG2Decoder::processEndOfFile(const PDFJBIG2SegmentHeader& header)
{
    if (header.getSegmentDataLength() != 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 end-of-file segment shouldn't contain any data, but has extra data of %1 bytes.").arg(header.getSegmentDataLength()));
    }

    if (!m_pageEnded)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 end of file before end of page."));
    }
    m_fileEnded = true;
    if (header.getPageAssociation() != 0)
    {
        m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 end-of-file segment is incorrectly associated with the decoded page."));
    }

    // We will write a warning, because end-of-file segments should not be in PDF according to specification
    if (!m_isDecodingFile)
    {
        m_errorReporter->reportRenderError(RenderErrorType::Warning, PDFTranslationContext::tr("JBIG2 end-of-file segment detected and ignored."));
    }
}

void PDFJBIG2Decoder::processProfiles(const PDFJBIG2SegmentHeader& header)
{
    skipSegment(header);
}

void PDFJBIG2Decoder::processCodeTables(const PDFJBIG2SegmentHeader& header)
{
    const uint8_t flags = m_reader.readUnsignedByte();
    const int32_t htLow = m_reader.readSignedInt();
    const int32_t htHigh = m_reader.readSignedInt();

    if ((flags & 0x80) || htLow >= htHigh)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid huffman table header."));
    }

    if (htLow == std::numeric_limits<int32_t>::min())
    {
        // Check for underflow, we subtract 1 from htLow value
        throw PDFException(PDFTranslationContext::tr("JBIG2 underflow of the low value in huffman table."));
    }

    const bool hasOOB = flags & 0x01;
    const PDFBitReader::Value htps = ((flags >> 1) & 0b111) + 1;
    const PDFBitReader::Value htrs = ((flags >> 4) & 0b111) + 1;

    std::vector<PDFJBIG2HuffmanTableEntry> table;
    table.reserve(32);

    // Read standard values
    int64_t currentRangeLow = htLow;
    while (currentRangeLow < htHigh)
    {
        PDFJBIG2HuffmanTableEntry entry;
        entry.prefixBitLength = m_reader.read(htps);
        entry.rangeBitLength = m_reader.read(htrs);
        if (entry.rangeBitLength > 32 || entry.prefixBitLength > 32 || table.size() >= MAX_JBIG2_SYMBOL_COUNT)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 unsupported huffman table size or code length."));
        }
        consumeDecodedBytes(sizeof(PDFJBIG2HuffmanTableEntry) * 4);
        entry.value = int32_t(currentRangeLow);
        currentRangeLow += int64_t(1) << entry.rangeBitLength;
        table.push_back(entry);
    }

    // Read "low" value
    PDFJBIG2HuffmanTableEntry lowEntry;
    lowEntry.prefixBitLength = m_reader.read(htps);
    lowEntry.rangeBitLength = 32;
    lowEntry.value = htLow - 1;
    lowEntry.type = PDFJBIG2HuffmanTableEntry::Type::Negative;
    table.push_back(lowEntry);

    // Read "high" value
    PDFJBIG2HuffmanTableEntry highEntry;
    highEntry.prefixBitLength = m_reader.read(htps);
    highEntry.rangeBitLength = 32;
    highEntry.value = htHigh;
    table.push_back(highEntry);

    // Read out-of-band value, if we have it
    if (hasOOB)
    {
        PDFJBIG2HuffmanTableEntry oobEntry;
        oobEntry.prefixBitLength = m_reader.read(htps);
        oobEntry.type = PDFJBIG2HuffmanTableEntry::Type::OutOfBand;
        table.push_back(oobEntry);
    }

    table = PDFJBIG2HuffmanCodeTable::buildPrefixes(table);
    m_segments[header.getSegmentNumber()] = std::make_unique<PDFJBIG2HuffmanCodeTable>(qMove(table));
}

void PDFJBIG2Decoder::processExtension(const PDFJBIG2SegmentHeader& header)
{
    // We will read the extension header, and check "Necessary bit"
    const uint32_t extensionHeader = m_reader.readUnsignedInt();
    // The necessary bit is the bit 31 of the extension type, see 7.4.14
    if (extensionHeader & 0x80000000)
    {
        const uint32_t extensionCode = extensionHeader & 0x3FFFFFFF;
        throw PDFException(PDFTranslationContext::tr("JBIG2 unknown extension %1 necessary for decoding the image.").arg(extensionCode));
    }

    m_reader.skipBytes(header.getSegmentDataLength() - 4);
}

PDFJBIG2Bitmap PDFJBIG2Decoder::takeBitmap(const uint32_t segmentIndex)
{
    auto it = m_segments.find(segmentIndex);
    if (it != m_segments.cend())
    {
        PDFJBIG2Bitmap* bitmap = it->second->asBitmap();

        if (!bitmap)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 segment %1 is not a bitmap.").arg(segmentIndex));
        }

        PDFJBIG2Bitmap result = qMove(*bitmap);
        discardSegment(segmentIndex);
        return result;
    }

    throw PDFException(PDFTranslationContext::tr("JBIG2 bitmap segment %1 not found.").arg(segmentIndex));
}

PDFJBIG2Bitmap PDFJBIG2Decoder::readBitmap(PDFJBIG2BitmapDecodingParameters& parameters)
{
    PDFJBIG2Bitmap::checkSize(parameters.GBW, parameters.GBH);
    checkBitmapSize(parameters.GBW);
    checkBitmapSize(parameters.GBH);
    consumeDecodedBytes(uint64_t(parameters.GBW) * parameters.GBH);
    if (parameters.GBW == 0 || parameters.GBH == 0)
    {
        return PDFJBIG2Bitmap(parameters.GBW, parameters.GBH, 0x00);
    }
    if (parameters.MMR)
    {
        // Use modified-modified-read (it corresponds to CCITT 2D encoding)
        PDFCCITTFaxDecoderParameters ccittParameters;
        ccittParameters.K = -1;
        ccittParameters.columns = parameters.GBW;
        ccittParameters.rows = parameters.GBH;
        ccittParameters.hasEndOfBlock = false;
        ccittParameters.decode = { 1.0, 0.0 };
        ccittParameters.hasBlackIsOne = true;

        // Packed output and the two change-position rows of the MMR decoder.
        consumeDecodedBytes(uint64_t((parameters.GBW + 7) / 8) * parameters.GBH + uint64_t(parameters.GBW + 2) * 16);
        PDFCCITTFaxDecoder decoder(&parameters.data, ccittParameters);
        PDFImageData data = decoder.decode();
        if (data.getWidth() != uint32_t(parameters.GBW) || data.getHeight() != uint32_t(parameters.GBH))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 truncated MMR bitmap."));
        }
        PDFBitReader tail = *decoder.getReader();
        if (parameters.requireMMREndOfBlock)
        {
            // Continue at the exact bit position, rather than searching compressed
            // pixels for a marker or scanning arbitrary remaining segment data.
            if (tail.read(24) != 0x1001)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 missing MMR end-of-block marker."));
            }
            tail.alignToBytes();
        }
        parameters.dataEndPosition = tail.getPosition();

        PDFJBIG2Bitmap bitmap(data.getWidth(), data.getHeight(), m_pageDefaultPixelValue);

        // Copy the data
        PDFBitReader reader(&data.getData(), data.getBitsPerComponent());
        for (unsigned int row = 0; row < data.getHeight(); ++row)
        {
            for (unsigned int column = 0; column < data.getWidth(); ++column)
            {
                bitmap.setPixel(column, row, (reader.read()) ? 0x00 : 0xFF);
            }

            reader.alignToBytes();
        }

        return bitmap;
    }
    else
    {
        // Use arithmetic encoding. For templates, we fill bytes from right to left, from bottom to top bits,
        // filling from lowest bit to highest bit. We will have a maximum of 16 bits.

        uint8_t LTP = 0;
        uint16_t LTPContext = 0;
        if (parameters.TPGDON)
        {
            if (parameters.GBTEMPLATE == 0)
            {
                //  Figure 8. Reused context for coding the SLTP value
                //
                //          ┌───┬───┬───┬───┬───┐
                //          │ 1 │ 0 │ 0 │ 1 │ 1 │
                //      ┌───┼───┼───┼───┼───┼───┼───┐
                //      │ 0 │ 1 │ 1 │ 0 │ 0 │ 1 │ 0 │
                //  ┌───┼───┼───┼───┼───┼───┴───┴───┘
                //  │ 0 │ 1 │ 0 │ 1 │ X │
                //  └───┴───┴───┴───┴───┘
                //
                // Bottom row from right to left: 1010
                // Middle row from right to left: 0100110
                // Top row from right to left: 11001
                //  => 0b1010 0100110 11001
                // WRONG! because first bits are lowest, we must flip the context (reverse it by bits)
                //LTPContext = 0b1010010011011001; // 16-bit context, hexadecimal value is 0xA4D9
                LTPContext = 0b1001101100100101; // 16-bit context, hexadecimal value is 0x9B25
            }

            else if (parameters.GBTEMPLATE == 1)
            {
                //  Figure 9. Reused context for coding the SLTP value
                //
                //          ┌───┬───┬───┬───┐
                //          │ 0 │ 0 │ 1 │ 1 │
                //      ┌───┼───┼───┼───┼───┼───┐
                //      │ 1 │ 1 │ 0 │ 0 │ 1 │ 0 │
                //  ┌───┼───┼───┼───┼───┴───┴───┘
                //  │ 1 │ 0 │ 1 │ x │
                //  └───┴───┴───┴───┘
                //
                // Bottom row from right to left: 101
                // Middle row from right to left: 010011
                // Top row from right to left: 1100
                //  => 0b101 010011 1100
                // WRONG! because first bits are lowest, we must flip the context (reverse it by bits)
                //LTPContext = 0b1010100111100; // 13-bit context, hexadecimal value is 0x153C
                LTPContext = 0b0011110010101; // 13-bit context, hexadecimal value is 0x0795
            }

            else if (parameters.GBTEMPLATE == 2)
            {
                //  Figure 10. Reused context for coding the SLTP value
                //
                //          ┌───┬───┬───┐
                //          │ 0 │ 0 │ 1 │
                //      ┌───┼───┼───┼───┼───┐
                //      │ 1 │ 1 │ 0 │ 0 │ 1 │
                //      ├───┼───┼───┼───┴───┘
                //      │ 0 │ 1 │ x │
                //      └───┴───┴───┘
                //
                // Bottom row from right to left: 10
                // Middle row from right to left: 10011
                // Top row from right to left: 100
                //  => 0b10 10011 100
                // WRONG! because first bits are lowest, we must flip the context (reverse it by bits)
                //LTPContext = 0b1010011100; // 10-bit context, hexadecimal value is 0x029C
                LTPContext = 0b0011100101; // 10-bit context, hexadecimal value is 0x00E5
            }

            else
            {
                //  Figure 11. Reused context for coding the SLTP value
                //
                //          ┌───┬───┬───┬───┬───┬───┐
                //          │ 0 │ 1 │ 1 │ 0 │ 0 │ 1 │
                //      ┌───┼───┼───┼───┼───┼───┴───┘
                //      │ 0 │ 1 │ 0 │ 1 │ x │
                //      └───┴───┴───┴───┴───┘
                //
                // Bottom row from right to left: 1010
                // Top row from right to left: 100110
                //  => 0b1010100110
                // WRONG! because first bits are lowest, we must flip the context (reverse it by bits)
                //LTPContext = 0b1010100110; // 10-bit context, hexadecimal value is 0x02A6
                LTPContext = 0b0110010101; // 10-bit context, hexadecimal value is 0x0195
            }
        }

        Q_ASSERT(parameters.arithmeticDecoder);
        PDFJBIG2ArithmeticDecoder& decoder = *parameters.arithmeticDecoder;

        PDFJBIG2Bitmap bitmap(parameters.GBW, parameters.GBH, 0x00);
        for (int y = 0; y < parameters.GBH; ++y)
        {
            // Check TPGDON prediction - if we use same pixels as in previous line
            if (parameters.TPGDON)
            {
                LTP = LTP ^ decoder.readBit(LTPContext, parameters.arithmeticDecoderState);
                if (LTP)
                {
                    if (y > 0)
                    {
                        bitmap.copyRow(y, y - 1);
                    }
                    continue;
                }
            }

            for (int x = 0; x < parameters.GBW; ++x)
            {
                // Check, if we have to skip pixel. Pixel should be set to 0, but it is done
                // in the initialization of the bitmap.
                if (parameters.SKIP && parameters.SKIP->getPixelSafe(x, y))
                {
                    continue;
                }

                uint16_t pixelContext = 0;
                uint16_t pixelContextShift = 0;
                auto createContextBit = [&](int offsetX, int offsetY)
                {
                    uint16_t bit = bitmap.getPixelSafe(offsetX, offsetY) ? 1 : 0;
                    bit = bit << pixelContextShift;
                    pixelContext |= bit;
                    ++pixelContextShift;
                };

                // Create pixel context based on used template
                if (parameters.GBTEMPLATE == 0)
                {
                    //  Figure 8. Reused context for coding the SLTP value
                    //
                    //          ┌───┬───┬───┬───┬───┐
                    //          │A15│ 14│ 13│ 12│A11│
                    //      ┌───┼───┼───┼───┼───┼───┼───┐
                    //      │A10│ 9 │ 8 │ 7 │ 6 │ 5 │A4 │
                    //  ┌───┼───┼───┼───┼───┼───┴───┴───┘
                    //  │ 3 │ 2 │ 1 │ 0 │ X │
                    //  └───┴───┴───┴───┴───┘

                    // 16-bit context
                    createContextBit(x - 1, y);
                    createContextBit(x - 2, y);
                    createContextBit(x - 3, y);
                    createContextBit(x - 4, y);
                    createContextBit(x + parameters.GBAT[0].x, y + parameters.GBAT[0].y);
                    createContextBit(x + 2, y - 1);
                    createContextBit(x + 1, y - 1);
                    createContextBit(x + 0, y - 1);
                    createContextBit(x - 1, y - 1);
                    createContextBit(x - 2, y - 1);
                    createContextBit(x + parameters.GBAT[1].x, y + parameters.GBAT[1].y);
                    createContextBit(x + parameters.GBAT[2].x, y + parameters.GBAT[2].y);
                    createContextBit(x + 1, y - 2);
                    createContextBit(x + 0, y - 2);
                    createContextBit(x - 1, y - 2);
                    createContextBit(x + parameters.GBAT[3].x, y + parameters.GBAT[3].y);
                }

                else if (parameters.GBTEMPLATE == 1)
                {
                    //  Figure 9. Reused context for coding the SLTP value
                    //
                    //          ┌───┬───┬───┬───┐
                    //          │ 12│ 11│ 10│ 9 │
                    //      ┌───┼───┼───┼───┼───┼───┐
                    //      │ 8 │ 7 │ 6 │ 5 │ 4 │A3 │
                    //  ┌───┼───┼───┼───┼───┴───┴───┘
                    //  │ 2 │ 1 │ 0 │ x │
                    //  └───┴───┴───┴───┘

                    // 13-bit context
                    createContextBit(x - 1, y);
                    createContextBit(x - 2, y);
                    createContextBit(x - 3, y);
                    createContextBit(x + parameters.GBAT[0].x, y + parameters.GBAT[0].y);
                    createContextBit(x + 2, y - 1);
                    createContextBit(x + 1, y - 1);
                    createContextBit(x + 0, y - 1);
                    createContextBit(x - 1, y - 1);
                    createContextBit(x - 2, y - 1);
                    createContextBit(x + 2, y - 2);
                    createContextBit(x + 1, y - 2);
                    createContextBit(x + 0, y - 2);
                    createContextBit(x - 1, y - 2);
                }

                else if (parameters.GBTEMPLATE == 2)
                {
                    //  Figure 10. Reused context for coding the SLTP value
                    //
                    //          ┌───┬───┬───┐
                    //          │ 9 │ 8 │ 7 │
                    //      ┌───┼───┼───┼───┼───┐
                    //      │ 6 │ 5 │ 4 │ 3 │A2 │
                    //      ├───┼───┼───┼───┴───┘
                    //      │ 1 │ 0 │ x │
                    //      └───┴───┴───┘

                    // 10-bit context
                    createContextBit(x - 1, y);
                    createContextBit(x - 2, y);
                    createContextBit(x + parameters.GBAT[0].x, y + parameters.GBAT[0].y);
                    createContextBit(x + 1, y - 1);
                    createContextBit(x + 0, y - 1);
                    createContextBit(x - 1, y - 1);
                    createContextBit(x - 2, y - 1);
                    createContextBit(x + 1, y - 2);
                    createContextBit(x + 0, y - 2);
                    createContextBit(x - 1, y - 2);
                }

                else
                {
                    //  Figure 11. Reused context for coding the SLTP value
                    //
                    //          ┌───┬───┬───┬───┬───┬───┐
                    //          │ 9 │ 8 │ 7 │ 6 │ 5 │A4 │
                    //      ┌───┼───┼───┼───┼───┼───┴───┘
                    //      │ 3 │ 2 │ 1 │ 0 │ x │
                    //      └───┴───┴───┴───┴───┘

                    // 10-bit context
                    createContextBit(x - 1, y);
                    createContextBit(x - 2, y);
                    createContextBit(x - 3, y);
                    createContextBit(x - 4, y);
                    createContextBit(x + parameters.GBAT[0].x, y + parameters.GBAT[0].y);
                    createContextBit(x + 1, y - 1);
                    createContextBit(x + 0, y - 1);
                    createContextBit(x - 1, y - 1);
                    createContextBit(x - 2, y - 1);
                    createContextBit(x - 3, y - 1);
                }

                bitmap.setPixel(x, y, (decoder.readBit(pixelContext, parameters.arithmeticDecoderState)) ? 0xFF : 0x00);
            }
        }

        return bitmap;
    }
}

PDFJBIG2Bitmap PDFJBIG2Decoder::readRefinementBitmap(PDFJBIG2BitmapRefinementDecodingParameters& parameters)
{
    // Use algorithm described in 6.3.5.6
    checkBitmapSize(parameters.GRW);
    checkBitmapSize(parameters.GRH);
    consumeDecodedBytes(uint64_t(parameters.GRW) * parameters.GRH);
    PDFJBIG2Bitmap GRREG(parameters.GRW, parameters.GRH, 0x00);

    // Use arithmetic encoding. For templates, we fill bytes from right to left, from bottom to top bits,
    // filling from lowest bit to highest bit. We will have a maximum of 13 bits.

    uint32_t LTP = 0;
    const uint32_t LTPContext = !parameters.GRTEMPLATE ? 0b0000100000000 : 0b0010000000;

    PDFJBIG2ArithmeticDecoder& decoder = *parameters.decoder;

    auto createContext = [&](int x, int y) -> uint16_t
    {
        uint16_t pixelContext = 0;
        uint16_t pixelContextShift = 0;
        auto createContextBit = [&](const PDFJBIG2Bitmap* bitmap, int64_t offsetX, int64_t offsetY)
        {
            uint16_t bit = bitmap->getPixelSafe(offsetX, offsetY) ? 1 : 0;
            bit = bit << pixelContextShift;
            pixelContext |= bit;
            ++pixelContextShift;
        };

        if (!parameters.GRTEMPLATE)
        {
            // 13-bit context
            createContextBit(&GRREG, x - 1, y);
            createContextBit(&GRREG, x + 1, y - 1);
            createContextBit(&GRREG, x + 0, y - 1);
            createContextBit(&GRREG, x + parameters.GRAT[0].x, y + parameters.GRAT[0].y);

            const int64_t refX = int64_t(x) - parameters.GRREFERENCEX;
            const int64_t refY = int64_t(y) - parameters.GRREFERENCEY;

            createContextBit(parameters.GRREFERENCE, refX + 1, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX - 1, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX + 1, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX - 1, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX + 1, refY - 1);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY - 1);
            createContextBit(parameters.GRREFERENCE, refX + parameters.GRAT[1].x, refY + parameters.GRAT[1].y);
        }
        else
        {
            // 10-bit context
            createContextBit(&GRREG, x - 1, y);
            createContextBit(&GRREG, x + 1, y - 1);
            createContextBit(&GRREG, x + 0, y - 1);
            createContextBit(&GRREG, x - 1, y - 1);

            const int64_t refX = int64_t(x) - parameters.GRREFERENCEX;
            const int64_t refY = int64_t(y) - parameters.GRREFERENCEY;

            createContextBit(parameters.GRREFERENCE, refX + 1, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY + 1);
            createContextBit(parameters.GRREFERENCE, refX + 1, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX - 1, refY + 0);
            createContextBit(parameters.GRREFERENCE, refX + 0, refY - 1);
        }

        return pixelContext;
    };

    auto evaluateTPGRPIX = [&](int x, int y, uint8_t& value) -> bool
    {
        const int64_t refX = int64_t(x) - parameters.GRREFERENCEX;
        const int64_t refY = int64_t(y) - parameters.GRREFERENCEY;

        value = parameters.GRREFERENCE->getPixelSafe(refX, refY);

        return parameters.GRREFERENCE->getPixelSafe(refX - 1, refY - 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 0, refY - 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 1, refY - 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX - 1, refY + 0) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 1, refY + 0) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX - 1, refY + 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 0, refY + 1) == value &&
               parameters.GRREFERENCE->getPixelSafe(refX + 1, refY + 1) == value;
    };

    for (int32_t y = 0; y < static_cast<int32_t>(parameters.GRH); ++y)
    {
        if (parameters.TPGRON)
        {
            LTP = LTP ^ decoder.readBit(LTPContext, parameters.arithmeticDecoderState);
        }

        if (!LTP)
        {
            for (int32_t x = 0; x < static_cast<int32_t>(parameters.GRW); ++x)
            {
                GRREG.setPixel(x, y, (decoder.readBit(createContext(x, y), parameters.arithmeticDecoderState)) ? 0xFF : 0x00);
            }
        }
        else
        {
            for (int32_t x = 0; x < static_cast<int32_t>(parameters.GRW); ++x)
            {
                uint8_t TPGRVAL = 0;
                if (evaluateTPGRPIX(x, y, TPGRVAL))
                {
                    GRREG.setPixel(x, y, TPGRVAL);
                }
                else
                {
                    GRREG.setPixel(x, y, (decoder.readBit(createContext(x, y), parameters.arithmeticDecoderState)) ? 0xFF : 0x00);
                }
            }
        }
    }

    return GRREG;
}

PDFJBIG2Bitmap PDFJBIG2Decoder::readTextBitmap(PDFJBIG2TextRegionDecodingParameters& parameters)
{
    /* 6.4.5 step 1) */
    if (parameters.SBNUMINSTANCES > MAX_SYMBOL_COUNT)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 maximum symbol instance count exceeded."));
    }
    checkBitmapSize(parameters.SBW);
    checkBitmapSize(parameters.SBH);
    consumeDecodedBytes(uint64_t(parameters.SBW) * parameters.SBH);
    PDFJBIG2Bitmap SBREG(parameters.SBW, parameters.SBH, parameters.SBDEFPIXEL);

    Q_ASSERT(parameters.SBNUMSYMS == parameters.SBSYMS.size());

    /* 6.4.5 step 2) */
    int64_t STRIPT = checkInteger(parameters.SBHUFF ? parameters.SBHUFFDT.readSignedInteger() : parameters.arithmeticDecoder->getSignedInteger(parameters.IADT));
    STRIPT *= -parameters.SBSTRIPS;
    int64_t FIRSTS = 0;
    uint32_t NINSTANCES = 0;

    /* 6.4.5. step 3) */
    while (NINSTANCES < parameters.SBNUMINSTANCES)
    {
        /* 6.4.5. step 3) b), using decoding procedure 6.4.6 */
        int32_t DT = checkInteger(parameters.SBHUFF ? parameters.SBHUFFDT.readSignedInteger() : parameters.arithmeticDecoder->getSignedInteger(parameters.IADT));
        STRIPT += int64_t(DT) * parameters.SBSTRIPS;
        int64_t CURS = 0;

        bool firstSymbolInstance = true;
        while (true)
        {
            if (firstSymbolInstance)
            {
                /* 6.4.5. step 3) i), using decoding procedure 6.4.7 */
                int32_t DFS = checkInteger(parameters.SBHUFF ? parameters.SBHUFFFS.readSignedInteger() : parameters.arithmeticDecoder->getSignedInteger(parameters.IAFS));
                FIRSTS += DFS;
                CURS = FIRSTS;
                firstSymbolInstance = false;
            }
            else
            {
                /* 6.4.5. step 3) ii), using decoding procedure 6.4.8 */
                std::optional<int32_t> DS = parameters.SBHUFF ? parameters.SBHUFFDS.readSignedInteger() : parameters.arithmeticDecoder->getSignedInteger(parameters.IADS);
                if (DS.has_value())
                {
                    const int32_t IDS = *DS;
                    CURS += int64_t(IDS) + parameters.SBDSOFFSET;
                }
                else
                {
                    // End of strip, proceed to the next strip
                    break;
                }
            }

            // A strip is always terminated by the out-of-band value, so the counter of the
            // instances is checked here and not in the condition of the loop. Leaving the
            // loop without reading that value would stop in the middle of the strip, which
            // desynchronizes everything read after the text region - the text region of a
            // symbol dictionary is followed by the rest of the dictionary.
            if (NINSTANCES >= parameters.SBNUMINSTANCES)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 text region has more symbol instances, than defined in its header (%1).").arg(parameters.SBNUMINSTANCES));
            }

            /* 6.4.5. step 3) iii), using decoding procedure 6.4.9 */
            int32_t CURT = 0;
            if (parameters.SBSTRIPS > 1)
            {
                CURT = parameters.SBHUFF ? parameters.reader->read(parameters.LOG2SBSTRIPS) : checkInteger(parameters.arithmeticDecoder->getSignedInteger(parameters.IAIT));
            }

            if (CURT < 0 || CURT >= parameters.SBSTRIPS)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 invalid symbol position within strip."));
            }
            const int64_t TI = STRIPT + CURT;

            /* 6.4.5. step 3) iv), using decoding procedure 6.4.10 */
            uint32_t ID = parameters.SBHUFF ? checkInteger(parameters.SBSYMCODES.readSignedInteger()) : parameters.arithmeticDecoder->getIAID(parameters.SBSYMCODELEN, parameters.IAID);

            /* 6.4.5. step 3) v), determine instance bitmap according to 6.4.11 */
            if (ID >= parameters.SBNUMSYMS)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 symbol index %1 not found in symbol table of length %2.").arg(ID).arg(parameters.SBNUMSYMS));
            }

            bool RI = 0;
            if (parameters.SBREFINE)
            {
                const int32_t value = parameters.SBHUFF ? int32_t(parameters.reader->read(1)) : checkInteger(parameters.arithmeticDecoder->getSignedInteger(parameters.IARI));
                if (value < 0 || value > 1)
                {
                    throw PDFException(PDFTranslationContext::tr("JBIG2 invalid refinement indicator."));
                }
                RI = value != 0;
            }

            PDFJBIG2Bitmap refinedBitmap;
            const PDFJBIG2Bitmap* instanceBitmap = parameters.SBSYMS[ID];
            if (RI)
            {
                /* 6.4.11 1), 2), 3), 4) */
                int32_t RDW = checkInteger(parameters.SBHUFF ? parameters.SBHUFFRDW.readSignedInteger() : parameters.arithmeticDecoder->getSignedInteger(parameters.IARDW));
                int32_t RDH = checkInteger(parameters.SBHUFF ? parameters.SBHUFFRDH.readSignedInteger() : parameters.arithmeticDecoder->getSignedInteger(parameters.IARDH));
                int32_t RDX = checkInteger(parameters.SBHUFF ? parameters.SBHUFFRDX.readSignedInteger() : parameters.arithmeticDecoder->getSignedInteger(parameters.IARDX));
                int32_t RDY = checkInteger(parameters.SBHUFF ? parameters.SBHUFFRDY.readSignedInteger() : parameters.arithmeticDecoder->getSignedInteger(parameters.IARDY));

                /* 6.4.11 5) */
                QByteArray refinementData;
                PDFBitReader refinementReader(&refinementData, 8);
                PDFJBIG2ArithmeticDecoder refinementDecoder(&refinementReader, &m_workRemaining);
                if (parameters.SBHUFF)
                {
                    const int32_t bmsize = checkInteger(parameters.SBHUFFRSIZE.readSignedInteger());
                    refinementData = readRefinementData(parameters.reader, bmsize);
                    refinementDecoder.initialize();
                }

                /* 6.4.11 6) */
                const PDFJBIG2Bitmap* IBO = parameters.SBSYMS[ID];
                const int WOI = IBO->getWidth();
                const int HOI = IBO->getHeight();

                // The deltas are decoded from the data, so the size of the refined
                // bitmap is arbitrary and it must be validated before the bitmap is
                // created. The sum is computed in 64 bits, because it can overflow.
                const int64_t GRW = int64_t(WOI) + int64_t(RDW);
                const int64_t GRH = int64_t(HOI) + int64_t(RDH);

                if (GRW <= 0 || GRH <= 0 || GRW > MAX_BITMAP_SIZE || GRH > MAX_BITMAP_SIZE)
                {
                    throw PDFException(PDFTranslationContext::tr("JBIG2 invalid size (%1 x %2) of a refined symbol instance bitmap.").arg(GRW).arg(GRH));
                }

                // Apply the refinement procedure acc. to Table 12
                PDFJBIG2BitmapRefinementDecodingParameters refinementParameters;
                refinementParameters.decoder = parameters.SBHUFF ? &refinementDecoder : parameters.arithmeticDecoder;
                refinementParameters.arithmeticDecoderState = parameters.refinementDecoderState;
                refinementParameters.GRW = uint32_t(GRW);
                refinementParameters.GRH = uint32_t(GRH);
                refinementParameters.GRTEMPLATE = parameters.SBRTEMPLATE;
                refinementParameters.GRREFERENCE = IBO;
                refinementParameters.GRREFERENCEX = int64_t(floorDivideByTwo(RDW)) + RDX;
                refinementParameters.GRREFERENCEY = int64_t(floorDivideByTwo(RDH)) + RDY;
                refinementParameters.TPGRON = false;
                refinementParameters.GRAT = parameters.SBRAT;
                refinedBitmap = readRefinementBitmap(refinementParameters);
                instanceBitmap = &refinedBitmap;

                /* 6.4.11 7) */
                if (parameters.SBHUFF)
                {
                    refinementDecoder.finalize();
                }
            }

            const PDFJBIG2Bitmap& IB = *instanceBitmap;
            consumeWork(uint64_t(IB.getPixelCount()) + 1);
            const int32_t WI = IB.getWidth();
            const int32_t HI = IB.getHeight();

            /* 6.4.5. step 3) vi) */
            if (parameters.TRANSPOSED == 0 && (parameters.REFCORNER == PDFJBIG2TextRegionDecodingParameters::TOPRIGHT ||
                                               parameters.REFCORNER == PDFJBIG2TextRegionDecodingParameters::BOTTOMRIGHT))
            {
                CURS += WI - 1;
            }
            if (parameters.TRANSPOSED == 1 && (parameters.REFCORNER == PDFJBIG2TextRegionDecodingParameters::BOTTOMLEFT ||
                                               parameters.REFCORNER == PDFJBIG2TextRegionDecodingParameters::BOTTOMRIGHT))
            {
                CURS += HI - 1;
            }

            /* 6.4.5. step 3) c) vii) */
            const int64_t SI = CURS;

            /* 6.4.5. step 3) c) viii) + ix) - the reference corner is the corner of the symbol
               placed at (S, T). The bit 0 of REFCORNER is set for the top corners and the bit 1
               for the right corners, see 7.4.3.1.1. */
            const bool isTopCorner = (parameters.REFCORNER & 0x01) != 0;
            const bool isRightCorner = (parameters.REFCORNER & 0x02) != 0;
            const int32_t cornerOffsetX = isRightCorner ? (WI - 1) : 0;
            const int32_t cornerOffsetY = isTopCorner ? 0 : (HI - 1);

            if (parameters.TRANSPOSED == 0)
            {
                SBREG.paint(IB, SI - cornerOffsetX, TI - cornerOffsetY, parameters.SBCOMBOP, false, 0x00);
            }
            else
            {
                SBREG.paint(IB, TI - cornerOffsetX, SI - cornerOffsetY, parameters.SBCOMBOP, false, 0x00);
            }

            /* 6.4.5. step 3) c) x) */
            if (parameters.TRANSPOSED == 0 && (parameters.REFCORNER == PDFJBIG2TextRegionDecodingParameters::TOPLEFT ||
                                               parameters.REFCORNER == PDFJBIG2TextRegionDecodingParameters::BOTTOMLEFT))
            {
                CURS += WI - 1;
            }
            if (parameters.TRANSPOSED == 1 && (parameters.REFCORNER == PDFJBIG2TextRegionDecodingParameters::TOPLEFT ||
                                               parameters.REFCORNER == PDFJBIG2TextRegionDecodingParameters::TOPRIGHT))
            {
                CURS += HI - 1;
            }

            /* 6.4.5. step 3) c) xi) */
            ++NINSTANCES;
        }
    }

    /* 6.4.5 4) */
    return SBREG;
}

PDFJBIG2RegionSegmentInformationField PDFJBIG2Decoder::readRegionSegmentInformationField()
{
    PDFJBIG2RegionSegmentInformationField result;

    result.width = m_reader.readUnsignedInt();
    result.height = m_reader.readUnsignedInt();
    result.offsetX = m_reader.readSignedInt();
    result.offsetY = m_reader.readSignedInt();

    // Parse flags
    const uint8_t flags = m_reader.readUnsignedByte();

    if ((flags & 0b11111000) != 0)
    {
        // This is forbidden by the specification
        throw PDFException(PDFTranslationContext::tr("JBIG2 region segment information flags are invalid."));
    }

    switch (flags)
    {
        case 0:
            result.operation = PDFJBIG2BitOperation::Or;
            break;

        case 1:
            result.operation = PDFJBIG2BitOperation::And;
            break;

        case 2:
            result.operation = PDFJBIG2BitOperation::Xor;
            break;

        case 3:
            result.operation = PDFJBIG2BitOperation::NotXor;
            break;

        case 4:
            result.operation = PDFJBIG2BitOperation::Replace;
            break;

        default:
            throw PDFException(PDFTranslationContext::tr("JBIG2 region segment information - invalid bit operation mode."));
    }

    checkRegionSegmentInformationField(result);
    m_currentRegionInformation = result;
    return result;
}

PDFJBIG2ATPositions PDFJBIG2Decoder::readATTemplatePixelPositions(int count, bool refinement)
{
    PDFJBIG2ATPositions result = { };

    for (int i = 0; i < count; ++i)
    {
        result[i].x = m_reader.readSignedByte();
        result[i].y = m_reader.readSignedByte();
        if ((!refinement || i == 0) && (result[i].y > 0 || (result[i].y == 0 && result[i].x >= 0)))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 non-causal adaptive template pixel."));
        }
    }

    return result;
}

void PDFJBIG2Decoder::skipSegment(const PDFJBIG2SegmentHeader& header)
{
    m_reader.skipBytes(header.getSegmentDataLength());
}

PDFJBIG2ReferencedSegments PDFJBIG2Decoder::getReferencedSegments(const PDFJBIG2SegmentHeader& header)
{
    PDFJBIG2ReferencedSegments segments;

    for (const uint32_t referredSegmentId : header.getReferredSegments())
    {
        auto it = m_segments.find(referredSegmentId);
        if (it != m_segments.cend())
        {
            const PDFJBIG2Segment* referredSegment = it->second.get();
            if (const PDFJBIG2Bitmap* bitmap = referredSegment->asBitmap())
            {
                segments.bitmaps.push_back(bitmap);
            }
            else if (const PDFJBIG2HuffmanCodeTable* huffmanCodeTable = referredSegment->asHuffmanCodeTable())
            {
                consumeDecodedBytes(uint64_t(huffmanCodeTable->getEntries().size()) * sizeof(PDFJBIG2HuffmanTableEntry) * 2);
                segments.codeTables.push_back(huffmanCodeTable);
            }
            else if (const PDFJBIG2SymbolDictionary* symbolDictionary = referredSegment->asSymbolDictionary())
            {
                consumeDecodedBytes(uint64_t(symbolDictionary->getBitmaps().size()) * sizeof(void*) * 2);
                segments.symbolDictionaries.push_back(symbolDictionary);
            }
            else
            {
                // A stored segment is a bitmap, a code table, a symbol dictionary or
                // a pattern dictionary - nothing else is stored
                const PDFJBIG2PatternDictionary* patternDictionary = referredSegment->asPatternDictionary();
                Q_ASSERT(patternDictionary);
                consumeDecodedBytes(uint64_t(patternDictionary->getBitmaps().size()) * sizeof(void*) * 2);
                segments.patternDictionaries.push_back(patternDictionary);
            }
        }
        else
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid referred segment %1 referenced by segment %2.").arg(referredSegmentId).arg(header.getSegmentNumber()));
        }
    }

    return segments;
}

int PDFJBIG2Decoder::getSegmentDataBytes(const PDFJBIG2SegmentHeader& header, int segmentHeaderBytes) const
{
    Q_ASSERT(header.isSegmentDataLengthDefined());

    // The length of the segment is unsigned and the length of the already read header
    // is subtracted from it. In the unsigned arithmetic the subtraction wraps around
    // for a segment, which declares less data than its own header occupies, so the
    // difference is computed in 64 bits and validated.
    const int64_t segmentDataBytes = int64_t(header.getSegmentDataLength()) - int64_t(segmentHeaderBytes);

    if (segmentDataBytes < 0 || segmentDataBytes > m_reader.getStream()->size())
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid data length %1 of the segment %2.").arg(header.getSegmentDataLength()).arg(header.getSegmentNumber()));
    }

    return int(segmentDataBytes);
}

void PDFJBIG2Decoder::consumeWork(uint64_t count)
{
    if (count > m_workRemaining)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 decoding work limit exceeded."));
    }
    m_workRemaining -= count;
}

void PDFJBIG2Decoder::consumeDecodedBytes(uint64_t count)
{
    constexpr uint64_t limit = MAX_DECODED_BYTES;
    if (count > limit - m_decodedBytes)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 cumulative decoding memory limit exceeded."));
    }
    m_decodedBytes += count;
    consumeWork(count);
}

void PDFJBIG2Decoder::checkBitmapSize(const uint32_t size)
{
    if (size > MAX_BITMAP_SIZE)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 maximum bitmap size exceeded (%1 > %2).").arg(size).arg(MAX_BITMAP_SIZE));
    }
}

void PDFJBIG2Decoder::checkRegionSegmentInformationField(const PDFJBIG2RegionSegmentInformationField& field)
{
    checkBitmapSize(field.width);
    checkBitmapSize(field.height);
    checkBitmapSize(field.offsetX);
    checkBitmapSize(field.offsetY);

    if (field.width == 0 || field.height == 0)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid bitmap size (%1 x %2).").arg(field.width).arg(field.height));
    }

    // Both dimensions are in the allowed range, but their product still can be too
    // large - a region of 65536 x 65536 pixels passes the checks above
    PDFJBIG2Bitmap::checkSize(field.width, field.height);

    // The operation has been validated, when the flags of the field were read
    Q_ASSERT(field.operation != PDFJBIG2BitOperation::Invalid);
}

int32_t PDFJBIG2Decoder::checkInteger(std::optional<int32_t> value)
{
    if (value.has_value())
    {
        return *value;
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 can't read integer."));
    }
}

PDFJBIG2Bitmap::PDFJBIG2Bitmap() :
    m_width(0),
    m_height(0)
{

}

PDFJBIG2Bitmap::PDFJBIG2Bitmap(int width, int height, uint8_t fill) :
    m_width(width),
    m_height(height)
{
    checkSize(width, height);
    m_data.resize(size_t(width) * size_t(height), fill);
}

void PDFJBIG2Bitmap::checkSize(int64_t width, int64_t height)
{
    if (width < 0 || height < 0 || width > std::numeric_limits<int>::max() || height > std::numeric_limits<int>::max())
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid bitmap size (%1 x %2).").arg(width).arg(height));
    }

    // The product must be computed in 64 bits. Both dimensions are limited separately,
    // but their product can still overflow the int - the buffer allocated for such a
    // bitmap would be much smaller than the bitmap itself and every write into it
    // would land outside of the allocated memory.
    if (width != 0 && height > MAX_PIXEL_COUNT / width)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 maximum bitmap pixel count exceeded (%1 x %2 > %3).").arg(width).arg(height).arg(MAX_PIXEL_COUNT));
    }
}

PDFJBIG2Bitmap::~PDFJBIG2Bitmap()
{

}

PDFJBIG2Bitmap PDFJBIG2Bitmap::getSubbitmap(int64_t offsetX, int64_t offsetY, int width, int height) const
{
    PDFJBIG2Bitmap result(width, height, 0x00);
    if (!result.isValid() || offsetX >= m_width || offsetY >= m_height ||
        offsetX <= -int64_t(width) || offsetY <= -int64_t(height))
    {
        return result;
    }

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            result.setPixel(x, y, getPixelSafe(x + offsetX, y + offsetY));
        }
    }

    return result;
}

void PDFJBIG2Bitmap::paint(const PDFJBIG2Bitmap& bitmap, int64_t offsetX, int64_t offsetY, PDFJBIG2BitOperation operation, bool expandY, const uint8_t expandPixel)
{
    if (!bitmap.isValid())
    {
        return;
    }

    // Reject fully clipped input before adding offsets, which may be any int64.
    if (offsetX >= m_width || offsetX <= -int64_t(bitmap.getWidth()) ||
        offsetY <= -int64_t(bitmap.getHeight()) || (!expandY && offsetY >= m_height))
    {
        return;
    }
    if (expandY)
    {
        if (offsetY > int64_t(std::numeric_limits<int>::max()) - bitmap.getHeight())
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 expanded bitmap height out of range."));
        }
        const int64_t requiredHeight = offsetY + bitmap.getHeight();
        if (requiredHeight > m_height)
        {
            resizeHeight(int(requiredHeight), expandPixel);
        }
    }

    // The painted area is the intersection of the painted bitmap and this bitmap,
    // so the source pixel of a target pixel always exists
    const int targetStartX = int(qMax<int64_t>(offsetX, 0));
    const int targetEndX = int(qMax<int64_t>(0, qMin<int64_t>(offsetX + bitmap.getWidth(), m_width)));
    const int targetStartY = int(qMax<int64_t>(offsetY, 0));
    const int targetEndY = int(qMax<int64_t>(0, qMin<int64_t>(offsetY + bitmap.getHeight(), m_height)));

    for (int targetY = targetStartY; targetY < targetEndY; ++targetY)
    {
        for (int targetX = targetStartX; targetX < targetEndX; ++targetX)
        {
            const int sourceX = int(targetX - offsetX);
            const int sourceY = int(targetY - offsetY);

            switch (operation)
            {
                case PDFJBIG2BitOperation::Or:
                    setPixel(targetX, targetY, getPixel(targetX, targetY) | bitmap.getPixel(sourceX, sourceY));
                    break;

                case PDFJBIG2BitOperation::And:
                    setPixel(targetX, targetY, getPixel(targetX, targetY) & bitmap.getPixel(sourceX, sourceY));
                    break;

                case PDFJBIG2BitOperation::Xor:
                    setPixel(targetX, targetY, getPixel(targetX, targetY) ^ bitmap.getPixel(sourceX, sourceY));
                    break;

                case PDFJBIG2BitOperation::NotXor:
                    setPixel(targetX, targetY, getPixel(targetX, targetY) ^ (~bitmap.getPixel(sourceX, sourceY)));
                    break;

                case PDFJBIG2BitOperation::Replace:
                    setPixel(targetX, targetY, bitmap.getPixel(sourceX, sourceY));
                    break;

                default:
                    throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid bitmap paint operation."));
            }
        }
    }
}

void PDFJBIG2Bitmap::resizeHeight(int height, uint8_t fill)
{
    checkSize(m_width, height);
    m_data.resize(size_t(m_width) * size_t(height), fill);
    m_height = height;
}

void PDFJBIG2Bitmap::copyRow(int target, int source)
{
    if (target < 0 || target >= m_height || source < 0 || source >= m_height)
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 - invalid bitmap copy row operation."));
    }

    auto itSource = std::next(m_data.cbegin(), source * m_width);
    auto itSourceEnd = std::next(itSource, m_width);
    auto itTarget = std::next(m_data.begin(), target * m_width);
    std::copy(itSource, itSourceEnd, itTarget);
}

PDFJBIG2HuffmanCodeTable::PDFJBIG2HuffmanCodeTable(std::vector<PDFJBIG2HuffmanTableEntry>&& entries) :
    m_entries(qMove(entries))
{

}

PDFJBIG2HuffmanCodeTable::~PDFJBIG2HuffmanCodeTable()
{

}

std::vector<PDFJBIG2HuffmanTableEntry> PDFJBIG2HuffmanCodeTable::buildPrefixes(const std::vector<PDFJBIG2HuffmanTableEntry>& entries)
{
    std::vector<PDFJBIG2HuffmanTableEntry> result = entries;
    result.erase(std::remove_if(result.begin(), result.end(), [](const PDFJBIG2HuffmanTableEntry& entry) { return entry.prefixBitLength == 0; }), result.end());
    std::stable_sort(result.begin(), result.end(), [](const PDFJBIG2HuffmanTableEntry& l, const PDFJBIG2HuffmanTableEntry& r) { return l.prefixBitLength < r.prefixBitLength; });

    // B.3: a wider accumulator represents the sentinel 2^32 as well as all
    // supported codes. Checking the next code includes shorter prefixes too.
    uint64_t nextCode = 0;
    uint16_t previousLength = 0;
    for (PDFJBIG2HuffmanTableEntry& entry : result)
    {
        if (entry.prefixBitLength > 32 || entry.rangeBitLength > 32)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 unsupported huffman code length."));
        }
        nextCode <<= entry.prefixBitLength - previousLength;
        if (nextCode >= (uint64_t(1) << entry.prefixBitLength))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 overflow of prefix bit values in huffman table."));
        }
        entry.prefix = uint32_t(nextCode++);
        previousLength = entry.prefixBitLength;
    }

    return result;
}

PDFJBIG2Segment::~PDFJBIG2Segment()
{

}

PDFJBIG2HuffmanDecoder::PDFJBIG2HuffmanDecoder(PDFBitReader* reader, uint64_t* workRemaining, const PDFJBIG2HuffmanTableEntry* begin, const PDFJBIG2HuffmanTableEntry* end) :
    m_reader(reader), m_workRemaining(workRemaining), m_entries(begin, end)
{
    initializeEntries();
}

PDFJBIG2HuffmanDecoder::PDFJBIG2HuffmanDecoder(PDFBitReader* reader, uint64_t* workRemaining, const PDFJBIG2HuffmanCodeTable* table) :
    m_reader(reader), m_workRemaining(workRemaining), m_entries(table->getEntries())
{
    initializeEntries();
}

PDFJBIG2HuffmanDecoder::PDFJBIG2HuffmanDecoder(PDFBitReader* reader, uint64_t* workRemaining, std::vector<PDFJBIG2HuffmanTableEntry>&& table) :
    m_reader(reader), m_workRemaining(workRemaining), m_entries(qMove(table))
{
    initializeEntries();
}

void PDFJBIG2HuffmanDecoder::initializeEntries()
{
    for (const auto& entry : m_entries)
    {
        if (entry.prefixBitLength == 0 || entry.prefixBitLength > 32 || entry.rangeBitLength > 32 ||
            uint64_t(entry.prefix) >= (uint64_t(1) << entry.prefixBitLength))
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 invalid huffman code length or prefix."));
        }
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const auto& a, const auto& b)
    {
        return a.prefixBitLength < b.prefixBitLength || (a.prefixBitLength == b.prefixBitLength && a.prefix < b.prefix);
    });
    m_begin = m_entries.empty() ? nullptr : m_entries.data();
    m_end = m_entries.empty() ? nullptr : m_entries.data() + m_entries.size();
    m_groupWork = 32 + 2 * log2ceil(uint32_t(m_entries.size() + 1));
}

bool PDFJBIG2HuffmanDecoder::hasOutOfBand() const
{
    return std::any_of(m_entries.begin(), m_entries.end(), [](const auto& entry) { return entry.isOutOfBand(); });
}

PDFJBIG2HuffmanDecoder& PDFJBIG2HuffmanDecoder::operator=(PDFJBIG2HuffmanDecoder&& other)
{
    if (this != &other)
    {
        m_reader = other.m_reader;
        m_workRemaining = other.m_workRemaining;
        m_groupWork = other.m_groupWork;
        m_entries = qMove(other.m_entries);
        m_begin = m_entries.empty() ? nullptr : m_entries.data();
        m_end = m_entries.empty() ? nullptr : m_entries.data() + m_entries.size();
        other.m_begin = other.m_end = nullptr;
        other.m_reader = nullptr;
    }

    return *this;
}

std::optional<int32_t> PDFJBIG2HuffmanDecoder::readSignedInteger()
{
    uint32_t prefixBitCount = 0;
    uint32_t prefix = 0;
    // At most 32 groups and logarithmic searches. A large symbol table must not
    // turn every decoded instance into a linear scan over all imported symbols.
    for (const PDFJBIG2HuffmanTableEntry* first = m_begin; first != m_end;)
    {
        if (m_workRemaining)
        {
            if (*m_workRemaining < m_groupWork)
            {
                throw PDFException(PDFTranslationContext::tr("JBIG2 decoding work limit exceeded."));
            }
            *m_workRemaining -= m_groupWork;
        }
        const uint16_t length = first->prefixBitLength;
        const auto* last = std::upper_bound(first, m_end, length, [](uint16_t bits, const auto& entry)
        {
            return bits < entry.prefixBitLength;
        });
        prefix = uint32_t((uint64_t(prefix) << (length - prefixBitCount)) | m_reader->read(length - prefixBitCount));
        prefixBitCount = length;
        const auto* entry = std::lower_bound(first, last, prefix, [](const auto& item, uint32_t code)
        {
            return item.prefix < code;
        });
        if (entry != last && entry->prefix == prefix)
        {
            if (entry->isOutOfBand())
            {
                return std::nullopt;
            }
            const int64_t offset = int64_t(m_reader->read(entry->rangeBitLength));
            return checkedJBIG2Integer(int64_t(entry->value) + (entry->isLowValue() ? -offset : offset));
        }
        first = last;
    }
    throw PDFException(PDFTranslationContext::tr("JBIG2 invalid huffman prefix."));
}

std::vector<const PDFJBIG2Bitmap*> PDFJBIG2ReferencedSegments::getSymbolBitmaps() const
{
    size_t total = 0;
    for (const auto* dictionary : symbolDictionaries)
    {
        if (dictionary->getBitmaps().size() > MAX_JBIG2_SYMBOL_COUNT - total)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 maximum referenced symbol count exceeded."));
        }
        total += dictionary->getBitmaps().size();
    }
    std::vector<const PDFJBIG2Bitmap*> result;
    result.reserve(total);
    for (const auto* dictionary : symbolDictionaries)
    {
        for (const PDFJBIG2Bitmap& bitmap : dictionary->getBitmaps())
        {
            result.push_back(&bitmap);
        }
    }
    return result;
}

std::vector<const PDFJBIG2Bitmap*> PDFJBIG2ReferencedSegments::getPatternBitmaps() const
{
    size_t total = 0;
    for (const auto* dictionary : patternDictionaries)
    {
        if (dictionary->getBitmaps().size() > MAX_JBIG2_SYMBOL_COUNT - total)
        {
            throw PDFException(PDFTranslationContext::tr("JBIG2 maximum referenced pattern count exceeded."));
        }
        total += dictionary->getBitmaps().size();
    }
    std::vector<const PDFJBIG2Bitmap*> result;
    result.reserve(total);
    for (const auto* dictionary : patternDictionaries)
    {
        for (const PDFJBIG2Bitmap& bitmap : dictionary->getBitmaps())
        {
            result.push_back(&bitmap);
        }
    }
    return result;
}

PDFJBIG2HuffmanDecoder PDFJBIG2ReferencedSegments::getUserTable(PDFBitReader* reader, uint64_t* workRemaining)
{
    if (currentUserCodeTableIndex < codeTables.size())
    {
        return PDFJBIG2HuffmanDecoder(reader, workRemaining, codeTables[currentUserCodeTableIndex++]);
    }
    else
    {
        throw PDFException(PDFTranslationContext::tr("JBIG2 invalid user huffman code table."));
    }
}

}   // namespace pdf
