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

// Edit Content, stage E0, package P06: the layout of a shown text by ISO 32000-2, 9.4.2 - 9.4.4
// with the widths of the font dictionary (reading only; zadani_jadro.md 4.7, 5.2).
//
// What the layout takes from where:
//   - the character codes from the string itself (9.4.3): one byte in a simple and in a Type 3
//     font, the codes matched by the CMap in a Type 0 font;
//   - the width w0 of a code from the font dictionary only: /Widths and /MissingWidth of a simple
//     font (9.6.2.1 Table 109, 9.8.1 Table 120), /Widths through /FontMatrix of a Type 3 font
//     (9.6.4 Table 110), /W and /DW of the CIDFont of a Type 0 font (9.7.4.3). Never from the
//     font program;
//   - the outline of a glyph from the realized font of the text state (it is used only to show
//     the glyph; a text without a realized font is laid out the same way, without outlines);
//   - the text of a code from /ToUnicode, else from the encoding (9.10.2).
//
// The displacement of a glyph (9.4.4): tx = ((w0 - Tj / 1000) * Tfs + Tc + Tw) * Th, ty = 0 in
// the horizontal writing mode; tx = 0, ty = (w1 - Tj / 1000) * Tfs + Tc + Tw in the vertical one.
// Tw belongs to the single-byte character code 32 only (9.3.3). A negative font size is not
// treated specially: w0 * Tfs changes its sign, Tc and Tw do not (9.3.2, 9.3.3). The text rise
// and the horizontal scaling of the glyph itself are not parts of the text matrix: they belong
// to the text rendering matrix [Tfs * Th 0 0 Tfs 0 Trise] x Tm x CTM of the user of the items.
//
// The outline of an item is in the space of the realized font: the text space scaled for the
// absolute value of the font size of the realized font (the glyph of the size 1 multiplied by
// the size), as the processor uses it - its matrix is [Th * s 0 0 s 0 Trise] x Tm, where s is the
// sign of the font size.

#include "pdfcontenttextlayout.h"
#include "pdfexception.h"

#include <cmath>

namespace pdf
{

/// Private helpers of the text layout: static functions, which read the immutable font only
/// (reentrant).
class PDFContentTextLayoutHelper
{
public:
    PDFContentTextLayoutHelper() = delete;

    /// A number of /Widths of a simple font, of /W and /DW is in thousandths of the text space unit
    static constexpr PDFReal GLYPH_SPACE_UNIT = 0.001;

    /// The horizontal scaling Tz is in percent
    static constexpr PDFReal PERCENT = 0.01;

    /// The character code, which gets the word spacing, when it has one byte (9.3.3)
    static constexpr unsigned int WORD_SPACE_CODE = 32;

    /// The last character code of a simple and of a Type 3 font
    static constexpr unsigned int MAXIMUM_SINGLE_BYTE_CODE = 255;

    /// The character, which the encoding tables of the library have for a code without
    /// a character (pdfencoding.cpp: "not present in character set")
    static constexpr char16_t UNDEFINED_CHARACTER = 0xFFFD;

    /// The font as a Type 0 font of the vertical writing mode (/WMode 1 of its CMap, 9.7.5),
    /// nullptr for every other font. The accessors of the widths of such a font give the
    /// vertical displacement w1 of /W2 and /DW2, not w0.
    static const PDFType0Font* getVerticalFont(const PDFFont* font);

    /// The font dictionary of a simple font has /Widths with a width of at least one code
    static bool hasWidths(const PDFSimpleFont* font);

    /// The font dictionary of a Type 3 font has /Widths with a width of at least one code
    static bool hasWidths(const PDFType3Font* font);

    /// All numbers of the matrix are finite numbers
    static bool isFiniteMatrix(const QTransform& matrix);

    /// A matrix, whose lazily evaluated kind (QTransform::type, stored in the object by a const
    /// function) is evaluated: its copies are plain data, which more threads can read
    static QTransform getEvaluatedMatrix(QTransform matrix);

    /// matrix = [1 0 0 1 tx ty] x matrix (9.4.2, 9.4.4). A move, which is not a finite number or
    /// which takes the matrix out of the finite numbers (huge operands of the content), is not
    /// made: the matrix stays a matrix of numbers and the function returns false.
    static bool moveMatrix(QTransform* matrix, PDFReal tx, PDFReal ty);

