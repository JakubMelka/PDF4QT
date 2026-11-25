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

#include "pdfcontentstreamtext.h"
#include "pdfdocumentwriter.h"
#include "pdfparser.h"

#include <cmath>

#include "pdfdbgheap.h"

namespace pdf
{

/// The parts of the texts of a content stream, which work on single bytes (PDFContentStreamText)
class PDFContentStreamTextHelper
{
public:
    PDFContentStreamTextHelper() = delete;

    static constexpr char HEXADECIMAL_DIGITS[] = "0123456789ABCDEF";

    /// Appends the two upper case hexadecimal digits of a byte
    static void appendHexadecimalByte(QByteArray* text, char byte)
    {
        const unsigned int value = static_cast<unsigned char>(byte);
        text->append(HEXADECIMAL_DIGITS[value >> 4]);
        text->append(HEXADECIMAL_DIGITS[value & 0x0F]);
    }

    /// Appends the three octal digits of a byte
    static void appendOctalByte(QByteArray* text, char byte)
    {
        const unsigned int value = static_cast<unsigned char>(byte);
        text->append(static_cast<char>('0' + ((value >> 6) & 0x07)));
        text->append(static_cast<char>('0' + ((value >> 3) & 0x07)));
        text->append(static_cast<char>('0' + (value & 0x07)));
    }

    /// The byte is printable ASCII (0x20 ... 0x7E)
    static constexpr bool isPrintable(char byte)
    {
        return byte >= 0x20 && byte <= 0x7E;
    }
};

/// Formats the real number, so it can be written into the content stream.
/// Numbers in the content stream must not use the exponential notation
/// (see PDF 32000-1, chapter 7.3.3), but the default QTextStream formatting
/// produces it for very small or very large values (for example '1e-05').
/// Such a number is rejected by the parser and the whole operator, in which
/// it appears, is skipped. At most 9 significant digits are written: they keep any
/// IEEE single exactly, which is what a reader may read (PDF Reference 1.7, Table C.1).
/// A shorter value keeps its shortest text, an integral part longer than 9 digits
/// keeps its digits. An operand needs no decimal point, a type of an operand does
/// not matter and the content stream stays shorter.
QByteArray PDFContentStreamText::formatNumber(PDFReal value)
{
    QByteArray text = PDFDocumentWriter::getRealNumberText(value);
    if (text.endsWith(".0"))
    {
        // No fraction: the digits of an integral part are kept, whatever their count is.
        // An operand of a negative zero is the same value as zero, its shortest text has no sign.
        text.chop(2);
        return text == "-0" ? QByteArrayLiteral("0") : text;
    }

    int significantDigits = 0;
    for (const char character : text)
    {
        if (character >= '0' && character <= '9' && (significantDigits > 0 || character != '0'))
        {
            ++significantDigits;
        }
    }

    if (significantDigits <= MAXIMUM_SIGNIFICANT_DIGITS)
    {
        return text;
    }

    // The exponent is taken after the rounding: 9.9999999996 has the exponent 1 then
    const QByteArray scientific = QByteArray::number(value, 'e', MAXIMUM_SIGNIFICANT_DIGITS - 1);
    const int exponent = scientific.mid(scientific.indexOf('e') + 1).toInt();

    text = QByteArray::number(value, 'f', qMax(MAXIMUM_SIGNIFICANT_DIGITS - 1 - exponent, 0));
    if (text.contains('.'))
    {
        while (text.endsWith('0'))
        {
            text.chop(1);
        }

        if (text.endsWith('.'))
        {
            text.chop(1);
        }
    }

    return text;
}

bool PDFContentStreamText::isNumberWritable(PDFReal value)
{
    return std::isfinite(value);
}

QByteArray PDFContentStreamText::formatFixedNumber(PDFReal value, int decimals)
{
    if (!std::isfinite(value))
    {
        value = 0.0;
    }

    QByteArray text = QByteArray::number(value, 'f', qMax(decimals, 0));
    if (text.contains('.'))
    {
        while (text.endsWith('0'))
        {
            text.chop(1);
        }

        if (text.endsWith('.'))
        {
            text.chop(1);
        }
    }

    // A negative value, which is rounded to zero, has the sign in the text of Qt
    if (text == "-0")
    {
        text = "0";
    }

    return text;
}

QByteArray PDFContentStreamText::formatName(const QByteArray& name)
{
    QByteArray text;
    text.reserve(name.size() + 1);
    text.append('/');

    for (const char character : name)
    {
        // The white space is outside of '!' ... '~', the delimiters are inside of it
        if (character >= '!' && character <= '~' && character != '#' && PDFLexicalAnalyzer::isRegular(character))
        {
            text.append(character);
        }
        else
        {
            text.append('#');
            PDFContentStreamTextHelper::appendHexadecimalByte(&text, character);
        }
    }

    return text;
}

QByteArray PDFContentStreamText::formatString(const QByteArray& string)
{
    for (const char character : string)
    {
        if (!PDFContentStreamTextHelper::isPrintable(character))
        {
            return formatHexString(string);
        }
    }

    return formatLiteralString(string);
}

QByteArray PDFContentStreamText::formatLiteralString(const QByteArray& string)
{
    QByteArray text;
    text.reserve(string.size() + 2);
    text.append('(');

    for (const char character : string)
    {
        switch (character)
        {
            case '\\':
                text.append("\\\\");
                break;

            case '(':
                text.append("\\(");
                break;

            case ')':
                text.append("\\)");
                break;

            case '\n':
                text.append("\\n");
                break;

            case '\r':
                text.append("\\r");
                break;

            case '\t':
                text.append("\\t");
                break;

            case '\b':
                text.append("\\b");
                break;

            case '\f':
                text.append("\\f");
                break;

            default:
            {
                if (PDFContentStreamTextHelper::isPrintable(character))
                {
                    text.append(character);
                }
                else
                {
                    text.append('\\');
                    PDFContentStreamTextHelper::appendOctalByte(&text, character);
                }
                break;
            }
        }
    }

    text.append(')');
    return text;
}

QByteArray PDFContentStreamText::formatHexString(const QByteArray& string)
{
    QByteArray text;
    text.reserve(2 * string.size() + 2);
    text.append('<');

    for (const char character : string)
    {
        PDFContentStreamTextHelper::appendHexadecimalByte(&text, character);
    }

    text.append('>');
    return text;
}

bool PDFContentStreamText::isSeparatorNeeded(const QByteArray& before, const QByteArray& after)
{
    return !before.isEmpty() && !after.isEmpty() && PDFLexicalAnalyzer::isRegular(before.back()) && PDFLexicalAnalyzer::isRegular(after.front());
}

}   // namespace pdf
