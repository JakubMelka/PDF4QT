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

#include "pdftoolinkcoverage.h"
#include "pdftransparencyrenderer.h"

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

    // We are ready to calculate the document's ink coverage
    pdf::PDFOptionalContentActivity optionalContentActivity(&document, pdf::OCUsage::Print, nullptr);
    pdf::PDFCMSManager cmsManager(nullptr);
    cmsManager.setDocument(&document);
    cmsManager.setSettings(options.cmsSettings);

    pdf::PDFFontCache fontCache(pdf::DEFAULT_FONT_CACHE_LIMIT, pdf::DEFAULT_REALIZED_FONT_CACHE_LIMIT);
    pdf::PDFModifiedDocument md(&document, &optionalContentActivity);
    fontCache.setDocument(md);
    fontCache.setCacheShrinkEnabled(nullptr, false);

    pdf::PDFInkMapper inkMapper(&cmsManager, &document);
    inkMapper.createSpotColors(true);

    pdf::PDFInkCoverageCalculator calculator(&document,
                                             &fontCache,
                                             &cmsManager,
                                             &optionalContentActivity,
                                             &inkMapper,
                                             nullptr,
                                             pdf::PDFTransparencyRendererSettings());
    calculator.perform(QSize(1920, 1920), pageIndices);

    fontCache.setCacheShrinkEnabled(nullptr, true);

    PDFOutputFormatter formatter(options.outputStyle);
    formatter.beginDocument("ink-coverage", PDFToolTranslationContext::tr("Ink Coverage"));
    formatter.endl();

    formatter.beginTable("ink-coverage-by-page", PDFToolTranslationContext::tr("Ink Coverage by Page"));

    QLocale locale;
    std::vector<pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo> headerCoverage;

    for (const pdf::PDFInteger pageIndex : pageIndices)
    {
        const std::vector<pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo>* coverage = calculator.getInkCoverage(pageIndex);

        if (!coverage)
        {
            // Jakub Melka: Something bad happened?
            continue;
        }

        for (const auto& info : *coverage)
        {
            if (!pdf::PDFInkCoverageCalculator::findCoverageInfoByName(headerCoverage, info.name))
            {
                headerCoverage.push_back(info);
            }
        }
    }

    formatter.beginTableHeaderRow("header");
    formatter.writeTableHeaderColumn("page-no", PDFToolTranslationContext::tr("Page No."), Qt::AlignLeft);
    for (const pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo& info : headerCoverage)
    {
        formatter.writeTableHeaderColumn(QString("%1-ratio").arg(QString::fromLatin1(info.name)), PDFToolTranslationContext::tr("%1 Ratio [%]").arg(info.textName), Qt::AlignLeft);
        formatter.writeTableHeaderColumn(QString("%1-area").arg(QString::fromLatin1(info.name)), PDFToolTranslationContext::tr("%1 Covered [mm^2]").arg(info.textName), Qt::AlignLeft);
    }
    formatter.endTableHeaderRow();

    for (const pdf::PDFInteger pageIndex : pageIndices)
    {
        const std::vector<pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo>* coverage = calculator.getInkCoverage(pageIndex);

        if (!coverage)
        {
            // Jakub Melka: Something bad happened?
            continue;
        }

        formatter.beginTableRow("page-coverage", pageIndex + 1);
        formatter.writeTableColumn("page-no", locale.toString(pageIndex + 1), Qt::AlignRight);

        for (const auto& info : headerCoverage)
        {
            const pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo* channelInfo = pdf::PDFInkCoverageCalculator::findCoverageInfoByName(*coverage, info.name);
            pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo* sumChannelInfo = pdf::PDFInkCoverageCalculator::findCoverageInfoByName(headerCoverage, info.name);

            if (channelInfo)
            {
                formatter.writeTableColumn(QString("%1-ratio").arg(QString::fromLatin1(info.name)), locale.toString(channelInfo->ratio * 100.0, 'f', 2), Qt::AlignRight);
                formatter.writeTableColumn(QString("%1-area").arg(QString::fromLatin1(info.name)), locale.toString(channelInfo->coveredArea, 'f', 2), Qt::AlignRight);
                sumChannelInfo->coveredArea += channelInfo->coveredArea;
            }
            else
            {
                formatter.writeTableColumn(QString("%1-ratio").arg(QString::fromLatin1(info.name)), QString(), Qt::AlignRight);
                formatter.writeTableColumn(QString("%1-area").arg(QString::fromLatin1(info.name)), QString(), Qt::AlignRight);
            }
        }

        formatter.endTableRow();
    }

    // Summary
    formatter.beginTableRow("sum-coverage");
    formatter.writeTableColumn("sum", PDFToolTranslationContext::tr("Sum"), Qt::AlignRight);

    for (const auto& info : headerCoverage)
    {
        const pdf::PDFInkCoverageCalculator::InkCoverageChannelInfo* channelInfo = pdf::PDFInkCoverageCalculator::findCoverageInfoByName(headerCoverage, info.name);
        formatter.writeTableColumn(QString("%1-ratio").arg(QString::fromLatin1(info.name)), QString(), Qt::AlignRight);
        formatter.writeTableColumn(QString("%1-area").arg(QString::fromLatin1(info.name)), locale.toString(channelInfo->coveredArea, 'f', 2), Qt::AlignRight);
    }

    formatter.endTableRow();

    formatter.endTable();

    formatter.endDocument();
    PDFConsole::writeText(formatter.getString(), options.outputCodec);

    return ExitSuccess;
}

PDFToolAbstractApplication::Options PDFToolInkCoverageApplication::getOptionsFlags() const
{
    return ConsoleFormat | OpenDocument | PageSelector | ColorManagementSystem;
}

}   // namespace pdftool