    /// Sets the outlines of the glyphs of the items of one string from the realized font. The
    /// text sequence of the realized font has at most one item per character code, in the order
    /// of the string; a code, which has neither a glyph nor an advance there, has none. The
    /// items of the same string are matched by the code and its length (the same code gives the
    /// same answer always, so the first match is the right one).
    static void fillOutlines(std::vector<PDFContentTextItem>& items, const QByteArray& string, const PDFRealizedFontPointer& realizedFont);
};

const PDFType0Font* PDFContentTextLayoutHelper::getVerticalFont(const PDFFont* font)
{
    const PDFType0Font* type0Font = dynamic_cast<const PDFType0Font*>(font);
    if (type0Font && type0Font->getCMap()->isVertical())
    {
        return type0Font;
    }

    return nullptr;
}

bool PDFContentTextLayoutHelper::hasWidths(const PDFSimpleFont* font)
{
    for (unsigned int code = 0; code <= MAXIMUM_SINGLE_BYTE_CODE; ++code)
    {
        if (font->hasGlyphAdvance(code))
        {
            return true;
        }
    }

    return false;
}

bool PDFContentTextLayoutHelper::hasWidths(const PDFType3Font* font)
{
    for (unsigned int code = 0; code <= MAXIMUM_SINGLE_BYTE_CODE; ++code)
    {
        if (font->hasWidth(static_cast<int>(code)))
        {
            return true;
        }
    }

    return false;
}

bool PDFContentTextLayoutHelper::isFiniteMatrix(const QTransform& matrix)
{
    return std::isfinite(matrix.m11()) && std::isfinite(matrix.m12()) && std::isfinite(matrix.m13()) &&
           std::isfinite(matrix.m21()) && std::isfinite(matrix.m22()) && std::isfinite(matrix.m23()) &&
           std::isfinite(matrix.m31()) && std::isfinite(matrix.m32()) && std::isfinite(matrix.m33());
}

QTransform PDFContentTextLayoutHelper::getEvaluatedMatrix(QTransform matrix)
{
    const QTransform::TransformationType type = matrix.type();
    Q_UNUSED(type);
    return matrix;
}

bool PDFContentTextLayoutHelper::moveMatrix(QTransform* matrix, PDFReal tx, PDFReal ty)
{
    // A displacement, which is not a finite number, gives a matrix with such a number too
    const QTransform movedMatrix = QTransform::fromTranslate(tx, ty) * (*matrix);
    if (!isFiniteMatrix(movedMatrix))
    {
        return false;
    }

    *matrix = getEvaluatedMatrix(movedMatrix);
    return true;
}

void PDFContentTextLayoutHelper::fillOutlines(std::vector<PDFContentTextItem>& items, const QByteArray& string, const PDFRealizedFontPointer& realizedFont)
{
    if (!realizedFont || items.empty())
    {
        return;
    }

    // A glyph, which the font program cannot give (a damaged program), ends the sequence with an
    // exception: the glyphs before it have their outlines, the rest of the string has none. The
    // layout does not depend on it.
    TextSequence textSequence;
    PDFRenderErrorReporterDummy reporter;
    try
    {
        realizedFont->fillTextSequence(string, textSequence, &reporter);
    }
    catch (const PDFException&)
    {
    }
    catch (const PDFRendererException&)
    {
    }

    auto it = textSequence.items.cbegin();
    for (PDFContentTextItem& item : items)
    {
        if (it == textSequence.items.cend())
        {
            break;
        }

        if (it->code == item.code && it->codeLength == static_cast<unsigned int>(item.codeBytes.size()))
        {
            // The glyph of a Type 3 font is a glyph procedure: the font has it, but there is no outline
            item.hasOutline = it->glyph || it->characterContentStream;
            item.outline = it->glyph;
            ++it;
        }
    }
}

PDFContentTextLayout::PDFContentTextLayout() = default;

void PDFContentTextLayout::beginText()
{
    // 9.4.1: the text matrix and the text line matrix are the identity matrix at the beginning
    // of a text object
    m_textMatrix = QTransform();
    m_textLineMatrix = QTransform();
    m_isComplete = true;
}

void PDFContentTextLayout::moveByOffset(PDFReal tx, PDFReal ty)
{
    // 9.4.2, Table 106, Td: Tm = Tlm = [1 0 0 1 tx ty] x Tlm. A move out of the finite numbers
    // is not made: the text returns to the start of its line and is not complete.
    if (!PDFContentTextLayoutHelper::moveMatrix(&m_textLineMatrix, tx, ty))
    {
        m_isComplete = false;
    }

    m_textMatrix = m_textLineMatrix;
}

void PDFContentTextLayout::setMatrix(const QTransform& matrix)
{
    // 9.4.2, Table 106, Tm: the operands replace the matrices, they are not concatenated. Operands,
    // which are not finite numbers, set nothing: the text returns to the start of its line and
    // is not complete.
    if (PDFContentTextLayoutHelper::isFiniteMatrix(matrix))
    {
        m_textLineMatrix = PDFContentTextLayoutHelper::getEvaluatedMatrix(matrix);
    }
    else
    {
        m_isComplete = false;
    }

    m_textMatrix = m_textLineMatrix;
}

void PDFContentTextLayout::moveByLeading(PDFReal leading)
{
    // 9.4.2, Table 106, T*: the same as 0 -Tl TD, which is 0 -Tl Td (the leading stays Tl)
    moveByOffset(0.0, -leading);
}

std::vector<PDFContentTextItem> PDFContentTextLayout::showString(const QByteArray& string, const PDFContentTextState& state)
{
    std::vector<PDFContentTextItem> items;

    const PDFFont* font = state.font.data();
    if (!font)
    {
        // Tf has no initial value (9.3.1): without a font the string cannot be divided to
        // character codes, so nothing is shown and nothing moves
        if (!string.isEmpty())
        {
            m_isComplete = false;
        }

        return items;
    }

    const std::vector<Code> codes = decodeString(font, string);
    const PDFType0Font* verticalFont = PDFContentTextLayoutHelper::getVerticalFont(font);
    const PDFReal horizontalScaling = state.horizontalScaling * PDFContentTextLayoutHelper::PERCENT;

    qsizetype decodedLength = 0;
    items.reserve(codes.size());
    for (const Code& code : codes)
    {
        PDFContentTextItem item;
        item.kind = PDFContentTextItem::Kind::Glyph;
        item.codeBytes = code.bytes;
        item.code = code.code;
        item.cid = code.cid;
        item.unicode = getUnicode(font, code);
        item.width = getDictionaryWidth(font, code);
        item.isWordSpace = code.bytes.size() == 1 && code.code == PDFContentTextLayoutHelper::WORD_SPACE_CODE;
        item.textMatrix = m_textMatrix;

        // 9.3.2, 9.3.3: the character spacing of every code, the word spacing of the single-byte
        // code 32; both in unscaled text space units, whatever the sign of the font size is
        const PDFReal spacing = state.characterSpacing + (item.isWordSpace ? state.wordSpacing : 0.0);

        PDFReal tx = 0.0;
        PDFReal ty = 0.0;
        if (verticalFont)
        {
            // 9.4.4: tx = 0, ty = w1 * Tfs + Tc + Tw. The accessor of the font gives w1 of /W2,
            // else of /DW2 (9.7.4.3), so the text matrix moves as the standard says. The position
            // vector of the glyph is not known and the dictionary width w0 is not used: the
            // glyphs of the text are not laid out, the text is movable as a whole only.
            const PDFReal w1 = verticalFont->getGlyphAdvance(code.cid) * PDFContentTextLayoutHelper::GLYPH_SPACE_UNIT;
            ty = w1 * state.fontSize + spacing;
            m_isComplete = false;
        }
        else
        {
            // 9.4.4: tx = (w0 * Tfs + Tc + Tw) * Th, ty = 0. A code without a width in the
            // dictionary has only its spacing.
            const PDFReal w0 = item.width.value_or(0.0);
            tx = (w0 * state.fontSize + spacing) * horizontalScaling;

            if (!item.width.has_value())
            {
                m_isComplete = false;
            }
        }

        // Tm = [1 0 0 1 tx ty] x Tm. A displacement out of the finite numbers is none.
        if (!PDFContentTextLayoutHelper::moveMatrix(&m_textMatrix, tx, ty))
        {
            tx = 0.0;
            m_isComplete = false;
        }

        item.advance = tx;

        decodedLength += code.bytes.size();
        items.emplace_back(std::move(item));
    }

    if (decodedLength != string.size())
    {
        // Bytes at the end of the string, which are no character code of the CMap (a part of
        // a code of more bytes), are no item: the items do not say the whole string
        m_isComplete = false;
    }

    PDFContentTextLayoutHelper::fillOutlines(items, string, state.realizedFont);

    return items;
}

PDFContentTextItem PDFContentTextLayout::applyAdjustment(PDFReal number, const PDFContentTextState& state)
{
    PDFContentTextItem item;
    item.kind = PDFContentTextItem::Kind::Spacer;
    item.adjustment = number;
    item.textMatrix = m_textMatrix;

    // 9.4.3, Table 107, TJ and 9.4.4: the number is subtracted from the displacement in
    // thousandths of the text space unit - tx = -Tj / 1000 * Tfs * Th in the horizontal writing
    // mode, ty = -Tj / 1000 * Tfs in the vertical one; no character and no word spacing
    PDFReal tx = 0.0;
    PDFReal ty = 0.0;
    const PDFReal displacement = -number * PDFContentTextLayoutHelper::GLYPH_SPACE_UNIT * state.fontSize;
    if (PDFContentTextLayoutHelper::getVerticalFont(state.font.data()))
    {
        ty = displacement;
    }
    else
    {
        tx = displacement * state.horizontalScaling * PDFContentTextLayoutHelper::PERCENT;
    }

    // A displacement out of the finite numbers is none
    if (!PDFContentTextLayoutHelper::moveMatrix(&m_textMatrix, tx, ty))
    {
        tx = 0.0;
        m_isComplete = false;
    }

    item.advance = tx;

    return item;
}

std::vector<PDFContentTextLayout::Code> PDFContentTextLayout::decodeString(const PDFFont* font, const QByteArray& string)
{
    std::vector<Code> codes;

    if (!font || string.isEmpty())
    {
        return codes;
    }

    if (font->getFontType() == FontType::Type0)
    {
        // 9.7.6.2: the CMap divides the string to the codes of one to four bytes. A CMap without
        // any code (a font, which was not made by PDFFont::createFont) decodes nothing.
        const PDFFontCMap* cmap = font->getCMap();
        if (!cmap || !cmap->isValid() || cmap->getMaxKeyLength() == 0)
        {
            return codes;
        }

        const std::vector<PDFFontCMap::MappedCode> mappedCodes = cmap->interpretWithCode(string);
        codes.reserve(mappedCodes.size());

        // The codes follow one another in the string without a gap; bytes at the end, which
        // are not a whole code, are not returned
        qsizetype position = 0;
        for (const PDFFontCMap::MappedCode& mappedCode : mappedCodes)
        {
            const qsizetype byteCount = static_cast<qsizetype>(mappedCode.byteCount);
            if (byteCount <= 0 || byteCount > string.size() - position)
            {
                break;
            }

            Code code;
            code.bytes = string.mid(position, byteCount);
            code.code = mappedCode.code;
            code.cid = mappedCode.cid;
            codes.emplace_back(std::move(code));

            position += byteCount;
        }

        return codes;
    }

    // 9.4.3: a simple and a Type 3 font have codes of one byte
    codes.reserve(static_cast<size_t>(string.size()));
    for (qsizetype i = 0; i < string.size(); ++i)
    {
        Code code;
        code.bytes = string.mid(i, 1);
        code.code = static_cast<unsigned char>(string[i]);
        code.cid = code.code;
        codes.emplace_back(std::move(code));
    }

    return codes;
}

std::optional<PDFReal> PDFContentTextLayout::getDictionaryWidth(const PDFFont* font, const Code& code)
{
    if (!font)
    {
        return std::nullopt;
    }

    if (const PDFType0Font* type0Font = dynamic_cast<const PDFType0Font*>(font))
    {
        // A font of the vertical writing mode keeps w1 of /W2 and /DW2 in the place of the
        // widths: its w0 is not readable (and its text is not edited by glyphs)
        if (type0Font->getCMap()->isVertical())
        {
            return std::nullopt;
        }

        // 9.7.4.3: the width of the CID in /W, else /DW (1000, when the key is missing) - every
        // CID has a width
        const PDFReal width = type0Font->hasExplicitGlyphAdvance(code.cid) ? type0Font->getGlyphAdvance(code.cid)
                                                                           : type0Font->getDefaultGlyphAdvance();
        return width * PDFContentTextLayoutHelper::GLYPH_SPACE_UNIT;
    }

    if (code.code > PDFContentTextLayoutHelper::MAXIMUM_SINGLE_BYTE_CODE)
    {
        return std::nullopt;
    }

    if (const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font))
    {
        // 9.6.4, Table 110: /Widths is in the glyph space; "If FontMatrix specifies a rotation,
        // only the horizontal component of the transformed width shall be used", the
        // displacement is horizontal in the text space
        const int characterIndex = static_cast<int>(code.code);
        if (type3Font->hasWidth(characterIndex))
        {
            return type3Font->getWidth(characterIndex) * type3Font->getFontMatrix().m11();
        }

        // Table 110: "For character codes outside the range FirstChar to LastChar, the width
        // shall be 0" - a width, when the font has /Widths (the key is required; a font without
        // it has no width in its dictionary at all)
        if (PDFContentTextLayoutHelper::hasWidths(type3Font))
        {
            return 0.0;
        }

        return std::nullopt;
    }

