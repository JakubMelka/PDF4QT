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

#include "pdftooldiff.h"

#include "pdfdiff.h"
#include "pdfdocumentreader.h"

namespace pdftool
{

static PDFToolDiff s_toolDiffApplication;

QString PDFToolDiff::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "diff";

        case Name:
            return PDFToolTranslationContext::tr("Compare documents");

        case Description:
            return PDFToolTranslationContext::tr("Compare contents of two documents.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolDiff::execute(const PDFToolOptions& options)
{
    if (options.diffFiles.size() != 2)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Exactly two documents must be specified."), options.outputCodec);
        return ErrorInvalidArguments;
    }

    pdf::PDFDocumentReader reader(nullptr, [](bool* ok) { *ok = false; return QString(); }, options.permissiveReading, false);

    pdf::PDFDocument leftDocument = reader.readFromFile(options.diffFiles.front());
    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot open document '%1'.").arg(options.diffFiles.front()), options.outputCodec);
        return ErrorDocumentReading;
    }

    pdf::PDFDocument rightDocument = reader.readFromFile(options.diffFiles.back());
    if (reader.getReadingResult() != pdf::PDFDocumentReader::Result::OK)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Cannot open document '%1'.").arg(options.diffFiles.back()), options.outputCodec);
        return ErrorDocumentReading;
    }

    pdf::PDFClosedIntervalSet leftPages;
    leftPages.addInterval(0, leftDocument.getCatalog()->getPageCount() - 1);

    pdf::PDFClosedIntervalSet rightPages;
    rightPages.addInterval(0, rightDocument.getCatalog()->getPageCount() - 1);

    pdf::PDFDiff diff(nullptr);
    diff.setOption(pdf::PDFDiff::Asynchronous, false);
    diff.setLeftDocument(&leftDocument);
    diff.setRightDocument(&rightDocument);
    diff.setPagesForLeftDocument(std::move(leftPages));
    diff.setPagesForRightDocument(std::move(rightPages));
    diff.start();

    QLocale locale;

    const pdf::PDFDiffResult& result = diff.getResult();
    if (result.getResult())
    {
        PDFOutputFormatter formatter(options.outputStyle);
        formatter.beginDocument("diff", PDFToolTranslationContext::tr("Difference Report").arg(options.document));
        formatter.endl();

        formatter.beginTable("differences", PDFToolTranslationContext::tr("Differences"));

        formatter.beginTableHeaderRow("header");
        formatter.writeTableHeaderColumn("no", PDFToolTranslationContext::tr("No."), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("type", PDFToolTranslationContext::tr("Type"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("left-page-number", PDFToolTranslationContext::tr("Left Page"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("right-page-number", PDFToolTranslationContext::tr("Right Page"), Qt::AlignLeft);
        formatter.writeTableHeaderColumn("description", PDFToolTranslationContext::tr("Description"), Qt::AlignLeft);
        formatter.endTableHeaderRow();

        const size_t size = result.getDifferencesCount();
        for (size_t i = 0; i < size; ++i)
        {
            pdf::PDFInteger leftPageIndex = result.getLeftPage(i);
            pdf::PDFInteger rightPageIndex = result.getRightPage(i);

            QString leftPageDescription = leftPageIndex != -1 ? locale.toString(leftPageIndex + 1) : QString();
            QString rightPageDescription = rightPageIndex != -1 ? locale.toString(rightPageIndex + 1) : QString();

            formatter.beginTableRow("difference", int(i));
            formatter.writeTableColumn("no", locale.toString(i + 1), Qt::AlignRight);
            formatter.writeTableColumn("type", result.getTypeDescription(i), Qt::AlignLeft);
            formatter.writeTableColumn("left-page-number", leftPageDescription, Qt::AlignRight);
            formatter.writeTableColumn("right-page-number", rightPageDescription, Qt::AlignRight);
            formatter.writeTableColumn("description", result.getMessage(i), Qt::AlignLeft);
            formatter.endTableRow();
        }

        formatter.endTable();
        formatter.endDocument();

        if (options.outputStyle == PDFOutputFormatter::Style::Xml)
        {
            QString xml;
            result.saveToXML(&xml);
            PDFConsole::writeText(xml, options.outputCodec);
        }
        else
        {
            PDFConsole::writeText(formatter.getString(), options.outputCodec);
        }
    }
    else
    {
        PDFConsole::writeError(result.getResult().getErrorMessage(), options.outputCodec);
        return ErrorUnknown;
    }

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolDiff::getOptionsFlags() const
{
    return ConsoleFormat | Diff;
}

}   // namespace pdftool
