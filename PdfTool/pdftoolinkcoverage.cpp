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

#include "pdftoolinkcoverage.h"
#include "pdfdocumenttool_inkcoverage.h"

namespace pdftool
{

static PDFToolInkCoverageApplication s_toolInkCoverageApplication;

QString PDFToolInkCoverageApplication::getStandardString(PDFToolAbstractApplication::StandardString standardString) const
{
    switch (standardString)
    {
        case Command:
            return "ink-coverage";

        case Name:
            return PDFToolTranslationContext::tr("Ink coverage");

        case Description:
            return PDFToolTranslationContext::tr("Calculate ink coverage of the selected pages, or a whole document.");

        default:
            Q_ASSERT(false);
            break;
    }

    return QString();
}

int PDFToolInkCoverageApplication::execute(const PDFToolOptions& options)
{
    pdf::PDFDocument document;
    QByteArray sourceData;
    if (!readDocument(options, document, &sourceData, false))
    {
        return ErrorDocumentReading;
    }

    QString parseError;
    std::vector<pdf::PDFInteger> pageIndices = options.getPageRange(document.getCatalog()->getPageCount(), parseError, true);

    if (!parseError.isEmpty() || pageIndices.empty())
    {
        PDFConsole::writeError(parseError, options.outputCodec);
        return ErrorInvalidArguments;
    }

    // One implementation with Ink Coverage of the new application (plan of step 7 of phase 2, 4.5):
    // the tool measures the pages as printing in the process color space (B196), the spot inks of
    // every resource (B197), the ratio over the trim box (Q7.6), the total counts each page once
    // (B163); its table has fixed element names, the name of an ink is in the title (B179); the
    // numbers in the C locale (Q7.14, R7-14). The color management of the options --cms*.
    using Tool = pdf::PDFDocumentToolInkCoverage;

    QVariantMap parameters;
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_PAGES), pdf::PDFDocumentToolPageSelection::createRangeText(pageIndices));
    parameters.insert(QString::fromLatin1(Tool::PARAMETER_RESOLUTION), Tool::DEFAULT_RESOLUTION);

    pdf::PDFDocumentToolContext context = pdf::PDFDocumentToolContext::create(pdf::PDFDocumentToolInputDocument::create(pdf::PDFDocumentPointer(new pdf::PDFDocument(std::move(document))), sourceData, options.document, false));
    context.rendering.cmsSettings = options.cmsSettings;

    const Tool tool;
    const pdf::PDFDocumentToolResult result = tool.run(context, parameters, nullptr, nullptr);
    if (!result.isSuccess())
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("The ink coverage cannot be measured. %1").arg(result.message), options.outputCodec);
        return ErrorUnknown;
    }

    // Q7.9 (b): a page with a problem of the rendering is measured with a warning, a page, which
    // cannot be rendered, is left out
    for (const pdf::PDFDocumentToolPageProblem& problem : result.renderingIssues)
    {
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: page %1: %2").arg(problem.pageIndex + 1).arg(problem.message), options.outputCodec);
    }
    for (const QJsonValue& value : result.data.value(QString::fromLatin1(Tool::DATA_UNMEASURED_PAGES)).toArray())
    {
        const QJsonObject unmeasured = value.toObject();
        PDFConsole::writeError(PDFToolTranslationContext::tr("Warning: page %1 is not measured: %2").arg(unmeasured.value(QStringLiteral("page")).toInteger()).arg(unmeasured.value(QStringLiteral("message")).toString()), options.outputCodec);
    }

    pdf::PDFDocumentToolTableFormat format = pdf::PDFDocumentToolTableFormat::Text;
    switch (options.outputStyle)
    {
        case PDFOutputFormatter::Style::Text:
            format = pdf::PDFDocumentToolTableFormat::Text;
            break;
        case PDFOutputFormatter::Style::Xml:
            format = pdf::PDFDocumentToolTableFormat::Xml;
            break;
        case PDFOutputFormatter::Style::Html:
            format = pdf::PDFDocumentToolTableFormat::Html;
            break;
    }

    const pdf::PDFDocumentToolTableWriter::Result output = pdf::PDFDocumentToolTableWriter::write(Tool::createTables(result.data), format, pdf::PDFDocumentToolTableWriter::Options());
    if (!output.isSuccess())
    {
        PDFConsole::writeError(output.errorMessage, options.outputCodec);
        return ErrorUnknown;
    }

    PDFConsole::writeText(QString::fromUtf8(output.data), options.outputCodec);
    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInkCoverageApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ColorManagementSystem;
}

}   // namespace pdftool
