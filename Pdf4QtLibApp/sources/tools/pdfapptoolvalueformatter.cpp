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

#include "pdfapptoolvalueformatter.h"
#include "pdfapplocale.h"

#include <cmath>

namespace pdf
{

PDFAppToolValueFormatter::~PDFAppToolValueFormatter()
{

}

QString PDFAppToolValueFormatter::formatInteger(qint64 value) const
{
    return PDFAppLocale::formatNumber(value);
}

QString PDFAppToolValueFormatter::formatReal(double value, int decimals) const
{
    if (!std::isfinite(value))
    {
        return QString();
    }
    return PDFAppLocale::getUiLocale().toString(value, 'f', qBound(0, decimals, 10));
}

QString PDFAppToolValueFormatter::formatDateTime(const QDateTime& dateTime, PDFDocumentToolDateFormat format) const
{
    if (!dateTime.isValid())
    {
        return QString();
    }

    switch (format)
    {
        case PDFDocumentToolDateFormat::Short:
            return PDFAppLocale::formatDateTime(dateTime);
        case PDFDocumentToolDateFormat::Long:
            return PDFAppLocale::getUiLocale().toString(dateTime, QLocale::LongFormat);
        case PDFDocumentToolDateFormat::Iso:
        case PDFDocumentToolDateFormat::Rfc2822:
            break;
    }

    // ISO 8601 and RFC 2822 are formats of data, never translated
    return PDFDocumentToolValueFormatter::formatDateTime(dateTime, format);
}

PDFDocumentToolTableWriter::Options PDFAppToolValueFormatter::createOptions(PDFDocumentToolDateFormat dateFormat)
{
    static const PDFAppToolValueFormatter formatter;

    PDFDocumentToolTableWriter::Options options;
    options.formatter = &formatter;
    options.dateFormat = dateFormat;
    return options;
}

}   // namespace pdf