    if (const PDFSimpleFont* simpleFont = dynamic_cast<const PDFSimpleFont*>(font))
    {
        // 9.6.2.1, Table 109: /Widths has the widths of the codes /FirstChar ... /LastChar
        if (simpleFont->hasGlyphAdvance(code.code))
        {
            return static_cast<PDFReal>(simpleFont->getGlyphAdvance(code.code)) * PDFContentTextLayoutHelper::GLYPH_SPACE_UNIT;
        }

        // Table 109: "For character codes outside the range FirstChar to LastChar, the value of
        // MissingWidth from the FontDescriptor entry for this font shall be used"; 9.8.1, Table 120:
        // it is the width of the codes, whose widths are not in /Widths, its default value is 0.
        // It completes /Widths: a font without /Widths (one of the standard 14 fonts of a document
        // older than PDF 2.0, whose widths the reader knows by itself, or a damaged font, which
        // a reader lays out by the font program) has no width in its dictionary at all.
        if (PDFContentTextLayoutHelper::hasWidths(simpleFont))
        {
            return simpleFont->getFontDescriptor()->missingWidth * PDFContentTextLayoutHelper::GLYPH_SPACE_UNIT;
        }
    }

    return std::nullopt;
}

bool PDFContentTextLayout::isEditable(const PDFFont* font)
{
    if (!font)
    {
        return false;
    }

    if (const PDFType0Font* type0Font = dynamic_cast<const PDFType0Font*>(font))
    {
        // /DW has a default value, so every CID of a font of the horizontal writing mode has
        // a width; a CMap without any code decodes nothing
        const PDFFontCMap* cmap = type0Font->getCMap();
        return cmap->isValid() && cmap->getMaxKeyLength() > 0 && !cmap->isVertical();
    }

    if (const PDFType3Font* type3Font = dynamic_cast<const PDFType3Font*>(font))
    {
        return PDFContentTextLayoutHelper::hasWidths(type3Font);
    }

    if (const PDFSimpleFont* simpleFont = dynamic_cast<const PDFSimpleFont*>(font))
    {
        return PDFContentTextLayoutHelper::hasWidths(simpleFont);
    }

    return false;
}

