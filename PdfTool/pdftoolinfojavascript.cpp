// MIT License
//
// Copyright (c) 2018-2025 Jakub Melka and Contributors
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

#include "pdftoolinfojavascript.h"
#include "pdfjavascriptscanner.h"

namespace pdftool
{

static PDFToolInfoJavaScriptApplication s_infoJavaScriptApplication;

QString PDFToolInfoJavaScriptApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-javascript";

        case Name:
            return PDFToolTranslationContext::tr("Info (JavaScript code)");

        case Description:
            return PDFToolTranslationContext::tr("Retrieve informations about JavaScript usage in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoJavaScriptApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pages = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFJavaScriptScanner scanner(&document);
    pdf::PDFJavaScriptScanner::Entries javascripts = scanner.scan(pages, pdf::PDFJavaScriptScanner::ScanMask | pdf::PDFJavaScriptScanner::Optimize);

    QLocale locale;

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("info-javascripts", PDFToolTranslationContext::tr("JavaScript used in document %1").arg(options.document));
    formatter.endl();

    formatter.beginTable("overview", PDFToolTranslationContext::tr("JavaScript Usage Overview"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("context", PDFToolTranslationContext::tr("Context"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("page-number", PDFToolTranslationContext::tr("Page Number"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("code-size", PDFToolTranslationContext::tr("Code Size"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("code-snippet", PDFToolTranslationContext::tr("Code Snippet"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    int ref = 1;
    for (const pdf::PDFJavaScriptEntry& entry : javascripts)
    {
        formatter.beginTableRow("javascript", ref);

        QString contextText;
        switch (entry.type)
        {
            case pdf::PDFJavaScriptEntry::Type::Document:
                contextText = PDFToolTranslationContext::tr("Document");
                break;

            case pdf::PDFJavaScriptEntry::Type::Named:
                contextText = PDFToolTranslationContext::tr("Named");
                break;

            case pdf::PDFJavaScriptEntry::Type::Form:
                contextText = PDFToolTranslationContext::tr("Form");
                break;

            case pdf::PDFJavaScriptEntry::Type::Page:
                contextText = PDFToolTranslationContext::tr("Page");
                break;

            case pdf::PDFJavaScriptEntry::Type::Annotation:
                contextText = PDFToolTranslationContext::tr("Annotation");
                break;

            default:
                Q_ASSERT(false);
                break;
        }

        formatter.writeTableColumn("no", locale.toString(ref), Qt::AlignRight);
        formatter.writeTableColumn("context", contextText);
        formatter.writeTableColumn("page-number", (entry.pageIndex != -1) ? locale.toString(entry.pageIndex + 1) : QString(), Qt::AlignRight);
        formatter.writeTableColumn("code-size", locale.toString(entry.javaScript.size()), Qt::AlignRight);
        formatter.writeTableColumn("code-snippet", entry.javaScript.left(64));

        formatter.endTableRow();
        ++ref;
    }

    formatter.endTable();

    formatter.endl();

    formatter.beginHeader("details", PDFToolTranslationContext::tr("Details"));

    ref = 1;
    for (const pdf::PDFJavaScriptEntry& entry : javascripts)
    {
        formatter.endl();
        formatter.beginHeader("javascript", PDFToolTranslationContext::tr("JavaScript #%1").arg(ref), ref);
        formatter.writeText("code", entry.javaScript);
        formatter.endHeader();
        ++ref;
    }

    formatter.endHeader();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoJavaScriptApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector;
}

}   // namespace pdftool
