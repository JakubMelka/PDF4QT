//    Copyright (C) 2020 Jakub Melka
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
//    along with PDFForQt. If not, see <https://www.gnu.org/licenses/>.

#ifndef PDFJAVASCRIPTSCANNER_H
#define PDFJAVASCRIPTSCANNER_H

#include "pdfdocument.h"

namespace pdf
{

struct PDFJavaScriptEntry
{
    enum class Type
    {
        Invalid,
        Document,
        Named,
        Form,
        Page,
        Annotation
    };

    explicit PDFJavaScriptEntry() = default;
    explicit PDFJavaScriptEntry(Type type, PDFInteger pageIndex, QString javaScript) :
        type(type), pageIndex(pageIndex), javaScript(javaScript)
    {

    }

    Type type = Type::Invalid;
    PDFInteger pageIndex = -1;
    QString javaScript;
};

/// Scans document for all javascript presence (in actions). Several option
/// can be set, for example, scan only document actions, or stop scanning,
/// when first javascript is found.
class PDFFORQTLIBSHARED_EXPORT PDFJavaScriptScanner
{
public:
    explicit PDFJavaScriptScanner(const PDFDocument* document);

    enum Option
    {
        AllPages        = 0x0001,   ///< Scan all pages
        FindFirstOnly   = 0x0002,   ///< Return only first javascript found
        ScanDocument    = 0x0004,   ///< Scan document related actions for javascript
        ScanNamed       = 0x0008,   ///< Scan named javascript in catalog
        ScanForm        = 0x0010,   ///< Scan javascript in form actions
        ScanPage        = 0x0020,   ///< Scan javascript in page annotations
    };
    Q_DECLARE_FLAGS(Options, Option)

    using Entries = std::vector<PDFJavaScriptEntry>;

    /// Scans document for javascript actions using flags
    Entries scan(const std::vector<PDFInteger>& pages, Options options)  const;

    /// Returns true, if document has any java script action. Calling
    /// this function can be slow.
    bool hasJavaScript() const;

private:
    const PDFDocument* m_document;
};

}   // namespace pdf

Q_DECLARE_OPERATORS_FOR_FLAGS(pdf::PDFJavaScriptScanner::Options)

#endif // PDFJAVASCRIPTSCANNER_H
