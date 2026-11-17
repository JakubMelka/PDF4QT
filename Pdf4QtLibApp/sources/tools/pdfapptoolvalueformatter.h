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

#ifndef PDFAPPTOOLVALUEFORMATTER_H
#define PDFAPPTOOLVALUEFORMATTER_H

#include "pdfappglobal.h"
#include "pdfdocumenttooltablewriter.h"

namespace pdf
{

/// The numbers and dates of the exports of the analysis tools in the application (plan of step 7 of phase 2,
/// R7-14; O17): the locale of the user interface of PDFAppLocale - "1,234", "61.42", "Oct 23, 2026, 14:05";
/// ISO 8601 and RFC 2822 are never translated (the base class). CSV and XML never use a formatter (they are
/// written in the C locale, PDFDocumentToolTableWriter). The GUI passes it to the static writeOutput of a tool
/// in PDFDocumentToolTableWriter::Options. GUI thread or any: it only reads the locale.
class PDF4QTLIBAPPSHARED_EXPORT PDFAppToolValueFormatter : public PDFDocumentToolValueFormatter
{
public:
    PDFAppToolValueFormatter() = default;
    virtual ~PDFAppToolValueFormatter() override;

    virtual QString formatInteger(qint64 value) const override;
    virtual QString formatReal(double value, int decimals) const override;
    virtual QString formatDateTime(const QDateTime& dateTime, PDFDocumentToolDateFormat format) const override;

    /// The options of a write of the application: this formatter (a static instance) and the date format
    static PDFDocumentToolTableWriter::Options createOptions(PDFDocumentToolDateFormat dateFormat);
};

}   // namespace pdf

#endif // PDFAPPTOOLVALUEFORMATTER_H