QString PDFContentTextLayout::getUnicode(const PDFFont* font, const Code& code)
{
    if (!font)
    {
        return QString();
    }

    // 9.10.2: /ToUnicode first. What it says is the text of the code, also when it is the
    // replacement character.
    const unsigned int codeLength = static_cast<unsigned int>(code.bytes.size());
    QString text = font->getToUnicodeText(code.code, codeLength);
    if (!text.isEmpty())
    {
        return text;
    }

    if (const PDFType0Font* type0Font = dynamic_cast<const PDFType0Font*>(font))
    {
        // 9.10.2: a font of one of the predefined character collections maps its CID by the
        // Unicode CMap of the collection; another Type 0 font has no text without /ToUnicode
        const QChar character = type0Font->getUnicodeFromCID(code.cid);
        return character.isNull() ? QString() : QString(character);
    }

    // 9.10.2: the glyph name of the encoding of a simple or of a Type 3 font. An undefined code of
    // a base encoding has the replacement character in the tables of the library: the encoding
    // says no character for it, so it is no text.
    text = font->getEncodingText(code.code, codeLength);
    if (text.size() == 1 && text.front() == QChar(PDFContentTextLayoutHelper::UNDEFINED_CHARACTER))
    {
        text.clear();
    }

    return text;
}

}   // namespace pdf
