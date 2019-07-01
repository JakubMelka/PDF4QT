//    Copyright (C) 2018-2019 Jakub Melka
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

#ifndef PDFENCODING_H
#define PDFENCODING_H

#include <QString>
#include <QDateTime>

#include <array>

namespace pdf
{

namespace encoding
{
using EncodingTable = std::array<QChar, 256>;
}

/// This class can convert byte stream to the QString in unicode encoding.
/// PDF has several encodings, see PDF Reference 1.7, Appendix D.
class PDFEncoding
{
public:
    explicit PDFEncoding() = delete;

    enum class Encoding
    {
        Standard,       ///< Appendix D, Section D.1, StandardEncoding
        MacRoman,       ///< Appendix D, Section D.1, MacRomanEncoding
        WinAnsi,        ///< Appendix D, Section D.1, WinAnsiEncoding
        PDFDoc,         ///< Appendix D, Section D.1/D.2, PDFDocEncoding
        MacExpert,      ///< Appendix D, Section D.3, MacExpertEncoding
        Symbol,         ///< Appendix D, Section D.4, Symbol Set and Encoding
        ZapfDingbats,   ///< Appendix D, Section D.5, Zapf Dingbats Encoding

        // Following encodings are used for internal use only and are not a part of PDF reference
        Custom,
        Invalid
    };

    /// Converts byte array to the unicode string using specified encoding
    /// \param stream Stream (byte array string) to be processed
    /// \param encoding Encoding used to convert to unicode string
    /// \returns Converted unicode string
    static QString convert(const QByteArray& stream, Encoding encoding);

    /// Convert text string to the unicode string, using either PDFDocEncoding,
    /// or UTF-16BE encoding. Please see PDF Reference 1.7, Chapter 3.8.1. If
    /// UTF-16BE encoding is used, then leading bytes should be 0xFE and 0xFF
    /// \param Stream
    /// \returns Converted unicode string
    static QString convertTextString(const QByteArray& stream);

    /// Converts byte array from UTF-16BE encoding to QString with same encoding.
    /// \param Stream
    /// \returns Converted unicode string
    static QString convertFromUnicode(const QByteArray& stream);

    /// Convert stream to date time according to PDF Reference 1.7, Chapter 3.8.1.
    /// If date cannot be converted (string is invalid), then invalid QDateTime
    /// is returned.
    /// \param stream Stream, from which date/time is read
    static QDateTime convertToDateTime(const QByteArray& stream);

    /// Returns conversion table for particular encoding
    /// \param encoding Encoding
    static const encoding::EncodingTable* getTableForEncoding(Encoding encoding);

private:
    /// Returns true, if byte array has UTF-16BE unicode marking bytes at the
    /// stream start. If they are present, then byte stream is probably encoded
    /// as unicode.
    /// \param stream Stream to be tested
    static bool hasUnicodeLeadMarkings(const QByteArray& stream);
};

}   // namespace pdf

#endif // PDFENCODING_H
