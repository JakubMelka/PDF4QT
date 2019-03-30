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

#ifndef PDFFONT_H
#define PDFFONT_H

#include "pdfglobal.h"
#include "pdfencoding.h"
#include "pdfobject.h"

#include <QRawFont>
#include <QSharedPointer>

namespace pdf
{
class PDFDocument;

enum class TextRenderingMode
{
    Fill = 0,
    Stroke = 1,
    FillStroke = 2,
    Invisible = 3,
    FillClip = 4,
    StrokeClip = 5,
    FillStrokeClip = 6,
    Clip = 7
};

constexpr bool isTextRenderingModeFilled(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Fill:
        case TextRenderingMode::FillClip:
        case TextRenderingMode::FillStroke:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

constexpr bool isTextRenderingModeStroked(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Stroke:
        case TextRenderingMode::FillStroke:
        case TextRenderingMode::StrokeClip:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

constexpr bool isTextRenderingModeClipped(TextRenderingMode mode)
{
    switch (mode)
    {
        case TextRenderingMode::Clip:
        case TextRenderingMode::FillClip:
        case TextRenderingMode::StrokeClip:
        case TextRenderingMode::FillStrokeClip:
            return true;

        default:
            return false;
    }
}

enum class FontType
{
    Invalid,
    Type1,
    TrueType
};

/// Standard Type1 fonts
enum class StandardFontType
{
    Invalid,
    TimesRoman,
    TimesRomanBold,
    TimesRomanItalics,
    TimesRomanBoldItalics,
    Helvetica,
    HelveticaBold,
    HelveticaOblique,
    HelveticaBoldOblique,
    Courier,
    CourierBold,
    CourierOblique,
    CourierBoldOblique,
    Symbol,
    ZapfDingbats
};

/// Returns builtin encoding for the standard font
static constexpr PDFEncoding::Encoding getEncodingForStandardFont(StandardFontType standardFont)
{
    switch (standardFont)
    {
        case StandardFontType::Symbol:
            return PDFEncoding::Encoding::Symbol;

        case StandardFontType::ZapfDingbats:
            return PDFEncoding::Encoding::ZapfDingbats;

        default:
            return PDFEncoding::Encoding::Standard;
    }
}

class PDFFont;

using PDFFontPointer = QSharedPointer<PDFFont>;

/// Base  class representing font in the PDF file
class PDFFont
{
public:
    explicit PDFFont();
    virtual ~PDFFont() = default;

    /// Returns the font type
    virtual FontType getFontType() const = 0;

    /// Realizes the font (physical materialization of the font using pixel size,
    /// if font can't be realized, then empty QRawFont is returned).
    /// \param fontSize Size of the font
    virtual QRawFont getRealizedFont(PDFReal fontSize) const = 0;

    /// Returns text using the font encoding
    /// \param byteArray Byte array with encoded string
    virtual QString getTextUsingEncoding(const QByteArray& byteArray) const = 0;

    static PDFFontPointer createFont(const PDFObject& object, const PDFDocument* document);
};

/// Simple font, see PDF reference 1.7, chapter 5.5. Simple fonts have encoding table,
/// which maps single-byte character to the glyph in the font.
class PDFSimpleFont : public PDFFont
{
public:
    explicit PDFSimpleFont(QByteArray name,
                           QByteArray baseFont,
                           PDFInteger firstChar,
                           PDFInteger lastChar,
                           std::vector<PDFInteger> widths,
                           PDFEncoding::Encoding encodingType,
                           encoding::EncodingTable encoding);
    virtual ~PDFSimpleFont() override = default;

    virtual QRawFont getRealizedFont(PDFReal fontSize) const override;
    virtual QString getTextUsingEncoding(const QByteArray& byteArray) const override;

protected:
    QByteArray m_name;
    QByteArray m_baseFont;
    PDFInteger m_firstChar;
    PDFInteger m_lastChar;
    std::vector<PDFInteger> m_widths;
    PDFEncoding::Encoding m_encodingType;
    encoding::EncodingTable m_encoding;
};

class PDFType1Font : public PDFSimpleFont
{
public:
    explicit PDFType1Font(QByteArray name,
                          QByteArray baseFont,
                          PDFInteger firstChar,
                          PDFInteger lastChar,
                          std::vector<PDFInteger> widths,
                          PDFEncoding::Encoding encodingType,
                          encoding::EncodingTable encoding,
                          StandardFontType standardFontType);
    virtual ~PDFType1Font() override = default;

    virtual FontType getFontType() const override;

    /// Returns the assigned standard font (or invalid, if font is not standard)
    StandardFontType getStandardFontType() const { return m_standardFontType; }

private:
    StandardFontType m_standardFontType; ///< Type of the standard font (or invalid, if it is not a standard font)
};

class PDFTrueTypeFont : public PDFSimpleFont
{
public:
    using PDFSimpleFont::PDFSimpleFont;

    virtual FontType getFontType() const override;
};

}   // namespace pdf

#endif // PDFFONT_H
