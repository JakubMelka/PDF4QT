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

#ifndef PDFCONTENTTEXTLAYOUT_H
#define PDFCONTENTTEXTLAYOUT_H

#include "pdffont.h"
#include "pdfglobal.h"

#include <QByteArray>
#include <QString>
#include <QTransform>

#include <optional>
#include <vector>

namespace pdf
{

// Edit Content, stage E0, package P06 (text layout, reading only). Plan: zadani_jadro.md 4.7 (first
// part), 5.2; zadani.md KE.19, KE.24, RE-8. The layout follows ISO 32000-2, 9.4.4 with the widths
// of the font dictionary; it does not take positions from PDFPageContentProcessor (the processor
// takes the advance of an embedded simple font from the font program - NOTES.txt 9.4.4).

/// The text state a show operator uses (ISO 32000-2, 9.3, Table 102)
///
/// Threads and ownership: a plain value; it co-owns the font and the realized font by shared
/// pointers (both are only read), so a copy keeps them alive in any thread.
struct PDFContentTextState
{
    PDFFontPointer font;                    ///< nullptr: the text cannot be laid out
    PDFRealizedFontPointer realizedFont;    ///< Outlines of the glyphs (may be nullptr: no outlines); it owns them
    PDFReal fontSize = 0.0;                 ///< Tfs
    PDFReal characterSpacing = 0.0;         ///< Tc
    PDFReal wordSpacing = 0.0;              ///< Tw
    PDFReal horizontalScaling = 100.0;      ///< Tz in percent (Th = Tz / 100)
    PDFReal leading = 0.0;                  ///< TL
    PDFReal rise = 0.0;                     ///< Ts
    TextRenderingMode renderingMode = TextRenderingMode::Fill;
};

/// One glyph or one spacer of a shown text.
///
/// Threads and ownership: a plain value; \p outline points into the realized font of the text
/// state, which the owner of the items (the node data of the text) holds by a shared pointer.
struct PDFContentTextItem
{
    enum class Kind
    {
        Glyph,      ///< A character code of the string (also one without a Unicode value and one, whose glyph is missing in the font program)
        Spacer      ///< A horizontal move without a glyph: a number of TJ
    };

    Kind kind = Kind::Glyph;

    // Glyph
    QByteArray codeBytes;                   ///< The bytes of the code (1 to 4)
    unsigned int code = 0;                  ///< The bytes as a big-endian number
    CID cid = 0;                            ///< Type 0: the CID of the code; else the code
    QString unicode;                        ///< Text of the glyph (getUnicode); empty = no Unicode value
    std::optional<PDFReal> width;           ///< w0 of the font dictionary in text space units for the font size 1 (/Widths, /MissingWidth; /W, /DW; Type 3: /Widths through /FontMatrix); no value = the dictionary gives none
    bool isWordSpace = false;               ///< The single-byte code 32 (9.3.3)
    bool hasOutline = false;                ///< The font program has the glyph
    const PDFGlyphOutline* outline = nullptr;   ///< Its outline in the space of the realized font (text space scaled by the absolute font size, as PDFPageContentProcessor::drawText uses it); nullptr for a Type 3 glyph and a missing one

    // Spacer
    PDFReal adjustment = 0.0;               ///< The number of TJ (thousandths of text space, subtracted)

    // Both
    QTransform textMatrix;                  ///< Tm before the item (text space to the space of the text object)
    PDFReal advance = 0.0;                  ///< tx of 9.4.4 (the move of Tm along x): ((w0 - Tj / 1000) * Tfs + Tc + Tw) * Th; (Tc + Tw) * Th for a glyph without a width; 0 in the vertical writing mode (Tm moves along y there, read the move from the text matrices)
};

/// The text matrices of one text object by ISO 32000-2, 9.4.2 - 9.4.4, and the layout of the
/// strings of its show operators.
///
/// Threads and ownership: a local state machine of one thread (the tree builder creates one per
/// BT ... ET). The static functions are reentrant and read the immutable font only.
class PDF4QTLIBCORESHARED_EXPORT PDFContentTextLayout
{
public:
    explicit PDFContentTextLayout();

    /// BT: Tm = Tlm = identity
    void beginText();

    /// Td: Tlm = translate(tx, ty) x Tlm, Tm = Tlm
    void moveByOffset(PDFReal tx, PDFReal ty);

    /// Tm: Tm = Tlm = matrix
    void setMatrix(const QTransform& matrix);

    /// T* (and the first step of ' and "): moveByOffset(0, -leading)
    void moveByLeading(PDFReal leading);

    /// A string of Tj, ', " or of an element of TJ: its glyphs with their positions; Tm moves by
    /// their advances. A glyph without a width moves by Tc (+ Tw) only and isComplete becomes false.
    std::vector<PDFContentTextItem> showString(const QByteArray& string, const PDFContentTextState& state);

    /// A number of TJ: Tm moves by -number / 1000 * Tfs * Th
    PDFContentTextItem applyAdjustment(PDFReal number, const PDFContentTextState& state);

    const QTransform& getTextMatrix() const { return m_textMatrix; }
    const QTransform& getTextLineMatrix() const { return m_textLineMatrix; }

    /// Every glyph shown so far had a width in the font dictionary: the positions are the ones
    /// of the standard. Otherwise the text is only movable as a whole (no editing of glyphs).
    /// Also false after: a vertical font, a string without a font, trailing bytes that are no
    /// whole code of the CMap, a move that is not finite (such a move is not made).
    bool isComplete() const { return m_isComplete; }

    /// The character codes of a string: one byte each in a simple and a Type 3 font, the codes
    /// matched by the CMap in a Type 0 font (PDFFontCMap::interpretWithCode). A code is a plain
    /// value.
    struct Code
    {
        QByteArray bytes;
        unsigned int code = 0;
        CID cid = 0;
    };
    static std::vector<Code> decodeString(const PDFFont* font, const QByteArray& string);

    /// w0 of a code in text space units for the font size 1, from the font dictionary only
    /// (never from the font program): no value, when the dictionary has no width for the code
    /// (a standard 14 font without /Widths). The accessors of pdffont.h tell "no width" from the
    /// width 0: PDFSimpleFont::hasGlyphAdvance / getGlyphAdvance (/Widths), getFirstChar,
    /// getLastChar and getFontDescriptor()->missingWidth (/MissingWidth); PDFType3Font::hasWidth
    /// / getWidth; PDFType0Font::hasExplicitGlyphAdvance / getGlyphAdvance (/W) and
    /// getDefaultGlyphAdvance (/DW).
    static std::optional<PDFReal> getDictionaryWidth(const PDFFont* font, const Code& code);

    /// The glyphs of a text in this font can be edited: horizontal writing and widths in the
    /// dictionary (a vertical font, a font without widths: the text is moved as a whole only)
    static bool isEditable(const PDFFont* font);

    /// The text of a code: /ToUnicode (PDFFont::getToUnicodeText), else the glyph name of a simple
    /// or Type 3 font (PDFFont::getEncodingText), else the Unicode CMap of the predefined character
    /// collection of a Type 0 font; empty = no Unicode value (also for an undefined code of a
    /// base encoding)
    static QString getUnicode(const PDFFont* font, const Code& code);

private:
    QTransform m_textMatrix;
    QTransform m_textLineMatrix;
    bool m_isComplete = true;
};

}   // namespace pdf

#endif // PDFCONTENTTEXTLAYOUT_H
