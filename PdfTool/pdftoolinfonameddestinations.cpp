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

#include "pdftoolinfonameddestinations.h"
#include "pdfdocumentreport.h"

namespace pdftool
{

static PDFToolInfoNamedDestinationsApplication s_infoNamedDestinationsApplication;

QString PDFToolInfoNamedDestinationsApplication::getStandardString(StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "info-dests";

        case Name:
            return PDFToolTranslationContext::tr("Info (Named Destinations)");

        case Description:
            return PDFToolTranslationContext::tr("Retrieve informations about named destinations in a document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInfoNamedDestinationsApplication::execute(const PDFToolOptions& options)
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

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("info-named-destinations", PDFToolTranslationContext::tr("Named destinations used in document %1").arg(options.document));
    formatter.endl();

    formatter.beginTable("named-dests", PDFToolTranslationContext::tr("Named Destinations"));

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("page-number", PDFToolTranslationContext::tr("Page Number"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("left", PDFToolTranslationContext::tr("Left"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("top", PDFToolTranslationContext::tr("Top"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("right", PDFToolTranslationContext::tr("Right"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("bottom", PDFToolTranslationContext::tr("Bottom"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("zoom", PDFToolTranslationContext::tr("Zoom"), Qt::AlignLeft);
    formatter.writeTableHeaderColumn("name", PDFToolTranslationContext::tr("Name"), Qt::AlignLeft);
    formatter.endTableHeaderRow();

    // The one implementation of the report (R7-5); B189: a destination, whose page is not in the
    // document, is "unresolved" (never page 0 or a value of nothing); the numbers in the C locale (Q7.14)
    const QLocale locale = QLocale::c();
    auto toText = [&locale](const std::optional<pdf::PDFReal>& value)
    {
        return value.has_value() ? locale.toString(value.value()) : QString();
    };

    int ref = 1;
    for (const pdf::PDFDocumentReportDestination& destination : pdf::PDFDocumentReport::getNamedDestinations(&document, pages))
    {
        formatter.beginTableRow("destination", ref);

        formatter.writeTableColumn("no", locale.toString(ref), Qt::AlignRight);
        formatter.writeTableColumn("page-number", destination.pageIndex >= 0 ? locale.toString(destination.pageIndex + 1) : PDFToolTranslationContext::tr("unresolved"), Qt::AlignRight);
        formatter.writeTableColumn("type", destination.type, Qt::AlignRight);
        formatter.writeTableColumn("left", toText(destination.left), Qt::AlignRight);
        formatter.writeTableColumn("top", toText(destination.top), Qt::AlignRight);
        formatter.writeTableColumn("right", toText(destination.right), Qt::AlignRight);
        formatter.writeTableColumn("bottom", toText(destination.bottom), Qt::AlignRight);
        formatter.writeTableColumn("zoom", toText(destination.zoom), Qt::AlignRight);
        formatter.writeTableColumn("name", destination.name);

        formatter.endTableRow();
        ++ref;
    }

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInfoNamedDestinationsApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector;
}

}   // namespace pdftool
