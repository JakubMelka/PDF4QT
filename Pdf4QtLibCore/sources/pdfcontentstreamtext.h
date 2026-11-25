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

#ifndef PDFCONTENTSTREAMTEXT_H
#define PDFCONTENTSTREAMTEXT_H

#include "pdfglobal.h"

#include <QByteArray>

namespace pdf
{

/// The one format of the numbers, names and strings written into a content stream (ISO 32000-2,
/// 7.3.3, 7.3.4, 7.3.5, 7.8.2). The texts are ASCII and do not depend on the locale.
///
/// The class has static functions only and no state: every function works with its arguments
/// only, so it is safe to call from any thread.
class PDF4QTLIBCORESHARED_EXPORT PDFContentStreamText
{
public:
    PDFContentStreamText() = delete;

    static constexpr int MAXIMUM_SIGNIFICANT_DIGITS = 9;

    /// Canonical number of the editor: the shortest text, which reads back as the same single precision value,
    /// at most MAXIMUM_SIGNIFICANT_DIGITS significant digits, no exponent, no ".0", "-0" is "0"
    static QByteArray formatNumber(PDFReal value);

    /// false for NaN and infinity: formatNumber then writes the replacement of PDFDocumentWriter::getRealNumberText
    /// (the old editor keeps doing so); the new writers treat such a value as an error of the node instead
    static bool isNumberWritable(PDFReal value);

    /// At most \p decimals decimal places, no trailing zeros, no decimal point of an integral value, "-0" is "0";
    /// a value, which is not finite, is "0"
    static QByteArray formatFixedNumber(PDFReal value, int decimals);

    /// A name object with its solidus; ISO 32000-2, 7.3.5: #xx for every byte, which is not a regular character,
    /// for '#' and for the regular characters outside of '!' ... '~'
    static QByteArray formatName(const QByteArray& name);

    /// A string object: literal, when every byte is printable ASCII (0x20 ... 0x7E), hexadecimal otherwise
    static QByteArray formatString(const QByteArray& string);

    /// A literal string object; ISO 32000-2, 7.3.4.2: the reverse solidus and the parentheses are escaped,
    /// the line feed, the carriage return, the horizontal tab, the backspace and the form feed are
    /// written as \\n, \\r, \\t, \\b and \\f, every other byte outside of 0x20 ... 0x7E as three octal digits
    static QByteArray formatLiteralString(const QByteArray& string);

    /// A hexadecimal string object; ISO 32000-2, 7.3.4.3: upper case digits, two per byte
    static QByteArray formatHexString(const QByteArray& string);

    /// R-7 of the plan: true, when \p before ends and \p after begins with a regular character (7.2.3: neither
    /// white space nor a delimiter), so the two would be read as one token, if written next to each other
    static bool isSeparatorNeeded(const QByteArray& before, const QByteArray& after);
};

}   // namespace pdf

#endif // PDFCONTENTSTREAMTEXT_H
