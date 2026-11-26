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

#ifndef PDFNAMETOUNICODE_H
#define PDFNAMETOUNICODE_H

#include "pdfglobal.h"

#include <QChar>
#include <QString>
#include <QByteArray>

namespace pdf
{

class PDF4QTLIBCORESHARED_EXPORT PDFNameToUnicode
{
public:
    explicit PDFNameToUnicode() = delete;

    /// Returns unicode character for name. If name is not found, then null character is returned.
    static QChar getUnicodeForName(const QByteArray& name);

    /// Returns unicode character for name (for ZapfDingbats). If name is not found, then null character is returned.
    static QChar getUnicodeForNameZapfDingbats(const QByteArray& name);

    /// Returns glyph name for unicode character. If character is not found, then empty byte array is returned.
    static QByteArray getNameForUnicode(QChar character);

    /// Returns ZapfDingbats glyph name for unicode character. If character is not found, then empty byte array is returned.
    static QByteArray getNameForUnicodeZapfDingbats(QChar character);

    /// Returns glyph name of the Symbol encoding (glyph name of the font Symbol) for unicode
    /// character. If character is not found, then empty byte array is returned.
    static QByteArray getNameForUnicodeSymbol(QChar character);

    /// Tries to resolve unicode name
    static QChar getUnicodeUsingResolvedName(const QByteArray& name);

    /// Maps a glyph name to its character string by the Adobe Glyph List Specification, section 2
    /// (The mapping): everything from the first period of the name is dropped, the rest is split
    /// to components at the underscores and the strings of the components are joined. A component
    /// is a name of the ZapfDingbats list (first for the font ZapfDingbats), a name of the glyph
    /// list, "uni" with groups of four hexadecimal digits (each group a value of 0000 - D7FF or
    /// E000 - FFFF) or "u" with four to six hexadecimal digits (a value of 0000 - D7FF or
    /// E000 - 10FFFF); any other component gives nothing. The result is a string of UTF-16
    /// code units, not a character: it is empty (.notdef, an unknown name), it has one code
    /// unit, or more of them (f_i, uni00410042, a surrogate pair of u1F600). The name is data
    /// of a document: any bytes of any length are accepted, the time and the memory are linear
    /// to the length of the name.
    ///
    /// Two deliberate deviations from the specification (NOTES.txt, 9.10.2), which keep the texts
    /// the library gave before it followed the specification: lower case hexadecimal digits are
    /// accepted (uni20ac), and a component, which is neither a name of the glyph list nor of
    /// the form "uni" / "u", is looked up in the ZapfDingbats list in every simple font (the
    /// specification asks that list only for the font ZapfDingbats). A Type 3 font never asks
    /// that list - ZapfDingbatsLookup::Never of the other overload.
    /// \param name Glyph name
    /// \param isZapfDingbats The font is ZapfDingbats (its PostScript name is ZapfDingbats):
    ///        ZapfDingbatsLookup::First, otherwise ZapfDingbatsLookup::Last
    static QString getUnicodeTextForName(const QByteArray& name, bool isZapfDingbats);

    /// When a component of a glyph name is looked up in the ZapfDingbats list
    enum class ZapfDingbatsLookup
    {
        First,  ///< Before the glyph list: the font ZapfDingbats (the specification)
        Last,   ///< After the glyph list and the forms "uni" / "u": another simple font (the deviation of getUnicodeTextForName)
        Never   ///< A Type 3 font: the names a<code> of its glyphs (bitmap fonts of TeX) are no dingbats
    };

    /// Maps a glyph name to its character string as getUnicodeTextForName(name, isZapfDingbats),
    /// with the say, when the ZapfDingbats list is asked
    static QString getUnicodeTextForName(const QByteArray& name, ZapfDingbatsLookup zapfDingbatsLookup);

private:
    /// Appends the character string of one component of a glyph name (getUnicodeTextForName)
    static void appendUnicodeTextForComponent(const QByteArray& component, ZapfDingbatsLookup zapfDingbatsLookup, QString& text);

    /// Value of a hexadecimal digit (upper or lower case), -1 for any other character
    static int getHexadecimalDigit(char character);

    struct Comparator
    {
        inline bool operator()(const QByteArray& left, const std::pair<QChar, const char*>& right)
        {
            return left < right.second;
        }
        inline bool operator()(const std::pair<QChar, const char*>& left, const QByteArray& right)
        {
            return left.second < right;
        }
        inline bool operator()(const std::pair<QChar, const char*>& left, const std::pair<QChar, const char*>& right)
        {
            return QLatin1String(left.second) < QLatin1String(right.second);
        }
    };
};

}   // namespace pdf

#endif // PDFNAMETOUNICODE_H
